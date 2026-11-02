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
	uint32_t parent = 0; // id of the comment this one replies to, 0 for a thread's first comment
	std::string assignee; // who should deal with it, empty for nobody
	uint8_t kind = 0; // see MapComments::kindName
};

// Free-form notes pinned to map positions. Stored in a "<map name>-comments.xml" sidecar file.
class MapComments {
public:
	// Name of the local user, from Config::COLLAB_USER_NAME (falls back to the OS user name).
	static std::string localAuthor();
	// Stable per-name color, 0xRRGGBB.
	static uint32_t colorForAuthor(const std::string &author);
	// Fixed palette shared with the collaboration user colors.
	static constexpr size_t kPaletteSize = 32;
	static uint32_t paletteColor(size_t index);

	// In a collaboration session comments are written under the session name (the host
	// checks it); empty means the settings name again.
	static void setAuthorOverride(const std::string &name);
	static constexpr uint8_t kKindCount = 4;
	static const char* kindName(uint8_t kind);
	// Marker color of a kind, 0 for plain notes (they use their author's color).
	static uint32_t kindColor(uint8_t kind);

	// Ids are (prefix << 20 | counter) so that two users adding comments at the same time
	// never pick the same id; the session sets the prefix to the user id.
	void setIdPrefix(uint32_t prefix) noexcept {
		idPrefix = prefix & 0xFFF;
	}

	// Adds a comment authored by the local user and returns it.
	const MapComment &add(const Position &pos, const std::string &text, uint32_t parent = 0, const std::string &assignee = std::string(), uint8_t kind = 0);
	bool edit(uint32_t id, const std::string &text);
	bool update(uint32_t id, const std::string &text, const std::string &assignee, uint8_t kind);
	bool setResolved(uint32_t id, bool resolved);
	// Removes the comment and the replies to it.
	bool remove(uint32_t id);
	// Insert or replace a comment as it came from somebody else.
	void upsert(const MapComment &comment);
	void clear();

	const MapComment* get(uint32_t id) const;
	// The first comment of a thread on the tile (replies are not returned), or nullptr.
	const MapComment* at(const Position &pos) const;
	// The thread's first comment for any comment of it.
	const MapComment* rootOf(const MapComment &comment) const;
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
	uint32_t nextId = 1; // counter part of the next id
	uint32_t idPrefix = 0;
};

#endif
