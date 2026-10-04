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

#ifndef RME_COLLAB_WINDOW_H
#define RME_COLLAB_WINDOW_H

#include <wx/panel.h>

#include "../map_comments.h"
#include "collab_session.h"

class wxButton;
class wxCheckBox;
class wxChoice;
class wxListCtrl;
class wxListEvent;
class wxNotebook;
class wxSearchCtrl;
class wxSpinCtrl;
class wxStaticText;
class wxTextCtrl;

// Dockable "Collaborate" panel: Session (host/join, participants), Chat, Comments and
// History (still a placeholder).
class CollabWindow : public wxPanel {
public:
	explicit CollabWindow(wxWindow* parent);
	~CollabWindow() override;

	enum Page {
		PAGE_SESSION,
		PAGE_CHAT,
		PAGE_COMMENTS,
		PAGE_HISTORY,
	};
	void SelectPage(Page page);

	// Rebuilds the comments list from the current editor's map.
	void RefreshComments();

	static CollabWindow* Get() {
		return instance;
	}

	// Asks for a comment on the tile under the map cursor. Used by the menu and the panel.
	static void AddCommentAtCursor();

private:
	void BuildSessionPage(wxWindow* page);
	void BuildChatPage(wxWindow* page);
	void BuildCommentsPage(wxWindow* page);
	void BuildHistoryPage(wxWindow* page);
	const MapComment* SelectedComment() const;

	void OnSessionChanged();
	void RefreshUsers();
	void RebuildChat();
	void AppendChat(const collab::ChatLine &line);
	void UpdateStartButton();
	void SendChat();
	void ShowUserMenu(uint32_t userId);
	void RefreshHistory();
	std::string HistoryFilter() const;
	const collab::JournalEntry* SelectedHistoryEntry() const;

	wxNotebook* notebook = nullptr;
	wxTextCtrl* name_text = nullptr;
	wxStaticText* status_label = nullptr;

	// Session page: idle forms and active view
	wxPanel* idle_panel = nullptr;
	wxPanel* active_panel = nullptr;
	wxSpinCtrl* host_port = nullptr;
	wxTextCtrl* host_password = nullptr;
	wxChoice* host_role = nullptr;
	wxCheckBox* host_share = nullptr;
	wxCheckBox* host_save_all = nullptr;
	wxButton* start_button = nullptr;
	wxTextCtrl* join_address = nullptr;
	wxSpinCtrl* join_port = nullptr;
	wxTextCtrl* join_password = nullptr;
	wxListCtrl* user_list = nullptr;
	wxCheckBox* show_cursors = nullptr;
	wxCheckBox* show_names = nullptr;
	wxButton* leave_button = nullptr;
	wxButton* save_request_button = nullptr;

	// Chat page
	wxPanel* chat_page = nullptr;
	wxTextCtrl* chat_log = nullptr;
	wxTextCtrl* chat_input = nullptr;
	int unread_chat = 0;
	bool was_active = false;

	// History page (host and admins)
	wxPanel* history_page = nullptr;
	wxListCtrl* history_list = nullptr;
	wxChoice* history_user = nullptr;
	wxCheckBox* history_force = nullptr;
	wxStaticText* history_status = nullptr;
	bool history_requested = false;

	// Comments page
	wxListCtrl* comment_list = nullptr;
	wxCheckBox* show_resolved = nullptr;
	wxSearchCtrl* search = nullptr;

	static CollabWindow* instance;
};

#endif
