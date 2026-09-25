/// @file pagination.hpp
/// @brief Offset paging for ArcGIS MapServer `query` requests.

#pragma once

#include <cstdint>

namespace spc {

/// Tracks `resultOffset` across ArcGIS query pages.
///
/// ArcGIS caps each response (2000 records on NOAA's services) and sets
/// `exceededTransferLimit` when more remain. The server may also return fewer
/// records than `resultRecordCount`, so `advance()` moves the offset by the
/// number actually returned. SPC layers rarely need a second page.
class ArcGISPager {
public:
	/// `page_size` is the `resultRecordCount` sent with each request.
	/// `max_pages` stops a server that keeps reporting truncation; see
	/// `page_limit_reached()`. Non-positive values fall back to the defaults.
	// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
	explicit ArcGISPager(std::int32_t page_size = 2000, std::int32_t max_pages = 100)
		: page_size_(page_size > 0 ? page_size : 2000),
		  max_pages_(max_pages > 0 ? max_pages : 100) {}

	[[nodiscard]] std::int32_t page_size() const noexcept { return page_size_; }
	[[nodiscard]] std::int32_t max_pages() const noexcept { return max_pages_; }

	/// `resultOffset` for the next request.
	[[nodiscard]] std::int64_t offset() const noexcept { return offset_; }

	/// Whether to fetch another page. True until `advance()` says otherwise.
	[[nodiscard]] bool has_more() const noexcept { return has_more_; }

	/// True when paging stopped at `max_pages` while the server still reported
	/// truncation, i.e. the result is incomplete.
	[[nodiscard]] bool page_limit_reached() const noexcept {
		return server_truncated_ && pages_fetched_ >= max_pages_;
	}

	/// Record a fetched page: the records it held and its
	/// `exceededTransferLimit` flag.
	void advance(std::int32_t records_returned, bool exceeded_transfer_limit) noexcept {
		offset_ += records_returned > 0 ? records_returned : 0;
		++pages_fetched_;
		server_truncated_ = exceeded_transfer_limit;
		has_more_ = exceeded_transfer_limit && pages_fetched_ < max_pages_;
	}

	/// Start again from the first page.
	void reset() noexcept {
		offset_ = 0;
		pages_fetched_ = 0;
		has_more_ = true;
		server_truncated_ = false;
	}

private:
	std::int32_t page_size_;
	std::int32_t max_pages_;
	std::int64_t offset_{0};
	std::int32_t pages_fetched_{0};
	bool has_more_{true};
	bool server_truncated_{false};
};

} // namespace spc
