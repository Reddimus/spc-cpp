#include "spc/http_client.hpp"

#include <array>
#include <cstddef>
#include <curl/curl.h>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#if !CURL_AT_LEAST_VERSION(7, 85, 0)
#error "spc-cpp needs libcurl 7.85.0 or newer for CURLOPT_PROTOCOLS_STR"
#endif

namespace spc {

namespace {

/// Idle handles kept for reuse. More can be open at once; extras are closed
/// when their request finishes.
constexpr std::size_t kMaxIdleHandles = 8;

/// What `config()` reports on a moved-from client.
const ClientConfig kMovedFromConfig{};

/// Collects the body up to a ceiling. Returning a short count from the write
/// callback makes libcurl abort with CURLE_WRITE_ERROR.
struct BodySink {
	std::string body;
	std::size_t limit{0};
	bool overflowed{false};
	bool failed{false};
};

// libcurl is C, so no exception may leave this callback.
std::size_t write_body(char* data, std::size_t size, std::size_t count, void* user) noexcept {
	BodySink* sink = static_cast<BodySink*>(user);
	const std::size_t chunk = size * count;
	if (sink->limit > 0 && chunk > sink->limit - sink->body.size()) {
		sink->overflowed = true;
		return 0;
	}
	try {
		sink->body.append(data, chunk);
	} catch (...) {
		sink->failed = true;
		return 0;
	}
	return chunk;
}

bool is_absolute_url(std::string_view path) {
	return path.starts_with("http://") || path.starts_with("https://");
}

/// libcurl's process-wide state. Each client holds a shared reference, so the
/// global cleanup runs only after the last handle is gone, whatever order
/// static objects are destroyed in.
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
	CurlRuntime(CurlRuntime&&) = delete;
	CurlRuntime& operator=(CurlRuntime&&) = delete;

	[[nodiscard]] CURLcode status() const noexcept { return status_; }

private:
	CURLcode status_;
};

std::shared_ptr<const CurlRuntime> curl_runtime() {
	static const std::shared_ptr<const CurlRuntime> runtime = std::make_shared<const CurlRuntime>();
	return runtime;
}

/// One easy handle and the error buffer libcurl writes into. They are pooled
/// together, so the buffer lives as long as the handle.
struct Connection {
	CURL* curl{curl_easy_init()};
	std::array<char, CURL_ERROR_SIZE> errors{};

	Connection() = default;
	~Connection() {
		if (curl != nullptr) {
			curl_easy_cleanup(curl);
		}
	}
	Connection(const Connection&) = delete;
	Connection& operator=(const Connection&) = delete;
	Connection(Connection&&) = delete;
	Connection& operator=(Connection&&) = delete;
};

/// The final response's headers. Redirect hops, 1xx responses, and trailers
/// are left out.
std::vector<std::pair<std::string, std::string>> final_headers(CURL* curl) {
	std::vector<std::pair<std::string, std::string>> headers;
	curl_header* previous = nullptr;
	while (curl_header* header = curl_easy_nextheader(curl, CURLH_HEADER, -1, previous)) {
		headers.emplace_back(header->name, header->value);
		previous = header;
	}
	return headers;
}

Result<HttpResponse> perform(Connection& connection, const std::string& url,
							 const ClientConfig& config) {
	CURL* curl = connection.curl;
	curl_easy_reset(curl);
	connection.errors.fill('\0');

	// Refuse to send anything unless the restrictions actually took effect;
	// an older runtime libcurl rejects CURLOPT_PROTOCOLS_STR and would then
	// happily read file:// URLs.
	const long verify = config.verify_ssl ? 1L : 0L;
	const bool configured =
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str()) == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https") == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https") == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, verify) == CURLE_OK &&
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, verify * 2L) == CURLE_OK;
	if (!configured) {
		return std::unexpected(Error::network("libcurl rejected a required transport option"));
	}

	BodySink sink;
	sink.limit = config.max_response_bytes;
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, connection.errors.data());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_body);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(config.timeout.count()));
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, config.user_agent.c_str());
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
	if (sink.limit > 0) {
		// Lets libcurl refuse early when Content-Length is already too big.
		curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(sink.limit));
	}

	const CURLcode rc = curl_easy_perform(curl);
	// Not a NetworkError: retrying would download the same body again.
	if (sink.overflowed || rc == CURLE_FILESIZE_EXCEEDED) {
		return std::unexpected(
			Error::invalid_request("response exceeded ClientConfig::max_response_bytes"));
	}
	if (sink.failed) {
		return std::unexpected(Error::network("out of memory while reading the response"));
	}
	if (rc != CURLE_OK) {
		const bool detailed = connection.errors.front() != '\0';
		return std::unexpected(
			Error::network(detailed ? connection.errors.data() : curl_easy_strerror(rc)));
	}

	long status = 0;
	if (curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status) != CURLE_OK) {
		return std::unexpected(Error::network("libcurl did not report a status code"));
	}
	return HttpResponse{
		static_cast<std::int16_t>(status),
		std::move(sink.body),
		final_headers(curl),
	};
}

} // namespace

struct HttpClient::Impl {
	// First member, so it is destroyed last.
	std::shared_ptr<const CurlRuntime> runtime{curl_runtime()};
	ClientConfig config;
	std::mutex mutex;
	std::vector<std::unique_ptr<Connection>> idle; // guarded by mutex

	explicit Impl(ClientConfig cfg) : config(std::move(cfg)) { idle.reserve(kMaxIdleHandles); }

	/// An idle connection, or a new one. Never waits for other requests.
	std::unique_ptr<Connection> checkout() {
		{
			const std::lock_guard<std::mutex> lock(mutex);
			if (!idle.empty()) {
				std::unique_ptr<Connection> connection = std::move(idle.back());
				idle.pop_back();
				return connection;
			}
		}
		return std::make_unique<Connection>();
	}

	/// Keep the connection for reuse, or close it when the pool is full.
	void checkin(std::unique_ptr<Connection> connection) {
		const std::lock_guard<std::mutex> lock(mutex);
		if (idle.size() < kMaxIdleHandles) {
			idle.push_back(std::move(connection)); // capacity is reserved
		}
	}
};

HttpClient::HttpClient(ClientConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}

HttpClient::~HttpClient() = default;
HttpClient::HttpClient(HttpClient&&) noexcept = default;
HttpClient& HttpClient::operator=(HttpClient&&) noexcept = default;

Result<HttpResponse> HttpClient::get(std::string_view path) const {
	if (impl_ == nullptr) {
		return std::unexpected(Error::invalid_request("HttpClient was moved from"));
	}
	Impl& impl = *impl_;
	if (impl.runtime->status() != CURLE_OK) {
		return std::unexpected(Error::network(curl_easy_strerror(impl.runtime->status())));
	}
	std::unique_ptr<Connection> connection = impl.checkout();
	if (connection->curl == nullptr) {
		return std::unexpected(Error::network("curl_easy_init failed"));
	}
	const std::string url =
		is_absolute_url(path) ? std::string{path} : impl.config.base_url + std::string{path};
	Result<HttpResponse> response = perform(*connection, url, impl.config);
	impl.checkin(std::move(connection));
	return response;
}

const ClientConfig& HttpClient::config() const noexcept {
	if (impl_ == nullptr) {
		return kMovedFromConfig;
	}
	return impl_->config;
}

} // namespace spc
