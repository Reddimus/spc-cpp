/// @file json.hpp
/// @brief Private, null-safe helpers over Glaze's generic JSON tree.
///
/// The outlook helpers (`lookup` through `parse_rings`) preserve key-case
/// fallbacks, numeric strings, and outer-ring-only geometry across SPC feeds.
/// `tests/golden/` guards parsed output against unintended changes.

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
/// "0.15" as 0. Leading whitespace and '+' are rejected; "nan" and "inf" parse,
/// as with std::from_chars.
ParsedNumber parse_double(std::string_view text);

/// `obj[key]` as a finite number, accepting numeric strings such as "5" or
/// "0.15". Returns nullopt when the key is absent, null, non-numeric,
/// non-finite, or a string that is not wholly a number such as "5xyz".
std::optional<double> try_json_number_or_numeric_string(const Json& obj, const char* key);

/// `obj[key]` as a number, accepting numeric strings such as "5" or "0.15".
/// Missing and non-finite values, and strings that are not wholly a number
/// such as "5xyz", yield 0.
double json_number_or_numeric_string(const Json& obj, const char* key);

/// The LABEL/label/dn probability normalized to [0, 1]. Values above 1 are
/// percentages and are divided by 100; out-of-range values yield 0.
double normalized_probability(const Json& obj);

/// "YYYYMMDDHHMM" to "YYYY-MM-DDTHH:MM:00Z"; any other input, including twelve
/// digits that name no real UTC time such as month 13, is returned as is.
std::string spc_ts_to_iso8601(std::string_view spc_ts);

/// `spc_ts_to_iso8601(json_string(j, key))`.
std::string as_spc_ts(const Json& j, const char* key);

/// Outer rings of a GeoJSON Polygon or MultiPolygon. Holes are dropped.
std::vector<Polygon> parse_rings(const Json& geom);

/// Outer rings of an Esri `{"rings": [...]}` geometry. Esri mixes outer
/// rings (clockwise) and holes (counter-clockwise) in one list; the holes are
/// dropped so the result matches `parse_rings` for the same layer. Web Mercator
/// is converted to degrees with at most one metre of boundary error. A geometry
/// needing more added points than its payload's `ProjectionBudget` has left
/// fails. A geometry reference overrides the response reference.
/// Malformed points fail for every supported spatial reference.
std::vector<Polygon> parse_esri_rings(const Json& geom, const Json* reference = nullptr);

/// Shoelace signed area; positive means counter-clockwise.
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

/// A feature's fields: GeoJSON `properties` or Esri `attributes`, or null
/// when they are missing or not an object.
const Json* feature_fields(const Json& feature);

/// The points Web Mercator projection may still add to one payload: one
/// million, 16 MB, at the start. An edge is subdivided every 10 km, so 20
/// bytes of JSON can add 4,000 points.
struct ProjectionBudget {
	std::size_t remaining_added_points{1000000};
};

/// One per response: shares the collection reference and projection budget
/// across all features, accepting Esri rings or GeoJSON coordinates. The
/// budget is the payload's. A caller that builds one payload from several
/// responses passes the same `shared` budget for each; otherwise the response
/// is the payload and the parser uses its own.
class PayloadGeometryParser {
public:
	explicit PayloadGeometryParser(const Json& root, ProjectionBudget* shared = nullptr);
	~PayloadGeometryParser() = default;
	PayloadGeometryParser(const PayloadGeometryParser&) = delete;
	PayloadGeometryParser& operator=(const PayloadGeometryParser&) = delete;
	PayloadGeometryParser(PayloadGeometryParser&&) = delete;
	PayloadGeometryParser& operator=(PayloadGeometryParser&&) = delete;
	std::vector<Polygon> parse(const Json& geometry);

private:
	const Json* reference_;
	ProjectionBudget own_;
	ProjectionBudget* budget_;
};

/// The first non-empty string among `keys`, e.g. {"LABEL", "label"}.
std::string first_string(const Json& obj, std::initializer_list<const char*> keys);

/// The first non-empty SPC timestamp among `keys`, converted to ISO 8601.
std::string first_timestamp(const Json& obj, std::initializer_list<const char*> keys);

/// `value` as an int32, or nullopt when it is NaN, infinite, or out of range.
/// A plain cast of such a value is undefined behavior.
std::optional<std::int32_t> to_int32(double value) noexcept;

} // namespace spc::detail
