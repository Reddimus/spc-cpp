#pragma once

#include "spc/error.hpp"

/// Set from `PROJECT_VERSION` by the build. The fallback keeps the header
/// usable when it is read outside the project's own CMake targets.
#ifndef SPC_VERSION_STRING
#define SPC_VERSION_STRING "0.3.0"
#endif

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace spc {

/// HTTP response.
struct HttpResponse {
	std::int16_t status_code; // HTTP status codes fit in int16 (100-599)
	std::string body;
	std::vector<std::pair<std::string, std::string>> headers;
};

/// HTTP client configuration. SPC / ArcGIS / IEM are all unauthenticated
/// GETs; the only required field semantics match spc-data's fetcher.
struct ClientConfig {
	/// Optional base URL. Empty (the default) means callers pass absolute
	/// URLs — one client then serves spc.noaa.gov, the ArcGIS MapServer,
	/// and the IEM archive interchangeably (spc-data's fetcher behavior).
	std::string base_url;
	std::string user_agent{"spc-cpp/" SPC_VERSION_STRING " (contact@predictioncast.ai)"};
	std::chrono::seconds timeout{15};
	bool verify_ssl{true};
	/// Hard ceiling on a single response body. A wide IEM archive window is
	/// unbounded by construction — the caller chooses the date range — and the
	/// body is buffered whole, then parsed into a full JSON AST, then into the
	/// payload. Exceeding this aborts the transfer with a network error.
	std::size_t max_response_bytes{64UL * 1024UL * 1024UL};
};

/// GET transport boundary used by the high-level clients.
///
/// Applications normally use HttpClient. The interface also lets callers
/// supply their own networking stack and lets tests run without NOAA access.
class HttpTransport {
public:
	virtual ~HttpTransport() = default;

	[[nodiscard]] virtual Result<HttpResponse> get(std::string_view path) const = 0;
};

/// GET-only HTTP client. Behavior parity with spc-data/src/fetcher.cpp:
/// FOLLOWLOCATION on, NOSIGNAL on, empty ACCEPT_ENCODING (advertise all
/// supported), and the SPC User-Agent.
///
/// @note NOT thread-safe — the CURL handle is shared per instance. Use one
/// client per thread or guard with a mutex.
class HttpClient final : public HttpTransport {
public:
	explicit HttpClient(ClientConfig config = {});
	~HttpClient() override;

	HttpClient(HttpClient&&) noexcept;
	HttpClient& operator=(HttpClient&&) noexcept;
	HttpClient(const HttpClient&) = delete;
	HttpClient& operator=(const HttpClient&) = delete;

	/// GET `path`. If `path` is an absolute `http://` or `https://` URL it is
	/// used verbatim; otherwise it is appended to `config().base_url`. Only
	/// those two schemes are accepted — every other scheme (`file://`,
	/// `dict://`, `scp://`, ...) is refused by the transport, on the request
	/// and on any redirect.
	[[nodiscard]] Result<HttpResponse> get(std::string_view path) const override;

	[[nodiscard]] const ClientConfig& config() const noexcept;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace spc
