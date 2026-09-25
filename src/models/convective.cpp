/// @file convective.cpp
/// @brief Day 4-8 and conditional-intensity parsers. Each reads both the
/// GeoJSON and the Esri response shape.

#include "spc/models/convective.hpp"

#include "models/json.hpp"

#include <utility>

namespace spc {

using detail::Json;

std::uint8_t cig_severity_from_label(std::string_view label) noexcept {
	if (label == "CIG1") {
		return 1;
	}
	if (label == "CIG2") {
		return 2;
	}
	if (label == "CIG3") {
		return 3;
	}
	return 0;
}

Day48OutlookPayload parse_day4_8(std::string_view body, std::int32_t day) {
	const Json root = detail::parse_root_or_throw(body);
	Day48OutlookPayload payload;
	payload.day = day;
	const Json* features_node = detail::lookup(root, "features");
	if (features_node == nullptr || !features_node->is_array()) {
		return payload;
	}
	for (const Json& feat : features_node->get_array()) {
		const Json* props = detail::feature_fields(feat);
		const Json* geometry = detail::lookup(feat, "geometry");
		if (props == nullptr || geometry == nullptr) {
			continue;
		}
		Day48Feature f;
		f.day = day;
		f.label = detail::first_string(*props, {"LABEL", "label"});
		f.probability = detail::normalized_probability(*props);
		f.issued_at = detail::first_timestamp(*props, {"ISSUE", "issue"});
		f.valid_from = detail::first_timestamp(*props, {"VALID", "valid"});
		f.valid_until = detail::first_timestamp(*props, {"EXPIRE", "expire"});
		f.rings = detail::feature_rings(*geometry);
		if (f.probability > 0.0 && !f.rings.empty()) {
			payload.features.push_back(std::move(f));
		}
	}
	return payload;
}

ConditionalIntensityPayload parse_conditional_intensity(std::string_view body, std::int32_t day,
														std::string hazard) {
	const Json root = detail::parse_root_or_throw(body);
	ConditionalIntensityPayload payload;
	payload.day = day;
	payload.hazard = std::move(hazard);
	const Json* features_node = detail::lookup(root, "features");
	if (features_node == nullptr || !features_node->is_array()) {
		return payload;
	}
	for (const Json& feat : features_node->get_array()) {
		const Json* props = detail::feature_fields(feat);
		const Json* geometry = detail::lookup(feat, "geometry");
		if (props == nullptr || geometry == nullptr) {
			continue;
		}
		ConditionalIntensityFeature f;
		f.label = detail::first_string(*props, {"LABEL", "label"});
		f.cig_level = cig_severity_from_label(f.label);
		f.issued_at = detail::first_timestamp(*props, {"ISSUE", "issue"});
		f.valid_from = detail::first_timestamp(*props, {"VALID", "valid"});
		f.valid_until = detail::first_timestamp(*props, {"EXPIRE", "expire"});
		f.rings = detail::feature_rings(*geometry);
		if (!f.rings.empty()) {
			payload.features.push_back(std::move(f));
		}
	}
	return payload;
}

} // namespace spc
