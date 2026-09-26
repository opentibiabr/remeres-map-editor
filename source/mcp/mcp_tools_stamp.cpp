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
// Composition: named region stamps, placed with rotation and mirroring.
// region_copy holds exactly one clipboard, which is enough to move a thing
// but not to build a village out of one house, or a symmetric arena out of
// one wing.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "mcp_tools.h"
#include "mcp_write.h"

#include "../basemap.h"
#include "../common.h"
#include "../const.h"
#include "../editor.h"
#include "../gui.h"
#include "../item.h"
#include "../items.h"
#include "../map.h"
#include "../tile.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mcp {

	namespace {

		// A stamp keeps its own deep-copied tiles, detached from any map, so it
		// survives the source area being edited or the map being closed.
		struct Stamp {
			int width = 0;
			int height = 0;
			int floors = 0;
			Position origin;
			std::string description;
			// Keyed by local offset so sparse areas stay cheap.
			std::map<std::tuple<int, int, int>, std::unique_ptr<Tile>> tiles;
		};

		// Session-scoped: stamps live as long as the editor runs. They are a
		// scratch workspace, not map data, so nothing is persisted.
		std::map<std::string, std::unique_ptr<Stamp>> &stampLibrary() {
			static std::map<std::string, std::unique_ptr<Stamp>> library;
			return library;
		}

		constexpr int64_t MAX_STAMP_TILES = 65536;

		std::string requireName(const json &params) {
			const std::string name = readString(params, "name");
			if (name.empty()) {
				throw McpError("name is required");
			}
			return name;
		}

		Stamp &requireStamp(const std::string &name) {
			auto &library = stampLibrary();
			const auto found = library.find(name);
			if (found == library.end()) {
				throw McpError(fmt::format("no stamp named '{}'; use stamp_manage list to see them", name));
			}
			return *found->second;
		}

		// Rotation is in quarter turns clockwise; mirroring happens first so
		// "mirror then rotate" is a predictable composition.
		void transformOffset(int &x, int &y, int width, int height, int quarterTurns, bool mirrorX, bool mirrorY, int &outWidth, int &outHeight) {
			if (mirrorX) {
				x = width - 1 - x;
			}
			if (mirrorY) {
				y = height - 1 - y;
			}

			outWidth = width;
			outHeight = height;
			for (int turn = 0; turn < quarterTurns; ++turn) {
				const int rotatedX = outHeight - 1 - y;
				const int rotatedY = x;
				x = rotatedX;
				y = rotatedY;
				std::swap(outWidth, outHeight);
			}
		}

		json stampSummary(const std::string &name, const Stamp &stamp) {
			return json {
				{ "name", name },
				{ "width", stamp.width },
				{ "height", stamp.height },
				{ "floors", stamp.floors },
				{ "tiles", stamp.tiles.size() },
				{ "capturedFrom", positionToJson(stamp.origin) },
				{ "description", stamp.description }
			};
		}

		json toolStampManage(const json &params) {
			const std::string op = readString(params, "op");
			if (op.empty()) {
				throw McpError("op is required");
			}

			auto &library = stampLibrary();

			if (op == "list") {
				json stamps = json::array();
				for (const auto &[name, stamp] : library) {
					stamps.push_back(stampSummary(name, *stamp));
				}
				return jsonResult(json {
					{ "count", stamps.size() },
					{ "stamps", std::move(stamps) },
					{ "note", "stamps live for this editor session only" } });
			}

			if (op == "delete") {
				const std::string name = requireName(params);
				if (library.erase(name) == 0) {
					throw McpError(fmt::format("no stamp named '{}'", name));
				}
				return jsonResult(json { { "op", op }, { "name", name }, { "remaining", library.size() } });
			}

			if (op == "capture") {
				Editor* editor = requireEditor();
				Map &map = editor->getMap();
				const std::string name = requireName(params);

				if (!params.contains("from") || !params.contains("to")) {
					throw McpError("capture needs from and to, marking two corners of the area to store");
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
				if (volume > MAX_STAMP_TILES) {
					throw McpError(fmt::format("that area is {} tiles, above the {} stamp limit", volume, MAX_STAMP_TILES));
				}

				auto stamp = std::make_unique<Stamp>();
				stamp->width = maxX - minX + 1;
				stamp->height = maxY - minY + 1;
				stamp->floors = maxZ - minZ + 1;
				stamp->origin = Position(minX, minY, minZ);
				stamp->description = readString(params, "description");

				for (int z = minZ; z <= maxZ; ++z) {
					for (int y = minY; y <= maxY; ++y) {
						for (int x = minX; x <= maxX; ++x) {
							Tile* source = map.getTile(x, y, z);
							if (!source || source->empty()) {
								continue;
							}
							// deepCopy needs a map to allocate from; the copy is
							// then owned by the stamp, not by any map.
							stamp->tiles.emplace(
								std::make_tuple(x - minX, y - minY, z - minZ),
								std::unique_ptr<Tile>(source->deepCopy(map))
							);
						}
					}
				}

				if (stamp->tiles.empty()) {
					throw McpError("that area is empty; there is nothing to capture");
				}

				json summary = stampSummary(name, *stamp);
				library[name] = std::move(stamp);
				summary["op"] = op;
				return jsonResult(summary);
			}

			throw McpError("op must be one of: capture, list, delete");
		}

		json toolStampPlace(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			const std::string name = requireName(params);
			const Stamp &stamp = requireStamp(name);

			if (!params.contains("at")) {
				throw McpError("at is required: where the stamp top-left corner should land");
			}
			const Position at = parsePosition(params["at"], "at");

			const int quarterTurns = ((readInt(params, "rotate", 0, -270, 270) / 90) % 4 + 4) % 4;
			const bool mirrorX = params.value("mirrorX", false);
			const bool mirrorY = params.value("mirrorY", false);
			const bool onlyIfEmpty = params.value("onlyIfEmpty", false);

			int placed = 0;
			int skipped = 0;
			int outOfBounds = 0;

			TileBatch batch(*editor, ACTION_PASTE_TILES);

			for (const auto &[offset, sourceTile] : stamp.tiles) {
				auto [localX, localY, localZ] = offset;
				int rotatedWidth = 0;
				int rotatedHeight = 0;
				int x = localX;
				int y = localY;
				transformOffset(x, y, stamp.width, stamp.height, quarterTurns, mirrorX, mirrorY, rotatedWidth, rotatedHeight);

				const Position target(at.x + x, at.y + y, at.z + localZ);
				if (target.x < 0 || target.y < 0 || target.x >= map.getWidth() || target.y >= map.getHeight() || target.z < 0 || target.z >= rme::MapLayers) {
					++outOfBounds;
					continue;
				}

				const Tile* existing = map.getTile(target);
				if (onlyIfEmpty && existing && !existing->empty()) {
					++skipped;
					continue;
				}

				// Copy again per placement: one stamp can be placed many times,
				// and each placement needs tiles of its own.
				// merge() steals the contents but not the shell, so the copy
				// has to be owned here or it leaks.
				Tile* destination = batch.edit(target);
				std::unique_ptr<Tile> incoming(sourceTile->deepCopy(map));
				destination->merge(incoming.get());
				destination->update();
				++placed;
			}

			const size_t changed = batch.commit();

			json out {
				{ "stamp", name },
				{ "at", positionToJson(at) },
				{ "rotate", quarterTurns * 90 },
				{ "mirrorX", mirrorX },
				{ "mirrorY", mirrorY },
				{ "tilesPlaced", placed },
				{ "tilesChanged", changed },
				{ "undoable", true }
			};
			if (skipped > 0) {
				out["tilesSkipped"] = skipped;
			}
			if (outOfBounds > 0) {
				out["tilesOutOfBounds"] = outOfBounds;
			}
			out["note"] = "run border_check over the placed area afterwards; a stamp carries its own borders, which may not match the terrain it lands on";
			return jsonResult(out);
		}

		// Mirroring or rotating in place, without going through a stamp: read
		// the area, then write it back transformed.
		json toolRegionTransform(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			if (!params.contains("from") || !params.contains("to")) {
				throw McpError("from and to are required, marking two corners of the area to transform");
			}
			const Position a = parsePosition(params["from"], "from");
			const Position b = parsePosition(params["to"], "to");

			const int minX = std::min(a.x, b.x);
			const int maxX = std::max(a.x, b.x);
			const int minY = std::min(a.y, b.y);
			const int maxY = std::max(a.y, b.y);
			const int minZ = std::min(a.z, b.z);
			const int maxZ = std::max(a.z, b.z);

			const int width = maxX - minX + 1;
			const int height = maxY - minY + 1;

			const int64_t volume = static_cast<int64_t>(width) * height * (maxZ - minZ + 1);
			if (volume > MAX_STAMP_TILES) {
				throw McpError(fmt::format("that area is {} tiles, above the {} limit", volume, MAX_STAMP_TILES));
			}

			const int quarterTurns = ((readInt(params, "rotate", 0, -270, 270) / 90) % 4 + 4) % 4;
			const bool mirrorX = params.value("mirrorX", false);
			const bool mirrorY = params.value("mirrorY", false);

			if (quarterTurns == 0 && !mirrorX && !mirrorY) {
				throw McpError("nothing to do: give rotate, mirrorX or mirrorY");
			}
			if (quarterTurns % 2 == 1 && width != height) {
				throw McpError(fmt::format(
					"a 90 or 270 degree rotation needs a square area; this one is {}x{}. Use stamp_manage capture plus stamp_place to rotate into a different spot.",
					width, height
				));
			}

			// Snapshot everything before writing, or the transform would read
			// tiles it has already overwritten.
			std::map<std::tuple<int, int, int>, std::unique_ptr<Tile>> snapshot;
			for (int z = minZ; z <= maxZ; ++z) {
				for (int y = minY; y <= maxY; ++y) {
					for (int x = minX; x <= maxX; ++x) {
						Tile* source = map.getTile(x, y, z);
						if (source && !source->empty()) {
							snapshot.emplace(
								std::make_tuple(x - minX, y - minY, z - minZ),
								std::unique_ptr<Tile>(source->deepCopy(map))
							);
						}
					}
				}
			}

			TileBatch batch(*editor);

			// Clear the whole area first so tiles that move away leave nothing.
			for (int z = minZ; z <= maxZ; ++z) {
				for (int y = minY; y <= maxY; ++y) {
					for (int x = minX; x <= maxX; ++x) {
						Tile* tile = batch.edit(Position(x, y, z));
						tile->clearGround();
						while (!tile->items.empty()) {
							delete tile->items.back();
							tile->items.pop_back();
						}
						tile->clearMonsters();
						delete tile->npc;
						tile->npc = nullptr;
						delete tile->spawnMonster;
						tile->spawnMonster = nullptr;
						delete tile->spawnNpc;
						tile->spawnNpc = nullptr;
					}
				}
			}

			int moved = 0;
			for (const auto &[offset, sourceTile] : snapshot) {
				auto [localX, localY, localZ] = offset;
				int rotatedWidth = 0;
				int rotatedHeight = 0;
				int x = localX;
				int y = localY;
				transformOffset(x, y, width, height, quarterTurns, mirrorX, mirrorY, rotatedWidth, rotatedHeight);

				Tile* destination = batch.edit(Position(minX + x, minY + y, minZ + localZ));
				std::unique_ptr<Tile> incoming(sourceTile->deepCopy(map));
				destination->merge(incoming.get());
				destination->update();
				++moved;
			}

			const size_t changed = batch.commit();
			return jsonResult(json {
				{ "region", json { { "from", json { { "x", minX }, { "y", minY }, { "z", minZ } } }, { "to", json { { "x", maxX }, { "y", maxY }, { "z", maxZ } } } } },
				{ "rotate", quarterTurns * 90 },
				{ "mirrorX", mirrorX },
				{ "mirrorY", mirrorY },
				{ "tilesMoved", moved },
				{ "tilesChanged", changed },
				{ "undoable", true },
				{ "note", "walls and borders are moved as-is, not re-oriented; run border_check, and brush_apply over walls if they look wrong" } });
		}

	} // namespace

	void registerStampTools(ToolRegistry &registry) {
		registry.add({ "stamp_manage",
					   "A named library of reusable map pieces for this editor session: capture an area under a name, list what is "
					   "stored, or delete one. Unlike region_copy, which holds a single clipboard, stamps let you keep a house, a "
					   "cave module and a tower side by side and place each many times.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "op", json { { "type", "string" }, { "enum", json::array({ "capture", "list", "delete" }) } } }, { "name", json { { "type", "string" } } }, { "from", positionSchema("for capture: one corner") }, { "to", positionSchema("for capture: the opposite corner") }, { "description", json { { "type", "string" }, { "description", "what this piece is, for your own later reference" } } } } },
						   { "required", json::array({ "op" }) } },
					   true,
					   toolStampManage });

		registry.add({ "stamp_place",
					   "Place a stored stamp, optionally rotated in 90 degree steps and mirrored. "
					   "Mirroring is applied before rotation. onlyIfEmpty leaves existing content alone, which is how you scatter "
					   "pieces without overwriting what is already there. Undoable.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "name", json { { "type", "string" } } }, { "at", positionSchema("where the stamp top-left corner lands") }, { "rotate", json { { "type", "integer" }, { "description", "0, 90, 180 or 270 degrees clockwise" } } }, { "mirrorX", json { { "type", "boolean" }, { "description", "flip horizontally" } } }, { "mirrorY", json { { "type", "boolean" }, { "description", "flip vertically" } } }, { "onlyIfEmpty", json { { "type", "boolean" }, { "description", "skip tiles that already have content" } } } } },
						   { "required", json::array({ "name", "at" }) } },
					   true,
					   toolStampPlace });

		registry.add({ "region_transform",
					   "Rotate or mirror an area in place - the other half of symmetric design, for arenas, temples and dungeon wings. "
					   "A 90 or 270 degree rotation needs a square area; use a stamp to rotate into a different shape or spot. "
					   "Walls and borders are moved rather than re-oriented, so check the result. Undoable.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "from", positionSchema("one corner") }, { "to", positionSchema("the opposite corner") }, { "rotate", json { { "type", "integer" }, { "description", "0, 90, 180 or 270 degrees clockwise" } } }, { "mirrorX", json { { "type", "boolean" } } }, { "mirrorY", json { { "type", "boolean" } } } } },
						   { "required", json::array({ "from", "to" }) } },
					   true,
					   toolRegionTransform });
	}

} // namespace mcp
