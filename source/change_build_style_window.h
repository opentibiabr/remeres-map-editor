//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//////////////////////////////////////////////////////////////////////

#ifndef RME_CHANGE_BUILD_STYLE_WINDOW_H_
#define RME_CHANGE_BUILD_STYLE_WINDOW_H_

#include "main.h"
#include "basemap.h"
#include "change_build_style.h"
#include "map_display.h"

class Editor;
class WallBrush;

struct ChangeBuildStyleStyleEntry {
	WallBrush* brush;
	bool fullMatch;
	size_t conflictCount;
	wxString note;
};

class ChangeBuildStyleListBox : public wxVListBox {
public:
	ChangeBuildStyleListBox(wxWindow* parent);

	void setStyles(const std::vector<ChangeBuildStyleStyleEntry> &styles);
	WallBrush* getSelectedBrush() const;

protected:
	void OnDrawItem(wxDC &dc, const wxRect &rect, size_t index) const override;
	wxCoord OnMeasureItem(size_t index) const override;

private:
	std::vector<ChangeBuildStyleStyleEntry> styles;
};

class ChangeBuildStylePreview : public MapCanvas {
public:
	ChangeBuildStylePreview(wxWindow* parent, Editor &editor, const Position &origin);

	BaseMap &getPreviewMap() noexcept {
		return previewMap;
	}
	void centerOn(const Position &position);
	void fitBuilding(const PositionVector &positions);
	void showFloor(int floor);

private:
	void OnMouseMove(wxMouseEvent &event);
	void OnWheel(wxMouseEvent &event);
	void OnMouseCenterClick(wxMouseEvent &event);
	void OnMouseCenterRelease(wxMouseEvent &event);
	void OnMouseRightClick(wxMouseEvent &event);
	void OnMouseRightRelease(wxMouseEvent &event);
	void OnIgnoredMouseButton(wxMouseEvent &event);
	void OnIgnoredKey(wxKeyEvent &event);
	void OnGainMouse(wxMouseEvent &event);
	void OnLoseMouse(wxMouseEvent &event);
	void startPan(const wxMouseEvent &event, bool rightButton);
	void stopPan(const wxMouseEvent &event, bool rightButton);

	void ScreenToMap(int screen_x, int screen_y, int* map_x, int* map_y) override;
	void GetScreenCenter(int* map_x, int* map_y) override;
	void GetViewBox(int* view_scroll_x, int* view_scroll_y, int* screensize_x, int* screensize_y) const override;
	void UpdatePositionStatus(int x = -1, int y = -1) override;
	void UpdateZoomStatus() override;
	void ConfigureDrawingOptions(DrawingOptions &options) override;
	void CenterViewOnPosition(const Position &position) override;
	void OnFloorChanged() override;

	BaseMap previewMap;
	int viewX;
	int viewY;
	bool panning;
	wxPoint panAnchor;

	DECLARE_EVENT_TABLE()
};

class ChangeBuildStyleDialog : public wxDialog {
public:
	ChangeBuildStyleDialog(wxWindow* parent, Editor &editor, const Position &origin);

	bool isValid() const noexcept {
		return service.isValid();
	}

private:
	void refreshStyles();
	void refreshPreview();
	void updateFloorControls();
	void updateConflictControls();
	std::set<int> selectedFloors() const;

	void OnFilterChanged(wxCommandEvent &event);
	void OnStyleSelected(wxCommandEvent &event);
	void OnFloorsChanged(wxCommandEvent &event);
	void OnConflictSelected(wxCommandEvent &event);
	void OnOnlyCurrentFloor(wxCommandEvent &event);
	void OnFloorUp(wxCommandEvent &event);
	void OnFloorDown(wxCommandEvent &event);
	void OnApply(wxCommandEvent &event);

	ChangeBuildStyleService service;
	std::vector<WallBrush*> allStyles;
	std::vector<ChangeBuildStyleConflict> currentConflicts;
	int displayFloorIndex;

	wxTextCtrl* search;
	wxChoice* category;
	wxCheckBox* fullMatchOnly;
	ChangeBuildStyleListBox* styleList;
	wxCheckListBox* floorList;
	wxCheckBox* onlyCurrentFloor;
	wxStaticText* conflictLabel;
	wxListBox* conflictList;
	wxStaticText* previewFloorLabel;
	ChangeBuildStylePreview* preview;
	wxStaticText* status;
	wxButton* applyButton;
};

#endif
