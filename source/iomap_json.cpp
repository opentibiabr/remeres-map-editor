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

json IOMapJSON::serializeTile(const Tile &tile) {
	json tileData;

	// Add position information
	const Position &pos = tile.getPosition();
	tileData["x"] = pos.x;
	tileData["y"] = pos.y;
	tileData["z"] = pos.z;

	// Add map flags (PZ, PVP, etc.)
	uint16_t mapFlags = tile.getMapFlags();
	if (mapFlags != 0) {
		json flags;
		if (tile.isPZ()) flags["protection_zone"] = true;
		if (tile.isPVP()) flags["pvp_zone"] = true;
		if (tile.isNoPVP()) flags["no_pvp"] = true;
		if (tile.isNoLogout()) flags["no_logout"] = true;
		if (!flags.empty()) {
			tileData["flags"] = flags;
		}
	}

	// Add house information
	if (tile.isHouseTile()) {
		tileData["house_id"] = tile.getHouseID();
	}

	// Add zones
	if (tile.hasZone()) {
		json zones = json::array();
		for (unsigned int zone : tile.zones) {
			zones.push_back(zone);
		}
		tileData["zones"] = zones;
	}

	// Add ground item
	if (tile.hasGround()) {
		tileData["ground"] = serializeItem(*tile.ground);
	}

	// Add items
	if (!tile.items.empty()) {
		json items = json::array();
		for (const Item* item : tile.items) {
			if (item) {
				items.push_back(serializeItem(*item));
			}
		}
		if (!items.empty()) {
			tileData["items"] = items;
		}
	}

	// Add monster spawns
	if (tile.spawnMonster) {
		json spawn;
		spawn["radius"] = tile.spawnMonster->getSize();
		json monsters = json::array();
		for (const Monster* monster : tile.monsters) {
			if (monster) {
				json monsterData;
				monsterData["name"] = monster->getName();
				// Add more monster properties as needed
				monsters.push_back(monsterData);
			}
		}
		if (!monsters.empty()) {
			spawn["monsters"] = monsters;
		}
		tileData["spawn"] = spawn;
	}

	// Add NPC
	if (tile.npc) {
		json npcData;
		npcData["name"] = tile.npc->getName();
		// Add more NPC properties as needed
		tileData["npc"] = npcData;
	}

	return tileData;
}

json IOMapJSON::serializeItem(const Item &item) {
	json itemData;

	// Basic item properties
	itemData["id"] = item.getID();

	// Add subtype/count if it's not the default
	uint16_t subtype = item.getSubtype();
	if (subtype != 0 && subtype != 0xFFFF) {
		itemData["count"] = subtype;
	}

	// Add unique ID if present
	uint16_t uid = item.getUniqueID();
	if (uid != 0) {
		itemData["unique_id"] = uid;
	}

	// Add action ID if present
	uint16_t aid = item.getActionID();
	if (aid != 0) {
		itemData["action_id"] = aid;
	}

	// Add text if present
	std::string text = item.getText();
	if (!text.empty()) {
		itemData["text"] = text;
	}

	// Add description if present
	std::string desc = item.getDescription();
	if (!desc.empty()) {
		itemData["description"] = desc;
	}

	// Need to cast away const since getter methods are not const
	Item* nonConstItem = const_cast<Item*>(&item);

	// Add container contents if it's a container
	if (nonConstItem->getContainer()) {
		const Container* container = nonConstItem->getContainer();
		json contents = json::array();
		// Need const cast for getVector too since it's not const
		Container* nonConstContainer = const_cast<Container*>(container);
		for (const Item* containerItem : nonConstContainer->getVector()) {
			if (containerItem) {
				contents.push_back(serializeItem(*containerItem));
			}
		}
		if (!contents.empty()) {
			itemData["contents"] = contents;
		}
	}

	// Add depot info if it's a depot
	if (nonConstItem->getDepot()) {
		const Depot* depot = nonConstItem->getDepot();
		json depotData;
		depotData["depot_id"] = depot->getDepotID();
		itemData["depot"] = depotData;
	}

	// Add teleport destination if it's a teleport
	if (nonConstItem->getTeleport()) {
		const Teleport* teleport = nonConstItem->getTeleport();
		const Position& dest = teleport->getDestination();
		json teleportData;
		teleportData["x"] = dest.x;
		teleportData["y"] = dest.y;
		teleportData["z"] = dest.z;
		itemData["teleport"] = teleportData;
	}

	// Add door properties if it's a door
	if (nonConstItem->getDoor()) {
		const Door* door = nonConstItem->getDoor();
		json doorData;
		doorData["door_id"] = door->getDoorID();
		itemData["door"] = doorData;
	}

	return itemData;
}
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
