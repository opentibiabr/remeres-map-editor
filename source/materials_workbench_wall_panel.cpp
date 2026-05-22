#include "main.h"

#include "materials_workbench_wall_panel.h"

#include <algorithm>
#include <utility>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/wrapsizer.h>

#include "common_windows.h"
#include "find_item_window.h"
#include "materials_workbench_controller.h"

namespace {
	class WallWorkspaceToggleButton : public ItemToggleButton {
	public:
		WallWorkspaceToggleButton(wxWindow* parent, int itemId) :
			ItemToggleButton(parent, RENDER_SIZE_32x32, itemId) {
		}
	};

	wxString DescribeDoor(const WallPartDoorRecord &door) {
		return wxString::Format("%s | id %d | %s%s",
			door.doorType,
			door.itemId,
			door.isOpen ? "open" : "closed",
			door.wallHateMe ? " | hate" : "");
	}

	void NormalizeWallPartRecord(WallPartRecord &part) {
		for (size_t i = 0; i < part.items.size(); ++i) {
			part.items[i].sortOrder = static_cast<int>(i);
		}
		for (size_t i = 0; i < part.doors.size(); ++i) {
			part.doors[i].sortOrder = static_cast<int>(i);
		}
	}
} // namespace

MaterialsWorkbenchWallPanel::MaterialsWorkbenchWallPanel(wxWindow* parent, MaterialsWorkbenchController &controller) :
	wxPanel(parent, wxID_ANY),
	controller_(controller) {
	BuildLayout();
	ClearWorkspace("Select a wall brush in the navigation tree to edit its wall parts.");
}

void MaterialsWorkbenchWallPanel::SetOnWallBrushSaved(std::function<void(int64_t)> callback) {
	onWallBrushSaved_ = std::move(callback);
}

const WallPartRecord* MaterialsWorkbenchWallPanel::GetSelectedPart() const {
	if (selectedPartIndex_ < 0 || selectedPartIndex_ >= static_cast<int>(wallBrushStorage_.wallParts.size())) {
		return nullptr;
	}
	return &wallBrushStorage_.wallParts[selectedPartIndex_];
}

WallPartRecord* MaterialsWorkbenchWallPanel::GetSelectedPart() {
	if (selectedPartIndex_ < 0 || selectedPartIndex_ >= static_cast<int>(wallBrushStorage_.wallParts.size())) {
		return nullptr;
	}
	return &wallBrushStorage_.wallParts[selectedPartIndex_];
}

void MaterialsWorkbenchWallPanel::BuildLayout() {
	wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);

	wxStaticText* title = new wxStaticText(this, wxID_ANY, "Wall Workspace");
	wxFont titleFont = title->GetFont();
	titleFont.SetPointSize(titleFont.GetPointSize() + 4);
	titleFont.SetWeight(wxFONTWEIGHT_BOLD);
	title->SetFont(titleFont);

	titleLabel_ = new wxStaticText(this, wxID_ANY, "No wall brush selected");
	subtitleLabel_ = new wxStaticText(this, wxID_ANY, "Edit persisted wall parts, door definitions and alternates from materials.db.");

	wxScrolledWindow* scrolled = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	scrolled->SetScrollRate(FromDIP(10), FromDIP(10));

	wxBoxSizer* contentSizer = new wxBoxSizer(wxVERTICAL);
	summaryLabel_ = new wxStaticText(scrolled, wxID_ANY, "");

	wxStaticBoxSizer* identityBox = new wxStaticBoxSizer(wxVERTICAL, scrolled, "Wall Brush");
	wxFlexGridSizer* identityGrid = new wxFlexGridSizer(2, FromDIP(8), FromDIP(10));
	identityGrid->AddGrowableCol(1, 1);

	brushIdCtrl_ = new wxTextCtrl(scrolled, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
	brushNameCtrl_ = new wxTextCtrl(scrolled, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
	brushSourceCtrl_ = new wxTextCtrl(scrolled, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
	partChoice_ = new wxChoice(scrolled, wxID_ANY);
	partSummaryLabel_ = new wxStaticText(scrolled, wxID_ANY, "");

	identityGrid->Add(new wxStaticText(scrolled, wxID_ANY, "SQLite ID"), 0, wxALIGN_CENTER_VERTICAL);
	identityGrid->Add(brushIdCtrl_, 1, wxEXPAND);
	identityGrid->Add(new wxStaticText(scrolled, wxID_ANY, "Name"), 0, wxALIGN_CENTER_VERTICAL);
	identityGrid->Add(brushNameCtrl_, 1, wxEXPAND);
	identityGrid->Add(new wxStaticText(scrolled, wxID_ANY, "Source"), 0, wxALIGN_CENTER_VERTICAL);
	identityGrid->Add(brushSourceCtrl_, 1, wxEXPAND);
	identityGrid->Add(new wxStaticText(scrolled, wxID_ANY, "Part Type"), 0, wxALIGN_CENTER_VERTICAL);
	identityGrid->Add(partChoice_, 1, wxEXPAND);

	identityBox->Add(identityGrid, 0, wxEXPAND | wxALL, FromDIP(8));
	identityBox->Add(partSummaryLabel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

	wxBoxSizer* gridsRow = new wxBoxSizer(wxHORIZONTAL);

	wxStaticBoxSizer* itemBox = new wxStaticBoxSizer(wxVERTICAL, scrolled, "Wall Items");
	itemGridScroll_ = new wxScrolledWindow(scrolled, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(220)), wxVSCROLL);
	itemGridScroll_->SetScrollRate(FromDIP(10), FromDIP(10));
	itemGridSizer_ = new wxWrapSizer(wxHORIZONTAL);
	itemGridScroll_->SetSizer(itemGridSizer_);
	itemBox->Add(itemGridScroll_, 1, wxEXPAND | wxALL, FromDIP(8));

	wxStaticBoxSizer* doorBox = new wxStaticBoxSizer(wxVERTICAL, scrolled, "Doors");
	doorGridScroll_ = new wxScrolledWindow(scrolled, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(220)), wxVSCROLL);
	doorGridScroll_->SetScrollRate(FromDIP(10), FromDIP(10));
	doorGridSizer_ = new wxWrapSizer(wxHORIZONTAL);
	doorGridScroll_->SetSizer(doorGridSizer_);
	doorBox->Add(doorGridScroll_, 1, wxEXPAND | wxALL, FromDIP(8));

	gridsRow->Add(itemBox, 1, wxRIGHT | wxEXPAND, FromDIP(10));
	gridsRow->Add(doorBox, 1, wxEXPAND);

	wxBoxSizer* editorRow = new wxBoxSizer(wxHORIZONTAL);

	wxStaticBoxSizer* itemEditorBox = new wxStaticBoxSizer(wxVERTICAL, scrolled, "Selected Item");
	selectedItemLabel_ = new wxStaticText(scrolled, wxID_ANY, "No wall item selected");
	itemPreviewButton_ = new ItemButton(scrolled, RENDER_SIZE_32x32, 0);
	itemIdCtrl_ = new wxSpinCtrl(scrolled, wxID_ANY);
	itemIdCtrl_->SetRange(0, 100000);
	itemChanceCtrl_ = new wxSpinCtrl(scrolled, wxID_ANY);
	itemChanceCtrl_->SetRange(0, 100000);

	wxFlexGridSizer* itemEditorGrid = new wxFlexGridSizer(2, FromDIP(8), FromDIP(10));
	itemEditorGrid->AddGrowableCol(1, 1);
	itemEditorGrid->Add(new wxStaticText(scrolled, wxID_ANY, "Preview"), 0, wxALIGN_CENTER_VERTICAL);
	itemEditorGrid->Add(itemPreviewButton_, 0, wxALIGN_LEFT);
	itemEditorGrid->Add(new wxStaticText(scrolled, wxID_ANY, "Item ID"), 0, wxALIGN_CENTER_VERTICAL);
	itemEditorGrid->Add(itemIdCtrl_, 1, wxEXPAND);
	itemEditorGrid->Add(new wxStaticText(scrolled, wxID_ANY, "Chance"), 0, wxALIGN_CENTER_VERTICAL);
	itemEditorGrid->Add(itemChanceCtrl_, 1, wxEXPAND);

	wxBoxSizer* itemActionSizer = new wxBoxSizer(wxHORIZONTAL);
	wxButton* pickItemButton = new wxButton(scrolled, wxID_ANY, "Pick Item");
	wxButton* applyItemButton = new wxButton(scrolled, wxID_ANY, "Apply Item");
	wxButton* removeItemButton = new wxButton(scrolled, wxID_ANY, "Remove Item");
	itemActionSizer->Add(pickItemButton, 0, wxRIGHT, FromDIP(6));
	itemActionSizer->Add(applyItemButton, 0, wxRIGHT, FromDIP(6));
	itemActionSizer->Add(removeItemButton, 0);

	itemEditorBox->Add(selectedItemLabel_, 0, wxEXPAND | wxALL, FromDIP(8));
	itemEditorBox->Add(itemEditorGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
	itemEditorBox->Add(itemActionSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

	wxStaticBoxSizer* doorEditorBox = new wxStaticBoxSizer(wxVERTICAL, scrolled, "Selected Door");
	selectedDoorLabel_ = new wxStaticText(scrolled, wxID_ANY, "No door selected");
	doorPreviewButton_ = new ItemButton(scrolled, RENDER_SIZE_32x32, 0);
	doorItemIdCtrl_ = new wxSpinCtrl(scrolled, wxID_ANY);
	doorItemIdCtrl_->SetRange(0, 100000);
	doorTypeChoice_ = new wxChoice(scrolled, wxID_ANY);
	const wxString doorTypes[] = { "archway", "normal", "locked", "quest", "magic", "window", "hatch_window", "hatch window", "any door", "any window", "any" };
	for (const wxString &doorType : doorTypes) {
		doorTypeChoice_->Append(doorType);
	}
	doorOpenCtrl_ = new wxCheckBox(scrolled, wxID_ANY, "Open");
	doorHateCtrl_ = new wxCheckBox(scrolled, wxID_ANY, "Wall Hate");

	wxFlexGridSizer* doorEditorGrid = new wxFlexGridSizer(2, FromDIP(8), FromDIP(10));
	doorEditorGrid->AddGrowableCol(1, 1);
	doorEditorGrid->Add(new wxStaticText(scrolled, wxID_ANY, "Preview"), 0, wxALIGN_CENTER_VERTICAL);
	doorEditorGrid->Add(doorPreviewButton_, 0, wxALIGN_LEFT);
	doorEditorGrid->Add(new wxStaticText(scrolled, wxID_ANY, "Item ID"), 0, wxALIGN_CENTER_VERTICAL);
	doorEditorGrid->Add(doorItemIdCtrl_, 1, wxEXPAND);
	doorEditorGrid->Add(new wxStaticText(scrolled, wxID_ANY, "Door Type"), 0, wxALIGN_CENTER_VERTICAL);
	doorEditorGrid->Add(doorTypeChoice_, 1, wxEXPAND);
	doorEditorGrid->Add(new wxStaticText(scrolled, wxID_ANY, "Flags"), 0, wxALIGN_CENTER_VERTICAL);
	wxBoxSizer* doorFlagSizer = new wxBoxSizer(wxHORIZONTAL);
	doorFlagSizer->Add(doorOpenCtrl_, 0, wxRIGHT, FromDIP(8));
	doorFlagSizer->Add(doorHateCtrl_, 0);
	doorEditorGrid->Add(doorFlagSizer, 1, wxEXPAND);

	wxBoxSizer* doorActionSizer = new wxBoxSizer(wxHORIZONTAL);
	wxButton* pickDoorItemButton = new wxButton(scrolled, wxID_ANY, "Pick Door Item");
	wxButton* applyDoorButton = new wxButton(scrolled, wxID_ANY, "Apply Door");
	wxButton* removeDoorButton = new wxButton(scrolled, wxID_ANY, "Remove Door");
	doorActionSizer->Add(pickDoorItemButton, 0, wxRIGHT, FromDIP(6));
	doorActionSizer->Add(applyDoorButton, 0, wxRIGHT, FromDIP(6));
	doorActionSizer->Add(removeDoorButton, 0);

	doorEditorBox->Add(selectedDoorLabel_, 0, wxEXPAND | wxALL, FromDIP(8));
	doorEditorBox->Add(doorEditorGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
	doorEditorBox->Add(doorActionSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

	editorRow->Add(itemEditorBox, 1, wxRIGHT | wxEXPAND, FromDIP(10));
	editorRow->Add(doorEditorBox, 1, wxEXPAND);

	contentSizer->Add(summaryLabel_, 0, wxEXPAND | wxALL, FromDIP(8));
	contentSizer->Add(identityBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
	contentSizer->Add(gridsRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
	contentSizer->Add(editorRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

	scrolled->SetSizer(contentSizer);

	wxBoxSizer* headerSizer = new wxBoxSizer(wxVERTICAL);
	headerSizer->Add(title, 0, wxBOTTOM, FromDIP(4));
	headerSizer->Add(titleLabel_, 0, wxBOTTOM, FromDIP(2));
	headerSizer->Add(subtitleLabel_, 0);

	wxBoxSizer* actionSizer = new wxBoxSizer(wxHORIZONTAL);
	wxButton* saveButton = new wxButton(this, wxID_SAVE, "Save Wall Brush");
	wxButton* revertButton = new wxButton(this, wxID_ANY, "Revert");
	actionSizer->Add(saveButton, 0, wxRIGHT, FromDIP(6));
	actionSizer->Add(revertButton, 0);

	statusLabel_ = new wxStaticText(this, wxID_ANY, "");

	rootSizer->Add(headerSizer, 0, wxEXPAND | wxALL, FromDIP(10));
	rootSizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));
	rootSizer->Add(scrolled, 1, wxEXPAND | wxALL, FromDIP(10));
	rootSizer->Add(actionSizer, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
	rootSizer->Add(statusLabel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
	SetSizer(rootSizer);

	partChoice_->Bind(wxEVT_CHOICE, &MaterialsWorkbenchWallPanel::OnPartChanged, this);
	pickItemButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchWallPanel::OnPickItem, this);
	applyItemButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchWallPanel::OnApplyItem, this);
	removeItemButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchWallPanel::OnRemoveItem, this);
	pickDoorItemButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchWallPanel::OnPickDoorItem, this);
	applyDoorButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchWallPanel::OnApplyDoor, this);
	removeDoorButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchWallPanel::OnRemoveDoor, this);
	saveButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchWallPanel::OnSave, this);
	revertButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchWallPanel::OnRevert, this);
	itemIdCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchWallPanel::OnItemIdChanged, this);
	itemIdCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchWallPanel::OnItemIdSpin, this);
	doorItemIdCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchWallPanel::OnDoorItemIdChanged, this);
	doorItemIdCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchWallPanel::OnDoorItemIdSpin, this);
}

void MaterialsWorkbenchWallPanel::ClearWorkspace(const wxString &message) {
	wallBrushStorage_ = BrushStorageRecord();
	currentContextKey_.clear();
	currentItemIndex_ = -1;
	selectedPartIndex_ = -1;
	selectedItemIndex_ = -1;
	selectedDoorIndex_ = -1;
	hasWallBrush_ = false;

	titleLabel_->SetLabel("No wall brush selected");
	subtitleLabel_->SetLabel("Select a wall brush in the navigation tree to edit its wall parts.");
	summaryLabel_->SetLabel(message);
	brushIdCtrl_->SetValue("");
	brushNameCtrl_->SetValue("");
	brushSourceCtrl_->SetValue("");
	partChoice_->Clear();
	partSummaryLabel_->SetLabel("");
	selectedItemLabel_->SetLabel("No wall item selected");
	itemIdCtrl_->SetValue(0);
	itemChanceCtrl_->SetValue(0);
	itemPreviewButton_->SetSprite(0);
	selectedDoorLabel_->SetLabel("No door selected");
	doorItemIdCtrl_->SetValue(0);
	doorTypeChoice_->SetSelection(wxNOT_FOUND);
	doorOpenCtrl_->SetValue(false);
	doorHateCtrl_->SetValue(false);
	doorPreviewButton_->SetSprite(0);

	itemGridSizer_->Clear(true);
	doorGridSizer_->Clear(true);
	itemButtons_.clear();
	doorButtons_.clear();

	SetFieldsEnabled(false);
	SetStatusMessage(message);
	Layout();
}

bool MaterialsWorkbenchWallPanel::LoadWallBrush(const wxString &contextKey, int itemIndex) {
	wxString error;
	BrushStorageRecord storage;
	if (!controller_.GetBrushDetails(contextKey, itemIndex, storage, error)) {
		ClearWorkspace("Failed to load wall brush details: " + error);
		return false;
	}

	wallBrushStorage_ = storage;
	currentContextKey_ = contextKey;
	currentItemIndex_ = itemIndex;
	hasWallBrush_ = true;
	selectedPartIndex_ = wallBrushStorage_.wallParts.empty() ? -1 : 0;
	selectedItemIndex_ = -1;
	selectedDoorIndex_ = -1;

	PopulateFields();
	SetFieldsEnabled(true);
	SetStatusMessage("Wall brush loaded from materials.db.");
	Layout();
	return true;
}

void MaterialsWorkbenchWallPanel::PopulateFields() {
	const BrushRecord &brush = wallBrushStorage_.brush;
	titleLabel_->SetLabel("Editing wall brush: " + brush.name);
	subtitleLabel_->SetLabel("Edit wall parts, alternates and door definitions visually before polishing the full wall authoring flow.");
	summaryLabel_->SetLabel(wxString::Format(
		"Wall parts: %zu | Links: %zu | Source: %s",
		wallBrushStorage_.wallParts.size(),
		wallBrushStorage_.links.size(),
		brush.sourceFile
	));
	brushIdCtrl_->SetValue(wxString::Format("%lld", static_cast<long long>(brush.id)));
	brushNameCtrl_->SetValue(brush.name);
	brushSourceCtrl_->SetValue(brush.sourceFile);

	RefreshPartChoice();
	RefreshSelectedPart();
}

void MaterialsWorkbenchWallPanel::RefreshPartChoice() {
	partChoice_->Clear();
	for (const WallPartRecord &part : wallBrushStorage_.wallParts) {
		partChoice_->Append(part.partType);
	}

	if (wallBrushStorage_.wallParts.empty()) {
		selectedPartIndex_ = -1;
		partChoice_->SetSelection(wxNOT_FOUND);
		return;
	}

	if (selectedPartIndex_ < 0 || selectedPartIndex_ >= static_cast<int>(wallBrushStorage_.wallParts.size())) {
		selectedPartIndex_ = 0;
	}
	partChoice_->SetSelection(selectedPartIndex_);
}

void MaterialsWorkbenchWallPanel::RefreshSelectedPart() {
	const WallPartRecord* part = GetSelectedPart();
	if (!part) {
		partSummaryLabel_->SetLabel("This wall brush has no editable wall parts yet.");
		itemGridSizer_->Clear(true);
		doorGridSizer_->Clear(true);
		itemButtons_.clear();
		doorButtons_.clear();
		SyncSelectedItemEditor();
		SyncSelectedDoorEditor();
		Layout();
		return;
	}

	partSummaryLabel_->SetLabel(wxString::Format(
		"Part `%s` | Items: %zu | Doors: %zu",
		part->partType,
		part->items.size(),
		part->doors.size()
	));

	if (selectedItemIndex_ >= static_cast<int>(part->items.size())) {
		selectedItemIndex_ = -1;
	}
	if (selectedDoorIndex_ >= static_cast<int>(part->doors.size())) {
		selectedDoorIndex_ = -1;
	}

	RefreshItemGrid();
	RefreshDoorGrid();
	SyncSelectedItemEditor();
	SyncSelectedDoorEditor();
	Layout();
}

void MaterialsWorkbenchWallPanel::RefreshItemGrid() {
	itemGridSizer_->Clear(true);
	itemButtons_.clear();

	const WallPartRecord* part = GetSelectedPart();
	if (!part) {
		itemGridScroll_->Layout();
		return;
	}

	for (size_t i = 0; i < part->items.size(); ++i) {
		const WallPartItemRecord &item = part->items[i];
		wxPanel* cell = new wxPanel(itemGridScroll_, wxID_ANY);
		wxBoxSizer* cellSizer = new wxBoxSizer(wxVERTICAL);
		auto* button = new WallWorkspaceToggleButton(cell, item.itemId);
		button->SetValue(static_cast<int>(i) == selectedItemIndex_);
		button->Bind(wxEVT_LEFT_DOWN, [this, index = static_cast<int>(i)](wxMouseEvent &event) {
			selectedItemIndex_ = index;
			CallAfter([this]() {
				RefreshItemGrid();
				SyncSelectedItemEditor();
			});
		});
		cellSizer->Add(button, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(4));
		cellSizer->Add(new wxStaticText(cell, wxID_ANY, wxString::Format("id %d", item.itemId)), 0, wxALIGN_CENTER);
		cellSizer->Add(new wxStaticText(cell, wxID_ANY, wxString::Format("chance %d", item.chance)), 0, wxALIGN_CENTER);
		cell->SetSizer(cellSizer);
		itemGridSizer_->Add(cell, 0, wxALL, FromDIP(4));
		itemButtons_.push_back(button);
	}

	itemGridScroll_->FitInside();
	itemGridScroll_->Layout();
}

void MaterialsWorkbenchWallPanel::RefreshDoorGrid() {
	doorGridSizer_->Clear(true);
	doorButtons_.clear();

	const WallPartRecord* part = GetSelectedPart();
	if (!part) {
		doorGridScroll_->Layout();
		return;
	}

	for (size_t i = 0; i < part->doors.size(); ++i) {
		const WallPartDoorRecord &door = part->doors[i];
		wxPanel* cell = new wxPanel(doorGridScroll_, wxID_ANY);
		wxBoxSizer* cellSizer = new wxBoxSizer(wxVERTICAL);
		auto* button = new WallWorkspaceToggleButton(cell, door.itemId);
		button->SetValue(static_cast<int>(i) == selectedDoorIndex_);
		button->Bind(wxEVT_LEFT_DOWN, [this, index = static_cast<int>(i)](wxMouseEvent &event) {
			selectedDoorIndex_ = index;
			CallAfter([this]() {
				RefreshDoorGrid();
				SyncSelectedDoorEditor();
			});
		});
		cellSizer->Add(button, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(4));
		cellSizer->Add(new wxStaticText(cell, wxID_ANY, wxString::Format("id %d", door.itemId)), 0, wxALIGN_CENTER);
		cellSizer->Add(new wxStaticText(cell, wxID_ANY, door.doorType), 0, wxALIGN_CENTER);
		cellSizer->Add(new wxStaticText(cell, wxID_ANY, door.isOpen ? "open" : "closed"), 0, wxALIGN_CENTER);
		cell->SetSizer(cellSizer);
		doorGridSizer_->Add(cell, 0, wxALL, FromDIP(4));
		doorButtons_.push_back(button);
	}

	doorGridScroll_->FitInside();
	doorGridScroll_->Layout();
}

void MaterialsWorkbenchWallPanel::SyncSelectedItemEditor() {
	const WallPartRecord* part = GetSelectedPart();
	if (!part || selectedItemIndex_ < 0 || selectedItemIndex_ >= static_cast<int>(part->items.size())) {
		selectedItemLabel_->SetLabel("No wall item selected");
		itemIdCtrl_->SetValue(0);
		itemChanceCtrl_->SetValue(0);
		itemPreviewButton_->SetSprite(0);
		return;
	}

	const WallPartItemRecord &item = part->items[selectedItemIndex_];
	selectedItemLabel_->SetLabel(wxString::Format("Editing item #%d", selectedItemIndex_ + 1));
	itemIdCtrl_->SetValue(item.itemId);
	itemChanceCtrl_->SetValue(item.chance);
	itemPreviewButton_->SetSprite(item.itemId);
}

void MaterialsWorkbenchWallPanel::SyncSelectedDoorEditor() {
	const WallPartRecord* part = GetSelectedPart();
	if (!part || selectedDoorIndex_ < 0 || selectedDoorIndex_ >= static_cast<int>(part->doors.size())) {
		selectedDoorLabel_->SetLabel("No door selected");
		doorItemIdCtrl_->SetValue(0);
		doorTypeChoice_->SetSelection(wxNOT_FOUND);
		doorOpenCtrl_->SetValue(false);
		doorHateCtrl_->SetValue(false);
		doorPreviewButton_->SetSprite(0);
		return;
	}

	const WallPartDoorRecord &door = part->doors[selectedDoorIndex_];
	selectedDoorLabel_->SetLabel("Editing door: " + DescribeDoor(door));
	doorItemIdCtrl_->SetValue(door.itemId);
	doorTypeChoice_->SetStringSelection(door.doorType);
	doorOpenCtrl_->SetValue(door.isOpen);
	doorHateCtrl_->SetValue(door.wallHateMe);
	doorPreviewButton_->SetSprite(door.itemId);
}

void MaterialsWorkbenchWallPanel::NormalizeWallParts() {
	for (size_t i = 0; i < wallBrushStorage_.wallParts.size(); ++i) {
		wallBrushStorage_.wallParts[i].sortOrder = static_cast<int>(i);
		NormalizeWallPartRecord(wallBrushStorage_.wallParts[i]);
	}
}

void MaterialsWorkbenchWallPanel::SetStatusMessage(const wxString &message) {
	statusLabel_->SetLabel(message);
}

void MaterialsWorkbenchWallPanel::SetFieldsEnabled(bool enabled) {
	partChoice_->Enable(enabled);
	itemIdCtrl_->Enable(enabled);
	itemChanceCtrl_->Enable(enabled);
	doorItemIdCtrl_->Enable(enabled);
	doorTypeChoice_->Enable(enabled);
	doorOpenCtrl_->Enable(enabled);
	doorHateCtrl_->Enable(enabled);
	itemPreviewButton_->Enable(enabled);
	doorPreviewButton_->Enable(enabled);
}

bool MaterialsWorkbenchWallPanel::SaveCurrentWallBrush() {
	if (!hasWallBrush_) {
		SetStatusMessage("Select a wall brush before saving.");
		return false;
	}

	NormalizeWallParts();

	wxString error;
	if (!controller_.SaveWallBrushParts(wallBrushStorage_, error)) {
		SetStatusMessage("Failed to save wall brush parts: " + error);
		return false;
	}

	SetStatusMessage("Wall brush parts saved to materials.db.");
	if (onWallBrushSaved_) {
		onWallBrushSaved_(wallBrushStorage_.brush.id);
	}
	return true;
}

void MaterialsWorkbenchWallPanel::OnPartChanged(wxCommandEvent &event) {
	selectedPartIndex_ = partChoice_->GetSelection();
	selectedItemIndex_ = -1;
	selectedDoorIndex_ = -1;
	RefreshSelectedPart();
	event.Skip();
}

void MaterialsWorkbenchWallPanel::OnPickItem(wxCommandEvent &event) {
	if (!GetSelectedPart()) {
		SetStatusMessage("Select a wall part before choosing an item.");
		return;
	}

	FindItemDialog dialog(this, "Select Wall Item");
	dialog.setSearchMode(FindItemDialog::ItemIDs);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	itemIdCtrl_->SetValue(dialog.getResultID());
	itemPreviewButton_->SetSprite(dialog.getResultID());
}

void MaterialsWorkbenchWallPanel::OnApplyItem(wxCommandEvent &event) {
	WallPartRecord* part = GetSelectedPart();
	if (!part) {
		SetStatusMessage("Select a wall part before applying an item.");
		return;
	}

	WallPartItemRecord record;
	record.itemId = itemIdCtrl_->GetValue();
	record.chance = itemChanceCtrl_->GetValue();
	if (record.itemId <= 0) {
		SetStatusMessage("Wall item id must be greater than zero.");
		return;
	}

	if (selectedItemIndex_ >= 0 && selectedItemIndex_ < static_cast<int>(part->items.size())) {
		record.sortOrder = part->items[selectedItemIndex_].sortOrder;
		part->items[selectedItemIndex_] = record;
	} else {
		record.sortOrder = static_cast<int>(part->items.size());
		part->items.push_back(record);
		selectedItemIndex_ = static_cast<int>(part->items.size()) - 1;
	}

	NormalizeWallPartRecord(*part);
	RefreshSelectedPart();
	SetStatusMessage("Wall item updated locally. Save the wall brush to persist.");
}

void MaterialsWorkbenchWallPanel::OnRemoveItem(wxCommandEvent &event) {
	WallPartRecord* part = GetSelectedPart();
	if (!part || selectedItemIndex_ < 0 || selectedItemIndex_ >= static_cast<int>(part->items.size())) {
		SetStatusMessage("Select a wall item before removing it.");
		return;
	}

	part->items.erase(part->items.begin() + selectedItemIndex_);
	selectedItemIndex_ = -1;
	NormalizeWallPartRecord(*part);
	RefreshSelectedPart();
	SetStatusMessage("Wall item removed locally. Save the wall brush to persist.");
}

void MaterialsWorkbenchWallPanel::OnPickDoorItem(wxCommandEvent &event) {
	if (!GetSelectedPart()) {
		SetStatusMessage("Select a wall part before choosing a door item.");
		return;
	}

	FindItemDialog dialog(this, "Select Door Item");
	dialog.setSearchMode(FindItemDialog::ItemIDs);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	doorItemIdCtrl_->SetValue(dialog.getResultID());
	doorPreviewButton_->SetSprite(dialog.getResultID());
}

void MaterialsWorkbenchWallPanel::OnApplyDoor(wxCommandEvent &event) {
	WallPartRecord* part = GetSelectedPart();
	if (!part) {
		SetStatusMessage("Select a wall part before applying a door.");
		return;
	}
	if (doorTypeChoice_->GetSelection() == wxNOT_FOUND) {
		SetStatusMessage("Select a door type before applying the door.");
		return;
	}

	WallPartDoorRecord record;
	record.itemId = doorItemIdCtrl_->GetValue();
	record.doorType = doorTypeChoice_->GetStringSelection();
	record.isOpen = doorOpenCtrl_->GetValue();
	record.wallHateMe = doorHateCtrl_->GetValue();
	if (record.itemId <= 0) {
		SetStatusMessage("Door item id must be greater than zero.");
		return;
	}

	if (selectedDoorIndex_ >= 0 && selectedDoorIndex_ < static_cast<int>(part->doors.size())) {
		record.sortOrder = part->doors[selectedDoorIndex_].sortOrder;
		part->doors[selectedDoorIndex_] = record;
	} else {
		record.sortOrder = static_cast<int>(part->doors.size());
		part->doors.push_back(record);
		selectedDoorIndex_ = static_cast<int>(part->doors.size()) - 1;
	}

	NormalizeWallPartRecord(*part);
	RefreshSelectedPart();
	SetStatusMessage("Door updated locally. Save the wall brush to persist.");
}

void MaterialsWorkbenchWallPanel::OnRemoveDoor(wxCommandEvent &event) {
	WallPartRecord* part = GetSelectedPart();
	if (!part || selectedDoorIndex_ < 0 || selectedDoorIndex_ >= static_cast<int>(part->doors.size())) {
		SetStatusMessage("Select a door before removing it.");
		return;
	}

	part->doors.erase(part->doors.begin() + selectedDoorIndex_);
	selectedDoorIndex_ = -1;
	NormalizeWallPartRecord(*part);
	RefreshSelectedPart();
	SetStatusMessage("Door removed locally. Save the wall brush to persist.");
}

void MaterialsWorkbenchWallPanel::OnSave(wxCommandEvent &event) {
	SaveCurrentWallBrush();
}

void MaterialsWorkbenchWallPanel::OnRevert(wxCommandEvent &event) {
	if (!hasWallBrush_) {
		ClearWorkspace("Select a wall brush in the navigation tree to edit its wall parts.");
		return;
	}

	if (!LoadWallBrush(currentContextKey_, currentItemIndex_)) {
		return;
	}

	SetStatusMessage("Wall brush reloaded from materials.db.");
}

void MaterialsWorkbenchWallPanel::OnItemIdChanged(wxCommandEvent &event) {
	itemPreviewButton_->SetSprite(itemIdCtrl_->GetValue());
	event.Skip();
}

void MaterialsWorkbenchWallPanel::OnItemIdSpin(wxSpinEvent &event) {
	itemPreviewButton_->SetSprite(itemIdCtrl_->GetValue());
	event.Skip();
}

void MaterialsWorkbenchWallPanel::OnDoorItemIdChanged(wxCommandEvent &event) {
	doorPreviewButton_->SetSprite(doorItemIdCtrl_->GetValue());
	event.Skip();
}

void MaterialsWorkbenchWallPanel::OnDoorItemIdSpin(wxSpinEvent &event) {
	doorPreviewButton_->SetSprite(doorItemIdCtrl_->GetValue());
	event.Skip();
}
