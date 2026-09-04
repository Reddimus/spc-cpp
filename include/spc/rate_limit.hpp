#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

namespace spc {

/// Token-bucket rate limiter with optional daily quota.
///
/// SPC / ArcGIS impose no published rate limit, but the IEM archive
/// (`mesonet.agron.iastate.edu`) is a courtesy third party — `ArchiveClient`
/// constructs this with conservative defaults. Thread-safe.
///
/// Logic is the ncei-cpp RateLimiter verbatim (namespace adapted).
class RateLimiter {
public:
	struct Config {
		std::uint16_t max_tokens = 2; ///< default: gentle on IEM
		/// Time per token. Clamped to 1000 ms if set to zero or less.
		std::chrono::milliseconds refill_interval{1000};
		std::uint16_t initial_tokens = 2; ///< clamped to `max_tokens`
		/// Longest `acquire()` may block. Unset means block indefinitely.
		std::optional<std::chrono::milliseconds> max_wait;
		std::int32_t daily_limit{0}; ///< 0 = no daily cap
	};

	explicit RateLimiter(Config config);

	[[nodiscard]] bool try_acquire() noexcept;

	/// Block until a token is available. With `Config::max_wait` set this is
	/// `acquire_for(*max_wait)`; without it the wait is unbounded, so a
	/// synchronous caller can stall for as long as the bucket stays empty.
	/// Returns false only on a bounded wait that expired, or when a
	/// configured `daily_limit` is spent — no amount of waiting clears that
	/// before the next UTC day, so it fails immediately rather than spinning.
	[[nodiscard]] bool acquire();
	[[nodiscard]] bool acquire_for(std::chrono::milliseconds max_wait);
	[[nodiscard]] std::uint16_t available_tokens() const noexcept;
	[[nodiscard]] std::int32_t daily_requests_remaining() const noexcept;
	void reset() noexcept;
	[[nodiscard]] const Config& config() const noexcept;

private:
	void refill() noexcept;
	void check_daily_reset() noexcept;
	[[nodiscard]] bool daily_quota_exhausted() noexcept;

	Config config_;
	mutable std::mutex mutex_;
	std::uint16_t tokens_;
	std::chrono::steady_clock::time_point last_refill_;
	std::int32_t daily_requests_used_{0};
	std::chrono::system_clock::time_point day_start_;
};

/// RAII rate-limit acquisition.
class ScopedRateLimit {
public:
	explicit ScopedRateLimit(RateLimiter& limiter);
	[[nodiscard]] bool acquired() const noexcept;
	explicit operator bool() const noexcept;

private:
	bool acquired_;
};

} // namespace spc
