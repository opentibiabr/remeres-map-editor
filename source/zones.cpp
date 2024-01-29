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

#include "zones.h"
#include "map.h"

Zones::~Zones() {
	zones.clear();
}

bool Zones::addZone(std::string name, unsigned int id) {
	if (hasZone(name)) {
		return false;
	}
	if (used_ids.find(id) != used_ids.end()) {
		return false;
	}
	zones.emplace(name, id);
	used_ids.insert(id);
	return true;
}

bool Zones::addZone(std::string name) {
	return addZone(name, generateID());
}

bool Zones::hasZone(std::string name) {
	to_lower_str(name);
	return zones.find(name) != zones.end();
}

bool Zones::hasZone(unsigned int id) {
	return used_ids.find(id) != used_ids.end();
}

void Zones::removeZone(std::string name) {
	to_lower_str(name);
	if (!hasZone(name)) {
		return;
	}
	used_ids.erase(zones[name]);
	zones.erase(name);
}

unsigned int Zones::generateID() {
	unsigned int id = 1;
	while (used_ids.find(id) != used_ids.end()) {
		id++;
	}
	return id;
}
