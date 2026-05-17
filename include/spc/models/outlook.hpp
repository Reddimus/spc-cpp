/// @file outlook.hpp
/// @brief SPC convective (categorical + probabilistic) GeoJSON parsers.
///
/// EXTRACTED VERBATIM from spc-data/src/parser.hpp (Workstream C). Signatures
/// and throw-contract are byte-identical to spc-data; only the namespace
/// changed (`predictioncast::spc_data` -> `spc`). This is the parity-critical
/// path — the byte-identity gate in the spc-data refactor depends on it.

#pragma once

#include "spc/types.hpp"

#include <string_view>

namespace spc {

/// Map SPC categorical label to a 1..5 severity value. Returns 0 if unknown.
[[nodiscard]] std::uint8_t severity_from_label(std::string_view label) noexcept;

/// Parse SPC's day-N categorical outlook GeoJSON (day{1,2,3}otlk_cat.geojson,
/// day4-8otlk.geojson) from a raw JSON body. Caller passes the target day for
/// the payload header. Throws std::runtime_error on malformed JSON.
[[nodiscard]] CategoricalOutlookPayload parse_categorical(std::string_view body,
														  std::int32_t day_offset);

/// Parse SPC's day-N probabilistic outlook GeoJSON
/// (day{1,2}probotlk_{torn,hail,wind}.geojson) from a raw JSON body. Throws
/// std::runtime_error on malformed JSON.
[[nodiscard]] ProbOutlookPayload parse_probabilistic(std::string_view body, std::int32_t day_offset,
													 std::string hazard);

} // namespace spc
