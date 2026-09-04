#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace spc {

/// Error codes for SPC SDK operations.
enum class ErrorCode {
	Ok = 0,
	NetworkError,
	RateLimited,
	ServerError,
	/// A genuine fault: a bad URL, a retired product, a renamed MapServer
	/// path, or a wrong ArcGIS layer id. Every 404 that is not an SPC static
	/// feed lands here — including a logical ArcGIS `{"error":{"code":404}}`
	/// envelope, which is how a retired MapServer path is reported.
	NotFound,
	/// SPC returns HTTP 404 for "no active outlook" (e.g. overnight day-1
	/// probabilistic). This is a normal, expected state — distinct from a
	/// genuine `NotFound` (bad URL / retired product). Consumers treat it as
	/// "clear the rows for this day/hazard", not as an error. Only
	/// `StaticFeedClient` produces it: see `Feed404`.
	FeedUnavailable,
	InvalidRequest,
	ParseError,
	Unknown
};

/// Convert ErrorCode to string.
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

/// How a transport-level HTTP 404 is to be read for the feed that answered.
///
/// `Error::from_response` cannot tell a "no active outlook" 404 from a
/// retired endpoint by looking at the status alone, so the caller — which
/// knows which host it addressed — states the semantics explicitly.
enum class Feed404 : std::uint8_t {
	/// A 404 is a fault (bad URL / retired product) -> `ErrorCode::NotFound`.
	/// The safe default: every feed except the SPC static products.
	NotFound,
	/// SPC's static `.nolyr.geojson` products answer 404 with an HTML page
	/// when nothing is issued -> `ErrorCode::FeedUnavailable`.
	NoActiveOutlook,
};

/// Error information returned by SDK operations.
struct Error {
	ErrorCode code;
	std::string message;
	/// Transport HTTP status. For a logical ArcGIS failure (reported over
	/// HTTP 200) this carries the ArcGIS error code when that code is in the
	/// 100..599 HTTP range, and 0 otherwise — ArcGIS also uses codes such as
	/// 1000 that are not HTTP statuses. See `from_arcgis`.
	int http_status{0};
	std::string detail;

	[[nodiscard]] constexpr bool is_ok() const noexcept { return code == ErrorCode::Ok; }

	/// True for SPC's "no active outlook" 404 — callers branch on this to
	/// clear the corresponding rows instead of surfacing an error.
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

	/// Create an Error from an HTTP response status code and body.
	///
	/// `semantics` decides what a 404 means for the feed that answered:
	/// `Feed404::NoActiveOutlook` (SPC static products only) yields
	/// `FeedUnavailable`; the default `Feed404::NotFound` yields `NotFound`.
	[[nodiscard]] static Error from_response(int status, const std::string& body,
											 Feed404 semantics = Feed404::NotFound);

	/// Create an Error from an ArcGIS logical failure envelope
	/// (`{"error":{"code":...,"message":...}}`), which the MapServer reports
	/// over HTTP 200. A code of 404 — how a renamed or retired service path
	/// is reported — is a genuine `NotFound`, never `FeedUnavailable`.
	[[nodiscard]] static Error from_arcgis(int arcgis_code, const std::string& body);
};

/// Result type for SDK operations.
template <typename T>
using Result = std::expected<T, Error>;

} // namespace spc
