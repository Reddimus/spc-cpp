/// @file client.cpp
/// @brief spc_api translation unit.
///
/// PR-1 placeholder: locks the layered `spc_api` target and its
/// install/export shape from the start. The real high-level clients —
/// `StaticFeedClient` (7 static `.nolyr.geojson` feeds), `ArcGISClient`
/// (primary path, `ArcGISPager` 2000-rec paging) and `ArchiveClient` (IEM
/// historical backfill) — are implemented in PR-2.

namespace spc {

// Intentionally empty in PR-1. A TU with no external symbols is a valid
// static-library member; CMake/ar handle the empty archive object fine.

} // namespace spc
