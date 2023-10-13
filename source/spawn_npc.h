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

#ifndef RME_SPAWN_NPC_H_
#define RME_SPAWN_NPC_H_

class Tile;

class SpawnNpc {
public:
	SpawnNpc(int size = 3);

	SpawnNpc* deepCopy() const;

	bool isSelected() const noexcept {
		return selected;
	}
	void select() noexcept {
		selected = true;
	}
	void deselect() noexcept {
		selected = false;
	}

	int getSize() const noexcept {
		return size;
	}

	// Does not compare selection!
	bool operator==(const SpawnNpc &other) {
		return size == other.size;
	}
	bool operator!=(const SpawnNpc &other) {
		return size != other.size;
	}

	void setSize(int newsize) {
		ASSERT(size < 100);
		size = newsize;
	}

protected:
	int size;
	bool selected;
};

typedef std::set<Position> SpawnNpcPositionList;
typedef std::list<SpawnNpc*> SpawnNpcList;

class SpawnsNpc {
public:
	void addSpawnNpc(Tile* tile);
	void removeSpawnNpc(Tile* tile);

	SpawnNpcPositionList::iterator begin() noexcept {
		return spawnsNpc.begin();
	}
	SpawnNpcPositionList::const_iterator begin() const noexcept {
		return spawnsNpc.begin();
	}
	SpawnNpcPositionList::iterator end() noexcept {
		return spawnsNpc.end();
	}
	SpawnNpcPositionList::const_iterator end() const noexcept {
		return spawnsNpc.end();
	}
	void erase(SpawnNpcPositionList::iterator iter) noexcept {
		spawnsNpc.erase(iter);
	}
	SpawnNpcPositionList::iterator find(Position &pos) {
		return spawnsNpc.find(pos);
	}

private:
	SpawnNpcPositionList spawnsNpc;
};

#endif
