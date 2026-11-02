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

#ifndef RME_COLLAB_JOURNAL_H
#define RME_COLLAB_JOURNAL_H

#include "../position.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct sqlite3;

namespace collab {

	constexpr int kRestorePoint = 100; // JournalEntry::actionType of a restore point

	enum class EntryState : int {
		Applied = 0,
		Reverted = 1,
		PartiallyReverted = 2,
		Info = 3, // informational only (houses, towns...), cannot be reverted
	};

	struct JournalEntry {
		int64_t id = 0;
		std::string user;
		uint32_t color = 0;
		int actionType = 0;
		std::string label;
		int64_t created = 0; // unix seconds
		int64_t updated = 0; // unix milliseconds
		uint32_t tileCount = 0;
		Position center;
		int state = 0;
	};

	struct JournalTile {
		Position pos;
		std::string before; // encodeTile() bytes
		std::string after;
	};

	// The host's per-user history of tile edits, in a SQLite file next to the map. GUI thread only.
	class Journal {
	public:
		Journal() = default;
		~Journal();
		Journal(const Journal &) = delete;
		Journal &operator=(const Journal &) = delete;

		bool open(const std::string &path, std::string &error);
		void close();
		bool isOpen() const noexcept {
			return db != nullptr;
		}

		// Records one batch of tile changes. A brush stroke is many batches: batches of the same
		// user and action type less than 1.5 s apart share one entry (the first "before" is kept,
		// the last "after" wins). Tiles whose bytes did not change are dropped. Returns the entry
		// the batch ended up in, id 0 if nothing was recorded.
		JournalEntry append(const std::string &user, uint32_t color, int actionType, const std::string &label, const std::vector<JournalTile> &tiles);
		// actionType kRestorePoint marks a named restore point.
		JournalEntry appendInfo(const std::string &user, uint32_t color, const std::string &label, int actionType = 0);
		// Entries of a user (any user when empty) made since a time, newest first, that can still be reverted.
		std::vector<JournalEntry> listSince(const std::string &user, int64_t sinceSeconds);
		// Revertable entries after the given one, oldest first.
		std::vector<int64_t> idsAfter(int64_t id);

		std::vector<JournalEntry> list(int64_t beforeId, int limit, const std::string &userFilter);
		bool get(int64_t id, JournalEntry &out);
		std::vector<JournalTile> tiles(int64_t id);
		// Every user name in the history, for the filter.
		std::vector<std::string> users();
		void setState(int64_t id, EntryState state);
		void addChat(const std::string &user, const std::string &text);

	private:
		bool exec(const char* sql);

		sqlite3* db = nullptr;
		struct OpenEntry {
			int64_t id = 0;
			int actionType = 0;
			int64_t updatedMs = 0;
		};
		std::map<std::string, OpenEntry> openEntries; // per user
	};

} // namespace collab

#endif
