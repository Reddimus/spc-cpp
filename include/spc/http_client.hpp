/// @file http_client.hpp
/// @brief The GET transport used by every client.

#pragma once

#include "spc/error.hpp"
#include "spc/version.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace spc {

/// An HTTP response. Any status code is a successful transfer; the clients
/// decide what a non-200 status means.
struct HttpResponse {
	std::int16_t status_code;
	std::string body;
	/// Headers of the final response, after redirects.
	std::vector<std::pair<std::string, std::string>> headers;
};

/// HttpClient settings. SPC, NOAA ArcGIS, and IEM are all unauthenticated.
struct ClientConfig {
	/// Prefix for relative paths. Leave empty to pass absolute URLs, which is
	/// what the SDK's clients do.
	std::string base_url;
	std::string user_agent{"spc-cpp/" SPC_VERSION_STRING " (contact@predictioncast.ai)"};
	std::chrono::seconds timeout{15};
	bool verify_ssl{true};
	/// Largest response body accepted. Bodies are held in memory and parsed
	/// whole, and an IEM date range can be arbitrarily large. A bigger
	/// response fails with `ErrorCode::InvalidRequest` and is not retried.
	std::size_t max_response_bytes{64UL * 1024UL * 1024UL};
};

/// The GET interface the clients use. Implement it to supply your own
/// network stack or canned test responses. One transport may be shared by
/// several clients and threads, so `get` must be safe to call concurrently.
class HttpTransport {
public:
	virtual ~HttpTransport() = default;

	[[nodiscard]] virtual Result<HttpResponse> get(std::string_view path) const = 0;

protected:
	HttpTransport() = default;
	HttpTransport(const HttpTransport&) = default;
	HttpTransport& operator=(const HttpTransport&) = default;
	HttpTransport(HttpTransport&&) = default;
	HttpTransport& operator=(HttpTransport&&) = default;
};

/// libcurl-backed transport. Follows redirects, accepts compressed bodies,
/// and speaks only http and https.
///
/// Thread-safe: concurrent `get` calls each use their own pooled libcurl
/// handle, and handles are reused so connections stay open between requests.
class HttpClient final : public HttpTransport {
public:
	explicit HttpClient(ClientConfig config = {});
	~HttpClient() override;

	HttpClient(HttpClient&&) noexcept;
	HttpClient& operator=(HttpClient&&) noexcept;
	HttpClient(const HttpClient&) = delete;
	HttpClient& operator=(const HttpClient&) = delete;

	/// GET `path`. An absolute `http://` or `https://` URL is used as is;
	/// anything else is appended to `config().base_url`. Every other scheme,
	/// such as `file://`, is refused, including on redirects.
	[[nodiscard]] Result<HttpResponse> get(std::string_view path) const override;

	/// The settings this client was built with; defaults on a moved-from client.
	[[nodiscard]] const ClientConfig& config() const noexcept;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace spc
