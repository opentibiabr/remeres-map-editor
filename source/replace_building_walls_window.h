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

#ifndef RME_REPLACE_BUILDING_WALLS_WINDOW_H
#define RME_REPLACE_BUILDING_WALLS_WINDOW_H

#include <functional>

#include "basemap.h"
#include "building_walls.h"
#include "map_display.h"

class Editor;
class wxCheckListBox;
class wxToggleButton;

struct WallStyleEntry {
	WallStyle style;
	wxString note;
};

class WallStyleList : public wxVListBox {
public:
	explicit WallStyleList(wxWindow* parent);

	void setEntries(std::vector<WallStyleEntry> entries);
	WallBrush* getSelectedBrush() const;

protected:
	void OnDrawItem(wxDC &dc, const wxRect &rect, size_t index) const override;
	wxCoord OnMeasureItem(size_t index) const override;

private:
	std::vector<WallStyleEntry> entries;
};

class BuildingPreviewCanvas : public MapCanvas {
public:
	BuildingPreviewCanvas(wxWindow* parent, Editor &editor, const Position &origin, std::function<void(const Position &)> onWallClick);

	BaseMap &getPreviewMap() noexcept {
		return previewMap;
	}
	void setComparing(bool comparing);
	void fitTiles(const std::set<Position> &tiles);
	void showFloor(int floor);

	bool AcceptsFocusFromKeyboard() const override {
		return false;
	}

private:
	void OnSize(wxSizeEvent &event);
	void OnPreviewDrag(wxMouseEvent &event);
	void OnPreviewZoom(wxMouseEvent &event);
	void OnMouseDown(wxMouseEvent &event);
	void OnLeftUp(wxMouseEvent &event);
	void OnMouseUp(wxMouseEvent &event);
	void OnCaptureLost(wxMouseCaptureLostEvent &event);
	void OnIgnoredMouse(wxMouseEvent &event);
	void OnIgnoredKey(wxKeyEvent &event);

	void ScreenToMap(int screen_x, int screen_y, int* map_x, int* map_y) override;
	void GetScreenCenter(int* map_x, int* map_y) override;
	void GetViewBox(int* view_scroll_x, int* view_scroll_y, int* screensize_x, int* screensize_y) const override;
	void UpdatePositionStatus(int x = -1, int y = -1) override;
	void UpdateZoomStatus() override;
	void ConfigureDrawingOptions(DrawingOptions &options) override;
	void CenterViewOnPosition(const Position &position) override;
	void OnFloorChanged() override;

	BaseMap previewMap;
	std::function<void(const Position &)> onWallClick;
	std::set<Position> pendingFit;
	int viewX = 0;
	int viewY = 0;
	wxPoint pressPoint;
	wxPoint lastPoint;
	bool dragged = false;

	DECLARE_EVENT_TABLE()
};

class ReplaceBuildingWallsDialog : public wxDialog {
public:
	ReplaceBuildingWallsDialog(wxWindow* parent, Editor &editor, const Position &origin);

private:
	struct OpeningRow {
		wxStaticBitmap* icon = nullptr;
		wxStaticText* label = nullptr;
		wxChoice* action = nullptr;
		::DoorType type = WALL_UNDEFINED;
		std::vector<OpeningAction> actions;
	};

	wxPanel* createStylePanel();
	wxPanel* createPreviewPanel();
	wxPanel* createBuildingPanel();

	void rescan();
	void rebuildFloorList();
	void rebuildSourceList();
	void refreshStyles();
	void refreshPlan();
	void updateLitTiles(const WallReplacementRequest &request);
	std::vector<OpeningGroup> collectOpenings() const;
	void updateOpeningRow(OpeningRow &row, const OpeningGroup &group);
	void updateOpenings();
	void updateRailings();
	void updateSummary();
	void updateFloorHeader();
	void updateScopeNote();
	void moveDisplayedFloor(int offset);
	void toggleWall(const Position &clicked);
	WallReplacementRequest makeRequest(WallBrush* target) const;

	void OnCharHook(wxKeyEvent &event);
	void OnScopeChanged(wxCommandEvent &event);
	void OnOtherSide(wxCommandEvent &event);
	void OnFilterChanged(wxCommandEvent &event);
	void OnEmptyAction(wxCommandEvent &event);
	void OnStyleSelected(wxCommandEvent &event);
	void OnFloorSelected(wxCommandEvent &event);
	void OnSelectionChanged(wxCommandEvent &event);
	void OnOpeningAction(wxCommandEvent &event);
	void OnRailingAction(wxCommandEvent &event);
	void OnCompare(wxCommandEvent &event);
	void OnFit(wxCommandEvent &event);
	void OnFloorUp(wxCommandEvent &event);
	void OnFloorDown(wxCommandEvent &event);
	void OnApply(wxCommandEvent &event);

	Editor &editor;
	Position origin;
	BuildingScope scope = BuildingScope::House;
	bool houseFound = false;
	int side = -1;
	BuildingScan scan;
	WallReplacementPlan plan;
	std::vector<WallStyle> styles;
	std::vector<WallBrush*> sourceBrushes;
	std::set<Position> excluded;
	std::set<Position> litTiles;
	std::map<::DoorType, OpeningAction> openingActions;
	std::vector<OpeningRow> openingRows;
	bool keepRailings = false;
	int shownFloor = 0;

	wxTextCtrl* search = nullptr;
	wxCheckBox* completeOnly = nullptr;
	WallStyleList* styleList = nullptr;
	wxPanel* emptyState = nullptr;
	wxStaticText* emptyText = nullptr;
	wxButton* emptyAction = nullptr;
	wxPanel* stylePanel = nullptr;

	wxStaticText* floorHeader = nullptr;
	wxToggleButton* compareButton = nullptr;
	BuildingPreviewCanvas* preview = nullptr;

	wxPanel* buildingPanel = nullptr;
	wxRadioBox* scopeChoice = nullptr;
	wxButton* otherSideButton = nullptr;
	wxStaticText* scopeNote = nullptr;
	wxCheckListBox* floorList = nullptr;
	wxStaticText* sourceHeader = nullptr;
	wxCheckListBox* sourceList = nullptr;
	wxCheckBox* sharedCheck = nullptr;
	wxStaticText* openingHeader = nullptr;
	wxStaticText* railingHeader = nullptr;
	wxStaticBitmap* railingIcon = nullptr;
	wxStaticText* railingLabel = nullptr;
	wxChoice* railingAction = nullptr;

	wxStaticText* summary = nullptr;
	wxButton* applyButton = nullptr;
};

#endif
