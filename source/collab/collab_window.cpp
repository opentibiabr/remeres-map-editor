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
#include <wx/clipbrd.h>
#include <wx/filedlg.h>
#include <wx/combobox.h>
#include <wx/dialog.h>
#include <wx/wrapsizer.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/srchctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/textdlg.h>

#ifdef __WINDOWS__
	#include <mmsystem.h>
#endif

CollabWindow* CollabWindow::instance = nullptr;

namespace {
	enum Column {
		COL_AUTHOR,
		COL_POSITION,
		COL_TEXT,
		COL_TYPE,
		COL_ASSIGNEE,
		COL_STATUS,
	};

	enum UserColumn {
		USER_NAME,
		USER_ROLE,
		USER_DOING,
		USER_PING,
	};

	// data/sounds/message-notification.mp3 (and .ogg). wxSound only plays WAV, so Windows uses MCI
	// and the other systems hand the file to a player that is usually installed; when nothing can
	// play it, the system beep stands in.
	void playNotificationSound() {
#ifdef __WINDOWS__
		const wxString path = GUI::GetDataDirectory() + "sounds" + wxFileName::GetPathSeparator() + "message-notification.mp3";
		if (wxFileExists(path)) {
			mciSendStringW(L"close rmeNotification", nullptr, 0, nullptr);
			const std::wstring open = L"open \"" + path.ToStdWstring() + L"\" type mpegvideo alias rmeNotification";
			if (mciSendStringW(open.c_str(), nullptr, 0, nullptr) == 0 && mciSendStringW(L"play rmeNotification from 0", nullptr, 0, nullptr) == 0) {
				return;
			}
		}
#else
		struct Player {
			const char* program;
			const char* file;
			const char* arguments;
		};
		static const Player players[] = {
	#ifdef __APPLE__
			{ "afplay", "message-notification.mp3", "" },
	#else
			{ "paplay", "message-notification.ogg", "" },
			{ "pw-play", "message-notification.ogg", "" },
			{ "ogg123", "message-notification.ogg", "-q" },
			{ "ffplay", "message-notification.ogg", "-nodisp -autoexit -loglevel quiet" },
			{ "mpv", "message-notification.ogg", "--no-video --really-quiet" },
			{ "play", "message-notification.ogg", "-q" }, // sox
	#endif
		};
		wxPathList searchPath;
		searchPath.AddEnvList("PATH");
		for (const Player &player : players) {
			const wxString program = searchPath.FindValidPath(player.program);
			const wxString file = GUI::GetDataDirectory() + "sounds" + wxFileName::GetPathSeparator() + player.file;
			if (!program.empty() && wxFileExists(file)) {
				wxExecute("\"" + program + "\" " + player.arguments + " \"" + file + "\"", wxEXEC_ASYNC);
				return;
			}
		}
#endif
		wxBell();
	}

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

	session_page = newd wxScrolledWindow(notebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	session_page->SetScrollRate(0, 12);
	auto* session = session_page;
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
	collab_session.onPresenceChanged = [this] { RefreshPresence(); };
	collab_session.onHistoryChanged = [this] { RefreshHistory(); };
	collab_session.onCommentsChanged = [this] { RefreshComments(); };
	collab_session.onHistoryResult = [this](const std::string &message) { history_status->SetLabel(wxstr(message)); };
	RebuildChat();
	OnSessionChanged();
}

CollabWindow::~CollabWindow() {
	collab::Session &collab_session = collab::Session::get();
	collab_session.onChanged = nullptr;
	collab_session.onChat = nullptr;
	collab_session.onPresenceChanged = nullptr;
	collab_session.onHistoryChanged = nullptr;
	collab_session.onCommentsChanged = nullptr;
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
	host_viewer_password = newd wxTextCtrl(host_box, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
	host_viewer_password->SetHint("Optional: whoever uses it joins as a Viewer");
	addRow(host, host_box, "Viewer password:", host_viewer_password);
	host_role = newd wxChoice(host_box, wxID_ANY);
	host_role->Append("Editor");
	host_role->Append("Viewer");
	host_role->Append("Commenter");
	const int savedRole = g_settings.getInteger(Config::COLLAB_DEFAULT_ROLE);
	host_role->SetSelection(savedRole == static_cast<int>(collab::Role::Viewer) ? 1 : (savedRole == static_cast<int>(collab::Role::Commenter) ? 2 : 0));
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

	host_perm_meta = newd wxCheckBox(host_box, wxID_ANY, "Editors can change houses, towns, waypoints and zones");
	host_perm_meta->SetValue(g_settings.getBoolean(Config::COLLAB_PERM_META));
	host->Add(host_perm_meta, 0, wxALL, 3);
	host_perm_props = newd wxCheckBox(host_box, wxID_ANY, "Editors can change the map properties");
	host_perm_props->SetValue(g_settings.getBoolean(Config::COLLAB_PERM_PROPS));
	host->Add(host_perm_props, 0, wxALL, 3);
	host_perm_spawns = newd wxCheckBox(host_box, wxID_ANY, "Editors can change spawns, monsters and npcs");
	host_perm_spawns->SetValue(g_settings.getBoolean(Config::COLLAB_PERM_SPAWNS));
	host->Add(host_perm_spawns, 0, wxALL, 3);
	host_autosave = newd wxSpinCtrl(host_box, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 240, g_settings.getInteger(Config::COLLAB_AUTOSAVE_MINUTES));
	host_autosave->SetToolTip("The host saves the map by itself every N minutes while somebody is connected (needs a saved map). 0 turns it off.");
	addRow(host, host_box, "Autosave (minutes):", host_autosave);
	start_button = newd wxButton(host_box, wxID_ANY, "Start hosting");
	start_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
		const auto role = host_role->GetSelection() == 1 ? collab::Role::Viewer : (host_role->GetSelection() == 2 ? collab::Role::Commenter : collab::Role::Editor);
		g_settings.setInteger(Config::COLLAB_PERM_META, host_perm_meta->GetValue());
		g_settings.setInteger(Config::COLLAB_PERM_PROPS, host_perm_props->GetValue());
		g_settings.setInteger(Config::COLLAB_PERM_SPAWNS, host_perm_spawns->GetValue());
		g_settings.setInteger(Config::COLLAB_AUTOSAVE_MINUTES, host_autosave->GetValue());
		collab::HostOptions options;
		options.editorsMeta = host_perm_meta->GetValue();
		options.editorsProps = host_perm_props->GetValue();
		options.editorsSpawns = host_perm_spawns->GetValue();
		options.autosaveMinutes = host_autosave->GetValue();
		options.viewerPassword = nstr(host_viewer_password->GetValue());
		collab::Session::get().hostOptions = options;
		g_settings.setInteger(Config::COLLAB_PORT, host_port->GetValue());
		g_settings.setInteger(Config::COLLAB_DEFAULT_ROLE, static_cast<int>(role));
		g_settings.setInteger(Config::COLLAB_SHARE_MAP, host_share->GetValue());
		g_settings.setInteger(Config::COLLAB_SAVE_ON_PARTICIPANTS, host_save_all->GetValue());
		std::string error;
		if (collab::Session::get().startHosting(g_gui.GetCurrentEditor(), nstr(name_text->GetValue()), static_cast<uint16_t>(host_port->GetValue()), nstr(host_password->GetValue()), role, host_share->GetValue(), host_save_all->GetValue(), error)) {
			host_password->Clear();
			host_viewer_password->Clear();
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
	// An invite is "address:port": pasting it fills both fields.
	join_address->Bind(wxEVT_TEXT, [this](wxCommandEvent &) {
		const wxString text = join_address->GetValue().Trim().Trim(false);
		const int colon = text.Find(':', true);
		long port = 0;
		if (colon != wxNOT_FOUND && text.find(':') == static_cast<size_t>(colon) && text.Mid(colon + 1).ToLong(&port) && port > 0 && port < 65536) {
			join_address->ChangeValue(text.Left(colon));
			join_port->SetValue(static_cast<int>(port));
		}
	});
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
	leave_button = newd wxButton(active_panel, wxID_ANY, "Leave");
	leave_button->Bind(wxEVT_BUTTON, [](wxCommandEvent &) { collab::Session::get().leave(); });
	save_request_button = newd wxButton(active_panel, wxID_ANY, "Request save");
	save_request_button->SetToolTip("Ask the host to save the map");
	save_request_button->Bind(wxEVT_BUTTON, [](wxCommandEvent &) { collab::Session::get().requestSave(); });
	auto* buttons = newd wxBoxSizer(wxHORIZONTAL);
	buttons->Add(leave_button, 0, wxRIGHT, 4);
	buttons->Add(save_request_button, 0);
	active->Add(buttons, 0, wxBOTTOM, 6);
	user_list = newd wxListCtrl(active_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	user_list->InsertColumn(USER_NAME, "Participant", wxLIST_FORMAT_LEFT, 180);
	user_list->InsertColumn(USER_ROLE, "Role", wxLIST_FORMAT_LEFT, 74);
	user_list->InsertColumn(USER_DOING, "Doing", wxLIST_FORMAT_LEFT, 100);
	user_list->InsertColumn(USER_PING, "Ping", wxLIST_FORMAT_LEFT, 56);
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

	invite_label = newd wxStaticText(active_panel, wxID_ANY, "");
	active->Add(invite_label, 0, wxEXPAND | wxBOTTOM, 3);
	invite_button = newd wxButton(active_panel, wxID_ANY, "Copy invite");
	invite_button->SetToolTip("Copies address:port. Send the password separately.");
	invite_button->Bind(wxEVT_BUTTON, [](wxCommandEvent &) {
		const auto &addresses = collab::Session::get().lanAddresses();
		const wxString text = wxstr((addresses.empty() ? std::string("localhost") : addresses.front()) + ":" + std::to_string(collab::Session::get().port()));
		if (wxTheClipboard->Open()) {
			wxTheClipboard->SetData(newd wxTextDataObject(text));
			wxTheClipboard->Close();
		}
	});
	active->Add(invite_button, 0, wxBOTTOM, 6);

	follow_label = newd wxStaticText(active_panel, wxID_ANY, "");
	active->Add(follow_label, 0, wxEXPAND | wxBOTTOM, 3);

	auto* areas = newd wxBoxSizer(wxHORIZONTAL);
	claim_button = newd wxButton(active_panel, wxID_ANY, "Reserve selection");
	claim_button->SetToolTip("Mark the selected tiles as your area: the others see it with your name.");
	claim_button->Bind(wxEVT_BUTTON, [](wxCommandEvent &) {
		Editor* editor = g_gui.GetCurrentEditor();
		if (!editor || !editor->hasSelection()) {
			g_gui.SetStatusText("Select the tiles you want to reserve first");
			return;
		}
		const Position from = editor->getSelection().minPosition();
		const Position to = editor->getSelection().maxPosition();
		if (from.z != to.z) {
			g_gui.SetStatusText("A reserved area belongs to a single floor");
			return;
		}
		collab::Session::get().claimArea(from, to);
	});
	release_button = newd wxButton(active_panel, wxID_ANY, "Release my areas");
	release_button->Bind(wxEVT_BUTTON, [](wxCommandEvent &) { collab::Session::get().releaseMyClaims(); });
	areas->Add(claim_button, 0, wxRIGHT, 4);
	areas->Add(release_button, 0);
	active->Add(areas, 0, wxBOTTOM, 6);

	summon_button = newd wxButton(active_panel, wxID_ANY, "Bring everyone here");
	summon_button->SetToolTip("Moves every participant's camera to where yours is");
	summon_button->Bind(wxEVT_BUTTON, [](wxCommandEvent &) { collab::Session::get().summonAll(); });
	active->Add(summon_button, 0, wxBOTTOM, 6);

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

void CollabWindow::RefreshToggles() {
	show_cursors->SetValue(g_settings.getBoolean(Config::COLLAB_SHOW_CURSORS));
	show_names->SetValue(g_settings.getBoolean(Config::COLLAB_SHOW_NAMES));
}

void CollabWindow::OnSessionChanged() {
	RefreshToggles();
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

	const bool joined_or_host = collab_session.state() == collab::State::Hosting || collab_session.state() == collab::State::Joined;
	const bool can_edit_role = joined_or_host && me && collab::canEditMap(me->role);
	const bool can_summon = joined_or_host && me && (me->role == collab::Role::Host || me->role == collab::Role::Admin);
	invite_label->Show(collab_session.isHost());
	invite_button->Show(collab_session.isHost());
	if (collab_session.isHost()) {
		wxString text = "Invite: ";
		const auto &addresses = collab_session.lanAddresses();
		if (addresses.empty()) {
			text += wxString::Format("localhost:%u", static_cast<unsigned>(collab_session.port()));
		}
		for (size_t i = 0; i < addresses.size(); ++i) {
			text += (i ? ", " : "") + wxString::Format("%s:%u", wxstr(addresses[i]), static_cast<unsigned>(collab_session.port()));
		}
		invite_label->SetLabel(text);
		invite_label->Wrap(GetClientSize().GetWidth() > 40 ? GetClientSize().GetWidth() - 40 : 300);
	}
	claim_button->Show(can_edit_role);
	release_button->Show(can_edit_role);
	summon_button->Show(can_summon);
	const auto followed = collab_session.users().find(collab_session.followedUser());
	follow_label->SetLabel(collab_session.followedUser() != 0 && followed != collab_session.users().end() ? "Following " + wxstr(followed->second.name) + " (move your camera to stop)" : wxString());
	const bool history_allowed = collab_session.canSeeHistory();
	history_page->Enable(history_allowed);
	if (history_allowed && !history_requested) {
		history_requested = true;
		collab_session.requestHistory(0, HistoryFilter());
	} else if (!history_allowed) {
		history_requested = false;
		history_status->SetLabel(active ? "Only the host and admins see the history" : "Start or join a session to see the history");
	}

	RefreshUsers();
	Layout();
	session_page->FitInside(); // the scroll range follows what is shown
}

// Somebody started or stopped typing, or changed tool: the list and the chat hint follow.
void CollabWindow::RefreshPresence() {
	RefreshUsers();

	wxString who;
	int typing = 0;
	for (const auto &entry : collab::Session::get().presences()) {
		auto user = collab::Session::get().users().find(entry.first);
		if (entry.second.typing && user != collab::Session::get().users().end()) {
			who += (typing++ ? ", " : "") + wxstr(user->second.name);
		}
	}
	typing_label->SetLabel(typing == 0 ? wxString() : (typing == 1 ? who + " is typing..." : who + " are typing..."));
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
		const int latency = collab_session.latencyMs(user.id);
		if (latency >= 0) {
			user_list->SetItem(row, USER_PING, wxString::Format("%d ms", latency));
		}
		const auto presence = collab_session.presences().find(user.id);
		if (presence != collab_session.presences().end()) {
			wxString doing = wxstr(presence->second.tool);
			if (presence->second.typing) {
				doing += doing.empty() ? "typing..." : " (typing...)";
			}
			user_list->SetItem(row, USER_DOING, doing);
		}
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
		ID_FOLLOW,
		ID_COMMENTER,
	};
	wxMenu menu;
	menu.Append(ID_GOTO, "Go to cursor")->Enable(collab_session.cursors().count(userId) > 0);
	if (userId != collab_session.myId()) {
		menu.Append(ID_FOLLOW, collab_session.followedUser() == userId ? "Stop following" : "Follow");
	}
	if (collab_session.canManage(user)) {
		menu.AppendSeparator();
		if (collab_session.isHost()) {
			menu.Append(ID_ADMIN, "Make admin");
		}
		menu.Append(ID_EDITOR, "Make editor");
		menu.Append(ID_VIEWER, "Make viewer");
		menu.Append(ID_COMMENTER, "Make commenter");
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
				case ID_FOLLOW:
					if (s.followedUser() == userId) {
						s.stopFollowing();
					} else {
						s.follow(userId);
					}
					break;
				case ID_ADMIN:
					s.setRole(userId, collab::Role::Admin);
					break;
				case ID_EDITOR:
					s.setRole(userId, collab::Role::Editor);
					break;
				case ID_VIEWER:
					s.setRole(userId, collab::Role::Viewer);
					break;
				case ID_COMMENTER:
					s.setRole(userId, collab::Role::Commenter);
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

	typing_label = newd wxStaticText(page, wxID_ANY, "");
	root->Add(typing_label, 0, wxEXPAND | wxLEFT | wxRIGHT, 6);

	auto* input_row = newd wxBoxSizer(wxHORIZONTAL);
	chat_input = newd wxTextCtrl(page, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	chat_input->SetMaxLength(collab::kMaxChat);
	chat_input->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent &) { SendChat(); });
	chat_input->Bind(wxEVT_TEXT, [](wxCommandEvent &) { collab::Session::get().onLocalTyping(); });
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

	chat_sound = newd wxCheckBox(page, wxID_ANY, "Play a sound for new messages");
	chat_sound->SetValue(g_settings.getBoolean(Config::COLLAB_CHAT_SOUND));
	chat_sound->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { g_settings.setInteger(Config::COLLAB_CHAT_SOUND, chat_sound->GetValue()); });
	root->Add(chat_sound, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

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

	if (!rebuilding_chat && !line.system && line.userId != collab::Session::get().myId() && g_settings.getBoolean(Config::COLLAB_CHAT_SOUND)) {
		playNotificationSound();
	}
}

void CollabWindow::RebuildChat() {
	chat_log->Clear();
	const int unread = unread_chat;
	rebuilding_chat = true; // old messages do not beep
	for (const collab::ChatLine &line : collab::Session::get().chat()) {
		AppendChat(line);
	}
	rebuilding_chat = false;
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
	history_user->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) { collab::Session::get().requestHistory(0, HistoryFilter()); });
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

	auto* buttons = newd wxWrapSizer(wxHORIZONTAL);
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
	makeButton(page, buttons, "Preview")->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
		if (const collab::JournalEntry* entry = SelectedHistoryEntry()) {
			// Yellow tiles would change, red ones are conflicts that would be skipped.
			collab::Session::get().previewEntry(entry->id, entry->state == static_cast<int>(collab::EntryState::Reverted), history_force->GetValue());
		}
	});
	makeButton(page, buttons, "Clear preview")->Bind(wxEVT_BUTTON, [](wxCommandEvent &) { collab::Session::get().clearPreview(); });
	auto goTo = [this]() {
		if (const collab::JournalEntry* entry = SelectedHistoryEntry()) {
			g_gui.SetScreenCenterPosition(entry->center);
		}
	};
	makeButton(page, buttons, "Go to")->Bind(wxEVT_BUTTON, [goTo](wxCommandEvent &) { goTo(); });
	history_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [goTo](wxListEvent &) { goTo(); });
	makeButton(page, buttons, "Mark restore point...")->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
		wxTextEntryDialog dialog(this, "Name of this moment (for example \"before the new town\"):", "Restore point");
		if (dialog.ShowModal() == wxID_OK && !dialog.GetValue().Trim().IsEmpty()) {
			collab::Session::get().markRestorePoint(nstr(dialog.GetValue()));
		}
	});
	makeButton(page, buttons, "Restore to point")->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
		const collab::JournalEntry* entry = SelectedHistoryEntry();
		if (!entry || entry->actionType != collab::kRestorePoint) {
			history_status->SetLabel("Select a restore point in the list first");
			return;
		}
		const int64_t pointId = entry->id; // copied: the history changes while the dialog is open
		const wxString label = wxstr(entry->label);
		if (wxMessageBox("Put the whole map back to \"" + label + "\"? Everything edited after it is undone, for everybody.", "Restore", wxYES_NO | wxICON_QUESTION, this) == wxYES) {
			history_status->SetLabel("Restoring...");
			collab::Session::get().restoreToPoint(pointId);
		}
	});
	history_minutes = newd wxSpinCtrl(page, wxID_ANY, "10", wxDefaultPosition, wxSize(56, -1), wxSP_ARROW_KEYS, 1, 1440, 10);
	buttons->Add(history_minutes, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 4);
	makeButton(page, buttons, "Revert user's last minutes")->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
		const std::string user = HistoryFilter();
		if (user.empty()) {
			history_status->SetLabel("Pick a user in the filter first");
			return;
		}
		if (wxMessageBox(wxString::Format("Revert everything %s did in the last %d minutes?", wxstr(user), history_minutes->GetValue()), "Revert", wxYES_NO | wxICON_QUESTION, this) == wxYES) {
			collab::Session::get().revertRecent(user, history_minutes->GetValue(), history_force->GetValue());
		}
	});
	makeButton(page, buttons, "Export CSV")->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
		wxFileDialog dialog(this, "Export the history", wxEmptyString, "history.csv", "CSV (*.csv)|*.csv", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
		if (dialog.ShowModal() == wxID_OK) {
			history_status->SetLabel(collab::Session::get().exportHistoryCsv(nstr(dialog.GetPath())) ? "History exported" : "Could not write the file");
		}
	});
	makeButton(page, buttons, "Refresh")->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { collab::Session::get().requestHistory(0, HistoryFilter()); });
	makeButton(page, buttons, "Older")->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
		const auto &entries = collab::Session::get().history();
		if (!entries.empty()) {
			collab::Session::get().requestHistory(entries.back().id, HistoryFilter());
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

std::string CollabWindow::HistoryFilter() const {
	return history_user->GetSelection() > 0 ? nstr(history_user->GetStringSelection()) : std::string();
}

void CollabWindow::RefreshHistory() {
	if (!history_list) {
		return;
	}
	const auto &entries = collab::Session::get().history();

	// The filter lists everybody who ever edited the map; the host applies the choice.
	const wxString selected = history_user->GetSelection() > 0 ? history_user->GetStringSelection() : wxString();
	history_user->Clear();
	history_user->Append("All users");
	for (const std::string &name : collab::Session::get().historyUsers()) {
		history_user->Append(wxstr(name));
	}
	const int again = selected.empty() ? wxNOT_FOUND : history_user->FindString(selected);
	history_user->SetSelection(again == wxNOT_FOUND ? 0 : again);

	long selectedId = -1;
	const long selectedRow = history_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (selectedRow >= 0) {
		selectedId = static_cast<long>(history_list->GetItemData(selectedRow));
	}

	history_list->DeleteAllItems();
	for (const collab::JournalEntry &entry : entries) {
		const long row = history_list->InsertItem(history_list->GetItemCount(), wxString::Format("%lld", static_cast<long long>(entry.id)));
		if (static_cast<long>(entry.id) == selectedId) {
			history_list->SetItemState(row, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
		}
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

bool CollabWindow::EditComment(wxWindow* parent, MapComment &comment, bool isNew) {
	wxDialog dialog(parent, wxID_ANY, isNew ? (comment.parent != 0 ? "Reply" : "Add Comment") : "Edit Comment", wxDefaultPosition, wxSize(440, comment.parent != 0 ? 250 : 300));
	auto* root = newd wxBoxSizer(wxVERTICAL);

	auto* text = newd wxTextCtrl(&dialog, wxID_ANY, wxstr(comment.text), wxDefaultPosition, wxSize(-1, 120), wxTE_MULTILINE);
	text->SetMaxLength(2000);
	root->Add(text, 1, wxEXPAND | wxALL, 8);
	root->Add(newd wxStaticText(&dialog, wxID_ANY, "Write @name to mention somebody: they get a notification."), 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

	wxChoice* kind = nullptr;
	wxComboBox* assignee = nullptr;
	if (comment.parent == 0) { // a reply belongs to its thread's type and assignee
		auto* row = newd wxBoxSizer(wxHORIZONTAL);
		row->Add(newd wxStaticText(&dialog, wxID_ANY, "Type:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
		kind = newd wxChoice(&dialog, wxID_ANY);
		for (uint8_t k = 0; k < MapComments::kKindCount; ++k) {
			kind->Append(MapComments::kindName(k));
		}
		kind->SetSelection(comment.kind % MapComments::kKindCount);
		row->Add(kind, 0, wxRIGHT, 12);
		row->Add(newd wxStaticText(&dialog, wxID_ANY, "Assign to:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
		assignee = newd wxComboBox(&dialog, wxID_ANY, wxstr(comment.assignee));
		for (const auto &entry : collab::Session::get().users()) {
			assignee->Append(wxstr(entry.second.name));
		}
		row->Add(assignee, 1);
		root->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
	}

	root->Add(dialog.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 8);
	dialog.SetSizer(root);
	text->SetFocus();
	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}

	const std::string value = nstr(text->GetValue().Trim().Trim(false));
	if (value.empty()) {
		return false;
	}
	comment.text = value;
	if (kind && assignee) {
		comment.kind = static_cast<uint8_t>(kind->GetSelection());
		comment.assignee = nstr(assignee->GetValue().Trim().Trim(false));
	}
	return true;
}

void CollabWindow::BuildCommentsPage(wxWindow* page) {
	auto* root = newd wxBoxSizer(wxVERTICAL);

	auto* filter = newd wxWrapSizer(wxHORIZONTAL);
	show_resolved = newd wxCheckBox(page, wxID_ANY, "Show resolved");
	show_resolved->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { RefreshComments(); });
	filter->Add(show_resolved, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
	mine_only = newd wxCheckBox(page, wxID_ANY, "Assigned to me");
	mine_only->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { RefreshComments(); });
	filter->Add(mine_only, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
	kind_filter = newd wxChoice(page, wxID_ANY);
	kind_filter->Append("All types");
	for (uint8_t k = 0; k < MapComments::kKindCount; ++k) {
		kind_filter->Append(MapComments::kindName(k));
	}
	kind_filter->SetSelection(0);
	kind_filter->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) { RefreshComments(); });
	filter->Add(kind_filter, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
	root->Add(filter, 0, wxEXPAND | wxALL, 6);

	search = newd wxSearchCtrl(page, wxID_ANY);
	search->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { RefreshComments(); });
	root->Add(search, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	comment_list = newd wxListCtrl(page, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	comment_list->InsertColumn(COL_AUTHOR, "Author", wxLIST_FORMAT_LEFT, 72);
	comment_list->InsertColumn(COL_POSITION, "Position", wxLIST_FORMAT_LEFT, 92);
	comment_list->InsertColumn(COL_TEXT, "Text", wxLIST_FORMAT_LEFT, 150);
	comment_list->InsertColumn(COL_TYPE, "Type", wxLIST_FORMAT_LEFT, 50);
	comment_list->InsertColumn(COL_ASSIGNEE, "Assigned", wxLIST_FORMAT_LEFT, 70);
	comment_list->InsertColumn(COL_STATUS, "Status", wxLIST_FORMAT_LEFT, 60);
	root->Add(comment_list, 1, wxEXPAND | wxLEFT | wxRIGHT, 6);

	auto goTo = [this]() {
		if (const MapComment* c = SelectedComment()) {
			g_gui.SetScreenCenterPosition(c->pos);
		}
	};
	comment_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [goTo](wxListEvent &) { goTo(); });

	// Changes go through the map's comments; the session replicates them like any other change.
	auto mutate = [this](const std::function<void(Editor &, const MapComment &)> &change) {
		const MapComment* c = SelectedComment();
		Editor* editor = g_gui.GetCurrentEditor();
		if (c && editor) {
			change(*editor, *c);
			editor->getMap().doChange();
			RefreshComments();
			g_gui.RefreshView();
		}
	};

	auto* buttons = newd wxWrapSizer(wxHORIZONTAL);
	makeButton(page, buttons, "Go to")->Bind(wxEVT_BUTTON, [goTo](wxCommandEvent &) { goTo(); });
	makeButton(page, buttons, "Next open")->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { GoToNextOpen(); });
	makeButton(page, buttons, "Reply")->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { ReplyToSelected(); });
	makeButton(page, buttons, "Edit")->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
		const MapComment* c = SelectedComment();
		Editor* editor = g_gui.GetCurrentEditor();
		if (!c || !editor) {
			return;
		}
		MapComment draft = *c;
		if (EditComment(this, draft, false)) {
			editor->getMap().comments.update(draft.id, draft.text, draft.assignee, draft.kind);
			editor->getMap().doChange();
			RefreshComments();
			g_gui.RefreshView();
		}
	});
	makeButton(page, buttons, "Resolve")->Bind(wxEVT_BUTTON, [mutate](wxCommandEvent &) {
		mutate([](Editor &editor, const MapComment &c) {
			const MapComment* root = editor.getMap().comments.rootOf(c); // the thread is resolved as a whole
			editor.getMap().comments.setResolved(root->id, !root->resolved);
		});
	});
	makeButton(page, buttons, "Delete")->Bind(wxEVT_BUTTON, [mutate](wxCommandEvent &) {
		mutate([](Editor &editor, const MapComment &c) { editor.getMap().comments.remove(c.id); });
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

// First comments of every thread (a reply whose first comment is gone counts as one), by id.
std::vector<const MapComment*> CollabWindow::ThreadRoots() const {
	std::vector<const MapComment*> roots;
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		return roots;
	}
	const MapComments &comments = editor->getMap().comments;
	for (const MapComment &c : comments.all()) {
		if (c.parent == 0 || !comments.get(c.parent)) {
			roots.push_back(&c);
		}
	}
	std::sort(roots.begin(), roots.end(), [](const MapComment* a, const MapComment* b) { return a->id < b->id; });
	return roots;
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

	const MapComments &comments = editor->getMap().comments;
	const std::string needle = nstr(search->GetValue().Lower());
	const std::string me = MapComments::localAuthor();
	const int kindIndex = kind_filter->GetSelection();

	auto matches = [&](const MapComment &c) {
		return needle.empty() || nstr(wxstr(c.text + " " + c.author).Lower()).find(needle) != std::string::npos;
	};
	auto addRow = [&](const MapComment &c, const MapComment &root, bool reply) {
		const wxString text = wxstr(c.text).BeforeFirst('\n');
		long row = comment_list->InsertItem(comment_list->GetItemCount(), wxstr(c.author));
		comment_list->SetItem(row, COL_POSITION, wxString::Format("%d, %d, %d", c.pos.x, c.pos.y, c.pos.z));
		comment_list->SetItem(row, COL_TEXT, reply ? ">> " + text : text);
		if (!reply) {
			comment_list->SetItem(row, COL_TYPE, MapComments::kindName(c.kind));
			comment_list->SetItem(row, COL_ASSIGNEE, wxstr(c.assignee));
			comment_list->SetItem(row, COL_STATUS, root.resolved ? "Resolved" : "Open");
		}
		comment_list->SetItemData(row, c.id);
		if (root.resolved) {
			comment_list->SetItemTextColour(row, wxColour(128, 128, 128));
		}
	};

	for (const MapComment* root : ThreadRoots()) {
		if (root->resolved && !show_resolved->GetValue()) {
			continue;
		}
		if (mine_only->GetValue() && root->assignee != me) {
			continue;
		}
		if (kindIndex > 0 && root->kind != kindIndex - 1) {
			continue;
		}

		std::vector<const MapComment*> replies;
		for (const MapComment &c : comments.all()) {
			if (c.parent == root->id) {
				replies.push_back(&c);
			}
		}
		std::sort(replies.begin(), replies.end(), [](const MapComment* a, const MapComment* b) { return a->id < b->id; });

		bool found = matches(*root);
		for (const MapComment* reply : replies) {
			found = found || matches(*reply);
		}
		if (!found) {
			continue;
		}
		addRow(*root, *root, false);
		for (const MapComment* reply : replies) {
			addRow(*reply, *root, true);
		}
	}
}

void CollabWindow::ReplyToSelected() {
	const MapComment* c = SelectedComment();
	Editor* editor = g_gui.GetCurrentEditor();
	if (!c || !editor) {
		return;
	}
	const MapComment* root = editor->getMap().comments.rootOf(*c);
	MapComment reply;
	reply.pos = root->pos;
	reply.parent = root->id;
	if (EditComment(this, reply, true)) {
		editor->getMap().comments.add(reply.pos, reply.text, reply.parent);
		editor->getMap().doChange();
		RefreshComments();
		g_gui.RefreshView();
	}
}

// Goes to the next unresolved thread after the selected one, wrapping around.
void CollabWindow::GoToNextOpen() {
	const std::vector<const MapComment*> roots = ThreadRoots();
	if (roots.empty()) {
		return;
	}
	const MapComment* selected = SelectedComment();
	const MapComment* current = selected && g_gui.GetCurrentEditor() ? g_gui.GetCurrentEditor()->getMap().comments.rootOf(*selected) : nullptr;

	size_t start = 0;
	if (current) {
		for (size_t i = 0; i < roots.size(); ++i) {
			if (roots[i]->id == current->id) {
				start = i + 1;
			}
		}
	}
	for (size_t step = 0; step < roots.size(); ++step) {
		const MapComment* candidate = roots[(start + step) % roots.size()];
		if (candidate->resolved) {
			continue;
		}
		g_gui.SetScreenCenterPosition(candidate->pos);
		for (long row = 0; row < comment_list->GetItemCount(); ++row) {
			if (static_cast<uint32_t>(comment_list->GetItemData(row)) == candidate->id) {
				comment_list->SetItemState(row, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
				comment_list->EnsureVisible(row);
				break;
			}
		}
		return;
	}
	g_gui.SetStatusText("No open comments");
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
	MapComment draft;
	draft.pos = Position(x, y, canvas->GetFloor());
	if (!draft.pos.isValid()) {
		return;
	}

	if (EditComment(tab, draft, true)) {
		editor->getMap().comments.add(draft.pos, draft.text, 0, draft.assignee, draft.kind);
		editor->getMap().doChange();
		g_gui.RefreshView();
		if (instance) {
			instance->RefreshComments();
		}
	}
}
