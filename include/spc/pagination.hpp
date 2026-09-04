#pragma once

#include <cstdint>

namespace spc {

/// ArcGIS MapServer `query` paging.
///
/// An ArcGIS layer query caps the result set (commonly 1000-2000 records)
/// and signals truncation with `"exceededTransferLimit": true` in the
/// response envelope. To page, re-issue with `resultOffset` advanced by the
/// number of records the server actually returned until it stops reporting
/// truncation.
///
/// The requested `resultRecordCount` is only a hint: ArcGIS clamps it to the
/// layer's own `maxRecordCount`, so a truncated page can be shorter than the
/// page size. Advancing by the request size would then skip every record in
/// the gap, which is why `advance()` requires the returned record count.
///
/// SPC outlook layers are tiny (single-digit feature counts), so paging
/// rarely triggers — but storm-report / LSR layers can exceed the cap, and
/// the contract is explicit, so the SDK implements it correctly rather than
/// assuming one page.
class ArcGISPager {
public:
	/// `page_size` is the `resultRecordCount` passed per request. ArcGIS's
	/// hard server max is typically 2000; that is the default. `max_pages`
	/// bounds a server (or caching proxy) that keeps reporting truncation
	/// without advancing — see `page_limit_reached()`.
	// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
	explicit ArcGISPager(std::int32_t page_size = 2000, std::int32_t max_pages = 100)
		: page_size_(page_size > 0 ? page_size : 2000),
		  max_pages_(max_pages > 0 ? max_pages : 100) {}

	[[nodiscard]] std::int32_t page_size() const noexcept { return page_size_; }

	/// Hard ceiling on the number of pages fetched for one query.
	[[nodiscard]] std::int32_t max_pages() const noexcept { return max_pages_; }

	/// Current `resultOffset` to send on the next request.
	[[nodiscard]] std::int64_t offset() const noexcept { return offset_; }

	/// Whether another page should be fetched. Seeded `true`; the caller
	/// passes the response's `exceededTransferLimit` after each page.
	[[nodiscard]] bool has_more() const noexcept { return has_more_; }

	/// True when paging stopped at `max_pages` while the server was still
	/// reporting truncation — a non-converging server, not a complete result.
	[[nodiscard]] bool page_limit_reached() const noexcept {
		return server_truncated_ && pages_fetched_ >= max_pages_;
	}

	/// Record the just-fetched page: advance the offset by the records the
	/// server returned, and keep going only while it still reports truncation
	/// and the page ceiling has not been reached.
	void advance(std::int32_t records_returned, bool exceeded_transfer_limit) noexcept {
		offset_ += records_returned > 0 ? records_returned : 0;
		++pages_fetched_;
		server_truncated_ = exceeded_transfer_limit;
		has_more_ = exceeded_transfer_limit && pages_fetched_ < max_pages_;
	}

	/// Reset to the first page.
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
