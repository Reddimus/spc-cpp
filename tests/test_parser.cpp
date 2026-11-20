/// @file test_parser.cpp
/// @brief Day 1-3 parser behavior shared with spc-data. The assertions match
/// spc-data's own parser tests.

#include "spc/models/convective.hpp"
#include "spc/models/fire_weather.hpp"
#include "spc/models/outlook.hpp"
#include "spc/models/storm_report.hpp"
#include "spc/models/watch.hpp"

#include <cmath>
#include <gtest/gtest.h>
#include <string>

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

} // namespace
