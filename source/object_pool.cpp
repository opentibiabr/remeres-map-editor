//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "object_pool.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

#ifndef RME_OBJECT_POOL_STATS
	#define RME_OBJECT_POOL_STATS 0
#endif

#if RME_OBJECT_POOL_STATS
	#include <cstdio>

	#ifdef _WIN32
		#ifndef NOMINMAX
			#define NOMINMAX
		#endif
		#include <windows.h>
	#endif
#endif

namespace {
	constexpr std::size_t kAlignment = alignof(std::max_align_t);
	static_assert((kAlignment & (kAlignment - 1)) == 0, "pool alignment must be a power of two");

	constexpr uint32_t kMagic = 0x524D4550; // "RMEP"
	constexpr uint16_t kFallbackClass = 0xFFFF;

	// Total block sizes including AllocationHeader. These cover Item subclasses,
	// Tile, and Floor while preserving a safe heap fallback for larger objects.
	constexpr std::array<std::size_t, 16> kClassSizes = {
		32, 48, 64, 80,
		96, 112, 128, 160,
		192, 224, 256, 320,
		384, 512, 768, 1024
	};

	constexpr std::size_t kClassCount = kClassSizes.size();
	constexpr std::size_t kMaxClassSize = 1024;
	static_assert(kClassSizes[kClassCount - 1] == kMaxClassSize, "max class size must match the largest pool class");

	constexpr std::size_t kSlabBytes = 4 * 1024 * 1024;
	constexpr std::size_t kMinBlocksPerSlab = 64;

#if RME_OBJECT_POOL_STATS
	constexpr std::memory_order kStatsMemoryOrder = std::memory_order_relaxed;

	void emitStatsLine(const char* line) noexcept {
	#ifdef _WIN32
		OutputDebugStringA(line);
		OutputDebugStringA("\n");
	#endif
		std::fputs(line, stderr);
		std::fputc('\n', stderr);
	}

	void updateMax(std::atomic<uint64_t> &target, uint64_t value) noexcept {
		uint64_t current = target.load(kStatsMemoryOrder);
		while (current < value && !target.compare_exchange_weak(current, value, kStatsMemoryOrder, kStatsMemoryOrder)) {
		}
	}

	struct PoolStats {
		std::atomic<uint64_t> allocCalls { 0 };
		std::atomic<uint64_t> pooledAllocCalls { 0 };
		std::atomic<uint64_t> fallbackSizeAllocCalls { 0 };
		std::atomic<uint64_t> fallbackThreadAllocCalls { 0 };
		std::atomic<uint64_t> fallbackFreeCalls { 0 };
		std::atomic<uint64_t> remoteFreeCalls { 0 };
		std::atomic<uint64_t> remoteDrainCalls { 0 };
		std::atomic<uint64_t> slabRefillCalls { 0 };
		std::atomic<uint64_t> maxFallbackPayloadSize { 0 };
		std::array<std::atomic<uint64_t>, kClassCount> allocByClass {};
		std::array<std::atomic<uint64_t>, kClassCount> refillByClass {};

		PoolStats() noexcept {
			reset();
		}

		void reset() noexcept {
			allocCalls.store(0, kStatsMemoryOrder);
			pooledAllocCalls.store(0, kStatsMemoryOrder);
			fallbackSizeAllocCalls.store(0, kStatsMemoryOrder);
			fallbackThreadAllocCalls.store(0, kStatsMemoryOrder);
			fallbackFreeCalls.store(0, kStatsMemoryOrder);
			remoteFreeCalls.store(0, kStatsMemoryOrder);
			remoteDrainCalls.store(0, kStatsMemoryOrder);
			slabRefillCalls.store(0, kStatsMemoryOrder);
			maxFallbackPayloadSize.store(0, kStatsMemoryOrder);

			for (auto &counter : allocByClass) {
				counter.store(0, kStatsMemoryOrder);
			}

			for (auto &counter : refillByClass) {
				counter.store(0, kStatsMemoryOrder);
			}
		}

		void dump() const noexcept {
			char line[512];
			std::snprintf(
				line,
				sizeof(line),
				"[object_pool] alloc=%llu pooled=%llu fallback_size=%llu fallback_thread=%llu fallback_free=%llu remote_free=%llu remote_drain=%llu slab_refill=%llu max_fallback_payload=%llu",
				static_cast<unsigned long long>(allocCalls.load(kStatsMemoryOrder)),
				static_cast<unsigned long long>(pooledAllocCalls.load(kStatsMemoryOrder)),
				static_cast<unsigned long long>(fallbackSizeAllocCalls.load(kStatsMemoryOrder)),
				static_cast<unsigned long long>(fallbackThreadAllocCalls.load(kStatsMemoryOrder)),
				static_cast<unsigned long long>(fallbackFreeCalls.load(kStatsMemoryOrder)),
				static_cast<unsigned long long>(remoteFreeCalls.load(kStatsMemoryOrder)),
				static_cast<unsigned long long>(remoteDrainCalls.load(kStatsMemoryOrder)),
				static_cast<unsigned long long>(slabRefillCalls.load(kStatsMemoryOrder)),
				static_cast<unsigned long long>(maxFallbackPayloadSize.load(kStatsMemoryOrder))
			);
			emitStatsLine(line);

			for (std::size_t i = 0; i < kClassCount; ++i) {
				const auto allocCount = allocByClass[i].load(kStatsMemoryOrder);
				const auto refillCount = refillByClass[i].load(kStatsMemoryOrder);
				if (allocCount == 0 && refillCount == 0) {
					continue;
				}

				std::snprintf(
					line,
					sizeof(line),
					"[object_pool] class=%zu block_size=%zu alloc=%llu refill=%llu",
					i,
					kClassSizes[i],
					static_cast<unsigned long long>(allocCount),
					static_cast<unsigned long long>(refillCount)
				);
				emitStatsLine(line);
			}
		}
	};
#endif

	struct alignas(std::max_align_t) AllocationHeader {
		uint32_t magic;
		uint16_t classIndex;
		uint16_t reserved;
	};

	static_assert(sizeof(AllocationHeader) <= 32, "unexpected pool allocation header size");

	struct FreeNode {
		FreeNode* next;
	};

	constexpr std::size_t alignUp(std::size_t value, std::size_t alignment) noexcept {
		return (value + alignment - 1) & ~(alignment - 1);
	}

	constexpr std::size_t kLookupCount = kMaxClassSize / kAlignment + 1;

	constexpr std::array<uint16_t, kLookupCount> makeClassLookup() {
		std::array<uint16_t, kLookupCount> lookup {};

		for (std::size_t unit = 0; unit < kLookupCount; ++unit) {
			const std::size_t bytes = unit * kAlignment;
			uint16_t selected = kFallbackClass;

			for (uint16_t i = 0; i < kClassCount; ++i) {
				if (bytes <= kClassSizes[i]) {
					selected = i;
					break;
				}
			}

			lookup[unit] = selected;
		}

		return lookup;
	}

	constexpr auto kClassLookup = makeClassLookup();

	uint16_t classForPayloadSize(std::size_t payloadSize) noexcept {
		const std::size_t totalSize = alignUp(
			sizeof(AllocationHeader) + std::max<std::size_t>(payloadSize, 1),
			kAlignment
		);

		if (totalSize > kMaxClassSize) {
			return kFallbackClass;
		}

		return kClassLookup[totalSize / kAlignment];
	}

	void* allocateFallback(std::size_t payloadSize) {
		const std::size_t totalSize = alignUp(
			sizeof(AllocationHeader) + std::max<std::size_t>(payloadSize, 1),
			kAlignment
		);

		auto* header = static_cast<AllocationHeader*>(::operator new(totalSize));
		header->magic = kMagic;
		header->classIndex = kFallbackClass;
		header->reserved = 0;
		return header + 1;
	}

	void deallocateFallback(AllocationHeader* header) noexcept {
		::operator delete(header);
	}

	class SmallObjectPool {
	public:
		void bindOwnerThread() noexcept {
			std::lock_guard<std::mutex> lock(ownerMutex_);

			const auto currentThread = std::this_thread::get_id();
			if (!ownerSet_.load(std::memory_order_relaxed)) {
				ownerThread_ = currentThread;
				ownerSet_.store(true, std::memory_order_release);
				return;
			}

			assert(ownerThread_ == currentThread);
		}

		void resetStats() noexcept {
#if RME_OBJECT_POOL_STATS
			stats_.reset();
#endif
		}

		void dumpStats() noexcept {
#if RME_OBJECT_POOL_STATS
			stats_.dump();
#endif
		}

		void* allocate(std::size_t payloadSize) {
#if RME_OBJECT_POOL_STATS
			stats_.allocCalls.fetch_add(1, kStatsMemoryOrder);
#endif

			const uint16_t cls = classForPayloadSize(payloadSize);
			if (cls == kFallbackClass) {
#if RME_OBJECT_POOL_STATS
				stats_.fallbackSizeAllocCalls.fetch_add(1, kStatsMemoryOrder);
				updateMax(stats_.maxFallbackPayloadSize, static_cast<uint64_t>(payloadSize));
#endif
				return allocateFallback(payloadSize);
			}

			if (!becomeOwnerOrIsOwner()) {
#if RME_OBJECT_POOL_STATS
				stats_.fallbackThreadAllocCalls.fetch_add(1, kStatsMemoryOrder);
#endif
				return allocateFallback(payloadSize);
			}

#if RME_OBJECT_POOL_STATS
			stats_.pooledAllocCalls.fetch_add(1, kStatsMemoryOrder);
			stats_.allocByClass[cls].fetch_add(1, kStatsMemoryOrder);
#endif

			FreeNode* node = localFreeLists_[cls];
			if (!node) {
				drainRemoteIntoEmptyLocal(cls);
				node = localFreeLists_[cls];

				if (!node) {
					refill(cls);
					node = localFreeLists_[cls];
				}
			}

			localFreeLists_[cls] = node->next;

			auto* header = reinterpret_cast<AllocationHeader*>(node);
			header->magic = kMagic;
			header->classIndex = cls;
			header->reserved = 0;
			return header + 1;
		}

		void deallocate(void* ptr) noexcept {
			if (!ptr) {
				return;
			}

			auto* header = static_cast<AllocationHeader*>(ptr) - 1;
			if (header->magic != kMagic) {
				assert(false && "invalid pooled object pointer");
				return;
			}

			const uint16_t cls = header->classIndex;
			if (cls == kFallbackClass) {
#if RME_OBJECT_POOL_STATS
				stats_.fallbackFreeCalls.fetch_add(1, kStatsMemoryOrder);
#endif
				deallocateFallback(header);
				return;
			}

			if (cls >= kClassCount) {
				assert(false && "invalid pooled object size class");
				return;
			}

			auto* node = reinterpret_cast<FreeNode*>(header);
			if (isCurrentThreadOwner()) {
				node->next = localFreeLists_[cls];
				localFreeLists_[cls] = node;
				return;
			}

#if RME_OBJECT_POOL_STATS
			stats_.remoteFreeCalls.fetch_add(1, kStatsMemoryOrder);
#endif
			std::lock_guard<std::mutex> lock(remoteMutex_);
			node->next = remoteFreeLists_[cls];
			remoteFreeLists_[cls] = node;
		}

	private:
		bool becomeOwnerOrIsOwner() {
			if (!ownerSet_.load(std::memory_order_acquire)) {
				std::lock_guard<std::mutex> lock(ownerMutex_);
				if (!ownerSet_.load(std::memory_order_relaxed)) {
					ownerThread_ = std::this_thread::get_id();
					ownerSet_.store(true, std::memory_order_release);
					return true;
				}
			}

			return ownerThread_ == std::this_thread::get_id();
		}

		bool isCurrentThreadOwner() const noexcept {
			return ownerSet_.load(std::memory_order_acquire) && ownerThread_ == std::this_thread::get_id();
		}

		void drainRemoteIntoEmptyLocal(uint16_t cls) {
			assert(localFreeLists_[cls] == nullptr);

			std::lock_guard<std::mutex> lock(remoteMutex_);
#if RME_OBJECT_POOL_STATS
			if (remoteFreeLists_[cls]) {
				stats_.remoteDrainCalls.fetch_add(1, kStatsMemoryOrder);
			}
#endif
			localFreeLists_[cls] = remoteFreeLists_[cls];
			remoteFreeLists_[cls] = nullptr;
		}

		void refill(uint16_t cls) {
#if RME_OBJECT_POOL_STATS
			stats_.slabRefillCalls.fetch_add(1, kStatsMemoryOrder);
			stats_.refillByClass[cls].fetch_add(1, kStatsMemoryOrder);
#endif

			const std::size_t blockSize = kClassSizes[cls];
			const std::size_t blockCount = std::max<std::size_t>(
				kMinBlocksPerSlab,
				kSlabBytes / blockSize
			);
			const std::size_t slabSize = blockCount * blockSize;

			auto* slab = static_cast<unsigned char*>(::operator new(slabSize));
			slabs_.push_back(slab);

			for (std::size_t i = 0; i < blockCount; ++i) {
				auto* node = reinterpret_cast<FreeNode*>(slab + i * blockSize);
				node->next = localFreeLists_[cls];
				localFreeLists_[cls] = node;
			}
		}

		std::atomic<bool> ownerSet_ { false };
		std::mutex ownerMutex_;
		std::thread::id ownerThread_;

		std::array<FreeNode*, kClassCount> localFreeLists_ {};
		std::array<FreeNode*, kClassCount> remoteFreeLists_ {};
		std::mutex remoteMutex_;
		std::vector<void*> slabs_;
#if RME_OBJECT_POOL_STATS
		PoolStats stats_;
#endif
	};

	SmallObjectPool &pooledObjectResource() {
		// Intentionally kept alive for the process lifetime. Map objects can be
		// destroyed from detached threads, so static destruction order is unsafe.
		static auto* pool = new SmallObjectPool();
		return *pool;
	}
}

void* rme::allocatePooledObject(std::size_t size) {
	return pooledObjectResource().allocate(size);
}

void rme::deallocatePooledObject(void* ptr) noexcept {
	pooledObjectResource().deallocate(ptr);
}

void rme::bindPooledObjectOwnerThread() noexcept {
	pooledObjectResource().bindOwnerThread();
}

void rme::resetPooledObjectStats() noexcept {
	pooledObjectResource().resetStats();
}

void rme::dumpPooledObjectStats() noexcept {
	pooledObjectResource().dumpStats();
}
