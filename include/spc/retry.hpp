#pragma once

#include "spc/error.hpp"
#include "spc/http_client.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

namespace spc {

/// Retry policy. Logic verbatim from ncei-cpp/include/ncei/retry.hpp
/// (namespace adapted). `ArchiveClient` (IEM) uses a conservative instance;
/// the SPC / ArcGIS paths default to a light policy since those endpoints
/// are stable.
struct RetryPolicy {
	std::chrono::milliseconds initial_delay{200};
	std::chrono::milliseconds max_delay{30000};
	double backoff_multiplier{2.0};
	double jitter_factor{0.1};
	std::uint8_t max_attempts{3};
	bool retry_on_network_error{true};
	bool retry_on_rate_limit{true};
	bool retry_on_server_error{true};
};

struct RetryResult {
	std::chrono::milliseconds total_delay;
	std::uint8_t attempts_made;
	bool succeeded;
};

[[nodiscard]] inline bool should_retry(const HttpResponse& response,
									   const RetryPolicy& policy) noexcept {
	if (policy.retry_on_rate_limit &&
		(response.status_code == 429 || response.status_code == 503)) {
		return true;
	}
	if (policy.retry_on_server_error && response.status_code >= 500) {
		return true;
	}
	return false;
}

[[nodiscard]] inline bool should_retry(const Error& error, const RetryPolicy& policy) noexcept {
	if (policy.retry_on_network_error && error.code == ErrorCode::NetworkError) {
		return true;
	}
	return false;
}

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

	// Clamp AFTER the jitter: clamping first let jitter carry the delay back
	// above the documented ceiling by up to jitter_factor.
	delay_ms = std::min(delay_ms, static_cast<double>(policy.max_delay.count()));
	delay_ms = std::max(delay_ms, 0.0);

	return std::chrono::milliseconds{static_cast<std::int64_t>(delay_ms)};
}

/// The `Retry-After` delay a 429/503 asked for, or zero.
///
/// Only the delta-seconds form is parsed; the HTTP-date form falls back to the
/// computed backoff. 429 and 503 are exactly the responses `should_retry`
/// fires on and exactly the ones that carry this header, and `HttpResponse`
/// already captures every header.
[[nodiscard]] inline std::chrono::milliseconds retry_after(const HttpResponse& response) {
	for (const std::pair<std::string, std::string>& header : response.headers) {
		const std::string& name = header.first;
		if (name.size() != 11) {
			continue;
		}
		bool matches = true;
		const std::string_view target{"retry-after"};
		for (std::size_t i = 0; i < name.size(); ++i) {
			const char lowered =
				name[i] >= 'A' && name[i] <= 'Z' ? static_cast<char>(name[i] - 'A' + 'a') : name[i];
			if (lowered != target[i]) {
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

/// Execute an HTTP operation with exponential-backoff retry.
template <typename Operation>
[[nodiscard]] Result<HttpResponse> with_retry(Operation&& operation, const RetryPolicy& policy) {
	std::chrono::milliseconds total_delay{0};
	// max_attempts == 0 would otherwise skip the operation entirely and report
	// a fabricated network error for a request that was never made. Iterate a
	// wider counter so the top of the uint8 range cannot wrap.
	const std::uint16_t attempts = std::max<std::uint16_t>(1, policy.max_attempts);

	for (std::uint16_t attempt = 1; attempt <= attempts; ++attempt) {
		Result<HttpResponse> result = operation();
		const std::uint8_t attempt_number = static_cast<std::uint8_t>(attempt);

		if (result.has_value()) {
			if (should_retry(*result, policy) && attempt < attempts) {
				// Respect a server that told us how long to wait.
				const std::chrono::milliseconds requested = retry_after(*result);
				const std::chrono::milliseconds delay =
					std::min(std::max(calculate_retry_delay(attempt_number, policy), requested),
							 policy.max_delay);
				total_delay += delay;
				std::this_thread::sleep_for(delay);
				continue;
			}
			return result;
		}

		if (should_retry(result.error(), policy) && attempt < attempts) {
			std::chrono::milliseconds delay = calculate_retry_delay(attempt_number, policy);
			total_delay += delay;
			std::this_thread::sleep_for(delay);
			continue;
		}

		return result;
	}

	return std::unexpected(Error::network("Max retry attempts exceeded"));
}

} // namespace spc
