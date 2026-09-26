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

#ifndef RME_MCP_WRITE_H
#define RME_MCP_WRITE_H

#include "mcp_common.h"

#include "../action.h"
#include "../position.h"

#include <map>

class Editor;
class Tile;

namespace mcp {

	// Collects tile edits into one undoable Action.
	//
	// This follows Editor::drawInternal (editor.cpp): build a deep copy of each
	// tile, mutate the copy, and hand it to the Action. Action::commit swaps the
	// copies in and already reconciles house membership and monster/npc spawns
	// on both commit and undo (action.cpp), so callers only touch the tile.
	//
	// The alternative - LuaTransaction's swap-on-the-live-map approach - needs
	// manual spawn/house/selection bookkeeping, which is exactly what this
	// avoids.
	class TileBatch {
	public:
		explicit TileBatch(Editor &editor, ActionIdentifier type = ACTION_MCP);
		~TileBatch();

		TileBatch(const TileBatch &) = delete;
		TileBatch &operator=(const TileBatch &) = delete;

		// The editable copy for this position, allocating the tile if the map
		// has none there. Repeated calls for the same position return the same
		// copy: an Action must never hold two Changes for one position.
		Tile* edit(const Position &position);

		// Applies the collected changes as a single undo step and refreshes the
		// view. Returns how many tiles were touched. Safe to call once; a batch
		// that collected nothing commits nothing.
		size_t commit();

		bool empty() const noexcept {
			return pending.empty();
		}

	private:
		Editor &editor;
		Action* action;
		std::map<Position, Tile*> pending;
		bool committed = false;
	};

} // namespace mcp

#endif // RME_MCP_WRITE_H
