/// @file client.cpp
/// @brief StaticFeed / ArcGIS / Archive client implementations.

#include "spc/api.hpp"
#include "spc/pagination.hpp"
#include "spc/rate_limit.hpp"
#include "spc/retry.hpp"

#include <format>
#include <string>
#include <utility>

namespace spc {

namespace {

constexpr const char* kStaticBase = "https://www.spc.noaa.gov/products/outlook/";
constexpr const char* kStaticDay48Base = "https://www.spc.noaa.gov/products/exper/day4-8/";
constexpr const char* kArcGisOutlks =
	"https://mapservices.weather.noaa.gov/vector/rest/services/outlooks/SPC_wx_outlks/MapServer";
constexpr const char* kArcGisFirewx =
	"https://mapservices.weather.noaa.gov/vector/rest/services/fire_weather/SPC_firewx/MapServer";
constexpr const char* kArcGisMd = "https://mapservices.weather.noaa.gov/vector/rest/services/"
								  "outlooks/spc_mesoscale_discussion/MapServer";
constexpr const char* kIemBase = "https://mesonet.agron.iastate.edu/";

/// SPC 404 == "no active outlook" (FeedUnavailable). Map HTTP status to the
/// right error; only a real body is handed to the parser.
Result<std::string> body_or_error(Result<HttpResponse> r) {
	if (!r) {
		return std::unexpected(r.error());
	}
	if (r->status_code == 200) {
		return std::move(r->body);
	}
	return std::unexpected(Error::from_response(r->status_code, r->body));
}

} // namespace

// ===================== StaticFeedClient =====================

struct StaticFeedClient::Impl {
	HttpClient http;
	RetryPolicy retry;
	explicit Impl(ClientConfig cfg) : http(std::move(cfg)) {}
};

StaticFeedClient::StaticFeedClient(ClientConfig config)
	: impl_(std::make_unique<Impl>(std::move(config))) {}
StaticFeedClient::~StaticFeedClient() = default;
StaticFeedClient::StaticFeedClient(StaticFeedClient&&) noexcept = default;
StaticFeedClient& StaticFeedClient::operator=(StaticFeedClient&&) noexcept = default;

Result<CategoricalOutlookPayload> StaticFeedClient::day_categorical(std::int32_t day) {
	const std::string url = std::format("{}day{}otlk_cat.nolyr.geojson", kStaticBase, day);
	Result<std::string> body =
		body_or_error(with_retry([&] { return impl_->http.get(url); }, impl_->retry));
	if (!body) {
		return std::unexpected(body.error());
	}
	try {
		return parse_categorical(*body, day);
	} catch (const std::exception& e) {
		return std::unexpected(Error::parse(e.what()));
	}
}

Result<ProbOutlookPayload> StaticFeedClient::day_probabilistic(std::int32_t day,
															   const std::string& hazard) {
	// Day 1: dayNprobotlk_{torn,hail,wind}; Day 2: day2probotlk_any.
	std::string tag = hazard;
	if (hazard == "tornado") {
		tag = "torn";
	}
	const std::string url = std::format("{}day{}probotlk_{}.nolyr.geojson", kStaticBase, day, tag);
	Result<std::string> body =
		body_or_error(with_retry([&] { return impl_->http.get(url); }, impl_->retry));
	if (!body) {
		return std::unexpected(body.error());
	}
	try {
		return parse_probabilistic(*body, day, hazard);
	} catch (const std::exception& e) {
		return std::unexpected(Error::parse(e.what()));
	}
}

Result<Day48OutlookPayload> StaticFeedClient::day4_8(std::int32_t day) {
	const std::string url = std::format("{}day{}prob.nolyr.geojson", kStaticDay48Base, day);
	Result<std::string> body =
		body_or_error(with_retry([&] { return impl_->http.get(url); }, impl_->retry));
	if (!body) {
		return std::unexpected(body.error());
	}
	try {
		return parse_day4_8(*body, day);
	} catch (const std::exception& e) {
		return std::unexpected(Error::parse(e.what()));
	}
}

// ===================== ArcGISClient =====================

struct ArcGISClient::Impl {
	HttpClient http;
	RetryPolicy retry;
	explicit Impl(ClientConfig cfg) : http(std::move(cfg)) {}

	/// One paged query. Concatenated raw page bodies are returned; the
	/// ArcGISPager advances on `exceededTransferLimit`.
	Result<std::vector<std::string>> paged(const char* base, std::int32_t layer,
										   const QueryParams& p) {
		std::vector<std::string> pages;
		ArcGISPager pager;
		while (pager.has_more()) {
			std::string url = std::format("{}/{}/query?where={}&outFields={}&returnGeometry={}&f={}"
										  "&resultOffset={}&resultRecordCount={}",
										  base, layer, p.where, p.out_fields,
										  p.return_geometry ? "true" : "false", p.f, pager.offset(),
										  pager.page_size());
			if (!p.geometry.empty()) {
				url += std::format("&geometry={}&geometryType={}&spatialRel={}", p.geometry,
								   p.geometry_type, p.spatial_rel);
			}
			Result<std::string> body =
				body_or_error(with_retry([&] { return http.get(url); }, retry));
			if (!body) {
				return std::unexpected(body.error());
			}
			// Detect the ArcGIS truncation flag without a full parse.
			const bool exceeded =
				body->find("\"exceededTransferLimit\":true") != std::string::npos ||
				body->find("\"exceededTransferLimit\": true") != std::string::npos;
			pages.push_back(std::move(*body));
			pager.advance(exceeded);
		}
		return pages;
	}
};

ArcGISClient::ArcGISClient(ClientConfig config)
	: impl_(std::make_unique<Impl>(std::move(config))) {}
ArcGISClient::~ArcGISClient() = default;
ArcGISClient::ArcGISClient(ArcGISClient&&) noexcept = default;
ArcGISClient& ArcGISClient::operator=(ArcGISClient&&) noexcept = default;

namespace {

// SPC_wx_outlks MapServer layer ids (documented layout).
std::int32_t cat_layer(std::int32_t day) {
	if (day == 1) {
		return 1;
	}
	if (day == 2) {
		return 9;
	}
	return 17; // day 3
}

std::int32_t prob_layer(std::int32_t day, const std::string& hazard) {
	if (day == 1) {
		if (hazard == "tornado") {
			return 3;
		}
		if (hazard == "hail") {
			return 5;
		}
		return 7; // wind
	}
	return 15; // day 2 "any"
}

std::int32_t fire_layer(std::int32_t day) {
	// SPC_firewx: Day1 Outlook = layer 1, Day2 = 4, Day3 = 6 ...
	if (day == 1) {
		return 1;
	}
	if (day == 2) {
		return 4;
	}
	return 6;
}

} // namespace

Result<CategoricalOutlookPayload> ArcGISClient::query_categorical(std::int32_t day) {
	QueryParams p;
	// Request GeoJSON so the VERBATIM parse_categorical (a GeoJSON-only
	// walker, parity-critical) consumes it unchanged. parse_esri_rings is
	// proven equivalent (test_arcgis) but the convective path must stay
	// byte-for-byte the spc-data parser, so we feed it its native shape.
	p.f = "geojson";
	Result<std::vector<std::string>> pages = impl_->paged(kArcGisOutlks, cat_layer(day), p);
	if (!pages) {
		return std::unexpected(pages.error());
	}
	CategoricalOutlookPayload out;
	out.day_offset = day;
	for (const std::string& body : *pages) {
		try {
			CategoricalOutlookPayload pg = parse_categorical(body, day);
			out.features.insert(out.features.end(), pg.features.begin(), pg.features.end());
		} catch (const std::exception& e) {
			return std::unexpected(Error::parse(e.what()));
		}
	}
	return out;
}

Result<ProbOutlookPayload> ArcGISClient::query_probabilistic(std::int32_t day,
															 const std::string& hazard) {
	QueryParams p;
	// GeoJSON for the verbatim parse_probabilistic (see query_categorical).
	p.f = "geojson";
	Result<std::vector<std::string>> pages =
		impl_->paged(kArcGisOutlks, prob_layer(day, hazard), p);
	if (!pages) {
		return std::unexpected(pages.error());
	}
	ProbOutlookPayload out;
	out.day_offset = day;
	out.hazard = hazard;
	for (const std::string& body : *pages) {
		try {
			ProbOutlookPayload pg = parse_probabilistic(body, day, hazard);
			out.features.insert(out.features.end(), pg.features.begin(), pg.features.end());
		} catch (const std::exception& e) {
			return std::unexpected(Error::parse(e.what()));
		}
	}
	return out;
}

Result<FireWeatherPayload> ArcGISClient::query_fire_weather(std::int32_t day) {
	QueryParams p;
	Result<std::vector<std::string>> pages = impl_->paged(kArcGisFirewx, fire_layer(day), p);
	if (!pages) {
		return std::unexpected(pages.error());
	}
	FireWeatherPayload out;
	out.day = day;
	for (const std::string& body : *pages) {
		try {
			FireWeatherPayload pg = parse_fire_weather(body, day);
			out.features.insert(out.features.end(), pg.features.begin(), pg.features.end());
		} catch (const std::exception& e) {
			return std::unexpected(Error::parse(e.what()));
		}
	}
	return out;
}

Result<WatchPayload> ArcGISClient::query_active_watches() {
	// Active watches live in the hazards service; expose the raw escape
	// hatch consumers can also use. Layer 1 of SPC_wx_outlks is categorical,
	// so watches use the dedicated query_layer path with the watch parser.
	QueryParams p;
	Result<std::vector<std::string>> pages = query_layer(0, p); // placeholder layer
	if (!pages) {
		return std::unexpected(pages.error());
	}
	WatchPayload out;
	for (const std::string& body : *pages) {
		try {
			WatchPayload pg = parse_watches(body);
			out.watches.insert(out.watches.end(), pg.watches.begin(), pg.watches.end());
		} catch (const std::exception& e) {
			return std::unexpected(Error::parse(e.what()));
		}
	}
	return out;
}

Result<MesoscalePayload> ArcGISClient::query_active_md() {
	QueryParams p;
	Result<std::vector<std::string>> pages = impl_->paged(kArcGisMd, 0, p);
	if (!pages) {
		return std::unexpected(pages.error());
	}
	MesoscalePayload out;
	for (const std::string& body : *pages) {
		try {
			MesoscalePayload pg = parse_mesoscale_discussions(body);
			out.discussions.insert(out.discussions.end(), pg.discussions.begin(),
								   pg.discussions.end());
		} catch (const std::exception& e) {
			return std::unexpected(Error::parse(e.what()));
		}
	}
	return out;
}

Result<StormReportPayload> ArcGISClient::query_storm_reports() {
	// SPC storm reports are best sourced from IEM (ArchiveClient); the
	// MapServer has no LSR layer. Surface a clear error so callers route to
	// ArchiveClient instead of silently returning empty.
	return std::unexpected(
		Error::invalid_request("storm reports are served by ArchiveClient (IEM), not the "
							   "SPC ArcGIS MapServer"));
}

Result<std::vector<std::string>> ArcGISClient::query_layer(std::int32_t layer_id,
														   const QueryParams& params) {
	return impl_->paged(kArcGisOutlks, layer_id, params);
}

// ===================== ArchiveClient =====================

struct ArchiveClient::Impl {
	HttpClient http;
	RetryPolicy retry;
	RateLimiter limiter;

	explicit Impl(ClientConfig cfg)
		: http(std::move(cfg)), retry([] {
			  // Conservative: IEM is a courtesy third party.
			  RetryPolicy r;
			  r.max_attempts = 4;
			  r.initial_delay = std::chrono::milliseconds{500};
			  return r;
		  }()),
		  limiter(RateLimiter::Config{}) {}
};

ArchiveClient::ArchiveClient(ClientConfig config)
	: impl_(std::make_unique<Impl>(std::move(config))) {}
ArchiveClient::~ArchiveClient() = default;
ArchiveClient::ArchiveClient(ArchiveClient&&) noexcept = default;
ArchiveClient& ArchiveClient::operator=(ArchiveClient&&) noexcept = default;

Result<WatchPayload> ArchiveClient::watches(const std::string& ts) {
	if (!impl_->limiter.acquire()) {
		return std::unexpected(Error::rate_limited("IEM rate limit"));
	}
	std::string url = std::format("{}json/spcwatch.py", kIemBase);
	if (!ts.empty()) {
		url += std::format("?ts={}", ts);
	}
	Result<std::string> body =
		body_or_error(with_retry([&] { return impl_->http.get(url); }, impl_->retry));
	if (!body) {
		return std::unexpected(body.error());
	}
	try {
		return parse_watches(*body);
	} catch (const std::exception& e) {
		return std::unexpected(Error::parse(e.what()));
	}
}

Result<StormReportPayload> ArchiveClient::storm_reports(const std::string& start_iso,
														const std::string& end_iso,
														const std::string& wfo) {
	if (!impl_->limiter.acquire()) {
		return std::unexpected(Error::rate_limited("IEM rate limit"));
	}
	std::string url =
		std::format("{}geojson/lsr.geojson?sts={}&ets={}", kIemBase, start_iso, end_iso);
	if (!wfo.empty()) {
		url += std::format("&wfo={}", wfo);
	}
	Result<std::string> body =
		body_or_error(with_retry([&] { return impl_->http.get(url); }, impl_->retry));
	if (!body) {
		return std::unexpected(body.error());
	}
	try {
		return parse_storm_reports(*body);
	} catch (const std::exception& e) {
		return std::unexpected(Error::parse(e.what()));
	}
}

} // namespace spc
