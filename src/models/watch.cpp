/// @file watch.cpp
/// @brief IEM `spcwatch` parser. IEM already sends ISO 8601 times, so they
/// pass through unchanged.

#include "spc/models/watch.hpp"

#include "models/from_tree.hpp"
#include "models/json.hpp"

#include <utility>

namespace spc {

using detail::Json;

namespace {

bool flag(const Json& obj, const char* key) {
	const Json* v = detail::lookup(obj, key);
	return v != nullptr && v->is_boolean() && v->get<bool>();
}

std::int32_t int_field(const Json& obj, const char* key) {
	return detail::to_int32(detail::json_number_or_numeric_string(obj, key)).value_or(0);
}

} // namespace

WatchPayload detail::watches_from_tree(const Json& root) {
	WatchPayload payload;
	const Json* features_node = detail::lookup(root, "features");
	if (features_node == nullptr || !features_node->is_array()) {
		return payload;
	}
	for (const Json& feat : features_node->get_array()) {
		const Json* props = detail::lookup(feat, "properties");
		if (props == nullptr) {
			continue;
		}
		Watch w;
		w.number = int_field(*props, "number");
		w.year = int_field(*props, "year");
		w.type = detail::json_string(*props, "type");
		w.sel = detail::json_string(*props, "sel");
		w.is_pds = flag(*props, "is_pds");
		w.max_hail_size = detail::json_number_or_numeric_string(*props, "max_hail_size");
		w.max_wind_gust_knots =
			detail::json_number_or_numeric_string(*props, "max_wind_gust_knots");
		w.spc_url = detail::json_string(*props, "spcurl");
		w.issued_at = detail::json_string(*props, "issue");
		w.expires_at = detail::json_string(*props, "expire");
		const Json* geometry = detail::lookup(feat, "geometry");
		if (geometry != nullptr) {
			w.rings = detail::feature_rings(*geometry);
		}
		payload.watches.push_back(std::move(w));
	}
	return payload;
}

WatchPayload parse_watches(std::string_view body) {
	return detail::watches_from_tree(detail::parse_root_or_throw(body));
}

} // namespace spc
