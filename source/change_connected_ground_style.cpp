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

#include "change_connected_ground_style.h"

#include "action.h"
#include "basemap.h"
#include "brush.h"
#include "editor.h"
#include "ground_brush.h"
#include "tile.h"

namespace {
const Position connectedGroundCardinalDirections[] = {
	Position(0, -1, 0),
	Position(-1, 0, 0),
	Position(1, 0, 0),
	Position(0, 1, 0),
};

const Position connectedGroundSurroundingDirections[] = {
	Position(-1, -1, 0),
	Position(0, -1, 0),
	Position(1, -1, 0),
	Position(-1, 0, 0),
	Position(1, 0, 0),
	Position(-1, 1, 0),
	Position(0, 1, 0),
	Position(1, 1, 0),
};

const std::set<std::string> urbanGroundNames = {
	"cobblestone",
	"dark cobblestone",
	"ugly cobblestone",
	"grassy cobblestone",
	"yellow pavement",
	"dark pavement",
	"venore cobblestone",
	"venore plaster",
	"terracotta",
	"roshamuul pavement",
	"oramond pavement",
	"oramond other pavement",
	"new grass pavement",
	"new ornamented pavement",
	"sandstone",
};

std::string connectedGroundLowercase(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return value;
}

std::set<Position> expandPositions(const std::set<Position> &positions) {
	std::set<Position> expanded = positions;
	for (const Position &position : positions) {
		for (const Position &offset : connectedGroundSurroundingDirections) {
			const Position adjacent = position + offset;
			if (adjacent.isValid()) {
				expanded.insert(adjacent);
			}
		}
	}
	return expanded;
}

std::set<Position> positionSet(const PositionVector &positions) {
	return { positions.begin(), positions.end() };
}
}

bool UrbanGroundStyleCatalog::isUrbanGround(const GroundBrush* brush) {
	return brush && urbanGroundNames.contains(connectedGroundLowercase(brush->getName()));
}

wxString UrbanGroundStyleCatalog::familyLabel(const GroundBrush* brush) {
	if (!brush) {
		return "Unknown";
	}
	const std::string name = connectedGroundLowercase(brush->getName());
	if (name.find("venore") != std::string::npos || name == "terracotta") {
		return "Venore";
	}
	if (name == "sandstone") {
		return "Desert stone";
	}
	if (name.find("oramond") != std::string::npos) {
		return "Oramond";
	}
	if (name.find("roshamuul") != std::string::npos) {
		return "Roshamuul";
	}
	if (name.find("ornamented") != std::string::npos || name.find("grass pavement") != std::string::npos) {
		return "Ornamented";
	}
	return "Pavement";
}

std::vector<GroundBrush*> UrbanGroundStyleCatalog::styles(const GroundBrush* source) {
	std::vector<GroundBrush*> result;
	std::set<GroundBrush*> unique;
	for (const auto &entry : g_brushes.getMap()) {
		Brush* brush = entry.second;
		if (!brush || !brush->isGround()) {
			continue;
		}
		GroundBrush* ground = brush->asGround();
		if (ground != source && ground->visibleInPalette() && isUrbanGround(ground) && unique.insert(ground).second) {
			result.push_back(ground);
		}
	}
	std::sort(result.begin(), result.end(), [](const GroundBrush* left, const GroundBrush* right) {
		return connectedGroundLowercase(left->getName()) < connectedGroundLowercase(right->getName());
	});
	return result;
}

ChangeConnectedGroundStyleService::ChangeConnectedGroundStyleService(Editor &editor, const Position &origin) :
	editor(editor),
	origin(origin),
	sourceBrush(nullptr) {
	Tile* tile = editor.getMap().getTile(origin);
	sourceBrush = tile ? tile->getGroundBrush() : nullptr;
	if (!UrbanGroundStyleCatalog::isUrbanGround(sourceBrush)) {
		sourceBrush = nullptr;
		return;
	}

	positions = findComponent();
	if (positions.empty()) {
		sourceBrush = nullptr;
		return;
	}
	detectAdjacentUrbanBrushes();
}

PositionVector ChangeConnectedGroundStyleService::findComponent() const {
	PositionVector result;
	std::queue<Position> pending;
	std::set<Position> visited;
	pending.push(origin);

	while (!pending.empty()) {
		const Position position = pending.front();
		pending.pop();
		if (!position.isValid() || !visited.insert(position).second) {
			continue;
		}
		Tile* tile = editor.getMap().getTile(position);
		if (!tile || tile->getGroundBrush() != sourceBrush) {
			continue;
		}
		result.push_back(position);
		for (const Position &direction : connectedGroundCardinalDirections) {
			pending.push(position + direction);
		}
	}
	return result;
}

void ChangeConnectedGroundStyleService::detectAdjacentUrbanBrushes() {
	const std::set<Position> component = positionSet(positions);
	std::map<GroundBrush*, std::set<Position>> adjacentPositions;
	for (const Position &position : component) {
		for (const Position &offset : connectedGroundSurroundingDirections) {
			const Position adjacent = position + offset;
			if (!adjacent.isValid() || component.contains(adjacent)) {
				continue;
			}
			Tile* tile = editor.getMap().getTile(adjacent);
			GroundBrush* brush = tile ? tile->getGroundBrush() : nullptr;
			if (brush != sourceBrush && UrbanGroundStyleCatalog::isUrbanGround(brush)) {
				adjacentPositions[brush].insert(adjacent);
			}
		}
	}

	for (const auto &[brush, neighbouringPositions] : adjacentPositions) {
		adjacentUrbanBrushes.push_back({ brush, neighbouringPositions.size() });
	}
	std::sort(adjacentUrbanBrushes.begin(), adjacentUrbanBrushes.end(), [](const auto &left, const auto &right) {
		return left.brush->getName() < right.brush->getName();
	});
}

bool ChangeConnectedGroundStyleService::simulate(GroundBrush* target, BaseMap &working, wxString &reason) const {
	if (!isValid() || !target || target == sourceBrush || !UrbanGroundStyleCatalog::isUrbanGround(target)) {
		reason = "Select a different urban ground style.";
		return false;
	}

	const std::set<Position> component = positionSet(positions);
	const std::set<Position> context = expandPositions(component);
	for (const Position &position : context) {
		Tile* sourceTile = editor.getMap().getTile(position);
		if (sourceTile) {
			working.setTile(position, sourceTile->deepCopy(working));
		}
	}

	for (const Position &position : component) {
		Tile* tile = working.getTile(position);
		if (!tile || tile->getGroundBrush() != sourceBrush) {
			reason = "A selected ground tile no longer belongs to the source style.";
			return false;
		}
		target->draw(&working, tile, nullptr);
	}

	for (const Position &position : component) {
		Tile* tile = working.getTile(position);
		if (tile) {
			tile->borderize(&working);
		}
	}
	return true;
}

bool ChangeConnectedGroundStyleService::buildPreview(GroundBrush* target, BaseMap &previewTiles, wxString &reason) const {
	previewTiles.clear();
	return simulate(target, previewTiles, reason);
}

bool ChangeConnectedGroundStyleService::applyPreview(const BaseMap &previewTiles, wxString &reason) {
	if (!isValid()) {
		reason = "The selected urban ground component is no longer available.";
		return false;
	}

	Action* action = editor.createAction(ACTION_CHANGE_CONNECTED_GROUND_STYLE);
	for (const Position &position : positions) {
		const Tile* previewTile = previewTiles.getTile(position);
		if (!previewTile) {
			delete action;
			reason = "The preview is incomplete. Select a style again before applying.";
			return false;
		}
		Tile* replacement = previewTile->deepCopy(editor.getMap());
		replacement->setLocation(editor.getMap().createTileL(position));
		action->addChange(newd Change(replacement));
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
