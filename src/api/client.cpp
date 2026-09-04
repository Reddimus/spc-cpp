/// @file client.cpp
/// @brief StaticFeed / ArcGIS / Archive client implementations.

#include "spc/api.hpp"
#include "spc/models/common.hpp"
#include "spc/pagination.hpp"
#include "spc/rate_limit.hpp"
#include "spc/retry.hpp"

#include <array>
#include <cctype>
#include <format>
#include <memory>
#include <string>
#include <string_view>
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

enum class LayerProduct : std::uint8_t {
	Categorical,
	Probability,
	ConditionalIntensity,
	FireWeather,
};

struct LayerDescriptor {
	LayerProduct product;
	std::int32_t day;
	std::string_view subtype;
	std::int32_t id;
	std::string_view name;
};

constexpr std::array<LayerDescriptor, 38> kLayers{{
	{LayerProduct::Categorical, 1, "", 1, "Day 1 Categorical Outlook"},
	{LayerProduct::ConditionalIntensity, 1, "tornado", 2, "Day 1 Tornado Conditional Intensity"},
	{LayerProduct::Probability, 1, "tornado", 3, "Day 1 Probabilistic Tornado Outlook"},
	{LayerProduct::ConditionalIntensity, 1, "hail", 4, "Day 1 Hail Conditional Intensity"},
	{LayerProduct::Probability, 1, "hail", 5, "Day 1 Probabilistic Hail Outlook"},
	{LayerProduct::ConditionalIntensity, 1, "wind", 6, "Day 1 Wind Conditional Intensity"},
	{LayerProduct::Probability, 1, "wind", 7, "Day 1 Probabilistic Wind Outlook"},
	{LayerProduct::Categorical, 2, "", 9, "Day 2 Categorical Outlook"},
	{LayerProduct::ConditionalIntensity, 2, "tornado", 10, "Day 2 Tornado Conditional Intensity"},
	{LayerProduct::Probability, 2, "tornado", 11, "Day 2 Probabilistic Tornado Outlook"},
	{LayerProduct::ConditionalIntensity, 2, "hail", 12, "Day 2 Hail Conditional Intensity"},
	{LayerProduct::Probability, 2, "hail", 13, "Day 2 Probabilistic Hail Outlook"},
	{LayerProduct::ConditionalIntensity, 2, "wind", 14, "Day 2 Wind Conditional Intensity"},
	{LayerProduct::Probability, 2, "wind", 15, "Day 2 Probabilistic Wind Outlook"},
	{LayerProduct::Categorical, 3, "", 17, "Day 3 Categorical Outlook"},
	{LayerProduct::ConditionalIntensity, 3, "severe", 18, "Day 3 Severe Conditional Intensity"},
	{LayerProduct::Probability, 3, "severe", 19, "Day 3 Probabilistic Outlook"},
	{LayerProduct::Probability, 4, "severe", 21, "Day 4 Probabilistic Outlook"},
	{LayerProduct::Probability, 5, "severe", 22, "Day 5 Probabilistic Outlook"},
	{LayerProduct::Probability, 6, "severe", 23, "Day 6 Probabilistic Outlook"},
	{LayerProduct::Probability, 7, "severe", 24, "Day 7 Probabilistic Outlook"},
	{LayerProduct::Probability, 8, "severe", 25, "Day 8 Probabilistic Outlook"},
	{LayerProduct::FireWeather, 1, "outlook", 1, "Day 1 Outlook"},
	{LayerProduct::FireWeather, 1, "dry-thunderstorm", 2, "Day 1 Outlook Dry Thunderstorm"},
	{LayerProduct::FireWeather, 2, "outlook", 4, "Day 2 Outlook"},
	{LayerProduct::FireWeather, 2, "dry-thunderstorm", 5, "Day 2 Outlook Dry Thunderstorm"},
	{LayerProduct::FireWeather, 3, "dry-thunderstorm", 7, "Day 3 Dry Thunderstorm"},
	{LayerProduct::FireWeather, 3, "wind-low-humidity", 8, "Day 3 Winds and Low Humidity"},
	{LayerProduct::FireWeather, 4, "dry-thunderstorm", 10, "Day 4 Dry Thunderstorm"},
	{LayerProduct::FireWeather, 4, "wind-low-humidity", 11, "Day 4 Winds and Low Humidity"},
	{LayerProduct::FireWeather, 5, "dry-thunderstorm", 13, "Day 5 Dry Thunderstorm"},
	{LayerProduct::FireWeather, 5, "wind-low-humidity", 14, "Day 5 Winds and Low Humidity"},
	{LayerProduct::FireWeather, 6, "dry-thunderstorm", 16, "Day 6 Dry Thunderstorm"},
	{LayerProduct::FireWeather, 6, "wind-low-humidity", 17, "Day 6 Winds and Low Humidity"},
	{LayerProduct::FireWeather, 7, "dry-thunderstorm", 19, "Day 7 Dry Thunderstorm"},
	{LayerProduct::FireWeather, 7, "wind-low-humidity", 20, "Day 7 Winds and Low Humidity"},
	{LayerProduct::FireWeather, 8, "dry-thunderstorm", 22, "Day 8 Dry Thunderstorm"},
	{LayerProduct::FireWeather, 8, "wind-low-humidity", 23, "Day 8 Winds and Low Humidity"},
}};

const LayerDescriptor* find_layer(LayerProduct product, std::int32_t day,
								  std::string_view subtype) {
	for (const LayerDescriptor& descriptor : kLayers) {
		if (descriptor.product == product && descriptor.day == day &&
			descriptor.subtype == subtype) {
			return &descriptor;
		}
	}
	return nullptr;
}

std::shared_ptr<HttpTransport> usable_transport(std::shared_ptr<HttpTransport> transport) {
	if (transport != nullptr) {
		return transport;
	}
	return std::make_shared<HttpClient>();
}

std::string normalized_severe_hazard(std::int32_t day, const std::string& hazard) {
	if (day == 3 && hazard == "any") {
		return "severe";
	}
	return hazard;
}

std::string percent_encode(std::string_view value) {
	constexpr char kHex[] = "0123456789ABCDEF";
	std::string encoded;
	encoded.reserve(value.size());
	for (const unsigned char ch : value) {
		if (std::isalnum(ch) != 0 || ch == '-' || ch == '.' || ch == '_' || ch == '~') {
			encoded.push_back(static_cast<char>(ch));
		} else {
			encoded.push_back('%');
			encoded.push_back(kHex[ch >> 4U]);
			encoded.push_back(kHex[ch & 0x0FU]);
		}
	}
	return encoded;
}

const char* service_base(ArcGISService service) {
	switch (service) {
		case ArcGISService::Outlooks:
			return kArcGisOutlks;
		case ArcGISService::FireWeather:
			return kArcGisFirewx;
		case ArcGISService::MesoscaleDiscussions:
			return kArcGisMd;
	}
	return kArcGisOutlks;
}

/// What `paged()` needs from one ArcGIS response envelope: whether the server
/// truncated the result, and how many records it actually returned.
struct ArcGISEnvelope {
	std::int32_t feature_count{0};
	bool exceeded_transfer_limit{false};
};

bool envelope_flag(const Json& root, const char* key) {
	const Json* flag = detail::lookup(root, key);
	return flag != nullptr && flag->is_boolean() && flag->get<bool>();
}

Result<ArcGISEnvelope> inspect_arcgis_envelope(const std::string& body) {
	const glz::expected<Json, std::string> root = detail::parse_root(body);
	if (!root) {
		return std::unexpected(Error::parse(root.error()));
	}
	const Json* error = detail::lookup(*root, "error");
	if (error != nullptr && error->is_object()) {
		const double raw_code = detail::json_number_or_numeric_string(*error, "code");
		const int code = raw_code > 0.0 ? static_cast<int>(raw_code) : 400;
		// A logical ArcGIS failure travels over HTTP 200, so it must not go
		// through the HTTP status mapper: an ArcGIS code 404 (renamed or
		// retired service path) is a genuine NotFound, and a code like 1000
		// is not an HTTP status at all.
		return std::unexpected(Error::from_arcgis(code, body));
	}
	ArcGISEnvelope envelope;
	// Verified live against SPC_wx_outlks layer 1 with resultRecordCount=1:
	// `f=json` carries the flag at the root, `f=geojson` carries it at the
	// root AND under `properties`. Read both so either shape is honoured.
	envelope.exceeded_transfer_limit = envelope_flag(*root, "exceededTransferLimit");
	if (!envelope.exceeded_transfer_limit) {
		const Json* properties = detail::lookup(*root, "properties");
		envelope.exceeded_transfer_limit =
			properties != nullptr && envelope_flag(*properties, "exceededTransferLimit");
	}
	// Esri (`f=json`) and GeoJSON (`f=geojson`) both name the array `features`.
	const Json* features = detail::lookup(*root, "features");
	if (features != nullptr && features->is_array()) {
		envelope.feature_count = static_cast<std::int32_t>(features->get_array().size());
	}
	return envelope;
}

/// Map HTTP status to the right error; only a real body is handed to the
/// parser. `semantics` is the trust boundary: only the SPC static feeds may
/// read a 404 as "no active outlook" (FeedUnavailable). For every other host
/// a 404 is a retired or wrong URL, i.e. NotFound.
Result<std::string> body_or_error(Result<HttpResponse> r, Feed404 semantics) {
	if (!r) {
		return std::unexpected(r.error());
	}
	if (r->status_code == 200) {
		return std::move(r->body);
	}
	return std::unexpected(Error::from_response(r->status_code, r->body, semantics));
}

} // namespace

// ===================== StaticFeedClient =====================

struct StaticFeedClient::Impl {
	std::shared_ptr<HttpTransport> http;
	RetryPolicy retry;
	explicit Impl(ClientConfig cfg) : http(std::make_shared<HttpClient>(std::move(cfg))) {}
	explicit Impl(std::shared_ptr<HttpTransport> transport)
		: http(usable_transport(std::move(transport))) {}
};

StaticFeedClient::StaticFeedClient(ClientConfig config)
	: impl_(std::make_unique<Impl>(std::move(config))) {}
StaticFeedClient::StaticFeedClient(std::shared_ptr<HttpTransport> transport)
	: impl_(std::make_unique<Impl>(std::move(transport))) {}
StaticFeedClient::~StaticFeedClient() = default;
StaticFeedClient::StaticFeedClient(StaticFeedClient&&) noexcept = default;
StaticFeedClient& StaticFeedClient::operator=(StaticFeedClient&&) noexcept = default;

Result<CategoricalOutlookPayload> StaticFeedClient::day_categorical(std::int32_t day) {
	if (day < 1 || day > 3) {
		return std::unexpected(
			Error::invalid_request("categorical outlook day must be 1, 2, or 3"));
	}
	const std::string url = std::format("{}day{}otlk_cat.nolyr.geojson", kStaticBase, day);
	Result<std::string> body =
		body_or_error(with_retry([&] { return impl_->http->get(url); }, impl_->retry),
					  Feed404::NoActiveOutlook);
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
	const std::string normalized = normalized_severe_hazard(day, hazard);
	const LayerDescriptor* descriptor = find_layer(LayerProduct::Probability, day, normalized);
	if (descriptor == nullptr || day > 3) {
		return std::unexpected(
			Error::invalid_request("probabilistic outlook requires tornado, hail, or wind on day 1 "
								   "or 2, or severe on day 3"));
	}
	std::string tag = normalized;
	if (normalized == "tornado") {
		tag = "torn";
	}
	const std::string filename = day == 3 ? "day3otlk_prob.nolyr.geojson"
										  : std::format("day{}otlk_{}.nolyr.geojson", day, tag);
	const std::string url = std::string{kStaticBase} + filename;
	Result<std::string> body =
		body_or_error(with_retry([&] { return impl_->http->get(url); }, impl_->retry),
					  Feed404::NoActiveOutlook);
	if (!body) {
		return std::unexpected(body.error());
	}
	try {
		return parse_probabilistic(*body, day, normalized);
	} catch (const std::exception& e) {
		return std::unexpected(Error::parse(e.what()));
	}
}

Result<Day48OutlookPayload> StaticFeedClient::day4_8(std::int32_t day) {
	if (day < 4 || day > 8) {
		return std::unexpected(
			Error::invalid_request("extended outlook day must be between 4 and 8"));
	}
	const std::string url = std::format("{}day{}prob.nolyr.geojson", kStaticDay48Base, day);
	Result<std::string> body =
		body_or_error(with_retry([&] { return impl_->http->get(url); }, impl_->retry),
					  Feed404::NoActiveOutlook);
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
	std::shared_ptr<HttpTransport> http;
	RetryPolicy retry;
	explicit Impl(ClientConfig cfg) : http(std::make_shared<HttpClient>(std::move(cfg))) {}
	explicit Impl(std::shared_ptr<HttpTransport> transport)
		: http(usable_transport(std::move(transport))) {}

	/// One paged query. Concatenated raw page bodies are returned; the
	/// ArcGISPager advances on `exceededTransferLimit`.
	Result<std::vector<std::string>> paged(const char* base, std::int32_t layer,
										   const QueryParams& p,
										   std::string_view out_spatial_reference = {}) {
		std::vector<std::string> pages;
		ArcGISPager pager;
		while (pager.has_more()) {
			std::string url =
				std::format("{}/{}/query?where={}&outFields={}&returnGeometry={}&f={}"
							"&resultOffset={}&resultRecordCount={}",
							base, layer, percent_encode(p.where), percent_encode(p.out_fields),
							p.return_geometry ? "true" : "false", percent_encode(p.f),
							pager.offset(), pager.page_size());
			if (!p.geometry.empty()) {
				url += std::format("&geometry={}&geometryType={}&spatialRel={}",
								   percent_encode(p.geometry), percent_encode(p.geometry_type),
								   percent_encode(p.spatial_rel));
			}
			if (!out_spatial_reference.empty()) {
				url += "&outSR=" + percent_encode(out_spatial_reference);
			}
			Result<std::string> body =
				body_or_error(with_retry([&] { return http->get(url); }, retry), Feed404::NotFound);
			if (!body) {
				return std::unexpected(body.error());
			}
			const Result<ArcGISEnvelope> envelope = inspect_arcgis_envelope(*body);
			if (!envelope) {
				return std::unexpected(envelope.error());
			}
			if (envelope->exceeded_transfer_limit && envelope->feature_count == 0) {
				// The offset would never move: paging cannot converge.
				return std::unexpected(Error::server(
					"ArcGIS reported a truncated page containing no records"));
			}
			pages.push_back(std::move(*body));
			pager.advance(envelope->feature_count, envelope->exceeded_transfer_limit);
		}
		if (pager.page_limit_reached()) {
			return std::unexpected(Error::server(std::format(
				"ArcGIS paging did not converge within {} pages", pager.max_pages())));
		}
		return pages;
	}
};

ArcGISClient::ArcGISClient(ClientConfig config)
	: impl_(std::make_unique<Impl>(std::move(config))) {}
ArcGISClient::ArcGISClient(std::shared_ptr<HttpTransport> transport)
	: impl_(std::make_unique<Impl>(std::move(transport))) {}
ArcGISClient::~ArcGISClient() = default;
ArcGISClient::ArcGISClient(ArcGISClient&&) noexcept = default;
ArcGISClient& ArcGISClient::operator=(ArcGISClient&&) noexcept = default;

Result<CategoricalOutlookPayload> ArcGISClient::query_categorical(std::int32_t day) {
	const LayerDescriptor* descriptor = find_layer(LayerProduct::Categorical, day, "");
	if (descriptor == nullptr) {
		return std::unexpected(
			Error::invalid_request("categorical outlook day must be 1, 2, or 3"));
	}
	QueryParams p;
	// Request GeoJSON so the VERBATIM parse_categorical (a GeoJSON-only
	// walker, parity-critical) consumes it unchanged. parse_esri_rings is
	// proven equivalent (test_arcgis) but the convective path must stay
	// byte-for-byte the spc-data parser, so we feed it its native shape.
	p.f = "geojson";
	Result<std::vector<std::string>> pages = impl_->paged(kArcGisOutlks, descriptor->id, p);
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
	const std::string normalized = normalized_severe_hazard(day, hazard);
	const LayerDescriptor* descriptor = find_layer(LayerProduct::Probability, day, normalized);
	if (descriptor == nullptr || day > 3) {
		return std::unexpected(
			Error::invalid_request("probabilistic outlook requires tornado, hail, or wind on day 1 "
								   "or 2, or severe on day 3"));
	}
	QueryParams p;
	// GeoJSON for the verbatim parse_probabilistic (see query_categorical).
	p.f = "geojson";
	Result<std::vector<std::string>> pages = impl_->paged(kArcGisOutlks, descriptor->id, p);
	if (!pages) {
		return std::unexpected(pages.error());
	}
	ProbOutlookPayload out;
	out.day_offset = day;
	out.hazard = normalized;
	for (const std::string& body : *pages) {
		try {
			ProbOutlookPayload pg = parse_probabilistic(body, day, normalized);
			out.features.insert(out.features.end(), pg.features.begin(), pg.features.end());
		} catch (const std::exception& e) {
			return std::unexpected(Error::parse(e.what()));
		}
	}
	return out;
}

Result<ConditionalIntensityPayload>
ArcGISClient::query_conditional_intensity(std::int32_t day, const std::string& hazard) {
	const std::string normalized = normalized_severe_hazard(day, hazard);
	const LayerDescriptor* descriptor =
		find_layer(LayerProduct::ConditionalIntensity, day, normalized);
	if (descriptor == nullptr) {
		return std::unexpected(
			Error::invalid_request("conditional intensity requires tornado, hail, or wind on day 1 "
								   "or 2, or severe on day 3"));
	}
	QueryParams params;
	Result<std::vector<std::string>> pages = impl_->paged(kArcGisOutlks, descriptor->id, params);
	if (!pages) {
		return std::unexpected(pages.error());
	}
	ConditionalIntensityPayload output;
	output.day = day;
	output.hazard = normalized;
	for (const std::string& body : *pages) {
		try {
			ConditionalIntensityPayload page = parse_conditional_intensity(body, day, normalized);
			output.features.insert(output.features.end(), page.features.begin(),
								   page.features.end());
		} catch (const std::exception& e) {
			return std::unexpected(Error::parse(e.what()));
		}
	}
	return output;
}

Result<Day48OutlookPayload> ArcGISClient::query_day4_8(std::int32_t day) {
	const LayerDescriptor* descriptor = find_layer(LayerProduct::Probability, day, "severe");
	if (descriptor == nullptr || day < 4) {
		return std::unexpected(
			Error::invalid_request("extended outlook day must be between 4 and 8"));
	}
	QueryParams params;
	Result<std::vector<std::string>> pages = impl_->paged(kArcGisOutlks, descriptor->id, params);
	if (!pages) {
		return std::unexpected(pages.error());
	}
	Day48OutlookPayload output;
	output.day = day;
	for (const std::string& body : *pages) {
		try {
			Day48OutlookPayload page = parse_day4_8(body, day);
			output.features.insert(output.features.end(), page.features.begin(),
								   page.features.end());
		} catch (const std::exception& e) {
			return std::unexpected(Error::parse(e.what()));
		}
	}
	return output;
}

Result<FireWeatherPayload> ArcGISClient::query_fire_weather(std::int32_t day) {
	FireWeatherPayload out;
	out.day = day;
	const std::array<std::string_view, 2> subtypes =
		day <= 2 ? std::array<std::string_view, 2>{"outlook", "dry-thunderstorm"}
				 : std::array<std::string_view, 2>{"dry-thunderstorm", "wind-low-humidity"};
	for (const std::string_view subtype : subtypes) {
		const LayerDescriptor* descriptor = find_layer(LayerProduct::FireWeather, day, subtype);
		if (descriptor == nullptr) {
			return std::unexpected(
				Error::invalid_request("fire-weather outlook day must be between 1 and 8"));
		}
		// Fire-weather layers default to Web Mercator. The public model uses
		// longitude/latitude, so ask ArcGIS to transform geometry before parsing.
		Result<std::vector<std::string>> pages =
			impl_->paged(kArcGisFirewx, descriptor->id, {}, "4326");
		if (!pages) {
			return std::unexpected(pages.error());
		}
		for (const std::string& body : *pages) {
			try {
				FireWeatherLayer layer = FireWeatherLayer::WindLowHumidity;
				if (subtype == "outlook") {
					layer = FireWeatherLayer::Outlook;
				} else if (subtype == "dry-thunderstorm") {
					layer = FireWeatherLayer::DryThunderstorm;
				}
				FireWeatherPayload page = parse_fire_weather(body, day, layer);
				out.features.insert(out.features.end(), page.features.begin(), page.features.end());
			} catch (const std::exception& e) {
				return std::unexpected(Error::parse(e.what()));
			}
		}
	}
	return out;
}

Result<WatchPayload> ArcGISClient::query_active_watches() {
	return std::unexpected(Error::invalid_request(
		"NOAA WWA polygons omit SPC watch parameters; use ArchiveClient::watches()"));
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
	return query_layer(ArcGISService::Outlooks, layer_id, params);
}

Result<std::vector<std::string>>
ArcGISClient::query_layer(ArcGISService service, std::int32_t layer_id, const QueryParams& params) {
	if (layer_id < 0) {
		return std::unexpected(Error::invalid_request("ArcGIS layer id must be non-negative"));
	}
	return impl_->paged(service_base(service), layer_id, params);
}

// ===================== ArchiveClient =====================

struct ArchiveClient::Impl {
	std::shared_ptr<HttpTransport> http;
	RetryPolicy retry;
	RateLimiter limiter;

	explicit Impl(ClientConfig cfg)
		: http(std::make_shared<HttpClient>(std::move(cfg))), retry([] {
			  // Conservative: IEM is a courtesy third party.
			  RetryPolicy r;
			  r.max_attempts = 4;
			  r.initial_delay = std::chrono::milliseconds{500};
			  return r;
		  }()),
		  limiter(RateLimiter::Config{}) {}

	explicit Impl(std::shared_ptr<HttpTransport> transport)
		: http(usable_transport(std::move(transport))), retry([] {
			  RetryPolicy policy;
			  policy.max_attempts = 4;
			  policy.initial_delay = std::chrono::milliseconds{500};
			  return policy;
		  }()),
		  limiter(RateLimiter::Config{}) {}
};

ArchiveClient::ArchiveClient(ClientConfig config)
	: impl_(std::make_unique<Impl>(std::move(config))) {}
ArchiveClient::ArchiveClient(std::shared_ptr<HttpTransport> transport)
	: impl_(std::make_unique<Impl>(std::move(transport))) {}
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
		body_or_error(with_retry([&] { return impl_->http->get(url); }, impl_->retry),
					  Feed404::NotFound);
	if (!body) {
		return std::unexpected(body.error());
	}
	try {
		return parse_watches(*body);
	} catch (const std::exception& e) {
		return std::unexpected(Error::parse(e.what()));
	}
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
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
		body_or_error(with_retry([&] { return impl_->http->get(url); }, impl_->retry),
					  Feed404::NotFound);
	if (!body) {
		return std::unexpected(body.error());
	}
	try {
		return parse_storm_reports(*body);
	} catch (const std::exception& e) {
		return std::unexpected(Error::parse(e.what()));
	}
}
// NOLINTEND(bugprone-easily-swappable-parameters)

} // namespace spc
