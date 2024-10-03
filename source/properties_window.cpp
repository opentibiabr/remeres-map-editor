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

#include "properties_window.h"

#include "gui_ids.h"
#include "complexitem.h"
#include "container_properties_window.h"

const wxArrayString PropertiesWindow::types = {
	"Number",
	"Float",
	"Boolean",
	"String"
};

BEGIN_EVENT_TABLE(PropertiesWindow, wxDialog)
EVT_BUTTON(wxID_OK, PropertiesWindow::OnClickOK)
EVT_BUTTON(wxID_CANCEL, PropertiesWindow::OnClickCancel)
EVT_CHOICE(wxDEPOT_CTRL, PropertiesWindow::OnDepotChoice)
EVT_CHOICE(wxLIQUID_CTRL, PropertiesWindow::OnLiquidChoice)

EVT_BUTTON(ITEM_PROPERTIES_ADD_ATTRIBUTE, PropertiesWindow::OnClickAddAttribute)
EVT_BUTTON(ITEM_PROPERTIES_REMOVE_ATTRIBUTE, PropertiesWindow::OnClickRemoveAttribute)

EVT_SPINCTRL(wxID_ANY, PropertiesWindow::OnSpinArrowAttributeUpdate)

EVT_NOTEBOOK_PAGE_CHANGED(wxID_ANY, PropertiesWindow::OnNotebookPageChanged)

EVT_GRID_CELL_CHANGED(PropertiesWindow::OnGridValueChanged)
END_EVENT_TABLE()

PropertiesWindow::PropertiesWindow(wxWindow* parent, const Map* map, const Tile* tile_parent, Item* item, wxPoint pos) :
	ObjectPropertiesWindowBase(parent, "Item Properties", map, tile_parent, item, pos),
	currentPanel(nullptr) {
	ASSERT(edit_item);
	notebook = newd wxNotebook(this, wxID_ANY, wxDefaultPosition, wxSize(600, 300));

	notebook->AddPage(createGeneralPanel(notebook), "Simple", true);
	if (dynamic_cast<Container*>(item)) {
		notebook->AddPage(createContainerPanel(notebook), "Contents");
	}
	notebook->AddPage(createAttributesPanel(notebook), "Advanced");

	wxSizer* topSizer = newd wxBoxSizer(wxVERTICAL);
	topSizer->Add(notebook, wxSizerFlags(1).DoubleBorder());

	wxSizer* optSizer = newd wxBoxSizer(wxHORIZONTAL);
	optSizer->Add(newd wxButton(this, wxID_OK, "OK"), wxSizerFlags(0).Center());
	optSizer->Add(newd wxButton(this, wxID_CANCEL, "Cancel"), wxSizerFlags(0).Center());
	topSizer->Add(optSizer, wxSizerFlags(0).Center().DoubleBorder());

	SetSizerAndFit(topSizer);
	Centre(wxBOTH);
}

PropertiesWindow::~PropertiesWindow() {
	if (destinationXField) {
		destinationXField->Unbind(wxEVT_TEXT, &PropertiesWindow::OnTextPosition, this);
		destinationXField->Unbind(wxEVT_KILL_FOCUS, &PropertiesWindow::OnKillFocus, this);
		destinationXField->Unbind(wxEVT_TEXT_PASTE, &PropertiesWindow::OnClipboardText, this);
	}
	if (destinationYField) {
		destinationYField->Unbind(wxEVT_TEXT, &PropertiesWindow::OnTextPosition, this);
		destinationYField->Unbind(wxEVT_KILL_FOCUS, &PropertiesWindow::OnKillFocus, this);
		destinationYField->Unbind(wxEVT_TEXT_PASTE, &PropertiesWindow::OnClipboardText, this);
	}
	if (destinationZField) {
		destinationZField->Unbind(wxEVT_TEXT, &PropertiesWindow::OnTextPosition, this);
		destinationZField->Unbind(wxEVT_KILL_FOCUS, &PropertiesWindow::OnKillFocus, this);
		destinationZField->Unbind(wxEVT_TEXT_PASTE, &PropertiesWindow::OnClipboardText, this);
	}
}

void PropertiesWindow::Update() {
	Container* container = dynamic_cast<Container*>(edit_item);
	if (container) {
		for (uint32_t i = 0; i < container->getVolume(); ++i) {
			container_items[i]->setItem(container->getItem(i));
		}
	}
	wxDialog::Update();
}

void PropertiesWindow::SetAdvancedPropertyNumberData(const wxString &attributeName, int value) {
	const auto rowsCount = attributesGrid->GetNumberRows();

	bool found = false;

	if (rowsCount > 0) {
		for (int32_t rowIndex = 0; rowIndex < rowsCount; ++rowIndex) {
			wxString attributeKey = attributesGrid->GetCellValue(rowIndex, 0);
			if (strcmp(attributeKey, attributeName.c_str()) == 0) {
				attributesGrid->SetCellValue(rowIndex, 1, "Number");
				attributesGrid->SetCellValue(rowIndex, 2, wxString::Format("%i", value));
				found = true;
			}
		}
	}

	if (!found) {
		attributesGrid->InsertRows(0, 1);
		attributesGrid->SetCellValue(0, 0, attributeName);
		attributesGrid->SetCellValue(0, 1, "Number");
		attributesGrid->SetCellValue(0, 2, wxString::Format("%i", value));
	}
}

void PropertiesWindow::createTeleportDestinationCtrl(wxPanel* panel, wxFlexGridSizer* gridsizer) {
	if (const auto teleport = edit_item->getTeleport()) {

		wxSizer* subGridSizer = newd wxBoxSizer(wxHORIZONTAL);

		gridsizer->Add(newd wxStaticText(panel, wxID_ANY, "Destination"));

		destinationXField = newd NumberTextCtrl(panel, wxX_DESTINATION_CTRL, teleport->getX(), 0, edit_map->getWidth(), wxTE_PROCESS_ENTER, "X", wxDefaultPosition, wxSize(60, 20));
		subGridSizer->Add(destinationXField, 2, wxEXPAND | wxLEFT | wxBOTTOM, 5);
		destinationXField->Bind(wxEVT_TEXT, &PropertiesWindow::OnTextPosition, this);
		destinationXField->Bind(wxEVT_KILL_FOCUS, &PropertiesWindow::OnKillFocus, this);
		destinationXField->Bind(wxEVT_TEXT_PASTE, &PropertiesWindow::OnClipboardText, this);

		destinationYField = newd NumberTextCtrl(panel, wxY_DESTINATION_CTRL, teleport->getY(), 0, edit_map->getHeight(), wxTE_PROCESS_ENTER, "Y", wxDefaultPosition, wxSize(60, 20));
		subGridSizer->Add(destinationYField, 2, wxEXPAND | wxLEFT | wxBOTTOM, 5);
		destinationYField->Bind(wxEVT_TEXT, &PropertiesWindow::OnTextPosition, this);
		destinationYField->Bind(wxEVT_KILL_FOCUS, &PropertiesWindow::OnKillFocus, this);
		destinationXField->Bind(wxEVT_TEXT_PASTE, &PropertiesWindow::OnClipboardText, this);

		destinationZField = newd NumberTextCtrl(panel, wxZ_DESTINATION_CTRL, teleport->getZ(), 0, rme::MapMaxLayer, wxTE_PROCESS_ENTER, "Z", wxDefaultPosition, wxSize(60, 20));
		subGridSizer->Add(destinationZField, 1, wxEXPAND | wxLEFT | wxBOTTOM, 5);
		destinationZField->Bind(wxEVT_TEXT, &PropertiesWindow::OnTextPosition, this);
		destinationZField->Bind(wxEVT_KILL_FOCUS, &PropertiesWindow::OnKillFocus, this);
		destinationXField->Bind(wxEVT_TEXT_PASTE, &PropertiesWindow::OnClipboardText, this);
		gridsizer->Add(subGridSizer);
	}
}

void PropertiesWindow::createDoorIdCtrl(wxPanel* panel, wxFlexGridSizer* gridsizer) {
	if (const auto door = edit_item->getDoor()) {
		gridsizer->Add(newd wxStaticText(panel, wxID_ANY, "Door ID"));
		doorIdField = newd wxSpinCtrl(panel, wxDOOR_CTRL, i2ws(door->getDoorID()), wxDefaultPosition, wxSize(-1, 20), wxSP_ARROW_KEYS, 0, 0xFFFF, door->getDoorID());
		if (!edit_tile->isHouseTile()) {
			doorIdField->Disable();
		}
		gridsizer->Add(doorIdField, wxSizerFlags(1).Expand());
	}
}

void PropertiesWindow::createDepotIdChoiceCtrl(wxPanel* panel, wxFlexGridSizer* gridsizer) {
	if (const auto depot = edit_item->getDepot()) {
		const auto &towns = edit_map->towns;
		gridsizer->Add(newd wxStaticText(panel, wxID_ANY, "Depot ID"));
		depotIdField = newd wxChoice(panel, wxDEPOT_CTRL);
		int selectIndex = 0;
		if (towns.count() > 0) {
			bool found = false;
			for (auto townIt = towns.begin(); townIt != towns.end(); ++townIt) {
				if (townIt->second->getID() == depot->getDepotID()) {
					found = true;
				}
				depotIdField->Append(wxstr(townIt->second->getName()), newd int(townIt->second->getID()));
				if (!found) {
					++selectIndex;
				}
			}
			if (!found) {
				if (depot->getDepotID() != 0) {
					depotIdField->Append("Undefined Town (id:" + i2ws(depot->getDepotID()) + ")", newd int(depot->getDepotID()));
				}
			}
		}
		depotIdField->Append("No Town", newd int(0));
		if (depot->getDepotID() == 0) {
			selectIndex = depotIdField->GetCount() - 1;
		}
		depotIdField->SetSelection(selectIndex);

		gridsizer->Add(depotIdField, wxSizerFlags(1).Expand());
	}
}

void PropertiesWindow::createLiquidChoiceCtrl(wxPanel* panel, wxFlexGridSizer* gridsizer) {
	if (edit_item->isSplash() || edit_item->isFluidContainer()) {
		gridsizer->Add(newd wxStaticText(panel, wxID_ANY, "Fluid Type"));

		const auto liquidNoneName = wxstr(Item::LiquidID2Name(LIQUID_NONE));
		const auto liquidNoneUInt = newd uint8_t(LIQUID_NONE);

		liquidTypeField = newd wxChoice(panel, wxLIQUID_CTRL);

		if (edit_item->isFluidContainer()) {
			liquidTypeField->Append(liquidNoneName, liquidNoneUInt);
		}

		for (SplashType splashType = LIQUID_FIRST; splashType <= LIQUID_LAST; ++splashType) {
			const auto splashTypeName = wxstr(Item::LiquidID2Name(splashType));
			if (splashTypeName != "Unknown") {
				liquidTypeField->Append(splashTypeName, newd uint8_t(splashType));
			}
		}

		if (edit_item->getSubtype()) {
			const std::string &what = Item::LiquidID2Name(edit_item->getSubtype());
			if (what == "Unknown") {
				liquidTypeField->Append(what, newd uint8_t(LIQUID_NONE));
			}
			liquidTypeField->SetStringSelection(what);
		} else {
			liquidTypeField->SetSelection(0);
		}

		gridsizer->Add(liquidTypeField, wxSizerFlags(1).Expand());
	}
}

wxWindow* PropertiesWindow::createGeneralPanel(wxWindow* parent) {
	wxPanel* panel = newd wxPanel(parent, ITEM_PROPERTIES_GENERAL_TAB);
	wxFlexGridSizer* gridsizer = newd wxFlexGridSizer(2, 10, 10);
	gridsizer->AddGrowableCol(1);

	gridsizer->Add(newd wxStaticText(panel, wxID_ANY, "ID " + i2ws(edit_item->getID())));
	gridsizer->Add(newd wxStaticText(panel, wxID_ANY, "\"" + wxstr(edit_item->getName()) + "\""));

	gridsizer->Add(newd wxStaticText(panel, wxID_ANY, "Action ID"));
	simpleActionIdField = newd wxSpinCtrl(panel, wxAID_CTRL, i2ws(edit_item->getActionID()), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 0xFFFF, edit_item->getActionID());
	gridsizer->Add(simpleActionIdField, wxSizerFlags(1).Expand());

	gridsizer->Add(newd wxStaticText(panel, wxID_ANY, "Unique ID"));
	simpleUniqueIdField = newd wxSpinCtrl(panel, wxUID_CTRL, i2ws(edit_item->getUniqueID()), wxDefaultPosition, wxSize(-1, 20), wxSP_ARROW_KEYS, 0, 0xFFFF, edit_item->getUniqueID());
	gridsizer->Add(simpleUniqueIdField, wxSizerFlags(1).Expand());

	createDepotIdChoiceCtrl(panel, gridsizer);
	createDoorIdCtrl(panel, gridsizer);
	createTeleportDestinationCtrl(panel, gridsizer);
	createLiquidChoiceCtrl(panel, gridsizer);

	panel->SetSizerAndFit(gridsizer);

	return panel;
}

wxWindow* PropertiesWindow::createContainerPanel(wxWindow* parent) {
	Container* container = (Container*)edit_item;
	wxPanel* panel = newd wxPanel(parent, ITEM_PROPERTIES_CONTAINER_TAB);
	wxSizer* topSizer = newd wxBoxSizer(wxVERTICAL);

	wxSizer* gridSizer = newd wxGridSizer(6, 5, 5);

	bool use_large_sprites = g_settings.getBoolean(Config::USE_LARGE_CONTAINER_ICONS);
	for (uint32_t i = 0; i < container->getVolume(); ++i) {
		Item* item = container->getItem(i);
		ContainerItemButton* containerItemButton = newd ContainerItemButton(panel, use_large_sprites, i, edit_map, item);

		container_items.push_back(containerItemButton);
		gridSizer->Add(containerItemButton, wxSizerFlags(0));
	}

	topSizer->Add(gridSizer, wxSizerFlags(1).Expand());

	/*
	wxSizer* optSizer = newd wxBoxSizer(wxHORIZONTAL);
	optSizer->Add(newd wxButton(panel, ITEM_PROPERTIES_ADD_ATTRIBUTE, "Add Item"), wxSizerFlags(0).Center());
	// optSizer->Add(newd wxButton(panel, ITEM_PROPERTIES_REMOVE_ATTRIBUTE, "Remove Attribute"), wxSizerFlags(0).Center());
	topSizer->Add(optSizer, wxSizerFlags(0).Center().DoubleBorder());
	*/

	panel->SetSizer(topSizer);
	return panel;
}

wxWindow* PropertiesWindow::createAttributesPanel(wxWindow* parent) {
	wxPanel* panel = newd wxPanel(parent, wxID_ANY);
	wxSizer* topSizer = newd wxBoxSizer(wxVERTICAL);

	attributesGrid = newd wxGrid(panel, ITEM_PROPERTIES_ADVANCED_TAB, wxDefaultPosition, wxSize(-1, 160));
	topSizer->Add(attributesGrid, wxSizerFlags(1).Expand());

	wxFont time_font(*wxSWISS_FONT);
	attributesGrid->SetDefaultCellFont(time_font);
	attributesGrid->CreateGrid(0, 3);
	attributesGrid->DisableDragRowSize();
	attributesGrid->DisableDragColSize();
	attributesGrid->SetSelectionMode(wxGrid::wxGridSelectRows);
	attributesGrid->SetRowLabelSize(0);
	// log->SetColLabelSize(0);
	// log->EnableGridLines(false);
	attributesGrid->EnableEditing(true);

	attributesGrid->SetColLabelValue(0, "Key");
	attributesGrid->SetColSize(0, 100);
	attributesGrid->SetColLabelValue(1, "Type");
	attributesGrid->SetColSize(1, 80);
	attributesGrid->SetColLabelValue(2, "Value");
	attributesGrid->SetColSize(2, 410);

	// contents
	ItemAttributeMap attrs = edit_item->getAttributes();
	attributesGrid->AppendRows(attrs.size());
	int i = 0;
	for (ItemAttributeMap::iterator aiter = attrs.begin(); aiter != attrs.end(); ++aiter, ++i) {
		SetGridValue(attributesGrid, i, aiter->first, aiter->second);
	}

	wxSizer* optSizer = newd wxBoxSizer(wxHORIZONTAL);
	optSizer->Add(newd wxButton(panel, ITEM_PROPERTIES_ADD_ATTRIBUTE, "Add Attribute"), wxSizerFlags(0).Center());
	optSizer->Add(newd wxButton(panel, ITEM_PROPERTIES_REMOVE_ATTRIBUTE, "Remove Attribute"), wxSizerFlags(0).Center());
	topSizer->Add(optSizer, wxSizerFlags(0).Center().DoubleBorder());

	panel->SetSizer(topSizer);

	return panel;
}

void PropertiesWindow::SetGridValue(wxGrid* grid, int rowIndex, std::string label, const ItemAttribute &attr) {
	grid->SetCellValue(rowIndex, 0, label);
	switch (attr.type) {
		case ItemAttribute::STRING: {
			grid->SetCellValue(rowIndex, 1, "String");
			grid->SetCellValue(rowIndex, 2, wxstr(*attr.getString()));
			grid->SetCellRenderer(rowIndex, 2, new wxGridCellStringRenderer);
			grid->SetCellEditor(rowIndex, 2, new wxGridCellTextEditor);
			break;
		}
		case ItemAttribute::INTEGER: {
			grid->SetCellValue(rowIndex, 1, "Number");
			grid->SetCellValue(rowIndex, 2, i2ws(*attr.getInteger()));
			grid->SetCellRenderer(rowIndex, 2, new wxGridCellNumberRenderer);
			grid->SetCellEditor(rowIndex, 2, new wxGridCellNumberEditor);
			break;
		}
		case ItemAttribute::DOUBLE:
		case ItemAttribute::FLOAT: {
			grid->SetCellValue(rowIndex, 1, "Float");
			const float value = *attr.getFloat();
			grid->SetCellValue(rowIndex, 2, wxString::Format("%f", value ? value : 0.0));
			grid->SetCellRenderer(rowIndex, 2, new wxGridCellFloatRenderer);
			grid->SetCellEditor(rowIndex, 2, new wxGridCellFloatEditor);
			break;
		}
		case ItemAttribute::BOOLEAN: {
			grid->SetCellValue(rowIndex, 1, "Boolean");
			grid->SetCellValue(rowIndex, 2, *attr.getBoolean() ? "1" : "");
			grid->SetCellRenderer(rowIndex, 2, new wxGridCellBoolRenderer);
			grid->SetCellEditor(rowIndex, 2, new wxGridCellBoolEditor);
			break;
		}
		default: {
			grid->SetCellValue(rowIndex, 1, "Unknown");
			grid->SetCellBackgroundColour(rowIndex, 1, *wxLIGHT_GREY);
			grid->SetCellBackgroundColour(rowIndex, 2, *wxLIGHT_GREY);
			grid->SetReadOnly(rowIndex, 1, true);
			grid->SetReadOnly(rowIndex, 2, true);
			break;
		}
	}
	grid->SetCellAlignment(rowIndex, 2, 2, 0);
	grid->SetCellEditor(rowIndex, 1, new wxGridCellChoiceEditor(types));
}

void PropertiesWindow::OnResize(wxSizeEvent &evt) {
	/*
	if(wxGrid* grid = (wxGrid*)currentPanel->FindWindowByName("AdvancedGrid")) {
		int tWidth = 0;
		for(int i = 0; i < 3; ++i)
			tWidth += grid->GetColumnWidth(i);

		int wWidth = grid->GetParent()->GetSize().GetWidth();

		grid->SetColumnWidth(2, wWidth - 100 - 80);
	}
	*/
}

void PropertiesWindow::OnNotebookPageChanged(wxNotebookEvent &evt) {
	wxWindow* page = notebook->GetCurrentPage();

	// TODO: Save

	switch (page->GetId()) {
		case ITEM_PROPERTIES_GENERAL_TAB: {
			// currentPanel = createGeneralPanel(page);
			break;
		}
		case ITEM_PROPERTIES_ADVANCED_TAB: {
			// currentPanel = createAttributesPanel(page);
			break;
		}
		default:
			break;
	}
}

void PropertiesWindow::saveGeneralPanel() {
	////
}

void PropertiesWindow::saveContainerPanel() {
	////
}

void PropertiesWindow::setBasicAttributes() {
	const auto aid = simpleActionIdField->GetValue();
	const auto uid = simpleUniqueIdField->GetValue();

	if (aid > 0) {
		edit_item->setAttribute("aid", ItemAttribute(aid));
	}

	if (uid > 0) {
		edit_item->setAttribute("uid", ItemAttribute(uid));
	}
}

void PropertiesWindow::setTeleportAttributes(const std::string &key, int value) {
	if (!edit_item) {
		return;
	}

	setBasicAttributes();

	auto teleportItem = dynamic_cast<Teleport*>(edit_item);
	if (!teleportItem) {
		return;
	}

	auto position = teleportItem->getDestination();
	if (strcmp(key.c_str(), "destination.x") == 0) {
		position.x = value;
	} else if (strcmp(key.c_str(), "destination.y") == 0) {
		position.y = value;
	} else if (strcmp(key.c_str(), "destination.z") == 0) {
		position.z = value;
	}

	teleportItem->setDestination(position);
}

void PropertiesWindow::setDepotAttributes(const std::string &key, int value) {
	if (!edit_item) {
		return;
	}

	setBasicAttributes();

	auto depot = dynamic_cast<Depot*>(edit_item);
	if (!depot) {
		return;
	}

	if (strcmp(key.c_str(), "depotid") == 0) {
		depot->setDepotID(value);
	}
}

void PropertiesWindow::setDoorAttributes(const std::string &key, int value) {
	if (!edit_item) {
		return;
	}

	setBasicAttributes();

	auto door = dynamic_cast<Door*>(edit_item);
	if (!door) {
		return;
	}

	if (strcmp(key.c_str(), "doorid") == 0) {
		door->setDoorID(value);
	}
}

void PropertiesWindow::setLiquidAttributes(const std::string &key, int value) {
	if (!edit_item) {
		return;
	}

	setBasicAttributes();

	if (!edit_item->isSplash() && !edit_item->isFluidContainer()) {
		return;
	}

	if (strcmp(key.c_str(), "subtype") == 0) {
		edit_item->setSubtype(value);
	}
}

void PropertiesWindow::saveAttributesPanel() {
	edit_item->clearAllAttributes();

	for (int32_t rowIndex = 0; rowIndex < attributesGrid->GetNumberRows(); ++rowIndex) {
		ItemAttribute attr;
		wxString type = attributesGrid->GetCellValue(rowIndex, 1);
		const auto cellValue = attributesGrid->GetCellValue(rowIndex, 2);
		if (type == "String") {
			attr.set(nstr(cellValue));
		} else if (type == "Float") {
			double value;
			if (cellValue.ToDouble(&value)) {
				attr.set(value);
			}
		} else if (type == "Number") {
			long value;
			if (cellValue.ToLong(&value)) {
				attr.set(static_cast<int32_t>(value));
			}
		} else if (type == "Boolean") {
			attr.set(cellValue == "1");
		} else {
			continue;
		}

		const auto key = nstr(attributesGrid->GetCellValue(rowIndex, 0));

		int value;
		const auto gotValue = cellValue.ToInt(&value);

		if (dynamic_cast<Teleport*>(edit_item) && gotValue) {
			setTeleportAttributes(key, value);
		} else if (dynamic_cast<Depot*>(edit_item) && gotValue) {
			setDepotAttributes(key, value);
		} else if (dynamic_cast<Door*>(edit_item) && gotValue) {
			setDoorAttributes(key, value);
		} else if (edit_item->isSplash() || edit_item->isFluidContainer()) {
			setLiquidAttributes(key, value);
		} else {
			edit_item->setAttribute(key, attr);
		}
	}
}

void PropertiesWindow::OnGridValueChanged(wxGridEvent &event) {
	const auto attributeKey = attributesGrid->GetCellValue(event.GetRow(), 0);
	if (event.GetCol() == 1) {
		wxString newType = attributesGrid->GetCellValue(event.GetRow(), 1);
		if (newType == event.GetString()) {
			return;
		}

		ItemAttribute attr;
		if (newType == "String") {
			attr.set(std::string());
		} else if (newType == "Float") {
			attr.set(0.0f);
		} else if (newType == "Number") {
			attr.set(0);
		} else if (newType == "Boolean") {
			attr.set(false);
		}
		SetGridValue(attributesGrid, event.GetRow(), nstr(attributeKey), attr);
	} else if (event.GetCol() == 2 || event.GetCol() == 0) {
		const auto value = attributesGrid->GetCellValue(event.GetRow(), 2);
		if (strcmp(attributeKey, "aid") == 0) {
			simpleActionIdField->SetValue(value);
		} else if (strcmp(attributeKey, "uid") == 0) {
			simpleUniqueIdField->SetValue(value);
		} else if (strcmp(attributeKey, "doorid") == 0) {
			doorIdField->SetValue(value);
		} else if (strcmp(attributeKey, "destination.x") == 0) {
			destinationXField->ChangeValue(value);
		} else if (strcmp(attributeKey, "destination.y") == 0) {
			destinationYField->ChangeValue(value);
		} else if (strcmp(attributeKey, "destination.z") == 0) {
			destinationZField->ChangeValue(value);
		} else if (strcmp(attributeKey, "depotid") == 0) {
			int depotIdCellValue;
			if (value.ToInt(&depotIdCellValue)) {
				bool selected = false;
				for (auto i = 0; i < depotIdField->GetCount(); ++i) {
					const auto depotId = reinterpret_cast<int*>(depotIdField->GetClientData(i));
					if (depotId && *depotId == depotIdCellValue) {
						depotIdField->SetSelection(i);
						selected = true;
					}
				}
				if (!selected) {
					const auto unknownDepotId = wxString::Format("Undefined Town (id: %i)", depotIdCellValue);
					depotIdField->Append(unknownDepotId);
					depotIdField->SetStringSelection(unknownDepotId);
				}
			}
		} else if (strcmp(attributeKey, "subtype") == 0 && (edit_item->isSplash() || edit_item->isFluidContainer())) {
			int liquidTypeCellValue;
			if (value.ToInt(&liquidTypeCellValue)) {
				bool selected = false;
				for (auto i = 0; i < liquidTypeField->GetCount(); ++i) {
					const auto depotId = reinterpret_cast<int*>(liquidTypeField->GetClientData(i));
					if (depotId && *depotId == liquidTypeCellValue) {
						liquidTypeField->SetSelection(i);
						selected = true;
					}
				}

				if (!selected) {
					const auto unknownType = Item::LiquidID2Name(LIQUID_NONE);
					liquidTypeField->Append(unknownType, newd uint8_t(LIQUID_NONE));
					liquidTypeField->SetStringSelection(unknownType);
				}
			}
		}
	}
}

void PropertiesWindow::OnClickOK(wxCommandEvent &) {
	saveAttributesPanel();
	EndModal(1);
}

void PropertiesWindow::OnClickAddAttribute(wxCommandEvent &) {
	attributesGrid->AppendRows(1);
	ItemAttribute attr(0);
	SetGridValue(attributesGrid, attributesGrid->GetNumberRows() - 1, "", attr);
}

void PropertiesWindow::OnClickRemoveAttribute(wxCommandEvent &) {
	wxArrayInt rowIndexes = attributesGrid->GetSelectedRows();
	if (rowIndexes.Count() != 1) {
		return;
	}

	int rowIndex = rowIndexes[0];
	attributesGrid->DeleteRows(rowIndex, 1);
}

void PropertiesWindow::OnSpinArrowAttributeUpdate(wxSpinEvent &event) {
	const auto spinCtrl = dynamic_cast<wxSpinCtrl*>(event.GetEventObject());
	if (!spinCtrl) {
		return;
	}

	const auto number = event.GetInt();

	if (number < 0) {
		return;
	}

	const auto spinCtrlId = spinCtrl->GetId();

	if (spinCtrlId == wxAID_CTRL) {
		SetAdvancedPropertyNumberData("aid", number);
	} else if (spinCtrlId == wxUID_CTRL) {
		SetAdvancedPropertyNumberData("uid", number);
	} else if (spinCtrlId == wxDOOR_CTRL) {
		SetAdvancedPropertyNumberData("doorid", number);
	}
}

void PropertiesWindow::OnClickCancel(wxCommandEvent &) {
	EndModal(0);
}

void PropertiesWindow::OnClipboardText(wxClipboardTextEvent &evt) {
	if (!clipboardPositionToFields(destinationXField, destinationYField, destinationZField)) {
		evt.Skip();
	}
}

void PropertiesWindow::OnKillFocus(wxFocusEvent &evt) {
	const auto numberTextCtrl = static_cast<NumberTextCtrl*>(evt.GetEventObject());
	const auto specificPosition = numberTextCtrl->GetValue();
	const auto ctrlId = numberTextCtrl->GetId();

	numberTextCtrl->OnKillFocus(evt);

	int value;
	const auto gotValue = specificPosition.ToInt(&value);

	if (!gotValue) {
		return;
	}

	OnDestinationUpdate(ctrlId, value);
}

void PropertiesWindow::OnTextPosition(wxCommandEvent &evt) {
	const auto numberTextCtrl = static_cast<NumberTextCtrl*>(evt.GetEventObject());
	const auto specificPosition = evt.GetString();

	numberTextCtrl->EnsureOnlyNumbers(evt);

	const auto ctrlId = numberTextCtrl->GetId();

	int value;
	const auto gotValue = specificPosition.ToInt(&value);

	if (!gotValue) {
		return;
	}

	OnDestinationUpdate(ctrlId, value);
}

void PropertiesWindow::OnDestinationUpdate(const wxWindowID &ctrlId, int value) {
	if (ctrlId == wxX_DESTINATION_CTRL) {
		SetAdvancedPropertyNumberData("destination.x", value);
	} else if (ctrlId == wxY_DESTINATION_CTRL) {
		SetAdvancedPropertyNumberData("destination.y", value);
	} else if (ctrlId == wxZ_DESTINATION_CTRL) {
		SetAdvancedPropertyNumberData("destination.z", value);
	}
}

void PropertiesWindow::OnDepotChoice(wxCommandEvent &event) {
	if (!depotIdField) {
		return;
	}

	const auto newDepotId = static_cast<int*>(depotIdField->GetClientData(depotIdField->GetSelection()));

	const auto rowsCount = attributesGrid->GetNumberRows();

	SetAdvancedPropertyNumberData("depotid", *newDepotId);
}

void PropertiesWindow::OnLiquidChoice(wxCommandEvent &event) {
	if (!liquidTypeField) {
		return;
	}

	const auto newLiquidType = static_cast<uint8_t*>(liquidTypeField->GetClientData(liquidTypeField->GetSelection()));

	const auto rowsCount = attributesGrid->GetNumberRows();

	SetAdvancedPropertyNumberData("subtype", *newLiquidType);
}
