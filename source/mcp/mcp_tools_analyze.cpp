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
//
// Diagnostics: what is wrong with the map, and can a player actually get
// there. These read the map and never change it.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "mcp_tools.h"

#include "../basemap.h"
#include "../complexitem.h"
#include <cstdlib>
#include "../const.h"
#include "../editor.h"
#include "../gui.h"
#include "../house.h"
#include "../item.h"
#include "../items.h"
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

#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mcp {

	namespace {

		// A validation issue keeps a few example positions rather than every
		// hit, so a map with 40000 broken tiles still returns a usable report.
		constexpr size_t MAX_SAMPLES = 20;

		struct Issue {
			int64_t count = 0;
			json samples = json::array();

			void hit(const Position &position) {
				++count;
				if (samples.size() < MAX_SAMPLES) {
					samples.push_back(positionToJson(position));
				}
			}
		};

		void addIssue(json &out, const char* id, const char* severity, const char* description, const Issue &issue, const char* fix) {
			if (issue.count == 0) {
				return;
			}
			json entry {
				{ "id", id },
				{ "severity", severity },
				{ "description", description },
				{ "count", issue.count },
				{ "howToFix", fix }
			};
			if (!issue.samples.empty()) {
				entry["samplePositions"] = issue.samples;
			}
			out.push_back(std::move(entry));
		}

		// Entity-level problems are identified by id, not by a tile position.
		struct IdIssue {
			int64_t count = 0;
			json ids = json::array();

			void hit(uint32_t id) {
				++count;
				if (ids.size() < MAX_SAMPLES) {
					ids.push_back(id);
				}
			}
		};

		void addIdIssue(json &out, const char* id, const char* severity, const char* description, const IdIssue &issue, const char* fix) {
			if (issue.count == 0) {
				return;
			}
			out.push_back(json {
				{ "id", id },
				{ "severity", severity },
				{ "description", description },
				{ "count", issue.count },
				{ "sampleIds", issue.ids },
				{ "howToFix", fix } });
		}

		uint64_t positionKey(const Position &position) {
			return (static_cast<uint64_t>(position.z) << 48) | (static_cast<uint64_t>(position.y) << 24) | static_cast<uint64_t>(position.x);
		}

		json toolMapValidate(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			Issue itemsWithoutGround;
			Issue stackedGrounds;
			Issue duplicateItems;
			Issue wallsUponWalls;
			Issue spawnWithoutCreature;
			Issue creatureWithoutSpawn;
			Issue houseTileWithoutGround;
			Issue houseTileUnknownHouse;
			Issue houseTileWithoutPz;
			Issue missingMonsters;
			Issue spawnInProtectionZone;
			Issue creatureOutsideSpawnRadius;

			std::map<uint16_t, std::vector<Position>> uniqueIdOwners;
			std::set<std::string> missingMonsterNames;
			std::map<uint32_t, int64_t> houseBedCounts;
			std::map<uint32_t, std::vector<uint8_t>> houseDoorIds;

			auto visit = [&](TileLocation* location) {
				Tile* tile = location ? location->get() : nullptr;
				if (!tile || tile->empty()) {
					return;
				}
				const Position &position = tile->getPosition();

				if (!tile->ground && !tile->items.empty()) {
					itemsWithoutGround.hit(position);
				}

				if (tile->house_id != 0) {
					if (!tile->ground) {
						houseTileWithoutGround.hit(position);
					}
					if (!map.houses.getHouse(tile->house_id)) {
						houseTileUnknownHouse.hit(position);
					}
					// House tiles are expected to be protection zone; without
					// it players can fight inside a rented house.
					if (!tile->isPZ()) {
						houseTileWithoutPz.hit(position);
					}
					// Seed the entries so a house with tiles but no beds or
					// doors is distinguishable from one never scanned.
					houseBedCounts.try_emplace(tile->house_id, 0);
					houseDoorIds.try_emplace(tile->house_id);

					for (const Item* item : tile->items) {
						if (item->getItemType().isBed()) {
							++houseBedCounts[tile->house_id];
						}
						if (const auto* door = dynamic_cast<const Door*>(item)) {
							houseDoorIds[tile->house_id].push_back(door->getDoorID());
						}
					}
				}

				// A monster spawn inside a protection zone never spawns
				// anything, and the mistake is invisible in the editor.
				if (tile->spawnMonster && tile->isPZ()) {
					spawnInProtectionZone.hit(position);
				}

				// A spawn with nothing to spawn, and creatures with no spawn to
				// hold them, are both silently broken in game.
				const bool hasCreature = !tile->monsters.empty() || tile->npc;
				if ((tile->spawnMonster && tile->monsters.empty()) || (tile->spawnNpc && !tile->npc)) {
					spawnWithoutCreature.hit(position);
				}
				if (hasCreature && !tile->spawnMonster && !tile->spawnNpc) {
					// The spawn may sit on a nearby tile; only flag it when no
					// spawn covers this position at all.
					if (map.getSpawnMonsterList(position).empty() && map.getSpawnNpcList(position).empty()) {
						creatureWithoutSpawn.hit(position);
					}
				}

				for (const Monster* monster : tile->monsters) {
					MonsterType* type = g_monsters[monster->getName()];
					if (!type || type->missing) {
						missingMonsters.hit(position);
						missingMonsterNames.insert(monster->getName());
					}
				}
				if (tile->npc) {
					NpcType* type = g_npcs[tile->npc->getName()];
					if (!type || type->missing) {
						missingMonsters.hit(position);
						missingMonsterNames.insert(tile->npc->getName());
					}
				}

				int groundLikeCount = tile->ground ? 1 : 0;
				int wallCount = 0;
				std::set<uint16_t> seen;

				for (const Item* item : tile->items) {
					const ItemType &type = item->getItemType();
					if (type.isGroundTile()) {
						++groundLikeCount;
					}
					if (type.isWall && !type.isBrushDoor) {
						++wallCount;
					}
					if (!seen.insert(item->getID()).second) {
						duplicateItems.hit(position);
					}
					if (item->getUniqueID() != 0) {
						uniqueIdOwners[item->getUniqueID()].push_back(position);
					}
				}

				if (groundLikeCount > 1) {
					stackedGrounds.hit(position);
				}
				if (wallCount > 1) {
					wallsUponWalls.hit(position);
				}
			};
			map.forEachTileLocation(visit);

			// Entity-level checks, which do not need a tile walk.
			IdIssue houseWithoutExit;
			IdIssue houseWithoutTiles;
			IdIssue houseUnknownTown;

			for (const auto &[houseId, house] : map.houses) {
				if (!house) {
					continue;
				}
				if (!house->getExit().isValid()) {
					houseWithoutExit.hit(houseId);
				}
				if (house->size() == 0) {
					houseWithoutTiles.hit(houseId);
				}
				if (house->townid != 0 && !map.towns.getTown(house->townid)) {
					houseUnknownTown.hit(houseId);
				}
			}

			// Creatures are only respawned by a spawn that covers their tile.
			for (const Position &spawnPosition : map.spawnsMonster) {
				const Tile* spawnTile = map.getTile(spawnPosition);
				if (!spawnTile || !spawnTile->spawnMonster) {
					continue;
				}
				const int radius = spawnTile->spawnMonster->getSize();
				// Look one ring beyond the radius: creatures just outside are
				// the mistake worth reporting.
				for (int y = -radius - 2; y <= radius + 2; ++y) {
					for (int x = -radius - 2; x <= radius + 2; ++x) {
						if (std::abs(x) <= radius && std::abs(y) <= radius) {
							continue;
						}
						const Position nearby = spawnPosition + Position(x, y, 0);
						const Tile* tile = map.getTile(nearby);
						if (!tile || tile->monsters.empty()) {
							continue;
						}
						// Only a complaint when no spawn at all covers it.
						if (map.getSpawnMonsterList(nearby).empty()) {
							creatureOutsideSpawnRadius.hit(nearby);
						}
					}
				}
			}

			IdIssue houseWithoutDoor;
			IdIssue houseDuplicateDoorIds;
			IdIssue houseBedMismatch;
			json bedDetails = json::array();

			for (const auto &[houseId, house] : map.houses) {
				if (!house || house->size() == 0) {
					continue;
				}

				const auto doors = houseDoorIds.find(houseId);
				if (doors == houseDoorIds.end() || doors->second.empty()) {
					// A house with no door cannot be entered or auctioned.
					houseWithoutDoor.hit(houseId);
				} else {
					std::set<uint8_t> seenDoorIds;
					for (const uint8_t doorId : doors->second) {
						if (doorId != 0 && !seenDoorIds.insert(doorId).second) {
							houseDuplicateDoorIds.hit(houseId);
							break;
						}
					}
				}

				const int64_t actualBeds = houseBedCounts.count(houseId) ? houseBedCounts[houseId] : 0;
				if (actualBeds != house->beds) {
					houseBedMismatch.hit(houseId);
					if (bedDetails.size() < MAX_SAMPLES) {
						bedDetails.push_back(json { { "houseId", houseId }, { "declaredBeds", house->beds }, { "bedItemsFound", actualBeds } });
					}
				}
			}

			Issue waypointOnEmptyTile;
			for (const auto &[key, waypoint] : map.waypoints.waypoints) {
				if (!waypoint) {
					continue;
				}
				const Tile* tile = map.getTile(waypoint->pos);
				if (!tile || tile->empty()) {
					waypointOnEmptyTile.hit(waypoint->pos);
				}
			}

			Issue duplicateUniqueIds;
			json duplicatedUids = json::array();
			for (const auto &[uid, positions] : uniqueIdOwners) {
				if (positions.size() > 1) {
					duplicateUniqueIds.count += static_cast<int64_t>(positions.size());
					if (duplicatedUids.size() < MAX_SAMPLES) {
						duplicatedUids.push_back(json { { "uniqueId", uid }, { "occurrences", positions.size() } });
					}
					for (const Position &position : positions) {
						if (duplicateUniqueIds.samples.size() < MAX_SAMPLES) {
							duplicateUniqueIds.samples.push_back(positionToJson(position));
						}
					}
				}
			}

			json issues = json::array();
			addIssue(issues, "items_without_ground", "error", "Tiles that hold items but have no ground; the client cannot render them.", itemsWithoutGround, "Add a ground with tile_edit, or clear the tile.");
			addIssue(issues, "stacked_grounds", "warning", "More than one ground-type item on the same tile.", stackedGrounds, "Remove the extra ground with tile_edit removeItemIds.");
			addIssue(issues, "duplicate_items", "warning", "The same item id appears more than once on a tile.", duplicateItems, "Run map_cleanup with removeDuplicateItems.");
			addIssue(issues, "walls_upon_walls", "warning", "Two or more wall items stacked on one tile.", wallsUponWalls, "Remove the extra wall, or redraw the area with brush_apply.");
			addIssue(issues, "spawn_without_creature", "error", "A spawn exists but has no creature inside its radius.", spawnWithoutCreature, "Add a creature with spawn_manage add_creature, or delete the spawn.");
			addIssue(issues, "creature_without_spawn", "error", "A creature sits on a tile that no spawn covers; it will never appear in game.", creatureWithoutSpawn, "Create a spawn with spawn_manage create.");
			addIssue(issues, "missing_creature_type", "error", "The map references a creature this installation has no definition for.", missingMonsters, "Import the creature definitions, or replace the creature.");
			addIssue(issues, "house_tile_without_ground", "error", "A house tile has no ground.", houseTileWithoutGround, "Add a ground with tile_edit.");
			addIssue(issues, "house_tile_unknown_house", "error", "A tile points at a house id that does not exist.", houseTileUnknownHouse, "Run map_cleanup with clearInvalidHouses, or reassign with house_manage.");
			addIssue(issues, "house_tile_without_pz", "warning", "A house tile is not flagged as protection zone, so players can fight inside it.", houseTileWithoutPz, "Set protectionZone on those tiles with tile_edit flags.");
			addIssue(issues, "spawn_in_protection_zone", "error", "A monster spawn sits inside a protection zone and will never spawn anything.", spawnInProtectionZone, "Move the spawn out of the PZ, or clear the protectionZone flag on that tile.");
			addIssue(issues, "creature_outside_spawn_radius", "error", "A creature stands outside any spawn radius, so it is never respawned.", creatureOutsideSpawnRadius, "Grow the spawn radius with spawn_manage update, or move the creature inside it.");
			addIdIssue(issues, "house_without_exit", "error", "A house has no valid exit position.", houseWithoutExit, "Set one with house_manage set_exit.");
			addIdIssue(issues, "house_without_tiles", "warning", "A house has no tiles assigned.", houseWithoutTiles, "Assign tiles with house_manage assign_tiles, or delete the house.");
			addIdIssue(issues, "house_unknown_town", "warning", "A house points at a town id that does not exist.", houseUnknownTown, "Fix it with house_manage update, or create the town.");
			addIdIssue(issues, "house_without_door", "error", "A house has tiles but no door, so it cannot be entered or auctioned.", houseWithoutDoor, "Place a door on a house tile with tile_edit, giving it a doorId.");
			addIdIssue(issues, "house_duplicate_door_ids", "error", "A house has two doors sharing the same doorId.", houseDuplicateDoorIds, "Give each door its own doorId with tile_edit.");
			addIdIssue(issues, "house_bed_count_mismatch", "warning", "The house bed count does not match the bed items actually on its tiles.", houseBedMismatch, "Fix the count with house_manage update, or add/remove beds.");
			addIssue(issues, "waypoint_on_empty_tile", "warning", "A waypoint points at a tile with nothing on it.", waypointOnEmptyTile, "Move it with waypoint_manage move, or delete it.");
			addIssue(issues, "duplicate_unique_ids", "error", "The same unique id is used by more than one item.", duplicateUniqueIds, "Give each item its own unique id with tile_edit.");

			int64_t errors = 0;
			int64_t warnings = 0;
			for (const json &issue : issues) {
				if (issue["severity"] == "error") {
					++errors;
				} else {
					++warnings;
				}
			}

			json out {
				{ "mapName", map.getName() },
				{ "tilesScanned", map.getTileCount() },
				{ "errorKinds", errors },
				{ "warningKinds", warnings },
				{ "issues", std::move(issues) }
			};

			if (!missingMonsterNames.empty()) {
				out["missingCreatureNames"] = json(missingMonsterNames);
			}
			if (!bedDetails.empty()) {
				out["bedCountDetails"] = std::move(bedDetails);
			}
			if (errors == 0 && warnings == 0) {
				out["summary"] = "No problems found.";
			}

			return jsonResult(out);
		}

		// ------------------------------------------------------------------
		// Walkability
		// ------------------------------------------------------------------

		bool isWalkable(const Map &map, const Position &position) {
			const Tile* tile = map.getTile(position);
			if (!tile || !tile->ground) {
				return false;
			}
			return !tile->isBlocking();
		}

		// Flood fill over walkable tiles on one floor. Floor changes are a
		// separate concern (floor_transitions) - mixing them in would make a
		// "can I reach it" answer depend on stair semantics the editor does not
		// model.
		struct FloodResult {
			std::unordered_set<uint64_t> visited;
			bool reachedGoal = false;
			std::vector<Position> path;
			bool hitLimit = false;
		};

		FloodResult floodFill(const Map &map, const Position &start, const Position* goal, int64_t maxTiles) {
			FloodResult result;
			if (!isWalkable(map, start)) {
				return result;
			}

			std::unordered_map<uint64_t, Position> cameFrom;
			std::deque<Position> queue;

			queue.push_back(start);
			result.visited.insert(positionKey(start));

			static constexpr int DX[8] = { 0, 0, -1, 1, -1, 1, -1, 1 };
			static constexpr int DY[8] = { -1, 1, 0, 0, -1, -1, 1, 1 };

			while (!queue.empty()) {
				const Position current = queue.front();
				queue.pop_front();

				if (goal && current == *goal) {
					result.reachedGoal = true;
					// Walk the parent chain back to the start.
					Position step = current;
					while (!(step == start)) {
						result.path.push_back(step);
						step = cameFrom[positionKey(step)];
					}
					result.path.push_back(start);
					std::reverse(result.path.begin(), result.path.end());
					return result;
				}

				if (static_cast<int64_t>(result.visited.size()) >= maxTiles) {
					result.hitLimit = true;
					return result;
				}

				for (int i = 0; i < 8; ++i) {
					const Position next(current.x + DX[i], current.y + DY[i], current.z);
					if (next.x < 0 || next.y < 0 || next.x >= map.getWidth() || next.y >= map.getHeight()) {
						continue;
					}
					const uint64_t key = positionKey(next);
					if (result.visited.count(key) || !isWalkable(map, next)) {
						continue;
					}
					result.visited.insert(key);
					cameFrom[key] = current;
					queue.push_back(next);
				}
			}

			return result;
		}

		json toolPathCheck(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			if (!params.contains("from")) {
				throw McpError("from is required");
			}
			const Position from = parsePosition(params["from"], "from");
			const int64_t maxTiles = readInt(params, "maxTiles", 100000, 100, 1000000);

			if (!isWalkable(map, from)) {
				const Tile* tile = map.getTile(from);
				return jsonResult(json {
					{ "from", positionToJson(from) },
					{ "startWalkable", false },
					{ "reason", !tile || !tile->ground ? "the start tile has no ground" : "the start tile is blocking" } });
			}

			if (params.contains("to")) {
				const Position to = parsePosition(params["to"], "to");
				if (to.z != from.z) {
					throw McpError("path_check works on one floor: from.z and to.z must match. Use floor_transitions to find the stairs between floors.");
				}

				const FloodResult result = floodFill(map, from, &to, maxTiles);

				json out {
					{ "from", positionToJson(from) },
					{ "to", positionToJson(to) },
					{ "startWalkable", true },
					{ "reachable", result.reachedGoal }
				};
				if (result.reachedGoal) {
					json path = json::array();
					for (const Position &step : result.path) {
						path.push_back(positionToJson(step));
					}
					out["steps"] = result.path.size();
					out["path"] = std::move(path);
				} else {
					out["tilesExplored"] = result.visited.size();
					out["reason"] = result.hitLimit
						? "gave up after maxTiles; the areas may still be connected"
						: "the destination is not walkable, or is sealed off from the start";
					if (!isWalkable(map, to)) {
						out["destinationWalkable"] = false;
					}
				}
				return jsonResult(out);
			}

			// No destination: report the enclosed area, which is how you check
			// whether a room, arena or dungeon is actually sealed.
			const FloodResult result = floodFill(map, from, nullptr, maxTiles);

			int minX = from.x;
			int maxX = from.x;
			int minY = from.y;
			int maxY = from.y;
			for (const uint64_t key : result.visited) {
				const int x = static_cast<int>(key & 0xFFFFFF);
				const int y = static_cast<int>((key >> 24) & 0xFFFFFF);
				minX = std::min(minX, x);
				maxX = std::max(maxX, x);
				minY = std::min(minY, y);
				maxY = std::max(maxY, y);
			}

			return jsonResult(json {
				{ "from", positionToJson(from) },
				{ "startWalkable", true },
				{ "reachableTiles", result.visited.size() },
				{ "sealed", !result.hitLimit },
				{ "bounds", json { { "from", json { { "x", minX }, { "y", minY }, { "z", from.z } } }, { "to", json { { "x", maxX }, { "y", maxY }, { "z", from.z } } } } },
				{ "note", result.hitLimit ? "hit maxTiles, so this area is open to the rest of the map (or very large)" : "the walkable area is fully enclosed; these are all the tiles reachable from the start" } });
		}

		// ------------------------------------------------------------------
		// Floor transitions
		// ------------------------------------------------------------------

		json toolFloorTransitions(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			if (!params.contains("from") || !params.contains("to")) {
				throw McpError("from and to are required, marking two corners of the region to scan");
			}
			const Position a = parsePosition(params["from"], "from");
			const Position b = parsePosition(params["to"], "to");

			const int minX = std::min(a.x, b.x);
			const int maxX = std::max(a.x, b.x);
			const int minY = std::min(a.y, b.y);
			const int maxY = std::max(a.y, b.y);
			const int minZ = std::min(a.z, b.z);
			const int maxZ = std::max(a.z, b.z);

			const int64_t volume = static_cast<int64_t>(maxX - minX + 1) * (maxY - minY + 1) * (maxZ - minZ + 1);
			if (volume > MAX_REGION_VOLUME) {
				throw McpError(fmt::format("the region covers {} tiles, above the {} limit; narrow it", volume, MAX_REGION_VOLUME));
			}

			const int limit = readInt(params, "limit", 500, 1, 5000);
			json transitions = json::array();
			int64_t total = 0;

			for (int z = minZ; z <= maxZ; ++z) {
				for (int y = minY; y <= maxY; ++y) {
					for (int x = minX; x <= maxX; ++x) {
						const Tile* tile = map.getTile(x, y, z);
						if (!tile) {
							continue;
						}

						auto inspect = [&](const Item* item) {
							if (!item) {
								return;
							}
							const ItemType &type = item->getItemType();
							if (!type.isFloorChange()) {
								return;
							}

							++total;
							if (static_cast<int>(transitions.size()) >= limit) {
								return;
							}

							// floorChangeDown goes to z+1 (deeper); the
							// directional ones go up to z-1 on the neighbour.
							std::string direction = "unknown";
							Position target(x, y, z);
							if (type.floorChangeDown) {
								direction = "down";
								target = Position(x, y, z + 1);
							} else if (type.floorChangeNorth) {
								direction = "up-north";
								target = Position(x, y - 1, z - 1);
							} else if (type.floorChangeSouth) {
								direction = "up-south";
								target = Position(x, y + 1, z - 1);
							} else if (type.floorChangeEast) {
								direction = "up-east";
								target = Position(x + 1, y, z - 1);
							} else if (type.floorChangeWest) {
								direction = "up-west";
								target = Position(x - 1, y, z - 1);
							}

							const bool targetValid = target.z >= 0 && target.z < rme::MapLayers && target.x >= 0 && target.y >= 0 && target.x < map.getWidth() && target.y < map.getHeight();
							const Tile* targetTile = targetValid ? map.getTile(target) : nullptr;

							// A stair with no way back is the classic dungeon
							// bug: the player drops in and is stuck. Look for
							// any floor change on or beside the landing tile
							// that would bring them back to this floor.
							bool hasReturn = false;
							if (targetValid) {
								for (int ny = -1; ny <= 1 && !hasReturn; ++ny) {
									for (int nx = -1; nx <= 1 && !hasReturn; ++nx) {
										const Tile* neighbour = map.getTile(target.x + nx, target.y + ny, target.z);
										if (!neighbour) {
											continue;
										}
										auto leadsBack = [z](const Item* candidate) {
											if (!candidate) {
												return false;
											}
											const ItemType &candidateType = candidate->getItemType();
											if (!candidateType.isFloorChange()) {
												return false;
											}
											// Down goes deeper, the directional
											// ones go up; either can be the
											// return leg depending on which way
											// this transition went.
											return true;
										};
										if (leadsBack(neighbour->ground)) {
											hasReturn = true;
										}
										for (const Item* candidate : neighbour->items) {
											if (leadsBack(candidate)) {
												hasReturn = true;
												break;
											}
										}
									}
								}
							}

							json entry {
								{ "position", positionToJson(Position(x, y, z)) },
								{ "itemId", item->getID() },
								{ "itemName", item->getName() },
								{ "direction", direction },
								{ "target", positionToJson(target) },
								{ "targetHasGround", targetTile && targetTile->ground },
								{ "targetWalkable", targetValid && isWalkable(map, target) },
								{ "hasReturnRoute", hasReturn }
							};
							if (targetValid && !hasReturn) {
								entry["problem"] = "nothing at or beside the landing tile leads back to another floor";
							}
							transitions.push_back(std::move(entry));
						};

						inspect(tile->ground);
						for (const Item* item : tile->items) {
							inspect(item);
						}
					}
				}
			}

			return jsonResult(json {
				{ "total", total },
				{ "returned", transitions.size() },
				{ "transitions", std::move(transitions) },
				{ "note", "targetWalkable=false means the stair leads nowhere: the landing tile is missing or blocked. hasReturnRoute=false means a player who takes it cannot get back, which is the classic dungeon dead end." } });
		}

		// ------------------------------------------------------------------
		// Statistics
		// ------------------------------------------------------------------

		json toolMapStatistics(const json &) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			int64_t tiles = 0;
			int64_t items = 0;
			int64_t grounds = 0;
			int64_t blocking = 0;
			int64_t containers = 0;
			int64_t withActionId = 0;
			int64_t withUniqueId = 0;
			int64_t withText = 0;
			int64_t monsters = 0;
			int64_t npcs = 0;
			int64_t houseTiles = 0;
			std::map<int, int64_t> tilesPerFloor;
			std::map<std::string, int64_t> monsterCounts;

			auto visit = [&](TileLocation* location) {
				Tile* tile = location ? location->get() : nullptr;
				if (!tile || tile->empty()) {
					return;
				}
				++tiles;
				++tilesPerFloor[tile->getZ()];
				if (tile->ground) {
					++grounds;
					++items;
				}
				if (tile->isBlocking()) {
					++blocking;
				}
				if (tile->house_id != 0) {
					++houseTiles;
				}
				for (const Item* item : tile->items) {
					++items;
					if (dynamic_cast<const Container*>(item)) {
						++containers;
					}
					if (item->getActionID() != 0) {
						++withActionId;
					}
					if (item->getUniqueID() != 0) {
						++withUniqueId;
					}
					if (!item->getText().empty()) {
						++withText;
					}
				}
				monsters += static_cast<int64_t>(tile->monsters.size());
				for (const Monster* monster : tile->monsters) {
					++monsterCounts[monster->getName()];
				}
				if (tile->npc) {
					++npcs;
				}
			};
			map.forEachTileLocation(visit);

			json floors = json::array();
			for (const auto &[z, count] : tilesPerFloor) {
				floors.push_back(json { { "z", z }, { "tiles", count } });
			}

			// Top creatures by count; the full list is what monster_types_list
			// is for.
			std::vector<std::pair<std::string, int64_t>> ranked(monsterCounts.begin(), monsterCounts.end());
			std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
				return a.second > b.second;
			});
			json topMonsters = json::array();
			for (size_t i = 0; i < ranked.size() && i < 25; ++i) {
				topMonsters.push_back(json { { "name", ranked[i].first }, { "count", ranked[i].second } });
			}

			return jsonResult(json {
				{ "mapName", map.getName() },
				{ "dimensions", json { { "width", map.getWidth() }, { "height", map.getHeight() } } },
				{ "tiles", tiles },
				{ "items", items },
				{ "grounds", grounds },
				{ "blockingTiles", blocking },
				{ "containers", containers },
				{ "itemsWithActionId", withActionId },
				{ "itemsWithUniqueId", withUniqueId },
				{ "itemsWithText", withText },
				{ "monsters", monsters },
				{ "npcs", npcs },
				{ "houseTiles", houseTiles },
				{ "distinctMonsterTypes", monsterCounts.size() },
				{ "houses", map.houses.count() },
				{ "towns", map.towns.count() },
				{ "waypoints", map.waypoints.waypoints.size() },
				{ "zones", map.zones.zones.size() },
				{ "tilesPerFloor", std::move(floors) },
				{ "topMonsters", std::move(topMonsters) } });
		}

		// Teleports are the backbone of quest routing and the easiest thing to
		// get silently wrong: a destination left at 0,0,0, or pointing at a
		// tile that has no ground.
		json toolTeleportGraph(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			const int limit = readInt(params, "limit", 500, 1, 5000);
			const bool onlyBroken = params.value("onlyBroken", false);

			json teleports = json::array();
			int64_t total = 0;
			int64_t broken = 0;

			auto visit = [&](TileLocation* location) {
				Tile* tile = location ? location->get() : nullptr;
				if (!tile) {
					return;
				}

				auto inspect = [&](const Item* item) {
					const auto* teleport = dynamic_cast<const Teleport*>(item);
					if (!teleport) {
						return;
					}

					const Position &destination = teleport->getDestination();
					const Tile* target = teleport->hasDestination() ? map.getTile(destination) : nullptr;

					std::string problem;
					if (!teleport->hasDestination()) {
						problem = "no destination set";
					} else if (!target || target->empty()) {
						problem = "destination tile is empty";
					} else if (!target->ground) {
						problem = "destination tile has no ground";
					} else if (target->isBlocking()) {
						problem = "destination tile is blocking";
					}

					if (!problem.empty()) {
						++broken;
					} else if (onlyBroken) {
						++total;
						return;
					}

					++total;
					if (static_cast<int>(teleports.size()) >= limit) {
						return;
					}

					json entry {
						{ "from", positionToJson(tile->getPosition()) },
						{ "to", positionToJson(destination) },
						{ "itemId", item->getID() }
					};
					if (!problem.empty()) {
						entry["problem"] = problem;
					}
					teleports.push_back(std::move(entry));
				};

				inspect(tile->ground);
				for (const Item* item : tile->items) {
					inspect(item);
				}
			};
			map.forEachTileLocation(visit);

			return jsonResult(json {
				{ "total", total },
				{ "broken", broken },
				{ "returned", teleports.size() },
				{ "teleports", std::move(teleports) },
				{ "note", "set the destination of a teleport with tile_edit, passing destination on the item" } });
		}

	} // namespace

	void registerAnalyzeTools(ToolRegistry &registry) {
		registry.add({ "map_validate",
					   "Scan the whole map for problems and return a report: tiles with items but no ground, stacked grounds, "
					   "duplicate items, walls on walls, spawns with no creature and creatures with no spawn, creatures this "
					   "installation cannot resolve, broken house and town links, waypoints on empty tiles, and duplicate unique ids. "
					   "Run this before and after editing. It only reports - map_cleanup is what applies fixes.",
					   json { { "type", "object" }, { "properties", json::object() } },
					   false,
					   toolMapValidate });

		registry.add({ "path_check",
					   "Walkability analysis on one floor. With from and to, answers whether a player can walk between them and "
					   "returns the path. With only from, flood-fills the walkable area and reports whether it is sealed - "
					   "which is how you check that a boss arena, a quest room or a dungeon has no leaks and no unreachable parts.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "from", positionSchema() }, { "to", positionSchema() }, { "maxTiles", json { { "type", "integer" }, { "description", "exploration budget, default 100000" } } } } },
						   { "required", json::array({ "from" }) } },
					   false,
					   toolPathCheck });

		registry.add({ "floor_transitions",
					   "Find the stairs, ladders and holes in a region and where each one leads, flagging the ones whose "
					   "destination has no ground or is blocked. Use it when designing multi-floor dungeons.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "from", positionSchema() }, { "to", positionSchema() }, { "limit", json { { "type", "integer" }, { "description", "default 500" } } } } },
						   { "required", json::array({ "from", "to" }) } },
					   false,
					   toolFloorTransitions });

		registry.add({ "teleport_graph",
					   "Every teleport in the map with its destination, flagging the ones that lead nowhere: "
					   "no destination set, or a destination that is empty, groundless or blocking. "
					   "This is how you verify a quest's routing without walking it in game.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "onlyBroken", json { { "type", "boolean" }, { "description", "list only the teleports with a problem" } } }, { "limit", json { { "type", "integer" }, { "description", "default 500" } } } } } },
					   false,
					   toolTeleportGraph });

		registry.add({ "map_statistics",
					   "Counts across the whole map: tiles, items, grounds, blocking tiles, containers, items carrying action/unique ids "
					   "or text, creatures, house tiles, tiles per floor, and the most common monsters.",
					   json { { "type", "object" }, { "properties", json::object() } },
					   false,
					   toolMapStatistics });
	}

} // namespace mcp
