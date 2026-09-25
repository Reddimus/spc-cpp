/// @file fire_weather.hpp
/// @brief Fire-weather outlooks.

#pragma once

#include "spc/types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace spc {

/// The NOAA layer a fire-weather feature came from.
enum class FireWeatherLayer : std::uint8_t {
	Outlook,		 ///< days 1-2: elevated, critical, extremely critical
	DryThunderstorm, ///< days 1-8
	WindLowHumidity, ///< days 3-8
};

struct FireWeatherFeature {
	std::int32_t day = 0;
	FireWeatherLayer layer{FireWeatherLayer::Outlook};
	std::string label;		   ///< e.g. "ELEV", "CRIT", "EXTM", "IDRT", "SDRT"
	std::uint8_t severity = 0; ///< ELEV 1, CRIT 2, EXTM 3; 0 otherwise
	double probability = 0.0;  ///< days 3-8, normalized to [0, 1]
	std::vector<Polygon> rings;
	std::string issued_at;
	std::string valid_from;
	std::string valid_until;
};

struct FireWeatherPayload {
	std::int32_t day = 0;
	std::vector<FireWeatherFeature> features;
};

/// ELEV 1, CRIT 2, EXTM 3. Dry-thunderstorm labels and anything else map to 0.
[[nodiscard]] std::uint8_t fire_severity_from_label(std::string_view label) noexcept;

/// Parse one fire-weather layer (Esri or GeoJSON). No-risk polygons are
/// skipped.
///
/// `layer` is required because days 1 and 2 carry only a numeric `dn` band,
/// and the outlook (5 ELEV, 8 CRIT, 10 EXTM) and dry-thunderstorm (5 IDRT,
/// 8 SDRT) layers reuse the same numbers. Days 3-8 report probabilities.
/// @throws std::runtime_error if `body` is not valid JSON.
[[nodiscard]] FireWeatherPayload parse_fire_weather(std::string_view body, std::int32_t day,
													FireWeatherLayer layer);

} // namespace spc
