//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "change_connected_ground_style_window.h"

#include "change_build_style_window.h"
#include "ground_brush.h"
#include "gui.h"
#include "sprites.h"

namespace {
std::string connectedGroundDialogLowercase(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return value;
}
}

ChangeConnectedGroundStyleListBox::ChangeConnectedGroundStyleListBox(wxWindow* parent) :
	wxVListBox(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLB_SINGLE) {
}

void ChangeConnectedGroundStyleListBox::setBrushes(const std::vector<GroundBrush*> &newBrushes) {
	brushes = newBrushes;
	SetItemCount(brushes.size());
	if (!brushes.empty()) {
		SetSelection(0);
	}
	Refresh();
}

GroundBrush* ChangeConnectedGroundStyleListBox::getSelectedBrush() const {
	const int selection = GetSelection();
	if (selection == wxNOT_FOUND || static_cast<size_t>(selection) >= brushes.size()) {
		return nullptr;
	}
	return brushes[selection];
}

void ChangeConnectedGroundStyleListBox::OnDrawItem(wxDC &dc, const wxRect &rect, size_t index) const {
	if (index >= brushes.size()) {
		return;
	}
	GroundBrush* brush = brushes[index];
	if (IsSelected(index)) {
		dc.SetTextForeground(HasFocus() ? wxColour(255, 255, 255) : wxColour(0, 0, 255));
	} else {
		dc.SetTextForeground(wxColour(0, 0, 0));
	}
	if (Sprite* sprite = g_gui.gfx.getSprite(brush->getLookID())) {
		sprite->DrawTo(&dc, SPRITE_SIZE_32x32, rect.GetX() + 3, rect.GetY() + 3, rect.GetWidth(), rect.GetHeight());
	}
	dc.DrawText(wxstr(brush->getName()), rect.GetX() + 42, rect.GetY() + 4);
	dc.DrawText(UrbanGroundStyleCatalog::familyLabel(brush), rect.GetX() + 42, rect.GetY() + 20);
}

wxCoord ChangeConnectedGroundStyleListBox::OnMeasureItem(size_t WXUNUSED(index)) const {
	return 42;
}

ChangeConnectedGroundStyleDialog::ChangeConnectedGroundStyleDialog(wxWindow* parent, Editor &editor, const Position &origin) :
	wxDialog(parent, wxID_ANY, "Change Connected Ground Style", wxDefaultPosition, wxSize(930, 610), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	service(editor, origin),
	allStyles(UrbanGroundStyleCatalog::styles(service.getSourceBrush())) {
	wxBoxSizer* root = newd wxBoxSizer(wxVERTICAL);
	wxBoxSizer* content = newd wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* left = newd wxBoxSizer(wxVERTICAL);
	wxBoxSizer* right = newd wxBoxSizer(wxVERTICAL);

	sourceLabel = newd wxStaticText(this, wxID_ANY, wxEmptyString);
	componentLabel = newd wxStaticText(this, wxID_ANY, wxEmptyString);
	if (service.isValid()) {
		sourceLabel->SetLabel(wxString::Format("Source: %s", wxstr(service.getSourceBrush()->getName()).c_str()));
		componentLabel->SetLabel(wxString::Format("Connected tiles on floor %d: %zu", origin.z, service.getPositions().size()));
	} else {
		sourceLabel->SetLabel("Source: unavailable");
		componentLabel->SetLabel("No supported urban ground component selected.");
	}
	left->Add(sourceLabel, 0, wxBOTTOM | wxEXPAND, 3);
	left->Add(componentLabel, 0, wxBOTTOM | wxEXPAND, 10);

	wxBoxSizer* filterRow = newd wxBoxSizer(wxHORIZONTAL);
	search = newd wxTextCtrl(this, wxID_ANY);
	search->SetHint("Search ground style");
	filterRow->Add(search, 1, wxRIGHT | wxEXPAND, 6);
	family = newd wxChoice(this, wxID_ANY);
	family->Append("All families");
	family->Append("Pavement");
	family->Append("Venore");
	family->Append("Desert stone");
	family->Append("Oramond");
	family->Append("Roshamuul");
	family->Append("Ornamented");
	family->SetSelection(0);
	filterRow->Add(family, 0, wxEXPAND);
	left->Add(filterRow, 0, wxBOTTOM | wxEXPAND, 8);

	styleList = newd ChangeConnectedGroundStyleListBox(this);
	styleList->SetMinSize(wxSize(365, 315));
	left->Add(styleList, 1, wxBOTTOM | wxEXPAND, 10);

	compositionWarning = newd wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(365, -1));
	compositionWarning->Wrap(350);
	left->Add(compositionWarning, 0, wxEXPAND);

	right->Add(newd wxStaticText(this, wxID_ANY, wxString::Format("Preview - floor %d", origin.z)), 0, wxBOTTOM | wxEXPAND, 8);
	preview = newd ChangeBuildStylePreview(this, editor, origin);
	preview->centerOn(origin);
	right->Add(preview, 1, wxEXPAND);

	content->Add(left, 1, wxALL | wxEXPAND, 12);
	content->Add(right, 1, wxTOP | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
	root->Add(content, 1, wxEXPAND);

	status = newd wxStaticText(this, wxID_ANY, wxEmptyString);
	root->Add(status, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
	wxStdDialogButtonSizer* buttons = newd wxStdDialogButtonSizer();
	applyButton = newd wxButton(this, wxID_OK, "Apply style");
	applyButton->Enable(false);
	buttons->AddButton(applyButton);
	buttons->AddButton(newd wxButton(this, wxID_CANCEL, "Cancel"));
	buttons->Realize();
	root->Add(buttons, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxALIGN_RIGHT, 12);

	SetSizer(root);
	Centre(wxBOTH);

	search->Bind(wxEVT_TEXT, &ChangeConnectedGroundStyleDialog::OnFilterChanged, this);
	family->Bind(wxEVT_CHOICE, &ChangeConnectedGroundStyleDialog::OnFilterChanged, this);
	styleList->Bind(wxEVT_LISTBOX, &ChangeConnectedGroundStyleDialog::OnStyleSelected, this);
	applyButton->Bind(wxEVT_BUTTON, &ChangeConnectedGroundStyleDialog::OnApply, this);

	updateCompositionWarning();
	refreshStyles();
}

void ChangeConnectedGroundStyleDialog::refreshStyles() {
	const std::string filter = connectedGroundDialogLowercase(std::string(search->GetValue().mb_str()));
	const wxString selectedFamily = family->GetStringSelection();
	std::vector<GroundBrush*> filtered;
	for (GroundBrush* style : allStyles) {
		const std::string name = connectedGroundDialogLowercase(style->getName());
		if (!filter.empty() && name.find(filter) == std::string::npos) {
			continue;
		}
		if (family->GetSelection() > 0 && UrbanGroundStyleCatalog::familyLabel(style) != selectedFamily) {
			continue;
		}
		filtered.push_back(style);
	}
	styleList->setBrushes(filtered);
	refreshPreview();
}

void ChangeConnectedGroundStyleDialog::refreshPreview() {
	if (!service.isValid()) {
		status->SetLabel("Select a supported urban ground before using this action.");
		applyButton->Enable(false);
		return;
	}
	GroundBrush* target = styleList->getSelectedBrush();
	if (!target) {
		status->SetLabel("No urban ground style matches this filter.");
		preview->getPreviewMap().clear();
		preview->Refresh();
		applyButton->Enable(false);
		return;
	}

	wxString reason;
	if (!service.buildPreview(target, preview->getPreviewMap(), reason)) {
		status->SetLabel(reason);
		preview->Refresh();
		applyButton->Enable(false);
		return;
	}
	status->SetLabel(wxString::Format("Ready: replace with %s. Surrounding grounds and borders are preserved.", wxstr(target->getName()).c_str()));
	applyButton->Enable(true);
	preview->Refresh();
}

void ChangeConnectedGroundStyleDialog::updateCompositionWarning() {
	const auto &adjacent = service.getAdjacentUrbanBrushes();
	if (adjacent.empty() || !service.getSourceBrush()) {
		compositionWarning->Hide();
		return;
	}

	wxString neighbours;
	for (size_t index = 0; index < adjacent.size(); ++index) {
		if (index > 0) {
			neighbours += ", ";
		}
		neighbours += wxstr(adjacent[index].brush->getName());
	}
	compositionWarning->SetLabel(wxString::Format(
		"Composite urban path detected. Only the selected %s component will change; neighbouring urban grounds remain unchanged: %s.",
		wxstr(service.getSourceBrush()->getName()).c_str(),
		neighbours.c_str()
	));
	compositionWarning->Show();
}

void ChangeConnectedGroundStyleDialog::OnFilterChanged(wxCommandEvent &WXUNUSED(event)) {
	refreshStyles();
}

void ChangeConnectedGroundStyleDialog::OnStyleSelected(wxCommandEvent &WXUNUSED(event)) {
	refreshPreview();
}

void ChangeConnectedGroundStyleDialog::OnApply(wxCommandEvent &WXUNUSED(event)) {
	wxString reason;
	if (!service.applyPreview(preview->getPreviewMap(), reason)) {
		status->SetLabel(reason);
		applyButton->Enable(false);
		return;
	}
	EndModal(wxID_OK);
}
