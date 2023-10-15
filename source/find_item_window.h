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
		ServerIDs = 0,
		ClientIDs,
		Names,
		Types,
		Properties,
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

	FindItemDialog(wxWindow* parent, const wxString &title, bool onlyPickupables = false);
	~FindItemDialog();

	Brush* getResult() const {
		return result_brush;
	}
	uint16_t getResultID() const {
		return result_id;
	}

	SearchMode getSearchMode() const;
	void setSearchMode(SearchMode mode);

private:
	void EnableProperties(bool enable);
	void RefreshContentsInternal();

	void OnOptionChange(wxCommandEvent &event);
	void OnServerIdChange(wxCommandEvent &event);
	void OnClientIdChange(wxCommandEvent &event);
	void OnText(wxCommandEvent &event);
	void OnTypeChange(wxCommandEvent &event);
	void OnPropertyChange(wxCommandEvent &event);
	void OnInputTimer(wxTimerEvent &event);
	void OnClickOK(wxCommandEvent &event);
	void OnClickCancel(wxCommandEvent &event);

	std::shared_ptr<wxRadioBox> options_radio_box;

	std::shared_ptr<wxRadioBox> types_radio_box;

	std::shared_ptr<wxSpinCtrl> server_id_spin;
	std::shared_ptr<wxSpinCtrl> client_id_spin;
	std::shared_ptr<wxTextCtrl> name_text_input;
	wxTimer input_timer;
	std::shared_ptr<wxCheckBox> unpassable;
	std::shared_ptr<wxCheckBox> unmovable;
	std::shared_ptr<wxCheckBox> block_missiles;
	std::shared_ptr<wxCheckBox> block_pathfinder;
	std::shared_ptr<wxCheckBox> readable;
	std::shared_ptr<wxCheckBox> writeable;
	std::shared_ptr<wxCheckBox> pickupable;
	std::shared_ptr<wxCheckBox> stackable;
	std::shared_ptr<wxCheckBox> rotatable;
	std::shared_ptr<wxCheckBox> hangable;
	std::shared_ptr<wxCheckBox> hook_east;
	std::shared_ptr<wxCheckBox> hook_south;
	std::shared_ptr<wxCheckBox> has_elevation;
	std::shared_ptr<wxCheckBox> ignore_look;
	std::shared_ptr<wxCheckBox> floor_change;

	std::shared_ptr<FindDialogListBox> items_list;
	std::shared_ptr<wxStdDialogButtonSizer> buttons_box_sizer;
	std::shared_ptr<wxButton> ok_button;
	std::shared_ptr<wxButton> cancel_button;
	std::shared_ptr<Brush> result_brush;
	uint16_t result_id;
	bool only_pickupables;

	DECLARE_EVENT_TABLE()
};

#endif // RME_FIND_ITEM_WINDOW_H_
