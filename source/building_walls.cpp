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

#include "building_walls.h"

#include "action.h"
#include "brush.h"
#include "editor.h"
#include "house.h"
#include "item.h"
#include "items.h"
#include "map.h"
#include "tile.h"
#include "wall_brush.h"

#include <array>
#include <optional>

namespace {
	// Walled areas bigger than this are outdoors, like a town behind its walls
	constexpr size_t maxRoomTiles = 1600;
	// A door or a counter into something bigger than this leads out of the building
	constexpr size_t maxRoomBehindDoor = 400;
	// Share of matching walls that makes the walls of another floor part of the same building
	constexpr size_t floorOverlapPercent = 40;
	// Walls that close no room are no building, so following them stops somewhere around here
	constexpr size_t maxLooseWalls = 1600;
	// Maps have stray tiles of a house in other towns, and no house is wider than this
	constexpr int maxHouseReach = 64;

	// The visible face of a wall looks south or east, so those sides are tried first
	const std::array<Position, 4> cardinalDirections = {
		Position(0, 1, 0),
		Position(1, 0, 0),
		Position(0, -1, 0),
		Position(-1, 0, 0),
	};

	const std::array<Position, 4> diagonalDirections = {
		Position(1, 1, 0),
		Position(-1, 1, 0),
		Position(1, -1, 0),
		Position(-1, -1, 0),
	};

	// A position and the eight around it
	std::array<Position, 9> withNeighbours(const Position &position) {
		std::array<Position, 9> positions;
		size_t index = 0;
		for (int y = -1; y <= 1; ++y) {
			for (int x = -1; x <= 1; ++x) {
				positions[index++] = position + Position(x, y, 0);
			}
		}
		return positions;
	}

	bool isLooseArchway(const Item* item) {
		return !item->isWall() && as_lower_str(item->getFullName()).contains("archway");
	}

	const Item* findLooseArchway(const Tile* tile) {
		for (const Item* item : tile->items) {
			if (isLooseArchway(item)) {
				return item;
			}
		}
		return nullptr;
	}

	bool isStructuralWall(const Item* item) {
		// Pipes and cracks are drawn with wall brushes too, but nothing stops at them
		if (!item->isWall() || (!item->isBlocking() && !item->isBrushDoor())) {
			return false;
		}
		const WallBrush* brush = item->getWallBrush();
		return brush && !brush->isWallDecoration();
	}

	// A full wall comes first, a railing on the same tile is not what a click on that wall means
	const Item* findStructuralWall(const Tile* tile) {
		const Item* found = nullptr;
		for (const Item* item : tile->items) {
			if (isStructuralWall(item)) {
				if (item->isBlockMissiles()) {
					return item;
				}
				found = found ? found : item;
			}
		}
		return found;
	}

	// Rooms end at anything that can not be crossed or moved away. Item flags decide this instead
	// of wall brushes because maps are full of walls and windows that no brush knows.
	bool isRoomLimit(const Tile* tile) {
		if (!tile) {
			return false;
		}
		if (tile->ground && tile->ground->isBlocking() && tile->ground->isBlockMissiles()) {
			return true;
		}
		for (const Item* item : tile->items) {
			if (isStructuralWall(item) || item->isDoor() || (item->isBlocking() && !item->isMoveable()) || isLooseArchway(item)) {
				return true;
			}
		}
		return false;
	}

	// Counters, lockers and railings stop a walker but not the view, so they stand inside rooms
	bool isFurniture(const Tile* tile) {
		if (!tile || (tile->ground && tile->ground->isBlocking())) {
			return false;
		}

		bool blocks = false;
		for (const Item* item : tile->items) {
			if (item->isDoor() || item->isBrushDoor() || isLooseArchway(item) || item->isBlockMissiles()) {
				return false;
			}
			blocks = blocks || (item->isBlocking() && !item->isMoveable());
		}
		return blocks;
	}

	bool isUnderRoof(const Map &map, const Position &position) {
		return position.z > rme::MapMinLayer && map.getTile(position.x, position.y, position.z - 1) != nullptr;
	}

	bool isMostlyRoofed(const Map &map, const std::set<Position> &room) {
		const size_t covered = std::count_if(room.begin(), room.end(), [&map](const Position &position) {
			return isUnderRoof(map, position);
		});
		return covered * 2 >= room.size();
	}

	bool hasSolidWall(const Tile* tile) {
		return tile && std::any_of(tile->items.begin(), tile->items.end(), [](const Item* item) {
				   return isStructuralWall(item) && (item->isBlockMissiles() || item->isBrushDoor());
			   });
	}

	// Something that blocks walkers between two walls is a window that no brush knows
	bool isInWallLine(const Map &map, const Position &position) {
		const auto solidAt = [&map, &position](int x, int y) {
			return hasSolidWall(map.getTile(position.x + x, position.y + y, position.z));
		};
		return (solidAt(-1, 0) && solidAt(1, 0)) || (solidAt(0, -1) && solidAt(0, 1));
	}

	bool isDoorway(const Tile* tile) {
		if (!tile) {
			return false;
		}
		if (findLooseArchway(tile)) {
			return true;
		}
		for (const Item* item : tile->items) {
			// Some windows are named after their wall, so the brush knows better than the name
			if (item->isBrushDoor() && item->getWallBrush()) {
				const ::DoorType type = item->getWallBrush()->getDoorTypeFromID(item->getID());
				return type != WALL_WINDOW && type != WALL_HATCH_WINDOW;
			}
			if (item->isDoor()) {
				return !as_lower_str(item->getFullName()).contains("window");
			}
		}
		return false;
	}

	class RoomFinder {
	public:
		explicit RoomFinder(const Map &map) :
			map(map) { }

		// The tiles of the room around a position, empty when the search reaches the outside
		std::set<Position> findRoom(const Position &start) {
			std::set<Position> room;
			std::set<Position> visited;
			std::queue<Position> pending;
			pending.push(start);

			while (!pending.empty()) {
				const Position position = pending.front();
				pending.pop();
				if (!visited.insert(position).second) {
					continue;
				}

				const Tile* tile = position.isValid() ? map.getTile(position) : nullptr;
				if (isRoomLimit(tile)) {
					continue;
				}

				if (!tile || !tile->hasGround() || outdoors.contains(position) || room.size() >= maxRoomTiles) {
					outdoors.insert(position);
					outdoors.insert(room.begin(), room.end());
					return {};
				}

				room.insert(position);
				for (const Position &direction : cardinalDirections) {
					pending.push(position + direction);
				}
			}
			return room;
		}

		std::vector<std::set<Position>> findRoomsAround(const Position &wall) {
			return collectSides(wall, [this](const Position &start, const Position &direction) {
				// Lockers against a wall do not hide the room they stand in
				Position position = start;
				while (standsInRoom(position)) {
					position = position + direction;
				}

				std::set<Position> room = findRoom(position);
				// Under an open sky this is a terrace, a roof or a street between the houses
				if (position.z <= rme::MapGroundLayer && !isRoofed(room)) {
					room.clear();
				}
				return room;
			});
		}

		// A building can stand open to the street, like a gate without a door. Its roof still shows
		// where it ends, which says nothing underground because rock covers all of it.
		std::set<Position> findRoofedArea(const Position &start) const {
			std::set<Position> area;
			if (start.z > rme::MapGroundLayer) {
				return area;
			}

			std::set<Position> visited;
			std::queue<Position> pending;
			pending.push(start);

			while (!pending.empty()) {
				const Position position = pending.front();
				pending.pop();
				if (!position.isValid() || !visited.insert(position).second) {
					continue;
				}

				const Tile* tile = map.getTile(position);
				if (!tile || !tile->hasGround() || !isUnderRoof(map, position) || (isRoomLimit(tile) && !standsInRoom(position))) {
					continue;
				}

				area.insert(position);
				if (area.size() >= maxRoomTiles) {
					return {};
				}
				for (const Position &direction : cardinalDirections) {
					pending.push(position + direction);
				}
			}
			return area;
		}

		std::vector<std::set<Position>> findRoofedAreasAround(const Position &wall) const {
			return collectSides(wall, [this](const Position &start, const Position &) { return findRoofedArea(start); });
		}

		// Rooms that open into each other are one building, and what stands in a room does not divide it
		std::set<Position> expandRooms(std::set<Position> interior, bool throughDoors) {
			const bool indoors = isRoofed(interior);
			const std::set<uint32_t> houses = findHouses(interior);

			std::set<Position> rejected;
			PositionVector pending(interior.begin(), interior.end());

			const auto addRoom = [&](const Position &start) {
				const std::set<Position> room = findRoom(start);
				// A way out of a roofed building leads to the street, even when walls enclose it
				if (room.empty() || room.size() > maxRoomBehindDoor || (indoors && !isRoofed(room)) || leavesHouses(room, houses)) {
					rejected.insert(start);
					rejected.insert(room.begin(), room.end());
					return;
				}

				interior.insert(room.begin(), room.end());
				pending.insert(pending.end(), room.begin(), room.end());
			};

			while (!pending.empty()) {
				const Position position = pending.back();
				pending.pop_back();

				for (const Position &direction : cardinalDirections) {
					const Position next = position + direction;
					if (interior.contains(next) || rejected.contains(next)) {
						continue;
					}

					if (standsInRoom(next)) {
						interior.insert(next);
						pending.push_back(next);
						continue;
					}

					const std::optional<Position> start = findRoomStart(next, direction, throughDoors);
					if (start && !interior.contains(*start) && !rejected.contains(*start)) {
						addRoom(*start);
					}
				}
			}
			return interior;
		}

	private:
		std::set<uint32_t> findHouses(const std::set<Position> &tiles) const {
			std::set<uint32_t> houses;
			for (const Position &position : tiles) {
				const uint32_t house = map.getTile(position)->getHouseID();
				if (house != 0) {
					houses.insert(house);
				}
			}
			return houses;
		}

		bool leavesHouses(const std::set<Position> &room, const std::set<uint32_t> &houses) const {
			const size_t inside = std::count_if(room.begin(), room.end(), [this, &houses](const Position &position) {
				return houses.contains(map.getTile(position)->getHouseID());
			});
			return !houses.empty() && inside * 2 < room.size();
		}

		// Where a room can go on from the tile next to it
		std::optional<Position> findRoomStart(const Position &next, const Position &direction, bool throughDoors) const {
			const Tile* tile = next.isValid() ? map.getTile(next) : nullptr;
			if (tile && !isRoomLimit(tile)) {
				return next;
			}
			if (throughDoors && isDoorway(tile)) {
				return next + direction;
			}
			return std::nullopt;
		}

		bool standsInRoom(const Position &position) const {
			return position.isValid() && isFurniture(map.getTile(position)) && !isInWallLine(map, position);
		}

		template <typename Search>
		static std::vector<std::set<Position>> collectSides(const Position &wall, Search search) {
			std::vector<std::set<Position>> rooms;
			const auto addSide = [&rooms, &wall, &search](const Position &direction) {
				std::set<Position> room = search(wall + direction, direction);
				const bool known = std::any_of(rooms.begin(), rooms.end(), [&room](const std::set<Position> &other) {
					return !room.empty() && other.contains(*room.begin());
				});
				if (!room.empty() && !known) {
					rooms.push_back(std::move(room));
				}
			};

			for (const Position &direction : cardinalDirections) {
				addSide(direction);
			}
			// A corner only touches its room across the diagonal
			for (const Position &direction : diagonalDirections) {
				addSide(direction);
			}
			return rooms;
		}

		bool isRoofed(const std::set<Position> &room) const {
			return isMostlyRoofed(map, room);
		}

		const Map &map;
		std::set<Position> outdoors;
	};

	WallBrush* findLooseArchwayBrush(const Map &map, const Position &position, ::BorderType &alignment) {
		const auto wallAt = [&map](const Position &wall) {
			const Tile* tile = map.getTile(wall);
			return tile ? findStructuralWall(tile) : nullptr;
		};

		const Item* west = wallAt(position + Position(-1, 0, 0));
		const Item* east = wallAt(position + Position(1, 0, 0));
		if (west && east && west->getWallBrush() == east->getWallBrush()) {
			alignment = WALL_HORIZONTAL;
			return west->getWallBrush();
		}

		const Item* north = wallAt(position + Position(0, -1, 0));
		const Item* south = wallAt(position + Position(0, 1, 0));
		if (north && south && north->getWallBrush() == south->getWallBrush()) {
			alignment = WALL_VERTICAL;
			return north->getWallBrush();
		}
		return nullptr;
	}

	void readWalls(const Map &map, const Position &position, std::vector<BuildingWall> &walls) {
		const Tile* tile = map.getTile(position);
		if (!tile) {
			return;
		}

		for (const Item* item : tile->items) {
			if (!isStructuralWall(item)) {
				continue;
			}

			BuildingWall wall;
			wall.position = position;
			wall.brush = item->getWallBrush();
			wall.alignment = item->getWallAlignment();
			wall.itemId = item->getID();
			if (item->isBrushDoor()) {
				wall.opening = wall.brush->getDoorTypeFromID(item->getID());
				wall.open = item->isOpen();
			}
			wall.low = wall.opening == WALL_UNDEFINED && (!item->isBlockMissiles() || wall.brush->isRailing());
			walls.push_back(wall);
		}

		if (const Item* archway = findLooseArchway(tile)) {
			BuildingWall wall;
			wall.position = position;
			wall.brush = findLooseArchwayBrush(map, position, wall.alignment);
			wall.itemId = archway->getID();
			wall.opening = WALL_ARCHWAY;
			if (wall.brush) {
				walls.push_back(wall);
			}
		}
	}

	bool isUnknownWall(const Tile* tile) {
		if (!tile || findStructuralWall(tile)) {
			return false;
		}
		return std::any_of(tile->items.begin(), tile->items.end(), [](const Item* item) {
			return item->isBlocking() && item->isBlockMissiles() && !item->isMoveable();
		});
	}

	struct FloorScan {
		std::vector<BuildingWall> walls;
		std::set<Position> rooms;
		bool enclosed = false;
		bool openToStreet = false;
		int sideCount = 0;
		int side = 0;
		size_t unknownWalls = 0;
	};

	// A wall of one style against a house of another belongs to whatever else it closes, but
	// a wall that touches a house made of that same style is the house itself
	class HouseStyles {
	public:
		explicit HouseStyles(const Map &map) :
			map(map) { }

		bool isHouseWall(const Position &position, const WallBrush* brush) {
			const std::array<Position, 9> around = withNeighbours(position);
			return std::any_of(around.begin(), around.end(), [this, brush](const Position &neighbour) {
				const Tile* tile = map.getTile(neighbour);
				return tile && tile->getHouseID() != 0 && getStyle(tile->getHouseID()) == brush;
			});
		}

	private:
		const WallBrush* getStyle(uint32_t id) {
			const auto known = styles.find(id);
			if (known != styles.end()) {
				return known->second;
			}

			std::map<const WallBrush*, size_t> counts;
			const House* house = map.houses.getHouse(id);
			if (house) {
				countWallStyles(*house, counts);
			}

			const auto mostUsed = std::max_element(counts.begin(), counts.end(), [](const auto &left, const auto &right) {
				return left.second < right.second;
			});
			const WallBrush* style = mostUsed != counts.end() ? mostUsed->first : nullptr;
			styles[id] = style;
			return style;
		}

		void countWallStyles(const House &house, std::map<const WallBrush*, size_t> &counts) const {
			std::set<Position> visited;
			for (const Position &tile : house.getTiles()) {
				for (const Position &adjacent : withNeighbours(tile)) {
					const Tile* other = visited.insert(adjacent).second ? map.getTile(adjacent) : nullptr;
					const Item* wall = other ? findStructuralWall(other) : nullptr;
					if (wall) {
						++counts[wall->getWallBrush()];
					}
				}
			}
		}

		const Map &map;
		std::map<uint32_t, const WallBrush*> styles;
	};

	std::vector<BuildingWall> findConnectedWalls(const Map &map, const Position &seed, const WallBrush* brush, bool stopAtHouses = false) {
		HouseStyles houses(map);
		std::vector<BuildingWall> walls;
		std::set<Position> visited;
		std::queue<Position> pending;
		pending.push(seed);

		while (!pending.empty()) {
			const Position position = pending.front();
			pending.pop();
			if (!position.isValid() || !visited.insert(position).second) {
				continue;
			}

			std::vector<BuildingWall> found;
			readWalls(map, position, found);
			const bool matches = std::any_of(found.begin(), found.end(), [brush](const BuildingWall &wall) {
				return wall.brush == brush;
			});
			if (!matches || (stopAtHouses && !(position == seed) && houses.isHouseWall(position, brush))) {
				continue;
			}

			// Another style on the same tile belongs to whatever else stands there
			std::copy_if(found.begin(), found.end(), std::back_inserter(walls), [brush](const BuildingWall &wall) {
				return wall.brush == brush;
			});
			if (stopAtHouses && walls.size() >= maxLooseWalls) {
				break;
			}

			for (const Position &direction : cardinalDirections) {
				pending.push(position + direction);
			}
		}
		return walls;
	}

	int chooseSide(const Map &map, const Position &wall, const std::vector<std::set<Position>> &rooms) {
		const Tile* tile = map.getTile(wall);
		const uint32_t house = tile ? tile->getHouseID() : 0;

		// The house of this wall, then any house, then what has a roof, because streets between
		// houses are closed too. The smallest area last, a street is bigger than what it surrounds.
		const auto rank = [&map, house](const std::set<Position> &room) {
			size_t sameHouse = 0;
			size_t anyHouse = 0;
			for (const Position &position : room) {
				const uint32_t id = map.getTile(position)->getHouseID();
				sameHouse += house != 0 && id == house ? 1 : 0;
				anyHouse += id != 0 ? 1 : 0;
			}
			return std::make_tuple(sameHouse != 0, anyHouse * 2 >= room.size(), isMostlyRoofed(map, room), -static_cast<int64_t>(room.size()));
		};

		size_t best = 0;
		for (size_t index = 1; index < rooms.size(); ++index) {
			if (rank(rooms[index]) > rank(rooms[best])) {
				best = index;
			}
		}
		return static_cast<int>(best);
	}

	void markSharedWalls(const Map &map, RoomFinder &finder, const std::set<Position> &interior, std::vector<BuildingWall> &walls) {
		std::set<Position> otherRooms;
		for (BuildingWall &wall : walls) {
			for (const Position &direction : cardinalDirections) {
				const Position neighbour = wall.position + direction;
				if (interior.contains(neighbour) || isRoomLimit(map.getTile(neighbour))) {
					continue;
				}

				if (!otherRooms.contains(neighbour)) {
					const std::set<Position> room = finder.findRoom(neighbour);
					otherRooms.insert(room.begin(), room.end());
				}
				if (otherRooms.contains(neighbour)) {
					wall.shared = true;
					break;
				}
			}
		}
	}

	std::set<Position> collectRoomWalls(const Map &map, FloorScan &scan) {
		std::set<Position> visited;
		for (const Position &position : scan.rooms) {
			for (const Position &adjacent : withNeighbours(position)) {
				// Railings are part of the rooms they stand in, but they are walls all the same
				const bool inside = scan.rooms.contains(adjacent) && !isFurniture(map.getTile(adjacent));
				if (inside || !visited.insert(adjacent).second) {
					continue;
				}

				readWalls(map, adjacent, scan.walls);
				const Tile* tile = map.getTile(adjacent);
				scan.unknownWalls += isUnknownWall(tile) || (isFurniture(tile) && isInWallLine(map, adjacent)) ? 1 : 0;
			}
		}
		return visited;
	}

	// Finds the rooms around a wall and takes the one on the requested side
	void findFloorRooms(const Map &map, RoomFinder &finder, const Position &seed, int requestedSide, const WallBrush* originBrush, bool followWalls, FloorScan &scan) {
		const auto findSides = [&finder, &scan](const Position &wall) {
			std::vector<std::set<Position>> rooms = finder.findRoomsAround(wall);
			if (rooms.empty()) {
				rooms = finder.findRoofedAreasAround(wall);
				scan.openToStreet = !rooms.empty();
			}
			return rooms;
		};

		Position start = seed;
		std::vector<std::set<Position>> rooms = findSides(start);

		// A wall over a void touches no room, but the walls it connects to do
		if (rooms.empty() && followWalls) {
			for (const BuildingWall &wall : findConnectedWalls(map, seed, originBrush)) {
				start = wall.position;
				rooms = findSides(start);
				if (!rooms.empty()) {
					break;
				}
			}
		}

		if (!rooms.empty()) {
			scan.enclosed = true;
			scan.sideCount = static_cast<int>(rooms.size());
			scan.side = requestedSide >= 0 ? requestedSide % scan.sideCount : chooseSide(map, start, rooms);
			scan.rooms = std::move(rooms[scan.side]);
		}
	}

	FloorScan scanFloor(const Map &map, RoomFinder &finder, const Position &seed, BuildingScope scope, int requestedSide, const WallBrush* originBrush, bool followWalls = true) {
		FloorScan scan;

		if (scope != BuildingScope::ConnectedWalls) {
			findFloorRooms(map, finder, seed, requestedSide, originBrush, followWalls, scan);
		}

		if (!scan.enclosed) {
			if (followWalls) {
				scan.walls = findConnectedWalls(map, seed, originBrush, scope != BuildingScope::ConnectedWalls);
			}
			return scan;
		}

		scan.rooms = finder.expandRooms(std::move(scan.rooms), scope == BuildingScope::Building);

		// A counter can close a corner of an open building into a room of its own
		if (scope == BuildingScope::Building && !scan.openToStreet) {
			std::set<Position> area = finder.findRoofedArea(*scan.rooms.begin());
			if (area.size() >= scan.rooms.size() * 2) {
				scan.openToStreet = true;
				scan.rooms = finder.expandRooms(std::move(area), true);
			}
		}

		std::set<Position> visited = collectRoomWalls(map, scan);

		// Parts of an open building can have an entrance of their own, but its walls lead to them
		if (scan.openToStreet && scope == BuildingScope::Building) {
			for (const BuildingWall &wall : findConnectedWalls(map, seed, originBrush, true)) {
				if (visited.insert(wall.position).second) {
					scan.walls.push_back(wall);
				}
			}
		}

		markSharedWalls(map, finder, scan.rooms, scan.walls);
		return scan;
	}

	void addFloor(BuildingScan &scan, int z, FloorScan &&floor) {
		// Besides railings, low pieces like store counters have nothing to do with a wall style
		const bool lowOrigin = isLowWallStyle(scan.originBrush);

		// The floors of a building can use different styles, so each one also goes by its most used style
		std::map<const WallBrush*, size_t> styleCounts;
		for (const BuildingWall &wall : floor.walls) {
			if (!wall.low) {
				++styleCounts[wall.brush];
			}
		}

		const auto mostUsed = std::max_element(styleCounts.begin(), styleCounts.end(), [](const auto &left, const auto &right) {
			return left.second < right.second;
		});
		const WallBrush* floorStyle = mostUsed != styleCounts.end() ? mostUsed->first : scan.originBrush;
		const WallBrush* originBrush = scan.originBrush;
		const auto isOwnStyle = [floorStyle, originBrush](const BuildingWall &wall) {
			return wall.brush == originBrush || wall.brush == floorStyle;
		};

		// Two buildings that touch can keep their walls on the same tile
		std::set<Position> ownTiles;
		for (const BuildingWall &wall : floor.walls) {
			if (isOwnStyle(wall)) {
				ownTiles.insert(wall.position);
			}
		}

		// A roof can reach over an alley to the walls of the next building, unlike a closed room
		const bool openToStreet = floor.openToStreet;
		floor.walls.erase(std::remove_if(floor.walls.begin(), floor.walls.end(), [&ownTiles, &isOwnStyle, lowOrigin, openToStreet](const BuildingWall &wall) {
							  const bool neighbour = !isOwnStyle(wall) && (ownTiles.contains(wall.position) || (openToStreet && !wall.low));
							  return neighbour || (wall.low != lowOrigin && !(wall.low && wall.brush->isRailing()));
						  }),
						  floor.walls.end());

		scan.tiles.insert(floor.rooms.begin(), floor.rooms.end());
		for (const BuildingWall &wall : floor.walls) {
			scan.tiles.insert(wall.position);
		}
		scan.unknownWalls += floor.unknownWalls;
		scan.floors.push_back({ z, std::move(floor.walls) });
	}

	bool hasStairs(const Map &map, const std::set<Position> &rooms) {
		return std::any_of(rooms.begin(), rooms.end(), [&map](const Position &position) {
			const Tile* tile = map.getTile(position);
			if (tile->ground && g_items.getItemType(tile->ground->getID()).isFloorChange()) {
				return true;
			}
			return std::any_of(tile->items.begin(), tile->items.end(), [](const Item* item) {
				return g_items.getItemType(item->getID()).isFloorChange();
			});
		});
	}

	// What overlaps the most with the floor next to it, and closes rooms unless the scope follows the walls
	FloorScan findBestFloor(const Map &map, const BuildingScan &scan, const std::set<Position> &projection, BuildingScope scope, size_t &bestOverlap) {
		FloorScan best;
		bestOverlap = 0;

		RoomFinder finder(map);
		std::set<Position> visited;
		for (const Position &seed : projection) {
			const Tile* tile = map.getTile(seed);
			if (visited.contains(seed) || !tile || !findStructuralWall(tile)) {
				continue;
			}

			FloorScan candidate = scanFloor(map, finder, seed, scope, -1, scan.originBrush, scope == BuildingScope::ConnectedWalls);
			size_t overlap = 0;
			for (const BuildingWall &wall : candidate.walls) {
				visited.insert(wall.position);
				overlap += projection.count(wall.position);
			}
			if (overlap > bestOverlap) {
				bestOverlap = overlap;
				best = std::move(candidate);
			}
		}
		return best;
	}

	// A building continues on the floor whose walls overlap the most with the floor next to it
	void scanFloorsInDirection(const Map &map, BuildingScan &scan, const std::vector<BuildingWall> &start, const std::set<Position> &startRooms, int originZ, int step, BuildingScope scope) {
		std::vector<BuildingWall> previous = start;
		std::set<Position> previousRooms = startRooms;
		for (int z = originZ + step; z >= rme::MapMinLayer && z <= rme::MapMaxLayer; z += step) {
			std::set<Position> projection;
			for (const BuildingWall &wall : previous) {
				const Position projected(wall.position.x, wall.position.y, z);
				projection.insert(projected);
				for (const Position &direction : cardinalDirections) {
					projection.insert(projected + direction);
				}
			}

			size_t bestOverlap = 0;
			FloorScan best = findBestFloor(map, scan, projection, scope, bestOverlap);

			// Only rooms say which walls belong together, so walls that reach a neighbour are the last resort
			size_t connectedOverlap = 0;
			FloorScan connected = findBestFloor(map, scan, projection, BuildingScope::ConnectedWalls, connectedOverlap);
			if (bestOverlap * 100 < connectedOverlap * floorOverlapPercent) {
				best = std::move(connected);
				bestOverlap = connectedOverlap;
			}

			// Past this size the walls and rooms around are a cave or a whole town, not a building
			size_t pieces = best.walls.size();
			for (const BuildingFloor &floor : scan.floors) {
				pieces += floor.walls.size();
			}
			if (pieces > maxLooseWalls) {
				return;
			}

			// A small room under a big building proves nothing by itself. Stairs can tell, as long as they
			// neither lead underground, where tunnels pass under anything, nor into something bigger.
			const size_t smallest = std::min(previous.size(), best.walls.size());
			const size_t largest = std::max(previous.size(), best.walls.size());
			const bool sameShape = bestOverlap * 100 >= largest * floorOverlapPercent;
			const bool sameLevel = (z <= rme::MapGroundLayer) == (z - step <= rme::MapGroundLayer);
			const bool notBigger = best.walls.size() * 2 <= previous.size() * 3;
			const bool linked = sameLevel && notBigger && bestOverlap * 100 >= smallest * floorOverlapPercent && (hasStairs(map, previousRooms) || hasStairs(map, best.rooms));
			if (smallest == 0 || (!sameShape && !linked)) {
				return;
			}

			previous = best.walls;
			previousRooms = best.rooms;
			addFloor(scan, z, std::move(best));
		}
	}

	// A wall standing on a wall of the building is part of it, even over a void where no room finds it
	void addStackedWalls(const Map &map, BuildingScan &scan) {
		HouseStyles houses(map);
		const auto copyWalls = [&map, &scan, &houses](const BuildingFloor &from, BuildingFloor &to) {
			if (std::abs(from.z - to.z) != 1) {
				return;
			}

			std::set<Position> known;
			for (const BuildingWall &wall : to.walls) {
				known.insert(wall.position);
			}

			for (const BuildingWall &wall : from.walls) {
				const Position stacked(wall.position.x, wall.position.y, to.z);
				// What stands on a wall shared with a neighbour can just as well be theirs
				if (wall.low || wall.shared || wall.brush != scan.originBrush || !known.insert(stacked).second) {
					continue;
				}

				std::vector<BuildingWall> found;
				readWalls(map, stacked, found);
				// Another style on top of a wall is the next building, whose floor can reach over this one
				for (const BuildingWall &piece : found) {
					if (piece.brush == wall.brush && !houses.isHouseWall(stacked, piece.brush)) {
						to.walls.push_back(piece);
						scan.tiles.insert(stacked);
					}
				}
			}
		};

		for (size_t index = 1; index < scan.floors.size(); ++index) {
			copyWalls(scan.floors[index - 1], scan.floors[index]);
		}
		for (size_t index = scan.floors.size(); index > 1; --index) {
			copyWalls(scan.floors[index - 1], scan.floors[index - 2]);
		}
	}

	// The first house a check finds around a wall, which touches what is across a corner on the diagonals
	template <typename Check>
	const House* findHouseAround(Check check) {
		for (const Position &direction : cardinalDirections) {
			if (const House* house = check(direction)) {
				return house;
			}
		}
		for (const Position &direction : diagonalDirections) {
			if (const House* house = check(direction)) {
				return house;
			}
		}
		return nullptr;
	}

	// The house most of these tiles belong to
	const House* findMainHouse(const Map &map, const std::set<Position> &rooms) {
		std::map<uint32_t, size_t> counts;
		for (const Position &position : rooms) {
			const uint32_t id = map.getTile(position)->getHouseID();
			if (id != 0) {
				++counts[id];
			}
		}

		const auto mostUsed = std::max_element(counts.begin(), counts.end(), [](const auto &left, const auto &right) {
			return left.second < right.second;
		});
		if (mostUsed == counts.end() || mostUsed->second * 2 < rooms.size()) {
			return nullptr;
		}

		const House* house = map.houses.getHouse(mostUsed->first);
		return house && !house->getTiles().empty() ? house : nullptr;
	}

	// The house right behind the wall, or else the one most of these tiles belong to
	const House* findHouse(const Map &map, const std::set<Position> &rooms, const Position &wall, bool openArea) {
		bool touchesRooms = false;
		const auto houseBehind = [&map, &rooms, &wall, &touchesRooms](const Position &direction) {
			const Position position = wall + direction;
			if (!rooms.contains(position)) {
				return static_cast<const House*>(nullptr);
			}

			touchesRooms = true;
			const uint32_t id = map.getTile(position)->getHouseID();
			return id != 0 ? map.houses.getHouse(id) : nullptr;
		};

		if (const House* house = findHouseAround(houseBehind)) {
			return house;
		}

		// Seen from a covered alley a thick wall hides the house behind its second layer, and
		// the walls of a terrace close no room at all, so the tiles around them have to tell
		if (openArea || !touchesRooms) {
			const House* house = findHouseAround([&map, &wall](const Position &direction) {
				const Tile* tile = map.getTile(wall + direction);
				return tile && tile->getHouseID() != 0 ? map.houses.getHouse(tile->getHouseID()) : nullptr;
			});
			if (house) {
				return house;
			}
		}

		return findMainHouse(map, rooms);
	}

	// Houses that stand on each other are floors of one building, houses side by side are not
	std::set<const House*> findBuildingHouses(const Map &map, const House &house) {
		std::set<const House*> found = { &house };
		std::vector<const House*> pending = { &house };

		while (!pending.empty()) {
			const House* current = pending.back();
			pending.pop_back();

			for (const Position &position : current->getTiles()) {
				for (const int z : { position.z - 1, position.z + 1 }) {
					const Tile* tile = z >= rme::MapMinLayer && z <= rme::MapMaxLayer ? map.getTile(position.x, position.y, z) : nullptr;
					const House* other = tile && tile->getHouseID() != 0 ? map.houses.getHouse(tile->getHouseID()) : nullptr;
					if (other && found.insert(other).second) {
						pending.push_back(other);
					}
				}
			}
		}
		return found;
	}

	// A house knows its tiles on every floor, which beats any guess from the shape of its walls
	void scanHouses(const Map &map, const std::set<const House*> &houses, const Position &origin, BuildingScan &scan) {
		std::map<int, std::set<Position>> floors;
		for (const House* house : houses) {
			for (const Position &position : house->getTiles()) {
				const bool nearby = std::abs(position.x - origin.x) <= maxHouseReach && std::abs(position.y - origin.y) <= maxHouseReach;
				if (nearby && !hasSolidWall(map.getTile(position))) {
					floors[position.z].insert(position);
				}
			}
		}

		RoomFinder finder(map);
		for (auto &entry : floors) {
			FloorScan floor;
			floor.enclosed = true;
			floor.rooms = std::move(entry.second);
			collectRoomWalls(map, floor);
			markSharedWalls(map, finder, floor.rooms, floor.walls);
			addFloor(scan, entry.first, std::move(floor));
		}
	}

	uint16_t findOpeningItem(const WallBrush* target, const BuildingWall &wall, ::DoorType type) {
		const uint16_t id = target->getDoorItemID(wall.alignment, type, wall.open);
		return id != 0 ? id : target->getDoorItemID(wall.alignment, type, !wall.open);
	}

	uint16_t findWallItem(const WallBrush* target, const BuildingWall &wall) {
		const uint32_t seed = static_cast<uint32_t>(wall.position.x) * 73856093u ^ static_cast<uint32_t>(wall.position.y) * 19349663u ^ static_cast<uint32_t>(wall.position.z) * 83492791u;
		const uint16_t id = target->getWallItemID(wall.alignment, seed);
		return id != 0 ? id : target->getWallItemID(WallBrush::getSimplifiedAlignment(wall.alignment), seed);
	}

	const std::vector<::DoorType> &getOpeningAlternatives(::DoorType type) {
		static const std::vector<::DoorType> none;
		static const std::map<::DoorType, std::vector<::DoorType>> alternatives = {
			{ WALL_DOOR_NORMAL, { WALL_DOOR_LOCKED, WALL_DOOR_QUEST, WALL_DOOR_MAGIC } },
			{ WALL_DOOR_LOCKED, { WALL_DOOR_NORMAL, WALL_DOOR_QUEST, WALL_DOOR_MAGIC } },
			{ WALL_DOOR_QUEST, { WALL_DOOR_MAGIC, WALL_DOOR_LOCKED, WALL_DOOR_NORMAL } },
			{ WALL_DOOR_MAGIC, { WALL_DOOR_QUEST, WALL_DOOR_LOCKED, WALL_DOOR_NORMAL } },
			{ WALL_ARCHWAY, { WALL_DOOR_NORMAL } },
			{ WALL_WINDOW, { WALL_HATCH_WINDOW } },
			{ WALL_HATCH_WINDOW, { WALL_WINDOW } },
		};

		const auto it = alternatives.find(type);
		return it != alternatives.end() ? it->second : none;
	}

	bool isWindow(::DoorType type) {
		return type == WALL_WINDOW || type == WALL_HATCH_WINDOW;
	}

	// The new item of an opening the style has no match for, 0 when it stays as it is
	uint16_t resolveMissingOpening(const WallReplacementRequest &request, const BuildingWall &wall, OpeningGroup &group) {
		group.missing = true;

		for (::DoorType alternative : getOpeningAlternatives(wall.opening)) {
			if (group.closestType == WALL_UNDEFINED && findOpeningItem(request.target, wall, alternative) != 0) {
				group.closestType = alternative;
			}
		}

		const auto choice = request.openingActions.find(wall.opening);
		const OpeningAction action = choice != request.openingActions.end() ? choice->second : OpeningAction::KeepCurrent;
		if (action == OpeningAction::UseClosest && group.closestType != WALL_UNDEFINED) {
			return findOpeningItem(request.target, wall, group.closestType);
		}
		return action == OpeningAction::ReplaceWithWall ? findWallItem(request.target, wall) : 0;
	}

	uint16_t planOpening(const WallReplacementRequest &request, const BuildingWall &wall, std::map<::DoorType, OpeningGroup> &groups, WallReplacementPlan &plan) {
		OpeningGroup &group = groups[wall.opening];
		group.type = wall.opening;
		++group.count;

		uint16_t replacement = findOpeningItem(request.target, wall, wall.opening);
		if (replacement == 0) {
			++plan.missingOpenings;
			replacement = resolveMissingOpening(request, wall, group);
		}

		group.sampleItem = replacement != 0 ? replacement : wall.itemId;
		if (replacement != 0) {
			size_t &count = isWindow(wall.opening) ? plan.windowCount : plan.doorCount;
			++count;
		}
		return replacement;
	}

	// A railing never grows into a wall: it takes the railing of the new style, or it stays
	uint16_t planRailing(const WallReplacementRequest &request, const BuildingWall &wall, WallReplacementPlan &plan) {
		++plan.railingCount;
		plan.railingSample = wall.itemId;

		const bool replaced = plan.railing && request.replaceRailings && wall.brush != plan.railing;
		const uint16_t railing = replaced ? findWallItem(plan.railing, wall) : 0;
		plan.railingsChanged += railing != 0 ? 1 : 0;
		return railing;
	}

	void applyChange(Tile* tile, const WallChange &change) {
		for (Item* item : tile->items) {
			if (item->getID() == change.fromItem) {
				// Keeps the door, action and unique ids of the item
				transformItem(item, change.toItem, tile);
				return;
			}
		}
	}
}

bool isLowWallStyle(const WallBrush* brush) {
	if (!brush) {
		return false;
	}
	if (brush->isRailing()) {
		return true;
	}

	const uint16_t run = brush->getWallItemID(WALL_HORIZONTAL, 0);
	const uint16_t piece = run != 0 ? run : brush->getWallItemID(WALL_VERTICAL, 0);
	return piece != 0 && !g_items.getItemType(piece).blockMissiles;
}

bool hasBuildingWall(const Tile* tile) {
	return tile && findStructuralWall(tile) != nullptr;
}

BuildingScan scanBuilding(const Map &map, const Position &origin, BuildingScope scope, int side) {
	BuildingScan scan;

	const Tile* tile = map.getTile(origin);
	const Item* wall = tile ? findStructuralWall(tile) : nullptr;
	if (!wall) {
		return scan;
	}
	scan.originBrush = wall->getWallBrush();

	// A wall that belongs to no house falls back on the building around it
	const bool onlyHouse = scope == BuildingScope::House;
	if (onlyHouse) {
		scope = BuildingScope::Building;
	}

	RoomFinder finder(map);
	FloorScan floor = scanFloor(map, finder, origin, scope, side, scan.originBrush);
	scan.enclosed = floor.enclosed;
	scan.openToStreet = floor.openToStreet;
	scan.sideCount = floor.sideCount;
	scan.side = floor.side;

	if (const House* house = findHouse(map, floor.rooms, origin, floor.openToStreet)) {
		scan.house = house->id;
		scan.houseName = house->name;

		if (scope == BuildingScope::Building) {
			scanHouses(map, onlyHouse ? std::set<const House*> { house } : findBuildingHouses(map, *house), origin, scan);
			return scan;
		}
	}

	const std::vector<BuildingWall> originWalls = floor.walls;
	const std::set<Position> originRooms = floor.rooms;
	addFloor(scan, origin.z, std::move(floor));

	// A room is one floor by definition, and walls that close no room are no building to go up
	if (scope == BuildingScope::ConnectedWalls || (scope == BuildingScope::Building && scan.enclosed)) {
		scanFloorsInDirection(map, scan, originWalls, originRooms, origin.z, -1, scope);
		scanFloorsInDirection(map, scan, originWalls, originRooms, origin.z, 1, scope);
	}

	std::sort(scan.floors.begin(), scan.floors.end(), [](const BuildingFloor &left, const BuildingFloor &right) {
		return left.z < right.z;
	});

	if (scope == BuildingScope::Building) {
		addStackedWalls(map, scan);
	}
	return scan;
}

bool addWallToScan(BuildingScan &scan, const Map &map, const Position &position) {
	for (BuildingFloor &floor : scan.floors) {
		if (floor.z != position.z) {
			continue;
		}

		std::vector<BuildingWall> walls;
		readWalls(map, position, walls);
		if (walls.empty()) {
			return false;
		}

		floor.walls.insert(floor.walls.end(), walls.begin(), walls.end());
		scan.tiles.insert(position);
		return true;
	}
	return false;
}

std::vector<WallStyle> collectWallStyles(const WallBrush* source) {
	const bool lowSource = isLowWallStyle(source);

	std::vector<WallStyle> styles;
	std::set<std::string> names;
	for (const auto &entry : g_brushes.getMap()) {
		Brush* brush = entry.second;
		if (!brush || !brush->isWall() || brush->isWallDecoration() || !brush->visibleInPalette()) {
			continue;
		}

		WallBrush* wall = brush->asWall();
		const uint16_t run = wall->getWallItemID(WALL_HORIZONTAL, 0);
		if (run == 0 || wall->getWallItemID(WALL_VERTICAL, 0) == 0 || isLowWallStyle(wall) != lowSource) {
			continue;
		}

		if (names.insert(as_lower_str(wall->getName())).second) {
			const uint16_t corner = wall->getWallItemID(WALL_NORTHWEST_DIAGONAL, 0);
			styles.push_back({ wall, corner != 0 ? corner : run, run });
		}
	}

	std::sort(styles.begin(), styles.end(), [](const WallStyle &left, const WallStyle &right) {
		return as_lower_str(left.brush->getName()) < as_lower_str(right.brush->getName());
	});
	return styles;
}

WallReplacementPlan planWallReplacement(const BuildingScan &scan, const WallReplacementRequest &request) {
	WallReplacementPlan plan;
	if (!request.target) {
		return plan;
	}

	std::map<::DoorType, OpeningGroup> groups;
	std::set<int> changedFloors;

	const bool lowTarget = isLowWallStyle(request.target);
	plan.railing = lowTarget ? nullptr : request.target->getRailing();

	for (const BuildingFloor &floor : scan.floors) {
		if (!request.floors.contains(floor.z)) {
			continue;
		}

		for (const BuildingWall &wall : floor.walls) {
			if (request.excluded.contains(wall.position)) {
				continue;
			}

			uint16_t replacement = 0;
			if (wall.low != lowTarget) {
				replacement = wall.low ? planRailing(request, wall, plan) : 0;
			} else if (!request.sources.contains(wall.brush) || (wall.shared && !request.includeShared) || wall.brush == request.target) {
				continue;
			} else if (wall.opening != WALL_UNDEFINED) {
				replacement = planOpening(request, wall, groups, plan);
			} else {
				replacement = findWallItem(request.target, wall);
				if (replacement == 0) {
					plan.problem = wxString::Format("This style has no piece for a wall shape used on floor %d.", wall.position.z);
					plan.changes.clear();
					return plan;
				}
				++plan.wallCount;
			}

			if (replacement != 0) {
				plan.changes.push_back({ wall.position, wall.itemId, replacement });
				changedFloors.insert(wall.position.z);
			}
		}
	}

	plan.floorCount = changedFloors.size();
	for (const auto &entry : groups) {
		plan.openings.push_back(entry.second);
	}
	return plan;
}

void writeWallPreview(const Map &map, const WallReplacementPlan &plan, BaseMap &preview) {
	preview.clear();
	for (const WallChange &change : plan.changes) {
		Tile* tile = preview.getTile(change.position);
		if (!tile) {
			tile = map.getTile(change.position)->deepCopy(preview);
			preview.setTile(change.position, tile);
		}
		applyChange(tile, change);
	}
}

bool commitWallReplacement(Editor &editor, const WallReplacementPlan &plan) {
	Map &map = editor.getMap();

	std::map<Position, Tile*> tiles;
	for (const WallChange &change : plan.changes) {
		Tile*&tile = tiles[change.position];
		if (!tile) {
			tile = map.getTile(change.position)->deepCopy(map);
		}
		applyChange(tile, change);
	}

	if (tiles.empty()) {
		return false;
	}

	Action* action = editor.createAction(ACTION_REPLACE_BUILDING_WALLS);
	for (const auto &entry : tiles) {
		action->addChange(newd Change(entry.second));
	}
	editor.addAction(action);
	editor.updateActions();
	return true;
}

wxString getOpeningName(::DoorType type, bool plural) {
	wxString name;
	switch (type) {
		case WALL_ARCHWAY:
			name = "archway";
			break;
		case WALL_DOOR_NORMAL:
			name = "door";
			break;
		case WALL_DOOR_LOCKED:
			name = "locked door";
			break;
		case WALL_DOOR_QUEST:
			name = "quest door";
			break;
		case WALL_DOOR_MAGIC:
			name = "magic door";
			break;
		case WALL_WINDOW:
			name = "window";
			break;
		case WALL_HATCH_WINDOW:
			name = "hatch window";
			break;
		default:
			name = "opening";
			break;
	}
	return plural ? name + "s" : name;
}
