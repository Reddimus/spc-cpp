/// @file test_rate_limit.cpp
/// @brief RateLimiter contract: a caller-settable Config must not be able to
/// crash or hang the limiter.

#include "spc/rate_limit.hpp"

#include <chrono>
#include <gtest/gtest.h>

namespace {

using namespace spc;

TEST(RateLimit, AZeroRefillIntervalIsClampedInsteadOfDividingByZero) {
	// Config is a public aggregate with no validation, so a zero interval
	// reached `elapsed / refill_interval` and raised SIGFPE on first use.
	RateLimiter::Config config;
	config.refill_interval = std::chrono::milliseconds{0};
	RateLimiter limiter{config};

	EXPECT_GT(limiter.config().refill_interval.count(), 0);
	EXPECT_TRUE(limiter.try_acquire());
}

TEST(RateLimit, InitialTokensAreClampedToTheBucketSize) {
	RateLimiter::Config config;
	config.max_tokens = 2;
	config.initial_tokens = 9;
	RateLimiter limiter{config};

	EXPECT_EQ(limiter.available_tokens(), 2);
}

TEST(RateLimit, AnExhaustedDailyQuotaFailsFastInsteadOfWaitingForMidnight) {
	// try_acquire() returns false permanently once the daily quota is spent,
	// and only a UTC-midnight rollover clears it. acquire() with no max_wait
	// used to busy-wait toward that rollover; it must give up at once.
	RateLimiter::Config config;
	config.daily_limit = 1;
	RateLimiter limiter{config};

	ASSERT_TRUE(limiter.acquire());

	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	EXPECT_FALSE(limiter.acquire());
	EXPECT_FALSE(limiter.acquire_for(std::chrono::milliseconds{5000}));
	const std::chrono::milliseconds waited = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - start);
	EXPECT_LT(waited.count(), 1000);
}

} // namespace
