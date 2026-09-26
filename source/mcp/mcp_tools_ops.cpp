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
// Whole-map and region operations: duplicating areas, bulk item replacement,
// cleanup passes, opening maps, and capturing what the editor is drawing.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "mcp_tools.h"
#include "mcp_write.h"

#include "../common.h"
#include "../copybuffer.h"
#include "../editor.h"
#include "../gui.h"
#include "../iomap.h"
#include "../item.h"
#include "../items.h"
#include "../map.h"
#include "../map_display.h"
#include "../materials.h"
#include "../map_tab.h"
#include "../map_window.h"
#include "../selection.h"
#include "../tile.h"

#include <wx/filename.h>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

namespace mcp {

	namespace {

		struct Box {
			int minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;

			int64_t volume() const {
				return static_cast<int64_t>(maxX - minX + 1) * (maxY - minY + 1) * (maxZ - minZ + 1);
			}
		};

		Box parseBox(const json &params, const char* fromKey = "from", const char* toKey = "to") {
			if (!params.contains(fromKey) || !params.contains(toKey)) {
				throw McpError(fmt::format("{} and {} are required, marking two opposite corners", fromKey, toKey));
			}
			const Position a = parsePosition(params[fromKey], fromKey);
			const Position b = parsePosition(params[toKey], toKey);

			Box box;
			box.minX = std::min(a.x, b.x);
			box.maxX = std::max(a.x, b.x);
			box.minY = std::min(a.y, b.y);
			box.maxY = std::max(a.y, b.y);
			box.minZ = std::min(a.z, b.z);
			box.maxZ = std::max(a.z, b.z);

			if (box.volume() > MAX_REGION_VOLUME) {
				throw McpError(fmt::format("the region covers {} tiles, above the {} limit; narrow it", box.volume(), MAX_REGION_VOLUME));
			}
			return box;
		}

		// ------------------------------------------------------------------
		// Copy / paste
		// ------------------------------------------------------------------

		// CopyBuffer works off the editor selection, so a region copy is
		// "select the box, copy, restore nothing" - the selection is left in
		// place because that is what a user would see happen.
		json toolRegionCopy(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();
			const Box box = parseBox(params);

			Selection &selection = editor->getSelection();
			selection.start(Selection::NONE);
			selection.clear();

			int64_t selected = 0;
			for (int z = box.minZ; z <= box.maxZ; ++z) {
				for (int y = box.minY; y <= box.maxY; ++y) {
					for (int x = box.minX; x <= box.maxX; ++x) {
						Tile* tile = map.getTile(x, y, z);
						if (tile && !tile->empty()) {
							selection.add(tile);
							++selected;
						}
					}
				}
			}
			selection.finish(Selection::NONE);

			if (selected == 0) {
				throw McpError("the region is empty; there is nothing to copy");
			}

			g_gui.copybuffer.copy(*editor, box.minZ);
			g_gui.RefreshView();

			return jsonResult(json {
				{ "tilesCopied", g_gui.copybuffer.GetTileCount() },
				{ "tilesSelected", selected },
				{ "origin", json { { "x", box.minX }, { "y", box.minY }, { "z", box.minZ } } },
				{ "note", "paste with region_paste; the paste position is where the region top-left corner lands" } });
		}

		json toolRegionPaste(const json &params) {
			Editor* editor = requireEditor();

			if (!g_gui.copybuffer.canPaste()) {
				throw McpError("the copy buffer is empty; call region_copy first");
			}
			if (!params.contains("position")) {
				throw McpError("position is required: where the copied region top-left corner should land");
			}

			const Position position = parsePosition(params["position"], "position");
			const size_t tiles = g_gui.copybuffer.GetTileCount();

			// CopyBuffer::paste goes through the action queue itself, so this
			// stays undoable.
			g_gui.copybuffer.paste(*editor, position);
			editor->getMap().doChange();
			g_gui.RefreshView();

			return jsonResult(json { { "tilesPasted", tiles }, { "at", positionToJson(position) }, { "undoable", true } });
		}

		// ------------------------------------------------------------------
		// Bulk item replacement
		// ------------------------------------------------------------------

		json toolRegionReplaceItems(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			const auto fromId = static_cast<uint16_t>(readInt(params, "fromItemId", 0, 1, 0xFFFF));
			if (fromId == 0) {
				throw McpError("fromItemId is required");
			}
			const int toIdRaw = readInt(params, "toItemId", 0, 0, 0xFFFF);
			const auto toId = static_cast<uint16_t>(toIdRaw);

			if (g_items[fromId].id == 0) {
				throw McpError(fmt::format("item id {} does not exist in the loaded client", fromId));
			}
			if (toId != 0 && g_items[toId].id == 0) {
				throw McpError(fmt::format("item id {} does not exist in the loaded client", toId));
			}

			// Region is optional: without it the whole map is swept, which is
			// the usual "fix this wrong id everywhere" case.
			const bool scoped = params.contains("from") && params.contains("to");
			std::vector<Position> targets;

			auto consider = [&](const Tile* tile) {
				if (!tile) {
					return;
				}
				bool hit = tile->ground && tile->ground->getID() == fromId;
				if (!hit) {
					for (const Item* item : tile->items) {
						if (item->getID() == fromId) {
							hit = true;
							break;
						}
					}
				}
				if (hit) {
					targets.push_back(tile->getPosition());
				}
			};

			if (scoped) {
				const Box box = parseBox(params);
				for (int z = box.minZ; z <= box.maxZ; ++z) {
					for (int y = box.minY; y <= box.maxY; ++y) {
						for (int x = box.minX; x <= box.maxX; ++x) {
							consider(map.getTile(x, y, z));
						}
					}
				}
			} else {
				auto visit = [&](TileLocation* location) {
					consider(location ? location->get() : nullptr);
				};
				map.forEachTileLocation(visit);
			}

			if (targets.empty()) {
				return jsonResult(json { { "tilesChanged", 0 }, { "itemsReplaced", 0 }, { "note", "no tile carries that item id in the given scope" } });
			}

			int64_t replaced = 0;
			TileBatch batch(*editor, ACTION_REPLACE_ITEMS);

			for (const Position &position : targets) {
				Tile* tile = batch.edit(position);

				if (tile->ground && tile->ground->getID() == fromId) {
					++replaced;
					if (toId == 0) {
						tile->clearGround();
					} else {
						tile->replaceGround(Item::Create(toId));
					}
				}

				for (auto it = tile->items.begin(); it != tile->items.end();) {
					if ((*it)->getID() != fromId) {
						++it;
						continue;
					}
					++replaced;
					if (toId == 0) {
						delete *it;
						it = tile->items.erase(it);
					} else {
						// Keep the stack position; only the id changes.
						Item* replacement = Item::Create(toId);
						replacement->setActionID((*it)->getActionID());
						replacement->setUniqueID((*it)->getUniqueID());
						delete *it;
						*it = replacement;
						++it;
					}
				}

				tile->update();
			}

			const size_t changed = batch.commit();
			return jsonResult(json {
				{ "fromItemId", fromId },
				{ "toItemId", toIdRaw },
				{ "scope", scoped ? "region" : "whole map" },
				{ "tilesChanged", changed },
				{ "itemsReplaced", replaced },
				{ "undoable", true } });
		}

		// ------------------------------------------------------------------
		// Cleanup
		// ------------------------------------------------------------------

		json toolMapCleanup(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			// These passes run outside the action queue and the editor clears
			// the history around them, so they genuinely cannot be undone.
			if (!params.value("confirm", false)) {
				throw McpError(
					"map_cleanup rewrites the map and CANNOT be undone - it clears the undo history. "
					"Run map_validate first to see what would be affected, make sure the map is saved, "
					"then call again with confirm=true."
				);
			}

			const bool invalidTiles = params.value("removeInvalidTiles", false);
			const bool invalidHouses = params.value("clearInvalidHouses", false);
			const bool borderize = params.value("borderizeMap", false);
			const bool randomize = params.value("randomizeMap", false);
			const bool clearModified = params.value("clearModifiedState", false);
			const bool removeCorpses = params.value("removeCorpses", false);
			const bool removeUnreachable = params.value("removeUnreachableTiles", false);
			const bool removeDuplicates = params.value("removeDuplicateItems", false);
			const bool removeEmptySpawns = params.value("removeEmptySpawns", false);

			if (!invalidTiles && !invalidHouses && !borderize && !randomize && !clearModified && !removeCorpses && !removeUnreachable && !removeDuplicates && !removeEmptySpawns) {
				throw McpError(
					"pick at least one pass: removeInvalidTiles, clearInvalidHouses, borderizeMap, randomizeMap, "
					"clearModifiedState, removeCorpses, removeUnreachableTiles, removeDuplicateItems, removeEmptySpawns"
				);
			}

			json applied = json::array();
			json counts = json::object();

			editor->getSelection().clear();
			editor->clearActions();

			if (invalidTiles) {
				map.cleanInvalidTiles(false);
				applied.push_back("removeInvalidTiles");
			}
			if (invalidHouses) {
				editor->clearInvalidHouseTiles(false);
				applied.push_back("clearInvalidHouses");
			}
			if (borderize) {
				editor->borderizeMap(false);
				applied.push_back("borderizeMap");
			}
			if (randomize) {
				editor->randomizeMap(false);
				applied.push_back("randomizeMap");
			}
			if (clearModified) {
				editor->clearModifiedTileState(false);
				applied.push_back("clearModifiedState");
			}

			// The remaining passes have no Editor wrapper, so they run here
			// against the same map iteration helpers the menu handlers use.
			if (removeCorpses) {
				int64_t removed = 0;
				auto visit = [&](TileLocation* location) {
					Tile* tile = location ? location->get() : nullptr;
					if (!tile) {
						return;
					}
					for (auto it = tile->items.begin(); it != tile->items.end();) {
						if (g_materials.isInTileset(*it, "Corpses") && !(*it)->isComplex()) {
							delete *it;
							it = tile->items.erase(it);
							++removed;
						} else {
							++it;
						}
					}
				};
				map.forEachTileLocation(visit);
				applied.push_back("removeCorpses");
				counts["corpsesRemoved"] = removed;
			}

			if (removeDuplicates) {
				int64_t removed = 0;
				auto visit = [&](TileLocation* location) {
					Tile* tile = location ? location->get() : nullptr;
					if (!tile) {
						return;
					}
					std::unordered_set<uint16_t> seen;
					for (auto it = tile->items.begin(); it != tile->items.end();) {
						if ((*it)->isGroundTile() || (*it)->getItemType().hasElevation) {
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
				};
				map.forEachTileLocation(visit);
				applied.push_back("removeDuplicateItems");
				counts["duplicateItemsRemoved"] = removed;
			}

			if (removeEmptySpawns) {
				// A spawn with no creature inside its radius never produces
				// anything, so it is dead weight in the saved map.
				std::vector<Position> emptyMonsterSpawns;
				for (const Position &position : map.spawnsMonster) {
					const Tile* tile = map.getTile(position);
					if (!tile || !tile->spawnMonster) {
						continue;
					}
					const int radius = tile->spawnMonster->getSize();
					bool empty = true;
					for (int y = -radius; y <= radius && empty; ++y) {
						for (int x = -radius; x <= radius && empty; ++x) {
							const Tile* inner = map.getTile(position + Position(x, y, 0));
							if (inner && !inner->monsters.empty()) {
								empty = false;
							}
						}
					}
					if (empty) {
						emptyMonsterSpawns.push_back(position);
					}
				}
				for (const Position &position : emptyMonsterSpawns) {
					if (Tile* tile = map.getTile(position)) {
						map.removeSpawnMonster(tile);
						delete tile->spawnMonster;
						tile->spawnMonster = nullptr;
					}
				}

				std::vector<Position> emptyNpcSpawns;
				for (const Position &position : map.spawnsNpc) {
					const Tile* tile = map.getTile(position);
					if (!tile || !tile->spawnNpc) {
						continue;
					}
					const int radius = tile->spawnNpc->getSize();
					bool empty = true;
					for (int y = -radius; y <= radius && empty; ++y) {
						for (int x = -radius; x <= radius && empty; ++x) {
							const Tile* inner = map.getTile(position + Position(x, y, 0));
							if (inner && inner->npc) {
								empty = false;
							}
						}
					}
					if (empty) {
						emptyNpcSpawns.push_back(position);
					}
				}
				for (const Position &position : emptyNpcSpawns) {
					if (Tile* tile = map.getTile(position)) {
						map.removeSpawnNpc(tile);
						delete tile->spawnNpc;
						tile->spawnNpc = nullptr;
					}
				}

				applied.push_back("removeEmptySpawns");
				counts["emptyMonsterSpawnsRemoved"] = emptyMonsterSpawns.size();
				counts["emptyNpcSpawnsRemoved"] = emptyNpcSpawns.size();
			}

			if (removeUnreachable) {
				// Same heuristic the editor uses: a tile is unreachable when
				// no walkable tile exists in the box a player could stand in.
				struct Unreachable {
					Map &map;
					bool operator()(Map &, Tile* tile, long long, long long, long long) const {
						const Position &pos = tile->getPosition();
						const int sx = std::max(pos.x - 10, 0);
						const int ex = std::min(pos.x + 10, 65535);
						const int sy = std::max(pos.y - 8, 0);
						const int ey = std::min(pos.y + 8, 65535);
						int sz = 0;
						int ez = 9;
						if (pos.z >= 8) {
							sz = std::max(pos.z - 2, rme::MapGroundLayer);
							ez = std::min(pos.z + 2, rme::MapMaxLayer);
						}
						for (int z = sz; z <= ez; ++z) {
							for (int y = sy; y <= ey; ++y) {
								for (int x = sx; x <= ex; ++x) {
									const Tile* other = map.getTile(x, y, z);
									if (other && !other->isBlocking()) {
										return false;
									}
								}
							}
						}
						return true;
					}
				} condition { map };

				const long long removed = remove_if_TileOnMap(map, condition);
				applied.push_back("removeUnreachableTiles");
				counts["unreachableTilesRemoved"] = removed;
			}

			map.doChange();
			g_gui.RefreshView();

			return jsonResult(json {
				{ "applied", std::move(applied) },
				{ "counts", std::move(counts) },
				{ "undoable", false },
				{ "note", "the undo history was cleared by these passes" },
				{ "tileCount", map.getTileCount() } });
		}

		// ------------------------------------------------------------------
		// Session: open / new / properties
		// ------------------------------------------------------------------

		json toolMapOpen(const json &params) {
			const std::string path = readString(params, "path");
			if (path.empty()) {
				throw McpError("path is required, pointing at an .otbm file");
			}

			const wxFileName fileName(wxString::FromUTF8(path));
			if (!fileName.FileExists()) {
				throw McpError(fmt::format("{} does not exist", path));
			}

			// Refuse to drop unsaved work on the floor.
			Editor* current = g_gui.GetCurrentEditor();
			if (current && current->getMap().hasChanged() && !params.value("discardUnsaved", false)) {
				throw McpError(
					"the map currently open has unsaved changes. Save it with editor_action save, "
					"or call again with discardUnsaved=true to abandon them."
				);
			}

			if (!g_gui.LoadMap(fileName)) {
				throw McpError(fmt::format("the editor could not load {}", path));
			}

			Editor* editor = requireEditor();
			Map &map = editor->getMap();
			return jsonResult(json {
				{ "opened", true },
				{ "name", map.getName() },
				{ "filename", map.getFilename() },
				{ "width", map.getWidth() },
				{ "height", map.getHeight() },
				{ "tileCount", map.getTileCount() } });
		}

		json toolMapNew(const json &params) {
			Editor* current = g_gui.GetCurrentEditor();
			if (current && current->getMap().hasChanged() && !params.value("discardUnsaved", false)) {
				throw McpError(
					"the map currently open has unsaved changes. Save it with editor_action save, "
					"or call again with discardUnsaved=true to abandon them."
				);
			}

			if (!g_gui.NewMap()) {
				throw McpError("the editor could not create a new map");
			}

			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			if (params.contains("width") || params.contains("height")) {
				map.setWidth(readInt(params, "width", map.getWidth(), 64, 65000));
				map.setHeight(readInt(params, "height", map.getHeight(), 64, 65000));
			}
			if (params.contains("name")) {
				map.setName(params["name"].get<std::string>());
			}
			if (params.contains("description")) {
				map.setMapDescription(params["description"].get<std::string>());
			}

			g_gui.RefreshView();
			return jsonResult(json {
				{ "created", true },
				{ "name", map.getName() },
				{ "width", map.getWidth() },
				{ "height", map.getHeight() } });
		}

		json toolMapProperties(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			bool changed = false;
			if (params.contains("name")) {
				map.setName(params["name"].get<std::string>());
				changed = true;
			}
			if (params.contains("description")) {
				map.setMapDescription(params["description"].get<std::string>());
				changed = true;
			}
			if (params.contains("houseFilename")) {
				map.setHouseFilename(params["houseFilename"].get<std::string>());
				changed = true;
			}
			if (params.contains("spawnMonsterFilename")) {
				map.setSpawnMonsterFilename(params["spawnMonsterFilename"].get<std::string>());
				changed = true;
			}
			if (params.contains("spawnNpcFilename")) {
				map.setSpawnNpcFilename(params["spawnNpcFilename"].get<std::string>());
				changed = true;
			}
			if (params.contains("zoneFilename")) {
				map.setZoneFilename(params["zoneFilename"].get<std::string>());
				changed = true;
			}
			// Shrinking the map would silently discard whatever lies outside
			// the new bounds, so growing only unless the caller insists.
			if (params.contains("width") || params.contains("height")) {
				const int width = readInt(params, "width", map.getWidth(), 64, 65000);
				const int height = readInt(params, "height", map.getHeight(), 64, 65000);
				if ((width < map.getWidth() || height < map.getHeight()) && !params.value("allowShrink", false)) {
					throw McpError(
						"shrinking the map would drop everything outside the new bounds; "
						"pass allowShrink=true if that is intended"
					);
				}
				map.setWidth(width);
				map.setHeight(height);
				changed = true;
			}

			if (changed) {
				map.doChange();
				g_gui.RefreshView();
			}

			return jsonResult(json {
				{ "changed", changed },
				{ "name", map.getName() },
				{ "description", map.getMapDescription() },
				{ "width", map.getWidth() },
				{ "height", map.getHeight() },
				{ "houseFilename", map.getHouseFilename() },
				{ "spawnFilename", map.getSpawnFilename() } });
		}

		// ------------------------------------------------------------------
		// Screenshot
		// ------------------------------------------------------------------

		json toolMapScreenshot(const json &params) {
			requireEditor();

			MapTab* tab = g_gui.GetCurrentMapTab();
			if (!tab || !tab->GetView() || !tab->GetView()->GetCanvas()) {
				throw McpError("there is no map view to capture");
			}

			// Optional reframing, so a caller can say "show me this spot"
			// without driving the camera in a separate call.
			if (params.contains("position")) {
				g_gui.SetScreenCenterPosition(parsePosition(params["position"], "position"), false);
			}
			if (params.contains("zoom")) {
				g_gui.SetCurrentZoom(params["zoom"].get<double>());
			}

			// Unlike map_render_region, this is the editor's real OpenGL view:
			// actual sprites, current floor, current view settings.
			const wxImage image = tab->GetView()->GetCanvas()->CaptureScreenshot();
			if (!image.IsOk()) {
				throw McpError("the screen capture failed; the map view may be hidden or the video driver may not support it");
			}

			return imageResult(encodePngBase64(image), "image/png");
		}

		// Editor::importMap merges another .otbm in at an offset, which is how
		// a world gets assembled from separately built pieces.
		ImportType parseImportType(const json &params, const char* key) {
			const std::string value = as_lower_str(readString(params, key, "smart"));
			if (value == "smart") {
				return IMPORT_SMART_MERGE;
			}
			if (value == "merge") {
				return IMPORT_MERGE;
			}
			if (value == "insert") {
				return IMPORT_INSERT;
			}
			if (value == "dont" || value == "none" || value == "skip") {
				return IMPORT_DONT;
			}
			throw McpError(std::string(key) + " must be one of: smart, merge, insert, none");
		}

		json toolMapImport(const json &params) {
			Editor* editor = requireEditor();

			const std::string path = readString(params, "path");
			if (path.empty()) {
				throw McpError("path is required, pointing at the .otbm file to merge in");
			}
			const wxFileName fileName(wxString::FromUTF8(path));
			if (!fileName.FileExists()) {
				throw McpError(fmt::format("{} does not exist", path));
			}

			const int offsetX = readInt(params, "offsetX", 0, -65000, 65000);
			const int offsetY = readInt(params, "offsetY", 0, -65000, 65000);
			const int offsetZ = readInt(params, "offsetZ", 0, -15, 15);

			// Importing rewrites a swath of the map and is not a single undo
			// step, so make the caller acknowledge it.
			if (!params.value("confirm", false)) {
				throw McpError(
					"map_import merges another map into this one and is not undoable as a single step. "
					"Save first, then call again with confirm=true."
				);
			}

			const uint64_t before = editor->getMap().getTileCount();
			if (!editor->importMap(fileName, offsetX, offsetY, offsetZ, parseImportType(params, "houses"), parseImportType(params, "monsterSpawns"), parseImportType(params, "npcSpawns"))) {
				throw McpError(fmt::format("the editor could not import {}", path));
			}

			editor->getMap().doChange();
			g_gui.RefreshView();

			const uint64_t after = editor->getMap().getTileCount();
			return jsonResult(json {
				{ "imported", path },
				{ "offset", json { { "x", offsetX }, { "y", offsetY }, { "z", offsetZ } } },
				{ "tilesBefore", before },
				{ "tilesAfter", after },
				{ "tilesAdded", after > before ? after - before : 0 } });
		}

	} // namespace

	void registerOpsTools(ToolRegistry &registry) {
		registry.add({ "region_copy",
					   "Select a rectangular region and copy it into the editor clipboard, ready for region_paste. "
					   "This is how you duplicate a room, a tower or a camp instead of rebuilding it tile by tile.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "from", positionSchema() }, { "to", positionSchema() } } },
						   { "required", json::array({ "from", "to" }) } },
					   true,
					   toolRegionCopy });

		registry.add({ "region_paste",
					   "Paste whatever region_copy last put in the clipboard, with its top-left corner at the given position. Undoable.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "position", positionSchema() } } },
						   { "required", json::array({ "position" }) } },
					   true,
					   toolRegionPaste });

		registry.add({ "region_replace_items",
					   "Swap every occurrence of one item id for another, in a region or across the whole map. "
					   "Set toItemId to 0 to delete the item instead. Action and unique ids are carried over. Undoable.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "fromItemId", json { { "type", "integer" } } }, { "toItemId", json { { "type", "integer" }, { "description", "0 removes the item instead of replacing it" } } }, { "from", positionSchema() }, { "to", positionSchema() } } },
						   { "required", json::array({ "fromItemId" }) } },
					   true,
					   toolRegionReplaceItems });

		registry.add({ "map_cleanup",
					   "Run the editor's whole-map cleanup passes. These CANNOT be undone and clear the undo history, "
					   "so they require confirm=true. Run map_validate first to see what is actually wrong.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "confirm", json { { "type", "boolean" }, { "description", "required; acknowledges that this cannot be undone" } } }, { "removeInvalidTiles", json { { "type", "boolean" }, { "description", "delete tiles holding invalid items" } } }, { "clearInvalidHouses", json { { "type", "boolean" }, { "description", "detach tiles pointing at houses that do not exist" } } }, { "borderizeMap", json { { "type", "boolean" }, { "description", "recompute auto-borders across the whole map" } } }, { "randomizeMap", json { { "type", "boolean" }, { "description", "randomize ground tiles across the whole map" } } }, { "clearModifiedState", json { { "type", "boolean" }, { "description", "reset the per-tile modified flag" } } }, { "removeCorpses", json { { "type", "boolean" }, { "description", "delete every item in the Corpses tileset" } } }, { "removeUnreachableTiles", json { { "type", "boolean" }, { "description", "delete tiles no player could ever stand near" } } }, { "removeDuplicateItems", json { { "type", "boolean" }, { "description", "drop repeated item ids on a tile" } } }, { "removeEmptySpawns", json { { "type", "boolean" }, { "description", "delete monster and npc spawns with no creature inside" } } } } } },
					   true,
					   toolMapCleanup });

		registry.add({ "map_open",
					   "Open an .otbm map file in the editor. Refuses to discard unsaved changes unless told to.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "path", json { { "type", "string" }, { "description", "path to the .otbm file" } } }, { "discardUnsaved", json { { "type", "boolean" }, { "description", "abandon unsaved changes in the current map" } } } } },
						   { "required", json::array({ "path" }) } },
					   true,
					   toolMapOpen });

		registry.add({ "map_new",
					   "Create a new empty map, optionally with a name, description and dimensions.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "name", json { { "type", "string" } } }, { "description", json { { "type", "string" } } }, { "width", json { { "type", "integer" } } }, { "height", json { { "type", "integer" } } }, { "discardUnsaved", json { { "type", "boolean" } } } } } },
					   true,
					   toolMapNew });

		registry.add({ "map_properties",
					   "Read or change the map's name, description, dimensions and sidecar filenames. "
					   "Called with no fields it just reports the current values.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "name", json { { "type", "string" } } }, { "description", json { { "type", "string" } } }, { "width", json { { "type", "integer" } } }, { "height", json { { "type", "integer" } } }, { "houseFilename", json { { "type", "string" } } }, { "spawnMonsterFilename", json { { "type", "string" } } }, { "spawnNpcFilename", json { { "type", "string" } } }, { "zoneFilename", json { { "type", "string" } } }, { "allowShrink", json { { "type", "boolean" }, { "description", "permit dimensions smaller than the current ones" } } } } } },
					   true,
					   toolMapProperties });

		registry.add({ "map_import",
					   "Merge another .otbm into the open map at an offset - how a world gets assembled from separately built pieces. "
					   "Houses and spawns each get their own merge policy. Not undoable as one step, so it requires confirm=true.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "path", json { { "type", "string" }, { "description", "the .otbm to merge in" } } }, { "offsetX", json { { "type", "integer" } } }, { "offsetY", json { { "type", "integer" } } }, { "offsetZ", json { { "type", "integer" } } }, { "houses", json { { "type", "string" }, { "enum", json::array({ "smart", "merge", "insert", "none" }) }, { "description", "default smart" } } }, { "monsterSpawns", json { { "type", "string" }, { "enum", json::array({ "smart", "merge", "insert", "none" }) } } }, { "npcSpawns", json { { "type", "string" }, { "enum", json::array({ "smart", "merge", "insert", "none" }) } } }, { "confirm", json { { "type", "boolean" }, { "description", "required; acknowledges this is not a single undo step" } } } } },
						   { "required", json::array({ "path" }) } },
					   true,
					   toolMapImport });

		registry.add({ "map_screenshot",
					   "Capture what the editor is actually drawing: real sprites, current floor, current view settings. "
					   "Higher fidelity than map_render_region, which only paints minimap colours. "
					   "Optionally moves the view first.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "position", positionSchema() }, { "zoom", json { { "type", "number" }, { "description", "1.0 is normal; higher values zoom out" } } } } } },
					   false,
					   toolMapScreenshot });
	}

} // namespace mcp
