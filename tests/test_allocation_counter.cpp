/// @file test_allocation_counter.cpp
/// @brief The allocation counter behind the benchmarks' heap numbers. A
/// separate binary, because it replaces the global operator new and delete.

#include "support/allocation_counter.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <new>
#include <stdexcept>
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

TEST(AllocationCounter, CountsEveryOperatorNewAndDeleteForm) {
	constexpr std::size_t kSize = 8;
	constexpr std::uintptr_t kAlignmentBytes = 64;
	constexpr std::align_val_t kAlignment{kAlignmentBytes};
	const AllocationProbe probe;
	// Twelve blocks from the eight new forms, one for each delete form.
	const std::array<void*, 3> single = {::operator new(kSize), ::operator new(kSize, std::nothrow),
										 ::operator new(kSize)};
	const std::array<void*, 3> array = {
		::operator new[](kSize), ::operator new[](kSize, std::nothrow), ::operator new[](kSize)};
	const std::array<void*, 3> aligned = {::operator new(kSize, kAlignment),
										  ::operator new(kSize, kAlignment, std::nothrow),
										  ::operator new(kSize, kAlignment)};
	const std::array<void*, 3> aligned_array = {::operator new[](kSize, kAlignment),
												::operator new[](kSize, kAlignment, std::nothrow),
												::operator new[](kSize, kAlignment)};
	bool aligned_ok = true;
	for (const std::array<void*, 3>& blocks : {aligned, aligned_array}) {
		for (const void* block : blocks) {
			aligned_ok = aligned_ok && std::bit_cast<std::uintptr_t>(block) % kAlignmentBytes == 0;
		}
	}
	for (const std::array<void*, 3>& blocks : {single, array, aligned, aligned_array}) {
		for (const void* block : blocks) {
			escaped = block;
		}
	}
	::operator delete(single[0]);
	::operator delete(single[1], kSize);
	::operator delete(single[2], std::nothrow);
	::operator delete[](array[0]);
	::operator delete[](array[1], kSize);
	::operator delete[](array[2], std::nothrow);
	::operator delete(aligned[0], kAlignment);
	::operator delete(aligned[1], kSize, kAlignment);
	::operator delete(aligned[2], kAlignment, std::nothrow);
	::operator delete[](aligned_array[0], kAlignment);
	::operator delete[](aligned_array[1], kSize, kAlignment);
	::operator delete[](aligned_array[2], kAlignment, std::nothrow);
	const AllocationStats stats = probe.stats();

	EXPECT_EQ(stats.allocations, 12u);
	EXPECT_EQ(stats.bytes, 12 * kSize);
	// Each delete form gave its block back to the count.
	EXPECT_EQ(stats.retained_bytes, 0);
	EXPECT_TRUE(aligned_ok);
}

TEST(AllocationCounter, SeesAllocationsMadeInsideTheStandardLibrary) {
	// std::runtime_error copies its message inside libc++ and libstdc++, so
	// this checks that the replacement reaches allocations made there.
	// std::string growth would not show that: GCC inlines it from C++20 on.
	constexpr char kMessage[] = "a message longer than any short-string buffer";
	const AllocationProbe probe;
	const std::runtime_error error{kMessage};
	escaped = error.what();
	const AllocationStats stats = probe.stats();

	EXPECT_GE(stats.allocations, 1u);
	EXPECT_GE(stats.bytes, sizeof(kMessage));
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
