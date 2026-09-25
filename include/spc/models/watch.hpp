/// @file watch.hpp
/// @brief SPC tornado and severe-thunderstorm watches.

#pragma once

#include "spc/types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace spc {

struct Watch {
	std::int32_t number = 0;	///< e.g. 139; numbering restarts each year
	std::int32_t year = 0;		///< with `number`, identifies the watch
	std::string type;			///< "TOR" or "SVR"
	std::string sel;			///< SEL product id, e.g. "SEL9"
	bool is_pds = false;		///< Particularly Dangerous Situation
	double max_hail_size = 0.0; ///< inches
	double max_wind_gust_knots = 0.0;
	std::string spc_url;
	std::string issued_at; ///< ISO 8601
	std::string expires_at;
	std::vector<Polygon> rings;
};

struct WatchPayload {
	std::vector<Watch> watches;
};

/// Parse IEM's `spcwatch` GeoJSON.
/// @throws std::runtime_error if `body` is not valid JSON.
[[nodiscard]] WatchPayload parse_watches(std::string_view body);

} // namespace spc
