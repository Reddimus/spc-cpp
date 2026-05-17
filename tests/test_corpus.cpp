/// @file test_corpus.cpp
/// @brief Drives the shared live-captured SPC fixture corpus through the
/// extracted `spc::parse_*` functions. This is the spc-cpp side of the
/// byte-identity contract: the same fixtures the spc-data PR-0 golden was
/// frozen over must parse to the same feature counts / labels / probs here.
/// The authoritative byte-for-byte SQL diff runs in spc-data PR-3; this
/// guards the SDK in isolation so a regression is caught in spc-cpp's own CI.

#include "spc/models/outlook.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>

namespace {

using namespace spc;

std::string slurp(const std::string& name) {
	std::ifstream f(std::filesystem::path(SPC_FIXTURES_DIR) / name, std::ios::binary);
	EXPECT_TRUE(f.is_open()) << "missing fixture: " << name;
	std::stringstream buf;
	buf << f.rdbuf();
	return buf.str();
}

TEST(Corpus, StaticCategoricalFeeds) {
	// The 3 live static www.spc.noaa.gov day1-3 categorical feeds (uppercase
	// LABEL/ISSUE/VALID/EXPIRE, Polygon + MultiPolygon).
	const CategoricalOutlookPayload d1 = parse_categorical(slurp("day1otlk_cat.nolyr.geojson"), 1);
	const CategoricalOutlookPayload d2 = parse_categorical(slurp("day2otlk_cat.nolyr.geojson"), 2);
	const CategoricalOutlookPayload d3 = parse_categorical(slurp("day3otlk_cat.nolyr.geojson"), 3);
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
	// ArcGIS f=geojson mirror uses lowercase label/issue/valid/expire — this
	// exercises the parser's case-variant fallback chain.
	const CategoricalOutlookPayload p =
		parse_categorical(slurp("arcgis_day1_categorical.geojson"), 1);
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
	// ArcGIS f=geojson probabilistic tornado: label is "0.02"/"0.05"/"0.10".
	// parse_probabilistic divides by 100 (verbatim spc-data semantics), so a
	// "0.02" label -> 0.0002 probability.
	const ProbOutlookPayload p =
		parse_probabilistic(slurp("arcgis_day1_prob_tornado.geojson"), 1, "tornado");
	ASSERT_GT(p.features.size(), 0u);
	for (const ProbOutlookFeature& f : p.features) {
		EXPECT_EQ(f.hazard, "tornado");
		EXPECT_GT(f.probability, 0.0);
		EXPECT_LT(f.probability, 1.0);
		EXPECT_FALSE(f.rings.empty());
	}
}

TEST(Corpus, MalformedBodyThrows) {
	// spc-data contract: parse_* throws std::runtime_error on malformed JSON
	// (the SPC HTML 404 body is the canonical example). main.cpp catches it.
	EXPECT_THROW((void)parse_categorical(slurp("spc_404_no_active_outlook.html"), 1),
				 std::runtime_error);
}

TEST(Corpus, EmptyFeatureCollectionIsEmptyNotError) {
	const CategoricalOutlookPayload p =
		parse_categorical(R"({"type":"FeatureCollection","features":[]})", 7);
	EXPECT_EQ(p.day_offset, 7);
	EXPECT_TRUE(p.features.empty());
}

} // namespace
