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
#include "change_build_style.h"

class Editor;
class WallBrush;

class ChangeBuildStyleListBox : public wxVListBox {
public:
	ChangeBuildStyleListBox(wxWindow* parent);

	void setBrushes(const std::vector<WallBrush*> &brushes);
	WallBrush* getSelectedBrush() const;

protected:
	void OnDrawItem(wxDC &dc, const wxRect &rect, size_t index) const override;
	wxCoord OnMeasureItem(size_t index) const override;

private:
	std::vector<WallBrush*> brushes;
};

class ChangeBuildStylePreview : public wxScrolledWindow {
public:
	ChangeBuildStylePreview(wxWindow* parent);

	void setItems(const std::vector<ChangeBuildStylePreviewItem> &items);

private:
	void OnPaint(wxPaintEvent &event);

	std::vector<ChangeBuildStylePreviewItem> items;
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
	std::set<int> selectedFloors() const;

	void OnFilterChanged(wxCommandEvent &event);
	void OnStyleSelected(wxCommandEvent &event);
	void OnFloorsChanged(wxCommandEvent &event);
	void OnOnlyCurrentFloor(wxCommandEvent &event);
	void OnFloorUp(wxCommandEvent &event);
	void OnFloorDown(wxCommandEvent &event);
	void OnApply(wxCommandEvent &event);

	ChangeBuildStyleService service;
	std::vector<WallBrush*> allStyles;
	int displayFloorIndex;

	wxTextCtrl* search;
	wxChoice* category;
	ChangeBuildStyleListBox* styleList;
	wxCheckListBox* floorList;
	wxCheckBox* onlyCurrentFloor;
	wxStaticText* previewFloorLabel;
	ChangeBuildStylePreview* preview;
	wxStaticText* status;
	wxButton* applyButton;
};

#endif
