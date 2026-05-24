#include "main.h"

#include "materials_workbench_wall_panel.h"

#include <algorithm>
#include <limits>
#include <utility>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/msgdlg.h>
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
#include "items.h"
#include "materials_workbench_controller.h"
#include "wall_brush.h"

namespace {
	enum class WallPanelDoorFamily {
		Unknown,
		Door,
		Window,
	};

	struct WallPanelDoorTypeSpec {
		bool valid = false;
		bool allowAny = false;
		bool expectsExact = false;
		::DoorType exactType = WALL_UNDEFINED;
		WallPanelDoorFamily family = WallPanelDoorFamily::Unknown;
		wxString normalizedLabel;
	};

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

	template <typename T, typename Compare>
	bool WallPanelVectorsEqual(const std::vector<T> &left, const std::vector<T> &right, Compare compare) {
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

	bool AreWallPanelPartItemRecordsEqual(const WallPartItemRecord &left, const WallPartItemRecord &right) {
		return left.itemId == right.itemId &&
			   left.chance == right.chance &&
			   left.sortOrder == right.sortOrder;
	}

	bool AreWallPanelPartDoorRecordsEqual(const WallPartDoorRecord &left, const WallPartDoorRecord &right) {
		return left.itemId == right.itemId &&
			   left.doorType == right.doorType &&
			   left.isOpen == right.isOpen &&
			   left.wallHateMe == right.wallHateMe &&
			   left.sortOrder == right.sortOrder;
	}

	bool AreWallPanelPartRecordsEqual(const WallPartRecord &left, const WallPartRecord &right) {
		return left.partType == right.partType &&
			   left.sortOrder == right.sortOrder &&
			   WallPanelVectorsEqual(left.items, right.items, AreWallPanelPartItemRecordsEqual) &&
			   WallPanelVectorsEqual(left.doors, right.doors, AreWallPanelPartDoorRecordsEqual);
	}

	bool IsKnownWallPanelItemId(int itemId) {
		if (itemId <= 0 || itemId > std::numeric_limits<uint16_t>::max()) {
			return false;
		}
		return g_items.isValidID(static_cast<uint16_t>(itemId));
	}

	int GetWallPanelMaxEditableItemId() {
		return static_cast<int>(std::min<uint32_t>(g_items.getMaxID(), std::numeric_limits<uint16_t>::max()));
	}

	WallPanelDoorFamily GetWallPanelDoorFamily(::DoorType doorType) {
		switch (doorType) {
		case WALL_ARCHWAY:
		case WALL_DOOR_NORMAL:
		case WALL_DOOR_LOCKED:
		case WALL_DOOR_QUEST:
		case WALL_DOOR_MAGIC:
			return WallPanelDoorFamily::Door;
		case WALL_WINDOW:
		case WALL_HATCH_WINDOW:
			return WallPanelDoorFamily::Window;
		default:
			return WallPanelDoorFamily::Unknown;
		}
	}

	wxString DescribeWallPanelDoorType(::DoorType doorType) {
		switch (doorType) {
		case WALL_ARCHWAY:
			return "archway";
		case WALL_DOOR_NORMAL:
			return "normal";
		case WALL_DOOR_LOCKED:
			return "locked";
		case WALL_DOOR_QUEST:
			return "quest";
		case WALL_DOOR_MAGIC:
			return "magic";
		case WALL_WINDOW:
			return "window";
		case WALL_HATCH_WINDOW:
			return "hatch_window";
		default:
			return "unknown";
		}
	}

	WallPanelDoorTypeSpec ParseWallPanelDoorTypeSpec(const wxString &doorType) {
		WallPanelDoorTypeSpec spec;
		const wxString normalized = doorType.Lower();
		spec.normalizedLabel = normalized;

		if (normalized == "archway") {
			spec.valid = true;
			spec.expectsExact = true;
			spec.exactType = WALL_ARCHWAY;
			spec.family = WallPanelDoorFamily::Door;
		} else if (normalized == "normal") {
			spec.valid = true;
			spec.expectsExact = true;
			spec.exactType = WALL_DOOR_NORMAL;
			spec.family = WallPanelDoorFamily::Door;
		} else if (normalized == "locked") {
			spec.valid = true;
			spec.expectsExact = true;
			spec.exactType = WALL_DOOR_LOCKED;
			spec.family = WallPanelDoorFamily::Door;
		} else if (normalized == "quest") {
			spec.valid = true;
			spec.expectsExact = true;
			spec.exactType = WALL_DOOR_QUEST;
			spec.family = WallPanelDoorFamily::Door;
		} else if (normalized == "magic") {
			spec.valid = true;
			spec.expectsExact = true;
			spec.exactType = WALL_DOOR_MAGIC;
			spec.family = WallPanelDoorFamily::Door;
		} else if (normalized == "window") {
			spec.valid = true;
			spec.expectsExact = true;
			spec.exactType = WALL_WINDOW;
			spec.family = WallPanelDoorFamily::Window;
		} else if (normalized == "hatch_window" || normalized == "hatch window") {
			spec.valid = true;
			spec.expectsExact = true;
			spec.exactType = WALL_HATCH_WINDOW;
			spec.family = WallPanelDoorFamily::Window;
			spec.normalizedLabel = "hatch_window";
		} else if (normalized == "any door") {
			spec.valid = true;
			spec.family = WallPanelDoorFamily::Door;
		} else if (normalized == "any window") {
			spec.valid = true;
			spec.family = WallPanelDoorFamily::Window;
		} else if (normalized == "any") {
			spec.valid = true;
			spec.allowAny = true;
		}

		return spec;
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

void MaterialsWorkbenchWallPanel::SetOnWallBrushStateChanged(std::function<void()> callback) {
	onWallBrushStateChanged_ = std::move(callback);
}

bool MaterialsWorkbenchWallPanel::HasPendingChanges() const {
	return hasWallBrush_ && dirty_;
}

bool MaterialsWorkbenchWallPanel::IsCurrentWallSelection(const wxString &contextKey, int itemIndex) const {
	return hasWallBrush_ && currentContextKey_ == contextKey && currentItemIndex_ == itemIndex;
}

wxString MaterialsWorkbenchWallPanel::GetCurrentWallDisplayName() const {
	return hasWallBrush_ ? wallBrushStorage_.brush.name : "";
}

bool MaterialsWorkbenchWallPanel::ResolvePendingChangesBeforeSwitch(wxWindow* parent, const wxString &targetLabel) {
	if (!HasPendingChanges()) {
		return true;
	}

	const wxString destination = targetLabel.IsEmpty() ? "the selected entry" : "\"" + targetLabel + "\"";
	wxMessageDialog dialog(
		parent,
		"Wall brush \"" + wallBrushStorage_.brush.name + "\" has unsaved changes.\n\n"
		"You are switching to " + destination + ".\n\n"
		"Yes: save and continue\n"
		"No: discard local changes and continue\n"
		"Cancel: stay on the current wall brush",
		"Unsaved Wall Changes",
		wxYES_NO | wxCANCEL | wxICON_WARNING
	);
	dialog.SetYesNoCancelLabels("Save", "Discard", "Cancel");

	switch (dialog.ShowModal()) {
	case wxID_YES:
		return SaveCurrentWallBrush();
	case wxID_NO:
		return LoadWallBrush(currentContextKey_, currentItemIndex_);
	default:
		SetStatusMessage("Selection change canceled. Pending wall edits were kept.");
		return false;
	}
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
	itemIdCtrl_->SetRange(0, GetWallPanelMaxEditableItemId());
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
	doorItemIdCtrl_->SetRange(0, GetWallPanelMaxEditableItemId());
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
	saveButton_ = new wxButton(this, wxID_SAVE, "Save Wall Brush");
	revertButton_ = new wxButton(this, wxID_ANY, "Revert");
	actionSizer->Add(saveButton_, 0, wxRIGHT, FromDIP(6));
	actionSizer->Add(revertButton_, 0);

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
	saveButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchWallPanel::OnSave, this);
	revertButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchWallPanel::OnRevert, this);
	itemIdCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchWallPanel::OnItemIdChanged, this);
	itemIdCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchWallPanel::OnItemIdSpin, this);
	doorItemIdCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchWallPanel::OnDoorItemIdChanged, this);
	doorItemIdCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchWallPanel::OnDoorItemIdSpin, this);
}

void MaterialsWorkbenchWallPanel::ClearWorkspace(const wxString &message) {
	wallBrushStorage_ = BrushStorageRecord();
	loadedWallBrushStorage_ = BrushStorageRecord();
	currentContextKey_.clear();
	currentItemIndex_ = -1;
	selectedPartIndex_ = -1;
	selectedItemIndex_ = -1;
	selectedDoorIndex_ = -1;
	hasWallBrush_ = false;
	dirty_ = false;

	UpdateWorkspaceHeader();
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
	UpdateActionButtons();
	NotifyWallBrushStateChanged();
	SetStatusMessage(message);
	Layout();
}

bool MaterialsWorkbenchWallPanel::LoadWallBrush(const wxString &contextKey, int itemIndex) {
	const int64_t previousBrushId = hasWallBrush_ ? wallBrushStorage_.brush.id : 0;
	const WallEditorState previousEditorState = hasWallBrush_ ? CaptureEditorState() : WallEditorState();

	wxString error;
	BrushStorageRecord storage;
	if (!controller_.GetBrushDetails(contextKey, itemIndex, storage, error)) {
		spdlog::warn(
			"Materials Workbench failed to load wall brush details: context='{}' index={} error='{}'",
			contextKey.ToStdString(),
			itemIndex,
			error.ToStdString()
		);
		ClearWorkspace("Failed to load wall brush details: " + error);
		return false;
	}

	BrushStorageRecord comparableStorage = storage;
	for (WallPartRecord &part : comparableStorage.wallParts) {
		NormalizeWallPartRecord(part);
	}
	for (size_t i = 0; i < comparableStorage.wallParts.size(); ++i) {
		comparableStorage.wallParts[i].sortOrder = static_cast<int>(i);
	}

	const bool preserveEditorState = previousEditorState.valid && previousBrushId == comparableStorage.brush.id;
	wallBrushStorage_ = storage;
	loadedWallBrushStorage_ = comparableStorage;
	currentContextKey_ = contextKey;
	currentItemIndex_ = itemIndex;
	hasWallBrush_ = true;
	dirty_ = false;
	selectedPartIndex_ = storage.wallParts.empty() ? -1 : 0;
	selectedItemIndex_ = -1;
	selectedDoorIndex_ = -1;

	PopulateFields();
	if (preserveEditorState) {
		RestoreEditorState(previousEditorState);
	}
	SetFieldsEnabled(true);
	UpdateActionButtons();
	NotifyWallBrushStateChanged();
	SetStatusMessage("Wall brush loaded from materials.db.");
	spdlog::info(
		"Materials Workbench loaded wall brush from materials.db: id={} name='{}' preserved_context={}",
		static_cast<long long>(wallBrushStorage_.brush.id),
		wallBrushStorage_.brush.name.ToStdString(),
		preserveEditorState
	);
	Layout();
	return true;
}

void MaterialsWorkbenchWallPanel::PopulateFields() {
	const BrushRecord &brush = wallBrushStorage_.brush;
	UpdateWorkspaceHeader();
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

BrushStorageRecord MaterialsWorkbenchWallPanel::BuildComparableStorageFromCurrentState() const {
	BrushStorageRecord storage = wallBrushStorage_;
	for (size_t i = 0; i < storage.wallParts.size(); ++i) {
		storage.wallParts[i].sortOrder = static_cast<int>(i);
		NormalizeWallPartRecord(storage.wallParts[i]);
	}
	return storage;
}

MaterialsWorkbenchWallPanel::WallEditorState MaterialsWorkbenchWallPanel::CaptureEditorState() const {
	WallEditorState state;
	state.valid = hasWallBrush_;

	const WallPartRecord* part = GetSelectedPart();
	if (!part) {
		return state;
	}

	state.partType = part->partType;
	if (selectedItemIndex_ >= 0 && selectedItemIndex_ < static_cast<int>(part->items.size())) {
		state.itemSortOrder = part->items[selectedItemIndex_].sortOrder;
		state.itemId = part->items[selectedItemIndex_].itemId;
	}
	if (selectedDoorIndex_ >= 0 && selectedDoorIndex_ < static_cast<int>(part->doors.size())) {
		const WallPartDoorRecord &door = part->doors[selectedDoorIndex_];
		state.doorSortOrder = door.sortOrder;
		state.doorItemId = door.itemId;
		state.doorType = door.doorType;
		state.doorIsOpen = door.isOpen;
		state.doorWallHateMe = door.wallHateMe;
	}
	return state;
}

void MaterialsWorkbenchWallPanel::RestoreEditorState(const WallEditorState &state) {
	if (!state.valid || !hasWallBrush_) {
		return;
	}

	selectedPartIndex_ = 0;
	for (size_t i = 0; i < wallBrushStorage_.wallParts.size(); ++i) {
		if (wallBrushStorage_.wallParts[i].partType == state.partType) {
			selectedPartIndex_ = static_cast<int>(i);
			break;
		}
	}

	const WallPartRecord* part = GetSelectedPart();
	selectedItemIndex_ = -1;
	selectedDoorIndex_ = -1;
	if (!part) {
		RefreshPartChoice();
		RefreshSelectedPart();
		return;
	}

	for (size_t i = 0; i < part->items.size(); ++i) {
		const WallPartItemRecord &item = part->items[i];
		if (item.sortOrder == state.itemSortOrder && item.itemId == state.itemId) {
			selectedItemIndex_ = static_cast<int>(i);
			break;
		}
	}
	if (selectedItemIndex_ == -1 && state.itemId > 0) {
		for (size_t i = 0; i < part->items.size(); ++i) {
			if (part->items[i].itemId == state.itemId) {
				selectedItemIndex_ = static_cast<int>(i);
				break;
			}
		}
	}

	for (size_t i = 0; i < part->doors.size(); ++i) {
		const WallPartDoorRecord &door = part->doors[i];
		if (door.sortOrder == state.doorSortOrder &&
			door.itemId == state.doorItemId &&
			door.doorType == state.doorType &&
			door.isOpen == state.doorIsOpen &&
			door.wallHateMe == state.doorWallHateMe) {
			selectedDoorIndex_ = static_cast<int>(i);
			break;
		}
	}
	if (selectedDoorIndex_ == -1 && state.doorItemId > 0) {
		for (size_t i = 0; i < part->doors.size(); ++i) {
			const WallPartDoorRecord &door = part->doors[i];
			if (door.itemId == state.doorItemId && door.doorType == state.doorType) {
				selectedDoorIndex_ = static_cast<int>(i);
				break;
			}
		}
	}

	RefreshPartChoice();
	RefreshSelectedPart();
}

void MaterialsWorkbenchWallPanel::RefreshDirtyState() {
	if (!hasWallBrush_) {
		dirty_ = false;
		UpdateWorkspaceHeader();
		UpdateActionButtons();
		NotifyWallBrushStateChanged();
		return;
	}

	dirty_ = !WallPanelVectorsEqual(
		BuildComparableStorageFromCurrentState().wallParts,
		loadedWallBrushStorage_.wallParts,
		AreWallPanelPartRecordsEqual
	);
	UpdateWorkspaceHeader();
	UpdateActionButtons();
	NotifyWallBrushStateChanged();
}

void MaterialsWorkbenchWallPanel::NotifyWallBrushStateChanged() {
	if (onWallBrushStateChanged_) {
		onWallBrushStateChanged_();
	}
}

void MaterialsWorkbenchWallPanel::UpdateWorkspaceHeader() {
	if (!hasWallBrush_) {
		titleLabel_->SetLabel("No wall brush selected");
		subtitleLabel_->SetLabel("Select a wall brush in the navigation tree to edit its wall parts.");
		return;
	}

	titleLabel_->SetLabel("Editing wall brush: " + wallBrushStorage_.brush.name + (dirty_ ? " [modified]" : ""));
	subtitleLabel_->SetLabel(
		dirty_
			? "Local wall edits differ from materials.db. Save, revert or switch entries carefully."
			: "Edit wall parts, alternates and door definitions visually before polishing the full wall authoring flow."
	);
}

void MaterialsWorkbenchWallPanel::UpdateActionButtons() {
	if (saveButton_) {
		saveButton_->Enable(hasWallBrush_ && dirty_);
	}
	if (revertButton_) {
		revertButton_->Enable(hasWallBrush_ && dirty_);
	}
}

bool MaterialsWorkbenchWallPanel::ValidateWallBrushStorage(wxString &error) const {
	for (size_t partIndex = 0; partIndex < wallBrushStorage_.wallParts.size(); ++partIndex) {
		const WallPartRecord &part = wallBrushStorage_.wallParts[partIndex];

		for (size_t itemIndex = 0; itemIndex < part.items.size(); ++itemIndex) {
			const WallPartItemRecord &item = part.items[itemIndex];
			if (item.itemId <= 0) {
				error = wxString::Format("Wall part %zu item %zu must use an item id greater than zero.", partIndex + 1, itemIndex + 1);
				return false;
			}
			if (!IsKnownWallPanelItemId(item.itemId)) {
				error = wxString::Format("Wall part %zu item %zu uses unknown item id %d.", partIndex + 1, itemIndex + 1, item.itemId);
				return false;
			}
		}

		for (size_t doorIndex = 0; doorIndex < part.doors.size(); ++doorIndex) {
			const WallPartDoorRecord &door = part.doors[doorIndex];
			if (door.itemId <= 0) {
				error = wxString::Format("Wall part %zu door %zu must use an item id greater than zero.", partIndex + 1, doorIndex + 1);
				return false;
			}
			if (!IsKnownWallPanelItemId(door.itemId)) {
				error = wxString::Format("Wall part %zu door %zu uses unknown item id %d.", partIndex + 1, doorIndex + 1, door.itemId);
				return false;
			}

			const WallPanelDoorTypeSpec doorTypeSpec = ParseWallPanelDoorTypeSpec(door.doorType);
			if (!doorTypeSpec.valid) {
				error = wxString::Format(
					"Wall part %zu door %zu uses unsupported door type \"%s\".",
					partIndex + 1,
					doorIndex + 1,
					door.doorType
				);
				return false;
			}

			const ItemType &itemType = g_items.getItemType(static_cast<uint16_t>(door.itemId));
			if (!itemType.isWall || !itemType.isBrushDoor || !itemType.brush || !itemType.brush->isWall()) {
				error = wxString::Format(
					"Wall part %zu door %zu item id %d is not registered as a wall door/window item.",
					partIndex + 1,
					doorIndex + 1,
					door.itemId
				);
				return false;
			}
			if (itemType.isOpen != door.isOpen) {
				error = wxString::Format(
					"Wall part %zu door %zu uses item id %d with open=%s, but the selected record is %s.",
					partIndex + 1,
					doorIndex + 1,
					door.itemId,
					itemType.isOpen ? "true" : "false",
					door.isOpen ? "open" : "closed"
				);
				return false;
			}
			if (itemType.wall_hate_me != door.wallHateMe) {
				error = wxString::Format(
					"Wall part %zu door %zu uses item id %d with hate=%s, but the selected record is %s.",
					partIndex + 1,
					doorIndex + 1,
					door.itemId,
					itemType.wall_hate_me ? "true" : "false",
					door.wallHateMe ? "hate" : "not hate"
				);
				return false;
			}

			const ::DoorType runtimeDoorType = itemType.brush->asWall()->getDoorTypeFromID(static_cast<uint16_t>(door.itemId));
			if (runtimeDoorType == WALL_UNDEFINED) {
				error = wxString::Format(
					"Wall part %zu door %zu item id %d is not mapped to a wall door type in the runtime catalog.",
					partIndex + 1,
					doorIndex + 1,
					door.itemId
				);
				return false;
			}
			const WallPanelDoorFamily runtimeDoorFamily = GetWallPanelDoorFamily(runtimeDoorType);
			if (doorTypeSpec.expectsExact && runtimeDoorType != doorTypeSpec.exactType) {
				error = wxString::Format(
					"Wall part %zu door %zu uses item id %d for \"%s\", but the runtime door type is \"%s\".",
					partIndex + 1,
					doorIndex + 1,
					door.itemId,
					doorTypeSpec.normalizedLabel,
					DescribeWallPanelDoorType(runtimeDoorType)
				);
				return false;
			}
			if (!doorTypeSpec.allowAny &&
				doorTypeSpec.family != WallPanelDoorFamily::Unknown &&
				runtimeDoorFamily != WallPanelDoorFamily::Unknown &&
				runtimeDoorFamily != doorTypeSpec.family) {
				error = wxString::Format(
					"Wall part %zu door %zu uses item id %d for \"%s\", but the item belongs to \"%s\".",
					partIndex + 1,
					doorIndex + 1,
					door.itemId,
					doorTypeSpec.normalizedLabel,
					DescribeWallPanelDoorType(runtimeDoorType)
				);
				return false;
			}
		}
	}

	return true;
}

bool MaterialsWorkbenchWallPanel::SaveCurrentWallBrush() {
	if (!hasWallBrush_) {
		SetStatusMessage("Select a wall brush before saving.");
		return false;
	}

	if (!dirty_) {
		SetStatusMessage("No pending wall changes to save.");
		return true;
	}

	const WallEditorState previousEditorState = CaptureEditorState();
	wallBrushStorage_ = BuildComparableStorageFromCurrentState();

	wxString validationError;
	if (!ValidateWallBrushStorage(validationError)) {
		spdlog::warn(
			"Materials Workbench blocked wall brush save for id={} name='{}': {}",
			static_cast<long long>(wallBrushStorage_.brush.id),
			wallBrushStorage_.brush.name.ToStdString(),
			validationError.ToStdString()
		);
		SetStatusMessage("Cannot save wall brush: " + validationError);
		return false;
	}

	wxString error;
	if (!controller_.SaveWallBrushParts(wallBrushStorage_, error)) {
		spdlog::warn(
			"Materials Workbench failed to save wall brush parts after validation: id={} name='{}' error='{}'",
			static_cast<long long>(wallBrushStorage_.brush.id),
			wallBrushStorage_.brush.name.ToStdString(),
			error.ToStdString()
		);
		SetStatusMessage("Failed to save wall brush parts: " + error);
		return false;
	}

	loadedWallBrushStorage_ = wallBrushStorage_;
	PopulateFields();
	if (previousEditorState.valid) {
		RestoreEditorState(previousEditorState);
	}
	RefreshDirtyState();
	SetStatusMessage("Wall brush parts saved to materials.db.");
	spdlog::info(
		"Materials Workbench saved wall brush parts: id={} name='{}' preserved_context={}",
		static_cast<long long>(wallBrushStorage_.brush.id),
		wallBrushStorage_.brush.name.ToStdString(),
		previousEditorState.valid
	);
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
	if (!IsKnownWallPanelItemId(record.itemId)) {
		SetStatusMessage(wxString::Format("Wall item id %d is not present in the current item catalog.", record.itemId));
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
	RefreshDirtyState();
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
	RefreshDirtyState();
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
	if (!IsKnownWallPanelItemId(record.itemId)) {
		SetStatusMessage(wxString::Format("Door item id %d is not present in the current item catalog.", record.itemId));
		return;
	}

	const bool replacingExistingDoor = selectedDoorIndex_ >= 0 && selectedDoorIndex_ < static_cast<int>(part->doors.size());
	WallPartDoorRecord previousDoorRecord;
	if (replacingExistingDoor) {
		previousDoorRecord = part->doors[selectedDoorIndex_];
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
	wxString validationError;
	if (!ValidateWallBrushStorage(validationError)) {
		spdlog::warn(
			"Materials Workbench blocked wall door apply for brush id={} name='{}' part='{}' item_id={} type='{}': {}",
			static_cast<long long>(wallBrushStorage_.brush.id),
			wallBrushStorage_.brush.name.ToStdString(),
			part->partType.ToStdString(),
			record.itemId,
			record.doorType.ToStdString(),
			validationError.ToStdString()
		);
		if (replacingExistingDoor && selectedDoorIndex_ >= 0 && selectedDoorIndex_ < static_cast<int>(part->doors.size())) {
			part->doors[selectedDoorIndex_] = previousDoorRecord;
		} else if (selectedDoorIndex_ >= 0 && selectedDoorIndex_ < static_cast<int>(part->doors.size())) {
			part->doors.erase(part->doors.begin() + selectedDoorIndex_);
			selectedDoorIndex_ = -1;
		}
		NormalizeWallPartRecord(*part);
		RefreshSelectedPart();
		SetStatusMessage("Cannot apply door: " + validationError);
		return;
	}
	RefreshSelectedPart();
	RefreshDirtyState();
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
	RefreshDirtyState();
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
	const int64_t brushId = wallBrushStorage_.brush.id;
	const wxString brushName = wallBrushStorage_.brush.name;
	if (!dirty_) {
		SetStatusMessage("Wall brush already matches materials.db.");
		return;
	}

	if (!LoadWallBrush(currentContextKey_, currentItemIndex_)) {
		spdlog::warn(
			"Materials Workbench failed to revert wall brush from materials.db: id={} name='{}'",
			static_cast<long long>(brushId),
			brushName.ToStdString()
		);
		return;
	}

	SetStatusMessage("Wall brush reloaded from materials.db.");
	spdlog::info(
		"Materials Workbench reverted wall brush from materials.db: id={} name='{}'",
		static_cast<long long>(brushId),
		brushName.ToStdString()
	);
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
