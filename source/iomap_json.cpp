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
#include "iomap_json.h"
#include "map.h"
#include "tile.h"
#include "item.h"
#include "complexitem.h"
#include "town.h"
#include "house.h"
#include "waypoints.h"
#include "zones.h"
#include "spawn_monster.h"
#include "spawn_npc.h"
#include "monster.h"
#include "npc.h"
#include "gui.h"

#include <fstream>

bool IOMapJSON::loadMap(Map &map, const FileName &identifier) {
	// JSON loading is not implemented in this version
	// This could be added later for round-trip compatibility
	error("Loading JSON maps is not yet supported.");
	return false;
}

bool IOMapJSON::saveMap(Map &map, const FileName &identifier) {
	std::string fullPath = identifier.GetFullPath().mb_str(wxConvUTF8).data();

	try {
		// Create JSON document
		json document;

		// Add version information
		json version;
		version["format"] = "RME_JSON";
		version["version"] = "1.0";
		version["editor"] = "Remere's Map Editor " + __RME_VERSION__;
		document["version"] = version;

		// Add map metadata
		document["map"] = serializeMapData(map);

		// Add tiles
		json tiles = json::array();

		g_gui.SetLoadDone(0, "Exporting map tiles...");

		uint32_t processed_tiles = 0;
		uint32_t total_tiles = map.getTileCount();

		for (auto it = map.begin(); it != map.end(); ++it) {
			Tile *tile = it->get();
			if (tile && !tile->empty()) {
				tiles.push_back(serializeTile(*tile));
			}

			processed_tiles++;
			if (processed_tiles % 1000 == 0) {
				g_gui.SetLoadDone(processed_tiles * 100 / total_tiles, "Exporting map tiles...");
			}
		}
		document["tiles"] = tiles;

		// Add towns
		g_gui.SetLoadDone(80, "Exporting towns...");
		document["towns"] = serializeTowns(map);

		// Add houses
		g_gui.SetLoadDone(85, "Exporting houses...");
		document["houses"] = serializeHouses(map);

		// Add waypoints
		g_gui.SetLoadDone(90, "Exporting waypoints...");
		document["waypoints"] = serializeWaypoints(map);

		// Add zones
		g_gui.SetLoadDone(93, "Exporting zones...");
		document["zones"] = serializeZones(map);

		// Add spawns
		g_gui.SetLoadDone(96, "Exporting monster spawns...");
		document["spawns"] = serializeSpawns(map);

		// Add NPC spawns
		g_gui.SetLoadDone(98, "Exporting NPC spawns...");
		document["npc_spawns"] = serializeNpcSpawns(map);

		// Write to file
		g_gui.SetLoadDone(99, "Writing JSON file...");
		std::ofstream file(fullPath);
		if (!file.is_open()) {
			error("Could not open file for writing: %s", fullPath.c_str());
			return false;
		}

		file << document.dump(2); // Pretty print with 2-space indentation
		file.close();

		g_gui.SetLoadDone(100, "JSON export complete!");
		return true;

	} catch (const std::exception &e) {
		error("Error exporting to JSON: %s", e.what());
		return false;
	}
}

json IOMapJSON::serializeMapData(const Map &map) {
	json mapData;

	mapData["width"] = map.getWidth();
	mapData["height"] = map.getHeight();
	mapData["description"] = map.getMapDescription();

	// Add file references
	json files;
	files["spawn_monster"] = map.getSpawnFilename();
	files["spawn_npc"] = map.getSpawnNpcFilename();
	files["house"] = map.getHouseFilename();
	files["zone"] = map.getZoneFilename();
	mapData["files"] = files;

	// Add version info
	json versionInfo;
	versionInfo["otbm"] = static_cast<int>(map.getVersion().otbm);
	mapData["version"] = versionInfo;

	return mapData;
}

// Simplified implementations for now
json IOMapJSON::serializeTile(const Tile &tile) { return json{}; }
json IOMapJSON::serializeItem(const Item &item) { return json{}; }
json IOMapJSON::serializeTowns(const Map &map) { return json::array(); }
json IOMapJSON::serializeHouses(const Map &map) { return json::array(); }
json IOMapJSON::serializeWaypoints(const Map &map) { return json::array(); }
json IOMapJSON::serializeZones(const Map &map) { return json::array(); }
json IOMapJSON::serializeSpawns(const Map &map) { return json::array(); }
json IOMapJSON::serializeNpcSpawns(const Map &map) { return json::array(); }

// Stub implementations for deserialization
bool IOMapJSON::deserializeMapData(Map &map, const json &jsonData) { return false; }
bool IOMapJSON::deserializeTile(Map &map, const json &jsonData, const Position &pos) { return false; }
Item* IOMapJSON::deserializeItem(const json &jsonData) { return nullptr; }
bool IOMapJSON::deserializeTowns(Map &map, const json &jsonData) { return false; }
bool IOMapJSON::deserializeHouses(Map &map, const json &jsonData) { return false; }
bool IOMapJSON::deserializeWaypoints(Map &map, const json &jsonData) { return false; }
bool IOMapJSON::deserializeZones(Map &map, const json &jsonData) { return false; }
bool IOMapJSON::deserializeSpawns(Map &map, const json &jsonData) { return false; }
bool IOMapJSON::deserializeNpcSpawns(Map &map, const json &jsonData) { return false; }
