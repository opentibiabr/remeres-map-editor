#include "main.h"

#include "materials_workbench_border_panel.h"

#include <array>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/choicdlg.h>
#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>
#include <wx/filedlg.h>
#include <wx/grid.h>
#include <wx/imaglist.h>
#include <wx/listbox.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/textdlg.h>
#include <wx/wrapsizer.h>

#include "common_windows.h"
#include "brush_database.h"
#include "find_item_window.h"
#include "graphics.h"
#include "items.h"
#include "materials_workbench_controller.h"
#include "gui.h"

#include <nlohmann/json.hpp>

namespace {
	void StyleBorderWorkspaceActionButton(wxButton* button, const wxString &tooltip);
	void StyleBorderWorkspaceCaption(wxStaticText* label);
	wxString DescribeGroundCaseCondition(const GroundBorderCaseConditionRecord &condition);
	wxString DescribeGroundCaseAction(const GroundBorderCaseActionRecord &action);
	void NormalizeGroundCaseSortOrders(GroundBorderCaseRecord &caseRecord);
	void NormalizeGroundBorderCases(std::vector<GroundBorderCaseRecord> &cases);

	wxString GetDatabaseBrushType(const Brush* brush) {
		if (!brush) {
			return wxString();
		}
		if (brush->isGround()) {
			return "ground";
		}
		if (brush->isWall()) {
			return "wall";
		}
		if (brush->isDoodad()) {
			return "doodad";
		}
		if (brush->isCarpet()) {
			return "carpet";
		}
		if (brush->isTable()) {
			return "table";
		}
		if (brush->isRaw()) {
			return "raw";
		}
		return wxString();
	}

	bool TryResolveMaterialsBrushIdFromDatabase(const wxString &type, const wxString &name, int64_t &outBrushId, wxString &outError) {
		outBrushId = 0;
		outError.clear();
		if (type.IsEmpty() || name.IsEmpty()) {
			outError = "Brush type or name is empty.";
			return false;
		}
		const wxString normalizedType = type.Lower() == "terrain" ? wxString("ground") : type.Lower();
		BrushRecord brush;
		if (!g_brush_database.findBrushByNameAndType(name, normalizedType, brush)) {
			outError = g_brush_database.getLastError();
			return false;
		}
		outBrushId = brush.id;
		if (outBrushId <= 0) {
			outError = "Resolved brush id is invalid.";
			return false;
		}
		return true;
	}

	bool IsKnownBorderPanelItemId(int itemId) {
		if (itemId <= 0 || itemId > std::numeric_limits<uint16_t>::max()) {
			return false;
		}
		return g_items.isValidID(static_cast<uint16_t>(itemId));
	}

	struct BorderEdgeSpec {
		const char* edge;
		const char* label;
		int row;
		int col;
	};

	const std::array<BorderEdgeSpec, 12> kBorderEdgeSpecs = { {
		{ "cnw", "Corner SE", 4, 4 },
		{ "n", "South", 4, 2 },
		{ "cne", "Corner SW", 4, 0 },
		{ "dnw", "Diag SW", 3, 3 },
		{ "dne", "Diag SE", 3, 1 },
		{ "w", "East", 2, 4 },
		{ "e", "West", 2, 0 },
		{ "dsw", "Diag NW", 1, 3 },
		{ "dse", "Diag NE", 1, 1 },
		{ "csw", "Corner NE", 0, 4 },
		{ "s", "North", 0, 2 },
		{ "cse", "Corner NW", 0, 0 },
	} };

	constexpr int kBorderGridSize = 5;
	constexpr int kBorderGridCenterIndex = 2;
	constexpr int kBorderGridGapDip = 4;
	constexpr int kBorderGridCellWidthDip = 66;
	constexpr int kBorderGridCellHeightDip = 58;

	const BorderEdgeSpec* FindEdgeSpec(const wxString &edge) {
		for (const BorderEdgeSpec &spec : kBorderEdgeSpecs) {
			if (edge == wxString::FromUTF8(spec.edge)) {
				return &spec;
			}
		}
		return nullptr;
	}

	const BorderEdgeSpec* FindEdgeSpecForCell(int row, int col) {
		for (const BorderEdgeSpec &spec : kBorderEdgeSpecs) {
			if (spec.row == row && spec.col == col) {
				return &spec;
			}
		}
		return nullptr;
	}

	int ToItemPreviewSpriteId(int itemId) {
		if (!IsKnownBorderPanelItemId(itemId)) {
			return 0;
		}
		return g_items[static_cast<uint16_t>(itemId)].clientID;
	}

	wxBitmap RenderSpriteToBitmap(int spriteId) {
		Sprite* sprite = g_gui.gfx.getSprite(spriteId);
		if (!sprite) {
			return wxBitmap();
		}

		wxBitmap bitmap(32, 32, 32);
		wxMemoryDC dc(bitmap);
		dc.SetBackground(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE)));
		dc.Clear();
		sprite->DrawTo(&dc, SPRITE_SIZE_32x32, 0, 0, 32, 32);
		dc.SelectObject(wxNullBitmap);
		return bitmap;
	}

	nlohmann::json ExportBorderSetToJson(const BorderSetStorageRecord &storage) {
		nlohmann::json entity;
		entity["kind"] = "border_set";
		entity["borderSet"] = {
			{ "xmlBorderId", storage.borderSet.xmlBorderId },
			{ "borderScope", storage.borderSet.borderScope.ToStdString() },
			{ "borderType", storage.borderSet.borderType.ToStdString() },
			{ "borderGroup", storage.borderSet.borderGroup },
			{ "groundEquivalent", storage.borderSet.groundEquivalent },
		};

		nlohmann::json items = nlohmann::json::array();
		for (const BorderSetItemRecord &item : storage.items) {
			items.push_back({
				{ "edge", item.edge.ToStdString() },
				{ "itemId", item.itemId },
				{ "sortOrder", item.sortOrder },
			});
		}
		entity["items"] = std::move(items);

		nlohmann::json root;
		root["format"] = "rme-materials";
		root["formatVersion"] = 1;
		root["entities"] = nlohmann::json::array({ std::move(entity) });
		return root;
	}

	bool ValidateMaterialsExportRoot(const nlohmann::json &root, const nlohmann::json*&outEntities, wxString &error) {
		outEntities = nullptr;
		if (!root.is_object()) {
			error = "Invalid JSON: expected an object.";
			return false;
		}
		if (!root.contains("format") || !root["format"].is_string() || root["format"].get<std::string>() != "rme-materials") {
			error = "Invalid JSON: unknown format.";
			return false;
		}
		if (!root.contains("formatVersion") || !root["formatVersion"].is_number_integer() || root["formatVersion"].get<int>() != 1) {
			error = "Invalid JSON: unsupported format version.";
			return false;
		}
		if (!root.contains("entities") || !root["entities"].is_array() || root["entities"].empty()) {
			error = "Invalid JSON: missing entities.";
			return false;
		}
		outEntities = &root["entities"];
		return true;
	}

	const nlohmann::json* FindMaterialsExportEntity(const nlohmann::json &entities, std::string_view kind) {
		for (const nlohmann::json &entity : entities) {
			if (!entity.is_object()) {
				continue;
			}
			if (!entity.contains("kind") || !entity["kind"].is_string()) {
				continue;
			}
			if (entity["kind"].get<std::string>() == kind) {
				return &entity;
			}
		}
		return nullptr;
	}

	bool ParseBorderSetExportBorderSet(const nlohmann::json &borderSet, BorderSetRecord &outBorderSet, wxString &error) {
		if (!borderSet.contains("xmlBorderId") || !borderSet["xmlBorderId"].is_number_integer()) {
			error = "Invalid JSON: missing xmlBorderId.";
			return false;
		}

		outBorderSet = BorderSetRecord();
		outBorderSet.xmlBorderId = borderSet["xmlBorderId"].get<int>();
		if (outBorderSet.xmlBorderId <= 0) {
			error = "Invalid JSON: xmlBorderId must be greater than zero.";
			return false;
		}

		outBorderSet.borderScope = "global";
		if (borderSet.contains("borderScope") && borderSet["borderScope"].is_string()) {
			const std::string value = borderSet["borderScope"].get<std::string>();
			outBorderSet.borderScope = wxString::FromUTF8(value.c_str());
		}
		if (!outBorderSet.borderScope.IsSameAs("global", false)) {
			error = "Only global border sets are supported for import right now.";
			return false;
		}

		if (borderSet.contains("borderType") && borderSet["borderType"].is_string()) {
			const std::string value = borderSet["borderType"].get<std::string>();
			outBorderSet.borderType = wxString::FromUTF8(value.c_str());
		}
		if (outBorderSet.borderType.IsEmpty()) {
			outBorderSet.borderType = "normal";
		}
		if (borderSet.contains("borderGroup") && borderSet["borderGroup"].is_number_integer()) {
			outBorderSet.borderGroup = borderSet["borderGroup"].get<int>();
		}
		if (borderSet.contains("groundEquivalent") && borderSet["groundEquivalent"].is_number_integer()) {
			outBorderSet.groundEquivalent = borderSet["groundEquivalent"].get<int>();
		}
		return true;
	}

	bool ParseBorderSetExportItems(const nlohmann::json &items, std::vector<BorderSetItemRecord> &outItems, wxString &error) {
		outItems.clear();
		for (const nlohmann::json &row : items) {
			if (!row.is_object()) {
				continue;
			}
			if (!row.contains("edge") || !row["edge"].is_string()) {
				error = "Invalid JSON: item missing edge.";
				return false;
			}
			const std::string value = row["edge"].get<std::string>();
			const wxString edge = wxString::FromUTF8(value.c_str());
			if (!FindEdgeSpec(edge)) {
				error = wxString::Format("Invalid JSON: unknown edge \"%s\".", edge);
				return false;
			}
			if (!row.contains("itemId") || !row["itemId"].is_number_integer()) {
				error = "Invalid JSON: item missing itemId.";
				return false;
			}

			BorderSetItemRecord item;
			item.edge = edge;
			item.itemId = row["itemId"].get<int>();
			item.sortOrder = static_cast<int>(outItems.size());
			if (row.contains("sortOrder") && row["sortOrder"].is_number_integer()) {
				item.sortOrder = row["sortOrder"].get<int>();
			}
			outItems.push_back(item);
		}
		return true;
	}

	bool TryParseBorderSetExportJson(const nlohmann::json &root, BorderSetRecord &outBorderSet, std::vector<BorderSetItemRecord> &outItems, wxString &error) {
		const nlohmann::json* entities = nullptr;
		if (!ValidateMaterialsExportRoot(root, entities, error)) {
			return false;
		}

		const nlohmann::json* borderEntity = FindMaterialsExportEntity(*entities, "border_set");
		if (!borderEntity) {
			error = "Invalid JSON: no border_set entity found.";
			return false;
		}

		if (!borderEntity->contains("borderSet") || !(*borderEntity)["borderSet"].is_object()) {
			error = "Invalid JSON: missing borderSet.";
			return false;
		}
		if (!ParseBorderSetExportBorderSet((*borderEntity)["borderSet"], outBorderSet, error)) {
			return false;
		}

		if (!borderEntity->contains("items") || !(*borderEntity)["items"].is_array()) {
			error = "Invalid JSON: missing items.";
			return false;
		}
		return ParseBorderSetExportItems((*borderEntity)["items"], outItems, error);
	}

	class BorderEdgePreviewPanel final : public wxPanel {
	public:
		explicit BorderEdgePreviewPanel(wxWindow* parent) :
			wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(56), FromDIP(56)), wxBORDER_THEME) {
			SetBackgroundStyle(wxBG_STYLE_PAINT);
			Bind(wxEVT_PAINT, &BorderEdgePreviewPanel::OnPaint, this);
		}

		void SetEdge(const wxString &edge) {
			edge_ = edge;
			Refresh();
		}

		void ClearEdge() {
			edge_.clear();
			Refresh();
		}

	private:
		void OnPaint(wxPaintEvent &) {
			wxAutoBufferedPaintDC dc(this);
			dc.SetBackground(wxBrush(GetBackgroundColour()));
			dc.Clear();

			const wxSize size = GetClientSize();
			const int gridSize = 5;
			const int cell = std::max(FromDIP(6), std::min(size.GetWidth(), size.GetHeight()) / (gridSize + 1));
			const int gridW = cell * gridSize;
			const int gridH = cell * gridSize;
			const int startX = (size.GetWidth() - gridW) / 2;
			const int startY = (size.GetHeight() - gridH) / 2;

			const BorderEdgeSpec* spec = FindEdgeSpec(edge_);
			wxColour border = wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW);
			wxColour fill = wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE);
			wxColour highlight = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT);

			for (int row = 0; row < gridSize; ++row) {
				for (int col = 0; col < gridSize; ++col) {
					wxRect rect(startX + col * cell, startY + row * cell, cell, cell);
					const bool isTarget = spec && row == spec->row && col == spec->col;
					dc.SetPen(wxPen(border));
					dc.SetBrush(wxBrush(isTarget ? highlight : fill));
					dc.DrawRectangle(rect);
				}
			}
		}

		wxString edge_;
	};

	bool IsBorderGridCenterCell(int row, int col) {
		return row == kBorderGridCenterIndex && col == kBorderGridCenterIndex;
	}

	wxString GetBorderEdgeDisplayLabel(const wxString &edge) {
		const BorderEdgeSpec* spec = FindEdgeSpec(edge);
		return spec ? wxString::FromUTF8(spec->label) : edge;
	}

	wxString FormatSpecificCaseTypeLabel(const wxString &type) {
		if (type.IsSameAs("match_border", false)) {
			return "Match Border";
		}
		if (type.IsSameAs("match_group", false)) {
			return "Match Group";
		}
		if (type.IsSameAs("match_item", false)) {
			return "Match Item";
		}
		if (type.IsSameAs("replace_border", false)) {
			return "Replace Border";
		}
		if (type.IsSameAs("replace_item", false)) {
			return "Replace Item";
		}
		if (type.IsSameAs("delete_borders", false)) {
			return "Delete Borders";
		}
		return type;
	}

	wxString FormatSpecificCaseValueLabel(const wxString &type, int value) {
		if (value <= 0) {
			return "-";
		}
		if (type.IsSameAs("match_border", false) || type.IsSameAs("replace_border", false)) {
			return wxString::Format("Border #%d", value);
		}
		if (type.IsSameAs("match_group", false)) {
			return wxString::Format("Group #%d", value);
		}
		if (type.IsSameAs("match_item", false) || type.IsSameAs("replace_item", false)) {
			return wxString::Format("Item #%d", value);
		}
		return wxString::Format("%d", value);
	}

	wxString FormatOptionalBorderItemText(int itemId, const wxString &emptyLabel = "not set") {
		return itemId > 0 ? wxString::Format("item %d", itemId) : emptyLabel;
	}

	wxString FormatCompactItemIdText(int itemId, const wxString &emptyLabel = "empty") {
		return itemId > 0 ? wxString::Format("%d", itemId) : emptyLabel;
	}

	wxString FormatCompactCenterGroundText(int itemId) {
		return itemId > 0 ? wxString::Format("%d", itemId) : wxString::FromUTF8("painted");
	}

	wxString BuildBorderGroupDisplayLabel(int group) {
		return group <= 0 ? wxString("None") : wxString::Format("Group %d", group);
	}

	int ParseBorderGroupChoiceValue(const wxString &selection) {
		if (selection.IsEmpty() || selection.IsSameAs("None", false)) {
			return 0;
		}

		long parsedValue = 0;
		wxString numericPortion = selection.AfterLast(' ');
		if (numericPortion.ToLong(&parsedValue) && parsedValue >= 0) {
			return static_cast<int>(parsedValue);
		}
		return 0;
	}

	void RebuildBorderGroupChoices(wxChoice* choice, int selectedGroup) {
		if (!choice) {
			return;
		}

		choice->Clear();
		choice->Append(BuildBorderGroupDisplayLabel(0));
		choice->Append(BuildBorderGroupDisplayLabel(1));
		if (selectedGroup > 1) {
			choice->Append(BuildBorderGroupDisplayLabel(selectedGroup));
		}
		choice->SetStringSelection(BuildBorderGroupDisplayLabel(selectedGroup));
		if (choice->GetSelection() == wxNOT_FOUND) {
			choice->SetStringSelection(BuildBorderGroupDisplayLabel(0));
		}
	}

	int ResolveUsagePreviewItemId(const BorderSetUsageRecord &usage) {
		if (usage.primaryItemId > 0) {
			return usage.primaryItemId;
		}
		if (usage.serverLookId > 0) {
			return usage.serverLookId;
		}
		return usage.lookId;
	}

	wxString BuildBrushPickerLabel(const wxString &brushName, int64_t brushId, const wxString &emptyLabel = "Not selected");

	constexpr int kUsageGridColumnCount = 7;
	constexpr int kUsageGridColumnBrush = 0;
	constexpr int kUsageGridColumnBrushId = 1;
	constexpr int kUsageGridColumnType = 2;
	constexpr int kUsageGridColumnAlign = 3;
	constexpr int kUsageGridColumnRole = 4;
	constexpr int kUsageGridColumnTarget = 5;
	constexpr int kUsageGridColumnCenter = 6;

	wxString BuildUsageTypeLabel(const BorderSetUsageRecord &usage) {
		return usage.brushType.IsEmpty() ? wxString("ground") : usage.brushType;
	}

	wxString BuildUsageAlignLabel(const BorderSetUsageRecord &usage) {
		return usage.align.IsEmpty() ? wxString("outer") : usage.align;
	}

	wxString BuildUsageRoleLabel(const BorderSetUsageRecord &usage) {
		wxString label = usage.borderRole.IsEmpty() ? wxString("normal") : usage.borderRole;
		if (usage.superBorder) {
			label << " (super)";
		}
		return label;
	}

	wxString BuildUsageTargetLabel(const BorderSetUsageRecord &usage) {
		if (usage.targetMode.IsSameAs("brush", false)) {
			return BuildBrushPickerLabel(usage.targetBrushName, usage.targetBrushId, "brush");
		}
		return usage.targetMode.IsEmpty() ? wxString("all") : usage.targetMode;
	}

	wxString BuildUsageCenterLabel(const BorderSetUsageRecord &usage) {
		const int previewItemId = ResolveUsagePreviewItemId(usage);
		return previewItemId > 0 ? wxString::Format("%d", previewItemId) : wxString::FromUTF8("painted");
	}

	wxString BuildUsageSelectionSummary(const BorderSetUsageRecord &usage) {
		return wxString::Format(
			"%s (#%lld) | %s | %s | %s | target: %s | center: %s",
			usage.brushName,
			static_cast<long long>(usage.brushId),
			BuildUsageTypeLabel(usage),
			BuildUsageAlignLabel(usage),
			BuildUsageRoleLabel(usage),
			BuildUsageTargetLabel(usage),
			BuildUsageCenterLabel(usage)
		);
	}

	void ApplyUsageGridRowStyle(wxGrid* grid, int row) {
		if (!grid || row < 0) {
			return;
		}

		const wxColour background = row % 2 == 0
			? wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW)
			: wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
		const wxColour text = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
		for (int col = 0; col < grid->GetNumberCols(); ++col) {
			grid->SetReadOnly(row, col, true);
			grid->SetCellBackgroundColour(row, col, background);
			grid->SetCellTextColour(row, col, text);
		}
	}

	wxString BuildUsageSearchHaystack(const BorderSetUsageRecord &usage) {
		const int previewItemId = ResolveUsagePreviewItemId(usage);
		wxString haystack;
		haystack << usage.brushName << " ";
		haystack << usage.brushType << " ";
		haystack << wxString::Format("%lld ", static_cast<long long>(usage.brushId));
		haystack << wxString::Format("brush %lld ", static_cast<long long>(usage.brushId));
		haystack << (usage.align.IsEmpty() ? wxString("outer") : usage.align) << " ";
		haystack << (usage.borderRole.IsEmpty() ? wxString("normal") : usage.borderRole) << " ";
		haystack << (usage.targetMode.IsEmpty() ? wxString("all") : usage.targetMode) << " ";
		haystack << usage.targetBrushName << " ";
		if (usage.targetBrushId > 0) {
			haystack << wxString::Format("%lld ", static_cast<long long>(usage.targetBrushId));
			haystack << wxString::Format("target %lld ", static_cast<long long>(usage.targetBrushId));
			haystack << wxString::Format("target brush %lld ", static_cast<long long>(usage.targetBrushId));
		}
		if (previewItemId > 0) {
			haystack << wxString::Format("%d ", previewItemId);
			haystack << wxString::Format("center %d ", previewItemId);
			haystack << wxString::Format("item %d ", previewItemId);
		} else {
			haystack << "center painted ";
			haystack << "painted ";
		}
		if (usage.primaryItemId > 0) {
			haystack << wxString::Format("primary %d ", usage.primaryItemId);
		}
		if (usage.lookId > 0) {
			haystack << wxString::Format("look %d ", usage.lookId);
		}
		if (usage.serverLookId > 0) {
			haystack << wxString::Format("server %d ", usage.serverLookId);
		}
		if (usage.superBorder) {
			haystack << "super ";
			haystack << "super border ";
		}
		return haystack.Lower();
	}

	wxString BuildBrushPickerLabel(const wxString &brushName, int64_t brushId, const wxString &emptyLabel) {
		if (!brushName.IsEmpty() && brushId > 0) {
			return wxString::Format("%s (#%lld)", brushName, static_cast<long long>(brushId));
		}
		if (!brushName.IsEmpty()) {
			return brushName;
		}
		if (brushId > 0) {
			return wxString::Format("#%lld", static_cast<long long>(brushId));
		}
		return emptyLabel;
	}

	struct GlobalUsageEditData {
		int64_t ownerBrushId = 0;
		wxString ownerBrushName;
		wxString borderRole = "normal";
		wxString align = "outer";
		wxString targetMode = "all";
		int64_t targetBrushId = 0;
		wxString targetBrushName;
		bool superBorder = false;
	};

	class TerrainBrushPickerDialog final : public wxDialog {
	public:
		TerrainBrushPickerDialog(
			wxWindow* parent,
			const MaterialsWorkbenchController &controller,
			const wxString &title,
			int64_t selectedBrushId = 0
		) :
			wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
			const auto &brushGroups = controller.GetBrushGroups();
			for (const MaterialsWorkbenchBrushGroup &group : brushGroups) {
				if (group.brushType != "ground") {
					continue;
				}
				for (const BrushRecord &brush : group.brushes) {
					terrainBrushes_.push_back(brush);
				}
			}

			std::sort(
				terrainBrushes_.begin(),
				terrainBrushes_.end(),
				[](const BrushRecord &left, const BrushRecord &right) {
					const int compareNames = left.name.CmpNoCase(right.name);
					if (compareNames != 0) {
						return compareNames < 0;
					}
					return left.id < right.id;
				}
			);

			wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);
			searchCtrl_ = new wxTextCtrl(this, wxID_ANY);
			searchCtrl_->SetHint("Search terrain brush by name or id...");
			listBox_ = new wxListBox(this, wxID_ANY);
			listBox_->SetMinSize(wxSize(FromDIP(420), FromDIP(320)));

			wxStdDialogButtonSizer* buttons = new wxStdDialogButtonSizer();
			okButton_ = new wxButton(this, wxID_OK);
			okButton_->Enable(false);
			buttons->AddButton(okButton_);
			buttons->AddButton(new wxButton(this, wxID_CANCEL));
			buttons->Realize();

			rootSizer->Add(searchCtrl_, 0, wxEXPAND | wxALL, FromDIP(12));
			rootSizer->Add(listBox_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
			rootSizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
			SetSizerAndFit(rootSizer);
			SetMinSize(wxSize(FromDIP(460), FromDIP(420)));

			searchCtrl_->Bind(wxEVT_TEXT, &TerrainBrushPickerDialog::OnSearchChanged, this);
			listBox_->Bind(wxEVT_LISTBOX, &TerrainBrushPickerDialog::OnSelectionChanged, this);
			listBox_->Bind(wxEVT_LISTBOX_DCLICK, &TerrainBrushPickerDialog::OnItemActivated, this);
			okButton_->Bind(wxEVT_BUTTON, &TerrainBrushPickerDialog::OnConfirm, this);

			RebuildList(selectedBrushId);
		}

		const BrushRecord* GetSelectedBrush() const {
			if (selectedVisibleIndex_ == wxNOT_FOUND || selectedVisibleIndex_ < 0 || selectedVisibleIndex_ >= static_cast<int>(filteredIndexes_.size())) {
				return nullptr;
			}
			const int sourceIndex = filteredIndexes_[selectedVisibleIndex_];
			if (sourceIndex < 0 || sourceIndex >= static_cast<int>(terrainBrushes_.size())) {
				return nullptr;
			}
			return &terrainBrushes_[sourceIndex];
		}

	private:
		void RebuildList(int64_t preferredBrushId) {
			filteredIndexes_.clear();
			listBox_->Clear();

			const wxString query = searchCtrl_->GetValue().Lower().Trim(true).Trim(false);
			for (size_t i = 0; i < terrainBrushes_.size(); ++i) {
				const BrushRecord &brush = terrainBrushes_[i];
				wxString haystack;
				haystack << brush.name << " ";
				haystack << wxString::Format("%lld", static_cast<long long>(brush.id));
				haystack = haystack.Lower();
				if (!query.IsEmpty() && !haystack.Contains(query)) {
					continue;
				}

				filteredIndexes_.push_back(static_cast<int>(i));
				listBox_->Append(wxString::Format("%s (#%lld)", brush.name, static_cast<long long>(brush.id)));
			}

			selectedVisibleIndex_ = wxNOT_FOUND;
			if (filteredIndexes_.empty()) {
				okButton_->Enable(false);
				return;
			}

			int preferredVisibleIndex = 0;
			for (size_t i = 0; i < filteredIndexes_.size(); ++i) {
				const BrushRecord &brush = terrainBrushes_[filteredIndexes_[i]];
				if (brush.id == preferredBrushId) {
					preferredVisibleIndex = static_cast<int>(i);
					break;
				}
			}
			listBox_->SetSelection(preferredVisibleIndex);
			selectedVisibleIndex_ = preferredVisibleIndex;
			okButton_->Enable(true);
		}

		void OnSearchChanged(wxCommandEvent &) {
			const BrushRecord* current = GetSelectedBrush();
			const int64_t preferredBrushId = current ? current->id : 0;
			RebuildList(preferredBrushId);
		}

		void OnSelectionChanged(wxCommandEvent &) {
			selectedVisibleIndex_ = listBox_->GetSelection();
			okButton_->Enable(selectedVisibleIndex_ != wxNOT_FOUND);
		}

		void OnItemActivated(wxCommandEvent &) {
			ConfirmSelection();
		}

		void OnConfirm(wxCommandEvent &) {
			ConfirmSelection();
		}

		void ConfirmSelection() {
			selectedVisibleIndex_ = listBox_->GetSelection();
			if (GetSelectedBrush()) {
				EndModal(wxID_OK);
			}
		}

		std::vector<BrushRecord> terrainBrushes_;
		std::vector<int> filteredIndexes_;
		wxTextCtrl* searchCtrl_ = nullptr;
		wxListBox* listBox_ = nullptr;
		wxButton* okButton_ = nullptr;
		int selectedVisibleIndex_ = wxNOT_FOUND;
	};

	class GlobalUsageDialog final : public wxDialog {
	public:
		GlobalUsageDialog(wxWindow* parent, MaterialsWorkbenchController &controller, const wxString &title, const GlobalUsageEditData &initialData) :
			wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
			controller_(controller),
			data_(initialData) {
			wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);
			wxFlexGridSizer* formSizer = new wxFlexGridSizer(0, 2, FromDIP(8), FromDIP(8));
			formSizer->AddGrowableCol(1, 1);

			ownerBrushCtrl_ = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
			wxButton* pickOwnerButton = new wxButton(this, wxID_ANY, "Choose...");
			roleChoice_ = new wxChoice(this, wxID_ANY);
			roleChoice_->Append("normal");
			roleChoice_->Append("optional");
			alignChoice_ = new wxChoice(this, wxID_ANY);
			alignChoice_->Append("outer");
			alignChoice_->Append("inner");
			alignChoice_->Append("optional");
			targetModeChoice_ = new wxChoice(this, wxID_ANY);
			targetModeChoice_->Append("all");
			targetModeChoice_->Append("brush");
			targetModeChoice_->Append("none");
			superBorderCtrl_ = new wxCheckBox(this, wxID_ANY, "Enabled");
			targetBrushCtrl_ = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
			targetBrushButton_ = new wxButton(this, wxID_ANY, "Choose...");

			const auto addFieldRow = [&](const wxString &label, wxWindow* field, wxWindow* secondary = nullptr) {
				formSizer->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
				if (!secondary) {
					formSizer->Add(field, 1, wxEXPAND);
					return;
				}
				wxBoxSizer* rowSizer = new wxBoxSizer(wxHORIZONTAL);
				rowSizer->Add(field, 1, wxRIGHT, FromDIP(6));
				rowSizer->Add(secondary, 0);
				formSizer->Add(rowSizer, 1, wxEXPAND);
			};

			addFieldRow("Brush", ownerBrushCtrl_, pickOwnerButton);
			addFieldRow("Role", roleChoice_);
			addFieldRow("Align", alignChoice_);
			addFieldRow("Super Border", superBorderCtrl_);
			addFieldRow("Target", targetModeChoice_);
			addFieldRow("Target Brush", targetBrushCtrl_, targetBrushButton_);

			rootSizer->Add(formSizer, 1, wxEXPAND | wxALL, FromDIP(12));

			wxStdDialogButtonSizer* buttons = new wxStdDialogButtonSizer();
			buttons->AddButton(new wxButton(this, wxID_OK));
			buttons->AddButton(new wxButton(this, wxID_CANCEL));
			buttons->Realize();
			rootSizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

			SetSizerAndFit(rootSizer);
			SetMinSize(wxSize(FromDIP(420), GetMinSize().y));

			roleChoice_->SetStringSelection(data_.borderRole.IsEmpty() ? wxString::FromUTF8("normal") : data_.borderRole);
			alignChoice_->SetStringSelection(data_.align.IsEmpty() ? wxString::FromUTF8("outer") : data_.align);
			targetModeChoice_->SetStringSelection(data_.targetMode.IsEmpty() ? wxString::FromUTF8("all") : data_.targetMode);
			superBorderCtrl_->SetValue(data_.superBorder);
			ownerBrushCtrl_->SetValue(BuildBrushPickerLabel(data_.ownerBrushName, data_.ownerBrushId));
			targetBrushCtrl_->SetValue(BuildBrushPickerLabel(data_.targetBrushName, data_.targetBrushId));
			UpdateTargetBrushState();

			pickOwnerButton->Bind(wxEVT_BUTTON, &GlobalUsageDialog::OnPickOwnerBrush, this);
			targetBrushButton_->Bind(wxEVT_BUTTON, &GlobalUsageDialog::OnPickTargetBrush, this);
			targetModeChoice_->Bind(wxEVT_CHOICE, &GlobalUsageDialog::OnTargetModeChanged, this);
		}

		const GlobalUsageEditData &GetData() const {
			return data_;
		}

		bool TransferDataFromWindow() override {
			data_.borderRole = roleChoice_->GetStringSelection();
			data_.align = alignChoice_->GetStringSelection();
			data_.superBorder = superBorderCtrl_ && superBorderCtrl_->GetValue();
			data_.targetMode = targetModeChoice_->GetStringSelection();
			if (data_.ownerBrushId <= 0) {
				wxMessageBox("Choose the brush that uses this global border.", "Usage Context", wxOK | wxICON_WARNING, this);
				return false;
			}
			if (data_.targetMode != "brush") {
				data_.targetBrushId = 0;
				data_.targetBrushName.clear();
			} else if (data_.targetBrushId <= 0 || data_.targetBrushName.IsEmpty()) {
				wxMessageBox("Choose the target brush for this context or switch the target mode.", "Usage Context", wxOK | wxICON_WARNING, this);
				return false;
			}
			return true;
		}

	private:
		void OnPickOwnerBrush(wxCommandEvent &) {
			TerrainBrushPickerDialog dialog(this, controller_, "Choose Terrain Brush", data_.ownerBrushId);
			if (dialog.ShowModal() != wxID_OK) {
				return;
			}
			const BrushRecord* brush = dialog.GetSelectedBrush();
			if (!brush) {
				return;
			}
			data_.ownerBrushId = brush->id;
			data_.ownerBrushName = brush->name;
			ownerBrushCtrl_->SetValue(BuildBrushPickerLabel(data_.ownerBrushName, data_.ownerBrushId));
		}

		void OnPickTargetBrush(wxCommandEvent &) {
			TerrainBrushPickerDialog dialog(this, controller_, "Choose Target Terrain Brush", data_.targetBrushId);
			if (dialog.ShowModal() != wxID_OK) {
				return;
			}
			const BrushRecord* brush = dialog.GetSelectedBrush();
			if (!brush) {
				return;
			}
			data_.targetBrushId = brush->id;
			data_.targetBrushName = brush->name;
			targetBrushCtrl_->SetValue(BuildBrushPickerLabel(data_.targetBrushName, data_.targetBrushId));
		}

		void OnTargetModeChanged(wxCommandEvent &) {
			UpdateTargetBrushState();
		}

		void UpdateTargetBrushState() {
			const bool usesBrushTarget = targetModeChoice_->GetStringSelection() == "brush";
			targetBrushCtrl_->Enable(usesBrushTarget);
			targetBrushButton_->Enable(usesBrushTarget);
			if (!usesBrushTarget) {
				targetBrushCtrl_->SetValue("Not used");
			} else {
				targetBrushCtrl_->SetValue(BuildBrushPickerLabel(data_.targetBrushName, data_.targetBrushId));
			}
		}

		MaterialsWorkbenchController &controller_;
		GlobalUsageEditData data_;
		wxTextCtrl* ownerBrushCtrl_ = nullptr;
		wxChoice* roleChoice_ = nullptr;
		wxChoice* alignChoice_ = nullptr;
		wxCheckBox* superBorderCtrl_ = nullptr;
		wxChoice* targetModeChoice_ = nullptr;
		wxTextCtrl* targetBrushCtrl_ = nullptr;
		wxButton* targetBrushButton_ = nullptr;
	};

	class GroundSpecificConditionDialog final : public wxDialog {
	public:
		GroundSpecificConditionDialog(wxWindow* parent, const GroundBorderCaseConditionRecord &initial) :
			wxDialog(parent, wxID_ANY, "Edit Condition", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
			value_(initial) {
			wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);
			wxFlexGridSizer* formSizer = new wxFlexGridSizer(0, 2, FromDIP(8), FromDIP(8));
			formSizer->AddGrowableCol(1, 1);

			typeChoice_ = new wxChoice(this, wxID_ANY);
			typeChoice_->Append("match_border");
			typeChoice_->Append("match_group");
			typeChoice_->Append("match_item");
			edgeChoice_ = new wxChoice(this, wxID_ANY);
			const std::array<const char*, 12> edges = { "n", "s", "e", "w", "cnw", "cne", "csw", "cse", "dnw", "dne", "dsw", "dse" };
			for (const char* edge : edges) {
				edgeChoice_->Append(edge);
			}
			valueCtrl_ = new wxSpinCtrl(this, wxID_ANY);
			valueCtrl_->SetRange(0, std::numeric_limits<int>::max());
			itemPreview_ = new ItemButton(this, RENDER_SIZE_32x32, 0);
			pickItemButton_ = new wxButton(this, wxID_ANY, "Pick");
			StyleBorderWorkspaceActionButton(pickItemButton_, "Pick an item id.");
			wxBoxSizer* valueRow = new wxBoxSizer(wxHORIZONTAL);
			valueRow->Add(valueCtrl_, 1, wxRIGHT, FromDIP(6));
			valueRow->Add(itemPreview_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
			valueRow->Add(pickItemButton_, 0, wxALIGN_CENTER_VERTICAL);

			formSizer->Add(new wxStaticText(this, wxID_ANY, "Type"), 0, wxALIGN_CENTER_VERTICAL);
			formSizer->Add(typeChoice_, 1, wxEXPAND);
			formSizer->Add(new wxStaticText(this, wxID_ANY, "Edge"), 0, wxALIGN_CENTER_VERTICAL);
			formSizer->Add(edgeChoice_, 1, wxEXPAND);
			formSizer->Add(new wxStaticText(this, wxID_ANY, "Value"), 0, wxALIGN_CENTER_VERTICAL);
			formSizer->Add(valueRow, 1, wxEXPAND);

			rootSizer->Add(formSizer, 1, wxEXPAND | wxALL, FromDIP(12));

			wxStdDialogButtonSizer* buttons = new wxStdDialogButtonSizer();
			okButton_ = new wxButton(this, wxID_OK);
			buttons->AddButton(okButton_);
			buttons->AddButton(new wxButton(this, wxID_CANCEL));
			buttons->Realize();
			rootSizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

			SetSizerAndFit(rootSizer);
			SetMinSize(wxSize(FromDIP(380), GetMinSize().y));

			typeChoice_->SetStringSelection(value_.conditionType.IsEmpty() ? wxString::FromUTF8("match_border") : value_.conditionType);
			if (!value_.edge.IsEmpty()) {
				edgeChoice_->SetStringSelection(value_.edge);
			} else {
				edgeChoice_->SetSelection(0);
			}
			valueCtrl_->SetValue(value_.matchValue);
			UpdateFieldVisibility();

			typeChoice_->Bind(wxEVT_CHOICE, &GroundSpecificConditionDialog::OnTypeChanged, this);
			valueCtrl_->Bind(wxEVT_TEXT, &GroundSpecificConditionDialog::OnValueChanged, this);
			valueCtrl_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent &) { RefreshItemPreview(); });
			itemPreview_->Bind(wxEVT_BUTTON, &GroundSpecificConditionDialog::OnPickItem, this);
			pickItemButton_->Bind(wxEVT_BUTTON, &GroundSpecificConditionDialog::OnPickItem, this);
		}

		const GroundBorderCaseConditionRecord &GetValue() const {
			return value_;
		}

		bool TransferDataFromWindow() override {
			value_.conditionType = typeChoice_->GetStringSelection();
			value_.edge = edgeChoice_->IsShown() ? edgeChoice_->GetStringSelection() : wxString();
			value_.matchValue = valueCtrl_->GetValue();
			value_.sortOrder = 0;

			if (value_.conditionType.IsSameAs("match_item", false)) {
				value_.edge.clear();
				if (value_.matchValue <= 0) {
					wxMessageBox("Item ID must be greater than zero.", "Condition", wxOK | wxICON_WARNING, this);
					return false;
				}
				return true;
			}

			if (value_.edge.IsEmpty()) {
				wxMessageBox("Choose an edge.", "Condition", wxOK | wxICON_WARNING, this);
				return false;
			}
			if (value_.matchValue <= 0) {
				wxMessageBox("Value must be greater than zero.", "Condition", wxOK | wxICON_WARNING, this);
				return false;
			}
			return true;
		}

	private:
		void OnTypeChanged(wxCommandEvent &) {
			UpdateFieldVisibility();
		}

		void UpdateFieldVisibility() {
			const wxString type = typeChoice_->GetStringSelection();
			const bool needsEdge = !type.IsSameAs("match_item", false);
			edgeChoice_->Show(needsEdge);
			itemPreview_->Show(!needsEdge);
			pickItemButton_->Show(!needsEdge);
			RefreshItemPreview();
			okButton_->Enable(true);
			Layout();
			Fit();
		}

		void RefreshItemPreview() {
			if (!itemPreview_->IsShown()) {
				itemPreview_->SetSprite(0);
				return;
			}
			itemPreview_->SetSprite(ToItemPreviewSpriteId(valueCtrl_->GetValue()));
		}

		void OnValueChanged(wxCommandEvent &) {
			RefreshItemPreview();
		}

		void OnPickItem(wxCommandEvent &) {
			FindItemDialog dialog(this, "Select Item");
			dialog.setSearchMode(FindItemDialog::ItemIDs);
			if (dialog.ShowModal() != wxID_OK) {
				return;
			}
			valueCtrl_->SetValue(dialog.getResultID());
			RefreshItemPreview();
		}

		GroundBorderCaseConditionRecord value_;
		wxChoice* typeChoice_ = nullptr;
		wxChoice* edgeChoice_ = nullptr;
		wxSpinCtrl* valueCtrl_ = nullptr;
		ItemButton* itemPreview_ = nullptr;
		wxButton* pickItemButton_ = nullptr;
		wxButton* okButton_ = nullptr;
	};

	class GroundSpecificActionDialog final : public wxDialog {
	public:
		GroundSpecificActionDialog(wxWindow* parent, const GroundBorderCaseActionRecord &initial) :
			wxDialog(parent, wxID_ANY, "Edit Action", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
			value_(initial) {
			wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);
			wxFlexGridSizer* formSizer = new wxFlexGridSizer(0, 2, FromDIP(8), FromDIP(8));
			formSizer->AddGrowableCol(1, 1);

			typeChoice_ = new wxChoice(this, wxID_ANY);
			typeChoice_->Append("replace_border");
			typeChoice_->Append("replace_item");
			typeChoice_->Append("delete_borders");
			edgeChoice_ = new wxChoice(this, wxID_ANY);
			const std::array<const char*, 12> edges = { "n", "s", "e", "w", "cnw", "cne", "csw", "cse", "dnw", "dne", "dsw", "dse" };
			for (const char* edge : edges) {
				edgeChoice_->Append(edge);
			}
			targetCtrl_ = new wxSpinCtrl(this, wxID_ANY);
			targetCtrl_->SetRange(0, std::numeric_limits<int>::max());
			withCtrl_ = new wxSpinCtrl(this, wxID_ANY);
			withCtrl_->SetRange(0, std::numeric_limits<int>::max());
			targetPreview_ = new ItemButton(this, RENDER_SIZE_32x32, 0);
			withPreview_ = new ItemButton(this, RENDER_SIZE_32x32, 0);
			pickTargetButton_ = new wxButton(this, wxID_ANY, "Pick");
			pickWithButton_ = new wxButton(this, wxID_ANY, "Pick");
			StyleBorderWorkspaceActionButton(pickTargetButton_, "Pick the target item id.");
			StyleBorderWorkspaceActionButton(pickWithButton_, "Pick the replacement item id.");
			wxBoxSizer* targetRow = new wxBoxSizer(wxHORIZONTAL);
			targetRow->Add(targetCtrl_, 1, wxRIGHT, FromDIP(6));
			targetRow->Add(targetPreview_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
			targetRow->Add(pickTargetButton_, 0, wxALIGN_CENTER_VERTICAL);
			wxBoxSizer* withRow = new wxBoxSizer(wxHORIZONTAL);
			withRow->Add(withCtrl_, 1, wxRIGHT, FromDIP(6));
			withRow->Add(withPreview_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
			withRow->Add(pickWithButton_, 0, wxALIGN_CENTER_VERTICAL);

			formSizer->Add(new wxStaticText(this, wxID_ANY, "Type"), 0, wxALIGN_CENTER_VERTICAL);
			formSizer->Add(typeChoice_, 1, wxEXPAND);
			formSizer->Add(new wxStaticText(this, wxID_ANY, "Edge"), 0, wxALIGN_CENTER_VERTICAL);
			formSizer->Add(edgeChoice_, 1, wxEXPAND);
			formSizer->Add(new wxStaticText(this, wxID_ANY, "Target"), 0, wxALIGN_CENTER_VERTICAL);
			formSizer->Add(targetRow, 1, wxEXPAND);
			formSizer->Add(new wxStaticText(this, wxID_ANY, "With"), 0, wxALIGN_CENTER_VERTICAL);
			formSizer->Add(withRow, 1, wxEXPAND);

			rootSizer->Add(formSizer, 1, wxEXPAND | wxALL, FromDIP(12));

			wxStdDialogButtonSizer* buttons = new wxStdDialogButtonSizer();
			okButton_ = new wxButton(this, wxID_OK);
			buttons->AddButton(okButton_);
			buttons->AddButton(new wxButton(this, wxID_CANCEL));
			buttons->Realize();
			rootSizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

			SetSizerAndFit(rootSizer);
			SetMinSize(wxSize(FromDIP(420), GetMinSize().y));

			typeChoice_->SetStringSelection(value_.actionType.IsEmpty() ? wxString::FromUTF8("replace_item") : value_.actionType);
			if (!value_.edge.IsEmpty()) {
				edgeChoice_->SetStringSelection(value_.edge);
			} else {
				edgeChoice_->SetSelection(0);
			}
			targetCtrl_->SetValue(value_.targetValue);
			withCtrl_->SetValue(value_.replacementValue);
			UpdateFieldVisibility();

			typeChoice_->Bind(wxEVT_CHOICE, &GroundSpecificActionDialog::OnTypeChanged, this);
			targetCtrl_->Bind(wxEVT_TEXT, &GroundSpecificActionDialog::OnValuesChanged, this);
			targetCtrl_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent &) { RefreshItemPreviews(); });
			withCtrl_->Bind(wxEVT_TEXT, &GroundSpecificActionDialog::OnValuesChanged, this);
			withCtrl_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent &) { RefreshItemPreviews(); });
			targetPreview_->Bind(wxEVT_BUTTON, &GroundSpecificActionDialog::OnPickTarget, this);
			withPreview_->Bind(wxEVT_BUTTON, &GroundSpecificActionDialog::OnPickWith, this);
			pickTargetButton_->Bind(wxEVT_BUTTON, &GroundSpecificActionDialog::OnPickTarget, this);
			pickWithButton_->Bind(wxEVT_BUTTON, &GroundSpecificActionDialog::OnPickWith, this);
		}

		const GroundBorderCaseActionRecord &GetValue() const {
			return value_;
		}

		bool TransferDataFromWindow() override {
			value_.actionType = typeChoice_->GetStringSelection();
			value_.edge = edgeChoice_->IsShown() ? edgeChoice_->GetStringSelection() : wxString();
			value_.targetValue = targetCtrl_->IsShown() ? targetCtrl_->GetValue() : 0;
			value_.replacementValue = withCtrl_->IsShown() ? withCtrl_->GetValue() : 0;
			value_.sortOrder = 0;

			if (value_.actionType.IsSameAs("delete_borders", false)) {
				value_.edge.clear();
				value_.targetValue = 0;
				value_.replacementValue = 0;
				return true;
			}

			if (value_.actionType.IsSameAs("replace_item", false)) {
				value_.edge.clear();
				if (value_.targetValue <= 0 || value_.replacementValue <= 0) {
					wxMessageBox("Target and With must be greater than zero.", "Action", wxOK | wxICON_WARNING, this);
					return false;
				}
				return true;
			}

			if (value_.edge.IsEmpty()) {
				wxMessageBox("Choose an edge.", "Action", wxOK | wxICON_WARNING, this);
				return false;
			}
			if (value_.targetValue <= 0 || value_.replacementValue <= 0) {
				wxMessageBox("Target and With must be greater than zero.", "Action", wxOK | wxICON_WARNING, this);
				return false;
			}
			return true;
		}

	private:
		void OnTypeChanged(wxCommandEvent &) {
			UpdateFieldVisibility();
		}

		void UpdateFieldVisibility() {
			const wxString type = typeChoice_->GetStringSelection();
			const bool isReplaceItem = type.IsSameAs("replace_item", false);
			const bool needsEdge = type.IsSameAs("replace_border", false);
			const bool needsTarget = !type.IsSameAs("delete_borders", false);
			const bool needsWith = type.IsSameAs("replace_border", false) || type.IsSameAs("replace_item", false);
			edgeChoice_->Show(needsEdge);
			targetCtrl_->Show(needsTarget);
			withCtrl_->Show(needsWith);
			targetPreview_->Show(isReplaceItem);
			withPreview_->Show(isReplaceItem);
			pickTargetButton_->Show(isReplaceItem);
			pickWithButton_->Show(isReplaceItem);
			RefreshItemPreviews();
			okButton_->Enable(true);
			Layout();
			Fit();
		}

		void RefreshItemPreviews() {
			if (!targetPreview_->IsShown()) {
				targetPreview_->SetSprite(0);
				withPreview_->SetSprite(0);
				return;
			}
			targetPreview_->SetSprite(ToItemPreviewSpriteId(targetCtrl_->GetValue()));
			withPreview_->SetSprite(ToItemPreviewSpriteId(withCtrl_->GetValue()));
		}

		void OnValuesChanged(wxCommandEvent &) {
			RefreshItemPreviews();
		}

		void OnPickTarget(wxCommandEvent &) {
			FindItemDialog dialog(this, "Select Target Item");
			dialog.setSearchMode(FindItemDialog::ItemIDs);
			if (dialog.ShowModal() != wxID_OK) {
				return;
			}
			targetCtrl_->SetValue(dialog.getResultID());
			RefreshItemPreviews();
		}

		void OnPickWith(wxCommandEvent &) {
			FindItemDialog dialog(this, "Select Replacement Item");
			dialog.setSearchMode(FindItemDialog::ItemIDs);
			if (dialog.ShowModal() != wxID_OK) {
				return;
			}
			withCtrl_->SetValue(dialog.getResultID());
			RefreshItemPreviews();
		}

		GroundBorderCaseActionRecord value_;
		wxChoice* typeChoice_ = nullptr;
		wxChoice* edgeChoice_ = nullptr;
		wxSpinCtrl* targetCtrl_ = nullptr;
		wxSpinCtrl* withCtrl_ = nullptr;
		ItemButton* targetPreview_ = nullptr;
		ItemButton* withPreview_ = nullptr;
		wxButton* pickTargetButton_ = nullptr;
		wxButton* pickWithButton_ = nullptr;
		wxButton* okButton_ = nullptr;
	};

	class GroundSpecificCasesDialog final : public wxDialog {
	public:
		GroundSpecificCasesDialog(wxWindow* parent, std::vector<GroundBorderCaseRecord> initialCases) :
			wxDialog(parent, wxID_ANY, "Specific Cases", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
			cases_(std::move(initialCases)) {
			wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);
			wxBoxSizer* contentSizer = new wxBoxSizer(wxHORIZONTAL);

			wxPanel* casePanel = new wxPanel(this, wxID_ANY);
			wxBoxSizer* caseSizer = new wxBoxSizer(wxVERTICAL);
			ui_.cases.caseList = new wxListBox(casePanel, wxID_ANY);
			wxBoxSizer* caseButtons = new wxBoxSizer(wxHORIZONTAL);
			ui_.cases.addCaseButton = new wxButton(casePanel, wxID_ANY, "+");
			ui_.cases.removeCaseButton = new wxButton(casePanel, wxID_ANY, "-");
			ui_.cases.caseUpButton = new wxButton(casePanel, wxID_ANY, "Up");
			ui_.cases.caseDownButton = new wxButton(casePanel, wxID_ANY, "Down");
			StyleBorderWorkspaceActionButton(ui_.cases.addCaseButton, "Add a new case.");
			StyleBorderWorkspaceActionButton(ui_.cases.removeCaseButton, "Remove the selected case.");
			StyleBorderWorkspaceActionButton(ui_.cases.caseUpButton, "Move the selected case earlier.");
			StyleBorderWorkspaceActionButton(ui_.cases.caseDownButton, "Move the selected case later.");
			caseButtons->Add(ui_.cases.addCaseButton, 0, wxRIGHT, FromDIP(4));
			caseButtons->Add(ui_.cases.removeCaseButton, 0, wxRIGHT, FromDIP(4));
			caseButtons->Add(ui_.cases.caseUpButton, 0, wxRIGHT, FromDIP(4));
			caseButtons->Add(ui_.cases.caseDownButton, 0);
			caseSizer->Add(new wxStaticText(casePanel, wxID_ANY, "Cases"), 0, wxBOTTOM, FromDIP(4));
			caseSizer->Add(ui_.cases.caseList, 1, wxEXPAND | wxBOTTOM, FromDIP(6));
			caseSizer->Add(caseButtons, 0, wxEXPAND);
			casePanel->SetSizer(caseSizer);

			wxPanel* detailPanel = new wxPanel(this, wxID_ANY);
			wxBoxSizer* detailSizer = new wxBoxSizer(wxVERTICAL);
			ui_.lists.warningLabel = new wxStaticText(detailPanel, wxID_ANY, "");
			StyleBorderWorkspaceCaption(ui_.lists.warningLabel);
			ui_.lists.conditionsList = new wxListCtrl(detailPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_THEME);
			ui_.lists.conditionsList->InsertColumn(0, "");
			ui_.lists.conditionsList->InsertColumn(1, "Type");
			ui_.lists.conditionsList->InsertColumn(2, "Edge");
			ui_.lists.conditionsList->InsertColumn(3, "Value");
			ui_.lists.conditionsList->SetColumnWidth(0, FromDIP(44));
			ui_.lists.conditionsList->SetColumnWidth(1, FromDIP(160));
			ui_.lists.conditionsList->SetColumnWidth(2, FromDIP(100));
			ui_.lists.conditionsList->SetColumnWidth(3, FromDIP(160));
			ui_.lists.actionsList = new wxListCtrl(detailPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_THEME);
			ui_.lists.actionsList->InsertColumn(0, "");
			ui_.lists.actionsList->InsertColumn(1, "Type");
			ui_.lists.actionsList->InsertColumn(2, "Edge");
			ui_.lists.actionsList->InsertColumn(3, "Target");
			ui_.lists.actionsList->InsertColumn(4, "With");
			ui_.lists.actionsList->SetColumnWidth(0, FromDIP(44));
			ui_.lists.actionsList->SetColumnWidth(1, FromDIP(160));
			ui_.lists.actionsList->SetColumnWidth(2, FromDIP(100));
			ui_.lists.actionsList->SetColumnWidth(3, FromDIP(160));
			ui_.lists.actionsList->SetColumnWidth(4, FromDIP(160));

			ui_.lists.conditionImages = new wxImageList(32, 32, true);
			ui_.lists.actionImages = new wxImageList(32, 32, true);
			ui_.lists.conditionsList->AssignImageList(ui_.lists.conditionImages, wxIMAGE_LIST_SMALL);
			ui_.lists.actionsList->AssignImageList(ui_.lists.actionImages, wxIMAGE_LIST_SMALL);

			wxBoxSizer* condButtons = new wxBoxSizer(wxHORIZONTAL);
			ui_.cond.addCondButton = new wxButton(detailPanel, wxID_ANY, "Add");
			ui_.cond.editCondButton = new wxButton(detailPanel, wxID_ANY, "Edit");
			ui_.cond.removeCondButton = new wxButton(detailPanel, wxID_ANY, "Remove");
			ui_.cond.condUpButton = new wxButton(detailPanel, wxID_ANY, "Up");
			ui_.cond.condDownButton = new wxButton(detailPanel, wxID_ANY, "Down");
			StyleBorderWorkspaceActionButton(ui_.cond.addCondButton, "Add a new condition.");
			StyleBorderWorkspaceActionButton(ui_.cond.editCondButton, "Edit the selected condition.");
			StyleBorderWorkspaceActionButton(ui_.cond.removeCondButton, "Remove the selected condition.");
			StyleBorderWorkspaceActionButton(ui_.cond.condUpButton, "Move the selected condition earlier.");
			StyleBorderWorkspaceActionButton(ui_.cond.condDownButton, "Move the selected condition later.");
			condButtons->Add(ui_.cond.addCondButton, 0, wxRIGHT, FromDIP(4));
			condButtons->Add(ui_.cond.editCondButton, 0, wxRIGHT, FromDIP(4));
			condButtons->Add(ui_.cond.removeCondButton, 0, wxRIGHT, FromDIP(4));
			condButtons->Add(ui_.cond.condUpButton, 0, wxRIGHT, FromDIP(4));
			condButtons->Add(ui_.cond.condDownButton, 0);

			wxBoxSizer* actButtons = new wxBoxSizer(wxHORIZONTAL);
			ui_.act.addActButton = new wxButton(detailPanel, wxID_ANY, "Add");
			ui_.act.editActButton = new wxButton(detailPanel, wxID_ANY, "Edit");
			ui_.act.removeActButton = new wxButton(detailPanel, wxID_ANY, "Remove");
			ui_.act.actUpButton = new wxButton(detailPanel, wxID_ANY, "Up");
			ui_.act.actDownButton = new wxButton(detailPanel, wxID_ANY, "Down");
			StyleBorderWorkspaceActionButton(ui_.act.addActButton, "Add a new action.");
			StyleBorderWorkspaceActionButton(ui_.act.editActButton, "Edit the selected action.");
			StyleBorderWorkspaceActionButton(ui_.act.removeActButton, "Remove the selected action.");
			StyleBorderWorkspaceActionButton(ui_.act.actUpButton, "Move the selected action earlier.");
			StyleBorderWorkspaceActionButton(ui_.act.actDownButton, "Move the selected action later.");
			actButtons->Add(ui_.act.addActButton, 0, wxRIGHT, FromDIP(4));
			actButtons->Add(ui_.act.editActButton, 0, wxRIGHT, FromDIP(4));
			actButtons->Add(ui_.act.removeActButton, 0, wxRIGHT, FromDIP(4));
			actButtons->Add(ui_.act.actUpButton, 0, wxRIGHT, FromDIP(4));
			actButtons->Add(ui_.act.actDownButton, 0);

			detailSizer->Add(ui_.lists.warningLabel, 0, wxBOTTOM, FromDIP(6));
			detailSizer->Add(new wxStaticText(detailPanel, wxID_ANY, "Conditions"), 0, wxBOTTOM, FromDIP(4));
			detailSizer->Add(ui_.lists.conditionsList, 1, wxEXPAND | wxBOTTOM, FromDIP(4));
			detailSizer->Add(condButtons, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
			detailSizer->Add(new wxStaticText(detailPanel, wxID_ANY, "Actions"), 0, wxBOTTOM, FromDIP(4));
			detailSizer->Add(ui_.lists.actionsList, 1, wxEXPAND | wxBOTTOM, FromDIP(4));
			wxBoxSizer* selectionRow = new wxBoxSizer(wxHORIZONTAL);
			auto ownedEdgePreview = std::make_unique<BorderEdgePreviewPanel>(detailPanel);
			ui_.selection.selectionEdgePreview = ownedEdgePreview.get();
			ownedEdgePreview.release();
			auto ownedPreviewPrimary = std::make_unique<ItemButton>(detailPanel, RENDER_SIZE_32x32, 0);
			ui_.selection.selectionPreviewPrimary = ownedPreviewPrimary.get();
			ownedPreviewPrimary.release();
			auto ownedPreviewSecondary = std::make_unique<ItemButton>(detailPanel, RENDER_SIZE_32x32, 0);
			ui_.selection.selectionPreviewSecondary = ownedPreviewSecondary.get();
			ownedPreviewSecondary.release();
			ui_.selection.selectionPreviewArrow = new wxStaticText(detailPanel, wxID_ANY, "→");
			ui_.selection.selectionPreviewPrimary->Enable(false);
			ui_.selection.selectionPreviewSecondary->Enable(false);
			StyleBorderWorkspaceCaption(ui_.selection.selectionPreviewArrow);
			ui_.selection.selectionPreviewLabel = new wxStaticText(detailPanel, wxID_ANY, "Select a condition or action to see details.");
			StyleBorderWorkspaceCaption(ui_.selection.selectionPreviewLabel);
			selectionRow->Add(ui_.selection.selectionEdgePreview, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));
			selectionRow->Add(ui_.selection.selectionPreviewPrimary, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
			selectionRow->Add(ui_.selection.selectionPreviewArrow, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
			selectionRow->Add(ui_.selection.selectionPreviewSecondary, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
			selectionRow->Add(ui_.selection.selectionPreviewLabel, 1, wxALIGN_CENTER_VERTICAL);
			detailSizer->Add(selectionRow, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
			detailSizer->Add(actButtons, 0, wxEXPAND);
			detailPanel->SetSizer(detailSizer);

			contentSizer->Add(casePanel, 0, wxEXPAND | wxRIGHT, FromDIP(10));
			contentSizer->Add(detailPanel, 1, wxEXPAND);
			rootSizer->Add(contentSizer, 1, wxEXPAND | wxALL, FromDIP(12));

			wxStdDialogButtonSizer* buttons = new wxStdDialogButtonSizer();
			buttons->AddButton(new wxButton(this, wxID_OK));
			buttons->AddButton(new wxButton(this, wxID_CANCEL));
			buttons->Realize();
			rootSizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

			SetSizerAndFit(rootSizer);
			SetMinSize(wxSize(FromDIP(760), FromDIP(520)));

			ui_.cases.addCaseButton->Bind(wxEVT_BUTTON, &GroundSpecificCasesDialog::OnAddCase, this);
			ui_.cases.removeCaseButton->Bind(wxEVT_BUTTON, &GroundSpecificCasesDialog::OnRemoveCase, this);
			ui_.cases.caseUpButton->Bind(wxEVT_BUTTON, &GroundSpecificCasesDialog::OnMoveCaseUp, this);
			ui_.cases.caseDownButton->Bind(wxEVT_BUTTON, &GroundSpecificCasesDialog::OnMoveCaseDown, this);
			ui_.cases.caseList->Bind(wxEVT_LISTBOX, &GroundSpecificCasesDialog::OnCaseSelected, this);

			ui_.cond.addCondButton->Bind(wxEVT_BUTTON, &GroundSpecificCasesDialog::OnAddCondition, this);
			ui_.cond.editCondButton->Bind(wxEVT_BUTTON, &GroundSpecificCasesDialog::OnEditCondition, this);
			ui_.cond.removeCondButton->Bind(wxEVT_BUTTON, &GroundSpecificCasesDialog::OnRemoveCondition, this);
			ui_.cond.condUpButton->Bind(wxEVT_BUTTON, &GroundSpecificCasesDialog::OnMoveConditionUp, this);
			ui_.cond.condDownButton->Bind(wxEVT_BUTTON, &GroundSpecificCasesDialog::OnMoveConditionDown, this);
			ui_.lists.conditionsList->Bind(wxEVT_LIST_ITEM_ACTIVATED, &GroundSpecificCasesDialog::OnConditionActivated, this);
			ui_.lists.conditionsList->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent &event) {
				selectedConditionIndex_ = event.GetIndex();
				activeSelectionIsAction_ = false;
				UpdateButtons();
				RefreshSelectionPreview();
			});

			ui_.act.addActButton->Bind(wxEVT_BUTTON, &GroundSpecificCasesDialog::OnAddAction, this);
			ui_.act.editActButton->Bind(wxEVT_BUTTON, &GroundSpecificCasesDialog::OnEditAction, this);
			ui_.act.removeActButton->Bind(wxEVT_BUTTON, &GroundSpecificCasesDialog::OnRemoveAction, this);
			ui_.act.actUpButton->Bind(wxEVT_BUTTON, &GroundSpecificCasesDialog::OnMoveActionUp, this);
			ui_.act.actDownButton->Bind(wxEVT_BUTTON, &GroundSpecificCasesDialog::OnMoveActionDown, this);
			ui_.lists.actionsList->Bind(wxEVT_LIST_ITEM_ACTIVATED, &GroundSpecificCasesDialog::OnActionActivated, this);
			ui_.lists.actionsList->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent &event) {
				selectedActionIndex_ = event.GetIndex();
				activeSelectionIsAction_ = true;
				UpdateButtons();
				RefreshSelectionPreview();
			});

			RefreshCaseList();
			SelectCase(0);
		}

		const std::vector<GroundBorderCaseRecord> &GetCases() const {
			return cases_;
		}

	private:
		int ResolveBorderEdgeSpriteId(int xmlBorderId, const wxString &edge) {
			if (xmlBorderId <= 0 || edge.IsEmpty()) {
				return 0;
			}
			auto it = borderSetItemsByXmlId_.find(xmlBorderId);
			if (it == borderSetItemsByXmlId_.end()) {
				BorderSetRecord borderSet;
				if (!g_brush_database.findBorderSetByXmlBorderId(xmlBorderId, borderSet) || borderSet.id <= 0) {
					borderSetItemsByXmlId_.try_emplace(xmlBorderId);
					return 0;
				}
				std::vector<BorderSetItemRecord> items;
				if (!g_brush_database.getBorderSetItems(borderSet.id, items)) {
					borderSetItemsByXmlId_.try_emplace(xmlBorderId);
					return 0;
				}
				it = borderSetItemsByXmlId_.try_emplace(xmlBorderId, std::move(items)).first;
			}

			const std::vector<BorderSetItemRecord> &items = it->second;
			int bestItemId = 0;
			int bestSortOrder = std::numeric_limits<int>::max();
			for (const BorderSetItemRecord &item : items) {
				if (!item.edge.IsSameAs(edge, false) || item.itemId <= 0) {
					continue;
				}
				if (item.sortOrder < bestSortOrder) {
					bestSortOrder = item.sortOrder;
					bestItemId = item.itemId;
				}
			}
			return ToItemPreviewSpriteId(bestItemId);
		}

		int EnsureConditionIcon(int spriteId) {
			if (spriteId <= 0) {
				return -1;
			}
			for (const IconCacheEntry &entry : conditionIconCache_) {
				if (entry.spriteId == spriteId) {
					return entry.imageIndex;
				}
			}
			const wxBitmap bitmap = RenderSpriteToBitmap(spriteId);
			if (!bitmap.IsOk()) {
				return -1;
			}
			const int imageIndex = ui_.lists.conditionImages->Add(bitmap);
			conditionIconCache_.push_back({ spriteId, imageIndex });
			return imageIndex;
		}

		int EnsureActionIcon(int spriteId) {
			if (spriteId <= 0) {
				return -1;
			}
			for (const IconCacheEntry &entry : actionIconCache_) {
				if (entry.spriteId == spriteId) {
					return entry.imageIndex;
				}
			}
			const wxBitmap bitmap = RenderSpriteToBitmap(spriteId);
			if (!bitmap.IsOk()) {
				return -1;
			}
			const int imageIndex = ui_.lists.actionImages->Add(bitmap);
			actionIconCache_.push_back({ spriteId, imageIndex });
			return imageIndex;
		}

		void RefreshCaseList() {
			ui_.cases.caseList->Clear();
			for (size_t i = 0; i < cases_.size(); ++i) {
				const GroundBorderCaseRecord &caseRecord = cases_[i];
				ui_.cases.caseList->Append(wxString::Format("Case %zu  (%zu cond / %zu act)", i + 1, caseRecord.conditions.size(), caseRecord.actions.size()));
			}
			if (cases_.empty()) {
				ui_.cases.caseList->Append("No cases");
			}
		}

		void SelectCase(int index) {
			if (cases_.empty()) {
				selectedCaseIndex_ = -1;
				ui_.cases.caseList->SetSelection(0);
				RefreshDetails();
				return;
			}
			selectedCaseIndex_ = std::clamp(index, 0, static_cast<int>(cases_.size()) - 1);
			ui_.cases.caseList->SetSelection(selectedCaseIndex_);
			RefreshDetails();
		}

		void RefreshDetails() {
			ui_.lists.conditionsList->DeleteAllItems();
			ui_.lists.actionsList->DeleteAllItems();
			ui_.lists.conditionImages->RemoveAll();
			ui_.lists.actionImages->RemoveAll();
			conditionIconCache_.clear();
			actionIconCache_.clear();
			borderSetItemsByXmlId_.clear();
			selectedConditionIndex_ = -1;
			selectedActionIndex_ = -1;
			activeSelectionIsAction_ = false;
			if (selectedCaseIndex_ < 0 || selectedCaseIndex_ >= static_cast<int>(cases_.size())) {
				ui_.lists.warningLabel->SetLabel("Select a case to edit its conditions and actions.");
				RefreshSelectionPreview();
				UpdateButtons();
				return;
			}

			const GroundBorderCaseRecord &caseRecord = cases_[static_cast<size_t>(selectedCaseIndex_)];
			for (size_t i = 0; i < caseRecord.conditions.size(); ++i) {
				const GroundBorderCaseConditionRecord &condition = caseRecord.conditions[i];
				int spriteId = 0;
				if (condition.conditionType.IsSameAs("match_item", false)) {
					spriteId = ToItemPreviewSpriteId(condition.matchValue);
				} else if (condition.conditionType.IsSameAs("match_border", false)) {
					spriteId = ResolveBorderEdgeSpriteId(condition.matchValue, condition.edge);
				}
				const long row = ui_.lists.conditionsList->InsertItem(static_cast<long>(i), "", EnsureConditionIcon(spriteId));
				ui_.lists.conditionsList->SetItem(row, 1, FormatSpecificCaseTypeLabel(condition.conditionType));
				const wxString edgeLabel = condition.edge.IsEmpty() ? wxString::FromUTF8("-") : GetBorderEdgeDisplayLabel(condition.edge);
				ui_.lists.conditionsList->SetItem(row, 2, edgeLabel);
				ui_.lists.conditionsList->SetItem(row, 3, FormatSpecificCaseValueLabel(condition.conditionType, condition.matchValue));
			}
			for (size_t i = 0; i < caseRecord.actions.size(); ++i) {
				const GroundBorderCaseActionRecord &action = caseRecord.actions[i];
				int spriteId = 0;
				if (action.actionType.IsSameAs("replace_item", false)) {
					spriteId = ToItemPreviewSpriteId(action.replacementValue);
				} else if (action.actionType.IsSameAs("replace_border", false)) {
					spriteId = ResolveBorderEdgeSpriteId(action.replacementValue, action.edge);
				}
				const long row = ui_.lists.actionsList->InsertItem(static_cast<long>(i), "", EnsureActionIcon(spriteId));
				ui_.lists.actionsList->SetItem(row, 1, FormatSpecificCaseTypeLabel(action.actionType));
				const wxString edgeLabel = action.edge.IsEmpty() ? wxString::FromUTF8("-") : GetBorderEdgeDisplayLabel(action.edge);
				ui_.lists.actionsList->SetItem(row, 2, edgeLabel);
				if (action.actionType.IsSameAs("delete_borders", false)) {
					ui_.lists.actionsList->SetItem(row, 3, wxString());
					ui_.lists.actionsList->SetItem(row, 4, wxString());
				} else if (action.actionType.IsSameAs("replace_item", false)) {
					ui_.lists.actionsList->SetItem(row, 3, FormatSpecificCaseValueLabel(action.actionType, action.targetValue));
					ui_.lists.actionsList->SetItem(row, 4, FormatSpecificCaseValueLabel(action.actionType, action.replacementValue));
				} else {
					ui_.lists.actionsList->SetItem(row, 3, FormatSpecificCaseValueLabel(action.actionType, action.targetValue));
					ui_.lists.actionsList->SetItem(row, 4, FormatSpecificCaseValueLabel(action.actionType, action.replacementValue));
				}
			}
			RefreshSelectionPreview();

			wxString warning;
			if (caseRecord.conditions.empty()) {
				warning += "No conditions: this case always matches. ";
			}
			if (caseRecord.actions.empty()) {
				warning += "No actions: this case does nothing. ";
			}
			ui_.lists.warningLabel->SetLabel(warning);
			UpdateButtons();
		}

		void RefreshSelectionPreview() {
			auto SetPreviewItem = [](ItemButton* button, int itemId) {
				button->SetSprite(ToItemPreviewSpriteId(itemId));
			};

			ui_.selection.selectionEdgePreview->ClearEdge();
			SetPreviewItem(ui_.selection.selectionPreviewPrimary, 0);
			SetPreviewItem(ui_.selection.selectionPreviewSecondary, 0);
			ui_.selection.selectionEdgePreview->Hide();
			ui_.selection.selectionPreviewPrimary->Hide();
			ui_.selection.selectionPreviewSecondary->Hide();
			ui_.selection.selectionPreviewArrow->Hide();
			ui_.selection.selectionPreviewLabel->SetLabel("Select a condition or action to see details.");

			if (selectedCaseIndex_ < 0 || selectedCaseIndex_ >= static_cast<int>(cases_.size())) {
				Layout();
				return;
			}

			const GroundBorderCaseRecord &caseRecord = cases_[static_cast<size_t>(selectedCaseIndex_)];
			if (activeSelectionIsAction_) {
				if (selectedActionIndex_ < 0 || selectedActionIndex_ >= static_cast<int>(caseRecord.actions.size())) {
					Layout();
					return;
				}
				const GroundBorderCaseActionRecord &action = caseRecord.actions[static_cast<size_t>(selectedActionIndex_)];
				ui_.selection.selectionPreviewLabel->SetLabel(DescribeGroundCaseAction(action));
				ui_.selection.selectionEdgePreview->SetEdge(action.edge);
				ui_.selection.selectionEdgePreview->Show(!action.edge.IsEmpty());
				if (action.actionType.IsSameAs("replace_item", false)) {
					SetPreviewItem(ui_.selection.selectionPreviewPrimary, action.targetValue);
					SetPreviewItem(ui_.selection.selectionPreviewSecondary, action.replacementValue);
					ui_.selection.selectionPreviewPrimary->Show();
					ui_.selection.selectionPreviewSecondary->Show();
					ui_.selection.selectionPreviewArrow->Show();
				} else if (action.actionType.IsSameAs("replace_border", false)) {
					ui_.selection.selectionPreviewPrimary->SetSprite(ResolveBorderEdgeSpriteId(action.targetValue, action.edge));
					ui_.selection.selectionPreviewSecondary->SetSprite(ResolveBorderEdgeSpriteId(action.replacementValue, action.edge));
					ui_.selection.selectionPreviewPrimary->Show();
					ui_.selection.selectionPreviewSecondary->Show();
					ui_.selection.selectionPreviewArrow->Show();
				}
				Layout();
				return;
			}

			if (selectedConditionIndex_ < 0 || selectedConditionIndex_ >= static_cast<int>(caseRecord.conditions.size())) {
				Layout();
				return;
			}
			const GroundBorderCaseConditionRecord &condition = caseRecord.conditions[static_cast<size_t>(selectedConditionIndex_)];
			ui_.selection.selectionPreviewLabel->SetLabel(DescribeGroundCaseCondition(condition));
			ui_.selection.selectionEdgePreview->SetEdge(condition.edge);
			ui_.selection.selectionEdgePreview->Show(!condition.edge.IsEmpty());
			if (condition.conditionType.IsSameAs("match_item", false)) {
				SetPreviewItem(ui_.selection.selectionPreviewPrimary, condition.matchValue);
				ui_.selection.selectionPreviewPrimary->Show();
			} else if (condition.conditionType.IsSameAs("match_border", false)) {
				ui_.selection.selectionPreviewPrimary->SetSprite(ResolveBorderEdgeSpriteId(condition.matchValue, condition.edge));
				ui_.selection.selectionPreviewPrimary->Show();
			}
			Layout();
		}

		void UpdateButtons() {
			const bool hasCase = selectedCaseIndex_ >= 0 && selectedCaseIndex_ < static_cast<int>(cases_.size());
			ui_.cases.removeCaseButton->Enable(hasCase);
			ui_.cases.caseUpButton->Enable(hasCase && selectedCaseIndex_ > 0);
			ui_.cases.caseDownButton->Enable(hasCase && selectedCaseIndex_ >= 0 && selectedCaseIndex_ < static_cast<int>(cases_.size()) - 1);

			ui_.cond.addCondButton->Enable(hasCase);
			ui_.cond.editCondButton->Enable(hasCase && selectedConditionIndex_ >= 0);
			ui_.cond.removeCondButton->Enable(hasCase && selectedConditionIndex_ >= 0);
			ui_.cond.condUpButton->Enable(hasCase && selectedConditionIndex_ > 0);
			ui_.cond.condDownButton->Enable(hasCase && selectedConditionIndex_ >= 0 && selectedConditionIndex_ < static_cast<int>(cases_[static_cast<size_t>(selectedCaseIndex_)].conditions.size()) - 1);

			ui_.act.addActButton->Enable(hasCase);
			ui_.act.editActButton->Enable(hasCase && selectedActionIndex_ >= 0);
			ui_.act.removeActButton->Enable(hasCase && selectedActionIndex_ >= 0);
			ui_.act.actUpButton->Enable(hasCase && selectedActionIndex_ > 0);
			ui_.act.actDownButton->Enable(hasCase && selectedActionIndex_ >= 0 && selectedActionIndex_ < static_cast<int>(cases_[static_cast<size_t>(selectedCaseIndex_)].actions.size()) - 1);
		}

		void OnCaseSelected(wxCommandEvent &) {
			SelectCase(ui_.cases.caseList->GetSelection());
		}

		void OnAddCase(wxCommandEvent &) {
			GroundBorderCaseRecord newCase;
			cases_.push_back(newCase);
			NormalizeGroundBorderCases(cases_);
			RefreshCaseList();
			SelectCase(static_cast<int>(cases_.size()) - 1);
		}

		void OnRemoveCase(wxCommandEvent &) {
			if (selectedCaseIndex_ < 0 || selectedCaseIndex_ >= static_cast<int>(cases_.size())) {
				return;
			}
			cases_.erase(cases_.begin() + selectedCaseIndex_);
			NormalizeGroundBorderCases(cases_);
			RefreshCaseList();
			SelectCase(std::min(selectedCaseIndex_, static_cast<int>(cases_.size()) - 1));
		}

		void OnMoveCaseUp(wxCommandEvent &) {
			MoveCaseBy(-1);
		}
		void OnMoveCaseDown(wxCommandEvent &) {
			MoveCaseBy(1);
		}

		void MoveCaseBy(int delta) {
			if (selectedCaseIndex_ < 0 || selectedCaseIndex_ >= static_cast<int>(cases_.size())) {
				return;
			}
			const int next = selectedCaseIndex_ + delta;
			if (next < 0 || next >= static_cast<int>(cases_.size())) {
				return;
			}
			std::swap(cases_[static_cast<size_t>(selectedCaseIndex_)], cases_[static_cast<size_t>(next)]);
			selectedCaseIndex_ = next;
			NormalizeGroundBorderCases(cases_);
			RefreshCaseList();
			SelectCase(selectedCaseIndex_);
		}

		void OnAddCondition(wxCommandEvent &) {
			if (selectedCaseIndex_ < 0) {
				return;
			}
			GroundBorderCaseConditionRecord condition;
			condition.conditionType = "match_item";
			GroundSpecificConditionDialog dialog(this, condition);
			if (dialog.ShowModal() != wxID_OK) {
				return;
			}
			cases_[static_cast<size_t>(selectedCaseIndex_)].conditions.push_back(dialog.GetValue());
			NormalizeGroundCaseSortOrders(cases_[static_cast<size_t>(selectedCaseIndex_)]);
			RefreshCaseList();
			RefreshDetails();
		}

		void OnEditCondition(wxCommandEvent &) {
			if (selectedCaseIndex_ < 0 || selectedConditionIndex_ < 0) {
				return;
			}
			auto &conditions = cases_[static_cast<size_t>(selectedCaseIndex_)].conditions;
			if (selectedConditionIndex_ >= static_cast<int>(conditions.size())) {
				return;
			}
			GroundSpecificConditionDialog dialog(this, conditions[static_cast<size_t>(selectedConditionIndex_)]);
			if (dialog.ShowModal() != wxID_OK) {
				return;
			}
			conditions[static_cast<size_t>(selectedConditionIndex_)] = dialog.GetValue();
			NormalizeGroundCaseSortOrders(cases_[static_cast<size_t>(selectedCaseIndex_)]);
			RefreshCaseList();
			RefreshDetails();
		}

		void OnRemoveCondition(wxCommandEvent &) {
			if (selectedCaseIndex_ < 0 || selectedConditionIndex_ < 0) {
				return;
			}
			auto &conditions = cases_[static_cast<size_t>(selectedCaseIndex_)].conditions;
			if (selectedConditionIndex_ >= static_cast<int>(conditions.size())) {
				return;
			}
			conditions.erase(conditions.begin() + selectedConditionIndex_);
			selectedConditionIndex_ = -1;
			NormalizeGroundCaseSortOrders(cases_[static_cast<size_t>(selectedCaseIndex_)]);
			RefreshCaseList();
			RefreshDetails();
		}

		void OnMoveConditionUp(wxCommandEvent &) {
			MoveConditionBy(-1);
		}
		void OnMoveConditionDown(wxCommandEvent &) {
			MoveConditionBy(1);
		}

		void MoveConditionBy(int delta) {
			if (selectedCaseIndex_ < 0 || selectedConditionIndex_ < 0) {
				return;
			}
			auto &conditions = cases_[static_cast<size_t>(selectedCaseIndex_)].conditions;
			const int next = selectedConditionIndex_ + delta;
			if (next < 0 || next >= static_cast<int>(conditions.size())) {
				return;
			}
			std::swap(conditions[static_cast<size_t>(selectedConditionIndex_)], conditions[static_cast<size_t>(next)]);
			selectedConditionIndex_ = next;
			NormalizeGroundCaseSortOrders(cases_[static_cast<size_t>(selectedCaseIndex_)]);
			RefreshDetails();
		}

		void OnConditionActivated(wxListEvent &) {
			selectedConditionIndex_ = ui_.lists.conditionsList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
			activeSelectionIsAction_ = false;
			UpdateButtons();
			RefreshSelectionPreview();
			wxCommandEvent dummy;
			OnEditCondition(dummy);
		}

		void OnAddAction(wxCommandEvent &) {
			if (selectedCaseIndex_ < 0) {
				return;
			}
			GroundBorderCaseActionRecord action;
			action.actionType = "replace_item";
			GroundSpecificActionDialog dialog(this, action);
			if (dialog.ShowModal() != wxID_OK) {
				return;
			}
			cases_[static_cast<size_t>(selectedCaseIndex_)].actions.push_back(dialog.GetValue());
			NormalizeGroundCaseSortOrders(cases_[static_cast<size_t>(selectedCaseIndex_)]);
			RefreshCaseList();
			RefreshDetails();
		}

		void OnEditAction(wxCommandEvent &) {
			if (selectedCaseIndex_ < 0 || selectedActionIndex_ < 0) {
				return;
			}
			auto &actions = cases_[static_cast<size_t>(selectedCaseIndex_)].actions;
			if (selectedActionIndex_ >= static_cast<int>(actions.size())) {
				return;
			}
			GroundSpecificActionDialog dialog(this, actions[static_cast<size_t>(selectedActionIndex_)]);
			if (dialog.ShowModal() != wxID_OK) {
				return;
			}
			actions[static_cast<size_t>(selectedActionIndex_)] = dialog.GetValue();
			NormalizeGroundCaseSortOrders(cases_[static_cast<size_t>(selectedCaseIndex_)]);
			RefreshCaseList();
			RefreshDetails();
		}

		void OnRemoveAction(wxCommandEvent &) {
			if (selectedCaseIndex_ < 0 || selectedActionIndex_ < 0) {
				return;
			}
			auto &actions = cases_[static_cast<size_t>(selectedCaseIndex_)].actions;
			if (selectedActionIndex_ >= static_cast<int>(actions.size())) {
				return;
			}
			actions.erase(actions.begin() + selectedActionIndex_);
			selectedActionIndex_ = -1;
			NormalizeGroundCaseSortOrders(cases_[static_cast<size_t>(selectedCaseIndex_)]);
			RefreshCaseList();
			RefreshDetails();
		}

		void OnMoveActionUp(wxCommandEvent &) {
			MoveActionBy(-1);
		}
		void OnMoveActionDown(wxCommandEvent &) {
			MoveActionBy(1);
		}

		void MoveActionBy(int delta) {
			if (selectedCaseIndex_ < 0 || selectedActionIndex_ < 0) {
				return;
			}
			auto &actions = cases_[static_cast<size_t>(selectedCaseIndex_)].actions;
			const int next = selectedActionIndex_ + delta;
			if (next < 0 || next >= static_cast<int>(actions.size())) {
				return;
			}
			std::swap(actions[static_cast<size_t>(selectedActionIndex_)], actions[static_cast<size_t>(next)]);
			selectedActionIndex_ = next;
			NormalizeGroundCaseSortOrders(cases_[static_cast<size_t>(selectedCaseIndex_)]);
			RefreshDetails();
		}

		void OnActionActivated(wxListEvent &) {
			selectedActionIndex_ = ui_.lists.actionsList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
			activeSelectionIsAction_ = true;
			UpdateButtons();
			RefreshSelectionPreview();
			wxCommandEvent dummy;
			OnEditAction(dummy);
		}

		struct Widgets {
			struct Cases {
				wxListBox* caseList = nullptr;
				wxButton* addCaseButton = nullptr;
				wxButton* removeCaseButton = nullptr;
				wxButton* caseUpButton = nullptr;
				wxButton* caseDownButton = nullptr;
			};
			struct Lists {
				wxStaticText* warningLabel = nullptr;
				wxListCtrl* conditionsList = nullptr;
				wxListCtrl* actionsList = nullptr;
				wxImageList* conditionImages = nullptr;
				wxImageList* actionImages = nullptr;
			};
			struct SelectionPreview {
				BorderEdgePreviewPanel* selectionEdgePreview = nullptr;
				ItemButton* selectionPreviewPrimary = nullptr;
				ItemButton* selectionPreviewSecondary = nullptr;
				wxStaticText* selectionPreviewArrow = nullptr;
				wxStaticText* selectionPreviewLabel = nullptr;
			};
			struct ConditionButtons {
				wxButton* addCondButton = nullptr;
				wxButton* editCondButton = nullptr;
				wxButton* removeCondButton = nullptr;
				wxButton* condUpButton = nullptr;
				wxButton* condDownButton = nullptr;
			};
			struct ActionButtons {
				wxButton* addActButton = nullptr;
				wxButton* editActButton = nullptr;
				wxButton* removeActButton = nullptr;
				wxButton* actUpButton = nullptr;
				wxButton* actDownButton = nullptr;
			};

			Cases cases;
			Lists lists;
			SelectionPreview selection;
			ConditionButtons cond;
			ActionButtons act;
		};

		std::vector<GroundBorderCaseRecord> cases_;
		Widgets ui_;
		int selectedCaseIndex_ = -1;
		int selectedConditionIndex_ = -1;
		int selectedActionIndex_ = -1;
		bool activeSelectionIsAction_ = false;

		struct IconCacheEntry {
			int spriteId;
			int imageIndex;
		};
		std::vector<IconCacheEntry> conditionIconCache_;
		std::vector<IconCacheEntry> actionIconCache_;
		std::unordered_map<int, std::vector<BorderSetItemRecord>> borderSetItemsByXmlId_;
	};

	wxString BuildBorderSetDisplayLabel(const BorderSetRecord &borderSet) {
		if (borderSet.xmlBorderId > 0) {
			return wxString::Format("Border %d", borderSet.xmlBorderId);
		}
		return wxString::Format("Border Set #%lld", static_cast<long long>(borderSet.id));
	}

	wxStaticText* CreateBorderSectionLabel(wxWindow* parent, const wxString &label) {
		wxStaticText* text = new wxStaticText(parent, wxID_ANY, label);
		wxFont font = text->GetFont();
		font.SetWeight(wxFONTWEIGHT_BOLD);
		text->SetFont(font);
		return text;
	}

	void StyleBorderWorkspaceSubtitle(wxStaticText* label) {
		label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
	}

	void StyleBorderWorkspaceStatusLabel(wxStaticText* label) {
		label->SetMinSize(wxSize(-1, label->GetParent()->FromDIP(20)));
		label->Wrap(label->GetParent()->FromDIP(760));
	}

	void StyleBorderWorkspaceActionButton(wxButton* button, const wxString &tooltip) {
		button->SetMinSize(wxSize(-1, button->GetParent()->FromDIP(20)));
		button->SetToolTip(tooltip);
	}

	wxString DescribeGroundCaseCondition(const GroundBorderCaseConditionRecord &condition) {
		if (condition.conditionType.IsSameAs("match_group", false)) {
			return wxString::Format("Match group %s on %s.", FormatSpecificCaseValueLabel("match_group", condition.matchValue), GetBorderEdgeDisplayLabel(condition.edge));
		}
		if (condition.conditionType.IsSameAs("match_border", false)) {
			return wxString::Format("Match %s on %s.", FormatSpecificCaseValueLabel("match_border", condition.matchValue), GetBorderEdgeDisplayLabel(condition.edge));
		}
		if (condition.conditionType.IsSameAs("match_item", false)) {
			return wxString::Format("Match %s.", FormatSpecificCaseValueLabel("match_item", condition.matchValue));
		}
		return condition.conditionType.IsEmpty() ? wxString::FromUTF8("Condition") : condition.conditionType;
	}

	wxString DescribeGroundCaseAction(const GroundBorderCaseActionRecord &action) {
		if (action.actionType.IsSameAs("replace_border", false)) {
			return wxString::Format(
				"Replace %s on %s with %s.",
				FormatSpecificCaseValueLabel("replace_border", action.targetValue),
				GetBorderEdgeDisplayLabel(action.edge),
				FormatSpecificCaseValueLabel("replace_border", action.replacementValue)
			);
		}
		if (action.actionType.IsSameAs("replace_item", false)) {
			return wxString::Format(
				"Replace %s with %s.",
				FormatSpecificCaseValueLabel("replace_item", action.targetValue),
				FormatSpecificCaseValueLabel("replace_item", action.replacementValue)
			);
		}
		if (action.actionType.IsSameAs("delete_borders", false)) {
			return "Delete borders on the selected edge.";
		}
		return action.actionType.IsEmpty() ? wxString::FromUTF8("Action") : action.actionType;
	}

	void NormalizeGroundCaseSortOrders(GroundBorderCaseRecord &caseRecord) {
		caseRecord.sortOrder = 0;
		for (size_t i = 0; i < caseRecord.conditions.size(); ++i) {
			caseRecord.conditions[i].sortOrder = static_cast<int>(i);
		}
		for (size_t i = 0; i < caseRecord.actions.size(); ++i) {
			caseRecord.actions[i].sortOrder = static_cast<int>(i);
		}
	}

	void NormalizeGroundBorderCases(std::vector<GroundBorderCaseRecord> &cases) {
		for (size_t i = 0; i < cases.size(); ++i) {
			cases[i].sortOrder = static_cast<int>(i);
			NormalizeGroundCaseSortOrders(cases[i]);
		}
	}

	void StyleBorderWorkspaceCaption(wxStaticText* label) {
		label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
	}

	void StyleBorderWorkspaceStrongValue(wxStaticText* label) {
		wxFont font = label->GetFont();
		font.SetWeight(wxFONTWEIGHT_BOLD);
		label->SetFont(font);
	}

	wxPanel* CreateSpacerCell(wxWindow* parent, int widthDip, int heightDip) {
		wxPanel* spacer = new wxPanel(parent, wxID_ANY);
		spacer->SetMinSize(wxSize(parent->FromDIP(widthDip), parent->FromDIP(heightDip)));
		spacer->SetBackgroundColour(parent->GetBackgroundColour());
		return spacer;
	}

	wxPoint GetBorderPreviewDisplayCell(const BorderEdgeSpec &spec) {
		int row = spec.row;
		int col = spec.col;

		// Pull the outer silhouette inward without inventing extra center fill.
		if (row == 0 && col == 0) {
			return wxPoint(1, 0);
		}
		if (row == 0 && col == 4) {
			return wxPoint(3, 0);
		}
		if (row == 4 && col == 0) {
			return wxPoint(1, 4);
		}
		if (row == 4 && col == 4) {
			return wxPoint(3, 4);
		}
		return wxPoint(col, row);
	}

	std::vector<wxPoint> GetBorderPreviewDuplicateCells(const wxString &edge) {
		if (edge == "cse") {
			return { wxPoint(0, 1) }; // b1
		}
		if (edge == "csw") {
			return { wxPoint(4, 1) }; // b5
		}
		if (edge == "cne") {
			return { wxPoint(0, 3) }; // d1
		}
		if (edge == "cnw") {
			return { wxPoint(4, 3) }; // d5
		}
		return {};
	}

	class BorderPreviewMatrixPanel : public wxPanel {
	public:
		explicit BorderPreviewMatrixPanel(wxWindow* parent) :
			wxPanel(parent, wxID_ANY) {
			SetBackgroundStyle(wxBG_STYLE_PAINT);
			SetMinSize(wxSize(FromDIP(172), FromDIP(172)));
			Bind(wxEVT_PAINT, &BorderPreviewMatrixPanel::OnPaint, this);
		}

		void SetPreviewState(const std::map<wxString, int> &slotItemIds, int centerItemId, const wxString &selectedEdge) {
			slotItemIds_ = slotItemIds;
			centerItemId_ = centerItemId;
			selectedEdge_ = selectedEdge;
			Refresh();
		}

	private:
		void DrawSprite(wxDC &dc, int itemId, const wxRect &cellRect, int tileSize) const {
			if (itemId <= 0) {
				return;
			}
			if (Sprite* sprite = g_gui.gfx.getSprite(itemId)) {
				const int margin = std::max(1, FromDIP(2));
				const int spriteSize = std::max(1, tileSize - margin * 2);
				const int drawX = cellRect.GetX() + (tileSize - spriteSize) / 2;
				const int drawY = cellRect.GetY() + (tileSize - spriteSize) / 2;
				sprite->DrawTo(&dc, SPRITE_SIZE_32x32, drawX, drawY, spriteSize, spriteSize);
			}
		}

		void OnPaint(wxPaintEvent &) {
			wxAutoBufferedPaintDC dc(this);
			const wxSize clientSize = GetClientSize();
			dc.SetBackground(wxBrush(wxColour(16, 16, 16)));
			dc.Clear();

			const int tileSize = std::max(1, std::min(clientSize.GetWidth(), clientSize.GetHeight()) / kBorderGridSize);
			const int sceneSize = tileSize * kBorderGridSize;
			const int originX = std::max(0, (clientSize.GetWidth() - sceneSize) / 2);
			const int originY = std::max(0, (clientSize.GetHeight() - sceneSize) / 2);

			const wxColour cellA(28, 28, 28);
			const wxColour cellB(24, 24, 24);
			const wxColour gridLine(0, 0, 0, 72);

			dc.SetPen(wxPen(gridLine, 1));
			for (int row = 0; row < kBorderGridSize; ++row) {
				for (int col = 0; col < kBorderGridSize; ++col) {
					const wxRect cellRect(originX + col * tileSize, originY + row * tileSize, tileSize, tileSize);
					dc.SetBrush(wxBrush(((row + col) % 2 == 0) ? cellA : cellB));
					dc.DrawRectangle(cellRect);
				}
			}

			if (centerItemId_ > 0) {
				DrawSprite(
					dc,
					centerItemId_,
					wxRect(originX + kBorderGridCenterIndex * tileSize, originY + kBorderGridCenterIndex * tileSize, tileSize, tileSize),
					tileSize
				);
			}

			for (const BorderEdgeSpec &spec : kBorderEdgeSpecs) {
				const wxString edge = wxString::FromUTF8(spec.edge);
				const auto it = slotItemIds_.find(edge);
				if (it == slotItemIds_.end() || it->second <= 0) {
					continue;
				}
				for (const wxPoint &duplicateCell : GetBorderPreviewDuplicateCells(edge)) {
					DrawSprite(
						dc,
						it->second,
						wxRect(originX + duplicateCell.x * tileSize, originY + duplicateCell.y * tileSize, tileSize, tileSize),
						tileSize
					);
				}
			}

			for (const BorderEdgeSpec &spec : kBorderEdgeSpecs) {
				const wxString edge = wxString::FromUTF8(spec.edge);
				const auto it = slotItemIds_.find(edge);
				if (it == slotItemIds_.end() || it->second <= 0) {
					continue;
				}
				const wxPoint displayCell = GetBorderPreviewDisplayCell(spec);
				DrawSprite(
					dc,
					it->second,
					wxRect(originX + displayCell.x * tileSize, originY + displayCell.y * tileSize, tileSize, tileSize),
					tileSize
				);
			}

			if (!selectedEdge_.IsEmpty()) {
				if (const BorderEdgeSpec* selectedSpec = FindEdgeSpec(selectedEdge_)) {
					const wxPoint displayCell = GetBorderPreviewDisplayCell(*selectedSpec);
					wxRect highlightRect(
						originX + displayCell.x * tileSize,
						originY + displayCell.y * tileSize,
						tileSize,
						tileSize
					);
					dc.SetPen(wxPen(wxColour(255, 215, 90), std::max(1, FromDIP(2))));
					dc.SetBrush(wxBrush(wxColour(255, 215, 90, 48)));
					dc.DrawRectangle(highlightRect);
					for (const wxPoint &duplicateCell : GetBorderPreviewDuplicateCells(selectedEdge_)) {
						dc.DrawRectangle(wxRect(
							originX + duplicateCell.x * tileSize,
							originY + duplicateCell.y * tileSize,
							tileSize,
							tileSize
						));
					}
				}
			}
		}

		std::map<wxString, int> slotItemIds_;
		int centerItemId_ = 0;
		wxString selectedEdge_;
	};

	class BorderSlotButton : public ItemToggleButton {
	public:
		explicit BorderSlotButton(wxWindow* parent) :
			ItemToggleButton(parent, RENDER_SIZE_32x32, 0) {
		}
	};

	bool AreBorderSetRecordsEqual(const BorderSetRecord &left, const BorderSetRecord &right) {
		return left.id == right.id && left.xmlBorderId == right.xmlBorderId && left.borderScope == right.borderScope && left.borderType == right.borderType && left.borderGroup == right.borderGroup && left.groundEquivalent == right.groundEquivalent && left.ownerBrushId == right.ownerBrushId && left.sourceFile == right.sourceFile;
	}

	bool AreBorderSetItemRecordsEqual(const BorderSetItemRecord &left, const BorderSetItemRecord &right) {
		return left.borderSetId == right.borderSetId && left.edge == right.edge && left.itemId == right.itemId && left.sortOrder == right.sortOrder;
	}

	bool AreBorderSetStorageRecordsEqual(const BorderSetStorageRecord &left, const BorderSetStorageRecord &right) {
		if (!AreBorderSetRecordsEqual(left.borderSet, right.borderSet) || left.items.size() != right.items.size()) {
			return false;
		}
		for (size_t i = 0; i < left.items.size(); ++i) {
			if (!AreBorderSetItemRecordsEqual(left.items[i], right.items[i])) {
				return false;
			}
		}
		return true;
	}
} // namespace

struct MaterialsWorkbenchBorderPanel::Impl {
	explicit Impl(MaterialsWorkbenchBorderPanel &self, MaterialsWorkbenchController &controller) :
		self_(self),
		controller_(controller) {
	}

	struct Callbacks {
		std::function<void(int64_t)> onBorderSetSaved;
		std::function<void(int64_t, const wxString &)> onBorderSetDeleted;
		std::function<void()> onBorderSetStateChanged;
		std::function<void(int64_t)> onOpenLinkedBrush;
	};

	struct State {
		bool hasBorderSet = false;
		bool dirty = false;
		bool internalUpdate = false;
		wxString currentContextKey;
		int currentItemIndex = -1;
		int selectedUsageIndex = wxNOT_FOUND;
		wxString selectedEdge;

		BorderSetStorageRecord borderSetStorage;
		BorderSetStorageRecord loadedBorderSetStorage;
		std::vector<BorderSetUsageRecord> borderSetUsages;
		std::vector<int> filteredUsageIndexes;
		std::map<wxString, int> slotItemIds;
		std::map<wxString, ItemToggleButton*> slotButtons;
		std::map<wxString, wxStaticText*> slotValueLabels;
		std::unordered_map<int64_t, wxString> borderSetSelectedEdges;
	};

	struct Ui {
		wxStaticText* titleLabel = nullptr;
		wxStaticText* subtitleLabel = nullptr;
		wxStaticText* summaryLabel = nullptr;
		wxStaticText* identityLabel = nullptr;

		wxTextCtrl* idCtrl = nullptr;
		wxStaticText* xmlBorderIdLabel = nullptr;
		wxSpinCtrl* xmlBorderIdCtrl = nullptr;
		wxStaticText* scopeLabel = nullptr;
		wxChoice* scopeChoice = nullptr;
		wxStaticText* typeLabel = nullptr;
		wxChoice* typeCtrl = nullptr;
		wxStaticText* borderGroupLabel = nullptr;
		wxChoice* borderGroupCtrl = nullptr;
		wxTextCtrl* sourceCtrl = nullptr;

		wxButton* createBorderButton = nullptr;
		wxButton* deleteBorderButton = nullptr;

		wxPanel* inlineDetailsPanel = nullptr;
		wxSpinCtrl* groundEquivalentCtrl = nullptr;
		wxTextCtrl* ownerBrushCtrl = nullptr;
		wxButton* openOwnerBrushButton = nullptr;

		wxPanel* globalDetailsPanel = nullptr;
		wxButton* openLinkedBrushButton = nullptr;
		wxTextCtrl* usageSearchCtrl = nullptr;
		wxStaticText* usageSearchHintLabel = nullptr;
		wxStaticText* usageSummaryLabel = nullptr;
		wxGrid* usageGrid = nullptr;
		ItemButton* usagePreviewItem = nullptr;
		wxStaticText* usageSelectionLabel = nullptr;
		wxButton* addUsageContextButton = nullptr;
		wxButton* editUsageContextButton = nullptr;
		wxButton* editUsageCasesButton = nullptr;
		wxButton* removeUsageContextButton = nullptr;

		ItemButton* centerGroundSlotPreview = nullptr;
		wxStaticText* centerGroundSlotValueLabel = nullptr;

		wxStaticText* selectedEdgeLabel = nullptr;
		wxSpinCtrl* selectedItemIdCtrl = nullptr;
		ItemButton* selectedItemPreview = nullptr;

		wxPanel* previewMatrixPanel = nullptr;

		wxButton* exportButton = nullptr;
		wxButton* importButton = nullptr;
		wxButton* saveButton = nullptr;
		wxButton* revertButton = nullptr;

		wxStaticText* statusLabel = nullptr;
	};

	int FromDIP(int value) const {
		return self_.FromDIP(value);
	}

	wxSize FromDIP(const wxSize &value) const {
		return self_.FromDIP(value);
	}

	void Layout() {
		self_.Layout();
	}

	wxSizer* GetSizer() {
		return self_.GetSizer();
	}

	void SetSizer(wxSizer* sizer) {
		self_.SetSizer(sizer);
	}

	void SetOnBorderSetSaved(std::function<void(int64_t)> callback);
	void SetOnBorderSetDeleted(std::function<void(int64_t, const wxString &)> callback);
	void SetOnBorderSetStateChanged(std::function<void()> callback);
	void SetOnOpenLinkedBrush(std::function<void(int64_t)> callback);
	bool HasPendingChanges() const;
	bool IsCurrentBorderSelection(const wxString &contextKey, int itemIndex) const;
	wxString GetCurrentBorderSetDisplayName() const;
	bool ResolvePendingChangesBeforeSwitch(wxWindow* parent, const wxString &targetLabel);

	void BuildLayout();
	void ClearWorkspace(const wxString &message);
	bool LoadBorderSet(const wxString &contextKey, int itemIndex);
	void PopulateFields();
	void UpdateSummaryLabels();
	BorderSetStorageRecord BuildComparableStorageFromCurrentState() const;
	void PopulateUsageContextList();
	void UpdateUsageContextControls();
	const BorderSetUsageRecord* GetSelectedUsageContext() const;
	bool ReloadBorderSetById(int64_t borderSetId);
	bool LoadBrushStorageById(int64_t brushId, BrushStorageRecord &outBrush, wxString &error) const;
	int FindMatchingGroundBorderIndex(const BrushStorageRecord &brushStorage, const BorderSetUsageRecord &usage) const;
	int SuggestNextBorderId() const;
	int ResolveCenterPreviewItemId() const;
	wxString ResolveCenterSourceLabel() const;
	wxString BuildOwnerBrushDisplayLabel(int64_t ownerBrushId) const;
	void HandleUsageContextChanged();
	void RefreshScopeSpecificLayout();
	void RefreshUsageDetails();
	bool ValidateBorderSetStorage(const BorderSetStorageRecord &storage, wxString &error) const;
	void SaveCurrentBorderEditorState();
	void RestoreCurrentBorderEditorState();
	void RefreshDirtyState();
	void NotifyBorderSetStateChanged();
	void UpdateWorkspaceHeader();
	void UpdateActionButtons();
	void RefreshSlotGrid();
	void RefreshPreviewGrid();
	void RefreshPreviewSelectionState();
	void SelectEdge(const wxString &edge);
	void UpdateSelectedEdgeEditor();
	void SyncSelectedSlotFromEditor(bool updateStatus);
	void SetStatusMessage(const wxString &message);
	void SetFieldsEnabled(bool enabled);
	bool SaveCurrentBorderSet();
	void OnApplyToSlot(wxCommandEvent &event);
	void OnClearSlot(wxCommandEvent &event);
	void OnPickItem(wxCommandEvent &event);
	void OnExportBorderSet(wxCommandEvent &event);
	void OnImportBorderSet(wxCommandEvent &event);
	void OnSave(wxCommandEvent &event);
	void OnRevert(wxCommandEvent &event);
	void OnSelectedItemIdChanged(wxCommandEvent &event);
	void OnSelectedItemIdSpin(wxSpinEvent &event);
	void OnMetadataFieldChanged(wxCommandEvent &event);
	void OnUsageContextChanged(wxGridEvent &event);
	void OnUsageSearchChanged(wxCommandEvent &event);
	void OnOpenLinkedBrush(wxCommandEvent &event);
	void OnOpenOwnerBrush(wxCommandEvent &event);
	void OnCreateBorder(wxCommandEvent &event);
	bool RemoveGlobalBorderContextsBeforeDelete(int64_t borderSetId, wxString &error);
	void OnDeleteBorder(wxCommandEvent &event);
	void OnAddUsageContext(wxCommandEvent &event);
	void OnEditUsageContext(wxCommandEvent &event);
	void OnEditUsageCases(wxCommandEvent &event);
	void OnRemoveUsageContext(wxCommandEvent &event);
	bool PersistEditedUsageContext(
		const BorderSetUsageRecord &usage,
		const GlobalUsageEditData &result,
		int existingIndex,
		BrushStorageRecord &sourceBrushStorage,
		GroundBrushBorderRecord &updatedRecord,
		wxString &error
	);
	void SelectUsageIndexAfterEdit(int64_t ownerBrushId, const GroundBrushBorderRecord &record);

	MaterialsWorkbenchBorderPanel &self_;
	MaterialsWorkbenchController &controller_;
	Callbacks callbacks_;
	State state_;
	Ui ui_;
};

MaterialsWorkbenchBorderPanel::MaterialsWorkbenchBorderPanel(wxWindow* parent, MaterialsWorkbenchController &controller) :
	wxPanel(parent, wxID_ANY),
	impl_(std::make_unique<Impl>(*this, controller)) {
	impl_->BuildLayout();
	impl_->ClearWorkspace("Select a border set in the navigation tree to edit its layout.");
}

MaterialsWorkbenchBorderPanel::~MaterialsWorkbenchBorderPanel() = default;

void MaterialsWorkbenchBorderPanel::ClearWorkspace(const wxString &message) {
	impl_->ClearWorkspace(message);
}

bool MaterialsWorkbenchBorderPanel::LoadBorderSet(const wxString &contextKey, int itemIndex) {
	return impl_->LoadBorderSet(contextKey, itemIndex);
}

void MaterialsWorkbenchBorderPanel::SetOnBorderSetSaved(std::function<void(int64_t)> callback) {
	impl_->SetOnBorderSetSaved(std::move(callback));
}

void MaterialsWorkbenchBorderPanel::SetOnBorderSetDeleted(std::function<void(int64_t, const wxString &)> callback) {
	impl_->SetOnBorderSetDeleted(std::move(callback));
}

void MaterialsWorkbenchBorderPanel::SetOnBorderSetStateChanged(std::function<void()> callback) {
	impl_->SetOnBorderSetStateChanged(std::move(callback));
}

void MaterialsWorkbenchBorderPanel::SetOnOpenLinkedBrush(std::function<void(int64_t)> callback) {
	impl_->SetOnOpenLinkedBrush(std::move(callback));
}

bool MaterialsWorkbenchBorderPanel::HasPendingChanges() const {
	return impl_->HasPendingChanges();
}

bool MaterialsWorkbenchBorderPanel::IsCurrentBorderSelection(const wxString &contextKey, int itemIndex) const {
	return impl_->IsCurrentBorderSelection(contextKey, itemIndex);
}

wxString MaterialsWorkbenchBorderPanel::GetCurrentBorderSetDisplayName() const {
	return impl_->GetCurrentBorderSetDisplayName();
}

bool MaterialsWorkbenchBorderPanel::ResolvePendingChangesBeforeSwitch(wxWindow* parent, const wxString &targetLabel) {
	return impl_->ResolvePendingChangesBeforeSwitch(parent, targetLabel);
}

void MaterialsWorkbenchBorderPanel::Impl::SetOnBorderSetSaved(std::function<void(int64_t)> callback) {
	callbacks_.onBorderSetSaved = std::move(callback);
}

void MaterialsWorkbenchBorderPanel::Impl::SetOnBorderSetDeleted(std::function<void(int64_t, const wxString &)> callback) {
	callbacks_.onBorderSetDeleted = std::move(callback);
}

void MaterialsWorkbenchBorderPanel::Impl::SetOnBorderSetStateChanged(std::function<void()> callback) {
	callbacks_.onBorderSetStateChanged = std::move(callback);
}

void MaterialsWorkbenchBorderPanel::Impl::SetOnOpenLinkedBrush(std::function<void(int64_t)> callback) {
	callbacks_.onOpenLinkedBrush = std::move(callback);
}

bool MaterialsWorkbenchBorderPanel::Impl::HasPendingChanges() const {
	return state_.hasBorderSet && state_.dirty;
}

bool MaterialsWorkbenchBorderPanel::Impl::IsCurrentBorderSelection(const wxString &contextKey, int itemIndex) const {
	return state_.hasBorderSet && state_.currentContextKey == contextKey && state_.currentItemIndex == itemIndex;
}

wxString MaterialsWorkbenchBorderPanel::Impl::GetCurrentBorderSetDisplayName() const {
	if (!state_.hasBorderSet) {
		return "";
	}

	return BuildBorderSetDisplayLabel(BuildComparableStorageFromCurrentState().borderSet);
}

bool MaterialsWorkbenchBorderPanel::Impl::ResolvePendingChangesBeforeSwitch(wxWindow* parent, const wxString &targetLabel) {
	if (!HasPendingChanges()) {
		return true;
	}

	const wxString currentLabel = BuildBorderSetDisplayLabel(state_.borderSetStorage.borderSet);
	const wxString destination = targetLabel.IsEmpty()
		? wxString::FromUTF8("the selected entry")
		: wxString::Format("\"%s\"", targetLabel);
	wxMessageDialog dialog(
		parent,
		"Border set \"" + currentLabel + "\" has unsaved changes.\n\n"
										 "You are switching to "
			+ destination + ".\n\n"
							"Yes: save and continue\n"
							"No: discard local changes and continue\n"
							"Cancel: stay on the current border set",
		"Unsaved Border Set Changes",
		wxYES_NO | wxCANCEL | wxICON_WARNING
	);
	dialog.SetYesNoCancelLabels("Save", "Discard", "Cancel");

	switch (dialog.ShowModal()) {
		case wxID_YES:
			return SaveCurrentBorderSet();
		case wxID_NO:
			return LoadBorderSet(state_.currentContextKey, state_.currentItemIndex);
		default:
			SetStatusMessage("Selection change canceled. Pending border edits were kept.");
			return false;
	}
}

void MaterialsWorkbenchBorderPanel::Impl::BuildLayout() {
	wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);

	wxStaticText* title = new wxStaticText(&self_, wxID_ANY, "Border Workspace");
	wxFont titleFont = title->GetFont();
	titleFont.SetPointSize(titleFont.GetPointSize() + 4);
	titleFont.SetWeight(wxFONTWEIGHT_BOLD);
	title->SetFont(titleFont);

	ui_.titleLabel = new wxStaticText(&self_, wxID_ANY, "No border set selected");
	ui_.subtitleLabel = new wxStaticText(&self_, wxID_ANY, "Edit border slots visually, assign item IDs and preview the resulting composition.");
	StyleBorderWorkspaceSubtitle(ui_.subtitleLabel);

	wxScrolledWindow* scrolled = new wxScrolledWindow(&self_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	scrolled->SetScrollRate(FromDIP(10), FromDIP(10));

	wxBoxSizer* contentSizer = new wxBoxSizer(wxVERTICAL);
	ui_.summaryLabel = new wxStaticText(scrolled, wxID_ANY, "");
	ui_.identityLabel = new wxStaticText(scrolled, wxID_ANY, "");
	StyleBorderWorkspaceCaption(ui_.identityLabel);

	wxStaticBoxSizer* metadataBox = new wxStaticBoxSizer(wxVERTICAL, scrolled, "Border Set");
	wxWindow* metadataParent = metadataBox->GetStaticBox();
	wxFlexGridSizer* metadataGrid = new wxFlexGridSizer(0, 2, FromDIP(4), FromDIP(6));
	metadataGrid->AddGrowableCol(1, 1);

	ui_.idCtrl = new wxTextCtrl(metadataParent, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
	ui_.xmlBorderIdCtrl = new wxSpinCtrl(metadataParent, wxID_ANY);
	ui_.xmlBorderIdCtrl->SetRange(0, 1000000);
	ui_.scopeChoice = new wxChoice(metadataParent, wxID_ANY);
	ui_.scopeChoice->Append("global");
	ui_.scopeChoice->Append("inline");
	ui_.typeCtrl = new wxChoice(metadataParent, wxID_ANY);
	ui_.typeCtrl->Append("normal");
	ui_.typeCtrl->Append("optional");
	ui_.borderGroupCtrl = new wxChoice(metadataParent, wxID_ANY);
	RebuildBorderGroupChoices(ui_.borderGroupCtrl, 0);
	ui_.sourceCtrl = new wxTextCtrl(metadataParent, wxID_ANY);

	const wxSize metadataFieldSize(FromDIP(132), -1);
	ui_.idCtrl->SetMinSize(metadataFieldSize);
	ui_.xmlBorderIdCtrl->SetMinSize(metadataFieldSize);
	ui_.scopeChoice->SetMinSize(metadataFieldSize);
	ui_.typeCtrl->SetMinSize(metadataFieldSize);
	ui_.borderGroupCtrl->SetMinSize(metadataFieldSize);
	ui_.sourceCtrl->Hide();

	const auto addMetadataField = [&](const wxString &label, wxWindow* control) {
		wxStaticText* fieldLabel = new wxStaticText(metadataParent, wxID_ANY, label);
		StyleBorderWorkspaceCaption(fieldLabel);
		metadataGrid->Add(fieldLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
		metadataGrid->Add(control, 1, wxEXPAND);
		if (control == ui_.xmlBorderIdCtrl) {
			ui_.xmlBorderIdLabel = fieldLabel;
		} else if (control == ui_.scopeChoice) {
			ui_.scopeLabel = fieldLabel;
		} else if (control == ui_.typeCtrl) {
			ui_.typeLabel = fieldLabel;
		} else if (control == ui_.borderGroupCtrl) {
			ui_.borderGroupLabel = fieldLabel;
		}
	};

	addMetadataField("Internal ID", ui_.idCtrl);
	addMetadataField("Global Border ID", ui_.xmlBorderIdCtrl);
	addMetadataField("Scope", ui_.scopeChoice);
	addMetadataField("Border Style", ui_.typeCtrl);
	addMetadataField("Autoborder Group", ui_.borderGroupCtrl);
	ui_.typeCtrl->SetToolTip("Choose whether this border behaves as normal or optional.");
	ui_.borderGroupCtrl->SetToolTip("Used by autoborder matching rules. `None` disables group matching for this border.");
	if (ui_.borderGroupLabel) {
		ui_.borderGroupLabel->SetToolTip("Used by autoborder matching rules. `None` disables group matching for this border.");
	}
	if (ui_.scopeChoice) {
		ui_.scopeChoice->Hide();
		ui_.scopeChoice->Enable(false);
	}
	if (ui_.scopeLabel) {
		ui_.scopeLabel->Hide();
	}
	metadataBox->Add(metadataGrid, 1, wxEXPAND | wxALL, FromDIP(6));
	wxBoxSizer* borderCrudRow = new wxBoxSizer(wxHORIZONTAL);
	ui_.createBorderButton = new wxButton(metadataParent, wxID_ANY, "New Border Set");
	ui_.deleteBorderButton = new wxButton(metadataParent, wxID_ANY, "Delete Border Set");
	StyleBorderWorkspaceActionButton(ui_.createBorderButton, "Create a new border set in the current scope.");
	StyleBorderWorkspaceActionButton(ui_.deleteBorderButton, "Delete this border set from materials.db.");
	borderCrudRow->Add(ui_.createBorderButton, 1, wxRIGHT, FromDIP(4));
	borderCrudRow->Add(ui_.deleteBorderButton, 1);
	metadataBox->Add(borderCrudRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

	ui_.inlineDetailsPanel = new wxPanel(scrolled, wxID_ANY);
	wxStaticBoxSizer* inlineDetailsBox = new wxStaticBoxSizer(wxVERTICAL, ui_.inlineDetailsPanel, "Inline Details");
	wxWindow* inlineDetailsParent = inlineDetailsBox->GetStaticBox();
	ui_.groundEquivalentCtrl = new wxSpinCtrl(inlineDetailsParent, wxID_ANY);
	ui_.groundEquivalentCtrl->SetRange(0, std::max(100000, static_cast<int>(g_items.getMaxID())));
	ui_.groundEquivalentCtrl->SetMinSize(metadataFieldSize);
	ui_.ownerBrushCtrl = new wxTextCtrl(inlineDetailsParent, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
	ui_.ownerBrushCtrl->SetMinSize(wxSize(FromDIP(210), -1));
	wxFlexGridSizer* inlineGrid = new wxFlexGridSizer(0, 2, FromDIP(4), FromDIP(6));
	inlineGrid->AddGrowableCol(1, 1);
	const auto addInlineField = [&](const wxString &label, wxWindow* control) {
		wxStaticText* fieldLabel = new wxStaticText(inlineDetailsParent, wxID_ANY, label);
		StyleBorderWorkspaceCaption(fieldLabel);
		inlineGrid->Add(fieldLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
		inlineGrid->Add(control, 1, wxEXPAND);
	};
	addInlineField("Center Tile", ui_.groundEquivalentCtrl);
	addInlineField("Owner Brush", ui_.ownerBrushCtrl);
	inlineDetailsBox->Add(inlineGrid, 1, wxEXPAND | wxALL, FromDIP(6));
	ui_.openOwnerBrushButton = new wxButton(inlineDetailsParent, wxID_ANY, "Open Owner Brush");
	StyleBorderWorkspaceActionButton(ui_.openOwnerBrushButton, "Open the owner brush for this inline border set.");
	inlineDetailsBox->Add(ui_.openOwnerBrushButton, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
	ui_.inlineDetailsPanel->SetSizer(inlineDetailsBox);

	ui_.globalDetailsPanel = new wxPanel(scrolled, wxID_ANY);
	wxStaticBoxSizer* globalDetailsBox = new wxStaticBoxSizer(wxVERTICAL, ui_.globalDetailsPanel, "Used By");
	wxWindow* globalDetailsParent = globalDetailsBox->GetStaticBox();
	ui_.openLinkedBrushButton = new wxButton(globalDetailsParent, wxID_ANY, "Open Brush");
	StyleBorderWorkspaceActionButton(ui_.openLinkedBrushButton, "Open the selected brush that uses this global border.");
	ui_.usageSearchCtrl = new wxTextCtrl(globalDetailsParent, wxID_ANY);
	ui_.usageSearchCtrl->SetHint("Search by brush, ID, align, role, center, target...");
	ui_.usageSearchHintLabel = new wxStaticText(globalDetailsParent, wxID_ANY, "Matches brush name, IDs, align, role, center, target, and painted contexts.");
	StyleBorderWorkspaceCaption(ui_.usageSearchHintLabel);
	ui_.usageSummaryLabel = new wxStaticText(globalDetailsParent, wxID_ANY, "No usage contexts loaded");
	StyleBorderWorkspaceCaption(ui_.usageSummaryLabel);
	ui_.usageGrid = new wxGrid(globalDetailsParent, wxID_ANY);
	ui_.usageGrid->CreateGrid(0, kUsageGridColumnCount);
	ui_.usageGrid->EnableEditing(false);
	ui_.usageGrid->EnableDragGridSize(false);
	ui_.usageGrid->EnableDragRowSize(false);
	ui_.usageGrid->EnableDragColMove(false);
	ui_.usageGrid->EnableDragColSize(true);
	ui_.usageGrid->EnableGridLines(true);
	ui_.usageGrid->SetSelectionMode(wxGrid::wxGridSelectRows);
	ui_.usageGrid->SetMargins(0, 0);
	ui_.usageGrid->SetRowLabelSize(0);
	ui_.usageGrid->SetDefaultCellAlignment(wxALIGN_LEFT, wxALIGN_CENTER);
	ui_.usageGrid->SetDefaultCellOverflow(false);
	ui_.usageGrid->SetColLabelAlignment(wxALIGN_LEFT, wxALIGN_CENTER);
	ui_.usageGrid->SetGridLineColour(wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT));
	ui_.usageGrid->SetColLabelValue(kUsageGridColumnBrush, "Brush");
	ui_.usageGrid->SetColLabelValue(kUsageGridColumnBrushId, "ID");
	ui_.usageGrid->SetColLabelValue(kUsageGridColumnType, "Type");
	ui_.usageGrid->SetColLabelValue(kUsageGridColumnAlign, "Align");
	ui_.usageGrid->SetColLabelValue(kUsageGridColumnRole, "Role");
	ui_.usageGrid->SetColLabelValue(kUsageGridColumnTarget, "Target");
	ui_.usageGrid->SetColLabelValue(kUsageGridColumnCenter, "Center");
	ui_.usageGrid->SetColSize(kUsageGridColumnBrush, FromDIP(150));
	ui_.usageGrid->SetColSize(kUsageGridColumnBrushId, FromDIP(62));
	ui_.usageGrid->SetColSize(kUsageGridColumnType, FromDIP(72));
	ui_.usageGrid->SetColSize(kUsageGridColumnAlign, FromDIP(68));
	ui_.usageGrid->SetColSize(kUsageGridColumnRole, FromDIP(78));
	ui_.usageGrid->SetColSize(kUsageGridColumnTarget, FromDIP(120));
	ui_.usageGrid->SetColSize(kUsageGridColumnCenter, FromDIP(74));
	ui_.usageGrid->SetDefaultRowSize(FromDIP(26), true);
	ui_.usageGrid->SetMinSize(wxSize(FromDIP(240), FromDIP(178)));
	ui_.usagePreviewItem = std::make_unique<ItemButton>(globalDetailsParent, RENDER_SIZE_32x32, 0).release();
	ui_.usagePreviewItem->Enable(false);
	ui_.usageSelectionLabel = new wxStaticText(globalDetailsParent, wxID_ANY, "Select a context row to drive the global center preview.");
	ui_.usageSelectionLabel->Wrap(FromDIP(280));
	ui_.addUsageContextButton = new wxButton(globalDetailsParent, wxID_ANY, "Add Context");
	ui_.editUsageContextButton = new wxButton(globalDetailsParent, wxID_ANY, "Edit Context");
	ui_.removeUsageContextButton = new wxButton(globalDetailsParent, wxID_ANY, "Remove Context");
	ui_.editUsageCasesButton = new wxButton(globalDetailsParent, wxID_ANY, "Specific Cases");
	StyleBorderWorkspaceActionButton(ui_.addUsageContextButton, "Add a new brush usage context for this global border.");
	StyleBorderWorkspaceActionButton(ui_.editUsageContextButton, "Edit the selected usage context.");
	StyleBorderWorkspaceActionButton(ui_.removeUsageContextButton, "Remove the selected usage context from its owner brush.");
	StyleBorderWorkspaceActionButton(ui_.editUsageCasesButton, "Edit legacy <specific> cases for the selected context.");
	globalDetailsBox->Add(CreateBorderSectionLabel(globalDetailsParent, "Search"), 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(6));
	globalDetailsBox->Add(ui_.usageSearchCtrl, 0, wxEXPAND | wxALL, FromDIP(6));
	globalDetailsBox->Add(ui_.usageSearchHintLabel, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(6));
	globalDetailsBox->Add(ui_.usageSummaryLabel, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(6));
	globalDetailsBox->Add(CreateBorderSectionLabel(globalDetailsParent, "Contexts"), 0, wxLEFT | wxRIGHT, FromDIP(6));
	globalDetailsBox->Add(ui_.usageGrid, 1, wxEXPAND | wxALL, FromDIP(6));
	globalDetailsBox->Add(CreateBorderSectionLabel(globalDetailsParent, "Selection"), 0, wxLEFT | wxRIGHT, FromDIP(6));
	wxBoxSizer* usageDetailsRow = new wxBoxSizer(wxHORIZONTAL);
	usageDetailsRow->Add(ui_.usagePreviewItem, 0, wxALIGN_TOP | wxRIGHT, FromDIP(6));
	usageDetailsRow->Add(ui_.usageSelectionLabel, 1, wxEXPAND);
	globalDetailsBox->Add(usageDetailsRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
	wxWrapSizer* usageActionGrid = new wxWrapSizer(wxHORIZONTAL, 0);
	usageActionGrid->Add(ui_.addUsageContextButton, 0, wxRIGHT | wxBOTTOM, FromDIP(4));
	usageActionGrid->Add(ui_.editUsageContextButton, 0, wxRIGHT | wxBOTTOM, FromDIP(4));
	usageActionGrid->Add(ui_.editUsageCasesButton, 0, wxRIGHT | wxBOTTOM, FromDIP(4));
	usageActionGrid->Add(ui_.removeUsageContextButton, 0, wxRIGHT | wxBOTTOM, FromDIP(4));
	usageActionGrid->Add(ui_.openLinkedBrushButton, 0, wxBOTTOM, FromDIP(4));
	globalDetailsBox->Add(usageActionGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
	ui_.globalDetailsPanel->SetSizer(globalDetailsBox);

	wxBoxSizer* workspaceSizer = new wxBoxSizer(wxHORIZONTAL);

	wxStaticBoxSizer* gridBox = new wxStaticBoxSizer(wxVERTICAL, scrolled, "Slot Grid");
	wxWindow* gridParent = gridBox->GetStaticBox();
	wxFlexGridSizer* slotGridSizer = new wxFlexGridSizer(kBorderGridSize, kBorderGridSize, FromDIP(kBorderGridGapDip), FromDIP(kBorderGridGapDip));
	const int gridColumnWidthDip = (kBorderGridSize * kBorderGridCellWidthDip) + ((kBorderGridSize - 1) * kBorderGridGapDip) + 32;

	for (int row = 0; row < kBorderGridSize; ++row) {
		for (int col = 0; col < kBorderGridSize; ++col) {
			const BorderEdgeSpec* specForCell = FindEdgeSpecForCell(row, col);

			if (IsBorderGridCenterCell(row, col)) {
				wxPanel* centerPanel = new wxPanel(gridParent, wxID_ANY);
				centerPanel->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
				wxBoxSizer* centerSizer = new wxBoxSizer(wxVERTICAL);
				ui_.centerGroundSlotPreview = std::make_unique<ItemButton>(centerPanel, RENDER_SIZE_32x32, 0).release();
				ui_.centerGroundSlotPreview->Enable(false);
				ui_.centerGroundSlotValueLabel = new wxStaticText(centerPanel, wxID_ANY, "not set");
				StyleBorderWorkspaceStrongValue(ui_.centerGroundSlotValueLabel);
				centerPanel->SetToolTip("Center Ground");
				ui_.centerGroundSlotPreview->SetToolTip("Center Ground");

				centerSizer->AddStretchSpacer(1);
				centerSizer->Add(ui_.centerGroundSlotPreview, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(2));
				centerSizer->Add(ui_.centerGroundSlotValueLabel, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(2));
				centerSizer->AddStretchSpacer(1);
				centerPanel->SetSizer(centerSizer);
				centerPanel->SetMinSize(wxSize(FromDIP(kBorderGridCellWidthDip), FromDIP(kBorderGridCellHeightDip)));
				slotGridSizer->Add(centerPanel, 0, wxEXPAND);
				continue;
			}

			if (!specForCell) {
				slotGridSizer->Add(CreateSpacerCell(gridParent, kBorderGridCellWidthDip, kBorderGridCellHeightDip), 0, wxEXPAND);
				continue;
			}

			wxPanel* cell = new wxPanel(gridParent, wxID_ANY);
			cell->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
			wxBoxSizer* cellSizer = new wxBoxSizer(wxVERTICAL);
			wxString edge = wxString::FromUTF8(specForCell->edge);
			auto* button = std::make_unique<BorderSlotButton>(cell).release();
			auto* value = std::make_unique<wxStaticText>(cell, wxID_ANY, "empty").release();
			StyleBorderWorkspaceStrongValue(value);
			cell->SetToolTip(specForCell->label);
			button->SetToolTip(specForCell->label);
			value->SetToolTip(specForCell->label);

			button->Bind(wxEVT_LEFT_DOWN, [this, edge](wxMouseEvent &event) {
				SelectEdge(edge);
				event.Skip();
			});

			cellSizer->AddStretchSpacer(1);
			cellSizer->Add(button, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(2));
			cellSizer->Add(value, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(2));
			cellSizer->AddStretchSpacer(1);
			cell->SetSizer(cellSizer);
			cell->SetMinSize(wxSize(FromDIP(kBorderGridCellWidthDip), FromDIP(kBorderGridCellHeightDip)));

			state_.slotButtons[edge] = button;
			state_.slotValueLabels[edge] = value;
			slotGridSizer->Add(cell, 0, wxEXPAND);
		}
	}

	wxBoxSizer* gridCenterRow = new wxBoxSizer(wxHORIZONTAL);
	gridCenterRow->AddStretchSpacer(1);
	gridCenterRow->Add(slotGridSizer, 0, wxALL, FromDIP(6));
	gridCenterRow->AddStretchSpacer(1);
	gridBox->Add(gridCenterRow, 0, wxEXPAND);

	wxStaticBoxSizer* editorBox = new wxStaticBoxSizer(wxVERTICAL, scrolled, "Selected Slot");
	wxWindow* editorParent = editorBox->GetStaticBox();
	ui_.selectedEdgeLabel = new wxStaticText(editorParent, wxID_ANY, "Edge: none");
	ui_.selectedItemIdCtrl = new wxSpinCtrl(editorParent, wxID_ANY);
	ui_.selectedItemIdCtrl->SetRange(0, std::max(100000, static_cast<int>(g_items.getMaxID())));
	ui_.selectedItemPreview = std::make_unique<ItemButton>(editorParent, RENDER_SIZE_32x32, 0).release();
	ui_.selectedItemIdCtrl->SetMinSize(wxSize(FromDIP(110), -1));

	wxBoxSizer* selectionRow = new wxBoxSizer(wxHORIZONTAL);
	selectionRow->Add(ui_.selectedItemPreview, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
	selectionRow->Add(ui_.selectedItemIdCtrl, 1, wxALIGN_CENTER_VERTICAL);

	wxGridSizer* selectionActions = new wxGridSizer(2, FromDIP(4), FromDIP(4));
	wxButton* pickItemButton = new wxButton(editorParent, wxID_ANY, "Pick Item");
	wxButton* applyButton = new wxButton(editorParent, wxID_ANY, "Apply To Slot");
	wxButton* clearButton = new wxButton(editorParent, wxID_ANY, "Clear Slot");
	StyleBorderWorkspaceActionButton(pickItemButton, "Choose an item id from the picker for the selected border slot.");
	StyleBorderWorkspaceActionButton(applyButton, "Apply the selected item id to the active border slot.");
	StyleBorderWorkspaceActionButton(clearButton, "Clear the selected slot back to item id 0 in the local editor.");
	selectionActions->Add(pickItemButton, 0, wxEXPAND);
	selectionActions->Add(applyButton, 0, wxEXPAND);
	selectionActions->Add(clearButton, 0, wxEXPAND);
	selectionActions->AddSpacer(0);

	editorBox->Add(ui_.selectedEdgeLabel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));
	editorBox->Add(selectionRow, 0, wxEXPAND | wxALL, FromDIP(6));
	editorBox->Add(selectionActions, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

	wxStaticBoxSizer* previewBox = new wxStaticBoxSizer(wxVERTICAL, scrolled, "Preview Matrix");
	wxWindow* previewParent = previewBox->GetStaticBox();
	ui_.previewMatrixPanel = std::make_unique<BorderPreviewMatrixPanel>(previewParent).release();
	wxBoxSizer* previewCenterRow = new wxBoxSizer(wxHORIZONTAL);
	previewCenterRow->AddStretchSpacer(1);
	previewCenterRow->Add(ui_.previewMatrixPanel, 0, wxALL, FromDIP(6));
	previewCenterRow->AddStretchSpacer(1);

	previewBox->Add(previewCenterRow, 1, wxEXPAND);

	wxBoxSizer* metadataColumn = new wxBoxSizer(wxVERTICAL);
	metadataColumn->Add(metadataBox, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
	metadataColumn->Add(ui_.inlineDetailsPanel, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
	metadataColumn->Add(ui_.globalDetailsPanel, 0, wxEXPAND);
	metadataColumn->SetMinSize(wxSize(FromDIP(250), -1));

	wxBoxSizer* gridColumn = new wxBoxSizer(wxVERTICAL);
	gridBox->SetMinSize(wxSize(FromDIP(gridColumnWidthDip), -1));
	gridColumn->Add(gridBox, 0, wxEXPAND);

	wxBoxSizer* previewColumn = new wxBoxSizer(wxVERTICAL);
	previewBox->SetMinSize(wxSize(FromDIP(220), -1));
	previewColumn->Add(previewBox, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
	previewColumn->Add(editorBox, 0, wxEXPAND);

	workspaceSizer->Add(metadataColumn, 1, wxRIGHT | wxEXPAND, FromDIP(8));
	workspaceSizer->Add(gridColumn, 0, wxRIGHT | wxEXPAND, FromDIP(8));
	workspaceSizer->Add(previewColumn, 0, wxEXPAND);

	contentSizer->Add(ui_.summaryLabel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));
	contentSizer->Add(ui_.identityLabel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(2));
	contentSizer->Add(new wxStaticLine(scrolled), 0, wxEXPAND | wxALL, FromDIP(6));
	contentSizer->Add(workspaceSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

	scrolled->SetSizer(contentSizer);

	wxBoxSizer* headerSizer = new wxBoxSizer(wxVERTICAL);
	headerSizer->Add(title, 0, wxBOTTOM, FromDIP(4));
	headerSizer->Add(ui_.titleLabel, 0, wxBOTTOM, FromDIP(2));
	headerSizer->Add(ui_.subtitleLabel, 0);

	wxBoxSizer* actionSizer = new wxBoxSizer(wxHORIZONTAL);
	ui_.exportButton = new wxButton(&self_, wxID_ANY, "Export...");
	ui_.importButton = new wxButton(&self_, wxID_ANY, "Import...");
	ui_.saveButton = new wxButton(&self_, wxID_SAVE, "Save Border Set");
	ui_.revertButton = new wxButton(&self_, wxID_ANY, "Revert");
	StyleBorderWorkspaceActionButton(ui_.exportButton, "Export this border set to a JSON file for sharing or backup.");
	StyleBorderWorkspaceActionButton(ui_.importButton, "Import a border set from a JSON file. This can create or update a border by Global Border ID.");
	StyleBorderWorkspaceActionButton(ui_.saveButton, "Write the current border set metadata and slots to materials.db.");
	StyleBorderWorkspaceActionButton(ui_.revertButton, "Discard local border edits and reload the current border set from materials.db.");
	actionSizer->Add(ui_.exportButton, 0, wxRIGHT, FromDIP(2));
	actionSizer->Add(ui_.importButton, 0, wxRIGHT, FromDIP(10));
	actionSizer->Add(ui_.saveButton, 0, wxRIGHT, FromDIP(2));
	actionSizer->Add(ui_.revertButton, 0);

	ui_.statusLabel = new wxStaticText(&self_, wxID_ANY, "");
	StyleBorderWorkspaceStatusLabel(ui_.statusLabel);
	wxBoxSizer* footerSizer = new wxBoxSizer(wxHORIZONTAL);
	footerSizer->Add(ui_.statusLabel, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
	footerSizer->Add(actionSizer, 0, wxALIGN_CENTER_VERTICAL);

	rootSizer->Add(headerSizer, 0, wxEXPAND | wxALL, FromDIP(6));
	rootSizer->Add(new wxStaticLine(&self_), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(6));
	rootSizer->Add(scrolled, 1, wxEXPAND | wxALL, FromDIP(6));
	rootSizer->Add(footerSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));
	SetSizer(rootSizer);

	pickItemButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &event) { OnPickItem(event); });
	applyButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &event) { OnApplyToSlot(event); });
	clearButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &event) { OnClearSlot(event); });
	ui_.saveButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &event) { OnSave(event); });
	ui_.revertButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &event) { OnRevert(event); });
	ui_.exportButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &event) { OnExportBorderSet(event); });
	ui_.importButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &event) { OnImportBorderSet(event); });
	ui_.createBorderButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &event) { OnCreateBorder(event); });
	ui_.deleteBorderButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &event) { OnDeleteBorder(event); });
	ui_.selectedItemIdCtrl->Bind(wxEVT_TEXT, [this](wxCommandEvent &event) { OnSelectedItemIdChanged(event); });
	ui_.selectedItemIdCtrl->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent &event) { OnSelectedItemIdSpin(event); });
	ui_.xmlBorderIdCtrl->Bind(wxEVT_TEXT, [this](wxCommandEvent &event) { OnMetadataFieldChanged(event); });
	ui_.xmlBorderIdCtrl->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent &event) { OnMetadataFieldChanged(event); });
	ui_.scopeChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent &event) { OnMetadataFieldChanged(event); });
	ui_.typeCtrl->Bind(wxEVT_CHOICE, [this](wxCommandEvent &event) { OnMetadataFieldChanged(event); });
	ui_.borderGroupCtrl->Bind(wxEVT_CHOICE, [this](wxCommandEvent &event) { OnMetadataFieldChanged(event); });
	ui_.groundEquivalentCtrl->Bind(wxEVT_TEXT, [this](wxCommandEvent &event) { OnMetadataFieldChanged(event); });
	ui_.usageSearchCtrl->Bind(wxEVT_TEXT, [this](wxCommandEvent &event) { OnUsageSearchChanged(event); });
	ui_.usageGrid->Bind(wxEVT_GRID_SELECT_CELL, [this](wxGridEvent &event) { OnUsageContextChanged(event); });
	ui_.openLinkedBrushButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &event) { OnOpenLinkedBrush(event); });
	ui_.openOwnerBrushButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &event) { OnOpenOwnerBrush(event); });
	ui_.addUsageContextButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &event) { OnAddUsageContext(event); });
	ui_.editUsageContextButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &event) { OnEditUsageContext(event); });
	ui_.editUsageCasesButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &event) { OnEditUsageCases(event); });
	ui_.removeUsageContextButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &event) { OnRemoveUsageContext(event); });
	ui_.groundEquivalentCtrl->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent &event) { OnMetadataFieldChanged(event); });
}

void MaterialsWorkbenchBorderPanel::Impl::ClearWorkspace(const wxString &message) {
	state_.borderSetStorage = BorderSetStorageRecord();
	state_.loadedBorderSetStorage = BorderSetStorageRecord();
	state_.borderSetUsages.clear();
	state_.currentContextKey.clear();
	state_.currentItemIndex = -1;
	state_.selectedUsageIndex = wxNOT_FOUND;
	state_.hasBorderSet = false;
	state_.dirty = false;
	state_.selectedEdge.clear();
	state_.slotItemIds.clear();

	UpdateWorkspaceHeader();
	ui_.summaryLabel->SetLabel(message);
	if (ui_.identityLabel) {
		ui_.identityLabel->SetLabel("");
	}

	state_.internalUpdate = true;
	ui_.idCtrl->SetValue("");
	ui_.xmlBorderIdCtrl->SetValue(0);
	ui_.scopeChoice->SetSelection(wxNOT_FOUND);
	ui_.typeCtrl->SetSelection(wxNOT_FOUND);
	RebuildBorderGroupChoices(ui_.borderGroupCtrl, 0);
	ui_.groundEquivalentCtrl->SetValue(0);
	ui_.ownerBrushCtrl->SetValue("");
	ui_.sourceCtrl->SetValue("");
	if (ui_.usageSearchCtrl) {
		ui_.usageSearchCtrl->SetValue("");
	}
	if (ui_.usageGrid && ui_.usageGrid->GetNumberRows() > 0) {
		ui_.usageGrid->DeleteRows(0, ui_.usageGrid->GetNumberRows());
	}
	if (ui_.usageSummaryLabel) {
		ui_.usageSummaryLabel->SetLabel("No usage contexts loaded");
	}
	ui_.selectedEdgeLabel->SetLabel("Edge: none");
	ui_.selectedItemIdCtrl->SetValue(0);
	ui_.selectedItemPreview->SetSprite(0);
	state_.internalUpdate = false;

	if (ui_.centerGroundSlotPreview) {
		ui_.centerGroundSlotPreview->SetSprite(0);
	}
	if (ui_.centerGroundSlotValueLabel) {
		ui_.centerGroundSlotValueLabel->SetLabel("not set");
	}

	for (const auto &entry : state_.slotButtons) {
		entry.second->SetSprite(0);
		entry.second->SetValue(false);
	}
	for (const auto &entry : state_.slotValueLabels) {
		entry.second->SetLabel("empty");
	}
	if (ui_.previewMatrixPanel) {
		static_cast<BorderPreviewMatrixPanel*>(ui_.previewMatrixPanel)->SetPreviewState({}, 0, "");
	}
	RefreshPreviewSelectionState();
	RefreshScopeSpecificLayout();
	RefreshUsageDetails();

	SetFieldsEnabled(false);
	UpdateActionButtons();
	NotifyBorderSetStateChanged();
	SetStatusMessage(message);
	Layout();
}

bool MaterialsWorkbenchBorderPanel::Impl::LoadBorderSet(const wxString &contextKey, int itemIndex) {
	SaveCurrentBorderEditorState();

	wxString error;
	BorderSetStorageRecord storage;
	if (!controller_.GetBorderSetDetails(contextKey, itemIndex, storage, error)) {
		ClearWorkspace("Failed to load border set details: " + error);
		return false;
	}

	std::vector<BorderSetUsageRecord> usages;
	if (!controller_.GetBorderSetUsages(storage.borderSet.id, usages, error)) {
		ClearWorkspace("Failed to load border set usage contexts: " + error);
		return false;
	}

	state_.borderSetStorage = storage;
	state_.loadedBorderSetStorage = storage;
	state_.borderSetUsages = std::move(usages);
	state_.currentContextKey = contextKey;
	state_.currentItemIndex = itemIndex;
	state_.hasBorderSet = true;
	state_.dirty = false;
	state_.selectedUsageIndex = state_.borderSetUsages.empty() ? wxNOT_FOUND : 0;

	PopulateFields();
	SetFieldsEnabled(true);
	UpdateActionButtons();
	NotifyBorderSetStateChanged();
	SetStatusMessage("Ready. Editing border data from materials.db. Update slots or metadata, then Save or Revert.");
	Layout();
	return true;
}

void MaterialsWorkbenchBorderPanel::Impl::PopulateFields() {
	const BorderSetRecord &borderSet = state_.borderSetStorage.borderSet;

	state_.internalUpdate = true;
	ui_.idCtrl->SetValue(wxString::Format("%lld", static_cast<long long>(borderSet.id)));
	ui_.xmlBorderIdCtrl->SetValue(borderSet.xmlBorderId);
	ui_.scopeChoice->SetStringSelection(borderSet.borderScope);
	ui_.typeCtrl->SetStringSelection(borderSet.borderType.IsEmpty() ? wxString::FromUTF8("normal") : borderSet.borderType);
	if (ui_.typeCtrl->GetSelection() == wxNOT_FOUND) {
		ui_.typeCtrl->SetStringSelection("normal");
	}
	RebuildBorderGroupChoices(ui_.borderGroupCtrl, borderSet.borderGroup);
	ui_.groundEquivalentCtrl->SetValue(borderSet.groundEquivalent);
	ui_.ownerBrushCtrl->SetValue(BuildOwnerBrushDisplayLabel(borderSet.ownerBrushId));
	ui_.sourceCtrl->SetValue(borderSet.sourceFile);
	if (ui_.usageSearchCtrl) {
		ui_.usageSearchCtrl->ChangeValue("");
	}
	PopulateUsageContextList();
	state_.internalUpdate = false;
	UpdateWorkspaceHeader();
	UpdateUsageContextControls();
	RefreshScopeSpecificLayout();

	state_.slotItemIds.clear();
	for (const BorderSetItemRecord &item : state_.borderSetStorage.items) {
		state_.slotItemIds[item.edge] = item.itemId;
	}

	RefreshSlotGrid();
	RefreshPreviewGrid();
	RestoreCurrentBorderEditorState();
}

void MaterialsWorkbenchBorderPanel::Impl::UpdateSummaryLabels() {
	if (!state_.hasBorderSet) {
		return;
	}

	const BorderSetStorageRecord currentStorage = BuildComparableStorageFromCurrentState();
	const BorderSetRecord &borderSet = currentStorage.borderSet;

	ui_.summaryLabel->SetLabel(wxString::Format("Border: %s | Filled Slots: %zu | Scope: %s | Style: %s", BuildBorderSetDisplayLabel(borderSet), currentStorage.items.size(), borderSet.borderScope, borderSet.borderType));

	const wxString centerGroundText = ResolveCenterSourceLabel();
	if (borderSet.borderScope == "global") {
		ui_.identityLabel->SetLabel(wxString::Format("Global Border ID %d | Internal ID %lld | Used By: %zu brushes | Center source: %s", borderSet.xmlBorderId, static_cast<long long>(borderSet.id), state_.borderSetUsages.size(), centerGroundText));
	} else {
		ui_.identityLabel->SetLabel(wxString::Format("Inline border | Internal ID %lld | Owner Brush: %s | Center Tile: %s", static_cast<long long>(borderSet.id), BuildOwnerBrushDisplayLabel(borderSet.ownerBrushId), centerGroundText));
	}
}

BorderSetStorageRecord MaterialsWorkbenchBorderPanel::Impl::BuildComparableStorageFromCurrentState() const {
	BorderSetStorageRecord storage = state_.borderSetStorage;
	storage.borderSet.xmlBorderId = ui_.xmlBorderIdCtrl->GetValue();
	storage.borderSet.borderScope = ui_.scopeChoice->GetSelection() == wxNOT_FOUND ? wxString() : ui_.scopeChoice->GetStringSelection();
	storage.borderSet.borderType = ui_.typeCtrl->GetSelection() == wxNOT_FOUND ? wxString() : ui_.typeCtrl->GetStringSelection();
	storage.borderSet.borderGroup = ui_.borderGroupCtrl && ui_.borderGroupCtrl->GetSelection() != wxNOT_FOUND
		? ParseBorderGroupChoiceValue(ui_.borderGroupCtrl->GetStringSelection())
		: 0;
	storage.borderSet.groundEquivalent = ui_.groundEquivalentCtrl->GetValue();
	storage.borderSet.sourceFile = ui_.sourceCtrl->GetValue().Trim(true).Trim(false);

	if (storage.borderSet.borderType.IsEmpty()) {
		storage.borderSet.borderType = "normal";
	}

	storage.items.clear();
	int sortOrder = 0;
	for (const BorderEdgeSpec &spec : kBorderEdgeSpecs) {
		const wxString edge = wxString::FromUTF8(spec.edge);
		const int itemId = state_.slotItemIds.count(edge) > 0 ? state_.slotItemIds.at(edge) : 0;
		if (itemId <= 0) {
			continue;
		}

		BorderSetItemRecord item;
		item.borderSetId = storage.borderSet.id;
		item.edge = edge;
		item.itemId = itemId;
		item.sortOrder = sortOrder++;
		storage.items.push_back(item);
	}

	return storage;
}

void MaterialsWorkbenchBorderPanel::Impl::PopulateUsageContextList() {
	if (!ui_.usageGrid) {
		return;
	}

	state_.filteredUsageIndexes.clear();
	if (ui_.usageGrid->GetNumberRows() > 0) {
		ui_.usageGrid->DeleteRows(0, ui_.usageGrid->GetNumberRows());
	}
	const wxString query = ui_.usageSearchCtrl ? ui_.usageSearchCtrl->GetValue().Lower() : wxString();
	for (size_t i = 0; i < state_.borderSetUsages.size(); ++i) {
		const BorderSetUsageRecord &usage = state_.borderSetUsages[i];
		const wxString haystack = BuildUsageSearchHaystack(usage);
		if (!query.IsEmpty() && !haystack.Contains(query)) {
			continue;
		}
		state_.filteredUsageIndexes.push_back(static_cast<int>(i));
	}

	if (!state_.filteredUsageIndexes.empty()) {
		ui_.usageGrid->AppendRows(static_cast<int>(state_.filteredUsageIndexes.size()));
		for (size_t row = 0; row < state_.filteredUsageIndexes.size(); ++row) {
			const BorderSetUsageRecord &usage = state_.borderSetUsages[state_.filteredUsageIndexes[row]];
			ui_.usageGrid->SetCellValue(static_cast<int>(row), kUsageGridColumnBrush, usage.brushName);
			ui_.usageGrid->SetCellValue(static_cast<int>(row), kUsageGridColumnBrushId, wxString::Format("%lld", static_cast<long long>(usage.brushId)));
			ui_.usageGrid->SetCellValue(static_cast<int>(row), kUsageGridColumnType, BuildUsageTypeLabel(usage));
			ui_.usageGrid->SetCellValue(static_cast<int>(row), kUsageGridColumnAlign, BuildUsageAlignLabel(usage));
			ui_.usageGrid->SetCellValue(static_cast<int>(row), kUsageGridColumnRole, BuildUsageRoleLabel(usage));
			ui_.usageGrid->SetCellValue(static_cast<int>(row), kUsageGridColumnTarget, BuildUsageTargetLabel(usage));
			ui_.usageGrid->SetCellValue(static_cast<int>(row), kUsageGridColumnCenter, BuildUsageCenterLabel(usage));
			ApplyUsageGridRowStyle(ui_.usageGrid, static_cast<int>(row));
		}
	}

	if (ui_.usageSummaryLabel) {
		if (query.IsEmpty()) {
			ui_.usageSummaryLabel->SetLabel(wxString::Format("%zu context%s available", state_.borderSetUsages.size(), state_.borderSetUsages.size() == 1 ? "" : "s"));
		} else {
			ui_.usageSummaryLabel->SetLabel(wxString::Format("%zu match%s for \"%s\" (%zu total)", state_.filteredUsageIndexes.size(), state_.filteredUsageIndexes.size() == 1 ? "" : "es", query, state_.borderSetUsages.size()));
		}
	}

	if (state_.filteredUsageIndexes.empty()) {
		state_.selectedUsageIndex = wxNOT_FOUND;
		ui_.usageGrid->ClearSelection();
		RefreshUsageDetails();
		return;
	}

	if (state_.selectedUsageIndex == wxNOT_FOUND || std::ranges::find(state_.filteredUsageIndexes, state_.selectedUsageIndex) == state_.filteredUsageIndexes.end()) {
		state_.selectedUsageIndex = state_.filteredUsageIndexes.front();
	}
	for (size_t visibleIndex = 0; visibleIndex < state_.filteredUsageIndexes.size(); ++visibleIndex) {
		if (state_.filteredUsageIndexes[visibleIndex] == state_.selectedUsageIndex) {
			state_.internalUpdate = true;
			ui_.usageGrid->ClearSelection();
			ui_.usageGrid->SetGridCursor(static_cast<int>(visibleIndex), 0);
			ui_.usageGrid->SelectRow(static_cast<int>(visibleIndex), false);
			ui_.usageGrid->MakeCellVisible(static_cast<int>(visibleIndex), 0);
			state_.internalUpdate = false;
			break;
		}
	}
	RefreshUsageDetails();
}

void MaterialsWorkbenchBorderPanel::Impl::UpdateUsageContextControls() {
	const auto setEnabled = [](wxWindow* window, bool enabled) {
		if (window) {
			window->Enable(enabled);
		}
	};

	if (!state_.hasBorderSet) {
		setEnabled(ui_.usageSearchCtrl, false);
		setEnabled(ui_.usageGrid, false);
		setEnabled(ui_.openLinkedBrushButton, false);
		setEnabled(ui_.openOwnerBrushButton, false);
		setEnabled(ui_.addUsageContextButton, false);
		setEnabled(ui_.editUsageContextButton, false);
		setEnabled(ui_.editUsageCasesButton, false);
		setEnabled(ui_.removeUsageContextButton, false);
		return;
	}

	const wxString scope = ui_.scopeChoice->GetStringSelection();
	const bool isGlobal = scope == "global";
	const bool isInline = scope == "inline";
	const BorderSetUsageRecord* usage = GetSelectedUsageContext();
	const bool hasUsage = usage != nullptr;

	setEnabled(ui_.usageSearchCtrl, isGlobal);
	setEnabled(ui_.usageGrid, isGlobal && !state_.filteredUsageIndexes.empty());
	setEnabled(ui_.openLinkedBrushButton, isGlobal && usage && usage->brushId > 0);
	setEnabled(ui_.openOwnerBrushButton, isInline && state_.borderSetStorage.borderSet.ownerBrushId > 0);
	setEnabled(ui_.addUsageContextButton, isGlobal);
	setEnabled(ui_.editUsageContextButton, isGlobal && hasUsage);
	setEnabled(ui_.editUsageCasesButton, isGlobal && hasUsage);
	setEnabled(ui_.removeUsageContextButton, isGlobal && hasUsage);
	if (ui_.usageGrid) {
		wxString tooltip;
		if (!isGlobal) {
			tooltip = wxString::FromUTF8("Inline borders edit their Center Tile directly.");
		} else if (usage) {
			tooltip = wxString::Format("Selected context from brush \"%s\" drives the center preview.", usage->brushName);
		} else {
			tooltip = wxString::FromUTF8("This global border has no visible usage context for the current search.");
		}
		ui_.usageGrid->SetToolTip(tooltip);
	}
	RefreshUsageDetails();
}

const BorderSetUsageRecord* MaterialsWorkbenchBorderPanel::Impl::GetSelectedUsageContext() const {
	if (state_.selectedUsageIndex == wxNOT_FOUND || state_.selectedUsageIndex < 0 || state_.selectedUsageIndex >= static_cast<int>(state_.borderSetUsages.size())) {
		return nullptr;
	}
	return &state_.borderSetUsages[state_.selectedUsageIndex];
}

bool MaterialsWorkbenchBorderPanel::Impl::ReloadBorderSetById(int64_t borderSetId) {
	wxString contextKey;
	int itemIndex = -1;
	if (!controller_.LocateBorderSetNode(borderSetId, contextKey, itemIndex)) {
		ClearWorkspace("The selected border set is no longer available in materials.db.");
		return false;
	}
	return LoadBorderSet(contextKey, itemIndex);
}

bool MaterialsWorkbenchBorderPanel::Impl::LoadBrushStorageById(int64_t brushId, BrushStorageRecord &outBrush, wxString &error) const {
	wxString contextKey;
	int itemIndex = -1;
	if (!controller_.LocateBrushNode(brushId, contextKey, itemIndex)) {
		error = wxString::Format("Brush #%lld is not available in the Workbench catalog.", static_cast<long long>(brushId));
		return false;
	}
	return controller_.GetBrushDetails(contextKey, itemIndex, outBrush, error);
}

int MaterialsWorkbenchBorderPanel::Impl::FindMatchingGroundBorderIndex(const BrushStorageRecord &brushStorage, const BorderSetUsageRecord &usage) const {
	for (size_t i = 0; i < brushStorage.borders.size(); ++i) {
		const GroundBrushBorderRecord &border = brushStorage.borders[i];
		if (border.borderSetId != state_.borderSetStorage.borderSet.id) {
			continue;
		}
		if (border.sortOrder != usage.sortOrder) {
			continue;
		}
		if (border.borderRole != usage.borderRole || border.align != usage.align || border.targetMode != usage.targetMode) {
			continue;
		}
		if (border.targetBrushId != usage.targetBrushId || border.targetBrushName != usage.targetBrushName) {
			continue;
		}
		return static_cast<int>(i);
	}
	return -1;
}

int MaterialsWorkbenchBorderPanel::Impl::SuggestNextBorderId() const {
	return controller_.SuggestNextBorderId();
}

int MaterialsWorkbenchBorderPanel::Impl::ResolveCenterPreviewItemId() const {
	if (!state_.hasBorderSet) {
		return 0;
	}

	const BorderSetStorageRecord currentStorage = BuildComparableStorageFromCurrentState();
	const BorderSetRecord &borderSet = currentStorage.borderSet;
	if (borderSet.groundEquivalent > 0) {
		return borderSet.groundEquivalent;
	}

	if (borderSet.borderScope == "global") {
		if (const BorderSetUsageRecord* usage = GetSelectedUsageContext()) {
			return ResolveUsagePreviewItemId(*usage);
		}
	}

	return 0;
}

wxString MaterialsWorkbenchBorderPanel::Impl::ResolveCenterSourceLabel() const {
	if (!state_.hasBorderSet) {
		return "not set";
	}

	const BorderSetStorageRecord currentStorage = BuildComparableStorageFromCurrentState();
	const BorderSetRecord &borderSet = currentStorage.borderSet;
	if (borderSet.groundEquivalent > 0) {
		return wxString::Format("Center Tile %s", FormatOptionalBorderItemText(borderSet.groundEquivalent));
	}

	if (borderSet.borderScope == "global") {
		if (const BorderSetUsageRecord* usage = GetSelectedUsageContext()) {
			const int previewItemId = ResolveUsagePreviewItemId(*usage);
			return previewItemId > 0
				? wxString::Format("linked brush %s uses %s", usage->brushName, FormatOptionalBorderItemText(previewItemId))
				: wxString::Format("linked brush %s has no preview item", usage->brushName);
		}
		return "linked brush context not selected";
	}

	return "painted base ground";
}

wxString MaterialsWorkbenchBorderPanel::Impl::BuildOwnerBrushDisplayLabel(int64_t ownerBrushId) const {
	if (ownerBrushId <= 0) {
		return "Not linked";
	}

	wxString contextKey;
	int itemIndex = -1;
	if (controller_.LocateBrushNode(ownerBrushId, contextKey, itemIndex)) {
		BrushStorageRecord brushStorage;
		wxString error;
		if (controller_.GetBrushDetails(contextKey, itemIndex, brushStorage, error)) {
			return wxString::Format("%s (#%lld)", brushStorage.brush.name, static_cast<long long>(ownerBrushId));
		}
	}

	return wxString::Format("#%lld", static_cast<long long>(ownerBrushId));
}

void MaterialsWorkbenchBorderPanel::Impl::HandleUsageContextChanged() {
	UpdateUsageContextControls();
	RefreshSlotGrid();
	RefreshPreviewGrid();
}

void MaterialsWorkbenchBorderPanel::Impl::RefreshScopeSpecificLayout() {
	const bool isInline = state_.hasBorderSet && ui_.scopeChoice && ui_.scopeChoice->GetStringSelection() == "inline";
	const bool isGlobal = state_.hasBorderSet && ui_.scopeChoice && ui_.scopeChoice->GetStringSelection() == "global";
	if (ui_.scopeLabel) {
		ui_.scopeLabel->Hide();
	}
	if (ui_.scopeChoice) {
		ui_.scopeChoice->Hide();
		ui_.scopeChoice->Enable(false);
	}
	if (ui_.xmlBorderIdLabel) {
		ui_.xmlBorderIdLabel->SetLabel("Global Border ID");
		ui_.xmlBorderIdLabel->Show(isGlobal);
	}
	if (ui_.xmlBorderIdCtrl) {
		ui_.xmlBorderIdCtrl->Show(isGlobal);
	}
	if (ui_.inlineDetailsPanel) {
		ui_.inlineDetailsPanel->Show(isInline);
	}
	if (ui_.globalDetailsPanel) {
		ui_.globalDetailsPanel->Show(isGlobal);
	}
	Layout();
	if (GetSizer()) {
		GetSizer()->Layout();
	}
}

void MaterialsWorkbenchBorderPanel::Impl::RefreshUsageDetails() {
	const BorderSetUsageRecord* usage = GetSelectedUsageContext();
	const int previewItemId = usage ? ResolveUsagePreviewItemId(*usage) : 0;
	if (ui_.usagePreviewItem) {
		ui_.usagePreviewItem->SetSprite(previewItemId);
		ui_.usagePreviewItem->SetToolTip(
			previewItemId > 0
				? wxString::Format("Effective center preview item %d.", previewItemId)
				: wxString::FromUTF8("This context resolves to a painted center.")
		);
	}
	if (ui_.usageSelectionLabel) {
		ui_.usageSelectionLabel->SetLabel(usage ? BuildUsageSelectionSummary(*usage) : wxString::FromUTF8("Select a context row to drive the global center preview."));
		ui_.usageSelectionLabel->Wrap(ui_.globalDetailsPanel->FromDIP(280));
	}
}

namespace {
	bool ValidateBorderSetScope(const BorderSetRecord &borderSet, wxString &error) {
		const wxString scope = borderSet.borderScope.Lower();
		if (scope.IsEmpty()) {
			error = "Border scope must be selected.";
			return false;
		}
		if (scope != "global" && scope != "inline") {
			error = wxString::Format("Border scope \"%s\" is not supported.", borderSet.borderScope);
			return false;
		}
		if (scope == "global" && borderSet.xmlBorderId <= 0) {
			error = "Global border sets must use a Global Border ID greater than zero so runtime refresh can target the saved set.";
			return false;
		}
		if (scope == "global" && borderSet.ownerBrushId > 0) {
			error = "Global border sets must not use an owner brush. Use Inline scope instead.";
			return false;
		}
		if (scope == "inline" && borderSet.ownerBrushId <= 0) {
			error = "Inline border sets must stay linked to an owner brush before saving.";
			return false;
		}
		if (scope == "inline" && borderSet.xmlBorderId > 0) {
			error = "Inline border sets must not use a Global Border ID.";
			return false;
		}
		if (scope == "inline" && borderSet.groundEquivalent <= 0) {
			error = "Inline border sets must use a Center Tile greater than zero.";
			return false;
		}
		return true;
	}

	bool ValidateBorderSetGroundEquivalent(const BorderSetRecord &borderSet, wxString &error) {
		if (borderSet.groundEquivalent < 0) {
			error = "Ground equivalent cannot be negative.";
			return false;
		}
		if (borderSet.groundEquivalent > 0 && !IsKnownBorderPanelItemId(borderSet.groundEquivalent)) {
			error = wxString::Format("Ground equivalent uses unknown item id %d.", borderSet.groundEquivalent);
			return false;
		}
		return true;
	}

	bool ValidateBorderSetItems(const std::vector<BorderSetItemRecord> &items, wxString &error) {
		std::map<int, wxString> itemEdgeById;
		for (const BorderSetItemRecord &item : items) {
			if (!FindEdgeSpec(item.edge)) {
				error = wxString::Format("Border slot edge \"%s\" is not supported.", item.edge);
				return false;
			}
			if (item.itemId <= 0) {
				error = wxString::Format("Border slot \"%s\" must use a positive item id.", item.edge);
				return false;
			}
			if (!IsKnownBorderPanelItemId(item.itemId)) {
				error = wxString::Format("Border slot \"%s\" uses unknown item id %d.", item.edge, item.itemId);
				return false;
			}

			const auto duplicateIt = itemEdgeById.find(item.itemId);
			if (duplicateIt != itemEdgeById.end() && duplicateIt->second != item.edge) {
				error = wxString::Format(
					"Item id %d is used by both border slots \"%s\" and \"%s\". Each slot must use its own item id so runtime border alignment stays unambiguous.",
					item.itemId,
					duplicateIt->second,
					item.edge
				);
				return false;
			}

			itemEdgeById[item.itemId] = item.edge;
		}
		return true;
	}
} // namespace

bool MaterialsWorkbenchBorderPanel::Impl::ValidateBorderSetStorage(const BorderSetStorageRecord &storage, wxString &error) const {
	if (!ValidateBorderSetScope(storage.borderSet, error)) {
		return false;
	}
	if (!ValidateBorderSetGroundEquivalent(storage.borderSet, error)) {
		return false;
	}
	if (!ValidateBorderSetItems(storage.items, error)) {
		return false;
	}
	error.clear();
	return true;
}

void MaterialsWorkbenchBorderPanel::Impl::SaveCurrentBorderEditorState() {
	if (!state_.hasBorderSet || state_.borderSetStorage.borderSet.id <= 0) {
		return;
	}

	if (state_.selectedEdge.IsEmpty()) {
		state_.borderSetSelectedEdges.erase(state_.borderSetStorage.borderSet.id);
		return;
	}

	state_.borderSetSelectedEdges[state_.borderSetStorage.borderSet.id] = state_.selectedEdge;
}

void MaterialsWorkbenchBorderPanel::Impl::RestoreCurrentBorderEditorState() {
	wxString edgeToSelect;
	if (state_.borderSetStorage.borderSet.id > 0) {
		auto it = state_.borderSetSelectedEdges.find(state_.borderSetStorage.borderSet.id);
		if (it != state_.borderSetSelectedEdges.end()) {
			edgeToSelect = it->second;
		}
	}

	if (edgeToSelect.IsEmpty() && state_.slotButtons.count(state_.selectedEdge)) {
		edgeToSelect = state_.selectedEdge;
	}
	if (edgeToSelect.IsEmpty() || !state_.slotButtons.count(edgeToSelect)) {
		edgeToSelect = "n";
	}

	SelectEdge(edgeToSelect);
}

void MaterialsWorkbenchBorderPanel::Impl::RefreshDirtyState() {
	if (!state_.hasBorderSet) {
		state_.dirty = false;
		UpdateWorkspaceHeader();
		UpdateActionButtons();
		NotifyBorderSetStateChanged();
		return;
	}

	state_.dirty = !AreBorderSetStorageRecordsEqual(BuildComparableStorageFromCurrentState(), state_.loadedBorderSetStorage);
	UpdateWorkspaceHeader();
	UpdateActionButtons();
	NotifyBorderSetStateChanged();
}

void MaterialsWorkbenchBorderPanel::Impl::NotifyBorderSetStateChanged() {
	if (callbacks_.onBorderSetStateChanged) {
		callbacks_.onBorderSetStateChanged();
	}
}

void MaterialsWorkbenchBorderPanel::Impl::UpdateWorkspaceHeader() {
	if (!state_.hasBorderSet) {
		ui_.titleLabel->SetLabel("No border set selected");
		ui_.subtitleLabel->SetLabel("Select a border set in the navigation tree to edit authoring data, slot ownership, and sprite layout.");
		return;
	}

	const wxString modifiedSuffix = state_.dirty ? " [modified]" : "";
	ui_.titleLabel->SetLabel("Editing " + BuildBorderSetDisplayLabel(BuildComparableStorageFromCurrentState().borderSet) + modifiedSuffix);
	const wxString scope = ui_.scopeChoice->GetStringSelection();
	if (state_.dirty) {
		ui_.subtitleLabel->SetLabel("Unsaved local border set edits differ from materials.db. Save to persist them or Revert to discard them before switching sets.");
	} else if (scope == "global") {
		ui_.subtitleLabel->SetLabel("Global border sprites are shared. The selected Used By context drives the effective center preview.");
	} else {
		ui_.subtitleLabel->SetLabel("Inline borders own their Center Tile directly. Adjust authoring fields, slot sprites, and save when ready.");
	}
}

void MaterialsWorkbenchBorderPanel::Impl::UpdateActionButtons() {
	if (ui_.saveButton) {
		ui_.saveButton->Enable(state_.hasBorderSet && state_.dirty);
	}
	if (ui_.revertButton) {
		ui_.revertButton->Enable(state_.hasBorderSet && state_.dirty);
	}
	if (ui_.exportButton) {
		ui_.exportButton->Enable(state_.hasBorderSet);
	}
	if (ui_.importButton) {
		ui_.importButton->Enable(true);
	}
	if (ui_.createBorderButton) {
		ui_.createBorderButton->Enable(true);
	}
	if (ui_.deleteBorderButton) {
		ui_.deleteBorderButton->Enable(state_.hasBorderSet);
	}
}

void MaterialsWorkbenchBorderPanel::Impl::RefreshSlotGrid() {
	const int centerPreviewItemId = ResolveCenterPreviewItemId();
	const wxString centerSourceLabel = ResolveCenterSourceLabel();
	if (ui_.centerGroundSlotPreview) {
		ui_.centerGroundSlotPreview->SetSprite(centerPreviewItemId);
		ui_.centerGroundSlotPreview->SetToolTip(
			centerPreviewItemId > 0
				? wxString::Format("Center preview resolves to %s.", centerSourceLabel)
				: wxString::Format("Center preview source: %s.", centerSourceLabel)
		);
	}
	if (ui_.centerGroundSlotValueLabel) {
		ui_.centerGroundSlotValueLabel->SetLabel(FormatCompactCenterGroundText(centerPreviewItemId));
		ui_.centerGroundSlotValueLabel->SetToolTip(centerSourceLabel);
	}

	for (const auto &entry : state_.slotButtons) {
		const wxString &edge = entry.first;
		const wxString slotLabel = GetBorderEdgeDisplayLabel(edge);
		const int itemId = state_.slotItemIds.count(edge) > 0 ? state_.slotItemIds[edge] : 0;
		entry.second->SetSprite(itemId);
		entry.second->SetValue(edge == state_.selectedEdge);
		state_.slotValueLabels[edge]->SetLabel(FormatCompactItemIdText(itemId, "empty"));
		entry.second->SetToolTip(
			itemId > 0
				? wxString::Format("%s slot uses item %d.", slotLabel, itemId)
				: wxString::Format("%s slot is empty.", slotLabel)
		);
	}

	UpdateSummaryLabels();
}

void MaterialsWorkbenchBorderPanel::Impl::RefreshPreviewGrid() {
	const int centerPreviewItemId = ResolveCenterPreviewItemId();
	const wxString centerSourceLabel = ResolveCenterSourceLabel();
	if (ui_.previewMatrixPanel) {
		wxString previewTooltip = centerPreviewItemId > 0
			? wxString::Format("Preview composes the runtime scene from the 5x5 matrix using %s as base ground.", centerSourceLabel)
			: wxString::Format("Preview composes the runtime scene from the 5x5 matrix. Center source: %s.", centerSourceLabel);
		if (state_.selectedEdge.IsEmpty()) {
			previewTooltip += " No slot selected.";
		} else {
			const int selectedItemId = state_.slotItemIds.count(state_.selectedEdge) > 0 ? state_.slotItemIds[state_.selectedEdge] : 0;
			previewTooltip += wxString::Format(" Selected slot: %s (%s).", GetBorderEdgeDisplayLabel(state_.selectedEdge), FormatCompactItemIdText(selectedItemId, "empty"));
		}
		ui_.previewMatrixPanel->SetToolTip(previewTooltip);
		static_cast<BorderPreviewMatrixPanel*>(ui_.previewMatrixPanel)->SetPreviewState(state_.slotItemIds, centerPreviewItemId, state_.selectedEdge);
	}

	RefreshPreviewSelectionState();
}

void MaterialsWorkbenchBorderPanel::Impl::RefreshPreviewSelectionState() {
	if (ui_.previewMatrixPanel) {
		ui_.previewMatrixPanel->Refresh();
	}
}

void MaterialsWorkbenchBorderPanel::Impl::SelectEdge(const wxString &edge) {
	if (!state_.slotButtons.count(edge)) {
		return;
	}

	state_.selectedEdge = edge;
	SaveCurrentBorderEditorState();
	RefreshSlotGrid();
	RefreshPreviewGrid();
	UpdateSelectedEdgeEditor();
}

void MaterialsWorkbenchBorderPanel::Impl::UpdateSelectedEdgeEditor() {
	const BorderEdgeSpec* spec = FindEdgeSpec(state_.selectedEdge);
	const int itemId = state_.slotItemIds.count(state_.selectedEdge) > 0 ? state_.slotItemIds[state_.selectedEdge] : 0;
	if (spec) {
		ui_.selectedEdgeLabel->SetLabel("Edge: " + wxString::FromUTF8(spec->label));
	} else {
		ui_.selectedEdgeLabel->SetLabel("Edge: " + state_.selectedEdge);
	}
	state_.internalUpdate = true;
	ui_.selectedItemIdCtrl->SetValue(itemId);
	ui_.selectedItemPreview->SetSprite(itemId);
	state_.internalUpdate = false;
}

void MaterialsWorkbenchBorderPanel::Impl::SyncSelectedSlotFromEditor(bool updateStatus) {
	if (!state_.hasBorderSet || state_.selectedEdge.IsEmpty()) {
		return;
	}

	const int itemId = ui_.selectedItemIdCtrl->GetValue();
	const std::map<wxString, int> previousSlotItemIds = state_.slotItemIds;
	if (itemId > 0) {
		state_.slotItemIds[state_.selectedEdge] = itemId;
	} else {
		state_.slotItemIds.erase(state_.selectedEdge);
	}

	if (updateStatus) {
		const BorderSetStorageRecord candidateStorage = BuildComparableStorageFromCurrentState();
		wxString validationError;
		if (!ValidateBorderSetStorage(candidateStorage, validationError)) {
			state_.slotItemIds = previousSlotItemIds;
			RefreshSlotGrid();
			RefreshPreviewGrid();
			SetStatusMessage("Cannot apply slot: " + validationError);
			return;
		}
	}

	ui_.selectedItemPreview->SetSprite(itemId);
	RefreshSlotGrid();
	RefreshPreviewGrid();
	RefreshDirtyState();
	if (updateStatus) {
		SetStatusMessage("Slot updated locally. Save the border set to persist.");
	}
}

void MaterialsWorkbenchBorderPanel::Impl::SetStatusMessage(const wxString &message) {
	ui_.statusLabel->SetLabel(message);
	ui_.statusLabel->Wrap(FromDIP(760));
}

void MaterialsWorkbenchBorderPanel::Impl::SetFieldsEnabled(bool enabled) {
	ui_.xmlBorderIdCtrl->Enable(enabled);
	ui_.scopeChoice->Enable(false);
	ui_.typeCtrl->Enable(enabled);
	ui_.borderGroupCtrl->Enable(enabled);
	ui_.groundEquivalentCtrl->Enable(enabled);
	ui_.selectedItemIdCtrl->Enable(enabled);
	ui_.selectedItemPreview->Enable(enabled);
	if (ui_.ownerBrushCtrl) {
		ui_.ownerBrushCtrl->Enable(false);
	}
	for (const auto &entry : state_.slotButtons) {
		entry.second->Enable(enabled);
	}
	UpdateUsageContextControls();
	RefreshScopeSpecificLayout();
}

bool MaterialsWorkbenchBorderPanel::Impl::SaveCurrentBorderSet() {
	if (!state_.hasBorderSet) {
		SetStatusMessage("Select a border set before saving.");
		return false;
	}

	BorderSetStorageRecord comparableStorage = BuildComparableStorageFromCurrentState();
	wxString validationError;
	if (!ValidateBorderSetStorage(comparableStorage, validationError)) {
		SetStatusMessage("Cannot save border set: " + validationError);
		return false;
	}

	state_.borderSetStorage = comparableStorage;

	wxString error;
	if (!controller_.SaveBorderSet(state_.borderSetStorage, error)) {
		SetStatusMessage("Failed to save border set: " + error);
		return false;
	}

	state_.loadedBorderSetStorage = state_.borderSetStorage;
	state_.dirty = false;
	PopulateFields();
	UpdateActionButtons();
	NotifyBorderSetStateChanged();
	SetStatusMessage("Saved border metadata and slots to materials.db. Targeted runtime sync remained in place.");
	if (callbacks_.onBorderSetSaved) {
		callbacks_.onBorderSetSaved(state_.borderSetStorage.borderSet.id);
	}
	return true;
}

void MaterialsWorkbenchBorderPanel::Impl::OnApplyToSlot(wxCommandEvent &event) {
	if (!state_.hasBorderSet || state_.selectedEdge.IsEmpty()) {
		SetStatusMessage("Select a border slot before applying an item.");
		return;
	}

	SyncSelectedSlotFromEditor(true);
}

void MaterialsWorkbenchBorderPanel::Impl::OnClearSlot(wxCommandEvent &event) {
	if (!state_.hasBorderSet || state_.selectedEdge.IsEmpty()) {
		SetStatusMessage("Select a border slot before clearing it.");
		return;
	}

	state_.slotItemIds.erase(state_.selectedEdge);
	ui_.selectedItemIdCtrl->SetValue(0);
	ui_.selectedItemPreview->SetSprite(0);
	RefreshSlotGrid();
	RefreshPreviewGrid();
	RefreshDirtyState();
	SetStatusMessage("Slot cleared locally. Save the border set to persist.");
}

void MaterialsWorkbenchBorderPanel::Impl::OnPickItem(wxCommandEvent &event) {
	if (!state_.hasBorderSet) {
		SetStatusMessage("Load a border set before choosing an item.");
		return;
	}

	FindItemDialog dialog(&self_, "Select Border Item");
	dialog.setSearchMode(FindItemDialog::ItemIDs);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	ui_.selectedItemIdCtrl->SetValue(dialog.getResultID());
	ui_.selectedItemPreview->SetSprite(dialog.getResultID());
}

void MaterialsWorkbenchBorderPanel::Impl::OnExportBorderSet(wxCommandEvent &) {
	if (!state_.hasBorderSet) {
		SetStatusMessage("Load a border set before exporting.");
		return;
	}

	const BorderSetStorageRecord storage = BuildComparableStorageFromCurrentState();
	if (!storage.borderSet.borderScope.IsSameAs("global", false) || storage.borderSet.xmlBorderId <= 0) {
		wxMessageBox("Export currently supports global border sets only.", "Export Border Set", wxOK | wxICON_WARNING, &self_);
		return;
	}

	const wxString defaultName = wxString::Format("border-%d.rme-materials.json", storage.borderSet.xmlBorderId);
	wxFileDialog dialog(
		&self_,
		"Export Border Set",
		"",
		defaultName,
		"RME Materials JSON (*.rme-materials.json)|*.rme-materials.json|JSON (*.json)|*.json|All files (*.*)|*.*",
		wxFD_SAVE | wxFD_OVERWRITE_PROMPT
	);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	const wxString path = dialog.GetPath();
	const nlohmann::json root = ExportBorderSetToJson(storage);

	wxCharBuffer utf8 = path.ToUTF8();
	std::ofstream out(utf8.data(), std::ios::binary | std::ios::trunc);
	if (!out.is_open()) {
		wxMessageBox("Failed to write the export file.", "Export Border Set", wxOK | wxICON_ERROR, &self_);
		return;
	}

	out << root.dump(2);
	out.close();

	SetStatusMessage(wxString::Format("Exported Border %d to JSON.", storage.borderSet.xmlBorderId));
}

void MaterialsWorkbenchBorderPanel::Impl::OnImportBorderSet(wxCommandEvent &) {
	wxFileDialog dialog(
		&self_,
		"Import Border Set",
		"",
		"",
		"RME Materials JSON (*.rme-materials.json)|*.rme-materials.json|JSON (*.json)|*.json|All files (*.*)|*.*",
		wxFD_OPEN | wxFD_FILE_MUST_EXIST
	);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	const wxString path = dialog.GetPath();
	wxCharBuffer utf8 = path.ToUTF8();
	std::ifstream in(utf8.data(), std::ios::binary);
	if (!in.is_open()) {
		wxMessageBox("Failed to open the import file.", "Import Border Set", wxOK | wxICON_ERROR, &self_);
		return;
	}

	nlohmann::json root;
	try {
		in >> root;
	} catch (const nlohmann::json::parse_error &) {
		wxMessageBox("Invalid JSON file.", "Import Border Set", wxOK | wxICON_ERROR, &self_);
		return;
	}

	BorderSetRecord borderSet;
	std::vector<BorderSetItemRecord> items;
	wxString parseError;
	if (!TryParseBorderSetExportJson(root, borderSet, items, parseError)) {
		wxMessageBox(parseError, "Import Border Set", wxOK | wxICON_ERROR, &self_);
		return;
	}

	BorderSetRecord existing;
	const bool hasExisting = g_brush_database.findBorderSetByXmlBorderId(borderSet.xmlBorderId, existing) && existing.id > 0;
	if (hasExisting) {
		const int result = wxMessageBox(
			wxString::Format("Border %d already exists. Import will overwrite its slot items.\n\nContinue?", borderSet.xmlBorderId),
			"Import Border Set",
			wxYES_NO | wxICON_WARNING,
			&self_
		);
		if (result != wxYES) {
			return;
		}
	}

	if (state_.hasBorderSet && state_.dirty) {
		if (!ResolvePendingChangesBeforeSwitch(&self_, wxString::Format("Border %d", borderSet.xmlBorderId))) {
			return;
		}
	}

	int64_t borderSetId = 0;
	if (!g_brush_database.upsertBorderSet(borderSet, borderSetId) || borderSetId <= 0) {
		wxMessageBox(g_brush_database.getLastError(), "Import Border Set", wxOK | wxICON_ERROR, &self_);
		return;
	}

	for (BorderSetItemRecord &item : items) {
		item.borderSetId = borderSetId;
	}
	std::sort(items.begin(), items.end(), [](const BorderSetItemRecord &a, const BorderSetItemRecord &b) {
		if (a.sortOrder != b.sortOrder) {
			return a.sortOrder < b.sortOrder;
		}
		return a.edge < b.edge;
	});
	for (size_t i = 0; i < items.size(); ++i) {
		items[i].sortOrder = static_cast<int>(i);
	}

	if (!g_brush_database.replaceBorderSetItems(borderSetId, items)) {
		wxMessageBox(g_brush_database.getLastError(), "Import Border Set", wxOK | wxICON_ERROR, &self_);
		return;
	}

	controller_.ReloadCatalog();
	if (callbacks_.onBorderSetSaved) {
		callbacks_.onBorderSetSaved(borderSetId);
	}
	ReloadBorderSetById(borderSetId);
	SetStatusMessage(wxString::Format("Imported Border %d from JSON.", borderSet.xmlBorderId));
}

void MaterialsWorkbenchBorderPanel::Impl::OnSave(wxCommandEvent &event) {
	SaveCurrentBorderSet();
}

void MaterialsWorkbenchBorderPanel::Impl::OnRevert(wxCommandEvent &event) {
	if (!state_.hasBorderSet) {
		ClearWorkspace("Select a border set in the navigation tree to edit its layout.");
		return;
	}

	if (!LoadBorderSet(state_.currentContextKey, state_.currentItemIndex)) {
		return;
	}

	SetStatusMessage("Reverted local border edits and reloaded the border set from materials.db.");
}

void MaterialsWorkbenchBorderPanel::Impl::OnSelectedItemIdChanged(wxCommandEvent &event) {
	if (state_.internalUpdate) {
		event.Skip();
		return;
	}
	SyncSelectedSlotFromEditor(false);
	event.Skip();
}

void MaterialsWorkbenchBorderPanel::Impl::OnSelectedItemIdSpin(wxSpinEvent &event) {
	if (state_.internalUpdate) {
		event.Skip();
		return;
	}
	SyncSelectedSlotFromEditor(false);
	event.Skip();
}

void MaterialsWorkbenchBorderPanel::Impl::OnMetadataFieldChanged(wxCommandEvent &event) {
	if (state_.internalUpdate || !state_.hasBorderSet) {
		event.Skip();
		return;
	}

	UpdateUsageContextControls();
	RefreshScopeSpecificLayout();
	UpdateWorkspaceHeader();
	RefreshSlotGrid();
	RefreshPreviewGrid();
	RefreshDirtyState();
	event.Skip();
}

void MaterialsWorkbenchBorderPanel::Impl::OnUsageContextChanged(wxGridEvent &event) {
	if (state_.internalUpdate || !state_.hasBorderSet) {
		event.Skip();
		return;
	}

	const int visibleSelection = event.GetRow();
	state_.selectedUsageIndex = (visibleSelection != wxNOT_FOUND && visibleSelection >= 0 && visibleSelection < static_cast<int>(state_.filteredUsageIndexes.size()))
		? state_.filteredUsageIndexes[visibleSelection]
		: wxNOT_FOUND;
	if (ui_.usageGrid && visibleSelection != wxNOT_FOUND) {
		state_.internalUpdate = true;
		ui_.usageGrid->ClearSelection();
		ui_.usageGrid->SetGridCursor(visibleSelection, 0);
		ui_.usageGrid->SelectRow(visibleSelection, false);
		state_.internalUpdate = false;
	}
	HandleUsageContextChanged();
	SetStatusMessage("Preview context updated. Global border center now reflects the selected linked brush.");
	event.Skip();
}

void MaterialsWorkbenchBorderPanel::Impl::OnUsageSearchChanged(wxCommandEvent &event) {
	if (state_.internalUpdate) {
		event.Skip();
		return;
	}

	PopulateUsageContextList();
	UpdateUsageContextControls();
	RefreshPreviewGrid();
	event.Skip();
}

void MaterialsWorkbenchBorderPanel::Impl::OnOpenLinkedBrush(wxCommandEvent &event) {
	if (!state_.hasBorderSet) {
		SetStatusMessage("Load a border set before opening a linked brush.");
		return;
	}

	const BorderSetUsageRecord* usage = GetSelectedUsageContext();
	if (!usage || usage->brushId <= 0) {
		SetStatusMessage("This border set has no linked brush context to open.");
		return;
	}
	if (!callbacks_.onOpenLinkedBrush) {
		SetStatusMessage("Linked brush navigation is unavailable in this workspace.");
		return;
	}

	callbacks_.onOpenLinkedBrush(usage->brushId);
}

void MaterialsWorkbenchBorderPanel::Impl::OnOpenOwnerBrush(wxCommandEvent &) {
	if (!state_.hasBorderSet) {
		SetStatusMessage("Load a border set before opening the owner brush.");
		return;
	}
	if (ui_.scopeChoice && ui_.scopeChoice->GetStringSelection() != "inline") {
		SetStatusMessage("Only inline borders have an owner brush to open.");
		return;
	}
	const int64_t ownerBrushId = state_.borderSetStorage.borderSet.ownerBrushId;
	if (ownerBrushId <= 0) {
		SetStatusMessage("This inline border set is missing an owner brush.");
		return;
	}
	if (!callbacks_.onOpenLinkedBrush) {
		SetStatusMessage("Brush navigation is unavailable in this workspace.");
		return;
	}
	callbacks_.onOpenLinkedBrush(ownerBrushId);
}

void MaterialsWorkbenchBorderPanel::Impl::OnCreateBorder(wxCommandEvent &event) {
	if (state_.hasBorderSet && !ResolvePendingChangesBeforeSwitch(&self_, "a new border")) {
		SetStatusMessage("Border creation cancelled (pending changes were not resolved).");
		return;
	}

	wxArrayString scopeChoices;
	scopeChoices.Add("global");
	scopeChoices.Add("inline");
	wxSingleChoiceDialog scopeDialog(&self_, "Choose the scope for the new border.", "New Border", scopeChoices);
	if (state_.hasBorderSet) {
		scopeDialog.SetSelection(ui_.scopeChoice->GetStringSelection() == "inline" ? 1 : 0);
	}
	if (scopeDialog.ShowModal() != wxID_OK) {
		SetStatusMessage("Border creation cancelled.");
		return;
	}

	BorderSetStorageRecord newStorage;
	newStorage.borderSet.borderScope = scopeDialog.GetStringSelection();
	newStorage.borderSet.borderType = state_.hasBorderSet && ui_.typeCtrl->GetSelection() != wxNOT_FOUND ? ui_.typeCtrl->GetStringSelection() : wxString::FromUTF8("normal");
	newStorage.borderSet.borderGroup = state_.hasBorderSet && ui_.borderGroupCtrl && ui_.borderGroupCtrl->GetSelection() != wxNOT_FOUND
		? ParseBorderGroupChoiceValue(ui_.borderGroupCtrl->GetStringSelection())
		: 0;
	newStorage.borderSet.sourceFile = "materials.db";
	if (newStorage.borderSet.borderType.IsEmpty()) {
		newStorage.borderSet.borderType = "normal";
	}

	if (newStorage.borderSet.borderScope == "global") {
		newStorage.borderSet.xmlBorderId = SuggestNextBorderId();
	} else {
		FindBrushDialog brushDialog(&self_, "Choose Owner Brush");
		if (brushDialog.ShowModal() == 0) {
			SetStatusMessage("Inline border creation cancelled.");
			return;
		}
		const Brush* brush = brushDialog.getResult();
		if (!brush) {
			SetStatusMessage("Inline border creation needs an owner brush.");
			return;
		}
		const wxString brushType = GetDatabaseBrushType(brush);
		if (brushType != "ground") {
			wxMessageBox("Inline border creation needs a ground brush owner.", "New Border", wxOK | wxICON_INFORMATION, &self_);
			SetStatusMessage("Inline border creation needs a ground brush owner.");
			return;
		}
		const wxString brushName = wxString::FromUTF8(brush->getName());
		int64_t ownerBrushId = 0;
		wxString ownerError;
		if (!TryResolveMaterialsBrushIdFromDatabase(brushType, brushName, ownerBrushId, ownerError)) {
			const wxString message = "Inline border creation failed: could not resolve the selected brush in materials.db.\n"
									 "Brush: "
				+ brushName + "\n"
							  "Error: "
				+ ownerError;
			wxMessageBox(message, "New Border", wxOK | wxICON_ERROR, &self_);
			SetStatusMessage("Inline border creation failed: could not resolve brush.");
			return;
		}
		newStorage.borderSet.ownerBrushId = ownerBrushId;
	}

	wxString error;
	if (!controller_.SaveBorderSet(newStorage, error)) {
		wxMessageBox("Failed to create border: " + error, "New Border", wxOK | wxICON_ERROR, &self_);
		SetStatusMessage("Failed to create border: " + error);
		return;
	}
	if (newStorage.borderSet.id <= 0) {
		const wxString message = "Created the border, but it returned an invalid id.";
		wxMessageBox(message, "New Border", wxOK | wxICON_ERROR, &self_);
		SetStatusMessage(message);
		return;
	}

	if (!ReloadBorderSetById(newStorage.borderSet.id)) {
		const wxString message = "Created the border, but failed to reload it in the workspace. The navigation tree will be refreshed so you can locate it manually.";
		wxMessageBox(message, "New Border", wxOK | wxICON_WARNING, &self_);
		SetStatusMessage(message);
		if (callbacks_.onBorderSetSaved) {
			callbacks_.onBorderSetSaved(newStorage.borderSet.id);
		}
		return;
	}

	SetStatusMessage(wxString::Format("Created border set #%lld in materials.db.", static_cast<long long>(newStorage.borderSet.id)));
	if (callbacks_.onBorderSetSaved) {
		callbacks_.onBorderSetSaved(newStorage.borderSet.id);
	}
}

namespace {
	wxString BuildGlobalBorderDeleteWarningText(size_t usageCount, const wxString &usagePreview) {
		if (usageCount <= 0) {
			return "Delete this global border from materials.db?\n\nThis cannot be undone.";
		}
		return wxString::Format(
			"Delete this global border?\n\nUsed By contexts: %zu\n\nPreview:\n%s\nThis will also remove those Used By contexts from linked brushes.\n\nThis cannot be undone.",
			usageCount,
			usagePreview
		);
	}

	wxString BuildInlineBorderDeleteWarningText(const wxString &ownerLabel, size_t filledSlotCount, const wxString &style) {
		return wxString::Format(
			"Delete this inline border set from materials.db?\n\nOwner Brush: %s\nFilled Slots: %zu\nStyle: %s\n\nThis cannot be undone.",
			ownerLabel,
			filledSlotCount,
			style
		);
	}
} // namespace

bool MaterialsWorkbenchBorderPanel::Impl::RemoveGlobalBorderContextsBeforeDelete(int64_t borderSetId, wxString &error) {
	std::unordered_set<int64_t> processedBrushIds;
	for (const BorderSetUsageRecord &usage : state_.borderSetUsages) {
		if (usage.brushId <= 0) {
			continue;
		}
		if (!processedBrushIds.insert(usage.brushId).second) {
			continue;
		}

		BrushStorageRecord brushStorage;
		if (!LoadBrushStorageById(usage.brushId, brushStorage, error)) {
			return false;
		}

		std::erase_if(brushStorage.borders, [borderSetId](const GroundBrushBorderRecord &record) {
			return record.borderSetId == borderSetId;
		});
		for (size_t i = 0; i < brushStorage.borders.size(); ++i) {
			brushStorage.borders[i].sortOrder = static_cast<int>(i);
		}
		if (!controller_.SaveGroundBrushBorders(usage.brushId, brushStorage.borders, error)) {
			return false;
		}
	}
	return true;
}

void MaterialsWorkbenchBorderPanel::Impl::OnDeleteBorder(wxCommandEvent &event) {
	if (!state_.hasBorderSet) {
		SetStatusMessage("Load a border set before deleting it.");
		return;
	}

	const BorderSetRecord border = state_.borderSetStorage.borderSet;
	wxString warningText = "Delete this border set from materials.db?\n\nThis cannot be undone.";
	if (border.borderScope == "global") {
		const size_t usageCount = state_.borderSetUsages.size();
		wxString usagePreview;
		const size_t previewCount = std::min<size_t>(usageCount, 6);
		for (size_t i = 0; i < previewCount; ++i) {
			usagePreview << "- " << BuildUsageSelectionSummary(state_.borderSetUsages[i]) << "\n";
		}
		if (usageCount > previewCount) {
			usagePreview << wxString::Format("- ...and %zu more\n", usageCount - previewCount);
		}
		warningText = BuildGlobalBorderDeleteWarningText(usageCount, usagePreview);
	} else if (border.borderScope == "inline") {
		const wxString ownerLabel = BuildOwnerBrushDisplayLabel(border.ownerBrushId);
		const wxString style = border.borderType.IsEmpty() ? wxString("normal") : border.borderType;
		warningText = BuildInlineBorderDeleteWarningText(ownerLabel, state_.borderSetStorage.items.size(), style);
	}

	if (wxMessageBox(
			warningText,
			"Delete Border",
			wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
			&self_
		)
		!= wxYES) {
		return;
	}

	wxString error;
	if (border.borderScope == "global" && !RemoveGlobalBorderContextsBeforeDelete(border.id, error)) {
		SetStatusMessage("Failed to remove linked contexts before delete: " + error);
		return;
	}

	if (!controller_.DeleteBorderSet(border.id, error)) {
		SetStatusMessage("Failed to delete border: " + error);
		return;
	}

	ClearWorkspace("Border deleted from materials.db.");
	SetStatusMessage("Deleted border and refreshed the Workbench catalog.");
	if (callbacks_.onBorderSetDeleted) {
		callbacks_.onBorderSetDeleted(border.id, border.borderScope);
	}
}

void MaterialsWorkbenchBorderPanel::Impl::OnAddUsageContext(wxCommandEvent &event) {
	if (!state_.hasBorderSet || state_.borderSetStorage.borderSet.borderScope != "global") {
		SetStatusMessage("Load a global border before adding a context.");
		return;
	}

	GlobalUsageEditData dialogData;
	dialogData.borderRole = "normal";
	dialogData.align = "outer";
	dialogData.targetMode = "all";
	GlobalUsageDialog dialog(&self_, controller_, "Add Used By Context", dialogData);
	if (dialog.ShowModal() != wxID_OK || !dialog.Validate() || !dialog.TransferDataFromWindow()) {
		return;
	}

	const GlobalUsageEditData &result = dialog.GetData();
	wxString error;
	BrushStorageRecord brushStorage;
	if (!LoadBrushStorageById(result.ownerBrushId, brushStorage, error)) {
		SetStatusMessage("Failed to load the selected brush: " + error);
		return;
	}

	GroundBrushBorderRecord borderRecord;
	borderRecord.borderSetId = state_.borderSetStorage.borderSet.id;
	borderRecord.borderRole = result.borderRole;
	borderRecord.align = result.align;
	borderRecord.targetMode = result.targetMode;
	borderRecord.targetBrushId = result.targetMode == "brush" ? result.targetBrushId : 0;
	borderRecord.targetBrushName = result.targetMode == "brush" ? result.targetBrushName : wxString();
	borderRecord.superBorder = result.superBorder;
	borderRecord.sortOrder = static_cast<int>(brushStorage.borders.size());
	brushStorage.borders.push_back(borderRecord);

	if (!controller_.SaveGroundBrushBorders(result.ownerBrushId, brushStorage.borders, error)) {
		SetStatusMessage("Failed to save the new usage context: " + error);
		return;
	}
	if (!controller_.ReloadCatalog()) {
		SetStatusMessage("Saved the new usage context, but failed to reload the catalog.");
		return;
	}
	if (!ReloadBorderSetById(state_.borderSetStorage.borderSet.id)) {
		SetStatusMessage("Saved the new usage context, but failed to reload this border.");
		return;
	}

	for (size_t i = 0; i < state_.borderSetUsages.size(); ++i) {
		if (state_.borderSetUsages[i].brushId == result.ownerBrushId && state_.borderSetUsages[i].sortOrder == borderRecord.sortOrder) {
			state_.selectedUsageIndex = static_cast<int>(i);
			break;
		}
	}
	PopulateUsageContextList();
	HandleUsageContextChanged();
	SetStatusMessage("Added a new Used By context and refreshed the global preview.");
	if (callbacks_.onBorderSetSaved) {
		callbacks_.onBorderSetSaved(state_.borderSetStorage.borderSet.id);
	}
}

namespace {
	void NormalizeGroundBorderSortOrders(std::vector<GroundBrushBorderRecord> &borders) {
		int sortOrder = 0;
		for (GroundBrushBorderRecord &record : borders) {
			record.sortOrder = sortOrder;
			++sortOrder;
		}
	}

	GlobalUsageEditData BuildGlobalUsageDialogData(const BorderSetUsageRecord &usage) {
		GlobalUsageEditData dialogData;
		dialogData.ownerBrushId = usage.brushId;
		dialogData.ownerBrushName = usage.brushName;

		dialogData.borderRole = usage.borderRole;
		if (dialogData.borderRole.IsEmpty()) {
			dialogData.borderRole = "normal";
		}

		dialogData.align = usage.align;
		if (dialogData.align.IsEmpty()) {
			dialogData.align = "outer";
		}

		dialogData.targetMode = usage.targetMode;
		if (dialogData.targetMode.IsEmpty()) {
			dialogData.targetMode = "all";
		}

		dialogData.targetBrushId = usage.targetBrushId;
		dialogData.targetBrushName = usage.targetBrushName;
		dialogData.superBorder = usage.superBorder;
		return dialogData;
	}

	GroundBrushBorderRecord BuildUpdatedGroundBorderRecord(const GroundBrushBorderRecord &base, const GlobalUsageEditData &result) {
		GroundBrushBorderRecord updatedRecord = base;
		updatedRecord.borderRole = result.borderRole;
		updatedRecord.align = result.align;
		updatedRecord.targetMode = result.targetMode;
		if (result.targetMode == "brush") {
			updatedRecord.targetBrushId = result.targetBrushId;
			updatedRecord.targetBrushName = result.targetBrushName;
		} else {
			updatedRecord.targetBrushId = 0;
			updatedRecord.targetBrushName.clear();
		}
		updatedRecord.superBorder = result.superBorder;
		return updatedRecord;
	}

	bool IsMatchingUsageAfterEdit(const BorderSetUsageRecord &candidate, int64_t ownerBrushId, const GroundBrushBorderRecord &record) {
		return candidate.brushId == ownerBrushId
			&& candidate.sortOrder == record.sortOrder
			&& candidate.borderRole == record.borderRole
			&& candidate.align == record.align
			&& candidate.targetMode == record.targetMode
			&& candidate.targetBrushId == record.targetBrushId
			&& candidate.targetBrushName == record.targetBrushName
			&& candidate.superBorder == record.superBorder;
	}
} // namespace

bool MaterialsWorkbenchBorderPanel::Impl::PersistEditedUsageContext(
	const BorderSetUsageRecord &usage,
	const GlobalUsageEditData &result,
	int existingIndex,
	BrushStorageRecord &sourceBrushStorage,
	GroundBrushBorderRecord &updatedRecord,
	wxString &error
) {
	if (result.ownerBrushId == usage.brushId) {
		sourceBrushStorage.borders[existingIndex] = updatedRecord;
		if (!controller_.SaveGroundBrushBorders(usage.brushId, sourceBrushStorage.borders, error)) {
			SetStatusMessage("Failed to save the edited usage context: " + error);
			return false;
		}
		return true;
	}

	sourceBrushStorage.borders.erase(sourceBrushStorage.borders.begin() + existingIndex);
	NormalizeGroundBorderSortOrders(sourceBrushStorage.borders);
	if (!controller_.SaveGroundBrushBorders(usage.brushId, sourceBrushStorage.borders, error)) {
		SetStatusMessage("Failed to update the previous owner brush: " + error);
		return false;
	}

	BrushStorageRecord targetBrushStorage;
	if (!LoadBrushStorageById(result.ownerBrushId, targetBrushStorage, error)) {
		SetStatusMessage("Failed to load the new owner brush: " + error);
		return false;
	}

	updatedRecord.sortOrder = static_cast<int>(targetBrushStorage.borders.size());
	targetBrushStorage.borders.push_back(updatedRecord);
	if (!controller_.SaveGroundBrushBorders(result.ownerBrushId, targetBrushStorage.borders, error)) {
		SetStatusMessage("Failed to save the context on the new owner brush: " + error);
		return false;
	}

	return true;
}

void MaterialsWorkbenchBorderPanel::Impl::SelectUsageIndexAfterEdit(int64_t ownerBrushId, const GroundBrushBorderRecord &record) {
	for (size_t i = 0; i < state_.borderSetUsages.size(); ++i) {
		if (IsMatchingUsageAfterEdit(state_.borderSetUsages[i], ownerBrushId, record)) {
			state_.selectedUsageIndex = static_cast<int>(i);
			return;
		}
	}
}

void MaterialsWorkbenchBorderPanel::Impl::OnEditUsageContext(wxCommandEvent &event) {
	if (!state_.hasBorderSet || state_.borderSetStorage.borderSet.borderScope != "global") {
		SetStatusMessage("Load a global border before editing a context.");
		return;
	}

	const BorderSetUsageRecord* usage = GetSelectedUsageContext();
	if (!usage) {
		SetStatusMessage("Select a Used By context before editing it.");
		return;
	}

	const GlobalUsageEditData dialogData = BuildGlobalUsageDialogData(*usage);
	GlobalUsageDialog dialog(&self_, controller_, "Edit Used By Context", dialogData);
	if (dialog.ShowModal() != wxID_OK || !dialog.Validate() || !dialog.TransferDataFromWindow()) {
		return;
	}

	const GlobalUsageEditData &result = dialog.GetData();

	wxString error;
	BrushStorageRecord sourceBrushStorage;
	if (!LoadBrushStorageById(usage->brushId, sourceBrushStorage, error)) {
		SetStatusMessage("Failed to load the current owner brush: " + error);
		return;
	}

	const int existingIndex = FindMatchingGroundBorderIndex(sourceBrushStorage, *usage);
	if (existingIndex < 0) {
		SetStatusMessage("Could not match the selected usage context in the owner brush.");
		return;
	}

	GroundBrushBorderRecord updatedRecord = BuildUpdatedGroundBorderRecord(sourceBrushStorage.borders[existingIndex], result);
	if (!PersistEditedUsageContext(*usage, result, existingIndex, sourceBrushStorage, updatedRecord, error)) {
		return;
	}

	if (!controller_.ReloadCatalog()) {
		SetStatusMessage("Saved the edited usage context, but failed to reload the catalog.");
		return;
	}
	if (!ReloadBorderSetById(state_.borderSetStorage.borderSet.id)) {
		SetStatusMessage("Saved the edited usage context, but failed to reload this border.");
		return;
	}

	SelectUsageIndexAfterEdit(result.ownerBrushId, updatedRecord);
	PopulateUsageContextList();
	HandleUsageContextChanged();
	SetStatusMessage("Updated the Used By context and refreshed the global preview.");
	if (callbacks_.onBorderSetSaved) {
		callbacks_.onBorderSetSaved(state_.borderSetStorage.borderSet.id);
	}
}

void MaterialsWorkbenchBorderPanel::Impl::OnEditUsageCases(wxCommandEvent &event) {
	if (!state_.hasBorderSet || state_.borderSetStorage.borderSet.borderScope != "global") {
		SetStatusMessage("Load a global border before editing specific cases.");
		return;
	}

	const BorderSetUsageRecord* usage = GetSelectedUsageContext();
	if (!usage) {
		SetStatusMessage("Select a Used By context before editing its specific cases.");
		return;
	}

	wxString error;
	BrushStorageRecord brushStorage;
	if (!LoadBrushStorageById(usage->brushId, brushStorage, error)) {
		SetStatusMessage("Failed to load the selected brush: " + error);
		return;
	}
	const int existingIndex = FindMatchingGroundBorderIndex(brushStorage, *usage);
	if (existingIndex < 0) {
		SetStatusMessage("Could not match the selected usage context in the owner brush.");
		return;
	}

	GroundBrushBorderRecord &borderRecord = brushStorage.borders[existingIndex];
	GroundSpecificCasesDialog dialog(&self_, borderRecord.cases);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	std::vector<GroundBorderCaseRecord> cases = dialog.GetCases();
	NormalizeGroundBorderCases(cases);
	borderRecord.cases = std::move(cases);

	if (!controller_.SaveGroundBrushBorders(usage->brushId, brushStorage.borders, error)) {
		SetStatusMessage("Failed to save the specific cases: " + error);
		return;
	}
	if (!controller_.ReloadCatalog()) {
		SetStatusMessage("Saved the specific cases, but failed to reload the catalog.");
		return;
	}
	if (!ReloadBorderSetById(state_.borderSetStorage.borderSet.id)) {
		SetStatusMessage("Saved the specific cases, but failed to reload this border.");
		return;
	}

	for (size_t i = 0; i < state_.borderSetUsages.size(); ++i) {
		if (state_.borderSetUsages[i].brushId == usage->brushId && state_.borderSetUsages[i].sortOrder == usage->sortOrder && state_.borderSetUsages[i].borderRole == usage->borderRole && state_.borderSetUsages[i].align == usage->align && state_.borderSetUsages[i].targetMode == usage->targetMode && state_.borderSetUsages[i].targetBrushId == usage->targetBrushId && state_.borderSetUsages[i].targetBrushName == usage->targetBrushName && state_.borderSetUsages[i].superBorder == usage->superBorder) {
			state_.selectedUsageIndex = static_cast<int>(i);
			break;
		}
	}

	PopulateUsageContextList();
	HandleUsageContextChanged();
	SetStatusMessage("Saved specific cases for the selected context.");
	if (callbacks_.onBorderSetSaved) {
		callbacks_.onBorderSetSaved(state_.borderSetStorage.borderSet.id);
	}
}

void MaterialsWorkbenchBorderPanel::Impl::OnRemoveUsageContext(wxCommandEvent &event) {
	if (!state_.hasBorderSet || state_.borderSetStorage.borderSet.borderScope != "global") {
		SetStatusMessage("Load a global border before removing a context.");
		return;
	}

	const BorderSetUsageRecord* usage = GetSelectedUsageContext();
	if (!usage) {
		SetStatusMessage("Select a Used By context before removing it.");
		return;
	}
	if (wxMessageBox(
			wxString::Format("Remove the context from brush \"%s\"?", usage->brushName),
			"Remove Context",
			wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
			&self_
		)
		!= wxYES) {
		return;
	}

	wxString error;
	BrushStorageRecord brushStorage;
	if (!LoadBrushStorageById(usage->brushId, brushStorage, error)) {
		SetStatusMessage("Failed to load the selected brush: " + error);
		return;
	}
	const int existingIndex = FindMatchingGroundBorderIndex(brushStorage, *usage);
	if (existingIndex < 0) {
		SetStatusMessage("Could not match the selected usage context in the owner brush.");
		return;
	}

	brushStorage.borders.erase(brushStorage.borders.begin() + existingIndex);
	for (size_t i = 0; i < brushStorage.borders.size(); ++i) {
		brushStorage.borders[i].sortOrder = static_cast<int>(i);
	}
	if (!controller_.SaveGroundBrushBorders(usage->brushId, brushStorage.borders, error)) {
		SetStatusMessage("Failed to remove the usage context: " + error);
		return;
	}
	if (!controller_.ReloadCatalog()) {
		SetStatusMessage("Removed the usage context, but failed to reload the catalog.");
		return;
	}
	if (!ReloadBorderSetById(state_.borderSetStorage.borderSet.id)) {
		SetStatusMessage("Removed the usage context, but failed to reload this border.");
		return;
	}

	state_.selectedUsageIndex = std::min<int>(state_.selectedUsageIndex, static_cast<int>(state_.borderSetUsages.size()) - 1);
	PopulateUsageContextList();
	HandleUsageContextChanged();
	SetStatusMessage("Removed the Used By context and refreshed the global preview.");
	if (callbacks_.onBorderSetSaved) {
		callbacks_.onBorderSetSaved(state_.borderSetStorage.borderSet.id);
	}
}
