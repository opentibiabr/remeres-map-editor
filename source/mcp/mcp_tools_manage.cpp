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

#include "mcp_tools.h"
#include "mcp_write.h"

#include "../common.h"
#include "../enums.h"
#include "../editor.h"
#include "../gui.h"
#include "../house.h"
#include "../map.h"
#include "../monster.h"
#include "../monsters.h"
#include "../npc.h"
#include "../npcs.h"
#include "../spawn_monster.h"
#include "../spawn_npc.h"
#include "../tile.h"
#include "../town.h"
#include "../waypoints.h"
#include "../zones.h"

#include <string>
#include <vector>

namespace mcp {

	namespace {

		std::vector<Position> readPositions(const json &params, const char* key) {
			if (!params.contains(key) || !params[key].is_array() || params[key].empty()) {
				throw McpError(std::string(key) + " must be a non-empty array of positions");
			}
			std::vector<Position> positions;
			positions.reserve(params[key].size());
			for (const json &entry : params[key]) {
				positions.push_back(parsePosition(entry, key));
			}
			return positions;
		}

		std::string requireOp(const json &params) {
			const std::string op = readString(params, "op");
			if (op.empty()) {
				throw McpError("op is required");
			}
			return op;
		}

		// ------------------------------------------------------------------
		// Houses
		// ------------------------------------------------------------------

		json toolHouseManage(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();
			const std::string op = requireOp(params);

			if (op == "create") {
				auto* house = newd House(map);
				// getEmptyID() matters: Houses::addHouse asserts on a duplicate id.
				house->id = map.houses.getEmptyID();
				house->name = readString(params, "name", fmt::format("Unnamed House {}", house->id));
				house->townid = static_cast<uint32_t>(readInt(params, "townId", 0, 0, 0x7FFFFFFF));
				house->rent = readInt(params, "rent", 0, 0, 0x7FFFFFFF);
				house->beds = readInt(params, "beds", 0, 0, 0xFFFF);
				house->guildhall = params.value("guildhall", false);
				map.houses.addHouse(house);

				if (params.contains("exit")) {
					house->setExit(&map, parsePosition(params["exit"], "exit"));
				}

				map.doChange();
				g_gui.RefreshView();
				return jsonResult(json { { "op", op }, { "houseId", house->id }, { "name", house->name } });
			}

			const auto id = static_cast<uint32_t>(readInt(params, "id", 0, 1, 0x7FFFFFFF));
			House* house = map.houses.getHouse(id);
			if (!house) {
				throw McpError(fmt::format("no house with id {}; use house_list to see the ids in this map", id));
			}

			if (op == "update") {
				if (params.contains("name")) {
					house->name = params["name"].get<std::string>();
				}
				if (params.contains("townId")) {
					house->townid = params["townId"].get<uint32_t>();
				}
				if (params.contains("rent")) {
					house->rent = params["rent"].get<int>();
				}
				if (params.contains("beds")) {
					house->beds = params["beds"].get<int>();
				}
				if (params.contains("guildhall")) {
					house->guildhall = params["guildhall"].get<bool>();
				}
				map.doChange();
				g_gui.RefreshView();
				return jsonResult(json { { "op", op }, { "houseId", id } });
			}

			if (op == "set_exit") {
				if (!params.contains("exit")) {
					throw McpError("set_exit requires an exit position");
				}
				house->setExit(&map, parsePosition(params["exit"], "exit"));
				map.doChange();
				g_gui.RefreshView();
				return jsonResult(json { { "op", op }, { "houseId", id } });
			}

			if (op == "delete") {
				const std::string name = house->name;
				const size_t tiles = house->size();
				// removeHouse() cleans every tile and deletes the object, so the
				// pointer must not be touched afterwards.
				map.houses.removeHouse(house);
				map.doChange();
				g_gui.RefreshView();
				return jsonResult(json { { "op", op }, { "houseId", id }, { "name", name }, { "tilesReleased", tiles } });
			}

			if (op == "assign_tiles" || op == "unassign_tiles") {
				const std::vector<Position> positions = readPositions(params, "positions");
				const bool assigning = op == "assign_tiles";

				// Action::commit/undo moves the tiles in and out of Houses from
				// the house_id difference, so this stays undoable.
				TileBatch batch(*editor);
				for (const Position &position : positions) {
					Tile* tile = batch.edit(position);
					if (assigning) {
						tile->setHouse(house);
						tile->setPZ(true);
					} else if (tile->getHouseID() == id) {
						tile->setHouse(nullptr);
					}
					tile->update();
				}
				const size_t changed = batch.commit();

				return jsonResult(json { { "op", op }, { "houseId", id }, { "tilesChanged", changed } });
			}

			throw McpError("op must be one of: create, update, delete, set_exit, assign_tiles, unassign_tiles");
		}

		// ------------------------------------------------------------------
		// Towns
		// ------------------------------------------------------------------

		json toolTownManage(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();
			const std::string op = requireOp(params);

			if (op == "create") {
				auto* town = newd Town(map.towns.getEmptyID());
				town->setName(readString(params, "name", fmt::format("Town {}", town->getID())));
				if (params.contains("templePosition")) {
					town->setTemplePosition(parsePosition(params["templePosition"], "templePosition"));
				}
				if (!map.towns.addTown(town)) {
					delete town;
					throw McpError("could not add the town: the id is already taken");
				}
				map.doChange();
				g_gui.RefreshView();
				return jsonResult(json { { "op", op }, { "townId", town->getID() }, { "name", town->getName() } });
			}

			const auto id = static_cast<uint32_t>(readInt(params, "id", 0, 1, 0x7FFFFFFF));
			Town* town = map.towns.getTown(id);
			if (!town) {
				throw McpError(fmt::format("no town with id {}", id));
			}

			if (op == "update") {
				if (params.contains("name")) {
					town->setName(params["name"].get<std::string>());
				}
				if (params.contains("templePosition")) {
					town->setTemplePosition(parsePosition(params["templePosition"], "templePosition"));
				}
				map.doChange();
				g_gui.RefreshView();
				return jsonResult(json { { "op", op }, { "townId", id } });
			}

			if (op == "delete") {
				// Houses keep a townid; deleting a town that still has houses
				// would leave them pointing at nothing.
				std::vector<uint32_t> attached;
				for (const auto &[houseId, house] : map.houses) {
					if (house && house->townid == id) {
						attached.push_back(houseId);
					}
				}
				if (!attached.empty() && !params.value("force", false)) {
					throw McpError(fmt::format(
						"town {} still has {} house(s) assigned; reassign them first, or pass force=true to delete anyway",
						id, attached.size()
					));
				}

				const std::string name = town->getName();
				// Towns has no removeTown(): erase the entry and free it here.
				auto iter = map.towns.find(id);
				if (iter != map.towns.end()) {
					map.towns.erase(iter);
				}
				delete town;

				map.doChange();
				g_gui.RefreshView();
				return jsonResult(json { { "op", op }, { "townId", id }, { "name", name }, { "orphanedHouses", attached } });
			}

			throw McpError("op must be one of: create, update, delete");
		}

		// ------------------------------------------------------------------
		// Waypoints
		// ------------------------------------------------------------------

		json toolWaypointManage(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();
			const std::string op = requireOp(params);

			const std::string name = readString(params, "name");
			if (name.empty()) {
				throw McpError("name is required");
			}

			if (op == "create" || op == "move") {
				if (!params.contains("position")) {
					throw McpError(op + " requires a position");
				}
				auto* waypoint = newd Waypoint();
				waypoint->name = name;
				waypoint->pos = parsePosition(params["position"], "position");
				// addWaypoint replaces any waypoint with the same name, so
				// create and move are the same call.
				map.waypoints.addWaypoint(waypoint);

				map.doChange();
				g_gui.RefreshView();
				return jsonResult(json { { "op", op }, { "name", waypoint->name }, { "position", positionToJson(waypoint->pos) } });
			}

			if (op == "delete") {
				if (!map.waypoints.getWaypoint(name)) {
					throw McpError(fmt::format("no waypoint named '{}'", name));
				}
				map.waypoints.removeWaypoint(name);
				map.doChange();
				g_gui.RefreshView();
				return jsonResult(json { { "op", op }, { "name", name } });
			}

			throw McpError("op must be one of: create, move, delete");
		}

		// ------------------------------------------------------------------
		// Zones
		// ------------------------------------------------------------------

		json toolZoneManage(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();
			const std::string op = requireOp(params);

			if (op == "create") {
				const std::string name = readString(params, "name");
				if (name.empty()) {
					throw McpError("name is required");
				}
				if (!map.zones.addZone(name)) {
					throw McpError(fmt::format("a zone named '{}' already exists", name));
				}
				map.doChange();
				return jsonResult(json { { "op", op }, { "name", name }, { "zoneId", map.zones.getZoneID(name) } });
			}

			if (op == "delete") {
				const std::string name = readString(params, "name");
				if (name.empty()) {
					throw McpError("name is required");
				}
				const unsigned int zoneId = map.zones.getZoneID(name);
				if (zoneId == 0) {
					throw McpError(fmt::format("no zone named '{}'", name));
				}

				// Strip the id off every tile first, otherwise tiles keep
				// referencing a zone that no longer has a name.
				std::vector<Position> tagged;
				auto collect = [&](TileLocation* location) {
					Tile* tile = location ? location->get() : nullptr;
					if (tile && tile->hasZone(zoneId)) {
						tagged.push_back(tile->getPosition());
					}
				};
				map.forEachTileLocation(collect);

				TileBatch batch(*editor);
				for (const Position &position : tagged) {
					batch.edit(position)->removeZone(zoneId);
				}
				batch.commit();

				map.zones.removeZone(name);
				map.doChange();
				g_gui.RefreshView();
				return jsonResult(json { { "op", op }, { "name", name }, { "zoneId", zoneId }, { "tilesCleared", tagged.size() } });
			}

			if (op == "assign_tiles" || op == "unassign_tiles") {
				const std::string name = readString(params, "name");
				unsigned int zoneId = static_cast<unsigned int>(readInt(params, "zoneId", 0, 0, 0x7FFFFFFF));
				if (zoneId == 0 && !name.empty()) {
					zoneId = map.zones.getZoneID(name);
				}
				if (zoneId == 0) {
					throw McpError("give an existing zone by name or zoneId; use zone_list to see them");
				}
				if (!map.zones.hasZone(zoneId)) {
					throw McpError(fmt::format("no zone with id {}", zoneId));
				}

				const std::vector<Position> positions = readPositions(params, "positions");
				const bool assigning = op == "assign_tiles";

				TileBatch batch(*editor);
				for (const Position &position : positions) {
					Tile* tile = batch.edit(position);
					if (assigning) {
						tile->addZone(zoneId);
					} else {
						tile->removeZone(zoneId);
					}
				}
				const size_t changed = batch.commit();

				return jsonResult(json { { "op", op }, { "zoneId", zoneId }, { "tilesChanged", changed } });
			}

			throw McpError("op must be one of: create, delete, assign_tiles, unassign_tiles");
		}

		// ------------------------------------------------------------------
		// Spawns
		// ------------------------------------------------------------------

		bool isNpcKind(const json &params) {
			const std::string kind = readString(params, "kind", "monster");
			if (kind == "monster") {
				return false;
			}
			if (kind == "npc") {
				return true;
			}
			throw McpError("kind must be monster or npc");
		}

		// Creature facing. The editor stores it per creature, and a boss or npc
		// placed facing the wrong way is a visible mistake in game.
		Direction parseDirection(const json &params, Direction fallback) {
			if (!params.contains("direction")) {
				return fallback;
			}
			const std::string name = as_lower_str(params["direction"].get<std::string>());
			if (name == "north" || name == "n") {
				return NORTH;
			}
			if (name == "east" || name == "e") {
				return EAST;
			}
			if (name == "south" || name == "s") {
				return SOUTH;
			}
			if (name == "west" || name == "w") {
				return WEST;
			}
			throw McpError("direction must be one of: north, east, south, west");
		}

		json toolSpawnManage(const json &params) {
			Editor* editor = requireEditor();
			const std::string op = requireOp(params);
			const bool npcKind = isNpcKind(params);

			if (!params.contains("position")) {
				throw McpError("position is required");
			}
			const Position position = parsePosition(params["position"], "position");

			// Action::commit routes spawn changes through Map::addSpawnMonster /
			// removeSpawnMonster, which keep the per-TileLocation spawn counts
			// correct on both commit and undo.
			TileBatch batch(*editor);
			Tile* tile = batch.edit(position);

			if (op == "create") {
				const int radius = readInt(params, "radius", 3, 0, 99);
				if (npcKind) {
					delete tile->spawnNpc;
					tile->spawnNpc = newd SpawnNpc(radius);
				} else {
					delete tile->spawnMonster;
					tile->spawnMonster = newd SpawnMonster(radius);
				}
				tile->update();
				batch.commit();
				return jsonResult(json { { "op", op }, { "kind", npcKind ? "npc" : "monster" }, { "position", positionToJson(position) }, { "radius", radius } });
			}

			if (op == "update") {
				const int radius = readInt(params, "radius", -1, 0, 99);
				if (radius < 0) {
					throw McpError("update needs a radius");
				}
				if (npcKind) {
					if (!tile->spawnNpc) {
						throw McpError("there is no npc spawn on that tile");
					}
					tile->spawnNpc->setSize(radius);
				} else {
					if (!tile->spawnMonster) {
						throw McpError("there is no monster spawn on that tile");
					}
					tile->spawnMonster->setSize(radius);
				}
				tile->update();
				batch.commit();
				return jsonResult(json { { "op", op }, { "kind", npcKind ? "npc" : "monster" }, { "position", positionToJson(position) }, { "radius", radius } });
			}

			if (op == "delete") {
				if (npcKind) {
					if (!tile->spawnNpc) {
						throw McpError("there is no npc spawn on that tile");
					}
					delete tile->spawnNpc;
					tile->spawnNpc = nullptr;
				} else {
					if (!tile->spawnMonster) {
						throw McpError("there is no monster spawn on that tile");
					}
					delete tile->spawnMonster;
					tile->spawnMonster = nullptr;
				}
				tile->update();
				batch.commit();
				return jsonResult(json { { "op", op }, { "kind", npcKind ? "npc" : "monster" }, { "position", positionToJson(position) } });
			}

			if (op == "add_creature") {
				const std::string name = readString(params, "name");
				if (name.empty()) {
					throw McpError("name is required");
				}

				if (npcKind) {
					NpcType* type = g_npcs[name];
					if (!type) {
						throw McpError(fmt::format("unknown npc '{}'; use npc_types_list to see valid names", name));
					}
					auto* npc = newd Npc(type);
					npc->setSpawnNpcTime(readInt(params, "spawnTime", 60, 1, 0xFFFF));
					npc->setDirection(parseDirection(params, SOUTH));
					delete tile->npc;
					// A tile holds at most one npc.
					tile->npc = npc;
				} else {
					MonsterType* type = g_monsters[name];
					if (!type) {
						throw McpError(fmt::format("unknown monster '{}'; use monster_types_list to see valid names", name));
					}
					auto* monster = newd Monster(type);
					monster->setSpawnMonsterTime(static_cast<uint16_t>(readInt(params, "spawnTime", 60, 1, 0xFFFF)));
					monster->setWeight(readInt(params, "weight", 1, 1, 0xFFFF));
					monster->setDirection(parseDirection(params, SOUTH));
					tile->addMonster(monster);
				}

				tile->update();
				batch.commit();
				return jsonResult(json { { "op", op }, { "kind", npcKind ? "npc" : "monster" }, { "name", name }, { "position", positionToJson(position) } });
			}

			if (op == "remove_creature") {
				const std::string name = readString(params, "name");

				if (npcKind) {
					if (!tile->npc) {
						throw McpError("there is no npc on that tile");
					}
					delete tile->npc;
					tile->npc = nullptr;
				} else if (name.empty()) {
					tile->clearMonsters();
				} else {
					const auto before = tile->monsters.size();
					for (auto it = tile->monsters.begin(); it != tile->monsters.end();) {
						if (as_lower_str((*it)->getName()) == as_lower_str(name)) {
							delete *it;
							it = tile->monsters.erase(it);
						} else {
							++it;
						}
					}
					if (tile->monsters.size() == before) {
						throw McpError(fmt::format("no monster named '{}' on that tile", name));
					}
				}

				tile->update();
				batch.commit();
				return jsonResult(json { { "op", op }, { "kind", npcKind ? "npc" : "monster" }, { "position", positionToJson(position) } });
			}

			throw McpError("op must be one of: create, delete, add_creature, remove_creature");
		}

		// ------------------------------------------------------------------
		// Schemas
		// ------------------------------------------------------------------

	} // namespace

	void registerManageTools(ToolRegistry &registry) {
		registry.add({ "house_manage",
					   "Create, update or delete houses, set a house exit, and attach or detach tiles. "
					   "Tile changes are undoable; creating and deleting a house is not part of the undo history.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "op", json { { "type", "string" }, { "enum", json::array({ "create", "update", "delete", "set_exit", "assign_tiles", "unassign_tiles" }) } } }, { "id", json { { "type", "integer" }, { "description", "required for everything but create" } } }, { "name", json { { "type", "string" } } }, { "townId", json { { "type", "integer" } } }, { "rent", json { { "type", "integer" } } }, { "beds", json { { "type", "integer" } } }, { "guildhall", json { { "type", "boolean" } } }, { "exit", positionSchema("house exit position") }, { "positions", positionArraySchema("tiles for assign_tiles and unassign_tiles") } } },
						   { "required", json::array({ "op" }) } },
					   true,
					   toolHouseManage });

		registry.add({ "town_manage",
					   "Create, update or delete towns. Deleting a town that still has houses assigned is refused unless force is set.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "op", json { { "type", "string" }, { "enum", json::array({ "create", "update", "delete" }) } } }, { "id", json { { "type", "integer" }, { "description", "required for update and delete" } } }, { "name", json { { "type", "string" } } }, { "templePosition", positionSchema("temple position") }, { "force", json { { "type", "boolean" }, { "description", "delete even with houses still assigned" } } } } },
						   { "required", json::array({ "op" }) } },
					   true,
					   toolTownManage });

		registry.add({ "waypoint_manage",
					   "Create, move or delete a waypoint by name.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "op", json { { "type", "string" }, { "enum", json::array({ "create", "move", "delete" }) } } }, { "name", json { { "type", "string" } } }, { "position", positionSchema("waypoint position, for create and move") } } },
						   { "required", json::array({ "op", "name" }) } },
					   true,
					   toolWaypointManage });

		registry.add({ "zone_manage",
					   "Create or delete a zone, and tag or untag tiles with it. Deleting a zone also strips its id from every tile that carried it.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "op", json { { "type", "string" }, { "enum", json::array({ "create", "delete", "assign_tiles", "unassign_tiles" }) } } }, { "name", json { { "type", "string" } } }, { "zoneId", json { { "type", "integer" }, { "description", "alternative to name for the tile operations" } } }, { "positions", positionArraySchema("tiles for assign_tiles and unassign_tiles") } } },
						   { "required", json::array({ "op" }) } },
					   true,
					   toolZoneManage });

		registry.add({ "spawn_manage",
					   "Create, resize or delete a spawn on a tile, and add or remove the creatures inside it. "
					   "A spawn stores only a centre and radius; the creatures live on the tiles. "
					   "add_creature takes spawnTime, weight and the direction the creature faces.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "op", json { { "type", "string" }, { "enum", json::array({ "create", "update", "delete", "add_creature", "remove_creature" }) } } }, { "kind", json { { "type", "string" }, { "enum", json::array({ "monster", "npc" }) }, { "description", "default monster" } } }, { "position", positionSchema("the tile to act on") }, { "radius", json { { "type", "integer" }, { "description", "for create and update, 0-99, default 3" } } }, { "name", json { { "type", "string" }, { "description", "creature name; omit on remove_creature to clear them all" } } }, { "spawnTime", json { { "type", "integer" }, { "description", "seconds between respawns, default 60" } } }, { "weight", json { { "type", "integer" }, { "description", "monster spawn weight, default 1" } } }, { "direction", json { { "type", "string" }, { "enum", json::array({ "north", "east", "south", "west" }) }, { "description", "which way the creature faces, default south" } } } } },
						   { "required", json::array({ "op", "position" }) } },
					   true,
					   toolSpawnManage });
	}

} // namespace mcp
