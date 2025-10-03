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

#ifndef RME_JSON_MAP_IO_H_
#define RME_JSON_MAP_IO_H_

#include "iomap.h"
#include <nlohmann/json.hpp>

using json = nlohmann::ordered_json;

class IOMapJSON : public IOMap {
public:
	IOMapJSON() = default;
	virtual ~IOMapJSON() = default;

	virtual bool loadMap(Map &map, const FileName &identifier) override;
	virtual bool saveMap(Map &map, const FileName &identifier) override;

private:
	// JSON serialization functions
	json serializeMapData(const Map &map);
	json serializeTile(const Tile &tile);
	json serializeItem(const Item &item);
	json serializeTowns(const Map &map);
	json serializeHouses(const Map &map);
	json serializeWaypoints(const Map &map);
	json serializeZones(const Map &map);
	json serializeSpawns(const Map &map);
	json serializeNpcSpawns(const Map &map);

	// JSON deserialization functions (for future loading support)
	bool deserializeMapData(Map &map, const json &jsonData);
	bool deserializeTile(Map &map, const json &jsonData, const Position &pos);
	Item* deserializeItem(const json &jsonData);
	bool deserializeTowns(Map &map, const json &jsonData);
	bool deserializeHouses(Map &map, const json &jsonData);
	bool deserializeWaypoints(Map &map, const json &jsonData);
	bool deserializeZones(Map &map, const json &jsonData);
	bool deserializeSpawns(Map &map, const json &jsonData);
	bool deserializeNpcSpawns(Map &map, const json &jsonData);
};

#endif // RME_JSON_MAP_IO_H_
