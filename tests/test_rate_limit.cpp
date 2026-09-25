/// @file test_rate_limit.cpp
/// @brief RateLimiter: bad configs cannot crash or hang it, waits are
/// bounded, and the refill rate is exact.

#include "spc/rate_limit.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

namespace {

using namespace spc;
using std::chrono::milliseconds;
using std::chrono::steady_clock;

milliseconds elapsed_since(steady_clock::time_point start) {
	return std::chrono::duration_cast<milliseconds>(steady_clock::now() - start);
}

TEST(RateLimit, AZeroRefillIntervalIsClampedInsteadOfDividingByZero) {
	RateLimiter::Config config;
	config.refill_interval = milliseconds{0};
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

TEST(RateLimit, AZeroSizedBucketStillEarnsTokens) {
	// A bucket that can never hold a token would make acquire() hang.
	RateLimiter::Config config;
	config.max_tokens = 0;
	config.initial_tokens = 0;
	config.refill_interval = milliseconds{5};
	RateLimiter limiter{config};

	EXPECT_TRUE(limiter.acquire_for(milliseconds{1000}));
}

TEST(RateLimit, AnExhaustedDailyQuotaFailsFastInsteadOfWaitingForMidnight) {
	RateLimiter::Config config;
	config.daily_limit = 1;
	RateLimiter limiter{config};

	ASSERT_TRUE(limiter.acquire());

	const steady_clock::time_point start = steady_clock::now();
	EXPECT_FALSE(limiter.acquire());
	EXPECT_FALSE(limiter.acquire_for(milliseconds{5000}));
	EXPECT_LT(elapsed_since(start).count(), 1000);
	EXPECT_EQ(limiter.daily_requests_remaining(), 0);
}

TEST(RateLimit, AcquireWaitsForTheNextTokenInsteadOfPolling) {
	RateLimiter::Config config;
	config.max_tokens = 1;
	config.initial_tokens = 0;
	config.refill_interval = milliseconds{50};
	RateLimiter limiter{config};

	const steady_clock::time_point start = steady_clock::now();
	ASSERT_TRUE(limiter.acquire());
	const milliseconds waited = elapsed_since(start);

	EXPECT_GE(waited.count(), 45);
	EXPECT_LT(waited.count(), 500);
}

TEST(RateLimit, ABoundedWaitGivesUpAtOnceWhenNoTokenCanArriveInTime) {
	RateLimiter::Config config;
	config.max_tokens = 1;
	config.initial_tokens = 0;
	config.refill_interval = milliseconds{10000};
	RateLimiter limiter{config};

	const steady_clock::time_point start = steady_clock::now();
	EXPECT_FALSE(limiter.acquire_for(milliseconds{100}));
	EXPECT_LT(elapsed_since(start).count(), 100);
}

TEST(RateLimit, TokensArriveAtTheConfiguredRate) {
	// Resetting the refill clock to "now" on every refill dropped the partial
	// interval, so the real rate ran below the configured one.
	RateLimiter::Config config;
	config.max_tokens = 1;
	config.initial_tokens = 0;
	config.refill_interval = milliseconds{20};
	RateLimiter limiter{config};

	constexpr int kTokens = 10;
	const steady_clock::time_point start = steady_clock::now();
	for (int i = 0; i < kTokens; ++i) {
		ASSERT_TRUE(limiter.acquire());
	}
	// Ten tokens at 20 ms each: never faster than 200 ms, and not far slower.
	const milliseconds waited = elapsed_since(start);
	EXPECT_GE(waited.count(), 195);
	EXPECT_LT(waited.count(), 600);
}

TEST(RateLimit, ConcurrentCallersNeverGetMoreTokensThanTheBucketHolds) {
	RateLimiter::Config config;
	config.max_tokens = 5;
	config.initial_tokens = 5;
	config.refill_interval = milliseconds{60000};
	RateLimiter limiter{config};
	std::atomic<std::int32_t> granted{0};

	std::vector<std::thread> workers;
	workers.reserve(8);
	for (int t = 0; t < 8; ++t) {
		workers.emplace_back([&limiter, &granted] {
			for (int i = 0; i < 50; ++i) {
				if (limiter.try_acquire()) {
					granted.fetch_add(1);
				}
			}
		});
	}
	for (std::thread& worker : workers) {
		worker.join();
	}

	EXPECT_EQ(granted.load(), 5);
	EXPECT_EQ(limiter.available_tokens(), 0);
}

} // namespace
