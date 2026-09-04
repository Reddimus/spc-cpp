#include "spc/api.hpp"
#include "spc/pagination.hpp"

#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace spc;

class RecordingTransport final : public HttpTransport {
public:
	mutable std::vector<std::string> requests;
	std::vector<HttpResponse> responses;

	[[nodiscard]] Result<HttpResponse> get(std::string_view path) const override {
		requests.emplace_back(path);
		if (requests.size() > responses.size()) {
			return std::unexpected(Error::network("no response queued"));
		}
		return responses[requests.size() - 1];
	}
};

HttpResponse empty_feature_collection() {
	return {200, R"({"type":"FeatureCollection","features":[],"exceededTransferLimit":false})", {}};
}

/// A FeatureCollection of `count` placeholder features that still reports
/// truncation — the shape ArcGIS returns when a layer's own maxRecordCount is
/// below the requested resultRecordCount.
std::string truncated_page(std::int32_t count) {
	std::string body = R"({"type":"FeatureCollection","features":[)";
	for (std::int32_t i = 0; i < count; ++i) {
		if (i > 0) {
			body += ",";
		}
		body += R"({"type":"Feature","properties":{},"geometry":null})";
	}
	body += R"(],"exceededTransferLimit":true})";
	return body;
}

/// Answers every request with the same truncated page, forever.
class AlwaysTruncatingTransport final : public HttpTransport {
public:
	mutable std::int32_t calls = 0;

	[[nodiscard]] Result<HttpResponse> get(std::string_view /*path*/) const override {
		++calls;
		return HttpResponse{200, truncated_page(1), {}};
	}
};

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
Result<WatchPayload> call_deprecated_active_watches(ArcGISClient& client) {
	return client.query_active_watches();
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

TEST(StaticFeedClientRouting, UsesPublishedProbabilisticFilenames) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses.assign(3, empty_feature_collection());
	StaticFeedClient client{transport};

	ASSERT_TRUE(client.day_probabilistic(1, "tornado"));
	ASSERT_TRUE(client.day_probabilistic(2, "hail"));
	ASSERT_TRUE(client.day_probabilistic(3, "severe"));

	ASSERT_EQ(transport->requests.size(), 3u);
	EXPECT_EQ(transport->requests[0],
			  "https://www.spc.noaa.gov/products/outlook/day1otlk_torn.nolyr.geojson");
	EXPECT_EQ(transport->requests[1],
			  "https://www.spc.noaa.gov/products/outlook/day2otlk_hail.nolyr.geojson");
	EXPECT_EQ(transport->requests[2],
			  "https://www.spc.noaa.gov/products/outlook/day3otlk_prob.nolyr.geojson");
}

TEST(StaticFeedClientRouting, RejectsUnsupportedProductsBeforeNetworkAccess) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	StaticFeedClient client{transport};

	const Result<CategoricalOutlookPayload> categorical = client.day_categorical(4);
	const Result<ProbOutlookPayload> probabilistic = client.day_probabilistic(2, "any");
	const Result<Day48OutlookPayload> extended = client.day4_8(3);

	ASSERT_FALSE(categorical);
	ASSERT_FALSE(probabilistic);
	ASSERT_FALSE(extended);
	EXPECT_EQ(categorical.error().code, ErrorCode::InvalidRequest);
	EXPECT_EQ(probabilistic.error().code, ErrorCode::InvalidRequest);
	EXPECT_EQ(extended.error().code, ErrorCode::InvalidRequest);
	EXPECT_TRUE(transport->requests.empty());
}

TEST(ArcGISClientRouting, UsesEveryPublishedConvectiveLayerType) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses.assign(4, empty_feature_collection());
	ArcGISClient client{transport};

	ASSERT_TRUE(client.query_categorical(3));
	ASSERT_TRUE(client.query_probabilistic(2, "tornado"));
	ASSERT_TRUE(client.query_conditional_intensity(3, "severe"));
	ASSERT_TRUE(client.query_day4_8(8));

	ASSERT_EQ(transport->requests.size(), 4u);
	EXPECT_NE(transport->requests[0].find("/17/query?"), std::string::npos);
	EXPECT_NE(transport->requests[1].find("/11/query?"), std::string::npos);
	EXPECT_NE(transport->requests[2].find("/18/query?"), std::string::npos);
	EXPECT_NE(transport->requests[3].find("/25/query?"), std::string::npos);
}

TEST(ArcGISClientRouting, CoversThePublishedOutlookLayerTable) {
	struct Case {
		std::int32_t day;
		std::string hazard;
		std::int32_t layer;
	};
	const std::vector<Case> probability_cases = {
		{1, "tornado", 3}, {1, "hail", 5},	{1, "wind", 7},	   {2, "tornado", 11},
		{2, "hail", 13},   {2, "wind", 15}, {3, "severe", 19},
	};
	const std::vector<Case> conditional_cases = {
		{1, "tornado", 2}, {1, "hail", 4},	{1, "wind", 6},	   {2, "tornado", 10},
		{2, "hail", 12},   {2, "wind", 14}, {3, "severe", 18},
	};
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses.assign(22, empty_feature_collection());
	ArcGISClient client{transport};

	for (std::int32_t day = 1; day <= 3; ++day) {
		ASSERT_TRUE(client.query_categorical(day));
	}
	for (const Case& test_case : probability_cases) {
		ASSERT_TRUE(client.query_probabilistic(test_case.day, test_case.hazard));
	}
	for (const Case& test_case : conditional_cases) {
		ASSERT_TRUE(client.query_conditional_intensity(test_case.day, test_case.hazard));
	}
	for (std::int32_t day = 4; day <= 8; ++day) {
		ASSERT_TRUE(client.query_day4_8(day));
	}

	const std::vector<std::int32_t> expected_layers = {
		1, 9, 17, 3, 5, 7, 11, 13, 15, 19, 2, 4, 6, 10, 12, 14, 18, 21, 22, 23, 24, 25,
	};
	ASSERT_EQ(transport->requests.size(), expected_layers.size());
	for (std::size_t index = 0; index < expected_layers.size(); ++index) {
		EXPECT_NE(transport->requests[index].find("/" + std::to_string(expected_layers[index]) +
												  "/query?"),
				  std::string::npos);
	}
}

TEST(ArcGISClientRouting, CombinesBothPublishedFireWeatherLayers) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses.assign(2, empty_feature_collection());
	ArcGISClient client{transport};

	ASSERT_TRUE(client.query_fire_weather(3));

	ASSERT_EQ(transport->requests.size(), 2u);
	EXPECT_NE(transport->requests[0].find("/7/query?"), std::string::npos);
	EXPECT_NE(transport->requests[1].find("/8/query?"), std::string::npos);
	EXPECT_NE(transport->requests[0].find("outSR=4326"), std::string::npos);
	EXPECT_NE(transport->requests[1].find("outSR=4326"), std::string::npos);
}

TEST(ArcGISClientRouting, PreservesLabelsFromBothDayOneFireProducts) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	const std::string body = R"({"features":[
		{"attributes":{"dn":5},"geometry":{"rings":[[[0,1],[1,1],[1,0],[0,0],[0,1]]]}}
	],"exceededTransferLimit":false})";
	transport->responses = {{200, body, {}}, {200, body, {}}};
	ArcGISClient client{transport};

	const Result<FireWeatherPayload> result = client.query_fire_weather(1);

	ASSERT_TRUE(result);
	ASSERT_EQ(result->features.size(), 2u);
	EXPECT_EQ(result->features[0].label, "ELEV");
	EXPECT_EQ(result->features[0].layer, FireWeatherLayer::Outlook);
	EXPECT_EQ(result->features[1].label, "IDRT");
	EXPECT_EQ(result->features[1].layer, FireWeatherLayer::DryThunderstorm);
}

TEST(ArcGISClientRouting, CoversEveryPublishedFireWeatherFeatureLayer) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses.assign(16, empty_feature_collection());
	ArcGISClient client{transport};

	for (std::int32_t day = 1; day <= 8; ++day) {
		ASSERT_TRUE(client.query_fire_weather(day));
	}

	const std::vector<std::int32_t> expected_layers = {
		1, 2, 4, 5, 7, 8, 10, 11, 13, 14, 16, 17, 19, 20, 22, 23,
	};
	ASSERT_EQ(transport->requests.size(), expected_layers.size());
	for (std::size_t index = 0; index < expected_layers.size(); ++index) {
		EXPECT_NE(transport->requests[index].find("/" + std::to_string(expected_layers[index]) +
												  "/query?"),
				  std::string::npos);
	}
}

TEST(ArcGISClientRouting, RejectsUnsupportedProductsBeforeNetworkAccess) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	ArcGISClient client{transport};

	const Result<CategoricalOutlookPayload> categorical = client.query_categorical(4);
	const Result<ProbOutlookPayload> probabilistic = client.query_probabilistic(3, "hail");
	const Result<ConditionalIntensityPayload> conditional =
		client.query_conditional_intensity(2, "severe");
	const Result<FireWeatherPayload> fire = client.query_fire_weather(9);

	EXPECT_FALSE(categorical);
	EXPECT_FALSE(probabilistic);
	EXPECT_FALSE(conditional);
	EXPECT_FALSE(fire);
	EXPECT_TRUE(transport->requests.empty());
}

TEST(ArcGISClientPaging, EncodesParametersAndFetchesEveryPage) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses = {
		{200, truncated_page(2000), {}},
		{200, R"({"features":[],"exceededTransferLimit":false})", {}},
	};
	ArcGISClient client{transport};
	QueryParams params;
	params.where = "LABEL = 'SLGT'";
	params.out_fields = "LABEL,valid";
	params.geometry = R"({"xmin":-105,"ymin":39})";
	params.geometry_type = "esriGeometryEnvelope";

	const Result<std::vector<std::string>> result =
		client.query_layer(ArcGISService::Outlooks, 1, params);

	ASSERT_TRUE(result);
	ASSERT_EQ(result->size(), 2u);
	ASSERT_EQ(transport->requests.size(), 2u);
	EXPECT_NE(transport->requests[0].find("where=LABEL%20%3D%20%27SLGT%27"), std::string::npos);
	EXPECT_NE(transport->requests[0].find("outFields=LABEL%2Cvalid"), std::string::npos);
	EXPECT_NE(transport->requests[0].find("geometry=%7B%22xmin%22%3A-105%2C%22ymin%22%3A39%7D"),
			  std::string::npos);
	EXPECT_NE(transport->requests[0].find("resultOffset=0"), std::string::npos);
	EXPECT_NE(transport->requests[1].find("resultOffset=2000"), std::string::npos);
}

TEST(ArcGISClientPaging, AdvancesTheOffsetByTheRecordsTheServerActuallyReturned) {
	// ArcGIS clamps resultRecordCount to the layer's own maxRecordCount, so a
	// truncated page can be shorter than the 2000 requested. Advancing by the
	// request size would skip the records in between.
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses = {
		{200, truncated_page(3), {}},
		{200, truncated_page(2), {}},
		{200, R"({"features":[],"exceededTransferLimit":false})", {}},
	};
	ArcGISClient client{transport};

	const Result<std::vector<std::string>> result =
		client.query_layer(ArcGISService::Outlooks, 1, {});

	ASSERT_TRUE(result);
	ASSERT_EQ(transport->requests.size(), 3u);
	EXPECT_NE(transport->requests[0].find("resultOffset=0"), std::string::npos);
	EXPECT_NE(transport->requests[1].find("resultOffset=3"), std::string::npos);
	EXPECT_NE(transport->requests[2].find("resultOffset=5"), std::string::npos);
}

TEST(ArcGISClientPaging, FailsWhenATruncatedPageCarriesNoRecords) {
	// Zero records plus exceededTransferLimit:true cannot converge — the
	// offset would never move. Fail instead of looping.
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses = {{200, R"({"features":[],"exceededTransferLimit":true})", {}}};
	ArcGISClient client{transport};

	const Result<std::vector<std::string>> result =
		client.query_layer(ArcGISService::Outlooks, 1, {});

	ASSERT_FALSE(result);
	EXPECT_EQ(result.error().code, ErrorCode::ServerError);
	EXPECT_EQ(transport->requests.size(), 1u);
}

TEST(ArcGISClientPaging, StopsAndFailsWhenTheServerNeverStopsReportingTruncation) {
	std::shared_ptr<AlwaysTruncatingTransport> transport =
		std::make_shared<AlwaysTruncatingTransport>();
	ArcGISClient client{transport};

	const Result<std::vector<std::string>> result =
		client.query_layer(ArcGISService::Outlooks, 1, {});

	ASSERT_FALSE(result);
	EXPECT_EQ(result.error().code, ErrorCode::ServerError);
	EXPECT_EQ(transport->calls, ArcGISPager{}.max_pages());
}

TEST(ArcGISClientPaging, ReturnsLogicalArcGISErrorsReportedWithHttp200) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses = {
		{200,
		 R"({"error":{"code":400,"message":"Invalid or missing input parameters.","details":[]}})",
		 {}},
	};
	ArcGISClient client{transport};

	const Result<std::vector<std::string>> result =
		client.query_layer(ArcGISService::Outlooks, 0, {});

	ASSERT_FALSE(result);
	EXPECT_EQ(result.error().code, ErrorCode::InvalidRequest);
	EXPECT_EQ(result.error().http_status, 400);
	EXPECT_EQ(result.error().message, "Invalid or missing input parameters.");
}

// ===== HTTP 404 trust boundary =====
//
// SPC's static products answer 404 with an HTML page when there is no active
// outlook (verified live: a retired www.spc.noaa.gov outlook path returns
// HTTP 404 text/html). Every other feed's 404 is a genuine fault. The ArcGIS
// MapServer reports a retired service path as HTTP 200 with a logical
// `{"error":{"code":404}}` envelope (verified live against a renamed service).

TEST(Feed404Semantics, StaticFeedReadsSpcHtml404AsNoActiveOutlook) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses = {{404, "<html><title>404 Not Found</title></html>", {}}};
	StaticFeedClient client{transport};

	const Result<CategoricalOutlookPayload> result = client.day_categorical(1);

	ASSERT_FALSE(result);
	EXPECT_EQ(result.error().code, ErrorCode::FeedUnavailable);
	EXPECT_TRUE(result.error().is_feed_unavailable());
	EXPECT_EQ(result.error().http_status, 404);
}

TEST(Feed404Semantics, ArcGisTransport404IsAGenuineNotFound) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses = {{404, "<html>not found</html>", {}}};
	ArcGISClient client{transport};

	const Result<CategoricalOutlookPayload> result = client.query_categorical(1);

	ASSERT_FALSE(result);
	EXPECT_EQ(result.error().code, ErrorCode::NotFound);
	EXPECT_FALSE(result.error().is_feed_unavailable());
}

TEST(Feed404Semantics, ArcGisLogicalNotFoundIsAGenuineNotFound) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	// Live shape for a renamed / retired MapServer path.
	transport->responses = {
		{200,
		 R"({"error":{"code":404,"message":"Service outlooks/SPC_wx_outlks_RETIRED/MapServer not found ","details":[]}})",
		 {}},
	};
	ArcGISClient client{transport};

	const Result<CategoricalOutlookPayload> result = client.query_categorical(1);

	ASSERT_FALSE(result);
	EXPECT_EQ(result.error().code, ErrorCode::NotFound);
	EXPECT_FALSE(result.error().is_feed_unavailable());
	EXPECT_NE(result.error().message.find("not found"), std::string::npos);
}

TEST(Feed404Semantics, ArcGisCodeThatIsNotAnHttpStatusStaysOutOfHttpStatus) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses = {
		{200, R"({"error":{"code":1000,"message":"Unable to complete operation.","details":[]}})", {}},
	};
	ArcGISClient client{transport};

	const Result<std::vector<std::string>> result =
		client.query_layer(ArcGISService::Outlooks, 1, {});

	ASSERT_FALSE(result);
	EXPECT_EQ(result.error().code, ErrorCode::InvalidRequest);
	EXPECT_EQ(result.error().http_status, 0);
	EXPECT_NE(result.error().detail.find("1000"), std::string::npos);
}

TEST(Feed404Semantics, ArchiveTransport404IsAGenuineNotFound) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses = {{404, "<html>gone</html>", {}}};
	ArchiveClient client{transport};

	const Result<WatchPayload> result = client.watches();

	ASSERT_FALSE(result);
	EXPECT_EQ(result.error().code, ErrorCode::NotFound);
	EXPECT_FALSE(result.error().is_feed_unavailable());
}

TEST(ArcGISClientRouting, ActiveWatchesDirectCallersToTheIemClient) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	ArcGISClient client{transport};

	const Result<WatchPayload> result = call_deprecated_active_watches(client);

	ASSERT_FALSE(result);
	EXPECT_EQ(result.error().code, ErrorCode::InvalidRequest);
	EXPECT_NE(result.error().message.find("ArchiveClient"), std::string::npos);
	EXPECT_TRUE(transport->requests.empty());
}

// ===== ArchiveClient (IEM) query construction =====
//
// api.hpp documents start_iso/end_iso as ISO 8601, which permits a "+HH:MM"
// UTC offset. A raw '+' in a query string decodes server-side as a space, so
// an unencoded offset silently queries a different window; a raw '&' in any
// value injects extra parameters.

TEST(ArchiveClientRouting, PercentEncodesTheWatchTimestamp) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses = {{200, R"({"features":[]})", {}}};
	ArchiveClient client{transport};

	ASSERT_TRUE(client.watches("202605191200&x=1"));

	ASSERT_EQ(transport->requests.size(), 1u);
	EXPECT_EQ(transport->requests[0],
			  "https://mesonet.agron.iastate.edu/json/spcwatch.py?ts=202605191200%26x%3D1");
}

TEST(ArchiveClientRouting, PercentEncodesIsoOffsetsAndTheWfoFilter) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses = {{200, R"({"features":[]})", {}}};
	ArchiveClient client{transport};

	ASSERT_TRUE(client.storm_reports("2026-05-19T12:00:00+00:00", "2026-05-20T12:00:00+00:00",
									 "ICT&sts=1900-01-01"));

	ASSERT_EQ(transport->requests.size(), 1u);
	EXPECT_EQ(transport->requests[0],
			  "https://mesonet.agron.iastate.edu/geojson/lsr.geojson"
			  "?sts=2026-05-19T12%3A00%3A00%2B00%3A00&ets=2026-05-20T12%3A00%3A00%2B00%3A00"
			  "&wfo=ICT%26sts%3D1900-01-01");
}

TEST(HttpClientLifecycle, DefaultUserAgentCarriesTheProjectVersion) {
	// The UA string is a literal in an installed header; nothing tied it to
	// project(spc-cpp VERSION ...), so a release bump left every outbound
	// request to NOAA and IEM announcing the previous version.
	const ClientConfig config;

	EXPECT_NE(config.user_agent.find(SPC_PROJECT_VERSION), std::string::npos)
		<< "user_agent \"" << config.user_agent << "\" does not carry version "
		<< SPC_PROJECT_VERSION;
}

TEST(HttpClientLifecycle, RefusesEverySchemeOtherThanHttpAndHttps) {
	// is_absolute_url() only recognises http:// and https://, so a file:// URL
	// was treated as relative, concatenated onto the empty default base_url,
	// and handed to libcurl — which read the local file and returned it as the
	// response body. Nothing in this SDK's scope should reach the filesystem.
	const HttpClient client;

	const Result<HttpResponse> result = client.get("file:///etc/hosts");

	ASSERT_FALSE(result) << "file:// must not be fetched";
	EXPECT_EQ(result.error().code, ErrorCode::NetworkError);
}

TEST(HttpClientLifecycle, ConcurrentClientsShareProcessWideCurlState) {
	std::vector<std::thread> workers;
	workers.reserve(16);
	for (int index = 0; index < 16; ++index) {
		workers.emplace_back([] {
			HttpClient first;
			HttpClient second;
			EXPECT_TRUE(first.config().verify_ssl);
			EXPECT_TRUE(second.config().verify_ssl);
		});
	}
	for (std::thread& worker : workers) {
		worker.join();
	}
}

} // namespace
