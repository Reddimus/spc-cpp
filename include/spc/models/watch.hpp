/// @file watch.hpp
/// @brief Net-new SPC watch model (tornado / severe-thunderstorm watches).
///
/// Watches carry a discrete `type` ("TOR" | "SVR") and parameters, not a
/// categorical severity band. No `severity_from_label` involvement.

#pragma once

#include "spc/types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace spc {

struct Watch {
	std::int32_t number = 0;	///< watch number (e.g. 139)
	std::string type;			///< "TOR" | "SVR" (raw, unmapped)
	std::string sel;			///< SEL product id (e.g. "SEL9")
	bool is_pds = false;		///< Particularly Dangerous Situation
	double max_hail_size = 0.0; ///< inches
	double max_wind_gust_knots = 0.0;
	std::string spc_url;
	std::string issued_at; ///< ISO 8601 (passed through as-is if already ISO)
	std::string expires_at;
	std::vector<Polygon> rings;
};

struct WatchPayload {
	std::vector<Watch> watches;
};

/// Parse the IEM `spcwatch` GeoJSON (active/historical SPC watches). Throws
/// std::runtime_error on malformed JSON.
[[nodiscard]] WatchPayload parse_watches(std::string_view body);

} // namespace spc
