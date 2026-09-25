/// @file rate_limit.hpp
/// @brief Thread-safe token bucket with an optional daily quota.

#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>

namespace spc {

/// Token bucket with an optional daily quota. `ArchiveClient` uses one to stay
/// gentle on IEM; SPC and NOAA publish no rate limit. Thread-safe.
class RateLimiter {
public:
	struct Config {
		std::uint16_t max_tokens = 2; ///< bucket size (burst); at least 1
		/// Time to earn one token. Zero or less becomes 1000 ms.
		std::chrono::milliseconds refill_interval{1000};
		std::uint16_t initial_tokens = 2; ///< capped at `max_tokens`
		/// Longest `acquire()` may block. Unset means no limit.
		std::optional<std::chrono::milliseconds> max_wait;
		std::int32_t daily_limit{0}; ///< requests per UTC day; 0 means no cap
	};

	explicit RateLimiter(Config config);

	/// Take a token if one is available now.
	[[nodiscard]] bool try_acquire() noexcept;

	/// Wait for a token, up to `Config::max_wait` if set. Returns false when
	/// no token can arrive in time or the daily quota is spent; it does not
	/// sleep when waiting cannot help.
	[[nodiscard]] bool acquire();

	/// `acquire()` with an explicit limit on the wait.
	[[nodiscard]] bool acquire_for(std::chrono::milliseconds max_wait);

	[[nodiscard]] std::uint16_t available_tokens() const noexcept;

	/// Requests left today. Returns 0 when `daily_limit` is 0 (no cap).
	[[nodiscard]] std::int32_t daily_requests_remaining() const noexcept;

	/// Refill the bucket and clear today's count.
	void reset() noexcept;

	[[nodiscard]] const Config& config() const noexcept;

private:
	using Clock = std::chrono::steady_clock;

	[[nodiscard]] bool acquire_until(std::optional<Clock::time_point> deadline);
	/// Take a token, or report how long until the next one. Requires `mutex_`.
	[[nodiscard]] std::optional<Clock::duration> take_or_wait_time() noexcept;
	void refill() noexcept;
	void check_daily_reset() noexcept;
	[[nodiscard]] bool daily_quota_spent() const noexcept;

	Config config_;
	mutable std::mutex mutex_;
	std::uint16_t tokens_;
	Clock::time_point last_refill_;
	std::int32_t daily_requests_used_{0};
	std::chrono::system_clock::time_point day_start_;
};

/// Calls `limiter.acquire()` on construction and records the result.
class ScopedRateLimit {
public:
	explicit ScopedRateLimit(RateLimiter& limiter);
	[[nodiscard]] bool acquired() const noexcept;
	explicit operator bool() const noexcept;

private:
	bool acquired_;
};

} // namespace spc
