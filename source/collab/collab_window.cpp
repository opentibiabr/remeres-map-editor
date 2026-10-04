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

#include "collab_window.h"

#include "../editor.h"
#include "../gui.h"
#include "../map.h"
#include "../map_display.h"
#include "../map_tab.h"
#include "../settings.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/notebook.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/srchctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/textdlg.h>

CollabWindow* CollabWindow::instance = nullptr;

namespace {
		enum Column {
		COL_AUTHOR,
		COL_POSITION,
		COL_TEXT,
		COL_STATUS,
	};

	enum UserColumn {
		USER_NAME,
		USER_ROLE,
	};

	wxButton* makeButton(wxWindow* parent, wxSizer* sizer, const wxString &label) {
		auto* button = newd wxButton(parent, wxID_ANY, label);
		sizer->Add(button, 0, wxRIGHT, 4);
		return button;
	}

	wxColour toColour(uint32_t rgb) {
		return wxColour((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
	}

	// Labelled row inside a static box.
	void addRow(wxSizer* box, wxWindow* parent, const wxString &label, wxWindow* field) {
		auto* row = newd wxBoxSizer(wxHORIZONTAL);
		row->Add(newd wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
		row->Add(field, 1);
		box->Add(row, 0, wxEXPAND | wxALL, 3);
	}
}

CollabWindow::CollabWindow(wxWindow* parent) :
	wxPanel(parent, wxID_ANY) {
	instance = this;

	notebook = newd wxNotebook(this, wxID_ANY);

	auto* session = newd wxPanel(notebook);
	BuildSessionPage(session);
	notebook->AddPage(session, "Session");

	chat_page = newd wxPanel(notebook);
	BuildChatPage(chat_page);
	notebook->AddPage(chat_page, "Chat");

	auto* comments = newd wxPanel(notebook);
	BuildCommentsPage(comments);
	notebook->AddPage(comments, "Comments");

	history_page = newd wxPanel(notebook);
	BuildHistoryPage(history_page);
	notebook->AddPage(history_page, "History");

	notebook->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent &event) {
		if (event.GetSelection() == PAGE_COMMENTS) {
			RefreshComments();
		}
		if (event.GetSelection() == PAGE_SESSION) {
			UpdateStartButton(); // a map may have been opened or closed meanwhile
		}
		if (event.GetSelection() == PAGE_CHAT && unread_chat > 0) {
			unread_chat = 0;
			notebook->SetPageText(PAGE_CHAT, "Chat");
		}
		event.Skip();
	});

	auto* sizer = newd wxBoxSizer(wxVERTICAL);
	sizer->Add(notebook, 1, wxEXPAND);
	SetSizer(sizer);

	// The session outlives the panel (closing the pane only hides it), so the callbacks are
	// attached here and detached in the destructor.
	collab::Session &collab_session = collab::Session::get();
	collab_session.onChanged = [this] { OnSessionChanged(); };
	collab_session.onChat = [this](const collab::ChatLine &line) { AppendChat(line); };
	collab_session.onHistoryChanged = [this] { RefreshHistory(); };
	collab_session.onHistoryResult = [this](const std::string &message) { history_status->SetLabel(wxstr(message)); };
	RebuildChat();
	OnSessionChanged();
}

CollabWindow::~CollabWindow() {
	collab::Session &collab_session = collab::Session::get();
	collab_session.onChanged = nullptr;
	collab_session.onChat = nullptr;
	collab_session.onHistoryChanged = nullptr;
	collab_session.onHistoryResult = nullptr;
	if (instance == this) {
		instance = nullptr;
	}
}

void CollabWindow::SelectPage(Page page) {
	notebook->SetSelection(page);
	if (page == PAGE_COMMENTS) {
		RefreshComments();
	}
}

// ---- Session page -------------------------------------------------------------------------

void CollabWindow::BuildSessionPage(wxWindow* page) {
	auto* root = newd wxBoxSizer(wxVERTICAL);

	auto* name_row = newd wxBoxSizer(wxHORIZONTAL);
	name_row->Add(newd wxStaticText(page, wxID_ANY, "Display name:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
	name_text = newd wxTextCtrl(page, wxID_ANY, wxstr(MapComments::localAuthor()));
	name_text->SetMaxLength(collab::kMaxName);
	name_text->Bind(wxEVT_TEXT, [this](wxCommandEvent &) {
		g_settings.setString(Config::COLLAB_USER_NAME, nstr(name_text->GetValue()));
	});
	name_row->Add(name_text, 1);
	root->Add(name_row, 0, wxEXPAND | wxALL, 8);

	status_label = newd wxStaticText(page, wxID_ANY, "");
	root->Add(status_label, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	// -- idle: host and join forms
	idle_panel = newd wxPanel(page);
	auto* idle = newd wxBoxSizer(wxVERTICAL);

	auto* host = newd wxStaticBoxSizer(wxVERTICAL, idle_panel, "Host");
	wxWindow* host_box = host->GetStaticBox();
	host_port = newd wxSpinCtrl(host_box, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 65535, g_settings.getInteger(Config::COLLAB_PORT));
	addRow(host, host_box, "Port:", host_port);
	host_password = newd wxTextCtrl(host_box, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
	host_password->SetHint("At least 6 characters");
	host_password->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { UpdateStartButton(); });
	addRow(host, host_box, "Password:", host_password);
	host_role = newd wxChoice(host_box, wxID_ANY);
	host_role->Append("Editor");
	host_role->Append("Viewer");
	host_role->SetSelection(g_settings.getInteger(Config::COLLAB_DEFAULT_ROLE) == static_cast<int>(collab::Role::Viewer) ? 1 : 0);
	addRow(host, host_box, "Joiners are:", host_role);
	host_share = newd wxCheckBox(host_box, wxID_ANY, "Share map with participants (they can keep and save a copy)");
	host_share->SetValue(g_settings.getBoolean(Config::COLLAB_SHARE_MAP));
	host_share->SetToolTip("Off: participants only see the map while connected; it is never written to their disk.");
	host_share->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { host_save_all->Enable(host_share->GetValue()); });
	host->Add(host_share, 0, wxALL, 3);
	host_save_all = newd wxCheckBox(host_box, wxID_ANY, "Also save on participants' machines");
	host_save_all->SetValue(g_settings.getBoolean(Config::COLLAB_SAVE_ON_PARTICIPANTS));
	host_save_all->Enable(host_share->GetValue());
	host->Add(host_save_all, 0, wxALL, 3);
	start_button = newd wxButton(host_box, wxID_ANY, "Start hosting");
	start_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
		const auto role = host_role->GetSelection() == 1 ? collab::Role::Viewer : collab::Role::Editor;
		g_settings.setInteger(Config::COLLAB_PORT, host_port->GetValue());
		g_settings.setInteger(Config::COLLAB_DEFAULT_ROLE, static_cast<int>(role));
		g_settings.setInteger(Config::COLLAB_SHARE_MAP, host_share->GetValue());
		g_settings.setInteger(Config::COLLAB_SAVE_ON_PARTICIPANTS, host_save_all->GetValue());
		std::string error;
		if (collab::Session::get().startHosting(g_gui.GetCurrentEditor(), nstr(name_text->GetValue()), static_cast<uint16_t>(host_port->GetValue()), nstr(host_password->GetValue()), role, host_share->GetValue(), host_save_all->GetValue(), error)) {
			host_password->Clear();
		} else {
			status_label->SetLabel(wxstr(error));
			Layout();
		}
	});
	host->Add(start_button, 0, wxALL, 3);
	idle->Add(host, 0, wxEXPAND | wxBOTTOM, 8);

	auto* join = newd wxStaticBoxSizer(wxVERTICAL, idle_panel, "Join");
	wxWindow* join_box = join->GetStaticBox();
	join_address = newd wxTextCtrl(join_box, wxID_ANY, wxstr(g_settings.getString(Config::COLLAB_LAST_ADDRESS)));
	addRow(join, join_box, "Address:", join_address);
	join_port = newd wxSpinCtrl(join_box, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 65535, g_settings.getInteger(Config::COLLAB_PORT));
	addRow(join, join_box, "Port:", join_port);
	join_password = newd wxTextCtrl(join_box, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
	addRow(join, join_box, "Password:", join_password);
	auto* join_button = newd wxButton(join_box, wxID_ANY, "Join");
	join_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
		g_settings.setString(Config::COLLAB_LAST_ADDRESS, nstr(join_address->GetValue()));
		collab::Session::get().join(nstr(name_text->GetValue()), nstr(join_address->GetValue()), static_cast<uint16_t>(join_port->GetValue()), nstr(join_password->GetValue()));
		join_password->Clear();
	});
	join->Add(join_button, 0, wxALL, 3);
	idle->Add(join, 0, wxEXPAND);

	idle_panel->SetSizer(idle);
	root->Add(idle_panel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	// -- active: participants and options
	active_panel = newd wxPanel(page);
	auto* active = newd wxBoxSizer(wxVERTICAL);
	user_list = newd wxListCtrl(active_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	user_list->InsertColumn(USER_NAME, "Participant", wxLIST_FORMAT_LEFT, 180);
	user_list->InsertColumn(USER_ROLE, "Role", wxLIST_FORMAT_LEFT, 80);
	user_list->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, [this](wxListEvent &event) {
		ShowUserMenu(static_cast<uint32_t>(user_list->GetItemData(event.GetIndex())));
	});
	user_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent &event) {
		const auto &cursors = collab::Session::get().cursors();
		auto it = cursors.find(static_cast<uint32_t>(user_list->GetItemData(event.GetIndex())));
		if (it != cursors.end()) {
			g_gui.SetScreenCenterPosition(it->second.pos);
		}
	});
	active->Add(user_list, 1, wxEXPAND | wxBOTTOM, 6);

	show_cursors = newd wxCheckBox(active_panel, wxID_ANY, "Show cursors");
	show_cursors->SetValue(g_settings.getBoolean(Config::COLLAB_SHOW_CURSORS));
	show_cursors->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) {
		g_settings.setInteger(Config::COLLAB_SHOW_CURSORS, show_cursors->GetValue());
		g_gui.RefreshView();
	});
	active->Add(show_cursors, 0, wxBOTTOM, 3);
	show_names = newd wxCheckBox(active_panel, wxID_ANY, "Show names");
	show_names->SetValue(g_settings.getBoolean(Config::COLLAB_SHOW_NAMES));
	show_names->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) {
		g_settings.setInteger(Config::COLLAB_SHOW_NAMES, show_names->GetValue());
		g_gui.RefreshView();
	});
	active->Add(show_names, 0, wxBOTTOM, 6);

	leave_button = newd wxButton(active_panel, wxID_ANY, "Leave");
	leave_button->Bind(wxEVT_BUTTON, [](wxCommandEvent &) { collab::Session::get().leave(); });
	save_request_button = newd wxButton(active_panel, wxID_ANY, "Request save");
	save_request_button->SetToolTip("Ask the host to save the map");
	save_request_button->Bind(wxEVT_BUTTON, [](wxCommandEvent &) { collab::Session::get().requestSave(); });
	auto* buttons = newd wxBoxSizer(wxHORIZONTAL);
	buttons->Add(leave_button, 0, wxRIGHT, 4);
	buttons->Add(save_request_button, 0);
	active->Add(buttons, 0);
	active_panel->SetSizer(active);
	root->Add(active_panel, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	page->SetSizer(root);
	UpdateStartButton();
}

void CollabWindow::UpdateStartButton() {
	Editor* editor = g_gui.GetCurrentEditor();
	const bool has_map = editor && !editor->IsCollabClient();
	const bool has_password = host_password->GetValue().length() >= collab::kMinPassword;
	start_button->Enable(has_map && has_password);
	start_button->SetToolTip(!has_map ? "Open the map you want to share first" : (has_password ? "" : "The password needs at least 6 characters"));
}

void CollabWindow::OnSessionChanged() {
	collab::Session &collab_session = collab::Session::get();
	const bool active = collab_session.active();

	status_label->SetLabel(wxstr(collab_session.status()));
	status_label->Wrap(GetClientSize().GetWidth() > 40 ? GetClientSize().GetWidth() - 40 : 300);
	idle_panel->Show(!active);
	active_panel->Show(active);
	leave_button->SetLabel(collab_session.isHost() ? "Stop hosting" : (collab_session.state() == collab::State::Connecting ? "Cancel" : "Leave"));
	chat_page->Enable(collab_session.state() == collab::State::Hosting || collab_session.state() == collab::State::Joined);

	if (active && !was_active) {
		RebuildChat(); // a new session starts with an empty log
	}
	was_active = active;

	const collab::User* me = collab_session.self();
	save_request_button->Show(collab_session.state() == collab::State::Joined && me && me->role == collab::Role::Admin);
	const bool history_allowed = collab_session.canSeeHistory();
	history_page->Enable(history_allowed);
	if (history_allowed && !history_requested) {
		history_requested = true;
		collab_session.requestHistory(0);
	} else if (!history_allowed) {
		history_requested = false;
		history_status->SetLabel(active ? "Only the host and admins see the history" : "Start or join a session to see the history");
	}

	RefreshUsers();
	Layout();
}

void CollabWindow::RefreshUsers() {
	user_list->DeleteAllItems();
	const collab::Session &collab_session = collab::Session::get();
	for (const auto &entry : collab_session.users()) {
		const collab::User &user = entry.second;
		wxString label = wxstr(user.name);
		if (user.id == collab_session.myId()) {
			label += " (you)";
		}
		long row = user_list->InsertItem(user_list->GetItemCount(), label);
		user_list->SetItem(row, USER_ROLE, collab::roleName(user.role));
		user_list->SetItemData(row, user.id);
		user_list->SetItemTextColour(row, toColour(user.color));
	}
}

void CollabWindow::ShowUserMenu(uint32_t userId) {
	collab::Session &collab_session = collab::Session::get();
	auto it = collab_session.users().find(userId);
	if (it == collab_session.users().end()) {
		return;
	}
	const collab::User user = it->second;

	enum {
		ID_GOTO = wxID_HIGHEST + 5000,
		ID_ADMIN,
		ID_EDITOR,
		ID_VIEWER,
		ID_KICK,
	};
	wxMenu menu;
	menu.Append(ID_GOTO, "Go to cursor")->Enable(collab_session.cursors().count(userId) > 0);
	if (collab_session.canManage(user)) {
		menu.AppendSeparator();
		if (collab_session.isHost()) {
			menu.Append(ID_ADMIN, "Make admin");
		}
		menu.Append(ID_EDITOR, "Make editor");
		menu.Append(ID_VIEWER, "Make viewer");
		menu.AppendSeparator();
		menu.Append(ID_KICK, "Kick");
	}

	menu.Bind(
		wxEVT_MENU, [userId](wxCommandEvent &event) {
			collab::Session &s = collab::Session::get();
			switch (event.GetId()) {
				case ID_GOTO: {
					auto cursor = s.cursors().find(userId);
					if (cursor != s.cursors().end()) {
						g_gui.SetScreenCenterPosition(cursor->second.pos);
					}
					break;
				}
				case ID_ADMIN:
					s.setRole(userId, collab::Role::Admin);
					break;
				case ID_EDITOR:
					s.setRole(userId, collab::Role::Editor);
					break;
				case ID_VIEWER:
					s.setRole(userId, collab::Role::Viewer);
					break;
				case ID_KICK:
					s.kick(userId);
					break;
			}
		},
		wxID_ANY
	);
	PopupMenu(&menu);
}

// ---- Chat page ----------------------------------------------------------------------------

void CollabWindow::BuildChatPage(wxWindow* page) {
	auto* root = newd wxBoxSizer(wxVERTICAL);

	chat_log = newd wxTextCtrl(page, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_RICH2 | wxTE_READONLY);
	root->Add(chat_log, 1, wxEXPAND | wxALL, 6);

	auto* input_row = newd wxBoxSizer(wxHORIZONTAL);
	chat_input = newd wxTextCtrl(page, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	chat_input->SetMaxLength(collab::kMaxChat);
	chat_input->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent &) { SendChat(); });
	// Typing must not trigger the editor's single-key hotkeys.
	chat_input->Bind(wxEVT_SET_FOCUS, [](wxFocusEvent &event) {
		g_gui.DisableHotkeys();
		event.Skip();
	});
	chat_input->Bind(wxEVT_KILL_FOCUS, [](wxFocusEvent &event) {
		g_gui.EnableHotkeys();
		event.Skip();
	});
	input_row->Add(chat_input, 1, wxRIGHT, 4);
	auto* send = newd wxButton(page, wxID_ANY, "Send");
	send->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { SendChat(); });
	input_row->Add(send, 0);
	root->Add(input_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	page->SetSizer(root);
}

void CollabWindow::SendChat() {
	collab::Session::get().sendChat(nstr(chat_input->GetValue()));
	chat_input->Clear();
}

void CollabWindow::AppendChat(const collab::ChatLine &line) {
	const wxDateTime time(static_cast<time_t>(line.time));
	const wxString stamp = "[" + time.Format("%H:%M") + "] ";
	const wxFont base = chat_log->GetFont();

	if (line.system) {
		wxTextAttr attr(wxColour(128, 128, 128));
		attr.SetFont(base.Italic());
		chat_log->SetDefaultStyle(attr);
		chat_log->AppendText(stamp + wxstr(line.text) + "\n");
	} else {
		wxTextAttr name_attr(toColour(line.color));
		name_attr.SetFont(base.Bold());
		chat_log->SetDefaultStyle(name_attr);
		chat_log->AppendText(stamp + wxstr(line.name) + ": ");
		wxTextAttr text_attr(chat_log->GetForegroundColour());
		text_attr.SetFont(base);
		chat_log->SetDefaultStyle(text_attr);
		chat_log->AppendText(wxstr(line.text) + "\n");
	}

	if (notebook->GetSelection() != PAGE_CHAT && !line.system) {
		++unread_chat;
		notebook->SetPageText(PAGE_CHAT, wxString::Format("Chat (%d)", unread_chat));
	}
}

void CollabWindow::RebuildChat() {
	chat_log->Clear();
	const int unread = unread_chat;
	for (const collab::ChatLine &line : collab::Session::get().chat()) {
		AppendChat(line);
	}
	unread_chat = unread;
	notebook->SetPageText(PAGE_CHAT, unread > 0 ? wxString::Format("Chat (%d)", unread) : wxString("Chat"));
}

// ---- History page -------------------------------------------------------------------------

namespace {
	enum HistoryColumn {
		HIST_ID,
		HIST_TIME,
		HIST_USER,
		HIST_ACTION,
		HIST_TILES,
		HIST_STATE,
	};

	const char* entryStateName(int state) {
		switch (static_cast<collab::EntryState>(state)) {
			case collab::EntryState::Applied:
				return "Applied";
			case collab::EntryState::Reverted:
				return "Reverted";
			case collab::EntryState::PartiallyReverted:
				return "Partial";
			case collab::EntryState::Info:
				return "Info";
		}
		return "?";
	}
}

void CollabWindow::BuildHistoryPage(wxWindow* page) {
	auto* root = newd wxBoxSizer(wxVERTICAL);

	auto* filter = newd wxBoxSizer(wxHORIZONTAL);
	filter->Add(newd wxStaticText(page, wxID_ANY, "User:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
	history_user = newd wxChoice(page, wxID_ANY);
	history_user->Append("All users");
	history_user->SetSelection(0);
	history_user->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) { RefreshHistory(); });
	filter->Add(history_user, 1);
	root->Add(filter, 0, wxEXPAND | wxALL, 6);

	history_list = newd wxListCtrl(page, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	history_list->InsertColumn(HIST_ID, "#", wxLIST_FORMAT_LEFT, 44);
	history_list->InsertColumn(HIST_TIME, "Time", wxLIST_FORMAT_LEFT, 62);
	history_list->InsertColumn(HIST_USER, "User", wxLIST_FORMAT_LEFT, 80);
	history_list->InsertColumn(HIST_ACTION, "Action", wxLIST_FORMAT_LEFT, 90);
	history_list->InsertColumn(HIST_TILES, "Tiles", wxLIST_FORMAT_LEFT, 48);
	history_list->InsertColumn(HIST_STATE, "State", wxLIST_FORMAT_LEFT, 64);
	root->Add(history_list, 1, wxEXPAND | wxLEFT | wxRIGHT, 6);

	history_status = newd wxStaticText(page, wxID_ANY, "");
	root->Add(history_status, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);

	history_force = newd wxCheckBox(page, wxID_ANY, "Force (overwrite tiles changed later)");
	root->Add(history_force, 0, wxLEFT | wxRIGHT | wxTOP, 6);

	auto* buttons = newd wxBoxSizer(wxHORIZONTAL);
	makeButton(page, buttons, "Revert")->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
		if (const collab::JournalEntry* entry = SelectedHistoryEntry()) {
			collab::Session::get().revertEntry(entry->id, history_force->GetValue());
		}
	});
	makeButton(page, buttons, "Reapply")->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
		if (const collab::JournalEntry* entry = SelectedHistoryEntry()) {
			collab::Session::get().reapplyEntry(entry->id, history_force->GetValue());
		}
	});
	auto goTo = [this]() {
		if (const collab::JournalEntry* entry = SelectedHistoryEntry()) {
			g_gui.SetScreenCenterPosition(entry->center);
		}
	};
	makeButton(page, buttons, "Go to")->Bind(wxEVT_BUTTON, [goTo](wxCommandEvent &) { goTo(); });
	history_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [goTo](wxListEvent &) { goTo(); });
	makeButton(page, buttons, "Refresh")->Bind(wxEVT_BUTTON, [](wxCommandEvent &) { collab::Session::get().requestHistory(0); });
	makeButton(page, buttons, "Older")->Bind(wxEVT_BUTTON, [](wxCommandEvent &) {
		const auto &entries = collab::Session::get().history();
		if (!entries.empty()) {
			collab::Session::get().requestHistory(entries.back().id);
		}
	});
	root->Add(buttons, 0, wxALL, 6);

	page->SetSizer(root);
}

const collab::JournalEntry* CollabWindow::SelectedHistoryEntry() const {
	const long item = history_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (item < 0) {
		return nullptr;
	}
	const auto id = static_cast<int64_t>(history_list->GetItemData(item));
	for (const collab::JournalEntry &entry : collab::Session::get().history()) {
		if (entry.id == id) {
			return &entry;
		}
	}
	return nullptr;
}

void CollabWindow::RefreshHistory() {
	if (!history_list) {
		return;
	}
	const auto &entries = collab::Session::get().history();

	// The user filter lists everybody who appears in the loaded entries.
	const wxString selected = history_user->GetSelection() > 0 ? history_user->GetStringSelection() : wxString();
	history_user->Clear();
	history_user->Append("All users");
	for (const collab::JournalEntry &entry : entries) {
		if (history_user->FindString(wxstr(entry.user)) == wxNOT_FOUND) {
			history_user->Append(wxstr(entry.user));
		}
	}
	const int again = selected.empty() ? wxNOT_FOUND : history_user->FindString(selected);
	history_user->SetSelection(again == wxNOT_FOUND ? 0 : again);

	history_list->DeleteAllItems();
	for (const collab::JournalEntry &entry : entries) {
		if (history_user->GetSelection() > 0 && wxstr(entry.user) != history_user->GetStringSelection()) {
			continue;
		}
		const long row = history_list->InsertItem(history_list->GetItemCount(), wxString::Format("%lld", static_cast<long long>(entry.id)));
		history_list->SetItem(row, HIST_TIME, wxDateTime(static_cast<time_t>(entry.created)).Format("%H:%M:%S"));
		history_list->SetItem(row, HIST_USER, wxstr(entry.user));
		history_list->SetItem(row, HIST_ACTION, wxstr(entry.label));
		history_list->SetItem(row, HIST_TILES, entry.state == static_cast<int>(collab::EntryState::Info) ? wxString("-") : wxString::Format("%u", entry.tileCount));
		history_list->SetItem(row, HIST_STATE, entryStateName(entry.state));
		history_list->SetItemData(row, static_cast<wxUIntPtr>(entry.id));
		history_list->SetItemTextColour(row, entry.state == static_cast<int>(collab::EntryState::Reverted) ? wxColour(128, 128, 128) : toColour(entry.color));
	}
}

// ---- Comments page ------------------------------------------------------------------------

void CollabWindow::BuildCommentsPage(wxWindow* page) {
	auto* root = newd wxBoxSizer(wxVERTICAL);

	auto* filter = newd wxBoxSizer(wxHORIZONTAL);
	show_resolved = newd wxCheckBox(page, wxID_ANY, "Show resolved");
	show_resolved->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { RefreshComments(); });
	filter->Add(show_resolved, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
	search = newd wxSearchCtrl(page, wxID_ANY);
	search->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { RefreshComments(); });
	filter->Add(search, 1);
	root->Add(filter, 0, wxEXPAND | wxALL, 6);

	comment_list = newd wxListCtrl(page, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	comment_list->InsertColumn(COL_AUTHOR, "Author", wxLIST_FORMAT_LEFT, 80);
	comment_list->InsertColumn(COL_POSITION, "Position", wxLIST_FORMAT_LEFT, 100);
	comment_list->InsertColumn(COL_TEXT, "Text", wxLIST_FORMAT_LEFT, 160);
	comment_list->InsertColumn(COL_STATUS, "Status", wxLIST_FORMAT_LEFT, 60);
	root->Add(comment_list, 1, wxEXPAND | wxLEFT | wxRIGHT, 6);

	auto goTo = [this]() {
		if (const MapComment* c = SelectedComment()) {
			g_gui.SetScreenCenterPosition(c->pos);
		}
	};
	comment_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [goTo](wxListEvent &) { goTo(); });

	auto* buttons = newd wxBoxSizer(wxHORIZONTAL);
	makeButton(page, buttons, "Go to")->Bind(wxEVT_BUTTON, [goTo](wxCommandEvent &) { goTo(); });
	makeButton(page, buttons, "Resolve")->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
		const MapComment* c = SelectedComment();
		Editor* editor = g_gui.GetCurrentEditor();
		if (c && editor) {
			editor->getMap().comments.setResolved(c->id, !c->resolved);
			editor->getMap().doChange();
			RefreshComments();
			g_gui.RefreshView();
		}
	});
	makeButton(page, buttons, "Delete")->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
		const MapComment* c = SelectedComment();
		Editor* editor = g_gui.GetCurrentEditor();
		if (c && editor) {
			editor->getMap().comments.remove(c->id);
			editor->getMap().doChange();
			RefreshComments();
			g_gui.RefreshView();
		}
	});
	makeButton(page, buttons, "Add at cursor")->Bind(wxEVT_BUTTON, [](wxCommandEvent &) { AddCommentAtCursor(); });
	makeButton(page, buttons, "Refresh")->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { RefreshComments(); });
	root->Add(buttons, 0, wxALL, 6);

	page->SetSizer(root);
}

const MapComment* CollabWindow::SelectedComment() const {
	Editor* editor = g_gui.GetCurrentEditor();
	long item = comment_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (!editor || item < 0) {
		return nullptr;
	}
	return editor->getMap().comments.get(static_cast<uint32_t>(comment_list->GetItemData(item)));
}

void CollabWindow::RefreshComments() {
	if (!comment_list) {
		return;
	}
	comment_list->DeleteAllItems();

	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		return;
	}

	const std::string needle = nstr(search->GetValue().Lower());
	for (const MapComment &c : editor->getMap().comments.all()) {
		if (c.resolved && !show_resolved->GetValue()) {
			continue;
		}
		if (!needle.empty() && nstr(wxstr(c.text + " " + c.author).Lower()).find(needle) == std::string::npos) {
			continue;
		}
		long row = comment_list->InsertItem(comment_list->GetItemCount(), wxstr(c.author));
		comment_list->SetItem(row, COL_POSITION, wxString::Format("%d, %d, %d", c.pos.x, c.pos.y, c.pos.z));
		comment_list->SetItem(row, COL_TEXT, wxstr(c.text).BeforeFirst('\n'));
		comment_list->SetItem(row, COL_STATUS, c.resolved ? "Resolved" : "Open");
		comment_list->SetItemData(row, c.id);
	}
}

void CollabWindow::AddCommentAtCursor() {
	Editor* editor = g_gui.GetCurrentEditor();
	MapTab* tab = g_gui.GetCurrentMapTab();
	if (!editor || !tab) {
		return;
	}

	MapCanvas* canvas = tab->GetCanvas();
	int x;
	int y;
	canvas->MouseToMap(&x, &y);
	const Position pos(x, y, canvas->GetFloor());
	if (!pos.isValid()) {
		return;
	}

	wxTextEntryDialog dialog(tab, "Comment:", "Add Comment", "", wxTextEntryDialogStyle | wxTE_MULTILINE);
	if (dialog.ShowModal() == wxID_OK && !dialog.GetValue().IsEmpty()) {
		editor->getMap().comments.add(pos, nstr(dialog.GetValue()));
		editor->getMap().doChange();
		g_gui.RefreshView();
		if (instance) {
			instance->RefreshComments();
		}
	}
}
