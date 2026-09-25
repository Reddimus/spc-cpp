/// @file test_parser.cpp
/// @brief Day 1-3 parser behavior shared with spc-data. The assertions match
/// spc-data's own parser tests.

#include "spc/models/outlook.hpp"

#include <gtest/gtest.h>

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

} // namespace
