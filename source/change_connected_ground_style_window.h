//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//////////////////////////////////////////////////////////////////////

#ifndef RME_CHANGE_CONNECTED_GROUND_STYLE_WINDOW_H_
#define RME_CHANGE_CONNECTED_GROUND_STYLE_WINDOW_H_

#include "main.h"
#include "change_connected_ground_style.h"

class ChangeBuildStylePreview;
class Editor;
class GroundBrush;

class ChangeConnectedGroundStyleListBox : public wxVListBox {
public:
	ChangeConnectedGroundStyleListBox(wxWindow* parent);

	void setBrushes(const std::vector<GroundBrush*> &brushes);
	GroundBrush* getSelectedBrush() const;

protected:
	void OnDrawItem(wxDC &dc, const wxRect &rect, size_t index) const override;
	wxCoord OnMeasureItem(size_t index) const override;

private:
	std::vector<GroundBrush*> brushes;
};

class ChangeConnectedGroundStyleDialog : public wxDialog {
public:
	ChangeConnectedGroundStyleDialog(wxWindow* parent, Editor &editor, const Position &origin);

	bool isValid() const noexcept {
		return service.isValid();
	}

private:
	void refreshStyles();
	void refreshPreview();
	void updateCompositionWarning();

	void OnFilterChanged(wxCommandEvent &event);
	void OnStyleSelected(wxCommandEvent &event);
	void OnApply(wxCommandEvent &event);

	ChangeConnectedGroundStyleService service;
	std::vector<GroundBrush*> allStyles;

	wxStaticText* sourceLabel;
	wxStaticText* componentLabel;
	wxTextCtrl* search;
	wxChoice* family;
	ChangeConnectedGroundStyleListBox* styleList;
	wxStaticText* compositionWarning;
	ChangeBuildStylePreview* preview;
	wxStaticText* status;
	wxButton* applyButton;
};

#endif
