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

#ifndef RME_NPC_H_
#define RME_NPC_H_

#include "npcs.h"
#include "enums.h"

class Npc {
public:
	Npc(NpcType* type);
	Npc(const std::string &type_name);

	Npc* deepCopy() const;

	const Outfit &getLookType() const;

	bool isSaved() const noexcept {
		return saved;
	}
	void save() noexcept {
		saved = true;
	}
	void reset() noexcept {
		saved = false;
	}

	bool isSelected() const noexcept {
		return selected;
	}
	void deselect() noexcept {
		selected = false;
	}
	void select() noexcept {
		selected = true;
	}

	bool isNpc() const;

	std::string getName() const;
	NpcBrush* getBrush() const;

	int getSpawnNpcTime() const noexcept {
		return spawnNpcTime;
	}
	void setSpawnNpcTime(int time) noexcept {
		spawnNpcTime = time;
	}

	Direction getDirection() const noexcept {
		return direction;
	}
	void setDirection(Direction _direction) noexcept {
		direction = _direction;
	}

	// Static conversions
	static std::string DirID2Name(uint16_t id);
	static uint16_t DirName2ID(std::string id);

protected:
	std::string type_name;
	Direction direction;
	int spawnNpcTime;
	bool saved;
	bool selected;
};

typedef std::vector<Npc*> NpcVector;
typedef std::list<Npc*> NpcList;

#endif
