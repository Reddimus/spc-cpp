/// @file json.hpp
/// @brief Private, null-safe helpers over Glaze's generic JSON tree.
///
/// The spc-data helpers (`lookup` through `parse_rings`) keep that service's
/// behavior exactly: key-case fallbacks, numeric strings, and outer rings only.
/// Change them only together with spc-data.

#pragma once

#include "spc/types.hpp"

#include <cstddef>
#include <cstdint>
#include <glaze/glaze.hpp>
#include <glaze/json/generic.hpp>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace spc::detail {

using Json = glz::generic;

/// `obj[key]`, or nullptr when `obj` is not an object or has no such key.
const Json* lookup(const Json& obj, const char* key);

/// `obj[key]` as a string, or "" when it is absent, null, or not a string.
std::string json_string(const Json& obj, const char* key);

/// Result of `parse_double`.
struct ParsedNumber {
	double value = 0.0;		  ///< meaningful only when `ok`
	std::size_t consumed = 0; ///< characters the number used
	bool ok = false;
};

/// Parse the leading decimal number of `text` regardless of the process
/// locale. `std::stod` follows LC_NUMERIC, so on a comma-decimal host it reads
/// "0.15" as 0. Leading whitespace and '+' are rejected.
ParsedNumber parse_double(std::string_view text);

/// `obj[key]` as a number, accepting numeric strings such as "5" or "0.15".
/// Missing or non-numeric values yield 0.
double json_number_or_numeric_string(const Json& obj, const char* key);

/// The LABEL/label/dn probability normalized to [0, 1]. Values above 1 are
/// percentages and are divided by 100; out-of-range values yield 0.
double normalized_probability(const Json& obj);

/// "YYYYMMDDHHMM" to "YYYY-MM-DDTHH:MM:00Z"; any other input is returned as is.
std::string spc_ts_to_iso8601(std::string_view spc_ts);

/// `spc_ts_to_iso8601(json_string(j, key))`.
std::string as_spc_ts(const Json& j, const char* key);

/// Outer rings of a GeoJSON Polygon or MultiPolygon. Holes are dropped.
std::vector<Polygon> parse_rings(const Json& geom);

/// Outer rings of an Esri `{"rings": [...]}` geometry. Esri mixes outer
/// rings (clockwise) and holes (counter-clockwise) in one list; the holes are
/// dropped so the result matches `parse_rings` for the same layer.
std::vector<Polygon> parse_esri_rings(const Json& geom);

/// Shoelace signed area; positive means counter-clockwise in lon/lat.
double ring_signed_area(const Polygon& ring);

/// Parse `body`, or return Glaze's formatted error message.
glz::expected<Json, std::string> parse_root(std::string_view body);

/// `parse_root` for a body held in a std::string, which is faster: the
/// terminator std::string keeps after its text lets Glaze skip end checks.
glz::expected<Json, std::string> parse_owned_root(const std::string& body);

// ===== Shared by the product parsers =====

/// Parse `body`; throws std::runtime_error on malformed JSON, which is the
/// public `parse_*` contract.
Json parse_root_or_throw(std::string_view body);

/// A feature's fields: GeoJSON `properties` or Esri `attributes`.
const Json* feature_fields(const Json& feature);

/// Rings from either geometry shape: Esri `rings` or GeoJSON `coordinates`.
std::vector<Polygon> feature_rings(const Json& geometry);

/// The first non-empty string among `keys`, e.g. {"LABEL", "label"}.
std::string first_string(const Json& obj, std::initializer_list<const char*> keys);

/// The first non-empty SPC timestamp among `keys`, converted to ISO 8601.
std::string first_timestamp(const Json& obj, std::initializer_list<const char*> keys);

/// `value` as an int32, or nullopt when it is NaN, infinite, or out of range.
/// A plain cast of such a value is undefined behavior.
std::optional<std::int32_t> to_int32(double value) noexcept;

} // namespace spc::detail
