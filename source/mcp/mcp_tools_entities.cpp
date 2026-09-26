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

#include "../common.h"
#include "../editor.h"
#include "../house.h"
#include "../map.h"
#include "../monster.h"
#include "../monsters.h"
#include "../npc.h"
#include "../npcs.h"
#include "../position.h"
#include "../spawn_monster.h"
#include "../spawn_npc.h"
#include "../tile.h"
#include "../town.h"
#include "../waypoints.h"
#include "../zones.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace mcp {

	namespace {

		// True when the position falls inside the optional from/to box. Tools
		// that accept a region filter share this so "region" means the same
		// thing everywhere.
		struct OptionalBox {
			bool active = false;
			int minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;

			bool contains(const Position &pos) const {
				if (!active) {
					return true;
				}
				return pos.x >= minX && pos.x <= maxX && pos.y >= minY && pos.y <= maxY && pos.z >= minZ && pos.z <= maxZ;
			}
		};

		OptionalBox parseOptionalBox(const json &params) {
			OptionalBox box;
			if (!params.is_object() || !params.contains("from") || !params.contains("to")) {
				return box;
			}

			const Position a = parsePosition(params["from"], "from");
			const Position b = parsePosition(params["to"], "to");

			box.active = true;
			box.minX = std::min(a.x, b.x);
			box.maxX = std::max(a.x, b.x);
			box.minY = std::min(a.y, b.y);
			box.maxY = std::max(a.y, b.y);
			box.minZ = std::min(a.z, b.z);
			box.maxZ = std::max(a.z, b.z);
			return box;
		}

		// ------------------------------------------------------------------
		// Houses and towns
		// ------------------------------------------------------------------

		json houseToJson(const House* house, Map &map, bool includeTiles) {
			json out {
				{ "id", house->id },
				{ "name", house->name },
				{ "rent", house->rent },
				{ "beds", house->beds },
				{ "clientId", house->clientid },
				{ "guildhall", house->guildhall },
				{ "townId", house->townid },
				{ "tileCount", house->size() },
				{ "exit", positionToJson(house->getExit()) }
			};

			const Town* town = map.towns.getTown(house->townid);
			if (town) {
				out["townName"] = town->getName();
			}

			if (includeTiles) {
				json tiles = json::array();
				for (const Position &pos : house->getTiles()) {
					tiles.push_back(positionToJson(pos));
				}
				out["tiles"] = std::move(tiles);
			}

			return out;
		}

		json toolHouseList(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			const int townFilter = readInt(params, "townId", -1, -1, 0x7FFFFFFF);
			const std::string nameFilter = as_lower_str(readString(params, "name"));

			json houses = json::array();
			for (const auto &[id, house] : map.houses) {
				if (!house) {
					continue;
				}
				if (townFilter >= 0 && house->townid != static_cast<uint32_t>(townFilter)) {
					continue;
				}
				if (!nameFilter.empty() && as_lower_str(house->name).find(nameFilter) == std::string::npos) {
					continue;
				}
				houses.push_back(houseToJson(house, map, false));
			}

			return jsonResult(json { { "count", houses.size() }, { "houses", std::move(houses) } });
		}

		json toolHouseGet(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			const int id = readInt(params, "id", 0, 1, 0x7FFFFFFF);
			const House* house = map.houses.getHouse(static_cast<uint32_t>(id));
			if (!house) {
				throw McpError(fmt::format("no house with id {}; use house_list to see the ids in this map", id));
			}
			return jsonResult(houseToJson(house, map, true));
		}

		json toolTownList(const json &) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			json towns = json::array();
			for (const auto &[id, town] : map.towns) {
				if (!town) {
					continue;
				}
				towns.push_back(json {
					{ "id", town->getID() },
					{ "name", town->getName() },
					{ "templePosition", positionToJson(town->getTemplePosition()) } });
			}

			return jsonResult(json { { "count", towns.size() }, { "towns", std::move(towns) } });
		}

		// ------------------------------------------------------------------
		// Waypoints and zones
		// ------------------------------------------------------------------

		json toolWaypointList(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			const OptionalBox box = parseOptionalBox(params);

			json waypoints = json::array();
			for (const auto &[key, waypoint] : map.waypoints.waypoints) {
				if (!waypoint || !box.contains(waypoint->pos)) {
					continue;
				}
				waypoints.push_back(json {
					{ "name", waypoint->name },
					{ "position", positionToJson(waypoint->pos) } });
			}

			return jsonResult(json { { "count", waypoints.size() }, { "waypoints", std::move(waypoints) } });
		}

		json toolZoneList(const json &) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			json zones = json::array();
			for (const auto &[name, id] : map.zones.zones) {
				zones.push_back(json { { "id", id }, { "name", name } });
			}

			return jsonResult(json {
				{ "count", zones.size() },
				{ "zones", std::move(zones) },
				{ "note", "use map_find with zoneId to locate the tiles assigned to a zone" } });
		}

		// ------------------------------------------------------------------
		// Spawns
		// ------------------------------------------------------------------

		// A spawn stores only its centre and radius; the creatures themselves
		// sit on the tiles inside that radius, so they have to be collected.
		json collectSpawnCreatures(Map &map, const Position &center, int radius, bool npcSpawn) {
			json creatures = json::array();

			for (int y = center.y - radius; y <= center.y + radius; ++y) {
				for (int x = center.x - radius; x <= center.x + radius; ++x) {
					if (x < 0 || y < 0 || x >= map.getWidth() || y >= map.getHeight()) {
						continue;
					}
					const Tile* tile = map.getTile(x, y, center.z);
					if (!tile) {
						continue;
					}

					if (npcSpawn) {
						if (tile->npc) {
							creatures.push_back(json {
								{ "name", tile->npc->getName() },
								{ "position", positionToJson(tile->getPosition()) },
								{ "spawnTime", tile->npc->getSpawnNpcTime() } });
						}
					} else {
						for (const Monster* monster : tile->monsters) {
							creatures.push_back(json {
								{ "name", monster->getName() },
								{ "position", positionToJson(tile->getPosition()) },
								{ "spawnTime", monster->getSpawnMonsterTime() },
								{ "weight", monster->getWeight() } });
						}
					}
				}
			}

			return creatures;
		}

		json toolSpawnList(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			const OptionalBox box = parseOptionalBox(params);
			const std::string kind = readString(params, "kind", "monster");
			if (kind != "monster" && kind != "npc" && kind != "both") {
				throw McpError("kind must be one of: monster, npc, both");
			}
			const int limit = readInt(params, "limit", 200, 1, 2000);
			const bool includeCreatures = params.value("includeCreatures", true);

			json spawns = json::array();
			int64_t total = 0;
			bool truncated = false;

			auto append = [&](const Position &pos, bool npcSpawn) {
				if (!box.contains(pos)) {
					return;
				}
				const Tile* tile = map.getTile(pos);
				if (!tile) {
					return;
				}
				const int radius = npcSpawn
					? (tile->spawnNpc ? tile->spawnNpc->getSize() : 0)
					: (tile->spawnMonster ? tile->spawnMonster->getSize() : 0);

				++total;
				if (static_cast<int>(spawns.size()) >= limit) {
					truncated = true;
					return;
				}

				json entry {
					{ "kind", npcSpawn ? "npc" : "monster" },
					{ "center", positionToJson(pos) },
					{ "radius", radius }
				};
				if (includeCreatures) {
					entry["creatures"] = collectSpawnCreatures(map, pos, radius, npcSpawn);
				}
				spawns.push_back(std::move(entry));
			};

			if (kind == "monster" || kind == "both") {
				for (const Position &pos : map.spawnsMonster) {
					append(pos, false);
				}
			}
			if (kind == "npc" || kind == "both") {
				for (const Position &pos : map.spawnsNpc) {
					append(pos, true);
				}
			}

			json out {
				{ "totalSpawns", total },
				{ "returned", spawns.size() },
				{ "spawns", std::move(spawns) }
			};
			if (truncated) {
				out["truncated"] = true;
				out["note"] = "raise limit or pass from/to to narrow the area";
			}
			return jsonResult(out);
		}

		// ------------------------------------------------------------------
		// Creature catalogues
		// ------------------------------------------------------------------

		json toolMonsterTypes(const json &params) {
			const std::string needle = as_lower_str(readString(params, "name"));
			const int limit = readInt(params, "limit", 200, 1, 5000);

			json monsters = json::array();
			int64_t total = 0;

			for (const auto &[name, type] : g_monsters) {
				if (!type) {
					continue;
				}
				if (!needle.empty() && as_lower_str(type->name).find(needle) == std::string::npos) {
					continue;
				}
				++total;
				if (static_cast<int>(monsters.size()) >= limit) {
					continue;
				}
				monsters.push_back(json {
					{ "name", type->name },
					{ "missing", type->missing },
					{ "hasBrush", type->brush != nullptr } });
			}

			return jsonResult(json {
				{ "total", total },
				{ "returned", monsters.size() },
				{ "monsters", std::move(monsters) },
				{ "note", "missing=true means the map references the monster but no definition was loaded" } });
		}

		json toolNpcTypes(const json &params) {
			const std::string needle = as_lower_str(readString(params, "name"));
			const int limit = readInt(params, "limit", 200, 1, 5000);

			json npcs = json::array();
			int64_t total = 0;

			for (const auto &[name, type] : g_npcs) {
				if (!type) {
					continue;
				}
				if (!needle.empty() && as_lower_str(type->name).find(needle) == std::string::npos) {
					continue;
				}
				++total;
				if (static_cast<int>(npcs.size()) >= limit) {
					continue;
				}
				npcs.push_back(json {
					{ "name", type->name },
					{ "missing", type->missing },
					{ "hasBrush", type->brush != nullptr } });
			}

			return jsonResult(json {
				{ "total", total },
				{ "returned", npcs.size() },
				{ "npcs", std::move(npcs) } });
		}

		json optionalRegionSchema() {
			json corner {
				{ "type", "object" },
				{ "properties", json { { "x", json { { "type", "integer" } } }, { "y", json { { "type", "integer" } } }, { "z", json { { "type", "integer" } } } } },
				{ "required", json::array({ "x", "y", "z" }) }
			};
			return json {
				{ "from", corner },
				{ "to", corner }
			};
		}

	} // namespace

	void registerEntityTools(ToolRegistry &registry) {
		registry.add({ "house_list",
					   "List the houses in the map, optionally filtered by town or name. Returns ids, rent, beds, exit and tile counts.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "townId", json { { "type", "integer" } } }, { "name", json { { "type", "string" }, { "description", "substring of the house name" } } } } } },
					   false,
					   toolHouseList });

		registry.add({ "house_get",
					   "Full detail for one house, including every tile position that belongs to it.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "id", json { { "type", "integer" } } } } },
						   { "required", json::array({ "id" }) } },
					   false,
					   toolHouseGet });

		registry.add({ "town_list",
					   "List the towns in the map with their ids and temple positions.",
					   json { { "type", "object" }, { "properties", json::object() } },
					   false,
					   toolTownList });

		registry.add({ "waypoint_list",
					   "List the map's waypoints and their positions. Pass from/to to only get waypoints inside that region.",
					   json { { "type", "object" }, { "properties", optionalRegionSchema() } },
					   false,
					   toolWaypointList });

		registry.add({ "zone_list",
					   "List the zones defined in the map with their ids and names.",
					   json { { "type", "object" }, { "properties", json::object() } },
					   false,
					   toolZoneList });

		registry.add({ "spawn_list",
					   "List monster and/or npc spawns with their centre, radius and the creatures placed inside that radius. Pass from/to to restrict it to a region.",
					   json {
						   { "type", "object" },
						   { "properties", [] {
								json properties = optionalRegionSchema();
								properties["kind"] = json { { "type", "string" }, { "enum", json::array({ "monster", "npc", "both" }) }, { "description", "default monster" } };
								properties["includeCreatures"] = json { { "type", "boolean" }, { "description", "default true; set false for a cheaper listing" } };
								properties["limit"] = json { { "type", "integer" }, { "description", "max spawns returned, default 200" } };
								return properties;
							}() } },
					   false, toolSpawnList });

		registry.add({ "monster_types_list",
					   "List the monster types available in this editor installation, so you know which names can legally be placed on the map.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "name", json { { "type", "string" }, { "description", "substring filter" } } }, { "limit", json { { "type", "integer" }, { "description", "default 200" } } } } } },
					   false,
					   toolMonsterTypes });

		registry.add({ "npc_types_list",
					   "List the npc types available in this editor installation.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "name", json { { "type", "string" }, { "description", "substring filter" } } }, { "limit", json { { "type", "integer" }, { "description", "default 200" } } } } } },
					   false,
					   toolNpcTypes });
	}

} // namespace mcp
