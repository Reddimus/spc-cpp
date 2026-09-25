/// @file test_http_client.cpp
/// @brief HttpClient against a real loopback socket: size limits, redirects,
/// headers, scheme restrictions, and concurrent use.

#include "spc/http_client.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "support/loopback_server.hpp"

namespace {

using namespace spc;
using test::http_response;
using test::LoopbackRequest;
using test::LoopbackServer;

/// Keep a developer's proxy settings away from the loopback requests. Runs
/// before any test starts a thread.
class NoProxyEnvironment : public testing::Environment {
public:
	void SetUp() override {
		(void)::setenv("NO_PROXY", "127.0.0.1", 1);
		(void)::setenv("no_proxy", "127.0.0.1", 1);
	}
};

[[maybe_unused]] testing::Environment* const kNoProxy =
	testing::AddGlobalTestEnvironment(new NoProxyEnvironment);

std::string chunked_response(std::size_t chunks, std::size_t chunk_size) {
	std::string response =
		"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
	const std::string chunk(chunk_size, 'x');
	char size_line[32];
	(void)std::snprintf(size_line, sizeof(size_line), "%zx\r\n", chunk_size);
	for (std::size_t i = 0; i < chunks; ++i) {
		response += size_line + chunk + "\r\n";
	}
	return response + "0\r\n\r\n";
}

TEST(HttpClient, ReturnsStatusBodyAndSendsTheConfiguredUserAgent) {
	LoopbackServer server{[](const LoopbackRequest&) {
		return http_response("404 Not Found", "nope", "X-Test: yes\r\n");
	}};
	ClientConfig config;
	config.user_agent = "spc-cpp-test/1.0";
	const HttpClient client{config};

	const Result<HttpResponse> response = client.get(server.url("/missing"));

	ASSERT_TRUE(response) << response.error().message;
	EXPECT_EQ(response->status_code, 404);
	EXPECT_EQ(response->body, "nope");
	ASSERT_EQ(server.requests().size(), 1u);
	EXPECT_EQ(server.requests()[0].target, "/missing");
	EXPECT_EQ(server.requests()[0].header("User-Agent"), "spc-cpp-test/1.0");
}

TEST(HttpClient, RelativePathsUseTheBaseUrl) {
	LoopbackServer server{[](const LoopbackRequest&) { return http_response("200 OK", "{}"); }};
	ClientConfig config;
	config.base_url = server.url("/api");
	const HttpClient client{config};

	ASSERT_TRUE(client.get("/items?x=1"));

	EXPECT_EQ(server.requests().at(0).target, "/api/items?x=1");
}

TEST(HttpClient, KeepsOnlyTheFinalResponseHeadersAfterARedirect) {
	// A Retry-After on the redirect hop must not reach retry_after().
	LoopbackServer server{[](const LoopbackRequest& request) {
		if (request.target == "/start") {
			return http_response("302 Found", "", "Location: /final\r\nRetry-After: 99\r\n");
		}
		return http_response("200 OK", "done", "X-Final: yes\r\n");
	}};
	const HttpClient client;

	const Result<HttpResponse> response = client.get(server.url("/start"));

	ASSERT_TRUE(response) << response.error().message;
	EXPECT_EQ(response->status_code, 200);
	EXPECT_EQ(response->body, "done");
	bool saw_final = false;
	for (const std::pair<std::string, std::string>& header : response->headers) {
		EXPECT_NE(header.first, "Retry-After");
		saw_final = saw_final || (header.first == "X-Final" && header.second == "yes");
	}
	EXPECT_TRUE(saw_final);
}

TEST(HttpClient, RejectsABodyOverTheLimitWhenContentLengthIsKnown) {
	LoopbackServer server{[](const LoopbackRequest&) {
		return http_response("200 OK", std::string(64 * 1024, 'x'));
	}};
	ClientConfig config;
	config.max_response_bytes = 1024;
	const HttpClient client{config};

	const Result<HttpResponse> response = client.get(server.url("/big"));

	ASSERT_FALSE(response);
	EXPECT_EQ(response.error().code, ErrorCode::NetworkError);
	EXPECT_NE(response.error().message.find("max_response_bytes"), std::string::npos);
}

TEST(HttpClient, RejectsAChunkedBodyThatGrowsPastTheLimit) {
	LoopbackServer server{[](const LoopbackRequest&) { return chunked_response(64, 1024); }};
	ClientConfig config;
	config.max_response_bytes = 4096;
	const HttpClient client{config};

	const Result<HttpResponse> response = client.get(server.url("/stream"));

	ASSERT_FALSE(response);
	EXPECT_NE(response.error().message.find("max_response_bytes"), std::string::npos);
}

TEST(HttpClient, RefusesEverySchemeOtherThanHttpAndHttps) {
	// file:// would otherwise read the local file and return it as the body.
	const HttpClient client;

	const Result<HttpResponse> result = client.get("file:///etc/hosts");

	ASSERT_FALSE(result) << "file:// must not be fetched";
	EXPECT_EQ(result.error().code, ErrorCode::NetworkError);
}

TEST(HttpClient, RefusesARedirectToAnotherScheme) {
	LoopbackServer server{[](const LoopbackRequest&) {
		return http_response("302 Found", "", "Location: file:///etc/hosts\r\n");
	}};
	const HttpClient client;

	const Result<HttpResponse> response = client.get(server.url("/escape"));

	ASSERT_FALSE(response);
	EXPECT_EQ(response.error().code, ErrorCode::NetworkError);
}

TEST(HttpClient, ReusedHandlesKeepTheSchemeRestriction) {
	LoopbackServer server{[](const LoopbackRequest&) { return http_response("200 OK", "ok"); }};
	const HttpClient client;

	ASSERT_TRUE(client.get(server.url("/first")));
	EXPECT_FALSE(client.get("file:///etc/hosts"));
	const Result<HttpResponse> after = client.get(server.url("/second"));

	ASSERT_TRUE(after);
	EXPECT_EQ(after->body, "ok");
}

TEST(HttpClient, OneClientServesConcurrentRequests) {
	// The TSan job's target: every thread shares one client.
	LoopbackServer server{
		[](const LoopbackRequest& request) { return http_response("200 OK", request.target); }};
	const HttpClient client;
	std::atomic<std::int32_t> succeeded{0};

	std::vector<std::thread> workers;
	workers.reserve(8);
	for (int t = 0; t < 8; ++t) {
		workers.emplace_back([&client, &server, &succeeded, t] {
			for (int i = 0; i < 5; ++i) {
				const std::string target = "/t" + std::to_string(t) + "/" + std::to_string(i);
				const Result<HttpResponse> response = client.get(server.url(target));
				if (response && response->status_code == 200 && response->body == target) {
					succeeded.fetch_add(1);
				}
			}
		});
	}
	for (std::thread& worker : workers) {
		worker.join();
	}

	EXPECT_EQ(succeeded.load(), 40);
}

TEST(HttpClient, AMovedFromClientReportsAnErrorInsteadOfCrashing) {
	HttpClient original;
	const HttpClient moved{std::move(original)};

	// NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
	const Result<HttpResponse> response = original.get("http://127.0.0.1:9/");

	ASSERT_FALSE(response);
	EXPECT_EQ(response.error().code, ErrorCode::InvalidRequest);
	EXPECT_TRUE(moved.config().verify_ssl);
}

} // namespace
