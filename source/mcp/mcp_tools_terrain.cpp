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
// Terrain quality. Where two different ground brushes meet without a border
// item between them the client shows a hard seam, and on a large or
// generated map that is impossible to spot by eye.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "mcp_server.h"
#include "mcp_tools.h"
#include "mcp_write.h"

#include "../basemap.h"
#include "../brush.h"
#include "../common.h"
#include "../const.h"
#include "../editor.h"
#include "../ground_brush.h"
#include "../gui.h"
#include "../item.h"
#include "../items.h"
#include "../map.h"
#include "../tile.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mcp {

	namespace {

		constexpr size_t MAX_SEAM_SAMPLES = 12;

		struct SeamGroup {
			int64_t count = 0;
			int zOrderA = 0;
			int zOrderB = 0;
			json samples = json::array();
		};

		json toolBorderCheck(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			if (!params.contains("from") || !params.contains("to")) {
				throw McpError("from and to are required, marking two corners of the area to check");
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

			// Grouping by brush pair is the point: "grass meets sand badly in
			// 240 places" is one fixable problem, not 240 separate ones.
			std::map<std::string, SeamGroup> seams;
			std::set<Position> seamTiles;

			int64_t tilesWithGround = 0;
			int64_t rawGroundTiles = 0;
			json rawGroundSamples = json::array();
			std::map<uint16_t, int64_t> rawGroundIds;

			// Only the four orthogonal neighbours: a diagonal-only difference
			// is a corner, which autoborder handles as part of the adjacent
			// straight edges, so counting it separately would double-report.
			static constexpr int DX[4] = { 0, 0, -1, 1 };
			static constexpr int DY[4] = { -1, 1, 0, 0 };

			for (int z = minZ; z <= maxZ; ++z) {
				for (int y = minY; y <= maxY; ++y) {
					for (int x = minX; x <= maxX; ++x) {
						Tile* tile = map.getTile(x, y, z);
						if (!tile || !tile->ground) {
							continue;
						}
						++tilesWithGround;

						GroundBrush* brushHere = tile->getGroundBrush();
						if (!brushHere) {
							// Ground placed as a raw item id belongs to no
							// brush, so autoborder can never touch it. This is
							// the usual root cause of seams on a generated map.
							++rawGroundTiles;
							const uint16_t id = tile->ground->getID();
							++rawGroundIds[id];
							if (rawGroundSamples.size() < MAX_SEAM_SAMPLES) {
								rawGroundSamples.push_back(positionToJson(tile->getPosition()));
							}
							continue;
						}

						for (int i = 0; i < 4; ++i) {
							Tile* neighbour = map.getTile(x + DX[i], y + DY[i], z);
							if (!neighbour || !neighbour->ground) {
								// A missing neighbour is a hole, not a seam;
								// client_view_scan is what reports those.
								continue;
							}
							GroundBrush* brushThere = neighbour->getGroundBrush();
							if (!brushThere || brushThere == brushHere) {
								continue;
							}

							// Autoborder puts the border item on one of the two
							// tiles; if neither carries one, the transition is
							// a hard edge.
							if (tile->hasBorders() || neighbour->hasBorders()) {
								continue;
							}

							std::string nameHere = brushHere->getName();
							std::string nameThere = brushThere->getName();
							int zHere = brushHere->getZ();
							int zThere = brushThere->getZ();
							if (nameThere < nameHere) {
								std::swap(nameHere, nameThere);
								std::swap(zHere, zThere);
							}

							SeamGroup &group = seams[nameHere + " <-> " + nameThere];
							++group.count;
							group.zOrderA = zHere;
							group.zOrderB = zThere;
							if (group.samples.size() < MAX_SEAM_SAMPLES) {
								group.samples.push_back(positionToJson(tile->getPosition()));
							}
							seamTiles.insert(tile->getPosition());
						}
					}
				}
			}

			std::vector<std::pair<std::string, SeamGroup*>> ranked;
			ranked.reserve(seams.size());
			for (auto &[pair, group] : seams) {
				ranked.emplace_back(pair, &group);
			}
			std::sort(ranked.begin(), ranked.end(), [](const auto &left, const auto &right) {
				return left.second->count > right.second->count;
			});

			json byBrushPair = json::array();
			int64_t totalSeams = 0;
			for (const auto &[pair, group] : ranked) {
				totalSeams += group->count;
				byBrushPair.push_back(json {
					{ "brushes", pair },
					{ "edges", group->count },
					{ "zOrders", json { { "lower", std::min(group->zOrderA, group->zOrderB) }, { "higher", std::max(group->zOrderA, group->zOrderB) } } },
					{ "samplePositions", group->samples } });
			}

			json out {
				{ "region", json { { "from", json { { "x", minX }, { "y", minY }, { "z", minZ } } }, { "to", json { { "x", maxX }, { "y", maxY }, { "z", maxZ } } } } },
				{ "tilesWithGround", tilesWithGround },
				{ "seamEdges", totalSeams },
				{ "seamTiles", seamTiles.size() },
				{ "byBrushPair", std::move(byBrushPair) }
			};

			if (rawGroundTiles > 0) {
				json ids = json::array();
				std::vector<std::pair<uint16_t, int64_t>> sortedIds(rawGroundIds.begin(), rawGroundIds.end());
				std::sort(sortedIds.begin(), sortedIds.end(), [](const auto &l, const auto &r) {
					return l.second > r.second;
				});
				for (size_t i = 0; i < sortedIds.size() && i < 15; ++i) {
					const ItemType &type = g_items[sortedIds[i].first];
					ids.push_back(json {
						{ "id", sortedIds[i].first },
						{ "name", type.id != 0 ? type.name : std::string("(unknown)") },
						{ "tiles", sortedIds[i].second } });
				}

				out["groundWithoutBrush"] = json {
					{ "tiles", rawGroundTiles },
					{ "itemIds", std::move(ids) },
					{ "samplePositions", std::move(rawGroundSamples) },
					{ "why", "these grounds were placed as raw item ids, so they belong to no brush and autoborder can never border them" },
					{ "howToFix", "lay ground with brush_apply instead of raw ids in tile_edit; use tileset_list and brush_info to pick the brush" }
				};
			}

			// Fixing is just borderize over the affected tiles, in one undo
			// step - the same thing the editor's Borderize Selection does.
			if (params.value("fix", false)) {
				// The scan itself is read-only, so the tool is not declared as
				// mutating; only the fix half needs the write gate.
				if (!Server::get().isWriteAllowed()) {
					throw McpError("fix=true edits the map, but the editor's MCP panel is in read-only mode; ask the user to enable writing");
				}
				if (seamTiles.empty()) {
					out["fixed"] = 0;
				} else {
					TileBatch batch(*editor, ACTION_BORDERIZE);
					for (const Position &position : seamTiles) {
						Tile* tile = batch.edit(position);
						tile->borderize(&map);
						tile->update();
					}
					out["fixed"] = batch.commit();
					out["fixNote"] = "borderized the seam tiles in one undoable step; re-run to confirm, and check automagic is on in editor_settings";
				}
			}

			if (totalSeams == 0 && rawGroundTiles == 0) {
				out["verdict"] = "every ground transition in this area is bordered";
			} else if (totalSeams == 0) {
				out["verdict"] = "no seams between brushes, but some ground cannot be bordered at all - see groundWithoutBrush";
			} else {
				out["verdict"] = fmt::format("{} hard transitions across {} tiles", totalSeams, seamTiles.size());
				if (!params.value("fix", false)) {
					out["howToFix"] = "call again with fix=true to borderize them, or use selection_op borderize over the area";
				}
			}

			return jsonResult(out);
		}

	} // namespace

	void registerTerrainTools(ToolRegistry &registry) {
		registry.add({ "border_check",
					   "Find hard seams in terrain: places where two different ground brushes touch with no border item between them, "
					   "which the client renders as an abrupt edge. Results are grouped by brush pair, so one broken transition reads "
					   "as one problem rather than hundreds of positions. "
					   "Also reports ground placed as raw item ids, which belongs to no brush and can never be auto-bordered - "
					   "the usual reason generated terrain looks wrong. Pass fix=true to borderize the affected tiles in one undo step.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "from", positionSchema("one corner of the area to check") }, { "to", positionSchema("the opposite corner") }, { "fix", json { { "type", "boolean" }, { "description", "borderize the seam tiles, undoable" } } } } },
						   { "required", json::array({ "from", "to" }) } },
					   // Read-only by default; the fix path checks the write gate itself.
					   false,
					   toolBorderCheck });
	}

} // namespace mcp
