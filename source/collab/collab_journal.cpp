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

#include "collab_journal.h"

#include <chrono>
#include <ctime>

#include <sqlite3.h>

namespace collab {

	namespace {
		constexpr int64_t kCoalesceMs = 1500;

		int64_t nowMs() {
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		}

		// A prepared statement that finalizes itself.
		class Stmt {
		public:
			Stmt(sqlite3* db, const char* sql) {
				if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
					stmt = nullptr;
				}
			}
			~Stmt() {
				sqlite3_finalize(stmt);
			}
			Stmt(const Stmt &) = delete;
			Stmt &operator=(const Stmt &) = delete;

			bool ok() const noexcept {
				return stmt != nullptr;
			}
			void bind(int index, int64_t value) {
				sqlite3_bind_int64(stmt, index, value);
			}
			void bind(int index, const std::string &value) {
				sqlite3_bind_text(stmt, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
			}
			void bindBlob(int index, const std::string &value) {
				sqlite3_bind_blob(stmt, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
			}
			bool step() {
				return stmt && sqlite3_step(stmt) == SQLITE_ROW;
			}
			void run() {
				if (stmt) {
					sqlite3_step(stmt);
				}
			}
			// Makes the statement reusable with new bindings.
			void reset() {
				if (stmt) {
					sqlite3_reset(stmt);
					sqlite3_clear_bindings(stmt);
				}
			}
			int64_t integer(int column) const {
				return sqlite3_column_int64(stmt, column);
			}
			std::string text(int column) const {
				const auto* chars = sqlite3_column_text(stmt, column);
				return chars ? std::string(reinterpret_cast<const char*>(chars), static_cast<size_t>(sqlite3_column_bytes(stmt, column))) : std::string();
			}
			std::string blob(int column) const {
				const void* data = sqlite3_column_blob(stmt, column);
				return data ? std::string(static_cast<const char*>(data), static_cast<size_t>(sqlite3_column_bytes(stmt, column))) : std::string();
			}

		private:
			sqlite3_stmt* stmt = nullptr;
		};

		constexpr const char* kEntryColumns = "id, user_name, user_color, action_type, label, created, updated, tile_count, min_x, min_y, max_x, max_y, z_mask, state";

		JournalEntry readEntry(const Stmt &s) {
			JournalEntry e;
			e.id = s.integer(0);
			e.user = s.text(1);
			e.color = static_cast<uint32_t>(s.integer(2));
			e.actionType = static_cast<int>(s.integer(3));
			e.label = s.text(4);
			e.created = s.integer(5);
			e.updated = s.integer(6);
			e.tileCount = static_cast<uint32_t>(s.integer(7));
			const int64_t zMask = s.integer(12);
			int z = 7;
			for (int bit = 0; bit < 16; ++bit) {
				if (zMask & (int64_t(1) << bit)) {
					z = bit;
					break;
				}
			}
			e.center = Position(static_cast<int>((s.integer(8) + s.integer(10)) / 2), static_cast<int>((s.integer(9) + s.integer(11)) / 2), z);
			e.state = static_cast<int>(s.integer(13));
			return e;
		}
	}

	Journal::~Journal() {
		close();
	}

	bool Journal::exec(const char* sql) {
		return db && sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
	}

	bool Journal::open(const std::string &path, std::string &error) {
		close();
		if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
			error = db ? sqlite3_errmsg(db) : "out of memory";
			close();
			return false;
		}
		const bool ok = exec("CREATE TABLE IF NOT EXISTS entries("
							 "id INTEGER PRIMARY KEY, user_name TEXT, user_color INTEGER, action_type INTEGER, label TEXT,"
							 "created INTEGER, updated INTEGER, tile_count INTEGER,"
							 "min_x INTEGER, min_y INTEGER, max_x INTEGER, max_y INTEGER, z_mask INTEGER, state INTEGER)")
			&& exec("CREATE TABLE IF NOT EXISTS entry_tiles("
					"entry_id INTEGER, x INTEGER, y INTEGER, z INTEGER, before_blob BLOB, after_blob BLOB,"
					"PRIMARY KEY(entry_id, x, y, z))")
			&& exec("CREATE TABLE IF NOT EXISTS chat(id INTEGER PRIMARY KEY, user_name TEXT, text TEXT, created INTEGER)");
		if (!ok) {
			error = sqlite3_errmsg(db);
			close();
			return false;
		}
		return true;
	}

	void Journal::close() {
		if (db) {
			sqlite3_close(db);
			db = nullptr;
		}
		openEntries.clear();
	}

	bool Journal::get(int64_t id, JournalEntry &out) {
		Stmt s(db, (std::string("SELECT ") + kEntryColumns + " FROM entries WHERE id = ?1").c_str());
		if (!s.ok()) {
			return false;
		}
		s.bind(1, id);
		if (!s.step()) {
			return false;
		}
		out = readEntry(s);
		return true;
	}

	JournalEntry Journal::append(const std::string &user, uint32_t color, int actionType, const std::string &label, const std::vector<JournalTile> &tiles) {
		JournalEntry result;
		if (!db) {
			return result;
		}

		std::vector<const JournalTile*> changed;
		for (const JournalTile &tile : tiles) {
			if (tile.before != tile.after) {
				changed.push_back(&tile);
			}
		}
		if (changed.empty()) {
			return result;
		}

		const int64_t now = nowMs();
		exec("BEGIN");

		int64_t id = 0;
		auto open = openEntries.find(user);
		if (open != openEntries.end() && open->second.actionType == actionType && now - open->second.updatedMs < kCoalesceMs) {
			JournalEntry existing;
			// An entry that was reverted in the meantime must not grow.
			if (get(open->second.id, existing) && existing.state == static_cast<int>(EntryState::Applied)) {
				id = open->second.id;
			}
		}
		if (id == 0) {
			Stmt insert(db, "INSERT INTO entries(user_name, user_color, action_type, label, created, updated, tile_count, min_x, min_y, max_x, max_y, z_mask, state) VALUES(?1, ?2, ?3, ?4, ?5, ?6, 0, 0, 0, 0, 0, 0, 0)");
			insert.bind(1, user);
			insert.bind(2, static_cast<int64_t>(color));
			insert.bind(3, static_cast<int64_t>(actionType));
			insert.bind(4, label);
			insert.bind(5, static_cast<int64_t>(std::time(nullptr)));
			insert.bind(6, now);
			insert.run();
			id = sqlite3_last_insert_rowid(db);
		}
		openEntries[user] = { id, actionType, now };

		{
			// Keep the first "before" of a tile, the newest "after".
			Stmt put(db, "INSERT INTO entry_tiles(entry_id, x, y, z, before_blob, after_blob) VALUES(?1, ?2, ?3, ?4, ?5, ?6) "
						 "ON CONFLICT(entry_id, x, y, z) DO UPDATE SET after_blob = excluded.after_blob");
			for (const JournalTile* tile : changed) {
				put.bind(1, id);
				put.bind(2, static_cast<int64_t>(tile->pos.x));
				put.bind(3, static_cast<int64_t>(tile->pos.y));
				put.bind(4, static_cast<int64_t>(tile->pos.z));
				put.bindBlob(5, tile->before);
				put.bindBlob(6, tile->after);
				put.run();
				put.reset();
			}
		}

		{
			Stmt update(db, "UPDATE entries SET updated = ?2,"
							" tile_count = (SELECT COUNT(*) FROM entry_tiles WHERE entry_id = ?1),"
							" min_x = (SELECT MIN(x) FROM entry_tiles WHERE entry_id = ?1),"
							" min_y = (SELECT MIN(y) FROM entry_tiles WHERE entry_id = ?1),"
							" max_x = (SELECT MAX(x) FROM entry_tiles WHERE entry_id = ?1),"
							" max_y = (SELECT MAX(y) FROM entry_tiles WHERE entry_id = ?1),"
							" z_mask = (SELECT COALESCE(SUM(1 << z), 0) FROM (SELECT DISTINCT z FROM entry_tiles WHERE entry_id = ?1))"
							" WHERE id = ?1");
			update.bind(1, id);
			update.bind(2, now);
			update.run();
		}
		exec("COMMIT");

		get(id, result);
		return result;
	}

	JournalEntry Journal::appendInfo(const std::string &user, uint32_t color, const std::string &label) {
		JournalEntry result;
		if (!db) {
			return result;
		}
		Stmt insert(db, "INSERT INTO entries(user_name, user_color, action_type, label, created, updated, tile_count, min_x, min_y, max_x, max_y, z_mask, state) VALUES(?1, ?2, 0, ?3, ?4, ?5, 0, 0, 0, 0, 0, 0, 3)");
		insert.bind(1, user);
		insert.bind(2, static_cast<int64_t>(color));
		insert.bind(3, label);
		insert.bind(4, static_cast<int64_t>(std::time(nullptr)));
		insert.bind(5, nowMs());
		insert.run();
		get(sqlite3_last_insert_rowid(db), result);
		return result;
	}

	std::vector<JournalEntry> Journal::list(int64_t beforeId, int limit, const std::string &userFilter) {
		std::vector<JournalEntry> entries;
		Stmt s(db, (std::string("SELECT ") + kEntryColumns + " FROM entries WHERE (?1 = 0 OR id < ?1) AND (?2 = '' OR user_name = ?2) ORDER BY id DESC LIMIT ?3").c_str());
		if (!s.ok()) {
			return entries;
		}
		s.bind(1, beforeId);
		s.bind(2, userFilter);
		s.bind(3, static_cast<int64_t>(limit));
		while (s.step()) {
			entries.push_back(readEntry(s));
		}
		return entries;
	}

	std::vector<JournalTile> Journal::tiles(int64_t id) {
		std::vector<JournalTile> rows;
		Stmt s(db, "SELECT x, y, z, before_blob, after_blob FROM entry_tiles WHERE entry_id = ?1");
		if (!s.ok()) {
			return rows;
		}
		s.bind(1, id);
		while (s.step()) {
			JournalTile row;
			row.pos = Position(static_cast<int>(s.integer(0)), static_cast<int>(s.integer(1)), static_cast<int>(s.integer(2)));
			row.before = s.blob(3);
			row.after = s.blob(4);
			rows.push_back(std::move(row));
		}
		return rows;
	}

	void Journal::setState(int64_t id, EntryState state) {
		Stmt s(db, "UPDATE entries SET state = ?2 WHERE id = ?1");
		s.bind(1, id);
		s.bind(2, static_cast<int64_t>(state));
		s.run();
	}

	void Journal::addChat(const std::string &user, const std::string &text) {
		Stmt s(db, "INSERT INTO chat(user_name, text, created) VALUES(?1, ?2, ?3)");
		s.bind(1, user);
		s.bind(2, text);
		s.bind(3, static_cast<int64_t>(std::time(nullptr)));
		s.run();
	}

} // namespace collab
