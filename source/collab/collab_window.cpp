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
#include <wx/listctrl.h>
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
	const char* kComingNext = "Coming in a later phase";

	enum Column {
		COL_AUTHOR,
		COL_POSITION,
		COL_TEXT,
		COL_STATUS,
	};

	wxButton* makeButton(wxWindow* parent, wxSizer* sizer, const wxString &label) {
		auto* button = newd wxButton(parent, wxID_ANY, label);
		sizer->Add(button, 0, wxRIGHT, 4);
		return button;
	}
}

CollabWindow::CollabWindow(wxWindow* parent) :
	wxPanel(parent, wxID_ANY) {
	instance = this;

	notebook = newd wxNotebook(this, wxID_ANY);

	auto* session = newd wxPanel(notebook);
	BuildSessionPage(session);
	notebook->AddPage(session, "Session");

	// Chat and History exist so the layout is final, but stay disabled until networking lands.
	for (const char* title : { "Chat", "History" }) {
		auto* page = newd wxPanel(notebook);
		auto* box = newd wxBoxSizer(wxVERTICAL);
		box->Add(newd wxStaticText(page, wxID_ANY, kComingNext), 0, wxALL, 12);
		page->SetSizer(box);
		page->Enable(false);
		notebook->AddPage(page, title);
	}

	auto* comments = newd wxPanel(notebook);
	BuildCommentsPage(comments);
	notebook->InsertPage(PAGE_COMMENTS, comments, "Comments");
	notebook->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent &event) {
		if (event.GetSelection() == PAGE_COMMENTS) {
			RefreshComments();
		}
		event.Skip();
	});

	auto* sizer = newd wxBoxSizer(wxVERTICAL);
	sizer->Add(notebook, 1, wxEXPAND);
	SetSizer(sizer);
}

CollabWindow::~CollabWindow() {
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

void CollabWindow::BuildSessionPage(wxWindow* page) {
	auto* root = newd wxBoxSizer(wxVERTICAL);

	auto* name_row = newd wxBoxSizer(wxHORIZONTAL);
	name_row->Add(newd wxStaticText(page, wxID_ANY, "Display name:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
	name_text = newd wxTextCtrl(page, wxID_ANY, wxstr(MapComments::localAuthor()));
	name_text->SetMaxLength(32);
	name_text->Bind(wxEVT_TEXT, [this](wxCommandEvent &) {
		g_settings.setString(Config::COLLAB_USER_NAME, nstr(name_text->GetValue()));
	});
	name_row->Add(name_text, 1);
	root->Add(name_row, 0, wxEXPAND | wxALL, 8);

	auto* host = newd wxStaticBoxSizer(wxVERTICAL, page, "Host");
	auto* host_box = host->GetStaticBox();
	auto addRow = [&](wxStaticBoxSizer* box, wxWindow* parent, const wxString &label, wxWindow* field) {
		auto* row = newd wxBoxSizer(wxHORIZONTAL);
		row->Add(newd wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
		row->Add(field, 1);
		box->Add(row, 0, wxEXPAND | wxALL, 3);
	};
	addRow(host, host_box, "Port:", newd wxSpinCtrl(host_box, wxID_ANY, "31313", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 65535, 31313));
	addRow(host, host_box, "Password:", newd wxTextCtrl(host_box, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD));
	host->Add(newd wxCheckBox(host_box, wxID_ANY, "Share map with participants"), 0, wxALL, 3);
	host->Add(newd wxCheckBox(host_box, wxID_ANY, "Also save on participants' machines"), 0, wxALL, 3);
	auto* start = newd wxButton(host_box, wxID_ANY, "Start hosting");
	host->Add(start, 0, wxALL, 3);
	host_box->Enable(false);
	host_box->SetToolTip(kComingNext);
	root->Add(host, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	auto* join = newd wxStaticBoxSizer(wxVERTICAL, page, "Join");
	auto* join_box = join->GetStaticBox();
	addRow(join, join_box, "Address:", newd wxTextCtrl(join_box, wxID_ANY, "localhost"));
	addRow(join, join_box, "Port:", newd wxSpinCtrl(join_box, wxID_ANY, "31313", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 65535, 31313));
	addRow(join, join_box, "Password:", newd wxTextCtrl(join_box, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD));
	join->Add(newd wxButton(join_box, wxID_ANY, "Join"), 0, wxALL, 3);
	join_box->Enable(false);
	join_box->SetToolTip(kComingNext);
	root->Add(join, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	page->SetSizer(root);
}

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
