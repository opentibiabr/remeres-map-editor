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

constexpr std::size_t kSlabBytes = 256 * 1024;
constexpr std::size_t kMinBlocksPerSlab = 64;

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
	void* allocate(std::size_t payloadSize) {
		const uint16_t cls = classForPayloadSize(payloadSize);
		if (cls == kFallbackClass) {
			return allocateFallback(payloadSize);
		}

		if (!becomeOwnerOrIsOwner()) {
			return allocateFallback(payloadSize);
		}

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
		localFreeLists_[cls] = remoteFreeLists_[cls];
		remoteFreeLists_[cls] = nullptr;
	}

	void refill(uint16_t cls) {
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
