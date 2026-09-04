/// @file common.cpp
/// @brief SPC GeoJSON null-safe helpers — Glaze-backed.
///
/// Bodies copied VERBATIM from spc-data/src/parser.cpp:28-152. The
/// case-variant-key / numeric-as-string / Polygon-vs-MultiPolygon semantics
/// are exactly the production spc-data behavior; do not "improve" them — a
/// downstream byte-identity gate depends on this code path being unchanged.

#include "spc/models/common.hpp"

#include <format>

// Floating-point std::from_chars is the locale-independent parse the standard
// intends, but libc++ only implements it from version 20 (the project's own
// clang-tidy job builds against libc++ 18, where the overload is deleted).
// libstdc++ and MSVC advertise it through __cpp_lib_to_chars; libc++ does not
// define that macro at all, so fall back to its version.
// Definable on the command line to exercise the fallback on a toolchain that
// has from_chars.
#ifndef SPC_HAS_FP_FROM_CHARS
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
#define SPC_HAS_FP_FROM_CHARS 1
#elif defined(_LIBCPP_VERSION) && _LIBCPP_VERSION >= 200000
#define SPC_HAS_FP_FROM_CHARS 1
#else
#define SPC_HAS_FP_FROM_CHARS 0
#endif
#endif

#if SPC_HAS_FP_FROM_CHARS
#include <charconv>
#include <system_error>
#else
#include <locale>
#include <sstream>
#endif

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

ParsedNumber parse_double(std::string_view text) {
	ParsedNumber parsed;
	if (text.empty()) {
		return parsed;
	}
	// Gate the leading character so both implementations below agree on what
	// they accept: from_chars takes '-', a digit, '.', or inf/nan, and never
	// leading whitespace or '+'.
	const char first = text.front();
	const bool leading_ok = first == '-' || first == '.' || (first >= '0' && first <= '9') ||
							first == 'i' || first == 'I' || first == 'n' || first == 'N';
	if (!leading_ok) {
		return parsed;
	}

#if SPC_HAS_FP_FROM_CHARS
	const std::from_chars_result result =
		std::from_chars(text.data(), text.data() + text.size(), parsed.value);
	if (result.ec != std::errc{}) {
		return ParsedNumber{};
	}
	parsed.consumed = static_cast<std::size_t>(result.ptr - text.data());
	parsed.ok = true;
	return parsed;
#else
	std::istringstream stream{std::string{text}};
	stream.imbue(std::locale::classic());
	stream >> parsed.value;
	if (stream.fail()) {
		return ParsedNumber{};
	}
	// tellg() reports -1 once the whole buffer was consumed.
	parsed.consumed = text.size();
	if (!stream.eof()) {
		const std::streamoff position = stream.tellg();
		if (position >= 0) {
			parsed.consumed = static_cast<std::size_t>(position);
		}
	}
	parsed.ok = true;
	return parsed;
#endif
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
		// Was std::stod, whose strtod honours LC_NUMERIC; see parse_double.
		// Byte-identical to a C-locale stod for every value in the fixture
		// corpus, which is what the spc-data byte-identity gate covers.
		const ParsedNumber parsed = parse_double(v->get<std::string>());
		return parsed.ok ? parsed.value : 0.0;
	}
	return 0.0;
}

double normalized_probability(const Json& obj) {
	double value = json_number_or_numeric_string(obj, "LABEL");
	if (value == 0.0) {
		value = json_number_or_numeric_string(obj, "label");
	}
	if (value == 0.0) {
		value = json_number_or_numeric_string(obj, "dn");
	}
	const double normalized = value > 1.0 ? value / 100.0 : value;
	return normalized >= 0.0 && normalized <= 1.0 ? normalized : 0.0;
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
