/// @file payload_text.hpp
/// @brief Every parsed payload as plain text, for the golden-output tests.
///
/// A header line, then one line per feature or report with its fields in
/// declaration order and its rings last. The text is the same on every
/// standard library: strings are quoted and escaped to printable ASCII, and
/// doubles use std::format's shortest round-trip form.
///
/// Each renderer destructures its struct, so a new field breaks the build
/// until it is printed here too.

#pragma once

#include "spc/models/convective.hpp"
#include "spc/models/fire_weather.hpp"
#include "spc/models/mesoscale.hpp"
#include "spc/models/storm_report.hpp"
#include "spc/models/watch.hpp"
#include "spc/types.hpp"

#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace spc::test {

/// `value` in double quotes, with quotes, backslashes, and bytes outside
/// printable ASCII escaped.
inline std::string quote(std::string_view value) {
	std::string out = "\"";
	for (const char c : value) {
		const unsigned char byte = static_cast<unsigned char>(c);
		if (c == '"' || c == '\\') {
			out += '\\';
			out += c;
		} else if (byte < 0x20 || byte > 0x7E) {
			out += std::format("\\x{:02x}", byte);
		} else {
			out += c;
		}
	}
	out += '"';
	return out;
}

/// Rings in WKT style: ((lon lat, lon lat, ...), (...)).
inline std::string rings_text(const std::vector<Polygon>& rings) {
	std::string out = "(";
	for (std::size_t r = 0; r < rings.size(); ++r) {
		out += r == 0 ? "(" : ", (";
		for (std::size_t p = 0; p < rings[r].size(); ++p) {
			const auto& [lon, lat] = rings[r][p];
			out += std::format("{}{} {}", p == 0 ? "" : ", ", lon, lat);
		}
		out += ')';
	}
	out += ')';
	return out;
}

inline std::string_view layer_name(FireWeatherLayer layer) {
	switch (layer) {
		case FireWeatherLayer::Outlook:
			return "Outlook";
		case FireWeatherLayer::DryThunderstorm:
			return "DryThunderstorm";
		case FireWeatherLayer::WindLowHumidity:
			return "WindLowHumidity";
	}
	return "Unknown";
}

inline std::string to_text(const CategoricalOutlookPayload& payload) {
	const auto& [day_offset, features] = payload;
	std::string out = std::format("CategoricalOutlookPayload day_offset={} features={}\n",
								  day_offset, features.size());
	for (const auto& [label, severity, rings, issued_at, valid_from, valid_until] : features) {
		out +=
			std::format("label={} severity={} issued_at={} valid_from={} valid_until={} rings={}\n",
						quote(label), int{severity}, quote(issued_at), quote(valid_from),
						quote(valid_until), rings_text(rings));
	}
	return out;
}

inline std::string to_text(const ProbOutlookPayload& payload) {
	const auto& [day_offset, payload_hazard, features] = payload;
	std::string out = std::format("ProbOutlookPayload day_offset={} hazard={} features={}\n",
								  day_offset, quote(payload_hazard), features.size());
	for (const auto& [hazard, probability, rings, issued_at, valid_from, valid_until] : features) {
		out += std::format(
			"hazard={} probability={} issued_at={} valid_from={} valid_until={} rings={}\n",
			quote(hazard), probability, quote(issued_at), quote(valid_from), quote(valid_until),
			rings_text(rings));
	}
	return out;
}

inline std::string to_text(const ConditionalIntensityPayload& payload) {
	const auto& [day, hazard, features] = payload;
	std::string out = std::format("ConditionalIntensityPayload day={} hazard={} features={}\n", day,
								  quote(hazard), features.size());
	for (const auto& [label, cig_level, rings, issued_at, valid_from, valid_until] : features) {
		out += std::format(
			"label={} cig_level={} issued_at={} valid_from={} valid_until={} rings={}\n",
			quote(label), int{cig_level}, quote(issued_at), quote(valid_from), quote(valid_until),
			rings_text(rings));
	}
	return out;
}

inline std::string to_text(const Day48OutlookPayload& payload) {
	const auto& [payload_day, features] = payload;
	std::string out =
		std::format("Day48OutlookPayload day={} features={}\n", payload_day, features.size());
	for (const auto& [day, label, probability, rings, issued_at, valid_from, valid_until] :
		 features) {
		out += std::format(
			"day={} label={} probability={} issued_at={} valid_from={} valid_until={} rings={}\n",
			day, quote(label), probability, quote(issued_at), quote(valid_from), quote(valid_until),
			rings_text(rings));
	}
	return out;
}

inline std::string to_text(const FireWeatherPayload& payload) {
	const auto& [payload_day, features] = payload;
	std::string out =
		std::format("FireWeatherPayload day={} features={}\n", payload_day, features.size());
	for (const auto& [day, layer, label, severity, probability, rings, issued_at, valid_from,
					  valid_until] : features) {
		out +=
			std::format("day={} layer={} label={} severity={} probability={} issued_at={} "
						"valid_from={} valid_until={} rings={}\n",
						day, layer_name(layer), quote(label), int{severity}, probability,
						quote(issued_at), quote(valid_from), quote(valid_until), rings_text(rings));
	}
	return out;
}

inline std::string to_text(const MesoscalePayload& payload) {
	const auto& [discussions] = payload;
	std::string out = std::format("MesoscalePayload discussions={}\n", discussions.size());
	for (const auto& [name, folder_path, url, rings] : discussions) {
		out += std::format("name={} folder_path={} url={} rings={}\n", quote(name),
						   quote(folder_path), quote(url), rings_text(rings));
	}
	return out;
}

inline std::string to_text(const WatchPayload& payload) {
	const auto& [watches] = payload;
	std::string out = std::format("WatchPayload watches={}\n", watches.size());
	for (const auto& [number, year, type, sel, is_pds, max_hail_size, max_wind_gust_knots, spc_url,
					  issued_at, expires_at, rings] : watches) {
		out += std::format("number={} year={} type={} sel={} is_pds={} max_hail_size={} "
						   "max_wind_gust_knots={} spc_url={} issued_at={} expires_at={} "
						   "rings={}\n",
						   number, year, quote(type), quote(sel), is_pds, max_hail_size,
						   max_wind_gust_knots, quote(spc_url), quote(issued_at), quote(expires_at),
						   rings_text(rings));
	}
	return out;
}

inline std::string to_text(const StormReportPayload& payload) {
	const auto& [reports] = payload;
	std::string out = std::format("StormReportPayload reports={}\n", reports.size());
	for (const auto& [location, type, type_text, magnitude, unit, city, county, state, wfo, source,
					  remark, valid_at] : reports) {
		const auto& [lon, lat] = location;
		out += std::format("location=({} {}) type={} type_text={} magnitude={} unit={} city={} "
						   "county={} state={} wfo={} source={} remark={} valid_at={}\n",
						   lon, lat, quote(type), quote(type_text), magnitude, quote(unit),
						   quote(city), quote(county), quote(state), quote(wfo), quote(source),
						   quote(remark), quote(valid_at));
	}
	return out;
}

} // namespace spc::test
