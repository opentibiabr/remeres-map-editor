#include "main.h"

#include "materials_workbench_border_panel.h"

#include <array>
#include <utility>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dcbuffer.h>
#include <wx/msgdlg.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "common_windows.h"
#include "find_item_window.h"
#include "graphics.h"
#include "items.h"
#include "materials_workbench_controller.h"
#include "gui.h"

namespace {
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

	const std::array<BorderEdgeSpec, 12> kBorderEdgeSpecs = {{
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
	}};

	constexpr int kBorderGridSize = 5;
	constexpr int kBorderGridCenterIndex = 2;
	constexpr int kBorderGridGapDip = 4;
	constexpr int kBorderGridCellSizeDip = 84;

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

	bool IsBorderGridCenterCell(int row, int col) {
		return row == kBorderGridCenterIndex && col == kBorderGridCenterIndex;
	}

	wxString GetBorderEdgeDisplayLabel(const wxString &edge) {
		const BorderEdgeSpec* spec = FindEdgeSpec(edge);
		return spec ? wxString::FromUTF8(spec->label) : edge;
	}

	wxString FormatOptionalBorderItemText(int itemId, const wxString &emptyLabel = "not set") {
		return itemId > 0 ? wxString::Format("item %d", itemId) : emptyLabel;
	}

	wxString FormatCenterGroundText(int groundEquivalent) {
		return groundEquivalent > 0 ? wxString::Format("item %d", groundEquivalent) : "painted base ground";
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

	wxString BuildUsageContextLabel(const BorderSetUsageRecord &usage) {
		const int previewItemId = ResolveUsagePreviewItemId(usage);
		if (previewItemId > 0) {
			return wxString::Format(
				"%s | %s | %s",
				usage.brushName,
				usage.align.IsEmpty() ? wxString("outer") : usage.align,
				FormatOptionalBorderItemText(previewItemId, "no center item")
			);
		}
		return wxString::Format(
			"%s | %s | no center item",
			usage.brushName,
			usage.align.IsEmpty() ? wxString("outer") : usage.align
		);
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

	void StyleBorderWorkspaceSubtitle(wxStaticText* label) {
		label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
	}

	void StyleBorderWorkspaceStatusLabel(wxStaticText* label) {
		label->SetMinSize(wxSize(-1, label->GetParent()->FromDIP(36)));
		label->Wrap(label->GetParent()->FromDIP(760));
	}

	void StyleBorderWorkspaceActionButton(wxButton* button, const wxString &tooltip) {
		button->SetMinSize(wxSize(button->GetParent()->FromDIP(120), -1));
		button->SetToolTip(tooltip);
	}

	void StyleBorderWorkspaceCaption(wxStaticText* label) {
		label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
	}

	void StyleBorderWorkspaceStrongValue(wxStaticText* label) {
		wxFont font = label->GetFont();
		font.SetWeight(wxFONTWEIGHT_BOLD);
		label->SetFont(font);
	}

	wxPanel* CreateSpacerCell(wxWindow* parent, int sizeDip) {
		wxPanel* spacer = new wxPanel(parent, wxID_ANY);
		spacer->SetMinSize(wxSize(parent->FromDIP(sizeDip), parent->FromDIP(sizeDip)));
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
			return {wxPoint(0, 1)}; // b1
		}
		if (edge == "csw") {
			return {wxPoint(4, 1)}; // b5
		}
		if (edge == "cne") {
			return {wxPoint(0, 3)}; // d1
		}
		if (edge == "cnw") {
			return {wxPoint(4, 3)}; // d5
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
		void DrawSprite(wxDC &dc, int itemId, int x, int y, int size) const {
			if (itemId <= 0) {
				return;
			}
			if (Sprite* sprite = g_gui.gfx.getSprite(itemId)) {
				sprite->DrawTo(&dc, SPRITE_SIZE_32x32, x, y, size, size);
			}
		}

		void OnPaint(wxPaintEvent &) {
			wxAutoBufferedPaintDC dc(this);
			const wxSize clientSize = GetClientSize();
			dc.SetBackground(wxBrush(wxColour(12, 12, 12)));
			dc.Clear();

			const int tileSize = std::max(1, std::min(clientSize.GetWidth(), clientSize.GetHeight()) / kBorderGridSize);
			const int sceneSize = tileSize * kBorderGridSize;
			const int originX = std::max(0, (clientSize.GetWidth() - sceneSize) / 2);
			const int originY = std::max(0, (clientSize.GetHeight() - sceneSize) / 2);

			if (centerItemId_ > 0) {
				DrawSprite(
					dc,
					centerItemId_,
					originX + kBorderGridCenterIndex * tileSize,
					originY + kBorderGridCenterIndex * tileSize,
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
					DrawSprite(dc, it->second, originX + duplicateCell.x * tileSize, originY + duplicateCell.y * tileSize, tileSize);
				}
			}

			for (const BorderEdgeSpec &spec : kBorderEdgeSpecs) {
				const wxString edge = wxString::FromUTF8(spec.edge);
				const auto it = slotItemIds_.find(edge);
				if (it == slotItemIds_.end() || it->second <= 0) {
					continue;
				}
				const wxPoint displayCell = GetBorderPreviewDisplayCell(spec);
				DrawSprite(dc, it->second, originX + displayCell.x * tileSize, originY + displayCell.y * tileSize, tileSize);
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
		return left.id == right.id &&
			   left.xmlBorderId == right.xmlBorderId &&
			   left.borderScope == right.borderScope &&
			   left.borderType == right.borderType &&
			   left.borderGroup == right.borderGroup &&
			   left.groundEquivalent == right.groundEquivalent &&
			   left.ownerBrushId == right.ownerBrushId &&
			   left.sourceFile == right.sourceFile;
	}

	bool AreBorderSetItemRecordsEqual(const BorderSetItemRecord &left, const BorderSetItemRecord &right) {
		return left.borderSetId == right.borderSetId &&
			   left.edge == right.edge &&
			   left.itemId == right.itemId &&
			   left.sortOrder == right.sortOrder;
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

MaterialsWorkbenchBorderPanel::MaterialsWorkbenchBorderPanel(wxWindow* parent, MaterialsWorkbenchController &controller) :
	wxPanel(parent, wxID_ANY),
	controller_(controller) {
	BuildLayout();
	ClearWorkspace("Select a border set in the navigation tree to edit its layout.");
}

void MaterialsWorkbenchBorderPanel::SetOnBorderSetSaved(std::function<void(int64_t)> callback) {
	onBorderSetSaved_ = std::move(callback);
}

void MaterialsWorkbenchBorderPanel::SetOnBorderSetStateChanged(std::function<void()> callback) {
	onBorderSetStateChanged_ = std::move(callback);
}

void MaterialsWorkbenchBorderPanel::SetOnOpenLinkedBrush(std::function<void(int64_t)> callback) {
	onOpenLinkedBrush_ = std::move(callback);
}

bool MaterialsWorkbenchBorderPanel::HasPendingChanges() const {
	return hasBorderSet_ && dirty_;
}

bool MaterialsWorkbenchBorderPanel::IsCurrentBorderSelection(const wxString &contextKey, int itemIndex) const {
	return hasBorderSet_ && currentContextKey_ == contextKey && currentItemIndex_ == itemIndex;
}

wxString MaterialsWorkbenchBorderPanel::GetCurrentBorderSetDisplayName() const {
	if (!hasBorderSet_) {
		return "";
	}

	return BuildBorderSetDisplayLabel(BuildComparableStorageFromCurrentState().borderSet);
}

bool MaterialsWorkbenchBorderPanel::ResolvePendingChangesBeforeSwitch(wxWindow* parent, const wxString &targetLabel) {
	if (!HasPendingChanges()) {
		return true;
	}

	const wxString currentLabel = BuildBorderSetDisplayLabel(borderSetStorage_.borderSet);
	const wxString destination = targetLabel.IsEmpty() ? "the selected entry" : "\"" + targetLabel + "\"";
	wxMessageDialog dialog(
		parent,
		"Border set \"" + currentLabel + "\" has unsaved changes.\n\n"
		"You are switching to " + destination + ".\n\n"
		"Yes: save and continue\n"
		"No: discard local changes and continue\n"
		"Cancel: stay on the current border set",
		"Unsaved Border Changes",
		wxYES_NO | wxCANCEL | wxICON_WARNING
	);
	dialog.SetYesNoCancelLabels("Save", "Discard", "Cancel");

	switch (dialog.ShowModal()) {
	case wxID_YES:
		return SaveCurrentBorderSet();
	case wxID_NO:
		return LoadBorderSet(currentContextKey_, currentItemIndex_);
	default:
		SetStatusMessage("Selection change canceled. Pending border edits were kept.");
		return false;
	}
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
	StyleBorderWorkspaceSubtitle(subtitleLabel_);

	wxScrolledWindow* scrolled = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	scrolled->SetScrollRate(FromDIP(10), FromDIP(10));

	wxBoxSizer* contentSizer = new wxBoxSizer(wxVERTICAL);
	summaryLabel_ = new wxStaticText(scrolled, wxID_ANY, "");
	identityLabel_ = new wxStaticText(scrolled, wxID_ANY, "");
	StyleBorderWorkspaceCaption(identityLabel_);

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
	usageContextChoice_ = new wxChoice(scrolled, wxID_ANY);
	openLinkedBrushButton_ = new wxButton(scrolled, wxID_ANY, "Open Linked Brush");
	usageContextHintLabel_ = new wxStaticText(scrolled, wxID_ANY, "Global borders resolve the center from linked brushes. Pick a usage context to preview the actual center tile.");
	StyleBorderWorkspaceCaption(usageContextHintLabel_);
	StyleBorderWorkspaceActionButton(openLinkedBrushButton_, "Open the linked brush that provides the center ground for this global border usage.");

	const wxSize metadataFieldSize(FromDIP(170), -1);
	idCtrl_->SetMinSize(metadataFieldSize);
	xmlBorderIdCtrl_->SetMinSize(metadataFieldSize);
	scopeChoice_->SetMinSize(metadataFieldSize);
	typeCtrl_->SetMinSize(metadataFieldSize);
	borderGroupCtrl_->SetMinSize(metadataFieldSize);
	groundEquivalentCtrl_->SetMinSize(metadataFieldSize);
	ownerBrushIdCtrl_->SetMinSize(metadataFieldSize);
	sourceCtrl_->SetMinSize(metadataFieldSize);
	usageContextChoice_->SetMinSize(metadataFieldSize);

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
	addMetadataField("Global Usage Context", usageContextChoice_);
	metadataGrid->Add(usageContextHintLabel_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
	metadataGrid->Add(openLinkedBrushButton_, 0, wxBOTTOM, FromDIP(8));
	metadataBox->Add(metadataGrid, 1, wxEXPAND | wxALL, FromDIP(8));

	wxBoxSizer* workspaceSizer = new wxBoxSizer(wxHORIZONTAL);

	wxStaticBoxSizer* gridBox = new wxStaticBoxSizer(wxVERTICAL, scrolled, "Slot Grid");
	wxFlexGridSizer* slotGridSizer = new wxFlexGridSizer(kBorderGridSize, kBorderGridSize, FromDIP(kBorderGridGapDip), FromDIP(kBorderGridGapDip));

	for (int row = 0; row < kBorderGridSize; ++row) {
		for (int col = 0; col < kBorderGridSize; ++col) {
			const BorderEdgeSpec* specForCell = FindEdgeSpecForCell(row, col);

			if (IsBorderGridCenterCell(row, col)) {
				wxPanel* centerPanel = new wxPanel(scrolled, wxID_ANY);
				centerPanel->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
				wxBoxSizer* centerSizer = new wxBoxSizer(wxVERTICAL);
				wxStaticText* centerLabel = CreateBorderSectionLabel(centerPanel, "Center Ground");
				wxStaticText* centerHint = new wxStaticText(centerPanel, wxID_ANY, "Uses groundEquivalent");
				centerGroundSlotPreview_ = new ItemButton(centerPanel, RENDER_SIZE_32x32, 0);
				centerGroundSlotPreview_->Enable(false);
				centerGroundSlotValueLabel_ = new wxStaticText(centerPanel, wxID_ANY, "item 0");
				StyleBorderWorkspaceCaption(centerHint);
				StyleBorderWorkspaceStrongValue(centerGroundSlotValueLabel_);

				centerSizer->Add(centerLabel, 0, wxALIGN_CENTER | wxTOP | wxBOTTOM, FromDIP(2));
				centerSizer->Add(centerGroundSlotPreview_, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(4));
				centerSizer->Add(centerGroundSlotValueLabel_, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(1));
				centerSizer->Add(centerHint, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(2));
				centerPanel->SetSizer(centerSizer);
				centerPanel->SetMinSize(wxSize(FromDIP(kBorderGridCellSizeDip), FromDIP(kBorderGridCellSizeDip)));
				slotGridSizer->Add(centerPanel, 0, wxEXPAND);
				continue;
			}

			if (!specForCell) {
				slotGridSizer->Add(CreateSpacerCell(scrolled, kBorderGridCellSizeDip), 0, wxEXPAND);
				continue;
			}

			wxPanel* cell = new wxPanel(scrolled, wxID_ANY);
			cell->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
			wxBoxSizer* cellSizer = new wxBoxSizer(wxVERTICAL);
			wxString edge = wxString::FromUTF8(specForCell->edge);
			wxStaticText* label = CreateBorderSectionLabel(cell, specForCell->label);
			BorderSlotButton* button = new BorderSlotButton(cell);
			wxStaticText* value = new wxStaticText(cell, wxID_ANY, "item 0");
			StyleBorderWorkspaceStrongValue(value);

			button->Bind(wxEVT_LEFT_DOWN, [this, edge](wxMouseEvent &event) {
				SelectEdge(edge);
				event.Skip();
			});

			cellSizer->Add(label, 0, wxALIGN_CENTER | wxTOP | wxBOTTOM, FromDIP(2));
			cellSizer->Add(button, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(4));
			cellSizer->Add(value, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(2));
			cell->SetSizer(cellSizer);
			cell->SetMinSize(wxSize(FromDIP(kBorderGridCellSizeDip), FromDIP(kBorderGridCellSizeDip)));

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
	StyleBorderWorkspaceActionButton(pickItemButton, "Choose an item id from the picker for the selected border slot.");
	StyleBorderWorkspaceActionButton(applyButton, "Apply the selected item id to the active border slot.");
	StyleBorderWorkspaceActionButton(clearButton, "Clear the selected slot back to item id 0 in the local editor.");
	selectionActions->Add(pickItemButton, 0, wxEXPAND);
	selectionActions->Add(applyButton, 0, wxEXPAND);
	selectionActions->Add(clearButton, 0, wxEXPAND);
	selectionActions->AddSpacer(0);

	editorBox->Add(selectedEdgeLabel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
	editorBox->Add(selectionRow, 0, wxEXPAND | wxALL, FromDIP(8));
	editorBox->Add(selectionActions, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

	wxStaticBoxSizer* previewBox = new wxStaticBoxSizer(wxVERTICAL, scrolled, "Preview Matrix");
	wxStaticText* previewHint = new wxStaticText(scrolled, wxID_ANY, "Compact composed silhouette. The selected slot stays highlighted in the same runtime position.");
	StyleBorderWorkspaceCaption(previewHint);
	previewSelectionLabel_ = new wxStaticText(scrolled, wxID_ANY, "Selected preview slot: none");
	StyleBorderWorkspaceStrongValue(previewSelectionLabel_);
	previewHint->Wrap(FromDIP(220));
	previewSelectionLabel_->Wrap(FromDIP(220));
	previewMatrixPanel_ = new BorderPreviewMatrixPanel(scrolled);

	previewBox->Add(previewHint, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
	previewBox->Add(previewSelectionLabel_, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(4));
	previewBox->Add(previewMatrixPanel_, 0, wxALL, FromDIP(8));

	wxBoxSizer* leftColumn = new wxBoxSizer(wxVERTICAL);
	leftColumn->Add(metadataBox, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
	leftColumn->Add(editorBox, 0, wxEXPAND);
	leftColumn->SetMinSize(wxSize(FromDIP(215), -1));

	wxBoxSizer* visualsRow = new wxBoxSizer(wxHORIZONTAL);
	visualsRow->Add(gridBox, 1, wxRIGHT | wxEXPAND, FromDIP(8));
	previewBox->SetMinSize(wxSize(FromDIP(250), -1));
	visualsRow->Add(previewBox, 0, wxEXPAND);

	workspaceSizer->Add(leftColumn, 0, wxRIGHT | wxEXPAND, FromDIP(10));
	workspaceSizer->Add(visualsRow, 1, wxEXPAND);

	contentSizer->Add(summaryLabel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
	contentSizer->Add(identityLabel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(2));
	contentSizer->Add(new wxStaticLine(scrolled), 0, wxEXPAND | wxALL, FromDIP(8));
	contentSizer->Add(workspaceSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

	scrolled->SetSizer(contentSizer);

	wxBoxSizer* headerSizer = new wxBoxSizer(wxVERTICAL);
	headerSizer->Add(title, 0, wxBOTTOM, FromDIP(4));
	headerSizer->Add(titleLabel_, 0, wxBOTTOM, FromDIP(2));
	headerSizer->Add(subtitleLabel_, 0);

	wxBoxSizer* actionSizer = new wxBoxSizer(wxHORIZONTAL);
	saveButton_ = new wxButton(this, wxID_SAVE, "Save Border");
	revertButton_ = new wxButton(this, wxID_ANY, "Revert");
	StyleBorderWorkspaceActionButton(saveButton_, "Write the current border set metadata and slots to materials.db.");
	StyleBorderWorkspaceActionButton(revertButton_, "Discard local border edits and reload the current border set from materials.db.");
	actionSizer->Add(saveButton_, 0, wxRIGHT, FromDIP(6));
	actionSizer->Add(revertButton_, 0);

	statusLabel_ = new wxStaticText(this, wxID_ANY, "");
	StyleBorderWorkspaceStatusLabel(statusLabel_);

	rootSizer->Add(headerSizer, 0, wxEXPAND | wxALL, FromDIP(8));
	rootSizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));
	rootSizer->Add(scrolled, 1, wxEXPAND | wxALL, FromDIP(8));
	rootSizer->Add(actionSizer, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
	rootSizer->Add(statusLabel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
	SetSizer(rootSizer);

	pickItemButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBorderPanel::OnPickItem, this);
	applyButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBorderPanel::OnApplyToSlot, this);
	clearButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBorderPanel::OnClearSlot, this);
	saveButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBorderPanel::OnSave, this);
	revertButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBorderPanel::OnRevert, this);
	selectedItemIdCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBorderPanel::OnSelectedItemIdChanged, this);
	selectedItemIdCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBorderPanel::OnSelectedItemIdSpin, this);
	xmlBorderIdCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBorderPanel::OnMetadataFieldChanged, this);
	xmlBorderIdCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBorderPanel::OnMetadataFieldChanged, this);
	scopeChoice_->Bind(wxEVT_CHOICE, &MaterialsWorkbenchBorderPanel::OnMetadataFieldChanged, this);
	typeCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBorderPanel::OnMetadataFieldChanged, this);
	borderGroupCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBorderPanel::OnMetadataFieldChanged, this);
	borderGroupCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBorderPanel::OnMetadataFieldChanged, this);
	groundEquivalentCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBorderPanel::OnMetadataFieldChanged, this);
	usageContextChoice_->Bind(wxEVT_CHOICE, &MaterialsWorkbenchBorderPanel::OnUsageContextChanged, this);
	openLinkedBrushButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBorderPanel::OnOpenLinkedBrush, this);
	groundEquivalentCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBorderPanel::OnMetadataFieldChanged, this);
	sourceCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBorderPanel::OnMetadataFieldChanged, this);
}

void MaterialsWorkbenchBorderPanel::ClearWorkspace(const wxString &message) {
	borderSetStorage_ = BorderSetStorageRecord();
	loadedBorderSetStorage_ = BorderSetStorageRecord();
	borderSetUsages_.clear();
	currentContextKey_.clear();
	currentItemIndex_ = -1;
	selectedUsageIndex_ = wxNOT_FOUND;
	hasBorderSet_ = false;
	dirty_ = false;
	selectedEdge_.clear();
	slotItemIds_.clear();

	UpdateWorkspaceHeader();
	summaryLabel_->SetLabel(message);
	if (identityLabel_) {
		identityLabel_->SetLabel("");
	}

	internalUpdate_ = true;
	idCtrl_->SetValue("");
	xmlBorderIdCtrl_->SetValue(0);
	scopeChoice_->SetSelection(wxNOT_FOUND);
	typeCtrl_->SetValue("");
	borderGroupCtrl_->SetValue(0);
	groundEquivalentCtrl_->SetValue(0);
	ownerBrushIdCtrl_->SetValue("");
	sourceCtrl_->SetValue("");
	usageContextChoice_->Clear();
	selectedEdgeLabel_->SetLabel("Edge: none");
	selectedItemIdCtrl_->SetValue(0);
	selectedItemPreview_->SetSprite(0);
	internalUpdate_ = false;

	if (centerGroundSlotPreview_) {
		centerGroundSlotPreview_->SetSprite(0);
	}
	if (centerGroundSlotValueLabel_) {
		centerGroundSlotValueLabel_->SetLabel("not set");
	}
	if (previewSelectionLabel_) {
		previewSelectionLabel_->SetLabel("Selected preview slot: none");
	}
	if (usageContextHintLabel_) {
		usageContextHintLabel_->SetLabel("Global borders resolve the center from linked brushes. Pick a usage context to preview the actual center tile.");
	}

	for (const auto &entry : slotButtons_) {
		entry.second->SetSprite(0);
		entry.second->SetValue(false);
	}
	for (const auto &entry : slotValueLabels_) {
		entry.second->SetLabel("item 0");
	}
	if (previewMatrixPanel_) {
		static_cast<BorderPreviewMatrixPanel*>(previewMatrixPanel_)->SetPreviewState({}, 0, "");
	}
	RefreshPreviewSelectionState();

	SetFieldsEnabled(false);
	UpdateActionButtons();
	NotifyBorderSetStateChanged();
	SetStatusMessage(message);
	Layout();
}

bool MaterialsWorkbenchBorderPanel::LoadBorderSet(const wxString &contextKey, int itemIndex) {
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

	borderSetStorage_ = storage;
	loadedBorderSetStorage_ = storage;
	borderSetUsages_ = std::move(usages);
	currentContextKey_ = contextKey;
	currentItemIndex_ = itemIndex;
	hasBorderSet_ = true;
	dirty_ = false;
	selectedUsageIndex_ = borderSetUsages_.empty() ? wxNOT_FOUND : 0;

	PopulateFields();
	SetFieldsEnabled(true);
	UpdateActionButtons();
	NotifyBorderSetStateChanged();
	SetStatusMessage("Ready. Editing border data from materials.db. Update slots or metadata, then Save or Revert.");
	Layout();
	return true;
}

void MaterialsWorkbenchBorderPanel::PopulateFields() {
	const BorderSetRecord &borderSet = borderSetStorage_.borderSet;

	UpdateWorkspaceHeader();

	internalUpdate_ = true;
	idCtrl_->SetValue(wxString::Format("%lld", static_cast<long long>(borderSet.id)));
	xmlBorderIdCtrl_->SetValue(borderSet.xmlBorderId);
	scopeChoice_->SetStringSelection(borderSet.borderScope);
	typeCtrl_->SetValue(borderSet.borderType);
	borderGroupCtrl_->SetValue(borderSet.borderGroup);
	groundEquivalentCtrl_->SetValue(borderSet.groundEquivalent);
	ownerBrushIdCtrl_->SetValue(borderSet.ownerBrushId > 0 ? wxString::Format("%lld", static_cast<long long>(borderSet.ownerBrushId)) : "");
	sourceCtrl_->SetValue(borderSet.sourceFile);
	PopulateUsageContextChoice();
	internalUpdate_ = false;
	UpdateUsageContextControls();

	slotItemIds_.clear();
	for (const BorderSetItemRecord &item : borderSetStorage_.items) {
		slotItemIds_[item.edge] = item.itemId;
	}

	RefreshSlotGrid();
	RefreshPreviewGrid();
	RestoreCurrentBorderEditorState();
}

void MaterialsWorkbenchBorderPanel::UpdateSummaryLabels() {
	if (!hasBorderSet_) {
		return;
	}

	const BorderSetStorageRecord currentStorage = BuildComparableStorageFromCurrentState();
	const BorderSetRecord &borderSet = currentStorage.borderSet;

	summaryLabel_->SetLabel(wxString::Format(
		"Border Set: %s | Filled Slots: %zu | Scope: %s | Type: %s",
		BuildBorderSetDisplayLabel(borderSet),
		currentStorage.items.size(),
		borderSet.borderScope,
		borderSet.borderType
	));

	const wxString centerGroundText = ResolveCenterSourceLabel();
	if (borderSet.borderScope == "global") {
		identityLabel_->SetLabel(wxString::Format(
			"Border identity: XML Border ID %d | SQLite row %lld | Usage contexts: %zu | Center source: %s",
			borderSet.xmlBorderId,
			static_cast<long long>(borderSet.id),
			borderSetUsages_.size(),
			centerGroundText
		));
	} else {
		identityLabel_->SetLabel(wxString::Format(
			"Border identity: inline set on SQLite row %lld | Owner Brush ID %lld | Center source: %s",
			static_cast<long long>(borderSet.id),
			static_cast<long long>(borderSet.ownerBrushId),
			centerGroundText
		));
	}
}

BorderSetStorageRecord MaterialsWorkbenchBorderPanel::BuildComparableStorageFromCurrentState() const {
	BorderSetStorageRecord storage = borderSetStorage_;
	storage.borderSet.xmlBorderId = xmlBorderIdCtrl_->GetValue();
	storage.borderSet.borderScope = scopeChoice_->GetSelection() == wxNOT_FOUND ? "" : scopeChoice_->GetStringSelection();
	storage.borderSet.borderType = typeCtrl_->GetValue().Trim(true).Trim(false);
	storage.borderSet.borderGroup = borderGroupCtrl_->GetValue();
	storage.borderSet.groundEquivalent = groundEquivalentCtrl_->GetValue();
	storage.borderSet.sourceFile = sourceCtrl_->GetValue().Trim(true).Trim(false);

	if (storage.borderSet.borderType.IsEmpty()) {
		storage.borderSet.borderType = "normal";
	}

	storage.items.clear();
	int sortOrder = 0;
	for (const BorderEdgeSpec &spec : kBorderEdgeSpecs) {
		const wxString edge = wxString::FromUTF8(spec.edge);
		const int itemId = slotItemIds_.count(edge) > 0 ? slotItemIds_.at(edge) : 0;
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

void MaterialsWorkbenchBorderPanel::PopulateUsageContextChoice() {
	if (!usageContextChoice_) {
		return;
	}

	usageContextChoice_->Clear();
	for (const BorderSetUsageRecord &usage : borderSetUsages_) {
		usageContextChoice_->Append(BuildUsageContextLabel(usage));
	}

	if (borderSetUsages_.empty()) {
		selectedUsageIndex_ = wxNOT_FOUND;
		usageContextChoice_->SetSelection(wxNOT_FOUND);
		return;
	}

	if (selectedUsageIndex_ == wxNOT_FOUND || selectedUsageIndex_ >= static_cast<int>(borderSetUsages_.size())) {
		selectedUsageIndex_ = 0;
	}
	usageContextChoice_->SetSelection(selectedUsageIndex_);
}

void MaterialsWorkbenchBorderPanel::UpdateUsageContextControls() {
	if (!hasBorderSet_) {
		if (usageContextChoice_) {
			usageContextChoice_->Enable(false);
		}
		if (openLinkedBrushButton_) {
			openLinkedBrushButton_->Enable(false);
		}
		return;
	}

	const bool isGlobal = scopeChoice_->GetStringSelection() == "global";
	const BorderSetUsageRecord* usage = GetSelectedUsageContext();
	if (usageContextChoice_) {
		usageContextChoice_->Enable(isGlobal && !borderSetUsages_.empty());
	}
	if (openLinkedBrushButton_) {
		openLinkedBrushButton_->Enable(isGlobal && usage && usage->brushId > 0);
	}
	if (usageContextHintLabel_) {
		if (!isGlobal) {
			usageContextHintLabel_->SetLabel("Inline border sets own their center tile directly through groundEquivalent.");
		} else if (usage) {
			usageContextHintLabel_->SetLabel(wxString::Format(
				"Previewing runtime context from brush \"%s\". Global border centers come from linked brushes, not from the shared border set.",
				usage->brushName
			));
		} else {
			usageContextHintLabel_->SetLabel("This global border set has no linked brush usage in materials.db yet.");
		}
		usageContextHintLabel_->Wrap(FromDIP(190));
	}
}

const BorderSetUsageRecord* MaterialsWorkbenchBorderPanel::GetSelectedUsageContext() const {
	if (selectedUsageIndex_ == wxNOT_FOUND || selectedUsageIndex_ < 0 || selectedUsageIndex_ >= static_cast<int>(borderSetUsages_.size())) {
		return nullptr;
	}
	return &borderSetUsages_[selectedUsageIndex_];
}

int MaterialsWorkbenchBorderPanel::ResolveCenterPreviewItemId() const {
	if (!hasBorderSet_) {
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

wxString MaterialsWorkbenchBorderPanel::ResolveCenterSourceLabel() const {
	if (!hasBorderSet_) {
		return "not set";
	}

	const BorderSetStorageRecord currentStorage = BuildComparableStorageFromCurrentState();
	const BorderSetRecord &borderSet = currentStorage.borderSet;
	if (borderSet.groundEquivalent > 0) {
		return wxString::Format("groundEquivalent %s", FormatOptionalBorderItemText(borderSet.groundEquivalent));
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

void MaterialsWorkbenchBorderPanel::HandleUsageContextChanged() {
	UpdateUsageContextControls();
	RefreshSlotGrid();
	RefreshPreviewGrid();
}

bool MaterialsWorkbenchBorderPanel::ValidateBorderSetStorage(const BorderSetStorageRecord &storage, wxString &error) const {
	const wxString scope = storage.borderSet.borderScope.Lower();
	if (scope.IsEmpty()) {
		error = "Border scope must be selected.";
		return false;
	}
	if (scope != "global" && scope != "inline") {
		error = wxString::Format("Border scope \"%s\" is not supported.", storage.borderSet.borderScope);
		return false;
	}
	if (scope == "global" && storage.borderSet.xmlBorderId <= 0) {
		error = "Global border sets must use an XML Border ID greater than zero so runtime refresh can target the saved set.";
		return false;
	}
	if (scope == "inline" && storage.borderSet.ownerBrushId <= 0) {
		error = "Inline border sets must stay linked to an owner brush before saving.";
		return false;
	}

	if (storage.borderSet.groundEquivalent < 0) {
		error = "Ground equivalent cannot be negative.";
		return false;
	}

	if (storage.borderSet.groundEquivalent > 0 && !IsKnownBorderPanelItemId(storage.borderSet.groundEquivalent)) {
		error = wxString::Format("Ground equivalent uses unknown item id %d.", storage.borderSet.groundEquivalent);
		return false;
	}

	std::map<int, wxString> itemEdgeById;
	for (const BorderSetItemRecord &item : storage.items) {
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

	error.clear();
	return true;
}

void MaterialsWorkbenchBorderPanel::SaveCurrentBorderEditorState() {
	if (!hasBorderSet_ || borderSetStorage_.borderSet.id <= 0) {
		return;
	}

	if (selectedEdge_.IsEmpty()) {
		borderSetSelectedEdges_.erase(borderSetStorage_.borderSet.id);
		return;
	}

	borderSetSelectedEdges_[borderSetStorage_.borderSet.id] = selectedEdge_;
}

void MaterialsWorkbenchBorderPanel::RestoreCurrentBorderEditorState() {
	wxString edgeToSelect;
	if (borderSetStorage_.borderSet.id > 0) {
		auto it = borderSetSelectedEdges_.find(borderSetStorage_.borderSet.id);
		if (it != borderSetSelectedEdges_.end()) {
			edgeToSelect = it->second;
		}
	}

	if (edgeToSelect.IsEmpty() && slotButtons_.count(selectedEdge_)) {
		edgeToSelect = selectedEdge_;
	}
	if (edgeToSelect.IsEmpty() || !slotButtons_.count(edgeToSelect)) {
		edgeToSelect = "n";
	}

	SelectEdge(edgeToSelect);
}

void MaterialsWorkbenchBorderPanel::RefreshDirtyState() {
	if (!hasBorderSet_) {
		dirty_ = false;
		UpdateWorkspaceHeader();
		UpdateActionButtons();
		NotifyBorderSetStateChanged();
		return;
	}

	dirty_ = !AreBorderSetStorageRecordsEqual(BuildComparableStorageFromCurrentState(), loadedBorderSetStorage_);
	UpdateWorkspaceHeader();
	UpdateActionButtons();
	NotifyBorderSetStateChanged();
}

void MaterialsWorkbenchBorderPanel::NotifyBorderSetStateChanged() {
	if (onBorderSetStateChanged_) {
		onBorderSetStateChanged_();
	}
}

void MaterialsWorkbenchBorderPanel::UpdateWorkspaceHeader() {
	if (!hasBorderSet_) {
		titleLabel_->SetLabel("No border set selected");
		subtitleLabel_->SetLabel("Select a border set in the navigation tree to edit metadata, slot ownership, and sprite layout.");
		return;
	}

	const wxString modifiedSuffix = dirty_ ? " [modified]" : "";
	titleLabel_->SetLabel("Editing " + BuildBorderSetDisplayLabel(BuildComparableStorageFromCurrentState().borderSet) + modifiedSuffix);
	const wxString scope = scopeChoice_->GetStringSelection();
	if (dirty_) {
		subtitleLabel_->SetLabel("Unsaved local border edits differ from materials.db. Save to persist them or Revert to discard them before switching sets.");
	} else if (scope == "global") {
		subtitleLabel_->SetLabel("Global border sprites are shared. Center preview comes from the selected linked brush context so the runtime composition stays truthful.");
	} else {
		subtitleLabel_->SetLabel("Ready to assign border sprites to runtime slots and save the layout back to materials.db.");
	}
}

void MaterialsWorkbenchBorderPanel::UpdateActionButtons() {
	if (saveButton_) {
		saveButton_->Enable(hasBorderSet_ && dirty_);
	}
	if (revertButton_) {
		revertButton_->Enable(hasBorderSet_ && dirty_);
	}
}

void MaterialsWorkbenchBorderPanel::RefreshSlotGrid() {
	const int centerPreviewItemId = ResolveCenterPreviewItemId();
	const wxString centerSourceLabel = ResolveCenterSourceLabel();
	if (centerGroundSlotPreview_) {
		centerGroundSlotPreview_->SetSprite(centerPreviewItemId);
		centerGroundSlotPreview_->SetToolTip(
			centerPreviewItemId > 0
				? wxString::Format("Center preview resolves to %s.", centerSourceLabel)
				: wxString::Format("Center preview source: %s.", centerSourceLabel)
		);
	}
	if (centerGroundSlotValueLabel_) {
		centerGroundSlotValueLabel_->SetLabel(centerSourceLabel);
	}

	for (const auto &entry : slotButtons_) {
		const wxString &edge = entry.first;
		const wxString slotLabel = GetBorderEdgeDisplayLabel(edge);
		const int itemId = slotItemIds_.count(edge) > 0 ? slotItemIds_[edge] : 0;
		entry.second->SetSprite(itemId);
		entry.second->SetValue(edge == selectedEdge_);
		slotValueLabels_[edge]->SetLabel(FormatOptionalBorderItemText(itemId, "empty"));
		entry.second->SetToolTip(
			itemId > 0
				? wxString::Format("%s slot uses item %d.", slotLabel, itemId)
				: wxString::Format("%s slot is empty.", slotLabel)
		);
	}

	UpdateSummaryLabels();
}

void MaterialsWorkbenchBorderPanel::RefreshPreviewGrid() {
	const int centerPreviewItemId = ResolveCenterPreviewItemId();
	const wxString centerSourceLabel = ResolveCenterSourceLabel();
	if (previewMatrixPanel_) {
		previewMatrixPanel_->SetToolTip(
			centerPreviewItemId > 0
				? wxString::Format("Preview composes the runtime scene from the 5x5 matrix using %s as base ground.", centerSourceLabel)
				: wxString::Format("Preview composes the runtime scene from the 5x5 matrix. Center source: %s.", centerSourceLabel)
		);
		static_cast<BorderPreviewMatrixPanel*>(previewMatrixPanel_)->SetPreviewState(slotItemIds_, centerPreviewItemId, selectedEdge_);
	}

	if (previewSelectionLabel_) {
		if (selectedEdge_.IsEmpty()) {
			previewSelectionLabel_->SetLabel("Selected preview slot: none | Center source: " + centerSourceLabel);
		} else {
			const int selectedItemId = slotItemIds_.count(selectedEdge_) > 0 ? slotItemIds_[selectedEdge_] : 0;
			previewSelectionLabel_->SetLabel(wxString::Format(
				"Selected preview slot: %s | %s",
				GetBorderEdgeDisplayLabel(selectedEdge_),
				FormatOptionalBorderItemText(selectedItemId, "empty")
			));
		}
	}

	RefreshPreviewSelectionState();
}

void MaterialsWorkbenchBorderPanel::RefreshPreviewSelectionState() {
	if (previewMatrixPanel_) {
		previewMatrixPanel_->Refresh();
	}
}

void MaterialsWorkbenchBorderPanel::SelectEdge(const wxString &edge) {
	if (!slotButtons_.count(edge)) {
		return;
	}

	selectedEdge_ = edge;
	SaveCurrentBorderEditorState();
	RefreshSlotGrid();
	RefreshPreviewGrid();
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
	internalUpdate_ = true;
	selectedItemIdCtrl_->SetValue(itemId);
	selectedItemPreview_->SetSprite(itemId);
	internalUpdate_ = false;
}

void MaterialsWorkbenchBorderPanel::SyncSelectedSlotFromEditor(bool updateStatus) {
	if (!hasBorderSet_ || selectedEdge_.IsEmpty()) {
		return;
	}

	const int itemId = selectedItemIdCtrl_->GetValue();
	const std::map<wxString, int> previousSlotItemIds = slotItemIds_;
	if (itemId > 0) {
		slotItemIds_[selectedEdge_] = itemId;
	} else {
		slotItemIds_.erase(selectedEdge_);
	}

	if (updateStatus) {
		const BorderSetStorageRecord candidateStorage = BuildComparableStorageFromCurrentState();
		wxString validationError;
		if (!ValidateBorderSetStorage(candidateStorage, validationError)) {
			slotItemIds_ = previousSlotItemIds;
			RefreshSlotGrid();
			RefreshPreviewGrid();
			SetStatusMessage("Cannot apply slot: " + validationError);
			return;
		}
	}

	selectedItemPreview_->SetSprite(itemId);
	RefreshSlotGrid();
	RefreshPreviewGrid();
	RefreshDirtyState();
	if (updateStatus) {
		SetStatusMessage("Slot updated locally. Save the border set to persist.");
	}
}

void MaterialsWorkbenchBorderPanel::SetStatusMessage(const wxString &message) {
	statusLabel_->SetLabel(message);
	statusLabel_->Wrap(FromDIP(760));
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
	UpdateUsageContextControls();
}

bool MaterialsWorkbenchBorderPanel::SaveCurrentBorderSet() {
	if (!hasBorderSet_) {
		SetStatusMessage("Select a border set before saving.");
		return false;
	}

	BorderSetStorageRecord comparableStorage = BuildComparableStorageFromCurrentState();
	wxString validationError;
	if (!ValidateBorderSetStorage(comparableStorage, validationError)) {
		SetStatusMessage("Cannot save border set: " + validationError);
		return false;
	}

	borderSetStorage_ = comparableStorage;

	wxString error;
	if (!controller_.SaveBorderSet(borderSetStorage_, error)) {
		SetStatusMessage("Failed to save border set: " + error);
		return false;
	}

	loadedBorderSetStorage_ = borderSetStorage_;
	dirty_ = false;
	PopulateFields();
	UpdateActionButtons();
	NotifyBorderSetStateChanged();
	SetStatusMessage("Saved border metadata and slots to materials.db. Targeted runtime sync remained in place.");
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
	RefreshDirtyState();
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

	SetStatusMessage("Reverted local border edits and reloaded the border set from materials.db.");
}

void MaterialsWorkbenchBorderPanel::OnSelectedItemIdChanged(wxCommandEvent &event) {
	if (internalUpdate_) {
		event.Skip();
		return;
	}
	SyncSelectedSlotFromEditor(false);
	event.Skip();
}

void MaterialsWorkbenchBorderPanel::OnSelectedItemIdSpin(wxSpinEvent &event) {
	if (internalUpdate_) {
		event.Skip();
		return;
	}
	SyncSelectedSlotFromEditor(false);
	event.Skip();
}

void MaterialsWorkbenchBorderPanel::OnMetadataFieldChanged(wxCommandEvent &event) {
	if (internalUpdate_ || !hasBorderSet_) {
		event.Skip();
		return;
	}

	UpdateUsageContextControls();
	UpdateWorkspaceHeader();
	RefreshSlotGrid();
	RefreshPreviewGrid();
	RefreshDirtyState();
	event.Skip();
}

void MaterialsWorkbenchBorderPanel::OnUsageContextChanged(wxCommandEvent &event) {
	if (internalUpdate_ || !hasBorderSet_) {
		event.Skip();
		return;
	}

	selectedUsageIndex_ = usageContextChoice_ ? usageContextChoice_->GetSelection() : wxNOT_FOUND;
	HandleUsageContextChanged();
	SetStatusMessage("Preview context updated. Global border center now reflects the selected linked brush.");
	event.Skip();
}

void MaterialsWorkbenchBorderPanel::OnOpenLinkedBrush(wxCommandEvent &event) {
	if (!hasBorderSet_) {
		SetStatusMessage("Load a border set before opening a linked brush.");
		return;
	}

	const BorderSetUsageRecord* usage = GetSelectedUsageContext();
	if (!usage || usage->brushId <= 0) {
		SetStatusMessage("This border set has no linked brush context to open.");
		return;
	}
	if (!onOpenLinkedBrush_) {
		SetStatusMessage("Linked brush navigation is unavailable in this workspace.");
		return;
	}

	onOpenLinkedBrush_(usage->brushId);
}
