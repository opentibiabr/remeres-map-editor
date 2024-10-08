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

#ifndef RME_SELECTION_H
#define RME_SELECTION_H

#include "position.h"
#include "npc.h"
#include "action.h"

class Action;
class Editor;
class BatchAction;

class SelectionThread;

class Selection {
public:
	Selection(Editor &editor);
	~Selection();

	// Selects the items on the tile/tiles
	// Won't work outside a selection session
	void add(const Tile* tile, Item* item);
	void add(const Tile* tile, SpawnMonster* spawnMonster);
	void add(const Tile* tile, SpawnNpc* spawnNpc);
	void add(const Tile* tile, const std::vector<Monster*> &monsters);
	void add(const Tile* tile, Monster* monster);
	void add(const Tile* tile, Npc* npc);
	void add(const Tile* tile);
	void remove(Tile* tile, Item* item);
	void remove(Tile* tile, SpawnMonster* spawnMonster);
	void remove(Tile* tile, SpawnNpc* spawnNpc);
	void remove(Tile* tile, const std::vector<Monster*> &monsters);
	void remove(Tile* tile, Monster* monster);
	void remove(Tile* tile, Npc* npc);
	void remove(Tile* tile);

	// The tile will be added to the list of selected tiles, however, the items on the tile won't be selected
	void addInternal(Tile* tile);
	void removeInternal(Tile* tile);

	// Clears the selection completely
	void clear();

	// Returns true when inside a session
	bool isBusy() const noexcept {
		return busy;
	}

	//
	Position minPosition() const;
	Position maxPosition() const;

	// This manages a "selection session"
	// Internal session doesn't store the result (eg. no undo)
	// Subthread means the session doesn't create a complete
	// action, just part of one to be merged with the main thread
	// later.
	enum SessionFlags {
		NONE,
		INTERNAL = 1,
		SUBTHREAD = 2,
	};

	void start(SessionFlags flags = NONE, ActionIdentifier identifier = ACTION_SELECT);
	void commit();
	void finish(SessionFlags flags = NONE);

	// Joins the selection instance in this thread with this instance
	// This deletes the thread
	void join(SelectionThread* thread);

	size_t size() const noexcept {
		return tiles.size();
	}
	bool empty() const noexcept {
		return tiles.empty();
	}
	void updateSelectionCount();
	TileSet::iterator begin() noexcept {
		return tiles.begin();
	}
	TileSet::iterator end() noexcept {
		return tiles.end();
	}
	const TileSet &getTiles() const noexcept {
		return tiles;
	}
	Tile* getSelectedTile() {
		ASSERT(size() == 1);
		return *tiles.begin();
	}

private:
	Editor &editor;
	BatchAction* session;
	Action* subsession;
	TileSet tiles;
	bool busy;

	friend class SelectionThread;
};

class SelectionThread : public wxThread {
public:
	SelectionThread(Editor &editor, Position start, Position end);

	void Execute(); // Calls "Create" and then "Run"

protected:
	virtual ExitCode Entry();
	Editor &editor;
	Position start, end;
	Selection selection;
	Action* result;

	friend class Selection;
};

#endif
