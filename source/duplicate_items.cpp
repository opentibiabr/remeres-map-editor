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

#include "duplicate_items.h"

#include "item.h"
#include "tile.h"

namespace DuplicateItems {

	ItemFacts factsOf(Item* item) {
		ItemFacts facts;
		if (!item) {
			return facts;
		}

		const ItemType &type = item->getItemType();
		facts.id = item->getID();
		// The editor calls the items that sit right above the ground "always on bottom" (items.cpp, ground_brush.cpp).
		facts.alwaysOnTop = type.alwaysOnBottom;
		facts.elevated = type.hasElevation;
		facts.plain = !item->isComplex() && !item->hasSubtype(type) && !item->getContainer() && !item->getDepot() && !item->getTeleport() && !item->getDoor();
		return facts;
	}

	void inspectTile(const Tile &tile, Report &report) {
		const Position &position = tile.getPosition();
		report.addTile({ position.x, position.y, position.z }, tile.items, factsOf);
	}

	size_t countRedundantCopies(const Tile &tile) {
		return visitRedundantCopies(tile.items, factsOf, [](size_t) {});
	}

	size_t removeRedundantCopies(Tile &tile) {
		return eraseRedundantCopies(tile.items, factsOf, [](Item* item) { delete item; });
	}

}
