/// @file mesoscale.cpp
/// @brief Net-new MD parser — metadata + geometry only; narrative untouched.

#include "spc/models/mesoscale.hpp"

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

const Json* props_of(const Json& feat) {
	const Json* p = detail::lookup(feat, "properties");
	if (p != nullptr) {
		return p;
	}
	return detail::lookup(feat, "attributes");
}

} // namespace

MesoscalePayload parse_mesoscale_discussions(std::string_view body) {
	const Json root = parse_root_or_throw(body);
	MesoscalePayload payload;
	const Json* features_node = detail::lookup(root, "features");
	if (features_node == nullptr || !features_node->is_array()) {
		return payload;
	}
	for (const glz::generic& feat : features_node->get_array()) {
		const Json* props = props_of(feat);
		const Json* geometry = detail::lookup(feat, "geometry");
		if (props == nullptr) {
			continue;
		}
		MesoscaleDiscussion md;
		md.name = detail::json_string(*props, "name");
		if (md.name.empty()) {
			md.name = detail::json_string(*props, "NAME");
		}
		md.folder_path = detail::json_string(*props, "folderpath");
		// `popupinfo` is the spc.noaa.gov product URL — kept raw, narrative
		// deliberately not fetched/parsed.
		md.url = detail::json_string(*props, "popupinfo");
		if (geometry != nullptr) {
			md.rings = detail::lookup(*geometry, "rings") != nullptr
						   ? detail::parse_esri_rings(*geometry)
						   : detail::parse_rings(*geometry);
		}
		payload.discussions.push_back(std::move(md));
	}
	return payload;
}

} // namespace spc
