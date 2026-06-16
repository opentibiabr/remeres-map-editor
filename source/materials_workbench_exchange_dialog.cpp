#include "main.h"

#include "materials_workbench_exchange_dialog.h"

#include <algorithm>
#include <functional>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/checklst.h>
#include <wx/choice.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/progdlg.h>
#include <wx/srchctrl.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include "brush_database.h"
#include "materials_workbench_controller.h"

namespace {
	wxString BuildBrushRowLabel(const BrushRecord &brush) {
		return wxString::Format("%s: %s", brush.type, brush.name);
	}

	wxString LowerCopy(wxString value) {
		value.MakeLower();
		return value;
	}

	bool VectorContains(const std::vector<int> &values, int value) {
		return std::find(values.begin(), values.end(), value) != values.end();
	}

	bool VectorContains(const std::vector<int64_t> &values, int64_t value) {
		return std::find(values.begin(), values.end(), value) != values.end();
	}

	bool VectorContains(const std::vector<wxString> &values, const wxString &value) {
		return std::find_if(values.begin(), values.end(), [&](const wxString &row) { return row.IsSameAs(value, false); }) != values.end();
	}

	void RemoveValue(std::vector<int> &values, int value) {
		values.erase(std::remove(values.begin(), values.end(), value), values.end());
	}

	void RemoveValue(std::vector<int64_t> &values, int64_t value) {
		values.erase(std::remove(values.begin(), values.end(), value), values.end());
	}

	void RemoveValue(std::vector<wxString> &values, const wxString &value) {
		values.erase(std::remove_if(values.begin(), values.end(), [&](const wxString &row) { return row.IsSameAs(value, false); }), values.end());
	}

	wxString JsonToWxStringLocal(const nlohmann::json &v) {
		const std::string value = v.get<std::string>();
		return wxString::FromUTF8(value.c_str());
	}

	unsigned int CountCheckedItems(wxCheckListBox* list) {
		wxArrayInt checked;
		return list->GetCheckedItems(checked);
	}
} // namespace

MaterialsWorkbenchExportDialog::MaterialsWorkbenchExportDialog(wxWindow* parent, MaterialsWorkbenchController &controller) :
	wxDialog(parent, wxID_ANY, "Export Materials", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	controller_(controller) {
	wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);

	includeDepsCtrl_ = new wxCheckBox(this, wxID_ANY, "Include dependencies");
	includeDepsCtrl_->SetValue(true);
	selection_.includeDependencies = true;

	notebook_ = new wxNotebook(this, wxID_ANY);

	wxPanel* borderPage = new wxPanel(notebook_, wxID_ANY);
	borderFilterCtrl_ = new wxSearchCtrl(borderPage, wxID_ANY);
	borderFilterCtrl_->ShowSearchButton(false);
	borderFilterCtrl_->ShowCancelButton(true);
	borderFilterCtrl_->SetDescriptiveText("Filter borders");
	borderList_ = new wxCheckListBox(borderPage, wxID_ANY);
	wxButton* borderSelectAll = new wxButton(borderPage, wxID_ANY, "Select shown");
	wxButton* borderClearAll = new wxButton(borderPage, wxID_ANY, "Clear shown");
	wxBoxSizer* borderSizer = new wxBoxSizer(wxVERTICAL);
	borderSizer->Add(borderFilterCtrl_, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
	borderSizer->Add(borderList_, 1, wxEXPAND);
	wxBoxSizer* borderActions = new wxBoxSizer(wxHORIZONTAL);
	borderActions->Add(borderSelectAll, 0, wxRIGHT, FromDIP(6));
	borderActions->Add(borderClearAll, 0);
	borderSizer->Add(borderActions, 0, wxTOP, FromDIP(8));
	borderPage->SetSizer(borderSizer);

	wxPanel* brushPage = new wxPanel(notebook_, wxID_ANY);
	brushFilterCtrl_ = new wxSearchCtrl(brushPage, wxID_ANY);
	brushFilterCtrl_->ShowSearchButton(false);
	brushFilterCtrl_->ShowCancelButton(true);
	brushFilterCtrl_->SetDescriptiveText("Filter brushes");
	brushTypeChoiceCtrl_ = new wxChoice(brushPage, wxID_ANY);
	brushList_ = new wxCheckListBox(brushPage, wxID_ANY);
	wxButton* brushSelectAll = new wxButton(brushPage, wxID_ANY, "Select shown");
	wxButton* brushClearAll = new wxButton(brushPage, wxID_ANY, "Clear shown");
	wxBoxSizer* brushSizer = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* brushFilters = new wxBoxSizer(wxHORIZONTAL);
	brushFilters->Add(brushTypeChoiceCtrl_, 0, wxRIGHT, FromDIP(8));
	brushFilters->Add(brushFilterCtrl_, 1, wxEXPAND);
	brushSizer->Add(brushFilters, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
	brushSizer->Add(brushList_, 1, wxEXPAND);
	wxBoxSizer* brushActions = new wxBoxSizer(wxHORIZONTAL);
	brushActions->Add(brushSelectAll, 0, wxRIGHT, FromDIP(6));
	brushActions->Add(brushClearAll, 0);
	brushSizer->Add(brushActions, 0, wxTOP, FromDIP(8));
	brushPage->SetSizer(brushSizer);

	wxPanel* groupPage = new wxPanel(notebook_, wxID_ANY);
	paletteGroupFilterCtrl_ = new wxSearchCtrl(groupPage, wxID_ANY);
	paletteGroupFilterCtrl_->ShowSearchButton(false);
	paletteGroupFilterCtrl_->ShowCancelButton(true);
	paletteGroupFilterCtrl_->SetDescriptiveText("Filter palette groups");
	paletteGroupList_ = new wxCheckListBox(groupPage, wxID_ANY);
	wxButton* groupSelectAll = new wxButton(groupPage, wxID_ANY, "Select shown");
	wxButton* groupClearAll = new wxButton(groupPage, wxID_ANY, "Clear shown");
	wxBoxSizer* groupSizer = new wxBoxSizer(wxVERTICAL);
	groupSizer->Add(paletteGroupFilterCtrl_, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
	groupSizer->Add(paletteGroupList_, 1, wxEXPAND);
	wxBoxSizer* groupActions = new wxBoxSizer(wxHORIZONTAL);
	groupActions->Add(groupSelectAll, 0, wxRIGHT, FromDIP(6));
	groupActions->Add(groupClearAll, 0);
	groupSizer->Add(groupActions, 0, wxTOP, FromDIP(8));
	groupPage->SetSizer(groupSizer);

	wxPanel* palettePage = new wxPanel(notebook_, wxID_ANY);
	paletteFilterCtrl_ = new wxSearchCtrl(palettePage, wxID_ANY);
	paletteFilterCtrl_->ShowSearchButton(false);
	paletteFilterCtrl_->ShowCancelButton(true);
	paletteFilterCtrl_->SetDescriptiveText("Filter palettes");
	paletteList_ = new wxCheckListBox(palettePage, wxID_ANY);
	wxButton* paletteSelectAll = new wxButton(palettePage, wxID_ANY, "Select shown");
	wxButton* paletteClearAll = new wxButton(palettePage, wxID_ANY, "Clear shown");
	wxBoxSizer* paletteSizer = new wxBoxSizer(wxVERTICAL);
	paletteSizer->Add(paletteFilterCtrl_, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
	paletteSizer->Add(paletteList_, 1, wxEXPAND);
	wxBoxSizer* paletteActions = new wxBoxSizer(wxHORIZONTAL);
	paletteActions->Add(paletteSelectAll, 0, wxRIGHT, FromDIP(6));
	paletteActions->Add(paletteClearAll, 0);
	paletteSizer->Add(paletteActions, 0, wxTOP, FromDIP(8));
	palettePage->SetSizer(paletteSizer);

	notebook_->AddPage(borderPage, "Borders");
	notebook_->AddPage(brushPage, "Brushes");
	notebook_->AddPage(groupPage, "Palette Groups");
	notebook_->AddPage(palettePage, "Palettes");

	summaryLabel_ = new wxStaticText(this, wxID_ANY, "");

	wxStdDialogButtonSizer* buttons = new wxStdDialogButtonSizer();
	okButton_ = new wxButton(this, wxID_OK, "Export");
	buttons->AddButton(okButton_);
	buttons->AddButton(new wxButton(this, wxID_CANCEL));
	buttons->Realize();

	rootSizer->Add(includeDepsCtrl_, 0, wxEXPAND | wxALL, FromDIP(12));
	rootSizer->Add(notebook_, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(12));
	rootSizer->Add(summaryLabel_, 0, wxEXPAND | wxALL, FromDIP(12));
	rootSizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

	SetSizerAndFit(rootSizer);
	SetMinSize(wxSize(FromDIP(980), FromDIP(520)));

	RebuildData();
	RebuildBorderList();
	RebuildBrushList();
	RebuildPaletteGroupList();
	RebuildPaletteList();
	UpdateSummary();
	UpdateOkState();

	includeDepsCtrl_->Bind(wxEVT_CHECKBOX, &MaterialsWorkbenchExportDialog::OnToggleIncludeDependencies, this);

	borderFilterCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { RebuildBorderList(); });
	borderFilterCtrl_->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN, [this](wxCommandEvent &) { borderFilterCtrl_->ChangeValue(""); RebuildBorderList(); });
	borderList_->Bind(wxEVT_CHECKLISTBOX, [this](wxCommandEvent &event) { OnListToggled(borderList_, event.GetInt()); });
	borderSelectAll->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { OnSelectAllShown(borderList_); });
	borderClearAll->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { OnClearAllShown(borderList_); });

	brushFilterCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { RebuildBrushList(); });
	brushFilterCtrl_->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN, [this](wxCommandEvent &) { brushFilterCtrl_->ChangeValue(""); RebuildBrushList(); });
	brushTypeChoiceCtrl_->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) { RebuildBrushList(); });
	brushList_->Bind(wxEVT_CHECKLISTBOX, [this](wxCommandEvent &event) { OnListToggled(brushList_, event.GetInt()); });
	brushSelectAll->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { OnSelectAllShown(brushList_); });
	brushClearAll->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { OnClearAllShown(brushList_); });

	paletteGroupFilterCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { RebuildPaletteGroupList(); });
	paletteGroupFilterCtrl_->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN, [this](wxCommandEvent &) { paletteGroupFilterCtrl_->ChangeValue(""); RebuildPaletteGroupList(); });
	paletteGroupList_->Bind(wxEVT_CHECKLISTBOX, [this](wxCommandEvent &event) { OnListToggled(paletteGroupList_, event.GetInt()); });
	groupSelectAll->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { OnSelectAllShown(paletteGroupList_); });
	groupClearAll->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { OnClearAllShown(paletteGroupList_); });

	paletteFilterCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { RebuildPaletteList(); });
	paletteFilterCtrl_->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN, [this](wxCommandEvent &) { paletteFilterCtrl_->ChangeValue(""); RebuildPaletteList(); });
	paletteList_->Bind(wxEVT_CHECKLISTBOX, [this](wxCommandEvent &event) { OnListToggled(paletteList_, event.GetInt()); });
	paletteSelectAll->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { OnSelectAllShown(paletteList_); });
	paletteClearAll->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { OnClearAllShown(paletteList_); });
}

void MaterialsWorkbenchExportDialog::RebuildData() {
	allBorders_.clear();
	for (const BorderSetRecord &border : controller_.GetGlobalBorderSets()) {
		if (border.xmlBorderId <= 0) {
			continue;
		}
		BorderRow row;
		row.xmlBorderId = border.xmlBorderId;
		row.label = wxString::Format("Border %d (%s)", border.xmlBorderId, border.borderType.IsEmpty() ? "normal" : border.borderType);
		allBorders_.push_back(row);
	}
	std::sort(allBorders_.begin(), allBorders_.end(), [](const BorderRow &a, const BorderRow &b) { return a.xmlBorderId < b.xmlBorderId; });

	allBrushes_.clear();
	brushTypeChoices_.clear();
	brushTypeChoices_.push_back("All");
	for (const MaterialsWorkbenchBrushGroup &group : controller_.GetBrushGroups()) {
		if (!group.brushType.IsEmpty() && !VectorContains(brushTypeChoices_, group.brushType)) {
			brushTypeChoices_.push_back(group.brushType);
		}
		for (const BrushRecord &brush : group.brushes) {
			BrushRow row;
			row.brushId = brush.id;
			row.brushType = brush.type;
			row.brushName = brush.name;
			row.label = BuildBrushRowLabel(brush);
			allBrushes_.push_back(row);
		}
	}
	for (const BrushRecord &brush : controller_.GetWallBrushes()) {
		if (!VectorContains(brushTypeChoices_, brush.type)) {
			brushTypeChoices_.push_back(brush.type);
		}
		BrushRow row;
		row.brushId = brush.id;
		row.brushType = brush.type;
		row.brushName = brush.name;
		row.label = BuildBrushRowLabel(brush);
		allBrushes_.push_back(row);
	}
	std::sort(brushTypeChoices_.begin() + 1, brushTypeChoices_.end(), [](const wxString &a, const wxString &b) { return LowerCopy(a) < LowerCopy(b); });
	std::sort(allBrushes_.begin(), allBrushes_.end(), [](const BrushRow &a, const BrushRow &b) {
		const wxString aType = LowerCopy(a.brushType);
		const wxString bType = LowerCopy(b.brushType);
		if (aType != bType) {
			return aType < bType;
		}
		return LowerCopy(a.brushName) < LowerCopy(b.brushName);
	});

	allPaletteGroups_.clear();
	for (const PaletteGroupRecord &group : controller_.GetPaletteGroups()) {
		allPaletteGroups_.push_back(group.name);
	}
	std::sort(allPaletteGroups_.begin(), allPaletteGroups_.end(), [](const wxString &a, const wxString &b) { return LowerCopy(a) < LowerCopy(b); });

	allPalettes_.clear();
	for (const TilesetStorageRecord &tileset : controller_.GetTilesets()) {
		allPalettes_.push_back(tileset.name);
	}
	std::sort(allPalettes_.begin(), allPalettes_.end(), [](const wxString &a, const wxString &b) { return LowerCopy(a) < LowerCopy(b); });

	brushTypeChoiceCtrl_->Clear();
	for (const wxString &choice : brushTypeChoices_) {
		brushTypeChoiceCtrl_->Append(choice);
	}
	brushTypeChoiceCtrl_->SetSelection(0);
}

void MaterialsWorkbenchExportDialog::RebuildBorderList() {
	const wxString query = LowerCopy(borderFilterCtrl_->GetValue());
	borderList_->Freeze();
	borderList_->Clear();
	for (const BorderRow &row : allBorders_) {
		if (!query.IsEmpty() && !LowerCopy(row.label).Contains(query)) {
			continue;
		}
		const size_t idx = borderList_->Append(row.label);
		borderList_->SetClientData(idx, reinterpret_cast<void*>(static_cast<intptr_t>(row.xmlBorderId)));
		borderList_->Check(idx, VectorContains(selection_.globalBorderXmlIds, row.xmlBorderId));
	}
	borderList_->Thaw();
	UpdateSummary();
	UpdateOkState();
}

void MaterialsWorkbenchExportDialog::RebuildBrushList() {
	const wxString query = LowerCopy(brushFilterCtrl_->GetValue());
	const wxString typeFilter = brushTypeChoiceCtrl_->GetSelection() <= 0 ? "" : brushTypeChoiceCtrl_->GetStringSelection();
	const wxString typeFilterLower = LowerCopy(typeFilter);

	brushList_->Freeze();
	brushList_->Clear();
	for (const BrushRow &row : allBrushes_) {
		if (!typeFilterLower.IsEmpty() && !LowerCopy(row.brushType).IsSameAs(typeFilterLower, false)) {
			continue;
		}
		if (!query.IsEmpty() && !LowerCopy(row.label).Contains(query)) {
			continue;
		}
		const size_t idx = brushList_->Append(row.label);
		brushList_->SetClientData(idx, reinterpret_cast<void*>(static_cast<intptr_t>(row.brushId)));
		brushList_->Check(idx, VectorContains(selection_.brushIds, row.brushId));
	}
	brushList_->Thaw();
	UpdateSummary();
	UpdateOkState();
}

void MaterialsWorkbenchExportDialog::RebuildPaletteGroupList() {
	const wxString query = LowerCopy(paletteGroupFilterCtrl_->GetValue());
	paletteGroupList_->Freeze();
	paletteGroupList_->Clear();
	for (const wxString &name : allPaletteGroups_) {
		if (!query.IsEmpty() && !LowerCopy(name).Contains(query)) {
			continue;
		}
		const size_t idx = paletteGroupList_->Append(name);
		paletteGroupList_->Check(idx, VectorContains(selection_.paletteGroupNames, name));
	}
	paletteGroupList_->Thaw();
	UpdateSummary();
	UpdateOkState();
}

void MaterialsWorkbenchExportDialog::RebuildPaletteList() {
	const wxString query = LowerCopy(paletteFilterCtrl_->GetValue());
	paletteList_->Freeze();
	paletteList_->Clear();
	for (const wxString &name : allPalettes_) {
		if (!query.IsEmpty() && !LowerCopy(name).Contains(query)) {
			continue;
		}
		const size_t idx = paletteList_->Append(name);
		paletteList_->Check(idx, VectorContains(selection_.paletteNames, name));
	}
	paletteList_->Thaw();
	UpdateSummary();
	UpdateOkState();
}

void MaterialsWorkbenchExportDialog::OnToggleIncludeDependencies(wxCommandEvent &) {
	selection_.includeDependencies = includeDepsCtrl_->GetValue();
	UpdateSummary();
}

void MaterialsWorkbenchExportDialog::UpdateSummary() {
	const std::set<int> selectedBorders(selection_.globalBorderXmlIds.begin(), selection_.globalBorderXmlIds.end());
	const std::set<int64_t> selectedBrushes(selection_.brushIds.begin(), selection_.brushIds.end());
	const std::set<wxString> selectedGroups(selection_.paletteGroupNames.begin(), selection_.paletteGroupNames.end());
	const std::set<wxString> selectedPalettes(selection_.paletteNames.begin(), selection_.paletteNames.end());

	const int borders = static_cast<int>(selectedBorders.size());
	const int brushes = static_cast<int>(selectedBrushes.size());
	const int groups = static_cast<int>(selectedGroups.size());
	const int palettes = static_cast<int>(selectedPalettes.size());

	wxString label = wxString::Format("Selected: %d borders, %d brushes, %d palette groups, %d palettes.", borders, brushes, groups, palettes);

	if (selection_.includeDependencies) {
		wxString error;
		MaterialsWorkbenchResolvedExportSelection resolved;
		if (ResolveMaterialsWorkbenchExportSelection(controller_, selection_, resolved, error) && error.IsEmpty()) {
			const std::set<int> exportingBorders(resolved.globalBorderXmlIds.begin(), resolved.globalBorderXmlIds.end());
			const std::set<int64_t> exportingBrushes(resolved.brushIds.begin(), resolved.brushIds.end());
			const std::set<wxString> exportingGroups(resolved.paletteGroupNames.begin(), resolved.paletteGroupNames.end());
			const std::set<wxString> exportingPalettes(resolved.paletteNames.begin(), resolved.paletteNames.end());

			const int depBorders = static_cast<int>(exportingBorders.size() - selectedBorders.size());
			const int depBrushes = static_cast<int>(exportingBrushes.size() - selectedBrushes.size());
			const int depGroups = static_cast<int>(exportingGroups.size() - selectedGroups.size());
			const int depPalettes = static_cast<int>(exportingPalettes.size() - selectedPalettes.size());

			label += wxString::Format(
				" Exporting: %d borders (+%d), %d brushes (+%d), %d palette groups (+%d), %d palettes (+%d).",
				static_cast<int>(exportingBorders.size()), depBorders,
				static_cast<int>(exportingBrushes.size()), depBrushes,
				static_cast<int>(exportingGroups.size()), depGroups,
				static_cast<int>(exportingPalettes.size()), depPalettes
			);
		}
	}

	summaryLabel_->SetLabel(label);
}

void MaterialsWorkbenchExportDialog::UpdateOkState() {
	const bool hasAnything =
		!selection_.globalBorderXmlIds.empty() ||
		!selection_.brushIds.empty() ||
		!selection_.paletteGroupNames.empty() ||
		!selection_.paletteNames.empty();
	okButton_->Enable(hasAnything);
}

void MaterialsWorkbenchExportDialog::OnSelectAllShown(wxCheckListBox* list) {
	for (size_t i = 0; i < list->GetCount(); ++i) {
		if (!list->IsChecked(i)) {
			list->Check(i, true);
			OnListToggled(list, static_cast<int>(i));
		}
	}
}

void MaterialsWorkbenchExportDialog::OnClearAllShown(wxCheckListBox* list) {
	for (size_t i = 0; i < list->GetCount(); ++i) {
		if (list->IsChecked(i)) {
			list->Check(i, false);
			OnListToggled(list, static_cast<int>(i));
		}
	}
}

void MaterialsWorkbenchExportDialog::OnListToggled(wxCheckListBox* list, int index) {
	if (!list || index < 0 || static_cast<size_t>(index) >= list->GetCount()) {
		return;
	}

	const bool checked = list->IsChecked(static_cast<unsigned int>(index));
	if (list == borderList_) {
		const int xmlBorderId = static_cast<int>(reinterpret_cast<intptr_t>(borderList_->GetClientData(static_cast<unsigned int>(index))));
		if (xmlBorderId <= 0) {
			return;
		}
		if (checked) {
			if (!VectorContains(selection_.globalBorderXmlIds, xmlBorderId)) {
				selection_.globalBorderXmlIds.push_back(xmlBorderId);
			}
		} else {
			RemoveValue(selection_.globalBorderXmlIds, xmlBorderId);
		}
	} else if (list == brushList_) {
		const int64_t brushId = static_cast<int64_t>(reinterpret_cast<intptr_t>(brushList_->GetClientData(static_cast<unsigned int>(index))));
		if (brushId <= 0) {
			return;
		}
		if (checked) {
			if (!VectorContains(selection_.brushIds, brushId)) {
				selection_.brushIds.push_back(brushId);
			}
		} else {
			RemoveValue(selection_.brushIds, brushId);
		}
	} else if (list == paletteGroupList_) {
		const wxString name = paletteGroupList_->GetString(static_cast<unsigned int>(index));
		if (checked) {
			if (!VectorContains(selection_.paletteGroupNames, name)) {
				selection_.paletteGroupNames.push_back(name);
			}
		} else {
			RemoveValue(selection_.paletteGroupNames, name);
		}
	} else if (list == paletteList_) {
		const wxString name = paletteList_->GetString(static_cast<unsigned int>(index));
		if (checked) {
			if (!VectorContains(selection_.paletteNames, name)) {
				selection_.paletteNames.push_back(name);
			}
		} else {
			RemoveValue(selection_.paletteNames, name);
		}
	}

	UpdateSummary();
	UpdateOkState();
}

MaterialsWorkbenchImportDialog::MaterialsWorkbenchImportDialog(wxWindow* parent, const nlohmann::json &root, MaterialsWorkbenchController &controller) :
	wxDialog(parent, wxID_ANY, "Import Preview", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	root_(root),
	controller_(controller) {
	wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* optionsSizer = new wxBoxSizer(wxHORIZONTAL);
	optionsSizer->Add(new wxStaticText(this, wxID_ANY, "On conflict:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
	conflictChoice_ = new wxChoice(this, wxID_ANY);
	conflictChoice_->Append("Update existing");
	conflictChoice_->Append("Skip existing");
	conflictChoice_->Append("Rename with suffix");
	conflictChoice_->SetSelection(0);
	optionsSizer->Add(conflictChoice_, 0, wxALIGN_CENTER_VERTICAL);
	optionsSizer->AddStretchSpacer(1);

	planList_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxBORDER_THEME);
	planList_->InsertColumn(0, "Kind");
	planList_->InsertColumn(1, "Key");
	planList_->InsertColumn(2, "Action");
	planList_->SetColumnWidth(0, FromDIP(120));
	planList_->SetColumnWidth(1, FromDIP(380));
	planList_->SetColumnWidth(2, FromDIP(120));

	summaryLabel_ = new wxStaticText(this, wxID_ANY, "");

	wxStdDialogButtonSizer* buttons = new wxStdDialogButtonSizer();
	okButton_ = new wxButton(this, wxID_OK, "Import");
	buttons->AddButton(okButton_);
	buttons->AddButton(new wxButton(this, wxID_CANCEL));
	buttons->Realize();

	rootSizer->Add(optionsSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
	rootSizer->Add(planList_, 1, wxEXPAND | wxALL, FromDIP(12));
	rootSizer->Add(summaryLabel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
	rootSizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

	SetSizerAndFit(rootSizer);
	SetMinSize(wxSize(FromDIP(720), FromDIP(420)));

	okButton_->Enable(false);
	summaryLabel_->SetLabel("Building preview...");

	conflictChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) {
		BuildPlan(nullptr, 0, 0);
	});
}

void MaterialsWorkbenchImportDialog::BuildPlanWithProgress(wxProgressDialog* progress, int progressStart, int progressSpan) {
	BuildPlan(progress, progressStart, progressSpan);
	UpdateSummary();
}

void MaterialsWorkbenchImportDialog::BuildPlan(wxProgressDialog* progress, int progressStart, int progressSpan) {
	planList_->Freeze();
	planList_->DeleteAllItems();
	int creates = 0;
	int updates = 0;
	int skips = 0;

	options_.onConflict = MaterialsWorkbenchImportConflictStrategy::UpdateExisting;
	if (conflictChoice_) {
		if (conflictChoice_->GetSelection() == 1) {
			options_.onConflict = MaterialsWorkbenchImportConflictStrategy::SkipExisting;
		} else if (conflictChoice_->GetSelection() == 2) {
			options_.onConflict = MaterialsWorkbenchImportConflictStrategy::RenameWithSuffix;
		}
	}

	if (!root_.is_object() || !root_.contains("entities") || !root_["entities"].is_array()) {
		okButton_->Enable(false);
		summaryLabel_->SetLabel("Plan: 0 create, 0 update, 0 skip.");
		planList_->Thaw();
		return;
	}

	const int total = static_cast<int>(root_["entities"].size());
	int processed = 0;
	std::unordered_map<wxString, wxString> renamedPaletteGroups;
	std::unordered_map<wxString, wxString> renamedPalettes;
	std::unordered_set<wxString> reservedGroupNames;
	std::unordered_set<wxString> reservedPaletteNames;

	auto normalizeName = [](const wxString &name) -> wxString {
		wxString value = name;
		value.MakeLower();
		return value;
	};

	auto makeUniqueName = [&](const wxString &base, const std::function<bool(const wxString &)> &exists, std::unordered_set<wxString> &reserved) -> wxString {
		const wxString baseKey = normalizeName(base);
		if (!exists(base) && reserved.find(baseKey) == reserved.end()) {
			reserved.insert(baseKey);
			return base;
		}

		for (int attempt = 1; attempt < 1000; ++attempt) {
			wxString candidate = base;
			if (attempt == 1) {
				candidate += " (imported)";
			} else {
				candidate += wxString::Format(" (imported %d)", attempt);
			}
			const wxString candidateKey = normalizeName(candidate);
			if (!exists(candidate) && reserved.find(candidateKey) == reserved.end()) {
				reserved.insert(candidateKey);
				return candidate;
			}
		}

		reserved.insert(baseKey);
		return base;
	};
	for (const nlohmann::json &entity : root_["entities"]) {
		if (!entity.is_object() || !entity.contains("kind") || !entity["kind"].is_string()) {
			continue;
		}
		const std::string kind = entity["kind"].get<std::string>();
		wxString keyLabel = "";
		wxString actionDetail = "";
		bool exists = false;

		if (kind == "border_set") {
			if (entity.contains("borderSet") && entity["borderSet"].is_object() && entity["borderSet"].contains("xmlBorderId") && entity["borderSet"]["xmlBorderId"].is_number_integer()) {
				const int xmlBorderId = entity["borderSet"]["xmlBorderId"].get<int>();
				keyLabel = wxString::Format("Border %d", xmlBorderId);
				BorderSetRecord existing;
				exists = g_brush_database.findBorderSetByXmlBorderId(xmlBorderId, existing) && existing.id > 0;
			}
		} else if (kind == "brush") {
			if (entity.contains("brush") && entity["brush"].is_object() && entity["brush"].contains("type") && entity["brush"].contains("name") && entity["brush"]["type"].is_string() && entity["brush"]["name"].is_string()) {
				const wxString type = JsonToWxStringLocal(entity["brush"]["type"]);
				const wxString name = JsonToWxStringLocal(entity["brush"]["name"]);
				keyLabel = wxString::Format("%s: %s", type, name);
				BrushRecord existing;
				exists = g_brush_database.findBrushByNameAndType(name, type, existing) && existing.id > 0;
			}
		} else if (kind == "palette_group") {
			if (entity.contains("group") && entity["group"].is_object() && entity["group"].contains("name") && entity["group"]["name"].is_string()) {
				const wxString name = JsonToWxStringLocal(entity["group"]["name"]);
				keyLabel = name;
				exists = controller_.HasPaletteGroupNamed(name);
				if (exists && options_.onConflict == MaterialsWorkbenchImportConflictStrategy::RenameWithSuffix) {
					auto it = renamedPaletteGroups.find(name);
					if (it == renamedPaletteGroups.end()) {
						const wxString newName = makeUniqueName(name, [&](const wxString &candidate) { return controller_.HasPaletteGroupNamed(candidate); }, reservedGroupNames);
						renamedPaletteGroups.insert({ name, newName });
						it = renamedPaletteGroups.find(name);
					}
					if (it != renamedPaletteGroups.end() && it->second != name) {
						actionDetail = " -> " + it->second;
					}
				}
			}
		} else if (kind == "palette") {
			if (entity.contains("palette") && entity["palette"].is_object() && entity["palette"].contains("name") && entity["palette"]["name"].is_string()) {
				const wxString name = JsonToWxStringLocal(entity["palette"]["name"]);
				keyLabel = name;
				exists = controller_.HasTilesetNamed(name);
				if (exists && options_.onConflict == MaterialsWorkbenchImportConflictStrategy::RenameWithSuffix) {
					auto it = renamedPalettes.find(name);
					if (it == renamedPalettes.end()) {
						const wxString newName = makeUniqueName(name, [&](const wxString &candidate) { return controller_.HasTilesetNamed(candidate); }, reservedPaletteNames);
						renamedPalettes.insert({ name, newName });
						it = renamedPalettes.find(name);
					}
					if (it != renamedPalettes.end() && it->second != name) {
						actionDetail = " -> " + it->second;
					}
				}
			}
		}

		wxString action = exists ? "update" : "create";
		if (exists) {
			if (options_.onConflict == MaterialsWorkbenchImportConflictStrategy::SkipExisting) {
				action = "skip";
			} else if (options_.onConflict == MaterialsWorkbenchImportConflictStrategy::RenameWithSuffix) {
				if (kind == "palette_group" || kind == "palette") {
					action = "rename";
				} else {
					action = "update";
				}
			}
		}

		if (action == "update") {
			++updates;
		} else if (action == "create" || action == "rename") {
			++creates;
		} else {
			++skips;
		}
		const long row = planList_->InsertItem(planList_->GetItemCount(), wxString::FromUTF8(kind.c_str()));
		planList_->SetItem(row, 1, keyLabel);
		planList_->SetItem(row, 2, action + actionDetail);

		++processed;
		if (progress && total > 0 && progressSpan > 0 && (processed % 200) == 0) {
			const int value = progressStart + static_cast<int>((static_cast<double>(processed) / total) * progressSpan);
			progress->Update(std::min(100, std::max(0, value)), "Building import preview...");
			wxYieldIfNeeded();
		}
	}

	okButton_->Enable(planList_->GetItemCount() > 0);
	summaryLabel_->SetLabel(wxString::Format("Plan: %d create, %d update, %d skip.", creates, updates, skips));
	planList_->Thaw();
}

void MaterialsWorkbenchImportDialog::UpdateSummary() {
}
