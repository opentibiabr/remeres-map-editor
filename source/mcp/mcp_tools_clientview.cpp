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
// What the game client actually shows a player standing somewhere - the
// editor's "Show ingame box", turned into something the model can reason
// about. The point is catching unmapped voids that a player would see from
// inside a cave or room, which break immersion and are invisible in the
// editor unless you go looking.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "mcp_tools.h"

#include "../basemap.h"
#include "../const.h"
#include "../editor.h"
#include "../gui.h"
#include "../item.h"
#include "../map.h"
#include "../position.h"
#include "../tile.h"

#include <algorithm>
#include <string>
#include <vector>

namespace mcp {

	namespace {

		// MapDrawer::DrawIngameBox frames ClientMapWidth+1 by ClientMapHeight+1
		// tiles with the player at half that, so the client shows 19x15 with
		// the player dead centre at (9, 7).
		constexpr int VIEW_WIDTH = rme::ClientMapWidth + 1;
		constexpr int VIEW_HEIGHT = rme::ClientMapHeight + 1;
		constexpr int VIEW_OFFSET_X = VIEW_WIDTH / 2;
		constexpr int VIEW_OFFSET_Y = VIEW_HEIGHT / 2;

		// Sweeping a region costs a client box per standing tile, so it gets a
		// tighter cap than the generic region guard.
		constexpr int64_t MAX_SCAN_TILES = 65536;

		struct ViewBox {
			int minX, maxX, minY, maxY, z;
		};

		ViewBox boxAround(const Position &position) {
			return ViewBox {
				position.x - VIEW_OFFSET_X,
				position.x - VIEW_OFFSET_X + VIEW_WIDTH - 1,
				position.y - VIEW_OFFSET_Y,
				position.y - VIEW_OFFSET_Y + VIEW_HEIGHT - 1,
				position.z
			};
		}

		json boxToJson(const ViewBox &box) {
			return json {
				{ "from", json { { "x", box.minX }, { "y", box.minY }, { "z", box.z } } },
				{ "to", json { { "x", box.maxX }, { "y", box.maxY }, { "z", box.z } } },
				{ "width", VIEW_WIDTH },
				{ "height", VIEW_HEIGHT }
			};
		}

		// A tile reads as "no map" to the player when it has no ground: the
		// client paints nothing there. Items without ground still float, but
		// the black hole is the missing ground.
		bool isVoid(Map &map, int x, int y, int z) {
			if (x < 0 || y < 0 || x >= map.getWidth() || y >= map.getHeight()) {
				return true;
			}
			const Tile* tile = map.getTile(x, y, z);
			return !tile || !tile->ground;
		}

		bool isStandable(Map &map, const Position &position) {
			const Tile* tile = map.getTile(position);
			return tile && tile->ground && !tile->isBlocking();
		}

		json toolClientView(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			if (!params.contains("position")) {
				throw McpError("position is required: where the player would be standing");
			}
			const Position position = parsePosition(params["position"], "position");
			const ViewBox box = boxAround(position);

			int64_t voidTiles = 0;
			json voidPositions = json::array();
			for (int y = box.minY; y <= box.maxY; ++y) {
				for (int x = box.minX; x <= box.maxX; ++x) {
					if (!isVoid(map, x, y, box.z)) {
						continue;
					}
					++voidTiles;
					if (voidPositions.size() < 128) {
						voidPositions.push_back(json { { "x", x }, { "y", y }, { "z", box.z } });
					}
				}
			}

			const int64_t total = static_cast<int64_t>(VIEW_WIDTH) * VIEW_HEIGHT;

			json out {
				{ "standingAt", positionToJson(position) },
				{ "standable", isStandable(map, position) },
				{ "clientView", boxToJson(box) },
				{ "visibleTiles", total },
				{ "voidTiles", voidTiles },
				{ "voidPercent", total > 0 ? (voidTiles * 100.0) / static_cast<double>(total) : 0.0 }
			};

			if (voidTiles > 0) {
				out["voidPositions"] = std::move(voidPositions);
				out["verdict"] = "a player standing here would see unmapped space";
				out["howToFix"] = "fill those positions with ground (tile_edit or brush_apply), or wall the area off so they fall outside the view";
			} else {
				out["verdict"] = "the client view is fully mapped from here";
			}

			if (params.value("render", false)) {
				// Draw exactly the box, so the model sees what the player sees.
				const int pixelsPerTile = readInt(params, "pixelsPerTile", 32, 8, 64);
				const wxImage image = renderTileRegion(map, Position(box.minX, box.minY, box.z), VIEW_WIDTH, VIEW_HEIGHT, pixelsPerTile);
				if (!image.IsOk()) {
					throw McpError("could not render the client view; are the client sprites loaded?");
				}
				return json {
					{ "content", json::array({ json { { "type", "text" }, { "text", out.dump(2) } }, json { { "type", "image" }, { "data", encodePngBase64(image) }, { "mimeType", "image/png" } } }) }
				};
			}

			return jsonResult(out);
		}

		json toolClientViewScan(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			if (!params.contains("from") || !params.contains("to")) {
				throw McpError("from and to are required, marking the area a player could walk through");
			}
			const Position a = parsePosition(params["from"], "from");
			const Position b = parsePosition(params["to"], "to");
			if (a.z != b.z) {
				throw McpError("scan one floor at a time: from.z and to.z must match");
			}

			const int minX = std::min(a.x, b.x);
			const int maxX = std::max(a.x, b.x);
			const int minY = std::min(a.y, b.y);
			const int maxY = std::max(a.y, b.y);
			const int z = a.z;

			const int64_t area = static_cast<int64_t>(maxX - minX + 1) * (maxY - minY + 1);
			if (area > MAX_SCAN_TILES) {
				throw McpError(fmt::format(
					"the area is {} tiles, above the {} scan limit - each standing tile costs a {}x{} check. Narrow it.",
					area, MAX_SCAN_TILES, VIEW_WIDTH, VIEW_HEIGHT
				));
			}

			// Build a void/standable grid once over the area plus the view
			// margin, then the per-tile checks are array reads instead of
			// hundreds of thousands of quadtree lookups.
			const int gridMinX = minX - VIEW_OFFSET_X;
			const int gridMinY = minY - VIEW_OFFSET_Y;
			const int gridWidth = (maxX - minX + 1) + VIEW_WIDTH;
			const int gridHeight = (maxY - minY + 1) + VIEW_HEIGHT;

			std::vector<uint8_t> voidGrid(static_cast<size_t>(gridWidth) * gridHeight, 1);
			std::vector<uint8_t> standGrid(static_cast<size_t>(gridWidth) * gridHeight, 0);

			for (int gy = 0; gy < gridHeight; ++gy) {
				for (int gx = 0; gx < gridWidth; ++gx) {
					const int x = gridMinX + gx;
					const int y = gridMinY + gy;
					const size_t index = static_cast<size_t>(gy) * gridWidth + gx;

					if (x < 0 || y < 0 || x >= map.getWidth() || y >= map.getHeight()) {
						continue;
					}
					const Tile* tile = map.getTile(x, y, z);
					if (!tile || !tile->ground) {
						continue;
					}
					voidGrid[index] = 0;
					standGrid[index] = tile->isBlocking() ? 0 : 1;
				}
			}

			const int worstLimit = readInt(params, "limit", 50, 1, 500);
			const int minVoid = readInt(params, "minVoidTiles", 1, 1, VIEW_WIDTH * VIEW_HEIGHT);

			std::vector<std::pair<Position, int>> offenders;
			std::vector<uint8_t> seenVoid(static_cast<size_t>(gridWidth) * gridHeight, 0);
			int64_t standableTiles = 0;
			int64_t exposedTiles = 0;
			int64_t visibleVoidCount = 0;

			for (int y = minY; y <= maxY; ++y) {
				for (int x = minX; x <= maxX; ++x) {
					const size_t standIndex = static_cast<size_t>(y - gridMinY) * gridWidth + (x - gridMinX);
					if (!standGrid[standIndex]) {
						continue;
					}
					++standableTiles;

					int voidsHere = 0;
					for (int vy = y - VIEW_OFFSET_Y; vy <= y - VIEW_OFFSET_Y + VIEW_HEIGHT - 1; ++vy) {
						for (int vx = x - VIEW_OFFSET_X; vx <= x - VIEW_OFFSET_X + VIEW_WIDTH - 1; ++vx) {
							const size_t index = static_cast<size_t>(vy - gridMinY) * gridWidth + (vx - gridMinX);
							if (!voidGrid[index]) {
								continue;
							}
							++voidsHere;
							// Every void tile is counted once, however many
							// standing tiles can see it - that is the list of
							// holes actually worth filling.
							if (!seenVoid[index]) {
								seenVoid[index] = 1;
								++visibleVoidCount;
							}
						}
					}

					if (voidsHere >= minVoid) {
						++exposedTiles;
						offenders.emplace_back(Position(x, y, z), voidsHere);
					}
				}
			}

			std::sort(offenders.begin(), offenders.end(), [](const auto &left, const auto &right) {
				return left.second > right.second;
			});

			json worst = json::array();
			for (size_t i = 0; i < offenders.size() && static_cast<int>(i) < worstLimit; ++i) {
				worst.push_back(json {
					{ "position", positionToJson(offenders[i].first) },
					{ "voidTilesVisible", offenders[i].second } });
			}

			// The holes themselves, capped: this is the fix list.
			json holes = json::array();
			if (params.value("listVoidTiles", true)) {
				for (int gy = 0; gy < gridHeight && holes.size() < 500; ++gy) {
					for (int gx = 0; gx < gridWidth && holes.size() < 500; ++gx) {
						const size_t index = static_cast<size_t>(gy) * gridWidth + gx;
						if (seenVoid[index]) {
							holes.push_back(json { { "x", gridMinX + gx }, { "y", gridMinY + gy }, { "z", z } });
						}
					}
				}
			}

			json out {
				{ "area", json { { "from", json { { "x", minX }, { "y", minY }, { "z", z } } }, { "to", json { { "x", maxX }, { "y", maxY }, { "z", z } } } } },
				{ "clientViewSize", json { { "width", VIEW_WIDTH }, { "height", VIEW_HEIGHT } } },
				{ "standableTiles", standableTiles },
				{ "standableTilesSeeingVoid", exposedTiles },
				{ "distinctVoidTilesVisible", visibleVoidCount },
				{ "worstPositions", std::move(worst) }
			};

			if (!holes.empty()) {
				out["voidTilesToFill"] = std::move(holes);
			}

			if (standableTiles == 0) {
				out["verdict"] = "nothing in this area is standable, so a player never sees it";
			} else if (exposedTiles == 0) {
				out["verdict"] = "no unmapped space is visible from anywhere a player can stand here";
			} else {
				out["verdict"] = fmt::format(
					"{} of {} standable tiles show unmapped space; {} distinct tiles need ground",
					exposedTiles, standableTiles, visibleVoidCount
				);
				out["howToFix"] = "fill voidTilesToFill with ground, or extend walls so those tiles fall outside the player's view";
			}

			return jsonResult(out);
		}

	} // namespace

	void registerClientViewTools(ToolRegistry &registry) {
		registry.add({ "client_view",
					   "What the game client shows a player standing at one position - the editor's Show Ingame Box, as data. "
					   "Reports the 19x15 tile viewport and any unmapped tiles inside it, which is what breaks immersion when "
					   "a player reaches the edge of a cave or room. Pass render=true to also get a PNG of exactly that view.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "position", positionSchema("where the player would be standing") }, { "render", json { { "type", "boolean" }, { "description", "also return a PNG of the client view" } } }, { "pixelsPerTile", json { { "type", "integer" }, { "description", "for render, 8-64, default 32" } } } } },
						   { "required", json::array({ "position" }) } },
					   false,
					   toolClientView });

		registry.add({ "client_view_scan",
					   "Walk every standable tile in an area and find where a player would see unmapped space. "
					   "Returns the worst spots and the distinct tiles that need ground - the fix list for a cave, dungeon or "
					   "room whose edges were never closed off. Run it after carving out any new interior.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "from", positionSchema("one corner of the walkable area") }, { "to", positionSchema("the opposite corner; same floor") }, { "minVoidTiles", json { { "type", "integer" }, { "description", "only report standing tiles seeing at least this many void tiles, default 1" } } }, { "limit", json { { "type", "integer" }, { "description", "worst positions returned, default 50" } } }, { "listVoidTiles", json { { "type", "boolean" }, { "description", "include the tiles that need ground, default true" } } } } },
						   { "required", json::array({ "from", "to" }) } },
					   false,
					   toolClientViewScan });
	}

} // namespace mcp
