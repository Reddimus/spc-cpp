/// @file mesoscale.cpp
/// @brief Mesoscale discussion parser: metadata and geometry only.

#include "spc/models/mesoscale.hpp"

#include "models/from_tree.hpp"
#include "models/json.hpp"

#include <utility>

namespace spc {

using detail::Json;

MesoscalePayload detail::mesoscale_from_tree(const Json& root) {
	MesoscalePayload payload;
	const Json* features_node = detail::lookup(root, "features");
	if (features_node == nullptr || !features_node->is_array()) {
		return payload;
	}
	for (const Json& feat : features_node->get_array()) {
		const Json* props = detail::feature_fields(feat);
		if (props == nullptr) {
			continue;
		}
		MesoscaleDiscussion md;
		md.name = detail::first_string(*props, {"name", "NAME"});
		// With no active discussion the layer still holds one placeholder
		// feature named "NoArea" with a tiny ring and null links.
		if (md.name == "NoArea") {
			continue;
		}
		md.folder_path = detail::json_string(*props, "folderpath");
		md.url = detail::json_string(*props, "popupinfo");
		const Json* geometry = detail::lookup(feat, "geometry");
		if (geometry != nullptr) {
			md.rings = detail::feature_rings(*geometry);
		}
		payload.discussions.push_back(std::move(md));
	}
	return payload;
}

MesoscalePayload parse_mesoscale_discussions(std::string_view body) {
	return detail::mesoscale_from_tree(detail::parse_root_or_throw(body));
}

} // namespace spc
