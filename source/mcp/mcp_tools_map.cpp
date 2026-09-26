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

#include "../basemap.h"
#include "../brush.h"
#include "../common.h"
#include "../const.h"
#include "../editor.h"
#include "../gui.h"
#include "../house.h"
#include "../item.h"
#include "../items.h"
#include "../map.h"
#include "../monster.h"
#include "../npc.h"
#include "../position.h"
#include "../selection.h"
#include "../spawn_monster.h"
#include "../spawn_npc.h"
#include "../tile.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace mcp {

	namespace {

		// ------------------------------------------------------------------
		// Region parsing
		// ------------------------------------------------------------------

		struct Region {
			int minX = 0, minY = 0, minZ = 0;
			int maxX = 0, maxY = 0, maxZ = 0;

			int64_t volume() const {
				return static_cast<int64_t>(maxX - minX + 1) * (maxY - minY + 1) * (maxZ - minZ + 1);
			}
			int width() const {
				return maxX - minX + 1;
			}
			int height() const {
				return maxY - minY + 1;
			}
		};

		// Accepts the corners in any order, so "from" does not have to be the
		// top-left; the LLM can pass the two corners it happens to know.
		Region parseRegion(const json &params) {
			if (!params.contains("from") || !params.contains("to")) {
				throw McpError("this tool requires the from and to fields, each {\"x\":..,\"y\":..,\"z\":..}");
			}

			const Position a = parsePosition(params["from"], "from");
			const Position b = parsePosition(params["to"], "to");

			Region region;
			region.minX = std::min(a.x, b.x);
			region.maxX = std::max(a.x, b.x);
			region.minY = std::min(a.y, b.y);
			region.maxY = std::max(a.y, b.y);
			region.minZ = std::min(a.z, b.z);
			region.maxZ = std::max(a.z, b.z);
			return region;
		}

		void requireScannableVolume(const Region &region, const char* hint) {
			const int64_t volume = region.volume();
			if (volume > MAX_REGION_VOLUME) {
				throw McpError(fmt::format(
					"the region covers {} tiles, above the {} limit. {}",
					volume, MAX_REGION_VOLUME, hint
				));
			}
		}

		json regionToJson(const Region &region) {
			return json {
				{ "from", json { { "x", region.minX }, { "y", region.minY }, { "z", region.minZ } } },
				{ "to", json { { "x", region.maxX }, { "y", region.maxY }, { "z", region.maxZ } } },
				{ "width", region.width() },
				{ "height", region.height() },
				{ "floors", region.maxZ - region.minZ + 1 }
			};
		}

		// ------------------------------------------------------------------
		// Serialization
		// ------------------------------------------------------------------


		json tileToJson(const Tile* tile) {
			json out { { "position", positionToJson(tile->getPosition()) } };

			if (tile->ground) {
				out["ground"] = itemToJson(tile->ground);
			}

			if (!tile->items.empty()) {
				json items = json::array();
				for (const Item* item : tile->items) {
					items.push_back(itemToJson(item));
				}
				out["items"] = std::move(items);
			}

			json flags = json::object();
			if (tile->isPZ()) {
				flags["protectionZone"] = true;
			}
			if (tile->isPVP()) {
				flags["pvpZone"] = true;
			}
			if (tile->isNoPVP()) {
				flags["noPvp"] = true;
			}
			if (tile->isNoLogout()) {
				flags["noLogout"] = true;
			}
			if (tile->isBlocking()) {
				flags["blocking"] = true;
			}
			if (!flags.empty()) {
				out["flags"] = std::move(flags);
			}

			if (tile->house_id != 0) {
				out["houseId"] = tile->house_id;
			}
			if (tile->isHouseExit()) {
				out["houseExit"] = true;
			}
			if (!tile->zones.empty()) {
				out["zones"] = json(tile->zones);
			}

			if (tile->spawnMonster) {
				out["monsterSpawnRadius"] = tile->spawnMonster->getSize();
			}
			if (tile->spawnNpc) {
				out["npcSpawnRadius"] = tile->spawnNpc->getSize();
			}

			if (!tile->monsters.empty()) {
				json monsters = json::array();
				for (const Monster* monster : tile->monsters) {
					monsters.push_back(json {
						{ "name", monster->getName() },
						{ "spawnTime", monster->getSpawnMonsterTime() },
						{ "weight", monster->getWeight() },
						{ "direction", Monster::DirID2Name(static_cast<uint16_t>(monster->getDirection())) } });
				}
				out["monsters"] = std::move(monsters);
			}

			if (tile->npc) {
				out["npc"] = json {
					{ "name", tile->npc->getName() },
					{ "spawnTime", tile->npc->getSpawnNpcTime() },
					{ "direction", Npc::DirID2Name(static_cast<uint16_t>(tile->npc->getDirection())) }
				};
			}

			return out;
		}

		// ------------------------------------------------------------------
		// Region modes
		// ------------------------------------------------------------------

		// Orientation info an LLM needs before asking for tile detail: what is
		// in the area and how dense it is, at a fraction of the token cost.
		json summarizeRegion(Map &map, const Region &region) {
			int64_t tilesWithContent = 0;
			int64_t blockingTiles = 0;
			int64_t houseTiles = 0;
			int64_t monsterSpawns = 0;
			int64_t npcSpawns = 0;
			int64_t monsters = 0;

			std::map<uint16_t, int64_t> groundCounts;
			std::map<uint16_t, int64_t> itemCounts;
			std::map<uint32_t, int64_t> houseCounts;
			std::map<unsigned int, int64_t> zoneCounts;
			std::map<std::string, int64_t> monsterCounts;

			for (int z = region.minZ; z <= region.maxZ; ++z) {
				for (int y = region.minY; y <= region.maxY; ++y) {
					for (int x = region.minX; x <= region.maxX; ++x) {
						const Tile* tile = map.getTile(x, y, z);
						if (!tile || tile->empty()) {
							continue;
						}

						++tilesWithContent;
						if (tile->isBlocking()) {
							++blockingTiles;
						}
						if (tile->ground) {
							++groundCounts[tile->ground->getID()];
						}
						for (const Item* item : tile->items) {
							++itemCounts[item->getID()];
						}
						if (tile->house_id != 0) {
							++houseTiles;
							++houseCounts[tile->house_id];
						}
						for (unsigned int zone : tile->zones) {
							++zoneCounts[zone];
						}
						if (tile->spawnMonster) {
							++monsterSpawns;
						}
						if (tile->spawnNpc) {
							++npcSpawns;
						}
						for (const Monster* monster : tile->monsters) {
							++monsters;
							++monsterCounts[monster->getName()];
						}
					}
				}
			}

			// Top-N histograms: the long tail is noise for an orienting read.
			auto topItems = [](const std::map<uint16_t, int64_t> &counts, size_t limit) {
				std::vector<std::pair<uint16_t, int64_t>> sorted(counts.begin(), counts.end());
				std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
					return a.second > b.second;
				});
				json out = json::array();
				for (size_t i = 0; i < sorted.size() && i < limit; ++i) {
					const ItemType &type = g_items[sorted[i].first];
					out.push_back(json {
						{ "id", sorted[i].first },
						{ "name", type.id != 0 ? type.name : std::string("(unknown)") },
						{ "count", sorted[i].second } });
				}
				return out;
			};

			json houses = json::array();
			for (const auto &[houseId, count] : houseCounts) {
				houses.push_back(json { { "houseId", houseId }, { "tiles", count } });
			}

			json zones = json::array();
			for (const auto &[zoneId, count] : zoneCounts) {
				zones.push_back(json { { "zoneId", zoneId }, { "tiles", count } });
			}

			json monsterList = json::array();
			for (const auto &[name, count] : monsterCounts) {
				monsterList.push_back(json { { "name", name }, { "count", count } });
			}

			return json {
				{ "region", regionToJson(region) },
				{ "scannedTiles", region.volume() },
				{ "tilesWithContent", tilesWithContent },
				{ "emptyTiles", region.volume() - tilesWithContent },
				{ "blockingTiles", blockingTiles },
				{ "houseTiles", houseTiles },
				{ "monsterSpawns", monsterSpawns },
				{ "npcSpawns", npcSpawns },
				{ "monsterCount", monsters },
				{ "topGrounds", topItems(groundCounts, 15) },
				{ "topItems", topItems(itemCounts, 25) },
				{ "houses", houses },
				{ "zones", zones },
				{ "monsters", monsterList }
			};
		}

		// One character per tile, keyed off the same minimap colour the editor
		// paints, so the model sees the actual layout (roads, water, walls)
		// instead of reconstructing it from a coordinate list.
		json renderRegionAscii(Map &map, const Region &region) {
			static const std::string CHARSET = "#*+=%@&$OoXxIl17T?";

			struct ColorInfo {
				int64_t count = 0;
				uint16_t sampleGroundId = 0;
			};
			std::map<uint8_t, ColorInfo> colors;

			for (int z = region.minZ; z <= region.maxZ; ++z) {
				for (int y = region.minY; y <= region.maxY; ++y) {
					for (int x = region.minX; x <= region.maxX; ++x) {
						const Tile* tile = map.getTile(x, y, z);
						if (!tile || tile->empty()) {
							continue;
						}
						const uint8_t color = tile->getMiniMapColor();
						if (color == INVALID_MINIMAP_COLOR) {
							continue;
						}
						ColorInfo &info = colors[color];
						if (info.count == 0 && tile->ground) {
							info.sampleGroundId = tile->ground->getID();
						}
						++info.count;
					}
				}
			}

			// The most common colours get the distinctive characters; anything
			// past the charset collapses into '.' so the grid stays readable.
			std::vector<std::pair<uint8_t, int64_t>> ranked;
			ranked.reserve(colors.size());
			for (const auto &[color, info] : colors) {
				ranked.emplace_back(color, info.count);
			}
			std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
				return a.second > b.second;
			});

			std::map<uint8_t, char> charForColor;
			json legend = json::array();
			for (size_t i = 0; i < ranked.size(); ++i) {
				const uint8_t color = ranked[i].first;
				const char symbol = i < CHARSET.size() ? CHARSET[i] : '.';
				charForColor[color] = symbol;

				const uint16_t groundId = colors[color].sampleGroundId;
				const wxColor rgb = colorFromEightBit(color);
				json entry {
					{ "char", std::string(1, symbol) },
					{ "minimapColor", color },
					{ "rgb", fmt::format("#{:02X}{:02X}{:02X}", rgb.Red(), rgb.Green(), rgb.Blue()) },
					{ "tiles", ranked[i].second }
				};
				if (groundId != 0) {
					const ItemType &type = g_items[groundId];
					entry["sampleGroundId"] = groundId;
					if (type.id != 0) {
						entry["sampleGroundName"] = type.name;
					}
				}
				legend.push_back(std::move(entry));
			}

			json floors = json::array();
			for (int z = region.minZ; z <= region.maxZ; ++z) {
				json rows = json::array();
				for (int y = region.minY; y <= region.maxY; ++y) {
					std::string row;
					row.reserve(region.width());
					for (int x = region.minX; x <= region.maxX; ++x) {
						const Tile* tile = map.getTile(x, y, z);
						if (!tile || tile->empty()) {
							row.push_back(' ');
							continue;
						}
						const uint8_t color = tile->getMiniMapColor();
						const auto it = charForColor.find(color);
						row.push_back(it != charForColor.end() ? it->second : '.');
					}
					rows.push_back(row);
				}
				floors.push_back(json { { "z", z }, { "rows", std::move(rows) } });
			}

			return json {
				{ "region", regionToJson(region) },
				{ "note", "one character per tile; a space is an empty tile. Row 0 is y=" + std::to_string(region.minY) + ", column 0 is x=" + std::to_string(region.minX) + "." },
				{ "legend", std::move(legend) },
				{ "floors", std::move(floors) }
			};
		}

		json listRegionTiles(Map &map, const Region &region, int limit, int offset) {
			json tiles = json::array();
			int64_t matched = 0;
			int64_t skipped = 0;
			bool truncated = false;

			for (int z = region.minZ; z <= region.maxZ && !truncated; ++z) {
				for (int y = region.minY; y <= region.maxY && !truncated; ++y) {
					for (int x = region.minX; x <= region.maxX; ++x) {
						const Tile* tile = map.getTile(x, y, z);
						if (!tile || tile->empty()) {
							continue;
						}
						++matched;
						if (skipped < offset) {
							++skipped;
							continue;
						}
						if (static_cast<int>(tiles.size()) >= limit) {
							truncated = true;
							break;
						}
						tiles.push_back(tileToJson(tile));
					}
				}
			}

			json out {
				{ "region", regionToJson(region) },
				{ "offset", offset },
				{ "returned", tiles.size() },
				{ "tiles", std::move(tiles) }
			};

			if (truncated) {
				out["hasMore"] = true;
				out["nextOffset"] = offset + limit;
				out["note"] = "more tiles remain; call again with this nextOffset";
			} else {
				out["hasMore"] = false;
				out["totalNonEmptyTiles"] = matched;
			}
			return out;
		}

		// ------------------------------------------------------------------
		// Tools
		// ------------------------------------------------------------------

		json toolMapInfo(const json &) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			return jsonResult(json {
				{ "name", map.getName() },
				{ "filename", map.getFilename() },
				{ "description", map.getMapDescription() },
				{ "width", map.getWidth() },
				{ "height", map.getHeight() },
				{ "floors", rme::MapLayers },
				{ "groundFloor", rme::MapGroundLayer },
				{ "tileCount", map.getTileCount() },
				{ "hasUnsavedChanges", map.hasChanged() },
				{ "houseCount", map.houses.count() },
				{ "townCount", map.towns.count() },
				{ "waypointCount", map.waypoints.waypoints.size() },
				{ "zoneCount", map.zones.zones.size() },
				// SpawnsMonster/SpawnsNpc expose iterators but no size().
				{ "monsterSpawnCount", std::distance(map.spawnsMonster.begin(), map.spawnsMonster.end()) },
				{ "npcSpawnCount", std::distance(map.spawnsNpc.begin(), map.spawnsNpc.end()) },
				{ "selectedTiles", editor->getSelection().size() } });
		}

		json toolReadRegion(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			const Region region = parseRegion(params);
			const std::string mode = readString(params, "mode", "summary");

			if (mode == "summary") {
				requireScannableVolume(region, "narrow the region.");
				return jsonResult(summarizeRegion(map, region));
			}
			if (mode == "ascii") {
				requireScannableVolume(region, "narrow the region or read one floor at a time.");
				return jsonResult(renderRegionAscii(map, region));
			}
			if (mode == "tiles") {
				requireScannableVolume(region, "narrow the region, or use mode \"summary\" first to find where the content is.");
				const int limit = readInt(params, "limit", 500, 1, 5000);
				const int offset = readInt(params, "offset", 0, 0, 1000000);
				return jsonResult(listRegionTiles(map, region, limit, offset));
			}

			throw McpError("mode must be one of: summary, ascii, tiles");
		}

		json toolTileGet(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			if (!params.contains("positions") || !params["positions"].is_array()) {
				throw McpError("positions must be an array of {\"x\":..,\"y\":..,\"z\":..}");
			}
			const json &positions = params["positions"];
			if (positions.empty()) {
				throw McpError("positions must not be empty");
			}
			if (positions.size() > 256) {
				throw McpError("at most 256 positions per call");
			}

			json tiles = json::array();
			for (const json &entry : positions) {
				const Position pos = parsePosition(entry, "positions[]");
				const Tile* tile = map.getTile(pos);
				if (!tile || tile->empty()) {
					tiles.push_back(json { { "position", positionToJson(pos) }, { "empty", true } });
				} else {
					tiles.push_back(tileToJson(tile));
				}
			}

			return jsonResult(json { { "tiles", std::move(tiles) } });
		}

		// Matching is deliberately OR-free: every supplied criterion must hold,
		// so the model can narrow a search instead of drowning in hits.
		struct FindCriteria {
			std::vector<uint16_t> itemIds;
			std::string itemName;
			std::string monsterName;
			int houseId = -1;
			int zoneId = -1;
			int actionId = -1;
			int uniqueId = -1;
			bool requireSpawn = false;
			bool requireHouseExit = false;
			bool any = false;
		};

		bool tileMatches(const Tile* tile, const FindCriteria &criteria) {
			if (!tile || tile->empty()) {
				return false;
			}

			if (criteria.houseId >= 0 && tile->house_id != static_cast<uint32_t>(criteria.houseId)) {
				return false;
			}
			if (criteria.zoneId >= 0 && !tile->hasZone(static_cast<unsigned int>(criteria.zoneId))) {
				return false;
			}
			if (criteria.requireSpawn && !tile->spawnMonster && !tile->spawnNpc) {
				return false;
			}
			if (criteria.requireHouseExit && !tile->isHouseExit()) {
				return false;
			}

			if (!criteria.monsterName.empty()) {
				bool found = false;
				for (const Monster* monster : tile->monsters) {
					if (as_lower_str(monster->getName()).find(criteria.monsterName) != std::string::npos) {
						found = true;
						break;
					}
				}
				if (!found && tile->npc && as_lower_str(tile->npc->getName()).find(criteria.monsterName) != std::string::npos) {
					found = true;
				}
				if (!found) {
					return false;
				}
			}

			const bool needsItemScan = !criteria.itemIds.empty() || !criteria.itemName.empty() || criteria.actionId >= 0 || criteria.uniqueId >= 0;
			if (!needsItemScan) {
				return true;
			}

			auto itemMatches = [&criteria](const Item* item) {
				if (!item) {
					return false;
				}
				if (!criteria.itemIds.empty() && std::find(criteria.itemIds.begin(), criteria.itemIds.end(), item->getID()) == criteria.itemIds.end()) {
					return false;
				}
				if (!criteria.itemName.empty() && as_lower_str(item->getName()).find(criteria.itemName) == std::string::npos) {
					return false;
				}
				if (criteria.actionId >= 0 && item->getActionID() != criteria.actionId) {
					return false;
				}
				if (criteria.uniqueId >= 0 && item->getUniqueID() != criteria.uniqueId) {
					return false;
				}
				return true;
			};

			if (itemMatches(tile->ground)) {
				return true;
			}
			for (const Item* item : tile->items) {
				if (itemMatches(item)) {
					return true;
				}
			}
			return false;
		}

		json toolMapFind(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			FindCriteria criteria;
			if (params.contains("itemIds") && params["itemIds"].is_array()) {
				for (const json &entry : params["itemIds"]) {
					criteria.itemIds.push_back(entry.get<uint16_t>());
				}
			}
			if (params.contains("itemId")) {
				criteria.itemIds.push_back(params["itemId"].get<uint16_t>());
			}
			criteria.itemName = as_lower_str(readString(params, "itemName"));
			criteria.monsterName = as_lower_str(readString(params, "monsterName"));
			criteria.houseId = readInt(params, "houseId", -1, -1, 0x7FFFFFFF);
			criteria.zoneId = readInt(params, "zoneId", -1, -1, 0x7FFFFFFF);
			criteria.actionId = readInt(params, "actionId", -1, -1, 0xFFFF);
			criteria.uniqueId = readInt(params, "uniqueId", -1, -1, 0xFFFF);
			criteria.requireSpawn = params.value("hasSpawn", false);
			criteria.requireHouseExit = params.value("isHouseExit", false);

			if (criteria.itemIds.empty() && criteria.itemName.empty() && criteria.monsterName.empty() && criteria.houseId < 0 && criteria.zoneId < 0 && criteria.actionId < 0 && criteria.uniqueId < 0 && !criteria.requireSpawn && !criteria.requireHouseExit) {
				throw McpError("give at least one criterion: itemId(s), itemName, monsterName, houseId, zoneId, actionId, uniqueId, hasSpawn or isHouseExit");
			}

			const int limit = readInt(params, "limit", 200, 1, 2000);
			json matches = json::array();
			bool truncated = false;
			int64_t totalMatches = 0;

			const bool scoped = params.contains("from") && params.contains("to");
			if (scoped) {
				const Region region = parseRegion(params);
				requireScannableVolume(region, "narrow the region, or drop from/to to search the whole map.");
				for (int z = region.minZ; z <= region.maxZ; ++z) {
					for (int y = region.minY; y <= region.maxY; ++y) {
						for (int x = region.minX; x <= region.maxX; ++x) {
							const Tile* tile = map.getTile(x, y, z);
							if (!tileMatches(tile, criteria)) {
								continue;
							}
							++totalMatches;
							if (static_cast<int>(matches.size()) < limit) {
								matches.push_back(positionToJson(tile->getPosition()));
							} else {
								truncated = true;
							}
						}
					}
				}
			} else {
				// No region given: walk the quadtree, which visits only
				// allocated tiles instead of the full 65535x65535x16 space.
				auto visit = [&](TileLocation* location) {
					const Tile* tile = location ? location->get() : nullptr;
					if (!tileMatches(tile, criteria)) {
						return;
					}
					++totalMatches;
					if (static_cast<int>(matches.size()) < limit) {
						matches.push_back(positionToJson(tile->getPosition()));
					} else {
						truncated = true;
					}
				};
				map.forEachTileLocation(visit);
			}

			json out {
				{ "scope", scoped ? "region" : "whole map" },
				{ "totalMatches", totalMatches },
				{ "returned", matches.size() },
				{ "positions", std::move(matches) }
			};
			if (truncated) {
				out["truncated"] = true;
				out["note"] = "raise limit or narrow the search to see the rest";
			}
			return jsonResult(out);
		}

		json itemTypeToJson(const ItemType &type) {
			return json {
				{ "id", type.id },
				{ "clientId", type.clientID },
				{ "name", type.name },
				{ "description", type.description },
				{ "weight", type.weight },
				{ "attack", type.attack },
				{ "defense", type.defense },
				{ "armor", type.armor },
				{ "isGround", type.isGroundTile() },
				{ "isBorder", type.isBorder },
				{ "isWall", type.isWall },
				{ "isDoor", type.isDoor() },
				{ "isTable", type.isTable },
				{ "isCarpet", type.isCarpet },
				{ "isContainer", type.isContainer() },
				{ "isTeleport", type.isTeleport() },
				{ "isDepot", type.isDepot() },
				{ "isBed", type.isBed() },
				{ "isSplash", type.isSplash() },
				{ "isFluidContainer", type.isFluidContainer() },
				{ "isStackable", type.stackable },
				{ "isMoveable", type.moveable },
				{ "isPickupable", type.pickupable },
				{ "isRotatable", type.rotable },
				{ "isHangable", type.isHangable },
				{ "blocking", type.unpassable },
				{ "blocksMissiles", type.blockMissiles },
				{ "blocksPathfinder", type.blockPathfinder },
				{ "hasElevation", type.hasElevation },
				{ "canReadText", type.canReadText },
				{ "canWriteText", type.canWriteText },
				{ "isFloorChange", type.isFloorChange() }
			};
		}

		json toolItemInfo(const json &params) {
			const int id = readInt(params, "id", 0, 0, 0xFFFF);
			if (id == 0) {
				throw McpError("id is required; use item_search to find an id from a name");
			}

			const ItemType &type = g_items[static_cast<uint16_t>(id)];
			if (type.id == 0) {
				throw McpError(fmt::format("item id {} does not exist in the loaded client", id));
			}
			return jsonResult(itemTypeToJson(type));
		}

		// There is no name index in ItemDatabase, so this is a linear scan by
		// design, same as find_item_window and the Lua items.findByName.
		json toolItemSearch(const json &params) {
			const std::string needle = as_lower_str(readString(params, "name"));
			if (needle.empty()) {
				throw McpError("name is required");
			}
			const int limit = readInt(params, "limit", 50, 1, 500);

			json exact = json::array();
			json partial = json::array();

			for (uint16_t id = g_items.getMinID(); id <= g_items.getMaxID(); ++id) {
				const ItemType &type = g_items[id];
				if (type.id == 0 || type.name.empty()) {
					continue;
				}
				const std::string name = as_lower_str(type.name);
				if (name == needle) {
					exact.push_back(json { { "id", type.id }, { "name", type.name } });
				} else if (partial.size() < static_cast<size_t>(limit) && name.find(needle) != std::string::npos) {
					partial.push_back(json { { "id", type.id }, { "name", type.name } });
				}
			}

			return jsonResult(json {
				{ "query", needle },
				{ "exactMatches", std::move(exact) },
				{ "partialMatches", std::move(partial) } });
		}

		json toolEditorState(const json &) {
			Editor* editor = requireEditor();
			const Selection &selection = editor->getSelection();

			json out {
				{ "mapName", editor->getMap().getName() },
				{ "hasUnsavedChanges", editor->getMap().hasChanged() },
				{ "canUndo", editor->canUndo() },
				{ "canRedo", editor->canRedo() },
				{ "selectedTiles", selection.size() }
			};

			if (selection.size() > 0) {
				out["selectionBounds"] = json {
					{ "from", positionToJson(selection.minPosition()) },
					{ "to", positionToJson(selection.maxPosition()) }
				};
			}

			if (Brush* brush = g_gui.GetCurrentBrush()) {
				out["currentBrush"] = brush->getName();
			}
			out["brushSize"] = g_gui.GetBrushSize();

			return jsonResult(out);
		}

		json toolSelectionGet(const json &params) {
			Editor* editor = requireEditor();
			const Selection &selection = editor->getSelection();

			if (selection.size() == 0) {
				return jsonResult(json { { "selectedTiles", 0 }, { "positions", json::array() } });
			}

			const int limit = readInt(params, "limit", 500, 1, 5000);
			json positions = json::array();
			for (const Tile* tile : selection.getTiles()) {
				if (static_cast<int>(positions.size()) >= limit) {
					break;
				}
				positions.push_back(positionToJson(tile->getPosition()));
			}

			return jsonResult(json {
				{ "selectedTiles", selection.size() },
				{ "bounds", json { { "from", positionToJson(selection.minPosition()) }, { "to", positionToJson(selection.maxPosition()) } } },
				{ "returned", positions.size() },
				{ "positions", std::move(positions) } });
		}

		// ------------------------------------------------------------------
		// Schemas
		// ------------------------------------------------------------------


	} // namespace

	void registerMapTools(ToolRegistry &registry) {
		registry.add({ "map_info",
					   "Overview of the map currently open in the editor: name, dimensions, tile count, and how many houses, towns, waypoints, zones and spawns it has. Call this first to orient yourself.",
					   emptySchema(),
					   false,
					   toolMapInfo });

		registry.add({ "map_read_region",
					   "Read a rectangular region of the map, from one corner to another (any two opposite corners, on one or several floors). "
					   "mode=summary (default) returns counts and the most common grounds/items - cheap, use it first. "
					   "mode=ascii draws the region as a character grid with a legend, so you can see the actual layout. "
					   "mode=tiles returns full per-tile detail for non-empty tiles, paginated.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "from", positionSchema("one corner of the region") }, { "to", positionSchema("the opposite corner of the region") }, { "mode", json { { "type", "string" }, { "enum", json::array({ "summary", "ascii", "tiles" }) }, { "description", "summary (default), ascii or tiles" } } }, { "limit", json { { "type", "integer" }, { "description", "tiles mode only: max tiles per page, default 500" } } }, { "offset", json { { "type", "integer" }, { "description", "tiles mode only: page offset, use nextOffset from the previous call" } } } } },
						   { "required", json::array({ "from", "to" }) } },
					   false,
					   toolReadRegion });

		registry.add({ "tile_get",
					   "Full detail for specific tiles: ground, item stack with action/unique ids and text, map flags, house, zones, spawns, monsters and npc.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "positions", json { { "type", "array" }, { "description", "up to 256 positions" }, { "items", positionSchema("a tile position") } } } } },
						   { "required", json::array({ "positions" }) } },
					   false,
					   toolTileGet });

		registry.add({ "map_find",
					   "Find tiles matching criteria. All supplied criteria must hold. Give from/to to search a region, or omit them to search the whole map.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "from", positionSchema("optional: one corner of the region to search") }, { "to", positionSchema("optional: the opposite corner") }, { "itemId", json { { "type", "integer" }, { "description", "server item id present on the tile" } } }, { "itemIds", json { { "type", "array" }, { "items", json { { "type", "integer" } } }, { "description", "any of these item ids" } } }, { "itemName", json { { "type", "string" }, { "description", "substring of an item name on the tile" } } }, { "monsterName", json { { "type", "string" }, { "description", "substring of a monster or npc name on the tile" } } }, { "houseId", json { { "type", "integer" } } }, { "zoneId", json { { "type", "integer" } } }, { "actionId", json { { "type", "integer" } } }, { "uniqueId", json { { "type", "integer" } } }, { "hasSpawn", json { { "type", "boolean" } } }, { "isHouseExit", json { { "type", "boolean" } } }, { "limit", json { { "type", "integer" }, { "description", "max positions returned, default 200" } } } } } },
					   false,
					   toolMapFind });

		registry.add({ "item_info",
					   "Properties of an item type by server id: name, description, client id, weight, combat values and the behaviour flags (ground, border, wall, door, container, blocking, stackable...).",
					   json {
						   { "type", "object" },
						   { "properties", json { { "id", json { { "type", "integer" }, { "description", "server item id" } } } } },
						   { "required", json::array({ "id" }) } },
					   false,
					   toolItemInfo });

		registry.add({ "item_search",
					   "Find item ids by name. Returns exact matches first, then partial ones.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "name", json { { "type", "string" } } }, { "limit", json { { "type", "integer" }, { "description", "max partial matches, default 50" } } } } },
						   { "required", json::array({ "name" }) } },
					   false,
					   toolItemSearch });

		registry.add({ "editor_state",
					   "Current editor state: unsaved changes, undo/redo availability, selection size and bounds, and the active brush.",
					   emptySchema(),
					   false,
					   toolEditorState });

		registry.add({ "selection_get",
					   "Positions currently selected in the editor, with the selection bounding box.",
					   json { { "type", "object" }, { "properties", json { { "limit", json { { "type", "integer" }, { "description", "max positions returned, default 500" } } } } } },
					   false,
					   toolSelectionGet });

		registerRenderTool(registry);
	}

} // namespace mcp
