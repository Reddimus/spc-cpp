/// @file convective.hpp
/// @brief Net-new convective models: Day 4-8 outlook + conditional intensity.
///
/// These are ADDITIVE and independent of the verbatim Day 1-3
/// categorical/probabilistic path in outlook.hpp. They have their own
/// product-specific severity/label handling — they do NOT call
/// `severity_from_label` (which is the Day1-3 categorical label set only).

#pragma once

#include "spc/types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace spc {

/// One Day 4-8 probabilistic feature. SPC publishes a single "any severe"
/// percentage per day (no per-hazard split before Day 3). Label is the
/// fractional string ("0.15"); we keep it raw AND expose the [0,1] prob.
struct Day48Feature {
	std::int32_t day = 0; ///< 4..8
	std::string label;	  ///< raw, e.g. "0.15" or "30"
	double probability = 0.0;
	std::vector<Polygon> rings;
	std::string issued_at;
	std::string valid_from;
	std::string valid_until;
};

struct Day48OutlookPayload {
	std::int32_t day = 0;
	std::vector<Day48Feature> features;
};

/// Conditional-intensity "CIG{n}" groups (e.g. Day-1 Tornado Conditional
/// Intensity). Net-new label set — `cig_severity_from_label` is its own
/// mapper, deliberately separate from the categorical `severity_from_label`.
struct ConditionalIntensityFeature {
	std::string label;			///< raw, e.g. "CIG1"
	std::uint8_t cig_level = 0; ///< 1..3 parsed from CIG{n}; 0 if unknown
	std::vector<Polygon> rings;
	std::string issued_at;
	std::string valid_from;
	std::string valid_until;
};

struct ConditionalIntensityPayload {
	std::int32_t day = 0;
	std::string hazard; ///< "tornado" | "hail" | "wind" | "severe"
	std::vector<ConditionalIntensityFeature> features;
};

/// "CIG1"/"CIG2"/"CIG3" -> 1/2/3. Returns 0 for anything else. This is a
/// PRODUCT-SPECIFIC mapper — not the categorical severity scale.
[[nodiscard]] std::uint8_t cig_severity_from_label(std::string_view label) noexcept;

/// Parse a Day 4-8 outlook. Accepts both the static
/// `www.spc.noaa.gov/products/exper/day4-8/dayNprob.nolyr.geojson` GeoJSON
/// shape and an ArcGIS `f=json` Esri FeatureSet (auto-detected by the
/// presence of `geometry.rings` vs `geometry.coordinates`). Throws
/// std::runtime_error on malformed JSON (same contract as outlook.hpp).
[[nodiscard]] Day48OutlookPayload parse_day4_8(std::string_view body, std::int32_t day);

/// Parse a conditional-intensity layer (ArcGIS Esri or GeoJSON).
[[nodiscard]] ConditionalIntensityPayload
parse_conditional_intensity(std::string_view body, std::int32_t day, std::string hazard);

} // namespace spc
