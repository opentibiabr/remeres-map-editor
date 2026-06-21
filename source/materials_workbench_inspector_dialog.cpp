#include "main.h"

#include "materials_workbench_inspector_dialog.h"

#include <algorithm>
#include <limits>
#include <set>

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
#include "items.h"
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

	bool IsKnownWorkbenchInspectorItemId(int itemId) {
		if (itemId <= 0 || itemId > std::numeric_limits<uint16_t>::max()) {
			return false;
		}
		return g_items.isValidID(static_cast<uint16_t>(itemId));
	}

	wxString NormalizeAlign(wxString value) {
		value.Trim(true);
		value.Trim(false);
		value.MakeLower();
		return value;
	}

	const std::vector<wxString>& GetRequiredCarpetAligns() {
		static const std::vector<wxString> aligns = {
			"center", "n", "s", "e", "w", "cnw", "cne", "csw", "cse"
		};
		return aligns;
	}

	const std::vector<wxString>& GetKnownWorkbenchInspectorCarpetAligns() {
		static const std::vector<wxString> aligns = {
			"center", "n", "s", "e", "w", "cnw", "cne", "csw", "cse", "dnw", "dne", "dsw", "dse"
		};
		return aligns;
	}

	const std::vector<wxString>& GetRequiredTableAligns() {
		static const std::vector<wxString> aligns = {
			"north", "vertical", "south", "west", "horizontal", "east", "alone"
		};
		return aligns;
	}

	wxString JoinStrings(const std::vector<wxString> &values, const wxString &separator) {
		wxString result;
		for (size_t i = 0; i < values.size(); ++i) {
			if (i > 0) {
				result << separator;
			}
			result << values[i];
		}
		return result;
	}

	bool CanGoToEntity(const wxString &entityKind, int64_t entityId, const wxString &entityName) {
		if (entityKind.IsEmpty()) {
			return false;
		}
		if (entityKind == "palette") {
			return !entityName.IsEmpty();
		}
		return entityId > 0;
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

	wxArrayString issues;
	issues.Add("All issues");
	warningIssueChoice_ = new wxChoice(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, issues);
	warningIssueChoice_->SetSelection(0);

	filterSizer->Add(warningSearchCtrl_, 1, wxEXPAND | wxRIGHT, FromDIP(8));
	filterSizer->Add(warningSeverityChoice_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
	filterSizer->Add(warningDomainChoice_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
	filterSizer->Add(warningIssueChoice_, 0, wxALIGN_CENTER_VERTICAL);

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
	warningIssueChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
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

	{
		std::vector<BrushRecord> carpetBrushes;
		if (!g_brush_database.listBrushesByType("carpet", carpetBrushes)) {
			WarningRow row;
			row.severity = "Error";
			row.domain = "Brush";
			row.entityKind = "database";
			row.issue = "Failed to list carpet brushes";
			row.count = 1;
			row.status = "Active";
			row.details = g_brush_database.getLastError();
			warnings_.push_back(std::move(row));
		} else {
			for (const BrushRecord &brush : carpetBrushes) {
				BrushStorageRecord storage;
				if (!g_brush_database.getCompleteBrushById(brush.id, storage)) {
					continue;
				}

				std::set<wxString> presentAligns;
				std::vector<wxString> unknownAligns;
				std::vector<wxString> emptyAligns;
				std::set<int> invalidItems;

				for (const CarpetNodeRecord &node : storage.carpetNodes) {
					const wxString align = NormalizeAlign(node.align);
					if (align.IsEmpty()) {
						continue;
					}
					bool known = false;
					for (const wxString &candidate : GetKnownWorkbenchInspectorCarpetAligns()) {
						if (candidate == align) {
							known = true;
							break;
						}
					}
					if (!known) {
						unknownAligns.push_back(node.align);
					}
					bool requiredAlign = false;
					for (const wxString &required : GetRequiredCarpetAligns()) {
						if (required == align) {
							requiredAlign = true;
							break;
						}
					}
					if (node.items.empty()) {
						emptyAligns.push_back(node.align);
						continue;
					}
					if (requiredAlign) {
						presentAligns.insert(align);
					}
					for (const CarpetNodeItemRecord &item : node.items) {
						if (!IsKnownWorkbenchInspectorItemId(item.itemId)) {
							invalidItems.insert(item.itemId);
						}
					}
				}

				std::vector<wxString> missingAligns;
				for (const wxString &required : GetRequiredCarpetAligns()) {
					if (presentAligns.find(required) == presentAligns.end()) {
						missingAligns.push_back(required);
					}
				}

				if (!emptyAligns.empty()) {
					WarningRow row;
					row.severity = "Error";
					row.domain = "Brush";
					row.entityKind = "brush";
					row.entityId = brush.id;
					row.entityName = brush.name;
					row.issue = "Carpet has empty contexts";
					row.count = static_cast<int>(emptyAligns.size());
					row.status = "Active";
					row.details = "Empty: " + JoinStrings(emptyAligns, ", ");
					warnings_.push_back(std::move(row));
				}

				if (!unknownAligns.empty()) {
					WarningRow row;
					row.severity = "Warning";
					row.domain = "Brush";
					row.entityKind = "brush";
					row.entityId = brush.id;
					row.entityName = brush.name;
					row.issue = "Carpet has unknown align slots";
					row.count = static_cast<int>(unknownAligns.size());
					row.status = "Active";
					row.details = "Unknown: " + JoinStrings(unknownAligns, ", ");
					warnings_.push_back(std::move(row));
				}

				if (!invalidItems.empty()) {
					wxArrayString ids;
					for (int id : invalidItems) {
						ids.Add(wxString::Format("%d", id));
					}

					WarningRow row;
					row.severity = "Error";
					row.domain = "Brush";
					row.entityKind = "brush";
					row.entityId = brush.id;
					row.entityName = brush.name;
					row.issue = "Carpet has invalid item ids";
					row.count = static_cast<int>(invalidItems.size());
					row.status = "Active";
					row.details = "Invalid item ids: " + wxJoin(ids, ',');
					warnings_.push_back(std::move(row));
				}

				if (!missingAligns.empty()) {
					WarningRow row;
					row.severity = "Warning";
					row.domain = "Brush";
					row.entityKind = "brush";
					row.entityId = brush.id;
					row.entityName = brush.name;
					row.issue = "Carpet missing contexts";
					row.count = static_cast<int>(missingAligns.size());
					row.status = "Active";
					row.details = "Missing: " + JoinStrings(missingAligns, ", ");
					warnings_.push_back(std::move(row));
				}
			}
		}
	}

	{
		std::vector<BrushRecord> tableBrushes;
		if (!g_brush_database.listBrushesByType("table", tableBrushes)) {
			WarningRow row;
			row.severity = "Error";
			row.domain = "Brush";
			row.entityKind = "database";
			row.issue = "Failed to list table brushes";
			row.count = 1;
			row.status = "Active";
			row.details = g_brush_database.getLastError();
			warnings_.push_back(std::move(row));
		} else {
			for (const BrushRecord &brush : tableBrushes) {
				BrushStorageRecord storage;
				if (!g_brush_database.getCompleteBrushById(brush.id, storage)) {
					continue;
				}

				std::set<wxString> presentAligns;
				std::vector<wxString> unknownAligns;
				std::vector<wxString> emptyAligns;
				std::set<int> invalidItems;

				for (const TableNodeRecord &node : storage.tableNodes) {
					const wxString align = NormalizeAlign(node.align);
					if (align.IsEmpty()) {
						continue;
					}
					bool known = false;
					for (const wxString &required : GetRequiredTableAligns()) {
						if (required == align) {
							known = true;
							break;
						}
					}
					if (!known) {
						unknownAligns.push_back(node.align);
					}
					if (node.items.empty()) {
						emptyAligns.push_back(node.align);
						continue;
					}
					presentAligns.insert(align);
					for (const TableNodeItemRecord &item : node.items) {
						if (!IsKnownWorkbenchInspectorItemId(item.itemId)) {
							invalidItems.insert(item.itemId);
						}
					}
				}

				std::vector<wxString> missingAligns;
				for (const wxString &required : GetRequiredTableAligns()) {
					if (presentAligns.find(required) == presentAligns.end()) {
						missingAligns.push_back(required);
					}
				}

				if (!emptyAligns.empty()) {
					WarningRow row;
					row.severity = "Error";
					row.domain = "Brush";
					row.entityKind = "brush";
					row.entityId = brush.id;
					row.entityName = brush.name;
					row.issue = "Table has empty states";
					row.count = static_cast<int>(emptyAligns.size());
					row.status = "Active";
					row.details = "Empty: " + JoinStrings(emptyAligns, ", ");
					warnings_.push_back(std::move(row));
				}

				if (!unknownAligns.empty()) {
					WarningRow row;
					row.severity = "Warning";
					row.domain = "Brush";
					row.entityKind = "brush";
					row.entityId = brush.id;
					row.entityName = brush.name;
					row.issue = "Table has unknown state slots";
					row.count = static_cast<int>(unknownAligns.size());
					row.status = "Active";
					row.details = "Unknown: " + JoinStrings(unknownAligns, ", ");
					warnings_.push_back(std::move(row));
				}

				if (!invalidItems.empty()) {
					wxArrayString ids;
					for (int id : invalidItems) {
						ids.Add(wxString::Format("%d", id));
					}

					WarningRow row;
					row.severity = "Error";
					row.domain = "Brush";
					row.entityKind = "brush";
					row.entityId = brush.id;
					row.entityName = brush.name;
					row.issue = "Table has invalid item ids";
					row.count = static_cast<int>(invalidItems.size());
					row.status = "Active";
					row.details = "Invalid item ids: " + wxJoin(ids, ',');
					warnings_.push_back(std::move(row));
				}

				if (!missingAligns.empty()) {
					WarningRow row;
					row.severity = "Warning";
					row.domain = "Brush";
					row.entityKind = "brush";
					row.entityId = brush.id;
					row.entityName = brush.name;
					row.issue = "Table missing states";
					row.count = static_cast<int>(missingAligns.size());
					row.status = "Active";
					row.details = "Missing: " + JoinStrings(missingAligns, ", ");
					warnings_.push_back(std::move(row));
				}
			}
		}
	}

	std::set<int64_t> validBorderSetIds;
	std::set<int64_t> globalBorderSetIds;
	{
		std::vector<BorderSetRecord> globalBorderSets;
		if (g_brush_database.listBorderSetsByScope("global", globalBorderSets)) {
			for (const BorderSetRecord &borderSet : globalBorderSets) {
				validBorderSetIds.insert(borderSet.id);
				globalBorderSetIds.insert(borderSet.id);
			}
		} else {
			WarningRow row;
			row.severity = "Error";
			row.domain = "Border";
			row.entityKind = "database";
			row.issue = "Failed to list global border sets";
			row.count = 1;
			row.status = "Active";
			row.details = g_brush_database.getLastError();
			warnings_.push_back(std::move(row));
		}

		std::vector<BorderSetRecord> inlineBorderSets;
		if (g_brush_database.listBorderSetsByScope("inline", inlineBorderSets)) {
			for (const BorderSetRecord &borderSet : inlineBorderSets) {
				validBorderSetIds.insert(borderSet.id);
			}
		} else {
			WarningRow row;
			row.severity = "Error";
			row.domain = "Border";
			row.entityKind = "database";
			row.issue = "Failed to list inline border sets";
			row.count = 1;
			row.status = "Active";
			row.details = g_brush_database.getLastError();
			warnings_.push_back(std::move(row));
		}
	}

	{
		for (const BrushTypeCountRecord &typeCount : report.brushTypeCounts) {
			std::vector<BrushRecord> brushes;
			if (!g_brush_database.listBrushesByType(typeCount.type, brushes)) {
				WarningRow row;
				row.severity = "Error";
				row.domain = "Brush";
				row.entityKind = "database";
				row.issue = "Failed to list brushes";
				row.count = 1;
				row.status = "Active";
				row.details = wxString::Format("Type: %s\n%s", typeCount.type, g_brush_database.getLastError());
				warnings_.push_back(std::move(row));
				continue;
			}

			for (const BrushRecord &brush : brushes) {
				BrushStorageRecord storage;
				if (!g_brush_database.getCompleteBrushById(brush.id, storage)) {
					continue;
				}

				if (storage.brush.lookId > 0 && !IsKnownWorkbenchInspectorItemId(storage.brush.lookId)) {
					WarningRow row;
					row.severity = "Error";
					row.domain = "Brush";
					row.entityKind = "brush";
					row.entityId = brush.id;
					row.entityName = brush.name;
					row.issue = "Brush has invalid lookId";
					row.count = 1;
					row.status = "Active";
					row.details = wxString::Format("lookId: %d", storage.brush.lookId);
					warnings_.push_back(std::move(row));
				}

				if (storage.brush.serverLookId > 0 && !IsKnownWorkbenchInspectorItemId(storage.brush.serverLookId)) {
					WarningRow row;
					row.severity = "Error";
					row.domain = "Brush";
					row.entityKind = "brush";
					row.entityId = brush.id;
					row.entityName = brush.name;
					row.issue = "Brush has invalid serverLookId";
					row.count = 1;
					row.status = "Active";
					row.details = wxString::Format("serverLookId: %d", storage.brush.serverLookId);
					warnings_.push_back(std::move(row));
				}

				if (storage.brush.lookId > 0 && storage.brush.serverLookId > 0) {
					WarningRow row;
					row.severity = "Warning";
					row.domain = "Brush";
					row.entityKind = "brush";
					row.entityId = brush.id;
					row.entityName = brush.name;
					row.issue = "Brush sets both lookId and serverLookId";
					row.count = 2;
					row.status = "Active";
					row.details = wxString::Format("lookId: %d\nserverLookId: %d", storage.brush.lookId, storage.brush.serverLookId);
					warnings_.push_back(std::move(row));
				}

				{
					std::set<int> invalidItemIds;
					for (const BrushItemRecord &item : storage.items) {
						if (!IsKnownWorkbenchInspectorItemId(item.itemId)) {
							invalidItemIds.insert(item.itemId);
						}
					}
					if (!invalidItemIds.empty()) {
						wxArrayString ids;
						for (int id : invalidItemIds) {
							ids.Add(wxString::Format("%d", id));
						}

						WarningRow row;
						row.severity = "Error";
						row.domain = "Brush";
						row.entityKind = "brush";
						row.entityId = brush.id;
						row.entityName = brush.name;
						row.issue = "Brush has invalid item ids";
						row.count = static_cast<int>(invalidItemIds.size());
						row.status = "Active";
						row.details = "Invalid item ids: " + wxJoin(ids, ',');
						warnings_.push_back(std::move(row));
					}
				}

				if (storage.brush.type == "ground") {
					if (storage.items.empty() && storage.borders.empty()) {
						WarningRow row;
						row.severity = "Error";
						row.domain = "Brush";
						row.entityKind = "brush";
						row.entityId = brush.id;
						row.entityName = brush.name;
						row.issue = "Ground brush has no items or borders";
						row.count = 1;
						row.status = "Active";
						row.details = "A ground brush with no items and no borders will not render meaningful content.";
						warnings_.push_back(std::move(row));
					}

					std::set<int64_t> missingBorderSetIds;
					std::vector<wxString> unresolvedTargets;
					for (const GroundBrushBorderRecord &border : storage.borders) {
						if (border.borderSetId > 0 && validBorderSetIds.find(border.borderSetId) == validBorderSetIds.end()) {
							missingBorderSetIds.insert(border.borderSetId);
						}
						if (border.targetMode == "brush" && !border.targetBrushName.IsEmpty() && border.targetBrushId <= 0) {
							unresolvedTargets.push_back(border.targetBrushName);
						}
					}

					if (!missingBorderSetIds.empty()) {
						wxArrayString ids;
						for (int64_t id : missingBorderSetIds) {
							ids.Add(wxString::Format("%lld", static_cast<long long>(id)));
						}

						WarningRow row;
						row.severity = "Error";
						row.domain = "Brush";
						row.entityKind = "brush";
						row.entityId = brush.id;
						row.entityName = brush.name;
						row.issue = "Ground brush references missing border sets";
						row.count = static_cast<int>(missingBorderSetIds.size());
						row.status = "Active";
						row.details = "Missing border set ids: " + wxJoin(ids, ',');
						warnings_.push_back(std::move(row));
					}

					if (!unresolvedTargets.empty()) {
						wxArrayString targets;
						for (const wxString &name : unresolvedTargets) {
							targets.Add(name);
						}

						WarningRow row;
						row.severity = "Warning";
						row.domain = "Brush";
						row.entityKind = "brush";
						row.entityId = brush.id;
						row.entityName = brush.name;
						row.issue = "Ground brush has unresolved border targets";
						row.count = static_cast<int>(unresolvedTargets.size());
						row.status = "Active";
						row.details = "Targets: " + wxJoin(targets, ',');
						warnings_.push_back(std::move(row));
					}
				}

				{
					std::vector<wxString> unresolvedLinks;
					for (const BrushLinkRecord &link : storage.links) {
						if (!link.targetBrushName.IsEmpty() && link.targetBrushName.CmpNoCase("all") != 0 && link.targetBrushId <= 0) {
							unresolvedLinks.push_back(link.targetBrushName);
						}
					}
					if (!unresolvedLinks.empty()) {
						wxArrayString targets;
						for (const wxString &name : unresolvedLinks) {
							targets.Add(name);
						}

						WarningRow row;
						row.severity = "Warning";
						row.domain = "Brush";
						row.entityKind = "brush";
						row.entityId = brush.id;
						row.entityName = brush.name;
						row.issue = "Brush has unresolved links";
						row.count = static_cast<int>(unresolvedLinks.size());
						row.status = "Active";
						row.details = "Targets: " + wxJoin(targets, ',');
						warnings_.push_back(std::move(row));
					}
				}

				if (storage.brush.type == "doodad") {
					if (storage.doodadAlternatives.empty()) {
						WarningRow row;
						row.severity = "Error";
						row.domain = "Brush";
						row.entityKind = "brush";
						row.entityId = brush.id;
						row.entityName = brush.name;
						row.issue = "Doodad has no alternatives";
						row.count = 1;
						row.status = "Active";
						row.details = "Doodad brushes should define at least one alternative.";
						warnings_.push_back(std::move(row));
					} else {
						std::vector<int> emptyAlternatives;
						std::vector<int> emptyComposites;
						std::vector<int> emptyTiles;
						std::set<int> invalidItemIds;

						for (size_t altIndex = 0; altIndex < storage.doodadAlternatives.size(); ++altIndex) {
							const DoodadAlternativeRecord &alt = storage.doodadAlternatives[altIndex];
							if (alt.singleItems.empty() && alt.composites.empty()) {
								emptyAlternatives.push_back(static_cast<int>(altIndex));
							}
							for (const DoodadSingleItemRecord &single : alt.singleItems) {
								if (!IsKnownWorkbenchInspectorItemId(single.itemId)) {
									invalidItemIds.insert(single.itemId);
								}
							}
							for (size_t compositeIndex = 0; compositeIndex < alt.composites.size(); ++compositeIndex) {
								const DoodadCompositeRecord &composite = alt.composites[compositeIndex];
								if (composite.tiles.empty()) {
									emptyComposites.push_back(static_cast<int>(compositeIndex));
									continue;
								}
								for (size_t tileIndex = 0; tileIndex < composite.tiles.size(); ++tileIndex) {
									const DoodadCompositeTileRecord &tile = composite.tiles[tileIndex];
									if (tile.items.empty()) {
										emptyTiles.push_back(static_cast<int>(tileIndex));
										continue;
									}
									for (const DoodadCompositeTileItemRecord &tileItem : tile.items) {
										if (!IsKnownWorkbenchInspectorItemId(tileItem.itemId)) {
											invalidItemIds.insert(tileItem.itemId);
										}
									}
								}
							}
						}

						if (!emptyAlternatives.empty()) {
							wxArrayString indices;
							for (int idx : emptyAlternatives) {
								indices.Add(wxString::Format("%d", idx + 1));
							}

							WarningRow row;
							row.severity = "Warning";
							row.domain = "Brush";
							row.entityKind = "brush";
							row.entityId = brush.id;
							row.entityName = brush.name;
							row.issue = "Doodad has empty alternatives";
							row.count = static_cast<int>(emptyAlternatives.size());
							row.status = "Active";
							row.details = "Alternatives: " + wxJoin(indices, ',');
							warnings_.push_back(std::move(row));
						}

						if (!emptyComposites.empty()) {
							wxArrayString indices;
							for (int idx : emptyComposites) {
								indices.Add(wxString::Format("%d", idx + 1));
							}

							WarningRow row;
							row.severity = "Warning";
							row.domain = "Brush";
							row.entityKind = "brush";
							row.entityId = brush.id;
							row.entityName = brush.name;
							row.issue = "Doodad has empty composites";
							row.count = static_cast<int>(emptyComposites.size());
							row.status = "Active";
							row.details = "Composites: " + wxJoin(indices, ',');
							warnings_.push_back(std::move(row));
						}

						if (!emptyTiles.empty()) {
							WarningRow row;
							row.severity = "Warning";
							row.domain = "Brush";
							row.entityKind = "brush";
							row.entityId = brush.id;
							row.entityName = brush.name;
							row.issue = "Doodad has empty tiles";
							row.count = static_cast<int>(emptyTiles.size());
							row.status = "Active";
							row.details = "Some doodad composite tiles contain no items.";
							warnings_.push_back(std::move(row));
						}

						if (!invalidItemIds.empty()) {
							wxArrayString ids;
							for (int id : invalidItemIds) {
								ids.Add(wxString::Format("%d", id));
							}

							WarningRow row;
							row.severity = "Error";
							row.domain = "Brush";
							row.entityKind = "brush";
							row.entityId = brush.id;
							row.entityName = brush.name;
							row.issue = "Doodad has invalid item ids";
							row.count = static_cast<int>(invalidItemIds.size());
							row.status = "Active";
							row.details = "Invalid item ids: " + wxJoin(ids, ',');
							warnings_.push_back(std::move(row));
						}
					}
				}

				if (storage.brush.type == "wall_brush" || storage.brush.type == "wall" || storage.brush.type == "wall decoration") {
					if (storage.wallParts.empty()) {
						WarningRow row;
						row.severity = "Error";
						row.domain = "Wall";
						row.entityKind = "brush";
						row.entityId = brush.id;
						row.entityName = brush.name;
						row.issue = "Wall has no parts";
						row.count = 1;
						row.status = "Active";
						row.details = "Wall brushes should define at least one part type.";
						warnings_.push_back(std::move(row));
					} else {
						std::vector<wxString> emptyParts;
						std::set<int> invalidItemIds;
						int emptyPartTypeCount = 0;
						int invalidDoorTypeCount = 0;

						for (const WallPartRecord &part : storage.wallParts) {
							if (part.partType.IsEmpty()) {
								++emptyPartTypeCount;
							}
							if (part.items.empty() && part.doors.empty()) {
								emptyParts.push_back(part.partType.IsEmpty() ? "<empty>" : part.partType);
							}
							for (const WallPartItemRecord &item : part.items) {
								if (!IsKnownWorkbenchInspectorItemId(item.itemId)) {
									invalidItemIds.insert(item.itemId);
								}
							}
							for (const WallPartDoorRecord &door : part.doors) {
								if (door.doorType.IsEmpty()) {
									++invalidDoorTypeCount;
								}
								if (!IsKnownWorkbenchInspectorItemId(door.itemId)) {
									invalidItemIds.insert(door.itemId);
								}
							}
						}

						if (emptyPartTypeCount > 0) {
							WarningRow row;
							row.severity = "Warning";
							row.domain = "Wall";
							row.entityKind = "brush";
							row.entityId = brush.id;
							row.entityName = brush.name;
							row.issue = "Wall has empty part types";
							row.count = emptyPartTypeCount;
							row.status = "Active";
							row.details = "Some wall parts have an empty partType.";
							warnings_.push_back(std::move(row));
						}

						if (!emptyParts.empty()) {
							wxArrayString parts;
							for (const wxString &part : emptyParts) {
								parts.Add(part);
							}

							WarningRow row;
							row.severity = "Error";
							row.domain = "Wall";
							row.entityKind = "brush";
							row.entityId = brush.id;
							row.entityName = brush.name;
							row.issue = "Wall has empty parts";
							row.count = static_cast<int>(emptyParts.size());
							row.status = "Active";
							row.details = "Empty parts: " + wxJoin(parts, ',');
							warnings_.push_back(std::move(row));
						}

						if (invalidDoorTypeCount > 0) {
							WarningRow row;
							row.severity = "Warning";
							row.domain = "Wall";
							row.entityKind = "brush";
							row.entityId = brush.id;
							row.entityName = brush.name;
							row.issue = "Wall has doors with empty type";
							row.count = invalidDoorTypeCount;
							row.status = "Active";
							row.details = "Some wall doors have an empty doorType.";
							warnings_.push_back(std::move(row));
						}

						if (!invalidItemIds.empty()) {
							wxArrayString ids;
							for (int id : invalidItemIds) {
								ids.Add(wxString::Format("%d", id));
							}

							WarningRow row;
							row.severity = "Error";
							row.domain = "Wall";
							row.entityKind = "brush";
							row.entityId = brush.id;
							row.entityName = brush.name;
							row.issue = "Wall has invalid item ids";
							row.count = static_cast<int>(invalidItemIds.size());
							row.status = "Active";
							row.details = "Invalid item ids: " + wxJoin(ids, ',');
							warnings_.push_back(std::move(row));
						}
					}
				}
			}
		}
	}

	{
		std::vector<BorderSetRecord> borderSets;
		std::vector<BorderSetRecord> globalBorderSets;
		std::vector<BorderSetRecord> inlineBorderSets;
		if (g_brush_database.listBorderSetsByScope("global", globalBorderSets)) {
			borderSets.insert(borderSets.end(), globalBorderSets.begin(), globalBorderSets.end());
		}
		if (g_brush_database.listBorderSetsByScope("inline", inlineBorderSets)) {
			borderSets.insert(borderSets.end(), inlineBorderSets.begin(), inlineBorderSets.end());
		}

		for (const BorderSetRecord &borderSet : borderSets) {
			std::vector<BorderSetItemRecord> items;
			if (!g_brush_database.getBorderSetItems(borderSet.id, items)) {
				WarningRow row;
				row.severity = "Error";
				row.domain = "Border";
				row.entityKind = "border_set";
				row.entityId = borderSet.id;
				row.entityName = wxString::Format("Border set %lld", static_cast<long long>(borderSet.id));
				row.issue = "Failed to load border set items";
				row.count = 1;
				row.status = "Active";
				row.details = g_brush_database.getLastError();
				warnings_.push_back(std::move(row));
				continue;
			}

			if (items.empty()) {
				WarningRow row;
				row.severity = "Error";
				row.domain = "Border";
				row.entityKind = "border_set";
				row.entityId = borderSet.id;
				row.entityName = wxString::Format("Border set %lld", static_cast<long long>(borderSet.id));
				row.issue = "Border set has no items";
				row.count = 1;
				row.status = "Active";
				row.details = "This border set defines no edge items.";
				warnings_.push_back(std::move(row));
			}

			if (borderSet.groundEquivalent > 0 && !IsKnownWorkbenchInspectorItemId(borderSet.groundEquivalent)) {
				WarningRow row;
				row.severity = "Warning";
				row.domain = "Border";
				row.entityKind = "border_set";
				row.entityId = borderSet.id;
				row.entityName = wxString::Format("Border set %lld", static_cast<long long>(borderSet.id));
				row.issue = "Border set has invalid groundEquivalent";
				row.count = 1;
				row.status = "Active";
				row.details = wxString::Format("groundEquivalent: %d", borderSet.groundEquivalent);
				warnings_.push_back(std::move(row));
			}

			std::set<int> invalidItemIds;
			std::set<wxString> emptyEdges;
			for (const BorderSetItemRecord &item : items) {
				if (item.edge.IsEmpty()) {
					emptyEdges.insert("<empty>");
				}
				if (!IsKnownWorkbenchInspectorItemId(item.itemId)) {
					invalidItemIds.insert(item.itemId);
				}
			}

			if (!emptyEdges.empty()) {
				wxArrayString edges;
				for (const wxString &edge : emptyEdges) {
					edges.Add(edge);
				}

				WarningRow row;
				row.severity = "Warning";
				row.domain = "Border";
				row.entityKind = "border_set";
				row.entityId = borderSet.id;
				row.entityName = wxString::Format("Border set %lld", static_cast<long long>(borderSet.id));
				row.issue = "Border set has empty edges";
				row.count = static_cast<int>(emptyEdges.size());
				row.status = "Active";
				row.details = "Edges: " + wxJoin(edges, ',');
				warnings_.push_back(std::move(row));
			}

			if (!invalidItemIds.empty()) {
				wxArrayString ids;
				for (int id : invalidItemIds) {
					ids.Add(wxString::Format("%d", id));
				}

				WarningRow row;
				row.severity = "Error";
				row.domain = "Border";
				row.entityKind = "border_set";
				row.entityId = borderSet.id;
				row.entityName = wxString::Format("Border set %lld", static_cast<long long>(borderSet.id));
				row.issue = "Border set has invalid item ids";
				row.count = static_cast<int>(invalidItemIds.size());
				row.status = "Active";
				row.details = "Invalid item ids: " + wxJoin(ids, ',');
				warnings_.push_back(std::move(row));
			}

			if (globalBorderSetIds.find(borderSet.id) != globalBorderSetIds.end()) {
				std::vector<BorderSetUsageRecord> usages;
				if (g_brush_database.listBorderSetUsages(borderSet.id, usages) && usages.empty()) {
					WarningRow row;
					row.severity = "Warning";
					row.domain = "Border";
					row.entityKind = "border_set";
					row.entityId = borderSet.id;
					row.entityName = wxString::Format("Border set %lld", static_cast<long long>(borderSet.id));
					row.issue = "Global border set is unused";
					row.count = 0;
					row.status = "Active";
					row.details = "This global border set is not referenced by any brush.";
					warnings_.push_back(std::move(row));
				}
			}
		}
	}

	{
		std::vector<TilesetStorageRecord> tilesets;
		if (!g_brush_database.getAllTilesets(tilesets)) {
			WarningRow row;
			row.severity = "Error";
			row.domain = "Palette";
			row.entityKind = "database";
			row.issue = "Failed to load palettes";
			row.count = 1;
			row.status = "Active";
			row.details = g_brush_database.getLastError();
			warnings_.push_back(std::move(row));
		} else {
			for (const TilesetStorageRecord &tileset : tilesets) {
				for (const TilesetSectionRecord &section : tileset.sections) {
					for (const TilesetEntryRecord &entry : section.entries) {
						if (entry.entryKind.CmpNoCase("brush") == 0) {
							if (!entry.brushName.IsEmpty() && entry.brushId <= 0) {
								WarningRow row;
								row.severity = "Warning";
								row.domain = "Palette";
								row.entityKind = "palette";
								row.entityId = 0;
								row.entityName = tileset.name;
								row.issue = "Palette entry references missing brush";
								row.count = 1;
								row.status = "Active";
								row.details = wxString::Format("Section: %s\nBrush: %s\nSort order: %d",
									section.sectionType.c_str(),
									entry.brushName.c_str(),
									entry.sortOrder);
								warnings_.push_back(std::move(row));
							}
							continue;
						}
						if (entry.entryKind.CmpNoCase("item") == 0) {
							if (entry.itemId > 0 && !IsKnownWorkbenchInspectorItemId(entry.itemId)) {
								WarningRow row;
								row.severity = "Error";
								row.domain = "Palette";
								row.entityKind = "palette";
								row.entityId = 0;
								row.entityName = tileset.name;
								row.issue = "Palette item entry has invalid itemId";
								row.count = 1;
								row.status = "Active";
								row.details = wxString::Format("Section: %s\nitemId: %d\nSort order: %d",
									section.sectionType.c_str(),
									entry.itemId,
									entry.sortOrder);
								warnings_.push_back(std::move(row));
							}
							if (entry.fromItemId > 0 && !IsKnownWorkbenchInspectorItemId(entry.fromItemId)) {
								WarningRow row;
								row.severity = "Error";
								row.domain = "Palette";
								row.entityKind = "palette";
								row.entityId = 0;
								row.entityName = tileset.name;
								row.issue = "Palette item entry has invalid fromItemId";
								row.count = 1;
								row.status = "Active";
								row.details = wxString::Format("Section: %s\nfromItemId: %d\nSort order: %d",
									section.sectionType.c_str(),
									entry.fromItemId,
									entry.sortOrder);
								warnings_.push_back(std::move(row));
							}
							if (entry.toItemId > 0 && !IsKnownWorkbenchInspectorItemId(entry.toItemId)) {
								WarningRow row;
								row.severity = "Error";
								row.domain = "Palette";
								row.entityKind = "palette";
								row.entityId = 0;
								row.entityName = tileset.name;
								row.issue = "Palette item entry has invalid toItemId";
								row.count = 1;
								row.status = "Active";
								row.details = wxString::Format("Section: %s\ntoItemId: %d\nSort order: %d",
									section.sectionType.c_str(),
									entry.toItemId,
									entry.sortOrder);
								warnings_.push_back(std::move(row));
							}
							if (entry.afterItemId > 0 && !IsKnownWorkbenchInspectorItemId(entry.afterItemId)) {
								WarningRow row;
								row.severity = "Error";
								row.domain = "Palette";
								row.entityKind = "palette";
								row.entityId = 0;
								row.entityName = tileset.name;
								row.issue = "Palette item entry has invalid afterItemId";
								row.count = 1;
								row.status = "Active";
								row.details = wxString::Format("Section: %s\nafterItemId: %d\nSort order: %d",
									section.sectionType.c_str(),
									entry.afterItemId,
									entry.sortOrder);
								warnings_.push_back(std::move(row));
							}
						}
					}
				}
			}
		}
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

	RebuildIssueFilterChoices();
	ApplyWarningFilter();
}

void MaterialsWorkbenchInspectorDialog::RebuildIssueFilterChoices() {
	if (!warningIssueChoice_) {
		return;
	}

	const wxString previousSelection = warningIssueChoice_->GetStringSelection();

	std::set<wxString> uniqueIssues;
	for (const WarningRow &row : warnings_) {
		if (!row.issue.IsEmpty() && row.issue != "No warnings") {
			uniqueIssues.insert(row.issue);
		}
	}

	warningIssueChoice_->Freeze();
	warningIssueChoice_->Clear();
	warningIssueChoice_->Append("All issues");
	for (const wxString &issue : uniqueIssues) {
		warningIssueChoice_->Append(issue);
	}

	int restoreIndex = wxNOT_FOUND;
	if (!previousSelection.IsEmpty() && previousSelection != "All issues") {
		restoreIndex = warningIssueChoice_->FindString(previousSelection);
	}
	warningIssueChoice_->SetSelection(restoreIndex != wxNOT_FOUND ? restoreIndex : 0);
	warningIssueChoice_->Thaw();
}

void MaterialsWorkbenchInspectorDialog::ApplyWarningFilter() {
	filteredWarningIndices_.clear();
	const wxString query = NormalizeQuery(warningSearchCtrl_ ? warningSearchCtrl_->GetValue() : "");
	const wxString severityFilter = warningSeverityChoice_ ? warningSeverityChoice_->GetStringSelection() : "All severities";
	const wxString domainFilter = warningDomainChoice_ ? warningDomainChoice_->GetStringSelection() : "All domains";
	const wxString issueFilter = warningIssueChoice_ ? warningIssueChoice_->GetStringSelection() : "All issues";

	for (size_t i = 0; i < warnings_.size(); ++i) {
		const WarningRow &row = warnings_[i];
		if (severityFilter != "All severities" && row.severity != severityFilter) {
			continue;
		}
		if (domainFilter != "All domains" && row.domain != domainFilter) {
			continue;
		}
		if (issueFilter != "All issues" && row.issue != issueFilter) {
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

	bool canGoTo = false;
	if (goToHandler_) {
		canGoTo = CanGoToEntity(row.entityKind, row.entityId, row.entityName);
	}
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
	if (!CanGoToEntity(row.entityKind, row.entityId, row.entityName)) {
		return;
	}
	goToHandler_(row.entityKind, row.entityId, row.entityName);
}
