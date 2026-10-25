/// @file loopback_server.hpp
/// @brief A tiny HTTP/1.1 server on 127.0.0.1 for testing HttpClient against
/// a real socket.
///
/// It serves one connection at a time and closes each after one response.
/// The handler returns the complete raw response, so tests control the
/// status line, headers, and framing.

#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace spc::test {

namespace socket_detail {
#ifdef _WIN32
using Socket = SOCKET;
using AddressLength = int;
using IoResult = int;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
constexpr int kShutdownWrite = SD_SEND;
inline void close_socket(Socket socket) {
	(void)::closesocket(socket);
}
inline int wait_for_connection(Socket socket) {
	WSAPOLLFD waiting{socket, POLLRDNORM, 0};
	return ::WSAPoll(&waiting, 1, 50);
}
inline void initialize() {
	struct Winsock {
		Winsock() {
			WSADATA data{};
			if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
				throw std::runtime_error("WSAStartup() failed");
			}
		}
		~Winsock() { (void)::WSACleanup(); }
	};
	static const Winsock initialized;
}
#else
using Socket = int;
using AddressLength = socklen_t;
using IoResult = ssize_t;
constexpr Socket kInvalidSocket = -1;
constexpr int kShutdownWrite = SHUT_WR;
inline void close_socket(Socket socket) {
	(void)::close(socket);
}
inline int wait_for_connection(Socket socket) {
	pollfd waiting{socket, POLLIN, 0};
	return ::poll(&waiting, 1, 50);
}
inline void initialize() {}
#endif
inline void set_option(Socket socket, int option, const void* value, int length) {
	(void)::setsockopt(socket, SOL_SOCKET, option, static_cast<const char*>(value), length);
}
} // namespace socket_detail

struct LoopbackRequest {
	std::string target; ///< e.g. "/final?x=1"
	std::vector<std::pair<std::string, std::string>> headers;

	[[nodiscard]] std::string header(std::string_view name) const {
		for (const std::pair<std::string, std::string>& h : headers) {
			if (h.first == name) {
				return h.second;
			}
		}
		return {};
	}
};

class LoopbackServer {
public:
	using Handler = std::function<std::string(const LoopbackRequest&)>;

	explicit LoopbackServer(Handler handler) : handler_(std::move(handler)) {
		socket_detail::initialize();
		listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
		if (listener_ == socket_detail::kInvalidSocket) {
			throw std::runtime_error("socket() failed");
		}
		const int reuse = 1;
		socket_detail::set_option(listener_, SO_REUSEADDR, &reuse, sizeof(reuse));
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = 0; // any free port
		if (::bind(listener_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
			::listen(listener_, 64) != 0) {
			socket_detail::close_socket(listener_);
			throw std::runtime_error("bind() or listen() failed");
		}
		socket_detail::AddressLength length = sizeof(address);
		(void)::getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length);
		port_ = ntohs(address.sin_port);
		thread_ = std::thread([this] { serve(); });
	}

	~LoopbackServer() {
		stop_.store(true);
		thread_.join();
		socket_detail::close_socket(listener_);
	}

	LoopbackServer(const LoopbackServer&) = delete;
	LoopbackServer& operator=(const LoopbackServer&) = delete;
	LoopbackServer(LoopbackServer&&) = delete;
	LoopbackServer& operator=(LoopbackServer&&) = delete;

	/// Absolute URL for `target` on this server; a numeric host, so libcurl
	/// never starts a resolver thread.
	[[nodiscard]] std::string url(std::string_view target) const {
		return "http://127.0.0.1:" + std::to_string(port_) + std::string{target};
	}

	[[nodiscard]] std::vector<LoopbackRequest> requests() const {
		const std::lock_guard<std::mutex> lock(mutex_);
		return requests_;
	}

private:
	void serve() {
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
#ifdef _WIN32
		const DWORD timeout = 2000;
#else
		const timeval timeout{2, 0};
#endif
		socket_detail::set_option(client, SO_RCVTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
		const int no_sigpipe = 1;
		socket_detail::set_option(client, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
		std::string raw;
		std::vector<char> buffer(4096);
		while (raw.find("\r\n\r\n") == std::string::npos && raw.size() < 65536) {
			const socket_detail::IoResult received =
				::recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
			if (received <= 0) {
				return;
			}
			raw.append(buffer.data(), static_cast<std::size_t>(received));
		}
		const LoopbackRequest request = parse(raw);
		{
			const std::lock_guard<std::mutex> lock(mutex_);
			requests_.push_back(request);
		}
		send_all(client, handler_(request));
		(void)::shutdown(client, socket_detail::kShutdownWrite);
	}

	static LoopbackRequest parse(const std::string& raw) {
		LoopbackRequest request;
		const std::size_t line_end = raw.find("\r\n");
		const std::string request_line = raw.substr(0, line_end);
		const std::size_t first_space = request_line.find(' ');
		const std::size_t second_space = request_line.find(' ', first_space + 1);
		if (first_space != std::string::npos && second_space != std::string::npos) {
			request.target = request_line.substr(first_space + 1, second_space - first_space - 1);
		}
		std::size_t start = line_end + 2;
		while (start < raw.size()) {
			const std::size_t end = raw.find("\r\n", start);
			if (end == std::string::npos || end == start) {
				break;
			}
			const std::string line = raw.substr(start, end - start);
			const std::size_t colon = line.find(':');
			if (colon != std::string::npos) {
				const std::size_t value_start = line.find_first_not_of(' ', colon + 1);
				request.headers.emplace_back(line.substr(0, colon), value_start == std::string::npos
																		? std::string{}
																		: line.substr(value_start));
			}
			start = end + 2;
		}
		return request;
	}

	static void send_all(socket_detail::Socket client, const std::string& response) {
#ifdef MSG_NOSIGNAL
		constexpr int kFlags = MSG_NOSIGNAL;
#else
		constexpr int kFlags = 0;
#endif
		std::size_t sent = 0;
		while (sent < response.size()) {
			const socket_detail::IoResult written = ::send(
				client, response.data() + sent,
				static_cast<int>(std::min(response.size() - sent, std::size_t{65536})), kFlags);
			if (written <= 0) {
				return; // the client hung up, e.g. after a size-limit abort
			}
			sent += static_cast<std::size_t>(written);
		}
	}

	Handler handler_;
	socket_detail::Socket listener_{socket_detail::kInvalidSocket};
	std::uint16_t port_{0};
	std::atomic<bool> stop_{false};
	mutable std::mutex mutex_;
	std::vector<LoopbackRequest> requests_;
	std::thread thread_;
};

/// A complete response with a Content-Length body.
inline std::string http_response(std::string_view status, std::string_view body,
								 std::string_view extra_headers = {}) {
	return "HTTP/1.1 " + std::string{status} +
		   "\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n" +
		   std::string{extra_headers} + "\r\n" + std::string{body};
}

} // namespace spc::test
