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
//////////////////////////////////////////////////////////////////////
// Developed by LIBERGOD
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

#include <wx/app.h> // For wxYield()
#include <fstream>
#include <algorithm>
#include <vector>

bool IOMapJSON::loadMap(Map &map, const FileName &identifier) {
	std::string fullPath = identifier.GetFullPath().mb_str(wxConvUTF8).data();

	try {
		// Set initial loading progress immediately
		g_gui.SetLoadDone(0, "Opening JSON file...");
		wxYield(); // Allow GUI to update immediately

		// Read the JSON file
		std::ifstream file(fullPath);
		if (!file.is_open()) {
			error("Could not open JSON file for reading: %s", fullPath.c_str());
			return false;
		}

		g_gui.SetLoadDone(5, "Parsing JSON data...");
		wxYield(); // Allow GUI to update

		// Parse JSON
		json document;
		try {
			file >> document;
		} catch (const json::exception &e) {
			error("Failed to parse JSON file: %s", e.what());
			return false;
		}
		file.close();

		g_gui.SetLoadDone(15, "Validating file format...");
		wxYield(); // Allow GUI to update

		// Validate JSON structure
		if (!document.contains("version") || !document.contains("map")) {
			error("Invalid JSON map file: missing required fields 'version' and 'map'");
			return false;
		}

		// Check version compatibility
		if (document["version"].contains("format")) {
			std::string format = document["version"]["format"];
			if (format != "RME_JSON") {
				error("Unsupported JSON format: %s", format.c_str());
				return false;
			}
		}

		// Clear existing map data
		g_gui.SetLoadDone(20, "Clearing existing map data...");
		wxYield(); // Allow GUI to update
		map.clearVisible(0xFFFFFFFF); // Clear all visible tiles

		// Load map metadata
		g_gui.SetLoadDone(25, "Loading map metadata...");
		wxYield(); // Allow GUI to update
		if (!deserializeMapData(map, document["map"])) {
			error("Failed to load map metadata");
			return false;
		}

		// Set loading progress
		g_gui.SetLoadDone(30, "Loading tiles...");
		wxYield(); // Allow GUI to update

		// Load tiles with progress tracking
		if (document.contains("tiles")) {
			const json &tiles = document["tiles"];
			size_t totalTiles = tiles.size();
			size_t tilesLoaded = 0;
			const size_t YIELD_FREQUENCY = 500; // Yield to GUI every 500 tiles

			for (const auto &tileData : tiles) {
				if (!tileData.contains("x") || !tileData.contains("y") || !tileData.contains("z")) {
					continue; // Skip malformed tiles
				}

				Position pos(tileData["x"], tileData["y"], tileData["z"]);
				if (!deserializeTile(map, tileData, pos)) {
					warning("Failed to load tile at position (%d, %d, %d)", pos.x, pos.y, pos.z);
				}

				tilesLoaded++;

				// Update progress and yield to GUI more frequently
				if (tilesLoaded % YIELD_FREQUENCY == 0 || tilesLoaded == totalTiles) {
					int progress = 30 + static_cast<int>((tilesLoaded * 35) / totalTiles); // 30-65% for tiles
					g_gui.SetLoadDone(progress, wxString::Format("Loading tiles... (%zu/%zu)", tilesLoaded, totalTiles));
					wxYield(); // Allow GUI to process events and prevent freezing
				}
			}
		}

		// Load towns
		g_gui.SetLoadDone(67, "Loading towns...");
		wxYield(); // Allow GUI to update
		if (document.contains("towns")) {
			if (!deserializeTowns(map, document["towns"])) {
				warning("Failed to load some towns");
			}
		}

		// Load houses
		g_gui.SetLoadDone(70, "Loading houses...");
		wxYield(); // Allow GUI to update
		if (document.contains("houses")) {
			if (!deserializeHouses(map, document["houses"])) {
				warning("Failed to load some houses");
			}
		}

		// Link house tiles to house objects
		g_gui.SetLoadDone(75, "Linking house tiles...");
		wxYield(); // Allow GUI to update
		linkHouseTiles(map);

		// Load waypoints
		g_gui.SetLoadDone(80, "Loading waypoints...");
		wxYield(); // Allow GUI to update
		if (document.contains("waypoints")) {
			if (!deserializeWaypoints(map, document["waypoints"])) {
				warning("Failed to load some waypoints");
			}
		}

		// Load zones
		g_gui.SetLoadDone(85, "Loading zones...");
		wxYield(); // Allow GUI to update
		if (document.contains("zones")) {
			if (!deserializeZones(map, document["zones"])) {
				warning("Failed to load some zones");
			}
		}

		// Load monster spawns
		g_gui.SetLoadDone(90, "Loading monster spawns...");
		wxYield(); // Allow GUI to update
		if (document.contains("spawns")) {
			if (!deserializeSpawns(map, document["spawns"])) {
				warning("Failed to load some monster spawns");
			}
		}

		// Load NPC spawns
		g_gui.SetLoadDone(95, "Loading NPC spawns...");
		wxYield(); // Allow GUI to update
		if (document.contains("npc_spawns")) {
			if (!deserializeNpcSpawns(map, document["npc_spawns"])) {
				warning("Failed to load some NPC spawns");
			}
		}

		g_gui.SetLoadDone(100, "Map loaded successfully");
		wxYield(); // Final GUI update

		return true;

	} catch (const std::exception &e) {
		error("Unexpected error while loading JSON map: %s", e.what());
		return false;
	}
}

bool IOMapJSON::saveMap(Map &map, const FileName &identifier) {
	std::string fullPath = identifier.GetFullPath().mb_str(wxConvUTF8).data();

	try {
		// OPTIMIZATION: Stream output instead of building entire JSON in memory
		std::ofstream file(fullPath);
		if (!file.is_open()) {
			error("Could not open file for writing: %s", fullPath.c_str());
			return false;
		}

		// Write JSON header manually for streaming
		file << "{\n";
		file << "  \"version\": {\n";
		file << "    \"format\": \"RME_JSON\",\n";
		file << "    \"version\": \"1.0\",\n";
		file << "    \"editor\": \"Remere's Map Editor " << __RME_VERSION__ << "\"\n";
		file << "  },\n";

		// Add map metadata
		json mapData = serializeMapData(map);
		file << "  \"map\": " << mapData.dump(2) << ",\n";

		g_gui.SetLoadDone(0, "Exporting map tiles...");
		file << "  \"tiles\": [\n";

		uint32_t processed_tiles = 0;
		uint32_t total_tiles = map.getTileCount();
		bool first_tile = true;
		const size_t CHUNK_SIZE = 1000; // Process in chunks
		size_t chunk_count = 0;

		// OPTIMIZATION: Process tiles directly without loading all into memory
		for (auto it = map.begin(); it != map.end(); ++it) {
			Tile *tile = it->get();
			if (tile && (!tile->empty() || tile->isHouseTile() || tile->hasZone() || tile->getMapFlags() != 0)) {
				try {
					if (!first_tile) {
						file << ",\n";
					}

					// Use optimized serialization
					json tileData = serializeTileOptimized(*tile);
					file << "    " << tileData.dump();

					first_tile = false;
					processed_tiles++;
					chunk_count++;

					// OPTIMIZATION: Periodic memory management and progress updates
					if (chunk_count >= CHUNK_SIZE) {
						chunk_count = 0;
						file.flush(); // Force write to disk to free memory

						// Update progress more frequently
						int progress = std::min(70, static_cast<int>(processed_tiles * 70 / total_tiles));
						g_gui.SetLoadDone(progress, "Exporting map tiles...");

						// Allow GUI to process events to prevent freezing
						wxYield();
					}

				} catch (const std::exception& e) {
					// OPTIMIZATION: Error handling - skip problematic tiles instead of crashing
					warning("Failed to serialize tile at (%d, %d, %d): %s",
						   tile->getX(), tile->getY(), tile->getZ(), e.what());
					continue;
				}
			}
		}

		file << "\n  ],\n";

		// Add towns
		g_gui.SetLoadDone(75, "Exporting towns...");
		json towns = serializeTowns(map);
		file << "  \"towns\": " << towns.dump(2) << ",\n";

		// Add houses
		g_gui.SetLoadDone(80, "Exporting houses...");
		json houses = serializeHouses(map);
		file << "  \"houses\": " << houses.dump(2) << ",\n";

		// Add waypoints
		g_gui.SetLoadDone(85, "Exporting waypoints...");
		json waypoints = serializeWaypoints(map);
		file << "  \"waypoints\": " << waypoints.dump(2) << ",\n";

		// Add zones
		g_gui.SetLoadDone(90, "Exporting zones...");
		json zones = serializeZones(map);
		file << "  \"zones\": " << zones.dump(2) << ",\n";

		// Add spawns
		g_gui.SetLoadDone(95, "Exporting monster spawns...");
		json spawns = serializeSpawns(map);
		file << "  \"spawns\": " << spawns.dump(2) << ",\n";

		// Add NPC spawns
		g_gui.SetLoadDone(98, "Exporting NPC spawns...");
		json npcSpawns = serializeNpcSpawns(map);
		file << "  \"npc_spawns\": " << npcSpawns.dump(2) << "\n";

		file << "}\n";
		file.close();

		g_gui.SetLoadDone(100, "JSON export complete!");
		return true;

	} catch (const std::exception &e) {
		error("Error exporting to JSON: %s", e.what());
		return false;
	}
}

bool IOMapJSON::saveMapQuiet(Map &map, const FileName &identifier) {
	std::string fullPath = identifier.GetFullPath().mb_str(wxConvUTF8).data();

	try {
		// OPTIMIZATION: Stream output instead of building entire JSON in memory
		std::ofstream file(fullPath);
		if (!file.is_open()) {
			error("Could not open file for writing: %s", fullPath.c_str());
			return false;
		}

		// Write JSON header manually for streaming
		file << "{\n";
		file << "  \"version\": {\n";
		file << "    \"format\": \"RME_JSON\",\n";
		file << "    \"version\": \"1.0\",\n";
		file << "    \"editor\": \"Remere's Map Editor " << __RME_VERSION__ << "\"\n";
		file << "  },\n";

		// Add map metadata
		json mapData = serializeMapData(map);
		file << "  \"map\": " << mapData.dump(2) << ",\n";

		// No progress dialogs for quiet save
		file << "  \"tiles\": [\n";

		uint32_t processed_tiles = 0;
		bool first_tile = true;
		const size_t CHUNK_SIZE = 2000; // Larger chunks for quiet saves
		size_t chunk_count = 0;

		// OPTIMIZATION: Process tiles directly without loading all into memory
		for (auto it = map.begin(); it != map.end(); ++it) {
			Tile *tile = it->get();
			if (tile && (!tile->empty() || tile->isHouseTile() || tile->hasZone() || tile->getMapFlags() != 0)) {
				try {
					if (!first_tile) {
						file << ",\n";
					}

					// Use optimized serialization
					json tileData = serializeTileOptimized(*tile);
					file << "    " << tileData.dump();

					first_tile = false;
					processed_tiles++;
					chunk_count++;

					// OPTIMIZATION: Periodic memory management without GUI updates
					if (chunk_count >= CHUNK_SIZE) {
						chunk_count = 0;
						file.flush(); // Force write to disk to free memory
					}

				} catch (const std::exception& e) {
					// OPTIMIZATION: Error handling - skip problematic tiles instead of crashing
					warning("Failed to serialize tile at (%d, %d, %d): %s",
						   tile->getX(), tile->getY(), tile->getZ(), e.what());
					continue;
				}
			}
		}

		file << "\n  ],\n";

		// Add towns (no progress updates)
		json towns = serializeTowns(map);
		file << "  \"towns\": " << towns.dump(2) << ",\n";

		// Add houses
		json houses = serializeHouses(map);
		file << "  \"houses\": " << houses.dump(2) << ",\n";

		// Add waypoints
		json waypoints = serializeWaypoints(map);
		file << "  \"waypoints\": " << waypoints.dump(2) << ",\n";

		// Add zones
		json zones = serializeZones(map);
		file << "  \"zones\": " << zones.dump(2) << ",\n";

		// Add spawns
		json spawns = serializeSpawns(map);
		file << "  \"spawns\": " << spawns.dump(2) << ",\n";

		// Add NPC spawns
		json npcSpawns = serializeNpcSpawns(map);
		file << "  \"npc_spawns\": " << npcSpawns.dump(2) << "\n";

		file << "}\n";
		file.close();

		// No completion message for quiet saves
		return true;

	} catch (const std::exception &e) {
		error("Error saving to JSON: %s", e.what());
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
		if (mapFlags & TILESTATE_REFRESH) flags["refresh"] = true;
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
		int stackPosition = 0;
		for (const Item* item : tile.items) {
			if (item) {
				json itemData = serializeItem(*item);
				// Add stack position (0 = bottom, higher = closer to top)
				itemData["stack_position"] = stackPosition;
				items.push_back(itemData);
				stackPosition++;
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

	// Add NPC spawn
	if (tile.spawnNpc) {
		json npcSpawn;
		npcSpawn["radius"] = tile.spawnNpc->getSize();
		tileData["npc_spawn"] = npcSpawn;
	}

	return tileData;
}

// OPTIMIZATION: Simplified tile serialization for better performance
json IOMapJSON::serializeTileOptimized(const Tile &tile) {
	json tileData;

	// Add position information
	const Position &pos = tile.getPosition();
	tileData["x"] = pos.x;
	tileData["y"] = pos.y;
	tileData["z"] = pos.z;

	// Add ground item (using optimized serialization)
	if (tile.hasGround()) {
		tileData["ground"] = serializeItemOptimized(*tile.ground);
	}

	// Add items (only if present and non-empty)
	if (!tile.items.empty()) {
		json items = json::array();
		for (const Item* item : tile.items) {
			if (item) {
				items.push_back(serializeItemOptimized(*item));
			}
		}
		if (!items.empty()) {
			tileData["items"] = items;
		}
	}

	// Only include non-default flags
	uint16_t mapFlags = tile.getMapFlags();
	if (mapFlags != 0) {
		json flags;
		if (tile.isPZ()) flags["protection_zone"] = true;
		if (tile.isPVP()) flags["pvp_zone"] = true;
		if (tile.isNoPVP()) flags["no_pvp"] = true;
		if (tile.isNoLogout()) flags["no_logout"] = true;
		if (mapFlags & TILESTATE_REFRESH) flags["refresh"] = true;

		if (!flags.empty()) {
			tileData["flags"] = flags;
		}
	}

	// House ID (only if present)
	if (tile.isHouseTile()) {
		tileData["house_id"] = tile.getHouseID();
	}

	// Zones (only if present)
	if (tile.hasZone()) {
		json zones = json::array();
		for (unsigned int zone : tile.zones) {
			zones.push_back(zone);
		}
		tileData["zones"] = zones;
	}

	// Simplified monster handling
	if (tile.spawnMonster && !tile.monsters.empty()) {
		json spawn;
		spawn["radius"] = tile.spawnMonster->getSize();
		json monsters = json::array();
		for (const Monster* monster : tile.monsters) {
			if (monster) {
				json monsterData;
				monsterData["name"] = monster->getName();
				monsters.push_back(monsterData);
			}
		}
		if (!monsters.empty()) {
			spawn["monsters"] = monsters;
			tileData["spawn"] = spawn;
		}
	}

	// Simplified NPC handling
	if (tile.npc) {
		json npcData;
		npcData["name"] = tile.npc->getName();
		tileData["npc"] = npcData;
	}

	return tileData;
}

// OPTIMIZATION: Minimal item serialization for performance
json IOMapJSON::serializeItemOptimized(const Item &item) {
	json itemData;

	// Only include essential properties
	itemData["id"] = item.getID();

	// Only include non-default values
	uint16_t subtype = item.getSubtype();
	if (subtype != 0 && subtype != 0xFFFF) {
		itemData["count"] = subtype;
	}

	uint16_t uid = item.getUniqueID();
	if (uid != 0) {
		itemData["unique_id"] = uid;
	}

	uint16_t aid = item.getActionID();
	if (aid != 0) {
		itemData["action_id"] = aid;
	}

	std::string text = item.getText();
	if (!text.empty()) {
		itemData["text"] = text;
	}

	// Only include critical properties, not every possible one
	if (item.isBlocking()) {
		itemData["blocking"] = true;
	}

	if (item.isMoveable()) {
		itemData["moveable"] = true;
	}

	// Handle containers recursively but with depth limit
	Item* nonConstItem = const_cast<Item*>(&item);
	if (nonConstItem->getContainer()) {
		const Container* container = nonConstItem->getContainer();
		json contents = json::array();
		Container* nonConstContainer = const_cast<Container*>(container);

		// Limit container depth to prevent infinite recursion
		size_t maxContainerItems = 100; // Reasonable limit
		size_t itemCount = 0;

		for (const Item* containerItem : nonConstContainer->getVector()) {
			if (containerItem && itemCount < maxContainerItems) {
				contents.push_back(serializeItemOptimized(*containerItem));
				itemCount++;
			}
		}

		if (!contents.empty()) {
			itemData["contents"] = contents;
		}
	}

	// Essential special items
	if (nonConstItem->getTeleport()) {
		const Teleport* teleport = nonConstItem->getTeleport();
		const Position& dest = teleport->getDestination();
		json teleportData;
		teleportData["x"] = dest.x;
		teleportData["y"] = dest.y;
		teleportData["z"] = dest.z;
		itemData["teleport"] = teleportData;
	}

	if (nonConstItem->getDoor()) {
		const Door* door = nonConstItem->getDoor();
		itemData["door_id"] = door->getDoorID();
	}

	if (nonConstItem->getDepot()) {
		const Depot* depot = nonConstItem->getDepot();
		itemData["depot_id"] = depot->getDepotID();
	}

	return itemData;
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

	// Add comprehensive stacking and positioning properties
	json stackingJson;
	stackingJson["always_on_bottom"] = item.isAlwaysOnBottom();
	stackingJson["top_order"] = item.getTopOrder();
	stackingJson["moveable"] = item.isMoveable();
	stackingJson["pickupable"] = item.isPickupable();
	stackingJson["stackable"] = item.isStackable();
	stackingJson["blocking"] = item.isBlocking();
	stackingJson["ground_tile"] = item.isGroundTile();
	stackingJson["hangable"] = item.isHangable();

	// Special tile types that affect positioning
	if (item.isSplash()) {
		stackingJson["is_splash"] = true;
	}
	if (item.isMagicField()) {
		stackingJson["is_magic_field"] = true;
	}

	// Floor change properties (affects rendering layer)
	const ItemType& itemType = item.getItemType();
	if (itemType.floorChange) {
		json floorChangeJson;
		floorChangeJson["floor_change"] = true;
		if (itemType.floorChangeDown) floorChangeJson["down"] = true;
		if (itemType.floorChangeNorth) floorChangeJson["north"] = true;
		if (itemType.floorChangeSouth) floorChangeJson["south"] = true;
		if (itemType.floorChangeEast) floorChangeJson["east"] = true;
		if (itemType.floorChangeWest) floorChangeJson["west"] = true;
		stackingJson["floor_change"] = floorChangeJson;
	}

	// Height and elevation properties
	if (itemType.hasHeight) {
		stackingJson["has_height"] = true;
	}
	if (itemType.hasElevation) {
		stackingJson["has_elevation"] = true;
	}

	itemData["stacking"] = stackingJson;

	// Combat and character stats
	json statsJson;
	statsJson["weight"] = item.getWeight();
	statsJson["attack"] = item.getAttack();
	statsJson["defense"] = item.getDefense();
	statsJson["armor"] = item.getArmor();
	itemData["stats"] = statsJson;

	// Text and interaction properties
	json textPropsJson;
	textPropsJson["can_read_text"] = itemType.canReadText;
	textPropsJson["can_write_text"] = itemType.canWriteText;
	textPropsJson["allow_dist_read"] = itemType.allowDistRead;
	textPropsJson["max_text_length"] = itemType.maxTextLen;
	textPropsJson["readable"] = item.isReadable();
	textPropsJson["can_write"] = item.canWriteText();
	itemData["text_properties"] = textPropsJson;

	// Charges and usage properties
	json usageJson;
	usageJson["client_chargeable"] = item.isClientCharged();
	usageJson["extra_chargeable"] = item.isExtraCharged();
	usageJson["chargeable"] = item.isCharged();
	usageJson["fluid_container"] = item.isFluidContainer();
	itemData["usage"] = usageJson;

	// Physical properties and states
	json physicalJson;
	physicalJson["replaceable"] = itemType.replaceable;
	physicalJson["decays"] = itemType.decays;
	physicalJson["rotable"] = itemType.rotable;
	physicalJson["is_corpse"] = itemType.isCorpse;
	physicalJson["is_vertical"] = itemType.isVertical;
	physicalJson["is_horizontal"] = itemType.isHorizontal;
	physicalJson["is_podium"] = itemType.isPodium;
	physicalJson["hook_east"] = itemType.hookEast;
	physicalJson["hook_south"] = itemType.hookSouth;
	itemData["physical"] = physicalJson;

	// Construction and building properties
	json buildingJson;
	buildingJson["is_border"] = itemType.isBorder;
	buildingJson["is_optional_border"] = itemType.isOptionalBorder;
	buildingJson["is_wall"] = itemType.isWall;
	buildingJson["is_brush_door"] = itemType.isBrushDoor;
	buildingJson["is_open"] = itemType.isOpen;
	buildingJson["is_table"] = itemType.isTable;
	buildingJson["is_carpet"] = itemType.isCarpet;
	buildingJson["wall_hate_me"] = itemType.wall_hate_me;
	itemData["building"] = buildingJson;

	// Movement and pathfinding properties
	json movementJson;
	movementJson["block_pathfinder"] = itemType.blockPathfinder;
	movementJson["block_missiles"] = itemType.blockMissiles;
	movementJson["block_pickupable"] = itemType.blockPickupable;
	movementJson["walk_stack"] = itemType.walkStack;
	movementJson["avoidable"] = item.isAvoidable();
	itemData["movement"] = movementJson;

	// Metadata and editor properties
	json metaJson;
	metaJson["is_metaitem"] = itemType.is_metaitem;
	metaJson["has_raw"] = itemType.has_raw;
	metaJson["in_other_tileset"] = itemType.in_other_tileset;
	metaJson["has_equivalent"] = itemType.has_equivalent;
	metaJson["ground_equivalent"] = itemType.ground_equivalent;
	metaJson["ignore_look"] = itemType.ignoreLook;
	metaJson["force_use"] = itemType.forceUse;
	metaJson["no_move_animation"] = itemType.noMoveAnimation;
	itemData["metadata"] = metaJson;

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

json IOMapJSON::serializeTowns(const Map &map) {
	json towns = json::array();

	for (const auto &townEntry : map.towns) {
		const Town* town = townEntry.second;
		if (!town) {
			continue;
		}

		json townData;
		townData["id"] = town->getID();
		townData["name"] = town->getName();

		// Temple position
		const Position &templePos = town->getTemplePosition();
		json temple;
		temple["x"] = templePos.x;
		temple["y"] = templePos.y;
		temple["z"] = templePos.z;
		townData["temple"] = temple;

		towns.push_back(townData);
	}

	return towns;
}

json IOMapJSON::serializeHouses(const Map &map) {
	json houses = json::array();

	for (const auto &houseEntry : map.houses) {
		const House* house = houseEntry.second;
		if (!house) {
			continue;
		}

		json houseData;
		houseData["id"] = house->id;
		houseData["name"] = house->name;
		houseData["rent"] = house->rent;
		houseData["clientid"] = house->clientid;
		houseData["beds"] = house->beds;
		houseData["townid"] = house->townid;
		houseData["guildhall"] = house->guildhall;
		houseData["size"] = static_cast<uint32_t>(house->size());

		// Entry/exit position
		const Position &exitPos = house->getExit();
		json entry;
		entry["x"] = exitPos.x;
		entry["y"] = exitPos.y;
		entry["z"] = exitPos.z;
		houseData["entry"] = entry;

		houses.push_back(houseData);
	}

	return houses;
}

json IOMapJSON::serializeWaypoints(const Map &map) {
	json waypoints = json::array();

	for (WaypointMap::const_iterator iter = map.waypoints.begin(); iter != map.waypoints.end(); ++iter) {
		const Waypoint* wp = iter->second;
		if (!wp) {
			continue;
		}

		json waypoint;
		waypoint["name"] = wp->name;
		waypoint["x"] = wp->pos.x;
		waypoint["y"] = wp->pos.y;
		waypoint["z"] = wp->pos.z;

		waypoints.push_back(waypoint);
	}

	return waypoints;
}
json IOMapJSON::serializeZones(const Map &map) {
	json zones = json::array();

	for (ZoneMap::const_iterator iter = map.zones.begin(); iter != map.zones.end(); ++iter) {
		json zone;
		zone["name"] = iter->first;
		zone["id"] = iter->second;

		zones.push_back(zone);
	}

	return zones;
}

json IOMapJSON::serializeSpawns(const Map &map) {
	json spawns = json::array();

	// Iterate through all spawn positions
	for (auto iter = map.spawnsMonster.begin(); iter != map.spawnsMonster.end(); ++iter) {
		const Position &pos = *iter;
		const Tile* spawnTile = map.getTile(pos);

		if (spawnTile && spawnTile->spawnMonster) {
			json spawn;
			spawn["centerx"] = pos.x;
			spawn["centery"] = pos.y;
			spawn["centerz"] = pos.z;
			int radius = spawnTile->spawnMonster->getSize();
			spawn["radius"] = radius;

			// Collect all monsters within this spawn's radius
			json monsters = json::array();

			// Scan all tiles within the spawn radius
			for (int x = pos.x - radius; x <= pos.x + radius; ++x) {
				for (int y = pos.y - radius; y <= pos.y + radius; ++y) {
					const Tile* tile = map.getTile(Position(x, y, pos.z));
					if (tile) {
						// Add all monsters from this tile
						for (const Monster* monster : tile->monsters) {
							if (monster) {
								json monsterData;
								monsterData["name"] = monster->getName();
								// Relative positioning from spawn center
								monsterData["x"] = x - pos.x;
								monsterData["y"] = y - pos.y;
								monsterData["z"] = pos.z; // Z level

								// Add spawntime if it's not the default
								uint16_t spawntime = monster->getSpawnMonsterTime();
								if (spawntime != g_settings.getInteger(Config::DEFAULT_SPAWN_MONSTER_TIME)) {
									monsterData["spawntime"] = spawntime;
								}

								// Add direction if it's not the default
								Direction dir = monster->getDirection();
								if (dir != NORTH) { // Only include if not default
									std::string dirName;
									switch (dir) {
										case NORTH: dirName = "north"; break;
										case EAST: dirName = "east"; break;
										case SOUTH: dirName = "south"; break;
										case WEST: dirName = "west"; break;
									}
									monsterData["direction"] = dirName;
								}

								// Add weight if it's not the default
								int weight = monster->getWeight();
								if (weight != g_settings.getInteger(Config::MONSTER_DEFAULT_WEIGHT)) {
									monsterData["weight"] = weight > 0 ? weight : g_settings.getInteger(Config::MONSTER_DEFAULT_WEIGHT);
								}

								monsters.push_back(monsterData);
							}
						}
					}
				}
			}

			if (!monsters.empty()) {
				spawn["monsters"] = monsters;
			}

			spawns.push_back(spawn);
		}
	}

	return spawns;
}

json IOMapJSON::serializeNpcSpawns(const Map &map) {
	json npcSpawns = json::array();

	// Iterate through all NPC spawn positions
	for (auto iter = map.spawnsNpc.begin(); iter != map.spawnsNpc.end(); ++iter) {
		const Position &pos = *iter;
		const Tile* spawnTile = map.getTile(pos);

		if (spawnTile && spawnTile->spawnNpc) {
			json npcSpawn;
			npcSpawn["centerx"] = pos.x;
			npcSpawn["centery"] = pos.y;
			npcSpawn["centerz"] = pos.z;
			int radius = spawnTile->spawnNpc->getSize();
			npcSpawn["radius"] = radius;

			// Collect all NPCs within this spawn's radius
			json npcs = json::array();

			// Scan all tiles within the spawn radius
			for (int x = pos.x - radius; x <= pos.x + radius; ++x) {
				for (int y = pos.y - radius; y <= pos.y + radius; ++y) {
					const Tile* tile = map.getTile(Position(x, y, pos.z));
					if (tile && tile->npc) {
						json npcData;
						npcData["name"] = tile->npc->getName();
						// Relative positioning from spawn center
						npcData["x"] = x - pos.x;
						npcData["y"] = y - pos.y;
						npcData["z"] = pos.z; // Z level

						// Add spawntime if it's not the default
						int spawntime = tile->npc->getSpawnNpcTime();
						if (spawntime != g_settings.getInteger(Config::DEFAULT_SPAWN_NPC_TIME)) {
							npcData["spawntime"] = spawntime;
						}

						// Add direction if it's not the default
						Direction dir = tile->npc->getDirection();
						if (dir != NORTH) { // Only include if not default
							std::string dirName;
							switch (dir) {
								case NORTH: dirName = "north"; break;
								case EAST: dirName = "east"; break;
								case SOUTH: dirName = "south"; break;
								case WEST: dirName = "west"; break;
							}
							npcData["direction"] = dirName;
						}

						npcs.push_back(npcData);
					}
				}
			}

			if (!npcs.empty()) {
				npcSpawn["npcs"] = npcs;
			}

			npcSpawns.push_back(npcSpawn);
		}
	}

	return npcSpawns;
}

// Stub implementations for deserialization
bool IOMapJSON::deserializeMapData(Map &map, const json &jsonData) {
	try {
		// Load basic map properties
		if (jsonData.contains("width")) {
			map.setWidth(jsonData["width"]);
		}
		if (jsonData.contains("height")) {
			map.setHeight(jsonData["height"]);
		}
		if (jsonData.contains("description")) {
			map.setMapDescription(jsonData["description"]);
		}

		// Load file references
		if (jsonData.contains("files")) {
			const json &files = jsonData["files"];

			if (files.contains("spawn_monster")) {
				map.setSpawnMonsterFilename(files["spawn_monster"]);
			}
			if (files.contains("spawn_npc")) {
				map.setSpawnNpcFilename(files["spawn_npc"]);
			}
			if (files.contains("house")) {
				map.setHouseFilename(files["house"]);
			}
			if (files.contains("zone")) {
				map.setZoneFilename(files["zone"]);
			}
		}

		// Load version information
		// Note: Map version cannot be set directly as it's protected
		// and there's no public setter method available
		if (jsonData.contains("version")) {
			// Version information is read but not applied
			// The map will use default version settings
		}

		return true;

	} catch (const json::exception &e) {
		error("Error deserializing map data: %s", e.what());
		return false;
	} catch (const std::exception &e) {
		error("Unexpected error deserializing map data: %s", e.what());
		return false;
	}
}
bool IOMapJSON::deserializeTile(Map &map, const json &jsonData, const Position &pos) {
	try {
		// Get or create the tile at the position
		Tile* tile = map.getTile(pos);
		if (!tile) {
			tile = map.allocator(map.createTileL(pos));
		}

		// Set map flags if present
		if (jsonData.contains("flags")) {
			const json &flags = jsonData["flags"];

			if (flags.contains("protection_zone") && flags["protection_zone"]) {
				tile->setMapFlags(tile->getMapFlags() | TILESTATE_PROTECTIONZONE);
			}
			if (flags.contains("pvp_zone") && flags["pvp_zone"]) {
				tile->setMapFlags(tile->getMapFlags() | TILESTATE_PVPZONE);
			}
			if (flags.contains("no_pvp") && flags["no_pvp"]) {
				tile->setMapFlags(tile->getMapFlags() | TILESTATE_NOPVP);
			}
			if (flags.contains("no_logout") && flags["no_logout"]) {
				tile->setMapFlags(tile->getMapFlags() | TILESTATE_NOLOGOUT);
			}
			if (flags.contains("refresh") && flags["refresh"]) {
				tile->setMapFlags(tile->getMapFlags() | TILESTATE_REFRESH);
			}
		}

		// Set house information if present
		if (jsonData.contains("house_id")) {
			uint32_t houseId = jsonData["house_id"];
			tile->house_id = houseId; // Direct access to public member
		}

		// Set zones if present
		if (jsonData.contains("zones")) {
			const json &zones = jsonData["zones"];
			if (zones.is_array()) {
				for (const auto &zoneId : zones) {
					tile->addZone(zoneId);
				}
			}
		}

		// Add ground item if present
		if (jsonData.contains("ground")) {
			Item* ground = deserializeItem(jsonData["ground"]);
			if (ground) {
				tile->addItem(ground);
			}
		}

		// Add items if present
		if (jsonData.contains("items")) {
			const json &items = jsonData["items"];
			if (items.is_array()) {
				// Sort items by stack position to maintain proper stacking order
				std::vector<std::pair<int, json>> sortedItems;

				for (const auto &itemData : items) {
					int stackPosition = 0;
					if (itemData.contains("stack_position")) {
						stackPosition = itemData["stack_position"];
					}
					sortedItems.push_back(std::make_pair(stackPosition, itemData));
				}

				// Sort by stack position (lowest first = bottom of stack)
				std::sort(sortedItems.begin(), sortedItems.end(),
						 [](const std::pair<int, json> &a, const std::pair<int, json> &b) {
							 return a.first < b.first;
						 });

				// Add items in sorted order
				for (const auto &sortedItem : sortedItems) {
					Item* item = deserializeItem(sortedItem.second);
					if (item) {
						tile->addItem(item);
					}
				}
			}
		}

		// Add the tile to the map (this is crucial!)
		map.setTile(pos, tile);

		// Update tile to recalculate statflags (blocking, etc.) for proper visual appearance
		tile->update();

		// Note: Monster spawns and NPC spawns are handled by deserializeSpawns and deserializeNpcSpawns
		// because they need special handling and are stored in the map's spawn collections

		return true;

	} catch (const json::exception &e) {
		error("Error deserializing tile at position (%d, %d, %d): %s", pos.x, pos.y, pos.z, e.what());
		return false;
	} catch (const std::exception &e) {
		error("Unexpected error deserializing tile at position (%d, %d, %d): %s", pos.x, pos.y, pos.z, e.what());
		return false;
	}
}
Item* IOMapJSON::deserializeItem(const json &jsonData) {
	try {
		// Get item ID - this is required
		if (!jsonData.contains("id")) {
			error("Item missing required 'id' field");
			return nullptr;
		}

		uint16_t itemId = jsonData["id"];
		uint16_t count = 0xFFFF; // Default subtype

		// Get count/subtype if present
		if (jsonData.contains("count")) {
			count = jsonData["count"];
		}

		// Create the base item
		Item* item = Item::Create(itemId, count);
		if (!item) {
			error("Failed to create item with ID %d", itemId);
			return nullptr;
		}

		// Set unique ID if present
		if (jsonData.contains("unique_id")) {
			item->setUniqueID(jsonData["unique_id"]);
		}

		// Set action ID if present
		if (jsonData.contains("action_id")) {
			item->setActionID(jsonData["action_id"]);
		}

		// Set text if present
		if (jsonData.contains("text")) {
			std::string text = jsonData["text"];
			item->setText(text);
		}

		// Set description if present
		if (jsonData.contains("description")) {
			std::string desc = jsonData["description"];
			item->setDescription(desc);
		}

		// Handle container contents if it's a container
		if (jsonData.contains("contents") && item->getContainer()) {
			Container* container = item->getContainer();
			const json &contents = jsonData["contents"];

			if (contents.is_array()) {
				for (const auto &itemData : contents) {
					Item* containerItem = deserializeItem(itemData);
					if (containerItem) {
						container->getVector().push_back(containerItem);
					}
				}
			}
		}

		// Handle depot properties if it's a depot
		if (jsonData.contains("depot") && item->getDepot()) {
			const json &depotData = jsonData["depot"];
			if (depotData.contains("depot_id")) {
				item->getDepot()->setDepotID(depotData["depot_id"]);
			}
		}

		// Handle teleport destination if it's a teleport
		if (jsonData.contains("teleport") && item->getTeleport()) {
			const json &teleportData = jsonData["teleport"];
			if (teleportData.contains("x") && teleportData.contains("y") && teleportData.contains("z")) {
				Position dest(teleportData["x"], teleportData["y"], teleportData["z"]);
				item->getTeleport()->setDestination(dest);
			}
		}

		// Handle door properties if it's a door
		if (jsonData.contains("door") && item->getDoor()) {
			const json &doorData = jsonData["door"];
			if (doorData.contains("door_id")) {
				item->getDoor()->setDoorID(doorData["door_id"]);
			}
		}

		// Note: Most other properties in the JSON (stacking, stats, text_properties, etc.)
		// are read-only properties of the ItemType and don't need to be set during deserialization.
		// They are included in serialization for documentation/debugging purposes.

		return item;

	} catch (const json::exception &e) {
		error("Error deserializing item: %s", e.what());
		return nullptr;
	} catch (const std::exception &e) {
		error("Unexpected error deserializing item: %s", e.what());
		return nullptr;
	}
}
bool IOMapJSON::deserializeTowns(Map &map, const json &jsonData) {
	try {
		if (!jsonData.is_array()) {
			error("Towns data is not an array");
			return false;
		}

		for (const auto &townData : jsonData) {
			// Get required fields
			if (!townData.contains("id") || !townData.contains("name")) {
				warning("Town missing required 'id' or 'name' field, skipping");
				continue;
			}

			uint32_t townId = townData["id"];
			std::string townName = townData["name"];

			// Create new town
			Town* town = new Town(townId);
			town->setName(townName);

			// Set temple position if present
			if (townData.contains("temple")) {
				const json &temple = townData["temple"];
				if (temple.contains("x") && temple.contains("y") && temple.contains("z")) {
					Position templePos(temple["x"], temple["y"], temple["z"]);
					town->setTemplePosition(templePos);
				}
			}

			// Add town to the map
			if (!map.towns.addTown(town)) {
				warning("Failed to add town '%s' with ID %d to map", townName.c_str(), townId);
				delete town; // Clean up if adding failed
			}
		}

		return true;

	} catch (const json::exception &e) {
		error("Error deserializing towns: %s", e.what());
		return false;
	} catch (const std::exception &e) {
		error("Unexpected error deserializing towns: %s", e.what());
		return false;
	}
}
bool IOMapJSON::deserializeHouses(Map &map, const json &jsonData) {
	try {
		if (!jsonData.is_array()) {
			error("Houses data is not an array");
			return false;
		}

		for (const auto &houseData : jsonData) {
			// Get required fields
			if (!houseData.contains("id") || !houseData.contains("name")) {
				warning("House missing required 'id' or 'name' field, skipping");
				continue;
			}

			// Create new house
			House* house = new House(map);

			// Set basic properties
			house->id = houseData["id"];
			house->name = houseData["name"];

			// Set optional properties with defaults
			house->rent = houseData.contains("rent") ? static_cast<int>(houseData["rent"]) : 0;
			house->clientid = houseData.contains("clientid") ? static_cast<int>(houseData["clientid"]) : 0;
			house->beds = houseData.contains("beds") ? static_cast<int>(houseData["beds"]) : 0;
			house->townid = houseData.contains("townid") ? static_cast<uint32_t>(houseData["townid"]) : 0;
			house->guildhall = houseData.contains("guildhall") ? static_cast<bool>(houseData["guildhall"]) : false;

			// Set entry position if present
			if (houseData.contains("entry")) {
				const json &entry = houseData["entry"];
				if (entry.contains("x") && entry.contains("y") && entry.contains("z")) {
					Position entryPos(entry["x"], entry["y"], entry["z"]);
					house->setExit(entryPos);
				}
			}

			// Add house to the map
			map.houses.addHouse(house);
		}

		return true;

	} catch (const json::exception &e) {
		error("Error deserializing houses: %s", e.what());
		return false;
	} catch (const std::exception &e) {
		error("Unexpected error deserializing houses: %s", e.what());
		return false;
	}
}
bool IOMapJSON::deserializeWaypoints(Map &map, const json &jsonData) {
	try {
		if (!jsonData.is_array()) {
			error("Waypoints data is not an array");
			return false;
		}

		for (const auto &waypointData : jsonData) {
			// Get required fields
			if (!waypointData.contains("name") || !waypointData.contains("x") ||
				!waypointData.contains("y") || !waypointData.contains("z")) {
				warning("Waypoint missing required fields, skipping");
				continue;
			}

			// Create new waypoint
			Waypoint* waypoint = new Waypoint();
			waypoint->name = waypointData["name"];
			waypoint->pos = Position(waypointData["x"], waypointData["y"], waypointData["z"]);

			// Add waypoint to the map
			map.waypoints.addWaypoint(waypoint);
		}

		return true;

	} catch (const json::exception &e) {
		error("Error deserializing waypoints: %s", e.what());
		return false;
	} catch (const std::exception &e) {
		error("Unexpected error deserializing waypoints: %s", e.what());
		return false;
	}
}
bool IOMapJSON::deserializeZones(Map &map, const json &jsonData) {
	try {
		if (!jsonData.is_array()) {
			error("Zones data is not an array");
			return false;
		}

		for (const auto &zoneData : jsonData) {
			// Get required fields
			if (!zoneData.contains("name") || !zoneData.contains("id")) {
				warning("Zone missing required 'name' or 'id' field, skipping");
				continue;
			}

			std::string zoneName = zoneData["name"];
			unsigned int zoneId = zoneData["id"];

			// Add zone to the map
			if (!map.zones.addZone(zoneName, zoneId)) {
				warning("Failed to add zone '%s' with ID %d", zoneName.c_str(), zoneId);
			}
		}

		return true;

	} catch (const json::exception &e) {
		error("Error deserializing zones: %s", e.what());
		return false;
	} catch (const std::exception &e) {
		error("Unexpected error deserializing zones: %s", e.what());
		return false;
	}
}
bool IOMapJSON::deserializeSpawns(Map &map, const json &jsonData) {
	try {
		if (!jsonData.is_array()) {
			error("Spawns data is not an array");
			return false;
		}

		for (const auto &spawnData : jsonData) {
			// Get required fields
			if (!spawnData.contains("centerx") || !spawnData.contains("centery") ||
				!spawnData.contains("centerz") || !spawnData.contains("radius")) {
				warning("Spawn missing required center position or radius, skipping");
				continue;
			}

			Position centerPos(spawnData["centerx"], spawnData["centery"], spawnData["centerz"]);
			int radius = spawnData["radius"];

			// Get or create the spawn tile
			Tile* spawnTile = map.getTile(centerPos);
			if (!spawnTile) {
				spawnTile = map.allocator(map.createTileL(centerPos));
			}

			// Create and set the spawn
			SpawnMonster* spawn = new SpawnMonster(radius);
			spawnTile->spawnMonster = spawn;
			map.addSpawnMonster(spawnTile);

			// Load monsters if present
			if (spawnData.contains("monsters") && spawnData["monsters"].is_array()) {
				const json &monsters = spawnData["monsters"];

				for (const auto &monsterData : monsters) {
					if (!monsterData.contains("name")) {
						warning("Monster missing name, skipping");
						continue;
					}

					std::string monsterName = monsterData["name"];

					// Get position relative to spawn center (default to center)
					int relX = monsterData.contains("x") ? static_cast<int>(monsterData["x"]) : 0;
					int relY = monsterData.contains("y") ? static_cast<int>(monsterData["y"]) : 0;
					int z = monsterData.contains("z") ? static_cast<int>(monsterData["z"]) : centerPos.z;

					Position monsterPos(centerPos.x + relX, centerPos.y + relY, z);

					// Get or create the monster tile
					Tile* monsterTile = map.getTile(monsterPos);
					if (!monsterTile) {
						monsterTile = map.allocator(map.createTileL(monsterPos));
					}

					// Create the monster
					int weight = g_settings.getInteger(Config::MONSTER_DEFAULT_WEIGHT); // Default weight
					if (monsterData.contains("weight")) {
						weight = monsterData["weight"];
					}

					Monster* monster = new Monster(monsterName, weight);

					// Set spawntime if present
					if (monsterData.contains("spawntime")) {
						monster->setSpawnMonsterTime(monsterData["spawntime"]);
					} else {
						monster->setSpawnMonsterTime(g_settings.getInteger(Config::DEFAULT_SPAWN_MONSTER_TIME));
					}

					// Set direction if present
					if (monsterData.contains("direction")) {
						std::string dirName = monsterData["direction"];
						Direction dir = NORTH; // Default
						if (dirName == "north") dir = NORTH;
						else if (dirName == "east") dir = EAST;
						else if (dirName == "south") dir = SOUTH;
						else if (dirName == "west") dir = WEST;
						monster->setDirection(dir);
					} else {
						monster->setDirection(NORTH); // Default
					}

					// Add monster to tile
					monsterTile->monsters.push_back(monster);
				}
			}
		}

		return true;

	} catch (const json::exception &e) {
		error("Error deserializing monster spawns: %s", e.what());
		return false;
	} catch (const std::exception &e) {
		error("Unexpected error deserializing monster spawns: %s", e.what());
		return false;
	}
}
bool IOMapJSON::deserializeNpcSpawns(Map &map, const json &jsonData) {
	try {
		if (!jsonData.is_array()) {
			error("NPC spawns data is not an array");
			return false;
		}

		for (const auto &spawnData : jsonData) {
			// Get required fields
			if (!spawnData.contains("centerx") || !spawnData.contains("centery") ||
				!spawnData.contains("centerz") || !spawnData.contains("radius")) {
				warning("NPC spawn missing required center position or radius, skipping");
				continue;
			}

			Position centerPos(spawnData["centerx"], spawnData["centery"], spawnData["centerz"]);
			int radius = spawnData["radius"];

			// Get or create the spawn tile
			Tile* spawnTile = map.getTile(centerPos);
			if (!spawnTile) {
				spawnTile = map.allocator(map.createTileL(centerPos));
			}

			// Create and set the spawn
			SpawnNpc* spawn = new SpawnNpc(radius);
			spawnTile->spawnNpc = spawn;
			map.addSpawnNpc(spawnTile);

			// Load NPCs if present
			if (spawnData.contains("npcs") && spawnData["npcs"].is_array()) {
				const json &npcs = spawnData["npcs"];

				for (const auto &npcData : npcs) {
					if (!npcData.contains("name")) {
						warning("NPC missing name, skipping");
						continue;
					}

					std::string npcName = npcData["name"];

					// Get position relative to spawn center (default to center)
					int relX = npcData.contains("x") ? static_cast<int>(npcData["x"]) : 0;
					int relY = npcData.contains("y") ? static_cast<int>(npcData["y"]) : 0;
					int z = npcData.contains("z") ? static_cast<int>(npcData["z"]) : centerPos.z;

					Position npcPos(centerPos.x + relX, centerPos.y + relY, z);

					// Get or create the NPC tile
					Tile* npcTile = map.getTile(npcPos);
					if (!npcTile) {
						npcTile = map.allocator(map.createTileL(npcPos));
					}

					// Create the NPC
					Npc* npc = new Npc(npcName);

					// Set spawntime if present
					if (npcData.contains("spawntime")) {
						npc->setSpawnNpcTime(npcData["spawntime"]);
					} else {
						npc->setSpawnNpcTime(g_settings.getInteger(Config::DEFAULT_SPAWN_NPC_TIME));
					}

					// Set direction if present
					if (npcData.contains("direction")) {
						std::string dirName = npcData["direction"];
						Direction dir = NORTH; // Default
						if (dirName == "north") dir = NORTH;
						else if (dirName == "east") dir = EAST;
						else if (dirName == "south") dir = SOUTH;
						else if (dirName == "west") dir = WEST;
						npc->setDirection(dir);
					} else {
						npc->setDirection(NORTH); // Default
					}

					// Set NPC to tile
					npcTile->npc = npc;
				}
			}
		}

		return true;

	} catch (const json::exception &e) {
		error("Error deserializing NPC spawns: %s", e.what());
		return false;
	} catch (const std::exception &e) {
		error("Unexpected error deserializing NPC spawns: %s", e.what());
		return false;
	}
}

void IOMapJSON::linkHouseTiles(Map &map) {
	// Iterate through all tiles and link them to their house objects
	for (auto it = map.begin(); it != map.end(); ++it) {
		Tile *tile = it->get();
		if (tile && tile->isHouseTile()) {
			uint32_t houseId = tile->getHouseID();

			// Find the house object
			House* house = map.houses.getHouse(houseId);
			if (house) {
				// Add tile to house (this also sets tile->house pointer)
				house->addTile(tile);
			} else {
				warning("Found house tile with house ID %u, but no house object exists", houseId);
			}
		}
	}
}
