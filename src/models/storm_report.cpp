/// @file storm_report.cpp
/// @brief IEM Local Storm Report parser.

#include "spc/models/storm_report.hpp"

#include <utility>

#include "models/json.hpp"

namespace spc {

using detail::Json;

StormReportPayload parse_storm_reports(std::string_view body) {
	const Json root = detail::parse_root_or_throw(body);
	StormReportPayload payload;
	const Json* features_node = detail::lookup(root, "features");
	if (features_node == nullptr || !features_node->is_array()) {
		return payload;
	}
	for (const Json& feat : features_node->get_array()) {
		const Json* props = detail::lookup(feat, "properties");
		if (props == nullptr) {
			continue;
		}
		StormReport sr;
		const Json* geometry = detail::lookup(feat, "geometry");
		const Json* coords =
			geometry != nullptr ? detail::lookup(*geometry, "coordinates") : nullptr;
		if (coords != nullptr && coords->is_array()) {
			const Json::array_t& c = coords->get_array();
			if (c.size() >= 2 && c[0].is_number() && c[1].is_number()) {
				sr.location = {c[0].get<double>(), c[1].get<double>()};
			}
		}
		// IEM repeats lon/lat in the properties; use them when the Point is missing.
		if (sr.location.lon == 0.0 && sr.location.lat == 0.0) {
			sr.location = {detail::json_number_or_numeric_string(*props, "lon"),
						   detail::json_number_or_numeric_string(*props, "lat")};
		}
		sr.type = detail::json_string(*props, "type");
		sr.type_text = detail::json_string(*props, "typetext");
		// `magf` is numeric; `magnitude` is the same value as a string.
		sr.magnitude = detail::json_number_or_numeric_string(*props, "magf");
		if (sr.magnitude == 0.0) {
			sr.magnitude = detail::json_number_or_numeric_string(*props, "magnitude");
		}
		sr.unit = detail::json_string(*props, "unit");
		sr.city = detail::json_string(*props, "city");
		sr.county = detail::json_string(*props, "county");
		sr.state = detail::json_string(*props, "state");
		sr.wfo = detail::json_string(*props, "wfo");
		sr.source = detail::json_string(*props, "source");
		sr.remark = detail::json_string(*props, "remark");
		sr.valid_at = detail::json_string(*props, "valid");
		payload.reports.push_back(std::move(sr));
	}
	return payload;
}

} // namespace spc
