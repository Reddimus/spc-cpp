#include "spc/rate_limit.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace spc {

namespace {

/// Config is a plain aggregate, so repair the values that would break the
/// limiter: a non-positive interval (division by zero in refill), a bucket
/// that can never hold a token, and an initial burst larger than the bucket.
RateLimiter::Config sanitized(RateLimiter::Config config) {
	if (config.refill_interval <= std::chrono::milliseconds::zero()) {
		config.refill_interval = std::chrono::milliseconds{1000};
	}
	config.max_tokens = std::max<std::uint16_t>(config.max_tokens, 1);
	config.initial_tokens = std::min(config.initial_tokens, config.max_tokens);
	return config;
}

std::chrono::system_clock::time_point utc_day_start() {
	return std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
}

} // namespace

RateLimiter::RateLimiter(Config config)
	: config_(sanitized(config)), tokens_(config_.initial_tokens), last_refill_(Clock::now()),
	  day_start_(utc_day_start()) {}

void RateLimiter::check_daily_reset() noexcept {
	const std::chrono::system_clock::time_point today = utc_day_start();
	if (today > day_start_) {
		daily_requests_used_ = 0;
		day_start_ = today;
	}
}

bool RateLimiter::daily_quota_spent() const noexcept {
	return config_.daily_limit > 0 && daily_requests_used_ >= config_.daily_limit;
}

void RateLimiter::refill() noexcept {
	const Clock::time_point now = Clock::now();
	if (tokens_ >= config_.max_tokens) {
		// A full bucket earns nothing; the next token starts counting now.
		last_refill_ = now;
		return;
	}
	const std::int64_t earned = (now - last_refill_) / config_.refill_interval;
	if (earned <= 0) {
		return;
	}
	// Widen before adding: after a long idle gap `earned` can exceed uint16.
	const std::int64_t total = static_cast<std::int64_t>(tokens_) + earned;
	if (total >= config_.max_tokens) {
		tokens_ = config_.max_tokens;
		last_refill_ = now;
	} else {
		tokens_ = static_cast<std::uint16_t>(total);
		// Keep the partial interval so the configured rate is exact.
		last_refill_ += earned * config_.refill_interval;
	}
}

std::optional<RateLimiter::Clock::duration> RateLimiter::take_or_wait_time() noexcept {
	refill();
	if (tokens_ > 0) {
		--tokens_;
		++daily_requests_used_;
		return std::nullopt;
	}
	const Clock::duration wait = last_refill_ + config_.refill_interval - Clock::now();
	return std::max(wait, Clock::duration::zero());
}

bool RateLimiter::try_acquire() noexcept {
	const std::lock_guard<std::mutex> lock(mutex_);
	check_daily_reset();
	return !daily_quota_spent() && !take_or_wait_time().has_value();
}

bool RateLimiter::acquire() {
	if (config_.max_wait) {
		return acquire_for(*config_.max_wait);
	}
	return acquire_until(std::nullopt);
}

bool RateLimiter::acquire_for(std::chrono::milliseconds max_wait) {
	return acquire_until(Clock::now() + max_wait);
}

bool RateLimiter::acquire_until(std::optional<Clock::time_point> deadline) {
	while (true) {
		std::optional<Clock::duration> wait;
		{
			const std::lock_guard<std::mutex> lock(mutex_);
			check_daily_reset();
			// Only a UTC-day rollover clears the quota; waiting cannot help.
			if (daily_quota_spent()) {
				return false;
			}
			wait = take_or_wait_time();
		}
		if (!wait) {
			return true;
		}
		if (deadline && Clock::now() + *wait > *deadline) {
			return false;
		}
		std::this_thread::sleep_for(*wait);
	}
}

std::uint16_t RateLimiter::available_tokens() const noexcept {
	const std::lock_guard<std::mutex> lock(mutex_);
	return tokens_;
}

std::int32_t RateLimiter::daily_requests_remaining() const noexcept {
	const std::lock_guard<std::mutex> lock(mutex_);
	if (config_.daily_limit <= 0) {
		return 0;
	}
	return config_.daily_limit - daily_requests_used_;
}

void RateLimiter::reset() noexcept {
	const std::lock_guard<std::mutex> lock(mutex_);
	tokens_ = config_.initial_tokens;
	last_refill_ = Clock::now();
	daily_requests_used_ = 0;
	day_start_ = utc_day_start();
}

const RateLimiter::Config& RateLimiter::config() const noexcept {
	return config_;
}

ScopedRateLimit::ScopedRateLimit(RateLimiter& limiter) : acquired_(limiter.acquire()) {}

bool ScopedRateLimit::acquired() const noexcept {
	return acquired_;
}

ScopedRateLimit::operator bool() const noexcept {
	return acquired_;
}

} // namespace spc
