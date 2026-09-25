/// @file api.hpp
/// @brief The SPC clients: ArcGIS (primary), static feeds (fallback), and the
/// IEM archive.
///
/// Every method returns `Result<T>` and never throws for network or data
/// errors. Clients are move-only. A client is safe to use from several
/// threads when its transport is; the default `HttpClient` is.

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
#include <string>
#include <string_view>
#include <vector>

namespace spc {

/// SPC's static GeoJSON files on www.spc.noaa.gov.
///
/// When SPC has not issued a product the file is missing, and methods return
/// `ErrorCode::FeedUnavailable` (see `Error::is_feed_unavailable()`).
class StaticFeedClient {
public:
	explicit StaticFeedClient(ClientConfig config = {});
	/// Use `transport` for every request; nullptr means a default HttpClient.
	explicit StaticFeedClient(std::shared_ptr<HttpTransport> transport);
	~StaticFeedClient();
	StaticFeedClient(StaticFeedClient&&) noexcept;
	StaticFeedClient& operator=(StaticFeedClient&&) noexcept;
	StaticFeedClient(const StaticFeedClient&) = delete;
	StaticFeedClient& operator=(const StaticFeedClient&) = delete;

	/// Categorical outlook for day 1, 2, or 3.
	[[nodiscard]] Result<CategoricalOutlookPayload> day_categorical(std::int32_t day) const;

	/// Probabilistic outlook: "tornado", "hail", or "wind" on days 1 and 2;
	/// "severe" (or "any") on day 3.
	[[nodiscard]] Result<ProbOutlookPayload> day_probabilistic(std::int32_t day,
															   std::string_view hazard) const;

	/// Day 4-8 severe probability, `day` 4 to 8.
	[[nodiscard]] Result<Day48OutlookPayload> day4_8(std::int32_t day) const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

/// Parameters for a raw ArcGIS layer `query`. Values are percent-encoded.
struct QueryParams {
	std::string where{"1=1"};
	std::string geometry;	   ///< optional spatial filter (Esri JSON)
	std::string geometry_type; ///< e.g. "esriGeometryEnvelope"
	std::string spatial_rel{"esriSpatialRelIntersects"};
	std::string out_fields{"*"};
	/// e.g. "objectid", which keeps pages stable while paging. Empty omits it.
	std::string order_by_fields;
	bool return_geometry{true};
	std::string f{"json"}; ///< "json" (Esri), "pjson", or "geojson"
};

/// The NOAA MapServers `ArcGISClient::query_layer` can reach.
enum class ArcGISService : std::uint8_t {
	Outlooks,			  ///< outlooks/SPC_wx_outlks
	FireWeather,		  ///< fire_weather/SPC_firewx
	MesoscaleDiscussions, ///< outlooks/spc_mesoscale_discussion
};

/// SPC products from NOAA's ArcGIS MapServers (mapservices.weather.noaa.gov).
///
/// An empty product is a successful, empty payload. Queries page through
/// results automatically.
class ArcGISClient {
public:
	explicit ArcGISClient(ClientConfig config = {});
	/// Use `transport` for every request; nullptr means a default HttpClient.
	explicit ArcGISClient(std::shared_ptr<HttpTransport> transport);
	~ArcGISClient();
	ArcGISClient(ArcGISClient&&) noexcept;
	ArcGISClient& operator=(ArcGISClient&&) noexcept;
	ArcGISClient(const ArcGISClient&) = delete;
	ArcGISClient& operator=(const ArcGISClient&) = delete;

	/// Categorical outlook for day 1, 2, or 3.
	[[nodiscard]] Result<CategoricalOutlookPayload> query_categorical(std::int32_t day) const;

	/// Probabilistic outlook: "tornado", "hail", or "wind" on days 1 and 2;
	/// "severe" (or "any") on day 3.
	[[nodiscard]] Result<ProbOutlookPayload> query_probabilistic(std::int32_t day,
																 std::string_view hazard) const;

	/// Conditional intensity, with the same day and hazard rules as
	/// `query_probabilistic`.
	[[nodiscard]] Result<ConditionalIntensityPayload>
	query_conditional_intensity(std::int32_t day, std::string_view hazard) const;

	/// Day 4-8 severe probability, `day` 4 to 8.
	[[nodiscard]] Result<Day48OutlookPayload> query_day4_8(std::int32_t day) const;

	/// Fire weather for day 1 to 8, merging the day's two layers (see
	/// `FireWeatherLayer`). If either layer fails, the whole call fails; use
	/// `query_layer` to read one layer on its own.
	[[nodiscard]] Result<FireWeatherPayload> query_fire_weather(std::int32_t day) const;

	/// Active mesoscale discussions; empty when none are active.
	[[nodiscard]] Result<MesoscalePayload> query_active_md() const;

	/// Any layer on one of the three MapServers. Returns each page's raw body.
	[[nodiscard]] Result<std::vector<std::string>>
	query_layer(ArcGISService service, std::int32_t layer_id, const QueryParams& params) const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

/// SPC watches and Local Storm Reports from the Iowa Environmental Mesonet
/// (mesonet.agron.iastate.edu), a free service run as a courtesy.
///
/// Requests are rate limited (2 burst, then 1 per second, waiting at most
/// 5 s) and retried with a slower backoff.
class ArchiveClient {
public:
	explicit ArchiveClient(ClientConfig config = {});
	/// Use `transport` for every request; nullptr means a default HttpClient.
	explicit ArchiveClient(std::shared_ptr<HttpTransport> transport);
	~ArchiveClient();
	ArchiveClient(ArchiveClient&&) noexcept;
	ArchiveClient& operator=(ArchiveClient&&) noexcept;
	ArchiveClient(const ArchiveClient&) = delete;
	ArchiveClient& operator=(const ArchiveClient&) = delete;

	/// Watches in effect now, or at `timestamp` ("YYYYMMDDHHMM", UTC).
	[[nodiscard]] Result<WatchPayload> watches(std::string_view timestamp = {}) const;

	/// Storm reports between two ISO 8601 times (e.g. "2024-04-26T12:00Z"),
	/// optionally for one NWS office (e.g. "ICT").
	[[nodiscard]] Result<StormReportPayload> storm_reports(std::string_view start_iso,
														   std::string_view end_iso,
														   std::string_view wfo = {}) const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace spc
