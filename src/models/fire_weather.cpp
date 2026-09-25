/// @file fire_weather.cpp
/// @brief Fire-weather parser, with its own label and severity mapping.

#include "spc/models/fire_weather.hpp"

#include "models/from_tree.hpp"
#include "models/json.hpp"

#include <string>
#include <utility>

namespace spc {

using detail::Json;

namespace {

/// NOAA marks "no risk" polygons with dn 0.
bool has_zero_dn(const Json& props) {
	const Json* dn = detail::lookup(props, "dn");
	if (dn == nullptr) {
		return false;
	}
	if (dn->is_number()) {
		return dn->get<double>() == 0.0;
	}
	if (!dn->is_string()) {
		return false;
	}
	const std::string text = dn->get<std::string>();
	const detail::ParsedNumber parsed = detail::parse_double(text);
	return parsed.ok && parsed.value == 0.0 && parsed.consumed == text.size();
}

/// Days 1 and 2 publish only a dn band index, and the two layers use the same
/// indexes for different labels.
std::string label_from_dn(const Json& props, FireWeatherLayer layer) {
	const double dn = detail::json_number_or_numeric_string(props, "dn");
	if (layer == FireWeatherLayer::Outlook) {
		if (dn == 5.0) {
			return "ELEV";
		}
		if (dn == 8.0) {
			return "CRIT";
		}
		if (dn == 10.0) {
			return "EXTM";
		}
	}
	if (layer == FireWeatherLayer::DryThunderstorm) {
		if (dn == 5.0) {
			return "IDRT";
		}
		if (dn == 8.0) {
			return "SDRT";
		}
	}
	return {};
}

} // namespace

std::uint8_t fire_severity_from_label(std::string_view label) noexcept {
	if (label == "ELEV") {
		return 1;
	}
	if (label == "CRIT") {
		return 2;
	}
	if (label == "EXTM") {
		return 3;
	}
	return 0;
}

FireWeatherPayload detail::fire_weather_from_tree(const Json& root, std::int32_t day,
												  FireWeatherLayer layer) {
	FireWeatherPayload payload;
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
		std::string label = detail::first_string(*props, {"LABEL", "label"});
		if (has_zero_dn(*props) || label == "Probability Too Low") {
			continue;
		}
		FireWeatherFeature f;
		f.day = day;
		f.layer = layer;
		f.label = std::move(label);
		if (day >= 3) {
			f.probability = detail::normalized_probability(*props);
		} else if (f.label.empty()) {
			f.label = label_from_dn(*props, layer);
		}
		f.severity = fire_severity_from_label(f.label);
		f.issued_at = detail::first_timestamp(*props, {"ISSUE", "issue"});
		f.valid_from = detail::first_timestamp(*props, {"VALID", "valid"});
		f.valid_until = detail::first_timestamp(*props, {"EXPIRE", "expire"});
		f.rings = detail::feature_rings(*geometry);
		if (!f.rings.empty() && (day <= 2 || f.probability > 0.0)) {
			payload.features.push_back(std::move(f));
		}
	}
	return payload;
}

FireWeatherPayload parse_fire_weather(std::string_view body, std::int32_t day,
									  FireWeatherLayer layer) {
	return detail::fire_weather_from_tree(detail::parse_root_or_throw(body), day, layer);
}

} // namespace spc
