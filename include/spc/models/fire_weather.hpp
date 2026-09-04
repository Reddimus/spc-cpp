/// @file fire_weather.hpp
/// @brief Net-new SPC fire-weather outlook model.
///
/// Independent of the convective path. Fire-weather uses its OWN label set
/// (elevated / critical / extremely critical, plus dry-thunderstorm) — it
/// does NOT reuse `severity_from_label`.

#pragma once

#include "spc/types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace spc {

enum class FireWeatherLayer : std::uint8_t {
	Outlook,
	DryThunderstorm,
	WindLowHumidity,
};

struct FireWeatherFeature {
	std::int32_t day = 0;
	FireWeatherLayer layer{FireWeatherLayer::Outlook};
	std::string label;		   ///< raw, e.g. "ELEV", "CRIT", "EXTM", "IDRT", "SDRT"
	std::uint8_t severity = 0; ///< product-specific 1..3 (0 if unknown)
	double probability = 0.0;  ///< day 3..8 probability normalized to [0,1]
	std::vector<Polygon> rings;
	std::string issued_at;
	std::string valid_from;
	std::string valid_until;
};

struct FireWeatherPayload {
	std::int32_t day = 0;
	std::vector<FireWeatherFeature> features;
};

/// Fire-weather severity, PRODUCT-SPECIFIC and intentionally separate from
/// the categorical scale: ELEV=1, CRIT=2, EXTM=3. Dry-thunderstorm bands
/// (IDRT/SDRT) and anything else map to 0 (kept by label, no severity).
[[nodiscard]] std::uint8_t fire_severity_from_label(std::string_view label) noexcept;

/// Parse one known ArcGIS fire-weather layer (Esri or GeoJSON body). Throws
/// std::runtime_error on malformed JSON.
///
/// `layer` is required and is not inferable from the body: NOAA's day-1 and
/// day-2 payloads carry no LABEL at all, only a numeric `dn` band index that
/// the outlook (5=ELEV, 8=CRIT, 10=EXTM) and dry-thunderstorm (5=IDRT,
/// 8=SDRT) products both use with different meanings. Day 3 through 8 decode
/// LABEL/label/dn as probabilities.
[[nodiscard]] FireWeatherPayload parse_fire_weather(std::string_view body, std::int32_t day,
													FireWeatherLayer layer);

} // namespace spc
