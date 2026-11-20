/// @file call_budget.hpp
/// @brief Private: what one ArcGIS client call may take from the server.

#pragma once

#include "models/json.hpp"
#include "spc/http_client.hpp"

#include <cstddef>

namespace spc::detail {

/// The bytes one call's pages may total: the response limit of an HttpClient,
/// so paging cannot multiply it. Another transport has no limit the client
/// can read, so it gets the default one.
[[nodiscard]] inline std::size_t call_byte_limit(const HttpTransport& transport) {
	const HttpClient* client = dynamic_cast<const HttpClient*>(&transport);
	return client != nullptr ? client->config().max_response_bytes
							 : ClientConfig{}.max_response_bytes;
}

/// What one client call may take from the server, across every page and layer
/// it reads. A response is bounded on its own, by the transport's size limit
/// and the parser's projection budget; charging every page to one budget keeps
/// a server that pages from multiplying those bounds.
class CallBudget {
public:
	/// `max_bytes` caps the bodies of all pages together; 0 means no limit.
	explicit CallBudget(std::size_t max_bytes) noexcept : max_bytes_(max_bytes) {}

	/// Whether the call can take a page of `body_bytes`. Its first page always
	/// fits: the size of a single response is the transport's to bound.
	[[nodiscard]] bool admits(std::size_t body_bytes) const noexcept {
		return max_bytes_ == 0 || !charged_ ||
			   (bytes_ <= max_bytes_ && body_bytes <= max_bytes_ - bytes_);
	}

	/// Count an accepted page, the first included.
	void charge(std::size_t body_bytes) noexcept {
		bytes_ += body_bytes;
		charged_ = true;
	}

	[[nodiscard]] ProjectionBudget& projection() noexcept { return projection_; }

private:
	std::size_t max_bytes_;
	std::size_t bytes_{0};
	bool charged_{false};
	ProjectionBudget projection_;
};

} // namespace spc::detail
