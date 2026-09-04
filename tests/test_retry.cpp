/// @file test_retry.cpp
/// @brief Retry arithmetic and the Retry-After contract.

#include "spc/http_client.hpp"
#include "spc/retry.hpp"

#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <string>

namespace {

using namespace spc;

TEST(Retry, JitterCannotPushTheDelayPastMaxDelay) {
	// The clamp used to run before the jitter multiply, so a delay pinned at
	// max_delay came back up to jitter_factor above the documented ceiling.
	RetryPolicy policy;
	policy.initial_delay = std::chrono::milliseconds{10000};
	policy.max_delay = std::chrono::milliseconds{30000};
	policy.jitter_factor = 0.5;

	for (std::uint8_t attempt = 1; attempt <= 8; ++attempt) {
		for (int sample = 0; sample < 64; ++sample) {
			const std::chrono::milliseconds delay = calculate_retry_delay(attempt, policy);
			EXPECT_LE(delay.count(), policy.max_delay.count());
			EXPECT_GE(delay.count(), 0);
		}
	}
}

TEST(Retry, ZeroMaxAttemptsStillPerformsTheRequestOnce) {
	// The loop `for (attempt = 1; attempt <= max_attempts; ...)` never ran, so
	// with_retry reported a network error for a request never made.
	RetryPolicy policy;
	policy.max_attempts = 0;
	int calls = 0;

	const Result<HttpResponse> result = with_retry(
		[&]() -> Result<HttpResponse> {
			++calls;
			return HttpResponse{200, "{}", {}};
		},
		policy);

	EXPECT_EQ(calls, 1);
	ASSERT_TRUE(result);
	EXPECT_EQ(result->status_code, 200);
}

TEST(Retry, MaxAttemptsAtTheTopOfTheRangeTerminates) {
	// The review claimed `++attempt` wrapping 255 -> 0 made this loop run
	// forever. It does not: the `attempt < max_attempts` guard returns the
	// result at attempt 255 before the counter can wrap. Pinned so the wider
	// loop counter keeps that true.
	RetryPolicy policy;
	policy.max_attempts = 255;
	policy.initial_delay = std::chrono::milliseconds{0};
	policy.max_delay = std::chrono::milliseconds{0};
	policy.jitter_factor = 0.0;
	int calls = 0;

	const Result<HttpResponse> result = with_retry(
		[&]() -> Result<HttpResponse> {
			++calls;
			return std::unexpected(Error::network("boom"));
		},
		policy);

	EXPECT_EQ(calls, 255);
	ASSERT_FALSE(result);
}

TEST(Retry, HonoursRetryAfterInsteadOfTheComputedBackoff) {
	// 429 and 503 are exactly the responses that carry Retry-After, and the
	// SDK already captures every header. A server asking for 60 s was being
	// retried after 200 ms.
	RetryPolicy policy;
	policy.max_attempts = 2;
	policy.initial_delay = std::chrono::milliseconds{1};
	policy.max_delay = std::chrono::milliseconds{150};
	policy.jitter_factor = 0.0;

	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	const Result<HttpResponse> result = with_retry(
		[]() -> Result<HttpResponse> {
			return HttpResponse{429, "slow down", {{"Retry-After", "60"}}};
		},
		policy);
	const std::chrono::milliseconds waited = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - start);

	ASSERT_TRUE(result);
	EXPECT_EQ(result->status_code, 429);
	// 60 s clamped to max_delay, not the 1 ms the backoff would have chosen.
	EXPECT_GE(waited.count(), 100);
}

TEST(Retry, IgnoresAnUnparseableRetryAfterAndFallsBackToTheBackoff) {
	RetryPolicy policy;
	policy.max_attempts = 2;
	policy.initial_delay = std::chrono::milliseconds{1};
	policy.max_delay = std::chrono::milliseconds{5000};
	policy.jitter_factor = 0.0;

	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	const Result<HttpResponse> result = with_retry(
		[]() -> Result<HttpResponse> {
			// The HTTP-date form, which the SDK does not parse.
			return HttpResponse{503, "", {{"retry-after", "Wed, 21 Oct 2026 07:28:00 GMT"}}};
		},
		policy);
	const std::chrono::milliseconds waited = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - start);

	ASSERT_TRUE(result);
	EXPECT_LT(waited.count(), 1000);
}

} // namespace
