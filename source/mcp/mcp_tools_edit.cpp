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

#include "../brush.h"
#include "../common.h"
#include "../editor.h"
#include "../gui.h"
#include "../house.h"
#include "../item.h"
#include "../items.h"
#include "../map.h"
#include "../selection.h"
#include "../tile.h"

#include <algorithm>
#include <string>
#include <vector>

namespace mcp {

	namespace {

		// One call may not rewrite the whole map in a single undo step; this is
		// the same order of magnitude as a large brush stroke.
		constexpr size_t MAX_EDIT_TILES = 20000;

		void applyFlags(Tile* tile, const json &flags) {
			const auto set = [&](const char* key, uint16_t flag) {
				if (!flags.contains(key)) {
					return;
				}
				if (flags[key].get<bool>()) {
					tile->setMapFlags(flag);
				} else {
					tile->unsetMapFlags(flag);
				}
			};

			set("protectionZone", TILESTATE_PROTECTIONZONE);
			set("pvpZone", TILESTATE_PVPZONE);
			set("noPvp", TILESTATE_NOPVP);
			set("noLogout", TILESTATE_NOLOGOUT);
		}

		json toolTileEdit(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			if (!params.contains("tiles") || !params["tiles"].is_array() || params["tiles"].empty()) {
				throw McpError("tiles must be a non-empty array of tile edits");
			}
			if (params["tiles"].size() > MAX_EDIT_TILES) {
				throw McpError(fmt::format("at most {} tiles per call", MAX_EDIT_TILES));
			}

			const bool borderize = params.value("borderize", false);
			const bool wallize = params.value("wallize", false);

			// Nothing reaches the map until commit(), so a bad entry halfway
			// through aborts the whole batch instead of half-applying it.
			TileBatch batch(*editor);

			for (const json &entry : params["tiles"]) {
				if (!entry.is_object() || !entry.contains("position")) {
					throw McpError("every tile edit needs a position");
				}

				const Position position = parsePosition(entry["position"], "position");
				Tile* tile = batch.edit(position);

				if (entry.value("clearItems", false)) {
					while (!tile->items.empty()) {
						delete tile->items.back();
						tile->items.pop_back();
					}
				}

				if (entry.contains("ground")) {
					if (entry["ground"].is_null()) {
						tile->clearGround();
					} else {
						tile->replaceGround(buildItem(entry["ground"]));
					}
				}

				if (entry.contains("removeItemIds") && entry["removeItemIds"].is_array()) {
					std::vector<uint16_t> ids;
					for (const json &id : entry["removeItemIds"]) {
						ids.push_back(id.get<uint16_t>());
					}
					for (auto it = tile->items.begin(); it != tile->items.end();) {
						if (std::find(ids.begin(), ids.end(), (*it)->getID()) != ids.end()) {
							delete *it;
							it = tile->items.erase(it);
						} else {
							++it;
						}
					}
				}

				if (entry.contains("addItems") && entry["addItems"].is_array()) {
					for (const json &spec : entry["addItems"]) {
						tile->addItem(buildItem(spec));
					}
				}

				if (entry.contains("flags")) {
					applyFlags(tile, entry["flags"]);
				}

				if (entry.contains("houseId")) {
					const auto houseId = entry["houseId"].get<uint32_t>();
					if (houseId == 0) {
						tile->setHouse(nullptr);
					} else {
						House* house = map.houses.getHouse(houseId);
						if (!house) {
							throw McpError(fmt::format("no house with id {}", houseId));
						}
						// Action::commit reconciles Houses membership from the
						// house_id difference, on both commit and undo.
						tile->setHouse(house);
					}
				}

				if (entry.contains("zones") && entry["zones"].is_array()) {
					tile->removeZones();
					for (const json &zone : entry["zones"]) {
						tile->addZone(zone.get<unsigned int>());
					}
				}

				if (borderize) {
					tile->borderize(&map);
				}
				if (wallize) {
					tile->wallize(&map);
				}

				tile->update();
			}

			const size_t changed = batch.commit();
			return jsonResult(json { { "tilesChanged", changed }, { "undoable", true } });
		}

		json toolBrushApply(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			const std::string name = readString(params, "brush");
			if (name.empty()) {
				throw McpError("brush is required; use brush_list to see the available names");
			}

			Brush* brush = g_brushes.getBrush(name);
			if (!brush) {
				throw McpError(fmt::format("no brush named '{}'; use brush_list to find one", name));
			}

			if (!params.contains("positions") || !params["positions"].is_array() || params["positions"].empty()) {
				throw McpError("positions must be a non-empty array");
			}
			if (params["positions"].size() > MAX_EDIT_TILES) {
				throw McpError(fmt::format("at most {} positions per call", MAX_EDIT_TILES));
			}

			const bool erase = params.value("erase", false);
			const bool borderize = params.value("borderize", false);
			bool alt = params.value("alt", false);

			TileBatch batch(*editor, erase ? ACTION_ERASE : ACTION_DRAW);

			for (const json &entry : params["positions"]) {
				const Position position = parsePosition(entry, "positions[]");
				Tile* tile = batch.edit(position);

				if (erase) {
					brush->undraw(&map, tile);
				} else {
					brush->draw(&map, tile, &alt);
				}

				if (borderize) {
					tile->borderize(&map);
				}
				tile->update();
			}

			const size_t changed = batch.commit();
			return jsonResult(json {
				{ "brush", name },
				{ "mode", erase ? "erase" : "draw" },
				{ "tilesChanged", changed } });
		}

		json toolBrushList(const json &params) {
			const std::string needle = as_lower_str(readString(params, "name"));
			const int limit = readInt(params, "limit", 200, 1, 5000);

			json brushes = json::array();
			int64_t total = 0;

			for (const auto &[key, brush] : g_brushes.getMap()) {
				if (!brush) {
					continue;
				}
				const std::string brushName = brush->getName();
				if (!needle.empty() && as_lower_str(brushName).find(needle) == std::string::npos) {
					continue;
				}
				++total;
				if (static_cast<int>(brushes.size()) >= limit) {
					continue;
				}
				brushes.push_back(json {
					{ "name", brushName },
					{ "isGround", brush->isGround() },
					{ "isWall", brush->isWall() },
					{ "isDoodad", brush->isDoodad() },
					{ "isRaw", brush->isRaw() },
					{ "isTable", brush->isTable() },
					{ "isCarpet", brush->isCarpet() },
					{ "isHouse", brush->isHouse() } });
			}

			return jsonResult(json { { "total", total }, { "returned", brushes.size() }, { "brushes", std::move(brushes) } });
		}

		json toolEditorAction(const json &params) {
			Editor* editor = requireEditor();
			const std::string op = readString(params, "op");

			if (op == "undo") {
				if (!editor->canUndo()) {
					throw McpError("there is nothing to undo");
				}
				editor->undo();
				g_gui.RefreshView();
				return jsonResult(json { { "op", op }, { "canUndo", editor->canUndo() }, { "canRedo", editor->canRedo() } });
			}

			if (op == "redo") {
				if (!editor->canRedo()) {
					throw McpError("there is nothing to redo");
				}
				editor->redo();
				g_gui.RefreshView();
				return jsonResult(json { { "op", op }, { "canUndo", editor->canUndo() }, { "canRedo", editor->canRedo() } });
			}

			if (op == "save") {
				// No dialog: there is no user in front of this call.
				g_gui.SaveCurrentMap(false);
				return jsonResult(json { { "op", op }, { "hasUnsavedChanges", editor->getMap().hasChanged() } });
			}

			if (op == "goto") {
				if (!params.contains("position")) {
					throw McpError("goto requires a position");
				}
				const Position position = parsePosition(params["position"], "position");
				g_gui.SetScreenCenterPosition(position);
				g_gui.RefreshView();
				return jsonResult(json { { "op", op }, { "position", positionToJson(position) } });
			}

			if (op == "select" || op == "deselect") {
				if (!params.contains("positions") || !params["positions"].is_array()) {
					throw McpError(op + " requires a positions array");
				}

				Map &map = editor->getMap();
				Selection &selection = editor->getSelection();
				const bool selecting = op == "select";

				selection.start(Selection::NONE);
				for (const json &entry : params["positions"]) {
					const Position position = parsePosition(entry, "positions[]");
					Tile* tile = map.getTile(position);
					if (!tile) {
						continue;
					}
					if (selecting) {
						selection.add(tile);
					} else {
						selection.remove(tile);
					}
				}
				selection.finish(Selection::NONE);

				g_gui.RefreshView();
				return jsonResult(json { { "op", op }, { "selectedTiles", selection.size() } });
			}

			if (op == "clear_selection") {
				Selection &selection = editor->getSelection();
				selection.start(Selection::NONE);
				selection.clear();
				selection.finish(Selection::NONE);
				g_gui.RefreshView();
				return jsonResult(json { { "op", op }, { "selectedTiles", selection.size() } });
			}

			throw McpError("op must be one of: undo, redo, save, goto, select, deselect, clear_selection");
		}

		json tileEditSchema() {
			json flags {
				{ "type", "object" },
				{ "properties", json { { "protectionZone", json { { "type", "boolean" } } }, { "pvpZone", json { { "type", "boolean" } } }, { "noPvp", json { { "type", "boolean" } } }, { "noLogout", json { { "type", "boolean" } } } } }
			};

			json entry {
				{ "type", "object" },
				{ "properties", json { { "position", positionSchema() }, { "ground", json { { "description", "an item id, an item object (see addItems), or null to clear the ground" } } }, { "addItems", json { { "type", "array" }, { "description", "items to place, each an id or an object" }, { "items", itemSpecSchema() } } }, { "removeItemIds", json { { "type", "array" }, { "items", json { { "type", "integer" } } } } }, { "clearItems", json { { "type", "boolean" }, { "description", "remove every item, keeping the ground" } } }, { "flags", flags }, { "houseId", json { { "type", "integer" }, { "description", "0 detaches the tile from its house" } } }, { "zones", json { { "type", "array" }, { "items", json { { "type", "integer" } } }, { "description", "replaces the tile zone set" } } } } },
				{ "required", json::array({ "position" }) }
			};

			return json {
				{ "type", "object" },
				{ "properties", json { { "tiles", json { { "type", "array" }, { "description", "up to 20000 tile edits" }, { "items", entry } } }, { "borderize", json { { "type", "boolean" }, { "description", "auto-border every edited tile afterwards" } } }, { "wallize", json { { "type", "boolean" }, { "description", "auto-connect walls on every edited tile afterwards" } } } } },
				{ "required", json::array({ "tiles" }) }
			};
		}

	} // namespace

	void registerEditTools(ToolRegistry &registry) {
		registry.add({ "tile_edit",
					   "Edit many tiles in one undoable step. Each entry needs a position; everything else is optional: "
					   "ground (item id, or null to clear), addItems, removeItemIds, clearItems, flags, houseId (0 detaches), zones. "
					   "Prefer one call with many tiles over many small calls - the whole batch becomes a single Ctrl+Z.",
					   tileEditSchema(),
					   true,
					   toolTileEdit });

		registry.add({ "brush_apply",
					   "Apply one of the editor's brushes over a list of positions, in a single undoable step. "
					   "This is how you get correct borders, wall connections and doodad composition, instead of placing raw item ids. "
					   "Use brush_list to find names.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "brush", json { { "type", "string" }, { "description", "brush name, for example grass or sandstone wall" } } }, { "positions", positionArraySchema("tiles to apply the brush to") }, { "erase", json { { "type", "boolean" }, { "description", "undraw instead of draw" } } }, { "borderize", json { { "type", "boolean" }, { "description", "auto-border afterwards" } } }, { "alt", json { { "type", "boolean" }, { "description", "brush alternate mode, like holding the modifier key" } } } } },
						   { "required", json::array({ "brush", "positions" }) } },
					   true,
					   toolBrushApply });

		registry.add({ "brush_list",
					   "List the brushes available in this editor installation, optionally filtered by name.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "name", json { { "type", "string" }, { "description", "substring filter" } } }, { "limit", json { { "type", "integer" }, { "description", "default 200" } } } } } },
					   false,
					   toolBrushList });

		registry.add({ "editor_action",
					   "Editor-level operations: undo, redo, save the map, move the view, and change the selection.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "op", json { { "type", "string" }, { "enum", json::array({ "undo", "redo", "save", "goto", "select", "deselect", "clear_selection" }) } } }, { "position", positionSchema() }, { "positions", positionArraySchema("for select and deselect") } } },
						   { "required", json::array({ "op" }) } },
					   true,
					   toolEditorAction });
	}

} // namespace mcp
