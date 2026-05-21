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

#include <cstddef>
#include <memory_resource>

namespace {
struct alignas(std::max_align_t) PooledAllocationHeader {
	size_t size;
};

std::pmr::synchronized_pool_resource &pooledObjectResource() {
	static auto* resource = new std::pmr::synchronized_pool_resource(
		std::pmr::pool_options {
			4096,
			2048
		},
		std::pmr::new_delete_resource()
	);
	return *resource;
}
}

void* rme::allocatePooledObject(std::size_t size) {
	const std::size_t allocationSize = sizeof(PooledAllocationHeader) + std::max<std::size_t>(size, 1);
	void* raw = pooledObjectResource().allocate(allocationSize, alignof(PooledAllocationHeader));
	auto* header = static_cast<PooledAllocationHeader*>(raw);
	header->size = allocationSize;
	return header + 1;
}

void rme::deallocatePooledObject(void* ptr) noexcept {
	if (!ptr) {
		return;
	}

	auto* header = static_cast<PooledAllocationHeader*>(ptr) - 1;
	pooledObjectResource().deallocate(header, header->size, alignof(PooledAllocationHeader));
}
