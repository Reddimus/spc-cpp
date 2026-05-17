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
	NotFound,
	/// SPC returns HTTP 404 for "no active outlook" (e.g. overnight day-1
	/// probabilistic). This is a normal, expected state — distinct from a
	/// genuine `NotFound` (bad URL / retired product). Consumers treat it as
	/// "clear the rows for this day/hazard", not as an error.
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

/// Error information returned by SDK operations.
struct Error {
	ErrorCode code;
	std::string message;
	int http_status{0};
	std::string detail;

	[[nodiscard]] constexpr bool is_ok() const noexcept { return code == ErrorCode::Ok; }

	/// True for SPC's "no active outlook" 404 — callers branch on this to
	/// clear the corresponding rows instead of surfacing an error.
	[[nodiscard]] constexpr bool is_feed_unavailable() const noexcept {
		return code == ErrorCode::FeedUnavailable;
	}

	[[nodiscard]] static Error ok() { return {ErrorCode::Ok, ""}; }

	[[nodiscard]] static Error network(std::string msg) {
		return {ErrorCode::NetworkError, std::move(msg)};
	}

	[[nodiscard]] static Error parse(std::string msg) {
		return {ErrorCode::ParseError, std::move(msg)};
	}

	[[nodiscard]] static Error not_found(std::string msg) {
		return {ErrorCode::NotFound, std::move(msg)};
	}

	[[nodiscard]] static Error feed_unavailable(std::string msg) {
		return {ErrorCode::FeedUnavailable, std::move(msg)};
	}

	[[nodiscard]] static Error rate_limited(std::string msg) {
		return {ErrorCode::RateLimited, std::move(msg)};
	}

	[[nodiscard]] static Error server(std::string msg) {
		return {ErrorCode::ServerError, std::move(msg)};
	}

	[[nodiscard]] static Error invalid_request(std::string msg) {
		return {ErrorCode::InvalidRequest, std::move(msg)};
	}

	/// Create an Error from an HTTP response status code and body. SPC's 404
	/// "no active outlook" maps to `FeedUnavailable` (not `NotFound`).
	[[nodiscard]] static Error from_response(int status, const std::string& body);
};

/// Result type for SDK operations.
template <typename T>
using Result = std::expected<T, Error>;

} // namespace spc
