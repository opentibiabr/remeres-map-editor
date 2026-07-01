//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//////////////////////////////////////////////////////////////////////

#ifndef RME_CHANGE_CONNECTED_GROUND_STYLE_H_
#define RME_CHANGE_CONNECTED_GROUND_STYLE_H_

#include "main.h"
#include "position.h"

class BaseMap;
class Editor;
class GroundBrush;

struct ChangeConnectedGroundAdjacency {
	GroundBrush* brush = nullptr;
	size_t tileCount = 0;
};

class UrbanGroundStyleCatalog {
public:
	static bool isUrbanGround(const GroundBrush* brush);
	static wxString familyLabel(const GroundBrush* brush);
	static std::vector<GroundBrush*> styles(const GroundBrush* source);
};

class ChangeConnectedGroundStyleService {
public:
	ChangeConnectedGroundStyleService(Editor &editor, const Position &origin);

	bool isValid() const noexcept {
		return sourceBrush != nullptr && !positions.empty();
	}
	GroundBrush* getSourceBrush() const noexcept {
		return sourceBrush;
	}
	const Position &getOrigin() const noexcept {
		return origin;
	}
	const PositionVector &getPositions() const noexcept {
		return positions;
	}
	const std::vector<ChangeConnectedGroundAdjacency> &getAdjacentUrbanBrushes() const noexcept {
		return adjacentUrbanBrushes;
	}

	bool buildPreview(GroundBrush* target, BaseMap &previewTiles, wxString &reason) const;
	bool applyPreview(const BaseMap &previewTiles, wxString &reason);

private:
	PositionVector findComponent() const;
	void detectAdjacentUrbanBrushes();
	bool simulate(GroundBrush* target, BaseMap &working, wxString &reason) const;

	Editor &editor;
	Position origin;
	GroundBrush* sourceBrush;
	PositionVector positions;
	std::vector<ChangeConnectedGroundAdjacency> adjacentUrbanBrushes;
};

#endif
