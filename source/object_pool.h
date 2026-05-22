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

#ifndef RME_OBJECT_POOL_H_
#define RME_OBJECT_POOL_H_

#include <cstddef>

namespace rme {
	void* allocatePooledObject(std::size_t size); // NOSONAR - pooled operator new API.
	void deallocatePooledObject(void* ptr) noexcept; // NOSONAR - pooled operator delete API.
	void bindPooledObjectOwnerThread() noexcept;
	void resetPooledObjectStats() noexcept;
	void dumpPooledObjectStats() noexcept;
}

#endif
