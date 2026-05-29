#include "main.h"

#include "materials_workbench_border_panel.h"

#include <array>
#include <utility>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/choicdlg.h>
#include <wx/dcbuffer.h>
#include <wx/msgdlg.h>
#include <wx/listbox.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/textdlg.h>

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

	wxString FormatCompactItemIdText(int itemId, const wxString &emptyLabel = "empty") {
		return itemId > 0 ? wxString::Format("%d", itemId) : emptyLabel;
	}

	wxString FormatCompactCenterGroundText(int itemId) {
		return itemId > 0 ? wxString::Format("%d", itemId) : "painted";
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

	wxString BuildUsageContextLabel(const BorderSetUsageRecord &usage) {
		const int previewItemId = ResolveUsagePreviewItemId(usage);
		if (previewItemId > 0) {
			return wxString::Format(
				"%s  |  %s  |  center %d",
				usage.brushName,
				usage.align.IsEmpty() ? wxString("outer") : usage.align,
				previewItemId
			);
		}
		return wxString::Format(
			"%s  |  %s  |  painted",
			usage.brushName,
			usage.align.IsEmpty() ? wxString("outer") : usage.align
		);
	}

	wxString BuildBrushPickerLabel(const wxString &brushName, int64_t brushId, const wxString &emptyLabel = "Not selected") {
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

	class GlobalUsageDialog final : public wxDialog {
	public:
		GlobalUsageDialog(wxWindow* parent, const wxString &title, const GlobalUsageEditData &initialData) :
			wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
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

			roleChoice_->SetStringSelection(data_.borderRole.IsEmpty() ? "normal" : data_.borderRole);
			alignChoice_->SetStringSelection(data_.align.IsEmpty() ? "outer" : data_.align);
			targetModeChoice_->SetStringSelection(data_.targetMode.IsEmpty() ? "all" : data_.targetMode);
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
			FindBrushDialog dialog(this, "Choose Context Brush");
			if (dialog.ShowModal() != wxID_OK) {
				return;
			}
			const Brush* brush = dialog.getResult();
			if (!brush) {
				return;
			}
			data_.ownerBrushId = static_cast<int64_t>(brush->getID());
			data_.ownerBrushName = wxString::FromUTF8(brush->getName());
			ownerBrushCtrl_->SetValue(BuildBrushPickerLabel(data_.ownerBrushName, data_.ownerBrushId));
		}

		void OnPickTargetBrush(wxCommandEvent &) {
			FindBrushDialog dialog(this, "Choose Target Brush");
			if (dialog.ShowModal() != wxID_OK) {
				return;
			}
			const Brush* brush = dialog.getResult();
			if (!brush) {
				return;
			}
			data_.targetBrushId = static_cast<int64_t>(brush->getID());
			data_.targetBrushName = wxString::FromUTF8(brush->getName());
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

		GlobalUsageEditData data_;
		wxTextCtrl* ownerBrushCtrl_ = nullptr;
		wxChoice* roleChoice_ = nullptr;
		wxChoice* alignChoice_ = nullptr;
		wxChoice* targetModeChoice_ = nullptr;
		wxTextCtrl* targetBrushCtrl_ = nullptr;
		wxButton* targetBrushButton_ = nullptr;
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

void MaterialsWorkbenchBorderPanel::SetOnBorderSetDeleted(std::function<void(const wxString &)> callback) {
	onBorderSetDeleted_ = std::move(callback);
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

	wxStaticBoxSizer* metadataBox = new wxStaticBoxSizer(wxVERTICAL, scrolled, "Border Authoring");
	wxFlexGridSizer* metadataGrid = new wxFlexGridSizer(0, 2, FromDIP(6), FromDIP(8));
	metadataGrid->AddGrowableCol(1, 1);

	idCtrl_ = new wxTextCtrl(scrolled, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
	xmlBorderIdCtrl_ = new wxSpinCtrl(scrolled, wxID_ANY);
	xmlBorderIdCtrl_->SetRange(0, 1000000);
	scopeChoice_ = new wxChoice(scrolled, wxID_ANY);
	scopeChoice_->Append("global");
	scopeChoice_->Append("inline");
	typeCtrl_ = new wxChoice(scrolled, wxID_ANY);
	typeCtrl_->Append("normal");
	typeCtrl_->Append("optional");
	borderGroupCtrl_ = new wxChoice(scrolled, wxID_ANY);
	RebuildBorderGroupChoices(borderGroupCtrl_, 0);
	sourceCtrl_ = new wxTextCtrl(scrolled, wxID_ANY);

	const wxSize metadataFieldSize(FromDIP(132), -1);
	idCtrl_->SetMinSize(metadataFieldSize);
	xmlBorderIdCtrl_->SetMinSize(metadataFieldSize);
	scopeChoice_->SetMinSize(metadataFieldSize);
	typeCtrl_->SetMinSize(metadataFieldSize);
	borderGroupCtrl_->SetMinSize(metadataFieldSize);
	sourceCtrl_->Hide();

	const auto addMetadataField = [&](const wxString &label, wxWindow* control) {
		wxStaticText* fieldLabel = new wxStaticText(scrolled, wxID_ANY, label);
		StyleBorderWorkspaceCaption(fieldLabel);
		metadataGrid->Add(fieldLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
		metadataGrid->Add(control, 1, wxEXPAND);
		if (control == xmlBorderIdCtrl_) {
			xmlBorderIdLabel_ = fieldLabel;
		} else if (control == typeCtrl_) {
			typeLabel_ = fieldLabel;
		} else if (control == borderGroupCtrl_) {
			borderGroupLabel_ = fieldLabel;
		}
	};

	addMetadataField("Internal ID", idCtrl_);
	addMetadataField("Global Border ID", xmlBorderIdCtrl_);
	addMetadataField("Scope", scopeChoice_);
	addMetadataField("Border Style", typeCtrl_);
	addMetadataField("Autoborder Group", borderGroupCtrl_);
	typeCtrl_->SetToolTip("Choose whether this border behaves as normal or optional.");
	borderGroupCtrl_->SetToolTip("Used by autoborder matching rules. `None` disables group matching for this border.");
	if (borderGroupLabel_) {
		borderGroupLabel_->SetToolTip("Used by autoborder matching rules. `None` disables group matching for this border.");
	}
	metadataBox->Add(metadataGrid, 1, wxEXPAND | wxALL, FromDIP(8));
	wxBoxSizer* borderCrudRow = new wxBoxSizer(wxHORIZONTAL);
	createBorderButton_ = new wxButton(scrolled, wxID_ANY, "New Border");
	deleteBorderButton_ = new wxButton(scrolled, wxID_ANY, "Delete Border");
	StyleBorderWorkspaceActionButton(createBorderButton_, "Create a new border set in the current scope.");
	StyleBorderWorkspaceActionButton(deleteBorderButton_, "Delete this border set from materials.db.");
	borderCrudRow->Add(createBorderButton_, 1, wxRIGHT, FromDIP(6));
	borderCrudRow->Add(deleteBorderButton_, 1);
	metadataBox->Add(borderCrudRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

	inlineDetailsPanel_ = new wxPanel(scrolled, wxID_ANY);
	groundEquivalentCtrl_ = new wxSpinCtrl(inlineDetailsPanel_, wxID_ANY);
	groundEquivalentCtrl_->SetRange(0, std::max(100000, static_cast<int>(g_items.getMaxID())));
	groundEquivalentCtrl_->SetMinSize(metadataFieldSize);
	ownerBrushCtrl_ = new wxTextCtrl(inlineDetailsPanel_, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
	ownerBrushCtrl_->SetMinSize(wxSize(FromDIP(210), -1));
	wxStaticBoxSizer* inlineDetailsBox = new wxStaticBoxSizer(wxVERTICAL, inlineDetailsPanel_, "Inline Authoring");
	wxFlexGridSizer* inlineGrid = new wxFlexGridSizer(0, 2, FromDIP(6), FromDIP(8));
	inlineGrid->AddGrowableCol(1, 1);
	const auto addInlineField = [&](const wxString &label, wxWindow* control) {
		wxStaticText* fieldLabel = new wxStaticText(inlineDetailsPanel_, wxID_ANY, label);
		StyleBorderWorkspaceCaption(fieldLabel);
		inlineGrid->Add(fieldLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
		inlineGrid->Add(control, 1, wxEXPAND);
	};
	addInlineField("Center Tile", groundEquivalentCtrl_);
	addInlineField("Owner Brush", ownerBrushCtrl_);
	inlineDetailsBox->Add(inlineGrid, 1, wxEXPAND | wxALL, FromDIP(8));
	inlineDetailsPanel_->SetSizer(inlineDetailsBox);

	globalDetailsPanel_ = new wxPanel(scrolled, wxID_ANY);
	openLinkedBrushButton_ = new wxButton(globalDetailsPanel_, wxID_ANY, "Open Brush");
	StyleBorderWorkspaceActionButton(openLinkedBrushButton_, "Open the selected brush that uses this global border.");
	wxStaticBoxSizer* globalDetailsBox = new wxStaticBoxSizer(wxVERTICAL, globalDetailsPanel_, "Used By");
	usageSearchCtrl_ = new wxTextCtrl(globalDetailsPanel_, wxID_ANY);
	usageSummaryLabel_ = new wxStaticText(globalDetailsPanel_, wxID_ANY, "No usage contexts loaded");
	StyleBorderWorkspaceCaption(usageSummaryLabel_);
	usageListBox_ = new wxListBox(globalDetailsPanel_, wxID_ANY);
	usageListBox_->SetMinSize(wxSize(FromDIP(210), FromDIP(150)));
	usagePreviewItem_ = new ItemButton(globalDetailsPanel_, RENDER_SIZE_32x32, 0);
	usagePreviewItem_->Enable(false);
	usageBrushLabel_ = new wxStaticText(globalDetailsPanel_, wxID_ANY, "Brush: none");
	usageAlignLabel_ = new wxStaticText(globalDetailsPanel_, wxID_ANY, "Align: -");
	usageRoleLabel_ = new wxStaticText(globalDetailsPanel_, wxID_ANY, "Role: -");
	usageCenterLabel_ = new wxStaticText(globalDetailsPanel_, wxID_ANY, "Center: painted");
	StyleBorderWorkspaceStrongValue(usageBrushLabel_);
	StyleBorderWorkspaceStrongValue(usageCenterLabel_);
	addUsageContextButton_ = new wxButton(globalDetailsPanel_, wxID_ANY, "Add Context");
	editUsageContextButton_ = new wxButton(globalDetailsPanel_, wxID_ANY, "Edit Context");
	removeUsageContextButton_ = new wxButton(globalDetailsPanel_, wxID_ANY, "Remove Context");
	StyleBorderWorkspaceActionButton(addUsageContextButton_, "Add a new brush usage context for this global border.");
	StyleBorderWorkspaceActionButton(editUsageContextButton_, "Edit the selected usage context.");
	StyleBorderWorkspaceActionButton(removeUsageContextButton_, "Remove the selected usage context from its owner brush.");
	globalDetailsBox->Add(CreateBorderSectionLabel(globalDetailsPanel_, "Search"), 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
	globalDetailsBox->Add(usageSearchCtrl_, 0, wxEXPAND | wxALL, FromDIP(8));
	globalDetailsBox->Add(usageSummaryLabel_, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));
	globalDetailsBox->Add(usageListBox_, 1, wxEXPAND | wxALL, FromDIP(8));
	wxBoxSizer* usageDetailsRow = new wxBoxSizer(wxHORIZONTAL);
	usageDetailsRow->Add(usagePreviewItem_, 0, wxALIGN_TOP | wxRIGHT, FromDIP(8));
	wxBoxSizer* usageDetailsText = new wxBoxSizer(wxVERTICAL);
	usageDetailsText->Add(usageBrushLabel_, 0, wxBOTTOM, FromDIP(4));
	usageDetailsText->Add(usageAlignLabel_, 0, wxBOTTOM, FromDIP(2));
	usageDetailsText->Add(usageRoleLabel_, 0, wxBOTTOM, FromDIP(2));
	usageDetailsText->Add(usageCenterLabel_, 0);
	usageDetailsRow->Add(usageDetailsText, 1, wxEXPAND);
	globalDetailsBox->Add(usageDetailsRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
	wxGridSizer* usageActionGrid = new wxGridSizer(2, FromDIP(6), FromDIP(6));
	usageActionGrid->Add(addUsageContextButton_, 0, wxEXPAND);
	usageActionGrid->Add(editUsageContextButton_, 0, wxEXPAND);
	usageActionGrid->Add(removeUsageContextButton_, 0, wxEXPAND);
	usageActionGrid->Add(openLinkedBrushButton_, 0, wxEXPAND);
	globalDetailsBox->Add(usageActionGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
	globalDetailsPanel_->SetSizer(globalDetailsBox);

	wxBoxSizer* workspaceSizer = new wxBoxSizer(wxHORIZONTAL);

	wxStaticBoxSizer* gridBox = new wxStaticBoxSizer(wxVERTICAL, scrolled, "Slot Grid");
	wxFlexGridSizer* slotGridSizer = new wxFlexGridSizer(kBorderGridSize, kBorderGridSize, FromDIP(kBorderGridGapDip), FromDIP(kBorderGridGapDip));
	const int gridColumnWidthDip =
		(kBorderGridSize * kBorderGridCellWidthDip) +
		((kBorderGridSize - 1) * kBorderGridGapDip) +
		32;

	for (int row = 0; row < kBorderGridSize; ++row) {
		for (int col = 0; col < kBorderGridSize; ++col) {
			const BorderEdgeSpec* specForCell = FindEdgeSpecForCell(row, col);

			if (IsBorderGridCenterCell(row, col)) {
				wxPanel* centerPanel = new wxPanel(scrolled, wxID_ANY);
				centerPanel->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
				wxBoxSizer* centerSizer = new wxBoxSizer(wxVERTICAL);
				centerGroundSlotPreview_ = new ItemButton(centerPanel, RENDER_SIZE_32x32, 0);
				centerGroundSlotPreview_->Enable(false);
				centerGroundSlotValueLabel_ = new wxStaticText(centerPanel, wxID_ANY, "not set");
				StyleBorderWorkspaceStrongValue(centerGroundSlotValueLabel_);
				centerPanel->SetToolTip("Center Ground");
				centerGroundSlotPreview_->SetToolTip("Center Ground");

				centerSizer->AddStretchSpacer(1);
				centerSizer->Add(centerGroundSlotPreview_, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(2));
				centerSizer->Add(centerGroundSlotValueLabel_, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(2));
				centerSizer->AddStretchSpacer(1);
				centerPanel->SetSizer(centerSizer);
				centerPanel->SetMinSize(wxSize(FromDIP(kBorderGridCellWidthDip), FromDIP(kBorderGridCellHeightDip)));
				slotGridSizer->Add(centerPanel, 0, wxEXPAND);
				continue;
			}

			if (!specForCell) {
				slotGridSizer->Add(CreateSpacerCell(scrolled, kBorderGridCellWidthDip, kBorderGridCellHeightDip), 0, wxEXPAND);
				continue;
			}

			wxPanel* cell = new wxPanel(scrolled, wxID_ANY);
			cell->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
			wxBoxSizer* cellSizer = new wxBoxSizer(wxVERTICAL);
			wxString edge = wxString::FromUTF8(specForCell->edge);
			BorderSlotButton* button = new BorderSlotButton(cell);
			wxStaticText* value = new wxStaticText(cell, wxID_ANY, "empty");
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

			slotButtons_[edge] = button;
			slotValueLabels_[edge] = value;
			slotGridSizer->Add(cell, 0, wxEXPAND);
		}
	}

	wxBoxSizer* gridCenterRow = new wxBoxSizer(wxHORIZONTAL);
	gridCenterRow->AddStretchSpacer(1);
	gridCenterRow->Add(slotGridSizer, 0, wxALL, FromDIP(8));
	gridCenterRow->AddStretchSpacer(1);
	gridBox->Add(gridCenterRow, 0, wxEXPAND);

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
	previewMatrixPanel_ = new BorderPreviewMatrixPanel(scrolled);
	wxBoxSizer* previewCenterRow = new wxBoxSizer(wxHORIZONTAL);
	previewCenterRow->AddStretchSpacer(1);
	previewCenterRow->Add(previewMatrixPanel_, 0, wxALL, FromDIP(8));
	previewCenterRow->AddStretchSpacer(1);

	previewBox->Add(previewCenterRow, 1, wxEXPAND);

	wxBoxSizer* metadataColumn = new wxBoxSizer(wxVERTICAL);
	metadataColumn->Add(metadataBox, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
	metadataColumn->Add(inlineDetailsPanel_, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
	metadataColumn->Add(globalDetailsPanel_, 0, wxEXPAND);
	metadataColumn->SetMinSize(wxSize(FromDIP(250), -1));

	wxBoxSizer* gridColumn = new wxBoxSizer(wxVERTICAL);
	gridBox->SetMinSize(wxSize(FromDIP(gridColumnWidthDip), -1));
	gridColumn->Add(gridBox, 0, wxEXPAND);

	wxBoxSizer* previewColumn = new wxBoxSizer(wxVERTICAL);
	previewBox->SetMinSize(wxSize(FromDIP(220), -1));
	previewColumn->Add(previewBox, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
	previewColumn->Add(editorBox, 0, wxEXPAND);

	workspaceSizer->Add(metadataColumn, 1, wxRIGHT | wxEXPAND, FromDIP(10));
	workspaceSizer->Add(gridColumn, 0, wxRIGHT | wxEXPAND, FromDIP(10));
	workspaceSizer->Add(previewColumn, 0, wxEXPAND);

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
	createBorderButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBorderPanel::OnCreateBorder, this);
	deleteBorderButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBorderPanel::OnDeleteBorder, this);
	selectedItemIdCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBorderPanel::OnSelectedItemIdChanged, this);
	selectedItemIdCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBorderPanel::OnSelectedItemIdSpin, this);
	xmlBorderIdCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBorderPanel::OnMetadataFieldChanged, this);
	xmlBorderIdCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBorderPanel::OnMetadataFieldChanged, this);
	scopeChoice_->Bind(wxEVT_CHOICE, &MaterialsWorkbenchBorderPanel::OnMetadataFieldChanged, this);
	typeCtrl_->Bind(wxEVT_CHOICE, &MaterialsWorkbenchBorderPanel::OnMetadataFieldChanged, this);
	borderGroupCtrl_->Bind(wxEVT_CHOICE, &MaterialsWorkbenchBorderPanel::OnMetadataFieldChanged, this);
	groundEquivalentCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBorderPanel::OnMetadataFieldChanged, this);
	usageSearchCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBorderPanel::OnUsageSearchChanged, this);
	usageListBox_->Bind(wxEVT_LISTBOX, &MaterialsWorkbenchBorderPanel::OnUsageContextChanged, this);
	openLinkedBrushButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBorderPanel::OnOpenLinkedBrush, this);
	addUsageContextButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBorderPanel::OnAddUsageContext, this);
	editUsageContextButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBorderPanel::OnEditUsageContext, this);
	removeUsageContextButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBorderPanel::OnRemoveUsageContext, this);
	groundEquivalentCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBorderPanel::OnMetadataFieldChanged, this);
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
	typeCtrl_->SetSelection(wxNOT_FOUND);
	RebuildBorderGroupChoices(borderGroupCtrl_, 0);
	groundEquivalentCtrl_->SetValue(0);
	ownerBrushCtrl_->SetValue("");
	sourceCtrl_->SetValue("");
	if (usageSearchCtrl_) {
		usageSearchCtrl_->SetValue("");
	}
	if (usageListBox_) {
		usageListBox_->Clear();
	}
	if (usageSummaryLabel_) {
		usageSummaryLabel_->SetLabel("No usage contexts loaded");
	}
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

	for (const auto &entry : slotButtons_) {
		entry.second->SetSprite(0);
		entry.second->SetValue(false);
	}
	for (const auto &entry : slotValueLabels_) {
		entry.second->SetLabel("empty");
	}
	if (previewMatrixPanel_) {
		static_cast<BorderPreviewMatrixPanel*>(previewMatrixPanel_)->SetPreviewState({}, 0, "");
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

	internalUpdate_ = true;
	idCtrl_->SetValue(wxString::Format("%lld", static_cast<long long>(borderSet.id)));
	xmlBorderIdCtrl_->SetValue(borderSet.xmlBorderId);
	scopeChoice_->SetStringSelection(borderSet.borderScope);
	typeCtrl_->SetStringSelection(borderSet.borderType.IsEmpty() ? "normal" : borderSet.borderType);
	if (typeCtrl_->GetSelection() == wxNOT_FOUND) {
		typeCtrl_->SetStringSelection("normal");
	}
	RebuildBorderGroupChoices(borderGroupCtrl_, borderSet.borderGroup);
	groundEquivalentCtrl_->SetValue(borderSet.groundEquivalent);
	ownerBrushCtrl_->SetValue(BuildOwnerBrushDisplayLabel(borderSet.ownerBrushId));
	sourceCtrl_->SetValue(borderSet.sourceFile);
	if (usageSearchCtrl_) {
		usageSearchCtrl_->ChangeValue("");
	}
	PopulateUsageContextList();
	internalUpdate_ = false;
	UpdateWorkspaceHeader();
	UpdateUsageContextControls();
	RefreshScopeSpecificLayout();

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
		"Border: %s | Filled Slots: %zu | Scope: %s | Style: %s",
		BuildBorderSetDisplayLabel(borderSet),
		currentStorage.items.size(),
		borderSet.borderScope,
		borderSet.borderType
	));

	const wxString centerGroundText = ResolveCenterSourceLabel();
	if (borderSet.borderScope == "global") {
		identityLabel_->SetLabel(wxString::Format(
			"Global Border ID %d | Internal ID %lld | Used By: %zu brushes | Center source: %s",
			borderSet.xmlBorderId,
			static_cast<long long>(borderSet.id),
			borderSetUsages_.size(),
			centerGroundText
		));
	} else {
		identityLabel_->SetLabel(wxString::Format(
			"Inline border | Internal ID %lld | Owner Brush: %s | Center Tile: %s",
			static_cast<long long>(borderSet.id),
			BuildOwnerBrushDisplayLabel(borderSet.ownerBrushId),
			centerGroundText
		));
	}
}

BorderSetStorageRecord MaterialsWorkbenchBorderPanel::BuildComparableStorageFromCurrentState() const {
	BorderSetStorageRecord storage = borderSetStorage_;
	storage.borderSet.xmlBorderId = xmlBorderIdCtrl_->GetValue();
	storage.borderSet.borderScope = scopeChoice_->GetSelection() == wxNOT_FOUND ? "" : scopeChoice_->GetStringSelection();
	storage.borderSet.borderType = typeCtrl_->GetSelection() == wxNOT_FOUND ? "" : typeCtrl_->GetStringSelection();
	storage.borderSet.borderGroup =
		borderGroupCtrl_ && borderGroupCtrl_->GetSelection() != wxNOT_FOUND
			? ParseBorderGroupChoiceValue(borderGroupCtrl_->GetStringSelection())
			: 0;
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

void MaterialsWorkbenchBorderPanel::PopulateUsageContextList() {
	if (!usageListBox_) {
		return;
	}

	filteredUsageIndexes_.clear();
	usageListBox_->Clear();
	const wxString query = usageSearchCtrl_ ? usageSearchCtrl_->GetValue().Lower() : "";
	for (size_t i = 0; i < borderSetUsages_.size(); ++i) {
		const BorderSetUsageRecord &usage = borderSetUsages_[i];
		const wxString haystack = (usage.brushName + " " + usage.align + " " + usage.borderRole + " " + usage.targetMode).Lower();
		if (!query.IsEmpty() && !haystack.Contains(query)) {
			continue;
		}
		filteredUsageIndexes_.push_back(static_cast<int>(i));
		usageListBox_->Append(BuildUsageContextLabel(usage));
	}

	if (usageSummaryLabel_) {
		usageSummaryLabel_->SetLabel(wxString::Format(
			"%zu context%s total | %zu visible",
			borderSetUsages_.size(),
			borderSetUsages_.size() == 1 ? "" : "s",
			filteredUsageIndexes_.size()
		));
	}

	if (filteredUsageIndexes_.empty()) {
		selectedUsageIndex_ = wxNOT_FOUND;
		usageListBox_->SetSelection(wxNOT_FOUND);
		RefreshUsageDetails();
		return;
	}

	if (selectedUsageIndex_ == wxNOT_FOUND ||
		std::find(filteredUsageIndexes_.begin(), filteredUsageIndexes_.end(), selectedUsageIndex_) == filteredUsageIndexes_.end()) {
		selectedUsageIndex_ = filteredUsageIndexes_.front();
	}
	for (size_t visibleIndex = 0; visibleIndex < filteredUsageIndexes_.size(); ++visibleIndex) {
		if (filteredUsageIndexes_[visibleIndex] == selectedUsageIndex_) {
			usageListBox_->SetSelection(static_cast<int>(visibleIndex));
			break;
		}
	}
	RefreshUsageDetails();
}

void MaterialsWorkbenchBorderPanel::UpdateUsageContextControls() {
	if (!hasBorderSet_) {
		if (usageSearchCtrl_) {
			usageSearchCtrl_->Enable(false);
		}
		if (usageListBox_) {
			usageListBox_->Enable(false);
		}
		if (openLinkedBrushButton_) {
			openLinkedBrushButton_->Enable(false);
		}
		if (addUsageContextButton_) {
			addUsageContextButton_->Enable(false);
		}
		if (editUsageContextButton_) {
			editUsageContextButton_->Enable(false);
		}
		if (removeUsageContextButton_) {
			removeUsageContextButton_->Enable(false);
		}
		return;
	}

	const bool isGlobal = scopeChoice_->GetStringSelection() == "global";
	const BorderSetUsageRecord* usage = GetSelectedUsageContext();
	if (usageSearchCtrl_) {
		usageSearchCtrl_->Enable(isGlobal);
	}
	if (usageListBox_) {
		usageListBox_->Enable(isGlobal && !filteredUsageIndexes_.empty());
	}
	if (openLinkedBrushButton_) {
		openLinkedBrushButton_->Enable(isGlobal && usage && usage->brushId > 0);
	}
	if (addUsageContextButton_) {
		addUsageContextButton_->Enable(isGlobal);
	}
	if (editUsageContextButton_) {
		editUsageContextButton_->Enable(isGlobal && usage != nullptr);
	}
	if (removeUsageContextButton_) {
		removeUsageContextButton_->Enable(isGlobal && usage != nullptr);
	}
	if (usageListBox_) {
		usageListBox_->SetToolTip(
			!isGlobal
				? "Inline borders edit their Center Tile directly."
				: usage
					? wxString::Format("Selected context from brush \"%s\" drives the center preview.", usage->brushName)
					: "This global border has no visible usage context for the current search."
		);
	}
	RefreshUsageDetails();
}

const BorderSetUsageRecord* MaterialsWorkbenchBorderPanel::GetSelectedUsageContext() const {
	if (selectedUsageIndex_ == wxNOT_FOUND || selectedUsageIndex_ < 0 || selectedUsageIndex_ >= static_cast<int>(borderSetUsages_.size())) {
		return nullptr;
	}
	return &borderSetUsages_[selectedUsageIndex_];
}

bool MaterialsWorkbenchBorderPanel::ReloadBorderSetById(int64_t borderSetId) {
	wxString contextKey;
	int itemIndex = -1;
	if (!controller_.LocateBorderSetNode(borderSetId, contextKey, itemIndex)) {
		ClearWorkspace("The selected border set is no longer available in materials.db.");
		return false;
	}
	return LoadBorderSet(contextKey, itemIndex);
}

bool MaterialsWorkbenchBorderPanel::LoadBrushStorageById(int64_t brushId, BrushStorageRecord &outBrush, wxString &error) const {
	wxString contextKey;
	int itemIndex = -1;
	if (!controller_.LocateBrushNode(brushId, contextKey, itemIndex)) {
		error = wxString::Format("Brush #%lld is not available in the Workbench catalog.", static_cast<long long>(brushId));
		return false;
	}
	return controller_.GetBrushDetails(contextKey, itemIndex, outBrush, error);
}

int MaterialsWorkbenchBorderPanel::FindMatchingGroundBorderIndex(const BrushStorageRecord &brushStorage, const BorderSetUsageRecord &usage) const {
	for (size_t i = 0; i < brushStorage.borders.size(); ++i) {
		const GroundBrushBorderRecord &border = brushStorage.borders[i];
		if (border.borderSetId != borderSetStorage_.borderSet.id) {
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

int MaterialsWorkbenchBorderPanel::SuggestNextBorderId() const {
	return controller_.SuggestNextBorderId();
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

wxString MaterialsWorkbenchBorderPanel::BuildOwnerBrushDisplayLabel(int64_t ownerBrushId) const {
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

void MaterialsWorkbenchBorderPanel::HandleUsageContextChanged() {
	UpdateUsageContextControls();
	RefreshSlotGrid();
	RefreshPreviewGrid();
}

void MaterialsWorkbenchBorderPanel::RefreshScopeSpecificLayout() {
	const bool isInline = hasBorderSet_ && scopeChoice_ && scopeChoice_->GetStringSelection() == "inline";
	const bool isGlobal = hasBorderSet_ && scopeChoice_ && scopeChoice_->GetStringSelection() == "global";
	if (xmlBorderIdLabel_) {
		xmlBorderIdLabel_->SetLabel("Global Border ID");
		xmlBorderIdLabel_->Show(isGlobal);
	}
	if (xmlBorderIdCtrl_) {
		xmlBorderIdCtrl_->Show(isGlobal);
	}
	if (inlineDetailsPanel_) {
		inlineDetailsPanel_->Show(isInline);
	}
	if (globalDetailsPanel_) {
		globalDetailsPanel_->Show(isGlobal);
	}
	Layout();
	if (GetSizer()) {
		GetSizer()->Layout();
	}
}

void MaterialsWorkbenchBorderPanel::RefreshUsageDetails() {
	const BorderSetUsageRecord* usage = GetSelectedUsageContext();
	const int previewItemId = usage ? ResolveUsagePreviewItemId(*usage) : 0;
	if (usagePreviewItem_) {
		usagePreviewItem_->SetSprite(previewItemId);
	}
	if (usageBrushLabel_) {
		usageBrushLabel_->SetLabel(
			usage
				? wxString::Format("Brush: %s (#%lld)", usage->brushName, static_cast<long long>(usage->brushId))
				: "Brush: none"
		);
	}
	if (usageAlignLabel_) {
		usageAlignLabel_->SetLabel(
			usage
				? "Align: " + (usage->align.IsEmpty() ? wxString("outer") : usage->align)
				: "Align: -"
		);
	}
	if (usageRoleLabel_) {
		usageRoleLabel_->SetLabel(
			usage
				? "Role: " + (usage->borderRole.IsEmpty() ? wxString("normal") : usage->borderRole)
				: "Role: -"
		);
	}
	if (usageCenterLabel_) {
		usageCenterLabel_->SetLabel(
			previewItemId > 0
				? wxString::Format("Center: %d", previewItemId)
				: "Center: painted"
		);
	}
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
		error = "Global border sets must use a Global Border ID greater than zero so runtime refresh can target the saved set.";
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
		subtitleLabel_->SetLabel("Select a border set in the navigation tree to edit authoring data, slot ownership, and sprite layout.");
		return;
	}

	const wxString modifiedSuffix = dirty_ ? " [modified]" : "";
	titleLabel_->SetLabel("Editing " + BuildBorderSetDisplayLabel(BuildComparableStorageFromCurrentState().borderSet) + modifiedSuffix);
	const wxString scope = scopeChoice_->GetStringSelection();
	if (dirty_) {
		subtitleLabel_->SetLabel("Unsaved local border edits differ from materials.db. Save to persist them or Revert to discard them before switching sets.");
	} else if (scope == "global") {
		subtitleLabel_->SetLabel("Global border sprites are shared. The selected Used By context drives the effective center preview.");
	} else {
		subtitleLabel_->SetLabel("Inline borders own their Center Tile directly. Adjust authoring fields, slot sprites, and save when ready.");
	}
}

void MaterialsWorkbenchBorderPanel::UpdateActionButtons() {
	if (saveButton_) {
		saveButton_->Enable(hasBorderSet_ && dirty_);
	}
	if (revertButton_) {
		revertButton_->Enable(hasBorderSet_ && dirty_);
	}
	if (createBorderButton_) {
		createBorderButton_->Enable(true);
	}
	if (deleteBorderButton_) {
		deleteBorderButton_->Enable(hasBorderSet_);
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
		centerGroundSlotValueLabel_->SetLabel(FormatCompactCenterGroundText(centerPreviewItemId));
		centerGroundSlotValueLabel_->SetToolTip(centerSourceLabel);
	}

	for (const auto &entry : slotButtons_) {
		const wxString &edge = entry.first;
		const wxString slotLabel = GetBorderEdgeDisplayLabel(edge);
		const int itemId = slotItemIds_.count(edge) > 0 ? slotItemIds_[edge] : 0;
		entry.second->SetSprite(itemId);
		entry.second->SetValue(edge == selectedEdge_);
		slotValueLabels_[edge]->SetLabel(FormatCompactItemIdText(itemId, "empty"));
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
		wxString previewTooltip =
			centerPreviewItemId > 0
				? wxString::Format("Preview composes the runtime scene from the 5x5 matrix using %s as base ground.", centerSourceLabel)
				: wxString::Format("Preview composes the runtime scene from the 5x5 matrix. Center source: %s.", centerSourceLabel);
		if (selectedEdge_.IsEmpty()) {
			previewTooltip += " No slot selected.";
		} else {
			const int selectedItemId = slotItemIds_.count(selectedEdge_) > 0 ? slotItemIds_[selectedEdge_] : 0;
			previewTooltip += wxString::Format(" Selected slot: %s (%s).", GetBorderEdgeDisplayLabel(selectedEdge_), FormatCompactItemIdText(selectedItemId, "empty"));
		}
		previewMatrixPanel_->SetToolTip(previewTooltip);
		static_cast<BorderPreviewMatrixPanel*>(previewMatrixPanel_)->SetPreviewState(slotItemIds_, centerPreviewItemId, selectedEdge_);
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
	selectedItemIdCtrl_->Enable(enabled);
	selectedItemPreview_->Enable(enabled);
	if (ownerBrushCtrl_) {
		ownerBrushCtrl_->Enable(false);
	}
	for (const auto &entry : slotButtons_) {
		entry.second->Enable(enabled);
	}
	UpdateUsageContextControls();
	RefreshScopeSpecificLayout();
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
	RefreshScopeSpecificLayout();
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

	const int visibleSelection = usageListBox_ ? usageListBox_->GetSelection() : wxNOT_FOUND;
	selectedUsageIndex_ =
		(visibleSelection != wxNOT_FOUND &&
		 visibleSelection >= 0 &&
		 visibleSelection < static_cast<int>(filteredUsageIndexes_.size()))
			? filteredUsageIndexes_[visibleSelection]
			: wxNOT_FOUND;
	HandleUsageContextChanged();
	SetStatusMessage("Preview context updated. Global border center now reflects the selected linked brush.");
	event.Skip();
}

void MaterialsWorkbenchBorderPanel::OnUsageSearchChanged(wxCommandEvent &event) {
	if (internalUpdate_) {
		event.Skip();
		return;
	}

	PopulateUsageContextList();
	UpdateUsageContextControls();
	RefreshPreviewGrid();
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

void MaterialsWorkbenchBorderPanel::OnCreateBorder(wxCommandEvent &event) {
	if (hasBorderSet_ && !ResolvePendingChangesBeforeSwitch(this, "a new border")) {
		return;
	}

	wxArrayString scopeChoices;
	scopeChoices.Add("global");
	scopeChoices.Add("inline");
	wxSingleChoiceDialog scopeDialog(this, "Choose the scope for the new border.", "New Border", scopeChoices);
	if (hasBorderSet_) {
		scopeDialog.SetSelection(scopeChoice_->GetStringSelection() == "inline" ? 1 : 0);
	}
	if (scopeDialog.ShowModal() != wxID_OK) {
		return;
	}

	BorderSetStorageRecord newStorage;
	newStorage.borderSet.borderScope = scopeDialog.GetStringSelection();
	newStorage.borderSet.borderType = hasBorderSet_ && typeCtrl_->GetSelection() != wxNOT_FOUND ? typeCtrl_->GetStringSelection() : "normal";
	newStorage.borderSet.borderGroup =
		hasBorderSet_ && borderGroupCtrl_ && borderGroupCtrl_->GetSelection() != wxNOT_FOUND
			? ParseBorderGroupChoiceValue(borderGroupCtrl_->GetStringSelection())
			: 0;
	newStorage.borderSet.sourceFile = "materials.db";
	if (newStorage.borderSet.borderType.IsEmpty()) {
		newStorage.borderSet.borderType = "normal";
	}

	if (newStorage.borderSet.borderScope == "global") {
		newStorage.borderSet.xmlBorderId = SuggestNextBorderId();
	} else {
		FindBrushDialog brushDialog(this, "Choose Owner Brush");
		if (brushDialog.ShowModal() != wxID_OK) {
			return;
		}
		const Brush* brush = brushDialog.getResult();
		if (!brush) {
			SetStatusMessage("Inline border creation needs an owner brush.");
			return;
		}
		newStorage.borderSet.ownerBrushId = static_cast<int64_t>(brush->getID());
	}

	wxString error;
	if (!controller_.SaveBorderSet(newStorage, error)) {
		SetStatusMessage("Failed to create border: " + error);
		return;
	}
	if (!ReloadBorderSetById(newStorage.borderSet.id)) {
		SetStatusMessage("Created the border, but failed to reload it in the workspace.");
		return;
	}

	SetStatusMessage("Created a new border set in materials.db.");
	if (onBorderSetSaved_) {
		onBorderSetSaved_(newStorage.borderSet.id);
	}
}

void MaterialsWorkbenchBorderPanel::OnDeleteBorder(wxCommandEvent &event) {
	if (!hasBorderSet_) {
		SetStatusMessage("Load a border set before deleting it.");
		return;
	}

	const BorderSetRecord border = borderSetStorage_.borderSet;
	const wxString warningText =
		border.borderScope == "global" && !borderSetUsages_.empty()
			? "Deleting this global border will also remove all its Used By contexts from linked brushes.\n\nThis cannot be undone."
			: "This will remove the current border set from materials.db.\n\nThis cannot be undone.";
	if (wxMessageBox(
			warningText,
			"Delete Border",
			wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
			this
		) != wxYES) {
		return;
	}

	wxString error;
	if (border.borderScope == "global") {
		std::vector<int64_t> processedBrushIds;
		for (const BorderSetUsageRecord &usage : borderSetUsages_) {
			if (std::find(processedBrushIds.begin(), processedBrushIds.end(), usage.brushId) != processedBrushIds.end()) {
				continue;
			}
			processedBrushIds.push_back(usage.brushId);

			BrushStorageRecord brushStorage;
			if (!LoadBrushStorageById(usage.brushId, brushStorage, error)) {
				SetStatusMessage("Failed to remove linked contexts before delete: " + error);
				return;
			}
			brushStorage.borders.erase(
				std::remove_if(
					brushStorage.borders.begin(),
					brushStorage.borders.end(),
					[border](const GroundBrushBorderRecord &record) {
						return record.borderSetId == border.id;
					}
				),
				brushStorage.borders.end()
			);
			for (size_t i = 0; i < brushStorage.borders.size(); ++i) {
				brushStorage.borders[i].sortOrder = static_cast<int>(i);
			}
			if (!controller_.SaveGroundBrushBorders(usage.brushId, brushStorage.borders, error)) {
				SetStatusMessage("Failed to remove linked contexts before delete: " + error);
				return;
			}
		}
	}

	if (!controller_.DeleteBorderSet(border.id, error)) {
		SetStatusMessage("Failed to delete border: " + error);
		return;
	}

	ClearWorkspace("Border deleted from materials.db.");
	SetStatusMessage("Deleted border and refreshed the Workbench catalog.");
	if (onBorderSetDeleted_) {
		onBorderSetDeleted_(border.borderScope);
	}
}

void MaterialsWorkbenchBorderPanel::OnAddUsageContext(wxCommandEvent &event) {
	if (!hasBorderSet_ || borderSetStorage_.borderSet.borderScope != "global") {
		SetStatusMessage("Load a global border before adding a context.");
		return;
	}

	GlobalUsageEditData dialogData;
	dialogData.borderRole = "normal";
	dialogData.align = "outer";
	dialogData.targetMode = "all";
	GlobalUsageDialog dialog(this, "Add Used By Context", dialogData);
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
	borderRecord.borderSetId = borderSetStorage_.borderSet.id;
	borderRecord.borderRole = result.borderRole;
	borderRecord.align = result.align;
	borderRecord.targetMode = result.targetMode;
	borderRecord.targetBrushId = result.targetMode == "brush" ? result.targetBrushId : 0;
	borderRecord.targetBrushName = result.targetMode == "brush" ? result.targetBrushName : "";
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
	if (!ReloadBorderSetById(borderSetStorage_.borderSet.id)) {
		SetStatusMessage("Saved the new usage context, but failed to reload this border.");
		return;
	}

	for (size_t i = 0; i < borderSetUsages_.size(); ++i) {
		if (borderSetUsages_[i].brushId == result.ownerBrushId && borderSetUsages_[i].sortOrder == borderRecord.sortOrder) {
			selectedUsageIndex_ = static_cast<int>(i);
			break;
		}
	}
	PopulateUsageContextList();
	HandleUsageContextChanged();
	SetStatusMessage("Added a new Used By context and refreshed the global preview.");
	if (onBorderSetSaved_) {
		onBorderSetSaved_(borderSetStorage_.borderSet.id);
	}
}

void MaterialsWorkbenchBorderPanel::OnEditUsageContext(wxCommandEvent &event) {
	if (!hasBorderSet_ || borderSetStorage_.borderSet.borderScope != "global") {
		SetStatusMessage("Load a global border before editing a context.");
		return;
	}

	const BorderSetUsageRecord* usage = GetSelectedUsageContext();
	if (!usage) {
		SetStatusMessage("Select a Used By context before editing it.");
		return;
	}

	GlobalUsageEditData dialogData;
	dialogData.ownerBrushId = usage->brushId;
	dialogData.ownerBrushName = usage->brushName;
	dialogData.borderRole = usage->borderRole.IsEmpty() ? "normal" : usage->borderRole;
	dialogData.align = usage->align.IsEmpty() ? "outer" : usage->align;
	dialogData.targetMode = usage->targetMode.IsEmpty() ? "all" : usage->targetMode;
	dialogData.targetBrushId = usage->targetBrushId;
	dialogData.targetBrushName = usage->targetBrushName;
	dialogData.superBorder = usage->superBorder;

	GlobalUsageDialog dialog(this, "Edit Used By Context", dialogData);
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

	GroundBrushBorderRecord updatedRecord = sourceBrushStorage.borders[existingIndex];
	updatedRecord.borderRole = result.borderRole;
	updatedRecord.align = result.align;
	updatedRecord.targetMode = result.targetMode;
	updatedRecord.targetBrushId = result.targetMode == "brush" ? result.targetBrushId : 0;
	updatedRecord.targetBrushName = result.targetMode == "brush" ? result.targetBrushName : "";

	if (result.ownerBrushId == usage->brushId) {
		sourceBrushStorage.borders[existingIndex] = updatedRecord;
		if (!controller_.SaveGroundBrushBorders(usage->brushId, sourceBrushStorage.borders, error)) {
			SetStatusMessage("Failed to save the edited usage context: " + error);
			return;
		}
	} else {
		sourceBrushStorage.borders.erase(sourceBrushStorage.borders.begin() + existingIndex);
		for (size_t i = 0; i < sourceBrushStorage.borders.size(); ++i) {
			sourceBrushStorage.borders[i].sortOrder = static_cast<int>(i);
		}
		if (!controller_.SaveGroundBrushBorders(usage->brushId, sourceBrushStorage.borders, error)) {
			SetStatusMessage("Failed to update the previous owner brush: " + error);
			return;
		}

		BrushStorageRecord targetBrushStorage;
		if (!LoadBrushStorageById(result.ownerBrushId, targetBrushStorage, error)) {
			SetStatusMessage("Failed to load the new owner brush: " + error);
			return;
		}
		updatedRecord.sortOrder = static_cast<int>(targetBrushStorage.borders.size());
		targetBrushStorage.borders.push_back(updatedRecord);
		if (!controller_.SaveGroundBrushBorders(result.ownerBrushId, targetBrushStorage.borders, error)) {
			SetStatusMessage("Failed to save the context on the new owner brush: " + error);
			return;
		}
	}

	if (!controller_.ReloadCatalog()) {
		SetStatusMessage("Saved the edited usage context, but failed to reload the catalog.");
		return;
	}
	if (!ReloadBorderSetById(borderSetStorage_.borderSet.id)) {
		SetStatusMessage("Saved the edited usage context, but failed to reload this border.");
		return;
	}

	for (size_t i = 0; i < borderSetUsages_.size(); ++i) {
		if (borderSetUsages_[i].brushId == result.ownerBrushId &&
			borderSetUsages_[i].borderRole == updatedRecord.borderRole &&
			borderSetUsages_[i].align == updatedRecord.align &&
			borderSetUsages_[i].targetMode == updatedRecord.targetMode &&
			borderSetUsages_[i].targetBrushName == updatedRecord.targetBrushName) {
			selectedUsageIndex_ = static_cast<int>(i);
			break;
		}
	}
	PopulateUsageContextList();
	HandleUsageContextChanged();
	SetStatusMessage("Updated the Used By context and refreshed the global preview.");
	if (onBorderSetSaved_) {
		onBorderSetSaved_(borderSetStorage_.borderSet.id);
	}
}

void MaterialsWorkbenchBorderPanel::OnRemoveUsageContext(wxCommandEvent &event) {
	if (!hasBorderSet_ || borderSetStorage_.borderSet.borderScope != "global") {
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
			this
		) != wxYES) {
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
	if (!ReloadBorderSetById(borderSetStorage_.borderSet.id)) {
		SetStatusMessage("Removed the usage context, but failed to reload this border.");
		return;
	}

	selectedUsageIndex_ = std::min<int>(selectedUsageIndex_, static_cast<int>(borderSetUsages_.size()) - 1);
	PopulateUsageContextList();
	HandleUsageContextChanged();
	SetStatusMessage("Removed the Used By context and refreshed the global preview.");
	if (onBorderSetSaved_) {
		onBorderSetSaved_(borderSetStorage_.borderSet.id);
	}
}
