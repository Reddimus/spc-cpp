/// @file spc.hpp
/// @brief Umbrella header — pulls in the whole spc-cpp public surface.

#pragma once

#include "spc/error.hpp"
#include "spc/geo.hpp"
#include "spc/geometry.hpp"
#include "spc/http_client.hpp"
#include "spc/models/common.hpp"
#include "spc/models/outlook.hpp"
#include "spc/pagination.hpp"
#include "spc/retry.hpp"
#include "spc/types.hpp"

// Net-new product models + the StaticFeed/ArcGIS/Archive clients are added
// to this umbrella in PR-2.
