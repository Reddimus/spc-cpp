/// @file common.cpp
/// @brief SPC GeoJSON null-safe helpers — Glaze-backed.
///
/// Bodies copied VERBATIM from spc-data/src/parser.cpp:28-152. The
/// case-variant-key / numeric-as-string / Polygon-vs-MultiPolygon semantics
/// are exactly the production spc-data behavior; do not "improve" them — a
/// downstream byte-identity gate depends on this code path being unchanged.

#include "spc/models/common.hpp"

#include <format>

namespace spc {
namespace detail {

// ===== glz::generic null-safe extractors (verbatim from spc-data) =====

const Json* lookup(const Json& obj, const char* key) {
	if (!obj.is_object()) {
		return nullptr;
	}
	const glz::generic::object_t& o = obj.get_object();
	glz::generic::object_t::const_iterator it = o.find(key);
	if (it == o.end()) {
		return nullptr;
	}
	return &it->second;
}

std::string json_string(const Json& obj, const char* key) {
	const Json* v = lookup(obj, key);
	if (v == nullptr || v->is_null()) {
		return {};
	}
	if (v->is_string()) {
		return v->get<std::string>();
	}
	return {};
}

/// SPC ships `LABEL` as either a string ("SLGT", "5") or a number (5). Always
/// returns a numeric view; non-numeric / missing yields 0.
double json_number_or_numeric_string(const Json& obj, const char* key) {
	const Json* v = lookup(obj, key);
	if (v == nullptr || v->is_null()) {
		return 0.0;
	}
	if (v->is_number()) {
		return v->get<double>();
	}
	if (v->is_string()) {
		const std::string s = v->get<std::string>();
		try {
			return std::stod(s);
		} catch (...) {
			return 0.0;
		}
	}
	return 0.0;
}

/// Convert SPC's compact "YYYYMMDDHHMM" timestamp to ISO 8601
/// "YYYY-MM-DDTHH:MM:00Z". Returns the input unchanged if the format doesn't
/// match.
std::string spc_ts_to_iso8601(std::string_view spc_ts) {
	if (spc_ts.size() != 12) {
		return std::string{spc_ts};
	}
	for (char c : spc_ts) {
		if (c < '0' || c > '9') {
			return std::string{spc_ts};
		}
	}
	return std::format("{}-{}-{}T{}:{}:00Z", spc_ts.substr(0, 4), spc_ts.substr(4, 2),
					   spc_ts.substr(6, 2), spc_ts.substr(8, 2), spc_ts.substr(10, 2));
}

std::string as_spc_ts(const Json& j, const char* key) {
	return spc_ts_to_iso8601(json_string(j, key));
}

/// Parse either a `Polygon` or a `MultiPolygon` geometry into a list of rings.
/// Both shapes collapse to our `std::vector<Polygon>` representation.
std::vector<Polygon> parse_rings(const Json& geom) {
	std::vector<Polygon> out;
	if (!geom.is_object()) {
		return out;
	}
	const std::string type = json_string(geom, "type");
	const Json* coords_node = lookup(geom, "coordinates");
	if (type.empty() || coords_node == nullptr || !coords_node->is_array()) {
		return out;
	}
	const glz::generic::array_t& coords = coords_node->get_array();

	auto parse_ring = [](const Json& ring) -> Polygon {
		Polygon r;
		if (!ring.is_array()) {
			return r;
		}
		const glz::generic::array_t& ring_arr = ring.get_array();
		r.reserve(ring_arr.size());
		for (const glz::generic& pt : ring_arr) {
			if (!pt.is_array()) {
				continue;
			}
			const glz::generic::array_t& pt_arr = pt.get_array();
			if (pt_arr.size() >= 2 && pt_arr[0].is_number() && pt_arr[1].is_number()) {
				r.push_back({pt_arr[0].get<double>(), pt_arr[1].get<double>()});
			}
		}
		return r;
	};

	if (type == "Polygon") {
		if (!coords.empty()) {
			out.push_back(parse_ring(coords[0])); // outer ring
		}
	} else if (type == "MultiPolygon") {
		for (const glz::generic& poly : coords) {
			if (poly.is_array()) {
				const glz::generic::array_t& poly_arr = poly.get_array();
				if (!poly_arr.empty()) {
					out.push_back(parse_ring(poly_arr[0])); // outer ring of each
				}
			}
		}
	}
	return out;
}

// ===== net-new: ArcGIS Esri-rings adapter (NOT the verbatim path) =====

double ring_signed_area(const Polygon& ring) {
	// Shoelace. Sign indicates orientation; magnitude is 2*area.
	double sum = 0.0;
	const std::size_t n = ring.size();
	if (n < 3) {
		return 0.0;
	}
	for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
		sum += (ring[j].lon * ring[i].lat) - (ring[i].lon * ring[j].lat);
	}
	return sum / 2.0;
}

std::vector<Polygon> parse_esri_rings(const Json& geom) {
	std::vector<Polygon> out;
	if (!geom.is_object()) {
		return out;
	}
	const Json* rings_node = lookup(geom, "rings");
	if (rings_node == nullptr || !rings_node->is_array()) {
		return out;
	}
	const glz::generic::array_t& rings = rings_node->get_array();
	out.reserve(rings.size());
	for (const glz::generic& ring : rings) {
		if (!ring.is_array()) {
			continue;
		}
		const glz::generic::array_t& ring_arr = ring.get_array();
		Polygon r;
		r.reserve(ring_arr.size());
		for (const glz::generic& pt : ring_arr) {
			if (!pt.is_array()) {
				continue;
			}
			const glz::generic::array_t& pt_arr = pt.get_array();
			if (pt_arr.size() >= 2 && pt_arr[0].is_number() && pt_arr[1].is_number()) {
				r.push_back({pt_arr[0].get<double>(), pt_arr[1].get<double>()});
			}
		}
		if (r.empty()) {
			continue;
		}
		// Parity with the verbatim GeoJSON `parse_rings`, which keeps only the
		// OUTER ring of each polygon (coordinates[0] / poly[0]) and discards
		// holes. Esri flattens outer + hole rings into one list distinguished
		// by winding: clockwise == outer, counter-clockwise == hole. In
		// lon/lat the shoelace signed area is NEGATIVE for clockwise. Keep
		// outer rings (area <= 0); drop counter-clockwise hole rings so the
		// Polygon set matches the GeoJSON path exactly.
		if (ring_signed_area(r) > 0.0) {
			continue; // counter-clockwise -> hole -> dropped (matches GeoJSON)
		}
		out.push_back(std::move(r));
	}
	return out;
}

/// Parse the top-level JSON body into a glz::generic. Returns the formatted
/// error message on malformed JSON; the public parse_* wrappers turn that
/// into the std::runtime_error the spc-data main.cpp catches (preserving the
/// pre-migration nlohmann::json::parse contract).
glz::expected<Json, std::string> parse_root(std::string_view body) {
	Json root{};
	glz::error_ctx ec = glz::read_json(root, body);
	if (ec) {
		return glz::unexpected(glz::format_error(ec, body));
	}
	return root;
}

} // namespace detail
} // namespace spc
