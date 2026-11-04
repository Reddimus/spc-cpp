/// @file test_http_client.cpp
/// @brief HttpClient against a real loopback socket: size limits, redirects,
/// headers, scheme restrictions, and concurrent use.

#include "spc/http_client.hpp"
#include "support/loopback_server.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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
#ifdef _WIN32
		(void)::_putenv_s("NO_PROXY", "127.0.0.1");
		(void)::_putenv_s("no_proxy", "127.0.0.1");
#else
		(void)::setenv("NO_PROXY", "127.0.0.1", 1);
		(void)::setenv("no_proxy", "127.0.0.1", 1);
#endif
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

TEST(LoopbackServer, ShutdownIsBoundedWhenAClientStopsReading) {
	std::atomic<bool> responding{false};
	std::unique_ptr<LoopbackServer> server =
		std::make_unique<LoopbackServer>([&](const LoopbackRequest&) {
			responding = true;
			return http_response("200 OK", std::string(8 * 1024 * 1024, 'x'));
		});
	struct Socket {
		test::socket_detail::Socket value;
		~Socket() { test::socket_detail::close_socket(value); }
	};
	const Socket client{::socket(AF_INET, SOCK_STREAM, 0)};
	ASSERT_NE(client.value, test::socket_detail::kInvalidSocket);
	const int receive_buffer = 1024;
	test::socket_detail::set_option(client.value, SO_RCVBUF, &receive_buffer,
									sizeof(receive_buffer));
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = htons(server->port());
	ASSERT_EQ(::connect(client.value, reinterpret_cast<const sockaddr*>(&address), sizeof(address)),
			  0);
	constexpr char request[] = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
	ASSERT_GT(::send(client.value, request, sizeof(request) - 1, 0), 0);
	const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
	while (!responding && std::chrono::steady_clock::now() - started < std::chrono::seconds{2}) {
		std::this_thread::yield();
	}
	ASSERT_TRUE(responding);
	char first_byte{};
	ASSERT_EQ(::recv(client.value, &first_byte, 1, 0), 1);
	server.reset(); // The client deliberately keeps its unread socket open.
	EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds{5});
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

TEST(HttpClient, AbsoluteHttpSchemesIgnoreCaseWithABaseUrl) {
	LoopbackServer server{
		[](const LoopbackRequest& request) { return http_response("200 OK", request.target); }};
	ClientConfig config;
	config.base_url = server.url("/base/");
	const HttpClient client{config};
	for (const std::string& scheme : {std::string{"HTTP"}, std::string{"hTtP"}}) {
		std::string url = server.url("/absolute");
		url.replace(0, 4, scheme);
		const Result<HttpResponse> response = client.get(url);
		ASSERT_TRUE(response) << response.error().message;
		EXPECT_EQ(response->body, "/absolute");
	}
}

TEST(HttpClient, AbsoluteHttpsSchemesNeverUseThePlaintextBaseUrl) {
	LoopbackServer server{[](const LoopbackRequest&) { return http_response("200 OK", "wrong"); }};
	ClientConfig config;
	config.base_url = server.url("/base/");
	config.timeout = std::chrono::seconds{1};
	const HttpClient client{config};
	// Port zero cannot host a server. A misclassified URL instead reaches the
	// plaintext base and succeeds, exposing an HTTPS request as an HTTP path.
	EXPECT_FALSE(client.get("HTTPS://127.0.0.1:0/absolute"));
	EXPECT_FALSE(client.get("hTtPs://127.0.0.1:0/absolute"));
	EXPECT_TRUE(server.requests().empty());
}

TEST(HttpClient, RejectsInvalidTimeoutsBeforeSendingARequest) {
	LoopbackServer server{[](const LoopbackRequest&) { return http_response("200 OK", "{}"); }};
	for (const std::chrono::seconds timeout :
		 {std::chrono::seconds{-1},
		  std::chrono::seconds{std::numeric_limits<int>::max() / 1000 + 1LL},
		  std::chrono::seconds::max()}) {
		ClientConfig config;
		config.timeout = timeout;
		const HttpClient client{config};
		const Result<HttpResponse> response = client.get(server.url("/"));
		ASSERT_FALSE(response);
		EXPECT_EQ(response.error().code, ErrorCode::InvalidRequest);
	}
	EXPECT_TRUE(server.requests().empty());
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
	// Not NetworkError, which with_retry would retry.
	EXPECT_EQ(response.error().code, ErrorCode::InvalidRequest);
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
