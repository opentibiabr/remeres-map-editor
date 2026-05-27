#include "main.h"

#include "materials_workbench_palette_panel.h"

#include <algorithm>
#include <utility>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/choicdlg.h>
#include <wx/control.h>
#include <wx/dcbuffer.h>
#include <wx/msgdlg.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>

#include "brush.h"
#include "gui.h"
#include "items.h"
#include "materials_workbench_controller.h"
#include "palette_common.h"
#include "raw_brush.h"

struct BrushGridItem {
	wxString label;
	Brush* brush = nullptr;
	int lookId = 0;
	int index = -1;
};

struct BrushGridTile {
	wxPanel* panel = nullptr;
	BrushButton* button = nullptr;
	wxStaticText* label = nullptr;
	int itemIndex = -1;
};

class MaterialsWorkbenchBrushGridPanel : public wxScrolledWindow {
public:
	explicit MaterialsWorkbenchBrushGridPanel(wxWindow* parent) :
		wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxHSCROLL | wxBORDER_THEME) {
		SetScrollRate(FromDIP(8), FromDIP(8));
		SetBackgroundStyle(wxBG_STYLE_PAINT);

		Bind(wxEVT_PAINT, &MaterialsWorkbenchBrushGridPanel::OnPaint, this);
		Bind(wxEVT_LEFT_DOWN, &MaterialsWorkbenchBrushGridPanel::OnLeftDown, this);
		Bind(wxEVT_SIZE, &MaterialsWorkbenchBrushGridPanel::OnSize, this);
		Bind(wxEVT_MOTION, &MaterialsWorkbenchBrushGridPanel::OnMouseMove, this);
		UpdateVirtualSize();
	}

	void SetSelectionChangedHandler(std::function<void(int)> handler) {
		onSelectionChanged_ = std::move(handler);
	}

	void SetItems(const std::vector<BrushGridItem> &items, int selectedIndex = -1) {
		items_ = items;
		selectedIndex_ = -1;
		UpdateVirtualSize();
		Refresh();

		if (!items_.empty()) {
			if (HasItemIndex(selectedIndex)) {
				SelectIndex(selectedIndex);
			} else {
				SelectIndex(items_.front().index);
			}
		} else if (onSelectionChanged_) {
			onSelectionChanged_(-1);
		}
	}

	void Clear() {
		SetItems({});
	}

	void SelectIndex(int index) {
		if (selectedIndex_ == index) {
			return;
		}
		selectedIndex_ = index;
		Refresh();
		if (onSelectionChanged_) {
			onSelectionChanged_(selectedIndex_);
		}
	}

private:
	int GetSpacing() const {
		return FromDIP(6);
	}

	int GetTileWidth() const {
		return FromDIP(96);
	}

	int GetTileHeight() const {
		return FromDIP(84);
	}

	int GetColumnCount() const {
		const int availableWidth = std::max(GetClientSize().GetWidth() - GetSpacing(), GetTileWidth());
		return std::max(1, availableWidth / (GetTileWidth() + GetSpacing()));
	}

	wxRect GetTileRect(size_t itemPosition) const {
		const int columns = GetColumnCount();
		const int spacing = GetSpacing();
		const int row = static_cast<int>(itemPosition) / columns;
		const int column = static_cast<int>(itemPosition) % columns;
		return wxRect(
			spacing + column * (GetTileWidth() + spacing),
			spacing + row * (GetTileHeight() + spacing),
			GetTileWidth(),
			GetTileHeight()
		);
	}

	void UpdateVirtualSize() {
		const int spacing = GetSpacing();
		const int columns = GetColumnCount();
		const int rows = items_.empty() ? 1 : static_cast<int>((items_.size() + columns - 1) / columns);
		const int width = spacing + columns * (GetTileWidth() + spacing);
		const int height = spacing + rows * (GetTileHeight() + spacing);
		SetVirtualSize(wxSize(width, std::max(height, FromDIP(144))));
	}

	int HitTestItem(const wxPoint &position) const {
		wxPoint logical = position;
		CalcUnscrolledPosition(position.x, position.y, &logical.x, &logical.y);
		for (size_t i = 0; i < items_.size(); ++i) {
			if (GetTileRect(i).Contains(logical)) {
				return static_cast<int>(i);
			}
		}
		return wxNOT_FOUND;
	}

	wxString BuildDisplayLabel(wxDC &dc, const wxString &label, int width) const {
		return wxControl::Ellipsize(label, dc, wxELLIPSIZE_END, width);
	}

	void DrawTile(wxDC &dc, size_t itemPosition) {
		const BrushGridItem &item = items_[itemPosition];
		const wxRect tileRect = GetTileRect(itemPosition);
		const bool isSelected = item.index == selectedIndex_;
		const wxColour baseColour = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
		const wxColour borderColour = isSelected ? wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT) : wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW);
		const wxColour fillColour = isSelected ? wxSystemSettings::GetColour(wxSYS_COLOUR_INFOBK) : baseColour;

		dc.SetPen(wxPen(borderColour, isSelected ? 2 : 1));
		dc.SetBrush(wxBrush(fillColour));
		dc.DrawRectangle(tileRect);

		const wxRect iconRect(
			tileRect.x + (tileRect.width - FromDIP(32)) / 2,
			tileRect.y + FromDIP(6),
			FromDIP(32),
			FromDIP(32)
		);

		dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW)));
		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.DrawRectangle(iconRect);

		const int lookId = item.brush ? item.brush->getLookID() : item.lookId;
		if (lookId > 0) {
			if (Sprite* sprite = g_gui.gfx.getSprite(lookId)) {
				sprite->DrawTo(&dc, SPRITE_SIZE_32x32, iconRect.x + FromDIP(1), iconRect.y + FromDIP(1), FromDIP(30), FromDIP(30));
			}
		}

		const wxRect labelRect(
			tileRect.x + FromDIP(4),
			iconRect.GetBottom() + FromDIP(4),
			tileRect.width - FromDIP(8),
			tileRect.height - iconRect.height - FromDIP(14)
		);

		dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
		dc.DrawLabel(BuildDisplayLabel(dc, item.label, labelRect.width), labelRect, wxALIGN_CENTER_HORIZONTAL | wxALIGN_TOP);
	}

	void OnPaint(wxPaintEvent &) {
		wxAutoBufferedPaintDC dc(this);
		PrepareDC(dc);
		dc.SetBackground(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW)));
		dc.Clear();

		for (size_t i = 0; i < items_.size(); ++i) {
			DrawTile(dc, i);
		}
	}

	void OnLeftDown(wxMouseEvent &event) {
		const int itemPosition = HitTestItem(event.GetPosition());
		if (itemPosition != wxNOT_FOUND) {
			SelectIndex(items_[itemPosition].index);
		}
		event.Skip();
	}

	void OnSize(wxSizeEvent &event) {
		UpdateVirtualSize();
		Refresh();
		event.Skip();
	}

	void OnMouseMove(wxMouseEvent &event) {
		const int itemPosition = HitTestItem(event.GetPosition());
		if (itemPosition != wxNOT_FOUND) {
			SetToolTip(items_[itemPosition].label);
		} else {
			UnsetToolTip();
		}
		event.Skip();
	}

	bool HasItemIndex(int index) const {
		return std::any_of(items_.begin(), items_.end(), [index](const BrushGridItem &item) {
			return item.index == index;
		});
	}

	std::vector<BrushGridItem> items_;
	int selectedIndex_ = -1;
	std::function<void(int)> onSelectionChanged_;
};

namespace {
	const wxString kItemBrushFamilyGroupKey = "item-family";
	const char* const kRuntimeSectionTypes[] = {
		"terrain",
		"terrain_and_raw",
		"doodad",
		"doodad_and_raw",
		"items",
		"items_and_raw",
	};

	wxString TrimmedCopy(wxString value) {
		value.Trim(true);
		value.Trim(false);
		return value;
	}

	bool IsSupportedRuntimeSectionType(const wxString &sectionType) {
		for (const char* candidate : kRuntimeSectionTypes) {
			if (sectionType.IsSameAs(candidate, false)) {
				return true;
			}
		}
		return false;
	}

	wxString DerivePaletteGroupFromSectionType(const wxString &sectionType) {
		if (sectionType.IsSameAs("terrain", false) || sectionType.IsSameAs("terrain_and_raw", false)) {
			return "terrain";
		}
		if (sectionType.IsSameAs("doodad", false) || sectionType.IsSameAs("doodad_and_raw", false)) {
			return "doodad";
		}
		if (sectionType.IsSameAs("item", false) || sectionType.IsSameAs("items", false) || sectionType.IsSameAs("items_and_raw", false)) {
			return "item";
		}
		return "other";
	}

	wxString BuildPaletteGroupLabel(const PaletteGroupRecord &group) {
		wxString label = group.name;
		if (group.isBuiltin) {
			label += " [built-in]";
		}
		return label;
	}

	wxString BuildSectionLabel(const TilesetSectionRecord &section) {
		return wxString::Format("%s (%zu)", section.sectionType, section.entries.size());
	}

	wxString ComputeAfterBrushName(const TilesetEntryRecord &entry) {
		if (!entry.brushName.IsEmpty()) {
			return entry.brushName;
		}
		return wxString();
	}

	int ComputeAfterItemId(const TilesetEntryRecord &entry) {
		if (entry.itemId > 0) {
			return entry.itemId;
		}
		if (entry.toItemId > 0) {
			return entry.toItemId;
		}
		return entry.fromItemId;
	}

	bool IsItemBrushGroupKey(const wxString &groupKey) {
		return groupKey == "carpet" || groupKey == "table" || groupKey == "wall";
	}

	wxString PreferredSectionTypeForBrushGroupKey(const wxString &groupKey) {
		if (groupKey == "ground") {
			return "terrain";
		}
		if (groupKey == "doodad") {
			return "doodad";
		}
		if (groupKey == kItemBrushFamilyGroupKey || IsItemBrushGroupKey(groupKey)) {
			return "items";
		}
		return "terrain";
	}

	int FindVisibleEntryIndex(const std::vector<std::pair<int, int>> &locations, int sectionIndex, int entryIndex) {
		for (size_t i = 0; i < locations.size(); ++i) {
			if (locations[i].first == sectionIndex && locations[i].second == entryIndex) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	int ResolveEntryPreviewItemId(const TilesetEntryRecord &entry) {
		if (entry.itemId > 0) {
			return entry.itemId;
		}
		if (entry.fromItemId > 0) {
			return entry.fromItemId;
		}
		return entry.toItemId;
	}

	wxString BuildItemEntryLabel(const TilesetEntryRecord &entry) {
		const int previewItemId = ResolveEntryPreviewItemId(entry);
		if (previewItemId <= 0) {
			return "Item";
		}

		const int fromItemId = entry.fromItemId > 0 ? entry.fromItemId : previewItemId;
		const int toItemId = entry.toItemId > 0 ? entry.toItemId : fromItemId;
		wxString itemName;
		if (auto type = g_items.getRawItemType(static_cast<uint16_t>(previewItemId)); type && type->id != 0 && !type->name.empty()) {
			itemName = wxString::FromUTF8(type->name.c_str());
		}

		if (toItemId > fromItemId) {
			if (!itemName.IsEmpty()) {
				return wxString::Format("%d-%d - %s", fromItemId, toItemId, itemName);
			}
			return wxString::Format("%d-%d", fromItemId, toItemId);
		}

		if (!itemName.IsEmpty()) {
			return wxString::Format("%d - %s", previewItemId, itemName);
		}
		return wxString::Format("Item %d", previewItemId);
	}

	const BrushRecord* FindCatalogBrushRecord(const MaterialsWorkbenchController &controller, int64_t brushId, const wxString &brushName) {
		for (const MaterialsWorkbenchBrushGroup &group : controller.GetBrushGroups()) {
			for (const BrushRecord &record : group.brushes) {
				if (brushId > 0 && record.id == brushId) {
					return &record;
				}
				if (brushId <= 0 && !brushName.IsEmpty() && record.name.IsSameAs(brushName, false)) {
					return &record;
				}
			}
		}
		for (const BrushRecord &record : controller.GetWallBrushes()) {
			if (brushId > 0 && record.id == brushId) {
				return &record;
			}
			if (brushId <= 0 && !brushName.IsEmpty() && record.name.IsSameAs(brushName, false)) {
				return &record;
			}
		}
		return nullptr;
	}

	wxString DescribePaletteEntry(const MaterialsWorkbenchController &controller, const TilesetEntryRecord &entry) {
		if (entry.entryKind.IsSameAs("brush", false)) {
			if (!entry.brushName.IsEmpty()) {
				return entry.brushName;
			}
			if (const BrushRecord* catalogBrush = FindCatalogBrushRecord(controller, entry.brushId, entry.brushName)) {
				return catalogBrush->name;
			}
			return "Brush";
		}
		if (entry.entryKind.IsSameAs("item", false)) {
			return BuildItemEntryLabel(entry);
		}
		if (!entry.entryKind.IsEmpty()) {
			return entry.entryKind;
		}
		return "Entry";
	}
} // namespace

MaterialsWorkbenchPalettePanel::MaterialsWorkbenchPalettePanel(wxWindow* parent, MaterialsWorkbenchController &controller) :
	wxPanel(parent, wxID_ANY),
	controller_(controller) {
	BuildLayout();
	ClearWorkspace("Select a palette in the navigation tree to edit it visually.");
}

void MaterialsWorkbenchPalettePanel::SetOnPaletteSaved(std::function<void(const wxString &)> callback) {
	onPaletteSaved_ = std::move(callback);
}

void MaterialsWorkbenchPalettePanel::BuildLayout() {
	wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);

	wxStaticText* title = new wxStaticText(this, wxID_ANY, "Palette Workspace");
	wxFont titleFont = title->GetFont();
	titleFont.SetPointSize(titleFont.GetPointSize() + 4);
	titleFont.SetWeight(wxFONTWEIGHT_BOLD);
	title->SetFont(titleFont);

	titleLabel_ = new wxStaticText(this, wxID_ANY, "No palette selected");
	sourceLabel_ = new wxStaticText(this, wxID_ANY, "");
	statusLabel_ = new wxStaticText(this, wxID_ANY, "");

	wxBoxSizer* headerSizer = new wxBoxSizer(wxVERTICAL);
	headerSizer->Add(title, 0, wxBOTTOM, FromDIP(4));
	headerSizer->Add(titleLabel_, 0, wxBOTTOM, FromDIP(2));
	headerSizer->Add(sourceLabel_, 0);

	wxBoxSizer* toolbarSizer = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* paletteRowSizer = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* groupRowSizer = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* entryRowSizer = new wxBoxSizer(wxHORIZONTAL);
	paletteGroupChoice_ = new wxChoice(this, wxID_ANY);
	currentSectionChoice_ = new wxChoice(this, wxID_ANY);
	availableBrushGroupChoice_ = new wxChoice(this, wxID_ANY);
	paletteGroupChoice_->SetMinSize(wxSize(FromDIP(220), -1));
	currentSectionChoice_->SetMinSize(wxSize(FromDIP(260), -1));
	availableBrushGroupChoice_->SetMinSize(wxSize(FromDIP(260), -1));
	createPaletteButton_ = new wxButton(this, wxID_ANY, "New Palette");
	renamePaletteButton_ = new wxButton(this, wxID_ANY, "Rename Palette");
	deletePaletteButton_ = new wxButton(this, wxID_ANY, "Delete Palette");
	createPaletteGroupButton_ = new wxButton(this, wxID_ANY, "New Group");
	renamePaletteGroupButton_ = new wxButton(this, wxID_ANY, "Rename Group");
	deletePaletteGroupButton_ = new wxButton(this, wxID_ANY, "Delete Group");
	addSectionButton_ = new wxButton(this, wxID_ANY, "New Section");
	renameSectionButton_ = new wxButton(this, wxID_ANY, "Rename Section");
	deleteSectionButton_ = new wxButton(this, wxID_ANY, "Delete Section");
	addBrushButton_ = new wxButton(this, wxID_ANY, "Add Brush");
	removeBrushButton_ = new wxButton(this, wxID_ANY, "Remove Brush");
	moveUpButton_ = new wxButton(this, wxID_ANY, "Move Up");
	moveDownButton_ = new wxButton(this, wxID_ANY, "Move Down");
	currentSectionChoice_->Hide();
	addSectionButton_->Hide();
	renameSectionButton_->Hide();
	deleteSectionButton_->Hide();

	paletteRowSizer->Add(createPaletteButton_, 0, wxRIGHT, FromDIP(6));
	paletteRowSizer->Add(renamePaletteButton_, 0, wxRIGHT, FromDIP(6));
	paletteRowSizer->Add(deletePaletteButton_, 0);

	groupRowSizer->Add(new wxStaticText(this, wxID_ANY, "Palette Group"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
	groupRowSizer->Add(paletteGroupChoice_, 0, wxRIGHT, FromDIP(12));
	groupRowSizer->Add(createPaletteGroupButton_, 0, wxRIGHT, FromDIP(6));
	groupRowSizer->Add(renamePaletteGroupButton_, 0, wxRIGHT, FromDIP(6));
	groupRowSizer->Add(deletePaletteGroupButton_, 0);

	toolbarSizer->Add(paletteRowSizer, 0, wxBOTTOM, FromDIP(8));
	toolbarSizer->Add(groupRowSizer, 0, wxBOTTOM, FromDIP(8));

	entryRowSizer->Add(new wxStaticText(this, wxID_ANY, "Brush Source"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
	entryRowSizer->Add(availableBrushGroupChoice_, 0, wxRIGHT, FromDIP(8));
	entryRowSizer->Add(addBrushButton_, 0, wxRIGHT, FromDIP(6));
	entryRowSizer->Add(removeBrushButton_, 0, wxRIGHT, FromDIP(6));
	entryRowSizer->Add(moveUpButton_, 0, wxRIGHT, FromDIP(6));
	entryRowSizer->Add(moveDownButton_, 0);

	toolbarSizer->Add(entryRowSizer, 0, wxEXPAND);

	wxSplitterWindow* contentSplitter = new wxSplitterWindow(this, wxID_ANY);
	contentSplitter->SetSashGravity(0.54);
	contentSplitter->SetMinimumPaneSize(FromDIP(240));

	wxPanel* currentSectionPanel = new wxPanel(contentSplitter, wxID_ANY);
	wxBoxSizer* currentSectionSizer = new wxBoxSizer(wxVERTICAL);
	currentSectionSizer->Add(new wxStaticText(currentSectionPanel, wxID_ANY, "Palette Brushes"), 0, wxBOTTOM, FromDIP(4));
	sectionSummaryLabel_ = new wxStaticText(currentSectionPanel, wxID_ANY, "");
	currentSectionSizer->Add(sectionSummaryLabel_, 0, wxBOTTOM, FromDIP(6));
	sectionBrushGrid_ = new MaterialsWorkbenchBrushGridPanel(currentSectionPanel);
	currentSectionSizer->Add(sectionBrushGrid_, 1, wxEXPAND);
	currentSectionPanel->SetSizer(currentSectionSizer);

	wxPanel* availablePanel = new wxPanel(contentSplitter, wxID_ANY);
	wxBoxSizer* availableSizer = new wxBoxSizer(wxVERTICAL);
	availableSizer->Add(new wxStaticText(availablePanel, wxID_ANY, "Available Brushes"), 0, wxBOTTOM, FromDIP(4));
	availableSizer->Add(new wxStaticText(availablePanel, wxID_ANY, "Choose a brush family and add the selected brush to the palette. Internal sections stay hidden."), 0, wxBOTTOM, FromDIP(6));
	availableBrushGrid_ = new MaterialsWorkbenchBrushGridPanel(availablePanel);
	availableSizer->Add(availableBrushGrid_, 1, wxEXPAND);
	availablePanel->SetSizer(availableSizer);

	contentSplitter->SplitVertically(currentSectionPanel, availablePanel, FromDIP(680));

	rootSizer->Add(headerSizer, 0, wxEXPAND | wxALL, FromDIP(10));
	rootSizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));
	rootSizer->Add(toolbarSizer, 0, wxEXPAND | wxALL, FromDIP(10));
	rootSizer->Add(contentSplitter, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));
	rootSizer->Add(statusLabel_, 0, wxEXPAND | wxALL, FromDIP(10));
	SetSizer(rootSizer);

	createPaletteButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchPalettePanel::OnCreatePalette, this);
	renamePaletteButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchPalettePanel::OnRenamePalette, this);
	deletePaletteButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchPalettePanel::OnDeletePalette, this);
	paletteGroupChoice_->Bind(wxEVT_CHOICE, &MaterialsWorkbenchPalettePanel::OnPaletteGroupChanged, this);
	createPaletteGroupButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchPalettePanel::OnCreatePaletteGroup, this);
	renamePaletteGroupButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchPalettePanel::OnRenamePaletteGroup, this);
	deletePaletteGroupButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchPalettePanel::OnDeletePaletteGroup, this);
	availableBrushGroupChoice_->Bind(wxEVT_CHOICE, &MaterialsWorkbenchPalettePanel::OnAvailableBrushGroupChanged, this);
	addBrushButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchPalettePanel::OnAddBrush, this);
	removeBrushButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchPalettePanel::OnRemoveBrush, this);
	moveUpButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchPalettePanel::OnMoveBrushUp, this);
	moveDownButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchPalettePanel::OnMoveBrushDown, this);

	sectionBrushGrid_->SetSelectionChangedHandler([this](int index) {
		if (index >= 0 && index < static_cast<int>(visibleEntryLocations_.size())) {
			currentSectionIndex_ = visibleEntryLocations_[index].first;
			selectedSectionEntryIndex_ = visibleEntryLocations_[index].second;
		} else {
			currentSectionIndex_ = 0;
			selectedSectionEntryIndex_ = -1;
		}
		UpdateButtonState();
	});
	availableBrushGrid_->SetSelectionChangedHandler([this](int index) {
		selectedAvailableBrushListIndex_ = index;
		UpdateButtonState();
	});
}

void MaterialsWorkbenchPalettePanel::ClearWorkspace(const wxString &message) {
	hasPalette_ = false;
	palette_ = TilesetStorageRecord();
	currentAvailableBrushes_.clear();
	availableBrushGroupKeys_.clear();
	paletteGroupKeys_.clear();
	visibleEntryLocations_.clear();
	currentSectionIndex_ = 0;
	selectedSectionEntryIndex_ = -1;
	selectedAvailableBrushListIndex_ = -1;

	titleLabel_->SetLabel("No palette selected");
	sourceLabel_->SetLabel("");
	paletteGroupChoice_->Clear();
	availableBrushGroupChoice_->Clear();
	sectionSummaryLabel_->SetLabel(message);
	sectionBrushGrid_->Clear();
	availableBrushGrid_->Clear();
	SetStatusMessage(message);
	UpdateButtonState();
}

bool MaterialsWorkbenchPalettePanel::LoadPalette(const TilesetStorageRecord &tileset) {
	palette_ = tileset;
	hasPalette_ = true;
	currentSectionIndex_ = 0;
	selectedSectionEntryIndex_ = -1;
	selectedAvailableBrushListIndex_ = -1;
	RefreshWorkspace();
	return true;
}

void MaterialsWorkbenchPalettePanel::RefreshWorkspace() {
	titleLabel_->SetLabel("Editing palette: " + palette_.name);
	sourceLabel_->SetLabel("Source: " + (palette_.sourceFile.IsEmpty() ? wxString("materials.db") : palette_.sourceFile));
	RefreshPaletteGroupChoice();
	RefreshSectionChoice();
	RefreshAvailableBrushGroups();
	RefreshSectionEntries();
	RefreshAvailableBrushes();
	SetStatusMessage(
		"Palette loaded from materials.db. Grouping is now DB-first through palette_groups, while sections still edit the runtime contents inside the palette."
	);
	UpdateButtonState();
	Layout();
}

void MaterialsWorkbenchPalettePanel::RefreshPaletteGroupChoice() {
	paletteGroupChoice_->Clear();
	paletteGroupKeys_.clear();

	for (const PaletteGroupRecord &group : controller_.GetPaletteGroups()) {
		paletteGroupChoice_->Append(BuildPaletteGroupLabel(group));
		paletteGroupKeys_.push_back(group.name);
	}

	if (paletteGroupKeys_.empty()) {
		return;
	}

	const wxString currentGroupName = palette_.paletteGroupName.IsEmpty() ? wxString("other") : palette_.paletteGroupName;
	int selectedGroupIndex = FindPaletteGroupChoiceIndexByName(currentGroupName);
	if (selectedGroupIndex == wxNOT_FOUND) {
		selectedGroupIndex = FindPaletteGroupChoiceIndexByName("other");
	}
	if (selectedGroupIndex == wxNOT_FOUND) {
		selectedGroupIndex = 0;
	}
	paletteGroupChoice_->SetSelection(selectedGroupIndex);
}

void MaterialsWorkbenchPalettePanel::RefreshSectionChoice() {
	currentSectionChoice_->Clear();

	for (const TilesetSectionRecord &section : palette_.sections) {
		currentSectionChoice_->Append(BuildSectionLabel(section));
	}

	if (!palette_.sections.empty()) {
		currentSectionIndex_ = std::clamp(currentSectionIndex_, 0, static_cast<int>(palette_.sections.size()) - 1);
		currentSectionChoice_->SetSelection(currentSectionIndex_);
	} else {
		currentSectionIndex_ = 0;
	}
}

void MaterialsWorkbenchPalettePanel::RefreshSectionEntries() {
	visibleEntryLocations_.clear();

	if (!hasPalette_ || palette_.sections.empty()) {
		sectionSummaryLabel_->SetLabel(
			"This palette is empty. Add brushes from the right side and the hidden runtime sections will be created automatically."
		);
		sectionBrushGrid_->Clear();
		currentSectionIndex_ = 0;
		selectedSectionEntryIndex_ = -1;
		return;
	}

	std::vector<BrushGridItem> items;
	int unsupportedEntries = 0;
	int missingPreviews = 0;
	const bool showSectionPrefix = palette_.sections.size() > 1;

	for (size_t sectionIndex = 0; sectionIndex < palette_.sections.size(); ++sectionIndex) {
		const TilesetSectionRecord &section = palette_.sections[sectionIndex];
		for (size_t entryIndex = 0; entryIndex < section.entries.size(); ++entryIndex) {
			const TilesetEntryRecord &entry = section.entries[entryIndex];
			const wxString baseLabel = DescribePaletteEntry(controller_, entry);
			const wxString displayLabel = showSectionPrefix ? section.sectionType + " / " + baseLabel : baseLabel;
			const int visibleIndex = static_cast<int>(visibleEntryLocations_.size());

			if (entry.entryKind.IsSameAs("brush", false)) {
				Brush* brush = g_brushes.getBrush(entry.brushName.ToStdString());
				const BrushRecord* catalogBrush = FindCatalogBrushRecord(controller_, entry.brushId, entry.brushName);
				const int lookId = brush ? brush->getLookID() : (catalogBrush ? catalogBrush->lookId : 0);
				if (!brush && lookId <= 0) {
					++missingPreviews;
					continue;
				}

				visibleEntryLocations_.push_back({ static_cast<int>(sectionIndex), static_cast<int>(entryIndex) });
				items.push_back({ displayLabel, brush, lookId, visibleIndex });
				continue;
			}

			if (entry.entryKind.IsSameAs("item", false)) {
				const int previewItemId = ResolveEntryPreviewItemId(entry);
				if (previewItemId <= 0) {
					++missingPreviews;
					continue;
				}

				Brush* brush = nullptr;
				if (auto type = g_items.getRawItemType(static_cast<uint16_t>(previewItemId)); type && type->id != 0) {
					brush = type->raw_brush;
				}

				visibleEntryLocations_.push_back({ static_cast<int>(sectionIndex), static_cast<int>(entryIndex) });
				items.push_back({ displayLabel, brush, previewItemId, visibleIndex });
				continue;
			}

			++unsupportedEntries;
		}
	}

	sectionSummaryLabel_->SetLabel(
		wxString::Format(
			"This palette shows %zu entries across %zu hidden runtime sections. Unsupported entry kinds: %d. Missing previews: %d.",
			items.size(),
			palette_.sections.size(),
			unsupportedEntries,
			missingPreviews
		)
	);

	int desiredSelection = FindVisibleEntryIndex(visibleEntryLocations_, currentSectionIndex_, selectedSectionEntryIndex_);
	if (desiredSelection < 0 && !items.empty()) {
		desiredSelection = items.front().index;
	}
	sectionBrushGrid_->SetItems(items, desiredSelection);
	if (items.empty()) {
		currentSectionIndex_ = 0;
		selectedSectionEntryIndex_ = -1;
	}
}

void MaterialsWorkbenchPalettePanel::RefreshAvailableBrushGroups() {
	availableBrushGroupChoice_->Clear();
	availableBrushGroupKeys_.clear();

	bool hasItemBrushFamily = !controller_.GetWallBrushes().empty();
	for (const MaterialsWorkbenchBrushGroup &group : controller_.GetBrushGroups()) {
		if (IsItemBrushGroupKey(group.brushType) && !group.brushes.empty()) {
			hasItemBrushFamily = true;
			break;
		}
	}
	if (hasItemBrushFamily) {
		availableBrushGroupChoice_->Append("Item Brushes");
		availableBrushGroupKeys_.push_back(kItemBrushFamilyGroupKey);
	}

	for (const MaterialsWorkbenchBrushGroup &group : controller_.GetBrushGroups()) {
		availableBrushGroupChoice_->Append(group.label);
		availableBrushGroupKeys_.push_back(group.brushType);
	}
	if (!controller_.GetWallBrushes().empty()) {
		availableBrushGroupChoice_->Append("Wall Brushes");
		availableBrushGroupKeys_.push_back("wall");
	}

	if (!availableBrushGroupKeys_.empty()) {
		const wxString recommendedGroup = RecommendBrushGroupForCurrentSection();
		int selection = 0;
		for (size_t i = 0; i < availableBrushGroupKeys_.size(); ++i) {
			if (availableBrushGroupKeys_[i] == recommendedGroup) {
				selection = static_cast<int>(i);
				break;
			}
		}
		availableBrushGroupChoice_->SetSelection(selection);
	}
}

void MaterialsWorkbenchPalettePanel::RefreshAvailableBrushes() {
	currentAvailableBrushes_.clear();
	selectedAvailableBrushListIndex_ = -1;

	if (availableBrushGroupChoice_->GetSelection() == wxNOT_FOUND) {
		availableBrushGrid_->Clear();
		return;
	}

	const wxString groupKey = availableBrushGroupKeys_[availableBrushGroupChoice_->GetSelection()];
	std::vector<BrushGridItem> items;

	if (groupKey == kItemBrushFamilyGroupKey) {
		for (const MaterialsWorkbenchBrushGroup &group : controller_.GetBrushGroups()) {
			if (IsItemBrushGroupKey(group.brushType)) {
				currentAvailableBrushes_.insert(currentAvailableBrushes_.end(), group.brushes.begin(), group.brushes.end());
			}
		}
		const std::vector<BrushRecord> &wallBrushes = controller_.GetWallBrushes();
		currentAvailableBrushes_.insert(currentAvailableBrushes_.end(), wallBrushes.begin(), wallBrushes.end());
	} else if (groupKey == "wall") {
		currentAvailableBrushes_ = controller_.GetWallBrushes();
	} else {
		for (const MaterialsWorkbenchBrushGroup &group : controller_.GetBrushGroups()) {
			if (group.brushType == groupKey) {
				currentAvailableBrushes_ = group.brushes;
				break;
			}
		}
	}

	for (size_t i = 0; i < currentAvailableBrushes_.size(); ++i) {
		const BrushRecord &record = currentAvailableBrushes_[i];
		Brush* brush = g_brushes.getBrush(record.name.ToStdString());
		const int lookId = brush ? brush->getLookID() : record.lookId;
		if (!brush && lookId <= 0) {
			continue;
		}
		items.push_back({ record.name, brush, lookId, static_cast<int>(i) });
	}

	availableBrushGrid_->SetItems(items);
	if (items.empty()) {
		selectedAvailableBrushListIndex_ = -1;
	}
}

void MaterialsWorkbenchPalettePanel::UpdateButtonState() {
	createPaletteButton_->Enable(true);
	renamePaletteButton_->Enable(hasPalette_);
	deletePaletteButton_->Enable(hasPalette_);
	createPaletteGroupButton_->Enable(hasPalette_);

	const PaletteGroupRecord* selectedGroup = GetSelectedPaletteGroup();
	const bool hasSelectedGroup = selectedGroup != nullptr;
	const bool isBuiltinGroup = hasSelectedGroup && selectedGroup->isBuiltin;
	paletteGroupChoice_->Enable(hasPalette_ && !paletteGroupKeys_.empty());
	renamePaletteGroupButton_->Enable(hasSelectedGroup && !isBuiltinGroup);
	deletePaletteGroupButton_->Enable(hasSelectedGroup && !isBuiltinGroup);

	const bool hasSection = hasPalette_ && !palette_.sections.empty() && currentSectionIndex_ < static_cast<int>(palette_.sections.size());
	addBrushButton_->Enable(hasSection && selectedAvailableBrushListIndex_ >= 0);
	removeBrushButton_->Enable(hasSection && selectedSectionEntryIndex_ >= 0);
	moveUpButton_->Enable(hasSection && selectedSectionEntryIndex_ > 0);
	if (hasSection && selectedSectionEntryIndex_ >= 0) {
		moveDownButton_->Enable(selectedSectionEntryIndex_ < static_cast<int>(palette_.sections[currentSectionIndex_].entries.size()) - 1);
	} else {
		moveDownButton_->Enable(false);
	}
}

void MaterialsWorkbenchPalettePanel::SetStatusMessage(const wxString &message) {
	statusLabel_->SetLabel(message);
}

void MaterialsWorkbenchPalettePanel::NormalizePaletteOrdering() {
	for (size_t sectionIndex = 0; sectionIndex < palette_.sections.size(); ++sectionIndex) {
		TilesetSectionRecord &section = palette_.sections[sectionIndex];
		section.sortOrder = static_cast<int>(sectionIndex);
		for (size_t entryIndex = 0; entryIndex < section.entries.size(); ++entryIndex) {
			TilesetEntryRecord &entry = section.entries[entryIndex];
			entry.sortOrder = static_cast<int>(entryIndex);
			entry.afterBrushName.clear();
			entry.afterItemId = 0;
			if (entryIndex == 0) {
				continue;
			}

			const TilesetEntryRecord &previousEntry = section.entries[entryIndex - 1];
			const wxString afterBrushName = ComputeAfterBrushName(previousEntry);
			if (!afterBrushName.IsEmpty()) {
				entry.afterBrushName = afterBrushName;
				continue;
			}

			entry.afterItemId = ComputeAfterItemId(previousEntry);
		}
	}
}

bool MaterialsWorkbenchPalettePanel::CommitPalette(const wxString &successMessage, const wxString &previousPaletteName, const wxString &selectionPaletteName) {
	NormalizePaletteOrdering();

	wxString error;
	if (!controller_.SaveTileset(palette_, previousPaletteName, error)) {
		SetStatusMessage("Failed to save palette: " + error);
		return false;
	}

	SetStatusMessage(successMessage);
	if (onPaletteSaved_) {
		onPaletteSaved_(selectionPaletteName.IsEmpty() ? palette_.name : selectionPaletteName);
	}
	return true;
}

wxString MaterialsWorkbenchPalettePanel::RecommendBrushGroupForCurrentSection() const {
	if (!hasPalette_) {
		return "ground";
	}

	if (!palette_.sections.empty() && currentSectionIndex_ >= 0 && currentSectionIndex_ < static_cast<int>(palette_.sections.size())) {
		const wxString sectionType = palette_.sections[currentSectionIndex_].sectionType.Lower();
		if (sectionType.Contains("terrain")) {
			return "ground";
		}
		if (sectionType.Contains("doodad")) {
			return "doodad";
		}
		if (sectionType.Contains("item")) {
			return kItemBrushFamilyGroupKey;
		}
	}

	if (palette_.paletteGroupName.IsSameAs("doodad", false)) {
		return "doodad";
	}
	if (palette_.paletteGroupName.IsSameAs("item", false)) {
		return kItemBrushFamilyGroupKey;
	}
	return "ground";
}

const BrushRecord* MaterialsWorkbenchPalettePanel::FindAvailableBrushRecord() const {
	if (selectedAvailableBrushListIndex_ < 0 || selectedAvailableBrushListIndex_ >= static_cast<int>(currentAvailableBrushes_.size())) {
		return nullptr;
	}
	return &currentAvailableBrushes_[selectedAvailableBrushListIndex_];
}

int MaterialsWorkbenchPalettePanel::FindSectionIndexByName(const wxString &sectionName) const {
	for (size_t i = 0; i < palette_.sections.size(); ++i) {
		if (palette_.sections[i].sectionType.IsSameAs(sectionName, false)) {
			return static_cast<int>(i);
		}
	}

	return -1;
}

int MaterialsWorkbenchPalettePanel::FindPaletteGroupChoiceIndexByName(const wxString &groupName) const {
	for (size_t i = 0; i < paletteGroupKeys_.size(); ++i) {
		if (paletteGroupKeys_[i].IsSameAs(groupName, false)) {
			return static_cast<int>(i);
		}
	}

	return wxNOT_FOUND;
}

const PaletteGroupRecord* MaterialsWorkbenchPalettePanel::GetSelectedPaletteGroup() const {
	const int selection = paletteGroupChoice_->GetSelection();
	if (selection == wxNOT_FOUND || selection < 0 || selection >= static_cast<int>(paletteGroupKeys_.size())) {
		return nullptr;
	}

	const wxString &selectedGroupName = paletteGroupKeys_[selection];
	for (const PaletteGroupRecord &group : controller_.GetPaletteGroups()) {
		if (group.name.IsSameAs(selectedGroupName, false)) {
			return &group;
		}
	}

	return nullptr;
}

bool MaterialsWorkbenchPalettePanel::PromptForPaletteName(const wxString &title, const wxString &caption, const wxString &initialValue, const wxString &currentName, wxString &outName) {
	wxTextEntryDialog dialog(this, caption, title, initialValue);
	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}

	const wxString candidateName = TrimmedCopy(dialog.GetValue());
	if (candidateName.IsEmpty()) {
		wxMessageBox("Palette name cannot be empty.", title, wxOK | wxICON_WARNING, this);
		return false;
	}
	if (!currentName.IsEmpty() && candidateName.IsSameAs(currentName, false)) {
		outName = candidateName;
		return true;
	}
	if (controller_.HasTilesetNamed(candidateName)) {
		wxMessageBox("A palette with this name already exists.", title, wxOK | wxICON_WARNING, this);
		return false;
	}

	outName = candidateName;
	return true;
}

bool MaterialsWorkbenchPalettePanel::PromptForPaletteGroupName(const wxString &title, const wxString &caption, const wxString &initialValue, const wxString &currentName, wxString &outName) {
	wxTextEntryDialog dialog(this, caption, title, initialValue);
	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}

	const wxString candidateName = TrimmedCopy(dialog.GetValue());
	if (candidateName.IsEmpty()) {
		wxMessageBox("Palette group name cannot be empty.", title, wxOK | wxICON_WARNING, this);
		return false;
	}
	if (!currentName.IsEmpty() && candidateName.IsSameAs(currentName, false)) {
		outName = candidateName;
		return true;
	}
	if (controller_.HasPaletteGroupNamed(candidateName)) {
		wxMessageBox("A palette group with this name already exists.", title, wxOK | wxICON_WARNING, this);
		return false;
	}

	outName = candidateName;
	return true;
}

bool MaterialsWorkbenchPalettePanel::PromptForNewSectionType(const wxString &title, const wxString &caption, wxString &outSectionType) {
	wxArrayString choices;
	for (const char* sectionType : kRuntimeSectionTypes) {
		choices.Add(sectionType);
	}

	wxSingleChoiceDialog dialog(this, caption, title, choices);
	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}

	outSectionType = dialog.GetStringSelection();
	return !outSectionType.IsEmpty();
}

bool MaterialsWorkbenchPalettePanel::PromptForSectionName(const wxString &title, const wxString &caption, const wxString &initialValue, const wxString &currentSectionType, wxString &outSectionType) {
	wxTextEntryDialog dialog(this, caption, title, initialValue);
	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}

	const wxString candidateType = TrimmedCopy(dialog.GetValue());
	if (candidateType.IsEmpty()) {
		wxMessageBox("Section name cannot be empty.", title, wxOK | wxICON_WARNING, this);
		return false;
	}
	if (!IsSupportedRuntimeSectionType(candidateType)) {
		wxMessageBox("Section type must be one of the supported runtime section types.", title, wxOK | wxICON_WARNING, this);
		return false;
	}

	for (const TilesetSectionRecord &section : palette_.sections) {
		if (!currentSectionType.IsEmpty() && section.sectionType.IsSameAs(currentSectionType, false)) {
			continue;
		}
		if (section.sectionType.IsSameAs(candidateType, false)) {
			wxMessageBox("This palette already has a section with that name.", title, wxOK | wxICON_WARNING, this);
			return false;
		}
	}

	outSectionType = candidateType;
	return true;
}

void MaterialsWorkbenchPalettePanel::OnCreatePalette(wxCommandEvent &event) {
	wxString newPaletteName;
	if (!PromptForPaletteName("New Palette", "Enter the new palette name:", "", "", newPaletteName)) {
		return;
	}
	const wxString initialGroupName = !palette_.paletteGroupName.IsEmpty() ? palette_.paletteGroupName : wxString("other");

	palette_ = TilesetStorageRecord();
	palette_.name = newPaletteName;
	palette_.sourceFile = "materials.db";
	palette_.paletteGroupName = initialGroupName;
	hasPalette_ = true;
	currentSectionIndex_ = 0;
	selectedSectionEntryIndex_ = -1;
	selectedAvailableBrushListIndex_ = -1;

	if (!CommitPalette("Created empty palette \"" + newPaletteName + "\" in group \"" + initialGroupName + "\".", "", newPaletteName)) {
		return;
	}

	RefreshWorkspace();
}

void MaterialsWorkbenchPalettePanel::OnRenamePalette(wxCommandEvent &event) {
	if (!hasPalette_) {
		return;
	}

	const wxString previousName = palette_.name;
	wxString renamedPaletteName;
	if (!PromptForPaletteName("Rename Palette", "Enter the new palette name:", previousName, previousName, renamedPaletteName)) {
		return;
	}
	if (renamedPaletteName == previousName) {
		SetStatusMessage("Palette name is unchanged.");
		return;
	}

	palette_.name = renamedPaletteName;
	if (!CommitPalette("Renamed palette to \"" + renamedPaletteName + "\".", previousName, renamedPaletteName)) {
		palette_.name = previousName;
		return;
	}

	RefreshWorkspace();
}

void MaterialsWorkbenchPalettePanel::OnDeletePalette(wxCommandEvent &event) {
	if (!hasPalette_) {
		return;
	}

	const wxString paletteName = palette_.name;
	if (wxMessageBox(
			"Delete palette \"" + paletteName + "\" from materials.db?",
			"Delete Palette",
			wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
			this
		) != wxYES) {
		return;
	}

	wxString error;
	if (!controller_.DeleteTileset(paletteName, error)) {
		SetStatusMessage("Failed to delete palette: " + error);
		return;
	}

	ClearWorkspace("Palette deleted. Select another palette in the navigation tree.");
	SetStatusMessage("Deleted palette \"" + paletteName + "\".");
	if (onPaletteSaved_) {
		onPaletteSaved_(wxString());
	}
}

void MaterialsWorkbenchPalettePanel::OnPaletteGroupChanged(wxCommandEvent &event) {
	const int selectedChoice = event.GetSelection();
	if (!hasPalette_ || selectedChoice == wxNOT_FOUND || selectedChoice >= static_cast<int>(paletteGroupKeys_.size())) {
		return;
	}

	const PaletteGroupRecord* group = GetSelectedPaletteGroup();
	if (!group) {
		SetStatusMessage("Failed to resolve the selected palette group.");
		return;
	}

	const wxString previousGroupName = palette_.paletteGroupName;
	palette_.paletteGroupName = group->name;
	if (!CommitPalette("Moved palette \"" + palette_.name + "\" to group \"" + group->name + "\".")) {
		palette_.paletteGroupName = previousGroupName;
		RefreshPaletteGroupChoice();
		UpdateButtonState();
		return;
	}

	RefreshWorkspace();
}

void MaterialsWorkbenchPalettePanel::OnCreatePaletteGroup(wxCommandEvent &event) {
	if (!hasPalette_) {
		return;
	}

	wxString groupName;
	if (!PromptForPaletteGroupName("New Palette Group", "Enter the new palette group name:", "", "", groupName)) {
		return;
	}

	PaletteGroupRecord group;
	group.name = groupName;
	wxString error;
	if (!controller_.SavePaletteGroup(group, error)) {
		SetStatusMessage("Failed to create palette group: " + error);
		return;
	}

	palette_.paletteGroupName = group.name;
	SetStatusMessage("Created palette group \"" + group.name + "\".");
	if (onPaletteSaved_) {
		onPaletteSaved_(palette_.name);
	}
}

void MaterialsWorkbenchPalettePanel::OnRenamePaletteGroup(wxCommandEvent &event) {
	const PaletteGroupRecord* selectedGroup = GetSelectedPaletteGroup();
	if (!selectedGroup || selectedGroup->isBuiltin) {
		return;
	}

	wxString renamedGroupName;
	if (!PromptForPaletteGroupName("Rename Palette Group", "Enter the new palette group name:", selectedGroup->name, selectedGroup->name, renamedGroupName)) {
		return;
	}
	if (renamedGroupName.IsSameAs(selectedGroup->name, false)) {
		SetStatusMessage("Palette group name is unchanged.");
		return;
	}

	PaletteGroupRecord updatedGroup = *selectedGroup;
	updatedGroup.name = renamedGroupName;
	wxString error;
	if (!controller_.SavePaletteGroup(updatedGroup, error)) {
		SetStatusMessage("Failed to rename palette group: " + error);
		return;
	}

	if (palette_.paletteGroupName.IsSameAs(selectedGroup->name, false)) {
		palette_.paletteGroupName = renamedGroupName;
	}
	SetStatusMessage("Renamed palette group to \"" + renamedGroupName + "\".");
	if (onPaletteSaved_) {
		onPaletteSaved_(palette_.name);
	}
}

void MaterialsWorkbenchPalettePanel::OnDeletePaletteGroup(wxCommandEvent &event) {
	const PaletteGroupRecord* selectedGroup = GetSelectedPaletteGroup();
	if (!selectedGroup || selectedGroup->isBuiltin) {
		return;
	}

	if (wxMessageBox(
			"Delete palette group \"" + selectedGroup->name + "\" from materials.db?\n\nAny palettes still using this group must be moved first.",
			"Delete Palette Group",
			wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
			this
		) != wxYES) {
		return;
	}

	wxString error;
	if (!controller_.DeletePaletteGroup(selectedGroup->name, error)) {
		SetStatusMessage("Failed to delete palette group: " + error);
		return;
	}

	SetStatusMessage("Deleted palette group \"" + selectedGroup->name + "\".");
	if (onPaletteSaved_) {
		onPaletteSaved_(palette_.name);
	}
}

void MaterialsWorkbenchPalettePanel::OnSectionChanged(wxCommandEvent &event) {
	const int selectedChoice = event.GetSelection();
	if (!hasPalette_ || selectedChoice == wxNOT_FOUND || selectedChoice >= static_cast<int>(palette_.sections.size())) {
		return;
	}
	currentSectionIndex_ = selectedChoice;
	selectedSectionEntryIndex_ = -1;
	RefreshAvailableBrushGroups();
	RefreshSectionEntries();
	RefreshAvailableBrushes();
	UpdateButtonState();
}

void MaterialsWorkbenchPalettePanel::OnAddSection(wxCommandEvent &event) {
	if (!hasPalette_) {
		return;
	}

	wxString sectionType;
	if (!PromptForNewSectionType("New Section", "Choose the section to create:", sectionType)) {
		return;
	}
	if (FindSectionIndexByName(sectionType) >= 0) {
		SetStatusMessage("This palette already has a section for \"" + sectionType + "\".");
		return;
	}

	TilesetSectionRecord section;
	section.sectionType = sectionType;
	section.sortOrder = static_cast<int>(palette_.sections.size());
	palette_.sections.push_back(section);
	currentSectionIndex_ = static_cast<int>(palette_.sections.size()) - 1;
	selectedSectionEntryIndex_ = -1;
	selectedAvailableBrushListIndex_ = -1;

	if (!CommitPalette("Created section \"" + sectionType + "\" in palette \"" + palette_.name + "\".")) {
		return;
	}

	RefreshWorkspace();
}

void MaterialsWorkbenchPalettePanel::OnRenameSection(wxCommandEvent &event) {
	if (!hasPalette_ || palette_.sections.empty() || currentSectionIndex_ >= static_cast<int>(palette_.sections.size())) {
		return;
	}

	TilesetSectionRecord &section = palette_.sections[currentSectionIndex_];
	const wxString previousSectionType = section.sectionType;
	wxString renamedSectionType;
	if (!PromptForSectionName("Rename Section", "Enter the new runtime section type:", previousSectionType, previousSectionType, renamedSectionType)) {
		return;
	}
	if (renamedSectionType == previousSectionType) {
		SetStatusMessage("Section type is unchanged.");
		return;
	}

	section.sectionType = renamedSectionType;
	if (!CommitPalette("Renamed section to \"" + renamedSectionType + "\".")) {
		section.sectionType = previousSectionType;
		return;
	}

	RefreshWorkspace();
}

void MaterialsWorkbenchPalettePanel::OnDeleteSection(wxCommandEvent &event) {
	if (!hasPalette_ || palette_.sections.empty() || currentSectionIndex_ >= static_cast<int>(palette_.sections.size())) {
		return;
	}

	const wxString sectionType = palette_.sections[currentSectionIndex_].sectionType;
	if (wxMessageBox(
			"Delete section \"" + sectionType + "\" from palette \"" + palette_.name + "\"?",
			"Delete Section",
			wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
			this
		) != wxYES) {
		return;
	}

	palette_.sections.erase(palette_.sections.begin() + currentSectionIndex_);
	if (currentSectionIndex_ >= static_cast<int>(palette_.sections.size())) {
		currentSectionIndex_ = std::max(0, static_cast<int>(palette_.sections.size()) - 1);
	}
	selectedSectionEntryIndex_ = -1;
	selectedAvailableBrushListIndex_ = -1;

	if (!CommitPalette("Deleted section \"" + sectionType + "\".")) {
		return;
	}

	RefreshWorkspace();
}

void MaterialsWorkbenchPalettePanel::OnAvailableBrushGroupChanged(wxCommandEvent &event) {
	selectedAvailableBrushListIndex_ = -1;
	RefreshAvailableBrushes();
	UpdateButtonState();
}

void MaterialsWorkbenchPalettePanel::OnAddBrush(wxCommandEvent &event) {
	if (!hasPalette_ || availableBrushGroupChoice_->GetSelection() == wxNOT_FOUND) {
		return;
	}

	const BrushRecord* brushRecord = FindAvailableBrushRecord();
	if (!brushRecord) {
		SetStatusMessage("Select an available brush to add.");
		return;
	}

	const wxString groupKey = availableBrushGroupKeys_[availableBrushGroupChoice_->GetSelection()];
	const wxString preferredSectionType = PreferredSectionTypeForBrushGroupKey(groupKey);
	int targetSectionIndex = -1;
	for (size_t i = 0; i < palette_.sections.size(); ++i) {
		if (palette_.sections[i].sectionType.IsSameAs(preferredSectionType, false)) {
			targetSectionIndex = static_cast<int>(i);
			break;
		}
	}
	if (targetSectionIndex == -1) {
		const wxString preferredFamily = DerivePaletteGroupFromSectionType(preferredSectionType);
		for (size_t i = 0; i < palette_.sections.size(); ++i) {
			if (DerivePaletteGroupFromSectionType(palette_.sections[i].sectionType).IsSameAs(preferredFamily, false)) {
				targetSectionIndex = static_cast<int>(i);
				break;
			}
		}
	}
	if (targetSectionIndex == -1) {
		TilesetSectionRecord section;
		section.sectionType = preferredSectionType;
		section.sortOrder = static_cast<int>(palette_.sections.size());
		palette_.sections.push_back(section);
		targetSectionIndex = static_cast<int>(palette_.sections.size()) - 1;
	}

	TilesetSectionRecord &section = palette_.sections[targetSectionIndex];
	TilesetEntryRecord entry;
	entry.entryKind = "brush";
	entry.brushId = brushRecord->id;
	entry.brushName = brushRecord->name;

	int insertIndex = static_cast<int>(section.entries.size());
	if (currentSectionIndex_ == targetSectionIndex && selectedSectionEntryIndex_ >= 0) {
		insertIndex = selectedSectionEntryIndex_ + 1;
	}
	insertIndex = std::clamp(insertIndex, 0, static_cast<int>(section.entries.size()));
	section.entries.insert(section.entries.begin() + insertIndex, entry);
	currentSectionIndex_ = targetSectionIndex;
	selectedSectionEntryIndex_ = insertIndex;

	if (!CommitPalette("Added brush \"" + brushRecord->name + "\" to palette \"" + palette_.name + "\".")) {
		return;
	}

	RefreshSectionChoice();
	RefreshSectionEntries();
	UpdateButtonState();
}

void MaterialsWorkbenchPalettePanel::OnRemoveBrush(wxCommandEvent &event) {
	if (!hasPalette_ || palette_.sections.empty() || currentSectionIndex_ >= static_cast<int>(palette_.sections.size()) || selectedSectionEntryIndex_ < 0) {
		return;
	}

	TilesetSectionRecord &section = palette_.sections[currentSectionIndex_];
	if (selectedSectionEntryIndex_ >= static_cast<int>(section.entries.size())) {
		return;
	}

	const wxString removedName = DescribePaletteEntry(controller_, section.entries[selectedSectionEntryIndex_]);
	section.entries.erase(section.entries.begin() + selectedSectionEntryIndex_);
	if (selectedSectionEntryIndex_ >= static_cast<int>(section.entries.size())) {
		selectedSectionEntryIndex_ = static_cast<int>(section.entries.size()) - 1;
	}

	if (!CommitPalette("Removed brush \"" + removedName + "\" from palette \"" + palette_.name + "\".")) {
		return;
	}

	RefreshSectionChoice();
	RefreshSectionEntries();
	UpdateButtonState();
}

void MaterialsWorkbenchPalettePanel::OnMoveBrushUp(wxCommandEvent &event) {
	if (!hasPalette_ || palette_.sections.empty() || currentSectionIndex_ >= static_cast<int>(palette_.sections.size()) || selectedSectionEntryIndex_ <= 0) {
		return;
	}

	TilesetSectionRecord &section = palette_.sections[currentSectionIndex_];
	std::swap(section.entries[selectedSectionEntryIndex_], section.entries[selectedSectionEntryIndex_ - 1]);
	--selectedSectionEntryIndex_;

	if (!CommitPalette("Moved brush entry up in palette \"" + palette_.name + "\".")) {
		return;
	}

	RefreshSectionEntries();
	UpdateButtonState();
}

void MaterialsWorkbenchPalettePanel::OnMoveBrushDown(wxCommandEvent &event) {
	if (!hasPalette_ || palette_.sections.empty() || currentSectionIndex_ >= static_cast<int>(palette_.sections.size()) || selectedSectionEntryIndex_ < 0) {
		return;
	}

	TilesetSectionRecord &section = palette_.sections[currentSectionIndex_];
	if (selectedSectionEntryIndex_ >= static_cast<int>(section.entries.size()) - 1) {
		return;
	}

	std::swap(section.entries[selectedSectionEntryIndex_], section.entries[selectedSectionEntryIndex_ + 1]);
	++selectedSectionEntryIndex_;

	if (!CommitPalette("Moved brush entry down in palette \"" + palette_.name + "\".")) {
		return;
	}

	RefreshSectionEntries();
	UpdateButtonState();
}
