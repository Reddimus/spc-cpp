/// @file test_allocation_counter.cpp
/// @brief The allocation counter behind the benchmarks' heap numbers. A
/// separate binary, because it replaces the global operator new and delete.

#include "support/allocation_counter.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using spc::test::AllocationProbe;
using spc::test::AllocationStats;

// Storing a pointer here keeps the optimizer from removing an allocation
// whose memory is never read.
const void* volatile escaped = nullptr;

TEST(AllocationCounter, CountsCallsAndRequestedBytes) {
	const AllocationProbe probe;
	void* block = ::operator new(100);
	escaped = block;
	::operator delete(block);
	const AllocationStats stats = probe.stats();

	EXPECT_EQ(stats.allocations, 1u);
	EXPECT_EQ(stats.bytes, 100u);
	EXPECT_GE(stats.peak_bytes, 100u);
	EXPECT_EQ(stats.retained_bytes, 0);
}

TEST(AllocationCounter, RetainedBytesAreWhatIsStillLive) {
	const AllocationProbe probe;
	std::vector<char> kept(1000);
	escaped = kept.data();
	{
		std::vector<char> scratch(4000);
		escaped = scratch.data();
	}
	const AllocationStats stats = probe.stats();

	EXPECT_EQ(stats.allocations, 2u);
	EXPECT_GE(stats.retained_bytes, 1000);
	EXPECT_LT(stats.retained_bytes, 4000);
	EXPECT_GE(stats.peak_bytes, 5000u);
}

TEST(AllocationCounter, FreeingOlderMemoryMakesRetainedNegative) {
	std::vector<char> older(1000);
	escaped = older.data();
	const AllocationProbe probe;
	std::vector<char>().swap(older);
	const AllocationStats stats = probe.stats();

	EXPECT_EQ(stats.allocations, 0u);
	EXPECT_EQ(stats.peak_bytes, 0u);
	EXPECT_LE(stats.retained_bytes, -1000);
}

TEST(AllocationCounter, CountsEveryOperatorNewForm) {
	constexpr std::align_val_t kAlignment{64};
	const AllocationProbe probe;
	void* plain = ::operator new(8);
	void* array = ::operator new[](8);
	void* nothrow = ::operator new(8, std::nothrow);
	void* aligned = ::operator new(8, kAlignment);
	void* aligned_array = ::operator new[](8, kAlignment, std::nothrow);
	for (const void* block : {plain, array, nothrow, aligned, aligned_array}) {
		escaped = block;
	}
	const bool aligned_ok = std::bit_cast<std::uintptr_t>(aligned) % 64 == 0 &&
							std::bit_cast<std::uintptr_t>(aligned_array) % 64 == 0;
	::operator delete(plain, 8);
	::operator delete[](array);
	::operator delete(nothrow, std::nothrow);
	::operator delete(aligned, 8, kAlignment);
	::operator delete[](aligned_array, kAlignment);
	const AllocationStats stats = probe.stats();

	EXPECT_EQ(stats.allocations, 5u);
	EXPECT_EQ(stats.bytes, 40u);
	EXPECT_EQ(stats.retained_bytes, 0);
	EXPECT_TRUE(aligned_ok);
}

TEST(AllocationCounter, SeesAllocationsMadeInsideTheStandardLibrary) {
	// libc++ and libstdc++ compile std::string's growth into their shared
	// library, so this checks that the replacement reaches it there too.
	const AllocationProbe probe;
	std::string text;
	text.append(5000, 'x');
	escaped = text.data();
	const AllocationStats stats = probe.stats();

	EXPECT_GE(stats.allocations, 1u);
	EXPECT_GE(stats.bytes, 5001u);
}

TEST(AllocationCounter, StartsEachProbeFromZero) {
	{
		const AllocationProbe first;
		std::vector<char> counted(100);
		escaped = counted.data();
	}
	const AllocationProbe second;
	const AllocationStats stats = second.stats();

	EXPECT_EQ(stats.allocations, 0u);
	EXPECT_EQ(stats.bytes, 0u);
	EXPECT_EQ(stats.peak_bytes, 0u);
	EXPECT_EQ(stats.retained_bytes, 0);
}

TEST(AllocationCounter, AllowsOneProbeAtATime) {
	const AllocationProbe probe;
	EXPECT_THROW((void)AllocationProbe{}, std::logic_error);
}

} // namespace
