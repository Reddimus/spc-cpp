#include "spc/geometry.hpp"

#include <cstddef>

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

} // namespace spc
