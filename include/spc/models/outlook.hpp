/// @file outlook.hpp
/// @brief Day 1-3 categorical and probabilistic outlook parsers.

#pragma once

#include "spc/types.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace spc {

/// Categorical label to severity: TSTM and MRGL 1, SLGT 2, ENH 3, MDT 4,
/// HIGH 5. Returns 0 for anything else.
[[nodiscard]] std::uint8_t severity_from_label(std::string_view label) noexcept;

/// Parse a categorical outlook (GeoJSON). `day_offset` is copied into the
/// payload. Bands with an unknown label or no rings are skipped.
/// @throws std::runtime_error if `body` is not valid JSON.
[[nodiscard]] CategoricalOutlookPayload parse_categorical(std::string_view body,
														  std::int32_t day_offset);

/// Parse a probabilistic outlook (GeoJSON). Probabilities are normalized to
/// [0, 1]; isopleths with no probability or no rings are skipped.
/// @throws std::runtime_error if `body` is not valid JSON.
[[nodiscard]] ProbOutlookPayload parse_probabilistic(std::string_view body, std::int32_t day_offset,
													 std::string hazard);

} // namespace spc
