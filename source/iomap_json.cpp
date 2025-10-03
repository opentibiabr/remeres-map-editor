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
#include <algorithm>
#include <vector>

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

		// Add tiles with RLE compression for ground tiles
		json tiles = json::array();
		json ground_rle = json::array();

		g_gui.SetLoadDone(0, "Exporting map tiles...");

		uint32_t processed_tiles = 0;
		uint32_t total_tiles = map.getTileCount();

		// Collect all tiles first for RLE processing
		std::vector<Tile*> allTiles;
		for (auto it = map.begin(); it != map.end(); ++it) {
			Tile *tile = it->get();
			if (tile && !tile->empty()) {
				allTiles.push_back(tile);
			}
		}

		// Sort tiles by z, y, x for optimal RLE compression
		std::sort(allTiles.begin(), allTiles.end(), [](const Tile* a, const Tile* b) {
			if (a->getZ() != b->getZ()) return a->getZ() < b->getZ();
			if (a->getY() != b->getY()) return a->getY() < b->getY();
			return a->getX() < b->getX();
		});

		// Process tiles with RLE compression for ground tiles
		size_t i = 0;
		while (i < allTiles.size()) {
			Tile* tile = allTiles[i];

			// Check if this is a simple tile (candidate for RLE)
			// A simple tile has no stacked items (only ground or no ground), no spawns, no NPCs
			bool isSimpleTile = tile->items.empty() && !tile->spawnMonster && !tile->npc && tile->monsters.empty();

			if (isSimpleTile) {
				// Start RLE sequence
				Position startPos(tile->getX(), tile->getY(), tile->getZ());
				Position endPos = startPos;
				json groundData;

				// Serialize ground data (or null if no ground)
				if (tile->hasGround()) {
					groundData = serializeItem(*tile->ground);
				} else {
					groundData = nullptr;  // JSON null for tiles with no ground
				}

				// Find consecutive identical simple tiles on the same row
				size_t j = i + 1;
				while (j < allTiles.size()) {
					Tile* nextTile = allTiles[j];

					// Check if next tile is identical simple tile on same z level and consecutive position
					bool isIdenticalSimpleTile = nextTile->items.empty() &&
												!nextTile->spawnMonster &&
												!nextTile->npc &&
												nextTile->monsters.empty() &&
												nextTile->getZ() == startPos.z &&
												nextTile->getY() == endPos.y &&
												nextTile->getX() == endPos.x + 1;

					// Check ground similarity
					if (isIdenticalSimpleTile) {
						if (tile->hasGround() && nextTile->hasGround()) {
							// Both have ground - check if identical
							isIdenticalSimpleTile = nextTile->ground->getID() == tile->ground->getID() &&
													nextTile->ground->getSubtype() == tile->ground->getSubtype() &&
													nextTile->ground->getUniqueID() == tile->ground->getUniqueID() &&
													nextTile->ground->getActionID() == tile->ground->getActionID() &&
													nextTile->ground->getText() == tile->ground->getText();
						} else if (!tile->hasGround() && !nextTile->hasGround()) {
							// Both have no ground - identical
							isIdenticalSimpleTile = true;
						} else {
							// One has ground, other doesn't - not identical
							isIdenticalSimpleTile = false;
						}
					}

					if (isIdenticalSimpleTile) {
						endPos.x = nextTile->getX();
						j++;
					} else {
						break;
					}
				}

				// Create RLE entry if we have more than one consecutive tile
				if (endPos.x > startPos.x) {
					json rleEntry;
					rleEntry["ground"] = groundData;
					rleEntry["from_x"] = startPos.x;
					rleEntry["to_x"] = endPos.x;
					rleEntry["y"] = startPos.y;
					rleEntry["z"] = startPos.z;
					rleEntry["count"] = (endPos.x - startPos.x + 1);
					ground_rle.push_back(rleEntry);

					i = j; // Skip all compressed tiles
				} else {
					// Single tile, add normally
					tiles.push_back(serializeTile(*tile));
					i++;
				}
			} else {
				// Complex tile, add normally
				tiles.push_back(serializeTile(*tile));
				i++;
			}

			processed_tiles++;
			if (processed_tiles % 1000 == 0) {
				g_gui.SetLoadDone(processed_tiles * 100 / total_tiles, "Exporting map tiles...");
			}
		}

		document["tiles"] = tiles;
		if (!ground_rle.empty()) {
			document["ground_rle"] = ground_rle;
		}

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
			spawn["x"] = pos.x;
			spawn["y"] = pos.y;
			spawn["z"] = pos.z;
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
								monsterData["x"] = x;
								monsterData["y"] = y;
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
			npcSpawn["x"] = pos.x;
			npcSpawn["y"] = pos.y;
			npcSpawn["z"] = pos.z;
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
						npcData["x"] = x;
						npcData["y"] = y;
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
bool IOMapJSON::deserializeMapData(Map &map, const json &jsonData) { return false; }
bool IOMapJSON::deserializeTile(Map &map, const json &jsonData, const Position &pos) { return false; }
Item* IOMapJSON::deserializeItem(const json &jsonData) { return nullptr; }
bool IOMapJSON::deserializeTowns(Map &map, const json &jsonData) { return false; }
bool IOMapJSON::deserializeHouses(Map &map, const json &jsonData) { return false; }
bool IOMapJSON::deserializeWaypoints(Map &map, const json &jsonData) { return false; }
bool IOMapJSON::deserializeZones(Map &map, const json &jsonData) { return false; }
bool IOMapJSON::deserializeSpawns(Map &map, const json &jsonData) { return false; }
bool IOMapJSON::deserializeNpcSpawns(Map &map, const json &jsonData) { return false; }
