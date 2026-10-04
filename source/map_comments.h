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

#ifndef RME_MAP_COMMENTS_H_
#define RME_MAP_COMMENTS_H_

#include "position.h"

struct MapComment {
	uint32_t id = 0;
	Position pos;
	std::string author;
	uint32_t authorColor = 0; // 0xRRGGBB
	std::string text;
	int64_t created = 0;
	int64_t edited = 0;
	bool resolved = false;
};

// Free-form notes pinned to map positions. Stored in a "<map name>-comments.xml" sidecar file.
class MapComments {
public:
	// Name of the local user, from Config::COLLAB_USER_NAME (falls back to the OS user name).
	static std::string localAuthor();
	// Stable per-name color, 0xRRGGBB.
	static uint32_t colorForAuthor(const std::string &author);
	// Fixed palette shared with the collaboration user colors.
	static constexpr size_t kPaletteSize = 12;
	static uint32_t paletteColor(size_t index);

	// Adds a comment authored by the local user and returns it.
	const MapComment &add(const Position &pos, const std::string &text);
	bool edit(uint32_t id, const std::string &text);
	bool setResolved(uint32_t id, bool resolved);
	bool remove(uint32_t id);
	void clear();

	const MapComment* get(uint32_t id) const;
	// First comment on the tile, or nullptr.
	const MapComment* at(const Position &pos) const;
	const std::vector<MapComment> &all() const noexcept {
		return comments;
	}
	bool empty() const noexcept {
		return comments.empty();
	}

	// Sidecar file. save() removes the file when there is nothing to store.
	bool load(const std::string &path);
	bool save(const std::string &path) const;
	// Same format, in memory (collaboration snapshots).
	bool loadXml(const std::string &xml);
	std::string toXml() const;

private:
	MapComment* find(uint32_t id);
	bool loadDocument(const pugi::xml_document &doc);
	void fillDocument(pugi::xml_document &doc) const;

	// ponytail: linear scans, fine for hundreds of comments; index by position if it grows.
	std::vector<MapComment> comments;
	uint32_t nextId = 1;
};

#endif
