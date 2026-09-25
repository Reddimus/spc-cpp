/// @file mesoscale.hpp
/// @brief Mesoscale discussions: identity, link, and area. The discussion
/// text itself is not fetched.

#pragma once

#include "spc/types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace spc {

struct MesoscaleDiscussion {
	std::string name;		 ///< e.g. "MD 0746"
	std::string folder_path; ///< e.g. "MD 0746 Active Till 0945 UTC"
	std::string url;		 ///< the discussion page on spc.noaa.gov
	std::vector<Polygon> rings;
};

struct MesoscalePayload {
	std::vector<MesoscaleDiscussion> discussions;
};

/// Parse the mesoscale-discussion layer (Esri or GeoJSON). The "NoArea"
/// placeholder NOAA publishes when nothing is active is skipped, so an empty
/// result means no active discussions.
/// @throws std::runtime_error if `body` is not valid JSON.
[[nodiscard]] MesoscalePayload parse_mesoscale_discussions(std::string_view body);

} // namespace spc
