/// @file api.hpp
/// @brief High-level SPC clients: StaticFeed (fallback), ArcGIS (primary),
/// Archive (IEM historical). All methods return `Result<T>`; pimpl;
/// rule-of-5 (move-only) like the nws-cpp clients.

#pragma once

#include "spc/error.hpp"
#include "spc/http_client.hpp"
#include "spc/models/convective.hpp"
#include "spc/models/fire_weather.hpp"
#include "spc/models/mesoscale.hpp"
#include "spc/models/outlook.hpp"
#include "spc/models/storm_report.hpp"
#include "spc/models/watch.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace spc {

// ===== StaticFeedClient — fallback path (www.spc.noaa.gov) =====

/// Fetches the static `.nolyr.geojson` products. This is the exact feed set
/// the internal spc-data service ships (Day1-3 cat + Day1/2 prob), plus the
/// Day4-8 experimental probabilistic feed.
class StaticFeedClient {
public:
	explicit StaticFeedClient(ClientConfig config = {});
	~StaticFeedClient();
	StaticFeedClient(StaticFeedClient&&) noexcept;
	StaticFeedClient& operator=(StaticFeedClient&&) noexcept;
	StaticFeedClient(const StaticFeedClient&) = delete;
	StaticFeedClient& operator=(const StaticFeedClient&) = delete;

	/// Day-N categorical (N = 1..3). A 404 maps to `FeedUnavailable` — SPC's
	/// normal "no active outlook"; callers clear rows, not error out.
	[[nodiscard]] Result<CategoricalOutlookPayload> day_categorical(std::int32_t day);

	/// Day-N probabilistic (day 1: hazard tornado|hail|wind; day 2: "any").
	[[nodiscard]] Result<ProbOutlookPayload> day_probabilistic(std::int32_t day,
															   const std::string& hazard);

	/// Day 4-8 experimental probabilistic (`dayNprob.nolyr.geojson`).
	[[nodiscard]] Result<Day48OutlookPayload> day4_8(std::int32_t day);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

// ===== ArcGISClient — primary path (MapServer) =====

/// Query parameters for an ArcGIS MapServer layer `query` request.
struct QueryParams {
	std::string where{"1=1"};
	std::string geometry;	   ///< optional spatial filter (Esri JSON)
	std::string geometry_type; ///< e.g. "esriGeometryEnvelope"
	std::string spatial_rel{"esriSpatialRelIntersects"};
	std::string out_fields{"*"};
	bool return_geometry{true};
	std::string f{"json"}; ///< "json" (Esri) or "geojson"
};

/// SPC ArcGIS MapServer client. Layer ids are the documented
/// `SPC_wx_outlks` / `SPC_firewx` / `spc_mesoscale_discussion` layout.
/// Paginates via `ArcGISPager` (2000-record transfer limit).
class ArcGISClient {
public:
	explicit ArcGISClient(ClientConfig config = {});
	~ArcGISClient();
	ArcGISClient(ArcGISClient&&) noexcept;
	ArcGISClient& operator=(ArcGISClient&&) noexcept;
	ArcGISClient(const ArcGISClient&) = delete;
	ArcGISClient& operator=(const ArcGISClient&) = delete;

	[[nodiscard]] Result<CategoricalOutlookPayload> query_categorical(std::int32_t day);
	[[nodiscard]] Result<ProbOutlookPayload> query_probabilistic(std::int32_t day,
																 const std::string& hazard);
	[[nodiscard]] Result<FireWeatherPayload> query_fire_weather(std::int32_t day);
	[[nodiscard]] Result<WatchPayload> query_active_watches();
	[[nodiscard]] Result<MesoscalePayload> query_active_md();
	[[nodiscard]] Result<StormReportPayload> query_storm_reports();

	/// Escape hatch: raw paged query against an arbitrary layer id; returns
	/// the concatenated raw response bodies (one per page).
	[[nodiscard]] Result<std::vector<std::string>> query_layer(std::int32_t layer_id,
															   const QueryParams& params);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

// ===== ArchiveClient — IEM historical backfill (best-effort) =====

/// IEM (`mesonet.agron.iastate.edu`) historical access. Best-effort:
/// conservative rate limit + retry, off any hot path. Third-party courtesy
/// service — treat flakiness as expected.
class ArchiveClient {
public:
	explicit ArchiveClient(ClientConfig config = {});
	~ArchiveClient();
	ArchiveClient(ArchiveClient&&) noexcept;
	ArchiveClient& operator=(ArchiveClient&&) noexcept;
	ArchiveClient(const ArchiveClient&) = delete;
	ArchiveClient& operator=(const ArchiveClient&) = delete;

	/// Historical SPC watches (optionally at a `YYYYMMDDHHMM` timestamp).
	[[nodiscard]] Result<WatchPayload> watches(const std::string& ts = {});

	/// Local Storm Reports in [start, end] (ISO 8601), optionally one WFO.
	[[nodiscard]] Result<StormReportPayload> storm_reports(const std::string& start_iso,
														   const std::string& end_iso,
														   const std::string& wfo = {});

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace spc
