/// @file types.hpp
/// @brief Plain data types for SPC outlook ingestion.
///
/// EXTRACTED VERBATIM from spc-data/src/types.hpp (Workstream C). The only
/// change is the namespace (`predictioncast::spc_data` -> `spc`). These are
/// contract-stable shapes; downstream byte-identity depends on them being
/// unchanged.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace spc {

/// A (lon, lat) pair in GeoJSON / WGS84 order.
struct LonLat {
	double lon = 0.0;
	double lat = 0.0;
};

/// One polygon (outer ring; SPC outlooks are single-ring polygons in practice).
using Polygon = std::vector<LonLat>;

/// One feature from a day-N categorical outlook GeoJSON.
/// Classification values: MRGL (1), SLGT (2), ENH (3), MDT (4), HIGH (5).
struct OutlookFeature {
	std::string label;			///< raw label from GeoJSON, e.g. "MRGL"
	std::uint8_t severity = 0;	///< 1..5; 0 if unrecognized
	std::vector<Polygon> rings; ///< all polygons in the MultiPolygon
	std::string issued_at;		///< ISO 8601
	std::string valid_from;
	std::string valid_until;
};

/// One probabilistic outlook feature (tornado/hail/wind percentage isopleth).
struct ProbOutlookFeature {
	std::string hazard;		  ///< "tornado" | "hail" | "wind"
	double probability = 0.0; ///< [0, 1]
	std::vector<Polygon> rings;
	std::string issued_at;
	std::string valid_from;
	std::string valid_until;
};

/// Parsed view of a single-day categorical outlook GeoJSON file.
struct CategoricalOutlookPayload {
	std::int32_t day_offset = 0; ///< 1..8
	std::vector<OutlookFeature> features;
};

/// Parsed view of a single-day probabilistic outlook GeoJSON file.
struct ProbOutlookPayload {
	std::int32_t day_offset = 0;
	std::string hazard;
	std::vector<ProbOutlookFeature> features;
};

} // namespace spc
