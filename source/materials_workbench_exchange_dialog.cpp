#include "main.h"

#include "materials_workbench_exchange_dialog.h"

#include <algorithm>
#include <functional>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include <wx/button.h>
#include <wx/bookctrl.h>
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
#include <wx/textctrl.h>

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

	template <typename T>
	std::vector<T> CollectAddedValues(const std::vector<T> &selected, const std::vector<T> &resolved, size_t maxCount) {
		std::unordered_set<T> selectedSet;
		selectedSet.reserve(selected.size());
		for (const T &value : selected) {
			selectedSet.insert(value);
		}
		std::vector<T> added;
		added.reserve(std::min(maxCount, resolved.size()));
		for (const T &value : resolved) {
			if (!selectedSet.count(value)) {
				added.push_back(value);
				if (added.size() >= maxCount) {
					break;
				}
			}
		}
		return added;
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
	depsPreview_ = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxBORDER_THEME);
	depsPreview_->SetMinSize(wxSize(-1, FromDIP(120)));

	wxStdDialogButtonSizer* buttons = new wxStdDialogButtonSizer();
	okButton_ = new wxButton(this, wxID_OK, "Export");
	buttons->AddButton(okButton_);
	buttons->AddButton(new wxButton(this, wxID_CANCEL));
	buttons->Realize();

	rootSizer->Add(includeDepsCtrl_, 0, wxEXPAND | wxALL, FromDIP(12));
	rootSizer->Add(notebook_, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(12));
	rootSizer->Add(summaryLabel_, 0, wxEXPAND | wxALL, FromDIP(12));
	rootSizer->Add(depsPreview_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
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
	wxString details;

	if (selection_.includeDependencies) {
		wxString error;
		MaterialsWorkbenchResolvedExportSelection resolved;
		if (!ResolveMaterialsWorkbenchExportSelection(controller_, selection_, resolved, error) || !error.IsEmpty()) {
			details << "Dependency resolution failed.\n";
			details << error;
		} else {
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

			details << "Exporting (including dependencies)\n";
			details << wxString::Format("Borders: %d (+%d deps)\n", static_cast<int>(exportingBorders.size()), depBorders);
			details << wxString::Format("Brushes: %d (+%d deps)\n", static_cast<int>(exportingBrushes.size()), depBrushes);
			details << wxString::Format("Palette Groups: %d (+%d deps)\n", static_cast<int>(exportingGroups.size()), depGroups);
			details << wxString::Format("Palettes: %d (+%d deps)\n", static_cast<int>(exportingPalettes.size()), depPalettes);

			const size_t kMaxPreviewItems = 12;

			if (depGroups > 0) {
				details << "\nAdded palette groups\n";
				for (const wxString &name : CollectAddedValues(selection_.paletteGroupNames, resolved.paletteGroupNames, kMaxPreviewItems)) {
					details << "  + " << name << "\n";
				}
			}

			if (depPalettes > 0) {
				details << "\nAdded palettes\n";
				for (const wxString &name : CollectAddedValues(selection_.paletteNames, resolved.paletteNames, kMaxPreviewItems)) {
					details << "  + " << name << "\n";
				}
			}

			if (depBrushes > 0) {
				std::unordered_map<int64_t, wxString> brushLabelById;
				brushLabelById.reserve(allBrushes_.size());
				for (const BrushRow &row : allBrushes_) {
					brushLabelById.insert({ row.brushId, row.label });
				}
				details << "\nAdded brushes\n";
				for (int64_t brushId : CollectAddedValues(selection_.brushIds, resolved.brushIds, kMaxPreviewItems)) {
					const auto it = brushLabelById.find(brushId);
					details << "  + " << (it != brushLabelById.end() ? it->second : wxString::Format("Brush %lld", static_cast<long long>(brushId))) << "\n";
				}
			}

			if (depBorders > 0) {
				std::unordered_map<int, wxString> borderLabelById;
				borderLabelById.reserve(allBorders_.size());
				for (const BorderRow &row : allBorders_) {
					borderLabelById.insert({ row.xmlBorderId, row.label });
				}
				details << "\nAdded borders\n";
				for (int xmlBorderId : CollectAddedValues(selection_.globalBorderXmlIds, resolved.globalBorderXmlIds, kMaxPreviewItems)) {
					const auto it = borderLabelById.find(xmlBorderId);
					details << "  + " << (it != borderLabelById.end() ? it->second : wxString::Format("Border %d", xmlBorderId)) << "\n";
				}
			}
		}
	}

	summaryLabel_->SetLabel(label);
	depsPreview_->Show(selection_.includeDependencies);
	if (selection_.includeDependencies) {
		depsPreview_->ChangeValue(details);
	}
	Layout();
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

	planFilterCtrl_ = new wxSearchCtrl(this, wxID_ANY);
	planFilterCtrl_->ShowSearchButton(false);
	planFilterCtrl_->ShowCancelButton(true);
	planFilterCtrl_->SetDescriptiveText("Filter plan");

	auto makePlanList = [&](wxWindow* parent) -> wxListCtrl* {
		wxListCtrl* list = new wxListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxBORDER_THEME);
		list->InsertColumn(0, "Kind");
		list->InsertColumn(1, "Key");
		list->InsertColumn(2, "Action");
		list->InsertColumn(3, "Detail");
		list->SetColumnWidth(0, FromDIP(120));
		list->SetColumnWidth(1, FromDIP(320));
		list->SetColumnWidth(2, FromDIP(120));
		list->SetColumnWidth(3, FromDIP(220));
		return list;
	};

	auto makePlanPage = [&](const wxString &title, wxListCtrl*& outList) {
		wxPanel* page = new wxPanel(planNotebook_, wxID_ANY);
		wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
		outList = makePlanList(page);
		sizer->Add(outList, 1, wxEXPAND | wxALL, FromDIP(8));
		page->SetSizer(sizer);
		planNotebook_->AddPage(page, title);
	};

	planNotebook_ = new wxNotebook(this, wxID_ANY);
	makePlanPage("All", planAllList_);
	makePlanPage("Palette Groups", planGroupsList_);
	makePlanPage("Palettes", planPalettesList_);
	makePlanPage("Brushes", planBrushesList_);
	makePlanPage("Borders", planBordersList_);
	planNotebook_->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent &) { RefreshPlanLists(); });

	summaryLabel_ = new wxStaticText(this, wxID_ANY, "");

	wxStdDialogButtonSizer* buttons = new wxStdDialogButtonSizer();
	okButton_ = new wxButton(this, wxID_OK, "Import");
	buttons->AddButton(okButton_);
	buttons->AddButton(new wxButton(this, wxID_CANCEL));
	buttons->Realize();

	rootSizer->Add(optionsSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
	rootSizer->Add(planFilterCtrl_, 0, wxEXPAND | wxALL, FromDIP(12));
	rootSizer->Add(planNotebook_, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(12));
	rootSizer->Add(summaryLabel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
	rootSizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

	SetSizerAndFit(rootSizer);
	SetMinSize(wxSize(FromDIP(860), FromDIP(520)));

	okButton_->Enable(false);
	summaryLabel_->SetLabel("Building preview...");

	conflictChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) {
		BuildPlan(nullptr, 0, 0);
	});

	planFilterCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { RefreshPlanLists(); });
	planFilterCtrl_->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN, [this](wxCommandEvent &) { planFilterCtrl_->ChangeValue(""); RefreshPlanLists(); });
}

void MaterialsWorkbenchImportDialog::BuildPlanWithProgress(wxProgressDialog* progress, int progressStart, int progressSpan) {
	BuildPlan(progress, progressStart, progressSpan);
}

void MaterialsWorkbenchImportDialog::BuildPlan(wxProgressDialog* progress, int progressStart, int progressSpan) {
	allPlanRows_.clear();
	int creates = 0;
	int updates = 0;
	int skips = 0;
	int invalid = 0;
	int warningRows = 0;

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
		summaryLabel_->SetLabel("Plan: 0 create, 0 update, 0 skip, 0 invalid, 0 warnings.");
		RefreshPlanLists();
		return;
	}

	auto normalizeName = [](const wxString &name) -> wxString {
		wxString value = name;
		value.MakeLower();
		return value;
	};

	std::unordered_set<int> importedBorderXmlIds;
	std::unordered_set<wxString> importedGroupNames;
	std::unordered_set<wxString> importedPaletteNames;
	std::unordered_set<wxString> importedBrushNames;
	std::unordered_set<wxString> importedBrushKeys;

	std::unordered_set<wxString> duplicateGroupNames;
	std::unordered_set<wxString> duplicatePaletteNames;
	std::unordered_set<wxString> duplicateBrushKeys;
	std::unordered_set<int> duplicateBorderXmlIds;

	std::unordered_set<int> existingBorderXmlIds;
	for (const BorderSetRecord &border : controller_.GetGlobalBorderSets()) {
		if (border.xmlBorderId > 0) {
			existingBorderXmlIds.insert(border.xmlBorderId);
		}
	}

	std::unordered_set<wxString> existingBrushNames;
	std::unordered_set<wxString> existingBrushKeys;
	for (const MaterialsWorkbenchBrushGroup &group : controller_.GetBrushGroups()) {
		for (const BrushRecord &brush : group.brushes) {
			existingBrushNames.insert(normalizeName(brush.name));
			existingBrushKeys.insert(normalizeName(brush.type) + ":" + normalizeName(brush.name));
		}
	}
	for (const BrushRecord &brush : controller_.GetWallBrushes()) {
		existingBrushNames.insert(normalizeName(brush.name));
		existingBrushKeys.insert(normalizeName(brush.type) + ":" + normalizeName(brush.name));
	}

	const int total = static_cast<int>(root_["entities"].size());
	int processed = 0;
	std::unordered_map<wxString, wxString> renamedPaletteGroups;
	std::unordered_map<wxString, wxString> renamedPalettes;
	std::unordered_set<wxString> reservedGroupNames;
	std::unordered_set<wxString> reservedPaletteNames;

	auto parseBrushKey = [&](const nlohmann::json &key, wxString &outType, wxString &outName) -> bool {
		if (!key.is_object()) {
			return false;
		}
		if (!key.contains("type") || !key["type"].is_string()) {
			return false;
		}
		if (!key.contains("name") || !key["name"].is_string()) {
			return false;
		}
		outType = JsonToWxStringLocal(key["type"]);
		outName = JsonToWxStringLocal(key["name"]);
		return !outType.IsEmpty() && !outName.IsEmpty();
	};

	auto existsBrushName = [&](const wxString &name) -> bool {
		if (name.IsEmpty()) {
			return false;
		}
		const wxString key = normalizeName(name);
		return importedBrushNames.find(key) != importedBrushNames.end() || existingBrushNames.find(key) != existingBrushNames.end();
	};

	auto existsBrushKey = [&](const wxString &type, const wxString &name) -> bool {
		if (type.IsEmpty() || name.IsEmpty()) {
			return false;
		}
		const wxString key = normalizeName(type) + ":" + normalizeName(name);
		return importedBrushKeys.find(key) != importedBrushKeys.end() || existingBrushKeys.find(key) != existingBrushKeys.end();
	};

	for (const nlohmann::json &entity : root_["entities"]) {
		if (!entity.is_object() || !entity.contains("kind") || !entity["kind"].is_string()) {
			continue;
		}
		const std::string kind = entity["kind"].get<std::string>();
		if (kind == "border_set") {
			if (entity.contains("borderSet") && entity["borderSet"].is_object() && entity["borderSet"].contains("xmlBorderId") && entity["borderSet"]["xmlBorderId"].is_number_integer()) {
				const int xmlBorderId = entity["borderSet"]["xmlBorderId"].get<int>();
				if (xmlBorderId > 0) {
					if (!importedBorderXmlIds.insert(xmlBorderId).second) {
						duplicateBorderXmlIds.insert(xmlBorderId);
					}
				}
			}
		} else if (kind == "palette_group") {
			if (entity.contains("group") && entity["group"].is_object() && entity["group"].contains("name") && entity["group"]["name"].is_string()) {
				const wxString name = JsonToWxStringLocal(entity["group"]["name"]);
				if (!name.IsEmpty()) {
					const wxString key = normalizeName(name);
					if (!importedGroupNames.insert(key).second) {
						duplicateGroupNames.insert(key);
					}
				}
			}
		} else if (kind == "palette") {
			if (entity.contains("palette") && entity["palette"].is_object() && entity["palette"].contains("name") && entity["palette"]["name"].is_string()) {
				const wxString name = JsonToWxStringLocal(entity["palette"]["name"]);
				if (!name.IsEmpty()) {
					const wxString key = normalizeName(name);
					if (!importedPaletteNames.insert(key).second) {
						duplicatePaletteNames.insert(key);
					}
				}
			}
		} else if (kind == "brush") {
			if (entity.contains("brush") && entity["brush"].is_object() && entity["brush"].contains("type") && entity["brush"].contains("name") && entity["brush"]["type"].is_string() && entity["brush"]["name"].is_string()) {
				const wxString type = JsonToWxStringLocal(entity["brush"]["type"]);
				const wxString name = JsonToWxStringLocal(entity["brush"]["name"]);
				if (!name.IsEmpty()) {
					importedBrushNames.insert(normalizeName(name));
				}
				if (!type.IsEmpty() && !name.IsEmpty()) {
					const wxString key = normalizeName(type) + ":" + normalizeName(name);
					if (!importedBrushKeys.insert(key).second) {
						duplicateBrushKeys.insert(key);
					}
				}
			}
		}
	}


	auto makeUniqueName = [&](const wxString &base, const std::function<bool(const wxString &)> &exists, std::unordered_set<wxString> &reserved) -> wxString {
		const wxString baseKey = normalizeName(base);
		if (!exists(base) && reserved.find(baseKey) == reserved.end()) {
			reserved.insert(baseKey);
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
		wxString detail = "";
		std::vector<wxString> warnings;
		bool exists = false;
		bool isValid = true;

		if (kind == "border_set") {
			if (entity.contains("borderSet") && entity["borderSet"].is_object() && entity["borderSet"].contains("xmlBorderId") && entity["borderSet"]["xmlBorderId"].is_number_integer()) {
				const int xmlBorderId = entity["borderSet"]["xmlBorderId"].get<int>();
				keyLabel = wxString::Format("Border %d", xmlBorderId);
				if (xmlBorderId <= 0) {
					isValid = false;
					detail = "Invalid border_set: xmlBorderId must be greater than zero.";
				} else if (duplicateBorderXmlIds.find(xmlBorderId) != duplicateBorderXmlIds.end()) {
					isValid = false;
					detail = "Duplicate border_set xmlBorderId in import file.";
				} else {
					BorderSetRecord existing;
					exists = g_brush_database.findBorderSetByXmlBorderId(xmlBorderId, existing) && existing.id > 0;
				}
			} else {
				isValid = false;
				detail = "Invalid border_set: missing xmlBorderId.";
			}
		} else if (kind == "brush") {
			if (entity.contains("brush") && entity["brush"].is_object() && entity["brush"].contains("type") && entity["brush"].contains("name") && entity["brush"]["type"].is_string() && entity["brush"]["name"].is_string()) {
				const wxString type = JsonToWxStringLocal(entity["brush"]["type"]);
				const wxString name = JsonToWxStringLocal(entity["brush"]["name"]);
				keyLabel = wxString::Format("%s: %s", type, name);
				BrushRecord existing;
				exists = g_brush_database.findBrushByNameAndType(name, type, existing) && existing.id > 0;

				const wxString brushKeyLower = normalizeName(type) + ":" + normalizeName(name);
				if (duplicateBrushKeys.find(brushKeyLower) != duplicateBrushKeys.end()) {
					isValid = false;
					detail = "Duplicate brush key in import file.";
				} else {
					if (entity.contains("links") && entity["links"].is_array()) {
						for (const nlohmann::json &row : entity["links"]) {
							if (!row.is_object()) {
								continue;
							}
							if (row.contains("target") && row["target"].is_object()) {
								wxString targetType;
								wxString targetName;
								if (parseBrushKey(row["target"], targetType, targetName)) {
									if (!existsBrushKey(targetType, targetName)) {
										warnings.push_back(wxString::Format("unresolved link target %s: %s", targetType, targetName));
									}
								}
							} else if (row.contains("targetName") && row["targetName"].is_string()) {
								const wxString targetName = JsonToWxStringLocal(row["targetName"]);
								if (!existsBrushName(targetName)) {
									warnings.push_back(wxString::Format("unresolved link target %s", targetName));
								}
							}
						}
					}

					if (entity.contains("groundBorders") && entity["groundBorders"].is_array()) {
						for (const nlohmann::json &row : entity["groundBorders"]) {
							if (!row.is_object()) {
								continue;
							}

							if (row.contains("borderRef") && row["borderRef"].is_object()) {
								const nlohmann::json &ref = row["borderRef"];
								if (ref.contains("scope") && ref["scope"].is_string()) {
									const wxString scope = JsonToWxStringLocal(ref["scope"]);
									if (scope.IsSameAs("global", false) && ref.contains("xmlBorderId") && ref["xmlBorderId"].is_number_integer()) {
										const int xmlBorderId = ref["xmlBorderId"].get<int>();
										if (xmlBorderId > 0 && importedBorderXmlIds.find(xmlBorderId) == importedBorderXmlIds.end() && existingBorderXmlIds.find(xmlBorderId) == existingBorderXmlIds.end()) {
											isValid = false;
											detail = wxString::Format("Missing global border_set Border %d (not in file and not in DB).", xmlBorderId);
											break;
										}
									}
								}
							}

							if (row.contains("targetBrush") && row["targetBrush"].is_object()) {
								wxString targetType;
								wxString targetName;
								if (parseBrushKey(row["targetBrush"], targetType, targetName)) {
									if (!existsBrushKey(targetType, targetName)) {
										warnings.push_back(wxString::Format("unresolved border target %s: %s", targetType, targetName));
									}
								}
							} else if (row.contains("targetBrushName") && row["targetBrushName"].is_string()) {
								const wxString targetName = JsonToWxStringLocal(row["targetBrushName"]);
								if (!existsBrushName(targetName)) {
									warnings.push_back(wxString::Format("unresolved border target %s", targetName));
								}
							}
						}
					}
				}
			} else {
				isValid = false;
				detail = "Invalid brush: missing type/name.";
			}
		} else if (kind == "palette_group") {
			if (entity.contains("group") && entity["group"].is_object() && entity["group"].contains("name") && entity["group"]["name"].is_string()) {
				const wxString name = JsonToWxStringLocal(entity["group"]["name"]);
				keyLabel = name;
				exists = controller_.HasPaletteGroupNamed(name);
				const wxString nameKey = normalizeName(name);
				if (duplicateGroupNames.find(nameKey) != duplicateGroupNames.end()) {
					isValid = false;
					detail = "Duplicate palette group name in import file.";
				}
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
			} else {
				isValid = false;
				detail = "Invalid palette_group: missing name.";
			}
		} else if (kind == "palette") {
			if (entity.contains("palette") && entity["palette"].is_object() && entity["palette"].contains("name") && entity["palette"]["name"].is_string()) {
				const wxString name = JsonToWxStringLocal(entity["palette"]["name"]);
				keyLabel = name;
				exists = controller_.HasTilesetNamed(name);
				const wxString nameKey = normalizeName(name);
				if (duplicatePaletteNames.find(nameKey) != duplicatePaletteNames.end()) {
					isValid = false;
					detail = "Duplicate palette name in import file.";
				}
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
				if (options_.onConflict == MaterialsWorkbenchImportConflictStrategy::RenameWithSuffix) {
					if (entity["palette"].contains("paletteGroupName") && entity["palette"]["paletteGroupName"].is_string()) {
						const wxString groupName = JsonToWxStringLocal(entity["palette"]["paletteGroupName"]);
						const auto groupIt = renamedPaletteGroups.find(groupName);
						if (groupIt != renamedPaletteGroups.end() && groupIt->second != groupName) {
							if (!actionDetail.IsEmpty()) {
								actionDetail << " |";
							}
							actionDetail << " group -> " << groupIt->second;
						}
					}
				}

				if (entity["palette"].contains("paletteGroupName") && entity["palette"]["paletteGroupName"].is_string()) {
					wxString groupName = JsonToWxStringLocal(entity["palette"]["paletteGroupName"]);
					if (options_.onConflict == MaterialsWorkbenchImportConflictStrategy::RenameWithSuffix) {
						const auto groupIt = renamedPaletteGroups.find(groupName);
						if (groupIt != renamedPaletteGroups.end()) {
							groupName = groupIt->second;
						}
					}
					if (!groupName.IsEmpty()) {
						const wxString groupKey = normalizeName(groupName);
						const bool groupExists = controller_.HasPaletteGroupNamed(groupName) || importedGroupNames.find(groupKey) != importedGroupNames.end();
						if (!groupExists) {
							isValid = false;
							detail = wxString::Format("Missing palette group '%s'.", groupName);
						}
					}
				}

				if (isValid && entity.contains("sections") && entity["sections"].is_array()) {
					for (const nlohmann::json &section : entity["sections"]) {
						if (!section.is_object() || !section.contains("entries") || !section["entries"].is_array()) {
							continue;
						}
						for (const nlohmann::json &e : section["entries"]) {
							if (!e.is_object()) {
								continue;
							}
							if (e.contains("brushName") && e["brushName"].is_string()) {
								const wxString brushName = JsonToWxStringLocal(e["brushName"]);
								if (!brushName.IsEmpty() && !existsBrushName(brushName)) {
									warnings.push_back(wxString::Format("unresolved palette brush '%s'", brushName));
								}
							}
							if (e.contains("afterBrushName") && e["afterBrushName"].is_string()) {
								const wxString brushName = JsonToWxStringLocal(e["afterBrushName"]);
								if (!brushName.IsEmpty() && !existsBrushName(brushName)) {
									warnings.push_back(wxString::Format("unresolved palette afterBrush '%s'", brushName));
								}
							}
						}
					}
				}
			} else {
				isValid = false;
				detail = "Invalid palette: missing name.";
			}
		} else {
			isValid = false;
			detail = "Unknown entity kind.";
		}

		wxString action = exists ? "update" : "create";
		if (!isValid) {
			action = "invalid";
		} else if (exists) {
			if (options_.onConflict == MaterialsWorkbenchImportConflictStrategy::SkipExisting) {
				action = "skip";
			} else if (options_.onConflict == MaterialsWorkbenchImportConflictStrategy::RenameWithSuffix) {
				if (kind == "palette_group" || kind == "palette") {
					action = "rename";
				}
			}
		}

		if (action == "update") {
			++updates;
		} else if (action == "create" || action == "rename") {
			++creates;
		} else if (action == "skip") {
			++skips;
		} else {
			++invalid;
		}
		PlanRow row;
		row.kind = wxString::FromUTF8(kind.c_str());
		row.key = keyLabel;
		row.action = action;
		wxString finalDetail = detail.IsEmpty() ? actionDetail : detail;
		if (isValid && !warnings.empty()) {
			if (!finalDetail.IsEmpty()) {
				finalDetail << " | ";
			}
			finalDetail << "Warning: ";
			for (size_t i = 0; i < warnings.size(); ++i) {
				if (i != 0) {
					finalDetail << "; ";
				}
				finalDetail << warnings[i];
			}
			++warningRows;
		}
		row.detail = finalDetail;
		allPlanRows_.push_back(std::move(row));

		++processed;
		if (progress && total > 0 && progressSpan > 0 && (processed % 200) == 0) {
			const int value = progressStart + static_cast<int>((static_cast<double>(processed) / total) * progressSpan);
			progress->Update(std::min(100, std::max(0, value)), "Building import preview...");
			wxYieldIfNeeded();
		}
	}

	RefreshPlanLists();
	okButton_->Enable(!allPlanRows_.empty() && invalid == 0);
	summaryLabel_->SetLabel(wxString::Format("Plan: %d create, %d update, %d skip, %d invalid, %d warnings.", creates, updates, skips, invalid, warningRows));
}

void MaterialsWorkbenchImportDialog::RefreshPlanLists() {
	const wxString queryLower = LowerCopy(planFilterCtrl_ ? planFilterCtrl_->GetValue() : "");

	auto countMatches = [&](const wxString &kindFilter) -> int {
		int count = 0;
		for (const PlanRow &row : allPlanRows_) {
			if (!kindFilter.IsEmpty() && !row.kind.IsSameAs(kindFilter, false)) {
				continue;
			}
			if (!queryLower.IsEmpty()) {
				const wxString kindLower = LowerCopy(row.kind);
				const wxString keyLower = LowerCopy(row.key);
				const wxString actionLower = LowerCopy(row.action);
				const wxString detailLower = LowerCopy(row.detail);
				if (!kindLower.Contains(queryLower) && !keyLower.Contains(queryLower) && !actionLower.Contains(queryLower) && !detailLower.Contains(queryLower)) {
					continue;
				}
			}
			++count;
		}
		return count;
	};

	const int allCount = countMatches("");
	const int groupsCount = countMatches("palette_group");
	const int palettesCount = countMatches("palette");
	const int brushesCount = countMatches("brush");
	const int bordersCount = countMatches("border_set");

	if (planNotebook_) {
		if (planNotebook_->GetPageCount() >= 5) {
			planNotebook_->SetPageText(0, wxString::Format("All (%d)", allCount));
			planNotebook_->SetPageText(1, wxString::Format("Palette Groups (%d)", groupsCount));
			planNotebook_->SetPageText(2, wxString::Format("Palettes (%d)", palettesCount));
			planNotebook_->SetPageText(3, wxString::Format("Brushes (%d)", brushesCount));
			planNotebook_->SetPageText(4, wxString::Format("Borders (%d)", bordersCount));
		}
	}

	wxListCtrl* activeList = planAllList_;
	wxString kindFilter = "";
	if (planNotebook_) {
		const int selection = planNotebook_->GetSelection();
		if (selection == 1) {
			activeList = planGroupsList_;
			kindFilter = "palette_group";
		} else if (selection == 2) {
			activeList = planPalettesList_;
			kindFilter = "palette";
		} else if (selection == 3) {
			activeList = planBrushesList_;
			kindFilter = "brush";
		} else if (selection == 4) {
			activeList = planBordersList_;
			kindFilter = "border_set";
		}
	}
	FillPlanList(activeList, kindFilter, queryLower);
}

void MaterialsWorkbenchImportDialog::FillPlanList(wxListCtrl* list, const wxString &kindFilter, const wxString &filterQuery) {
	if (!list) {
		return;
	}

	list->Freeze();
	list->DeleteAllItems();

	const wxString queryLower = LowerCopy(filterQuery);

	long rowIndex = 0;
	for (const PlanRow &row : allPlanRows_) {
		if (!kindFilter.IsEmpty() && !row.kind.IsSameAs(kindFilter, false)) {
			continue;
		}

		if (!queryLower.IsEmpty()) {
			const wxString kindLower = LowerCopy(row.kind);
			const wxString keyLower = LowerCopy(row.key);
			const wxString actionLower = LowerCopy(row.action);
			const wxString detailLower = LowerCopy(row.detail);
			if (!kindLower.Contains(queryLower) && !keyLower.Contains(queryLower) && !actionLower.Contains(queryLower) && !detailLower.Contains(queryLower)) {
				continue;
			}
		}

		const long itemIndex = list->InsertItem(rowIndex, row.kind);
		list->SetItem(itemIndex, 1, row.key);
		list->SetItem(itemIndex, 2, row.action);
		list->SetItem(itemIndex, 3, row.detail);

		if (row.action.IsSameAs("invalid", false)) {
			list->SetItemTextColour(itemIndex, wxColour(170, 0, 0));
		} else if (row.action.IsSameAs("skip", false)) {
			list->SetItemTextColour(itemIndex, wxColour(90, 90, 90));
		} else if (row.detail.Contains("Warning:")) {
			list->SetItemTextColour(itemIndex, wxColour(150, 90, 0));
		}

		++rowIndex;
	}

	list->Thaw();
}
