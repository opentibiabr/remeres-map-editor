#include "main.h"

#include "sqlite_materials_inspector.h"
#include "brush.h"
#include "gui.h"

namespace {
	wxString BoolToText(bool value) {
		return value ? "yes" : "no";
	}

	wxString JoinTypeCounts(const std::vector<BrushTypeCountRecord> &typeCounts) {
		wxString out;
		for (size_t i = 0; i < typeCounts.size(); ++i) {
			if (i > 0) {
				out += ", ";
			}
			out += wxString::Format("%s (%d)", typeCounts[i].type, typeCounts[i].count);
		}
		return out;
	}

	bool HasBrushType(const MaterialsDatabaseAuditReport &report, const wxString &type) {
		for (const BrushTypeCountRecord &typeCount : report.brushTypeCounts) {
			if (typeCount.type == type && typeCount.count > 0) {
				return true;
			}
		}
		return false;
	}

	bool TryGetCurrentSchemaVersion(BrushDatabase &database, int &outCurrentSchemaVersion, wxString &outSummary) {
		outCurrentSchemaVersion = 0;
		if (!database.getCurrentSchemaVersion(outCurrentSchemaVersion)) {
			outSummary = "Runtime-ready: unknown\nRuntime-ready reason: Failed to read database schema version.\n";
			return false;
		}
		return true;
	}

	wxString FormatSchemaMismatchSummary(int currentSchemaVersion, int expectedSchemaVersion) {
		return wxString::Format(
			"Runtime-ready: no\nRuntime-ready reason: Schema mismatch (found %d, expected %d).\n",
			currentSchemaVersion,
			expectedSchemaVersion
		);
	}

	bool HasRequiredBrushTypesForRuntime(const MaterialsDatabaseAuditReport &report) {
		return HasBrushType(report, "ground")
			&& HasBrushType(report, "wall")
			&& HasBrushType(report, "doodad")
			&& HasBrushType(report, "carpet")
			&& HasBrushType(report, "table");
	}

	bool HasNoUnresolvedReferencesForRuntime(const MaterialsDatabaseAuditReport &report) {
		return report.unresolvedGroundTargets == 0
			&& report.unresolvedBrushLinks == 0
			&& report.unresolvedTilesetEntries == 0
			&& report.unresolvedCaseMatchBorderIds == 0
			&& report.unresolvedCaseReplaceBorderTargetIds == 0;
	}

	wxString BuildUnresolvedTilesetEntryExamples(const MaterialsDatabaseAuditReport &report, size_t maxSamples) {
		if (report.unresolvedTilesetEntries <= 0 || report.unresolvedTilesetEntrySamples.empty()) {
			return wxString();
		}

		wxString detail;
		const size_t sampleCount = std::min(maxSamples, report.unresolvedTilesetEntrySamples.size());
		for (size_t i = 0; i < sampleCount; ++i) {
			const UnresolvedTilesetEntrySampleRecord &sample = report.unresolvedTilesetEntrySamples[i];
			if (i > 0) {
				detail += "; ";
			}
			wxString group = sample.paletteGroupName;
			group.Trim(true);
			group.Trim(false);
			if (group.IsEmpty()) {
				group = "<unknown>";
			}
			wxString tileset = sample.tilesetName;
			tileset.Trim(true);
			tileset.Trim(false);
			if (tileset.IsEmpty()) {
				tileset = "<unknown>";
			}
			wxString section = sample.sectionType;
			section.Trim(true);
			section.Trim(false);
			if (section.IsEmpty()) {
				section = "<unknown>";
			}
			wxString brush = sample.brushName;
			brush.Trim(true);
			brush.Trim(false);
			if (brush.IsEmpty()) {
				brush = "<unknown>";
			}
			detail += wxString::Format("group=\"%s\" palette=\"%s\" section=\"%s\" brush=\"%s\"", group, tileset, section, brush);
		}
		return detail;
	}

	wxString BuildRuntimeNotReadyReason(BrushDatabase &database, const MaterialsDatabaseAuditReport &report, const MaterialsImportStatusRecord* status) {
		const bool hasRequiredBrushTypes = HasRequiredBrushTypesForRuntime(report);
		if (!hasRequiredBrushTypes) {
			return "Missing required brush types (expected ground, wall, doodad, carpet, table).";
		}
		if (!(report.borderSetCount > 0 && report.tilesetCount > 0)) {
			return "Missing border sets or tilesets.";
		}
		if (report.unsupportedBrushTypeCount != 0) {
			const wxString detail = JoinTypeCounts(report.unsupportedBrushTypeCounts);
			if (!detail.IsEmpty()) {
				return wxString::Format("Database contains unsupported brush types (%d): %s.", report.unsupportedBrushTypeCount, detail);
			}
			return wxString::Format("Database contains unsupported brush types (%d).", report.unsupportedBrushTypeCount);
		}
		if (!HasNoUnresolvedReferencesForRuntime(report)) {
			wxString reason = wxString::Format(
				"Database contains unresolved references (ground targets=%d, brush links=%d, tileset entries=%d, match_border ids=%d, replace_border target ids=%d).",
				report.unresolvedGroundTargets,
				report.unresolvedBrushLinks,
				report.unresolvedTilesetEntries,
				report.unresolvedCaseMatchBorderIds,
				report.unresolvedCaseReplaceBorderTargetIds
			);
			const wxString examples = BuildUnresolvedTilesetEntryExamples(report, 3);
			if (!examples.IsEmpty()) {
				reason += " Examples: " + examples + ".";
			}
			return reason;
		}

		const bool markerComplete = status && status->completed;
		if (!markerComplete) {
			return "Import marker is incomplete.";
		}
		return wxString();
	}

	wxString BuildRuntimeReadySummary(BrushDatabase &database, const MaterialsImportStatusRecord* status, bool markerComplete) {
		if (markerComplete) {
			return "Runtime-ready: yes\n\n";
		}
		if (database.isReadOnly()) {
			return "Runtime-ready: yes\nRuntime-ready note: Import marker is incomplete, but database is read-only and audit checks are clean.\n\n";
		}
		return "Runtime-ready: yes\nRuntime-ready note: Import marker is incomplete, but audit checks are clean. The marker can be auto-written on a writable database.\n\n";
	}

	wxString BuildRuntimeReadinessSummary(BrushDatabase &database, const MaterialsDatabaseAuditReport &report, const MaterialsImportStatusRecord* status) {
		const int expectedSchemaVersion = database.getExpectedSchemaVersion();
		int currentSchemaVersion = 0;
		wxString schemaReadFailure;
		if (!TryGetCurrentSchemaVersion(database, currentSchemaVersion, schemaReadFailure)) {
			return schemaReadFailure;
		}
		if (currentSchemaVersion != expectedSchemaVersion) {
			return FormatSchemaMismatchSummary(currentSchemaVersion, expectedSchemaVersion);
		}

		const bool hasRequiredBrushTypes = HasRequiredBrushTypesForRuntime(report);
		const bool hasCatalogBasics = report.borderSetCount > 0 && report.tilesetCount > 0;
		const bool hasNoUnsupportedBrushTypes = report.unsupportedBrushTypeCount == 0;
		const bool hasNoUnresolvedRefs = HasNoUnresolvedReferencesForRuntime(report);

		const bool readyByAudit = hasRequiredBrushTypes && hasCatalogBasics && hasNoUnsupportedBrushTypes && hasNoUnresolvedRefs;
		const bool markerComplete = status && status->completed;
		if (readyByAudit) {
			return BuildRuntimeReadySummary(database, status, markerComplete);
		}

		const wxString reason = BuildRuntimeNotReadyReason(database, report, status);

		wxString text;
		text << "Runtime-ready: no\n";
		if (!reason.IsEmpty()) {
			text << "Runtime-ready reason: " << reason << "\n";
		}
		text << "\n";
		return text;
	}

	wxString FormatAuditReport(BrushDatabase &database, const MaterialsDatabaseAuditReport &report) {
		wxString text;
		text << "Database: " << database.getDatabasePath() << "\n";
		int currentSchemaVersion = 0;
		if (database.getCurrentSchemaVersion(currentSchemaVersion)) {
			text << "Schema version (db): " << currentSchemaVersion << "\n";
		} else {
			text << "Schema version (db): unknown\n";
		}
		text << "Schema version (expected): " << database.getExpectedSchemaVersion() << "\n\n";
		MaterialsImportStatusRecord status;
		wxString statusReason;
		if (database.getMaterialsImportStatus(status, statusReason)) {
			text << "Import marker completed: " << BoolToText(status.completed) << "\n";
			text << "Import marker completed_at: " << static_cast<long long>(status.completedAt) << "\n";
			text << "Import marker source: " << status.source << "\n\n";
			text << BuildRuntimeReadinessSummary(database, report, &status);
		} else {
			text << "Import marker error: " << statusReason << "\n\n";
			text << BuildRuntimeReadinessSummary(database, report, nullptr);
		}
		text << "Brushes: " << report.brushCount << "\n";
		text << "Border sets: " << report.borderSetCount << "\n";
		text << "Tilesets: " << report.tilesetCount << "\n";
		text << "Tileset sections: " << report.tilesetSectionCount << "\n";
		text << "Tileset entries: " << report.tilesetEntryCount << "\n\n";
		text << "Unsupported brush types: " << report.unsupportedBrushTypeCount << "\n\n";
		if (report.unsupportedBrushTypeCount > 0) {
			if (!report.unsupportedBrushTypeCounts.empty()) {
				text << "Unsupported brush types breakdown:\n";
				for (const BrushTypeCountRecord &typeCount : report.unsupportedBrushTypeCounts) {
					text << wxString::Format("  - %s: %d\n", typeCount.type, typeCount.count);
				}
				text << "\n";
			}
			if (!report.unsupportedBrushSamples.empty()) {
				text << "Unsupported brush samples:\n";
				for (const UnsupportedBrushSampleRecord &sample : report.unsupportedBrushSamples) {
					wxString source = sample.sourceFile;
					source.Trim(true);
					source.Trim(false);
					if (source.IsEmpty()) {
						source = "<unknown>";
					}
					text << wxString::Format(
						"  - id=%lld name=\"%s\" type=\"%s\" source=\"%s\"\n",
						static_cast<long long>(sample.id),
						sample.name,
						sample.type,
						source
					);
				}
				text << "\n";
			}
		}
		text << "Unresolved ground targets: " << report.unresolvedGroundTargets << "\n";
		text << "Unresolved brush links: " << report.unresolvedBrushLinks << "\n";
		text << "Unresolved tileset entries: " << report.unresolvedTilesetEntries << "\n\n";
		if (report.unresolvedTilesetEntries > 0 && !report.unresolvedTilesetEntrySamples.empty()) {
			text << "Unresolved tileset entry samples:\n";
			for (const UnresolvedTilesetEntrySampleRecord &sample : report.unresolvedTilesetEntrySamples) {
				wxString group = sample.paletteGroupName;
				group.Trim(true);
				group.Trim(false);
				if (group.IsEmpty()) {
					group = "<unknown>";
				}
				wxString tileset = sample.tilesetName;
				tileset.Trim(true);
				tileset.Trim(false);
				if (tileset.IsEmpty()) {
					tileset = "<unknown>";
				}
				wxString source = sample.tilesetSourceFile;
				source.Trim(true);
				source.Trim(false);
				if (source.IsEmpty()) {
					source = "<unknown>";
				}
				wxString section = sample.sectionType;
				section.Trim(true);
				section.Trim(false);
				if (section.IsEmpty()) {
					section = "<unknown>";
				}
				wxString brush = sample.brushName;
				brush.Trim(true);
				brush.Trim(false);
				if (brush.IsEmpty()) {
					brush = "<unknown>";
				}
				wxString entryKind = sample.entryKind;
				entryKind.Trim(true);
				entryKind.Trim(false);
				if (entryKind.IsEmpty()) {
					entryKind = "<unknown>";
				}
				text << wxString::Format(
					"  - group=\"%s\" palette=\"%s\" section=\"%s\" kind=\"%s\" brush=\"%s\" source=\"%s\"\n",
					group,
					tileset,
					section,
					entryKind,
					brush,
					source
				);
			}
			text << "\n";
		}
		text << "Unresolved case match_border ids: " << report.unresolvedCaseMatchBorderIds << "\n";
		text << "Unresolved case replace_border target ids: " << report.unresolvedCaseReplaceBorderTargetIds << "\n";
		text << "Case match_border edges without borderitem: " << report.caseMatchBorderEdgesWithoutItem << "\n";
		text << "Case replace_border edges without borderitem: " << report.caseReplaceBorderEdgesWithoutItem << "\n\n";
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
		text << "Look ID: " << brush.lookId << "\n";
		text << "Server look ID: " << brush.serverLookId << "\n";
		text << "Z order: " << brush.zOrder << "\n";
		text << "thickness: " << brush.thickness << "\n";
		text << "thicknessCeiling: " << brush.thicknessCeiling << "\n\n";
		text << "Flags:\n";
		text << "  draggable: " << BoolToText(brush.draggable) << "\n";
		text << "  onBlocking: " << BoolToText(brush.onBlocking) << "\n";
		text << "  onDuplicate: " << BoolToText(brush.onDuplicate) << "\n";
		text << "  redoBorders: " << BoolToText(brush.redoBorders) << "\n";
		text << "  removeOptionalBorder: " << BoolToText(brush.removeOptionalBorder) << "\n";
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
		text << "Group: " << (tileset.paletteGroupName.IsEmpty() ? wxString("other") : tileset.paletteGroupName) << "\n";
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

SQLiteMaterialsInspectorPanel::SQLiteMaterialsInspectorPanel(wxWindow* parent) :
	wxPanel(parent, wxID_ANY) {
	wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* toolbarSizer = new wxBoxSizer(wxHORIZONTAL);
	wxButton* reloadButton = new wxButton(this, wxID_REFRESH, "Reload");
	wxButton* resetDbButton = new wxButton(this, wxID_ANY, "Reset DB from XML...");
	toolbarSizer->Add(reloadButton, 0, wxALL, FromDIP(5));
	toolbarSizer->Add(resetDbButton, 0, wxALL, FromDIP(5));
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
	brushTypes.Add("wall decoration");
	brushTypes.Add("doodad");
	brushTypes.Add("carpet");
	brushTypes.Add("table");
	brushTypeChoice_ = new wxChoice(brushesPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, brushTypes);
	brushTypeChoice_->SetSelection(0);
	brushesSizer->Add(brushTypeChoice_, 0, wxEXPAND | wxALL, FromDIP(5));

	wxSplitterWindow* brushesSplitter = new wxSplitterWindow(brushesPanel, wxID_ANY);
	brushList_ = new wxListBox(brushesSplitter, wxID_ANY);
	brushDetailsNotebook_ = new wxNotebook(brushesSplitter, wxID_ANY);
	wxPanel* brushDetailsPanel = new wxPanel(brushDetailsNotebook_);
	wxBoxSizer* brushDetailsSizer = new wxBoxSizer(wxVERTICAL);
	brushDetailsText_ = new wxTextCtrl(brushDetailsPanel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
	brushDetailsSizer->Add(brushDetailsText_, 1, wxEXPAND | wxALL, FromDIP(5));
	brushDetailsPanel->SetSizer(brushDetailsSizer);
	brushDetailsNotebook_->AddPage(brushDetailsPanel, "Details");

	wxPanel* brushXmlPanel = new wxPanel(brushDetailsNotebook_);
	wxBoxSizer* brushXmlSizer = new wxBoxSizer(wxVERTICAL);
	brushRuntimeXmlText_ = new wxTextCtrl(brushXmlPanel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
	brushXmlSizer->Add(brushRuntimeXmlText_, 1, wxEXPAND | wxALL, FromDIP(5));
	brushXmlPanel->SetSizer(brushXmlSizer);
	brushDetailsNotebook_->AddPage(brushXmlPanel, "Runtime XML");

	brushesSplitter->SplitVertically(brushList_, brushDetailsNotebook_, FromDIP(280));
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
	SetSizer(topSizer);

	reloadButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { ReloadData(); });
	resetDbButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
		if (g_gui.IsAsyncSqliteBootstrapRunning()) {
			g_gui.PopupDialog(this, "SQLite Reset Unavailable", "SQLite bootstrap import is currently running. Wait for it to finish, then try again.", wxOK | wxICON_INFORMATION);
			return;
		}
		wxString dbPath;
		if (g_brush_database.isOpen()) {
			dbPath = g_brush_database.getDatabasePath();
		} else {
			dbPath = GUI::GetDataDirectory() + "materials/materials.db";
		}
		if (!wxFileName(dbPath).FileExists()) {
			g_gui.PopupDialog(this, "SQLite Reset Unavailable", "materials.db does not exist at the expected path.\n\nDatabase:\n" + dbPath, wxOK | wxICON_ERROR);
			return;
		}
		if (!wxFileName(dbPath).IsFileWritable()) {
			g_gui.PopupDialog(this, "SQLite Reset Unavailable", "materials.db is read-only. Reset requires a writable database file.\n\nDatabase:\n" + dbPath, wxOK | wxICON_ERROR);
			return;
		}
		wxString warningText = "Reset SQLite materials database from legacy XML?\n\n";
		warningText += "This will move the current materials.db to a timestamped backup file and close the database for this session (when open).\n";
		warningText += "Warning: This discards all edits made in materials.db since the last bootstrap. Use Export/Import if you need to keep changes.\n\n";
		warningText += "Database:\n";
		warningText += dbPath;

		if (wxMessageBox(warningText, "Reset materials.db", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES) {
			return;
		}

		const wxDateTime now = wxDateTime::Now();
		const wxString suffix = now.Format("-%Y%m%d-%H%M%S");
		const wxString backupPath = dbPath + ".bak" + suffix;

		if (g_brush_database.isOpen()) {
			g_brush_database.close();
		}
		inspectorDatabase_.close();

		auto moveFileIfExists = [](const wxString &from, const wxString &to) -> bool {
			if (wxFileName(from).FileExists()) {
				return wxRenameFile(from, to, true);
			}
			return true;
		};

		if (!moveFileIfExists(dbPath, backupPath)
			|| !moveFileIfExists(dbPath + "-wal", backupPath + "-wal")
			|| !moveFileIfExists(dbPath + "-shm", backupPath + "-shm")) {
			g_gui.PopupDialog(this, "SQLite Reset Failed", "Failed to move one or more SQLite files to the backup path.\n\nBackup:\n" + backupPath, wxOK | wxICON_ERROR);
			return;
		}

		wxString doneText = "materials.db was moved to:\n" + backupPath + "\n\n";
		doneText += "Technical note:\n";
		doneText += "- This process cannot safely rebuild and reload the materials graph in-place.\n";
		doneText += "- A restart is required so the next startup can bootstrap a fresh SQLite DB from XML.\n\n";
		doneText += "Next steps:\n";
		doneText += "- Restart the app\n";
		doneText += "- The SQLite database will be rebuilt from legacy XML automatically\n\n";
		doneText += "Note: Workbench editing from SQLite is disabled until restart.";

		wxMessageDialog dialog(this, doneText, "SQLite Reset Scheduled", wxOK | wxICON_INFORMATION);
		dialog.SetOKLabel("Close now");
		dialog.ShowModal();
		if (g_gui.root) {
			g_gui.root->Close(true);
		} else if (wxTheApp) {
			wxTheApp->ExitMainLoop();
		}
		return;
	});
	brushTypeChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) { RefreshBrushList(); });
	brushList_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent &) { RefreshBrushDetails(); });
	tilesetList_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent &) { RefreshTilesetDetails(); });

	ReloadData();
}

void SQLiteMaterialsInspectorPanel::ReloadData() {
	const auto resetInspectorState = [this](const wxString &summary, const wxString &brushDetails, const wxString &tilesetDetails) {
		currentBrushes_.clear();
		tilesets_.clear();
		brushList_->Clear();
		brushDetailsText_->SetValue(brushDetails);
		if (brushRuntimeXmlText_) {
			brushRuntimeXmlText_->SetValue(brushDetails);
		}
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

	const wxString previousBrushType = brushTypeChoice_ ? brushTypeChoice_->GetStringSelection() : wxString();
	if (brushTypeChoice_) {
		brushTypeChoice_->Freeze();
		brushTypeChoice_->Clear();
		brushTypeChoice_->Append("ground");
		brushTypeChoice_->Append("wall");
		brushTypeChoice_->Append("wall decoration");
		brushTypeChoice_->Append("doodad");
		brushTypeChoice_->Append("carpet");
		brushTypeChoice_->Append("table");
		for (const BrushTypeCountRecord &unsupported : auditReport_.unsupportedBrushTypeCounts) {
			if (!unsupported.type.IsEmpty()) {
				brushTypeChoice_->Append(unsupported.type);
			}
		}
		int selection = brushTypeChoice_->FindString(previousBrushType, true);
		if (selection == wxNOT_FOUND) {
			selection = 0;
		}
		brushTypeChoice_->SetSelection(selection);
		brushTypeChoice_->Thaw();
	}

	RefreshSummary();
	RefreshBrushList();
	RefreshTilesetList();
}

void SQLiteMaterialsInspectorPanel::RefreshSummary() {
	summaryText_->SetValue(FormatAuditReport(inspectorDatabase_, auditReport_));
}

void SQLiteMaterialsInspectorPanel::RefreshBrushList() {
	currentBrushes_.clear();
	brushList_->Clear();
	brushDetailsText_->Clear();
	if (brushRuntimeXmlText_) {
		brushRuntimeXmlText_->Clear();
	}

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

void SQLiteMaterialsInspectorPanel::RefreshBrushDetails() {
	brushDetailsText_->Clear();
	if (brushRuntimeXmlText_) {
		brushRuntimeXmlText_->Clear();
	}

	const int selection = brushList_->GetSelection();
	if (selection == wxNOT_FOUND || selection >= static_cast<int>(currentBrushes_.size())) {
		return;
	}

	BrushStorageRecord storage;
	if (!inspectorDatabase_.getCompleteBrushById(currentBrushes_[selection].id, storage)) {
		brushDetailsText_->SetValue(inspectorDatabase_.getLastError());
		if (brushRuntimeXmlText_) {
			brushRuntimeXmlText_->SetValue(inspectorDatabase_.getLastError());
		}
		return;
	}

	brushDetailsText_->SetValue(FormatBrushDetails(storage));
	if (brushRuntimeXmlText_) {
		wxString xml;
		wxString xmlError;
		if (g_brushes.buildBrushXmlFromStorage(storage, xml, xmlError)) {
			brushRuntimeXmlText_->SetValue(xml);
		} else {
			brushRuntimeXmlText_->SetValue(xmlError);
		}
	}
}

void SQLiteMaterialsInspectorPanel::RefreshTilesetList() {
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

void SQLiteMaterialsInspectorPanel::RefreshTilesetDetails() {
	tilesetDetailsText_->Clear();

	const int selection = tilesetList_->GetSelection();
	if (selection == wxNOT_FOUND || selection >= static_cast<int>(tilesets_.size())) {
		return;
	}

	tilesetDetailsText_->SetValue(FormatTilesetDetails(tilesets_[selection]));
}

SQLiteMaterialsInspectorDialog::SQLiteMaterialsInspectorDialog(wxWindow* parent) :
	wxDialog(parent, wxID_ANY, "SQLite Materials Inspector", wxDefaultPosition, wxSize(1000, 700), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
	wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);
	panel_ = new SQLiteMaterialsInspectorPanel(this);
	topSizer->Add(panel_, 1, wxEXPAND | wxALL, FromDIP(5));
	topSizer->Add(CreateSeparatedButtonSizer(wxCLOSE), 0, wxEXPAND | wxALL, FromDIP(5));
	SetSizer(topSizer);
}
