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
// Letting the model see the palette. Picking "the right brush for a river"
// is impossible from a list of names, so these tools expose the tileset
// structure the editor's palette is built from, and render brushes as images.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "mcp_tools.h"

#include "../basemap.h"
#include "../brush.h"
#include "../common.h"
#include "../const.h"
#include "../editor.h"
#include "../graphics.h"
#include "../gui.h"
#include "../items.h"
#include "../map.h"
#include "../materials.h"
#include "../tile.h"
#include "../tileset.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace mcp {

	namespace {

		const char* categoryName(TilesetCategoryType type) {
			switch (type) {
				case TILESET_TERRAIN:
					return "terrain";
				case TILESET_DOODAD:
					return "doodad";
				case TILESET_ITEM:
					return "item";
				case TILESET_RAW:
					return "raw";
				case TILESET_MONSTER:
					return "monster";
				case TILESET_NPC:
					return "npc";
				case TILESET_HOUSE:
					return "house";
				case TILESET_WAYPOINT:
					return "waypoint";
				case TILESET_ZONES:
					return "zones";
				default:
					return "unknown";
			}
		}

		TilesetCategoryType categoryFromName(const std::string &name) {
			if (name.empty()) {
				return TILESET_UNKNOWN;
			}
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
			throw McpError("category must be one of: terrain, doodad, item, raw, monster, npc, house");
		}

		std::string brushKind(const Brush* brush) {
			if (brush->isGround()) {
				return "ground";
			}
			if (brush->isWall()) {
				return "wall";
			}
			if (brush->isCarpet()) {
				return "carpet";
			}
			if (brush->isTable()) {
				return "table";
			}
			if (brush->isDoodad()) {
				return "doodad";
			}
			if (brush->isDoor()) {
				return "door";
			}
			if (brush->isHouse()) {
				return "house";
			}
			if (brush->isHouseExit()) {
				return "house exit";
			}
			if (brush->isSpawnMonster()) {
				return "monster spawn";
			}
			if (brush->isSpawnNpc()) {
				return "npc spawn";
			}
			if (brush->isMonster()) {
				return "monster";
			}
			if (brush->isNpc()) {
				return "npc";
			}
			if (brush->isWaypoint()) {
				return "waypoint";
			}
			if (brush->isEraser()) {
				return "eraser";
			}
			if (brush->isRaw()) {
				return "raw";
			}
			return "other";
		}

		// Which tilesets and categories a brush appears under, which is how a
		// human finds it in the palette.
		json brushTilesets(const Brush* brush) {
			json out = json::array();
			for (const auto &[name, tileset] : g_materials.tilesets) {
				if (!tileset) {
					continue;
				}
				for (const TilesetCategory* category : tileset->categories) {
					if (category && category->containsBrush(const_cast<Brush*>(brush))) {
						out.push_back(json { { "tileset", name }, { "category", categoryName(category->getType()) } });
					}
				}
			}
			return out;
		}

		json brushSummary(const Brush* brush, bool withTilesets) {
			json out {
				{ "name", brush->getName() },
				{ "kind", brushKind(brush) },
				{ "lookId", brush->getLookID() },
				{ "needsBorders", brush->needBorders() },
				{ "canDrag", brush->canDrag() },
				{ "oneSizeFitsAll", brush->oneSizeFitsAll() }
			};
			if (withTilesets) {
				out["foundIn"] = brushTilesets(brush);
			}
			return out;
		}

		json toolTilesetList(const json &params) {
			const std::string filter = as_lower_str(readString(params, "name"));
			const bool includeBrushes = params.value("includeBrushes", false);
			const TilesetCategoryType wanted = categoryFromName(readString(params, "category"));
			const int limit = readInt(params, "limit", 100, 1, 1000);

			json tilesets = json::array();
			for (const auto &[name, tileset] : g_materials.tilesets) {
				if (!tileset) {
					continue;
				}
				if (!filter.empty() && as_lower_str(name).find(filter) == std::string::npos) {
					continue;
				}

				json categories = json::array();
				size_t totalBrushes = 0;
				for (const TilesetCategory* category : tileset->categories) {
					if (!category || category->size() == 0) {
						continue;
					}
					if (wanted != TILESET_UNKNOWN && category->getType() != wanted) {
						continue;
					}
					totalBrushes += category->size();

					json entry { { "category", categoryName(category->getType()) }, { "brushCount", category->size() } };
					if (includeBrushes) {
						json names = json::array();
						for (const Brush* brush : category->brushlist) {
							if (brush) {
								names.push_back(brush->getName());
							}
						}
						entry["brushes"] = std::move(names);
					}
					categories.push_back(std::move(entry));
				}

				if (categories.empty()) {
					continue;
				}
				tilesets.push_back(json { { "name", name }, { "brushCount", totalBrushes }, { "categories", std::move(categories) } });
				if (static_cast<int>(tilesets.size()) >= limit) {
					break;
				}
			}

			return jsonResult(json {
				{ "count", tilesets.size() },
				{ "tilesets", std::move(tilesets) },
				{ "note", "tilesets mirror the editor palette; pass includeBrushes=true to list the brush names inside" } });
		}

		json toolBrushInfo(const json &params) {
			const std::string name = readString(params, "name");
			if (name.empty()) {
				throw McpError("name is required; use brush_list or tileset_list to find one");
			}

			Brush* brush = g_brushes.getBrush(name);
			if (!brush) {
				throw McpError(fmt::format("no brush named '{}'", name));
			}

			json out = brushSummary(brush, true);

			// A ground brush's look id is the item the palette shows; it is
			// also the best single answer to "what does this brush lay down".
			const int lookId = brush->getLookID();
			if (lookId > 0) {
				const ItemType &type = g_items[static_cast<uint16_t>(lookId)];
				if (type.id != 0) {
					out["lookItem"] = json { { "id", type.id }, { "name", type.name } };
				}
			}

			out["usage"] = brush->isGround() || brush->isWall() || brush->isCarpet() || brush->isTable()
				? "apply with brush_apply; borders and connections are computed for you"
				: "apply with brush_apply";
			out["previewHint"] = "call brush_preview to see what it actually looks like laid down";

			return jsonResult(out);
		}

		// Paints the brush onto a scratch map and renders that, so the preview
		// shows the real result - borders, wall corners, carpet edges - rather
		// than one isolated sprite.
		json toolBrushPreview(const json &params) {
			const std::string name = readString(params, "name");
			if (name.empty()) {
				throw McpError("name is required");
			}

			Brush* brush = g_brushes.getBrush(name);
			if (!brush) {
				throw McpError(fmt::format("no brush named '{}'", name));
			}

			const int size = readInt(params, "size", 7, 1, 24);
			const int pixelsPerTile = readInt(params, "pixelsPerTile", 32, 8, 64);
			const bool borderize = params.value("borderize", true);

			// A throwaway map: nothing here touches the map the user has open.
			BaseMap scratch;
			bool alt = false;

			for (int y = 0; y < size; ++y) {
				for (int x = 0; x < size; ++x) {
					Tile* tile = scratch.createTile(x, y, rme::MapGroundLayer);
					if (tile) {
						brush->draw(&scratch, tile, &alt);
					}
				}
			}
			if (borderize) {
				for (int y = 0; y < size; ++y) {
					for (int x = 0; x < size; ++x) {
						if (Tile* tile = scratch.getTile(x, y, rme::MapGroundLayer)) {
							tile->borderize(&scratch);
							tile->update();
						}
					}
				}
			}

			const wxImage image = renderTileRegion(scratch, Position(0, 0, rme::MapGroundLayer), size, size, pixelsPerTile);
			if (!image.IsOk()) {
				throw McpError("could not render the brush preview; are the client sprites loaded?");
			}

			return imageResult(encodePngBase64(image), "image/png");
		}

	} // namespace

	void registerBrushTools(ToolRegistry &registry) {
		registry.add({ "tileset_list",
					   "The editor palette, as the editor organises it: tilesets and their categories (terrain, doodad, item, raw, ...) "
					   "with the brushes inside. This is how to find the right brush for a river, a lake, a mountain or a road - "
					   "browse by tileset instead of guessing brush names.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "name", json { { "type", "string" }, { "description", "substring of the tileset name" } } }, { "category", json { { "type", "string" }, { "enum", json::array({ "terrain", "doodad", "item", "raw", "monster", "npc", "house" }) } } }, { "includeBrushes", json { { "type", "boolean" }, { "description", "list the brush names in each category" } } }, { "limit", json { { "type", "integer" }, { "description", "default 100" } } } } } },
					   false,
					   toolTilesetList });

		registry.add({ "brush_info",
					   "Everything about one brush: what kind it is, whether it auto-borders, the item it shows in the palette, "
					   "and which tilesets and categories it belongs to.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "name", json { { "type", "string" } } } } },
						   { "required", json::array({ "name" }) } },
					   false,
					   toolBrushInfo });

		registry.add({ "brush_preview",
					   "Render a PNG of what a brush actually produces, by painting it onto a scratch area and drawing the result "
					   "with real sprites - including the borders and connections it generates. "
					   "Use it to check that a brush is the water, grass or mountain you meant before painting the real map.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "name", json { { "type", "string" } } }, { "size", json { { "type", "integer" }, { "description", "preview patch side in tiles, 1-24, default 7" } } }, { "pixelsPerTile", json { { "type", "integer" }, { "description", "8-64, default 32 (native sprite size)" } } }, { "borderize", json { { "type", "boolean" }, { "description", "apply auto-borders, default true" } } } } },
						   { "required", json::array({ "name" }) } },
					   false,
					   toolBrushPreview });
	}

} // namespace mcp
