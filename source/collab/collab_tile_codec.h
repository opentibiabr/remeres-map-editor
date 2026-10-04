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

#ifndef RME_COLLAB_TILE_CODEC_H
#define RME_COLLAB_TILE_CODEC_H

#include "../position.h"

#include <string>

class IOMap;
class Map;
class Tile;

namespace collab {

	// Deterministic bytes for everything a tile persists: items, house id, map flags, zones,
	// monster/npc spawns and the creatures on it. Two tiles with the same content always give
	// the same bytes, so the result can be compared (conflict detection) as well as sent.
	// The empty string means "blank tile", which is also what a missing tile encodes to.
	std::string encodeTile(const Tile* tile, const IOMap &io);

	// True when both tiles have the same spawns, monsters and npc (either may be null).
	bool sameCreatures(const Tile* a, const Tile* b);

	// Builds a new tile owned by the caller (normally handed to a Change). An empty string gives
	// a blank tile. Throws ProtocolError on malformed or hostile input and never leaks.
	Tile* decodeTile(Map &map, const Position &pos, const std::string &bytes, const IOMap &io);

} // namespace collab

#endif
