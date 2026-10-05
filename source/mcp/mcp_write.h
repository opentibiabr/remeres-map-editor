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

#include <cstdint>
#include <map>
#include <string>
#include <utility>

class Brush;
class Editor;
class GroundBrush;
class Tile;

namespace mcp {

	// Which neighbour-dependent recalculations to run after the edits land.
	// Namespace scope, not nested: a nested type with default member
	// initializers cannot be used as a default argument inside its own class.
	struct TilePasses {
		bool borderize = false;
		bool wallize = false;
		bool tableize = false;
		bool carpetize = false;

		bool any() const noexcept {
			return borderize || wallize || tableize || carpetize;
		}
	};

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
		//
		// The passes recalculate the edited tiles and their 8 neighbours. That
		// has to happen after the edits are on the map (they read neighbours from
		// it), so it runs as a second action in the same undo step, like
		// Editor::draw's border pass.
		size_t commit(const TilePasses &passes = TilePasses());

		bool empty() const noexcept {
			return pending.empty();
		}

	private:
		Editor &editor;
		Action* action;
		std::map<Position, Tile*> pending;
		bool committed = false;
	};

	// Replaces every item with id fromId on the tile (ground and stack) by toId,
	// keeping the item's attributes (count, text, uid/aid, container contents,
	// teleport destination...) the way the editor's Replace Items does. toId 0
	// deletes them instead. Returns how many items matched.
	int64_t replaceItemOnTile(Tile* tile, uint16_t fromId, uint16_t toId);

	// Deletes repeated item ids on the tile, like the editor's duplicate cleaner:
	// ground and elevated items are left alone, and so is anything carrying an
	// action or unique id (distinct quest items often share a base id). Returns
	// how many items were deleted.
	int64_t removeDuplicateItems(Tile* tile);

	// Brush::draw takes an untyped parameter whose type depends on the brush
	// kind; passing the wrong one reads past the object. This builds the one the
	// editor's own draw paths use, for brush_apply and brush_preview alike.
	class BrushDrawParam {
	public:
		BrushDrawParam(Brush &brush, bool alt);

		void* get() noexcept {
			return param;
		}

	private:
		bool flag;
		int variation = 0;
		std::pair<bool, GroundBrush*> ground { true, nullptr };
		void* param = nullptr;
	};

	// Throws McpError for brushes a plain tool call cannot apply: ones that need
	// context (spawn size, a selected house) or that assert in the editor. Erasing
	// needs no context, so only waypoint and house exit are refused there.
	void requireApplicableBrush(Brush &brush, const std::string &name, bool erase);

} // namespace mcp

#endif // RME_MCP_WRITE_H
