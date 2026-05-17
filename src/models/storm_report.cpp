/// @file storm_report.cpp
/// @brief Net-new LSR parser. Point geometry; numeric `magf` preferred over
/// the string `magnitude` when both present (IEM ships both).

#include "spc/models/storm_report.hpp"

#include "spc/models/common.hpp"

#include <stdexcept>
#include <utility>

namespace spc {

namespace {

Json parse_root_or_throw(std::string_view body) {
	glz::expected<Json, std::string> root = detail::parse_root(body);
	if (!root) {
		throw std::runtime_error(root.error());
	}
	return std::move(*root);
}

double num(const Json& obj, const char* key) {
	const Json* v = detail::lookup(obj, key);
	if (v == nullptr || v->is_null()) {
		return 0.0;
	}
	if (v->is_number()) {
		return v->get<double>();
	}
	return detail::json_number_or_numeric_string(obj, key);
}

} // namespace

StormReportPayload parse_storm_reports(std::string_view body) {
	const Json root = parse_root_or_throw(body);
	StormReportPayload payload;
	const Json* features_node = detail::lookup(root, "features");
	if (features_node == nullptr || !features_node->is_array()) {
		return payload;
	}
	for (const glz::generic& feat : features_node->get_array()) {
		const Json* props = detail::lookup(feat, "properties");
		const Json* geometry = detail::lookup(feat, "geometry");
		if (props == nullptr) {
			continue;
		}
		StormReport sr;
		// Point geometry: coordinates = [lon, lat].
		if (geometry != nullptr) {
			const Json* coords = detail::lookup(*geometry, "coordinates");
			if (coords != nullptr && coords->is_array()) {
				const glz::generic::array_t& c = coords->get_array();
				if (c.size() >= 2 && c[0].is_number() && c[1].is_number()) {
					sr.location = {c[0].get<double>(), c[1].get<double>()};
				}
			}
		}
		// IEM also carries lon/lat in properties — fall back if geometry was
		// absent.
		if (sr.location.lon == 0.0 && sr.location.lat == 0.0) {
			sr.location = {num(*props, "lon"), num(*props, "lat")};
		}
		sr.type = detail::json_string(*props, "type");
		sr.type_text = detail::json_string(*props, "typetext");
		sr.magnitude = num(*props, "magf");
		if (sr.magnitude == 0.0) {
			sr.magnitude = detail::json_number_or_numeric_string(*props, "magnitude");
		}
		sr.unit = detail::json_string(*props, "unit");
		sr.city = detail::json_string(*props, "city");
		sr.county = detail::json_string(*props, "county");
		sr.state = detail::json_string(*props, "state");
		sr.source = detail::json_string(*props, "source");
		sr.remark = detail::json_string(*props, "remark");
		sr.valid_at = detail::json_string(*props, "valid");
		payload.reports.push_back(std::move(sr));
	}
	return payload;
}

} // namespace spc
