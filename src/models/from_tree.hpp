/// @file from_tree.hpp
/// @brief The product parsers, starting from an already parsed tree.
///
/// `parse_X(body, ...)` is `X_from_tree(parse_root_or_throw(body), ...)`, so a
/// client that parsed a response once builds its payload from that tree. The
/// names are distinct because glz::generic converts implicitly from
/// std::string, which would make overloads of `parse_*` ambiguous.
///
/// The parsers that project Esri rings take the payload's `ProjectionBudget`
/// when `root` is only part of it, such as one page of a paged query.

#pragma once

#include "models/json.hpp"
#include "spc/models/convective.hpp"
#include "spc/models/fire_weather.hpp"
#include "spc/models/mesoscale.hpp"
#include "spc/models/storm_report.hpp"
#include "spc/models/watch.hpp"
#include "spc/types.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace spc::detail {

CategoricalOutlookPayload categorical_from_tree(const Json& root, std::int32_t day_offset);

ProbOutlookPayload probabilistic_from_tree(const Json& root, std::int32_t day_offset,
										   std::string_view hazard);

Day48OutlookPayload day4_8_from_tree(const Json& root, std::int32_t day,
									 ProjectionBudget* budget = nullptr);

ConditionalIntensityPayload conditional_intensity_from_tree(const Json& root, std::int32_t day,
															std::string hazard,
															ProjectionBudget* budget = nullptr);

FireWeatherPayload fire_weather_from_tree(const Json& root, std::int32_t day,
										  FireWeatherLayer layer,
										  ProjectionBudget* budget = nullptr);

MesoscalePayload mesoscale_from_tree(const Json& root, ProjectionBudget* budget = nullptr);

StormReportPayload storm_reports_from_tree(const Json& root);

WatchPayload watches_from_tree(const Json& root);

} // namespace spc::detail
