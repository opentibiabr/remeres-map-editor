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
// The Edit > Find family, the Select menu, and the palette. These are the
// operations a mapper reaches for constantly, so the model needs them too.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "mcp_tools.h"
#include "mcp_write.h"

#include "../basemap.h"
#include "../brush.h"
#include "../common.h"
#include "../complexitem.h"
#include "../const.h"
#include "../editor.h"
#include "../gui.h"
#include "../gui_ids.h"
#include "../item.h"
#include "../items.h"
#include "../map.h"
#include "../materials.h"
#include "../monster.h"
#include "../palette_common.h"
#include "../selection.h"
#include "../settings.h"
#include "../tile.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace mcp {

	namespace {

		// ------------------------------------------------------------------
		// Scope shared by search and selection operations
		// ------------------------------------------------------------------

		enum class Scope {
			WholeMap,
			Region,
			Selection
		};

		struct ScopedTiles {
			Scope scope = Scope::WholeMap;
			std::vector<Tile*> tiles;
		};

		const char* scopeName(Scope scope) {
			switch (scope) {
				case Scope::Region:
					return "region";
				case Scope::Selection:
					return "selection";
				default:
					return "whole map";
			}
		}

		// Collects the tiles a tool should act on: an explicit from/to box, the
		// editor's current selection, or everything.
		ScopedTiles collectScope(Editor &editor, const json &params, bool defaultToSelection) {
			Map &map = editor.getMap();
			ScopedTiles result;

			if (params.contains("from") && params.contains("to")) {
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

				result.scope = Scope::Region;
				for (int z = minZ; z <= maxZ; ++z) {
					for (int y = minY; y <= maxY; ++y) {
						for (int x = minX; x <= maxX; ++x) {
							if (Tile* tile = map.getTile(x, y, z)) {
								result.tiles.push_back(tile);
							}
						}
					}
				}
				return result;
			}

			const bool useSelection = params.value("useSelection", defaultToSelection);
			if (useSelection) {
				Selection &selection = editor.getSelection();
				if (selection.size() == 0) {
					throw McpError("nothing is selected; select something first, or pass a from/to region");
				}
				result.scope = Scope::Selection;
				for (Tile* tile : selection.getTiles()) {
					result.tiles.push_back(tile);
				}
				return result;
			}

			result.scope = Scope::WholeMap;
			auto visit = [&result](TileLocation* location) {
				if (Tile* tile = location ? location->get() : nullptr) {
					result.tiles.push_back(tile);
				}
			};
			map.forEachTileLocation(visit);
			return result;
		}

		// ------------------------------------------------------------------
		// map_search - the Edit > Find on Map / Find on Selection family
		// ------------------------------------------------------------------

		json describeItem(const Item* item) {
			json out { { "id", item->getID() }, { "name", item->getName() } };
			if (item->getActionID() != 0) {
				out["actionId"] = item->getActionID();
			}
			if (item->getUniqueID() != 0) {
				out["uniqueId"] = item->getUniqueID();
			}
			const std::string text = item->getText();
			if (!text.empty()) {
				out["text"] = text.size() > 120 ? text.substr(0, 120) + "..." : text;
			}
			return out;
		}

		bool hasDuplicateItems(const Tile* tile) {
			std::unordered_set<uint16_t> seen;
			for (const Item* item : tile->items) {
				// Ground-like and elevation items legitimately repeat.
				if (item->isGroundTile() || item->getItemType().hasElevation) {
					continue;
				}
				if (!seen.insert(item->getID()).second) {
					return true;
				}
			}
			return false;
		}

		bool hasWallsUponWalls(const Tile* tile) {
			int walls = 0;
			for (const Item* item : tile->items) {
				const ItemType &type = item->getItemType();
				if (type.isWall || type.isBrushDoor) {
					++walls;
				}
			}
			return walls > 1;
		}

		json toolMapSearch(const json &params) {
			Editor* editor = requireEditor();

			const std::string what = readString(params, "what", "everything");
			static const std::set<std::string> VALID {
				"everything", "unique", "action", "container", "writeable",
				"duplicated", "walls_upon_walls", "monster", "item"
			};
			if (!VALID.count(what)) {
				throw McpError("what must be one of: everything, unique, action, container, writeable, duplicated, walls_upon_walls, monster, item");
			}

			const int limit = readInt(params, "limit", 300, 1, 5000);
			const std::string creatureName = as_lower_str(readString(params, "name"));
			const int wantedItemId = readInt(params, "itemId", 0, 0, 0xFFFF);

			if (what == "monster" && creatureName.empty()) {
				throw McpError("searching for monsters needs a name");
			}
			if (what == "item" && wantedItemId == 0) {
				throw McpError("searching for an item needs an itemId");
			}

			const ScopedTiles scoped = collectScope(*editor, params, false);

			json results = json::array();
			int64_t total = 0;

			// Tile-level predicates answer per tile; item-level ones per item.
			const bool tileLevel = what == "duplicated" || what == "walls_upon_walls" || what == "monster";

			for (Tile* tile : scoped.tiles) {
				if (tile->empty()) {
					continue;
				}

				if (tileLevel) {
					bool hit = false;
					json detail;

					if (what == "duplicated") {
						hit = hasDuplicateItems(tile);
					} else if (what == "walls_upon_walls") {
						hit = hasWallsUponWalls(tile);
					} else {
						json names = json::array();
						for (const Monster* monster : tile->monsters) {
							if (as_lower_str(monster->getName()).find(creatureName) != std::string::npos) {
								hit = true;
								names.push_back(json { { "name", monster->getName() }, { "spawnTime", monster->getSpawnMonsterTime() } });
							}
						}
						if (hit) {
							detail = std::move(names);
						}
					}

					if (!hit) {
						continue;
					}
					++total;
					if (static_cast<int>(results.size()) >= limit) {
						continue;
					}
					json entry { { "position", positionToJson(tile->getPosition()) } };
					if (!detail.is_null()) {
						entry["monsters"] = std::move(detail);
					}
					results.push_back(std::move(entry));
					continue;
				}

				auto consider = [&](const Item* item) {
					if (!item) {
						return;
					}
					bool hit = false;
					if (what == "item") {
						hit = item->getID() == static_cast<uint16_t>(wantedItemId);
					} else if (what == "unique") {
						hit = item->getUniqueID() > 0;
					} else if (what == "action") {
						hit = item->getActionID() > 0;
					} else if (what == "container") {
						hit = dynamic_cast<const Container*>(item) != nullptr;
					} else if (what == "writeable") {
						hit = !item->getText().empty();
					} else { // everything
						hit = item->getUniqueID() > 0 || item->getActionID() > 0 || !item->getText().empty() || dynamic_cast<const Container*>(item) != nullptr;
					}

					if (!hit) {
						return;
					}
					++total;
					if (static_cast<int>(results.size()) >= limit) {
						return;
					}
					results.push_back(json {
						{ "position", positionToJson(tile->getPosition()) },
						{ "item", describeItem(item) } });
				};

				consider(tile->ground);
				for (const Item* item : tile->items) {
					consider(item);
				}
			}

			json out {
				{ "what", what },
				{ "scope", scopeName(scoped.scope) },
				{ "total", total },
				{ "returned", results.size() },
				{ "results", std::move(results) }
			};
			if (total > static_cast<int64_t>(results.size())) {
				out["truncated"] = true;
				out["note"] = "raise limit, or narrow the scope with from/to or useSelection";
			}
			return jsonResult(out);
		}

		// ------------------------------------------------------------------
		// selection_op - the Select menu
		// ------------------------------------------------------------------

		json toolSelectionOp(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();
			Selection &selection = editor->getSelection();

			const std::string op = readString(params, "op");
			if (op.empty()) {
				throw McpError("op is required");
			}

			// Replace the selection from an explicit box. Everything else acts
			// on whatever is currently selected.
			if (op == "select_region") {
				if (!params.contains("from") || !params.contains("to")) {
					throw McpError("select_region needs from and to");
				}
				const Position a = parsePosition(params["from"], "from");
				const Position b = parsePosition(params["to"], "to");
				const bool additive = params.value("add", false);

				selection.start(Selection::NONE);
				if (!additive) {
					selection.clear();
				}
				int64_t added = 0;
				for (int z = std::min(a.z, b.z); z <= std::max(a.z, b.z); ++z) {
					for (int y = std::min(a.y, b.y); y <= std::max(a.y, b.y); ++y) {
						for (int x = std::min(a.x, b.x); x <= std::max(a.x, b.x); ++x) {
							Tile* tile = map.getTile(x, y, z);
							if (tile && !tile->empty()) {
								selection.add(tile);
								++added;
							}
						}
					}
				}
				selection.finish(Selection::NONE);
				g_gui.RefreshView();
				return jsonResult(json { { "op", op }, { "tilesAdded", added }, { "selectedTiles", selection.size() } });
			}

			if (selection.size() == 0) {
				throw McpError("nothing is selected; use selection_op select_region, or editor_action select, first");
			}

			if (op == "borderize") {
				editor->borderizeSelection();
				g_gui.RefreshView();
				return jsonResult(json { { "op", op }, { "tiles", selection.size() } });
			}

			if (op == "randomize") {
				editor->randomizeSelection();
				g_gui.RefreshView();
				return jsonResult(json { { "op", op }, { "tiles", selection.size() } });
			}

			if (op == "delete") {
				const size_t count = selection.size();
				editor->destroySelection();
				g_gui.RefreshView();
				return jsonResult(json { { "op", op }, { "tilesDeleted", count } });
			}

			if (op == "count_monsters") {
				std::map<std::string, int64_t> counts;
				int64_t total = 0;
				for (const Tile* tile : selection.getTiles()) {
					for (const Monster* monster : tile->monsters) {
						++counts[monster->getName()];
						++total;
					}
				}
				json byName = json::array();
				for (const auto &[name, count] : counts) {
					byName.push_back(json { { "name", name }, { "count", count } });
				}
				return jsonResult(json { { "op", op }, { "total", total }, { "byName", std::move(byName) } });
			}

			// The remaining ops mutate tiles, so they go through the batch and
			// stay undoable. Positions are snapshotted first because the
			// selection is rebuilt as tiles get swapped.
			std::vector<Position> positions;
			positions.reserve(selection.size());
			for (const Tile* tile : selection.getTiles()) {
				positions.push_back(tile->getPosition());
			}

			if (op == "remove_monsters") {
				const std::string name = as_lower_str(readString(params, "name"));
				int64_t removed = 0;

				TileBatch batch(*editor);
				for (const Position &position : positions) {
					Tile* tile = batch.edit(position);
					for (auto it = tile->monsters.begin(); it != tile->monsters.end();) {
						if (!name.empty() && as_lower_str((*it)->getName()).find(name) == std::string::npos) {
							++it;
							continue;
						}
						delete *it;
						it = tile->monsters.erase(it);
						++removed;
					}
					tile->update();
				}
				batch.commit();
				return jsonResult(json { { "op", op }, { "monstersRemoved", removed } });
			}

			if (op == "set_spawn_time") {
				const auto spawnTime = static_cast<uint16_t>(readInt(params, "spawnTime", 0, 1, 0xFFFF));
				if (spawnTime == 0) {
					throw McpError("set_spawn_time needs a spawnTime in seconds");
				}
				const std::string name = as_lower_str(readString(params, "name"));
				int64_t updated = 0;

				TileBatch batch(*editor);
				for (const Position &position : positions) {
					Tile* tile = batch.edit(position);
					for (Monster* monster : tile->monsters) {
						if (!name.empty() && as_lower_str(monster->getName()).find(name) == std::string::npos) {
							continue;
						}
						monster->setSpawnMonsterTime(spawnTime);
						++updated;
					}
					tile->update();
				}
				batch.commit();
				return jsonResult(json { { "op", op }, { "spawnTime", spawnTime }, { "monstersUpdated", updated } });
			}

			if (op == "remove_items" || op == "replace_items") {
				const auto fromId = static_cast<uint16_t>(readInt(params, "itemId", 0, 1, 0xFFFF));
				if (fromId == 0) {
					throw McpError(op + " needs an itemId");
				}
				const bool replacing = op == "replace_items";
				const auto toId = static_cast<uint16_t>(readInt(params, "toItemId", 0, 0, 0xFFFF));
				if (replacing && toId == 0) {
					throw McpError("replace_items needs a toItemId");
				}
				if (replacing && g_items[toId].id == 0) {
					throw McpError(fmt::format("item id {} does not exist in the loaded client", toId));
				}

				int64_t affected = 0;
				TileBatch batch(*editor);
				for (const Position &position : positions) {
					Tile* tile = batch.edit(position);

					if (tile->ground && tile->ground->getID() == fromId) {
						++affected;
						if (replacing) {
							tile->replaceGround(Item::Create(toId));
						} else {
							tile->clearGround();
						}
					}

					for (auto it = tile->items.begin(); it != tile->items.end();) {
						if ((*it)->getID() != fromId) {
							++it;
							continue;
						}
						++affected;
						if (replacing) {
							Item* replacement = Item::Create(toId);
							replacement->setActionID((*it)->getActionID());
							replacement->setUniqueID((*it)->getUniqueID());
							delete *it;
							*it = replacement;
							++it;
						} else {
							delete *it;
							it = tile->items.erase(it);
						}
					}
					tile->update();
				}
				batch.commit();
				return jsonResult(json { { "op", op }, { "itemsAffected", affected } });
			}

			if (op == "remove_duplicated_items") {
				int64_t removed = 0;
				TileBatch batch(*editor);
				for (const Position &position : positions) {
					Tile* tile = batch.edit(position);
					std::unordered_set<uint16_t> seen;
					for (auto it = tile->items.begin(); it != tile->items.end();) {
						const ItemType &type = (*it)->getItemType();
						if ((*it)->isGroundTile() || type.hasElevation) {
							++it;
							continue;
						}
						if (seen.insert((*it)->getID()).second) {
							++it;
							continue;
						}
						delete *it;
						it = tile->items.erase(it);
						++removed;
					}
					tile->update();
				}
				batch.commit();
				return jsonResult(json { { "op", op }, { "itemsRemoved", removed } });
			}

			throw McpError(
				"op must be one of: select_region, borderize, randomize, delete, count_monsters, "
				"remove_monsters, set_spawn_time, remove_items, replace_items, remove_duplicated_items"
			);
		}

		// ------------------------------------------------------------------
		// Palette
		// ------------------------------------------------------------------

		PaletteType paletteFromName(const std::string &name) {
			const std::string lowered = as_lower_str(name);
			if (lowered == "terrain") {
				return TILESET_TERRAIN;
			}
			if (lowered == "doodad") {
				return TILESET_DOODAD;
			}
			if (lowered == "item") {
				return TILESET_ITEM;
			}
			if (lowered == "raw") {
				return TILESET_RAW;
			}
			if (lowered == "monster") {
				return TILESET_MONSTER;
			}
			if (lowered == "npc") {
				return TILESET_NPC;
			}
			if (lowered == "house") {
				return TILESET_HOUSE;
			}
			if (lowered == "waypoint") {
				return TILESET_WAYPOINT;
			}
			if (lowered == "zones") {
				return TILESET_ZONES;
			}
			throw McpError("page must be one of: terrain, doodad, item, raw, monster, npc, house, waypoint, zones");
		}

		// Drives the editor's own palette state. brush_apply does not need
		// this, but setting it leaves the editor ready for the person at the
		// keyboard - and lets the model mirror what a mapper would do.
		json toolPaletteSelect(const json &params) {
			const std::string brushName = readString(params, "brush");
			const std::string page = readString(params, "page");

			if (brushName.empty() && page.empty()) {
				throw McpError("give a brush name, a palette page, or both");
			}

			json out = json::object();

			if (!page.empty()) {
				g_gui.SelectPalettePage(paletteFromName(page));
				out["page"] = page;
			}

			if (!brushName.empty()) {
				Brush* brush = g_brushes.getBrush(brushName);
				if (!brush) {
					throw McpError(fmt::format("no brush named '{}'; use brush_list or tileset_list", brushName));
				}
				if (!g_gui.SelectBrush(brush)) {
					throw McpError(fmt::format("the palette could not select '{}'; it may not be in any visible tileset", brushName));
				}
				out["brush"] = brush->getName();
			}

			if (params.contains("brushSize")) {
				g_gui.SetBrushSize(readInt(params, "brushSize", 0, 0, 11));
				out["brushSize"] = g_gui.GetBrushSize();
			}

			g_gui.RefreshView();
			return jsonResult(out);
		}

		// ------------------------------------------------------------------
		// Editor settings that change how the other tools behave
		// ------------------------------------------------------------------

		json toolEditorSettings(const json &params) {
			json out = json::object();
			bool changed = false;

			// Automagic is the big one: with it off, brushes stop generating
			// borders, so brush_apply produces visibly different results.
			if (params.contains("automagic")) {
				g_settings.setInteger(Config::USE_AUTOMAGIC, params["automagic"].get<bool>() ? 1 : 0);
				changed = true;
			}

			if (params.contains("selectionMode")) {
				const std::string mode = as_lower_str(params["selectionMode"].get<std::string>());
				int value = -1;
				if (mode == "current") {
					value = SELECT_CURRENT_FLOOR;
				} else if (mode == "lower") {
					value = SELECT_ALL_FLOORS;
				} else if (mode == "visible") {
					value = SELECT_VISIBLE_FLOORS;
				} else {
					throw McpError("selectionMode must be one of: current, lower, visible");
				}
				g_settings.setInteger(Config::SELECTION_TYPE, value);
				changed = true;
			}

			if (params.contains("compensateSelection")) {
				g_settings.setInteger(Config::COMPENSATED_SELECT, params["compensateSelection"].get<bool>() ? 1 : 0);
				changed = true;
			}

			const int selectionType = g_settings.getInteger(Config::SELECTION_TYPE);
			out["automagic"] = g_settings.getInteger(Config::USE_AUTOMAGIC) != 0;
			out["selectionMode"] = selectionType == SELECT_ALL_FLOORS ? "lower" : (selectionType == SELECT_VISIBLE_FLOORS ? "visible" : "current");
			out["compensateSelection"] = g_settings.getInteger(Config::COMPENSATED_SELECT) != 0;
			out["changed"] = changed;

			if (changed) {
				g_gui.RefreshView();
			}
			return jsonResult(out);
		}

	} // namespace

	void registerSelectTools(ToolRegistry &registry) {
		registry.add({ "map_search",
					   "The editor's Find family. Search the whole map, a region, or the current selection for: "
					   "everything (any item with a unique id, action id, text or contents), unique, action, container, writeable, "
					   "duplicated items, walls stacked on walls, a specific itemId, or a monster by name.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "what", json { { "type", "string" }, { "enum", json::array({ "everything", "unique", "action", "container", "writeable", "duplicated", "walls_upon_walls", "item", "monster" }) }, { "description", "default everything" } } }, { "itemId", json { { "type", "integer" }, { "description", "for what=item" } } }, { "name", json { { "type", "string" }, { "description", "for what=monster" } } }, { "from", positionSchema("optional region corner") }, { "to", positionSchema("optional opposite corner") }, { "useSelection", json { { "type", "boolean" }, { "description", "search the current selection instead of the whole map" } } }, { "limit", json { { "type", "integer" }, { "description", "default 300" } } } } } },
					   false,
					   toolMapSearch });

		registry.add({ "selection_op",
					   "Operate on the editor's current selection, mirroring the Select menu: borderize, randomize, delete, "
					   "count or remove monsters, set monster spawn time, remove or replace an item id, drop duplicated items. "
					   "Use select_region first to set the selection from coordinates. All mutations are undoable.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "op", json { { "type", "string" }, { "enum", json::array({ "select_region", "borderize", "randomize", "delete", "count_monsters", "remove_monsters", "set_spawn_time", "remove_items", "replace_items", "remove_duplicated_items" }) } } }, { "from", positionSchema("for select_region") }, { "to", positionSchema("for select_region") }, { "add", json { { "type", "boolean" }, { "description", "select_region: add to the selection instead of replacing it" } } }, { "itemId", json { { "type", "integer" }, { "description", "for remove_items and replace_items" } } }, { "toItemId", json { { "type", "integer" }, { "description", "for replace_items" } } }, { "name", json { { "type", "string" }, { "description", "monster name filter; omit to affect all of them" } } }, { "spawnTime", json { { "type", "integer" }, { "description", "for set_spawn_time, in seconds" } } } } },
						   { "required", json::array({ "op" }) } },
					   true,
					   toolSelectionOp });

		registry.add({ "palette_select",
					   "Set the editor's active brush, palette page and brush size - what the person at the keyboard would click. "
					   "brush_apply does not need this; use it to leave the editor set up, or to follow along with a human.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "brush", json { { "type", "string" } } }, { "page", json { { "type", "string" }, { "enum", json::array({ "terrain", "doodad", "item", "raw", "monster", "npc", "house", "waypoint", "zones" }) } } }, { "brushSize", json { { "type", "integer" }, { "description", "0-11" } } } } } },
					   true,
					   toolPaletteSelect });

		registry.add({ "editor_settings",
					   "Read or change the editor settings that change how the other tools behave. "
					   "automagic is the important one: with it off, brushes stop generating borders, so brush_apply gives a "
					   "visibly different result. Called with no fields it just reports the current values.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "automagic", json { { "type", "boolean" }, { "description", "auto-border and auto-wall while drawing" } } }, { "selectionMode", json { { "type", "string" }, { "enum", json::array({ "current", "lower", "visible" }) }, { "description", "which floors a selection covers" } } }, { "compensateSelection", json { { "type", "boolean" }, { "description", "compensate for floor difference when selecting" } } } } } },
					   true,
					   toolEditorSettings });
	}

} // namespace mcp
