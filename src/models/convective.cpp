/// @file convective.cpp
/// @brief Net-new Day 4-8 + conditional-intensity parsers.
///
/// Independent of the verbatim Day1-3 path. Reuses only the shared null-safe
/// `detail::*` helpers (which are themselves verbatim) and the net-new
/// `detail::parse_esri_rings`; does NOT reuse `severity_from_label`.

#include "spc/models/convective.hpp"

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

/// SPC's ArcGIS responses carry geometry as Esri `rings`; the static
/// www.spc.noaa.gov GeoJSON carries `coordinates`. Pick the right walker so
/// one parse path serves both sources without touching the verbatim
/// GeoJSON-only `parse_rings`.
std::vector<Polygon> rings_any(const Json& geometry) {
	if (detail::lookup(geometry, "rings") != nullptr) {
		return detail::parse_esri_rings(geometry);
	}
	return detail::parse_rings(geometry);
}

/// ArcGIS Esri features wrap fields in `attributes`; GeoJSON in
/// `properties`. Return whichever is present.
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

} // namespace

std::uint8_t cig_severity_from_label(std::string_view label) noexcept {
	// Product-specific: SPC conditional-intensity groups are "CIG1".."CIG3".
	// Deliberately NOT the categorical MRGL/SLGT/... scale.
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
	const Json root = parse_root_or_throw(body);
	Day48OutlookPayload payload;
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
		Day48Feature f;
		f.day = day;
		// Day4-8 publishes the percentage as LABEL ("0.15") or dn (15).
		f.label = detail::json_string(*props, "LABEL");
		if (f.label.empty()) {
			f.label = detail::json_string(*props, "label");
		}
		double pct = detail::json_number_or_numeric_string(*props, "LABEL");
		if (pct == 0.0) {
			pct = detail::json_number_or_numeric_string(*props, "label");
		}
		if (pct == 0.0) {
			pct = detail::json_number_or_numeric_string(*props, "dn");
		}
		// LABEL "0.15" is already a fraction; dn "15" is a percent. Normalize
		// to [0,1]: values > 1 are treated as percent.
		f.probability = pct > 1.0 ? pct / 100.0 : pct;
		f.issued_at = ts_any(*props, "ISSUE", "issue");
		f.valid_from = ts_any(*props, "VALID", "valid");
		f.valid_until = ts_any(*props, "EXPIRE", "expire");
		f.rings = rings_any(*geometry);
		if (f.probability > 0.0 && !f.rings.empty()) {
			payload.features.push_back(std::move(f));
		}
	}
	return payload;
}

ConditionalIntensityPayload parse_conditional_intensity(std::string_view body, std::int32_t day,
														std::string hazard) {
	const Json root = parse_root_or_throw(body);
	ConditionalIntensityPayload payload;
	payload.day = day;
	payload.hazard = std::move(hazard);
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
		ConditionalIntensityFeature f;
		f.label = detail::json_string(*props, "LABEL");
		if (f.label.empty()) {
			f.label = detail::json_string(*props, "label");
		}
		f.cig_level = cig_severity_from_label(f.label);
		f.issued_at = ts_any(*props, "ISSUE", "issue");
		f.valid_from = ts_any(*props, "VALID", "valid");
		f.valid_until = ts_any(*props, "EXPIRE", "expire");
		f.rings = rings_any(*geometry);
		if (!f.rings.empty()) {
			payload.features.push_back(std::move(f));
		}
	}
	return payload;
}

} // namespace spc
