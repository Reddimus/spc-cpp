/// @file allocation_counter.hpp
/// @brief Counts heap allocations while an AllocationProbe is active.
///
/// allocation_counter.cpp replaces the global operator new and delete, so
/// only the benchmarks and their own tests link it. Memory that bypasses
/// operator new is not seen: Glaze grows ordered_small_map index blocks with
/// std::realloc.

#pragma once

#include <cstdint>

namespace spc::test {

/// Heap use since the probe started.
struct AllocationStats {
	std::uint64_t allocations = 0; ///< operator new calls
	std::uint64_t bytes = 0;	   ///< bytes those calls asked for
	/// Most memory live at once. Live sizes are the allocator's block sizes,
	/// because an unsized delete does not say how much it frees.
	std::uint64_t peak_bytes = 0;
	/// Memory still live: what the measured code kept. Negative when it
	/// freed memory allocated before the probe.
	std::int64_t retained_bytes = 0;
};

/// Counts allocations on every thread from construction to destruction.
class AllocationProbe {
public:
	/// @throws std::logic_error if another probe is active.
	AllocationProbe();
	~AllocationProbe();
	AllocationProbe(const AllocationProbe&) = delete;
	AllocationProbe& operator=(const AllocationProbe&) = delete;
	AllocationProbe(AllocationProbe&&) = delete;
	AllocationProbe& operator=(AllocationProbe&&) = delete;

	[[nodiscard]] AllocationStats stats() const noexcept;
};

} // namespace spc::test
