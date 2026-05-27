//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//////////////////////////////////////////////////////////////////////

#ifndef RME_CHANGE_BUILD_STYLE_H_
#define RME_CHANGE_BUILD_STYLE_H_

#include "main.h"
#include "brush_enums.h"
#include "position.h"

class BaseMap;
class Editor;
class Item;
class Tile;
class WallBrush;

struct ChangeBuildStyleFloor {
	int z;
	PositionVector positions;
};

struct ChangeBuildStylePreviewItem {
	Position position;
	uint16_t itemId;
	bool affected;
};

struct ChangeBuildStyleCompatibility {
	bool compatible;
	wxString reason;
};

class ChangeBuildStyleService {
public:
	ChangeBuildStyleService(Editor &editor, const Position &origin);

	bool isValid() const noexcept {
		return sourceBrush != nullptr && !floors.empty();
	}
	WallBrush* getSourceBrush() const noexcept {
		return sourceBrush;
	}
	const Position &getOrigin() const noexcept {
		return origin;
	}
	const std::vector<ChangeBuildStyleFloor> &getFloors() const noexcept {
		return floors;
	}

	ChangeBuildStyleCompatibility checkCompatibility(WallBrush* target, const std::set<int> &selectedFloors) const;
	bool buildPreview(WallBrush* target, const std::set<int> &selectedFloors, int displayFloor, std::vector<ChangeBuildStylePreviewItem> &items, wxString &reason) const;
	bool apply(WallBrush* target, const std::set<int> &selectedFloors, wxString &reason);

	static Item* getStructuralWall(Tile* tile, WallBrush* required = nullptr);
	static const Item* getStructuralWall(const Tile* tile, const WallBrush* required = nullptr);

private:
	struct Opening {
		Position position;
		::DoorType type;
		bool open;
		uint8_t doorId;
	};

	PositionVector findComponent(const Position &seed) const;
	void detectFloors();
	std::set<Position> selectedPositions(const std::set<int> &selectedFloors) const;
	bool simulate(WallBrush* target, const std::set<int> &selectedFloors, BaseMap &working, std::set<Position> &changed, wxString &reason) const;

	Editor &editor;
	Position origin;
	WallBrush* sourceBrush;
	std::vector<ChangeBuildStyleFloor> floors;
};

#endif
