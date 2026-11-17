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

#ifndef RME_COLLAB_META_H
#define RME_COLLAB_META_H

#include "collab_protocol.h"

#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../map_comments.h"
#include "../position.h"

class Map;

// Houses, towns, waypoints, zones and the map properties are edited directly from many
// dialogs, brushes and scripts instead of going through the undo queue. Rather than hooking
// every call site, the session periodically captures this state and sends the difference.
namespace collab {

	enum class MetaKind : uint8_t {
		House,
		Town,
		Waypoint,
		Zone,
		MapProps,
		Comment,
		Count,
	};

	struct MetaOp {
		MetaKind kind = MetaKind::House;
		bool remove = false;
		std::string key;
		std::string data; // empty for removals
	};

	// (kind, key) -> canonical encoding of that entry.
	using MetaState = std::map<std::pair<uint8_t, std::string>, std::string>;

	constexpr size_t kMaxMetaOps = 4000;

	MetaState captureMeta(Map &map);
	// The current encoding of one entry, nullopt if it does not exist.
	std::optional<std::string> captureMetaEntry(Map &map, MetaKind kind, const std::string &key);
	std::vector<MetaOp> diffMeta(const MetaState &before, const MetaState &after);

	// A comment as carried by a Comment entry. Throws ProtocolError when malformed.
	MapComment decodeComment(uint32_t id, const std::string &data);

	// Where tiles of houses that did not exist yet wait for their house.
	using PendingHouseTiles = std::unordered_map<uint32_t, std::vector<Position>>;

	// Mirrors what the GUI does for the same edit. Throws ProtocolError on malformed input.
	void applyMetaOp(Map &map, const MetaOp &op);
	// Gives tiles that arrived before their house to the house that now exists.
	void attachPendingHouseTiles(Map &map, PendingHouseTiles &pending);

	void writeMetaOps(ByteWriter &w, const std::vector<MetaOp> &ops);
	std::vector<MetaOp> readMetaOps(ByteReader &r);

} // namespace collab

#endif
