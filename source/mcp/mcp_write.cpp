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

#include "mcp_write.h"

#include "../basemap.h"
#include "../brush.h"
#include "../doodad_brush.h"
#include "../editor.h"
#include "../ground_brush.h"
#include "../gui.h"
#include "../item.h"
#include "../items.h"
#include "../map.h"
#include "../tile.h"

#include <algorithm>
#include <set>
#include <vector>

namespace mcp {

	TileBatch::TileBatch(Editor &editor, ActionIdentifier type) :
		editor(editor),
		action(editor.createAction(type)) { }

	TileBatch::~TileBatch() {
		if (committed) {
			return;
		}
		// Never committed (a handler threw mid-batch): drop the copies and the
		// action so the map is left exactly as it was.
		for (auto &[position, tile] : pending) {
			delete tile;
		}
		delete action;
	}

	Tile* TileBatch::edit(const Position &position) {
		const auto existing = pending.find(position);
		if (existing != pending.end()) {
			return existing->second;
		}

		Map &map = editor.getMap();
		TileLocation* location = map.createTileL(position);

		Tile* current = location->get();
		Tile* copy = current ? current->deepCopy(map) : map.allocator(location);

		pending.emplace(position, copy);
		return copy;
	}

	size_t TileBatch::commit(const TilePasses &passes) {
		committed = true;

		if (pending.empty()) {
			delete action;
			return 0;
		}

		const size_t count = pending.size();
		std::set<Position> touched;
		for (auto &[position, tile] : pending) {
			action->addChange(newd Change(tile));
			touched.insert(position);
		}
		pending.clear();

		if (!passes.any()) {
			editor.addAction(action);
		} else {
			Map &map = editor.getMap();
			BatchAction* batch = editor.createBatch(action->getType());
			batch->addAndCommitAction(action);

			std::set<Position> targets;
			for (const Position &position : touched) {
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						const Position neighbour(position.x + dx, position.y + dy, position.z);
						if (neighbour.isValid()) {
							targets.insert(neighbour);
						}
					}
				}
			}

			Action* fixup = editor.createAction(batch);
			for (const Position &position : targets) {
				const Tile* current = map.getTile(position);
				if (!current) {
					// A ground brush can put border items on a tile that holds
					// nothing yet (an outer border to "none", the sea's shore), so
					// borderize empty neighbours too and keep the ones that gain
					// content, as Editor::drawInternal does.
					if (passes.borderize) {
						Tile* fresh = map.allocator(map.createTileL(position));
						fresh->borderize(&map);
						if (fresh->size() > 0) {
							fixup->addChange(newd Change(fresh));
						} else {
							delete fresh;
						}
					}
					continue;
				}
				Tile* copy = current->deepCopy(map);
				if (passes.wallize) {
					copy->wallize(&map);
				}
				if (passes.tableize) {
					copy->tableize(&map);
				}
				if (passes.carpetize) {
					copy->carpetize(&map);
				}
				if (passes.borderize) {
					copy->borderize(&map);
				}
				copy->update();
				fixup->addChange(newd Change(copy));
			}
			batch->addAndCommitAction(fixup);
			editor.addBatch(batch);
		}
		editor.getMap().doChange();
		g_gui.RefreshView();

		return count;
	}

	BrushDrawParam::BrushDrawParam(Brush &brush, bool alt) :
		flag(alt) {
		if (brush.isGround()) {
			// alt on a ground brush means "only where no ground brush is set yet".
			param = alt ? &ground : nullptr;
		} else if (brush.isDoodad()) {
			param = &variation;
		} else if (brush.isWall() || brush.isRaw()) {
			param = &flag;
		}
	}

	void requireApplicableBrush(Brush &brush, const std::string &name, bool erase) {
		// The house exit and waypoint brushes assert in both draw and undraw
		// (the editor never calls them), so neither direction is allowed.
		if (brush.isHouseExit() || brush.isWaypoint()) {
			throw McpError(fmt::format("brush '{}' is not supported here; use house_manage set_exit or waypoint_manage", name));
		}
		if (erase) {
			return;
		}

		// Drawing needs context this tool does not supply: an int (spawn size or
		// time) for creature/spawn brushes, a selected house for the house brush.
		if (brush.isMonster() || brush.isSpawnMonster() || brush.isNpc() || brush.isSpawnNpc() || brush.isHouse()) {
			throw McpError(fmt::format("brush '{}' cannot be drawn here; use spawn_manage or house_manage assign_tiles", name));
		}

		// A composite doodad is a multi-tile pattern placed through a separate
		// path; the single-tile draw would silently skip it.
		if (brush.isDoodad() && brush.asDoodad() && brush.asDoodad()->hasCompositeObjects(0)) {
			throw McpError(fmt::format("doodad brush '{}' places multi-tile composites, which cannot be placed here; build it with tile_edit or stamp_place", name));
		}
	}

	int64_t replaceItemOnTile(Tile* tile, uint16_t fromId, uint16_t toId) {
		int64_t matched = 0;

		if (tile->ground && tile->ground->getID() == fromId) {
			++matched;
			if (toId == 0) {
				tile->clearGround();
			} else {
				transformItem(tile->ground, toId, tile);
			}
		}

		// transformItem edits the stack in place, so collect the matches first.
		std::vector<Item*> matches;
		for (Item* item : tile->items) {
			if (item->getID() == fromId) {
				matches.push_back(item);
			}
		}
		for (Item* item : matches) {
			++matched;
			if (toId != 0) {
				transformItem(item, toId, tile);
				continue;
			}
			const auto found = std::find(tile->items.begin(), tile->items.end(), item);
			delete *found;
			tile->items.erase(found);
		}
		return matched;
	}

} // namespace mcp
