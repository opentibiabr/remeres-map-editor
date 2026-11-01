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

#ifndef RME_BUILDING_WALLS_H
#define RME_BUILDING_WALLS_H

#include "brush_enums.h"
#include "position.h"

class BaseMap;
class Editor;
class Map;
class Tile;
class WallBrush;

// How far the search for the walls of a building goes
enum class BuildingScope {
	Room,
	House,
	Building,
	ConnectedWalls,
};

// What to do with an opening that the new wall style does not have
enum class OpeningAction {
	KeepCurrent,
	UseClosest,
	ReplaceWithWall,
};

struct BuildingWall {
	Position position;
	WallBrush* brush = nullptr;
	::BorderType alignment = WALL_POLE;
	uint16_t itemId = 0;
	::DoorType opening = WALL_UNDEFINED;
	bool open = false;
	// Also closes a room that is not part of the building
	bool shared = false;
	// A railing or anything else that stops a walker but not the view
	bool low = false;
};

struct BuildingFloor {
	int z = 0;
	std::vector<BuildingWall> walls;
};

struct BuildingScan {
	std::vector<BuildingFloor> floors;
	// The walls and the rooms, which is what the preview keeps lit
	std::set<Position> tiles;
	WallBrush* originBrush = nullptr;
	// False when the walls close no room and the scan fell back to the connected walls
	bool enclosed = false;
	// The building has no closed room here, so its roof shows where it ends
	bool openToStreet = false;
	// The house around the wall the scan started from, when it has one
	uint32_t house = 0;
	std::string houseName;
	// Rooms found on each side of the wall the scan started from
	int sideCount = 0;
	int side = 0;
	// Walls of the building that no wall brush knows, these can not be replaced
	size_t unknownWalls = 0;

	bool isValid() const noexcept {
		return originBrush != nullptr && !floors.empty();
	}
};

// A wall brush that can be offered as a new style
struct WallStyle {
	WallBrush* brush = nullptr;
	uint16_t cornerItem = 0;
	uint16_t runItem = 0;
};

struct WallReplacementRequest {
	WallBrush* target = nullptr;
	std::set<int> floors;
	std::set<WallBrush*> sources;
	std::set<Position> excluded;
	bool includeShared = true;
	bool replaceRailings = true;
	std::map<::DoorType, OpeningAction> openingActions;
};

// All the openings of one type in a replacement
struct OpeningGroup {
	::DoorType type = WALL_UNDEFINED;
	size_t count = 0;
	// The new item, or the current one when the style has no equivalent
	uint16_t sampleItem = 0;
	bool missing = false;
	::DoorType closestType = WALL_UNDEFINED;
};

struct WallChange {
	Position position;
	uint16_t fromItem = 0;
	uint16_t toItem = 0;
};

struct WallReplacementPlan {
	std::vector<WallChange> changes;
	std::vector<OpeningGroup> openings;
	// Why the style can not be used, the plan has no changes when this is set
	wxString problem;
	size_t wallCount = 0;
	size_t doorCount = 0;
	size_t windowCount = 0;
	size_t floorCount = 0;
	size_t missingOpenings = 0;
	// The railing of the new style, railings stay as they are when it has none
	const WallBrush* railing = nullptr;
	size_t railingCount = 0;
	size_t railingsChanged = 0;
	uint16_t railingSample = 0;

	bool isUsable() const noexcept {
		return problem.empty();
	}
	bool isComplete() const noexcept {
		return isUsable() && missingOpenings == 0;
	}
};

// A low style replaces low walls only, a full one replaces full walls only
bool isLowWallStyle(const WallBrush* brush);

// If a wall on this tile can start a scan
bool hasBuildingWall(const Tile* tile);

// A negative side lets the scan choose the most likely room around the wall
BuildingScan scanBuilding(const Map &map, const Position &origin, BuildingScope scope, int side = -1);
// Adds a wall the scan left out, returns false when the position has none
bool addWallToScan(BuildingScan &scan, const Map &map, const Position &position);

// The wall brushes that can replace walls like the ones of this brush
std::vector<WallStyle> collectWallStyles(const WallBrush* source);

WallReplacementPlan planWallReplacement(const BuildingScan &scan, const WallReplacementRequest &request);
void writeWallPreview(const Map &map, const WallReplacementPlan &plan, BaseMap &preview);
bool commitWallReplacement(Editor &editor, const WallReplacementPlan &plan);

wxString getOpeningName(::DoorType type, bool plural);

#endif
