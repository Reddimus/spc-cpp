#include "models/json.hpp"

#include <cmath>
#include <format>
#include <limits>
#include <stdexcept>
#include <utility>

// libc++ implements floating-point std::from_chars only from version 20 and
// does not define __cpp_lib_to_chars, so check its version directly. Apple's
// libc++ ships it only from macOS 26 (iOS 26), so an older deployment target
// uses the stream fallback. Define SPC_HAS_FP_FROM_CHARS=0 on the command line
// to test the fallback.
#ifndef SPC_HAS_FP_FROM_CHARS
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
#define SPC_HAS_FP_FROM_CHARS 1
#elif defined(_LIBCPP_VERSION) && _LIBCPP_VERSION >= 200000 &&       \
	(!defined(_LIBCPP_AVAILABILITY_HAS_FROM_CHARS_FLOATING_POINT) || \
	 _LIBCPP_AVAILABILITY_HAS_FROM_CHARS_FLOATING_POINT)
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

namespace spc::detail {

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
	// Both implementations below must accept the same leading characters:
	// '-', '.', a digit, or the start of inf/nan.
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

double json_number_or_numeric_string(const Json& obj, const char* key) {
	const Json* v = lookup(obj, key);
	if (v == nullptr || v->is_null()) {
		return 0.0;
	}
	if (v->is_number()) {
		return v->get<double>();
	}
	if (v->is_string()) {
		// Matches a C-locale std::stod for every value SPC publishes.
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

std::string spc_ts_to_iso8601(std::string_view spc_ts) {
	if (spc_ts.size() != 12) {
		return std::string{spc_ts};
	}
	for (const char c : spc_ts) {
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

double ring_signed_area(const Polygon& ring) {
	const std::size_t n = ring.size();
	if (n < 3) {
		return 0.0;
	}
	double sum = 0.0;
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
		// Counter-clockwise (positive area in lon/lat) is an Esri hole.
		if (r.empty() || ring_signed_area(r) > 0.0) {
			continue;
		}
		out.push_back(std::move(r));
	}
	return out;
}

glz::expected<Json, std::string> parse_root(std::string_view body) {
	Json root{};
	const glz::error_ctx ec = glz::read_json(root, body);
	if (ec) {
		return glz::unexpected(glz::format_error(ec, body));
	}
	return root;
}

Json parse_root_or_throw(std::string_view body) {
	glz::expected<Json, std::string> root = parse_root(body);
	if (!root) {
		throw std::runtime_error(root.error());
	}
	return std::move(*root);
}

const Json* feature_fields(const Json& feature) {
	const Json* properties = lookup(feature, "properties");
	return properties != nullptr ? properties : lookup(feature, "attributes");
}

std::vector<Polygon> feature_rings(const Json& geometry) {
	return lookup(geometry, "rings") != nullptr ? parse_esri_rings(geometry)
												: parse_rings(geometry);
}

std::string first_string(const Json& obj, std::initializer_list<const char*> keys) {
	for (const char* key : keys) {
		std::string value = json_string(obj, key);
		if (!value.empty()) {
			return value;
		}
	}
	return {};
}

std::string first_timestamp(const Json& obj, std::initializer_list<const char*> keys) {
	return spc_ts_to_iso8601(first_string(obj, keys));
}

std::optional<std::int32_t> to_int32(double value) noexcept {
	constexpr double kMin = std::numeric_limits<std::int32_t>::min();
	constexpr double kMax = std::numeric_limits<std::int32_t>::max();
	if (!std::isfinite(value) || value < kMin || value > kMax) {
		return std::nullopt;
	}
	return static_cast<std::int32_t>(value);
}

} // namespace spc::detail
