/// @file payload_text.hpp
/// @brief Every parsed payload as plain text, for the golden-output tests.
///
/// A header line, then one line per feature or report with its fields in
/// declaration order and its rings last. The text is the same on every
/// standard library: strings are quoted and escaped to printable ASCII, and
/// doubles use std::format's shortest round-trip form.

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
			out += std::format("{}{} {}", p == 0 ? "" : ", ", rings[r][p].lon, rings[r][p].lat);
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
	std::string out = std::format("CategoricalOutlookPayload day_offset={} features={}\n",
								  payload.day_offset, payload.features.size());
	for (const OutlookFeature& f : payload.features) {
		out +=
			std::format("label={} severity={} issued_at={} valid_from={} valid_until={} rings={}\n",
						quote(f.label), int{f.severity}, quote(f.issued_at), quote(f.valid_from),
						quote(f.valid_until), rings_text(f.rings));
	}
	return out;
}

inline std::string to_text(const ProbOutlookPayload& payload) {
	std::string out =
		std::format("ProbOutlookPayload day_offset={} hazard={} features={}\n", payload.day_offset,
					quote(payload.hazard), payload.features.size());
	for (const ProbOutlookFeature& f : payload.features) {
		out += std::format(
			"hazard={} probability={} issued_at={} valid_from={} valid_until={} rings={}\n",
			quote(f.hazard), f.probability, quote(f.issued_at), quote(f.valid_from),
			quote(f.valid_until), rings_text(f.rings));
	}
	return out;
}

inline std::string to_text(const ConditionalIntensityPayload& payload) {
	std::string out = std::format("ConditionalIntensityPayload day={} hazard={} features={}\n",
								  payload.day, quote(payload.hazard), payload.features.size());
	for (const ConditionalIntensityFeature& f : payload.features) {
		out += std::format(
			"label={} cig_level={} issued_at={} valid_from={} valid_until={} rings={}\n",
			quote(f.label), int{f.cig_level}, quote(f.issued_at), quote(f.valid_from),
			quote(f.valid_until), rings_text(f.rings));
	}
	return out;
}

inline std::string to_text(const Day48OutlookPayload& payload) {
	std::string out = std::format("Day48OutlookPayload day={} features={}\n", payload.day,
								  payload.features.size());
	for (const Day48Feature& f : payload.features) {
		out += std::format(
			"day={} label={} probability={} issued_at={} valid_from={} valid_until={} rings={}\n",
			f.day, quote(f.label), f.probability, quote(f.issued_at), quote(f.valid_from),
			quote(f.valid_until), rings_text(f.rings));
	}
	return out;
}

inline std::string to_text(const FireWeatherPayload& payload) {
	std::string out = std::format("FireWeatherPayload day={} features={}\n", payload.day,
								  payload.features.size());
	for (const FireWeatherFeature& f : payload.features) {
		out += std::format("day={} layer={} label={} severity={} probability={} issued_at={} "
						   "valid_from={} valid_until={} rings={}\n",
						   f.day, layer_name(f.layer), quote(f.label), int{f.severity},
						   f.probability, quote(f.issued_at), quote(f.valid_from),
						   quote(f.valid_until), rings_text(f.rings));
	}
	return out;
}

inline std::string to_text(const MesoscalePayload& payload) {
	std::string out = std::format("MesoscalePayload discussions={}\n", payload.discussions.size());
	for (const MesoscaleDiscussion& d : payload.discussions) {
		out += std::format("name={} folder_path={} url={} rings={}\n", quote(d.name),
						   quote(d.folder_path), quote(d.url), rings_text(d.rings));
	}
	return out;
}

inline std::string to_text(const WatchPayload& payload) {
	std::string out = std::format("WatchPayload watches={}\n", payload.watches.size());
	for (const Watch& w : payload.watches) {
		out += std::format("number={} year={} type={} sel={} is_pds={} max_hail_size={} "
						   "max_wind_gust_knots={} spc_url={} issued_at={} expires_at={} "
						   "rings={}\n",
						   w.number, w.year, quote(w.type), quote(w.sel), w.is_pds, w.max_hail_size,
						   w.max_wind_gust_knots, quote(w.spc_url), quote(w.issued_at),
						   quote(w.expires_at), rings_text(w.rings));
	}
	return out;
}

inline std::string to_text(const StormReportPayload& payload) {
	std::string out = std::format("StormReportPayload reports={}\n", payload.reports.size());
	for (const StormReport& r : payload.reports) {
		out +=
			std::format("location=({} {}) type={} type_text={} magnitude={} unit={} city={} "
						"county={} state={} wfo={} source={} remark={} valid_at={}\n",
						r.location.lon, r.location.lat, quote(r.type), quote(r.type_text),
						r.magnitude, quote(r.unit), quote(r.city), quote(r.county), quote(r.state),
						quote(r.wfo), quote(r.source), quote(r.remark), quote(r.valid_at));
	}
	return out;
}

} // namespace spc::test
