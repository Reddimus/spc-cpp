/// @file test_client.cpp
/// @brief Client routing, paging, and error mapping, through a recording
/// transport. No network access.

#include "api/call_budget.hpp"
#include "spc/api.hpp"
#include "spc/pagination.hpp"
#include "spc/retry.hpp"
#include "spc/version.hpp"
#include "support/fixtures.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
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

/// `count` placeholder features plus the truncation flag, as ArcGIS returns
/// when a layer's maxRecordCount is below the requested page size.
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

/// Answers every request with a page padded to about `page_bytes`, truncated
/// or final, and counts what it served. Pages are built on demand so the
/// transport holds none of them.
class PaddedPagesTransport final : public HttpTransport {
public:
	PaddedPagesTransport(std::size_t page_bytes, bool truncated)
		: page_bytes_(page_bytes), truncated_(truncated) {}

	mutable std::int32_t calls = 0;
	mutable std::size_t bytes_served = 0;

	[[nodiscard]] Result<HttpResponse> get(std::string_view /*path*/) const override {
		HttpResponse response{200, {}, {}};
		response.body.reserve(page_bytes_ + 160);
		response.body += R"({"type":"FeatureCollection","features":[)"
						 R"({"type":"Feature","properties":{},"geometry":null}],"pad":")";
		response.body.append(page_bytes_, 'x');
		response.body += std::format(R"(","exceededTransferLimit":{}}})", truncated_);
		++calls;
		bytes_served += response.body.size();
		return response;
	}

private:
	std::size_t page_bytes_;
	bool truncated_;
};

/// Fails every request the way an allocation failure inside a call does.
class OutOfMemoryTransport final : public HttpTransport {
public:
	[[nodiscard]] Result<HttpResponse> get(std::string_view /*path*/) const override {
		throw std::bad_alloc{};
	}
};

/// Serves one truncated page, then throws something that is not a
/// std::exception, as a custom transport might.
class ThrowsAfterAPageTransport final : public HttpTransport {
public:
	mutable std::int32_t calls = 0;

	[[nodiscard]] Result<HttpResponse> get(std::string_view /*path*/) const override {
		if (calls++ == 0) {
			return HttpResponse{200, truncated_page(1), {}};
		}
		throw 42;
	}
};

/// The code `call` failed with, `Ok` if it succeeded, or a test failure if it
/// threw.
template <typename Call>
ErrorCode outcome_of(const Call& call) {
	try {
		const std::invoke_result_t<const Call&> result = call();
		return result ? ErrorCode::Ok : result.error().code;
	} catch (const std::exception& e) {
		ADD_FAILURE() << "client call threw " << e.what();
	} catch (...) {
		ADD_FAILURE() << "client call threw a non-std exception";
	}
	return ErrorCode::Unknown;
}

/// An Esri page of `count` Web Mercator features. Each ring has one diagonal
/// edge that projection subdivides into 3,800 steps, so a feature costs 3,799
/// of the one million added points a payload may hold.
std::string mercator_page(std::size_t count, bool truncated) {
	std::string features;
	for (std::size_t i = 0; i < count; ++i) {
		if (i != 0) {
			features += ',';
		}
		features +=
			R"({"attributes":{"dn":15,"LABEL":"15","name":"Risk"},"geometry":{"rings":[[[-19000000,-19000000],[-19000000,19000000],[19000000,19000000],[-19000000,-19000000]]]}})";
	}
	return std::format(
		R"({{"spatialReference":{{"wkid":3857}},"features":[{}],"exceededTransferLimit":{}}})",
		features, truncated);
}

std::string arcgis_error_body(std::int32_t code) {
	return std::format(R"({{"error":{{"code":{},"message":"Busy","details":[]}}}})", code);
}

TEST(StaticFeedClientRouting, UsesPublishedProbabilisticFilenames) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses.assign(3, empty_feature_collection());
	const StaticFeedClient client{transport};

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

TEST(ArcGISClientRouting, TypedQueriesUseTheirOwnMapServers) {
	// Layer ids alone still match when a query goes to the wrong MapServer.
	const std::string services = "https://mapservices.weather.noaa.gov/vector/rest/services/";
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses.assign(5, empty_feature_collection());
	ArcGISClient client{transport};

	ASSERT_TRUE(client.query_categorical(1));
	ASSERT_TRUE(client.query_fire_weather(1));
	ASSERT_TRUE(client.query_active_md());
	ASSERT_TRUE(client.query_day4_8(4));

	ASSERT_EQ(transport->requests.size(), 5u);
	EXPECT_TRUE(transport->requests[0].starts_with(services + "outlooks/SPC_wx_outlks/MapServer/"));
	EXPECT_TRUE(
		transport->requests[1].starts_with(services + "fire_weather/SPC_firewx/MapServer/"));
	EXPECT_TRUE(
		transport->requests[2].starts_with(services + "fire_weather/SPC_firewx/MapServer/"));
	EXPECT_TRUE(transport->requests[3].starts_with(
		services + "outlooks/spc_mesoscale_discussion/MapServer/0/query?"));
	EXPECT_TRUE(transport->requests[4].starts_with(services + "outlooks/SPC_wx_outlks/MapServer/"));
}

TEST(StaticFeedRouting, EachProductUsesItsPublishedFile) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses.assign(4, {200, R"({"type":"FeatureCollection","features":[]})", {}});
	StaticFeedClient client{transport};

	ASSERT_TRUE(client.day_categorical(2));
	ASSERT_TRUE(client.day_probabilistic(1, "tornado"));
	ASSERT_TRUE(client.day_probabilistic(3, "severe"));
	ASSERT_TRUE(client.day4_8(5));

	const std::string outlook = "https://www.spc.noaa.gov/products/outlook/";
	EXPECT_EQ(transport->requests, (std::vector<std::string>{
									   outlook + "day2otlk_cat.nolyr.geojson",
									   outlook + "day1otlk_torn.nolyr.geojson",
									   outlook + "day3otlk_prob.nolyr.geojson",
									   "https://www.spc.noaa.gov/products/exper/day4-8/"
									   "day5prob.nolyr.geojson",
								   }));
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

TEST(ArcGISClientRouting, FireWeatherIsAllOrNothingAcrossItsTwoMergedLayers) {
	// FireWeatherPayload cannot represent half a product.
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	const std::string body = R"({"features":[
		{"attributes":{"dn":5},"geometry":{"rings":[[[0,1],[1,1],[1,0],[0,0],[0,1]]]}}
	],"exceededTransferLimit":false})";
	// A 400 is not retryable, so the second layer fails on its first request.
	transport->responses = {{200, body, {}}, {400, "bad request", {}}};
	ArcGISClient client{transport};

	const Result<FireWeatherPayload> result = client.query_fire_weather(1);

	ASSERT_FALSE(result);
	EXPECT_EQ(result.error().code, ErrorCode::InvalidRequest);
	EXPECT_EQ(transport->requests.size(), 2u);
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
	EXPECT_EQ((*result)[0], truncated_page(2000));
	EXPECT_EQ((*result)[1], R"({"features":[],"exceededTransferLimit":false})");
	ASSERT_EQ(transport->requests.size(), 2u);
	EXPECT_NE(transport->requests[0].find("where=LABEL%20%3D%20%27SLGT%27"), std::string::npos);
	EXPECT_NE(transport->requests[0].find("outFields=LABEL%2Cvalid"), std::string::npos);
	EXPECT_NE(transport->requests[0].find("geometry=%7B%22xmin%22%3A-105%2C%22ymin%22%3A39%7D"),
			  std::string::npos);
	EXPECT_NE(transport->requests[0].find("resultOffset=0"), std::string::npos);
	EXPECT_NE(transport->requests[1].find("resultOffset=2000"), std::string::npos);
}

TEST(ArcGISClientPaging, AdvancesTheOffsetByTheRecordsTheServerActuallyReturned) {
	// A page can be shorter than the 2000 requested; advancing by 2000 would
	// skip records.
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

TEST(ArcGISClientPaging, TypedQueriesMergeFeaturesFromEveryPage) {
	const std::string feature =
		R"({"type":"Feature","properties":{"label":"TSTM"},)"
		R"("geometry":{"type":"Polygon","coordinates":[[[0,0],[1,0],[1,1],[0,0]]]}})";
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses = {
		{200, R"({"features":[)" + feature + R"(],"exceededTransferLimit":true})", {}},
		{200, R"({"features":[)" + feature + "," + feature + R"(]})", {}},
	};
	const ArcGISClient client{transport};

	const Result<CategoricalOutlookPayload> result = client.query_categorical(1);

	ASSERT_TRUE(result) << result.error().message;
	EXPECT_EQ(result->features.size(), 3u);
	ASSERT_EQ(transport->requests.size(), 2u);
	EXPECT_NE(transport->requests[1].find("resultOffset=1"), std::string::npos);
}

TEST(ArcGISClientPaging, FailsWhenATruncatedPageCarriesNoRecords) {
	// The offset would never move.
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
	// Not transient, so it is asked once.
	EXPECT_EQ(transport->requests.size(), 1u);
}

TEST(ArcGISClientPaging, KeepsTheArcGISErrorDetails) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses = {
		{200,
		 R"({"error":{"code":400,"message":"Cannot perform query.",)"
		 R"("details":["'where' parameter is invalid.","Check field names."]}})",
		 {}},
	};
	const ArcGISClient client{transport};

	const Result<std::vector<std::string>> result =
		client.query_layer(ArcGISService::Outlooks, 1, {});

	ASSERT_FALSE(result);
	EXPECT_EQ(result.error().detail, "'where' parameter is invalid.; Check field names.");
}

TEST(ArcGISClientPaging, ClassifiesArcGIS5xxCodesLikeHttpStatuses) {
	// HTTP 503 is RateLimited and other 5xx are ServerError, so the same codes
	// in an ArcGIS error object mean the same, and are retried the same.
	for (const auto& [code, expected] :
		 {std::pair{503, ErrorCode::RateLimited}, std::pair{500, ErrorCode::ServerError}}) {
		std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
		transport->responses.assign(RetryPolicy{}.max_attempts, {200, arcgis_error_body(code), {}});
		const ArcGISClient client{transport};

		const Result<CategoricalOutlookPayload> result = client.query_categorical(1);

		ASSERT_FALSE(result) << code;
		EXPECT_EQ(result.error().code, expected) << code;
		EXPECT_EQ(result.error().http_status, code);
		EXPECT_EQ(result.error().message, "Busy");
		// The ArcGIS error itself, not one rebuilt from the status.
		EXPECT_EQ(result.error().detail, std::format("arcgisCode={}", code));
		EXPECT_EQ(transport->requests.size(), RetryPolicy{}.max_attempts) << code;
	}
}

TEST(ArcGISClientPaging, RetriesTransientArcGISErrorsSentWithHttp200) {
	for (const std::int32_t code : {429, 500, 503}) {
		std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
		transport->responses = {{200, arcgis_error_body(code), {}}, empty_feature_collection()};
		const ArcGISClient client{transport};

		const Result<std::vector<std::string>> result =
			client.query_layer(ArcGISService::Outlooks, 1, {});

		ASSERT_TRUE(result) << code << ": " << result.error().message;
		// The error body is not a page.
		ASSERT_EQ(result->size(), 1u) << code;
		EXPECT_EQ((*result)[0], empty_feature_collection().body);
		EXPECT_EQ(transport->requests.size(), 2u) << code;
	}
}

TEST(ArcGISClientPaging, RetriesTheFailingPageWithoutRestartingTheQuery) {
	const std::string feature =
		R"({"type":"Feature","properties":{"label":"TSTM"},)"
		R"("geometry":{"type":"Polygon","coordinates":[[[0,0],[1,0],[1,1],[0,0]]]}})";
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses = {
		{200, R"({"features":[)" + feature + R"(],"exceededTransferLimit":true})", {}},
		{200, arcgis_error_body(500), {}},
		{200, R"({"features":[)" + feature + R"(]})", {}},
	};
	const ArcGISClient client{transport};

	const Result<CategoricalOutlookPayload> result = client.query_categorical(1);

	ASSERT_TRUE(result) << result.error().message;
	EXPECT_EQ(result->features.size(), 2u);
	ASSERT_EQ(transport->requests.size(), 3u);
	EXPECT_NE(transport->requests[1].find("resultOffset=1"), std::string::npos);
	EXPECT_EQ(transport->requests[2], transport->requests[1]);
}

TEST(ArcGISClientPaging, ARetriedPageReportsItsLastAttempt) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses = {{200, arcgis_error_body(503), {}}, {404, "gone", {}}};
	const ArcGISClient client{transport};

	const Result<std::vector<std::string>> result =
		client.query_layer(ArcGISService::Outlooks, 1, {});

	ASSERT_FALSE(result);
	EXPECT_EQ(result.error().code, ErrorCode::NotFound);
	EXPECT_EQ(result.error().http_status, 404);
	EXPECT_EQ(result.error().message, "gone");
	EXPECT_EQ(result.error().detail, "");
	EXPECT_EQ(transport->requests.size(), 2u);
}

TEST(ArcGISClientPaging, OtherArcGISCodesAreAskedOnce) {
	for (const std::int32_t code : {403, 404, 498, 1000}) {
		std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
		transport->responses = {{200, arcgis_error_body(code), {}}, empty_feature_collection()};
		const ArcGISClient client{transport};

		EXPECT_FALSE(client.query_layer(ArcGISService::Outlooks, 1, {})) << code;
		EXPECT_EQ(transport->requests.size(), 1u) << code;
	}
}

TEST(ArcGISClientPaging, AnArcGISErrorWithoutACodeDoesNotInventOne) {
	for (const std::string_view error :
		 {R"({"message":"Query failed."})", R"({"code":null,"message":"Query failed."})",
		  R"({"code":"abc","message":"Query failed."})"}) {
		std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
		transport->responses = {{200, std::format(R"({{"error":{}}})", error), {}}};
		const ArcGISClient client{transport};

		const Result<std::vector<std::string>> result =
			client.query_layer(ArcGISService::Outlooks, 1, {});

		ASSERT_FALSE(result) << error;
		EXPECT_EQ(result.error().code, ErrorCode::InvalidRequest) << error;
		EXPECT_EQ(result.error().http_status, 0) << error;
		EXPECT_EQ(result.error().message, "Query failed.") << error;
		EXPECT_EQ(result.error().detail.find("400"), std::string::npos) << result.error().detail;
		EXPECT_EQ(transport->requests.size(), 1u) << error;
	}
}

// ===== Bounds on one call =====
//
// A response is bounded on its own: its size by the transport, its projection
// growth by the parser. A call that pages must not multiply either bound by
// the number of pages.

TEST(ArcGISClientLimits, ProjectionBudgetSpansEveryPageOfOneCall) {
	// 300 features need 1,139,700 added points: over the limit on one page, so
	// over it when split across two.
	struct Case {
		std::string_view name;
		ErrorCode (*run)(const ArcGISClient&);
	};
	const std::vector<Case> cases = {
		{"query_day4_8",
		 [](const ArcGISClient& c) { return outcome_of([&c] { return c.query_day4_8(4); }); }},
		{"query_conditional_intensity",
		 [](const ArcGISClient& c) {
			 return outcome_of([&c] { return c.query_conditional_intensity(1, "tornado"); });
		 }},
		{"query_active_md",
		 [](const ArcGISClient& c) { return outcome_of([&c] { return c.query_active_md(); }); }},
	};
	for (const Case& test_case : cases) {
		std::shared_ptr<RecordingTransport> one_page = std::make_shared<RecordingTransport>();
		one_page->responses = {{200, mercator_page(300, false), {}}};
		EXPECT_EQ(test_case.run(ArcGISClient{one_page}), ErrorCode::ParseError) << test_case.name;

		std::shared_ptr<RecordingTransport> two_pages = std::make_shared<RecordingTransport>();
		two_pages->responses = {{200, mercator_page(150, true), {}},
								{200, mercator_page(150, false), {}}};
		EXPECT_EQ(test_case.run(ArcGISClient{two_pages}), ErrorCode::ParseError) << test_case.name;
		EXPECT_EQ(two_pages->requests.size(), 2u) << test_case.name;
	}
}

TEST(ArcGISClientLimits, ProjectionBudgetSpansBothFireWeatherLayers) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses = {{200, mercator_page(150, false), {}},
							{200, mercator_page(150, false), {}}};
	const ArcGISClient client{transport};

	const Result<FireWeatherPayload> result = client.query_fire_weather(1);

	ASSERT_FALSE(result) << result->features.size() << " features";
	EXPECT_EQ(result.error().code, ErrorCode::ParseError);
	EXPECT_NE(result.error().message.find("added-point limit"), std::string::npos);
	EXPECT_EQ(transport->requests.size(), 2u);
}

TEST(ArcGISClientLimits, PagesWithinTheProjectionBudgetStillMerge) {
	// 200 features need 759,800 added points.
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses = {{200, mercator_page(100, true), {}},
							{200, mercator_page(100, false), {}}};
	const ArcGISClient client{transport};

	const Result<Day48OutlookPayload> result = client.query_day4_8(4);

	ASSERT_TRUE(result) << result.error().message;
	EXPECT_EQ(result->features.size(), 200u);
}

TEST(ArcGISClientLimits, EachCallStartsWithAFreshProjectionBudget) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses.assign(2, {200, mercator_page(200, false), {}});
	const ArcGISClient client{transport};

	EXPECT_TRUE(client.query_day4_8(4));
	EXPECT_TRUE(client.query_day4_8(4));
}

TEST(ArcGISClientLimits, RawLayerQueryBoundsTheBytesItRetains) {
	// A custom transport enforces no response limit of its own.
	constexpr std::size_t kPageBytes = 1024UL * 1024UL;
	const std::size_t limit = ClientConfig{}.max_response_bytes;
	std::shared_ptr<PaddedPagesTransport> transport =
		std::make_shared<PaddedPagesTransport>(kPageBytes, true);
	const ArcGISClient client{transport};

	const Result<std::vector<std::string>> result =
		client.query_layer(ArcGISService::Outlooks, 1, {});

	ASSERT_FALSE(result);
	EXPECT_EQ(result.error().code, ErrorCode::InvalidRequest) << result.error().message;
	EXPECT_LT(transport->calls, ArcGISPager{}.max_pages());
	EXPECT_LE(transport->bytes_served, limit + 2 * kPageBytes);
}

TEST(ArcGISClientLimits, TypedQueriesBoundTheBytesOneCallAccepts) {
	constexpr std::size_t kPageBytes = 1024UL * 1024UL;
	const std::size_t limit = ClientConfig{}.max_response_bytes;
	std::shared_ptr<PaddedPagesTransport> transport =
		std::make_shared<PaddedPagesTransport>(kPageBytes, true);
	const ArcGISClient client{transport};

	const Result<CategoricalOutlookPayload> result = client.query_categorical(1);

	ASSERT_FALSE(result);
	EXPECT_EQ(result.error().code, ErrorCode::InvalidRequest) << result.error().message;
	EXPECT_LT(transport->calls, ArcGISPager{}.max_pages());
	EXPECT_LE(transport->bytes_served, limit + 2 * kPageBytes);
}

TEST(ArcGISClientLimits, TheByteLimitSpansBothFireWeatherLayers) {
	// Each layer's one page is under the limit; together they pass it.
	const std::size_t limit = ClientConfig{}.max_response_bytes;
	std::shared_ptr<PaddedPagesTransport> transport =
		std::make_shared<PaddedPagesTransport>(limit / 2 + 1, false);
	const ArcGISClient client{transport};

	const Result<FireWeatherPayload> result = client.query_fire_weather(1);

	ASSERT_FALSE(result);
	EXPECT_EQ(result.error().code, ErrorCode::InvalidRequest) << result.error().message;
	EXPECT_EQ(transport->calls, 2);
}

TEST(ArcGISClientLimits, AnOversizedFirstPageLeavesNoRoomForASecond) {
	const std::size_t limit = ClientConfig{}.max_response_bytes;
	std::shared_ptr<PaddedPagesTransport> transport =
		std::make_shared<PaddedPagesTransport>(limit + 1, true);
	const ArcGISClient client{transport};

	const Result<std::vector<std::string>> result =
		client.query_layer(ArcGISService::Outlooks, 1, {});

	ASSERT_FALSE(result);
	EXPECT_EQ(result.error().code, ErrorCode::InvalidRequest) << result.error().message;
	EXPECT_EQ(transport->calls, 2);
}

TEST(CallBudget, CapsTheBytesOfEveryPageAfterTheFirst) {
	detail::CallBudget budget{10};
	EXPECT_TRUE(budget.admits(4));
	budget.charge(4);
	// An exact fit is allowed; one byte more is not.
	EXPECT_TRUE(budget.admits(6));
	EXPECT_FALSE(budget.admits(7));
	budget.charge(6);
	EXPECT_TRUE(budget.admits(0));
	EXPECT_FALSE(budget.admits(1));
}

TEST(CallBudget, AnOversizedFirstPageIsCountedButNotRefused) {
	detail::CallBudget budget{10};
	EXPECT_TRUE(budget.admits(11));
	budget.charge(11);
	EXPECT_FALSE(budget.admits(0));
	EXPECT_FALSE(budget.admits(1));
}

TEST(CallBudget, ZeroMeansNoByteLimit) {
	// As ClientConfig::max_response_bytes = 0 means for one response.
	detail::CallBudget budget{0};
	budget.charge(std::numeric_limits<std::size_t>::max() / 2);
	EXPECT_TRUE(budget.admits(std::numeric_limits<std::size_t>::max() / 2));
}

TEST(CallBudget, TakesItsByteLimitFromAnHttpClientTransport) {
	ClientConfig config;
	config.max_response_bytes = 4096;
	EXPECT_EQ(detail::call_byte_limit(HttpClient{config}), 4096u);
	config.max_response_bytes = 0;
	EXPECT_EQ(detail::call_byte_limit(HttpClient{config}), 0u);
	// Any other transport has no limit to read.
	EXPECT_EQ(detail::call_byte_limit(RecordingTransport{}), ClientConfig{}.max_response_bytes);
}

TEST(ArcGISClientLimits, ASingleResponseIsTheTransportsToBound) {
	// The limit is on what paging adds: one response over it is accepted.
	const std::size_t limit = ClientConfig{}.max_response_bytes;
	std::shared_ptr<PaddedPagesTransport> transport =
		std::make_shared<PaddedPagesTransport>(limit + 1, false);
	const ArcGISClient client{transport};

	const Result<std::vector<std::string>> result =
		client.query_layer(ArcGISService::Outlooks, 1, {});

	ASSERT_TRUE(result) << result.error().message;
	ASSERT_EQ(result->size(), 1u);
	EXPECT_EQ((*result)[0].size(), transport->bytes_served);
}

// ===== What a 404 means =====
//
// SPC's static products answer 404 when nothing is issued. Any other 404 is a
// real fault, and ArcGIS reports a retired service as HTTP 200 with
// {"error":{"code":404}}.

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
	// The live body for a retired MapServer path.
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
		{200,
		 R"({"error":{"code":1000,"message":"Unable to complete operation.","details":[]}})",
		 {}},
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

TEST(ArcGISClientRouting, AnEmptyMesoscaleLayerIsAnEmptyResult) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses = {
		{200, test::read_fixture("arcgis_mesoscale_discussion_noarea.esri.json"), {}}};
	const ArcGISClient client{transport};

	const Result<MesoscalePayload> result = client.query_active_md();

	ASSERT_TRUE(result);
	EXPECT_TRUE(result->discussions.empty());
}

TEST(ArcGISClientRouting, AsksForAStableOrderWhilePaging) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses = {empty_feature_collection(), empty_feature_collection()};
	const ArcGISClient client{transport};

	// Typed queries order by objectid; raw queries only when asked.
	ASSERT_TRUE(client.query_categorical(1));
	ASSERT_TRUE(client.query_layer(ArcGISService::Outlooks, 1, {}));

	ASSERT_EQ(transport->requests.size(), 2u);
	EXPECT_NE(transport->requests[0].find("&orderByFields=objectid"), std::string::npos);
	EXPECT_EQ(transport->requests[1].find("orderByFields"), std::string::npos);
}

TEST(ArcGISClientRouting, RejectsNonJsonFormatsBeforeNetworkAccess) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	const ArcGISClient client{transport};
	QueryParams params;
	params.f = "kmz";

	const Result<std::vector<std::string>> result =
		client.query_layer(ArcGISService::FireWeather, 1, params);

	ASSERT_FALSE(result);
	EXPECT_EQ(result.error().code, ErrorCode::InvalidRequest);
	EXPECT_TRUE(transport->requests.empty());
}

TEST(ClientErrors, OnlyStatuses500To599AreServerErrors) {
	EXPECT_EQ(Error::from_response(500, "").code, ErrorCode::ServerError);
	EXPECT_EQ(Error::from_response(599, "").code, ErrorCode::ServerError);
	EXPECT_EQ(Error::from_response(600, "").code, ErrorCode::Unknown);
}

TEST(ClientErrors, AMalformedBodyIsAParseErrorNotAnException) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses = {{200, "<html>maintenance</html>", {}},
							{200, "<html>maintenance</html>", {}}};
	const StaticFeedClient feeds{transport};
	const ArcGISClient arcgis{transport};

	const Result<CategoricalOutlookPayload> from_feed = feeds.day_categorical(1);
	const Result<CategoricalOutlookPayload> from_arcgis = arcgis.query_categorical(1);

	ASSERT_FALSE(from_feed);
	ASSERT_FALSE(from_arcgis);
	EXPECT_EQ(from_feed.error().code, ErrorCode::ParseError);
	EXPECT_EQ(from_arcgis.error().code, ErrorCode::ParseError);
}

TEST(ClientErrors, AnAllocationFailureIsAnErrorNotAnException) {
	std::shared_ptr<OutOfMemoryTransport> transport = std::make_shared<OutOfMemoryTransport>();
	const ArcGISClient arcgis{transport};
	const StaticFeedClient feeds{transport};
	const ArchiveClient archive{transport};

	// One code for every client: the parsers' own.
	EXPECT_EQ(outcome_of([&] { return arcgis.query_layer(ArcGISService::Outlooks, 1, {}); }),
			  ErrorCode::ParseError);
	EXPECT_EQ(outcome_of([&] { return arcgis.query_categorical(1); }), ErrorCode::ParseError);
	EXPECT_EQ(outcome_of([&] { return arcgis.query_probabilistic(1, "tornado"); }),
			  ErrorCode::ParseError);
	EXPECT_EQ(outcome_of([&] { return arcgis.query_conditional_intensity(1, "tornado"); }),
			  ErrorCode::ParseError);
	EXPECT_EQ(outcome_of([&] { return arcgis.query_day4_8(4); }), ErrorCode::ParseError);
	EXPECT_EQ(outcome_of([&] { return arcgis.query_fire_weather(1); }), ErrorCode::ParseError);
	EXPECT_EQ(outcome_of([&] { return arcgis.query_active_md(); }), ErrorCode::ParseError);
	EXPECT_EQ(outcome_of([&] { return feeds.day_categorical(1); }), ErrorCode::ParseError);
	EXPECT_EQ(outcome_of([&] { return feeds.day_probabilistic(1, "tornado"); }),
			  ErrorCode::ParseError);
	EXPECT_EQ(outcome_of([&] { return feeds.day4_8(4); }), ErrorCode::ParseError);
	EXPECT_EQ(outcome_of([&] { return archive.watches(); }), ErrorCode::ParseError);
	EXPECT_EQ(outcome_of([&] { return archive.storm_reports("2026-05-19T12:00Z", "2026-05-20"); }),
			  ErrorCode::ParseError);
}

TEST(ClientErrors, AnExceptionOutsideStdFromATransportPropagates) {
	// Only a custom transport can throw one, and catching everything would also
	// swallow a thread cancellation's forced unwind.
	std::shared_ptr<ThrowsAfterAPageTransport> transport =
		std::make_shared<ThrowsAfterAPageTransport>();
	const ArcGISClient client{transport};

	EXPECT_THROW((void)client.query_categorical(1), int);
	EXPECT_EQ(transport->calls, 2);
}

TEST(ClientErrors, UnsupportedEsriProjectionBecomesParseError) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses = {
		{200,
		 R"({"spatialReference":{"wkid":32612},"features":[{"attributes":{"dn":5},"geometry":{"rings":[[[0,0],[0,1],[1,1],[1,0],[0,0]]]}}]})",
		 {}}};
	const ArcGISClient client{transport};
	const Result<FireWeatherPayload> result = client.query_fire_weather(1);
	ASSERT_FALSE(result);
	EXPECT_EQ(result.error().code, ErrorCode::ParseError);
}

// ===== ArchiveClient (IEM) URLs =====
//
// Every value is percent-encoded: an ISO offset's '+' would decode as a space
// and a raw '&' would inject a parameter.

TEST(ArchiveClientRouting, PercentEncodesTheWatchTimestamp) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses = {{200, R"({"features":[]})", {}}};
	ArchiveClient client{transport};

	ASSERT_TRUE(client.watches("202605191200&x=1"));

	ASSERT_EQ(transport->requests.size(), 1u);
	EXPECT_EQ(transport->requests[0],
			  "https://mesonet.agron.iastate.edu/json/spcwatch.py?ts=202605191200%26x%3D1");
}

TEST(ArchiveClientRouting, PercentEncodesIsoOffsetsAndFiltersByOfficeWithWfos) {
	// IEM ignores `wfo=` on lsr.geojson and returns every office; `wfos=` filters.
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->responses = {{200, R"({"features":[]})", {}}};
	ArchiveClient client{transport};

	ASSERT_TRUE(client.storm_reports("2026-05-19T12:00:00+00:00", "2026-05-20T12:00:00+00:00",
									 "ICT&sts=1900-01-01"));

	ASSERT_EQ(transport->requests.size(), 1u);
	EXPECT_EQ(transport->requests[0],
			  "https://mesonet.agron.iastate.edu/geojson/lsr.geojson"
			  "?sts=2026-05-19T12%3A00%3A00%2B00%3A00&ets=2026-05-20T12%3A00%3A00%2B00%3A00"
			  "&wfos=ICT%26sts%3D1900-01-01");
}

TEST(Version, TheHeaderAndTheLibraryReportTheProjectVersion) {
	EXPECT_EQ(std::string_view{SPC_VERSION_STRING}, SPC_PROJECT_VERSION);
	EXPECT_EQ(version(), SPC_PROJECT_VERSION);
}

TEST(Version, TheDefaultUserAgentCarriesTheProjectVersion) {
	const ClientConfig config;

	EXPECT_NE(config.user_agent.find(SPC_PROJECT_VERSION), std::string::npos)
		<< "user_agent \"" << config.user_agent << "\" does not carry version "
		<< SPC_PROJECT_VERSION;
}

} // namespace
