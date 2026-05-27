//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include <queue>

#include "change_build_style.h"

#include "action.h"
#include "basemap.h"
#include "complexitem.h"
#include "editor.h"
#include "item.h"
#include "tile.h"
#include "wall_brush.h"

namespace {
const Position cardinalDirections[] = {
	Position(0, -1, 0),
	Position(-1, 0, 0),
	Position(1, 0, 0),
	Position(0, 1, 0),
};

wxString doorTypeName(::DoorType type) {
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

std::set<Position> positionsWithNeighbours(const std::set<Position> &positions) {
	std::set<Position> result = positions;
	for (const Position &position : positions) {
		for (const Position &direction : cardinalDirections) {
			const Position adjacent = position + direction;
			if (adjacent.isValid()) {
				result.insert(adjacent);
			}
		}
	}
	return result;
}
}

ChangeBuildStyleService::ChangeBuildStyleService(Editor &editor, const Position &origin) :
	editor(editor),
	origin(origin),
	sourceBrush(nullptr) {
	Tile* tile = editor.getMap().getTile(origin);
	Item* wall = getStructuralWall(tile);
	if (!wall) {
		return;
	}

	sourceBrush = wall->getWallBrush();
	const PositionVector component = findComponent(origin);
	if (component.empty()) {
		sourceBrush = nullptr;
		return;
	}

	floors.push_back({ origin.z, component });
	detectFloors();
	std::sort(floors.begin(), floors.end(), [](const ChangeBuildStyleFloor &left, const ChangeBuildStyleFloor &right) {
		return left.z < right.z;
	});
}

Item* ChangeBuildStyleService::getStructuralWall(Tile* tile, WallBrush* required) {
	if (!tile) {
		return nullptr;
	}
	for (Item* item : tile->items) {
		if (!item || !item->isWall()) {
			continue;
		}
		WallBrush* wallBrush = item->getWallBrush();
		if (wallBrush && !wallBrush->isWallDecoration() && (!required || wallBrush == required)) {
			return item;
		}
	}
	return nullptr;
}

const Item* ChangeBuildStyleService::getStructuralWall(const Tile* tile, const WallBrush* required) {
	if (!tile) {
		return nullptr;
	}
	for (const Item* item : tile->items) {
		if (!item || !item->isWall()) {
			continue;
		}
		WallBrush* wallBrush = item->getWallBrush();
		if (wallBrush && !wallBrush->isWallDecoration() && (!required || wallBrush == required)) {
			return item;
		}
	}
	return nullptr;
}

PositionVector ChangeBuildStyleService::findComponent(const Position &seed) const {
	PositionVector result;
	std::queue<Position> pending;
	std::set<Position> visited;
	pending.push(seed);

	while (!pending.empty()) {
		const Position position = pending.front();
		pending.pop();
		if (!position.isValid() || !visited.insert(position).second) {
			continue;
		}
		if (!getStructuralWall(editor.getMap().getTile(position), sourceBrush)) {
			continue;
		}

		result.push_back(position);
		for (const Position &direction : cardinalDirections) {
			pending.push(position + direction);
		}
	}
	return result;
}

void ChangeBuildStyleService::detectFloors() {
	for (int direction : { -1, 1 }) {
		PositionVector previous = floors.front().positions;
		int z = origin.z + direction;
		while (z >= rme::MapMinLayer && z <= rme::MapMaxLayer) {
			std::set<Position> projection;
			for (const Position &position : previous) {
				const Position projected(position.x, position.y, z);
				projection.insert(projected);
				for (const Position &offset : cardinalDirections) {
					projection.insert(projected + offset);
				}
			}

			std::set<Position> visited;
			PositionVector best;
			size_t bestOverlap = 0;
			for (const Position &seed : projection) {
				if (visited.count(seed) != 0 || !getStructuralWall(editor.getMap().getTile(seed), sourceBrush)) {
					continue;
				}
				const PositionVector component = findComponent(seed);
				size_t overlap = 0;
				for (const Position &position : component) {
					visited.insert(position);
					if (projection.count(position) != 0) {
						++overlap;
					}
				}
				if (overlap > bestOverlap) {
					bestOverlap = overlap;
					best = component;
				}
			}

			const size_t denominator = std::min(previous.size(), best.size());
			if (best.empty() || denominator == 0 || bestOverlap * 100 < denominator * 40) {
				break;
			}
			floors.push_back({ z, best });
			previous = best;
			z += direction;
		}
	}
}

std::set<Position> ChangeBuildStyleService::selectedPositions(const std::set<int> &selectedFloors) const {
	std::set<Position> positions;
	for (const ChangeBuildStyleFloor &floor : floors) {
		if (selectedFloors.count(floor.z) != 0) {
			positions.insert(floor.positions.begin(), floor.positions.end());
		}
	}
	return positions;
}

bool ChangeBuildStyleService::simulate(WallBrush* target, const std::set<int> &selectedFloors, BaseMap &working, std::set<Position> &changed, wxString &reason) const {
	if (!isValid() || !target || target->isWallDecoration() || target == sourceBrush) {
		reason = "Select a different structural wall style.";
		return false;
	}

	changed = selectedPositions(selectedFloors);
	if (changed.empty()) {
		reason = "Select at least one detected floor.";
		return false;
	}

	const uint16_t placeholderId = target->getAnyWallItemID();
	if (placeholderId == 0) {
		reason = "The selected style has no structural wall items.";
		return false;
	}

	std::vector<Opening> openings;
	for (const Position &position : changed) {
		Item* original = getStructuralWall(editor.getMap().getTile(position), sourceBrush);
		if (!original) {
			continue;
		}
		if (original->isBrushDoor()) {
			Opening opening = { position, sourceBrush->getDoorTypeFromID(original->getID()), original->isOpen(), 0 };
			if (Door* door = original->getDoor()) {
				opening.doorId = door->getDoorID();
			}
			if (opening.type == WALL_UNDEFINED) {
				reason = "An existing opening has no semantic wall-brush type.";
				return false;
			}
			openings.push_back(opening);
		}
	}

	const std::set<Position> copyPositions = positionsWithNeighbours(changed);
	for (const Position &position : copyPositions) {
		Tile* sourceTile = editor.getMap().getTile(position);
		if (sourceTile) {
			working.setTile(position, sourceTile->deepCopy(working));
		}
	}

	for (const Position &position : changed) {
		Tile* tile = working.getTile(position);
		Item* wall = getStructuralWall(tile, sourceBrush);
		if (!wall) {
			reason = "A selected structural wall could not be converted.";
			return false;
		}
		transformItem(wall, placeholderId, tile);
	}

	for (const Position &position : changed) {
		Tile* tile = working.getTile(position);
		if (tile) {
			tile->wallize(&working);
		}
	}

	for (const Opening &opening : openings) {
		Tile* tile = working.getTile(opening.position);
		Item* wall = getStructuralWall(tile);
		if (!wall) {
			reason = "An opening lost its supporting wall during conversion.";
			return false;
		}
		const uint16_t doorItemId = target->getDoorItemID(wall->getWallAlignment(), opening.type, opening.open);
		if (doorItemId == 0) {
			reason = wxString::Format("This style has no matching %s on floor %d.", doorTypeName(opening.type).c_str(), opening.position.z);
			return false;
		}
		Item* converted = transformItem(wall, doorItemId, tile);
		if (opening.doorId != 0 && converted && converted->getDoor()) {
			converted->getDoor()->setDoorID(opening.doorId);
		}
	}
	return true;
}

ChangeBuildStyleCompatibility ChangeBuildStyleService::checkCompatibility(WallBrush* target, const std::set<int> &selectedFloors) const {
	BaseMap working;
	std::set<Position> changed;
	wxString reason;
	const bool compatible = simulate(target, selectedFloors, working, changed, reason);
	return { compatible, reason };
}

bool ChangeBuildStyleService::buildPreview(WallBrush* target, const std::set<int> &selectedFloors, int displayFloor, std::vector<ChangeBuildStylePreviewItem> &items, wxString &reason) const {
	BaseMap working;
	std::set<Position> changed;
	items.clear();
	if (!simulate(target, selectedFloors, working, changed, reason)) {
		return false;
	}

	for (const Position &position : changed) {
		if (position.z != displayFloor) {
			continue;
		}
		const Item* wall = getStructuralWall(working.getTile(position));
		if (wall) {
			items.push_back({ position, wall->getID(), true });
		}
	}
	return true;
}

bool ChangeBuildStyleService::apply(WallBrush* target, const std::set<int> &selectedFloors, wxString &reason) {
	BaseMap working;
	std::set<Position> changed;
	if (!simulate(target, selectedFloors, working, changed, reason)) {
		return false;
	}

	Action* action = editor.createAction(ACTION_CHANGE_BUILD_STYLE);
	for (const Position &position : changed) {
		Tile* tile = working.getTile(position);
		if (tile) {
			action->addChange(newd Change(tile->deepCopy(editor.getMap())));
		}
	}
	if (action->empty()) {
		delete action;
		reason = "No tiles were changed.";
		return false;
	}

	editor.addAction(action);
	editor.updateActions();
	return true;
}
