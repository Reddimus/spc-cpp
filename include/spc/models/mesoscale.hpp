/// @file mesoscale.hpp
/// @brief Net-new SPC Mesoscale Discussion (MD) model — RAW TEXT ONLY.
///
/// Per scope: the MD free-text narrative is intentionally NOT parsed. We
/// expose the identifying metadata + the product URL + the affected polygon
/// so consumers can geo-filter and link out; semantic extraction of the
/// discussion body is explicitly out of scope.

#pragma once

#include "spc/types.hpp"

#include <string>
#include <vector>

namespace spc {

struct MesoscaleDiscussion {
	std::string name;		 ///< e.g. "MD 0746" (raw)
	std::string folder_path; ///< e.g. "MD 0746 Active Till 0945 UTC" (raw)
	std::string url;		 ///< spc.noaa.gov product URL (raw, from popupinfo)
	std::vector<Polygon> rings;
};

struct MesoscalePayload {
	std::vector<MesoscaleDiscussion> discussions;
};

/// Parse the SPC mesoscale-discussion layer (ArcGIS Esri or GeoJSON). Only
/// metadata + geometry; the narrative is left untouched. Throws
/// std::runtime_error on malformed JSON.
[[nodiscard]] MesoscalePayload parse_mesoscale_discussions(std::string_view body);

} // namespace spc
