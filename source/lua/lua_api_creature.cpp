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
#include "lua_api_creature.h"
#include "lua_api.h"
#include "../monster.h"
#include "../monsters.h"
#include "../spawn_monster.h"
#include "../tile.h"
#include "../map.h"
#include "../editor.h"
#include "../gui.h"

namespace LuaAPI {

	void registerCreature(sol::state &lua) {
		// Register Direction enum
		lua.new_enum("Direction", "NORTH", NORTH, "EAST", EAST, "SOUTH", SOUTH, "WEST", WEST);

		// Register Monster usertype
		lua.new_usertype<Monster>(
			"Creature",
			sol::no_constructor,

			// Properties (read-only)
			"name", sol::property([](Monster* m) -> std::string {
				return m ? m->getName() : "";
			}),
			"isNpc", sol::property([](Monster*) -> bool {
				return false;
			}),

			// Properties (read/write)
			"spawnTime", sol::property([](Monster* m) -> int { return m ? m->getSpawnMonsterTime() : 0; }, [](Monster* m, int time) { if (m) { m->setSpawnMonsterTime(time); } }),
			"direction", sol::property([](Monster* m) -> int { return m ? static_cast<int>(m->getDirection()) : 0; }, [](Monster* m, int dir) {  
					if (m && dir >= DIRECTION_FIRST && dir <= DIRECTION_LAST) {  
						m->setDirection(static_cast<Direction>(dir));  
					} }),

			// Selection
			"isSelected", sol::property([](Monster* m) { return m && m->isSelected(); }),
			"select", [](Monster* m) { if (m) { m->select(); } },
			"deselect", [](Monster* m) { if (m) { m->deselect(); } },

			// String representation
			sol::meta_function::to_string, [](Monster* m) -> std::string {  
				if (!m) { return "Creature(invalid)"; }  
				std::string dir;  
				switch (m->getDirection()) {  
					case NORTH: dir = "N"; break;  
					case EAST: dir = "E"; break;  
					case SOUTH: dir = "S"; break;  
					case WEST: dir = "W"; break;  
					default: dir = "?"; break;  
				}  
				return "Creature(\"" + m->getName() + "\", dir=" + dir +  
					   ", spawn=" + std::to_string(m->getSpawnMonsterTime()) + "s)"; }
		);

		// Register SpawnMonster usertype
		lua.new_usertype<SpawnMonster>(
			"Spawn",
			sol::no_constructor,

			// Properties (read/write)
			"size", sol::property([](SpawnMonster* s) -> int { return s ? s->getSize() : 0; }, [](SpawnMonster* s, int size) { if (s && size > 0 && size < 100) { s->setSize(size); } }),
			"radius", sol::property([](SpawnMonster* s) -> int { return s ? s->getSize() : 0; }, [](SpawnMonster* s, int size) { if (s && size > 0 && size < 100) { s->setSize(size); } }),

			// Selection
			"isSelected", sol::property([](SpawnMonster* s) { return s && s->isSelected(); }),
			"select", [](SpawnMonster* s) { if (s) { s->select(); } },
			"deselect", [](SpawnMonster* s) { if (s) { s->deselect(); } },

			// String representation
			sol::meta_function::to_string, [](SpawnMonster* s) -> std::string {  
				if (!s) { return "Spawn(invalid)"; }  
				return "Spawn(radius=" + std::to_string(s->getSize()) + ")"; }
		);

		// Helper function to check if a monster type exists
		lua["creatureExists"] = [](const std::string &name) {
			return g_monsters[name] != nullptr;
		};

		// Helper function (NPCs always false in monster context)
		lua["isNpcType"] = [](const std::string &) {
			return false;
		};
	}

} // namespace LuaAPI
