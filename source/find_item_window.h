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

#ifndef RME_FIND_ITEM_WINDOW_H_
#define RME_FIND_ITEM_WINDOW_H_

#include <wx/radiobox.h>
#include <wx/spinctrl.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>
#include <wx/button.h>
#include <wx/dialog.h>

class FindDialogListBox;

class FindItemDialog : public wxDialog {
public:
	enum SearchMode {
		ItemIDs = 0,
		Names,
		Types,
		TileTypes,
		Properties,
	};

	enum SearchTileType {
		ProtectionZone,
		PlayerVsPlayer,
		NoPlayerVsPlayer,
		NoLogout
	};

	enum SearchItemType {
		Depot,
		Mailbox,
		TrashHolder,
		Container,
		Door,
		MagicField,
		Teleport,
		Bed,
		Key
	};

	FindItemDialog(wxWindow* parent, const wxString &title, bool onlyPickupables = false, bool onSelection = false);
	~FindItemDialog();

	Brush* getResult() const noexcept {
		return resultBrush;
	}
	uint16_t getResultID() const noexcept {
		return resultId;
	}

	SearchMode getSearchMode() const;
	SearchTileType getSearchTileType() const;
	void setSearchMode(SearchMode mode);

private:
	void EnableProperties(bool enable);
	void RefreshContentsInternal();

	void OnOptionChange(wxCommandEvent &event);
	void OnItemIdChange(wxCommandEvent &event);
	void OnText(wxCommandEvent &event);
	void OnTypeChange(wxCommandEvent &event);
	void OnPropertyChange(wxCommandEvent &event);
	void OnInputTimer(wxTimerEvent &event);
	void OnClickOK(wxCommandEvent &event);
	void OnClickCancel(wxCommandEvent &event);

	wxRadioBox* optionsRadioBox;

	wxRadioBox* typesRadioBox;
	wxRadioBox* tileTypesRadioBox;

	wxSpinCtrl* itemIdSpin;
	wxTextCtrl* nameTextInput;
	wxTimer inputTimer;
	wxCheckBox* unpassable;
	wxCheckBox* unmovable;
	wxCheckBox* blockMissiles;
	wxCheckBox* blockPathfinder;
	wxCheckBox* readable;
	wxCheckBox* writeable;
	wxCheckBox* pickupable;
	wxCheckBox* stackable;
	wxCheckBox* rotatable;
	wxCheckBox* hangable;
	wxCheckBox* hookEast;
	wxCheckBox* hookSouth;
	wxCheckBox* hasElevation;
	wxCheckBox* ignoreLook;
	wxCheckBox* floorChange;

	FindDialogListBox* itemsList;
	wxStdDialogButtonSizer* buttonsBoxSizer;
	wxButton* okButton;
	wxButton* cancelButton;
	Brush* resultBrush = nullptr;
	uint16_t resultId = 0;
	bool onlyPickupables = false;
	bool onSelection = false;

	DECLARE_EVENT_TABLE()
};

#endif // RME_FIND_ITEM_WINDOW_H_
