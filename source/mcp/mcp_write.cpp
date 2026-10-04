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
#include "../editor.h"
#include "../gui.h"
#include "../map.h"
#include "../tile.h"

#include <set>

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

} // namespace mcp
