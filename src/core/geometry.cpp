/// @file geometry.cpp
///
/// EXTRACTED VERBATIM from spc-data/src/geometry.cpp (Workstream C); only
/// the namespace changed (`predictioncast::spc_data` -> `spc`). The ray-cast
/// algorithm is byte-for-byte the spc-data implementation.

#include "spc/geometry.hpp"

namespace spc {

bool point_in_polygon(double lon, double lat, const Polygon& poly) noexcept {
	bool inside = false;
	const std::size_t n = poly.size();
	for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
		const double xi = poly[i].lon;
		const double yi = poly[i].lat;
		const double xj = poly[j].lon;
		const double yj = poly[j].lat;
		const bool intersect =
			((yi > lat) != (yj > lat)) && (lon < (xj - xi) * (lat - yi) / (yj - yi) + xi);
		if (intersect) {
			inside = !inside;
		}
	}
	return inside;
}

bool point_in_feature(double lon, double lat, const OutlookFeature& f) noexcept {
	for (const Polygon& ring : f.rings) {
		if (point_in_polygon(lon, lat, ring)) {
			return true;
		}
	}
	return false;
}

bool point_in_feature(double lon, double lat, const ProbOutlookFeature& f) noexcept {
	for (const Polygon& ring : f.rings) {
		if (point_in_polygon(lon, lat, ring)) {
			return true;
		}
	}
	return false;
}

} // namespace spc
