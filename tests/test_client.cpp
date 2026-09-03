#include "spc/api.hpp"

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
		{200, R"({"features":[],"exceededTransferLimit":true})", {}},
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

TEST(ArcGISClientRouting, ActiveWatchesDirectCallersToTheIemClient) {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	ArcGISClient client{transport};

	const Result<WatchPayload> result = call_deprecated_active_watches(client);

	ASSERT_FALSE(result);
	EXPECT_EQ(result.error().code, ErrorCode::InvalidRequest);
	EXPECT_NE(result.error().message.find("ArchiveClient"), std::string::npos);
	EXPECT_TRUE(transport->requests.empty());
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
