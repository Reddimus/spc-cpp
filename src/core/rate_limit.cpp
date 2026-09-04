// Logic verbatim from ncei-cpp/src/core/rate_limit.cpp (Workstream C);
// only the namespace changed (`ncei` -> `spc`).

#include "spc/rate_limit.hpp"

#include <algorithm>
#include <chrono>

namespace spc {

namespace {

/// Config is a public aggregate with no validation, so clamp the two fields
/// that can otherwise make the limiter misbehave: a non-positive refill
/// interval divides by zero in refill(), and initial_tokens above max_tokens
/// hands out a burst the bucket was never sized for.
RateLimiter::Config sanitized(RateLimiter::Config config) {
	if (config.refill_interval <= std::chrono::milliseconds::zero()) {
		config.refill_interval = std::chrono::milliseconds{1000};
	}
	config.initial_tokens = std::min(config.initial_tokens, config.max_tokens);
	return config;
}

} // namespace

RateLimiter::RateLimiter(Config config)
	: config_(sanitized(config)), tokens_(config_.initial_tokens),
	  last_refill_(std::chrono::steady_clock::now()),
	  day_start_(std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now())) {}

bool RateLimiter::daily_quota_exhausted() noexcept {
	std::lock_guard<std::mutex> lock(mutex_);
	check_daily_reset();
	return config_.daily_limit > 0 && daily_requests_used_ >= config_.daily_limit;
}

void RateLimiter::check_daily_reset() noexcept {
	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
	std::chrono::system_clock::time_point current_day = std::chrono::floor<std::chrono::days>(now);
	if (current_day > day_start_) {
		daily_requests_used_ = 0;
		day_start_ = current_day;
	}
}

void RateLimiter::refill() noexcept {
	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	std::chrono::milliseconds elapsed =
		std::chrono::duration_cast<std::chrono::milliseconds>(now - last_refill_);

	if (elapsed >= config_.refill_interval) {
		// Compute the refill in a width that cannot overflow: after a long idle
		// gap `elapsed/interval` can far exceed uint16, and narrowing it (or the
		// `tokens_ + new_tokens` sum) to uint16 *before* the min() wraps it to a
		// small value, defeating the cap. Widen, clamp to max_tokens, then narrow.
		const std::int64_t new_tokens = elapsed.count() / config_.refill_interval.count();
		const std::int64_t total = static_cast<std::int64_t>(tokens_) + new_tokens;
		tokens_ = static_cast<std::uint16_t>(
			std::min<std::int64_t>(total, static_cast<std::int64_t>(config_.max_tokens)));
		last_refill_ = now;
	}
}

bool RateLimiter::try_acquire() noexcept {
	std::lock_guard<std::mutex> lock(mutex_);
	check_daily_reset();

	if (config_.daily_limit > 0 && daily_requests_used_ >= config_.daily_limit) {
		return false;
	}

	refill();
	if (tokens_ > 0) {
		--tokens_;
		++daily_requests_used_;
		return true;
	}
	return false;
}

bool RateLimiter::acquire() {
	if (config_.max_wait) {
		return acquire_for(*config_.max_wait);
	}

	while (true) {
		if (try_acquire()) {
			return true;
		}
		// Waiting cannot help: only a UTC-day rollover clears the quota, and
		// spinning toward midnight is never what the caller wanted.
		if (daily_quota_exhausted()) {
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}

bool RateLimiter::acquire_for(std::chrono::milliseconds max_wait) {
	std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + max_wait;

	while (std::chrono::steady_clock::now() < deadline) {
		if (try_acquire()) {
			return true;
		}
		if (daily_quota_exhausted()) {
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return false;
}

std::uint16_t RateLimiter::available_tokens() const noexcept {
	std::lock_guard<std::mutex> lock(mutex_);
	return tokens_;
}

std::int32_t RateLimiter::daily_requests_remaining() const noexcept {
	std::lock_guard<std::mutex> lock(mutex_);
	if (config_.daily_limit <= 0) {
		return 0;
	}
	return config_.daily_limit - daily_requests_used_;
}

void RateLimiter::reset() noexcept {
	std::lock_guard<std::mutex> lock(mutex_);
	tokens_ = config_.initial_tokens;
	last_refill_ = std::chrono::steady_clock::now();
	daily_requests_used_ = 0;
	day_start_ = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
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
