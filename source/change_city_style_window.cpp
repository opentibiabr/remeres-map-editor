//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
////////////////////////////////////////////////////////////////////

#include "main.h"

#include <cctype>

#include <wx/artprov.h>

#include "change_city_style_window.h"

#include "change_build_style_window.h"
#include "editor.h"
#include "house.h"
#include "map.h"
#include "town.h"
#include "wall_brush.h"

namespace {
std::string lowercaseCityStyleWindowText(std::string text) {
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return text;
}
}

ChangeCityStyleDialog::ChangeCityStyleDialog(wxWindow* parent, Editor &editor) :
	wxDialog(parent, wxID_ANY, "Change City Style", wxDefaultPosition, wxSize(980, 650), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	editor(editor),
	service(editor),
	allStyles(ChangeCityStyleService::availableTargetStyles()) {
	wxBoxSizer* root = newd wxBoxSizer(wxVERTICAL);
	wxBoxSizer* content = newd wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* left = newd wxBoxSizer(wxVERTICAL);
	wxBoxSizer* right = newd wxBoxSizer(wxVERTICAL);

	left->Add(newd wxStaticText(this, wxID_ANY, "City / town"), 0, wxBOTTOM, 4);
	townChoice = newd wxChoice(this, wxID_ANY);
	left->Add(townChoice, 0, wxBOTTOM | wxEXPAND, 8);

	wxBoxSizer* filterRow = newd wxBoxSizer(wxHORIZONTAL);
	search = newd wxTextCtrl(this, wxID_ANY);
	search->SetHint("Search wall style");
	filterRow->Add(search, 1, wxEXPAND);
	left->Add(filterRow, 0, wxBOTTOM | wxEXPAND, 8);

	fullMatchOnly = newd wxCheckBox(this, wxID_ANY, "Show only full matches");
	fullMatchOnly->SetToolTip("Only list styles that can replace every detected city wall, door, window and archway without keeping old pieces.");
	left->Add(fullMatchOnly, 0, wxBOTTOM | wxEXPAND, 8);

	styleList = newd ChangeBuildStyleListBox(this);
	styleList->SetMinSize(wxSize(360, 260));
	left->Add(styleList, 1, wxBOTTOM | wxEXPAND, 10);

	summary = newd wxStaticText(this, wxID_ANY, wxEmptyString);
	left->Add(summary, 0, wxBOTTOM | wxEXPAND, 8);

	conflictLabel = newd wxStaticText(this, wxID_ANY, "Conflicts");
	left->Add(conflictLabel, 0, wxBOTTOM, 4);
	conflictList = newd wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(360, 110), 0, nullptr, wxLB_SINGLE | wxLB_ALWAYS_SB);
	left->Add(conflictList, 0, wxBOTTOM | wxEXPAND, 8);
	conflictLabel->Hide();
	conflictList->Hide();

	wxBoxSizer* previewHeader = newd wxBoxSizer(wxHORIZONTAL);
	previewFloorLabel = newd wxStaticText(this, wxID_ANY, "Preview");
	previewHeader->Add(previewFloorLabel, 1, wxALIGN_CENTER_VERTICAL);
	wxBitmapButton* up = newd wxBitmapButton(this, wxID_ANY, wxArtProvider::GetBitmap(wxART_GO_UP, wxART_BUTTON, wxSize(16, 16)));
	up->SetToolTip("Show upper floor");
	previewHeader->Add(up, 0, wxLEFT, 4);
	wxBitmapButton* down = newd wxBitmapButton(this, wxID_ANY, wxArtProvider::GetBitmap(wxART_GO_DOWN, wxART_BUTTON, wxSize(16, 16)));
	down->SetToolTip("Show lower floor");
	previewHeader->Add(down, 0, wxLEFT, 4);
	right->Add(previewHeader, 0, wxBOTTOM | wxEXPAND, 8);

	preview = newd ChangeBuildStylePreview(this, editor, Position(0, 0, rme::MapGroundLayer));
	right->Add(preview, 1, wxEXPAND);

	content->Add(left, 0, wxALL | wxEXPAND, 12);
	content->Add(right, 1, wxTOP | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
	root->Add(content, 1, wxEXPAND);

	status = newd wxStaticText(this, wxID_ANY, wxEmptyString);
	root->Add(status, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
	wxStdDialogButtonSizer* buttons = newd wxStdDialogButtonSizer();
	applyButton = newd wxButton(this, wxID_OK, "Apply city style");
	applyButton->Enable(false);
	buttons->AddButton(applyButton);
	buttons->AddButton(newd wxButton(this, wxID_CANCEL, "Cancel"));
	buttons->Realize();
	root->Add(buttons, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxALIGN_RIGHT, 12);

	SetSizer(root);
	Centre(wxBOTH);

	townChoice->Bind(wxEVT_CHOICE, &ChangeCityStyleDialog::OnTownChanged, this);
	search->Bind(wxEVT_TEXT, &ChangeCityStyleDialog::OnFilterChanged, this);
	fullMatchOnly->Bind(wxEVT_CHECKBOX, &ChangeCityStyleDialog::OnFilterChanged, this);
	styleList->Bind(wxEVT_LISTBOX, &ChangeCityStyleDialog::OnStyleSelected, this);
	conflictList->Bind(wxEVT_LISTBOX, &ChangeCityStyleDialog::OnConflictSelected, this);
	up->Bind(wxEVT_BUTTON, &ChangeCityStyleDialog::OnFloorUp, this);
	down->Bind(wxEVT_BUTTON, &ChangeCityStyleDialog::OnFloorDown, this);
	applyButton->Bind(wxEVT_BUTTON, &ChangeCityStyleDialog::OnApply, this);

	populateTowns();
	analyzeTown();
}

void ChangeCityStyleDialog::populateTowns() {
	townChoice->Clear();
	townIds.clear();
	Map &map = editor.getMap();
	for (auto townIterator = map.towns.begin(); townIterator != map.towns.end(); ++townIterator) {
		Town* town = townIterator->second;
		if (!town) {
			continue;
		}
		size_t houseCount = 0;
		for (const auto &houseEntry : map.houses) {
			House* house = houseEntry.second;
			if (house && house->townid == town->getID()) {
				++houseCount;
			}
		}
		townChoice->Append(wxString::Format("%s (%u) - %zu houses", wxstr(town->getName()).c_str(), town->getID(), houseCount));
		townIds.push_back(town->getID());
	}
	if (!townIds.empty()) {
		townChoice->SetSelection(0);
	}
}

uint32_t ChangeCityStyleDialog::selectedTownId() const {
	const int selection = townChoice->GetSelection();
	if (selection == wxNOT_FOUND || static_cast<size_t>(selection) >= townIds.size()) {
		return 0;
	}
	return townIds[selection];
}

void ChangeCityStyleDialog::analyzeTown() {
	service.analyze(selectedTownId());
	const ChangeCityStyleAnalysis &analysis = service.getAnalysis();
	previewFloors.assign(analysis.floors.begin(), analysis.floors.end());
	displayFloorIndex = 0;
	for (size_t index = 0; index < previewFloors.size(); ++index) {
		if (previewFloors[index] == analysis.center.z) {
			displayFloorIndex = static_cast<int>(index);
			break;
		}
	}
	if (analysis.valid && !analysis.previewPositions.empty()) {
		preview->fitBuilding(analysis.previewPositions);
		preview->centerOn(analysis.center);
	}
	updateSummary();
	updateFloorControls();
	refreshStyles();
}

void ChangeCityStyleDialog::refreshStyles() {
	const std::string filter = lowercaseCityStyleWindowText(std::string(search->GetValue().mb_str()));
	std::vector<ChangeBuildStyleStyleEntry> filtered;
	for (WallBrush* style : allStyles) {
		const std::string name = lowercaseCityStyleWindowText(style->getName());
		if (!filter.empty() && name.find(filter) == std::string::npos) {
			continue;
		}

		const ChangeCityStyleCompatibility compatibility = service.checkCompatibility(style);
		if (fullMatchOnly->IsChecked() && !compatibility.fullMatch()) {
			continue;
		}

		wxString note;
		if (compatibility.fullMatch()) {
			note = "Full city match";
		} else if (compatibility.compatible) {
			note = wxString::Format(
				"Missing %zu piece%s",
				compatibility.conflicts.size(),
				compatibility.conflicts.size() == 1 ? "" : "s"
			);
		} else if (!compatibility.reason.empty()) {
			note = compatibility.reason;
		} else {
			note = "Incomplete wall set";
		}
		filtered.push_back({ style, compatibility.fullMatch(), compatibility.conflicts.size(), note });
	}
	styleList->setStyles(filtered);
	refreshPreview();
}

void ChangeCityStyleDialog::refreshPreview() {
	currentConflicts.clear();
	WallBrush* target = styleList->getSelectedBrush();
	if (!target) {
		preview->getPreviewMap().clear();
		status->SetLabel(service.getAnalysis().valid ? wxString("No wall style matches the filter.") : service.getAnalysis().reason);
		applyButton->Enable(false);
		updateConflictControls();
		preview->Refresh();
		return;
	}

	wxString reason;
	if (!service.buildPreview(target, preview->getPreviewMap(), reason, &currentConflicts)) {
		status->SetLabel(reason);
		applyButton->Enable(false);
		updateConflictControls();
		preview->Refresh();
		return;
	}
	if (currentConflicts.empty()) {
		status->SetLabel(wxString::Format("Ready: %s", wxstr(target->getName()).c_str()));
	} else {
		status->SetLabel(wxString::Format(
			"Ready with %zu conflict%s: preview keeps old pieces. Apply will ask how to handle them.",
			currentConflicts.size(),
			currentConflicts.size() == 1 ? "" : "s"
		));
	}
	applyButton->Enable(true);
	updateConflictControls();
	preview->Refresh();
}

void ChangeCityStyleDialog::updateSummary() {
	const ChangeCityStyleAnalysis &analysis = service.getAnalysis();
	if (!analysis.valid) {
		summary->SetLabel(analysis.reason.empty() ? wxString("No city analyzed.") : analysis.reason);
		return;
	}
	summary->SetLabel(wxString::Format(
		"%s\nHouses: %zu | House tiles: %zu\nUrban tiles: %zu | Walls: %zu\nFloors: %zu | Source styles: %zu",
		analysis.townName.c_str(),
		analysis.houseCount,
		analysis.houseTileCount,
		analysis.urbanTileCount,
		analysis.wallCount,
		analysis.floors.size(),
		analysis.sourceStyles.size()
	));
}

void ChangeCityStyleDialog::updateFloorControls() {
	if (previewFloors.empty()) {
		previewFloorLabel->SetLabel("Preview");
		return;
	}
	displayFloorIndex = std::max(0, std::min(displayFloorIndex, static_cast<int>(previewFloors.size()) - 1));
	previewFloorLabel->SetLabel(wxString::Format("Preview - floor %d", previewFloors[displayFloorIndex]));
	preview->showFloor(previewFloors[displayFloorIndex]);
}

void ChangeCityStyleDialog::updateConflictControls() {
	conflictList->Clear();
	if (currentConflicts.empty()) {
		conflictLabel->Hide();
		conflictList->Hide();
		Layout();
		return;
	}
	conflictLabel->SetLabel(wxString::Format("Conflicts (%zu) - click to view", currentConflicts.size()));
	for (const ChangeCityStyleConflict &conflict : currentConflicts) {
		conflictList->Append(conflict.message);
	}
	conflictLabel->Show();
	conflictList->Show();
	Layout();
}

void ChangeCityStyleDialog::OnTownChanged(wxCommandEvent &WXUNUSED(event)) {
	analyzeTown();
}

void ChangeCityStyleDialog::OnFilterChanged(wxCommandEvent &WXUNUSED(event)) {
	refreshStyles();
}

void ChangeCityStyleDialog::OnStyleSelected(wxCommandEvent &WXUNUSED(event)) {
	refreshPreview();
}

void ChangeCityStyleDialog::OnConflictSelected(wxCommandEvent &WXUNUSED(event)) {
	const int selection = conflictList->GetSelection();
	if (selection == wxNOT_FOUND || static_cast<size_t>(selection) >= currentConflicts.size()) {
		return;
	}
	const Position &position = currentConflicts[selection].position;
	for (size_t index = 0; index < previewFloors.size(); ++index) {
		if (previewFloors[index] == position.z) {
			displayFloorIndex = static_cast<int>(index);
			break;
		}
	}
	updateFloorControls();
	preview->centerOn(position);
	preview->Refresh();
}

void ChangeCityStyleDialog::OnFloorUp(wxCommandEvent &WXUNUSED(event)) {
	if (displayFloorIndex > 0) {
		--displayFloorIndex;
		updateFloorControls();
		refreshPreview();
	}
}

void ChangeCityStyleDialog::OnFloorDown(wxCommandEvent &WXUNUSED(event)) {
	if (displayFloorIndex + 1 < static_cast<int>(previewFloors.size())) {
		++displayFloorIndex;
		updateFloorControls();
		refreshPreview();
	}
}

void ChangeCityStyleDialog::OnApply(wxCommandEvent &WXUNUSED(event)) {
	ChangeCityStyleConflictAction conflictAction = ChangeCityStyleConflictAction::KeepExisting;
	if (!currentConflicts.empty()) {
		size_t replaceable = 0;
		for (const ChangeCityStyleConflict &conflict : currentConflicts) {
			if (conflict.canReplaceWithWall) {
				++replaceable;
			}
		}
		wxMessageDialog dialog(
			this,
			wxString::Format(
				"This city conversion has %zu conflict%s.\n%zu can be replaced with normal target walls.\n\n"
				"Choose how to handle unsupported openings.",
				currentConflicts.size(),
				currentConflicts.size() == 1 ? "" : "s",
				replaceable
			),
			"Unresolved city pieces",
			wxYES_NO | wxCANCEL | wxICON_WARNING
		);
		dialog.SetYesNoLabels("Keep old pieces", "Replace openings with wall");
		const int result = dialog.ShowModal();
		if (result == wxID_CANCEL) {
			return;
		}
		if (result == wxID_NO) {
			conflictAction = ChangeCityStyleConflictAction::ReplaceOpeningWithWall;
		}
	}

	wxString reason;
	if (!service.apply(styleList->getSelectedBrush(), conflictAction, reason)) {
		status->SetLabel(reason);
		applyButton->Enable(false);
		return;
	}
	EndModal(wxID_OK);
}
