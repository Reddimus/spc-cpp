/// @file storm_report.hpp
/// @brief Net-new Local Storm Report (LSR) model.
///
/// LSRs are Point features (not polygons). Reported via the IEM `lsr`
/// GeoJSON. No severity scale — `type`/`type_text` are raw NWS LSR codes.

#pragma once

#include "spc/types.hpp"

#include <string>
#include <vector>

namespace spc {

struct StormReport {
	LonLat location;	   ///< (lon, lat)
	std::string type;	   ///< raw LSR code, e.g. "R" (rain), "T" (tornado)
	std::string type_text; ///< e.g. "RAIN", "TORNADO"
	double magnitude = 0.0;
	std::string unit; ///< e.g. "Inch", "MPH"
	std::string city;
	std::string county;
	std::string state;
	std::string source;
	std::string remark;
	std::string valid_at; ///< ISO 8601 (IEM emits ISO; passed through)
};

struct StormReportPayload {
	std::vector<StormReport> reports;
};

/// Parse the IEM `lsr` GeoJSON FeatureCollection. Throws std::runtime_error
/// on malformed JSON.
[[nodiscard]] StormReportPayload parse_storm_reports(std::string_view body);

} // namespace spc
