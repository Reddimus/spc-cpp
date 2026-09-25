/// @file test_corpus.cpp
/// @brief The captured SPC fixtures through the Day 1-3 parsers. spc-data
/// checks the same fixtures for identical output; this catches a regression
/// here first.

#include "spc/models/outlook.hpp"

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

#include "support/fixtures.hpp"

namespace {

using namespace spc;
using test::read_fixture;

TEST(Corpus, StaticCategoricalFeeds) {
	// Uppercase LABEL/ISSUE/VALID/EXPIRE, Polygon and MultiPolygon.
	const CategoricalOutlookPayload d1 =
		parse_categorical(read_fixture("day1otlk_cat.nolyr.geojson"), 1);
	const CategoricalOutlookPayload d2 =
		parse_categorical(read_fixture("day2otlk_cat.nolyr.geojson"), 2);
	const CategoricalOutlookPayload d3 =
		parse_categorical(read_fixture("day3otlk_cat.nolyr.geojson"), 3);
	EXPECT_EQ(d1.day_offset, 1);
	EXPECT_EQ(d2.day_offset, 2);
	EXPECT_EQ(d3.day_offset, 3);
	EXPECT_GT(d1.features.size(), 0u);
	EXPECT_GT(d2.features.size(), 0u);
	EXPECT_GT(d3.features.size(), 0u);
	for (const OutlookFeature& f : d1.features) {
		EXPECT_GE(f.severity, 1);
		EXPECT_LE(f.severity, 5);
		EXPECT_FALSE(f.rings.empty());
	}
}

TEST(Corpus, ArcGISGeoJsonCategoricalParses) {
	// ArcGIS uses lowercase label/issue/valid/expire.
	const CategoricalOutlookPayload p =
		parse_categorical(read_fixture("arcgis_day1_categorical.geojson"), 1);
	EXPECT_GT(p.features.size(), 0u);
	bool saw_tstm = false;
	for (const OutlookFeature& f : p.features) {
		if (f.label == "TSTM") {
			saw_tstm = true;
			EXPECT_EQ(f.severity, 1);
		}
	}
	EXPECT_TRUE(saw_tstm) << "expected the day-1 ArcGIS categorical to include a TSTM band";
}

TEST(Corpus, ProbabilisticParsesAndScales) {
	// Labels are fractions ("0.02"); every real isopleth is at least 2%, so
	// the 0.01 floor catches a stray division by 100.
	const ProbOutlookPayload p =
		parse_probabilistic(read_fixture("arcgis_day1_prob_tornado.geojson"), 1, "tornado");
	ASSERT_GT(p.features.size(), 0u);
	for (const ProbOutlookFeature& f : p.features) {
		EXPECT_EQ(f.hazard, "tornado");
		EXPECT_GE(f.probability, 0.01);
		EXPECT_LE(f.probability, 1.0);
		EXPECT_FALSE(f.rings.empty());
	}
}

TEST(Corpus, MalformedBodyThrows) {
	// SPC's HTML 404 page is the usual malformed body.
	EXPECT_THROW((void)parse_categorical(read_fixture("spc_404_no_active_outlook.html"), 1),
				 std::runtime_error);
}

TEST(Corpus, EmptyFeatureCollectionIsEmptyNotError) {
	const CategoricalOutlookPayload p =
		parse_categorical(R"({"type":"FeatureCollection","features":[]})", 7);
	EXPECT_EQ(p.day_offset, 7);
	EXPECT_TRUE(p.features.empty());
}

} // namespace
