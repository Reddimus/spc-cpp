/// @file fire_weather.cpp
/// @brief Net-new fire-weather parser. Own severity mapper; reuses only the
/// shared verbatim null-safe helpers + the net-new Esri-rings adapter.

#include "spc/models/fire_weather.hpp"

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

std::vector<Polygon> rings_any(const Json& geometry) {
	if (detail::lookup(geometry, "rings") != nullptr) {
		return detail::parse_esri_rings(geometry);
	}
	return detail::parse_rings(geometry);
}

const Json* props_of(const Json& feat) {
	const Json* p = detail::lookup(feat, "properties");
	if (p != nullptr) {
		return p;
	}
	return detail::lookup(feat, "attributes");
}

std::string ts_any(const Json& props, const char* upper, const char* lower) {
	std::string v = detail::as_spc_ts(props, upper);
	if (v.empty()) {
		v = detail::as_spc_ts(props, lower);
	}
	return v;
}

std::string label_any(const Json& props) {
	std::string label = detail::json_string(props, "LABEL");
	if (label.empty()) {
		label = detail::json_string(props, "label");
	}
	return label;
}

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
	// Locale-independent: a comma-decimal strtod stops at the '.' of "0.0",
	// the full-consume check fails, and the no-risk sentinel ships as a band.
	const std::string text = dn->get<std::string>();
	const detail::ParsedNumber parsed = detail::parse_double(text);
	return parsed.ok && parsed.value == 0.0 && parsed.consumed == text.size();
}

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
	// PRODUCT-SPECIFIC fire-weather scale — NOT the categorical MRGL/SLGT set.
	if (label == "ELEV") {
		return 1;
	}
	if (label == "CRIT") {
		return 2;
	}
	if (label == "EXTM") {
		return 3;
	}
	return 0; // dry-thunderstorm bands (IDRT/SDRT) and unknowns: label-only
}

FireWeatherPayload parse_fire_weather(std::string_view body, std::int32_t day,
									  FireWeatherLayer layer) {
	const Json root = parse_root_or_throw(body);
	FireWeatherPayload payload;
	payload.day = day;
	const Json* features_node = detail::lookup(root, "features");
	if (features_node == nullptr || !features_node->is_array()) {
		return payload;
	}
	for (const glz::generic& feat : features_node->get_array()) {
		const Json* props = props_of(feat);
		const Json* geometry = detail::lookup(feat, "geometry");
		if (props == nullptr || geometry == nullptr) {
			continue;
		}
		const std::string published_label = label_any(*props);
		if (has_zero_dn(*props) || published_label == "Probability Too Low") {
			continue;
		}
		FireWeatherFeature f;
		f.day = day;
		f.layer = layer;
		f.label = published_label;
		if (day >= 3) {
			f.probability = detail::normalized_probability(*props);
		} else if (f.label.empty()) {
			f.label = label_from_dn(*props, layer);
		}
		f.severity = fire_severity_from_label(f.label);
		f.issued_at = ts_any(*props, "ISSUE", "issue");
		f.valid_from = ts_any(*props, "VALID", "valid");
		f.valid_until = ts_any(*props, "EXPIRE", "expire");
		f.rings = rings_any(*geometry);
		if (!f.rings.empty() && (day <= 2 || f.probability > 0.0)) {
			payload.features.push_back(std::move(f));
		}
	}
	return payload;
}

} // namespace spc
