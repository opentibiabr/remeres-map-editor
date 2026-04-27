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

#ifndef RME_UTIL_OUTFIT_H_
#define RME_UTIL_OUTFIT_H_

#include <string>
#include <cstdint>

struct Outfit {
	Outfit() :
		lookType(0), lookItem(0), lookMount(0), lookAddon(0), lookHead(0), lookBody(0), lookLegs(0), lookFeet(0) { }
	~Outfit() = default;
	Outfit(const Outfit &) = default;
	Outfit &operator=(const Outfit &) = default;
	Outfit(Outfit &&) noexcept = default;
	Outfit &operator=(Outfit &&) noexcept = default;

	std::string name;
	int lookType;
	int lookItem;
	int lookMount;
	int lookAddon;
	int lookHead;
	int lookBody;
	int lookLegs;
	int lookFeet;

	uint32_t getColorHash() const {
		return static_cast<uint32_t>(lookHead) << 24 | static_cast<uint32_t>(lookBody) << 16 | static_cast<uint32_t>(lookLegs) << 8 | static_cast<uint32_t>(lookFeet);
	}
};

#endif
