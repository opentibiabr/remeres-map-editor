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

#include "main.h"
#include "find_item_window.h"
#include "common_windows.h"
#include "gui.h"
#include "items.h"
#include "brush.h"
#include "raw_brush.h"

BEGIN_EVENT_TABLE(FindItemDialog, wxDialog)
EVT_TIMER(wxID_ANY, FindItemDialog::OnInputTimer)
EVT_BUTTON(wxID_OK, FindItemDialog::OnClickOK)
EVT_BUTTON(wxID_CANCEL, FindItemDialog::OnClickCancel)
END_EVENT_TABLE()

FindItemDialog::FindItemDialog(wxWindow* parent, const wxString &title, bool onlyPickupables /* = false*/, bool onSelection /* = false*/) :
	wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxSize(800, 600), wxDEFAULT_DIALOG_STYLE),
	inputTimer(this),
	onlyPickupables(onlyPickupables),
	onSelection(onSelection) {
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* boxSizer = newd wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* optionsBoxSizer = newd wxBoxSizer(wxVERTICAL);

	wxString radioBoxChoices[] = { "Find by Item ID",
								   "Find by Name",
								   "Find by Types",
								   "Find by Tile Types",
								   "Find by Properties" };

	int radioBoxChoicesSize = sizeof(radioBoxChoices) / sizeof(wxString);
	optionsRadioBox = newd wxRadioBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, radioBoxChoicesSize, radioBoxChoices, 1, wxRA_SPECIFY_COLS);
	optionsRadioBox->SetSelection(SearchMode::ItemIDs);
	optionsBoxSizer->Add(optionsRadioBox, 0, wxALL | wxEXPAND, 5);

	wxStaticBoxSizer* itemIdBoxSizer = newd wxStaticBoxSizer(newd wxStaticBox(this, wxID_ANY, "Item ID"), wxVERTICAL);
	itemIdSpin = newd wxSpinCtrl(itemIdBoxSizer->GetStaticBox(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 100, g_items.getMaxID(), 100);
	itemIdBoxSizer->Add(itemIdSpin, 0, wxALL | wxEXPAND, 5);
	optionsBoxSizer->Add(itemIdBoxSizer, 1, wxALL | wxEXPAND, 5);

	wxStaticBoxSizer* nameBoxSizer = newd wxStaticBoxSizer(newd wxStaticBox(this, wxID_ANY, "Name"), wxVERTICAL);
	nameTextInput = newd wxTextCtrl(nameBoxSizer->GetStaticBox(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	nameTextInput->Enable(false);
	nameBoxSizer->Add(nameTextInput, 0, wxALL | wxEXPAND, 5);
	optionsBoxSizer->Add(nameBoxSizer, 1, wxALL | wxEXPAND, 5);

	// spacer
	optionsBoxSizer->Add(0, 0, 4, wxALL | wxEXPAND, 5);

	buttonsBoxSizer = newd wxStdDialogButtonSizer();
	okButton = newd wxButton(this, wxID_OK);
	buttonsBoxSizer->AddButton(okButton);
	cancelButton = newd wxButton(this, wxID_CANCEL);
	buttonsBoxSizer->AddButton(cancelButton);
	buttonsBoxSizer->Realize();
	optionsBoxSizer->Add(buttonsBoxSizer, 0, wxALIGN_CENTER | wxALL, 5);

	boxSizer->Add(optionsBoxSizer, 1, wxALL, 5);

	// --------------- Types ---------------

	wxStaticBoxSizer* typeBoxSizer = newd wxStaticBoxSizer(newd wxStaticBox(this, wxID_ANY, "Types"), wxVERTICAL);

	wxString typesChoices[] = { "Depot",
								"Mailbox",
								"Trash Holder",
								"Container",
								"Door",
								"Magic Field",
								"Teleport",
								"Bed",
								"Key" };

	int typesChoicesCount = sizeof(typesChoices) / sizeof(wxString);
	typesRadioBox = newd wxRadioBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, typesChoicesCount, typesChoices, 1, wxRA_SPECIFY_COLS);
	typesRadioBox->SetSelection(0);
	typesRadioBox->Enable(false);
	typeBoxSizer->Add(typesRadioBox, 0, wxALL | wxEXPAND, 5);

	boxSizer->Add(typeBoxSizer, 1, wxALL | wxEXPAND, 5);

	// --------------- Tile Types ---------------

	wxString tileTypesChoices[] = { "PZ",
									"PVP",
									"No PVP",
									"No Logout" };

	int tileTypesChoicesCount = sizeof(tileTypesChoices) / sizeof(wxString);
	tileTypesRadioBox = newd wxRadioBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, tileTypesChoicesCount, tileTypesChoices, 1, wxRA_SPECIFY_COLS);
	tileTypesRadioBox->SetSelection(0);
	tileTypesRadioBox->Enable(false);
	typeBoxSizer->Add(tileTypesRadioBox, 0, wxALL | wxEXPAND, 5);

	// --------------- Properties ---------------

	wxStaticBoxSizer* propertiesBoxSizer = newd wxStaticBoxSizer(newd wxStaticBox(this, wxID_ANY, "Properties"), wxVERTICAL);

	unpassable = newd wxCheckBox(propertiesBoxSizer->GetStaticBox(), wxID_ANY, "Unpassable", wxDefaultPosition, wxDefaultSize, 0);
	propertiesBoxSizer->Add(unpassable, 0, wxALL, 5);

	unmovable = newd wxCheckBox(propertiesBoxSizer->GetStaticBox(), wxID_ANY, "Unmovable", wxDefaultPosition, wxDefaultSize, 0);
	propertiesBoxSizer->Add(unmovable, 0, wxALL, 5);

	blockMissiles = newd wxCheckBox(propertiesBoxSizer->GetStaticBox(), wxID_ANY, "Block Missiles", wxDefaultPosition, wxDefaultSize, 0);
	propertiesBoxSizer->Add(blockMissiles, 0, wxALL, 5);

	blockPathfinder = newd wxCheckBox(propertiesBoxSizer->GetStaticBox(), wxID_ANY, "Block Pathfinder", wxDefaultPosition, wxDefaultSize, 0);
	propertiesBoxSizer->Add(blockPathfinder, 0, wxALL, 5);

	readable = newd wxCheckBox(propertiesBoxSizer->GetStaticBox(), wxID_ANY, "Readable", wxDefaultPosition, wxDefaultSize, 0);
	propertiesBoxSizer->Add(readable, 0, wxALL, 5);

	writeable = newd wxCheckBox(propertiesBoxSizer->GetStaticBox(), wxID_ANY, "Writeable", wxDefaultPosition, wxDefaultSize, 0);
	propertiesBoxSizer->Add(writeable, 0, wxALL, 5);

	pickupable = newd wxCheckBox(propertiesBoxSizer->GetStaticBox(), wxID_ANY, "Pickupable", wxDefaultPosition, wxDefaultSize, 0);
	pickupable->SetValue(onlyPickupables);
	pickupable->Enable(!onlyPickupables);
	propertiesBoxSizer->Add(pickupable, 0, wxALL, 5);

	stackable = newd wxCheckBox(propertiesBoxSizer->GetStaticBox(), wxID_ANY, "Stackable", wxDefaultPosition, wxDefaultSize, 0);
	propertiesBoxSizer->Add(stackable, 0, wxALL, 5);

	rotatable = newd wxCheckBox(propertiesBoxSizer->GetStaticBox(), wxID_ANY, "Rotatable", wxDefaultPosition, wxDefaultSize, 0);
	propertiesBoxSizer->Add(rotatable, 0, wxALL, 5);

	hangable = newd wxCheckBox(propertiesBoxSizer->GetStaticBox(), wxID_ANY, "Hangable", wxDefaultPosition, wxDefaultSize, 0);
	propertiesBoxSizer->Add(hangable, 0, wxALL, 5);

	hookEast = newd wxCheckBox(propertiesBoxSizer->GetStaticBox(), wxID_ANY, "Hook East", wxDefaultPosition, wxDefaultSize, 0);
	propertiesBoxSizer->Add(hookEast, 0, wxALL, 5);

	hookSouth = newd wxCheckBox(propertiesBoxSizer->GetStaticBox(), wxID_ANY, "Hook South", wxDefaultPosition, wxDefaultSize, 0);
	propertiesBoxSizer->Add(hookSouth, 0, wxALL, 5);

	hasElevation = newd wxCheckBox(propertiesBoxSizer->GetStaticBox(), wxID_ANY, "Has Elevation", wxDefaultPosition, wxDefaultSize, 0);
	propertiesBoxSizer->Add(hasElevation, 0, wxALL, 5);

	ignoreLook = newd wxCheckBox(propertiesBoxSizer->GetStaticBox(), wxID_ANY, "Ignore Look", wxDefaultPosition, wxDefaultSize, 0);
	propertiesBoxSizer->Add(ignoreLook, 0, wxALL, 5);

	floorChange = newd wxCheckBox(propertiesBoxSizer->GetStaticBox(), wxID_ANY, "Floor Change", wxDefaultPosition, wxDefaultSize, 0);
	propertiesBoxSizer->Add(floorChange, 0, wxALL, 5);

	boxSizer->Add(propertiesBoxSizer, 1, wxALL | wxEXPAND, 5);

	// --------------- Items list ---------------

	wxStaticBoxSizer* resultBoxSizer = newd wxStaticBoxSizer(newd wxStaticBox(this, wxID_ANY, "Result"), wxVERTICAL);
	itemsList = newd FindDialogListBox(resultBoxSizer->GetStaticBox(), wxID_ANY);
	itemsList->SetMinSize(wxSize(230, 512));
	resultBoxSizer->Add(itemsList, 0, wxALL, 5);
	boxSizer->Add(resultBoxSizer, 1, wxALL | wxEXPAND, 5);

	this->SetSizer(boxSizer);
	this->Layout();
	this->Centre(wxBOTH);
	this->EnableProperties(false);
	this->RefreshContentsInternal();

	// Connect Events
	optionsRadioBox->Connect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(FindItemDialog::OnOptionChange), nullptr, this);
	itemIdSpin->Connect(wxEVT_COMMAND_SPINCTRL_UPDATED, wxCommandEventHandler(FindItemDialog::OnItemIdChange), nullptr, this);
	itemIdSpin->Connect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(FindItemDialog::OnItemIdChange), nullptr, this);
	nameTextInput->Connect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(FindItemDialog::OnText), nullptr, this);

	typesRadioBox->Connect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(FindItemDialog::OnTypeChange), nullptr, this);
	tileTypesRadioBox->Connect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(FindItemDialog::OnTypeChange), nullptr, this);

	unpassable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	unmovable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	blockMissiles->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	blockPathfinder->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	readable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	writeable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	pickupable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	stackable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	rotatable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	hangable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	hookEast->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	hookSouth->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	hasElevation->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	ignoreLook->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	floorChange->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
}

FindItemDialog::~FindItemDialog() {
	// Disconnect Events
	optionsRadioBox->Disconnect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(FindItemDialog::OnOptionChange), NULL, this);
	itemIdSpin->Disconnect(wxEVT_COMMAND_SPINCTRL_UPDATED, wxCommandEventHandler(FindItemDialog::OnItemIdChange), NULL, this);
	itemIdSpin->Disconnect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(FindItemDialog::OnItemIdChange), NULL, this);
	nameTextInput->Disconnect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(FindItemDialog::OnText), NULL, this);

	typesRadioBox->Disconnect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(FindItemDialog::OnTypeChange), nullptr, this);
	tileTypesRadioBox->Disconnect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(FindItemDialog::OnTypeChange), nullptr, this);

	unpassable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	unmovable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	blockMissiles->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	blockPathfinder->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	readable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	writeable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	pickupable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	stackable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	rotatable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	hangable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	hookEast->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	hookSouth->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	hasElevation->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	ignoreLook->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
	floorChange->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), nullptr, this);
}

FindItemDialog::SearchMode FindItemDialog::getSearchMode() const {
	return static_cast<SearchMode>(optionsRadioBox->GetSelection());
}

FindItemDialog::SearchTileType FindItemDialog::getSearchTileType() const {
	return static_cast<SearchTileType>(tileTypesRadioBox->GetSelection());
}

void FindItemDialog::setSearchMode(SearchMode mode) {
	if (static_cast<SearchMode>(optionsRadioBox->GetSelection()) != mode) {
		optionsRadioBox->SetSelection(mode);
	}

	itemIdSpin->Enable(mode == SearchMode::ItemIDs);
	nameTextInput->Enable(mode == SearchMode::Names);
	typesRadioBox->Enable(mode == SearchMode::Types);
	tileTypesRadioBox->Enable(mode == SearchMode::TileTypes);
	EnableProperties(mode == SearchMode::Properties);
	RefreshContentsInternal();

	if (mode == SearchMode::ItemIDs) {
		itemIdSpin->SetFocus();
		itemIdSpin->SetSelection(-1, -1);
	} else if (mode == SearchMode::Names) {
		nameTextInput->SetFocus();
	}
}

void FindItemDialog::EnableProperties(bool enable) {
	unpassable->Enable(enable);
	unmovable->Enable(enable);
	blockMissiles->Enable(enable);
	blockPathfinder->Enable(enable);
	readable->Enable(enable);
	writeable->Enable(enable);
	pickupable->Enable(!onlyPickupables && enable);
	stackable->Enable(enable);
	rotatable->Enable(enable);
	hangable->Enable(enable);
	hookEast->Enable(enable);
	hookSouth->Enable(enable);
	hasElevation->Enable(enable);
	ignoreLook->Enable(enable);
	floorChange->Enable(enable);
}

void FindItemDialog::RefreshContentsInternal() {
	itemsList->Clear();
	okButton->Enable(false);

	SearchMode selection = (SearchMode)optionsRadioBox->GetSelection();
	bool foundSearchResults = false;

	if (selection == SearchMode::ItemIDs) {
		uint16_t itemID = (uint16_t)itemIdSpin->GetValue();
		for (int id = g_items.getMinID(); id <= g_items.getMaxID(); ++id) {
			const ItemType &item = g_items.getItemType(id);
			if (item.id == 0 || item.id != itemID) {
				continue;
			}

			RAWBrush* rawBrush = item.raw_brush;
			if (!rawBrush) {
				continue;
			}

			if (onlyPickupables && !item.pickupable) {
				continue;
			}

			foundSearchResults = true;
			itemsList->AddBrush(rawBrush);
		}
	} else if (selection == SearchMode::Names) {
		std::string searchString = as_lower_str(nstr(nameTextInput->GetValue()));
		if (searchString.size() >= 2) {
			for (int id = g_items.getMinID(); id <= g_items.getMaxID(); ++id) {
				const ItemType &item = g_items.getItemType(id);
				if (item.id == 0) {
					continue;
				}

				RAWBrush* rawBrush = item.raw_brush;
				if (!rawBrush) {
					continue;
				}

				if (onlyPickupables && !item.pickupable) {
					continue;
				}

				if (as_lower_str(rawBrush->getName()).find(searchString) == std::string::npos) {
					continue;
				}

				foundSearchResults = true;
				itemsList->AddBrush(rawBrush);
			}
		}
	} else if (selection == SearchMode::Types) {
		for (int id = g_items.getMinID(); id <= g_items.getMaxID(); ++id) {
			const ItemType &item = g_items.getItemType(id);
			if (item.id == 0) {
				continue;
			}

			RAWBrush* rawBrush = item.raw_brush;
			if (!rawBrush) {
				continue;
			}

			if (onlyPickupables && !item.pickupable) {
				continue;
			}

			SearchItemType selection = (SearchItemType)typesRadioBox->GetSelection();
			if ((selection == SearchItemType::Depot && !item.isDepot()) || (selection == SearchItemType::Mailbox && !item.isMailbox()) || (selection == SearchItemType::TrashHolder && !item.isTrashHolder()) || (selection == SearchItemType::Container && !item.isContainer()) || (selection == SearchItemType::Door && !item.isDoor()) || (selection == SearchItemType::MagicField && !item.isMagicField()) || (selection == SearchItemType::Teleport && !item.isTeleport()) || (selection == SearchItemType::Bed && !item.isBed()) || (selection == SearchItemType::Key && !item.isKey())) {
				continue;
			}

			foundSearchResults = true;
			itemsList->AddBrush(rawBrush);
		}
	} else if (selection == SearchMode::Properties) {
		bool hasSelected = (unpassable->GetValue() || unmovable->GetValue() || blockMissiles->GetValue() || blockPathfinder->GetValue() || readable->GetValue() || writeable->GetValue() || pickupable->GetValue() || stackable->GetValue() || rotatable->GetValue() || hangable->GetValue() || hookEast->GetValue() || hookSouth->GetValue() || hasElevation->GetValue() || ignoreLook->GetValue() || floorChange->GetValue());

		if (hasSelected) {
			for (int id = g_items.getMinID(); id <= g_items.getMaxID(); ++id) {
				const ItemType &item = g_items.getItemType(id);
				if (item.id == 0) {
					continue;
				}

				RAWBrush* rawBrush = item.raw_brush;
				if (!rawBrush) {
					continue;
				}

				if ((unpassable->GetValue() && !item.unpassable) || (unmovable->GetValue() && item.moveable) || (blockMissiles->GetValue() && !item.blockMissiles) || (blockPathfinder->GetValue() && !item.blockPathfinder) || (readable->GetValue() && !item.canReadText) || (writeable->GetValue() && !item.canWriteText) || (pickupable->GetValue() && !item.pickupable) || (stackable->GetValue() && !item.stackable) || (rotatable->GetValue() && !item.rotable) || (hangable->GetValue() && !item.isHangable) || (hookEast->GetValue() && (!item.hookEast && item.hook != ITEM_HOOK_EAST)) || (hookSouth->GetValue() && (!item.hookSouth && item.hook != ITEM_HOOK_SOUTH)) || (hasElevation->GetValue() && !item.hasElevation) || (ignoreLook->GetValue() && !item.ignoreLook) || (floorChange->GetValue() && !item.isFloorChange())) {
					continue;
				}

				foundSearchResults = true;
				itemsList->AddBrush(rawBrush);
			}
		}
	}

	okButton->Enable(foundSearchResults || selection == SearchMode::TileTypes);
	if (foundSearchResults) {
		itemsList->SetSelection(0);
	} else {
		itemsList->SetNoMatches();
	}

	itemsList->Refresh();
}

void FindItemDialog::OnOptionChange(wxCommandEvent &WXUNUSED(event)) {
	setSearchMode(static_cast<SearchMode>(optionsRadioBox->GetSelection()));
}

void FindItemDialog::OnItemIdChange(wxCommandEvent &WXUNUSED(event)) {
	RefreshContentsInternal();
}

void FindItemDialog::OnText(wxCommandEvent &WXUNUSED(event)) {
	inputTimer.Start(800, true);
}

void FindItemDialog::OnTypeChange(wxCommandEvent &WXUNUSED(event)) {
	RefreshContentsInternal();
}

void FindItemDialog::OnPropertyChange(wxCommandEvent &WXUNUSED(event)) {
	RefreshContentsInternal();
}

void FindItemDialog::OnInputTimer(wxTimerEvent &WXUNUSED(event)) {
	RefreshContentsInternal();
}

void FindItemDialog::OnClickOK(wxCommandEvent &WXUNUSED(event)) {
	if (itemsList->GetItemCount() != 0 && !tileTypesRadioBox->IsEnabled()) {
		Brush* brush = itemsList->GetSelectedBrush();
		if (brush) {
			resultBrush = brush;
			resultId = brush->asRaw()->getItemID();
			EndModal(wxID_OK);
			if (!onSelection) {
				g_gui.SelectBrush(brush->asRaw(), TILESET_RAW);
			}
		}
	} else if (tileTypesRadioBox->IsEnabled()) {
		resultBrush = nullptr;
		resultId = 0;
		EndModal(wxID_OK);
	}
}

void FindItemDialog::OnClickCancel(wxCommandEvent &WXUNUSED(event)) {
	EndModal(wxID_CANCEL);
}
