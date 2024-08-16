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

#include "map.h"
#include "gui.h"
#include "raw_brush.h"
#include "tile.h"
#include "graphics.h"
#include "gui.h"
#include "browse_tile_window.h"
#include "properties_window.h"
#include "old_properties_window.h"

// ============================================================================
//

BEGIN_EVENT_TABLE(BrowseTileListBox, wxVListBox)
EVT_LISTBOX_DCLICK(wxID_ANY, BrowseTileListBox::OnItemDoubleClick)
END_EVENT_TABLE()

BrowseTileListBox::BrowseTileListBox(wxWindow* parent, wxWindowID id, Tile* tile) :
	wxVListBox(parent, id, wxDefaultPosition, wxSize(200, 180), wxLB_MULTIPLE), editTile(tile) {
	UpdateItems();
}

void BrowseTileListBox::OnDrawItem(wxDC &dc, const wxRect &rect, size_t index) const {
	const auto itemIterator = items.find(int(index));
	const auto item = itemIterator->second;

	if (!item) {
		return;
	}

	const auto sprite = g_gui.gfx.getSprite(item->getClientID());
	if (sprite) {
		sprite->DrawTo(&dc, SPRITE_SIZE_32x32, rect.GetX(), rect.GetY(), rect.GetWidth(), rect.GetHeight());
	}

	if (IsSelected(index)) {
		item->select();
		const auto color = HasFocus() ? wxColor(0xFF, 0xFF, 0xFF) : wxColor(0x00, 0x00, 0xFF);
		dc.SetTextForeground(color);
	} else {
		item->deselect();
		dc.SetTextForeground(wxColor(0x00, 0x00, 0x00));
	}

	const auto label = wxString::Format("%d - %s", item->getID(), item->getName());
	dc.DrawText(label, rect.GetX() + 40, rect.GetY() + 6);
}

wxCoord BrowseTileListBox::OnMeasureItem(size_t n) const {
	return 32;
}

Item* BrowseTileListBox::GetSelectedItem() {
	if (GetItemCount() == 0 || GetSelectedCount() == 0) {
		return nullptr;
	}

	return editTile->getTopSelectedItem();
}

void BrowseTileListBox::RemoveSelected() {
	if (GetItemCount() == 0 || GetSelectedCount() == 0) {
		return;
	}

	Clear();
	items.clear();

	// Delete the items from the tile
	auto tileSelection = editTile->popSelectedItems(true);
	for (auto it = tileSelection.begin(); it != tileSelection.end(); ++it) {
		delete *it;
	}

	UpdateItems();
	Refresh();
}

void BrowseTileListBox::UpdateItems() {
	int index = 0;
	for (ItemVector::reverse_iterator it = editTile->items.rbegin(); it != editTile->items.rend(); ++it) {
		items[index] = (*it);
		++index;
	}

	if (editTile->ground) {
		items[index] = editTile->ground;
		++index;
	}

	SetItemCount(index);
}

void BrowseTileListBox::OpenPropertiesWindow(int index) {
	const auto* currentMap = &g_gui.GetCurrentEditor()->getMap();

	wxDialog* dialog;
	if (currentMap->getVersion().otbm >= MAP_OTBM_4) {
		dialog = newd PropertiesWindow(g_gui.root, currentMap, editTile, items[index]);
	} else {
		dialog = newd OldPropertiesWindow(g_gui.root, currentMap, editTile, items[index]);
	}
	dialog->ShowModal();
	dialog->Destroy();
}

void BrowseTileListBox::OnItemDoubleClick(wxCommandEvent &evt) {
	const auto index = evt.GetSelection();
	if (index == wxNOT_FOUND) {
		return;
	}

	OpenPropertiesWindow(index);
}

// ============================================================================
//

BEGIN_EVENT_TABLE(BrowseTileWindow, wxDialog)
EVT_BUTTON(wxID_REMOVE, BrowseTileWindow::OnClickDelete)
EVT_BUTTON(wxID_FIND, BrowseTileWindow::OnClickSelectRaw)
EVT_BUTTON(wxID_ABOUT, BrowseTileWindow::OnClickProperties)
EVT_BUTTON(wxID_OK, BrowseTileWindow::OnClickOK)
EVT_BUTTON(wxID_CANCEL, BrowseTileWindow::OnClickCancel)
EVT_BUTTON(wxID_UP, BrowseTileWindow::OnButtonUpClick)
EVT_BUTTON(wxID_DOWN, BrowseTileWindow::OnButtonDownClick)
END_EVENT_TABLE()

BrowseTileWindow::BrowseTileWindow(wxWindow* parent, Tile* tile, wxPoint position /* = wxDefaultPosition */) :
	wxDialog(parent, wxID_ANY, "Browse Field", position, wxSize(600, 400), wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER) {
	const auto sizer = newd wxBoxSizer(wxVERTICAL);
	itemList = newd BrowseTileListBox(this, wxID_ANY, tile);
	sizer->Add(itemList, wxSizerFlags(1).Expand());

	const auto infoSizer = newd wxBoxSizer(wxVERTICAL);

	AddTopOrderButtons(infoSizer);
	infoSizer->AddSpacer(5);

	AddActionButtons(infoSizer);
	infoSizer->AddSpacer(5);

	AddInformations(infoSizer);

	sizer->Add(infoSizer, wxSizerFlags(0).Center().DoubleBorder());

	// OK/Cancel buttons
	const auto btnSizer = newd wxBoxSizer(wxHORIZONTAL);
	btnSizer->Add(newd wxButton(this, wxID_OK, "OK"), wxSizerFlags(0).Center());
	btnSizer->Add(newd wxButton(this, wxID_CANCEL, "Cancel"), wxSizerFlags(0).Center());
	sizer->Add(btnSizer, wxSizerFlags(0).Center().DoubleBorder());

	SetSizerAndFit(sizer);

	// Bind Events
	itemList->Bind(wxEVT_COMMAND_LISTBOX_SELECTED, &BrowseTileWindow::OnItemSelected, this);
}

BrowseTileWindow::~BrowseTileWindow() {
	// Unbind Events
	itemList->Unbind(wxEVT_COMMAND_LISTBOX_SELECTED, &BrowseTileWindow::OnItemSelected, this);
}

void BrowseTileWindow::AddTopOrderButtons(wxSizer* sizer) {
	const auto topOrderButtons = newd wxBoxSizer(wxHORIZONTAL);

	upButton = newd wxButton(this, wxID_UP, "Up /\\");
	upButton->Enable(false);
	topOrderButtons->Add(upButton);

	topOrderButtons->AddSpacer(5);

	downButton = newd wxButton(this, wxID_DOWN, "Down \\/");
	downButton->Enable(false);
	topOrderButtons->Add(downButton);

	sizer->Add(topOrderButtons, wxSizerFlags(0).Center());
}

void BrowseTileWindow::AddActionButtons(wxSizer* sizer) {
	const auto actionButtons = newd wxBoxSizer(wxHORIZONTAL);

	deleteButton = newd wxButton(this, wxID_REMOVE, "Delete");
	deleteButton->Enable(false);
	actionButtons->Add(deleteButton);

	actionButtons->AddSpacer(5);

	selectRawButton = newd wxButton(this, wxID_FIND, "Select RAW");
	selectRawButton->Enable(false);
	actionButtons->Add(selectRawButton);

	actionButtons->AddSpacer(5);

	propertiesButton = newd wxButton(this, wxID_ABOUT, "Properties");
	propertiesButton->Enable(false);

	actionButtons->Add(propertiesButton);

	sizer->Add(actionButtons);
}

void BrowseTileWindow::AddInformations(wxSizer* sizer) {
	const auto tile = itemList->GetTile();
	const auto positionString = wxString::Format("x=%i, y=%i, z=%i", tile->getX(), tile->getY(), tile->getZ());

	sizer->Add(newd wxStaticText(this, wxID_ANY, wxString::Format("Position: %s", positionString)), wxSizerFlags(0).Left());
	sizer->Add(itemCountText = newd wxStaticText(this, wxID_ANY, wxString::Format("Item count: %i", itemList->GetItemCount())), wxSizerFlags(0).Left());
	sizer->Add(newd wxStaticText(this, wxID_ANY, wxString::Format("Protection zone: %s", b2yn(tile->isPZ()))), wxSizerFlags(0).Left());
	sizer->Add(newd wxStaticText(this, wxID_ANY, wxString::Format("No PvP: %s", b2yn(tile->getMapFlags() & TILESTATE_NOPVP))), wxSizerFlags(0).Left());
	sizer->Add(newd wxStaticText(this, wxID_ANY, wxString::Format("No logout: %s", b2yn(tile->getMapFlags() & TILESTATE_NOLOGOUT))), wxSizerFlags(0).Left());
	sizer->Add(newd wxStaticText(this, wxID_ANY, wxString::Format("PvP zone: %s", b2yn(tile->getMapFlags() & TILESTATE_PVPZONE))), wxSizerFlags(0).Left());
	sizer->Add(newd wxStaticText(this, wxID_ANY, wxString::Format("House: %s", b2yn(tile->isHouseTile()))), wxSizerFlags(0).Left());
}

void BrowseTileWindow::UpdateButtons(int selection) {
	const auto count = itemList->GetSelectedCount();
	const auto tile = itemList->GetTile();
	const auto itemsAmount = itemList->GetItemCount();
	const auto items = itemList->GetItems();
	const auto nextItemIsGround = itemsAmount - 1 == selection ? false : items.at(selection + 1) == tile->ground;

	deleteButton->Enable(count != 0);
	selectRawButton->Enable(count == 1);
	propertiesButton->Enable(count == 1);
	upButton->Enable(count == 1 && selection != 0 && items.at(selection) != tile->ground);
	downButton->Enable(count == 1 && selection != itemsAmount - 1 && !nextItemIsGround);
}

void BrowseTileWindow::OnItemSelected(wxCommandEvent &evt) {
	UpdateButtons(evt.GetSelection());
	const auto count = itemList->GetSelectedCount();
}

void BrowseTileWindow::OnClickDelete(wxCommandEvent &WXUNUSED(event)) {
	itemList->RemoveSelected();
	itemCountText->SetLabelText(wxString::Format("Item count: %i", itemList->GetItemCount()));
}

void BrowseTileWindow::OnClickSelectRaw(wxCommandEvent &WXUNUSED(event)) {
	const auto item = itemList->GetSelectedItem();
	if (item && item->getRAWBrush()) {
		g_gui.SelectBrush(item->getRAWBrush(), TILESET_RAW);
	}

	EndModal(1);
}

void BrowseTileWindow::OnClickProperties(wxCommandEvent &evt) {
	const auto index = itemList->GetSelection();
	if (index == wxNOT_FOUND) {
		return;
	}

	itemList->OpenPropertiesWindow(index);
}

void BrowseTileWindow::OnClickOK(wxCommandEvent &WXUNUSED(event)) {
	EndModal(1);
}

void BrowseTileWindow::OnClickCancel(wxCommandEvent &WXUNUSED(event)) {
	EndModal(0);
}

void BrowseTileWindow::ChangeItemIndex(bool up /* = true*/) {
	const auto i = up ? 1 : -1;
	const auto tile = itemList->GetTile();

	const auto selectedItemIndex = itemList->GetSelection();
	const auto tileItemsSize = tile->items.size() - 1;
	auto index = tileItemsSize - selectedItemIndex;

	const auto tmpItem = tile->items[index];
	tile->items[index] = tile->items[index + i];
	tile->items[index + i] = tmpItem;

	itemList->UpdateItems();

	itemList->DeselectAll();
	itemList->SetSelection(selectedItemIndex - i);
	UpdateButtons(selectedItemIndex - i);
	Refresh();
}

void BrowseTileWindow::OnButtonUpClick(wxCommandEvent &evt) {
	ChangeItemIndex();
}

void BrowseTileWindow::OnButtonDownClick(wxCommandEvent &evt) {
	ChangeItemIndex(false);
}
