#include "main.h"

#include "materials_workbench_inspector_dialog.h"

#include <algorithm>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/listctrl.h>
#include <wx/srchctrl.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "brush_database.h"
#include "sqlite_materials_inspector.h"

namespace {
	constexpr int kWarningListSeverityColumn = 0;
	constexpr int kWarningListDomainColumn = 1;
	constexpr int kWarningListEntityColumn = 2;
	constexpr int kWarningListIssueColumn = 3;
	constexpr int kWarningListCountColumn = 4;
	constexpr int kWarningListStatusColumn = 5;

	wxString NormalizeQuery(const wxString &value) {
		wxString normalized = value;
		normalized.Trim(true);
		normalized.Trim(false);
		return normalized.Lower();
	}

	bool TextMatchesQuery(const wxString &value, const wxString &normalizedQuery) {
		if (normalizedQuery.IsEmpty()) {
			return true;
		}
		return value.Lower().Find(normalizedQuery) != wxNOT_FOUND;
	}

	int GetSelectedListIndex(wxListCtrl* list) {
		if (!list) {
			return -1;
		}
		return list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	}
} // namespace

MaterialsWorkbenchInspectorDialog::MaterialsWorkbenchInspectorDialog(wxWindow* parent, GoToHandler goToHandler) :
	wxDialog(parent, wxID_ANY, "Inspector", wxDefaultPosition, wxSize(980, 620), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	goToHandler_(std::move(goToHandler)) {
	BuildLayout();
	ReloadWarnings();
}

void MaterialsWorkbenchInspectorDialog::SelectSqliteTab() {
	if (!notebook_) {
		return;
	}
	const int index = notebook_->FindPage(sqlitePanel_);
	if (index != wxNOT_FOUND) {
		notebook_->SetSelection(index);
	}
}

void MaterialsWorkbenchInspectorDialog::BuildLayout() {
	wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);

	notebook_ = new wxNotebook(this, wxID_ANY);
	BuildHealthTab(notebook_);
	BuildSqliteTab(notebook_);
	rootSizer->Add(notebook_, 1, wxEXPAND | wxALL, FromDIP(10));

	SetSizerAndFit(rootSizer);
	Layout();
}

void MaterialsWorkbenchInspectorDialog::BuildHealthTab(wxNotebook* notebook) {
	wxPanel* panel = new wxPanel(notebook, wxID_ANY);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	wxStaticText* title = new wxStaticText(panel, wxID_ANY, "Health / Warnings");
	wxFont titleFont = title->GetFont();
	titleFont.SetWeight(wxFONTWEIGHT_BOLD);
	title->SetFont(titleFont);

	wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);
	headerSizer->Add(title, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));
	headerSizer->AddStretchSpacer(1);

	warningRescanButton_ = new wxButton(panel, wxID_ANY, "Rescan");
	warningGoToButton_ = new wxButton(panel, wxID_ANY, "Go to");
	warningGoToButton_->Enable(false);
	headerSizer->Add(warningRescanButton_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
	headerSizer->Add(warningGoToButton_, 0, wxALIGN_CENTER_VERTICAL);

	wxBoxSizer* filterSizer = new wxBoxSizer(wxHORIZONTAL);
	warningSearchCtrl_ = new wxSearchCtrl(panel, wxID_ANY);
	warningSearchCtrl_->ShowSearchButton(false);
	warningSearchCtrl_->ShowCancelButton(true);
	warningSearchCtrl_->SetDescriptiveText("Search warnings");

	wxArrayString severities;
	severities.Add("All severities");
	severities.Add("Warning");
	severities.Add("Error");
	warningSeverityChoice_ = new wxChoice(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, severities);
	warningSeverityChoice_->SetSelection(0);

	wxArrayString domains;
	domains.Add("All domains");
	domains.Add("Brush");
	domains.Add("Palette");
	domains.Add("Border");
	domains.Add("Wall");
	warningDomainChoice_ = new wxChoice(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, domains);
	warningDomainChoice_->SetSelection(0);

	filterSizer->Add(warningSearchCtrl_, 1, wxEXPAND | wxRIGHT, FromDIP(8));
	filterSizer->Add(warningSeverityChoice_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
	filterSizer->Add(warningDomainChoice_, 0, wxALIGN_CENTER_VERTICAL);

	wxSplitterWindow* splitter = new wxSplitterWindow(panel, wxID_ANY);
	splitter->SetSashGravity(0.62);
	splitter->SetMinimumPaneSize(FromDIP(120));

	wxPanel* listPanel = new wxPanel(splitter, wxID_ANY);
	wxBoxSizer* listSizer = new wxBoxSizer(wxVERTICAL);
	warningList_ = new wxListCtrl(listPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	warningList_->AppendColumn("Severity", wxLIST_FORMAT_LEFT, FromDIP(90));
	warningList_->AppendColumn("Domain", wxLIST_FORMAT_LEFT, FromDIP(90));
	warningList_->AppendColumn("Entity", wxLIST_FORMAT_LEFT, FromDIP(220));
	warningList_->AppendColumn("Issue", wxLIST_FORMAT_LEFT, FromDIP(360));
	warningList_->AppendColumn("Count", wxLIST_FORMAT_RIGHT, FromDIP(70));
	warningList_->AppendColumn("Status", wxLIST_FORMAT_LEFT, FromDIP(90));
	listSizer->Add(warningList_, 1, wxEXPAND);
	listPanel->SetSizer(listSizer);

	wxPanel* detailsPanel = new wxPanel(splitter, wxID_ANY);
	wxBoxSizer* detailsSizer = new wxBoxSizer(wxVERTICAL);
	wxStaticText* detailsTitle = new wxStaticText(detailsPanel, wxID_ANY, "Details");
	wxFont detailsTitleFont = detailsTitle->GetFont();
	detailsTitleFont.SetWeight(wxFONTWEIGHT_BOLD);
	detailsTitle->SetFont(detailsTitleFont);
	warningDetails_ = new wxTextCtrl(detailsPanel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
	detailsSizer->Add(detailsTitle, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
	detailsSizer->Add(warningDetails_, 1, wxEXPAND);
	detailsPanel->SetSizer(detailsSizer);

	splitter->SplitVertically(listPanel, detailsPanel, FromDIP(600));

	sizer->Add(headerSizer, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
	sizer->Add(new wxStaticLine(panel), 0, wxEXPAND | wxBOTTOM, FromDIP(10));
	sizer->Add(filterSizer, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
	sizer->Add(splitter, 1, wxEXPAND);

	warningRescanButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		ReloadWarnings();
	});
	warningGoToButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		GoToSelectedWarning();
	});
	warningSearchCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
		ApplyWarningFilter();
	});
	warningSearchCtrl_->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN, [this](wxCommandEvent&) {
		warningSearchCtrl_->ChangeValue("");
		ApplyWarningFilter();
	});
	warningSeverityChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
		ApplyWarningFilter();
	});
	warningDomainChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
		ApplyWarningFilter();
	});
	warningList_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) {
		UpdateWarningDetails();
	});
	warningList_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) {
		GoToSelectedWarning();
	});

	panel->SetSizer(sizer);
	notebook->AddPage(panel, "Health");
}

void MaterialsWorkbenchInspectorDialog::BuildSqliteTab(wxNotebook* notebook) {
	wxPanel* panel = new wxPanel(notebook, wxID_ANY);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	wxStaticText* title = new wxStaticText(panel, wxID_ANY, "SQLite Materials Inspector");
	wxFont titleFont = title->GetFont();
	titleFont.SetWeight(wxFONTWEIGHT_BOLD);
	title->SetFont(titleFont);

	sizer->Add(title, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
	sizer->Add(new wxStaticLine(panel), 0, wxEXPAND | wxBOTTOM, FromDIP(10));

	sqlitePanel_ = new SQLiteMaterialsInspectorPanel(panel);
	sizer->Add(sqlitePanel_, 1, wxEXPAND);

	panel->SetSizer(sizer);
	notebook->AddPage(panel, "SQLite");
}

void MaterialsWorkbenchInspectorDialog::ReloadWarnings() {
	warnings_.clear();

	if (!g_brush_database.isOpen()) {
		WarningRow row;
		row.severity = "Error";
		row.domain = "Workbench";
		row.entityKind = "database";
		row.issue = "materials.db is not open";
		row.count = 1;
		row.status = "Active";
		row.details = "SQLite brush database is not open.";
		warnings_.push_back(std::move(row));
		ApplyWarningFilter();
		return;
	}

	MaterialsDatabaseAuditReport report;
	if (!g_brush_database.generateAuditReport(report)) {
		WarningRow row;
		row.severity = "Error";
		row.domain = "Workbench";
		row.entityKind = "database";
		row.issue = "Failed to scan materials.db";
		row.count = 1;
		row.status = "Active";
		row.details = g_brush_database.getLastError();
		warnings_.push_back(std::move(row));
		ApplyWarningFilter();
		return;
	}

	if (report.unresolvedGroundTargets > 0) {
		WarningRow row;
		row.severity = "Warning";
		row.domain = "Brush";
		row.entityKind = "brush";
		row.issue = "Unresolved ground targets";
		row.count = report.unresolvedGroundTargets;
		row.status = "Active";
		row.details = "Some ground border targets reference missing brushes.";
		warnings_.push_back(std::move(row));
	}

	if (report.unresolvedBrushLinks > 0) {
		WarningRow row;
		row.severity = "Warning";
		row.domain = "Brush";
		row.entityKind = "brush";
		row.issue = "Unresolved brush links";
		row.count = report.unresolvedBrushLinks;
		row.status = "Active";
		row.details = "Some brush relationships reference missing brushes.";
		warnings_.push_back(std::move(row));
	}

	if (report.unresolvedTilesetEntries > 0) {
		WarningRow row;
		row.severity = "Warning";
		row.domain = "Palette";
		row.entityKind = "palette";
		row.issue = "Unresolved palette entries";
		row.count = report.unresolvedTilesetEntries;
		row.status = "Active";
		row.details = "Some palette entries reference missing brushes/items.";
		warnings_.push_back(std::move(row));
	}

	if (warnings_.empty()) {
		WarningRow row;
		row.severity = "Warning";
		row.domain = "Workbench";
		row.issue = "No warnings";
		row.count = 0;
		row.status = "OK";
		row.details = "No issues detected by the current scanner.";
		warnings_.push_back(std::move(row));
	}

	ApplyWarningFilter();
}

void MaterialsWorkbenchInspectorDialog::ApplyWarningFilter() {
	filteredWarningIndices_.clear();
	const wxString query = NormalizeQuery(warningSearchCtrl_ ? warningSearchCtrl_->GetValue() : "");
	const wxString severityFilter = warningSeverityChoice_ ? warningSeverityChoice_->GetStringSelection() : "All severities";
	const wxString domainFilter = warningDomainChoice_ ? warningDomainChoice_->GetStringSelection() : "All domains";

	for (size_t i = 0; i < warnings_.size(); ++i) {
		const WarningRow &row = warnings_[i];
		if (severityFilter != "All severities" && row.severity != severityFilter) {
			continue;
		}
		if (domainFilter != "All domains" && row.domain != domainFilter) {
			continue;
		}
		if (!query.IsEmpty()) {
			wxString haystack;
			haystack << row.severity << " " << row.domain << " " << row.entityKind << " " << row.entityName << " " << row.issue << " " << row.status;
			if (!TextMatchesQuery(haystack, query)) {
				continue;
			}
		}
		filteredWarningIndices_.push_back(i);
	}

	if (!warningList_) {
		return;
	}

	warningList_->Freeze();
	warningList_->DeleteAllItems();
	for (size_t viewIndex = 0; viewIndex < filteredWarningIndices_.size(); ++viewIndex) {
		const WarningRow &row = warnings_[filteredWarningIndices_[viewIndex]];
		const wxString entityLabel = row.entityName.IsEmpty() ? row.entityKind : (row.entityKind + ": " + row.entityName);
		const long listIndex = warningList_->InsertItem(static_cast<long>(viewIndex), row.severity);
		warningList_->SetItem(listIndex, kWarningListDomainColumn, row.domain);
		warningList_->SetItem(listIndex, kWarningListEntityColumn, entityLabel);
		warningList_->SetItem(listIndex, kWarningListIssueColumn, row.issue);
		warningList_->SetItem(listIndex, kWarningListCountColumn, wxString::Format("%d", row.count));
		warningList_->SetItem(listIndex, kWarningListStatusColumn, row.status);
	}
	warningList_->Thaw();

	if (warningList_->GetItemCount() > 0) {
		warningList_->SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
	} else if (warningDetails_) {
		warningDetails_->Clear();
	}
	UpdateWarningDetails();
}

void MaterialsWorkbenchInspectorDialog::UpdateWarningDetails() {
	if (!warningDetails_ || !warningList_) {
		return;
	}
	const int selected = GetSelectedListIndex(warningList_);
	if (selected < 0 || static_cast<size_t>(selected) >= filteredWarningIndices_.size()) {
		warningDetails_->Clear();
		if (warningGoToButton_) {
			warningGoToButton_->Enable(false);
		}
		return;
	}

	const WarningRow &row = warnings_[filteredWarningIndices_[static_cast<size_t>(selected)]];
	wxString text;
	text << "Severity: " << row.severity << "\n";
	text << "Domain: " << row.domain << "\n";
	text << "Issue: " << row.issue << "\n";
	text << "Count: " << row.count << "\n";
	text << "Status: " << row.status << "\n";
	if (!row.entityKind.IsEmpty()) {
		text << "Entity: " << row.entityKind;
		if (row.entityId > 0) {
			text << wxString::Format(" #%lld", static_cast<long long>(row.entityId));
		}
		if (!row.entityName.IsEmpty()) {
			text << " (" << row.entityName << ")";
		}
		text << "\n";
	}
	if (!row.details.IsEmpty()) {
		text << "\n" << row.details;
	}
	warningDetails_->SetValue(text);

	const bool canGoTo = goToHandler_ && row.entityId > 0 && !row.entityKind.IsEmpty();
	if (warningGoToButton_) {
		warningGoToButton_->Enable(canGoTo);
	}
}

void MaterialsWorkbenchInspectorDialog::GoToSelectedWarning() {
	if (!goToHandler_ || !warningList_) {
		return;
	}
	const int selected = GetSelectedListIndex(warningList_);
	if (selected < 0 || static_cast<size_t>(selected) >= filteredWarningIndices_.size()) {
		return;
	}
	const WarningRow &row = warnings_[filteredWarningIndices_[static_cast<size_t>(selected)]];
	if (row.entityId <= 0 || row.entityKind.IsEmpty()) {
		return;
	}
	goToHandler_(row.entityKind, row.entityId, row.entityName);
}
