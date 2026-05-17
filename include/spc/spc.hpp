/// @file spc.hpp
/// @brief Umbrella header — pulls in the whole spc-cpp public surface.

#pragma once

// Core
#include "spc/error.hpp"
#include "spc/geo.hpp"
#include "spc/geometry.hpp"
#include "spc/http_client.hpp"
#include "spc/pagination.hpp"
#include "spc/rate_limit.hpp"
#include "spc/retry.hpp"
#include "spc/types.hpp"

// Models
#include "spc/models/common.hpp"
#include "spc/models/convective.hpp"
#include "spc/models/fire_weather.hpp"
#include "spc/models/mesoscale.hpp"
#include "spc/models/outlook.hpp"
#include "spc/models/storm_report.hpp"
#include "spc/models/watch.hpp"

// High-level clients
#include "spc/api.hpp"
