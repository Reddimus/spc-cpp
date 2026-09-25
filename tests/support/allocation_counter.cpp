/// @file allocation_counter.cpp
/// @brief Global operator new and delete. They forward to malloc and free and
/// count while an AllocationProbe is active.

#include "support/allocation_counter.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <stdexcept>

#if defined(__APPLE__)
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif

namespace {

struct Counters {
	std::atomic<bool> owned{false};	 // a probe exists
	std::atomic<bool> active{false}; // allocations are being counted
	std::atomic<std::uint64_t> allocations{0};
	std::atomic<std::uint64_t> bytes{0};
	std::atomic<std::int64_t> live{0};
	std::atomic<std::int64_t> peak{0};
};

// Constant-initialized, because operator new runs before main.
constinit Counters counters;

constexpr std::memory_order kRelaxed = std::memory_order_relaxed;

// An alignment of 0 means operator new's default.
void* raw_allocate(std::size_t size, std::size_t alignment) noexcept {
	if (alignment == 0) {
		return std::malloc(size);
	}
#if defined(_WIN32)
	return _aligned_malloc(size, alignment);
#else
	void* ptr = nullptr;
	const std::size_t valid = alignment < sizeof(void*) ? sizeof(void*) : alignment;
	return posix_memalign(&ptr, valid, size) == 0 ? ptr : nullptr;
#endif
}

void raw_free(void* ptr, [[maybe_unused]] std::size_t alignment) noexcept {
#if defined(_WIN32)
	if (alignment != 0) {
		_aligned_free(ptr);
		return;
	}
#endif
	std::free(ptr);
}

std::int64_t block_size(void* ptr, [[maybe_unused]] std::size_t alignment) noexcept {
#if defined(__APPLE__)
	const std::size_t size = malloc_size(ptr);
#elif defined(_WIN32)
	const std::size_t size = alignment != 0 ? _aligned_msize(ptr, alignment, 0) : _msize(ptr);
#else
	const std::size_t size = malloc_usable_size(ptr);
#endif
	return static_cast<std::int64_t>(size);
}

void* allocate(std::size_t size, std::size_t alignment) {
	// operator new(0) must still return a distinct pointer.
	const std::size_t request = size == 0 ? 1 : size;
	void* ptr = raw_allocate(request, alignment);
	while (ptr == nullptr) {
		const std::new_handler handler = std::get_new_handler();
		if (handler == nullptr) {
			throw std::bad_alloc();
		}
		handler();
		ptr = raw_allocate(request, alignment);
	}
	if (counters.active.load(kRelaxed)) {
		counters.allocations.fetch_add(1, kRelaxed);
		counters.bytes.fetch_add(size, kRelaxed);
		const std::int64_t block = block_size(ptr, alignment);
		const std::int64_t live = counters.live.fetch_add(block, kRelaxed) + block;
		std::int64_t peak = counters.peak.load(kRelaxed);
		while (live > peak && !counters.peak.compare_exchange_weak(peak, live, kRelaxed)) {
		}
	}
	return ptr;
}

void* allocate_nothrow(std::size_t size, std::size_t alignment) noexcept {
	try {
		return allocate(size, alignment);
	} catch (const std::bad_alloc&) {
		return nullptr;
	}
}

void deallocate(void* ptr, std::size_t alignment) noexcept {
	if (ptr == nullptr) {
		return;
	}
	if (counters.active.load(kRelaxed)) {
		counters.live.fetch_sub(block_size(ptr, alignment), kRelaxed);
	}
	raw_free(ptr, alignment);
}

std::size_t to_size(std::align_val_t alignment) noexcept {
	return static_cast<std::size_t>(alignment);
}

} // namespace

namespace spc::test {

AllocationProbe::AllocationProbe() {
	if (counters.owned.exchange(true)) {
		throw std::logic_error("another AllocationProbe is active");
	}
	// Zero before counting starts, so no allocation is counted and then wiped.
	counters.allocations.store(0);
	counters.bytes.store(0);
	counters.live.store(0);
	counters.peak.store(0);
	counters.active.store(true);
}

AllocationProbe::~AllocationProbe() {
	counters.active.store(false);
	counters.owned.store(false);
}

AllocationStats AllocationProbe::stats() const noexcept {
	AllocationStats stats;
	stats.allocations = counters.allocations.load();
	stats.bytes = counters.bytes.load();
	stats.peak_bytes = static_cast<std::uint64_t>(counters.peak.load());
	stats.retained_bytes = counters.live.load();
	return stats;
}

} // namespace spc::test

void* operator new(std::size_t size) {
	return allocate(size, 0);
}

void* operator new[](std::size_t size) {
	return allocate(size, 0);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
	return allocate(size, to_size(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
	return allocate(size, to_size(alignment));
}

void* operator new(std::size_t size, const std::nothrow_t& /*tag*/) noexcept {
	return allocate_nothrow(size, 0);
}

void* operator new[](std::size_t size, const std::nothrow_t& /*tag*/) noexcept {
	return allocate_nothrow(size, 0);
}

void* operator new(std::size_t size, std::align_val_t alignment,
				   const std::nothrow_t& /*tag*/) noexcept {
	return allocate_nothrow(size, to_size(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment,
					 const std::nothrow_t& /*tag*/) noexcept {
	return allocate_nothrow(size, to_size(alignment));
}

void operator delete(void* ptr) noexcept {
	deallocate(ptr, 0);
}

void operator delete[](void* ptr) noexcept {
	deallocate(ptr, 0);
}

void operator delete(void* ptr, std::size_t /*size*/) noexcept {
	deallocate(ptr, 0);
}

void operator delete[](void* ptr, std::size_t /*size*/) noexcept {
	deallocate(ptr, 0);
}

void operator delete(void* ptr, std::align_val_t alignment) noexcept {
	deallocate(ptr, to_size(alignment));
}

void operator delete[](void* ptr, std::align_val_t alignment) noexcept {
	deallocate(ptr, to_size(alignment));
}

void operator delete(void* ptr, std::size_t /*size*/, std::align_val_t alignment) noexcept {
	deallocate(ptr, to_size(alignment));
}

void operator delete[](void* ptr, std::size_t /*size*/, std::align_val_t alignment) noexcept {
	deallocate(ptr, to_size(alignment));
}

void operator delete(void* ptr, const std::nothrow_t& /*tag*/) noexcept {
	deallocate(ptr, 0);
}

void operator delete[](void* ptr, const std::nothrow_t& /*tag*/) noexcept {
	deallocate(ptr, 0);
}

void operator delete(void* ptr, std::align_val_t alignment,
					 const std::nothrow_t& /*tag*/) noexcept {
	deallocate(ptr, to_size(alignment));
}

void operator delete[](void* ptr, std::align_val_t alignment,
					   const std::nothrow_t& /*tag*/) noexcept {
	deallocate(ptr, to_size(alignment));
}
