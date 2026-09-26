/// @file test_client_parity.cpp
/// @brief Each client returns exactly what the public parse_* function gives
/// for the same body, and fails with the same message.

#include "spc/api.hpp"
#include "support/fixtures.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace spc;
using test::read_fixture;

/// Answers every request with the same body.
class FixedTransport final : public HttpTransport {
public:
	explicit FixedTransport(std::string body) : body_(std::move(body)) {}

	[[nodiscard]] Result<HttpResponse> get(std::string_view /*path*/) const override {
		return HttpResponse{200, body_, {}};
	}

private:
	std::string body_;
};

// Every field, one feature per line, so a mismatch shows where it is.
// Doubles print in their shortest round-trip form, so equal text means
// equal values.

std::string text(const std::vector<Polygon>& rings) {
	std::string out;
	for (const Polygon& ring : rings) {
		out += '[';
		for (const LonLat& point : ring) {
			out += std::format("({},{})", point.lon, point.lat);
		}
		out += ']';
	}
	return out;
}

std::string text(const CategoricalOutlookPayload& p) {
	std::string out = std::format("day_offset={}\n", p.day_offset);
	for (const OutlookFeature& f : p.features) {
		out += std::format("{}|{}|{}|{}|{}|{}\n", f.label, f.severity, f.issued_at, f.valid_from,
						   f.valid_until, text(f.rings));
	}
	return out;
}

std::string text(const ProbOutlookPayload& p) {
	std::string out = std::format("day_offset={} hazard={}\n", p.day_offset, p.hazard);
	for (const ProbOutlookFeature& f : p.features) {
		out += std::format("{}|{}|{}|{}|{}|{}\n", f.hazard, f.probability, f.issued_at,
						   f.valid_from, f.valid_until, text(f.rings));
	}
	return out;
}

std::string text(const ConditionalIntensityPayload& p) {
	std::string out = std::format("day={} hazard={}\n", p.day, p.hazard);
	for (const ConditionalIntensityFeature& f : p.features) {
		out += std::format("{}|{}|{}|{}|{}|{}\n", f.label, f.cig_level, f.issued_at, f.valid_from,
						   f.valid_until, text(f.rings));
	}
	return out;
}

std::string text(const Day48OutlookPayload& p) {
	std::string out = std::format("day={}\n", p.day);
	for (const Day48Feature& f : p.features) {
		out += std::format("{}|{}|{}|{}|{}|{}|{}\n", f.day, f.label, f.probability, f.issued_at,
						   f.valid_from, f.valid_until, text(f.rings));
	}
	return out;
}

std::string text(const FireWeatherPayload& p) {
	std::string out = std::format("day={}\n", p.day);
	for (const FireWeatherFeature& f : p.features) {
		out += std::format("{}|{}|{}|{}|{}|{}|{}|{}|{}\n", f.day, static_cast<int>(f.layer),
						   f.label, f.severity, f.probability, f.issued_at, f.valid_from,
						   f.valid_until, text(f.rings));
	}
	return out;
}

std::string text(const MesoscalePayload& p) {
	std::string out;
	for (const MesoscaleDiscussion& md : p.discussions) {
		out += std::format("{}|{}|{}|{}\n", md.name, md.folder_path, md.url, text(md.rings));
	}
	return out;
}

std::string text(const WatchPayload& p) {
	std::string out;
	for (const Watch& w : p.watches) {
		out += std::format("{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}\n", w.number, w.year, w.type, w.sel,
						   w.is_pds, w.max_hail_size, w.max_wind_gust_knots, w.spc_url, w.issued_at,
						   w.expires_at, text(w.rings));
	}
	return out;
}

std::string text(const StormReportPayload& p) {
	std::string out;
	for (const StormReport& r : p.reports) {
		out += std::format("{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}\n", r.location.lon,
						   r.location.lat, r.type, r.type_text, r.magnitude, r.unit, r.city,
						   r.county, r.state, r.wfo, r.source, r.remark, r.valid_at);
	}
	return out;
}

/// `query` run on a `Client` whose transport answers with `body`, rendered
/// as text, or the error message.
template <typename Client, typename Payload, typename Query>
std::string via_client(const std::string& body, const Query& query) {
	const Client client{std::make_shared<FixedTransport>(body)};
	const Result<Payload> result = query(client);
	if (!result) {
		return std::format("error {}: {}", to_string(result.error().code), result.error().message);
	}
	return text(*result);
}

/// Counts comparisons with a non-empty result, so an all-empty run fails.
struct Comparisons {
	std::size_t total = 0;
	std::size_t nonempty = 0;

	void check(const std::string& fixture, const std::string& expected, const std::string& actual,
			   bool empty) {
		++total;
		nonempty += empty ? 0U : 1U;
		EXPECT_EQ(actual, expected) << fixture;
	}
};

TEST(ClientParity, EveryArcGISFixtureMatchesItsParser) {
	Comparisons comparisons;

	for (const std::int32_t day : {1, 2, 3}) {
		for (const std::string_view format : {".geojson", ".esri.json"}) {
			const std::string fixture = std::format("arcgis_day{}_categorical{}", day, format);
			const std::string body = read_fixture(fixture);
			const CategoricalOutlookPayload parsed = parse_categorical(body, day);
			comparisons.check(
				fixture, text(parsed),
				via_client<ArcGISClient, CategoricalOutlookPayload>(
					body, [day](const ArcGISClient& c) { return c.query_categorical(day); }),
				parsed.features.empty());
		}
	}

	const std::vector<std::pair<std::int32_t, std::string>> hazards = {
		{1, "tornado"}, {1, "hail"}, {1, "wind"}, {2, "wind"}};
	for (const std::pair<std::int32_t, std::string>& entry : hazards) {
		const std::int32_t day = entry.first;
		const std::string& hazard = entry.second;
		for (const std::string_view format : {".geojson", ".esri.json"}) {
			const std::string fixture = std::format("arcgis_day{}_prob_{}{}", day, hazard, format);
			const std::string body = read_fixture(fixture);
			const ProbOutlookPayload parsed = parse_probabilistic(body, day, hazard);
			comparisons.check(
				fixture, text(parsed),
				via_client<ArcGISClient, ProbOutlookPayload>(
					body,
					[&](const ArcGISClient& c) { return c.query_probabilistic(day, hazard); }),
				parsed.features.empty());
		}
	}

	{
		const std::string fixture = "arcgis_day1_torn_conditional_intensity.esri.json";
		const std::string body = read_fixture(fixture);
		const ConditionalIntensityPayload parsed = parse_conditional_intensity(body, 1, "tornado");
		comparisons.check(
			fixture, text(parsed),
			via_client<ArcGISClient, ConditionalIntensityPayload>(
				body,
				[](const ArcGISClient& c) { return c.query_conditional_intensity(1, "tornado"); }),
			parsed.features.empty());
	}

	{
		const std::string fixture = "arcgis_day4_8_nonempty.synthetic.json";
		const std::string body = read_fixture(fixture);
		const Day48OutlookPayload parsed = parse_day4_8(body, 4);
		comparisons.check(fixture, text(parsed),
						  via_client<ArcGISClient, Day48OutlookPayload>(
							  body, [](const ArcGISClient& c) { return c.query_day4_8(4); }),
						  parsed.features.empty());
	}

	for (const std::int32_t day : {1, 2}) {
		// The client asks for both of the day's layers; this transport serves
		// the same body to each.
		const std::string fixture = std::format("arcgis_day{}_fire_weather.esri.json", day);
		const std::string body = read_fixture(fixture);
		FireWeatherPayload parsed = parse_fire_weather(body, day, FireWeatherLayer::Outlook);
		const FireWeatherPayload dry =
			parse_fire_weather(body, day, FireWeatherLayer::DryThunderstorm);
		parsed.features.insert(parsed.features.end(), dry.features.begin(), dry.features.end());
		comparisons.check(
			fixture, text(parsed),
			via_client<ArcGISClient, FireWeatherPayload>(
				body, [day](const ArcGISClient& c) { return c.query_fire_weather(day); }),
			parsed.features.empty());
	}

	for (const std::string fixture : {"arcgis_mesoscale_discussion.esri.json",
									  "arcgis_mesoscale_discussion_noarea.esri.json"}) {
		const std::string body = read_fixture(fixture);
		const MesoscalePayload parsed = parse_mesoscale_discussions(body);
		comparisons.check(fixture, text(parsed),
						  via_client<ArcGISClient, MesoscalePayload>(
							  body, [](const ArcGISClient& c) { return c.query_active_md(); }),
						  parsed.discussions.empty());
	}

	EXPECT_EQ(comparisons.total, 20U);
	EXPECT_EQ(comparisons.nonempty, 12U);
}

TEST(ClientParity, StaticFeedAndArchiveMatchTheirParsers) {
	Comparisons comparisons;

	for (const std::int32_t day : {1, 2, 3}) {
		const std::string fixture = std::format("day{}otlk_cat.nolyr.geojson", day);
		const std::string body = read_fixture(fixture);
		const CategoricalOutlookPayload parsed = parse_categorical(body, day);
		comparisons.check(
			fixture, text(parsed),
			via_client<StaticFeedClient, CategoricalOutlookPayload>(
				body, [day](const StaticFeedClient& c) { return c.day_categorical(day); }),
			parsed.features.empty());
	}

	{
		const std::string fixture = "arcgis_day1_prob_tornado.geojson";
		const std::string body = read_fixture(fixture);
		const ProbOutlookPayload parsed = parse_probabilistic(body, 1, "tornado");
		comparisons.check(
			fixture, text(parsed),
			via_client<StaticFeedClient, ProbOutlookPayload>(
				body, [](const StaticFeedClient& c) { return c.day_probabilistic(1, "tornado"); }),
			parsed.features.empty());
	}

	{
		const std::string fixture = "day4prob.nolyr.geojson";
		const std::string body = read_fixture(fixture);
		const Day48OutlookPayload parsed = parse_day4_8(body, 4);
		comparisons.check(fixture, text(parsed),
						  via_client<StaticFeedClient, Day48OutlookPayload>(
							  body, [](const StaticFeedClient& c) { return c.day4_8(4); }),
						  parsed.features.empty());
	}

	{
		const std::string fixture = "iem_spc_watch.json";
		const std::string body = read_fixture(fixture);
		const WatchPayload parsed = parse_watches(body);
		comparisons.check(fixture, text(parsed),
						  via_client<ArchiveClient, WatchPayload>(
							  body, [](const ArchiveClient& c) { return c.watches(); }),
						  parsed.watches.empty());
	}

	{
		const std::string fixture = "iem_storm_reports.json";
		const std::string body = read_fixture(fixture);
		const StormReportPayload parsed = parse_storm_reports(body);
		comparisons.check(fixture, text(parsed),
						  via_client<ArchiveClient, StormReportPayload>(
							  body,
							  [](const ArchiveClient& c) {
								  return c.storm_reports("2024-04-26T00:00Z", "2024-04-27T00:00Z");
							  }),
						  parsed.reports.empty());
	}

	EXPECT_EQ(comparisons.total, 7U);
	EXPECT_EQ(comparisons.nonempty, 7U);
}

/// What parse_categorical throws for `body`, or "" if it does not throw.
std::string parse_categorical_error(const std::string& body) {
	try {
		(void)parse_categorical(body, 1);
	} catch (const std::runtime_error& e) {
		return e.what();
	}
	return {};
}

/// "<code>: <message>" for a failed result.
template <typename T>
std::string failure(const Result<T>& result) {
	if (result) {
		return "succeeded";
	}
	return std::format("{}: {}", to_string(result.error().code), result.error().message);
}

TEST(ClientParity, AMalformedBodyFailsWithTheParsersMessage) {
	// Truncated bodies matter most: Glaze words some truncations differently
	// depending on how it reads the buffer.
	const std::vector<std::string> bodies = {
		read_fixture("spc_404_no_active_outlook.html"),
		"",
		" ",
		"{",
		R"({"features":[)",
		R"({"features":[1,2)",
		R"({"a":"x")",
		"1e",
		read_fixture("arcgis_day1_categorical.geojson").substr(0, 5000),
	};

	for (const std::string& body : bodies) {
		const std::string thrown = parse_categorical_error(body);
		ASSERT_FALSE(thrown.empty()) << "parse_categorical accepted: " << body.substr(0, 40);
		const std::string expected = "ParseError: " + thrown;
		const std::shared_ptr<FixedTransport> transport = std::make_shared<FixedTransport>(body);

		EXPECT_EQ(failure(ArcGISClient{transport}.query_categorical(1)), expected);
		EXPECT_EQ(failure(ArcGISClient{transport}.query_layer(ArcGISService::Outlooks, 1, {})),
				  expected);
		EXPECT_EQ(failure(StaticFeedClient{transport}.day_categorical(1)), expected);
		EXPECT_EQ(failure(ArchiveClient{transport}.watches()), expected);
	}
}

TEST(ClientParity, ATypedQueryRejectsATruncatedPageWithNoRecords) {
	// Checked before any features are built; the offset would never move.
	// f=geojson may carry the flag under `properties` only.
	for (
		const std::string body :
		{R"({"features":[],"exceededTransferLimit":true})",
		 R"({"type":"FeatureCollection","features":[],"properties":{"exceededTransferLimit":true}})"}) {
		const Result<CategoricalOutlookPayload> result =
			ArcGISClient{std::make_shared<FixedTransport>(body)}.query_categorical(1);

		ASSERT_FALSE(result) << body;
		EXPECT_EQ(result.error().code, ErrorCode::ServerError) << body;
		EXPECT_EQ(result.error().message, "ArcGIS reported a truncated page containing no records");
	}
}

} // namespace
