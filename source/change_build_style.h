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

struct ChangeBuildStyleConflict {
	Position position;
	wxString message;
};

struct ChangeBuildStyleCompatibility {
	bool compatible;
	wxString reason;
	std::vector<ChangeBuildStyleConflict> conflicts;

	bool fullMatch() const noexcept {
		return compatible && conflicts.empty();
	}
};

enum class ChangeBuildStyleMissingOpeningAction {
	KeepExisting,
	ReplaceWithWall,
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
	bool buildPreview(WallBrush* target, const std::set<int> &selectedFloors, BaseMap &previewTiles, wxString &reason, std::vector<ChangeBuildStyleConflict>* conflicts = nullptr) const;
	bool apply(WallBrush* target, const std::set<int> &selectedFloors, ChangeBuildStyleMissingOpeningAction missingOpeningAction, wxString &reason);

	static Item* getStructuralWall(Tile* tile, WallBrush* required = nullptr);
	static const Item* getStructuralWall(const Tile* tile, const WallBrush* required = nullptr);

private:
	struct Opening {
		Position position;
		::DoorType type;
		bool open;
		uint8_t doorId;
		uint16_t originalItemId;
		::BorderType alignment;
		bool loose;
	};

	PositionVector findComponent(const Position &seed) const;
	void detectFloors();
	std::set<Position> selectedPositions(const std::set<int> &selectedFloors) const;
	WallBrush* findLooseArchwaySourceBrush(const Position &position, ::BorderType* alignment) const;
	Item* getLooseArchway(Tile* tile) const;
	const Item* getLooseArchway(const Tile* tile) const;
	bool getLooseArchwayAlignment(const Position &position, ::BorderType &alignment) const;
	bool isSourceBuildPiece(const Position &position) const;
	::BorderType inferOpeningAlignment(const Position &position) const;
	bool simulate(
		WallBrush* target,
		const std::set<int> &selectedFloors,
		BaseMap &working,
		std::set<Position> &changed,
		wxString &reason,
		std::vector<ChangeBuildStyleConflict>* conflicts,
		ChangeBuildStyleMissingOpeningAction missingOpeningAction
	) const;

	Editor &editor;
	Position origin;
	WallBrush* sourceBrush;
	std::vector<ChangeBuildStyleFloor> floors;
};

#endif
