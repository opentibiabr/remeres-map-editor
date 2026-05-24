#include "main.h"

#include "materials_workbench_border_panel.h"

#include <array>
#include <utility>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "common_windows.h"
#include "find_item_window.h"
#include "items.h"
#include "materials_workbench_controller.h"

namespace {
	struct BorderEdgeSpec {
		const char* edge;
		const char* label;
		int row;
		int col;
	};

	const std::array<BorderEdgeSpec, 12> kBorderEdgeSpecs = {{
		{ "dnw", "Diag NW", 0, 1 },
		{ "n", "North", 0, 2 },
		{ "dne", "Diag NE", 0, 3 },
		{ "cnw", "Corner NW", 1, 1 },
		{ "cne", "Corner NE", 1, 3 },
		{ "w", "West", 2, 0 },
		{ "e", "East", 2, 4 },
		{ "csw", "Corner SW", 3, 1 },
		{ "cse", "Corner SE", 3, 3 },
		{ "dsw", "Diag SW", 4, 1 },
		{ "s", "South", 4, 2 },
		{ "dse", "Diag SE", 4, 3 },
	}};

	const BorderEdgeSpec* FindEdgeSpec(const wxString &edge) {
		for (const BorderEdgeSpec &spec : kBorderEdgeSpecs) {
			if (edge == wxString::FromUTF8(spec.edge)) {
				return &spec;
			}
		}
		return nullptr;
	}

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

	wxPanel* CreateSpacerCell(wxWindow* parent, int sizeDip) {
		wxPanel* spacer = new wxPanel(parent, wxID_ANY);
		spacer->SetMinSize(wxSize(parent->FromDIP(sizeDip), parent->FromDIP(sizeDip)));
		return spacer;
	}

	class BorderSlotButton : public ItemToggleButton {
	public:
		explicit BorderSlotButton(wxWindow* parent) :
			ItemToggleButton(parent, RENDER_SIZE_32x32, 0) {
		}
	};
} // namespace

MaterialsWorkbenchBorderPanel::MaterialsWorkbenchBorderPanel(wxWindow* parent, MaterialsWorkbenchController &controller) :
	wxPanel(parent, wxID_ANY),
	controller_(controller) {
	BuildLayout();
	ClearWorkspace("Select a border set in the navigation tree to edit its layout.");
}

void MaterialsWorkbenchBorderPanel::SetOnBorderSetSaved(std::function<void(int64_t)> callback) {
	onBorderSetSaved_ = std::move(callback);
}

void MaterialsWorkbenchBorderPanel::BuildLayout() {
	wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);

	wxStaticText* title = new wxStaticText(this, wxID_ANY, "Border Workspace");
	wxFont titleFont = title->GetFont();
	titleFont.SetPointSize(titleFont.GetPointSize() + 4);
	titleFont.SetWeight(wxFONTWEIGHT_BOLD);
	title->SetFont(titleFont);

	titleLabel_ = new wxStaticText(this, wxID_ANY, "No border set selected");
	subtitleLabel_ = new wxStaticText(this, wxID_ANY, "Edit border slots visually, assign item ids and preview the resulting composition.");

	wxScrolledWindow* scrolled = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	scrolled->SetScrollRate(FromDIP(10), FromDIP(10));

	wxBoxSizer* contentSizer = new wxBoxSizer(wxVERTICAL);
	summaryLabel_ = new wxStaticText(scrolled, wxID_ANY, "");

	wxStaticBoxSizer* metadataBox = new wxStaticBoxSizer(wxVERTICAL, scrolled, "Border Set Metadata");
	wxBoxSizer* metadataGrid = new wxBoxSizer(wxVERTICAL);

	idCtrl_ = new wxTextCtrl(scrolled, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
	xmlBorderIdCtrl_ = new wxSpinCtrl(scrolled, wxID_ANY);
	xmlBorderIdCtrl_->SetRange(0, 1000000);
	scopeChoice_ = new wxChoice(scrolled, wxID_ANY);
	scopeChoice_->Append("global");
	scopeChoice_->Append("inline");
	typeCtrl_ = new wxTextCtrl(scrolled, wxID_ANY);
	borderGroupCtrl_ = new wxSpinCtrl(scrolled, wxID_ANY);
	borderGroupCtrl_->SetRange(0, 1000000);
	groundEquivalentCtrl_ = new wxSpinCtrl(scrolled, wxID_ANY);
	groundEquivalentCtrl_->SetRange(0, std::max(100000, static_cast<int>(g_items.getMaxID())));
	ownerBrushIdCtrl_ = new wxTextCtrl(scrolled, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
	sourceCtrl_ = new wxTextCtrl(scrolled, wxID_ANY);

	const wxSize metadataFieldSize(FromDIP(170), -1);
	idCtrl_->SetMinSize(metadataFieldSize);
	xmlBorderIdCtrl_->SetMinSize(metadataFieldSize);
	scopeChoice_->SetMinSize(metadataFieldSize);
	typeCtrl_->SetMinSize(metadataFieldSize);
	borderGroupCtrl_->SetMinSize(metadataFieldSize);
	groundEquivalentCtrl_->SetMinSize(metadataFieldSize);
	ownerBrushIdCtrl_->SetMinSize(metadataFieldSize);
	sourceCtrl_->SetMinSize(metadataFieldSize);

	const auto addMetadataField = [&](const wxString &label, wxWindow* control) {
		metadataGrid->Add(new wxStaticText(scrolled, wxID_ANY, label), 0, wxBOTTOM, FromDIP(2));
		metadataGrid->Add(control, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
	};

	addMetadataField("SQLite ID", idCtrl_);
	addMetadataField("XML Border ID", xmlBorderIdCtrl_);
	addMetadataField("Scope", scopeChoice_);
	addMetadataField("Type", typeCtrl_);
	addMetadataField("Border Group", borderGroupCtrl_);
	addMetadataField("Ground Equivalent", groundEquivalentCtrl_);
	addMetadataField("Owner Brush ID", ownerBrushIdCtrl_);
	addMetadataField("Source", sourceCtrl_);
	metadataBox->Add(metadataGrid, 1, wxEXPAND | wxALL, FromDIP(8));

	wxBoxSizer* workspaceSizer = new wxBoxSizer(wxHORIZONTAL);

	wxStaticBoxSizer* gridBox = new wxStaticBoxSizer(wxVERTICAL, scrolled, "Slot Grid");
	wxFlexGridSizer* slotGridSizer = new wxFlexGridSizer(5, 5, FromDIP(4), FromDIP(4));

	for (int row = 0; row < 5; ++row) {
		for (int col = 0; col < 5; ++col) {
			const BorderEdgeSpec* specForCell = nullptr;
			for (const BorderEdgeSpec &spec : kBorderEdgeSpecs) {
				if (spec.row == row && spec.col == col) {
					specForCell = &spec;
					break;
				}
			}

			if (row == 2 && col == 2) {
				wxPanel* centerPanel = new wxPanel(scrolled, wxID_ANY);
				wxBoxSizer* centerSizer = new wxBoxSizer(wxVERTICAL);
				wxStaticText* centerLabel = new wxStaticText(centerPanel, wxID_ANY, "Center");
				wxFont centerFont = centerLabel->GetFont();
				centerFont.SetWeight(wxFONTWEIGHT_BOLD);
				centerLabel->SetFont(centerFont);
				centerSizer->AddStretchSpacer(1);
				centerSizer->Add(centerLabel, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(2));
				centerSizer->Add(new wxStaticText(centerPanel, wxID_ANY, "ground"), 0, wxALIGN_CENTER);
				centerSizer->AddStretchSpacer(1);
				centerPanel->SetSizer(centerSizer);
				centerPanel->SetMinSize(wxSize(FromDIP(76), FromDIP(76)));
				slotGridSizer->Add(centerPanel, 0, wxEXPAND);
				continue;
			}

			if (!specForCell) {
				slotGridSizer->Add(CreateSpacerCell(scrolled, 60), 0, wxEXPAND);
				continue;
			}

			wxPanel* cell = new wxPanel(scrolled, wxID_ANY);
			wxBoxSizer* cellSizer = new wxBoxSizer(wxVERTICAL);
			wxString edge = wxString::FromUTF8(specForCell->edge);
			wxStaticText* label = new wxStaticText(cell, wxID_ANY, specForCell->label);
			BorderSlotButton* button = new BorderSlotButton(cell);
			wxStaticText* value = new wxStaticText(cell, wxID_ANY, "item 0");

			button->Bind(wxEVT_LEFT_DOWN, [this, edge](wxMouseEvent &event) {
				SelectEdge(edge);
				event.Skip();
			});

			cellSizer->Add(label, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(2));
			cellSizer->Add(button, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(2));
			cellSizer->Add(value, 0, wxALIGN_CENTER);
			cell->SetSizer(cellSizer);
			cell->SetMinSize(wxSize(FromDIP(76), FromDIP(76)));

			slotButtons_[edge] = button;
			slotValueLabels_[edge] = value;
			slotGridSizer->Add(cell, 0, wxEXPAND);
		}
	}

	gridBox->Add(slotGridSizer, 0, wxALL, FromDIP(8));

	wxStaticBoxSizer* editorBox = new wxStaticBoxSizer(wxVERTICAL, scrolled, "Selected Slot");
	selectedEdgeLabel_ = new wxStaticText(scrolled, wxID_ANY, "Edge: none");
	selectedItemIdCtrl_ = new wxSpinCtrl(scrolled, wxID_ANY);
	selectedItemIdCtrl_->SetRange(0, std::max(100000, static_cast<int>(g_items.getMaxID())));
	selectedItemPreview_ = new ItemButton(scrolled, RENDER_SIZE_32x32, 0);
	selectedItemIdCtrl_->SetMinSize(wxSize(FromDIP(110), -1));

	wxBoxSizer* selectionRow = new wxBoxSizer(wxHORIZONTAL);
	selectionRow->Add(selectedItemPreview_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
	selectionRow->Add(selectedItemIdCtrl_, 1, wxALIGN_CENTER_VERTICAL);

	wxGridSizer* selectionActions = new wxGridSizer(2, FromDIP(6), FromDIP(6));
	wxButton* pickItemButton = new wxButton(scrolled, wxID_ANY, "Pick Item");
	wxButton* applyButton = new wxButton(scrolled, wxID_ANY, "Apply To Slot");
	wxButton* clearButton = new wxButton(scrolled, wxID_ANY, "Clear Slot");
	pickItemButton->SetMinSize(wxSize(FromDIP(108), -1));
	applyButton->SetMinSize(wxSize(FromDIP(108), -1));
	clearButton->SetMinSize(wxSize(FromDIP(108), -1));
	selectionActions->Add(pickItemButton, 0, wxEXPAND);
	selectionActions->Add(applyButton, 0, wxEXPAND);
	selectionActions->Add(clearButton, 0, wxEXPAND);
	selectionActions->AddSpacer(0);

	editorBox->Add(selectedEdgeLabel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
	editorBox->Add(selectionRow, 0, wxEXPAND | wxALL, FromDIP(8));
	editorBox->Add(selectionActions, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

	wxStaticBoxSizer* previewBox = new wxStaticBoxSizer(wxVERTICAL, scrolled, "Preview Matrix");
	wxFlexGridSizer* previewGridSizer = new wxFlexGridSizer(5, 5, FromDIP(2), FromDIP(2));

	for (int row = 0; row < 5; ++row) {
		for (int col = 0; col < 5; ++col) {
			const BorderEdgeSpec* specForCell = nullptr;
			for (const BorderEdgeSpec &spec : kBorderEdgeSpecs) {
				if (spec.row == row && spec.col == col) {
					specForCell = &spec;
					break;
				}
			}

			if (row == 2 && col == 2) {
				ItemButton* centerPreview = new ItemButton(scrolled, RENDER_SIZE_16x16, 0);
				centerPreview->Enable(false);
				previewButtons_["center"] = centerPreview;
				previewGridSizer->Add(centerPreview, 0, wxALIGN_CENTER);
				continue;
			}

			if (!specForCell) {
				previewGridSizer->Add(CreateSpacerCell(scrolled, 18), 0, wxEXPAND);
				continue;
			}

			wxString edge = wxString::FromUTF8(specForCell->edge);
			ItemButton* previewButton = new ItemButton(scrolled, RENDER_SIZE_16x16, 0);
			previewButton->Enable(false);
			previewButtons_[edge] = previewButton;
			previewGridSizer->Add(previewButton, 0, wxALIGN_CENTER);
		}
	}

	previewBox->Add(previewGridSizer, 0, wxALL, FromDIP(8));

	wxBoxSizer* leftColumn = new wxBoxSizer(wxVERTICAL);
	leftColumn->Add(metadataBox, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
	leftColumn->Add(editorBox, 0, wxEXPAND);
	leftColumn->SetMinSize(wxSize(FromDIP(225), -1));
	previewBox->SetMinSize(wxSize(FromDIP(120), -1));

	workspaceSizer->Add(leftColumn, 0, wxRIGHT | wxEXPAND, FromDIP(8));
	workspaceSizer->Add(gridBox, 1, wxRIGHT | wxEXPAND, FromDIP(8));
	workspaceSizer->Add(previewBox, 0, wxEXPAND);

	contentSizer->Add(summaryLabel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
	contentSizer->Add(new wxStaticLine(scrolled), 0, wxEXPAND | wxALL, FromDIP(8));
	contentSizer->Add(workspaceSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

	scrolled->SetSizer(contentSizer);

	wxBoxSizer* headerSizer = new wxBoxSizer(wxVERTICAL);
	headerSizer->Add(title, 0, wxBOTTOM, FromDIP(4));
	headerSizer->Add(titleLabel_, 0, wxBOTTOM, FromDIP(2));
	headerSizer->Add(subtitleLabel_, 0);

	wxBoxSizer* actionSizer = new wxBoxSizer(wxHORIZONTAL);
	wxButton* saveButton = new wxButton(this, wxID_SAVE, "Save Border");
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

	pickItemButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBorderPanel::OnPickItem, this);
	applyButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBorderPanel::OnApplyToSlot, this);
	clearButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBorderPanel::OnClearSlot, this);
	saveButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBorderPanel::OnSave, this);
	revertButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBorderPanel::OnRevert, this);
	selectedItemIdCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBorderPanel::OnSelectedItemIdChanged, this);
	selectedItemIdCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBorderPanel::OnSelectedItemIdSpin, this);
}

void MaterialsWorkbenchBorderPanel::ClearWorkspace(const wxString &message) {
	borderSetStorage_ = BorderSetStorageRecord();
	currentContextKey_.clear();
	currentItemIndex_ = -1;
	hasBorderSet_ = false;
	selectedEdge_.clear();
	slotItemIds_.clear();

	titleLabel_->SetLabel("No border set selected");
	subtitleLabel_->SetLabel("Select a border set in the navigation tree to edit its layout.");
	summaryLabel_->SetLabel(message);

	idCtrl_->SetValue("");
	xmlBorderIdCtrl_->SetValue(0);
	scopeChoice_->SetSelection(wxNOT_FOUND);
	typeCtrl_->SetValue("");
	borderGroupCtrl_->SetValue(0);
	groundEquivalentCtrl_->SetValue(0);
	ownerBrushIdCtrl_->SetValue("");
	sourceCtrl_->SetValue("");
	selectedEdgeLabel_->SetLabel("Edge: none");
	selectedItemIdCtrl_->SetValue(0);
	selectedItemPreview_->SetSprite(0);

	for (const auto &entry : slotButtons_) {
		entry.second->SetSprite(0);
		entry.second->SetValue(false);
	}
	for (const auto &entry : slotValueLabels_) {
		entry.second->SetLabel("item 0");
	}
	for (const auto &entry : previewButtons_) {
		entry.second->SetSprite(0);
	}

	SetFieldsEnabled(false);
	SetStatusMessage(message);
	Layout();
}

bool MaterialsWorkbenchBorderPanel::LoadBorderSet(const wxString &contextKey, int itemIndex) {
	wxString error;
	BorderSetStorageRecord storage;
	if (!controller_.GetBorderSetDetails(contextKey, itemIndex, storage, error)) {
		ClearWorkspace("Failed to load border set details: " + error);
		return false;
	}

	borderSetStorage_ = storage;
	currentContextKey_ = contextKey;
	currentItemIndex_ = itemIndex;
	hasBorderSet_ = true;

	PopulateFields();
	SetFieldsEnabled(true);
	SetStatusMessage("Border set loaded from materials.db.");
	Layout();
	return true;
}

void MaterialsWorkbenchBorderPanel::PopulateFields() {
	const BorderSetRecord &borderSet = borderSetStorage_.borderSet;

	titleLabel_->SetLabel("Editing " + BuildBorderSetDisplayLabel(borderSet));
	subtitleLabel_->SetLabel("Assign border sprites to the correct slots, then save the layout back to materials.db.");
	summaryLabel_->SetLabel(wxString::Format(
		"Items: %zu | Scope: %s | Type: %s | XML Border ID: %d | SQLite ID: %lld",
		borderSetStorage_.items.size(),
		borderSet.borderScope,
		borderSet.borderType,
		borderSet.xmlBorderId,
		static_cast<long long>(borderSet.id)
	));

	idCtrl_->SetValue(wxString::Format("%lld", static_cast<long long>(borderSet.id)));
	xmlBorderIdCtrl_->SetValue(borderSet.xmlBorderId);
	scopeChoice_->SetStringSelection(borderSet.borderScope);
	typeCtrl_->SetValue(borderSet.borderType);
	borderGroupCtrl_->SetValue(borderSet.borderGroup);
	groundEquivalentCtrl_->SetValue(borderSet.groundEquivalent);
	ownerBrushIdCtrl_->SetValue(borderSet.ownerBrushId > 0 ? wxString::Format("%lld", static_cast<long long>(borderSet.ownerBrushId)) : "");
	sourceCtrl_->SetValue(borderSet.sourceFile);

	slotItemIds_.clear();
	for (const BorderSetItemRecord &item : borderSetStorage_.items) {
		slotItemIds_[item.edge] = item.itemId;
	}

	RefreshSlotGrid();
	RefreshPreviewGrid();

	if (selectedEdge_.IsEmpty()) {
		SelectEdge("n");
	} else {
		UpdateSelectedEdgeEditor();
	}
}

void MaterialsWorkbenchBorderPanel::RefreshSlotGrid() {
	for (const auto &entry : slotButtons_) {
		const wxString &edge = entry.first;
		const int itemId = slotItemIds_.count(edge) > 0 ? slotItemIds_[edge] : 0;
		entry.second->SetSprite(itemId);
		entry.second->SetValue(edge == selectedEdge_);
		slotValueLabels_[edge]->SetLabel(wxString::Format("item %d", itemId));
	}
}

void MaterialsWorkbenchBorderPanel::RefreshPreviewGrid() {
	for (const auto &entry : previewButtons_) {
		if (entry.first == "center") {
			entry.second->SetSprite(groundEquivalentCtrl_->GetValue());
			continue;
		}

		const int itemId = slotItemIds_.count(entry.first) > 0 ? slotItemIds_[entry.first] : 0;
		entry.second->SetSprite(itemId);
	}
}

void MaterialsWorkbenchBorderPanel::SelectEdge(const wxString &edge) {
	if (!slotButtons_.count(edge)) {
		return;
	}

	selectedEdge_ = edge;
	RefreshSlotGrid();
	UpdateSelectedEdgeEditor();
}

void MaterialsWorkbenchBorderPanel::UpdateSelectedEdgeEditor() {
	const BorderEdgeSpec* spec = FindEdgeSpec(selectedEdge_);
	const int itemId = slotItemIds_.count(selectedEdge_) > 0 ? slotItemIds_[selectedEdge_] : 0;
	if (spec) {
		selectedEdgeLabel_->SetLabel("Edge: " + wxString::FromUTF8(spec->label));
	} else {
		selectedEdgeLabel_->SetLabel("Edge: " + selectedEdge_);
	}
	selectedItemIdCtrl_->SetValue(itemId);
	selectedItemPreview_->SetSprite(itemId);
}

void MaterialsWorkbenchBorderPanel::SyncSelectedSlotFromEditor(bool updateStatus) {
	if (!hasBorderSet_ || selectedEdge_.IsEmpty()) {
		return;
	}

	const int itemId = selectedItemIdCtrl_->GetValue();
	if (itemId > 0) {
		slotItemIds_[selectedEdge_] = itemId;
	} else {
		slotItemIds_.erase(selectedEdge_);
	}

	selectedItemPreview_->SetSprite(itemId);
	RefreshSlotGrid();
	RefreshPreviewGrid();
	if (updateStatus) {
		SetStatusMessage("Slot updated locally. Save the border set to persist.");
	}
}

void MaterialsWorkbenchBorderPanel::SetStatusMessage(const wxString &message) {
	statusLabel_->SetLabel(message);
}

void MaterialsWorkbenchBorderPanel::SetFieldsEnabled(bool enabled) {
	xmlBorderIdCtrl_->Enable(enabled);
	scopeChoice_->Enable(enabled);
	typeCtrl_->Enable(enabled);
	borderGroupCtrl_->Enable(enabled);
	groundEquivalentCtrl_->Enable(enabled);
	sourceCtrl_->Enable(enabled);
	selectedItemIdCtrl_->Enable(enabled);
	selectedItemPreview_->Enable(enabled);
	for (const auto &entry : slotButtons_) {
		entry.second->Enable(enabled);
	}
}

bool MaterialsWorkbenchBorderPanel::SaveCurrentBorderSet() {
	if (!hasBorderSet_) {
		SetStatusMessage("Select a border set before saving.");
		return false;
	}
	if (scopeChoice_->GetSelection() == wxNOT_FOUND) {
		SetStatusMessage("Border scope must be selected.");
		return false;
	}

	borderSetStorage_.borderSet.xmlBorderId = xmlBorderIdCtrl_->GetValue();
	borderSetStorage_.borderSet.borderScope = scopeChoice_->GetStringSelection();
	borderSetStorage_.borderSet.borderType = typeCtrl_->GetValue().Trim(true).Trim(false);
	borderSetStorage_.borderSet.borderGroup = borderGroupCtrl_->GetValue();
	borderSetStorage_.borderSet.groundEquivalent = groundEquivalentCtrl_->GetValue();
	borderSetStorage_.borderSet.sourceFile = sourceCtrl_->GetValue().Trim(true).Trim(false);

	if (borderSetStorage_.borderSet.borderType.IsEmpty()) {
		borderSetStorage_.borderSet.borderType = "normal";
	}

	borderSetStorage_.items.clear();
	int sortOrder = 0;
	for (const BorderEdgeSpec &spec : kBorderEdgeSpecs) {
		const wxString edge = wxString::FromUTF8(spec.edge);
		const int itemId = slotItemIds_.count(edge) > 0 ? slotItemIds_[edge] : 0;
		if (itemId <= 0) {
			continue;
		}

		BorderSetItemRecord item;
		item.borderSetId = borderSetStorage_.borderSet.id;
		item.edge = edge;
		item.itemId = itemId;
		item.sortOrder = sortOrder++;
		borderSetStorage_.items.push_back(item);
	}

	wxString error;
	if (!controller_.SaveBorderSet(borderSetStorage_, error)) {
		SetStatusMessage("Failed to save border set: " + error);
		return false;
	}

	PopulateFields();
	SetStatusMessage("Border set saved to materials.db.");
	if (onBorderSetSaved_) {
		onBorderSetSaved_(borderSetStorage_.borderSet.id);
	}
	return true;
}

void MaterialsWorkbenchBorderPanel::OnApplyToSlot(wxCommandEvent &event) {
	if (!hasBorderSet_ || selectedEdge_.IsEmpty()) {
		SetStatusMessage("Select a border slot before applying an item.");
		return;
	}

	SyncSelectedSlotFromEditor(true);
}

void MaterialsWorkbenchBorderPanel::OnClearSlot(wxCommandEvent &event) {
	if (!hasBorderSet_ || selectedEdge_.IsEmpty()) {
		SetStatusMessage("Select a border slot before clearing it.");
		return;
	}

	slotItemIds_.erase(selectedEdge_);
	selectedItemIdCtrl_->SetValue(0);
	selectedItemPreview_->SetSprite(0);
	RefreshSlotGrid();
	RefreshPreviewGrid();
	SetStatusMessage("Slot cleared locally. Save the border set to persist.");
}

void MaterialsWorkbenchBorderPanel::OnPickItem(wxCommandEvent &event) {
	if (!hasBorderSet_) {
		SetStatusMessage("Load a border set before choosing an item.");
		return;
	}

	FindItemDialog dialog(this, "Select Border Item");
	dialog.setSearchMode(FindItemDialog::ItemIDs);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	selectedItemIdCtrl_->SetValue(dialog.getResultID());
	selectedItemPreview_->SetSprite(dialog.getResultID());
}

void MaterialsWorkbenchBorderPanel::OnSave(wxCommandEvent &event) {
	SaveCurrentBorderSet();
}

void MaterialsWorkbenchBorderPanel::OnRevert(wxCommandEvent &event) {
	if (!hasBorderSet_) {
		ClearWorkspace("Select a border set in the navigation tree to edit its layout.");
		return;
	}

	if (!LoadBorderSet(currentContextKey_, currentItemIndex_)) {
		return;
	}

	SetStatusMessage("Border set fields reloaded from materials.db.");
}

void MaterialsWorkbenchBorderPanel::OnSelectedItemIdChanged(wxCommandEvent &event) {
	SyncSelectedSlotFromEditor(false);
	event.Skip();
}

void MaterialsWorkbenchBorderPanel::OnSelectedItemIdSpin(wxSpinEvent &event) {
	SyncSelectedSlotFromEditor(false);
	event.Skip();
}
