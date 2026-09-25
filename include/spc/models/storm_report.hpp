/// @file storm_report.hpp
/// @brief NWS Local Storm Reports (points, not polygons).

#pragma once

#include "spc/types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace spc {

struct StormReport {
	LonLat location;
	std::string type;	   ///< LSR code, e.g. "T" (tornado), "H" (hail)
	std::string type_text; ///< e.g. "TORNADO", "HAIL"
	double magnitude = 0.0;
	std::string unit; ///< as reported, e.g. "Inch", "MPH"
	std::string city;
	std::string county;
	std::string state;
	std::string wfo; ///< issuing NWS office, e.g. "ICT"
	std::string source;
	std::string remark;
	std::string valid_at; ///< ISO 8601
};

struct StormReportPayload {
	std::vector<StormReport> reports;
};

/// Parse IEM's `lsr.geojson`.
/// @throws std::runtime_error if `body` is not valid JSON.
[[nodiscard]] StormReportPayload parse_storm_reports(std::string_view body);

} // namespace spc
