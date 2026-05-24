#include "main.h"

#include "materials_workbench_brush_panel.h"

#include <limits>
#include <utility>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/settings.h>
#include <wx/spinctrl.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "items.h"
#include "materials_workbench_controller.h"

namespace {
	bool IsValidBrushEditorType(const wxString &type) {
		return type == "ground" || type == "carpet" || type == "table" || type == "doodad";
	}

	bool IsKnownItemId(int itemId) {
		if (itemId <= 0 || itemId > std::numeric_limits<uint16_t>::max()) {
			return false;
		}
		return g_items.isValidID(static_cast<uint16_t>(itemId));
	}

	wxStaticText* CreateSectionLabel(wxWindow* parent, const wxString &label) {
		wxStaticText* text = new wxStaticText(parent, wxID_ANY, label);
		wxFont font = text->GetFont();
		font.SetWeight(wxFONTWEIGHT_BOLD);
		text->SetFont(font);
		return text;
	}

	wxTextCtrl* CreateTextField(wxWindow* parent, long style = 0) {
		return new wxTextCtrl(parent, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, style);
	}

	wxSpinCtrl* CreateSpinField(wxWindow* parent, int minValue, int maxValue) {
		wxSpinCtrl* ctrl = new wxSpinCtrl(parent, wxID_ANY);
		ctrl->SetRange(minValue, maxValue);
		return ctrl;
	}

	int GetMaxEditableItemId() {
		return static_cast<int>(std::min<uint32_t>(g_items.getMaxID(), std::numeric_limits<uint16_t>::max()));
	}

	wxSpinCtrl* CreateItemIdSpinField(wxWindow* parent) {
		return CreateSpinField(parent, 0, GetMaxEditableItemId());
	}

	wxSpinCtrl* CreateLookIdSpinField(wxWindow* parent) {
		return CreateSpinField(parent, 0, GetMaxEditableItemId());
	}

	wxString FormatImportedFromValue(const wxString &sourceFile) {
		return sourceFile.IsEmpty() ? "Not imported from legacy XML" : sourceFile;
	}

	wxString ParseImportedFromEditorValue(const wxString &sourceFile) {
		return sourceFile == "Not imported from legacy XML" ? "" : sourceFile;
	}

	int CaptureListTopItem(wxListBox* listBox) {
		if (!listBox || listBox->GetCount() == 0) {
			return wxNOT_FOUND;
		}
		return listBox->GetTopItem();
	}

	void RestoreListTopItem(wxListBox* listBox, int topItem) {
		if (!listBox || listBox->GetCount() == 0 || topItem == wxNOT_FOUND) {
			return;
		}
		const int clampedTopItem = std::min<int>(topItem, static_cast<int>(listBox->GetCount()) - 1);
		if (clampedTopItem >= 0) {
			listBox->SetFirstItem(clampedTopItem);
		}
	}

	int ClampIndexForCount(int index, size_t count) {
		if (count == 0) {
			return -1;
		}
		if (index < 0) {
			return 0;
		}
		return std::min<int>(index, static_cast<int>(count) - 1);
	}

	wxString TrimmedValue(const wxTextCtrl* ctrl) {
		wxString value = ctrl->GetValue();
		value.Trim(true);
		value.Trim(false);
		return value;
	}

	wxString FormatAlignedNodeLabel(const wxString &align, size_t itemCount, size_t index) {
		return wxString::Format("%zu. %s (%zu item%s)", index + 1, align, itemCount, itemCount == 1 ? "" : "s");
	}

	wxString FormatAlignedItemLabel(int itemId, int chance, size_t index) {
		return wxString::Format("%zu. item %d (chance %d)", index + 1, itemId, chance);
	}

	wxString FormatGroundItemLabel(int itemId, int chance, size_t index) {
		return wxString::Format("%zu. item %d (chance %d)", index + 1, itemId, chance);
	}

	wxString FormatDoodadAlternativeLabel(const DoodadAlternativeRecord &alternative, size_t index) {
		return wxString::Format(
			"%zu. %zu single | %zu composite",
			index + 1,
			alternative.singleItems.size(),
			alternative.composites.size()
		);
	}

	wxString FormatDoodadSingleItemLabel(int itemId, int chance, size_t index) {
		return wxString::Format("%zu. item %d (chance %d)", index + 1, itemId, chance);
	}

	wxString FormatDoodadCompositeLabel(const DoodadCompositeRecord &composite, size_t index) {
		return wxString::Format("%zu. chance %d | %zu tile%s", index + 1, composite.chance, composite.tiles.size(), composite.tiles.size() == 1 ? "" : "s");
	}

	wxString FormatDoodadTileLabel(const DoodadCompositeTileRecord &tile, size_t index) {
		return wxString::Format(
			"%zu. (%d, %d, %d) | %zu item%s",
			index + 1,
			tile.offsetX,
			tile.offsetY,
			tile.offsetZ,
			tile.items.size(),
			tile.items.size() == 1 ? "" : "s"
		);
	}

	wxString FormatDoodadTileItemLabel(int itemId, size_t index) {
		return wxString::Format("%zu. item %d", index + 1, itemId);
	}

	wxString FormatBrushOverviewText(const BrushStorageRecord &storage) {
		const BrushRecord &brush = storage.brush;
		wxString text;
		text << "Brush: " << brush.name << "\n";
		text << "Type: " << brush.type << "\n";
		text << "ID: " << brush.id << "\n";
		text << "Storage: materials.db\n";
		text << "Imported from: " << FormatImportedFromValue(brush.sourceFile) << "\n\n";
		text << "lookId: " << brush.lookId << "\n";
		text << "serverLookId: " << brush.serverLookId << "\n";
		text << "zOrder: " << brush.zOrder << "\n";
		return text;
	}

	wxString FormatBrushInspectorText(const BrushStorageRecord &storage) {
		const BrushRecord &brush = storage.brush;
		wxString text = FormatBrushOverviewText(storage);
		text << "\nFlags\n";
		text << "  draggable: " << (brush.draggable ? "yes" : "no") << "\n";
		text << "  onBlocking: " << (brush.onBlocking ? "yes" : "no") << "\n";
		text << "  onDuplicate: " << (brush.onDuplicate ? "yes" : "no") << "\n";
		text << "  redoBorders: " << (brush.redoBorders ? "yes" : "no") << "\n";
		text << "  randomize: " << (brush.randomize ? "yes" : "no") << "\n";
		text << "  oneSize: " << (brush.oneSize ? "yes" : "no") << "\n";
		text << "  soloOptional: " << (brush.soloOptional ? "yes" : "no") << "\n";
		text << "\nBrush items: " << storage.items.size() << "\n";
		text << "Ground borders: " << storage.borders.size() << "\n";
		text << "Links: " << storage.links.size() << "\n";
		text << "Wall parts: " << storage.wallParts.size() << "\n";
		text << "Carpet nodes: " << storage.carpetNodes.size() << "\n";
		text << "Table nodes: " << storage.tableNodes.size() << "\n";
		text << "Doodad alternatives: " << storage.doodadAlternatives.size() << "\n";
		return text;
	}

	void NormalizeVariationSortOrdersForStorage(BrushStorageRecord &storage) {
		for (size_t i = 0; i < storage.items.size(); ++i) {
			storage.items[i].sortOrder = static_cast<int>(i);
		}
		for (size_t i = 0; i < storage.carpetNodes.size(); ++i) {
			storage.carpetNodes[i].sortOrder = static_cast<int>(i);
			for (size_t j = 0; j < storage.carpetNodes[i].items.size(); ++j) {
				storage.carpetNodes[i].items[j].sortOrder = static_cast<int>(j);
			}
		}
		for (size_t i = 0; i < storage.tableNodes.size(); ++i) {
			storage.tableNodes[i].sortOrder = static_cast<int>(i);
			for (size_t j = 0; j < storage.tableNodes[i].items.size(); ++j) {
				storage.tableNodes[i].items[j].sortOrder = static_cast<int>(j);
			}
		}
		for (size_t i = 0; i < storage.doodadAlternatives.size(); ++i) {
			DoodadAlternativeRecord &alternative = storage.doodadAlternatives[i];
			alternative.sortOrder = static_cast<int>(i);
			for (size_t j = 0; j < alternative.singleItems.size(); ++j) {
				alternative.singleItems[j].sortOrder = static_cast<int>(j);
			}
			for (size_t j = 0; j < alternative.composites.size(); ++j) {
				DoodadCompositeRecord &composite = alternative.composites[j];
				composite.sortOrder = static_cast<int>(j);
				for (size_t k = 0; k < composite.tiles.size(); ++k) {
					DoodadCompositeTileRecord &tile = composite.tiles[k];
					tile.sortOrder = static_cast<int>(k);
					for (size_t l = 0; l < tile.items.size(); ++l) {
						tile.items[l].sortOrder = static_cast<int>(l);
					}
				}
			}
		}
	}

	void NormalizeBrushStorageForEditing(BrushStorageRecord &storage) {
		storage.brush.type.MakeLower();
		if (storage.brush.type == "ground") {
			storage.carpetNodes.clear();
			storage.tableNodes.clear();
			storage.doodadAlternatives.clear();
		} else if (storage.brush.type == "carpet") {
			storage.items.clear();
			storage.tableNodes.clear();
			storage.doodadAlternatives.clear();
		} else if (storage.brush.type == "table") {
			storage.items.clear();
			storage.carpetNodes.clear();
			storage.doodadAlternatives.clear();
		} else if (storage.brush.type == "doodad") {
			storage.items.clear();
			storage.carpetNodes.clear();
			storage.tableNodes.clear();
		} else {
			storage.items.clear();
			storage.carpetNodes.clear();
			storage.tableNodes.clear();
			storage.doodadAlternatives.clear();
		}
		NormalizeVariationSortOrdersForStorage(storage);
	}

	template <typename T, typename Compare>
	bool VectorsEqual(const std::vector<T> &left, const std::vector<T> &right, Compare compare) {
		if (left.size() != right.size()) {
			return false;
		}
		for (size_t i = 0; i < left.size(); ++i) {
			if (!compare(left[i], right[i])) {
				return false;
			}
		}
		return true;
	}

	bool AreBrushRecordsEqual(const BrushRecord &left, const BrushRecord &right) {
		return left.id == right.id &&
			   left.name == right.name &&
			   left.type == right.type &&
			   left.lookId == right.lookId &&
			   left.zOrder == right.zOrder &&
			   left.sourceFile == right.sourceFile &&
			   left.serverLookId == right.serverLookId &&
			   left.draggable == right.draggable &&
			   left.onBlocking == right.onBlocking &&
			   left.onDuplicate == right.onDuplicate &&
			   left.redoBorders == right.redoBorders &&
			   left.randomize == right.randomize &&
			   left.oneSize == right.oneSize &&
			   left.soloOptional == right.soloOptional &&
			   left.thickness == right.thickness &&
			   left.thicknessCeiling == right.thicknessCeiling;
	}

	bool AreBrushItemRecordsEqual(const BrushItemRecord &left, const BrushItemRecord &right) {
		return left.brushId == right.brushId &&
			   left.itemId == right.itemId &&
			   left.chance == right.chance &&
			   left.sortOrder == right.sortOrder;
	}

	bool AreGroundBorderCaseConditionRecordsEqual(const GroundBorderCaseConditionRecord &left, const GroundBorderCaseConditionRecord &right) {
		return left.conditionType == right.conditionType &&
			   left.matchValue == right.matchValue &&
			   left.edge == right.edge &&
			   left.sortOrder == right.sortOrder;
	}

	bool AreGroundBorderCaseActionRecordsEqual(const GroundBorderCaseActionRecord &left, const GroundBorderCaseActionRecord &right) {
		return left.actionType == right.actionType &&
			   left.targetValue == right.targetValue &&
			   left.edge == right.edge &&
			   left.replacementValue == right.replacementValue &&
			   left.sortOrder == right.sortOrder;
	}

	bool AreGroundBorderCaseRecordsEqual(const GroundBorderCaseRecord &left, const GroundBorderCaseRecord &right) {
		return left.sortOrder == right.sortOrder &&
			   VectorsEqual(left.conditions, right.conditions, AreGroundBorderCaseConditionRecordsEqual) &&
			   VectorsEqual(left.actions, right.actions, AreGroundBorderCaseActionRecordsEqual);
	}

	bool AreGroundBrushBorderRecordsEqual(const GroundBrushBorderRecord &left, const GroundBrushBorderRecord &right) {
		return left.borderSetId == right.borderSetId &&
			   left.borderRole == right.borderRole &&
			   left.align == right.align &&
			   left.targetMode == right.targetMode &&
			   left.targetBrushId == right.targetBrushId &&
			   left.targetBrushName == right.targetBrushName &&
			   left.superBorder == right.superBorder &&
			   left.sortOrder == right.sortOrder &&
			   VectorsEqual(left.cases, right.cases, AreGroundBorderCaseRecordsEqual);
	}

	bool AreBrushLinkRecordsEqual(const BrushLinkRecord &left, const BrushLinkRecord &right) {
		return left.brushId == right.brushId &&
			   left.targetBrushId == right.targetBrushId &&
			   left.targetBrushName == right.targetBrushName &&
			   left.relationType == right.relationType &&
			   left.sortOrder == right.sortOrder;
	}

	bool AreWallPartItemRecordsEqual(const WallPartItemRecord &left, const WallPartItemRecord &right) {
		return left.itemId == right.itemId &&
			   left.chance == right.chance &&
			   left.sortOrder == right.sortOrder;
	}

	bool AreWallPartDoorRecordsEqual(const WallPartDoorRecord &left, const WallPartDoorRecord &right) {
		return left.itemId == right.itemId &&
			   left.doorType == right.doorType &&
			   left.isOpen == right.isOpen &&
			   left.wallHateMe == right.wallHateMe &&
			   left.sortOrder == right.sortOrder;
	}

	bool AreWallPartRecordsEqual(const WallPartRecord &left, const WallPartRecord &right) {
		return left.partType == right.partType &&
			   left.sortOrder == right.sortOrder &&
			   VectorsEqual(left.items, right.items, AreWallPartItemRecordsEqual) &&
			   VectorsEqual(left.doors, right.doors, AreWallPartDoorRecordsEqual);
	}

	bool AreCarpetNodeItemRecordsEqual(const CarpetNodeItemRecord &left, const CarpetNodeItemRecord &right) {
		return left.itemId == right.itemId &&
			   left.chance == right.chance &&
			   left.sortOrder == right.sortOrder;
	}

	bool AreCarpetNodeRecordsEqual(const CarpetNodeRecord &left, const CarpetNodeRecord &right) {
		return left.align == right.align &&
			   left.sortOrder == right.sortOrder &&
			   VectorsEqual(left.items, right.items, AreCarpetNodeItemRecordsEqual);
	}

	bool AreTableNodeItemRecordsEqual(const TableNodeItemRecord &left, const TableNodeItemRecord &right) {
		return left.itemId == right.itemId &&
			   left.chance == right.chance &&
			   left.sortOrder == right.sortOrder;
	}

	bool AreTableNodeRecordsEqual(const TableNodeRecord &left, const TableNodeRecord &right) {
		return left.align == right.align &&
			   left.sortOrder == right.sortOrder &&
			   VectorsEqual(left.items, right.items, AreTableNodeItemRecordsEqual);
	}

	bool AreDoodadSingleItemRecordsEqual(const DoodadSingleItemRecord &left, const DoodadSingleItemRecord &right) {
		return left.itemId == right.itemId &&
			   left.chance == right.chance &&
			   left.sortOrder == right.sortOrder;
	}

	bool AreDoodadCompositeTileItemRecordsEqual(const DoodadCompositeTileItemRecord &left, const DoodadCompositeTileItemRecord &right) {
		return left.itemId == right.itemId &&
			   left.sortOrder == right.sortOrder;
	}

	bool AreDoodadCompositeTileRecordsEqual(const DoodadCompositeTileRecord &left, const DoodadCompositeTileRecord &right) {
		return left.offsetX == right.offsetX &&
			   left.offsetY == right.offsetY &&
			   left.offsetZ == right.offsetZ &&
			   left.sortOrder == right.sortOrder &&
			   VectorsEqual(left.items, right.items, AreDoodadCompositeTileItemRecordsEqual);
	}

	bool AreDoodadCompositeRecordsEqual(const DoodadCompositeRecord &left, const DoodadCompositeRecord &right) {
		return left.chance == right.chance &&
			   left.sortOrder == right.sortOrder &&
			   VectorsEqual(left.tiles, right.tiles, AreDoodadCompositeTileRecordsEqual);
	}

	bool AreDoodadAlternativeRecordsEqual(const DoodadAlternativeRecord &left, const DoodadAlternativeRecord &right) {
		return left.sortOrder == right.sortOrder &&
			   VectorsEqual(left.singleItems, right.singleItems, AreDoodadSingleItemRecordsEqual) &&
			   VectorsEqual(left.composites, right.composites, AreDoodadCompositeRecordsEqual);
	}

	bool AreBrushStorageRecordsEqual(const BrushStorageRecord &left, const BrushStorageRecord &right) {
		return AreBrushRecordsEqual(left.brush, right.brush) &&
			   VectorsEqual(left.items, right.items, AreBrushItemRecordsEqual) &&
			   VectorsEqual(left.borders, right.borders, AreGroundBrushBorderRecordsEqual) &&
			   VectorsEqual(left.links, right.links, AreBrushLinkRecordsEqual) &&
			   VectorsEqual(left.wallParts, right.wallParts, AreWallPartRecordsEqual) &&
			   VectorsEqual(left.carpetNodes, right.carpetNodes, AreCarpetNodeRecordsEqual) &&
			   VectorsEqual(left.tableNodes, right.tableNodes, AreTableNodeRecordsEqual) &&
			   VectorsEqual(left.doodadAlternatives, right.doodadAlternatives, AreDoodadAlternativeRecordsEqual);
	}

	wxColour GetModifiedFieldColour() {
		return wxColour(255, 248, 214);
	}

	void ApplyModifiedEditorStyle(wxWindow* window, bool modified) {
		if (!window) {
			return;
		}

		wxFont font = window->GetFont();
		font.SetWeight(modified ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
		window->SetFont(font);
		window->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
		window->SetForegroundColour(modified ? wxColour(176, 102, 0) : wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
		window->Refresh();
	}

	void ApplyModifiedToggleStyle(wxCheckBox* checkBox, bool modified) {
		if (!checkBox) {
			return;
		}

		wxFont font = checkBox->GetFont();
		font.SetWeight(modified ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
		checkBox->SetFont(font);
		checkBox->Refresh();
	}

	void ApplyModifiedLabelStyle(wxStaticText* label, const wxString &baseLabel, bool modified) {
		if (!label) {
			return;
		}

		wxFont font = label->GetFont();
		font.SetWeight(modified ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
		label->SetFont(font);
		label->SetLabel(modified ? baseLabel + " [modified]" : baseLabel);
		label->SetForegroundColour(modified ? wxColour(176, 102, 0) : wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
		label->Refresh();
	}
} // namespace

MaterialsWorkbenchBrushPanel::MaterialsWorkbenchBrushPanel(wxWindow* parent, MaterialsWorkbenchController &controller) :
	wxPanel(parent, wxID_ANY),
	controller_(controller) {
	BuildLayout();
	ClearWorkspace("Select a brush in the navigation tree to edit its properties.");
}

void MaterialsWorkbenchBrushPanel::SetOnBrushSaved(std::function<void(int64_t, const wxString&, const wxString&)> callback) {
	onBrushSaved_ = std::move(callback);
}

void MaterialsWorkbenchBrushPanel::SetOnBrushStateChanged(std::function<void()> callback) {
	onBrushStateChanged_ = std::move(callback);
}

bool MaterialsWorkbenchBrushPanel::HasPendingChanges() const {
	return hasBrush_ && dirty_;
}

bool MaterialsWorkbenchBrushPanel::IsCurrentBrushSelection(const wxString &contextKey, int itemIndex) const {
	return hasBrush_ && currentContextKey_ == contextKey && currentItemIndex_ == itemIndex;
}

wxString MaterialsWorkbenchBrushPanel::GetCurrentBrushDisplayName() const {
	if (!hasBrush_) {
		return "";
	}

	const wxString displayName = nameCtrl_ ? TrimmedValue(nameCtrl_) : "";
	return displayName.IsEmpty() ? brushStorage_.brush.name : displayName;
}

wxString MaterialsWorkbenchBrushPanel::GetCurrentBrushInspectorText() const {
	if (!hasBrush_) {
		return "Select a palette, brush, border set or wall entry in the navigation tree to inspect its SQLite-backed metadata.";
	}

	return FormatBrushInspectorText(BuildEditableStorageFromCurrentState());
}

bool MaterialsWorkbenchBrushPanel::ResolvePendingChangesBeforeSwitch(wxWindow* parent, const wxString &targetLabel) {
	if (!HasPendingChanges()) {
		return true;
	}

	const wxString destination = targetLabel.IsEmpty() ? "the selected entry" : "\"" + targetLabel + "\"";
	wxMessageDialog dialog(
		parent,
		"Brush \"" + brushStorage_.brush.name + "\" has unsaved changes.\n\n"
		"You are switching to " + destination + ".\n\n"
		"Yes: save and continue\n"
		"No: discard local changes and continue\n"
		"Cancel: stay on the current brush",
		"Unsaved Brush Changes",
		wxYES_NO | wxCANCEL | wxICON_WARNING
	);
	dialog.SetYesNoCancelLabels("Save", "Discard", "Cancel");

	switch (dialog.ShowModal()) {
	case wxID_YES:
		return SaveCurrentBrush();
	case wxID_NO:
		return LoadBrush(currentContextKey_, currentItemIndex_);
	default:
		SetStatusMessage("Selection change canceled. Pending brush edits were kept.");
		return false;
	}
}

void MaterialsWorkbenchBrushPanel::BuildLayout() {
	wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);

	wxStaticText* title = new wxStaticText(this, wxID_ANY, "Brush Workspace");
	wxFont titleFont = title->GetFont();
	titleFont.SetPointSize(titleFont.GetPointSize() + 4);
	titleFont.SetWeight(wxFONTWEIGHT_BOLD);
	title->SetFont(titleFont);

	titleLabel_ = new wxStaticText(this, wxID_ANY, "No brush selected");
	subtitleLabel_ = new wxStaticText(this, wxID_ANY, "Edit the SQLite-backed brush metadata and variations used by the runtime catalog.");

	workspaceTabs_ = new wxNotebook(this, wxID_ANY);
	workspaceTabs_->AddPage(BuildMetadataPage(workspaceTabs_), "Metadata");
	workspaceTabs_->AddPage(BuildVariationsPage(workspaceTabs_), "Variations");

	wxBoxSizer* headerSizer = new wxBoxSizer(wxVERTICAL);
	headerSizer->Add(title, 0, wxBOTTOM, FromDIP(4));
	headerSizer->Add(titleLabel_, 0, wxBOTTOM, FromDIP(2));
	headerSizer->Add(subtitleLabel_, 0);

	wxBoxSizer* actionSizer = new wxBoxSizer(wxHORIZONTAL);
	saveButton_ = new wxButton(this, wxID_SAVE, "Save Brush");
	revertButton_ = new wxButton(this, wxID_ANY, "Revert");
	actionSizer->Add(saveButton_, 0, wxRIGHT, FromDIP(6));
	actionSizer->Add(revertButton_, 0);

	statusLabel_ = new wxStaticText(this, wxID_ANY, "");

	rootSizer->Add(headerSizer, 0, wxEXPAND | wxALL, FromDIP(10));
	rootSizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));
	rootSizer->Add(workspaceTabs_, 1, wxEXPAND | wxALL, FromDIP(10));
	rootSizer->Add(actionSizer, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
	rootSizer->Add(statusLabel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
	SetSizer(rootSizer);

	saveButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnSave, this);
	revertButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnRevert, this);
}

wxPanel* MaterialsWorkbenchBrushPanel::BuildMetadataPage(wxNotebook* notebook) {
	wxScrolledWindow* scrolled = new wxScrolledWindow(notebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	metadataPage_ = scrolled;
	scrolled->SetScrollRate(FromDIP(10), FromDIP(10));

	wxBoxSizer* contentSizer = new wxBoxSizer(wxVERTICAL);
	summaryLabel_ = new wxStaticText(scrolled, wxID_ANY, "");

	contentSizer->Add(CreateSectionLabel(scrolled, "Identity"), 0, wxBOTTOM, FromDIP(6));

	wxFlexGridSizer* identityGrid = new wxFlexGridSizer(2, FromDIP(8), FromDIP(10));
	identityGrid->AddGrowableCol(1, 1);

	idCtrl_ = CreateTextField(scrolled, wxTE_READONLY);
	storageCtrl_ = CreateTextField(scrolled, wxTE_READONLY);
	nameCtrl_ = CreateTextField(scrolled);
	typeCtrl_ = CreateTextField(scrolled);
	sourceCtrl_ = CreateTextField(scrolled);

	identityGrid->Add(new wxStaticText(scrolled, wxID_ANY, "SQLite ID"), 0, wxALIGN_CENTER_VERTICAL);
	identityGrid->Add(idCtrl_, 1, wxEXPAND);
	identityGrid->Add(new wxStaticText(scrolled, wxID_ANY, "Storage"), 0, wxALIGN_CENTER_VERTICAL);
	identityGrid->Add(storageCtrl_, 1, wxEXPAND);
	identityGrid->Add(new wxStaticText(scrolled, wxID_ANY, "Name"), 0, wxALIGN_CENTER_VERTICAL);
	identityGrid->Add(nameCtrl_, 1, wxEXPAND);
	identityGrid->Add(new wxStaticText(scrolled, wxID_ANY, "Type"), 0, wxALIGN_CENTER_VERTICAL);
	identityGrid->Add(typeCtrl_, 1, wxEXPAND);
	identityGrid->Add(new wxStaticText(scrolled, wxID_ANY, "Imported From"), 0, wxALIGN_CENTER_VERTICAL);
	identityGrid->Add(sourceCtrl_, 1, wxEXPAND);

	contentSizer->Add(identityGrid, 0, wxEXPAND | wxBOTTOM, FromDIP(12));
	contentSizer->Add(new wxStaticLine(scrolled), 0, wxEXPAND | wxBOTTOM, FromDIP(10));

	contentSizer->Add(CreateSectionLabel(scrolled, "Rendering And Placement"), 0, wxBOTTOM, FromDIP(6));

	wxFlexGridSizer* numericGrid = new wxFlexGridSizer(2, FromDIP(8), FromDIP(10));
	numericGrid->AddGrowableCol(1, 1);

	lookIdCtrl_ = CreateLookIdSpinField(scrolled);
	serverLookIdCtrl_ = CreateLookIdSpinField(scrolled);
	zOrderCtrl_ = CreateSpinField(scrolled, -1000000, 1000000);
	thicknessCtrl_ = CreateSpinField(scrolled, 0, 1000000);
	thicknessCeilingCtrl_ = CreateSpinField(scrolled, 0, 1000000);

	numericGrid->Add(new wxStaticText(scrolled, wxID_ANY, "lookId"), 0, wxALIGN_CENTER_VERTICAL);
	numericGrid->Add(lookIdCtrl_, 1, wxEXPAND);
	numericGrid->Add(new wxStaticText(scrolled, wxID_ANY, "serverLookId"), 0, wxALIGN_CENTER_VERTICAL);
	numericGrid->Add(serverLookIdCtrl_, 1, wxEXPAND);
	numericGrid->Add(new wxStaticText(scrolled, wxID_ANY, "zOrder"), 0, wxALIGN_CENTER_VERTICAL);
	numericGrid->Add(zOrderCtrl_, 1, wxEXPAND);
	numericGrid->Add(new wxStaticText(scrolled, wxID_ANY, "Thickness"), 0, wxALIGN_CENTER_VERTICAL);
	numericGrid->Add(thicknessCtrl_, 1, wxEXPAND);
	numericGrid->Add(new wxStaticText(scrolled, wxID_ANY, "Thickness Ceiling"), 0, wxALIGN_CENTER_VERTICAL);
	numericGrid->Add(thicknessCeilingCtrl_, 1, wxEXPAND);

	contentSizer->Add(numericGrid, 0, wxEXPAND | wxBOTTOM, FromDIP(12));
	contentSizer->Add(new wxStaticLine(scrolled), 0, wxEXPAND | wxBOTTOM, FromDIP(10));

	contentSizer->Add(CreateSectionLabel(scrolled, "Flags"), 0, wxBOTTOM, FromDIP(6));

	wxGridSizer* flagsGrid = new wxGridSizer(2, FromDIP(8), FromDIP(8));
	draggableCtrl_ = new wxCheckBox(scrolled, wxID_ANY, "Draggable");
	onBlockingCtrl_ = new wxCheckBox(scrolled, wxID_ANY, "On Blocking");
	onDuplicateCtrl_ = new wxCheckBox(scrolled, wxID_ANY, "On Duplicate");
	redoBordersCtrl_ = new wxCheckBox(scrolled, wxID_ANY, "Redo Borders");
	randomizeCtrl_ = new wxCheckBox(scrolled, wxID_ANY, "Randomize");
	oneSizeCtrl_ = new wxCheckBox(scrolled, wxID_ANY, "One Size");
	soloOptionalCtrl_ = new wxCheckBox(scrolled, wxID_ANY, "Solo Optional");

	flagsGrid->Add(draggableCtrl_, 0, wxEXPAND);
	flagsGrid->Add(onBlockingCtrl_, 0, wxEXPAND);
	flagsGrid->Add(onDuplicateCtrl_, 0, wxEXPAND);
	flagsGrid->Add(redoBordersCtrl_, 0, wxEXPAND);
	flagsGrid->Add(randomizeCtrl_, 0, wxEXPAND);
	flagsGrid->Add(oneSizeCtrl_, 0, wxEXPAND);
	flagsGrid->Add(soloOptionalCtrl_, 0, wxEXPAND);
	flagsGrid->AddSpacer(0);

	contentSizer->Add(flagsGrid, 0, wxEXPAND | wxBOTTOM, FromDIP(12));
	contentSizer->Add(new wxStaticLine(scrolled), 0, wxEXPAND | wxBOTTOM, FromDIP(10));

	contentSizer->Add(CreateSectionLabel(scrolled, "Stored Brush Data"), 0, wxBOTTOM, FromDIP(6));
	contentSizer->Add(summaryLabel_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

	scrolled->SetSizer(contentSizer);

	nameCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	typeCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	sourceCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	lookIdCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	lookIdCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	serverLookIdCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	serverLookIdCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	zOrderCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	zOrderCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	thicknessCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	thicknessCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	thicknessCeilingCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	thicknessCeilingCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	draggableCtrl_->Bind(wxEVT_CHECKBOX, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	onBlockingCtrl_->Bind(wxEVT_CHECKBOX, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	onDuplicateCtrl_->Bind(wxEVT_CHECKBOX, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	redoBordersCtrl_->Bind(wxEVT_CHECKBOX, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	randomizeCtrl_->Bind(wxEVT_CHECKBOX, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	oneSizeCtrl_->Bind(wxEVT_CHECKBOX, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	soloOptionalCtrl_->Bind(wxEVT_CHECKBOX, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	return scrolled;
}

wxPanel* MaterialsWorkbenchBrushPanel::BuildVariationsPage(wxNotebook* notebook) {
	wxPanel* panel = new wxPanel(notebook, wxID_ANY);
	wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);

	rootSizer->Add(
		new wxStaticText(
			panel,
			wxID_ANY,
			"Brush variations are stored in materials.db and edited per domain: weighted items for grounds, aligned nodes for carpet/table, alternatives for doodads."
		),
		0,
		wxEXPAND | wxBOTTOM,
		FromDIP(8)
	);

	variationsStatusLabel_ = new wxStaticText(panel, wxID_ANY, "Variation Data");
	rootSizer->Add(variationsStatusLabel_, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

	variationsBook_ = new wxSimplebook(panel, wxID_ANY);
	variationsBook_->AddPage(BuildUnsupportedVariationsPage(variationsBook_), "Unsupported");
	variationsBook_->AddPage(BuildGroundVariationsPage(variationsBook_), "Ground");
	variationsBook_->AddPage(BuildAlignedVariationsPage(variationsBook_), "Aligned");
	variationsBook_->AddPage(BuildDoodadVariationsPage(variationsBook_), "Doodad");

	rootSizer->Add(variationsBook_, 1, wxEXPAND);
	panel->SetSizer(rootSizer);
	return panel;
}

wxPanel* MaterialsWorkbenchBrushPanel::BuildUnsupportedVariationsPage(wxSimplebook* book) {
	wxPanel* panel = new wxPanel(book, wxID_ANY);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	variationsEmptyLabel_ = new wxStaticText(panel, wxID_ANY, "Select a brush to inspect variation data.");
	sizer->AddStretchSpacer();
	sizer->Add(variationsEmptyLabel_, 0, wxALIGN_CENTER | wxALL, FromDIP(12));
	sizer->AddStretchSpacer();
	panel->SetSizer(sizer);
	return panel;
}

wxPanel* MaterialsWorkbenchBrushPanel::BuildGroundVariationsPage(wxSimplebook* book) {
	wxPanel* panel = new wxPanel(book, wxID_ANY);
	wxBoxSizer* rootSizer = new wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* listSizer = new wxBoxSizer(wxVERTICAL);
	listSizer->Add(CreateSectionLabel(panel, "Ground Items"), 0, wxBOTTOM, FromDIP(6));
	groundItemsList_ = new wxListBox(panel, wxID_ANY);
	listSizer->Add(groundItemsList_, 1, wxEXPAND | wxBOTTOM, FromDIP(6));
	wxBoxSizer* buttonsSizer = new wxBoxSizer(wxHORIZONTAL);
	wxButton* addButton = new wxButton(panel, wxID_ANY, "Add Item");
	wxButton* removeButton = new wxButton(panel, wxID_ANY, "Remove");
	buttonsSizer->Add(addButton, 1, wxRIGHT, FromDIP(4));
	buttonsSizer->Add(removeButton, 1);
	listSizer->Add(buttonsSizer, 0, wxEXPAND);

	wxBoxSizer* editorSizer = new wxBoxSizer(wxVERTICAL);
	editorSizer->Add(CreateSectionLabel(panel, "Selected Item"), 0, wxBOTTOM, FromDIP(6));
	editorSizer->Add(
		new wxStaticText(panel, wxID_ANY, "Ground variations currently edit weighted item entries. Border rules stay in the dedicated border workflow."),
		0,
		wxEXPAND | wxBOTTOM,
		FromDIP(10)
	);
	wxFlexGridSizer* form = new wxFlexGridSizer(2, FromDIP(8), FromDIP(8));
	form->AddGrowableCol(1, 1);
	groundItemIdCtrl_ = CreateItemIdSpinField(panel);
	groundItemChanceCtrl_ = CreateSpinField(panel, 0, 1000000);
	form->Add(new wxStaticText(panel, wxID_ANY, "Item ID"), 0, wxALIGN_CENTER_VERTICAL);
	form->Add(groundItemIdCtrl_, 1, wxEXPAND);
	form->Add(new wxStaticText(panel, wxID_ANY, "Chance"), 0, wxALIGN_CENTER_VERTICAL);
	form->Add(groundItemChanceCtrl_, 1, wxEXPAND);
	editorSizer->Add(form, 0, wxEXPAND);

	rootSizer->Add(listSizer, 0, wxEXPAND | wxRIGHT, FromDIP(10));
	rootSizer->Add(new wxStaticLine(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL), 0, wxEXPAND | wxRIGHT, FromDIP(10));
	rootSizer->Add(editorSizer, 1, wxEXPAND);
	panel->SetSizer(rootSizer);

	addButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnAddGroundItem, this);
	removeButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnRemoveGroundItem, this);
	groundItemsList_->Bind(wxEVT_LISTBOX, &MaterialsWorkbenchBrushPanel::OnGroundItemSelected, this);
	groundItemIdCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnGroundItemValueChanged, this);
	groundItemChanceCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnGroundItemValueChanged, this);
	groundItemIdCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnGroundItemValueChanged, this);
	groundItemChanceCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnGroundItemValueChanged, this);
	return panel;
}

wxPanel* MaterialsWorkbenchBrushPanel::BuildAlignedVariationsPage(wxSimplebook* book) {
	wxPanel* panel = new wxPanel(book, wxID_ANY);
	wxBoxSizer* rootSizer = new wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* nodesSizer = new wxBoxSizer(wxVERTICAL);
	alignedSectionLabel_ = CreateSectionLabel(panel, "Aligned Nodes");
	alignedNodesList_ = new wxListBox(panel, wxID_ANY);
	wxBoxSizer* nodeButtons = new wxBoxSizer(wxHORIZONTAL);
	wxButton* addNodeButton = new wxButton(panel, wxID_ANY, "Add Node");
	wxButton* removeNodeButton = new wxButton(panel, wxID_ANY, "Remove");
	nodeButtons->Add(addNodeButton, 1, wxRIGHT, FromDIP(4));
	nodeButtons->Add(removeNodeButton, 1);
	nodesSizer->Add(alignedSectionLabel_, 0, wxBOTTOM, FromDIP(6));
	nodesSizer->Add(alignedNodesList_, 1, wxEXPAND | wxBOTTOM, FromDIP(6));
	nodesSizer->Add(nodeButtons, 0, wxEXPAND);

	wxBoxSizer* editorSizer = new wxBoxSizer(wxVERTICAL);
	editorSizer->Add(CreateSectionLabel(panel, "Node Properties"), 0, wxBOTTOM, FromDIP(6));

	wxFlexGridSizer* nodeForm = new wxFlexGridSizer(2, FromDIP(8), FromDIP(8));
	nodeForm->AddGrowableCol(1, 1);
	alignedNodeAlignCtrl_ = CreateTextField(panel);
	nodeForm->Add(new wxStaticText(panel, wxID_ANY, "Align"), 0, wxALIGN_CENTER_VERTICAL);
	nodeForm->Add(alignedNodeAlignCtrl_, 1, wxEXPAND);
	editorSizer->Add(nodeForm, 0, wxEXPAND | wxBOTTOM, FromDIP(10));

	editorSizer->Add(CreateSectionLabel(panel, "Node Items"), 0, wxBOTTOM, FromDIP(6));
	alignedItemsList_ = new wxListBox(panel, wxID_ANY);
	editorSizer->Add(alignedItemsList_, 1, wxEXPAND | wxBOTTOM, FromDIP(6));

	wxBoxSizer* itemButtons = new wxBoxSizer(wxHORIZONTAL);
	wxButton* addItemButton = new wxButton(panel, wxID_ANY, "Add Item");
	wxButton* removeItemButton = new wxButton(panel, wxID_ANY, "Remove");
	itemButtons->Add(addItemButton, 1, wxRIGHT, FromDIP(4));
	itemButtons->Add(removeItemButton, 1);
	editorSizer->Add(itemButtons, 0, wxEXPAND | wxBOTTOM, FromDIP(10));

	wxFlexGridSizer* itemForm = new wxFlexGridSizer(2, FromDIP(8), FromDIP(8));
	itemForm->AddGrowableCol(1, 1);
	alignedItemIdCtrl_ = CreateItemIdSpinField(panel);
	alignedItemChanceCtrl_ = CreateSpinField(panel, 0, 1000000);
	itemForm->Add(new wxStaticText(panel, wxID_ANY, "Item ID"), 0, wxALIGN_CENTER_VERTICAL);
	itemForm->Add(alignedItemIdCtrl_, 1, wxEXPAND);
	itemForm->Add(new wxStaticText(panel, wxID_ANY, "Chance"), 0, wxALIGN_CENTER_VERTICAL);
	itemForm->Add(alignedItemChanceCtrl_, 1, wxEXPAND);
	editorSizer->Add(itemForm, 0, wxEXPAND);

	rootSizer->Add(nodesSizer, 0, wxEXPAND | wxRIGHT, FromDIP(10));
	rootSizer->Add(new wxStaticLine(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL), 0, wxEXPAND | wxRIGHT, FromDIP(10));
	rootSizer->Add(editorSizer, 1, wxEXPAND);
	panel->SetSizer(rootSizer);

	addNodeButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnAddAlignedNode, this);
	removeNodeButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnRemoveAlignedNode, this);
	alignedNodesList_->Bind(wxEVT_LISTBOX, &MaterialsWorkbenchBrushPanel::OnAlignedNodeSelected, this);
	alignedNodeAlignCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnAlignedNodeAlignChanged, this);
	addItemButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnAddAlignedItem, this);
	removeItemButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnRemoveAlignedItem, this);
	alignedItemsList_->Bind(wxEVT_LISTBOX, &MaterialsWorkbenchBrushPanel::OnAlignedItemSelected, this);
	alignedItemIdCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnAlignedItemValueChanged, this);
	alignedItemChanceCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnAlignedItemValueChanged, this);
	alignedItemIdCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnAlignedItemValueChanged, this);
	alignedItemChanceCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnAlignedItemValueChanged, this);
	return panel;
}

wxPanel* MaterialsWorkbenchBrushPanel::BuildDoodadVariationsPage(wxSimplebook* book) {
	wxPanel* panel = new wxPanel(book, wxID_ANY);
	wxBoxSizer* rootSizer = new wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* altSizer = new wxBoxSizer(wxVERTICAL);
	altSizer->Add(CreateSectionLabel(panel, "Alternatives"), 0, wxBOTTOM, FromDIP(6));
	doodadAlternativesList_ = new wxListBox(panel, wxID_ANY);
	altSizer->Add(doodadAlternativesList_, 1, wxEXPAND | wxBOTTOM, FromDIP(6));
	wxBoxSizer* altButtons = new wxBoxSizer(wxHORIZONTAL);
	wxButton* addAltButton = new wxButton(panel, wxID_ANY, "Add Alternative");
	wxButton* removeAltButton = new wxButton(panel, wxID_ANY, "Remove");
	altButtons->Add(addAltButton, 1, wxRIGHT, FromDIP(4));
	altButtons->Add(removeAltButton, 1);
	altSizer->Add(altButtons, 0, wxEXPAND);

	wxBoxSizer* midSizer = new wxBoxSizer(wxVERTICAL);
	midSizer->Add(CreateSectionLabel(panel, "Single Items"), 0, wxBOTTOM, FromDIP(6));
	doodadSingleItemsList_ = new wxListBox(panel, wxID_ANY);
	midSizer->Add(doodadSingleItemsList_, 1, wxEXPAND | wxBOTTOM, FromDIP(6));
	wxBoxSizer* singleButtons = new wxBoxSizer(wxHORIZONTAL);
	wxButton* addSingleButton = new wxButton(panel, wxID_ANY, "Add Single");
	wxButton* removeSingleButton = new wxButton(panel, wxID_ANY, "Remove");
	singleButtons->Add(addSingleButton, 1, wxRIGHT, FromDIP(4));
	singleButtons->Add(removeSingleButton, 1);
	midSizer->Add(singleButtons, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
	wxFlexGridSizer* singleForm = new wxFlexGridSizer(2, FromDIP(8), FromDIP(8));
	singleForm->AddGrowableCol(1, 1);
	doodadSingleItemIdCtrl_ = CreateItemIdSpinField(panel);
	doodadSingleItemChanceCtrl_ = CreateSpinField(panel, 0, 1000000);
	singleForm->Add(new wxStaticText(panel, wxID_ANY, "Item ID"), 0, wxALIGN_CENTER_VERTICAL);
	singleForm->Add(doodadSingleItemIdCtrl_, 1, wxEXPAND);
	singleForm->Add(new wxStaticText(panel, wxID_ANY, "Chance"), 0, wxALIGN_CENTER_VERTICAL);
	singleForm->Add(doodadSingleItemChanceCtrl_, 1, wxEXPAND);
	midSizer->Add(singleForm, 0, wxEXPAND | wxBOTTOM, FromDIP(12));
	midSizer->Add(new wxStaticLine(panel), 0, wxEXPAND | wxBOTTOM, FromDIP(10));
	midSizer->Add(CreateSectionLabel(panel, "Composites"), 0, wxBOTTOM, FromDIP(6));
	doodadCompositesList_ = new wxListBox(panel, wxID_ANY);
	midSizer->Add(doodadCompositesList_, 1, wxEXPAND | wxBOTTOM, FromDIP(6));
	wxBoxSizer* compositeButtons = new wxBoxSizer(wxHORIZONTAL);
	wxButton* addCompositeButton = new wxButton(panel, wxID_ANY, "Add Composite");
	wxButton* removeCompositeButton = new wxButton(panel, wxID_ANY, "Remove");
	compositeButtons->Add(addCompositeButton, 1, wxRIGHT, FromDIP(4));
	compositeButtons->Add(removeCompositeButton, 1);
	midSizer->Add(compositeButtons, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
	wxFlexGridSizer* compositeForm = new wxFlexGridSizer(2, FromDIP(8), FromDIP(8));
	compositeForm->AddGrowableCol(1, 1);
	doodadCompositeChanceCtrl_ = CreateSpinField(panel, 0, 1000000);
	compositeForm->Add(new wxStaticText(panel, wxID_ANY, "Chance"), 0, wxALIGN_CENTER_VERTICAL);
	compositeForm->Add(doodadCompositeChanceCtrl_, 1, wxEXPAND);
	midSizer->Add(compositeForm, 0, wxEXPAND);

	wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);
	rightSizer->Add(CreateSectionLabel(panel, "Composite Tiles"), 0, wxBOTTOM, FromDIP(6));
	doodadTilesList_ = new wxListBox(panel, wxID_ANY);
	rightSizer->Add(doodadTilesList_, 1, wxEXPAND | wxBOTTOM, FromDIP(6));
	wxBoxSizer* tileButtons = new wxBoxSizer(wxHORIZONTAL);
	wxButton* addTileButton = new wxButton(panel, wxID_ANY, "Add Tile");
	wxButton* removeTileButton = new wxButton(panel, wxID_ANY, "Remove");
	tileButtons->Add(addTileButton, 1, wxRIGHT, FromDIP(4));
	tileButtons->Add(removeTileButton, 1);
	rightSizer->Add(tileButtons, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
	wxFlexGridSizer* offsetForm = new wxFlexGridSizer(2, FromDIP(8), FromDIP(8));
	offsetForm->AddGrowableCol(1, 1);
	doodadTileOffsetXCtrl_ = CreateSpinField(panel, -1000, 1000);
	doodadTileOffsetYCtrl_ = CreateSpinField(panel, -1000, 1000);
	doodadTileOffsetZCtrl_ = CreateSpinField(panel, -1000, 1000);
	offsetForm->Add(new wxStaticText(panel, wxID_ANY, "Offset X"), 0, wxALIGN_CENTER_VERTICAL);
	offsetForm->Add(doodadTileOffsetXCtrl_, 1, wxEXPAND);
	offsetForm->Add(new wxStaticText(panel, wxID_ANY, "Offset Y"), 0, wxALIGN_CENTER_VERTICAL);
	offsetForm->Add(doodadTileOffsetYCtrl_, 1, wxEXPAND);
	offsetForm->Add(new wxStaticText(panel, wxID_ANY, "Offset Z"), 0, wxALIGN_CENTER_VERTICAL);
	offsetForm->Add(doodadTileOffsetZCtrl_, 1, wxEXPAND);
	rightSizer->Add(offsetForm, 0, wxEXPAND | wxBOTTOM, FromDIP(12));
	rightSizer->Add(new wxStaticLine(panel), 0, wxEXPAND | wxBOTTOM, FromDIP(10));
	rightSizer->Add(CreateSectionLabel(panel, "Tile Items"), 0, wxBOTTOM, FromDIP(6));
	doodadTileItemsList_ = new wxListBox(panel, wxID_ANY);
	rightSizer->Add(doodadTileItemsList_, 1, wxEXPAND | wxBOTTOM, FromDIP(6));
	wxBoxSizer* tileItemButtons = new wxBoxSizer(wxHORIZONTAL);
	wxButton* addTileItemButton = new wxButton(panel, wxID_ANY, "Add Item");
	wxButton* removeTileItemButton = new wxButton(panel, wxID_ANY, "Remove");
	tileItemButtons->Add(addTileItemButton, 1, wxRIGHT, FromDIP(4));
	tileItemButtons->Add(removeTileItemButton, 1);
	rightSizer->Add(tileItemButtons, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
	wxFlexGridSizer* tileItemForm = new wxFlexGridSizer(2, FromDIP(8), FromDIP(8));
	tileItemForm->AddGrowableCol(1, 1);
	doodadTileItemIdCtrl_ = CreateItemIdSpinField(panel);
	tileItemForm->Add(new wxStaticText(panel, wxID_ANY, "Item ID"), 0, wxALIGN_CENTER_VERTICAL);
	tileItemForm->Add(doodadTileItemIdCtrl_, 1, wxEXPAND);
	rightSizer->Add(tileItemForm, 0, wxEXPAND);

	rootSizer->Add(altSizer, 0, wxEXPAND | wxRIGHT, FromDIP(10));
	rootSizer->Add(new wxStaticLine(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL), 0, wxEXPAND | wxRIGHT, FromDIP(10));
	rootSizer->Add(midSizer, 1, wxEXPAND | wxRIGHT, FromDIP(10));
	rootSizer->Add(new wxStaticLine(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL), 0, wxEXPAND | wxRIGHT, FromDIP(10));
	rootSizer->Add(rightSizer, 1, wxEXPAND);
	panel->SetSizer(rootSizer);

	addAltButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnAddDoodadAlternative, this);
	removeAltButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnRemoveDoodadAlternative, this);
	doodadAlternativesList_->Bind(wxEVT_LISTBOX, &MaterialsWorkbenchBrushPanel::OnDoodadAlternativeSelected, this);
	addSingleButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnAddDoodadSingleItem, this);
	removeSingleButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnRemoveDoodadSingleItem, this);
	doodadSingleItemsList_->Bind(wxEVT_LISTBOX, &MaterialsWorkbenchBrushPanel::OnDoodadSingleItemSelected, this);
	doodadSingleItemIdCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnDoodadSingleItemValueChanged, this);
	doodadSingleItemChanceCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnDoodadSingleItemValueChanged, this);
	doodadSingleItemIdCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnDoodadSingleItemValueChanged, this);
	doodadSingleItemChanceCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnDoodadSingleItemValueChanged, this);
	addCompositeButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnAddDoodadComposite, this);
	removeCompositeButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnRemoveDoodadComposite, this);
	doodadCompositesList_->Bind(wxEVT_LISTBOX, &MaterialsWorkbenchBrushPanel::OnDoodadCompositeSelected, this);
	doodadCompositeChanceCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnDoodadCompositeChanceChanged, this);
	doodadCompositeChanceCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnDoodadCompositeChanceChanged, this);
	addTileButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnAddDoodadTile, this);
	removeTileButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnRemoveDoodadTile, this);
	doodadTilesList_->Bind(wxEVT_LISTBOX, &MaterialsWorkbenchBrushPanel::OnDoodadTileSelected, this);
	doodadTileOffsetXCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnDoodadTileOffsetChanged, this);
	doodadTileOffsetYCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnDoodadTileOffsetChanged, this);
	doodadTileOffsetZCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnDoodadTileOffsetChanged, this);
	doodadTileOffsetXCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnDoodadTileOffsetChanged, this);
	doodadTileOffsetYCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnDoodadTileOffsetChanged, this);
	doodadTileOffsetZCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnDoodadTileOffsetChanged, this);
	addTileItemButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnAddDoodadTileItem, this);
	removeTileItemButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnRemoveDoodadTileItem, this);
	doodadTileItemsList_->Bind(wxEVT_LISTBOX, &MaterialsWorkbenchBrushPanel::OnDoodadTileItemSelected, this);
	doodadTileItemIdCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnDoodadTileItemValueChanged, this);
	doodadTileItemIdCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnDoodadTileItemValueChanged, this);
	return panel;
}

void MaterialsWorkbenchBrushPanel::ClearWorkspace(const wxString &message) {
	brushStorage_ = BrushStorageRecord();
	loadedBrushStorage_ = BrushStorageRecord();
	currentContextKey_.clear();
	currentItemIndex_ = -1;
	hasBrush_ = false;
	dirty_ = false;
	ResetVariationSelection();

	UpdateWorkspaceHeader();
	summaryLabel_->SetLabel(message);

	internalUpdate_ = true;
	idCtrl_->SetValue("");
	storageCtrl_->SetValue("");
	nameCtrl_->SetValue("");
	typeCtrl_->SetValue("");
	sourceCtrl_->SetValue("");
	lookIdCtrl_->SetValue(0);
	serverLookIdCtrl_->SetValue(0);
	zOrderCtrl_->SetValue(0);
	thicknessCtrl_->SetValue(0);
	thicknessCeilingCtrl_->SetValue(0);
	draggableCtrl_->SetValue(false);
	onBlockingCtrl_->SetValue(false);
	onDuplicateCtrl_->SetValue(false);
	redoBordersCtrl_->SetValue(false);
	randomizeCtrl_->SetValue(false);
	oneSizeCtrl_->SetValue(false);
	soloOptionalCtrl_->SetValue(false);
	internalUpdate_ = false;

	SetFieldsEnabled(false);
	UpdateActionButtons();
	RefreshVariationEditor();
	UpdateModifiedHighlights();
	NotifyBrushStateChanged();
	if (workspaceTabs_) {
		workspaceTabs_->SetSelection(0);
	}
	SetStatusMessage(message);
	Layout();
}

bool MaterialsWorkbenchBrushPanel::LoadBrush(const wxString &contextKey, int itemIndex) {
	const int64_t previousBrushId = hasBrush_ ? brushStorage_.brush.id : 0;
	const VariationEditorState previousVariationState = hasBrush_ ? CaptureVariationEditorState() : VariationEditorState();

	wxString error;
	BrushStorageRecord storage;
	if (!controller_.GetBrushDetails(contextKey, itemIndex, storage, error)) {
		spdlog::warn(
			"Materials Workbench failed to load brush details: context='{}' index={} error='{}'",
			contextKey.ToStdString(),
			itemIndex,
			error.ToStdString()
		);
		ClearWorkspace("Failed to load brush details: " + error);
		return false;
	}

	const bool preserveVariationState = previousVariationState.valid && previousBrushId == storage.brush.id;

	brushStorage_ = storage;
	loadedBrushStorage_ = storage;
	currentContextKey_ = contextKey;
	currentItemIndex_ = itemIndex;
	hasBrush_ = true;
	dirty_ = false;
	ResetVariationSelection();

	PopulateFields();
	if (previousVariationState.valid) {
		RestoreVariationEditorState(previousVariationState);
	}
	SetFieldsEnabled(true);
	UpdateActionButtons();
	UpdateModifiedHighlights();
	NotifyBrushStateChanged();
	SetStatusMessage("Brush loaded from materials.db.");
	spdlog::info(
		"Materials Workbench loaded brush from materials.db: id={} name='{}' type='{}' preserved_context={}",
		static_cast<long long>(brushStorage_.brush.id),
		brushStorage_.brush.name.ToStdString(),
		brushStorage_.brush.type.ToStdString(),
		previousVariationState.valid
	);
	Layout();
	return true;
}

void MaterialsWorkbenchBrushPanel::PopulateFields() {
	PopulateMetadataFields();
	UpdateSummary();
	RefreshVariationEditor();
}

void MaterialsWorkbenchBrushPanel::PopulateMetadataFields() {
	const BrushRecord &brush = brushStorage_.brush;

	UpdateWorkspaceHeader();

	internalUpdate_ = true;
	idCtrl_->SetValue(wxString::Format("%lld", static_cast<long long>(brush.id)));
	storageCtrl_->SetValue("materials.db");
	nameCtrl_->SetValue(brush.name);
	typeCtrl_->SetValue(brush.type);
	sourceCtrl_->SetValue(FormatImportedFromValue(brush.sourceFile));
	lookIdCtrl_->SetValue(brush.lookId);
	serverLookIdCtrl_->SetValue(brush.serverLookId);
	zOrderCtrl_->SetValue(brush.zOrder);
	thicknessCtrl_->SetValue(brush.thickness);
	thicknessCeilingCtrl_->SetValue(brush.thicknessCeiling);
	draggableCtrl_->SetValue(brush.draggable);
	onBlockingCtrl_->SetValue(brush.onBlocking);
	onDuplicateCtrl_->SetValue(brush.onDuplicate);
	redoBordersCtrl_->SetValue(brush.redoBorders);
	randomizeCtrl_->SetValue(brush.randomize);
	oneSizeCtrl_->SetValue(brush.oneSize);
	soloOptionalCtrl_->SetValue(brush.soloOptional);
	internalUpdate_ = false;
}

void MaterialsWorkbenchBrushPanel::UpdateSummary() {
	summaryLabel_->SetLabel(
		wxString::Format(
			"Items: %zu | Borders: %zu | Links: %zu | Wall parts: %zu | Carpet nodes: %zu | Table nodes: %zu | Doodad alternatives: %zu",
			brushStorage_.items.size(),
			brushStorage_.borders.size(),
			brushStorage_.links.size(),
			brushStorage_.wallParts.size(),
			brushStorage_.carpetNodes.size(),
			brushStorage_.tableNodes.size(),
			brushStorage_.doodadAlternatives.size()
		)
	);
}

void MaterialsWorkbenchBrushPanel::SetStatusMessage(const wxString &message) {
	statusLabel_->SetLabel(message);
}

BrushStorageRecord MaterialsWorkbenchBrushPanel::BuildEditableStorageFromCurrentState() const {
	BrushStorageRecord storage = brushStorage_;
	BrushRecord &brush = storage.brush;
	brush.name = TrimmedValue(nameCtrl_);
	brush.type = TrimmedValue(typeCtrl_);
	brush.sourceFile = ParseImportedFromEditorValue(TrimmedValue(sourceCtrl_));
	brush.lookId = lookIdCtrl_->GetValue();
	brush.serverLookId = serverLookIdCtrl_->GetValue();
	brush.zOrder = zOrderCtrl_->GetValue();
	brush.thickness = thicknessCtrl_->GetValue();
	brush.thicknessCeiling = thicknessCeilingCtrl_->GetValue();
	brush.draggable = draggableCtrl_->GetValue();
	brush.onBlocking = onBlockingCtrl_->GetValue();
	brush.onDuplicate = onDuplicateCtrl_->GetValue();
	brush.redoBorders = redoBordersCtrl_->GetValue();
	brush.randomize = randomizeCtrl_->GetValue();
	brush.oneSize = oneSizeCtrl_->GetValue();
	brush.soloOptional = soloOptionalCtrl_->GetValue();
	NormalizeBrushStorageForEditing(storage);
	return storage;
}

void MaterialsWorkbenchBrushPanel::RefreshDirtyState() {
	if (!hasBrush_) {
		dirty_ = false;
		UpdateWorkspaceHeader();
		UpdateActionButtons();
		UpdateModifiedHighlights();
		NotifyBrushStateChanged();
		return;
	}

	dirty_ = !AreBrushStorageRecordsEqual(BuildEditableStorageFromCurrentState(), loadedBrushStorage_);
	UpdateWorkspaceHeader();
	UpdateActionButtons();
	UpdateModifiedHighlights();
	NotifyBrushStateChanged();
}

void MaterialsWorkbenchBrushPanel::NotifyBrushStateChanged() {
	if (onBrushStateChanged_) {
		onBrushStateChanged_();
	}
}

void MaterialsWorkbenchBrushPanel::UpdateModifiedHighlights() {
	if (!hasBrush_) {
		ApplyModifiedEditorStyle(nameCtrl_, false);
		ApplyModifiedEditorStyle(typeCtrl_, false);
		ApplyModifiedEditorStyle(sourceCtrl_, false);
		ApplyModifiedEditorStyle(lookIdCtrl_, false);
		ApplyModifiedEditorStyle(serverLookIdCtrl_, false);
		ApplyModifiedEditorStyle(zOrderCtrl_, false);
		ApplyModifiedEditorStyle(thicknessCtrl_, false);
		ApplyModifiedEditorStyle(thicknessCeilingCtrl_, false);
		ApplyModifiedToggleStyle(draggableCtrl_, false);
		ApplyModifiedToggleStyle(onBlockingCtrl_, false);
		ApplyModifiedToggleStyle(onDuplicateCtrl_, false);
		ApplyModifiedToggleStyle(redoBordersCtrl_, false);
		ApplyModifiedToggleStyle(randomizeCtrl_, false);
		ApplyModifiedToggleStyle(oneSizeCtrl_, false);
		ApplyModifiedToggleStyle(soloOptionalCtrl_, false);
		ApplyModifiedLabelStyle(variationsStatusLabel_, "Variation Data", false);
		ApplyModifiedLabelStyle(alignedSectionLabel_, UsesAlignedVariationEditor() && GetEffectiveBrushType() == "table" ? "Table Nodes" : "Carpet Nodes", false);
		ApplyModifiedEditorStyle(groundItemsList_, false);
		ApplyModifiedEditorStyle(groundItemIdCtrl_, false);
		ApplyModifiedEditorStyle(groundItemChanceCtrl_, false);
		ApplyModifiedEditorStyle(alignedNodesList_, false);
		ApplyModifiedEditorStyle(alignedNodeAlignCtrl_, false);
		ApplyModifiedEditorStyle(alignedItemsList_, false);
		ApplyModifiedEditorStyle(alignedItemIdCtrl_, false);
		ApplyModifiedEditorStyle(alignedItemChanceCtrl_, false);
		ApplyModifiedEditorStyle(doodadAlternativesList_, false);
		ApplyModifiedEditorStyle(doodadSingleItemsList_, false);
		ApplyModifiedEditorStyle(doodadSingleItemIdCtrl_, false);
		ApplyModifiedEditorStyle(doodadSingleItemChanceCtrl_, false);
		ApplyModifiedEditorStyle(doodadCompositesList_, false);
		ApplyModifiedEditorStyle(doodadCompositeChanceCtrl_, false);
		ApplyModifiedEditorStyle(doodadTilesList_, false);
		ApplyModifiedEditorStyle(doodadTileOffsetXCtrl_, false);
		ApplyModifiedEditorStyle(doodadTileOffsetYCtrl_, false);
		ApplyModifiedEditorStyle(doodadTileOffsetZCtrl_, false);
		ApplyModifiedEditorStyle(doodadTileItemsList_, false);
		ApplyModifiedEditorStyle(doodadTileItemIdCtrl_, false);
		return;
	}

	const BrushStorageRecord editableStorage = BuildEditableStorageFromCurrentState();
	UpdateMetadataModifiedHighlights(editableStorage.brush);
	UpdateVariationModifiedHighlights(editableStorage);
}

void MaterialsWorkbenchBrushPanel::CommitVariationEditorState() {
	const wxString type = GetEffectiveBrushType();
	if (type == "ground") {
		const int selection = groundItemsList_ ? groundItemsList_->GetSelection() : wxNOT_FOUND;
		if (selection != wxNOT_FOUND && static_cast<size_t>(selection) < brushStorage_.items.size()) {
			brushStorage_.items[static_cast<size_t>(selection)].itemId = groundItemIdCtrl_->GetValue();
			brushStorage_.items[static_cast<size_t>(selection)].chance = groundItemChanceCtrl_->GetValue();
		}
		return;
	}

	if (type == "carpet") {
		const int nodeSelection = alignedNodesList_ ? alignedNodesList_->GetSelection() : wxNOT_FOUND;
		if (nodeSelection != wxNOT_FOUND && static_cast<size_t>(nodeSelection) < brushStorage_.carpetNodes.size()) {
			auto &node = brushStorage_.carpetNodes[static_cast<size_t>(nodeSelection)];
			node.align = alignedNodeAlignCtrl_->GetValue();
			const int itemSelection = alignedItemsList_ ? alignedItemsList_->GetSelection() : wxNOT_FOUND;
			if (itemSelection != wxNOT_FOUND && static_cast<size_t>(itemSelection) < node.items.size()) {
				node.items[static_cast<size_t>(itemSelection)].itemId = alignedItemIdCtrl_->GetValue();
				node.items[static_cast<size_t>(itemSelection)].chance = alignedItemChanceCtrl_->GetValue();
			}
		}
		return;
	}

	if (type == "table") {
		const int nodeSelection = alignedNodesList_ ? alignedNodesList_->GetSelection() : wxNOT_FOUND;
		if (nodeSelection != wxNOT_FOUND && static_cast<size_t>(nodeSelection) < brushStorage_.tableNodes.size()) {
			auto &node = brushStorage_.tableNodes[static_cast<size_t>(nodeSelection)];
			node.align = alignedNodeAlignCtrl_->GetValue();
			const int itemSelection = alignedItemsList_ ? alignedItemsList_->GetSelection() : wxNOT_FOUND;
			if (itemSelection != wxNOT_FOUND && static_cast<size_t>(itemSelection) < node.items.size()) {
				node.items[static_cast<size_t>(itemSelection)].itemId = alignedItemIdCtrl_->GetValue();
				node.items[static_cast<size_t>(itemSelection)].chance = alignedItemChanceCtrl_->GetValue();
			}
		}
		return;
	}

	if (type == "doodad") {
		const int alternativeSelection = doodadAlternativesList_ ? doodadAlternativesList_->GetSelection() : wxNOT_FOUND;
		if (alternativeSelection == wxNOT_FOUND || static_cast<size_t>(alternativeSelection) >= brushStorage_.doodadAlternatives.size()) {
			return;
		}

		auto &alternative = brushStorage_.doodadAlternatives[static_cast<size_t>(alternativeSelection)];
		const int singleSelection = doodadSingleItemsList_ ? doodadSingleItemsList_->GetSelection() : wxNOT_FOUND;
		if (singleSelection != wxNOT_FOUND && static_cast<size_t>(singleSelection) < alternative.singleItems.size()) {
			alternative.singleItems[static_cast<size_t>(singleSelection)].itemId = doodadSingleItemIdCtrl_->GetValue();
			alternative.singleItems[static_cast<size_t>(singleSelection)].chance = doodadSingleItemChanceCtrl_->GetValue();
		}

		const int compositeSelection = doodadCompositesList_ ? doodadCompositesList_->GetSelection() : wxNOT_FOUND;
		if (compositeSelection != wxNOT_FOUND && static_cast<size_t>(compositeSelection) < alternative.composites.size()) {
			auto &composite = alternative.composites[static_cast<size_t>(compositeSelection)];
			composite.chance = doodadCompositeChanceCtrl_->GetValue();

			const int tileSelection = doodadTilesList_ ? doodadTilesList_->GetSelection() : wxNOT_FOUND;
			if (tileSelection != wxNOT_FOUND && static_cast<size_t>(tileSelection) < composite.tiles.size()) {
				auto &tile = composite.tiles[static_cast<size_t>(tileSelection)];
				tile.offsetX = doodadTileOffsetXCtrl_->GetValue();
				tile.offsetY = doodadTileOffsetYCtrl_->GetValue();
				tile.offsetZ = doodadTileOffsetZCtrl_->GetValue();

				const int tileItemSelection = doodadTileItemsList_ ? doodadTileItemsList_->GetSelection() : wxNOT_FOUND;
				if (tileItemSelection != wxNOT_FOUND && static_cast<size_t>(tileItemSelection) < tile.items.size()) {
					tile.items[static_cast<size_t>(tileItemSelection)].itemId = doodadTileItemIdCtrl_->GetValue();
				}
			}
		}
	}
}

void MaterialsWorkbenchBrushPanel::UpdateMetadataModifiedHighlights(const BrushRecord &editableBrush) {
	ApplyModifiedEditorStyle(nameCtrl_, editableBrush.name != loadedBrushStorage_.brush.name);
	ApplyModifiedEditorStyle(typeCtrl_, editableBrush.type != loadedBrushStorage_.brush.type);
	ApplyModifiedEditorStyle(sourceCtrl_, editableBrush.sourceFile != loadedBrushStorage_.brush.sourceFile);
	ApplyModifiedEditorStyle(lookIdCtrl_, editableBrush.lookId != loadedBrushStorage_.brush.lookId);
	ApplyModifiedEditorStyle(serverLookIdCtrl_, editableBrush.serverLookId != loadedBrushStorage_.brush.serverLookId);
	ApplyModifiedEditorStyle(zOrderCtrl_, editableBrush.zOrder != loadedBrushStorage_.brush.zOrder);
	ApplyModifiedEditorStyle(thicknessCtrl_, editableBrush.thickness != loadedBrushStorage_.brush.thickness);
	ApplyModifiedEditorStyle(thicknessCeilingCtrl_, editableBrush.thicknessCeiling != loadedBrushStorage_.brush.thicknessCeiling);
	ApplyModifiedToggleStyle(draggableCtrl_, editableBrush.draggable != loadedBrushStorage_.brush.draggable);
	ApplyModifiedToggleStyle(onBlockingCtrl_, editableBrush.onBlocking != loadedBrushStorage_.brush.onBlocking);
	ApplyModifiedToggleStyle(onDuplicateCtrl_, editableBrush.onDuplicate != loadedBrushStorage_.brush.onDuplicate);
	ApplyModifiedToggleStyle(redoBordersCtrl_, editableBrush.redoBorders != loadedBrushStorage_.brush.redoBorders);
	ApplyModifiedToggleStyle(randomizeCtrl_, editableBrush.randomize != loadedBrushStorage_.brush.randomize);
	ApplyModifiedToggleStyle(oneSizeCtrl_, editableBrush.oneSize != loadedBrushStorage_.brush.oneSize);
	ApplyModifiedToggleStyle(soloOptionalCtrl_, editableBrush.soloOptional != loadedBrushStorage_.brush.soloOptional);
}

void MaterialsWorkbenchBrushPanel::UpdateVariationModifiedHighlights(const BrushStorageRecord &editableStorage) {
	const bool groundModified = !VectorsEqual(editableStorage.items, loadedBrushStorage_.items, AreBrushItemRecordsEqual);
	const bool carpetModified = !VectorsEqual(editableStorage.carpetNodes, loadedBrushStorage_.carpetNodes, AreCarpetNodeRecordsEqual);
	const bool tableModified = !VectorsEqual(editableStorage.tableNodes, loadedBrushStorage_.tableNodes, AreTableNodeRecordsEqual);
	const bool doodadModified = !VectorsEqual(editableStorage.doodadAlternatives, loadedBrushStorage_.doodadAlternatives, AreDoodadAlternativeRecordsEqual);
	const bool variationsModified = groundModified || carpetModified || tableModified || doodadModified;

	ApplyModifiedLabelStyle(variationsStatusLabel_, "Variation Data", variationsModified);
	ApplyModifiedEditorStyle(groundItemsList_, groundModified);
	ApplyModifiedEditorStyle(groundItemIdCtrl_, groundModified);
	ApplyModifiedEditorStyle(groundItemChanceCtrl_, groundModified);

	const bool alignedModified = GetEffectiveBrushType() == "table" ? tableModified : carpetModified;
	ApplyModifiedLabelStyle(alignedSectionLabel_, GetEffectiveBrushType() == "table" ? "Table Nodes" : "Carpet Nodes", alignedModified);
	ApplyModifiedEditorStyle(alignedNodesList_, alignedModified);
	ApplyModifiedEditorStyle(alignedNodeAlignCtrl_, alignedModified);
	ApplyModifiedEditorStyle(alignedItemsList_, alignedModified);
	ApplyModifiedEditorStyle(alignedItemIdCtrl_, alignedModified);
	ApplyModifiedEditorStyle(alignedItemChanceCtrl_, alignedModified);

	ApplyModifiedEditorStyle(doodadAlternativesList_, doodadModified);
	ApplyModifiedEditorStyle(doodadSingleItemsList_, doodadModified);
	ApplyModifiedEditorStyle(doodadSingleItemIdCtrl_, doodadModified);
	ApplyModifiedEditorStyle(doodadSingleItemChanceCtrl_, doodadModified);
	ApplyModifiedEditorStyle(doodadCompositesList_, doodadModified);
	ApplyModifiedEditorStyle(doodadCompositeChanceCtrl_, doodadModified);
	ApplyModifiedEditorStyle(doodadTilesList_, doodadModified);
	ApplyModifiedEditorStyle(doodadTileOffsetXCtrl_, doodadModified);
	ApplyModifiedEditorStyle(doodadTileOffsetYCtrl_, doodadModified);
	ApplyModifiedEditorStyle(doodadTileOffsetZCtrl_, doodadModified);
	ApplyModifiedEditorStyle(doodadTileItemsList_, doodadModified);
	ApplyModifiedEditorStyle(doodadTileItemIdCtrl_, doodadModified);
}

MaterialsWorkbenchBrushPanel::VariationEditorState MaterialsWorkbenchBrushPanel::CaptureVariationEditorState() const {
	VariationEditorState state;
	state.valid = hasBrush_;
	state.workspaceTabSelection = workspaceTabs_ ? workspaceTabs_->GetSelection() : 0;
	if (metadataPage_) {
		metadataPage_->GetViewStart(&state.metadataViewX, &state.metadataViewY);
	}
	state.groundItemIndex = groundItemIndex_;
	state.alignedNodeIndex = alignedNodeIndex_;
	state.alignedItemIndex = alignedItemIndex_;
	state.doodadAlternativeIndex = doodadAlternativeIndex_;
	state.doodadSingleItemIndex = doodadSingleItemIndex_;
	state.doodadCompositeIndex = doodadCompositeIndex_;
	state.doodadTileIndex = doodadTileIndex_;
	state.doodadTileItemIndex = doodadTileItemIndex_;
	state.groundTopItem = CaptureListTopItem(groundItemsList_);
	state.alignedNodesTopItem = CaptureListTopItem(alignedNodesList_);
	state.alignedItemsTopItem = CaptureListTopItem(alignedItemsList_);
	state.doodadAlternativesTopItem = CaptureListTopItem(doodadAlternativesList_);
	state.doodadSingleItemsTopItem = CaptureListTopItem(doodadSingleItemsList_);
	state.doodadCompositesTopItem = CaptureListTopItem(doodadCompositesList_);
	state.doodadTilesTopItem = CaptureListTopItem(doodadTilesList_);
	state.doodadTileItemsTopItem = CaptureListTopItem(doodadTileItemsList_);
	return state;
}

void MaterialsWorkbenchBrushPanel::RestoreVariationEditorState(const VariationEditorState &state) {
	if (!state.valid || !hasBrush_) {
		return;
	}

	if (workspaceTabs_ && workspaceTabs_->GetPageCount() > 0) {
		const int tabSelection = std::min<int>(std::max(0, state.workspaceTabSelection), static_cast<int>(workspaceTabs_->GetPageCount()) - 1);
		workspaceTabs_->SetSelection(tabSelection);
	}

	if (metadataPage_ && state.metadataViewX != -1 && state.metadataViewY != -1) {
		metadataPage_->Scroll(state.metadataViewX, state.metadataViewY);
	}

	groundItemIndex_ = ClampIndexForCount(state.groundItemIndex, brushStorage_.items.size());

	if (GetEffectiveBrushType() == "carpet") {
		alignedNodeIndex_ = ClampIndexForCount(state.alignedNodeIndex, brushStorage_.carpetNodes.size());
		if (alignedNodeIndex_ >= 0) {
			alignedItemIndex_ = ClampIndexForCount(state.alignedItemIndex, brushStorage_.carpetNodes[alignedNodeIndex_].items.size());
		} else {
			alignedItemIndex_ = -1;
		}
	} else if (GetEffectiveBrushType() == "table") {
		alignedNodeIndex_ = ClampIndexForCount(state.alignedNodeIndex, brushStorage_.tableNodes.size());
		if (alignedNodeIndex_ >= 0) {
			alignedItemIndex_ = ClampIndexForCount(state.alignedItemIndex, brushStorage_.tableNodes[alignedNodeIndex_].items.size());
		} else {
			alignedItemIndex_ = -1;
		}
	} else {
		alignedNodeIndex_ = -1;
		alignedItemIndex_ = -1;
	}

	doodadAlternativeIndex_ = ClampIndexForCount(state.doodadAlternativeIndex, brushStorage_.doodadAlternatives.size());
	if (doodadAlternativeIndex_ >= 0) {
		const auto &alternative = brushStorage_.doodadAlternatives[doodadAlternativeIndex_];
		doodadSingleItemIndex_ = ClampIndexForCount(state.doodadSingleItemIndex, alternative.singleItems.size());
		doodadCompositeIndex_ = ClampIndexForCount(state.doodadCompositeIndex, alternative.composites.size());
		if (doodadCompositeIndex_ >= 0) {
			const auto &composite = alternative.composites[doodadCompositeIndex_];
			doodadTileIndex_ = ClampIndexForCount(state.doodadTileIndex, composite.tiles.size());
			if (doodadTileIndex_ >= 0) {
				doodadTileItemIndex_ = ClampIndexForCount(state.doodadTileItemIndex, composite.tiles[doodadTileIndex_].items.size());
			} else {
				doodadTileItemIndex_ = -1;
			}
		} else {
			doodadTileIndex_ = -1;
			doodadTileItemIndex_ = -1;
		}
	} else {
		doodadSingleItemIndex_ = -1;
		doodadCompositeIndex_ = -1;
		doodadTileIndex_ = -1;
		doodadTileItemIndex_ = -1;
	}

	RefreshVariationEditor();

	RestoreListTopItem(groundItemsList_, state.groundTopItem);
	RestoreListTopItem(alignedNodesList_, state.alignedNodesTopItem);
	RestoreListTopItem(alignedItemsList_, state.alignedItemsTopItem);
	RestoreListTopItem(doodadAlternativesList_, state.doodadAlternativesTopItem);
	RestoreListTopItem(doodadSingleItemsList_, state.doodadSingleItemsTopItem);
	RestoreListTopItem(doodadCompositesList_, state.doodadCompositesTopItem);
	RestoreListTopItem(doodadTilesList_, state.doodadTilesTopItem);
	RestoreListTopItem(doodadTileItemsList_, state.doodadTileItemsTopItem);
}

void MaterialsWorkbenchBrushPanel::UpdateWorkspaceHeader() {
	if (!hasBrush_) {
		titleLabel_->SetLabel("No brush selected");
		subtitleLabel_->SetLabel("Select a brush in the navigation tree to edit its properties.");
		return;
	}

	const wxString modifiedSuffix = dirty_ ? " [modified]" : "";
	const wxString displayName = hasBrush_ ? TrimmedValue(nameCtrl_) : "";
	titleLabel_->SetLabel("Editing brush: " + (displayName.IsEmpty() ? brushStorage_.brush.name : displayName) + modifiedSuffix);
	subtitleLabel_->SetLabel(
		dirty_
			? "Local edits differ from materials.db. Save, revert or switch brushes carefully."
			: "Update metadata and variations for the SQLite-backed brush without leaving the main workbench flow."
	);
}

void MaterialsWorkbenchBrushPanel::UpdateActionButtons() {
	if (saveButton_) {
		saveButton_->Enable(hasBrush_ && dirty_);
	}
	if (revertButton_) {
		revertButton_->Enable(hasBrush_ && dirty_);
	}
}

void MaterialsWorkbenchBrushPanel::SetFieldsEnabled(bool enabled) {
	nameCtrl_->Enable(enabled);
	typeCtrl_->Enable(enabled);
	sourceCtrl_->Enable(enabled);
	lookIdCtrl_->Enable(enabled);
	serverLookIdCtrl_->Enable(enabled);
	zOrderCtrl_->Enable(enabled);
	thicknessCtrl_->Enable(enabled);
	thicknessCeilingCtrl_->Enable(enabled);
	draggableCtrl_->Enable(enabled);
	onBlockingCtrl_->Enable(enabled);
	onDuplicateCtrl_->Enable(enabled);
	redoBordersCtrl_->Enable(enabled);
	randomizeCtrl_->Enable(enabled);
	oneSizeCtrl_->Enable(enabled);
	soloOptionalCtrl_->Enable(enabled);
	if (workspaceTabs_) {
		workspaceTabs_->Enable(enabled);
	}
}

void MaterialsWorkbenchBrushPanel::ResetVariationSelection() {
	groundItemIndex_ = -1;
	alignedNodeIndex_ = -1;
	alignedItemIndex_ = -1;
	doodadAlternativeIndex_ = -1;
	doodadSingleItemIndex_ = -1;
	doodadCompositeIndex_ = -1;
	doodadTileIndex_ = -1;
	doodadTileItemIndex_ = -1;
}

wxString MaterialsWorkbenchBrushPanel::GetEffectiveBrushType() const {
	wxString type = hasBrush_ ? TrimmedValue(typeCtrl_) : "";
	if (type.IsEmpty()) {
		type = brushStorage_.brush.type;
	}
	type.MakeLower();
	return type;
}

bool MaterialsWorkbenchBrushPanel::UsesGroundVariationEditor() const {
	return GetEffectiveBrushType() == "ground";
}

bool MaterialsWorkbenchBrushPanel::UsesAlignedVariationEditor() const {
	const wxString type = GetEffectiveBrushType();
	return type == "carpet" || type == "table";
}

bool MaterialsWorkbenchBrushPanel::UsesDoodadVariationEditor() const {
	return GetEffectiveBrushType() == "doodad";
}

void MaterialsWorkbenchBrushPanel::RefreshVariationEditor() {
	if (!variationsBook_) {
		return;
	}

	if (!hasBrush_) {
		variationsEmptyLabel_->SetLabel("Select a brush to inspect variation data.");
		variationsBook_->SetSelection(0);
		return;
	}

	if (UsesGroundVariationEditor()) {
		variationsBook_->SetSelection(1);
		RefreshGroundItemList();
		RefreshGroundSelection();
		return;
	}

	if (UsesAlignedVariationEditor()) {
		alignedSectionLabel_->SetLabel(GetEffectiveBrushType() == "carpet" ? "Carpet Nodes" : "Table Nodes");
		variationsBook_->SetSelection(2);
		RefreshAlignedNodeList();
		RefreshAlignedSelection();
		return;
	}

	if (UsesDoodadVariationEditor()) {
		variationsBook_->SetSelection(3);
		RefreshDoodadAlternativeList();
		RefreshDoodadSelection();
		return;
	}

	variationsEmptyLabel_->SetLabel(
		"Variations are not yet exposed for '" + GetEffectiveBrushType() + "' brushes in this first workbench pass."
	);
	variationsBook_->SetSelection(0);
}

void MaterialsWorkbenchBrushPanel::RefreshGroundItemList() {
	if (!groundItemsList_) {
		return;
	}

	const int topItem = CaptureListTopItem(groundItemsList_);
	groundItemsList_->Clear();
	for (size_t i = 0; i < brushStorage_.items.size(); ++i) {
		groundItemsList_->Append(FormatGroundItemLabel(brushStorage_.items[i].itemId, brushStorage_.items[i].chance, i));
	}

	if (brushStorage_.items.empty()) {
		groundItemIndex_ = -1;
	} else if (groundItemIndex_ < 0 || groundItemIndex_ >= static_cast<int>(brushStorage_.items.size())) {
		groundItemIndex_ = 0;
	}

	if (groundItemIndex_ >= 0) {
		groundItemsList_->SetSelection(groundItemIndex_);
	}
	RestoreListTopItem(groundItemsList_, topItem);
}

void MaterialsWorkbenchBrushPanel::RefreshGroundSelection() {
	internalUpdate_ = true;

	const bool hasItem = groundItemIndex_ >= 0 && groundItemIndex_ < static_cast<int>(brushStorage_.items.size());
	groundItemIdCtrl_->Enable(hasItem);
	groundItemChanceCtrl_->Enable(hasItem);

	if (hasItem) {
		groundItemIdCtrl_->SetValue(brushStorage_.items[groundItemIndex_].itemId);
		groundItemChanceCtrl_->SetValue(brushStorage_.items[groundItemIndex_].chance);
	} else {
		groundItemIdCtrl_->SetValue(0);
		groundItemChanceCtrl_->SetValue(0);
	}

	internalUpdate_ = false;
}

void MaterialsWorkbenchBrushPanel::RefreshAlignedNodeList() {
	if (!alignedNodesList_) {
		return;
	}

	const int topItem = CaptureListTopItem(alignedNodesList_);
	alignedNodesList_->Clear();
	if (GetEffectiveBrushType() == "carpet") {
		const auto &nodes = brushStorage_.carpetNodes;
		for (size_t i = 0; i < nodes.size(); ++i) {
			alignedNodesList_->Append(FormatAlignedNodeLabel(nodes[i].align, nodes[i].items.size(), i));
		}

		if (nodes.empty()) {
			alignedNodeIndex_ = -1;
		} else if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			alignedNodeIndex_ = 0;
		}
	} else {
		const auto &nodes = brushStorage_.tableNodes;
		for (size_t i = 0; i < nodes.size(); ++i) {
			alignedNodesList_->Append(FormatAlignedNodeLabel(nodes[i].align, nodes[i].items.size(), i));
		}

		if (nodes.empty()) {
			alignedNodeIndex_ = -1;
		} else if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			alignedNodeIndex_ = 0;
		}
	}

	if (alignedNodeIndex_ >= 0) {
		alignedNodesList_->SetSelection(alignedNodeIndex_);
	}
	RestoreListTopItem(alignedNodesList_, topItem);
}

void MaterialsWorkbenchBrushPanel::RefreshAlignedItemList() {
	if (!alignedItemsList_) {
		return;
	}

	const int topItem = CaptureListTopItem(alignedItemsList_);
	alignedItemsList_->Clear();
	if (GetEffectiveBrushType() == "carpet") {
		const auto &nodes = brushStorage_.carpetNodes;
		if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			alignedItemIndex_ = -1;
			return;
		}

		const auto &items = nodes[alignedNodeIndex_].items;
		for (size_t i = 0; i < items.size(); ++i) {
			alignedItemsList_->Append(FormatAlignedItemLabel(items[i].itemId, items[i].chance, i));
		}

		if (items.empty()) {
			alignedItemIndex_ = -1;
		} else if (alignedItemIndex_ < 0 || alignedItemIndex_ >= static_cast<int>(items.size())) {
			alignedItemIndex_ = 0;
		}
	} else {
		const auto &nodes = brushStorage_.tableNodes;
		if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			alignedItemIndex_ = -1;
			return;
		}

		const auto &items = nodes[alignedNodeIndex_].items;
		for (size_t i = 0; i < items.size(); ++i) {
			alignedItemsList_->Append(FormatAlignedItemLabel(items[i].itemId, items[i].chance, i));
		}

		if (items.empty()) {
			alignedItemIndex_ = -1;
		} else if (alignedItemIndex_ < 0 || alignedItemIndex_ >= static_cast<int>(items.size())) {
			alignedItemIndex_ = 0;
		}
	}

	if (alignedItemIndex_ >= 0) {
		alignedItemsList_->SetSelection(alignedItemIndex_);
	}
	RestoreListTopItem(alignedItemsList_, topItem);
}

void MaterialsWorkbenchBrushPanel::RefreshAlignedSelection() {
	internalUpdate_ = true;
	bool hasNode = false;
	if (GetEffectiveBrushType() == "carpet") {
		const auto &nodes = brushStorage_.carpetNodes;
		hasNode = alignedNodeIndex_ >= 0 && alignedNodeIndex_ < static_cast<int>(nodes.size());
		alignedNodeAlignCtrl_->Enable(hasNode);
		if (hasNode) {
			alignedNodeAlignCtrl_->SetValue(nodes[alignedNodeIndex_].align);
		} else {
			alignedNodeAlignCtrl_->SetValue("");
			alignedItemIndex_ = -1;
		}
	} else {
		const auto &nodes = brushStorage_.tableNodes;
		hasNode = alignedNodeIndex_ >= 0 && alignedNodeIndex_ < static_cast<int>(nodes.size());
		alignedNodeAlignCtrl_->Enable(hasNode);
		if (hasNode) {
			alignedNodeAlignCtrl_->SetValue(nodes[alignedNodeIndex_].align);
		} else {
			alignedNodeAlignCtrl_->SetValue("");
			alignedItemIndex_ = -1;
		}
	}

	RefreshAlignedItemList();

	bool hasItem = false;
	if (hasNode) {
		if (GetEffectiveBrushType() == "carpet") {
			const auto &items = brushStorage_.carpetNodes[alignedNodeIndex_].items;
			hasItem = alignedItemIndex_ >= 0 && alignedItemIndex_ < static_cast<int>(items.size());
			if (hasItem) {
				alignedItemIdCtrl_->SetValue(items[alignedItemIndex_].itemId);
				alignedItemChanceCtrl_->SetValue(items[alignedItemIndex_].chance);
			}
		} else {
			const auto &items = brushStorage_.tableNodes[alignedNodeIndex_].items;
			hasItem = alignedItemIndex_ >= 0 && alignedItemIndex_ < static_cast<int>(items.size());
			if (hasItem) {
				alignedItemIdCtrl_->SetValue(items[alignedItemIndex_].itemId);
				alignedItemChanceCtrl_->SetValue(items[alignedItemIndex_].chance);
			}
		}
	}
	if (!hasItem) {
		alignedItemIdCtrl_->SetValue(0);
		alignedItemChanceCtrl_->SetValue(0);
	}
	alignedItemIdCtrl_->Enable(hasItem);
	alignedItemChanceCtrl_->Enable(hasItem);
	internalUpdate_ = false;
}

void MaterialsWorkbenchBrushPanel::RefreshDoodadAlternativeList() {
	if (!doodadAlternativesList_) {
		return;
	}

	const int topItem = CaptureListTopItem(doodadAlternativesList_);
	doodadAlternativesList_->Clear();
	for (size_t i = 0; i < brushStorage_.doodadAlternatives.size(); ++i) {
		doodadAlternativesList_->Append(FormatDoodadAlternativeLabel(brushStorage_.doodadAlternatives[i], i));
	}

	if (brushStorage_.doodadAlternatives.empty()) {
		doodadAlternativeIndex_ = -1;
	} else if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		doodadAlternativeIndex_ = 0;
	}

	if (doodadAlternativeIndex_ >= 0) {
		doodadAlternativesList_->SetSelection(doodadAlternativeIndex_);
	}
	RestoreListTopItem(doodadAlternativesList_, topItem);
}

void MaterialsWorkbenchBrushPanel::RefreshDoodadSingleItemList() {
	const int topItem = CaptureListTopItem(doodadSingleItemsList_);
	doodadSingleItemsList_->Clear();
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		doodadSingleItemIndex_ = -1;
		return;
	}

	const auto &items = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].singleItems;
	for (size_t i = 0; i < items.size(); ++i) {
		doodadSingleItemsList_->Append(FormatDoodadSingleItemLabel(items[i].itemId, items[i].chance, i));
	}

	if (items.empty()) {
		doodadSingleItemIndex_ = -1;
	} else if (doodadSingleItemIndex_ < 0 || doodadSingleItemIndex_ >= static_cast<int>(items.size())) {
		doodadSingleItemIndex_ = 0;
	}

	if (doodadSingleItemIndex_ >= 0) {
		doodadSingleItemsList_->SetSelection(doodadSingleItemIndex_);
	}
	RestoreListTopItem(doodadSingleItemsList_, topItem);
}

void MaterialsWorkbenchBrushPanel::RefreshDoodadCompositeList() {
	const int topItem = CaptureListTopItem(doodadCompositesList_);
	doodadCompositesList_->Clear();
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		doodadCompositeIndex_ = -1;
		return;
	}

	const auto &composites = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].composites;
	for (size_t i = 0; i < composites.size(); ++i) {
		doodadCompositesList_->Append(FormatDoodadCompositeLabel(composites[i], i));
	}

	if (composites.empty()) {
		doodadCompositeIndex_ = -1;
	} else if (doodadCompositeIndex_ < 0 || doodadCompositeIndex_ >= static_cast<int>(composites.size())) {
		doodadCompositeIndex_ = 0;
	}

	if (doodadCompositeIndex_ >= 0) {
		doodadCompositesList_->SetSelection(doodadCompositeIndex_);
	}
	RestoreListTopItem(doodadCompositesList_, topItem);
}

void MaterialsWorkbenchBrushPanel::RefreshDoodadTileList() {
	const int topItem = CaptureListTopItem(doodadTilesList_);
	doodadTilesList_->Clear();
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		doodadTileIndex_ = -1;
		return;
	}
	const auto &alternative = brushStorage_.doodadAlternatives[doodadAlternativeIndex_];
	if (doodadCompositeIndex_ < 0 || doodadCompositeIndex_ >= static_cast<int>(alternative.composites.size())) {
		doodadTileIndex_ = -1;
		return;
	}

	const auto &tiles = alternative.composites[doodadCompositeIndex_].tiles;
	for (size_t i = 0; i < tiles.size(); ++i) {
		doodadTilesList_->Append(FormatDoodadTileLabel(tiles[i], i));
	}

	if (tiles.empty()) {
		doodadTileIndex_ = -1;
	} else if (doodadTileIndex_ < 0 || doodadTileIndex_ >= static_cast<int>(tiles.size())) {
		doodadTileIndex_ = 0;
	}

	if (doodadTileIndex_ >= 0) {
		doodadTilesList_->SetSelection(doodadTileIndex_);
	}
	RestoreListTopItem(doodadTilesList_, topItem);
}

void MaterialsWorkbenchBrushPanel::RefreshDoodadTileItemList() {
	const int topItem = CaptureListTopItem(doodadTileItemsList_);
	doodadTileItemsList_->Clear();
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		doodadTileItemIndex_ = -1;
		return;
	}
	const auto &alternative = brushStorage_.doodadAlternatives[doodadAlternativeIndex_];
	if (doodadCompositeIndex_ < 0 || doodadCompositeIndex_ >= static_cast<int>(alternative.composites.size())) {
		doodadTileItemIndex_ = -1;
		return;
	}
	const auto &composite = alternative.composites[doodadCompositeIndex_];
	if (doodadTileIndex_ < 0 || doodadTileIndex_ >= static_cast<int>(composite.tiles.size())) {
		doodadTileItemIndex_ = -1;
		return;
	}

	const auto &items = composite.tiles[doodadTileIndex_].items;
	for (size_t i = 0; i < items.size(); ++i) {
		doodadTileItemsList_->Append(FormatDoodadTileItemLabel(items[i].itemId, i));
	}

	if (items.empty()) {
		doodadTileItemIndex_ = -1;
	} else if (doodadTileItemIndex_ < 0 || doodadTileItemIndex_ >= static_cast<int>(items.size())) {
		doodadTileItemIndex_ = 0;
	}

	if (doodadTileItemIndex_ >= 0) {
		doodadTileItemsList_->SetSelection(doodadTileItemIndex_);
	}
	RestoreListTopItem(doodadTileItemsList_, topItem);
}

void MaterialsWorkbenchBrushPanel::RefreshDoodadSelection() {
	internalUpdate_ = true;

	RefreshDoodadSingleItemList();
	RefreshDoodadCompositeList();
	RefreshDoodadTileList();
	RefreshDoodadTileItemList();

	const bool hasAlternative = doodadAlternativeIndex_ >= 0 && doodadAlternativeIndex_ < static_cast<int>(brushStorage_.doodadAlternatives.size());
	bool hasSingleItem = false;
	bool hasComposite = false;
	bool hasTile = false;
	bool hasTileItem = false;

	if (hasAlternative) {
		const auto &alternative = brushStorage_.doodadAlternatives[doodadAlternativeIndex_];
		hasSingleItem = doodadSingleItemIndex_ >= 0 && doodadSingleItemIndex_ < static_cast<int>(alternative.singleItems.size());
		hasComposite = doodadCompositeIndex_ >= 0 && doodadCompositeIndex_ < static_cast<int>(alternative.composites.size());

		if (hasSingleItem) {
			const auto &singleItem = alternative.singleItems[doodadSingleItemIndex_];
			doodadSingleItemIdCtrl_->SetValue(singleItem.itemId);
			doodadSingleItemChanceCtrl_->SetValue(singleItem.chance);
		} else {
			doodadSingleItemIdCtrl_->SetValue(0);
			doodadSingleItemChanceCtrl_->SetValue(0);
		}

		if (hasComposite) {
			const auto &composite = alternative.composites[doodadCompositeIndex_];
			doodadCompositeChanceCtrl_->SetValue(composite.chance);
			hasTile = doodadTileIndex_ >= 0 && doodadTileIndex_ < static_cast<int>(composite.tiles.size());
			if (hasTile) {
				const auto &tile = composite.tiles[doodadTileIndex_];
				doodadTileOffsetXCtrl_->SetValue(tile.offsetX);
				doodadTileOffsetYCtrl_->SetValue(tile.offsetY);
				doodadTileOffsetZCtrl_->SetValue(tile.offsetZ);
				hasTileItem = doodadTileItemIndex_ >= 0 && doodadTileItemIndex_ < static_cast<int>(tile.items.size());
				if (hasTileItem) {
					doodadTileItemIdCtrl_->SetValue(tile.items[doodadTileItemIndex_].itemId);
				} else {
					doodadTileItemIdCtrl_->SetValue(0);
				}
			} else {
				doodadTileOffsetXCtrl_->SetValue(0);
				doodadTileOffsetYCtrl_->SetValue(0);
				doodadTileOffsetZCtrl_->SetValue(0);
				doodadTileItemIdCtrl_->SetValue(0);
			}
		} else {
			doodadCompositeChanceCtrl_->SetValue(0);
			doodadTileOffsetXCtrl_->SetValue(0);
			doodadTileOffsetYCtrl_->SetValue(0);
			doodadTileOffsetZCtrl_->SetValue(0);
			doodadTileItemIdCtrl_->SetValue(0);
		}
	} else {
		doodadSingleItemIdCtrl_->SetValue(0);
		doodadSingleItemChanceCtrl_->SetValue(0);
		doodadCompositeChanceCtrl_->SetValue(0);
		doodadTileOffsetXCtrl_->SetValue(0);
		doodadTileOffsetYCtrl_->SetValue(0);
		doodadTileOffsetZCtrl_->SetValue(0);
		doodadTileItemIdCtrl_->SetValue(0);
	}

	doodadSingleItemIdCtrl_->Enable(hasSingleItem);
	doodadSingleItemChanceCtrl_->Enable(hasSingleItem);
	doodadCompositeChanceCtrl_->Enable(hasComposite);
	doodadTileOffsetXCtrl_->Enable(hasTile);
	doodadTileOffsetYCtrl_->Enable(hasTile);
	doodadTileOffsetZCtrl_->Enable(hasTile);
	doodadTileItemIdCtrl_->Enable(hasTileItem);

	internalUpdate_ = false;
}

void MaterialsWorkbenchBrushPanel::NormalizeVariationSortOrders() {
	NormalizeVariationSortOrdersForStorage(brushStorage_);
}

bool MaterialsWorkbenchBrushPanel::ValidateBrushStorage(wxString &error) const {
	const BrushRecord &brush = brushStorage_.brush;
	if (brush.name.IsEmpty()) {
		error = "Brush name cannot be empty.";
		return false;
	}
	if (brush.type.IsEmpty()) {
		error = "Brush type cannot be empty.";
		return false;
	}

	const wxString type = brush.type.Lower();
	if (!IsValidBrushEditorType(type)) {
		error = "Brush type must be one of: ground, carpet, table or doodad.";
		return false;
	}

	if (brush.lookId < 0) {
		error = "lookId cannot be negative.";
		return false;
	}
	if (brush.lookId > 0 && !IsKnownItemId(brush.lookId)) {
		error = wxString::Format("lookId uses unknown item id %d.", brush.lookId);
		return false;
	}
	if (brush.serverLookId < 0) {
		error = "serverLookId cannot be negative.";
		return false;
	}
	if (brush.serverLookId > 0 && !IsKnownItemId(brush.serverLookId)) {
		error = wxString::Format("serverLookId uses unknown item id %d.", brush.serverLookId);
		return false;
	}

	if (type == "ground") {
		if (brushStorage_.items.empty()) {
			error = "Ground brush must contain at least one weighted item.";
			return false;
		}
		for (size_t itemIndex = 0; itemIndex < brushStorage_.items.size(); ++itemIndex) {
			const BrushItemRecord &item = brushStorage_.items[itemIndex];
			if (item.itemId <= 0) {
				error = wxString::Format("Ground item %zu must use a positive item id.", itemIndex + 1);
				return false;
			}
			if (!IsKnownItemId(item.itemId)) {
				error = wxString::Format("Ground item %zu uses unknown item id %d.", itemIndex + 1, item.itemId);
				return false;
			}
		}
	}

	if (type == "carpet" || type == "table") {
		if (type == "carpet") {
			for (size_t nodeIndex = 0; nodeIndex < brushStorage_.carpetNodes.size(); ++nodeIndex) {
				const auto &node = brushStorage_.carpetNodes[nodeIndex];
				if (node.align.IsEmpty()) {
					error = wxString::Format("Node %zu requires an align value.", nodeIndex + 1);
					return false;
				}
				if (node.items.empty()) {
					error = wxString::Format("Node %zu must contain at least one item.", nodeIndex + 1);
					return false;
				}
				for (size_t itemIndex = 0; itemIndex < node.items.size(); ++itemIndex) {
					const auto &item = node.items[itemIndex];
					if (item.itemId <= 0) {
						error = wxString::Format("Node %zu item %zu must use a positive item id.", nodeIndex + 1, itemIndex + 1);
						return false;
					}
					if (!IsKnownItemId(item.itemId)) {
						error = wxString::Format("Node %zu item %zu uses unknown item id %d.", nodeIndex + 1, itemIndex + 1, item.itemId);
						return false;
					}
				}
			}
		} else {
			for (size_t nodeIndex = 0; nodeIndex < brushStorage_.tableNodes.size(); ++nodeIndex) {
				const auto &node = brushStorage_.tableNodes[nodeIndex];
				if (node.align.IsEmpty()) {
					error = wxString::Format("Node %zu requires an align value.", nodeIndex + 1);
					return false;
				}
				if (node.items.empty()) {
					error = wxString::Format("Node %zu must contain at least one item.", nodeIndex + 1);
					return false;
				}
				for (size_t itemIndex = 0; itemIndex < node.items.size(); ++itemIndex) {
					const auto &item = node.items[itemIndex];
					if (item.itemId <= 0) {
						error = wxString::Format("Node %zu item %zu must use a positive item id.", nodeIndex + 1, itemIndex + 1);
						return false;
					}
					if (!IsKnownItemId(item.itemId)) {
						error = wxString::Format("Node %zu item %zu uses unknown item id %d.", nodeIndex + 1, itemIndex + 1, item.itemId);
						return false;
					}
				}
			}
		}
	}

	if (type == "doodad") {
		for (size_t altIndex = 0; altIndex < brushStorage_.doodadAlternatives.size(); ++altIndex) {
			const DoodadAlternativeRecord &alternative = brushStorage_.doodadAlternatives[altIndex];
			if (alternative.singleItems.empty() && alternative.composites.empty()) {
				error = wxString::Format("Alternative %zu must contain at least one single item or composite.", altIndex + 1);
				return false;
			}
			for (size_t itemIndex = 0; itemIndex < alternative.singleItems.size(); ++itemIndex) {
				const DoodadSingleItemRecord &item = alternative.singleItems[itemIndex];
				if (item.itemId <= 0) {
					error = wxString::Format("Alternative %zu single item %zu must use a positive item id.", altIndex + 1, itemIndex + 1);
					return false;
				}
				if (!IsKnownItemId(item.itemId)) {
					error = wxString::Format("Alternative %zu single item %zu uses unknown item id %d.", altIndex + 1, itemIndex + 1, item.itemId);
					return false;
				}
			}
			for (size_t compositeIndex = 0; compositeIndex < alternative.composites.size(); ++compositeIndex) {
				const DoodadCompositeRecord &composite = alternative.composites[compositeIndex];
				if (composite.tiles.empty()) {
					error = wxString::Format("Alternative %zu composite %zu must contain at least one tile.", altIndex + 1, compositeIndex + 1);
					return false;
				}
				for (size_t tileIndex = 0; tileIndex < composite.tiles.size(); ++tileIndex) {
					const DoodadCompositeTileRecord &tile = composite.tiles[tileIndex];
					if (tile.items.empty()) {
						error = wxString::Format(
							"Alternative %zu composite %zu tile %zu must contain at least one item.",
							altIndex + 1,
							compositeIndex + 1,
							tileIndex + 1
						);
						return false;
					}
					for (size_t tileItemIndex = 0; tileItemIndex < tile.items.size(); ++tileItemIndex) {
						if (tile.items[tileItemIndex].itemId <= 0) {
							error = wxString::Format(
								"Alternative %zu composite %zu tile %zu item %zu must use a positive item id.",
								altIndex + 1,
								compositeIndex + 1,
								tileIndex + 1,
								tileItemIndex + 1
							);
							return false;
						}
						if (!IsKnownItemId(tile.items[tileItemIndex].itemId)) {
							error = wxString::Format(
								"Alternative %zu composite %zu tile %zu item %zu uses unknown item id %d.",
								altIndex + 1,
								compositeIndex + 1,
								tileIndex + 1,
								tileItemIndex + 1,
								tile.items[tileItemIndex].itemId
							);
							return false;
						}
					}
				}
			}
		}
	}

	error.clear();
	return true;
}

bool MaterialsWorkbenchBrushPanel::SaveCurrentBrush() {
	if (!hasBrush_) {
		SetStatusMessage("Select a brush before saving.");
		return false;
	}

	const VariationEditorState previousVariationState = CaptureVariationEditorState();
	CommitVariationEditorState();
	brushStorage_ = BuildEditableStorageFromCurrentState();

	wxString validationError;
	if (!ValidateBrushStorage(validationError)) {
		spdlog::warn(
			"Materials Workbench blocked brush save for id={} name='{}': {}",
			static_cast<long long>(brushStorage_.brush.id),
			brushStorage_.brush.name.ToStdString(),
			validationError.ToStdString()
		);
		SetStatusMessage("Cannot save brush: " + validationError);
		return false;
	}

	wxString error;
	const wxString previousName = loadedBrushStorage_.brush.name;
	if (!controller_.SaveBrushDetails(brushStorage_, error)) {
		spdlog::warn(
			"Materials Workbench failed to save brush after validation: id={} name='{}' type='{}' error='{}'",
			static_cast<long long>(brushStorage_.brush.id),
			brushStorage_.brush.name.ToStdString(),
			brushStorage_.brush.type.ToStdString(),
			error.ToStdString()
		);
		SetStatusMessage("Failed to save brush: " + error);
		return false;
	}

	loadedBrushStorage_ = brushStorage_;
	PopulateFields();
	if (previousVariationState.valid) {
		RestoreVariationEditorState(previousVariationState);
	}
	RefreshDirtyState();
	SetStatusMessage("Brush and variations saved to materials.db.");
	spdlog::info(
		"Materials Workbench saved brush and variations: id={} old_name='{}' new_name='{}' type='{}' preserved_context={}",
		static_cast<long long>(brushStorage_.brush.id),
		previousName.ToStdString(),
		brushStorage_.brush.name.ToStdString(),
		brushStorage_.brush.type.ToStdString(),
		previousVariationState.valid
	);

	if (onBrushSaved_) {
		onBrushSaved_(brushStorage_.brush.id, previousName, brushStorage_.brush.name);
	}
	return true;
}

void MaterialsWorkbenchBrushPanel::OnSave(wxCommandEvent &WXUNUSED(event)) {
	SaveCurrentBrush();
}

void MaterialsWorkbenchBrushPanel::OnRevert(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_) {
		ClearWorkspace("Select a brush in the navigation tree to edit its properties.");
		return;
	}

	const int64_t brushId = brushStorage_.brush.id;
	const wxString brushName = brushStorage_.brush.name;

	if (!LoadBrush(currentContextKey_, currentItemIndex_)) {
		spdlog::warn(
			"Materials Workbench failed to revert brush from materials.db: id={} name='{}'",
			static_cast<long long>(brushId),
			brushName.ToStdString()
		);
		return;
	}

	SetStatusMessage("Brush fields and variations reloaded from materials.db.");
	spdlog::info(
		"Materials Workbench reverted brush from materials.db: id={} name='{}'",
		static_cast<long long>(brushId),
		brushName.ToStdString()
	);
}

void MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged(wxCommandEvent &event) {
	if (internalUpdate_ || !hasBrush_) {
		event.Skip();
		return;
	}

	UpdateWorkspaceHeader();
	RefreshVariationEditor();
	RefreshDirtyState();
	event.Skip();
}

void MaterialsWorkbenchBrushPanel::OnAddGroundItem(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesGroundVariationEditor()) {
		return;
	}

	BrushItemRecord item;
	item.chance = 1;
	brushStorage_.items.push_back(item);
	groundItemIndex_ = static_cast<int>(brushStorage_.items.size()) - 1;
	RefreshGroundItemList();
	RefreshGroundSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Added ground variation item.");
}

void MaterialsWorkbenchBrushPanel::OnRemoveGroundItem(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesGroundVariationEditor()) {
		return;
	}
	if (groundItemIndex_ < 0 || groundItemIndex_ >= static_cast<int>(brushStorage_.items.size())) {
		SetStatusMessage("Select a ground item before removing it.");
		return;
	}

	brushStorage_.items.erase(brushStorage_.items.begin() + groundItemIndex_);
	if (groundItemIndex_ >= static_cast<int>(brushStorage_.items.size())) {
		groundItemIndex_ = static_cast<int>(brushStorage_.items.size()) - 1;
	}
	RefreshGroundItemList();
	RefreshGroundSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Removed ground variation item.");
}

void MaterialsWorkbenchBrushPanel::OnGroundItemSelected(wxCommandEvent &event) {
	groundItemIndex_ = event.GetSelection();
	RefreshGroundSelection();
}

void MaterialsWorkbenchBrushPanel::OnGroundItemValueChanged(wxCommandEvent &WXUNUSED(event)) {
	if (internalUpdate_ || !hasBrush_ || !UsesGroundVariationEditor()) {
		return;
	}
	if (groundItemIndex_ < 0 || groundItemIndex_ >= static_cast<int>(brushStorage_.items.size())) {
		return;
	}

	brushStorage_.items[groundItemIndex_].itemId = groundItemIdCtrl_->GetValue();
	brushStorage_.items[groundItemIndex_].chance = groundItemChanceCtrl_->GetValue();
	RefreshGroundItemList();
	if (groundItemIndex_ >= 0) {
		groundItemsList_->SetSelection(groundItemIndex_);
	}
	UpdateSummary();
	RefreshDirtyState();
}

void MaterialsWorkbenchBrushPanel::OnAddAlignedNode(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesAlignedVariationEditor()) {
		return;
	}
	if (GetEffectiveBrushType() == "carpet") {
		CarpetNodeRecord node;
		node.align = "center";
		brushStorage_.carpetNodes.push_back(node);
		alignedNodeIndex_ = static_cast<int>(brushStorage_.carpetNodes.size()) - 1;
	} else {
		TableNodeRecord node;
		node.align = "center";
		brushStorage_.tableNodes.push_back(node);
		alignedNodeIndex_ = static_cast<int>(brushStorage_.tableNodes.size()) - 1;
	}
	RefreshAlignedNodeList();
	RefreshAlignedSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Added variation node.");
}

void MaterialsWorkbenchBrushPanel::OnRemoveAlignedNode(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesAlignedVariationEditor()) {
		return;
	}
	if (GetEffectiveBrushType() == "carpet") {
		auto &nodes = brushStorage_.carpetNodes;
		if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			SetStatusMessage("Select a node before removing it.");
			return;
		}
		nodes.erase(nodes.begin() + alignedNodeIndex_);
		if (alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			alignedNodeIndex_ = static_cast<int>(nodes.size()) - 1;
		}
	} else {
		auto &nodes = brushStorage_.tableNodes;
		if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			SetStatusMessage("Select a node before removing it.");
			return;
		}
		nodes.erase(nodes.begin() + alignedNodeIndex_);
		if (alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			alignedNodeIndex_ = static_cast<int>(nodes.size()) - 1;
		}
	}
	RefreshAlignedNodeList();
	RefreshAlignedSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Removed variation node.");
}

void MaterialsWorkbenchBrushPanel::OnAlignedNodeSelected(wxCommandEvent &event) {
	alignedNodeIndex_ = event.GetSelection();
	RefreshAlignedSelection();
}

void MaterialsWorkbenchBrushPanel::OnAlignedNodeAlignChanged(wxCommandEvent &WXUNUSED(event)) {
	if (internalUpdate_ || !hasBrush_ || !UsesAlignedVariationEditor()) {
		return;
	}
	if (GetEffectiveBrushType() == "carpet") {
		auto &nodes = brushStorage_.carpetNodes;
		if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			return;
		}
		nodes[alignedNodeIndex_].align = TrimmedValue(alignedNodeAlignCtrl_);
	} else {
		auto &nodes = brushStorage_.tableNodes;
		if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			return;
		}
		nodes[alignedNodeIndex_].align = TrimmedValue(alignedNodeAlignCtrl_);
	}
	RefreshAlignedNodeList();
	if (alignedNodeIndex_ >= 0) {
		alignedNodesList_->SetSelection(alignedNodeIndex_);
	}
	RefreshDirtyState();
}

void MaterialsWorkbenchBrushPanel::OnAddAlignedItem(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesAlignedVariationEditor()) {
		return;
	}
	if (GetEffectiveBrushType() == "carpet") {
		auto &nodes = brushStorage_.carpetNodes;
		if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			SetStatusMessage("Add or select a node before adding items.");
			return;
		}
		CarpetNodeItemRecord item;
		item.chance = 1;
		nodes[alignedNodeIndex_].items.push_back(item);
		alignedItemIndex_ = static_cast<int>(nodes[alignedNodeIndex_].items.size()) - 1;
	} else {
		auto &nodes = brushStorage_.tableNodes;
		if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			SetStatusMessage("Add or select a node before adding items.");
			return;
		}
		TableNodeItemRecord item;
		item.chance = 1;
		nodes[alignedNodeIndex_].items.push_back(item);
		alignedItemIndex_ = static_cast<int>(nodes[alignedNodeIndex_].items.size()) - 1;
	}
	RefreshAlignedSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Added node item.");
}

void MaterialsWorkbenchBrushPanel::OnRemoveAlignedItem(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesAlignedVariationEditor()) {
		return;
	}
	if (GetEffectiveBrushType() == "carpet") {
		auto &nodes = brushStorage_.carpetNodes;
		if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			SetStatusMessage("Select a node first.");
			return;
		}
		auto &items = nodes[alignedNodeIndex_].items;
		if (alignedItemIndex_ < 0 || alignedItemIndex_ >= static_cast<int>(items.size())) {
			SetStatusMessage("Select an item before removing it.");
			return;
		}
		items.erase(items.begin() + alignedItemIndex_);
		if (alignedItemIndex_ >= static_cast<int>(items.size())) {
			alignedItemIndex_ = static_cast<int>(items.size()) - 1;
		}
	} else {
		auto &nodes = brushStorage_.tableNodes;
		if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			SetStatusMessage("Select a node first.");
			return;
		}
		auto &items = nodes[alignedNodeIndex_].items;
		if (alignedItemIndex_ < 0 || alignedItemIndex_ >= static_cast<int>(items.size())) {
			SetStatusMessage("Select an item before removing it.");
			return;
		}
		items.erase(items.begin() + alignedItemIndex_);
		if (alignedItemIndex_ >= static_cast<int>(items.size())) {
			alignedItemIndex_ = static_cast<int>(items.size()) - 1;
		}
	}
	RefreshAlignedSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Removed node item.");
}

void MaterialsWorkbenchBrushPanel::OnAlignedItemSelected(wxCommandEvent &event) {
	alignedItemIndex_ = event.GetSelection();
	RefreshAlignedSelection();
}

void MaterialsWorkbenchBrushPanel::OnAlignedItemValueChanged(wxCommandEvent &WXUNUSED(event)) {
	if (internalUpdate_ || !hasBrush_ || !UsesAlignedVariationEditor()) {
		return;
	}
	if (GetEffectiveBrushType() == "carpet") {
		auto &nodes = brushStorage_.carpetNodes;
		if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			return;
		}
		auto &items = nodes[alignedNodeIndex_].items;
		if (alignedItemIndex_ < 0 || alignedItemIndex_ >= static_cast<int>(items.size())) {
			return;
		}
		items[alignedItemIndex_].itemId = alignedItemIdCtrl_->GetValue();
		items[alignedItemIndex_].chance = alignedItemChanceCtrl_->GetValue();
	} else {
		auto &nodes = brushStorage_.tableNodes;
		if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			return;
		}
		auto &items = nodes[alignedNodeIndex_].items;
		if (alignedItemIndex_ < 0 || alignedItemIndex_ >= static_cast<int>(items.size())) {
			return;
		}
		items[alignedItemIndex_].itemId = alignedItemIdCtrl_->GetValue();
		items[alignedItemIndex_].chance = alignedItemChanceCtrl_->GetValue();
	}
	RefreshAlignedItemList();
	if (alignedItemIndex_ >= 0) {
		alignedItemsList_->SetSelection(alignedItemIndex_);
	}
	RefreshDirtyState();
}

void MaterialsWorkbenchBrushPanel::OnAddDoodadAlternative(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	brushStorage_.doodadAlternatives.emplace_back();
	doodadAlternativeIndex_ = static_cast<int>(brushStorage_.doodadAlternatives.size()) - 1;
	RefreshDoodadAlternativeList();
	RefreshDoodadSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Added doodad alternative.");
}

void MaterialsWorkbenchBrushPanel::OnRemoveDoodadAlternative(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		SetStatusMessage("Select an alternative before removing it.");
		return;
	}
	brushStorage_.doodadAlternatives.erase(brushStorage_.doodadAlternatives.begin() + doodadAlternativeIndex_);
	if (doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		doodadAlternativeIndex_ = static_cast<int>(brushStorage_.doodadAlternatives.size()) - 1;
	}
	RefreshDoodadAlternativeList();
	RefreshDoodadSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Removed doodad alternative.");
}

void MaterialsWorkbenchBrushPanel::OnDoodadAlternativeSelected(wxCommandEvent &event) {
	doodadAlternativeIndex_ = event.GetSelection();
	RefreshDoodadSelection();
}

void MaterialsWorkbenchBrushPanel::OnAddDoodadSingleItem(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		SetStatusMessage("Add or select an alternative before adding single items.");
		return;
	}
	DoodadSingleItemRecord item;
	item.chance = 1;
	auto &singleItems = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].singleItems;
	singleItems.push_back(item);
	doodadSingleItemIndex_ = static_cast<int>(singleItems.size()) - 1;
	RefreshDoodadSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Added doodad single item.");
}

void MaterialsWorkbenchBrushPanel::OnRemoveDoodadSingleItem(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		SetStatusMessage("Select an alternative first.");
		return;
	}
	auto &singleItems = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].singleItems;
	if (doodadSingleItemIndex_ < 0 || doodadSingleItemIndex_ >= static_cast<int>(singleItems.size())) {
		SetStatusMessage("Select a single item before removing it.");
		return;
	}
	singleItems.erase(singleItems.begin() + doodadSingleItemIndex_);
	if (doodadSingleItemIndex_ >= static_cast<int>(singleItems.size())) {
		doodadSingleItemIndex_ = static_cast<int>(singleItems.size()) - 1;
	}
	RefreshDoodadSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Removed doodad single item.");
}

void MaterialsWorkbenchBrushPanel::OnDoodadSingleItemSelected(wxCommandEvent &event) {
	doodadSingleItemIndex_ = event.GetSelection();
	RefreshDoodadSelection();
}

void MaterialsWorkbenchBrushPanel::OnDoodadSingleItemValueChanged(wxCommandEvent &WXUNUSED(event)) {
	if (internalUpdate_ || !hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		return;
	}
	auto &singleItems = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].singleItems;
	if (doodadSingleItemIndex_ < 0 || doodadSingleItemIndex_ >= static_cast<int>(singleItems.size())) {
		return;
	}
	singleItems[doodadSingleItemIndex_].itemId = doodadSingleItemIdCtrl_->GetValue();
	singleItems[doodadSingleItemIndex_].chance = doodadSingleItemChanceCtrl_->GetValue();
	RefreshDoodadSingleItemList();
	if (doodadSingleItemIndex_ >= 0) {
		doodadSingleItemsList_->SetSelection(doodadSingleItemIndex_);
	}
	RefreshDoodadAlternativeList();
	if (doodadAlternativeIndex_ >= 0) {
		doodadAlternativesList_->SetSelection(doodadAlternativeIndex_);
	}
	RefreshDirtyState();
}

void MaterialsWorkbenchBrushPanel::OnAddDoodadComposite(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		SetStatusMessage("Add or select an alternative before adding composites.");
		return;
	}
	DoodadCompositeRecord composite;
	composite.chance = 1;
	auto &composites = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].composites;
	composites.push_back(composite);
	doodadCompositeIndex_ = static_cast<int>(composites.size()) - 1;
	RefreshDoodadSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Added doodad composite.");
}

void MaterialsWorkbenchBrushPanel::OnRemoveDoodadComposite(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		SetStatusMessage("Select an alternative first.");
		return;
	}
	auto &composites = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].composites;
	if (doodadCompositeIndex_ < 0 || doodadCompositeIndex_ >= static_cast<int>(composites.size())) {
		SetStatusMessage("Select a composite before removing it.");
		return;
	}
	composites.erase(composites.begin() + doodadCompositeIndex_);
	if (doodadCompositeIndex_ >= static_cast<int>(composites.size())) {
		doodadCompositeIndex_ = static_cast<int>(composites.size()) - 1;
	}
	RefreshDoodadSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Removed doodad composite.");
}

void MaterialsWorkbenchBrushPanel::OnDoodadCompositeSelected(wxCommandEvent &event) {
	doodadCompositeIndex_ = event.GetSelection();
	RefreshDoodadSelection();
}

void MaterialsWorkbenchBrushPanel::OnDoodadCompositeChanceChanged(wxCommandEvent &WXUNUSED(event)) {
	if (internalUpdate_ || !hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		return;
	}
	auto &composites = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].composites;
	if (doodadCompositeIndex_ < 0 || doodadCompositeIndex_ >= static_cast<int>(composites.size())) {
		return;
	}
	composites[doodadCompositeIndex_].chance = doodadCompositeChanceCtrl_->GetValue();
	RefreshDoodadCompositeList();
	if (doodadCompositeIndex_ >= 0) {
		doodadCompositesList_->SetSelection(doodadCompositeIndex_);
	}
	RefreshDoodadAlternativeList();
	if (doodadAlternativeIndex_ >= 0) {
		doodadAlternativesList_->SetSelection(doodadAlternativeIndex_);
	}
	RefreshDirtyState();
}

void MaterialsWorkbenchBrushPanel::OnAddDoodadTile(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		SetStatusMessage("Select an alternative first.");
		return;
	}
	auto &composites = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].composites;
	if (doodadCompositeIndex_ < 0 || doodadCompositeIndex_ >= static_cast<int>(composites.size())) {
		SetStatusMessage("Select a composite before adding tiles.");
		return;
	}
	composites[doodadCompositeIndex_].tiles.emplace_back();
	doodadTileIndex_ = static_cast<int>(composites[doodadCompositeIndex_].tiles.size()) - 1;
	RefreshDoodadSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Added doodad composite tile.");
}

void MaterialsWorkbenchBrushPanel::OnRemoveDoodadTile(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		SetStatusMessage("Select an alternative first.");
		return;
	}
	auto &composites = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].composites;
	if (doodadCompositeIndex_ < 0 || doodadCompositeIndex_ >= static_cast<int>(composites.size())) {
		SetStatusMessage("Select a composite first.");
		return;
	}
	auto &tiles = composites[doodadCompositeIndex_].tiles;
	if (doodadTileIndex_ < 0 || doodadTileIndex_ >= static_cast<int>(tiles.size())) {
		SetStatusMessage("Select a tile before removing it.");
		return;
	}
	tiles.erase(tiles.begin() + doodadTileIndex_);
	if (doodadTileIndex_ >= static_cast<int>(tiles.size())) {
		doodadTileIndex_ = static_cast<int>(tiles.size()) - 1;
	}
	RefreshDoodadSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Removed doodad composite tile.");
}

void MaterialsWorkbenchBrushPanel::OnDoodadTileSelected(wxCommandEvent &event) {
	doodadTileIndex_ = event.GetSelection();
	RefreshDoodadSelection();
}

void MaterialsWorkbenchBrushPanel::OnDoodadTileOffsetChanged(wxCommandEvent &WXUNUSED(event)) {
	if (internalUpdate_ || !hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		return;
	}
	auto &composites = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].composites;
	if (doodadCompositeIndex_ < 0 || doodadCompositeIndex_ >= static_cast<int>(composites.size())) {
		return;
	}
	auto &tiles = composites[doodadCompositeIndex_].tiles;
	if (doodadTileIndex_ < 0 || doodadTileIndex_ >= static_cast<int>(tiles.size())) {
		return;
	}
	tiles[doodadTileIndex_].offsetX = doodadTileOffsetXCtrl_->GetValue();
	tiles[doodadTileIndex_].offsetY = doodadTileOffsetYCtrl_->GetValue();
	tiles[doodadTileIndex_].offsetZ = doodadTileOffsetZCtrl_->GetValue();
	RefreshDoodadTileList();
	if (doodadTileIndex_ >= 0) {
		doodadTilesList_->SetSelection(doodadTileIndex_);
	}
	RefreshDirtyState();
}

void MaterialsWorkbenchBrushPanel::OnAddDoodadTileItem(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		SetStatusMessage("Select an alternative first.");
		return;
	}
	auto &composites = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].composites;
	if (doodadCompositeIndex_ < 0 || doodadCompositeIndex_ >= static_cast<int>(composites.size())) {
		SetStatusMessage("Select a composite first.");
		return;
	}
	auto &tiles = composites[doodadCompositeIndex_].tiles;
	if (doodadTileIndex_ < 0 || doodadTileIndex_ >= static_cast<int>(tiles.size())) {
		SetStatusMessage("Select a tile before adding items.");
		return;
	}
	tiles[doodadTileIndex_].items.emplace_back();
	doodadTileItemIndex_ = static_cast<int>(tiles[doodadTileIndex_].items.size()) - 1;
	RefreshDoodadSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Added doodad tile item.");
}

void MaterialsWorkbenchBrushPanel::OnRemoveDoodadTileItem(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		SetStatusMessage("Select an alternative first.");
		return;
	}
	auto &composites = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].composites;
	if (doodadCompositeIndex_ < 0 || doodadCompositeIndex_ >= static_cast<int>(composites.size())) {
		SetStatusMessage("Select a composite first.");
		return;
	}
	auto &tiles = composites[doodadCompositeIndex_].tiles;
	if (doodadTileIndex_ < 0 || doodadTileIndex_ >= static_cast<int>(tiles.size())) {
		SetStatusMessage("Select a tile first.");
		return;
	}
	auto &items = tiles[doodadTileIndex_].items;
	if (doodadTileItemIndex_ < 0 || doodadTileItemIndex_ >= static_cast<int>(items.size())) {
		SetStatusMessage("Select a tile item before removing it.");
		return;
	}
	items.erase(items.begin() + doodadTileItemIndex_);
	if (doodadTileItemIndex_ >= static_cast<int>(items.size())) {
		doodadTileItemIndex_ = static_cast<int>(items.size()) - 1;
	}
	RefreshDoodadSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Removed doodad tile item.");
}

void MaterialsWorkbenchBrushPanel::OnDoodadTileItemSelected(wxCommandEvent &event) {
	doodadTileItemIndex_ = event.GetSelection();
	RefreshDoodadSelection();
}

void MaterialsWorkbenchBrushPanel::OnDoodadTileItemValueChanged(wxCommandEvent &WXUNUSED(event)) {
	if (internalUpdate_ || !hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		return;
	}
	auto &composites = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].composites;
	if (doodadCompositeIndex_ < 0 || doodadCompositeIndex_ >= static_cast<int>(composites.size())) {
		return;
	}
	auto &tiles = composites[doodadCompositeIndex_].tiles;
	if (doodadTileIndex_ < 0 || doodadTileIndex_ >= static_cast<int>(tiles.size())) {
		return;
	}
	auto &items = tiles[doodadTileIndex_].items;
	if (doodadTileItemIndex_ < 0 || doodadTileItemIndex_ >= static_cast<int>(items.size())) {
		return;
	}
	items[doodadTileItemIndex_].itemId = doodadTileItemIdCtrl_->GetValue();
	RefreshDoodadTileItemList();
	if (doodadTileItemIndex_ >= 0) {
		doodadTileItemsList_->SetSelection(doodadTileItemIndex_);
	}
	RefreshDoodadTileList();
	if (doodadTileIndex_ >= 0) {
		doodadTilesList_->SetSelection(doodadTileIndex_);
	}
	RefreshDoodadCompositeList();
	if (doodadCompositeIndex_ >= 0) {
		doodadCompositesList_->SetSelection(doodadCompositeIndex_);
	}
	RefreshDoodadAlternativeList();
	if (doodadAlternativeIndex_ >= 0) {
		doodadAlternativesList_->SetSelection(doodadAlternativeIndex_);
	}
	RefreshDirtyState();
}
