#include "main.h"

#include "materials_workbench_palette_panel.h"

#include <algorithm>
#include <utility>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/wrapsizer.h>

#include "brush.h"
#include "materials_workbench_controller.h"
#include "palette_common.h"

struct BrushGridItem {
	wxString label;
	Brush* brush = nullptr;
	int index = -1;
};

class MaterialsWorkbenchBrushGridPanel : public wxScrolledWindow {
public:
	explicit MaterialsWorkbenchBrushGridPanel(wxWindow* parent) :
		wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxHSCROLL | wxBORDER_THEME) {
		SetScrollRate(FromDIP(12), FromDIP(12));

		rootSizer_ = new wxWrapSizer(wxHORIZONTAL);
		rootSizer_->SetMinSize(wxSize(-1, FromDIP(180)));
		SetSizer(rootSizer_);
	}

	void SetSelectionChangedHandler(std::function<void(int)> handler) {
		onSelectionChanged_ = std::move(handler);
	}

	void SetItems(const std::vector<BrushGridItem> &items, int selectedIndex = -1) {
		for (wxWindow* child : tilePanels_) {
			child->Destroy();
		}
		tilePanels_.clear();
		buttons_.clear();
		items_.clear();
		selectedIndex_ = -1;

		for (const BrushGridItem &item : items) {
			wxPanel* tilePanel = new wxPanel(this, wxID_ANY);
			wxBoxSizer* tileSizer = new wxBoxSizer(wxVERTICAL);

			BrushButton* button = new BrushButton(tilePanel, item.brush, RENDER_SIZE_32x32);
			button->SetMinSize(wxSize(36, 36));
			button->SetMaxSize(wxSize(36, 36));

			wxStaticText* label = new wxStaticText(tilePanel, wxID_ANY, item.label, wxDefaultPosition, wxSize(FromDIP(96), -1), wxALIGN_CENTER_HORIZONTAL);
			label->Wrap(FromDIP(96));

			tileSizer->Add(button, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, FromDIP(6));
			tileSizer->Add(label, 0, wxALIGN_CENTER_HORIZONTAL);
			tilePanel->SetSizerAndFit(tileSizer);

			rootSizer_->Add(tilePanel, 0, wxALL, FromDIP(6));
			tilePanels_.push_back(tilePanel);
			buttons_.push_back(button);
			items_.push_back(item);

			button->Bind(wxEVT_TOGGLEBUTTON, [this, item](wxCommandEvent &) {
				SelectIndex(item.index);
			});
		}

		Layout();
		FitInside();

		if (!items_.empty()) {
			if (selectedIndex >= 0) {
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
		selectedIndex_ = index;
		for (size_t i = 0; i < items_.size(); ++i) {
			buttons_[i]->SetValue(items_[i].index == selectedIndex_);
		}
		if (onSelectionChanged_) {
			onSelectionChanged_(selectedIndex_);
		}
	}

private:
	wxWrapSizer* rootSizer_ = nullptr;
	std::vector<wxWindow*> tilePanels_;
	std::vector<BrushButton*> buttons_;
	std::vector<BrushGridItem> items_;
	int selectedIndex_ = -1;
	std::function<void(int)> onSelectionChanged_;
};

namespace {

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
} // namespace

MaterialsWorkbenchPalettePanel::MaterialsWorkbenchPalettePanel(wxWindow* parent, MaterialsWorkbenchController &controller) :
	wxPanel(parent, wxID_ANY),
	controller_(controller) {
	BuildLayout();
	ClearWorkspace("Select a palette in the navigation tree to edit it visually.");
}

void MaterialsWorkbenchPalettePanel::SetOnPaletteSaved(std::function<void()> callback) {
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

	wxBoxSizer* toolbarSizer = new wxBoxSizer(wxHORIZONTAL);
	sectionChoice_ = new wxChoice(this, wxID_ANY);
	availableBrushGroupChoice_ = new wxChoice(this, wxID_ANY);
	addBrushButton_ = new wxButton(this, wxID_ANY, "Add Brush");
	removeBrushButton_ = new wxButton(this, wxID_ANY, "Remove Brush");
	moveUpButton_ = new wxButton(this, wxID_ANY, "Move Up");
	moveDownButton_ = new wxButton(this, wxID_ANY, "Move Down");

	toolbarSizer->Add(new wxStaticText(this, wxID_ANY, "Section"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
	toolbarSizer->Add(sectionChoice_, 0, wxRIGHT, FromDIP(12));
	toolbarSizer->Add(new wxStaticText(this, wxID_ANY, "Available"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
	toolbarSizer->Add(availableBrushGroupChoice_, 0, wxRIGHT, FromDIP(12));
	toolbarSizer->Add(addBrushButton_, 0, wxRIGHT, FromDIP(6));
	toolbarSizer->Add(removeBrushButton_, 0, wxRIGHT, FromDIP(6));
	toolbarSizer->Add(moveUpButton_, 0, wxRIGHT, FromDIP(6));
	toolbarSizer->Add(moveDownButton_, 0);

	wxSplitterWindow* contentSplitter = new wxSplitterWindow(this, wxID_ANY);
	contentSplitter->SetSashGravity(0.58);
	contentSplitter->SetMinimumPaneSize(FromDIP(260));

	wxPanel* currentSectionPanel = new wxPanel(contentSplitter, wxID_ANY);
	wxBoxSizer* currentSectionSizer = new wxBoxSizer(wxVERTICAL);
	currentSectionSizer->Add(new wxStaticText(currentSectionPanel, wxID_ANY, "Palette Brushes"), 0, wxBOTTOM, FromDIP(6));
	sectionSummaryLabel_ = new wxStaticText(currentSectionPanel, wxID_ANY, "");
	currentSectionSizer->Add(sectionSummaryLabel_, 0, wxBOTTOM, FromDIP(8));
	sectionBrushGrid_ = new MaterialsWorkbenchBrushGridPanel(currentSectionPanel);
	currentSectionSizer->Add(sectionBrushGrid_, 1, wxEXPAND);
	currentSectionPanel->SetSizer(currentSectionSizer);

	wxPanel* availablePanel = new wxPanel(contentSplitter, wxID_ANY);
	wxBoxSizer* availableSizer = new wxBoxSizer(wxVERTICAL);
	availableSizer->Add(new wxStaticText(availablePanel, wxID_ANY, "Available Brushes"), 0, wxBOTTOM, FromDIP(6));
	availableSizer->Add(new wxStaticText(availablePanel, wxID_ANY, "Choose a brush type and add the selected brush to the current section."), 0, wxBOTTOM, FromDIP(8));
	availableBrushGrid_ = new MaterialsWorkbenchBrushGridPanel(availablePanel);
	availableSizer->Add(availableBrushGrid_, 1, wxEXPAND);
	availablePanel->SetSizer(availableSizer);

	contentSplitter->SplitVertically(currentSectionPanel, availablePanel, FromDIP(760));

	rootSizer->Add(headerSizer, 0, wxEXPAND | wxALL, FromDIP(14));
	rootSizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(14));
	rootSizer->Add(toolbarSizer, 0, wxEXPAND | wxALL, FromDIP(14));
	rootSizer->Add(contentSplitter, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(14));
	rootSizer->Add(statusLabel_, 0, wxEXPAND | wxALL, FromDIP(14));
	SetSizer(rootSizer);

	sectionChoice_->Bind(wxEVT_CHOICE, &MaterialsWorkbenchPalettePanel::OnSectionChanged, this);
	availableBrushGroupChoice_->Bind(wxEVT_CHOICE, &MaterialsWorkbenchPalettePanel::OnAvailableBrushGroupChanged, this);
	addBrushButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchPalettePanel::OnAddBrush, this);
	removeBrushButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchPalettePanel::OnRemoveBrush, this);
	moveUpButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchPalettePanel::OnMoveBrushUp, this);
	moveDownButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchPalettePanel::OnMoveBrushDown, this);

	sectionBrushGrid_->SetSelectionChangedHandler([this](int index) {
		selectedSectionEntryIndex_ = index;
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
	currentSectionIndex_ = 0;
	selectedSectionEntryIndex_ = -1;
	selectedAvailableBrushListIndex_ = -1;

	titleLabel_->SetLabel("No palette selected");
	sourceLabel_->SetLabel("");
	sectionChoice_->Clear();
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
	sourceLabel_->SetLabel("Source: " + palette_.sourceFile);
	RefreshSectionChoice();
	RefreshAvailableBrushGroups();
	RefreshSectionEntries();
	RefreshAvailableBrushes();
	SetStatusMessage("Palette loaded from materials.db. Select brushes visually and edit the section order.");
	UpdateButtonState();
	Layout();
}

void MaterialsWorkbenchPalettePanel::RefreshSectionChoice() {
	sectionChoice_->Clear();
	for (const TilesetSectionRecord &section : palette_.sections) {
		sectionChoice_->Append(BuildSectionLabel(section));
	}
	if (!palette_.sections.empty()) {
		currentSectionIndex_ = std::clamp(currentSectionIndex_, 0, static_cast<int>(palette_.sections.size()) - 1);
		sectionChoice_->SetSelection(currentSectionIndex_);
	} else {
		currentSectionIndex_ = 0;
	}
}

void MaterialsWorkbenchPalettePanel::RefreshSectionEntries() {
	if (!hasPalette_ || palette_.sections.empty() || currentSectionIndex_ >= static_cast<int>(palette_.sections.size())) {
		sectionSummaryLabel_->SetLabel("This palette does not expose any editable sections yet.");
		sectionBrushGrid_->Clear();
		selectedSectionEntryIndex_ = -1;
		return;
	}

	const TilesetSectionRecord &section = palette_.sections[currentSectionIndex_];
	std::vector<BrushGridItem> items;
	int nonBrushEntries = 0;
	int missingBrushes = 0;

	for (size_t i = 0; i < section.entries.size(); ++i) {
		const TilesetEntryRecord &entry = section.entries[i];
		if (!entry.entryKind.IsSameAs("brush", false)) {
			++nonBrushEntries;
			continue;
		}

		Brush* brush = g_brushes.getBrush(entry.brushName.ToStdString());
		if (!brush) {
			++missingBrushes;
			continue;
		}

		items.push_back({ entry.brushName, brush, static_cast<int>(i) });
	}

	sectionSummaryLabel_->SetLabel(
		wxString::Format(
			"Section \"%s\" has %zu brush entries. Preserved non-brush entries: %d. Missing runtime brushes: %d.",
			section.sectionType,
			items.size(),
			nonBrushEntries,
			missingBrushes
		)
	);

	int desiredSelection = selectedSectionEntryIndex_;
	if (desiredSelection < 0 && !items.empty()) {
		desiredSelection = items.front().index;
	}
	sectionBrushGrid_->SetItems(items, desiredSelection);
	if (items.empty()) {
		selectedSectionEntryIndex_ = -1;
	}
}

void MaterialsWorkbenchPalettePanel::RefreshAvailableBrushGroups() {
	availableBrushGroupChoice_->Clear();
	availableBrushGroupKeys_.clear();

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

	if (groupKey == "wall") {
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
		if (!brush) {
			continue;
		}
		items.push_back({ record.name, brush, static_cast<int>(i) });
	}

	availableBrushGrid_->SetItems(items);
	if (items.empty()) {
		selectedAvailableBrushListIndex_ = -1;
	}
}

void MaterialsWorkbenchPalettePanel::UpdateButtonState() {
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

bool MaterialsWorkbenchPalettePanel::CommitPalette(const wxString &successMessage) {
	NormalizePaletteOrdering();

	wxString error;
	if (!controller_.SaveTileset(palette_, error)) {
		SetStatusMessage("Failed to save palette: " + error);
		return false;
	}

	SetStatusMessage(successMessage);
	if (onPaletteSaved_) {
		onPaletteSaved_();
	}
	return true;
}

wxString MaterialsWorkbenchPalettePanel::RecommendBrushGroupForCurrentSection() const {
	if (!hasPalette_ || palette_.sections.empty() || currentSectionIndex_ >= static_cast<int>(palette_.sections.size())) {
		return "ground";
	}

	const wxString sectionType = palette_.sections[currentSectionIndex_].sectionType.Lower();
	if (sectionType.Contains("terrain")) {
		return "ground";
	}
	if (sectionType.Contains("doodad")) {
		return "doodad";
	}
	if (sectionType.Contains("item")) {
		return "wall";
	}
	return "ground";
}

const BrushRecord* MaterialsWorkbenchPalettePanel::FindAvailableBrushRecord() const {
	if (selectedAvailableBrushListIndex_ < 0 || selectedAvailableBrushListIndex_ >= static_cast<int>(currentAvailableBrushes_.size())) {
		return nullptr;
	}
	return &currentAvailableBrushes_[selectedAvailableBrushListIndex_];
}

void MaterialsWorkbenchPalettePanel::OnSectionChanged(wxCommandEvent &event) {
	currentSectionIndex_ = event.GetSelection();
	selectedSectionEntryIndex_ = -1;
	RefreshAvailableBrushGroups();
	RefreshSectionEntries();
	RefreshAvailableBrushes();
	UpdateButtonState();
}

void MaterialsWorkbenchPalettePanel::OnAvailableBrushGroupChanged(wxCommandEvent &event) {
	selectedAvailableBrushListIndex_ = -1;
	RefreshAvailableBrushes();
	UpdateButtonState();
}

void MaterialsWorkbenchPalettePanel::OnAddBrush(wxCommandEvent &event) {
	if (!hasPalette_ || palette_.sections.empty() || currentSectionIndex_ >= static_cast<int>(palette_.sections.size())) {
		return;
	}

	const BrushRecord* brushRecord = FindAvailableBrushRecord();
	if (!brushRecord) {
		SetStatusMessage("Select an available brush to add.");
		return;
	}

	TilesetSectionRecord &section = palette_.sections[currentSectionIndex_];
	TilesetEntryRecord entry;
	entry.entryKind = "brush";
	entry.brushId = brushRecord->id;
	entry.brushName = brushRecord->name;

	int insertIndex = selectedSectionEntryIndex_ >= 0 ? selectedSectionEntryIndex_ + 1 : static_cast<int>(section.entries.size());
	insertIndex = std::clamp(insertIndex, 0, static_cast<int>(section.entries.size()));
	section.entries.insert(section.entries.begin() + insertIndex, entry);
	selectedSectionEntryIndex_ = insertIndex;

	if (!CommitPalette("Added brush \"" + brushRecord->name + "\" to section \"" + section.sectionType + "\".")) {
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

	const wxString removedName = section.entries[selectedSectionEntryIndex_].brushName;
	section.entries.erase(section.entries.begin() + selectedSectionEntryIndex_);
	if (selectedSectionEntryIndex_ >= static_cast<int>(section.entries.size())) {
		selectedSectionEntryIndex_ = static_cast<int>(section.entries.size()) - 1;
	}

	if (!CommitPalette("Removed brush \"" + removedName + "\" from section \"" + section.sectionType + "\".")) {
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

	if (!CommitPalette("Moved brush entry up in section \"" + section.sectionType + "\".")) {
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

	if (!CommitPalette("Moved brush entry down in section \"" + section.sectionType + "\".")) {
		return;
	}

	RefreshSectionEntries();
	UpdateButtonState();
}
