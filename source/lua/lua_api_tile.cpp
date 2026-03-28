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
#include "lua_api_tile.h"
#include "lua_api.h"
#include <algorithm>
#include <iterator>
#include "../tile.h"
#include "../item.h"
#include "../monster.h"
#include "../monsters.h"
#include "../settings.h"
#include "../npc.h"
#include "../spawn_monster.h"
#include "../spawn_npc.h"
#include "../brush.h"
#include "../ground_brush.h"
#include "../doodad_brush.h"
#include "../wall_brush.h"
#include "../gui.h"
#include "../editor.h"
#include "../map.h"

namespace LuaAPI {

	// Helper to get items as a Lua table
	static sol::table getTileItems(Tile* tile, sol::this_state ts) {
		sol::state_view lua(ts);
		sol::table items = lua.create_table();

		if (!tile) {
			return items;
		}

		int idx = 1;
		for (Item* item : tile->items) {
			if (item) {
				items[idx++] = item;
			}
		}
		return items;
	}

	// Add item to tile
	static Item* addItemToTile(Tile* tile, int itemId, sol::optional<int> countOpt) {
		if (!tile) {
			throw sol::error("Invalid tile");
		}
		if (itemId <= 0 || itemId > 0xFFFF) {
			throw sol::error("Invalid item id: " + std::to_string(itemId));
		}
		if (countOpt && (*countOpt < 0 || *countOpt > 0xFFFF)) {
			throw sol::error("Invalid item count: " + std::to_string(*countOpt));
		}

		// Mark tile for undo before modification
		markTileForUndo(tile);

		Item* item = Item::Create(static_cast<uint16_t>(itemId));
		if (!item) {
			throw sol::error("Failed to create item with id " + std::to_string(itemId));
		}

		if (countOpt) {
			item->setSubtype(static_cast<uint16_t>(*countOpt));
		}

		tile->addItem(item);
		tile->modify();

		return item;
	}

	// Remove item from tile
	static bool removeItemFromTile(Tile* tile, const Item* itemToRemove) {
		if (!tile || !itemToRemove) {
			return false;
		}

		// Mark tile for undo before modification
		markTileForUndo(tile);

		// Find and remove the item
		if (tile->removeItem(itemToRemove)) {
			tile->modify();
			return true;
		}

		// Check if it's the ground
		if (tile->ground == itemToRemove) {
			tile->clearGround();
			tile->modify();
			return true;
		}

		return false;
	}

	// Set creature on tile
	static Monster* setTileCreature(Tile* tile, const std::string &creatureName, sol::optional<int> spawnTimeOpt, sol::optional<int> directionOpt) {
		if (!tile) {
			throw sol::error("Invalid tile");
		}

		// Check if monster type exists
		MonsterType* type = g_monsters[creatureName];
		if (!type) {
			type = g_monsters.addMissingMonsterType(creatureName);
		}

		// Mark tile for undo before modification
		markTileForUndo(tile);

		// Remove existing monsters
		tile->clearMonsters();

		// Create new monster
		auto monster = newd Monster(type);

		// Set spawn time
		int spawnTime = spawnTimeOpt.value_or(g_settings.getInteger(Config::DEFAULT_SPAWN_MONSTER_TIME));
		monster->setSpawnMonsterTime(spawnTime);

		// Set direction (default NORTH)
		auto dir = static_cast<Direction>(directionOpt.value_or(NORTH));
		if (dir >= DIRECTION_FIRST && dir <= DIRECTION_LAST) {
			monster->setDirection(dir);
		}

		tile->monsters.emplace_back(monster);
		tile->modify();
		return monster;
	}

	// Remove creature from tile
	static bool removeTileCreature(Tile* tile) {
		if (!tile || tile->monsters.empty()) {
			return false;
		}
		markTileForUndo(tile);
		tile->clearMonsters();
		tile->modify();
		return true;
	}

	// Set spawn on tile
	static SpawnMonster* setTileSpawn(Tile* tile, sol::optional<int> sizeOpt) {
		if (!tile) {
			throw sol::error("Invalid tile");
		}

		// Mark tile for undo before modification
		markTileForUndo(tile);

		// Get map instance
		Editor* editor = g_gui.GetCurrentEditor();
		if (!editor) {
			throw sol::error("No active editor");
		}
		Map &map = editor->getMap();

		// Remove existing spawn from map metadata
		if (tile->spawnMonster) {
			map.removeSpawnMonster(tile);
			tile->clearSpawnMonster();
		}

		// Create new spawn with given size (default 3)
		int size = sizeOpt.value_or(3);
		if (size < 1) {
			size = 1;
		}
		if (size > 50) {
			size = 50;
		}

		tile->spawnMonster = newd SpawnMonster(size);

		// Register new spawn with map
		map.addSpawnMonster(tile);

		tile->modify();
		return tile->spawnMonster;
	}

	// Remove spawn from tile
	static bool removeTileSpawn(Tile* tile) {
		if (!tile || !tile->spawnMonster) {
			return false;
		}

		// Mark tile for undo before modification
		markTileForUndo(tile);

		// Get map instance
		Editor* editor = g_gui.GetCurrentEditor();
		if (!editor) {
			throw sol::error("No active editor");
		}
		Map &map = editor->getMap();

		// Remove spawn from map metadata
		map.removeSpawnMonster(tile);

		tile->clearSpawnMonster();
		tile->modify();
		return true;
	}

	// Set ground item
	static void setTileGround(Tile* tile, sol::object groundObj) {
		if (!tile) {
			throw sol::error("Invalid tile");
		}

		Item* newGround = nullptr;

		if (groundObj.is<int>()) {
			int groundId = groundObj.as<int>();
			if (groundId > 0) {
				newGround = Item::Create(static_cast<uint16_t>(groundId));
				if (!newGround) {
					throw sol::error("Failed to create ground item with id " + std::to_string(groundId));
				}
			}
			// groundId <= 0 treated as removal (newGround stays nullptr)
		} else if (groundObj.is<Item*>()) {
			Item* item = groundObj.as<Item*>();
			if (item) {
				newGround = item->deepCopy();
			}
			// nullptr Item* treated as removal
		} else if (groundObj.is<sol::nil_t>()) {
			// Explicit nil: remove ground (newGround stays nullptr)
		} else {
			throw sol::error("Invalid argument: expected item id (int), Item*, or nil");
		}

		// Mark tile for undo before modification
		markTileForUndo(tile);

		// Replace ground
		tile->replaceGround(newGround);

		tile->modify();
	}

	// Set house ID
	static void setTileHouseId(Tile* tile, uint32_t houseId) {
		if (!tile) {
			return;
		}

		// Mark tile for undo before modification
		markTileForUndo(tile);

		tile->house_id = houseId;
		tile->modify();
	}

	// Apply a brush to a tile (with optional auto-bordering)
	static bool applyBrushToTile(Tile* tile, const std::string &brushName, sol::optional<bool> autoBorder) {
		if (!tile) {
			return false;
		}

		Brush* brush = g_brushes.getBrush(brushName);
		if (!brush) {
			return false;
		}

		Editor* editor = g_gui.GetCurrentEditor();
		if (!editor) {
			return false;
		}

		markTileForUndo(tile);

		bool success = false;

		// Apply the brush based on its type
		if (brush->isGround()) {
			GroundBrush* groundBrush = brush->asGround();
			if (groundBrush) {
				groundBrush->draw(&editor->getMap(), tile, nullptr);
				success = true;
			}
		} else if (brush->isDoodad()) {
			DoodadBrush* doodadBrush = brush->asDoodad();
			if (doodadBrush) {
				doodadBrush->draw(&editor->getMap(), tile, nullptr);
				success = true;
			}
		} else if (brush->isWall()) {
			WallBrush* wallBrush = brush->asWall();
			if (wallBrush) {
				wallBrush->draw(&editor->getMap(), tile, nullptr);
				success = true;
			}
		} else if (brush->isDoor()) {
			DoorBrush* doorBrush = brush->asDoor();
			if (doorBrush) {
				doorBrush->draw(&editor->getMap(), tile, nullptr);
				success = true;
			}
		}

		if (success) {
			bool doBorder = autoBorder.value_or(brush->isGround());
			if (doBorder) {
				tile->borderize(&editor->getMap());
			}
			tile->modify();
		}

		return success;
	}

	void registerTile(sol::state &lua) {
		// Register Tile usertype
		lua.new_usertype<Tile>(
			"Tile",
			// No public constructor - tiles are obtained from the map
			sol::no_constructor,

			// Position properties (read-only)
			"position", sol::property([](Tile* tile) { return tile ? tile->getPosition() : Position(); }),
			"x", sol::property([](Tile* tile) { return tile ? tile->getX() : 0; }),
			"y", sol::property([](Tile* tile) { return tile ? tile->getY() : 0; }),
			"z", sol::property([](Tile* tile) { return tile ? tile->getZ() : 0; }),

			// Ground (read/write)
			"ground", sol::property([](Tile* tile) -> Item* { return tile ? tile->ground : nullptr; }, setTileGround),
			"hasGround", sol::property([](Tile* tile) { return tile && tile->hasGround(); }),

			// Items collection (read-only - use addItem/removeItem to modify)
			"items", sol::property(getTileItems),
			"itemCount", sol::property([](Tile* tile) { return tile ? tile->size() : 0; }),

			// House
			"houseId", sol::property([](Tile* tile) -> uint32_t { return tile ? tile->getHouseID() : 0; }, setTileHouseId),
			"isHouseTile", sol::property([](Tile* tile) { return tile && tile->isHouseTile(); }),
			"isHouseExit", sol::property([](Tile* tile) { return tile && tile->isHouseExit(); }),

			// Flags (read-only for now, some could be made writable)
			"isPZ", sol::property([](Tile* tile) { return tile && tile->isPZ(); }),
			"isBlocking", sol::property([](Tile* tile) { return tile && tile->isBlocking(); }),
			"hasBorders", sol::property([](Tile* tile) { return tile && tile->hasBorders(); }),
			"hasWall", sol::property([](Tile* tile) { return tile && tile->hasWall(); }),
			"hasTable", sol::property([](Tile* tile) { return tile && tile->hasTable(); }),
			"hasCarpet", sol::property([](Tile* tile) { return tile && tile->hasCarpet(); }),
			"groundZOrder", sol::property([](Tile* tile) -> int {
				if (!tile) {
					return 0;
				}
				GroundBrush* brush = tile->getGroundBrush();
				return brush ? brush->getZ() : 0;
			}),
			"isNoPvp", sol::property([](Tile* tile) { return tile && (tile->getMapFlags() & TILESTATE_NOPVP); }),
			"isNoLogout", sol::property([](Tile* tile) { return tile && (tile->getMapFlags() & TILESTATE_NOLOGOUT); }),
			"isPvpZone", sol::property([](Tile* tile) { return tile && (tile->getMapFlags() & TILESTATE_PVPZONE); }),

			// Map flags
			"mapFlags", sol::property([](Tile* tile) -> uint16_t { return tile ? tile->getMapFlags() : 0; }, [](Tile* tile, uint16_t flags) {
					if (tile) {
						markTileForUndo(tile);
						tile->setMapFlags(flags);
						tile->modify();
					} }),

			// Selection
			"isSelected", sol::property([](Tile* tile) { return tile && tile->isSelected(); }),
			"select", [](Tile* tile) { if (tile){ tile->select();
} },
			"deselect", [](Tile* tile) { if (tile){ tile->deselect();
} },

			// Creature and Spawn (read-only access, use methods to modify)
			"creature", sol::property([](Tile* tile) -> Monster* { return (tile && !tile->monsters.empty()) ? tile->monsters.back() : nullptr; }),
			"spawn", sol::property([](Tile* tile) -> SpawnMonster* { return tile ? tile->spawnMonster : nullptr; }),
			"hasCreature", sol::property([](Tile* tile) { return tile && !tile->monsters.empty(); }),
			"hasSpawn", sol::property([](Tile* tile) { return tile && tile->spawnMonster != nullptr; }),

			// Creature methods
			"setCreature", sol::overload([](Tile* tile, const std::string &name) -> Monster* { return setTileCreature(tile, name, sol::nullopt, sol::nullopt); }, [](Tile* tile, const std::string &name, int spawnTime) -> Monster* { return setTileCreature(tile, name, spawnTime, sol::nullopt); }, [](Tile* tile, const std::string &name, int spawnTime, int direction) -> Monster* { return setTileCreature(tile, name, spawnTime, direction); }),
			"removeCreature", removeTileCreature,

			// Spawn methods
			"setSpawn", sol::overload([](Tile* tile) -> SpawnMonster* { return setTileSpawn(tile, sol::nullopt); }, [](Tile* tile, int size) -> SpawnMonster* { return setTileSpawn(tile, size); }),
			"removeSpawn", removeTileSpawn,

			// Methods
			"addItem", sol::overload([](Tile* tile, int itemId) -> Item* { return addItemToTile(tile, itemId, sol::nullopt); }, [](Tile* tile, int itemId, int count) -> Item* { return addItemToTile(tile, itemId, count); }),
			"removeItem", removeItemFromTile,
			"applyBrush", applyBrushToTile,
			"borderize", [](Tile* tile) {  
				if (!tile){ return; }  
				Editor* editor = g_gui.GetCurrentEditor();  
				if (!editor){ return; }  
				markTileForUndo(tile);  
				tile->borderize(&editor->getMap());  
				tile->modify(); },
			"wallize", [](Tile* tile) {  
				if (!tile){ return; }  
				Editor* editor = g_gui.GetCurrentEditor();  
				if (!editor){ return; }  
				markTileForUndo(tile);  
				tile->wallize(&editor->getMap());  
				tile->modify(); },
			"moveItem", sol::overload(
							// Index-based move within same tile
							// Semantics: Move item at fromIdx so it ends up at toIdx position
							[](Tile* tile, int fromIdx, int toIdx) {
								if (!tile) {
									return;
								}

								int from = fromIdx - 1;
								int to = toIdx - 1;
								int size = (int)tile->items.size();

								if (from < 0 || from >= size || to < 0 || to >= size || from == to) {
									return;
								}

								markTileForUndo(tile);

								Item* item = tile->items[from];
								tile->items.erase(tile->items.begin() + from);

								// After erasing from position 'from', indices shift:
								// - If to > from: the target position shifted down by 1, so use 'to' directly
								//   (because we want the item to end up at that visual slot)
								// - If to < from: no shift needed, use 'to' directly
								// In both cases, clamp to valid range after erase
								int insertPos = std::clamp(to, 0, (int)tile->items.size());

								tile->items.insert(tile->items.begin() + insertPos, item);
								tile->modify();
							},
							// Move specific item object to new index in same tile
							[](Tile* tile, Item* item, int toIdx) {
								if (!tile || !item) {
									return;
								}
								auto it = std::find(tile->items.begin(), tile->items.end(), item);
								if (it == tile->items.end()) {
									return;
								}

								int from = (int)std::distance(tile->items.begin(), it);
								int to = std::clamp(toIdx - 1, 0, (int)tile->items.size() - 1);
								if (from == to) {
									return;
								}

								markTileForUndo(tile);
								tile->items.erase(it);

								int insertPos = std::clamp(to, 0, (int)tile->items.size());
								tile->items.insert(tile->items.begin() + insertPos, item);
								tile->modify();
							},
							// Move item to DIFFERENT tile
							[](Tile* sourceTile, Item* item, Tile* destTile, sol::optional<int> toIdx) {
								if (!sourceTile || !item || !destTile) {
									return;
								}
								auto it = std::find(sourceTile->items.begin(), sourceTile->items.end(), item);
								if (it == sourceTile->items.end()) {
									return;
								}

								markTileForUndo(sourceTile);
								markTileForUndo(destTile);

								sourceTile->items.erase(it);
								int to = toIdx ? std::clamp(*toIdx - 1, 0, (int)destTile->items.size()) : (int)destTile->items.size();
								destTile->items.insert(destTile->items.begin() + to, item);

								sourceTile->modify();
								destTile->modify();
							}
						),

			"getPosition", [](Tile* tile, sol::this_state ts) {
				sol::state_view lua(ts);
				sol::table t = lua.create_table();
				if (tile) {
					Position p = tile->getPosition();
					t["x"] = p.x;
					t["y"] = p.y;
					t["z"] = p.z;
				}
				return t; },

			"getItemAt", [](Tile* tile, int index) -> Item* {
				if (!tile){ return nullptr;
}
				// Lua uses 1-based indexing
				return tile->getItemAt(index - 1); },

			"getTopItem", [](Tile* tile) -> Item* { return tile ? tile->getTopItem() : nullptr; },

			"getWall", [](Tile* tile) -> Item* { return tile ? tile->getWall() : nullptr; },

			"getTable", [](Tile* tile) -> Item* { return tile ? tile->getTable() : nullptr; },

			"getCarpet", [](Tile* tile) -> Item* { return tile ? tile->getCarpet() : nullptr; },

			// String representation
			sol::meta_function::to_string, [](Tile* tile) {
				if (!tile){ return std::string("Tile(invalid)");
}
				Position pos = tile->getPosition();
				return "Tile(" + std::to_string(pos.x) + ", " +
					std::to_string(pos.y) + ", " + std::to_string(pos.z) + ")"; }
		);
	}
} // namespace LuaAPI
