/// @file retry.hpp
/// @brief Exponential backoff with jitter, honoring `Retry-After`.

#pragma once

#include "spc/error.hpp"
#include "spc/http_client.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

namespace spc {

/// When and how often to retry. The SPC and ArcGIS clients use the defaults;
/// `ArchiveClient` uses a slower policy because IEM is a courtesy service.
struct RetryPolicy {
	std::chrono::milliseconds initial_delay{200};
	std::chrono::milliseconds max_delay{30000};
	double backoff_multiplier{2.0};
	double jitter_factor{0.1};	  ///< delay is scaled by a random factor in [1 - j, 1 + j]
	std::uint8_t max_attempts{3}; ///< total tries; 0 is treated as 1
	bool retry_on_network_error{true};
	bool retry_on_rate_limit{true};	  ///< HTTP 429 and 503
	bool retry_on_server_error{true}; ///< other HTTP 5xx
};

[[nodiscard]] inline bool should_retry(const HttpResponse& response,
									   const RetryPolicy& policy) noexcept {
	if (policy.retry_on_rate_limit &&
		(response.status_code == 429 || response.status_code == 503)) {
		return true;
	}
	return policy.retry_on_server_error && response.status_code >= 500;
}

[[nodiscard]] inline bool should_retry(const Error& error, const RetryPolicy& policy) noexcept {
	return policy.retry_on_network_error && error.code == ErrorCode::NetworkError;
}

/// Backoff before retry number `attempt` (1-based), jittered, then clamped to
/// [0, max_delay].
[[nodiscard]] inline std::chrono::milliseconds calculate_retry_delay(std::uint8_t attempt,
																	 const RetryPolicy& policy) {
	double delay_ms = static_cast<double>(policy.initial_delay.count());
	for (std::uint8_t i = 1; i < attempt; ++i) {
		delay_ms *= policy.backoff_multiplier;
	}

	if (policy.jitter_factor > 0) {
		static thread_local std::mt19937 rng{std::random_device{}()};
		std::uniform_real_distribution<double> dist(1.0 - policy.jitter_factor,
													1.0 + policy.jitter_factor);
		delay_ms *= dist(rng);
	}

	delay_ms = std::min(delay_ms, static_cast<double>(policy.max_delay.count()));
	delay_ms = std::max(delay_ms, 0.0);
	return std::chrono::milliseconds{static_cast<std::int64_t>(delay_ms)};
}

/// The delay a `Retry-After: <seconds>` header asks for, or zero. The
/// HTTP-date form is not parsed.
[[nodiscard]] inline std::chrono::milliseconds retry_after(const HttpResponse& response) {
	constexpr std::string_view kName{"retry-after"};
	for (const std::pair<std::string, std::string>& header : response.headers) {
		const std::string& name = header.first;
		if (name.size() != kName.size()) {
			continue;
		}
		bool matches = true;
		for (std::size_t i = 0; i < name.size(); ++i) {
			const char lowered =
				name[i] >= 'A' && name[i] <= 'Z' ? static_cast<char>(name[i] - 'A' + 'a') : name[i];
			if (lowered != kName[i]) {
				matches = false;
				break;
			}
		}
		if (!matches) {
			continue;
		}
		std::int64_t seconds = 0;
		const std::from_chars_result parsed = std::from_chars(
			header.second.data(), header.second.data() + header.second.size(), seconds);
		if (parsed.ec == std::errc{} && seconds > 0) {
			return std::chrono::milliseconds{seconds * 1000};
		}
		return std::chrono::milliseconds{0};
	}
	return std::chrono::milliseconds{0};
}

/// Run `operation` (returning `Result<HttpResponse>`) until it succeeds with a
/// non-retryable status, fails with a non-retryable error, or runs out of
/// attempts. Sleeps the calling thread between attempts.
template <typename Operation>
[[nodiscard]] Result<HttpResponse> with_retry(Operation operation, const RetryPolicy& policy) {
	// A wider counter, so max_attempts = 255 cannot wrap.
	const std::uint16_t attempts = std::max<std::uint16_t>(1, policy.max_attempts);
	for (std::uint16_t attempt = 1;; ++attempt) {
		Result<HttpResponse> result = operation();
		const bool last = attempt >= attempts;
		const std::uint8_t attempt_number = static_cast<std::uint8_t>(attempt);

		if (result.has_value()) {
			if (last || !should_retry(*result, policy)) {
				return result;
			}
			// Wait at least as long as the server asked, up to max_delay.
			const std::chrono::milliseconds delay = std::min(
				std::max(calculate_retry_delay(attempt_number, policy), retry_after(*result)),
				policy.max_delay);
			std::this_thread::sleep_for(delay);
			continue;
		}
		if (last || !should_retry(result.error(), policy)) {
			return result;
		}
		std::this_thread::sleep_for(calculate_retry_delay(attempt_number, policy));
	}
}

} // namespace spc
