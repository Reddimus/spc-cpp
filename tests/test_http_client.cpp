/// @file test_http_client.cpp
/// @brief HttpClient against a real loopback socket: size limits, redirects,
/// headers, scheme restrictions, and concurrent use.

#include "spc/http_client.hpp"
#include "spc/retry.hpp"
#include "support/loopback_server.hpp"
#include "support/tls_loopback_server.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <curl/curl.h>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
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

void append_le32(std::string& out, std::uint32_t value) {
	for (int shift = 0; shift < 32; shift += 8) {
		out += static_cast<char>((value >> shift) & 0xFFU);
	}
}

std::uint32_t crc32(std::string_view data) {
	std::uint32_t crc = 0xFFFFFFFFU;
	for (const char c : data) {
		crc ^= static_cast<unsigned char>(c);
		for (int bit = 0; bit < 8; ++bit) {
			crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
		}
	}
	return ~crc;
}

/// `data` as gzip with one stored deflate block, so the encoded body is 23
/// bytes longer than the decoded one.
std::string gzip_stored(std::string_view data) {
	std::string out{"\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\xff", 10};
	const std::uint16_t size = static_cast<std::uint16_t>(data.size());
	const std::uint16_t inverse = static_cast<std::uint16_t>(~size);
	out += '\x01'; // final block, stored
	out += static_cast<char>(size & 0xFFU);
	out += static_cast<char>(size >> 8);
	out += static_cast<char>(inverse & 0xFFU);
	out += static_cast<char>(inverse >> 8);
	out += data;
	append_le32(out, crc32(data));
	append_le32(out, static_cast<std::uint32_t>(data.size()));
	return out;
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

TEST(HttpClient, IgnoresTheSizeOfARedirectBodyItDiscards) {
	// The chunked final response has no length of its own, so the hop's must
	// not carry over to it.
	LoopbackServer server{[](const LoopbackRequest& request) {
		if (request.target == "/start") {
			return http_response("302 Found", "REDIRECT-BODY", "Location: /final\r\n");
		}
		if (request.target == "/start-chunked") {
			return http_response("302 Found", "REDIRECT-BODY", "Location: /final-chunked\r\n");
		}
		if (request.target == "/final-chunked") {
			return chunked_response(1, 2);
		}
		return http_response("200 OK", "OK");
	}};
	ClientConfig config;
	config.max_response_bytes = 8;
	const HttpClient client{config};

	const Result<HttpResponse> sized = client.get(server.url("/start"));
	const Result<HttpResponse> chunked = client.get(server.url("/start-chunked"));

	ASSERT_TRUE(sized) << sized.error().message;
	EXPECT_EQ(sized->status_code, 200);
	EXPECT_EQ(sized->body, "OK");
	ASSERT_TRUE(chunked) << chunked.error().message;
	EXPECT_EQ(chunked->body, "xx");
}

TEST(HttpClient, RefusesAnAdvertisedOversizeBodyBeforeReadingIt) {
	// The server stops after 10 bytes. Reading on would end in a partial-body
	// NetworkError, which with_retry would retry.
	LoopbackServer server{[](const LoopbackRequest&) {
		return std::string{"HTTP/1.1 200 OK\r\nContent-Length: 1048576\r\n"
						   "Connection: close\r\n\r\n0123456789"};
	}};
	ClientConfig config;
	config.max_response_bytes = 1024;
	const HttpClient client{config};

	const Result<HttpResponse> response = client.get(server.url("/big"));

	ASSERT_FALSE(response);
	EXPECT_EQ(response.error().code, ErrorCode::InvalidRequest);
	EXPECT_NE(response.error().message.find("max_response_bytes"), std::string::npos);
}

TEST(HttpClient, LimitsTheDecodedBodyNotTheEncodedOne) {
	if ((curl_version_info(CURLVERSION_NOW)->features & CURL_VERSION_LIBZ) == 0) {
		GTEST_SKIP() << "libcurl was built without gzip support";
	}
	const std::string body(64, 'x');
	const std::string encoded = gzip_stored(body);
	LoopbackServer server{[&encoded](const LoopbackRequest&) {
		return http_response("200 OK", encoded, "Content-Encoding: gzip\r\n");
	}};
	ClientConfig config;
	config.max_response_bytes = 80; // between the decoded 64 and the encoded 87
	const HttpClient client{config};

	const Result<HttpResponse> response = client.get(server.url("/gzip"));

	ASSERT_TRUE(response) << response.error().message;
	EXPECT_EQ(response->body, body);
}

TEST(HttpClient, RefusesEverySchemeOtherThanHttpAndHttps) {
	// file:// would otherwise read the local file and return it as the body.
	const HttpClient client;

	const Result<HttpResponse> result = client.get("file:///etc/hosts");

	ASSERT_FALSE(result) << "file:// must not be fetched";
	// Not NetworkError, which with_retry would retry.
	EXPECT_EQ(result.error().code, ErrorCode::InvalidRequest);
}

TEST(HttpClient, AMalformedUrlIsAnInvalidRequest) {
	const HttpClient client;

	const Result<HttpResponse> result = client.get("http://bad host/");

	ASSERT_FALSE(result);
	EXPECT_EQ(result.error().code, ErrorCode::InvalidRequest);
}

TEST(HttpClient, RefusesARedirectToAnotherScheme) {
	LoopbackServer server{[](const LoopbackRequest&) {
		return http_response("302 Found", "", "Location: file:///etc/hosts\r\n");
	}};
	const HttpClient client;

	const Result<HttpResponse> response = client.get(server.url("/escape"));

	ASSERT_FALSE(response);
	EXPECT_EQ(response.error().code, ErrorCode::InvalidRequest);
}

TEST(HttpClient, AFailedHttpsRequestLeavesHttpRedirectsAllowedOnItsHandle) {
	LoopbackServer target{[](const LoopbackRequest&) { return http_response("200 OK", "ok"); }};
	LoopbackServer origin{[&target](const LoopbackRequest&) {
		return http_response("302 Found", "", "Location: " + target.url("/final") + "\r\n");
	}};
	ClientConfig config;
	config.timeout = std::chrono::seconds{1};
	const HttpClient client{config};

	// Port zero cannot host a server; the request only configures the handle.
	EXPECT_FALSE(client.get("https://127.0.0.1:0/"));
	const Result<HttpResponse> response = client.get(origin.url("/start"));

	ASSERT_TRUE(response) << response.error().message;
	EXPECT_EQ(response->body, "ok");
}

#ifdef SPC_TEST_HAS_TLS_SERVER

using test::TlsLoopbackServer;

/// TlsLoopbackServer's certificate is self-signed.
ClientConfig unverified_tls() {
	ClientConfig config;
	config.verify_ssl = false;
	return config;
}

bool curl_has_tls() {
	return (curl_version_info(CURLVERSION_NOW)->features & CURL_VERSION_SSL) != 0;
}

TEST(HttpClient, AnHttpsRequestRefusesARedirectToHttp) {
	if (!curl_has_tls()) {
		GTEST_SKIP() << "libcurl was built without TLS";
	}
	// Anyone on the path can alter the plaintext hop, so its body must not
	// come back as the answer to an https request.
	LoopbackServer plain{
		[](const LoopbackRequest&) { return http_response("200 OK", "plaintext"); }};
	TlsLoopbackServer secure{[&plain](const LoopbackRequest& request) {
		if (request.target == "/two-hops") {
			return http_response("302 Found", "", "Location: /start\r\n");
		}
		return http_response("302 Found", "", "Location: " + plain.url("/final") + "\r\n");
	}};
	const HttpClient client{unverified_tls()};
	ClientConfig based = unverified_tls();
	based.base_url = "HTTPS://" + secure.authority();
	const HttpClient based_client{based};

	// libcurl ignores the scheme's case and takes one to three slashes after it.
	std::vector<std::pair<std::string, Result<HttpResponse>>> responses;
	for (const std::string scheme : {"https://", "HTTPS://", "hTtPs:/", "https:///"}) {
		responses.emplace_back(scheme, client.get(scheme + secure.authority() + "/start"));
	}
	responses.emplace_back("base_url", based_client.get("/start"));
	responses.emplace_back("https, https, http", client.get(secure.url("/two-hops")));

	for (const std::pair<std::string, Result<HttpResponse>>& entry : responses) {
		const Result<HttpResponse>& response = entry.second;
		if (response) {
			ADD_FAILURE() << entry.first << " followed the redirect and returned \""
						  << response->body << "\" with status " << response->status_code;
			continue;
		}
		// Not NetworkError, which with_retry would retry.
		EXPECT_EQ(response.error().code, ErrorCode::InvalidRequest)
			<< entry.first << ": " << response.error().message;
	}
	// Every request got its redirect: none failed earlier, in the handshake.
	EXPECT_EQ(secure.requests().size(), responses.size() + 1);
	EXPECT_TRUE(plain.requests().empty()) << "the plaintext server was contacted";
}

TEST(HttpClient, AnHttpsRequestFollowsHttpsRedirects) {
	if (!curl_has_tls()) {
		GTEST_SKIP() << "libcurl was built without TLS";
	}
	TlsLoopbackServer target{[](const LoopbackRequest& request) {
		if (request.target == "/relative") {
			return http_response("302 Found", "", "Location: /final\r\n");
		}
		return http_response("200 OK", "secure");
	}};
	TlsLoopbackServer origin{[&target](const LoopbackRequest&) {
		return http_response("302 Found", "", "Location: " + target.url("/relative") + "\r\n");
	}};
	const HttpClient client{unverified_tls()};

	const Result<HttpResponse> response = client.get(origin.url("/start"));

	ASSERT_TRUE(response) << response.error().message;
	EXPECT_EQ(response->body, "secure");
	EXPECT_EQ(target.requests().size(), 2U);
}

TEST(HttpClient, AnHttpRequestFollowsRedirectsToHttpAndHttps) {
	if (!curl_has_tls()) {
		GTEST_SKIP() << "libcurl was built without TLS";
	}
	LoopbackServer plain{
		[](const LoopbackRequest&) { return http_response("200 OK", "plaintext"); }};
	TlsLoopbackServer secure{
		[](const LoopbackRequest&) { return http_response("200 OK", "secure"); }};
	LoopbackServer origin{[&plain, &secure](const LoopbackRequest& request) {
		const std::string location =
			request.target == "/to-https" ? secure.url("/final") : plain.url("/final");
		return http_response("302 Found", "", "Location: " + location + "\r\n");
	}};
	const HttpClient client{unverified_tls()};

	const Result<HttpResponse> to_http = client.get(origin.url("/to-http"));
	const Result<HttpResponse> to_https = client.get(origin.url("/to-https"));

	ASSERT_TRUE(to_http) << to_http.error().message;
	EXPECT_EQ(to_http->body, "plaintext");
	ASSERT_TRUE(to_https) << to_https.error().message;
	EXPECT_EQ(to_https->body, "secure");
}

TEST(HttpClient, ReusedHandlesTakeTheRedirectRuleOfEachRequest) {
	if (!curl_has_tls()) {
		GTEST_SKIP() << "libcurl was built without TLS";
	}
	// Sequential requests share one pooled handle, so neither rule may outlive
	// the request that set it.
	LoopbackServer plain{[](const LoopbackRequest& request) {
		if (request.target == "/start") {
			return http_response("302 Found", "", "Location: /final\r\n");
		}
		return http_response("200 OK", "plaintext");
	}};
	TlsLoopbackServer secure{[&plain](const LoopbackRequest&) {
		return http_response("302 Found", "", "Location: " + plain.url("/final") + "\r\n");
	}};
	const HttpClient client{unverified_tls()};

	const Result<HttpResponse> first_https = client.get(secure.url("/start"));
	const Result<HttpResponse> http = client.get(plain.url("/start"));
	const Result<HttpResponse> second_https = client.get(secure.url("/start"));

	EXPECT_FALSE(first_https);
	ASSERT_TRUE(http) << http.error().message;
	EXPECT_EQ(http->body, "plaintext");
	EXPECT_FALSE(second_https);
	EXPECT_EQ(secure.requests().size(), 2U); // both https requests got their redirect
	EXPECT_EQ(plain.requests().size(), 2U);	 // only the http request's two hops
}

#else

TEST(HttpClient, HttpsRedirectTestsNeedATlsServer) {
	GTEST_SKIP() << "built without OpenSSL 3, so the https redirect tests did not run";
}

#endif // SPC_TEST_HAS_TLS_SERVER

TEST(HttpClient, ARedirectLoopIsAServerErrorThatIsNotRetried) {
	LoopbackServer server{[](const LoopbackRequest&) {
		return http_response("302 Found", "", "Location: /loop\r\n");
	}};
	const HttpClient client;

	const Result<HttpResponse> response = client.get(server.url("/loop"));

	ASSERT_FALSE(response);
	EXPECT_EQ(response.error().code, ErrorCode::ServerError);
	EXPECT_FALSE(should_retry(response.error(), RetryPolicy{}));
	EXPECT_EQ(server.requests().size(), 11U); // the first request and 10 redirects
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
	// NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
	EXPECT_EQ(original.config().max_response_bytes, ClientConfig{}.max_response_bytes);
}

} // namespace
