#include "spc/http_client.hpp"

#include <curl/curl.h>
#include <string>
#include <utility>

namespace spc {

namespace {

std::size_t write_cb(char* ptr, std::size_t size, std::size_t nmemb, void* user) {
	static_cast<std::string*>(user)->append(ptr, size * nmemb);
	return size * nmemb;
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

} // namespace

struct HttpClient::Impl {
	ClientConfig config;
	CURL* curl{nullptr};

	explicit Impl(ClientConfig cfg) : config(std::move(cfg)) {
		curl_global_init(CURL_GLOBAL_DEFAULT);
		curl = curl_easy_init();
	}

	~Impl() {
		if (curl != nullptr) {
			curl_easy_cleanup(curl);
		}
		curl_global_cleanup();
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
		return std::unexpected(Error::network("curl_easy_init failed"));
	}

	CURL* curl = impl_->curl;
	const std::string url =
		is_absolute_url(path) ? std::string{path} : impl_->config.base_url + std::string{path};
	std::string body;
	std::vector<std::pair<std::string, std::string>> response_headers;

	curl_easy_reset(curl);
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, &header_cb);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response_headers);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(impl_->config.timeout.count()));
	// Parity with spc-data/src/fetcher.cpp:
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, impl_->config.user_agent.c_str());
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, impl_->config.verify_ssl ? 1L : 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, impl_->config.verify_ssl ? 2L : 0L);

	CURLcode rc = curl_easy_perform(curl);
	if (rc != CURLE_OK) {
		return std::unexpected(Error::network(curl_easy_strerror(rc)));
	}

	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

	return HttpResponse{
		static_cast<std::int16_t>(http_code),
		std::move(body),
		std::move(response_headers),
	};
}

const ClientConfig& HttpClient::config() const noexcept {
	return impl_->config;
}

} // namespace spc
