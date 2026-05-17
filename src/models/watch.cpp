/// @file watch.cpp
/// @brief Net-new watch parser. IEM `spcwatch` GeoJSON: `properties` carry
/// type/number/hail/wind; `issue`/`expire` are already ISO-8601 strings
/// (passed through unchanged — NOT run through the compact-YYYYMMDDHHMM
/// converter, which would leave a 24-char ISO string untouched anyway but we
/// keep intent explicit).

#include "spc/models/watch.hpp"

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

bool flag(const Json& obj, const char* key) {
	const Json* v = detail::lookup(obj, key);
	return v != nullptr && v->is_boolean() && v->get<bool>();
}

} // namespace

WatchPayload parse_watches(std::string_view body) {
	const Json root = parse_root_or_throw(body);
	WatchPayload payload;
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
		Watch w;
		w.number = static_cast<std::int32_t>(num(*props, "number"));
		w.type = detail::json_string(*props, "type");
		w.sel = detail::json_string(*props, "sel");
		w.is_pds = flag(*props, "is_pds");
		w.max_hail_size = num(*props, "max_hail_size");
		w.max_wind_gust_knots = num(*props, "max_wind_gust_knots");
		w.spc_url = detail::json_string(*props, "spcurl");
		// IEM already emits ISO-8601 here; keep verbatim.
		w.issued_at = detail::json_string(*props, "issue");
		w.expires_at = detail::json_string(*props, "expire");
		if (geometry != nullptr) {
			w.rings = detail::lookup(*geometry, "rings") != nullptr
						  ? detail::parse_esri_rings(*geometry)
						  : detail::parse_rings(*geometry);
		}
		payload.watches.push_back(std::move(w));
	}
	return payload;
}

} // namespace spc
