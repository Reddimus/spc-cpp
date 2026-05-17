/// @file geometry.hpp
/// @brief Minimal point-in-polygon helpers for SPC outlook geometry.
///
/// EXTRACTED VERBATIM from spc-data/src/geometry.hpp (Workstream C); only
/// the namespace changed (`predictioncast::spc_data` -> `spc`).

#pragma once

#include "spc/types.hpp"

namespace spc {

/// Ray-casting point-in-polygon test (WGS84 lat/lon, planar approximation).
/// Accurate enough for CONUS-sized SPC outlook polygons.
[[nodiscard]] bool point_in_polygon(double lon, double lat, const Polygon& poly) noexcept;

/// True if (lon, lat) lies inside any ring of the feature.
[[nodiscard]] bool point_in_feature(double lon, double lat, const OutlookFeature& f) noexcept;
[[nodiscard]] bool point_in_feature(double lon, double lat, const ProbOutlookFeature& f) noexcept;

} // namespace spc
