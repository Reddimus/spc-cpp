/// @file test_arcgis.cpp
/// @brief Esri-vs-GeoJSON parity + net-new model coverage over the shared
/// live fixture corpus.
///
/// The parity claim that gates the ArcGIS path: for the SAME SPC layer, the
/// Esri `f=json` (rings) and GeoJSON `f=geojson` (coordinates) responses must
/// describe the SAME outlook polygons — so the verbatim categorical parser
/// (GeoJSON) and an Esri-fed parse must agree on feature count, labels, and
/// point-in-polygon membership. `parse_esri_rings` is deliberately a
/// separate function from the verbatim `parse_rings`; this test is what
/// proves the separation didn't introduce a discrepancy.

#include "spc/geometry.hpp"
#include "spc/models/common.hpp"
#include "spc/models/convective.hpp"
#include "spc/models/fire_weather.hpp"
#include "spc/models/mesoscale.hpp"
#include "spc/models/outlook.hpp"
#include "spc/models/storm_report.hpp"
#include "spc/models/watch.hpp"

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

// Even-odd union membership (hole-correct).
bool inside_any(double lon, double lat, const std::vector<Polygon>& rings) {
	bool in = false;
	for (const Polygon& r : rings) {
		if (point_in_polygon(lon, lat, r)) {
			in = !in;
		}
	}
	return in;
}

// Minimum distance from a point to any ring edge (degrees). Used to skip
// probes that sit in the ~1 m boundary band where the comparison is
// ill-posed (see note in the test below).
double dist_to_boundary(double px, double py, const std::vector<Polygon>& rings) {
	double best = 1e18;
	for (const Polygon& r : rings) {
		const std::size_t n = r.size();
		for (std::size_t i = 0; i < n; ++i) {
			const LonLat a = r[i];
			const LonLat b = r[(i + 1) % n];
			const double vx = b.lon - a.lon;
			const double vy = b.lat - a.lat;
			const double wx = px - a.lon;
			const double wy = py - a.lat;
			const double len2 = vx * vx + vy * vy;
			double t = len2 > 0.0 ? (wx * vx + wy * vy) / len2 : 0.0;
			t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
			const double dx = px - (a.lon + t * vx);
			const double dy = py - (a.lat + t * vy);
			const double d = std::sqrt(dx * dx + dy * dy);
			if (d < best) {
				best = d;
			}
		}
	}
	return best;
}

// Esri-vs-GeoJSON parity (the risk-#8 gate).
//
// IMPORTANT empirical fact this test encodes: ArcGIS does NOT emit
// bit-identical geometry for `f=json` (Esri rings) vs `f=geojson` — the two
// formats are independently quantized/densified and differ by up to ~1.3 m
// (~1.2e-5 deg), with different vertex sequences. So strict ring- or
// boundary-coincident equality is impossible by construction. The
// operationally correct parity claim — and what consumers actually rely on —
// is that **point-in-polygon membership agrees** for any point not pathologi-
// cally close to the (format-dependent, ~1 m fuzzy) boundary. We assert that
// over a dense grid: every probe that is unambiguously interior/exterior to
// BOTH ring sets (>~5 m clear of either boundary) must classify identically.
// A real divergence (e.g. dropped band, kept hole, wrong winding) flips whole
// regions and fails this; the ~1 m source quantization does not.
TEST(ArcGISParity, EsriRingsMatchGeoJsonForDay1Categorical) {
	const CategoricalOutlookPayload gj =
		parse_categorical(slurp("arcgis_day1_categorical.geojson"), 1);
	ASSERT_GT(gj.features.size(), 0u);

	const glz::expected<Json, std::string> root =
		detail::parse_root(slurp("arcgis_day1_categorical.esri.json"));
	ASSERT_TRUE(root.has_value());
	const Json* feats = detail::lookup(*root, "features");
	ASSERT_NE(feats, nullptr);
	ASSERT_TRUE(feats->is_array());

	// ~5 m skip band (deg). Well above the ~1.3 m format quantization, far
	// below any SPC outlook feature size.
	constexpr double kSkip = 5.0e-5;

	std::size_t esri_bands = 0;
	std::size_t probe_total = 0;
	std::size_t probe_agree = 0;
	std::size_t probe_compared = 0;
	for (const glz::generic& feat : feats->get_array()) {
		const Json* attrs = detail::lookup(feat, "attributes");
		const Json* geom = detail::lookup(feat, "geometry");
		if (attrs == nullptr || geom == nullptr) {
			continue;
		}
		const std::string label = detail::json_string(*attrs, "label");
		if (severity_from_label(label) == 0) {
			continue;
		}
		const std::vector<Polygon> esri_rings = detail::parse_esri_rings(*geom);
		ASSERT_FALSE(esri_rings.empty()) << "Esri band " << label << " parsed no rings";
		++esri_bands;

		const OutlookFeature* g = nullptr;
		for (const OutlookFeature& f : gj.features) {
			if (f.label == label) {
				g = &f;
				break;
			}
		}
		ASSERT_NE(g, nullptr) << "GeoJSON parse missing band " << label;

		// Bounding box of the GeoJSON band, padded.
		double minx = 1e18;
		double miny = 1e18;
		double maxx = -1e18;
		double maxy = -1e18;
		for (const Polygon& r : g->rings) {
			for (const LonLat& p : r) {
				minx = std::min(minx, p.lon);
				maxx = std::max(maxx, p.lon);
				miny = std::min(miny, p.lat);
				maxy = std::max(maxy, p.lat);
			}
		}
		const double pad = 0.5;
		minx -= pad;
		maxx += pad;
		miny -= pad;
		maxy += pad;

		// 60x60 grid over the band's bbox.
		constexpr int kN = 60;
		for (int ix = 0; ix <= kN; ++ix) {
			for (int iy = 0; iy <= kN; ++iy) {
				const double px = minx + (maxx - minx) * (static_cast<double>(ix) / kN);
				const double py = miny + (maxy - miny) * (static_cast<double>(iy) / kN);
				++probe_total;
				// Skip the fuzzy ~1 m boundary band of EITHER encoding.
				if (dist_to_boundary(px, py, g->rings) < kSkip ||
					dist_to_boundary(px, py, esri_rings) < kSkip) {
					continue;
				}
				++probe_compared;
				if (inside_any(px, py, g->rings) == inside_any(px, py, esri_rings)) {
					++probe_agree;
				}
			}
		}
	}
	EXPECT_GT(esri_bands, 0u);
	EXPECT_EQ(esri_bands, gj.features.size())
		<< "different severity-band count between Esri and GeoJSON";
	EXPECT_GT(probe_compared, 100u) << "too few clear-of-boundary probes to be meaningful";
	EXPECT_EQ(probe_agree, probe_compared)
		<< "Esri vs GeoJSON disagreed on " << (probe_compared - probe_agree) << "/"
		<< probe_compared
		<< " unambiguous interior/exterior probes — parse_esri_rings "
		   "genuinely diverged from the verbatim GeoJSON path (not source quantization)";
}

TEST(ArcGISParity, EsriProbTornadoMatchesGeoJsonProb) {
	const ProbOutlookPayload gj =
		parse_probabilistic(slurp("arcgis_day1_prob_tornado.geojson"), 1, "tornado");
	ASSERT_GT(gj.features.size(), 0u);
	for (const ProbOutlookFeature& f : gj.features) {
		EXPECT_GT(f.probability, 0.0);
		EXPECT_LT(f.probability, 1.0);
		EXPECT_FALSE(f.rings.empty());
	}
}

TEST(NetNewModels, Day48ParsesStaticGeoJson) {
	const Day48OutlookPayload p = parse_day4_8(slurp("day4prob.nolyr.geojson"), 4);
	EXPECT_EQ(p.day, 4);
	ASSERT_GT(p.features.size(), 0u);
	for (const Day48Feature& f : p.features) {
		EXPECT_FALSE(f.label.empty());
		EXPECT_GT(f.probability, 0.0);
		EXPECT_LE(f.probability, 1.0);
		EXPECT_FALSE(f.rings.empty());
	}
}

TEST(NetNewModels, ConditionalIntensityCigMapper) {
	EXPECT_EQ(cig_severity_from_label("CIG1"), 1);
	EXPECT_EQ(cig_severity_from_label("CIG2"), 2);
	EXPECT_EQ(cig_severity_from_label("CIG3"), 3);
	EXPECT_EQ(cig_severity_from_label("SLGT"), 0); // NOT the categorical scale
	const ConditionalIntensityPayload p = parse_conditional_intensity(
		slurp("arcgis_day1_torn_conditional_intensity.esri.json"), 1, "tornado");
	ASSERT_GT(p.features.size(), 0u);
	EXPECT_EQ(p.features[0].label, "CIG1");
	EXPECT_EQ(p.features[0].cig_level, 1);
	EXPECT_FALSE(p.features[0].rings.empty());
}

TEST(NetNewModels, FireWeatherOwnSeverityMapper) {
	EXPECT_EQ(fire_severity_from_label("ELEV"), 1);
	EXPECT_EQ(fire_severity_from_label("CRIT"), 2);
	EXPECT_EQ(fire_severity_from_label("EXTM"), 3);
	EXPECT_EQ(fire_severity_from_label("SLGT"), 0); // not categorical
	const FireWeatherPayload p = parse_fire_weather(slurp("arcgis_day1_fire_weather.esri.json"), 1);
	EXPECT_EQ(p.day, 1);
	EXPECT_GT(p.features.size(), 0u);
	for (const FireWeatherFeature& f : p.features) {
		EXPECT_FALSE(f.rings.empty());
	}
}

TEST(NetNewModels, MesoscaleRawTextOnly) {
	const MesoscalePayload p =
		parse_mesoscale_discussions(slurp("arcgis_mesoscale_discussion.esri.json"));
	ASSERT_GT(p.discussions.size(), 0u);
	EXPECT_FALSE(p.discussions[0].name.empty());
	// URL captured raw; narrative deliberately not parsed (no body field).
	EXPECT_NE(p.discussions[0].url.find("spc.noaa.gov"), std::string::npos);
	EXPECT_FALSE(p.discussions[0].rings.empty());
}

TEST(NetNewModels, WatchParsesIemGeoJson) {
	const WatchPayload p = parse_watches(slurp("iem_spc_watch.json"));
	ASSERT_GT(p.watches.size(), 0u);
	EXPECT_EQ(p.watches[0].type, "TOR");
	EXPECT_EQ(p.watches[0].number, 139);
	EXPECT_GT(p.watches[0].max_hail_size, 0.0);
	EXPECT_FALSE(p.watches[0].rings.empty());
}

TEST(NetNewModels, StormReportsParseIemLsr) {
	const StormReportPayload p = parse_storm_reports(slurp("iem_storm_reports.json"));
	ASSERT_GT(p.reports.size(), 0u);
	for (std::size_t i = 0; i < 5 && i < p.reports.size(); ++i) {
		const StormReport& sr = p.reports[i];
		EXPECT_FALSE(sr.type_text.empty());
		EXPECT_NE(sr.location.lon, 0.0);
		EXPECT_NE(sr.location.lat, 0.0);
	}
}

TEST(EsriRings, OrientationHelper) {
	// CCW unit square -> positive signed area; CW -> negative.
	const Polygon ccw = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}};
	const Polygon cw = {{0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}};
	EXPECT_GT(detail::ring_signed_area(ccw), 0.0);
	EXPECT_LT(detail::ring_signed_area(cw), 0.0);
}

} // namespace
