/// @file outlook.cpp
/// @brief SPC convective GeoJSON parsers, Glaze-backed.
///
/// `severity_from_label`, `parse_categorical`, `parse_probabilistic` are
/// copied VERBATIM from spc-data/src/parser.cpp:156-268. The only mechanical
/// change is the helper namespace (`detail::` instead of the anonymous
/// namespace inside the old parser.cpp) and that `detail::parse_root` now
/// returns `glz::expected` — this wrapper re-throws std::runtime_error on a
/// malformed body to preserve the exact spc-data contract that the service's
/// main.cpp catches. Parse output is byte-identical to spc-data. Do not
/// change behavior here — the downstream byte-identity gate depends on it.

#include "spc/models/outlook.hpp"

#include "spc/models/common.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace spc {

namespace {

/// VERBATIM equivalent of spc-data parser.cpp's `parse_root`: throws
/// std::runtime_error on malformed JSON (the contract main.cpp relies on).
Json parse_root_or_throw(std::string_view body) {
	glz::expected<Json, std::string> root = detail::parse_root(body);
	if (!root) {
		throw std::runtime_error(root.error());
	}
	return std::move(*root);
}

} // namespace

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
	const Json root = parse_root_or_throw(body);
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

ProbOutlookPayload parse_probabilistic(std::string_view body, std::int32_t day_offset,
									   std::string hazard) {
	const Json root = parse_root_or_throw(body);
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
		// SPC prob isopleths store percentage as LABEL ("2", "5", "10", ...) or
		// as DN, occasionally as a numeric.
		double pct = detail::json_number_or_numeric_string(*props, "LABEL");
		if (pct == 0.0) {
			pct = detail::json_number_or_numeric_string(*props, "label");
		}
		if (pct == 0.0) {
			pct = detail::json_number_or_numeric_string(*props, "dn");
		}
		pf.probability = pct / 100.0;
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
