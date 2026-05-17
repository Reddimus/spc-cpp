#pragma once

#include <cstdint>

namespace spc {

/// ArcGIS MapServer `query` paging.
///
/// An ArcGIS layer query caps the result set (commonly 1000-2000 records)
/// and signals truncation with `"exceededTransferLimit": true` in the
/// response envelope. To page, re-issue with `resultOffset` advanced by the
/// page size until the server stops reporting truncation.
///
/// SPC outlook layers are tiny (single-digit feature counts), so paging
/// rarely triggers — but storm-report / LSR layers can exceed the cap, and
/// the contract is explicit, so the SDK implements it correctly rather than
/// assuming one page.
class ArcGISPager {
public:
	/// `page_size` is the `resultRecordCount` passed per request. ArcGIS's
	/// hard server max is typically 2000; that is the default.
	explicit ArcGISPager(std::int32_t page_size = 2000)
		: page_size_(page_size > 0 ? page_size : 2000) {}

	[[nodiscard]] std::int32_t page_size() const noexcept { return page_size_; }

	/// Current `resultOffset` to send on the next request.
	[[nodiscard]] std::int32_t offset() const noexcept { return offset_; }

	/// Whether another page should be fetched. Seeded `true`; the caller
	/// passes the response's `exceededTransferLimit` after each page.
	[[nodiscard]] bool has_more() const noexcept { return has_more_; }

	/// Record the just-fetched page: advance the offset by `page_size` and
	/// keep going only while the server still reports truncation.
	void advance(bool exceeded_transfer_limit) noexcept {
		offset_ += page_size_;
		has_more_ = exceeded_transfer_limit;
	}

	/// Reset to the first page.
	void reset() noexcept {
		offset_ = 0;
		has_more_ = true;
	}

private:
	std::int32_t page_size_;
	std::int32_t offset_{0};
	bool has_more_{true};
};

} // namespace spc
