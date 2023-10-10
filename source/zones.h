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

#ifndef RME_ZONES_H_
#define RME_ZONES_H_

typedef std::map<std::string, unsigned int> ZoneMap;

class Zones {
public:
	Zones(Map &map) :
		map(map) { }
	virtual ~Zones();

	unsigned int getZoneID(std::string name) const {
		auto it = zones.find(name);
		if (it == zones.end()) {
			return 0;
		}
		return it->second;
	}
	bool addZone(std::string name);
	bool addZone(std::string name, unsigned int id);
	bool hasZone(std::string name);
	bool hasZone(unsigned int id);
	void removeZone(std::string name);

	ZoneMap zones;

	ZoneMap::iterator begin() {
		return zones.begin();
	}
	ZoneMap::const_iterator begin() const {
		return zones.begin();
	}
	ZoneMap::iterator end() {
		return zones.end();
	}
	ZoneMap::const_iterator end() const {
		return zones.end();
	}

private:
	Map &map;
	std::unordered_set<unsigned int> used_ids;

	unsigned int generateID();
};

#endif
