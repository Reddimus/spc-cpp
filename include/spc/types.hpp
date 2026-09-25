/// @file types.hpp
/// @brief Day 1-3 convective outlook types. Their shape matches the internal
/// spc-data service.

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

/// One polygon's outer ring. Holes are not kept.
using Polygon = std::vector<LonLat>;

/// One categorical outlook band.
struct OutlookFeature {
	std::string label;			///< raw label, e.g. "MRGL"
	std::uint8_t severity = 0;	///< TSTM/MRGL 1, SLGT 2, ENH 3, MDT 4, HIGH 5; 0 if unknown
	std::vector<Polygon> rings; ///< every polygon in the band
	std::string issued_at;		///< ISO 8601
	std::string valid_from;		///< ISO 8601
	std::string valid_until;	///< ISO 8601
};

/// One probabilistic isopleth (tornado, hail, wind, or day 3 severe).
struct ProbOutlookFeature {
	std::string hazard;		  ///< "tornado", "hail", "wind", or "severe"
	double probability = 0.0; ///< [0, 1]
	std::vector<Polygon> rings;
	std::string issued_at;
	std::string valid_from;
	std::string valid_until;
};

/// A day's categorical outlook.
struct CategoricalOutlookPayload {
	std::int32_t day_offset = 0; ///< 1..3
	std::vector<OutlookFeature> features;
};

/// A day's probabilistic outlook for one hazard.
struct ProbOutlookPayload {
	std::int32_t day_offset = 0;
	std::string hazard;
	std::vector<ProbOutlookFeature> features;
};

} // namespace spc
