/// @file error.hpp
/// @brief Error type and `Result<T>`, returned by every fallible SDK call.

#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace spc {

enum class ErrorCode {
	Ok = 0,
	/// Connection, TLS, timeout, or response-size failure.
	NetworkError,
	/// HTTP 429 or 503, or the SDK's own rate limit.
	RateLimited,
	/// HTTP 5xx, or an ArcGIS query that never finished paging.
	ServerError,
	/// A real fault: a bad URL, a retired product, or a wrong ArcGIS layer.
	NotFound,
	/// SPC has not issued this product right now; its static feeds answer
	/// HTTP 404. This is normal (day 1 probabilities overnight, for example),
	/// so treat it as "no data", not as a failure. Only `StaticFeedClient`
	/// returns it.
	FeedUnavailable,
	/// The request cannot succeed as asked: an unsupported day or hazard,
	/// HTTP 400, or an ArcGIS error such as an invalid parameter.
	InvalidRequest,
	/// The response was not the expected JSON.
	ParseError,
	Unknown
};

[[nodiscard]] constexpr std::string_view to_string(ErrorCode code) noexcept {
	switch (code) {
		case ErrorCode::Ok:
			return "Ok";
		case ErrorCode::NetworkError:
			return "NetworkError";
		case ErrorCode::RateLimited:
			return "RateLimited";
		case ErrorCode::ServerError:
			return "ServerError";
		case ErrorCode::NotFound:
			return "NotFound";
		case ErrorCode::FeedUnavailable:
			return "FeedUnavailable";
		case ErrorCode::InvalidRequest:
			return "InvalidRequest";
		case ErrorCode::ParseError:
			return "ParseError";
		case ErrorCode::Unknown:
			return "Unknown";
	}
	return "Unknown";
}

/// What an HTTP 404 means for the feed that returned it. The status alone
/// cannot tell "nothing issued" from "wrong URL", but the caller knows which
/// feed it asked.
enum class Feed404 : std::uint8_t {
	/// The URL is wrong or retired: `ErrorCode::NotFound`. The default.
	NotFound,
	/// SPC's static products 404 when nothing is issued:
	/// `ErrorCode::FeedUnavailable`.
	NoActiveOutlook,
};

struct Error {
	ErrorCode code{ErrorCode::Unknown};
	std::string message;
	/// HTTP status, or 0. For an ArcGIS error (sent with HTTP 200) this is the
	/// ArcGIS code when it lies in 100..599.
	int http_status{0};
	/// Extra context, such as ArcGIS error details or "arcgisCode=1000".
	std::string detail;

	[[nodiscard]] constexpr bool is_ok() const noexcept { return code == ErrorCode::Ok; }

	/// True when SPC has not issued the product: no data, not a failure.
	[[nodiscard]] constexpr bool is_feed_unavailable() const noexcept {
		return code == ErrorCode::FeedUnavailable;
	}

	[[nodiscard]] static Error ok() { return {ErrorCode::Ok, "", 0, ""}; }

	[[nodiscard]] static Error network(std::string msg) {
		return {ErrorCode::NetworkError, std::move(msg), 0, ""};
	}

	[[nodiscard]] static Error parse(std::string msg) {
		return {ErrorCode::ParseError, std::move(msg), 0, ""};
	}

	[[nodiscard]] static Error not_found(std::string msg) {
		return {ErrorCode::NotFound, std::move(msg), 0, ""};
	}

	[[nodiscard]] static Error feed_unavailable(std::string msg) {
		return {ErrorCode::FeedUnavailable, std::move(msg), 0, ""};
	}

	[[nodiscard]] static Error rate_limited(std::string msg) {
		return {ErrorCode::RateLimited, std::move(msg), 0, ""};
	}

	[[nodiscard]] static Error server(std::string msg) {
		return {ErrorCode::ServerError, std::move(msg), 0, ""};
	}

	[[nodiscard]] static Error invalid_request(std::string msg) {
		return {ErrorCode::InvalidRequest, std::move(msg), 0, ""};
	}

	/// An Error for a non-200 HTTP response. `semantics` says what a 404
	/// means for the feed that answered.
	[[nodiscard]] static Error from_response(int status, const std::string& body,
											 Feed404 semantics = Feed404::NotFound);

	/// An Error for an ArcGIS `{"error":{"code":...}}` body, which the
	/// MapServer sends with HTTP 200. Code 404 (a retired service) is
	/// `NotFound`.
	[[nodiscard]] static Error from_arcgis(int arcgis_code, const std::string& body);
};

/// The value, or the Error that prevented it.
template <typename T>
using Result = std::expected<T, Error>;

} // namespace spc
