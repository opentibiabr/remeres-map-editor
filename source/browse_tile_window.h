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

#ifndef RME_BROWSE_TILE_WINDOW_H_
#define RME_BROWSE_TILE_WINDOW_H_

#include "main.h"
#include "map.h"
#include "tile.h"

using ItemsMap = std::map<int, Item*>;

class BrowseTileListBox : public wxVListBox {
public:
	BrowseTileListBox(wxWindow* parent, wxWindowID id, Tile* tile);
	~BrowseTileListBox() = default;

	void OnDrawItem(wxDC &dc, const wxRect &rect, size_t index) const;
	wxCoord OnMeasureItem(size_t index) const;
	Item* GetSelectedItem();
	void RemoveSelected();
	void OnItemDoubleClick(wxCommandEvent &);

	Tile* GetTile() const noexcept {
		return editTile;
	}

	ItemsMap GetItems() const noexcept {
		return items;
	}

	void OpenPropertiesWindow(int index);

	void UpdateItems();

protected:
	ItemsMap items;
	Tile* editTile = nullptr;

	DECLARE_EVENT_TABLE();
};

class BrowseTileWindow : public wxDialog {
public:
	BrowseTileWindow(wxWindow* parent, Tile* tile, wxPoint position = wxDefaultPosition);
	~BrowseTileWindow();

	void OnItemSelected(wxCommandEvent &);
	void OnClickDelete(wxCommandEvent &);
	void OnClickSelectRaw(wxCommandEvent &);
	void OnClickProperties(wxCommandEvent &);
	void OnClickOK(wxCommandEvent &);
	void OnClickCancel(wxCommandEvent &);
	void OnButtonUpClick(wxCommandEvent &);
	void OnButtonDownClick(wxCommandEvent &);

	void ChangeItemIndex(bool up = true);
	void UpdateButtons(int selection);

protected:
	void AddTopOrderButtons(wxSizer* sizer);
	void AddActionButtons(wxSizer* sizer);
	void AddInformations(wxSizer* sizer);

	friend class BrowseTileListBox;
	BrowseTileListBox* itemList = nullptr;
	wxStaticText* itemCountText = nullptr;
	wxButton* deleteButton = nullptr;
	wxButton* selectRawButton = nullptr;
	wxButton* propertiesButton = nullptr;
	wxButton* upButton = nullptr;
	wxButton* downButton = nullptr;

	DECLARE_EVENT_TABLE();
};

#endif
