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

#include "complexitem.h" // Door
#include "house.h"
#include "tile.h"
#include "map.h"

namespace {

	template <typename Callback>
	bool forEachHouseDoor(const Map* map, const PositionList &houseTiles, uint32_t houseID, const Tile* pendingTile, uint8_t excludedDoorID, Callback callback) {
		const Position pendingPosition = pendingTile ? pendingTile->getPosition() : Position();
		PositionList tilePositions = houseTiles;
		if (pendingTile && pendingTile->getHouseID() == houseID && std::find(tilePositions.begin(), tilePositions.end(), pendingPosition) == tilePositions.end()) {
			tilePositions.push_back(pendingPosition);
		}

		bool excludedDoorSkipped = false;
		for (const Position &tilePosition : tilePositions) {
			const Tile* liveTile = map->getTile(tilePosition);
			if (!liveTile || liveTile->getHouseID() != houseID) {
				continue;
			}

			const Tile* tile = pendingTile && tilePosition == pendingPosition ? pendingTile : liveTile;
			if (tile->getHouseID() != houseID) {
				continue;
			}

			for (const Item* item : tile->items) {
				const auto* door = dynamic_cast<const Door*>(item);
				if (!door || door->getDoorID() == 0) {
					continue;
				}
				if (!excludedDoorSkipped && pendingTile && tilePosition == pendingPosition && door->getDoorID() == excludedDoorID) {
					excludedDoorSkipped = true;
					continue;
				}
				if (callback(tilePosition, door)) {
					return true;
				}
			}
		}
		return false;
	}

} // namespace

Houses::Houses(Map &map) :
	map(map),
	max_house_id(0) {
	////
}

Houses::~Houses() {
	for (auto it = houses.begin(); it != houses.end(); ++it) {
		delete it->second;
	}
	houses.clear();
}

uint32_t Houses::getEmptyID() {
	return ++max_house_id;
}

void Houses::addHouse(House* new_house) {
	ASSERT(new_house);
	HouseMap::iterator it = houses.find(new_house->id);
	ASSERT(it == houses.end());
	new_house->map = &map;
	if (new_house->id > max_house_id) {
		max_house_id = new_house->id;
	}
	houses[new_house->id] = new_house;
}

void Houses::removeHouse(House* house_to_remove) {
	HouseMap::iterator it = houses.find(house_to_remove->id);
	if (it != houses.end()) {
		houses.erase(it);
	}

	house_to_remove->clean();
	delete house_to_remove;
}

House* Houses::getHouse(uint32_t houseid) {
	HouseMap::iterator it = houses.find(houseid);
	if (it != houses.end()) {
		return it->second;
	}
	return nullptr;
}

const House* Houses::getHouse(uint32_t houseid) const {
	HouseMap::const_iterator it = houses.find(houseid);
	if (it != houses.end()) {
		return it->second;
	}

	return nullptr;
}

House::House(Map &map) :
	id(0),
	rent(0),
	townid(0),
	guildhall(false),
	clientid(0),
	beds(0),
	map(&map),
	exit(0, 0, 0) {
	////
}

void House::clean() {
	for (PositionList::const_iterator pos_iter = tiles.begin(); pos_iter != tiles.end(); ++pos_iter) {
		Tile* tile = map->getTile(*pos_iter);
		if (tile) {
			tile->setHouse(nullptr);
		}
	}

	Tile* tile = map->getTile(exit);
	if (tile) {
		tile->removeHouseExit(this);
	}
}

size_t House::size() const {
	size_t count = 0;
	for (PositionList::const_iterator pos_iter = tiles.begin(); pos_iter != tiles.end(); ++pos_iter) {
		auto tile = map->getTile(*pos_iter);
		auto topItem = tile->getTopItem();
		if (tile && (!tile->getWall() || tile->getTable() || topItem->isDoor())) {
			++count;
		}
	}
	return count;
}

void House::addTile(Tile* tile) {
	ASSERT(tile);
	tile->setHouse(this);
	const Position &position = tile->getPosition();
	if (tilePositionIndex.insert(position).second) {
		tiles.push_back(position);
	}
}

void House::removeTile(Tile* tile) {
	ASSERT(tile);
	bool removed = false;
	tilePositionIndex.erase(tile->getPosition());
	for (auto tile_iter = tiles.begin(); tile_iter != tiles.end();) {
		if (*tile_iter == tile->getPosition()) {
			tile_iter = tiles.erase(tile_iter);
			removed = true;
		} else {
			++tile_iter;
		}
	}
	if (removed) {
		tile->setHouse(nullptr);
	}
}

uint8_t House::getEmptyDoorID(const Tile* pendingTile, const std::bitset<256> &reservedDoorIDs) const {
	auto taken = reservedDoorIDs;
	const auto addDoorIDs = [&taken](const Tile* tile) {
		if (!tile) {
			return;
		}
		for (const Item* item : tile->items) {
			const auto* door = dynamic_cast<const Door*>(item);
			if (door) {
				taken.set(door->getDoorID());
			}
		}
	};
	bool pendingTileIncluded = false;
	for (const Position &tilePosition : tiles) {
		const bool isPendingTile = pendingTile && pendingTile->getPosition() == tilePosition;
		const Tile* tile = isPendingTile ? pendingTile : map->getTile(tilePosition);
		if (!tile || tile->getHouseID() != id) {
			continue;
		}
		addDoorIDs(tile);
		pendingTileIncluded = pendingTileIncluded || isPendingTile;
	}
	if (pendingTile && !pendingTileIncluded && pendingTile->getHouseID() == id) {
		addDoorIDs(pendingTile);
	}

	for (size_t doorID = 1; doorID < taken.size(); ++doorID) {
		if (!taken[doorID]) {
			return static_cast<uint8_t>(doorID);
		}
	}
	// Door ID 0 means "unassigned". Do not return an occupied ID when the
	// supported range is exhausted.
	return 0;
}

bool House::hasDoorID(uint8_t doorID, const Tile* pendingTile, uint8_t excludedDoorID) const {
	if (doorID == 0) {
		return false;
	}
	return forEachHouseDoor(map, tiles, id, pendingTile, excludedDoorID, [doorID](const Position &, const Door* door) {
		return door->getDoorID() == doorID;
	});
}

std::map<uint8_t, std::vector<Position>> House::getDoorIDPositions(const Tile* pendingTile, uint8_t excludedDoorID) const {
	std::map<uint8_t, std::vector<Position>> doorPositions;
	forEachHouseDoor(map, tiles, id, pendingTile, excludedDoorID, [&doorPositions](const Position &tilePosition, const Door* door) {
		doorPositions[door->getDoorID()].push_back(tilePosition);
		return false;
	});
	return doorPositions;
}

std::string House::getDescription() {
	std::ostringstream os;
	os << name;
	os << " (ID:" << id << "; Rent: " << rent << "; Max Beds: " << beds << ")";
	return os.str();
}

void House::setExit(Map* targetmap, const Position &pos) {
	// This might fail when the user decides to put an exit at 0,0,0, let's just hope noone does (Noone SHOULD, so there is no problem? Hm?)
	if (pos == exit || !pos.isValid()) {
		return;
	}

	if (exit.isValid()) {
		Tile* oldexit = targetmap->getTile(exit);
		if (oldexit) {
			oldexit->removeHouseExit(this);
		}
	}

	Tile* newexit = targetmap->getTile(pos);
	if (!newexit) {
		newexit = targetmap->allocator(targetmap->createTileL(pos));
		targetmap->setTile(pos, newexit);
	}

	newexit->addHouseExit(this);
	exit = pos;
}

void House::setExit(const Position &pos) {
	setExit(map, pos);
}
