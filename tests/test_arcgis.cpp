/// @file test_arcgis.cpp
/// @brief Esri-vs-GeoJSON parity over the captured fixtures, and the product
/// parsers.
///
/// ArcGIS serves the same layer as Esri rings (f=json) and GeoJSON
/// (f=geojson). `parse_esri_rings` must describe the same areas as the
/// spc-data GeoJSON walker, so these tests compare point membership.

#include "models/json.hpp"
#include "spc/geometry.hpp"
#include "spc/models/convective.hpp"
#include "spc/models/fire_weather.hpp"
#include "spc/models/mesoscale.hpp"
#include "spc/models/outlook.hpp"
#include "spc/models/storm_report.hpp"
#include "spc/models/watch.hpp"
#include "support/fixtures.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <gtest/gtest.h>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace spc;
using detail::Json;
using test::read_fixture;

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

// Distance in degrees from a point to the nearest ring edge.
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

// ArcGIS quantizes the two formats separately, so their vertices differ by up
// to ~1.3 m and exact ring equality is impossible. Membership is what callers
// rely on: every grid point more than ~5 m from either boundary must
// classify the same way. A dropped band, a kept hole, or a wrong winding
// flips whole regions and fails this.
TEST(ArcGISParity, EsriRingsMatchGeoJsonForDay1Categorical) {
	const CategoricalOutlookPayload gj =
		parse_categorical(read_fixture("arcgis_day1_categorical.geojson"), 1);
	ASSERT_GT(gj.features.size(), 0u);

	const glz::expected<Json, std::string> root =
		detail::parse_root(read_fixture("arcgis_day1_categorical.esri.json"));
	ASSERT_TRUE(root.has_value());
	const Json* feats = detail::lookup(*root, "features");
	ASSERT_NE(feats, nullptr);
	ASSERT_TRUE(feats->is_array());

	// ~5 m skip band (deg). Well above the ~1.3 m format quantization, far
	// below any SPC outlook feature size.
	constexpr double kSkip = 5.0e-5;

	std::size_t esri_bands = 0;
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
		<< probe_compared << " probes clear of the boundary";
}

// Grid-probe membership agreement between two ring sets, on the same terms as
// the day 1 categorical test above.
struct MembershipProbe {
	std::size_t compared = 0;
	std::size_t agreed = 0;
};

void probe_membership(const std::vector<Polygon>& reference, const std::vector<Polygon>& candidate,
					  MembershipProbe& probe) {
	constexpr double kSkip = 5.0e-5;
	constexpr int kN = 40;

	double minx = 1e18;
	double miny = 1e18;
	double maxx = -1e18;
	double maxy = -1e18;
	for (const Polygon& r : reference) {
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

	for (int ix = 0; ix <= kN; ++ix) {
		for (int iy = 0; iy <= kN; ++iy) {
			const double px = minx + (maxx - minx) * (static_cast<double>(ix) / kN);
			const double py = miny + (maxy - miny) * (static_cast<double>(iy) / kN);
			if (dist_to_boundary(px, py, reference) < kSkip ||
				dist_to_boundary(px, py, candidate) < kSkip) {
				continue;
			}
			++probe.compared;
			if (inside_any(px, py, reference) == inside_any(px, py, candidate)) {
				++probe.agreed;
			}
		}
	}
}

TEST(ArcGISParity, EveryCapturedEsriFixtureMatchesItsGeoJsonTwin) {
	struct Pair {
		std::string stem;
		std::int32_t day;
		std::string hazard; ///< empty for the categorical layers
	};
	const std::vector<Pair> pairs = {
		{"arcgis_day1_categorical", 1, ""},	  {"arcgis_day2_categorical", 2, ""},
		{"arcgis_day3_categorical", 3, ""},	  {"arcgis_day1_prob_tornado", 1, "tornado"},
		{"arcgis_day1_prob_hail", 1, "hail"}, {"arcgis_day1_prob_wind", 1, "wind"},
		{"arcgis_day2_prob_wind", 2, "wind"},
	};

	for (const Pair& pair : pairs) {
		// Label -> rings, from the GeoJSON walker.
		std::vector<std::pair<std::string, std::vector<Polygon>>> reference;
		if (pair.hazard.empty()) {
			const CategoricalOutlookPayload gj =
				parse_categorical(read_fixture(pair.stem + ".geojson"), pair.day);
			for (const OutlookFeature& f : gj.features) {
				reference.emplace_back(f.label, f.rings);
			}
		} else {
			const ProbOutlookPayload gj =
				parse_probabilistic(read_fixture(pair.stem + ".geojson"), pair.day, pair.hazard);
			for (const ProbOutlookFeature& f : gj.features) {
				EXPECT_GT(f.probability, 0.0) << pair.stem;
				EXPECT_LT(f.probability, 1.0) << pair.stem;
				// Isopleths have no label; the probability identifies the band.
				reference.emplace_back(std::format("{:.6f}", f.probability), f.rings);
			}
		}
		ASSERT_GT(reference.size(), 0u) << pair.stem;

		const glz::expected<Json, std::string> root =
			detail::parse_root(read_fixture(pair.stem + ".esri.json"));
		ASSERT_TRUE(root.has_value()) << pair.stem;
		const Json* feats = detail::lookup(*root, "features");
		ASSERT_NE(feats, nullptr) << pair.stem;
		ASSERT_TRUE(feats->is_array()) << pair.stem;

		MembershipProbe probe;
		std::size_t matched = 0;
		for (const glz::generic& feat : feats->get_array()) {
			const Json* attrs = detail::lookup(feat, "attributes");
			const Json* geom = detail::lookup(feat, "geometry");
			if (attrs == nullptr || geom == nullptr) {
				continue;
			}
			const std::string label =
				pair.hazard.empty() ? detail::json_string(*attrs, "label")
									: std::format("{:.6f}", detail::normalized_probability(*attrs));
			const std::vector<Polygon>* expected = nullptr;
			for (const std::pair<std::string, std::vector<Polygon>>& band : reference) {
				if (band.first == label) {
					expected = &band.second;
					break;
				}
			}
			if (expected == nullptr) {
				continue;
			}
			const std::vector<Polygon> esri_rings = detail::parse_esri_rings(*geom);
			ASSERT_FALSE(esri_rings.empty()) << pair.stem << " band " << label;
			++matched;
			probe_membership(*expected, esri_rings, probe);
		}

		EXPECT_EQ(matched, reference.size())
			<< pair.stem << ": Esri and GeoJSON disagree on the band set";
		EXPECT_GT(probe.compared, 100u) << pair.stem << ": too few clear-of-boundary probes";
		EXPECT_EQ(probe.agreed, probe.compared)
			<< pair.stem << ": Esri vs GeoJSON disagreed on " << (probe.compared - probe.agreed)
			<< "/" << probe.compared << " unambiguous interior/exterior probes";
	}
}

TEST(Models, Day48ParsesStaticGeoJson) {
	const Day48OutlookPayload p = parse_day4_8(read_fixture("day4prob.nolyr.geojson"), 4);
	EXPECT_EQ(p.day, 4);
	ASSERT_GT(p.features.size(), 0u);
	for (const Day48Feature& f : p.features) {
		EXPECT_FALSE(f.label.empty());
		EXPECT_GT(f.probability, 0.0);
		EXPECT_LE(f.probability, 1.0);
		EXPECT_FALSE(f.rings.empty());
	}
}

TEST(Models, Day48ParsesNonemptySyntheticArcGisResponse) {
	const Day48OutlookPayload payload =
		parse_day4_8(read_fixture("arcgis_day4_8_nonempty.synthetic.json"), 4);
	ASSERT_EQ(payload.features.size(), 1u);
	EXPECT_EQ(payload.features[0].day, 4);
	EXPECT_DOUBLE_EQ(payload.features[0].probability, 0.15);
	EXPECT_FALSE(payload.features[0].rings.empty());
}

TEST(Models, ConditionalIntensityCigMapper) {
	EXPECT_EQ(cig_severity_from_label("CIG1"), 1);
	EXPECT_EQ(cig_severity_from_label("CIG2"), 2);
	EXPECT_EQ(cig_severity_from_label("CIG3"), 3);
	EXPECT_EQ(cig_severity_from_label("SLGT"), 0); // NOT the categorical scale
	const ConditionalIntensityPayload p = parse_conditional_intensity(
		read_fixture("arcgis_day1_torn_conditional_intensity.esri.json"), 1, "tornado");
	ASSERT_GT(p.features.size(), 0u);
	EXPECT_EQ(p.features[0].label, "CIG1");
	EXPECT_EQ(p.features[0].cig_level, 1);
	EXPECT_FALSE(p.features[0].rings.empty());
}

TEST(Models, FireWeatherOwnSeverityMapper) {
	EXPECT_EQ(fire_severity_from_label("ELEV"), 1);
	EXPECT_EQ(fire_severity_from_label("CRIT"), 2);
	EXPECT_EQ(fire_severity_from_label("EXTM"), 3);
	EXPECT_EQ(fire_severity_from_label("SLGT"), 0); // not categorical
	const FireWeatherPayload p = parse_fire_weather(
		read_fixture("arcgis_day1_fire_weather.esri.json"), 1, FireWeatherLayer::Outlook);
	EXPECT_EQ(p.day, 1);
	ASSERT_EQ(p.features.size(), 3u);
	EXPECT_EQ(p.features[0].label, "ELEV");
	EXPECT_EQ(p.features[0].severity, 1);
	EXPECT_EQ(p.features[1].label, "CRIT");
	EXPECT_EQ(p.features[1].severity, 2);
	EXPECT_EQ(p.features[2].label, "EXTM");
	EXPECT_EQ(p.features[2].severity, 3);
	for (const FireWeatherFeature& f : p.features) {
		EXPECT_FALSE(f.rings.empty());
	}
}

TEST(Models, FireWeatherLayerIsTheOnlyThingThatDisambiguatesDayOneAndTwoDn) {
	// Days 1 and 2 carry only dn (5/8/10), which both layers use with
	// different meanings, so the caller has to name the layer.
	for (const std::string& name : {std::string{"arcgis_day1_fire_weather.esri.json"},
									std::string{"arcgis_day2_fire_weather.esri.json"}}) {
		const std::string body = read_fixture(name);
		EXPECT_EQ(body.find("LABEL"), std::string::npos) << name;
		EXPECT_EQ(body.find("\"label\""), std::string::npos) << name;

		const std::int32_t day = name.find("day1") != std::string::npos ? 1 : 2;
		const FireWeatherPayload outlook = parse_fire_weather(body, day, FireWeatherLayer::Outlook);
		const FireWeatherPayload dry =
			parse_fire_weather(body, day, FireWeatherLayer::DryThunderstorm);

		ASSERT_EQ(outlook.features.size(), 3u) << name;
		ASSERT_EQ(dry.features.size(), 3u) << name;
		EXPECT_EQ(outlook.features[0].label, "ELEV") << name;
		EXPECT_EQ(outlook.features[1].label, "CRIT") << name;
		EXPECT_EQ(outlook.features[2].label, "EXTM") << name;
		EXPECT_EQ(dry.features[0].label, "IDRT") << name;
		EXPECT_EQ(dry.features[1].label, "SDRT") << name;
		for (const FireWeatherFeature& f : dry.features) {
			EXPECT_EQ(f.layer, FireWeatherLayer::DryThunderstorm) << name;
			EXPECT_EQ(f.severity, 0) << name;
			EXPECT_NE(f.label, "ELEV") << name;
			EXPECT_NE(f.label, "CRIT") << name;
			EXPECT_NE(f.label, "EXTM") << name;
		}
	}
}

TEST(Models, FireWeatherDryThunderstormCodesUseTheirOwnLabels) {
	const std::string body = R"({"features":[
		{"attributes":{"dn":5},"geometry":{"rings":[[[0,1],[1,1],[1,0],[0,0],[0,1]]]}},
		{"attributes":{"dn":8},"geometry":{"rings":[[[2,1],[3,1],[3,0],[2,0],[2,1]]]}}
	]})";
	const FireWeatherPayload payload =
		parse_fire_weather(body, 1, FireWeatherLayer::DryThunderstorm);
	ASSERT_EQ(payload.features.size(), 2u);
	EXPECT_EQ(payload.features[0].label, "IDRT");
	EXPECT_EQ(payload.features[0].layer, FireWeatherLayer::DryThunderstorm);
	EXPECT_EQ(payload.features[0].severity, 0);
	EXPECT_EQ(payload.features[1].label, "SDRT");
	EXPECT_EQ(payload.features[1].layer, FireWeatherLayer::DryThunderstorm);
	EXPECT_EQ(payload.features[1].severity, 0);
}

TEST(Models, FireWeatherOmitsNoRiskSentinelPolygons) {
	const std::string body = R"({"features":[
		{"attributes":{"dn":0},"geometry":{"rings":[[[0,1],[1,1],[1,0],[0,0],[0,1]]]}},
		{"attributes":{"LABEL":"Probability Too Low"},"geometry":{"rings":[[[2,1],[3,1],[3,0],[2,0],[2,1]]]}},
		{"attributes":{"dn":5},"geometry":{"rings":[[[4,1],[5,1],[5,0],[4,0],[4,1]]]}}
	]})";

	const FireWeatherPayload payload =
		parse_fire_weather(body, 4, FireWeatherLayer::DryThunderstorm);

	ASSERT_EQ(payload.features.size(), 1u);
	EXPECT_TRUE(payload.features[0].label.empty());
	EXPECT_DOUBLE_EQ(payload.features[0].probability, 0.05);
	EXPECT_EQ(payload.features[0].severity, 0);
}

TEST(Models, ExtendedFireWeatherNormalizesPublishedProbabilities) {
	const std::string body = R"({"features":[
		{"attributes":{"label":"0.40","dn":40},"geometry":{"rings":[[[0,1],[1,1],[1,0],[0,0],[0,1]]]}},
		{"attributes":{"dn":"15"},"geometry":{"rings":[[[2,1],[3,1],[3,0],[2,0],[2,1]]]}},
		{"attributes":{"dn":150},"geometry":{"rings":[[[4,1],[5,1],[5,0],[4,0],[4,1]]]}},
		{"attributes":{"dn":-5},"geometry":{"rings":[[[6,1],[7,1],[7,0],[6,0],[6,1]]]}}
	]})";

	const FireWeatherPayload payload =
		parse_fire_weather(body, 5, FireWeatherLayer::WindLowHumidity);

	ASSERT_EQ(payload.features.size(), 2u);
	EXPECT_EQ(payload.features[0].label, "0.40");
	EXPECT_DOUBLE_EQ(payload.features[0].probability, 0.40);
	EXPECT_EQ(payload.features[0].severity, 0);
	EXPECT_TRUE(payload.features[1].label.empty());
	EXPECT_DOUBLE_EQ(payload.features[1].probability, 0.15);
	EXPECT_EQ(payload.features[1].severity, 0);
}

TEST(Models, MesoscaleRawTextOnly) {
	const MesoscalePayload p =
		parse_mesoscale_discussions(read_fixture("arcgis_mesoscale_discussion.esri.json"));
	ASSERT_GT(p.discussions.size(), 0u);
	EXPECT_FALSE(p.discussions[0].name.empty());
	EXPECT_NE(p.discussions[0].url.find("spc.noaa.gov"), std::string::npos);
	EXPECT_FALSE(p.discussions[0].rings.empty());
}

TEST(Models, MesoscaleSkipsTheNoAreaPlaceholder) {
	// Captured live with no active discussion: one "NoArea" feature with a
	// tiny ring and null links.
	const std::string body = read_fixture("arcgis_mesoscale_discussion_noarea.esri.json");
	ASSERT_NE(body.find("NoArea"), std::string::npos);

	const MesoscalePayload p = parse_mesoscale_discussions(body);

	EXPECT_TRUE(p.discussions.empty());
}

TEST(Models, WatchParsesIemGeoJson) {
	const WatchPayload p = parse_watches(read_fixture("iem_spc_watch.json"));
	ASSERT_GT(p.watches.size(), 0u);
	EXPECT_EQ(p.watches[0].type, "TOR");
	EXPECT_EQ(p.watches[0].number, 139);
	EXPECT_EQ(p.watches[0].year, 2024);
	EXPECT_EQ(p.watches[0].issued_at, "2024-04-26T15:10:00Z");
	EXPECT_GT(p.watches[0].max_hail_size, 0.0);
	EXPECT_FALSE(p.watches[0].rings.empty());
}

TEST(Models, WatchNumbersThatAreNotFiniteIntegersBecomeZero) {
	// A plain cast of inf or 1e300 to int32 is undefined behavior.
	const WatchPayload p = parse_watches(R"({"features":[
		{"properties":{"number":"inf","year":1e300}},
		{"properties":{"number":"nan","year":-5e20}}
	]})");
	ASSERT_EQ(p.watches.size(), 2u);
	for (const Watch& w : p.watches) {
		EXPECT_EQ(w.number, 0);
		EXPECT_EQ(w.year, 0);
	}
}

TEST(Models, StormReportsParseIemLsr) {
	const StormReportPayload p = parse_storm_reports(read_fixture("iem_storm_reports.json"));
	ASSERT_GT(p.reports.size(), 0u);
	for (std::size_t i = 0; i < 5 && i < p.reports.size(); ++i) {
		const StormReport& sr = p.reports[i];
		EXPECT_FALSE(sr.type_text.empty());
		EXPECT_NE(sr.location.lon, 0.0);
		EXPECT_NE(sr.location.lat, 0.0);
		EXPECT_EQ(sr.wfo.size(), 3u);
	}
}

TEST(JsonHelpers, ToInt32RejectsValuesACastCouldNotRepresent) {
	EXPECT_EQ(detail::to_int32(139.0), 139);
	EXPECT_EQ(detail::to_int32(-7.9), -7);
	EXPECT_EQ(detail::to_int32(std::numeric_limits<double>::infinity()), std::nullopt);
	EXPECT_EQ(detail::to_int32(std::numeric_limits<double>::quiet_NaN()), std::nullopt);
	EXPECT_EQ(detail::to_int32(3e9), std::nullopt);
	EXPECT_EQ(detail::to_int32(-3e9), std::nullopt);
}

TEST(EsriRings, OrientationHelper) {
	// CCW unit square -> positive signed area; CW -> negative.
	const Polygon ccw = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}};
	const Polygon cw = {{0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}};
	EXPECT_GT(detail::ring_signed_area(ccw), 0.0);
	EXPECT_LT(detail::ring_signed_area(cw), 0.0);
}

} // namespace
