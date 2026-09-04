#include "spc/http_client.hpp"

#include <curl/curl.h>
#include <string>
#include <utility>

namespace spc {

namespace {

/// Accumulates the body, refusing to grow past a ceiling. Returning a short
/// count makes libcurl abort the transfer with CURLE_WRITE_ERROR.
struct BodySink {
	std::string body;
	std::size_t limit{0};
	bool overflowed{false};
};

std::size_t write_cb(char* ptr, std::size_t size, std::size_t nmemb, void* user) {
	BodySink* sink = static_cast<BodySink*>(user);
	const std::size_t chunk = size * nmemb;
	if (sink->limit > 0 && sink->body.size() + chunk > sink->limit) {
		sink->overflowed = true;
		return 0;
	}
	sink->body.append(ptr, chunk);
	return chunk;
}

std::size_t header_cb(char* buffer, std::size_t size, std::size_t nitems, void* userdata) {
	std::vector<std::pair<std::string, std::string>>* headers =
		static_cast<std::vector<std::pair<std::string, std::string>>*>(userdata);
	std::string line(buffer, size * nitems);
	std::size_t colon = line.find(':');
	if (colon != std::string::npos) {
		std::string key = line.substr(0, colon);
		std::string value = line.substr(colon + 1);
		std::size_t start = value.find_first_not_of(" \t\r\n");
		std::size_t end = value.find_last_not_of(" \t\r\n");
		if (start != std::string::npos && end != std::string::npos) {
			value = value.substr(start, end - start + 1);
		}
		headers->emplace_back(std::move(key), std::move(value));
	}
	return size * nitems;
}

bool is_absolute_url(std::string_view path) {
	return path.starts_with("http://") || path.starts_with("https://");
}

class CurlRuntime {
public:
	CurlRuntime() : status_(curl_global_init(CURL_GLOBAL_DEFAULT)) {}
	~CurlRuntime() {
		if (status_ == CURLE_OK) {
			curl_global_cleanup();
		}
	}

	CurlRuntime(const CurlRuntime&) = delete;
	CurlRuntime& operator=(const CurlRuntime&) = delete;

	[[nodiscard]] CURLcode status() const noexcept { return status_; }

private:
	CURLcode status_;
};

CurlRuntime& curl_runtime() {
	// Function-local static initialization is thread-safe. Keep libcurl's
	// process-wide state alive until normal process shutdown.
	static CurlRuntime runtime;
	return runtime;
}

} // namespace

struct HttpClient::Impl {
	ClientConfig config;
	CURL* curl{nullptr};
	CURLcode global_status{CURLE_OK};

	explicit Impl(ClientConfig cfg) : config(std::move(cfg)) {
		global_status = curl_runtime().status();
		if (global_status == CURLE_OK) {
			curl = curl_easy_init();
		}
	}

	~Impl() {
		if (curl != nullptr) {
			curl_easy_cleanup(curl);
		}
	}

	Impl(const Impl&) = delete;
	Impl& operator=(const Impl&) = delete;
};

HttpClient::HttpClient(ClientConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}

HttpClient::~HttpClient() = default;
HttpClient::HttpClient(HttpClient&&) noexcept = default;
HttpClient& HttpClient::operator=(HttpClient&&) noexcept = default;

Result<HttpResponse> HttpClient::get(std::string_view path) const {
	if (impl_->curl == nullptr) {
		const char* message = impl_->global_status == CURLE_OK
								  ? "curl_easy_init failed"
								  : curl_easy_strerror(impl_->global_status);
		return std::unexpected(Error::network(message));
	}

	CURL* curl = impl_->curl;
	const std::string url =
		is_absolute_url(path) ? std::string{path} : impl_->config.base_url + std::string{path};
	BodySink sink;
	sink.limit = impl_->config.max_response_bytes;
	std::vector<std::pair<std::string, std::string>> response_headers;

	curl_easy_reset(curl);
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, &header_cb);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response_headers);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(impl_->config.timeout.count()));
	// This SDK speaks to public HTTP(S) endpoints only. Without this libcurl
	// happily honours file://, dict://, scp:// and friends, and a path built
	// from user input becomes a local-file read.
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
	// Parity with spc-data/src/fetcher.cpp:
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, impl_->config.user_agent.c_str());
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, impl_->config.verify_ssl ? 1L : 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, impl_->config.verify_ssl ? 2L : 0L);

	CURLcode rc = curl_easy_perform(curl);
	if (rc != CURLE_OK) {
		if (sink.overflowed) {
			return std::unexpected(
				Error::network("response exceeded ClientConfig::max_response_bytes"));
		}
		return std::unexpected(Error::network(curl_easy_strerror(rc)));
	}

	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

	return HttpResponse{
		static_cast<std::int16_t>(http_code),
		std::move(sink.body),
		std::move(response_headers),
	};
}

const ClientConfig& HttpClient::config() const noexcept {
	return impl_->config;
}

} // namespace spc
