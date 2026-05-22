#include "main.h"

#include "sqlite_materials_inspector.h"
#include "gui.h"

namespace {
	wxString BoolToText(bool value) {
		return value ? "yes" : "no";
	}

	wxString FormatAuditReport(const BrushDatabase &database, const MaterialsDatabaseAuditReport &report) {
		wxString text;
		text << "Database: " << database.getDatabasePath() << "\n";
		text << "Schema version: " << database.getExpectedSchemaVersion() << "\n\n";
		text << "Brushes: " << report.brushCount << "\n";
		text << "Border sets: " << report.borderSetCount << "\n";
		text << "Tilesets: " << report.tilesetCount << "\n";
		text << "Tileset sections: " << report.tilesetSectionCount << "\n";
		text << "Tileset entries: " << report.tilesetEntryCount << "\n\n";
		text << "Unresolved ground targets: " << report.unresolvedGroundTargets << "\n";
		text << "Unresolved brush links: " << report.unresolvedBrushLinks << "\n";
		text << "Unresolved tileset entries: " << report.unresolvedTilesetEntries << "\n\n";
		text << "Brush counts by type:\n";
		for (const BrushTypeCountRecord &typeCount : report.brushTypeCounts) {
			text << "  - " << typeCount.type << ": " << typeCount.count << "\n";
		}
		return text;
	}

	wxString FormatBrushDetails(const BrushStorageRecord &storage) {
		const BrushRecord &brush = storage.brush;

		wxString text;
		text << "Name: " << brush.name << "\n";
		text << "Type: " << brush.type << "\n";
		text << "ID: " << brush.id << "\n";
		text << "Source: " << brush.sourceFile << "\n\n";
		text << "lookId: " << brush.lookId << "\n";
		text << "serverLookId: " << brush.serverLookId << "\n";
		text << "zOrder: " << brush.zOrder << "\n";
		text << "thickness: " << brush.thickness << "\n";
		text << "thicknessCeiling: " << brush.thicknessCeiling << "\n\n";
		text << "Flags:\n";
		text << "  draggable: " << BoolToText(brush.draggable) << "\n";
		text << "  onBlocking: " << BoolToText(brush.onBlocking) << "\n";
		text << "  onDuplicate: " << BoolToText(brush.onDuplicate) << "\n";
		text << "  redoBorders: " << BoolToText(brush.redoBorders) << "\n";
		text << "  randomize: " << BoolToText(brush.randomize) << "\n";
		text << "  oneSize: " << BoolToText(brush.oneSize) << "\n";
		text << "  soloOptional: " << BoolToText(brush.soloOptional) << "\n\n";

		text << "Brush items: " << storage.items.size() << "\n";
		for (const BrushItemRecord &item : storage.items) {
			text << "  - item=" << item.itemId << " chance=" << item.chance << " sort=" << item.sortOrder << "\n";
		}
		text << "\nGround borders: " << storage.borders.size() << "\n";
		for (const GroundBrushBorderRecord &border : storage.borders) {
			text << "  - role=" << border.borderRole
				 << " align=" << border.align
				 << " targetMode=" << border.targetMode
				 << " targetBrush=" << border.targetBrushName
				 << " borderSetId=" << border.borderSetId
				 << " cases=" << border.cases.size() << "\n";
		}
		text << "\nLinks: " << storage.links.size() << "\n";
		for (const BrushLinkRecord &link : storage.links) {
			text << "  - " << link.relationType << " -> " << link.targetBrushName << " (id=" << link.targetBrushId << ")\n";
		}
		text << "\nWall parts: " << storage.wallParts.size() << "\n";
		for (const WallPartRecord &part : storage.wallParts) {
			text << "  - " << part.partType << " items=" << part.items.size() << " doors=" << part.doors.size() << "\n";
		}
		text << "\nCarpet nodes: " << storage.carpetNodes.size() << "\n";
		for (const CarpetNodeRecord &node : storage.carpetNodes) {
			text << "  - align=" << node.align << " items=" << node.items.size() << "\n";
		}
		text << "\nTable nodes: " << storage.tableNodes.size() << "\n";
		for (const TableNodeRecord &node : storage.tableNodes) {
			text << "  - align=" << node.align << " items=" << node.items.size() << "\n";
		}
		text << "\nDoodad alternatives: " << storage.doodadAlternatives.size() << "\n";
		for (const DoodadAlternativeRecord &alternative : storage.doodadAlternatives) {
			text << "  - singleItems=" << alternative.singleItems.size() << " composites=" << alternative.composites.size() << "\n";
		}

		return text;
	}

	wxString FormatTilesetDetails(const TilesetStorageRecord &tileset) {
		wxString text;
		text << "Name: " << tileset.name << "\n";
		text << "Source: " << tileset.sourceFile << "\n";
		text << "Sections: " << tileset.sections.size() << "\n\n";

		for (const TilesetSectionRecord &section : tileset.sections) {
			text << "[" << section.sectionType << "] entries=" << section.entries.size() << "\n";
			for (const TilesetEntryRecord &entry : section.entries) {
				text << "  - kind=" << entry.entryKind;
				if (!entry.brushName.IsEmpty()) {
					text << " brush=" << entry.brushName;
				}
				if (entry.itemId > 0) {
					text << " item=" << entry.itemId;
				}
				if (entry.fromItemId > 0 || entry.toItemId > 0) {
					text << " range=" << entry.fromItemId << "-" << entry.toItemId;
				}
				if (!entry.afterBrushName.IsEmpty()) {
					text << " after=" << entry.afterBrushName;
				}
				if (entry.afterItemId > 0) {
					text << " afterItem=" << entry.afterItemId;
				}
				text << "\n";
			}
			text << "\n";
		}

		return text;
	}
} // namespace

SQLiteMaterialsInspectorDialog::SQLiteMaterialsInspectorDialog(wxWindow* parent) :
	wxDialog(parent, wxID_ANY, "SQLite Materials Inspector", wxDefaultPosition, wxSize(1000, 700), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
	wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* toolbarSizer = new wxBoxSizer(wxHORIZONTAL);
	wxButton* reloadButton = new wxButton(this, wxID_REFRESH, "Reload");
	toolbarSizer->Add(reloadButton, 0, wxALL, FromDIP(5));
	topSizer->Add(toolbarSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(5));

	notebook_ = new wxNotebook(this, wxID_ANY);

	wxPanel* summaryPanel = new wxPanel(notebook_);
	wxBoxSizer* summarySizer = new wxBoxSizer(wxVERTICAL);
	summaryText_ = new wxTextCtrl(summaryPanel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
	summarySizer->Add(summaryText_, 1, wxEXPAND | wxALL, FromDIP(5));
	summaryPanel->SetSizer(summarySizer);
	notebook_->AddPage(summaryPanel, "Summary");

	wxPanel* brushesPanel = new wxPanel(notebook_);
	wxBoxSizer* brushesSizer = new wxBoxSizer(wxVERTICAL);
	wxArrayString brushTypes;
	brushTypes.Add("ground");
	brushTypes.Add("wall");
	brushTypes.Add("doodad");
	brushTypes.Add("carpet");
	brushTypes.Add("table");
	brushTypeChoice_ = new wxChoice(brushesPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, brushTypes);
	brushTypeChoice_->SetSelection(0);
	brushesSizer->Add(brushTypeChoice_, 0, wxEXPAND | wxALL, FromDIP(5));

	wxSplitterWindow* brushesSplitter = new wxSplitterWindow(brushesPanel, wxID_ANY);
	brushList_ = new wxListBox(brushesSplitter, wxID_ANY);
	brushDetailsText_ = new wxTextCtrl(brushesSplitter, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
	brushesSplitter->SplitVertically(brushList_, brushDetailsText_, FromDIP(280));
	brushesSplitter->SetMinimumPaneSize(FromDIP(180));
	brushesSizer->Add(brushesSplitter, 1, wxEXPAND | wxALL, FromDIP(5));
	brushesPanel->SetSizer(brushesSizer);
	notebook_->AddPage(brushesPanel, "Brushes");

	wxPanel* tilesetsPanel = new wxPanel(notebook_);
	wxBoxSizer* tilesetsSizer = new wxBoxSizer(wxVERTICAL);
	wxSplitterWindow* tilesetsSplitter = new wxSplitterWindow(tilesetsPanel, wxID_ANY);
	tilesetList_ = new wxListBox(tilesetsSplitter, wxID_ANY);
	tilesetDetailsText_ = new wxTextCtrl(tilesetsSplitter, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
	tilesetsSplitter->SplitVertically(tilesetList_, tilesetDetailsText_, FromDIP(280));
	tilesetsSplitter->SetMinimumPaneSize(FromDIP(180));
	tilesetsSizer->Add(tilesetsSplitter, 1, wxEXPAND | wxALL, FromDIP(5));
	tilesetsPanel->SetSizer(tilesetsSizer);
	notebook_->AddPage(tilesetsPanel, "Tilesets");

	topSizer->Add(notebook_, 1, wxEXPAND | wxALL, FromDIP(5));
	topSizer->Add(CreateSeparatedButtonSizer(wxCLOSE), 0, wxEXPAND | wxALL, FromDIP(5));
	SetSizer(topSizer);

	reloadButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { ReloadData(); });
	brushTypeChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) { RefreshBrushList(); });
	brushList_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent &) { RefreshBrushDetails(); });
	tilesetList_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent &) { RefreshTilesetDetails(); });

	ReloadData();
}

void SQLiteMaterialsInspectorDialog::ReloadData() {
	const auto resetInspectorState = [this](const wxString &summary, const wxString &brushDetails, const wxString &tilesetDetails) {
		currentBrushes_.clear();
		tilesets_.clear();
		brushList_->Clear();
		brushDetailsText_->SetValue(brushDetails);
		tilesetList_->Clear();
		tilesetDetailsText_->SetValue(tilesetDetails);
		summaryText_->SetValue(summary);
	};

	if (g_gui.IsAsyncSqliteBootstrapRunning()) {
		const wxString message = "SQLite materials database is currently being built in background. Please reopen or reload this inspector when the bootstrap import finishes.";
		resetInspectorState(message, message, message);
		return;
	}

	if (!g_brush_database.isOpen()) {
		resetInspectorState("SQLite brush database is not open.", "", "");
		inspectorDatabase_.close();
		return;
	}

	if (!inspectorDatabase_.openReadOnly(g_brush_database.getDatabasePath())) {
		const wxString error = inspectorDatabase_.getLastError();
		resetInspectorState(error, error, error);
		return;
	}

	if (!inspectorDatabase_.generateAuditReport(auditReport_)) {
		const wxString error = inspectorDatabase_.getLastError();
		resetInspectorState(error, error, error);
		return;
	}
	if (!inspectorDatabase_.getAllTilesets(tilesets_)) {
		const wxString error = inspectorDatabase_.getLastError();
		resetInspectorState(error, error, error);
		return;
	}

	RefreshSummary();
	RefreshBrushList();
	RefreshTilesetList();
}

void SQLiteMaterialsInspectorDialog::RefreshSummary() {
	summaryText_->SetValue(FormatAuditReport(inspectorDatabase_, auditReport_));
}

void SQLiteMaterialsInspectorDialog::RefreshBrushList() {
	currentBrushes_.clear();
	brushList_->Clear();
	brushDetailsText_->Clear();

	const wxString selectedType = brushTypeChoice_->GetStringSelection();
	if (!inspectorDatabase_.listBrushesByType(selectedType, currentBrushes_)) {
		brushDetailsText_->SetValue(inspectorDatabase_.getLastError());
		return;
	}

	for (const BrushRecord &brush : currentBrushes_) {
		brushList_->Append(brush.name);
	}

	if (!currentBrushes_.empty()) {
		brushList_->SetSelection(0);
		RefreshBrushDetails();
	}
}

void SQLiteMaterialsInspectorDialog::RefreshBrushDetails() {
	brushDetailsText_->Clear();

	const int selection = brushList_->GetSelection();
	if (selection == wxNOT_FOUND || selection >= static_cast<int>(currentBrushes_.size())) {
		return;
	}

	BrushStorageRecord storage;
	if (!inspectorDatabase_.getCompleteBrushById(currentBrushes_[selection].id, storage)) {
		brushDetailsText_->SetValue(inspectorDatabase_.getLastError());
		return;
	}

	brushDetailsText_->SetValue(FormatBrushDetails(storage));
}

void SQLiteMaterialsInspectorDialog::RefreshTilesetList() {
	tilesetList_->Clear();
	tilesetDetailsText_->Clear();

	for (const TilesetStorageRecord &tileset : tilesets_) {
		tilesetList_->Append(tileset.name);
	}

	if (!tilesets_.empty()) {
		tilesetList_->SetSelection(0);
		RefreshTilesetDetails();
	}
}

void SQLiteMaterialsInspectorDialog::RefreshTilesetDetails() {
	tilesetDetailsText_->Clear();

	const int selection = tilesetList_->GetSelection();
	if (selection == wxNOT_FOUND || selection >= static_cast<int>(tilesets_.size())) {
		return;
	}

	tilesetDetailsText_->SetValue(FormatTilesetDetails(tilesets_[selection]));
}
