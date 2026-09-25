/// @file convective.hpp
/// @brief Day 4-8 outlooks and conditional intensity.

#pragma once

#include "spc/types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace spc {

/// One Day 4-8 severe-weather probability area.
struct Day48Feature {
	std::int32_t day = 0;	  ///< 4..8
	std::string label;		  ///< raw, e.g. "0.15"
	double probability = 0.0; ///< [0, 1]
	std::vector<Polygon> rings;
	std::string issued_at;
	std::string valid_from;
	std::string valid_until;
};

struct Day48OutlookPayload {
	std::int32_t day = 0;
	std::vector<Day48Feature> features;
};

/// One conditional-intensity group ("CIG1".."CIG3").
struct ConditionalIntensityFeature {
	std::string label;			///< raw, e.g. "CIG1"
	std::uint8_t cig_level = 0; ///< 1..3; 0 if unknown
	std::vector<Polygon> rings;
	std::string issued_at;
	std::string valid_from;
	std::string valid_until;
};

struct ConditionalIntensityPayload {
	std::int32_t day = 0;
	std::string hazard; ///< "tornado", "hail", "wind", or "severe"
	std::vector<ConditionalIntensityFeature> features;
};

/// "CIG1", "CIG2", "CIG3" to 1, 2, 3. Returns 0 for anything else.
[[nodiscard]] std::uint8_t cig_severity_from_label(std::string_view label) noexcept;

/// Parse a Day 4-8 outlook from the static GeoJSON feed or an ArcGIS Esri
/// response. Areas with no probability or no rings are skipped.
/// @throws std::runtime_error if `body` is not valid JSON.
[[nodiscard]] Day48OutlookPayload parse_day4_8(std::string_view body, std::int32_t day);

/// Parse a conditional-intensity layer (GeoJSON or Esri).
/// @throws std::runtime_error if `body` is not valid JSON.
[[nodiscard]] ConditionalIntensityPayload
parse_conditional_intensity(std::string_view body, std::int32_t day, std::string hazard);

} // namespace spc
