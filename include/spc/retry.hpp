#pragma once

#include "spc/error.hpp"
#include "spc/http_client.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <random>
#include <thread>

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

	delay_ms = std::min(delay_ms, static_cast<double>(policy.max_delay.count()));

	if (policy.jitter_factor > 0) {
		static thread_local std::mt19937 rng{std::random_device{}()};
		std::uniform_real_distribution<double> dist(1.0 - policy.jitter_factor,
													1.0 + policy.jitter_factor);
		delay_ms *= dist(rng);
	}

	return std::chrono::milliseconds{static_cast<std::int64_t>(delay_ms)};
}

/// Execute an HTTP operation with exponential-backoff retry.
template <typename Operation>
[[nodiscard]] Result<HttpResponse> with_retry(Operation&& operation, const RetryPolicy& policy) {
	std::chrono::milliseconds total_delay{0};

	for (std::uint8_t attempt = 1; attempt <= policy.max_attempts; ++attempt) {
		Result<HttpResponse> result = operation();

		if (result.has_value()) {
			if (should_retry(*result, policy) && attempt < policy.max_attempts) {
				std::chrono::milliseconds delay = calculate_retry_delay(attempt, policy);
				total_delay += delay;
				std::this_thread::sleep_for(delay);
				continue;
			}
			return result;
		}

		if (should_retry(result.error(), policy) && attempt < policy.max_attempts) {
			std::chrono::milliseconds delay = calculate_retry_delay(attempt, policy);
			total_delay += delay;
			std::this_thread::sleep_for(delay);
			continue;
		}

		return result;
	}

	return std::unexpected(Error::network("Max retry attempts exceeded"));
}

} // namespace spc
