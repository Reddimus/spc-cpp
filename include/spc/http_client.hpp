#pragma once

#include "spc/error.hpp"

#include <chrono>
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
	std::string user_agent{"PredictionCastAI spc-cpp/0.1.0 (contact@predictioncast.ai)"};
	std::chrono::seconds timeout{15};
	bool verify_ssl{true};
};

/// GET-only HTTP client. Behavior parity with spc-data/src/fetcher.cpp:
/// FOLLOWLOCATION on, NOSIGNAL on, empty ACCEPT_ENCODING (advertise all
/// supported), and the SPC User-Agent.
///
/// @note NOT thread-safe — the CURL handle is shared per instance. Use one
/// client per thread or guard with a mutex.
class HttpClient {
public:
	explicit HttpClient(ClientConfig config = {});
	~HttpClient();

	HttpClient(HttpClient&&) noexcept;
	HttpClient& operator=(HttpClient&&) noexcept;
	HttpClient(const HttpClient&) = delete;
	HttpClient& operator=(const HttpClient&) = delete;

	/// GET `path`. If `path` is an absolute URL (starts with http) it is
	/// used verbatim; otherwise it is appended to `config().base_url`.
	[[nodiscard]] Result<HttpResponse> get(std::string_view path) const;

	[[nodiscard]] const ClientConfig& config() const noexcept;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace spc
