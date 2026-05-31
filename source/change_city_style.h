//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
////////////////////////////////////////////////////////////////////

#ifndef RME_CHANGE_CITY_STYLE_H_
#define RME_CHANGE_CITY_STYLE_H_

#include <set>
#include <vector>

#include "brush_enums.h"
#include "position.h"

class BaseMap;
class Editor;
class WallBrush;

enum class ChangeCityStyleConflictAction {
	KeepExisting,
	ReplaceOpeningWithWall,
};

struct ChangeCityStyleConflict {
	Position position;
	wxString message;
	bool canReplaceWithWall = false;
};

struct ChangeCityStyleCompatibility {
	bool compatible = false;
	wxString reason;
	std::vector<ChangeCityStyleConflict> conflicts;

	bool fullMatch() const noexcept {
		return compatible && conflicts.empty();
	}
};

struct ChangeCityStyleAnalysis {
	uint32_t townId = 0;
	wxString townName;
	size_t houseCount = 0;
	size_t houseTileCount = 0;
	size_t urbanTileCount = 0;
	size_t wallCount = 0;
	std::set<int> floors;
	std::set<wxString> sourceStyles;
	Position center;
	PositionVector previewPositions;
	bool valid = false;
	wxString reason;
};

class ChangeCityStyleService {
public:
	explicit ChangeCityStyleService(Editor &editor);

	ChangeCityStyleAnalysis analyze(uint32_t townId);
	const ChangeCityStyleAnalysis &getAnalysis() const noexcept {
		return analysis;
	}

	ChangeCityStyleCompatibility checkCompatibility(WallBrush* target) const;
	bool buildPreview(WallBrush* target, BaseMap &previewTiles, wxString &reason, std::vector<ChangeCityStyleConflict>* conflicts = nullptr) const;
	bool apply(WallBrush* target, ChangeCityStyleConflictAction conflictAction, wxString &reason);

	static std::vector<WallBrush*> availableTargetStyles();

private:
	struct WallPiece {
		Position position;
		uint16_t originalItemId = 0;
		::BorderType alignment = WALL_HORIZONTAL;
		WallBrush* sourceBrush = nullptr;
		bool opening = false;
		::DoorType doorType = WALL_UNDEFINED;
		bool open = false;
		uint8_t doorId = 0;
	};

	struct Bounds {
		int minX = 0;
		int maxX = 0;
		int minY = 0;
		int maxY = 0;
		bool valid = false;

		void include(const Position &position);
		void expand(int amount);
		bool contains(const Position &position) const;
	};

	using PositionSet = std::set<Position>;

	PositionSet collectTownHouseTiles(uint32_t townId, Bounds &bounds, size_t &houseCount) const;
	PositionSet collectUrbanSurface(const Bounds &bounds, const PositionSet &houseTiles, const Position &temple) const;
	void collectWallPieces(uint32_t townId, const Bounds &bounds, const PositionSet &houseTiles, const PositionSet &urbanSurface, const Position &temple);
	PositionVector findWallComponent(const Position &seed, WallBrush* sourceBrush, const Bounds &bounds, std::set<Position> &visited) const;
	bool wallComponentTouchesOtherTown(uint32_t townId, const PositionVector &component) const;
	bool wallComponentBelongsToCity(const PositionVector &component, const PositionSet &houseTiles, const PositionSet &urbanSurface, const Position &temple) const;
	bool simulate(WallBrush* target, BaseMap &working, std::set<Position> &changed, wxString &reason, std::vector<ChangeCityStyleConflict>* conflicts, ChangeCityStyleConflictAction conflictAction) const;

	Editor &editor;
	ChangeCityStyleAnalysis analysis;
	std::vector<WallPiece> pieces;
	std::set<Position> previewContext;
};

#endif
