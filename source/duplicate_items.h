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

#ifndef RME_DUPLICATE_ITEMS_H_
#define RME_DUPLICATE_ITEMS_H_

#include "duplicate_items_policy.h"

class Item;
class Tile;

// The rules of duplicate_items_policy.h applied to the items of a loaded map.
namespace DuplicateItems {

	ItemFacts factsOf(Item* item);

	// Adds the tile to the report.
	void inspectTile(const Tile &tile, Report &report);

	// How many redundant copies the tile stacks.
	size_t countRedundantCopies(const Tile &tile);

	// Takes the redundant copies off the tile and deletes them. Returns how many.
	size_t removeRedundantCopies(Tile &tile);

	// Whether the mapper switched the save warning off for this map file. Maps are told apart by their file path, so the
	// choice holds for that map alone and is kept between sessions.
	bool isWarningMuted(const std::string &mapFile);

	// Switches the save warning off or on for this map file and writes the setting. Returns false, leaving the setting
	// as it was, when the file cannot be remembered (no path, or a path that holds the list separator).
	bool setWarningMuted(const std::string &mapFile, bool muted);

}

#endif
