/// @file outlook.cpp
/// @brief Day 1-3 categorical and probabilistic parsers.
///
/// These match the internal spc-data parser output for output. Change them
/// only together with spc-data.

#include "spc/models/outlook.hpp"

#include "models/json.hpp"

#include <string>
#include <utility>

namespace spc {

using detail::Json;

std::uint8_t severity_from_label(std::string_view label) noexcept {
	if (label == "TSTM") {
		return 1; // General thunderstorm (some feeds include this)
	}
	if (label == "MRGL") {
		return 1;
	}
	if (label == "SLGT") {
		return 2;
	}
	if (label == "ENH") {
		return 3;
	}
	if (label == "MDT") {
		return 4;
	}
	if (label == "HIGH") {
		return 5;
	}
	return 0;
}

CategoricalOutlookPayload parse_categorical(std::string_view body, std::int32_t day_offset) {
	const Json root = detail::parse_root_or_throw(body);
	CategoricalOutlookPayload payload;
	payload.day_offset = day_offset;
	const Json* features_node = detail::lookup(root, "features");
	if (features_node == nullptr || !features_node->is_array()) {
		return payload;
	}
	for (const glz::generic& feat : features_node->get_array()) {
		const Json* props = detail::lookup(feat, "properties");
		const Json* geometry = detail::lookup(feat, "geometry");
		if (props == nullptr || geometry == nullptr) {
			continue;
		}
		OutlookFeature of;
		// SPC uses either `LABEL` or `label` or `dn` depending on product.
		of.label = detail::json_string(*props, "LABEL");
		if (of.label.empty()) {
			of.label = detail::json_string(*props, "label");
		}
		if (of.label.empty()) {
			of.label = detail::json_string(*props, "dn");
		}
		of.severity = severity_from_label(of.label);
		of.issued_at = detail::as_spc_ts(*props, "ISSUE");
		if (of.issued_at.empty()) {
			of.issued_at = detail::as_spc_ts(*props, "issue");
		}
		of.valid_from = detail::as_spc_ts(*props, "VALID");
		if (of.valid_from.empty()) {
			of.valid_from = detail::as_spc_ts(*props, "valid");
		}
		of.valid_until = detail::as_spc_ts(*props, "EXPIRE");
		if (of.valid_until.empty()) {
			of.valid_until = detail::as_spc_ts(*props, "expire");
		}
		of.rings = detail::parse_rings(*geometry);
		if (of.severity > 0 && !of.rings.empty()) {
			payload.features.push_back(std::move(of));
		}
	}
	return payload;
}

// The by-value `hazard` matches spc-data's signature.
ProbOutlookPayload
parse_probabilistic(std::string_view body, std::int32_t day_offset,
					std::string hazard) { // NOLINT(performance-unnecessary-value-param)
	const Json root = detail::parse_root_or_throw(body);
	ProbOutlookPayload payload;
	payload.day_offset = day_offset;
	payload.hazard = hazard;
	const Json* features_node = detail::lookup(root, "features");
	if (features_node == nullptr || !features_node->is_array()) {
		return payload;
	}
	for (const glz::generic& feat : features_node->get_array()) {
		const Json* props = detail::lookup(feat, "properties");
		const Json* geometry = detail::lookup(feat, "geometry");
		if (props == nullptr || geometry == nullptr) {
			continue;
		}
		ProbOutlookFeature pf;
		pf.hazard = hazard;
		// Feeds publish either a percentage ("5") or a fraction ("0.05").
		pf.probability = detail::normalized_probability(*props);
		pf.issued_at = detail::as_spc_ts(*props, "ISSUE");
		if (pf.issued_at.empty()) {
			pf.issued_at = detail::as_spc_ts(*props, "issue");
		}
		pf.valid_from = detail::as_spc_ts(*props, "VALID");
		if (pf.valid_from.empty()) {
			pf.valid_from = detail::as_spc_ts(*props, "valid");
		}
		pf.valid_until = detail::as_spc_ts(*props, "EXPIRE");
		if (pf.valid_until.empty()) {
			pf.valid_until = detail::as_spc_ts(*props, "expire");
		}
		pf.rings = detail::parse_rings(*geometry);
		if (pf.probability > 0.0 && !pf.rings.empty()) {
			payload.features.push_back(std::move(pf));
		}
	}
	return payload;
}

} // namespace spc
