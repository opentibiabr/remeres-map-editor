//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
////////////////////////////////////////////////////////////////////

#include "main.h"

#include <cctype>
#include <queue>

#include "change_city_style.h"

#include "action.h"
#include "basemap.h"
#include "brush.h"
#include "change_build_style.h"
#include "change_connected_ground_style.h"
#include "complexitem.h"
#include "editor.h"
#include "house.h"
#include "item.h"
#include "map.h"
#include "materials.h"
#include "tile.h"
#include "town.h"
#include "wall_brush.h"

namespace {
const Position cityStyleCardinalDirections[] = {
	Position(0, -1, 0),
	Position(-1, 0, 0),
	Position(1, 0, 0),
	Position(0, 1, 0),
};

const Position surroundingDirections[] = {
	Position(-1, -1, 0),
	Position(0, -1, 0),
	Position(1, -1, 0),
	Position(-1, 0, 0),
	Position(1, 0, 0),
	Position(-1, 1, 0),
	Position(0, 1, 0),
	Position(1, 1, 0),
};

constexpr int CityEnvelopePadding = 36;
constexpr int UrbanSeedRadius = 3;
constexpr int UrbanWallTouchRadius = 2;
constexpr int TempleWallRadius = 18;
constexpr size_t MaxUrbanSurfaceTiles = 180000;

std::string lowercaseCityStyleText(std::string text) {
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return text;
}

bool cityStyleHasWallCore(WallBrush* brush) {
	return brush &&
		brush->getWallItemID(WALL_HORIZONTAL) != 0 &&
		brush->getWallItemID(WALL_VERTICAL) != 0 &&
		brush->getWallItemID(WALL_NORTHWEST_DIAGONAL) != 0 &&
		brush->getWallItemID(WALL_POLE) != 0;
}

bool cityStyleExcludedBuildStyleName(const std::string &loweredName) {
	static const std::vector<std::string> blocked = {
		"ant trail",
		"blood pipe",
		"buoy line",
		"cracks",
		"fishing net",
		"floor ornament",
		"lava pipe",
		"lava stream",
		"railway",
		"small stream",
		"store counter",
		"venorean store counter",
	};
	return std::any_of(blocked.begin(), blocked.end(), [&loweredName](const std::string &fragment) {
		return loweredName.find(fragment) != std::string::npos;
	});
}

bool isSelectableBuildingWall(WallBrush* brush) {
	if (!brush || brush->isWallDecoration() || brush->getAnyWallItemID() == 0) {
		return false;
	}
	const std::string loweredName = lowercaseCityStyleText(brush->getName());
	return !cityStyleExcludedBuildStyleName(loweredName) && g_materials.isInTileset(brush, "Walls") && cityStyleHasWallCore(brush);
}

wxString cityStyleDoorTypeName(::DoorType type) {
	switch (type) {
		case WALL_ARCHWAY:
			return "archway";
		case WALL_DOOR_NORMAL:
			return "door";
		case WALL_DOOR_LOCKED:
			return "locked door";
		case WALL_DOOR_QUEST:
			return "quest door";
		case WALL_DOOR_MAGIC:
			return "magic door";
		case WALL_WINDOW:
			return "window";
		case WALL_HATCH_WINDOW:
			return "hatch window";
		default:
			return "opening";
	}
}

int chebyshevDistance(const Position &left, const Position &right) {
	return std::max(std::abs(left.x - right.x), std::abs(left.y - right.y));
}

bool validTemplePosition(const Position &position) {
	return position.isValid() && position.x > 0 && position.y > 0;
}

bool hasUrbanGround(Tile* tile) {
	return tile && UrbanGroundStyleCatalog::isUrbanGround(tile->getGroundBrush());
}

Item* cityStyleFindItemById(Tile* tile, uint16_t itemId) {
	if (!tile) {
		return nullptr;
	}
	for (Item* item : tile->items) {
		if (item && item->getID() == itemId) {
			return item;
		}
	}
	return nullptr;
}

bool hasNearbyPosition(const std::set<Position> &positions, const Position &center, int radius) {
	for (int dx = -radius; dx <= radius; ++dx) {
		for (int dy = -radius; dy <= radius; ++dy) {
			if (positions.count(Position(center.x + dx, center.y + dy, center.z)) != 0) {
				return true;
			}
		}
	}
	return false;
}
}

void ChangeCityStyleService::Bounds::include(const Position &position) {
	if (!valid) {
		minX = maxX = position.x;
		minY = maxY = position.y;
		valid = true;
		return;
	}
	minX = std::min(minX, position.x);
	maxX = std::max(maxX, position.x);
	minY = std::min(minY, position.y);
	maxY = std::max(maxY, position.y);
}

void ChangeCityStyleService::Bounds::expand(int amount) {
	if (!valid) {
		return;
	}
	minX -= amount;
	maxX += amount;
	minY -= amount;
	maxY += amount;
}

bool ChangeCityStyleService::Bounds::contains(const Position &position) const {
	return valid && position.x >= minX && position.x <= maxX && position.y >= minY && position.y <= maxY;
}

ChangeCityStyleService::ChangeCityStyleService(Editor &editor) :
	editor(editor) {
}

ChangeCityStyleService::PositionSet ChangeCityStyleService::collectTownHouseTiles(uint32_t townId, Bounds &bounds, size_t &houseCount) const {
	PositionSet result;
	houseCount = 0;
	Map &map = editor.getMap();
	for (const auto &entry : map.houses) {
		House* house = entry.second;
		if (!house || house->townid != townId) {
			continue;
		}
		++houseCount;
		for (const Position &position : house->getTiles()) {
			if (!position.isValid()) {
				continue;
			}
			result.insert(position);
			bounds.include(position);
		}
		const Position &exit = house->getExit();
		if (exit.isValid()) {
			bounds.include(exit);
		}
	}
	return result;
}

ChangeCityStyleService::PositionSet ChangeCityStyleService::collectUrbanSurface(const Bounds &bounds, const PositionSet &houseTiles, const Position &temple) const {
	PositionSet seeds;
	Map &map = editor.getMap();
	for (const Position &houseTile : houseTiles) {
		for (int dx = -UrbanSeedRadius; dx <= UrbanSeedRadius; ++dx) {
			for (int dy = -UrbanSeedRadius; dy <= UrbanSeedRadius; ++dy) {
				const Position candidate(houseTile.x + dx, houseTile.y + dy, houseTile.z);
				if (bounds.contains(candidate) && hasUrbanGround(map.getTile(candidate))) {
					seeds.insert(candidate);
				}
			}
		}
	}
	if (validTemplePosition(temple)) {
		for (int dx = -UrbanSeedRadius; dx <= UrbanSeedRadius; ++dx) {
			for (int dy = -UrbanSeedRadius; dy <= UrbanSeedRadius; ++dy) {
				const Position candidate(temple.x + dx, temple.y + dy, temple.z);
				if (bounds.contains(candidate) && hasUrbanGround(map.getTile(candidate))) {
					seeds.insert(candidate);
				}
			}
		}
	}

	PositionSet result;
	std::queue<Position> pending;
	for (const Position &seed : seeds) {
		pending.push(seed);
	}
	while (!pending.empty() && result.size() < MaxUrbanSurfaceTiles) {
		const Position position = pending.front();
		pending.pop();
		if (!position.isValid() || !bounds.contains(position) || !result.insert(position).second) {
			continue;
		}
		if (!hasUrbanGround(map.getTile(position))) {
			continue;
		}
		for (const Position &direction : cityStyleCardinalDirections) {
			pending.push(position + direction);
		}
	}
	return result;
}

PositionVector ChangeCityStyleService::findWallComponent(const Position &seed, WallBrush* sourceBrush, const Bounds &bounds, std::set<Position> &visited) const {
	PositionVector result;
	std::queue<Position> pending;
	pending.push(seed);
	while (!pending.empty()) {
		const Position position = pending.front();
		pending.pop();
		if (!position.isValid() || !bounds.contains(position) || !visited.insert(position).second) {
			continue;
		}
		Item* wall = ChangeBuildStyleService::getStructuralWall(editor.getMap().getTile(position), sourceBrush);
		if (!wall || !isSelectableBuildingWall(wall->getWallBrush())) {
			continue;
		}
		result.push_back(position);
		for (const Position &direction : cityStyleCardinalDirections) {
			pending.push(position + direction);
		}
	}
	return result;
}

bool ChangeCityStyleService::wallComponentTouchesOtherTown(uint32_t townId, const PositionVector &component) const {
	const Map &map = editor.getMap();
	for (const Position &position : component) {
		const Tile* tile = map.getTile(position);
		if (!tile || tile->getHouseID() == 0) {
			continue;
		}
		const House* house = map.houses.getHouse(tile->getHouseID());
		if (house && house->townid != 0 && house->townid != townId) {
			return true;
		}
	}
	return false;
}

bool ChangeCityStyleService::wallComponentBelongsToCity(const PositionVector &component, const PositionSet &houseTiles, const PositionSet &urbanSurface, const Position &temple) const {
	for (const Position &position : component) {
		if (houseTiles.count(position) != 0 || hasNearbyPosition(houseTiles, position, 1)) {
			return true;
		}
		if (hasNearbyPosition(urbanSurface, position, UrbanWallTouchRadius)) {
			return true;
		}
		if (validTemplePosition(temple) && position.z == temple.z && chebyshevDistance(position, temple) <= TempleWallRadius) {
			return true;
		}
	}
	return false;
}

void ChangeCityStyleService::collectWallPieces(uint32_t townId, const Bounds &bounds, const PositionSet &houseTiles, const PositionSet &urbanSurface, const Position &temple) {
	pieces.clear();
	previewContext.clear();

	Map &map = editor.getMap();
	std::set<Position> visited;
	for (MapIterator iterator = map.begin(), end = map.end(); iterator != end; ++iterator) {
		Tile* tile = (*iterator)->get();
		if (!tile) {
			continue;
		}
		const Position position = tile->getPosition();
		if (!bounds.contains(position) || visited.count(position) != 0) {
			continue;
		}
		Item* wall = ChangeBuildStyleService::getStructuralWall(tile);
		WallBrush* sourceBrush = wall ? wall->getWallBrush() : nullptr;
		if (!isSelectableBuildingWall(sourceBrush)) {
			continue;
		}
		PositionVector component = findWallComponent(position, sourceBrush, bounds, visited);
		if (component.empty() || wallComponentTouchesOtherTown(townId, component) || !wallComponentBelongsToCity(component, houseTiles, urbanSurface, temple)) {
			continue;
		}

		for (const Position &componentPosition : component) {
			Tile* componentTile = map.getTile(componentPosition);
			Item* componentWall = ChangeBuildStyleService::getStructuralWall(componentTile);
			if (!componentWall || !componentWall->getWallBrush()) {
				continue;
			}
			WallPiece piece;
			piece.position = componentPosition;
			piece.originalItemId = componentWall->getID();
			piece.alignment = componentWall->getWallAlignment();
			piece.sourceBrush = componentWall->getWallBrush();
			piece.opening = componentWall->isBrushDoor();
			piece.open = componentWall->isOpen();
			if (piece.opening) {
				piece.doorType = piece.sourceBrush->getDoorTypeFromID(piece.originalItemId);
				if (Door* door = componentWall->getDoor()) {
					piece.doorId = door->getDoorID();
				}
			}
			pieces.push_back(piece);
			analysis.floors.insert(componentPosition.z);
			analysis.sourceStyles.insert(wxstr(piece.sourceBrush->getName()));
			previewContext.insert(componentPosition);
			for (const Position &offset : surroundingDirections) {
				const Position contextPosition = componentPosition + offset;
				if (contextPosition.isValid() && bounds.contains(contextPosition)) {
					previewContext.insert(contextPosition);
				}
			}
		}
	}

	for (const Position &position : urbanSurface) {
		previewContext.insert(position);
	}
	for (const Position &position : houseTiles) {
		previewContext.insert(position);
	}
}

ChangeCityStyleAnalysis ChangeCityStyleService::analyze(uint32_t townId) {
	analysis = ChangeCityStyleAnalysis();
	pieces.clear();
	previewContext.clear();
	analysis.townId = townId;

	Map &map = editor.getMap();
	Town* town = map.towns.getTown(townId);
	if (!town) {
		analysis.reason = "Select a valid town.";
		return analysis;
	}
	analysis.townName = wxstr(town->getName());

	Bounds bounds;
	size_t houseCount = 0;
	PositionSet houseTiles = collectTownHouseTiles(townId, bounds, houseCount);
	analysis.houseCount = houseCount;
	analysis.houseTileCount = houseTiles.size();
	const Position temple = town->getTemplePosition();
	if (validTemplePosition(temple)) {
		bounds.include(temple);
	}
	if (houseTiles.empty()) {
		analysis.reason = "This town has no house tiles to anchor the city boundary.";
		return analysis;
	}

	bounds.expand(CityEnvelopePadding);
	PositionSet urbanSurface = collectUrbanSurface(bounds, houseTiles, temple);
	analysis.urbanTileCount = urbanSurface.size();
	collectWallPieces(townId, bounds, houseTiles, urbanSurface, temple);
	analysis.wallCount = pieces.size();
	analysis.previewPositions.assign(previewContext.begin(), previewContext.end());

	if (pieces.empty()) {
		analysis.reason = "No structural city walls were detected for this town.";
		return analysis;
	}

	long long sumX = 0;
	long long sumY = 0;
	int centerZ = temple.isValid() ? temple.z : pieces.front().position.z;
	for (const WallPiece &piece : pieces) {
		sumX += piece.position.x;
		sumY += piece.position.y;
	}
	analysis.center = Position(static_cast<int>(sumX / static_cast<long long>(pieces.size())), static_cast<int>(sumY / static_cast<long long>(pieces.size())), centerZ);
	analysis.valid = true;
	return analysis;
}

bool ChangeCityStyleService::simulate(
	WallBrush* target,
	BaseMap &working,
	std::set<Position> &changed,
	wxString &reason,
	std::vector<ChangeCityStyleConflict>* conflicts,
	ChangeCityStyleConflictAction conflictAction
) const {
	if (conflicts) {
		conflicts->clear();
	}
	if (!analysis.valid || pieces.empty()) {
		reason = analysis.reason.empty() ? wxString("Analyze a town before selecting a style.") : analysis.reason;
		return false;
	}
	if (!target || !isSelectableBuildingWall(target)) {
		reason = "Select a complete building wall style.";
		return false;
	}

	working.clear();
	changed.clear();
	for (const Position &position : previewContext) {
		Tile* sourceTile = editor.getMap().getTile(position);
		if (sourceTile) {
			working.setTile(position, sourceTile->deepCopy(working));
		}
	}

	for (const WallPiece &piece : pieces) {
		Tile* tile = working.getTile(piece.position);
		Item* wall = cityStyleFindItemById(tile, piece.originalItemId);
		if (!wall) {
			reason = "A detected city wall changed before conversion.";
			return false;
		}

		uint16_t replacementId = 0;
		bool canReplaceWithWall = false;
		if (piece.opening && piece.doorType != WALL_UNDEFINED) {
			replacementId = target->getDoorItemID(piece.alignment, piece.doorType, piece.open);
			canReplaceWithWall = target->getWallItemID(piece.alignment) != 0;
		} else {
			replacementId = target->getWallItemID(piece.alignment);
		}

		if (replacementId == 0) {
			if (conflicts) {
				conflicts->push_back({
					piece.position,
					wxString::Format(
						"No matching %s at %d, %d, %d.",
						piece.opening ? cityStyleDoorTypeName(piece.doorType).c_str() : "wall shape",
						piece.position.x,
						piece.position.y,
						piece.position.z
					),
					canReplaceWithWall
				});
			}
			if (piece.opening && conflictAction == ChangeCityStyleConflictAction::ReplaceOpeningWithWall && canReplaceWithWall) {
				replacementId = target->getWallItemID(piece.alignment);
			} else {
				continue;
			}
		}

		Item* converted = transformItem(wall, replacementId, tile);
		if (piece.doorId != 0 && converted && converted->getDoor()) {
			converted->getDoor()->setDoorID(piece.doorId);
		}
		changed.insert(piece.position);
	}

	return true;
}

ChangeCityStyleCompatibility ChangeCityStyleService::checkCompatibility(WallBrush* target) const {
	std::vector<ChangeCityStyleConflict> conflicts;
	if (!analysis.valid || pieces.empty()) {
		return { false, analysis.reason.empty() ? wxString("Analyze a town before selecting a style.") : analysis.reason, conflicts };
	}
	if (!target || !isSelectableBuildingWall(target)) {
		return { false, wxString("Select a complete building wall style."), conflicts };
	}

	for (const WallPiece &piece : pieces) {
		uint16_t replacementId = 0;
		bool canReplaceWithWall = false;
		if (piece.opening && piece.doorType != WALL_UNDEFINED) {
			replacementId = target->getDoorItemID(piece.alignment, piece.doorType, piece.open);
			canReplaceWithWall = target->getWallItemID(piece.alignment) != 0;
		} else {
			replacementId = target->getWallItemID(piece.alignment);
		}
		if (replacementId == 0) {
			conflicts.push_back({
				piece.position,
				wxString::Format(
					"No matching %s at %d, %d, %d.",
					piece.opening ? cityStyleDoorTypeName(piece.doorType).c_str() : "wall shape",
					piece.position.x,
					piece.position.y,
					piece.position.z
				),
				canReplaceWithWall
			});
		}
	}
	return { true, wxEmptyString, conflicts };
}

bool ChangeCityStyleService::buildPreview(WallBrush* target, BaseMap &previewTiles, wxString &reason, std::vector<ChangeCityStyleConflict>* conflicts) const {
	std::set<Position> changed;
	return simulate(target, previewTiles, changed, reason, conflicts, ChangeCityStyleConflictAction::KeepExisting);
}

bool ChangeCityStyleService::apply(WallBrush* target, ChangeCityStyleConflictAction conflictAction, wxString &reason) {
	BaseMap working;
	std::set<Position> changed;
	if (!simulate(target, working, changed, reason, nullptr, conflictAction)) {
		return false;
	}

	Action* action = editor.createAction(ACTION_CHANGE_CITY_STYLE);
	for (const Position &position : changed) {
		Tile* tile = working.getTile(position);
		if (tile) {
			action->addChange(newd Change(tile->deepCopy(editor.getMap())));
		}
	}
	if (action->empty()) {
		delete action;
		reason = "No city walls were changed.";
		return false;
	}

	editor.addAction(action);
	editor.updateActions();
	return true;
}

std::vector<WallBrush*> ChangeCityStyleService::availableTargetStyles() {
	std::vector<WallBrush*> result;
	std::set<std::string> uniqueNames;
	for (const auto &brushEntry : g_brushes.getMap()) {
		Brush* brush = brushEntry.second;
		if (!brush || !brush->isWall() || brush->isWallDecoration()) {
			continue;
		}
		WallBrush* wall = brush->asWall();
		const std::string loweredName = lowercaseCityStyleText(wall->getName());
		if (isSelectableBuildingWall(wall) && uniqueNames.insert(loweredName).second) {
			result.push_back(wall);
		}
	}
	std::sort(result.begin(), result.end(), [](WallBrush* left, WallBrush* right) {
		return lowercaseCityStyleText(left->getName()) < lowercaseCityStyleText(right->getName());
	});
	return result;
}
