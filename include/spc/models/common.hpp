/// @file common.hpp
/// @brief Null-safe `glz::generic` AST helpers shared by the SPC parsers.
///
/// Glaze adaptation of the house null-safe helper family. The bodies are
/// copied **verbatim** from spc-data/src/parser.cpp:28-152 — the exact,
/// audit-clean, production-proven extractors — so the convective parse path
/// stays byte-identical for the downstream byte-identity gate. The only
/// changes are: these now live in a named `spc::detail` namespace (instead
/// of an anonymous namespace inside parser.cpp) so net-new model parsers can
/// reuse them, and `parse_root` returns `Result<glz::generic>` instead of
/// throwing (the public `parse_*` wrappers re-throw to keep the spc-data
/// contract — see outlook.cpp).

#pragma once

#include "spc/types.hpp"

#include <glaze/glaze.hpp>
#include <glaze/json/generic.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace spc {

/// `using Json = glz::generic;` — the SDK's loose-JSON AST alias.
using Json = glz::generic;

namespace detail {

/// Look up `key` in a JSON object; nullptr if absent / not an object.
const Json* lookup(const Json& obj, const char* key);

/// String value of `obj[key]`, or "" if absent / null / non-string.
std::string json_string(const Json& obj, const char* key);

/// SPC ships `LABEL` as either a string ("SLGT", "5") or a number (5).
/// Always returns a numeric view; non-numeric / missing yields 0.
double json_number_or_numeric_string(const Json& obj, const char* key);

/// Convert SPC's compact "YYYYMMDDHHMM" timestamp to ISO 8601
/// "YYYY-MM-DDTHH:MM:00Z"; returns the input unchanged on format mismatch.
std::string spc_ts_to_iso8601(std::string_view spc_ts);

/// `spc_ts_to_iso8601(json_string(j, key))`.
std::string as_spc_ts(const Json& j, const char* key);

/// Parse either a `Polygon` or a `MultiPolygon` geometry into rings. Both
/// shapes collapse to `std::vector<Polygon>`. Verbatim spc-data semantics.
std::vector<Polygon> parse_rings(const Json& geom);

/// Parse an ArcGIS Esri geometry's `rings` array into our `Polygon` list.
///
/// SEPARATE from `parse_rings` (the verbatim GeoJSON walker) on purpose —
/// the convective GeoJSON path must stay parity-exact, so the Esri adapter
/// is its own function and is explicitly tuned to MATCH that path's
/// semantics rather than the raw Esri shape.
///
/// Esri `{"rings":[[[x,y],...],...]}` is a FLAT list of linear rings mixing
/// outer boundaries and interior holes, distinguished only by winding (Esri
/// convention: clockwise = outer, counter-clockwise = hole). The verbatim
/// GeoJSON `parse_rings` takes `coordinates[0]` / `poly[0]` — i.e. only the
/// OUTER ring of each polygon, **discarding holes**. To stay parity-exact
/// with that (the byte-identity gate depends on it), `parse_esri_rings`
/// likewise keeps only outer rings: it drops counter-clockwise (positive
/// signed-area, in lon/lat) hole rings. Verified probe-for-probe equivalent
/// to the GeoJSON path by tests/test_arcgis.cpp.
std::vector<Polygon> parse_esri_rings(const Json& geom);

/// Shoelace signed area (Esri ring orientation helper). Positive == counter-
/// clockwise in lon/lat space. Exposed for the parity test.
double ring_signed_area(const Polygon& ring);

/// Parse a JSON body into a `glz::generic` root. Glaze read; on malformed
/// JSON returns the formatted error message (the public parse_* wrappers
/// turn this into the std::runtime_error that spc-data's main.cpp catches).
glz::expected<Json, std::string> parse_root(std::string_view body);

} // namespace detail
} // namespace spc
