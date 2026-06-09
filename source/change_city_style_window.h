//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
////////////////////////////////////////////////////////////////////

#ifndef RME_CHANGE_CITY_STYLE_WINDOW_H_
#define RME_CHANGE_CITY_STYLE_WINDOW_H_

#include <vector>

#include "change_city_style.h"

class ChangeBuildStyleListBox;
class ChangeBuildStylePreview;
class Editor;
class WallBrush;

class ChangeCityStyleDialog : public wxDialog {
public:
	ChangeCityStyleDialog(wxWindow* parent, Editor &editor);

private:
	void populateTowns();
	uint32_t selectedTownId() const;
	void analyzeTown();
	void refreshStyles();
	void refreshPreview();
	void updateSummary();
	void updateFloorControls();
	void updateConflictControls();

	void OnTownChanged(wxCommandEvent &event);
	void OnFilterChanged(wxCommandEvent &event);
	void OnStyleSelected(wxCommandEvent &event);
	void OnConflictSelected(wxCommandEvent &event);
	void OnFloorUp(wxCommandEvent &event);
	void OnFloorDown(wxCommandEvent &event);
	void OnApply(wxCommandEvent &event);

	Editor &editor;
	ChangeCityStyleService service;
	std::vector<uint32_t> townIds;
	std::vector<WallBrush*> allStyles;
	std::vector<ChangeCityStyleConflict> currentConflicts;
	std::vector<int> previewFloors;
	int displayFloorIndex = 0;

	wxChoice* townChoice = nullptr;
	wxTextCtrl* search = nullptr;
	wxCheckBox* fullMatchOnly = nullptr;
	ChangeBuildStyleListBox* styleList = nullptr;
	wxStaticText* summary = nullptr;
	wxStaticText* conflictLabel = nullptr;
	wxListBox* conflictList = nullptr;
	wxStaticText* previewFloorLabel = nullptr;
	ChangeBuildStylePreview* preview = nullptr;
	wxStaticText* status = nullptr;
	wxButton* applyButton = nullptr;
};

#endif
