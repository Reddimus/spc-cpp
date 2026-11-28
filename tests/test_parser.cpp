/// @file test_parser.cpp
/// @brief Unit tests for outlook, probability, timestamp, ring, watch, and
/// storm-report parsers.

#include "spc/models/convective.hpp"
#include "spc/models/fire_weather.hpp"
#include "spc/models/outlook.hpp"
#include "spc/models/storm_report.hpp"
#include "spc/models/watch.hpp"

#include <cmath>
#include <cstddef>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace spc;

TEST(SeverityLabel, KnownValues) {
	EXPECT_EQ(severity_from_label("MRGL"), 1);
	EXPECT_EQ(severity_from_label("SLGT"), 2);
	EXPECT_EQ(severity_from_label("ENH"), 3);
	EXPECT_EQ(severity_from_label("MDT"), 4);
	EXPECT_EQ(severity_from_label("HIGH"), 5);
	EXPECT_EQ(severity_from_label("TSTM"), 1);
	EXPECT_EQ(severity_from_label("UNKNOWN"), 0);
	EXPECT_EQ(severity_from_label(""), 0);
}

TEST(Parser, EmptyFeatures) {
	const CategoricalOutlookPayload p =
		parse_categorical(R"({"type":"FeatureCollection","features":[]})", 1);
	EXPECT_TRUE(p.features.empty());
	EXPECT_EQ(p.day_offset, 1);
}

TEST(Parser, CategoricalPolygon) {
	const CategoricalOutlookPayload p = parse_categorical(R"({
		"type": "FeatureCollection",
		"features": [{
			"type": "Feature",
			"properties": {
				"LABEL": "SLGT",
				"ISSUE": "202604170100",
				"VALID": "202604171200",
				"EXPIRE": "202604181200"
			},
			"geometry": {
				"type": "Polygon",
				"coordinates": [[[-98, 34], [-94, 34], [-94, 38], [-98, 38], [-98, 34]]]
			}
		}]
	})",
														  1);
	ASSERT_EQ(p.features.size(), 1u);
	EXPECT_EQ(p.features[0].label, "SLGT");
	EXPECT_EQ(p.features[0].severity, 2);
	ASSERT_EQ(p.features[0].rings.size(), 1u);
	EXPECT_EQ(p.features[0].rings[0].size(), 5u);
}

TEST(Parser, MultiPolygon) {
	const CategoricalOutlookPayload p = parse_categorical(R"({
		"type": "FeatureCollection",
		"features": [{
			"type": "Feature",
			"properties": {"LABEL": "ENH"},
			"geometry": {
				"type": "MultiPolygon",
				"coordinates": [
					[[[-100, 35], [-98, 35], [-98, 37], [-100, 37], [-100, 35]]],
					[[[-85, 30], [-83, 30], [-83, 32], [-85, 32], [-85, 30]]]
				]
			}
		}]
	})",
														  1);
	ASSERT_EQ(p.features.size(), 1u);
	EXPECT_EQ(p.features[0].severity, 3);
	ASSERT_EQ(p.features[0].rings.size(), 2u);
}

TEST(Parser, ProbabilisticTornado) {
	const ProbOutlookPayload p = parse_probabilistic(R"({
		"type": "FeatureCollection",
		"features": [
			{"properties": {"LABEL": "2"},
			 "geometry": {"type":"Polygon","coordinates":[[[-100,34],[-95,34],[-95,38],[-100,38],[-100,34]]]}},
			{"properties": {"LABEL": "5"},
			 "geometry": {"type":"Polygon","coordinates":[[[-99,35],[-96,35],[-96,37],[-99,37],[-99,35]]]}}
		]
	})",
													 1, "tornado");
	ASSERT_EQ(p.features.size(), 2u);
	EXPECT_EQ(p.features[0].hazard, "tornado");
	EXPECT_DOUBLE_EQ(p.features[0].probability, 0.02);
	EXPECT_DOUBLE_EQ(p.features[1].probability, 0.05);
}

// Live feeds send fractions ("0.02"); older ones sent percentages ("2").
// Both must give the same probability.
TEST(Parser, ProbabilisticFractionalLabel) {
	const ProbOutlookPayload p = parse_probabilistic(R"({
		"type": "FeatureCollection",
		"features": [
			{"properties": {"LABEL": "0.02", "LABEL2": "2% Tornado Risk"},
			 "geometry": {"type":"Polygon","coordinates":[[[-100,34],[-95,34],[-95,38],[-100,38],[-100,34]]]}},
			{"properties": {"LABEL": "0.30", "LABEL2": "30% Tornado Risk"},
			 "geometry": {"type":"Polygon","coordinates":[[[-99,35],[-96,35],[-96,37],[-99,37],[-99,35]]]}}
		]
	})",
													 1, "tornado");
	ASSERT_EQ(p.features.size(), 2u);
	EXPECT_DOUBLE_EQ(p.features[0].probability, 0.02);
	EXPECT_DOUBLE_EQ(p.features[1].probability, 0.30);
}

TEST(Parser, UnknownLabelSkipped) {
	const CategoricalOutlookPayload p = parse_categorical(R"({
		"type": "FeatureCollection",
		"features": [{"properties":{"LABEL":"XYZ"},"geometry":{"type":"Polygon","coordinates":[[[0,0],[1,0],[1,1],[0,1],[0,0]]]}}]
	})",
														  1);
	EXPECT_TRUE(p.features.empty());
}

// A ring inside a feature whose fields are replaced per case.
std::string esri_feature(std::string_view fields) {
	return R"({"features":[{"attributes":)" + std::string{fields} +
		   R"(,"geometry":{"rings":[[[0,1],[1,1],[1,0],[0,0],[0,1]]]}}]})";
}

TEST(Parser, ConditionalIntensitySkipsFeaturesWithoutALabel) {
	for (const char* fields : {"null", "42", "{}", R"({"dn":null})"}) {
		EXPECT_TRUE(
			parse_conditional_intensity(esri_feature(fields), 1, "tornado").features.empty())
			<< fields;
	}
	EXPECT_EQ(parse_conditional_intensity(esri_feature(R"({"label":"CIG1"})"), 1, "tornado")
				  .features.size(),
			  1u);
}

TEST(Parser, FireWeatherSkipsDay1And2FeaturesWithNoBand) {
	for (const char* fields : {"null", "42", "{}", R"({"dn":null})", R"({"dn":6})"}) {
		EXPECT_TRUE(
			parse_fire_weather(esri_feature(fields), 1, FireWeatherLayer::Outlook).features.empty())
			<< fields;
	}
	const FireWeatherPayload kept =
		parse_fire_weather(esri_feature(R"({"dn":8})"), 1, FireWeatherLayer::Outlook);
	ASSERT_EQ(kept.features.size(), 1u);
	EXPECT_EQ(kept.features[0].label, "CRIT");
}

TEST(Parser, NonFiniteNumericStringsReadAsZero) {
	const StormReportPayload reports = parse_storm_reports(
		R"({"features":[{"properties":{"magnitude":"nan","lon":"inf","lat":"-inf"},"geometry":null}]})");
	ASSERT_EQ(reports.reports.size(), 1u);
	EXPECT_EQ(reports.reports[0].magnitude, 0.0);
	EXPECT_TRUE(std::isfinite(reports.reports[0].location.lon));
	EXPECT_TRUE(std::isfinite(reports.reports[0].location.lat));

	const WatchPayload watches = parse_watches(
		R"({"features":[{"properties":{"max_hail_size":"inf","max_wind_gust_knots":"nan"},"geometry":null}]})");
	ASSERT_EQ(watches.watches.size(), 1u);
	EXPECT_EQ(watches.watches[0].max_hail_size, 0.0);
	EXPECT_EQ(watches.watches[0].max_wind_gust_knots, 0.0);
}

TEST(Parser, NumericStringsMustBeWholeNumbers) {
	// "5xyz" is not 5: only a string that is entirely a number counts, as in
	// the fire-weather no-risk check.
	const StormReportPayload reports = parse_storm_reports(
		R"({"features":[{"properties":{"magf":"75xyz"},"geometry":null},
		{"properties":{"magf":"2.5in"},"geometry":null},
		{"properties":{"magf":"1.75"},"geometry":null}]})");
	ASSERT_EQ(reports.reports.size(), 3u);
	EXPECT_EQ(reports.reports[0].magnitude, 0.0);
	EXPECT_EQ(reports.reports[1].magnitude, 0.0);
	EXPECT_EQ(reports.reports[2].magnitude, 1.75);

	const WatchPayload watches = parse_watches(
		R"({"features":[{"properties":{"number":"123abc"},"geometry":null},
		{"properties":{"number":"139"},"geometry":null}]})");
	ASSERT_EQ(watches.watches.size(), 2u);
	EXPECT_EQ(watches.watches[0].number, 0);
	EXPECT_EQ(watches.watches[1].number, 139);

	for (const char* fields : {R"({"dn":"5xyz"})", R"({"dn":"8junk"})", R"({"dn":"5 "})",
							   R"({"dn":"5e"})", R"({"dn":"0abc"})", R"({"dn":"0 "})"}) {
		EXPECT_TRUE(
			parse_fire_weather(esri_feature(fields), 1, FireWeatherLayer::Outlook).features.empty())
			<< fields;
	}
	EXPECT_EQ(parse_fire_weather(esri_feature(R"({"dn":"5"})"), 1, FireWeatherLayer::Outlook)
				  .features[0]
				  .label,
			  "ELEV");
	EXPECT_EQ(parse_fire_weather(esri_feature(R"({"dn":"8"})"), 1, FireWeatherLayer::Outlook)
				  .features[0]
				  .label,
			  "CRIT");

	for (const char* fields : {R"({"label":"15xyz"})", R"({"label":"5%"})"}) {
		EXPECT_TRUE(
			parse_fire_weather(esri_feature(fields), 3, FireWeatherLayer::Outlook).features.empty())
			<< fields;
	}
	const FireWeatherPayload day3 =
		parse_fire_weather(esri_feature(R"({"label":"0.15"})"), 3, FireWeatherLayer::Outlook);
	ASSERT_EQ(day3.features.size(), 1u);
	EXPECT_DOUBLE_EQ(day3.features[0].probability, 0.15);
}

/// A day 1 SLGT feature whose ISSUE, VALID, and EXPIRE are `stamp`.
std::string categorical_with_times(std::string_view stamp) {
	const std::string quoted = "\"" + std::string{stamp} + "\"";
	return R"({"features":[{"properties":{"LABEL":"SLGT","ISSUE":)" + quoted + R"(,"VALID":)" +
		   quoted + R"(,"EXPIRE":)" + quoted +
		   R"(},"geometry":{"type":"Polygon","coordinates":[[[-100,35],[-99,35],[-99,36],[-100,36],[-100,35]]]}}]})";
}

TEST(Parser, ImpossibleSpcTimestampsStayAsSent) {
	// Twelve digits that name no real UTC minute are not a YYYYMMDDHHMM time.
	for (const char* stamp :
		 {"202613400000", "202602300000", "202613010000", "202302291200", "202601152400",
		  "202601151260", "202600011200", "202601001200", "000001011200"}) {
		const CategoricalOutlookPayload payload =
			parse_categorical(categorical_with_times(stamp), 1);
		ASSERT_EQ(payload.features.size(), 1u) << stamp;
		EXPECT_EQ(payload.features[0].issued_at, stamp);
		EXPECT_EQ(payload.features[0].valid_from, stamp);
		EXPECT_EQ(payload.features[0].valid_until, stamp);
	}
	const CategoricalOutlookPayload leap =
		parse_categorical(categorical_with_times("202402291200"), 1);
	ASSERT_EQ(leap.features.size(), 1u);
	EXPECT_EQ(leap.features[0].issued_at, "2024-02-29T12:00:00Z");
	const CategoricalOutlookPayload late =
		parse_categorical(categorical_with_times("202612312359"), 1);
	ASSERT_EQ(late.features.size(), 1u);
	EXPECT_EQ(late.features[0].valid_until, "2026-12-31T23:59:00Z");
}

TEST(Parser, ANullOrNonNumericLatestWkidFallsBackToWkid) {
	const std::string ring = R"("geometry":{"rings":[[[0,1],[1,1],[1,0],[0,0],[0,1]]]})";
	for (const char* reference :
		 {R"({"wkid":4326,"latestWkid":null})", R"({"wkid":4326,"latestWkid":"4326"})"}) {
		const std::string body = std::string{R"({"spatialReference":)"} + reference +
								 R"(,"features":[{"attributes":{"label":"CIG1"},)" + ring + "}]}";
		EXPECT_EQ(parse_conditional_intensity(body, 1, "tornado").features.size(), 1u) << reference;
	}
	// A geometry's null reference leaves the payload's in force.
	const std::string body =
		R"({"spatialReference":{"wkid":4326},"features":[{"attributes":{"label":"CIG1"},"geometry":{"spatialReference":null,"rings":[[[0,1],[1,1],[1,0],[0,0],[0,1]]]}}]})";
	EXPECT_EQ(parse_conditional_intensity(body, 1, "tornado").features.size(), 1u);
}

std::string geojson_feature(std::string_view props, std::string_view geometry) {
	return R"({"type":"FeatureCollection","features":[{"type":"Feature","properties":)" +
		   std::string{props} + R"(,"geometry":)" + std::string{geometry} + "}]}";
}

constexpr std::string_view kValidGeoJsonPolygon =
	R"({"type":"Polygon","coordinates":[[[-100,40],[-99,40],[-99,39],[-100,39],[-100,40]]]})";

TEST(Parser, PresentZeroProbabilityDoesNotFallThroughToDn) {
	for (const char* props : {R"({"LABEL":0,"dn":15})", R"({"LABEL":"0","dn":15})",
							  R"({"label":0,"dn":30})", R"({"label":"0","dn":25})"}) {
		const std::string body = geojson_feature(props, kValidGeoJsonPolygon);
		EXPECT_TRUE(parse_day4_8(body, 4).features.empty()) << props;
		EXPECT_TRUE(parse_probabilistic(body, 1, "tornado").features.empty()) << props;
		EXPECT_TRUE(
			parse_fire_weather(esri_feature(props), 3, FireWeatherLayer::Outlook).features.empty())
			<< props;
	}

	// Absent, null, empty, or non-numeric LABEL still falls through to dn.
	for (const char* props : {R"({"dn":15})", R"({"LABEL":null,"dn":15})",
							  R"({"LABEL":"","dn":15})", R"({"LABEL":"75xyz","dn":15})"}) {
		const Day48OutlookPayload day4 =
			parse_day4_8(geojson_feature(props, kValidGeoJsonPolygon), 4);
		ASSERT_EQ(day4.features.size(), 1u) << props;
		EXPECT_DOUBLE_EQ(day4.features[0].probability, 0.15) << props;
	}

	// Nonzero LABEL wins over dn.
	const Day48OutlookPayload nonzero =
		parse_day4_8(geojson_feature(R"({"LABEL":5,"dn":30})", kValidGeoJsonPolygon), 4);
	ASSERT_EQ(nonzero.features.size(), 1u);
	EXPECT_DOUBLE_EQ(nonzero.features[0].probability, 0.05);
}

TEST(Parser, DegenerateGeoJsonRingsAreDropped) {
	for (const char* geometry : {
			 R"({"type":"Polygon","coordinates":[[]]})",
			 R"({"type":"MultiPolygon","coordinates":[[[]]]})",
			 R"({"type":"Polygon","coordinates":[42]})",
			 R"({"type":"MultiPolygon","coordinates":[[42]]})",
			 R"({"type":"Polygon","coordinates":[["x"]]})",
			 R"({"type":"Polygon","coordinates":[[["x","y"],["a","b"]]]})",
			 R"({"type":"Polygon","coordinates":[[[-98,34]]]})",
			 R"({"type":"Polygon","coordinates":[[[-98,34],[-94,34]]]})",
			 R"({"type":"Polygon","coordinates":[[[-98,34],[-98,34],[-98,34],[-98,34],[-98,34]]]})",
			 R"({"type":"Polygon","coordinates":[[[-98,34],[-96,34],[-94,34],[-98,34]]]})",
		 }) {
		EXPECT_TRUE(
			parse_categorical(geojson_feature(R"({"LABEL":"MRGL"})", geometry), 1).features.empty())
			<< geometry;
		EXPECT_TRUE(parse_probabilistic(geojson_feature(R"({"LABEL":15})", geometry), 1, "tornado")
						.features.empty())
			<< geometry;
		EXPECT_TRUE(parse_conditional_intensity(geojson_feature(R"({"LABEL":"CIG1"})", geometry), 1,
												"tornado")
						.features.empty())
			<< geometry;
	}

	// A MultiPolygon with one degenerate and one valid polygon keeps only the valid ring.
	const CategoricalOutlookPayload mixed = parse_categorical(
		geojson_feature(
			R"({"LABEL":"SLGT"})",
			R"({"type":"MultiPolygon","coordinates":[[[]],[[[-100,35],[-99,35],[-99,36],[-100,36],[-100,35]]]]})"),
		1);
	ASSERT_EQ(mixed.features.size(), 1u);
	EXPECT_EQ(mixed.features[0].rings.size(), 1u);
}

TEST(Parser, DegenerateEsriRingsAreDropped) {
	for (const char* rings : {
			 R"([[]])",
			 R"([[[-98,34]]])",
			 R"([[[-98,34],[-94,34]]])",
			 R"([[[-98,34],[-98,34],[-98,34],[-98,34],[-98,34]]])",
			 R"([[[-98,34],[-96,34],[-94,34],[-98,34]]])",
		 }) {
		const std::string body =
			R"({"spatialReference":{"wkid":4326},"features":[{"attributes":{"label":"CIG1","dn":15},"geometry":{"rings":)" +
			std::string{rings} + "}}]}";
		EXPECT_TRUE(parse_conditional_intensity(body, 1, "tornado").features.empty()) << rings;
		EXPECT_TRUE(parse_day4_8(body, 4).features.empty()) << rings;
		EXPECT_TRUE(parse_fire_weather(body, 3, FireWeatherLayer::Outlook).features.empty())
			<< rings;
	}

	// Valid clockwise outer ring is kept; counter-clockwise hole is dropped.
	const std::string valid_and_hole =
		R"({"spatialReference":{"wkid":4326},"features":[{"attributes":{"label":"CIG1"},"geometry":{"rings":[
			[[-98,34],[-98,38],[-94,38],[-94,34],[-98,34]],
			[[-97,35],[-95,35],[-95,37],[-97,37],[-97,35]]
		]}}]})";
	const ConditionalIntensityPayload parsed =
		parse_conditional_intensity(valid_and_hole, 1, "tornado");
	ASSERT_EQ(parsed.features.size(), 1u);
	EXPECT_EQ(parsed.features[0].rings.size(), 1u);
}

TEST(Parser, CategoricalDnFallbackMapsSpcCodes) {
	struct Case {
		const char* props;
		const char* label;
		std::uint8_t severity;
	};
	for (const Case& c : {
			 Case{R"({"dn":2})", "TSTM", 1},
			 Case{R"({"dn":3})", "MRGL", 1},
			 Case{R"({"dn":4})", "SLGT", 2},
			 Case{R"({"dn":5})", "ENH", 3},
			 Case{R"({"dn":6})", "MDT", 4},
			 Case{R"({"dn":8})", "HIGH", 5},
			 Case{R"({"dn":"5"})", "ENH", 3},
			 Case{R"({"dn":"8"})", "HIGH", 5},
			 Case{R"({"dn":5,"label":"SLGT"})", "SLGT", 2},
		 }) {
		const CategoricalOutlookPayload p =
			parse_categorical(geojson_feature(c.props, kValidGeoJsonPolygon), 1);
		ASSERT_EQ(p.features.size(), 1u) << c.props;
		EXPECT_EQ(p.features[0].label, c.label) << c.props;
		EXPECT_EQ(p.features[0].severity, c.severity) << c.props;
	}

	// Unused dn codes (including 7) and malformed strings are dropped.
	for (const char* props : {R"({"dn":0})", R"({"dn":7})", R"({"dn":"7"})", R"({"dn":"5xyz"})"}) {
		EXPECT_TRUE(
			parse_categorical(geojson_feature(props, kValidGeoJsonPolygon), 1).features.empty())
			<< props;
	}
}

TEST(Parser, StormReportPresentZeroMagfDoesNotFallThrough) {
	const StormReportPayload reports = parse_storm_reports(R"({"features":[
		{"properties":{"magf":0,"magnitude":"2.5","type":"H"},"geometry":{"type":"Point","coordinates":[-97.5,35.5]}},
		{"properties":{"magf":"0","magnitude":"2.5","type":"H"},"geometry":{"type":"Point","coordinates":[-97.5,35.5]}},
		{"properties":{"magf":0.0,"magnitude":2.5,"type":"H"},"geometry":{"type":"Point","coordinates":[-97.5,35.5]}},
		{"properties":{"magf":1.75,"magnitude":"2.5","type":"H"},"geometry":{"type":"Point","coordinates":[-97.5,35.5]}},
		{"properties":{"magnitude":"2.5","type":"H"},"geometry":{"type":"Point","coordinates":[-97.5,35.5]}},
		{"properties":{"magf":null,"magnitude":"1.75","type":"H"},"geometry":{"type":"Point","coordinates":[-97.5,35.5]}},
		{"properties":{"magf":"75xyz","magnitude":"2.5","type":"H"},"geometry":{"type":"Point","coordinates":[-97.5,35.5]}},
		{"properties":{"magf":0,"magnitude":"EF0","type":"T"},"geometry":{"type":"Point","coordinates":[-97.5,35.5]}}
	]})");
	ASSERT_EQ(reports.reports.size(), 8u);
	EXPECT_DOUBLE_EQ(reports.reports[0].magnitude, 0.0);
	EXPECT_DOUBLE_EQ(reports.reports[1].magnitude, 0.0);
	EXPECT_DOUBLE_EQ(reports.reports[2].magnitude, 0.0);
	EXPECT_DOUBLE_EQ(reports.reports[3].magnitude, 1.75);
	EXPECT_DOUBLE_EQ(reports.reports[4].magnitude, 2.5);
	EXPECT_DOUBLE_EQ(reports.reports[5].magnitude, 1.75);
	EXPECT_DOUBLE_EQ(reports.reports[6].magnitude, 2.5);
	EXPECT_DOUBLE_EQ(reports.reports[7].magnitude, 0.0);
}

TEST(Parser, StormReportPointAtOriginIsKept) {
	// The property lon/lat stand in only for a missing Point, not for one at (0, 0).
	const StormReportPayload reports = parse_storm_reports(R"({"features":[
		{"properties":{"lon":-97.5,"lat":37.7},"geometry":{"type":"Point","coordinates":[0,0]}},
		{"properties":{"lon":-97.5,"lat":37.7},"geometry":{"type":"Point","coordinates":[0,37.6]}},
		{"properties":{"lon":-97.5,"lat":37.7},"geometry":{"type":"Point","coordinates":[-97.4,0]}},
		{"properties":{"lon":-97.5,"lat":37.7},"geometry":null},
		{"properties":{"lon":-97.5,"lat":37.7},"geometry":{"type":"Point"}},
		{"properties":{"lon":-97.5,"lat":37.7},"geometry":{"type":"Point","coordinates":["0","0"]}},
		{"properties":{},"geometry":{"type":"Point","coordinates":[0,0]}}
	]})");
	ASSERT_EQ(reports.reports.size(), 7u);
	const std::vector<LonLat> expected = {{0.0, 0.0},	 {0.0, 37.6},	{-97.4, 0.0}, {-97.5, 37.7},
										  {-97.5, 37.7}, {-97.5, 37.7}, {0.0, 0.0}};
	for (std::size_t i = 0; i < expected.size(); ++i) {
		EXPECT_DOUBLE_EQ(reports.reports[i].location.lon, expected[i].lon) << "feature " << i;
		EXPECT_DOUBLE_EQ(reports.reports[i].location.lat, expected[i].lat) << "feature " << i;
	}
}

} // namespace
