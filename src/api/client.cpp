/// @file client.cpp
/// @brief StaticFeedClient, ArcGISClient, and ArchiveClient.

#include "api/call_budget.hpp"
#include "models/from_tree.hpp"
#include "models/json.hpp"
#include "spc/api.hpp"
#include "spc/pagination.hpp"
#include "spc/rate_limit.hpp"
#include "spc/retry.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace spc {

namespace {

using detail::CallBudget;
using detail::Json;

constexpr std::string_view kStaticOutlookBase = "https://www.spc.noaa.gov/products/outlook/";
constexpr std::string_view kStaticDay48Base = "https://www.spc.noaa.gov/products/exper/day4-8/";
constexpr std::string_view kArcGisOutlooks =
	"https://mapservices.weather.noaa.gov/vector/rest/services/outlooks/SPC_wx_outlks/MapServer";
constexpr std::string_view kArcGisFireWeather = "https://mapservices.weather.noaa.gov/vector/rest/"
												"services/fire_weather/SPC_firewx/MapServer";
constexpr std::string_view kArcGisMesoscale =
	"https://mapservices.weather.noaa.gov/vector/rest/"
	"services/outlooks/spc_mesoscale_discussion/MapServer";
constexpr std::string_view kIemBase = "https://mesonet.agron.iastate.edu/";

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

// Layer ids and names as published on 2026-09-03; the live check in
// tools/verify_arcgis_metadata.py compares them with NOAA's metadata.
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

/// The Day 1-3 probability layer for `day` and `hazard`, or nullptr.
const LayerDescriptor* day1_3_probability_layer(std::int32_t day, std::string_view hazard) {
	return day <= 3 ? find_layer(LayerProduct::Probability, day, hazard) : nullptr;
}

Error unsupported_probability() {
	return Error::invalid_request(
		"probabilistic outlook requires tornado, hail, or wind on day 1 or 2, or severe on day 3");
}

/// The day's two fire-weather layers, in published order, or empty when the
/// day has none.
std::vector<const LayerDescriptor*> fire_weather_layers(std::int32_t day) {
	std::vector<const LayerDescriptor*> layers;
	for (const LayerDescriptor& descriptor : kLayers) {
		if (descriptor.product == LayerProduct::FireWeather && descriptor.day == day) {
			layers.push_back(&descriptor);
		}
	}
	return layers;
}

FireWeatherLayer fire_weather_layer(std::string_view subtype) {
	if (subtype == "outlook") {
		return FireWeatherLayer::Outlook;
	}
	return subtype == "dry-thunderstorm" ? FireWeatherLayer::DryThunderstorm
										 : FireWeatherLayer::WindLowHumidity;
}

std::shared_ptr<HttpTransport> usable_transport(std::shared_ptr<HttpTransport> transport) {
	if (transport != nullptr) {
		return transport;
	}
	return std::make_shared<HttpClient>();
}

/// "any" is an older spelling of day 3's "severe".
std::string_view normalized_hazard(std::int32_t day, std::string_view hazard) {
	return day == 3 && hazard == "any" ? std::string_view{"severe"} : hazard;
}

/// RFC 3986 unreserved characters, spelled out: std::isalnum depends on the C
/// locale and accepts bytes above 0x7F in UTF-8 locales on macOS.
bool is_unreserved(unsigned char ch) noexcept {
	return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
		   ch == '-' || ch == '.' || ch == '_' || ch == '~';
}

std::string percent_encode(std::string_view value) {
	constexpr std::string_view kHex = "0123456789ABCDEF";
	std::string encoded;
	encoded.reserve(value.size());
	for (const char c : value) {
		const unsigned char ch = static_cast<unsigned char>(c);
		if (is_unreserved(ch)) {
			encoded.push_back(c);
		} else {
			encoded.push_back('%');
			encoded.push_back(kHex[ch >> 4U]);
			encoded.push_back(kHex[ch & 0x0FU]);
		}
	}
	return encoded;
}

/// Map a transport result to its body. `semantics` says what a 404 means for
/// the feed that answered.
Result<std::string> body_or_error(Result<HttpResponse> response, Feed404 semantics) {
	if (!response) {
		return std::unexpected(std::move(response.error()));
	}
	if (response->status_code == 200) {
		return std::move(response->body);
	}
	return std::unexpected(Error::from_response(response->status_code, response->body, semantics));
}

/// The Error for the std::exception being handled; call it only inside a
/// catch block for one. ParseError, as for a parser's std::runtime_error: the
/// response could not be turned into a result. Other exceptions propagate:
/// catching everything would also swallow a thread cancellation.
Error current_failure() noexcept {
	try {
		try {
			throw;
		} catch (const std::exception& e) {
			return Error::parse(e.what());
		}
	} catch (const std::exception&) {
		// No memory for the message either; an empty string needs none.
		return {ErrorCode::ParseError, {}, 0, {}};
	}
}

/// Run `parse` and turn the std::exception it throws into a ParseError.
template <typename Parse>
Result<std::invoke_result_t<const Parse&>> parsed(const Parse& parse) {
	try {
		return parse();
	} catch (const std::exception&) {
		return std::unexpected(current_failure());
	}
}

/// Free `text`'s buffer now; clear() would keep the capacity.
void release(std::string& text) noexcept {
	std::string{}.swap(text);
}

/// Parse a fetched body once, free it, then build the result from the tree.
/// Malformed JSON fails as the public parse_* functions do.
template <typename Build>
Result<std::invoke_result_t<const Build&, const Json&>> built_from(Result<std::string> body,
																   const Build& build) {
	if (!body) {
		return std::unexpected(std::move(body.error()));
	}
	return parsed([&] {
		glz::expected<Json, std::string> root = detail::parse_owned_root(*body);
		release(*body);
		if (!root) {
			throw std::runtime_error(root.error());
		}
		return build(*root);
	});
}

// ===== ArcGIS paging =====

/// What paging needs from one ArcGIS response.
struct ArcGISEnvelope {
	std::int32_t feature_count{0};
	bool exceeded_transfer_limit{false};
};

bool envelope_flag(const Json& root, const char* key) {
	const Json* flag = detail::lookup(root, key);
	return flag != nullptr && flag->is_boolean() && flag->get<bool>();
}

/// `body` is the text `root` was parsed from, for an ArcGIS error's message.
Result<ArcGISEnvelope> inspect_arcgis_envelope(const Json& root, const std::string& body) {
	const Json* error = detail::lookup(root, "error");
	if (error != nullptr && error->is_object()) {
		// ArcGIS reports failures with HTTP 200 and an error object. The code
		// is passed on as sent; a missing or non-numeric one is 0, not a
		// made-up status.
		const std::int32_t code =
			detail::to_int32(detail::json_number_or_numeric_string(*error, "code")).value_or(0);
		return std::unexpected(Error::from_arcgis(code, body));
	}
	ArcGISEnvelope envelope;
	// f=json puts the flag at the root; f=geojson puts it at the root and
	// under `properties`.
	envelope.exceeded_transfer_limit = envelope_flag(root, "exceededTransferLimit");
	if (!envelope.exceeded_transfer_limit) {
		const Json* properties = detail::lookup(root, "properties");
		envelope.exceeded_transfer_limit =
			properties != nullptr && envelope_flag(*properties, "exceededTransferLimit");
	}
	const Json* features = detail::lookup(root, "features");
	if (features != nullptr && features->is_array()) {
		envelope.feature_count = static_cast<std::int32_t>(features->get_array().size());
	}
	return envelope;
}

/// One ArcGIS response, parsed once: the tree for the products and the
/// envelope for paging.
struct ArcGISPage {
	Json root;
	ArcGISEnvelope envelope;
};

Result<ArcGISPage> parse_arcgis_page(const std::string& body) {
	glz::expected<Json, std::string> root = detail::parse_owned_root(body);
	if (!root) {
		return std::unexpected(Error::parse(std::move(root.error())));
	}
	const Result<ArcGISEnvelope> envelope = inspect_arcgis_envelope(*root, body);
	if (!envelope) {
		return std::unexpected(envelope.error());
	}
	return ArcGISPage{std::move(*root), *envelope};
}

/// One accepted page: its body and what was parsed from it.
struct FetchedPage {
	std::string body;
	ArcGISPage page;
};

bool is_json_format(std::string_view format) {
	return format == "json" || format == "pjson" || format == "geojson";
}

/// One ArcGIS layer query.
struct LayerQuery {
	std::string_view service;
	std::int32_t layer{0};
	QueryParams params;
	/// Output spatial reference, e.g. "4326"; empty keeps the layer's own.
	std::string_view out_spatial_reference;
};

std::string page_url(const LayerQuery& query, const ArcGISPager& pager) {
	const QueryParams& p = query.params;
	std::string url =
		std::format("{}/{}/query?where={}&outFields={}&returnGeometry={}&f={}&resultOffset={}"
					"&resultRecordCount={}",
					query.service, query.layer, percent_encode(p.where),
					percent_encode(p.out_fields), p.return_geometry ? "true" : "false",
					percent_encode(p.f), pager.offset(), pager.page_size());
	if (!p.order_by_fields.empty()) {
		url += "&orderByFields=" + percent_encode(p.order_by_fields);
	}
	if (!p.geometry.empty()) {
		url += std::format("&geometry={}&geometryType={}&spatialRel={}", percent_encode(p.geometry),
						   percent_encode(p.geometry_type), percent_encode(p.spatial_rel));
	}
	if (!query.out_spatial_reference.empty()) {
		url += "&outSR=" + percent_encode(query.out_spatial_reference);
	}
	return url;
}

/// A query for one of the SDK's own products: every feature, ordered by
/// `objectid` (present on all NOAA SPC layers) so pages stay stable.
LayerQuery product_layer(std::string_view service, std::int32_t layer,
						 std::string_view format = "json") {
	LayerQuery query{service, layer, {}, {}};
	query.params.f = format;
	query.params.order_by_fields = "objectid";
	return query;
}

} // namespace

// ===================== StaticFeedClient =====================

struct StaticFeedClient::Impl {
	std::shared_ptr<HttpTransport> http;
	RetryPolicy retry;

	explicit Impl(std::shared_ptr<HttpTransport> transport)
		: http(usable_transport(std::move(transport))) {}

	/// GET `url`; a 404 means SPC has not issued the product.
	[[nodiscard]] Result<std::string> fetch(const std::string& url) const {
		return body_or_error(with_retry([&] { return http->get(url); }, retry),
							 Feed404::NoActiveOutlook);
	}
};

StaticFeedClient::StaticFeedClient(ClientConfig config)
	: impl_(std::make_unique<Impl>(std::make_shared<HttpClient>(std::move(config)))) {}
StaticFeedClient::StaticFeedClient(std::shared_ptr<HttpTransport> transport)
	: impl_(std::make_unique<Impl>(std::move(transport))) {}
StaticFeedClient::~StaticFeedClient() = default;
StaticFeedClient::StaticFeedClient(StaticFeedClient&&) noexcept = default;
StaticFeedClient& StaticFeedClient::operator=(StaticFeedClient&&) noexcept = default;

Result<CategoricalOutlookPayload> StaticFeedClient::day_categorical(std::int32_t day) const try {
	if (day < 1 || day > 3) {
		return std::unexpected(
			Error::invalid_request("categorical outlook day must be 1, 2, or 3"));
	}
	return built_from(
		impl_->fetch(std::format("{}day{}otlk_cat.nolyr.geojson", kStaticOutlookBase, day)),
		[day](const Json& root) { return detail::categorical_from_tree(root, day); });
} catch (const std::exception&) {
	return std::unexpected(current_failure());
}

Result<ProbOutlookPayload> StaticFeedClient::day_probabilistic(std::int32_t day,
															   std::string_view hazard) const try {
	const std::string_view normalized = normalized_hazard(day, hazard);
	if (day1_3_probability_layer(day, normalized) == nullptr) {
		return std::unexpected(unsupported_probability());
	}
	// SPC names the files day1otlk_torn, day2otlk_hail, ..., and day3otlk_prob.
	const std::string_view tag = normalized == "tornado" ? std::string_view{"torn"} : normalized;
	const std::string url =
		day == 3 ? std::format("{}day3otlk_prob.nolyr.geojson", kStaticOutlookBase)
				 : std::format("{}day{}otlk_{}.nolyr.geojson", kStaticOutlookBase, day, tag);
	return built_from(impl_->fetch(url), [day, normalized](const Json& root) {
		return detail::probabilistic_from_tree(root, day, normalized);
	});
} catch (const std::exception&) {
	return std::unexpected(current_failure());
}

Result<Day48OutlookPayload> StaticFeedClient::day4_8(std::int32_t day) const try {
	if (day < 4 || day > 8) {
		return std::unexpected(
			Error::invalid_request("extended outlook day must be between 4 and 8"));
	}
	return built_from(impl_->fetch(std::format("{}day{}prob.nolyr.geojson", kStaticDay48Base, day)),
					  [day](const Json& root) { return detail::day4_8_from_tree(root, day); });
} catch (const std::exception&) {
	return std::unexpected(current_failure());
}

// ===================== ArcGISClient =====================

struct ArcGISClient::Impl {
	std::shared_ptr<HttpTransport> http;
	RetryPolicy retry;
	/// Total page bytes one call accepts.
	std::size_t max_call_bytes;

	explicit Impl(std::shared_ptr<HttpTransport> transport)
		: http(usable_transport(std::move(transport))),
		  max_call_bytes(detail::call_byte_limit(*http)) {}

	[[nodiscard]] CallBudget call_budget() const noexcept { return CallBudget{max_call_bytes}; }

	/// GET one page and parse it, retrying under `retry`. ArcGIS sends its own
	/// failures with HTTP 200, so the body is parsed inside the retry loop and
	/// a transient ArcGIS code is retried like the same HTTP status. A 404 is
	/// NotFound: ArcGIS reports an empty product as HTTP 200 with no features.
	[[nodiscard]] Result<FetchedPage> fetch_page(const std::string& url,
												 const CallBudget& budget) const {
		// What the latest attempt's HTTP 200 body parsed to.
		std::optional<Result<ArcGISPage>> page;
		Result<HttpResponse> response = with_retry(
			[&]() -> Result<HttpResponse> {
				page.reset();
				Result<HttpResponse> attempt = http->get(url);
				if (!attempt || attempt->status_code != 200) {
					return attempt;
				}
				if (!budget.admits(attempt->body.size())) {
					// Refused before parsing, which would hold several times the body.
					return std::unexpected(Error::invalid_request(
						"ArcGIS query pages exceeded ClientConfig::max_response_bytes in total"));
				}
				page.emplace(parse_arcgis_page(attempt->body));
				if (!page->has_value() && page->error().http_status != 0) {
					// An ArcGIS error whose code is an HTTP status: let
					// with_retry judge that status.
					attempt->status_code = static_cast<std::int16_t>(page->error().http_status);
				}
				return attempt;
			},
			retry);
		if (!response) {
			return std::unexpected(std::move(response.error()));
		}
		if (!page) {
			return std::unexpected(
				Error::from_response(response->status_code, response->body, Feed404::NotFound));
		}
		if (!page->has_value()) {
			return std::unexpected(std::move(page->error()));
		}
		return FetchedPage{std::move(response->body), std::move(**page)};
	}

	/// Fetch every page of `query`, charge each to `budget`, and hand `on_page`
	/// the page's body and tree as it arrives. `on_page` may take the body.
	template <typename OnPage>
		requires std::is_invocable_r_v<Result<void>, const OnPage&, std::string&, const Json&>
	[[nodiscard]] Result<void> paged(const LayerQuery& query, CallBudget& budget,
									 const OnPage& on_page) const {
		ArcGISPager pager;
		while (pager.has_more()) {
			Result<FetchedPage> fetched = fetch_page(page_url(query, pager), budget);
			if (!fetched) {
				return std::unexpected(std::move(fetched.error()));
			}
			budget.charge(fetched->body.size());
			const ArcGISEnvelope envelope = fetched->page.envelope;
			if (envelope.exceeded_transfer_limit && envelope.feature_count == 0) {
				// The offset would never move.
				return std::unexpected(
					Error::server("ArcGIS reported a truncated page containing no records"));
			}
			Result<void> accepted = on_page(fetched->body, fetched->page.root);
			if (!accepted) {
				return accepted;
			}
			pager.advance(envelope.feature_count, envelope.exceeded_transfer_limit);
		}
		if (pager.page_limit_reached()) {
			return std::unexpected(Error::server(
				std::format("ArcGIS paging did not converge within {} pages", pager.max_pages())));
		}
		return {};
	}

	/// Page through `query`, build each page's payload from its tree with
	/// `build`, and move the page's `items` into `payload`. Each page's body is
	/// freed before its payload is built, so the two are never alive together.
	template <typename Payload, typename Item, typename Build>
	[[nodiscard]] Result<Payload> paged_into(const LayerQuery& query, CallBudget& budget,
											 Payload payload, std::vector<Item> Payload::*items,
											 const Build& build) const {
		Result<void> done =
			paged(query, budget, [&](std::string& body, const Json& root) -> Result<void> {
				release(body);
				Result<Payload> page = parsed([&] { return build(root); });
				if (!page) {
					return std::unexpected(std::move(page.error()));
				}
				std::vector<Item>& target = payload.*items;
				std::vector<Item>& source = (*page).*items;
				if (target.empty()) {
					target = std::move(source);
				} else {
					target.insert(target.end(), std::make_move_iterator(source.begin()),
								  std::make_move_iterator(source.end()));
				}
				return {};
			});
		if (!done) {
			return std::unexpected(std::move(done.error()));
		}
		return payload;
	}
};

ArcGISClient::ArcGISClient(ClientConfig config)
	: impl_(std::make_unique<Impl>(std::make_shared<HttpClient>(std::move(config)))) {}
ArcGISClient::ArcGISClient(std::shared_ptr<HttpTransport> transport)
	: impl_(std::make_unique<Impl>(std::move(transport))) {}
ArcGISClient::~ArcGISClient() = default;
ArcGISClient::ArcGISClient(ArcGISClient&&) noexcept = default;
ArcGISClient& ArcGISClient::operator=(ArcGISClient&&) noexcept = default;

Result<CategoricalOutlookPayload> ArcGISClient::query_categorical(std::int32_t day) const try {
	const LayerDescriptor* descriptor = find_layer(LayerProduct::Categorical, day, "");
	if (descriptor == nullptr) {
		return std::unexpected(
			Error::invalid_request("categorical outlook day must be 1, 2, or 3"));
	}
	CategoricalOutlookPayload seed;
	seed.day_offset = day;
	CallBudget budget = impl_->call_budget();
	// Request GeoJSON to match parse_categorical().
	return impl_->paged_into(
		product_layer(kArcGisOutlooks, descriptor->id, "geojson"), budget, std::move(seed),
		&CategoricalOutlookPayload::features,
		[day](const Json& root) { return detail::categorical_from_tree(root, day); });
} catch (const std::exception&) {
	return std::unexpected(current_failure());
}

Result<ProbOutlookPayload> ArcGISClient::query_probabilistic(std::int32_t day,
															 std::string_view hazard) const try {
	const std::string_view normalized = normalized_hazard(day, hazard);
	const LayerDescriptor* descriptor = day1_3_probability_layer(day, normalized);
	if (descriptor == nullptr) {
		return std::unexpected(unsupported_probability());
	}
	ProbOutlookPayload seed;
	seed.day_offset = day;
	seed.hazard = normalized;
	CallBudget budget = impl_->call_budget();
	return impl_->paged_into(product_layer(kArcGisOutlooks, descriptor->id, "geojson"), budget,
							 std::move(seed), &ProbOutlookPayload::features,
							 [day, normalized](const Json& root) {
								 return detail::probabilistic_from_tree(root, day, normalized);
							 });
} catch (const std::exception&) {
	return std::unexpected(current_failure());
}

Result<ConditionalIntensityPayload>
ArcGISClient::query_conditional_intensity(std::int32_t day, std::string_view hazard) const try {
	const std::string_view normalized = normalized_hazard(day, hazard);
	const LayerDescriptor* descriptor =
		find_layer(LayerProduct::ConditionalIntensity, day, normalized);
	if (descriptor == nullptr) {
		return std::unexpected(
			Error::invalid_request("conditional intensity requires tornado, hail, or wind on day 1 "
								   "or 2, or severe on day 3"));
	}
	ConditionalIntensityPayload seed;
	seed.day = day;
	seed.hazard = normalized;
	CallBudget budget = impl_->call_budget();
	return impl_->paged_into(product_layer(kArcGisOutlooks, descriptor->id), budget,
							 std::move(seed), &ConditionalIntensityPayload::features,
							 [day, hazard = std::string{normalized},
							  projection = &budget.projection()](const Json& root) {
								 return detail::conditional_intensity_from_tree(root, day, hazard,
																				projection);
							 });
} catch (const std::exception&) {
	return std::unexpected(current_failure());
}

Result<Day48OutlookPayload> ArcGISClient::query_day4_8(std::int32_t day) const try {
	const LayerDescriptor* descriptor = find_layer(LayerProduct::Probability, day, "severe");
	if (descriptor == nullptr || day < 4) {
		return std::unexpected(
			Error::invalid_request("extended outlook day must be between 4 and 8"));
	}
	Day48OutlookPayload seed;
	seed.day = day;
	CallBudget budget = impl_->call_budget();
	return impl_->paged_into(product_layer(kArcGisOutlooks, descriptor->id), budget,
							 std::move(seed), &Day48OutlookPayload::features,
							 [day, projection = &budget.projection()](const Json& root) {
								 return detail::day4_8_from_tree(root, day, projection);
							 });
} catch (const std::exception&) {
	return std::unexpected(current_failure());
}

Result<FireWeatherPayload> ArcGISClient::query_fire_weather(std::int32_t day) const try {
	const std::vector<const LayerDescriptor*> layers = fire_weather_layers(day);
	if (layers.empty()) {
		return std::unexpected(
			Error::invalid_request("fire-weather outlook day must be between 1 and 8"));
	}
	FireWeatherPayload payload;
	payload.day = day;
	// One budget for both layers: together they are the payload.
	CallBudget budget = impl_->call_budget();
	for (const LayerDescriptor* descriptor : layers) {
		const FireWeatherLayer layer = fire_weather_layer(descriptor->subtype);
		LayerQuery query = product_layer(kArcGisFireWeather, descriptor->id);
		// These layers default to Web Mercator; ask for lon/lat.
		query.out_spatial_reference = "4326";
		Result<FireWeatherPayload> merged = impl_->paged_into(
			query, budget, std::move(payload), &FireWeatherPayload::features,
			[day, layer, projection = &budget.projection()](const Json& root) {
				return detail::fire_weather_from_tree(root, day, layer, projection);
			});
		if (!merged) {
			return merged;
		}
		payload = std::move(*merged);
	}
	return payload;
} catch (const std::exception&) {
	return std::unexpected(current_failure());
}

Result<MesoscalePayload> ArcGISClient::query_active_md() const try {
	CallBudget budget = impl_->call_budget();
	return impl_->paged_into(product_layer(kArcGisMesoscale, 0), budget, MesoscalePayload{},
							 &MesoscalePayload::discussions,
							 [projection = &budget.projection()](const Json& root) {
								 return detail::mesoscale_from_tree(root, projection);
							 });
} catch (const std::exception&) {
	return std::unexpected(current_failure());
}

Result<std::vector<std::string>> ArcGISClient::query_layer(ArcGISService service,
														   std::int32_t layer_id,
														   const QueryParams& params) const try {
	if (layer_id < 0) {
		return std::unexpected(Error::invalid_request("ArcGIS layer id must be non-negative"));
	}
	if (!is_json_format(params.f)) {
		return std::unexpected(
			Error::invalid_request("QueryParams::f must be json, pjson, or geojson"));
	}
	std::string_view base = kArcGisOutlooks;
	switch (service) {
		case ArcGISService::Outlooks:
			break;
		case ArcGISService::FireWeather:
			base = kArcGisFireWeather;
			break;
		case ArcGISService::MesoscaleDiscussions:
			base = kArcGisMesoscale;
			break;
	}
	// The budget's byte limit bounds what `pages` retains.
	CallBudget budget = impl_->call_budget();
	std::vector<std::string> pages;
	Result<void> done =
		impl_->paged(LayerQuery{base, layer_id, params, {}}, budget,
					 [&pages](std::string& body, const Json& /*root*/) -> Result<void> {
						 pages.push_back(std::move(body));
						 return {};
					 });
	if (!done) {
		return std::unexpected(std::move(done.error()));
	}
	return pages;
} catch (const std::exception&) {
	return std::unexpected(current_failure());
}

// ===================== ArchiveClient =====================

namespace {

/// IEM is a courtesy service: back off slowly.
RetryPolicy archive_retry_policy() {
	RetryPolicy policy;
	policy.max_attempts = 4;
	policy.initial_delay = std::chrono::milliseconds{500};
	return policy;
}

/// Small bucket, and a bounded wait so a busy caller gets
/// `ErrorCode::RateLimited` instead of blocking indefinitely.
RateLimiter::Config archive_rate_limit() {
	RateLimiter::Config config;
	config.max_wait = std::chrono::seconds{5};
	return config;
}

} // namespace

struct ArchiveClient::Impl {
	std::shared_ptr<HttpTransport> http;
	RetryPolicy retry{archive_retry_policy()};
	mutable RateLimiter limiter{archive_rate_limit()};

	explicit Impl(std::shared_ptr<HttpTransport> transport)
		: http(usable_transport(std::move(transport))) {}

	/// GET with one rate-limit token per attempt, retries included: retries
	/// happen on 429/503, exactly when IEM wants less traffic.
	[[nodiscard]] Result<std::string> fetch(const std::string& url) const {
		return body_or_error(with_retry(
								 [&]() -> Result<HttpResponse> {
									 if (!limiter.acquire()) {
										 return std::unexpected(
											 Error::rate_limited("IEM rate limit"));
									 }
									 return http->get(url);
								 },
								 retry),
							 Feed404::NotFound);
	}
};

ArchiveClient::ArchiveClient(ClientConfig config)
	: impl_(std::make_unique<Impl>(std::make_shared<HttpClient>(std::move(config)))) {}
ArchiveClient::ArchiveClient(std::shared_ptr<HttpTransport> transport)
	: impl_(std::make_unique<Impl>(std::move(transport))) {}
ArchiveClient::~ArchiveClient() = default;
ArchiveClient::ArchiveClient(ArchiveClient&&) noexcept = default;
ArchiveClient& ArchiveClient::operator=(ArchiveClient&&) noexcept = default;

Result<WatchPayload> ArchiveClient::watches(std::string_view timestamp) const try {
	std::string url = std::format("{}json/spcwatch.py", kIemBase);
	if (!timestamp.empty()) {
		url += "?ts=" + percent_encode(timestamp);
	}
	return built_from(impl_->fetch(url),
					  [](const Json& root) { return detail::watches_from_tree(root); });
} catch (const std::exception&) {
	return std::unexpected(current_failure());
}

// A start/end pair cannot be told apart by type.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
Result<StormReportPayload> ArchiveClient::storm_reports(std::string_view start_iso,
														std::string_view end_iso,
														std::string_view wfo) const try {
	// NOLINTEND(bugprone-easily-swappable-parameters)
	// Encode every value: an ISO offset's '+' would otherwise decode as a space.
	std::string url = std::format("{}geojson/lsr.geojson?sts={}&ets={}", kIemBase,
								  percent_encode(start_iso), percent_encode(end_iso));
	if (!wfo.empty()) {
		// IEM filters on `wfos`; it silently ignores `wfo` here.
		url += "&wfos=" + percent_encode(wfo);
	}
	return built_from(impl_->fetch(url),
					  [](const Json& root) { return detail::storm_reports_from_tree(root); });
} catch (const std::exception&) {
	return std::unexpected(current_failure());
}

} // namespace spc
