/// @file geometry.hpp
/// @brief Point-in-polygon tests for parsed features.

#pragma once

#include "spc/types.hpp"

#include <algorithm>
#include <concepts>
#include <vector>

namespace spc {

/// Ray-casting point-in-polygon test on lon/lat treated as a plane. Accurate
/// enough for CONUS-sized SPC areas; points exactly on an edge may go either way.
[[nodiscard]] bool point_in_polygon(double lon, double lat, const Polygon& poly) noexcept;

/// Any parsed feature with `rings`: outlook bands, isopleths, fire-weather
/// areas, watches, mesoscale discussions, and so on.
template <typename Feature>
concept RingFeature = requires(const Feature& feature) {
	{ feature.rings } -> std::convertible_to<const std::vector<Polygon>&>;
};

/// True if (lon, lat) lies inside any ring of `feature`.
template <RingFeature Feature>
[[nodiscard]] bool point_in_feature(double lon, double lat, const Feature& feature) noexcept {
	return std::ranges::any_of(feature.rings, [lon, lat](const Polygon& ring) {
		return point_in_polygon(lon, lat, ring);
	});
}

} // namespace spc
