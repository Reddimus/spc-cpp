#include "models/json.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <glaze/util/fast_float.hpp>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace spc::detail {

namespace {

struct JsonReadOptions : glz::opts {
	bool validate_trailing_whitespace{true};
};

constexpr double kMercatorRadius = 6378137.0;
constexpr double kDegrees = 180.0 / std::numbers::pi;

void validate_mercator(const Polygon& ring) {
	for (const LonLat point : ring) {
		if (!std::isfinite(point.lon) || !std::isfinite(point.lat) ||
			std::abs(point.lon) > kMercatorRadius * std::numbers::pi ||
			std::abs(point.lat) > kMercatorRadius * std::numbers::pi) {
			throw std::runtime_error("Invalid Esri Web Mercator coordinates");
		}
	}
}

LonLat inverse_mercator(LonLat point) {
	return {(point.lon / kMercatorRadius) * kDegrees,
			std::copysign(90.0 - 2.0 * kDegrees *
									 std::atan(std::exp(-std::abs(point.lat) / kMercatorRadius)),
						  point.lat)};
}

std::size_t mercator_edge_steps(LonLat a, LonLat b) {
	if (a.lon == b.lon || a.lat == b.lat) {
		return 1; // Axis-aligned edges stay straight after projection.
	}
	// Inverse latitude has |second derivative| <= 1/(2 R^2). A 10 km
	// vertical step bounds linear interpolation error to < 1 metre on R.
	return std::max(std::size_t{1},
					static_cast<std::size_t>(std::ceil(std::abs(b.lat - a.lat) / 10000.0)));
}

Polygon project_mercator_ring(const Polygon& ring) {
	constexpr std::size_t kMaxInserted = 1000000;
	std::size_t inserted = 0;
	for (std::size_t i = 0; i < ring.size(); ++i) {
		const std::size_t extra = mercator_edge_steps(ring[i], ring[(i + 1) % ring.size()]) - 1;
		if (extra > kMaxInserted - inserted) {
			throw std::runtime_error("Esri Web Mercator ring exceeds projection expansion limit");
		}
		inserted += extra;
	}
	Polygon projected;
	projected.reserve(ring.size() + inserted);
	for (std::size_t i = 0; i < ring.size(); ++i) {
		const LonLat a = ring[i];
		const LonLat b = ring[(i + 1) % ring.size()];
		projected.push_back(inverse_mercator(a));
		const std::size_t steps = mercator_edge_steps(a, b);
		for (std::size_t j = 1; j < steps; ++j) {
			const double t = static_cast<double>(j) / static_cast<double>(steps);
			projected.push_back(
				inverse_mercator({std::lerp(a.lon, b.lon, t), std::lerp(a.lat, b.lat, t)}));
		}
	}
	return projected;
}

} // namespace

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
	// Glaze's bundled fast_float follows std::from_chars, which libc++ lacks for
	// floating point before version 20 and Apple ships only from macOS 26.
	ParsedNumber parsed;
	const glz::fast_float::from_chars_result result =
		glz::fast_float::from_chars(text.data(), text.data() + text.size(), parsed.value);
	if (result.ec != std::errc{}) {
		return ParsedNumber{};
	}
	parsed.consumed = static_cast<std::size_t>(result.ptr - text.data());
	parsed.ok = true;
	return parsed;
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

std::vector<Polygon> parse_esri_rings(const Json& geom, const Json* reference) {
	std::vector<Polygon> out;
	if (!geom.is_object()) {
		return out;
	}
	const Json* rings_node = lookup(geom, "rings");
	if (rings_node == nullptr || !rings_node->is_array()) {
		return out;
	}
	if (const Json* local = lookup(geom, "spatialReference")) {
		reference = local;
	}
	bool mercator = false;
	if (reference != nullptr) {
		const Json* id = lookup(*reference, "latestWkid");
		if (id == nullptr) {
			id = lookup(*reference, "wkid");
		}
		const double wkid = id != nullptr && id->is_number() ? id->get<double>() : 0.0;
		mercator = wkid == 3857 || wkid == 102100 || wkid == 102113;
		if (!mercator && wkid != 4326 && wkid != 4269) {
			throw std::runtime_error("Unsupported Esri spatial reference; request outSR=4326");
		}
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
		if (mercator) {
			validate_mercator(r); // Validate holes too, before the winding filter.
		}
		// Counter-clockwise (positive area in source coordinates) is an Esri hole.
		if (r.empty() || ring_signed_area(r) > 0.0) {
			continue;
		}
		if (mercator) {
			r = project_mercator_ring(r);
		}
		out.push_back(std::move(r));
	}
	return out;
}

glz::expected<Json, std::string> parse_root(std::string_view body) {
	Json root{};
	// A view can end mid-buffer, so don't let Glaze assume a terminator after it.
	constexpr JsonReadOptions options{{.null_terminated = false}};
	const glz::error_ctx ec = glz::read<options>(root, body);
	if (ec) {
		std::string message = glz::format_error(ec, body);
		// Embedded NULs must survive std::exception::what() and C-string logging.
		if (message.find('\0') != std::string::npos) {
			std::string printable;
			printable.reserve(message.size());
			for (const char byte : message) {
				if (byte == '\0') {
					printable += "\\0";
				} else {
					printable += byte;
				}
			}
			message = std::move(printable);
		}
		return glz::unexpected(std::move(message));
	}
	return root;
}

glz::expected<Json, std::string> parse_owned_root(const std::string& body) {
	Json root{};
	constexpr JsonReadOptions options{{.null_terminated = true}};
	const glz::error_ctx ec = glz::read<options>(root, body);
	if (!ec) {
		return root;
	}
	// Glaze words some truncations differently when it relies on the
	// terminator, so take parse_root's message, which parse_* throws.
	return parse_root(body);
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

std::vector<Polygon> feature_rings(const Json& geometry, const Json* reference) {
	return lookup(geometry, "rings") != nullptr ? parse_esri_rings(geometry, reference)
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
