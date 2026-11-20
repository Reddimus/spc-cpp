/// @file tls_loopback_server.hpp
/// @brief LoopbackServer over TLS, for tests where the response has to arrive
/// on an https request.
///
/// It needs OpenSSL 3 and POSIX sockets. tests/CMakeLists.txt treats that as
/// optional and defines SPC_TEST_HAS_TLS_SERVER when it is there. Each server
/// signs its own certificate at startup, so no private key is checked in, and
/// clients need `verify_ssl = false`.

#pragma once

#ifdef SPC_TEST_HAS_TLS_SERVER

#include "support/loopback_server.hpp"

#include <array>
#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <pthread.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace spc::test {

class TlsLoopbackServer {
public:
	explicit TlsLoopbackServer(LoopbackServer::Handler handler)
		: handler_(std::move(handler)), context_(self_signed_context()) {
		listener_ = socket_detail::listen_on_loopback(port_);
		thread_ = std::thread([this] { serve(); });
	}

	~TlsLoopbackServer() {
		stop_.store(true);
		thread_.join();
		socket_detail::close_socket(listener_);
	}

	TlsLoopbackServer(const TlsLoopbackServer&) = delete;
	TlsLoopbackServer& operator=(const TlsLoopbackServer&) = delete;
	TlsLoopbackServer(TlsLoopbackServer&&) = delete;
	TlsLoopbackServer& operator=(TlsLoopbackServer&&) = delete;

	/// "127.0.0.1:port", for tests that spell the scheme themselves.
	[[nodiscard]] std::string authority() const { return "127.0.0.1:" + std::to_string(port_); }

	[[nodiscard]] std::string url(std::string_view target) const {
		return "https://" + authority() + std::string{target};
	}

	[[nodiscard]] std::vector<LoopbackRequest> requests() const {
		const std::lock_guard<std::mutex> lock(mutex_);
		return requests_;
	}

private:
	struct Free {
		void operator()(SSL_CTX* context) const { SSL_CTX_free(context); }
		void operator()(SSL* ssl) const { SSL_free(ssl); }
		void operator()(EVP_PKEY* key) const { EVP_PKEY_free(key); }
		void operator()(X509* certificate) const { X509_free(certificate); }
		void operator()(X509_NAME* name) const { X509_NAME_free(name); }
	};

	static std::unique_ptr<SSL_CTX, Free> self_signed_context() {
		const std::unique_ptr<EVP_PKEY, Free> key{
			EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256")};
		const std::unique_ptr<X509, Free> certificate{X509_new()};
		const std::unique_ptr<X509_NAME, Free> name{X509_NAME_new()};
		std::unique_ptr<SSL_CTX, Free> context{SSL_CTX_new(TLS_server_method())};
		if (key == nullptr || certificate == nullptr || name == nullptr || context == nullptr) {
			throw std::runtime_error("OpenSSL could not allocate the test certificate");
		}
		constexpr unsigned char kHost[] = "127.0.0.1";
		const bool ready =
			X509_NAME_add_entry_by_txt(name.get(), "CN", MBSTRING_ASC, kHost, -1, -1, 0) == 1 &&
			X509_set_version(certificate.get(), X509_VERSION_3) == 1 &&
			ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 1) == 1 &&
			X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -3600) != nullptr &&
			X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 3600) != nullptr &&
			X509_set_subject_name(certificate.get(), name.get()) == 1 &&
			X509_set_issuer_name(certificate.get(), name.get()) == 1 &&
			X509_set_pubkey(certificate.get(), key.get()) == 1 &&
			X509_sign(certificate.get(), key.get(), EVP_sha256()) > 0 &&
			SSL_CTX_use_certificate(context.get(), certificate.get()) == 1 &&
			SSL_CTX_use_PrivateKey(context.get(), key.get()) == 1;
		if (!ready) {
			throw std::runtime_error("OpenSSL could not build the test certificate");
		}
		return context;
	}

	void serve() {
		// OpenSSL writes with write(), which raises SIGPIPE once the client has
		// hung up. Blocked, the signal becomes an error return on this thread.
		sigset_t broken_pipe;
		(void)sigemptyset(&broken_pipe);
		(void)sigaddset(&broken_pipe, SIGPIPE);
		(void)::pthread_sigmask(SIG_BLOCK, &broken_pipe, nullptr);
		while (!stop_.load()) {
			if (socket_detail::wait_for_connection(listener_) <= 0) {
				continue;
			}
			const socket_detail::Socket client = ::accept(listener_, nullptr, nullptr);
			if (client == socket_detail::kInvalidSocket) {
				continue;
			}
			handle(client);
			socket_detail::close_socket(client);
		}
	}

	void handle(socket_detail::Socket client) {
		socket_detail::limit_io_time(client);
		const std::unique_ptr<SSL, Free> ssl{SSL_new(context_.get())};
		if (ssl == nullptr || SSL_set_fd(ssl.get(), client) != 1 || SSL_accept(ssl.get()) != 1) {
			return; // e.g. a client that verifies the certificate
		}
		std::string raw;
		std::array<char, 4096> buffer{};
		while (raw.find("\r\n\r\n") == std::string::npos && raw.size() < 65536) {
			const int received =
				SSL_read(ssl.get(), buffer.data(), static_cast<int>(buffer.size()));
			if (received <= 0) {
				return;
			}
			raw.append(buffer.data(), static_cast<std::size_t>(received));
		}
		const LoopbackRequest request = parse_request(raw);
		{
			const std::lock_guard<std::mutex> lock(mutex_);
			requests_.push_back(request);
		}
		const std::string response = handler_(request);
		if (!response.empty() &&
			SSL_write(ssl.get(), response.data(), static_cast<int>(response.size())) > 0) {
			(void)SSL_shutdown(ssl.get());
		}
	}

	LoopbackServer::Handler handler_;
	std::unique_ptr<SSL_CTX, Free> context_;
	socket_detail::Socket listener_{socket_detail::kInvalidSocket};
	std::uint16_t port_{0};
	std::atomic<bool> stop_{false};
	mutable std::mutex mutex_;
	std::vector<LoopbackRequest> requests_;
	std::thread thread_;
};

} // namespace spc::test

#endif // SPC_TEST_HAS_TLS_SERVER
