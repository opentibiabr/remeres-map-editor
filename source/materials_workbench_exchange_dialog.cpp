#include "main.h"

#include "materials_workbench_exchange_dialog.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/checklst.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include "brush_database.h"
#include "materials_workbench_controller.h"

namespace {
	wxString BuildBrushRowLabel(const BrushRecord &brush) {
		return wxString::Format("%s: %s", brush.type, brush.name);
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

	wxBoxSizer* listSizer = new wxBoxSizer(wxHORIZONTAL);
	borderList_ = new wxCheckListBox(this, wxID_ANY);
	brushList_ = new wxCheckListBox(this, wxID_ANY);
	paletteGroupList_ = new wxCheckListBox(this, wxID_ANY);
	paletteList_ = new wxCheckListBox(this, wxID_ANY);

	wxBoxSizer* col1 = new wxBoxSizer(wxVERTICAL);
	col1->Add(new wxStaticText(this, wxID_ANY, "Borders"), 0, wxBOTTOM, FromDIP(4));
	col1->Add(borderList_, 1, wxEXPAND);
	wxBoxSizer* col2 = new wxBoxSizer(wxVERTICAL);
	col2->Add(new wxStaticText(this, wxID_ANY, "Brushes"), 0, wxBOTTOM, FromDIP(4));
	col2->Add(brushList_, 1, wxEXPAND);
	wxBoxSizer* col3 = new wxBoxSizer(wxVERTICAL);
	col3->Add(new wxStaticText(this, wxID_ANY, "Palette Groups"), 0, wxBOTTOM, FromDIP(4));
	col3->Add(paletteGroupList_, 1, wxEXPAND);
	wxBoxSizer* col4 = new wxBoxSizer(wxVERTICAL);
	col4->Add(new wxStaticText(this, wxID_ANY, "Palettes"), 0, wxBOTTOM, FromDIP(4));
	col4->Add(paletteList_, 1, wxEXPAND);

	listSizer->Add(col1, 1, wxEXPAND | wxRIGHT, FromDIP(8));
	listSizer->Add(col2, 1, wxEXPAND | wxRIGHT, FromDIP(8));
	listSizer->Add(col3, 1, wxEXPAND | wxRIGHT, FromDIP(8));
	listSizer->Add(col4, 1, wxEXPAND);

	summaryLabel_ = new wxStaticText(this, wxID_ANY, "");

	wxStdDialogButtonSizer* buttons = new wxStdDialogButtonSizer();
	okButton_ = new wxButton(this, wxID_OK, "Export");
	buttons->AddButton(okButton_);
	buttons->AddButton(new wxButton(this, wxID_CANCEL));
	buttons->Realize();

	rootSizer->Add(includeDepsCtrl_, 0, wxEXPAND | wxALL, FromDIP(12));
	rootSizer->Add(listSizer, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(12));
	rootSizer->Add(summaryLabel_, 0, wxEXPAND | wxALL, FromDIP(12));
	rootSizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

	SetSizerAndFit(rootSizer);
	SetMinSize(wxSize(FromDIP(980), FromDIP(520)));

	RebuildLists();
	UpdateSummary();
	UpdateOkState();

	includeDepsCtrl_->Bind(wxEVT_CHECKBOX, &MaterialsWorkbenchExportDialog::OnToggleIncludeDependencies, this);
	borderList_->Bind(wxEVT_CHECKLISTBOX, [this](wxCommandEvent &) { UpdateSummary(); UpdateOkState(); });
	brushList_->Bind(wxEVT_CHECKLISTBOX, [this](wxCommandEvent &) { UpdateSummary(); UpdateOkState(); });
	paletteGroupList_->Bind(wxEVT_CHECKLISTBOX, [this](wxCommandEvent &) { UpdateSummary(); UpdateOkState(); });
	paletteList_->Bind(wxEVT_CHECKLISTBOX, [this](wxCommandEvent &) { UpdateSummary(); UpdateOkState(); });

	Bind(wxEVT_BUTTON, [this](wxCommandEvent &event) {
		selection_.globalBorderXmlIds.clear();
		selection_.brushIds.clear();
		selection_.paletteNames.clear();
		selection_.paletteGroupNames.clear();

		for (size_t i = 0; i < borderList_->GetCount(); ++i) {
			if (!borderList_->IsChecked(i)) {
				continue;
			}
			const int xmlBorderId = static_cast<int>(reinterpret_cast<intptr_t>(borderList_->GetClientData(i)));
			if (xmlBorderId > 0) {
				selection_.globalBorderXmlIds.push_back(xmlBorderId);
			}
		}
		for (size_t i = 0; i < brushList_->GetCount(); ++i) {
			if (!brushList_->IsChecked(i)) {
				continue;
			}
			const int64_t brushId = static_cast<int64_t>(reinterpret_cast<intptr_t>(brushList_->GetClientData(i)));
			if (brushId > 0) {
				selection_.brushIds.push_back(brushId);
			}
		}
		for (size_t i = 0; i < paletteGroupList_->GetCount(); ++i) {
			if (paletteGroupList_->IsChecked(i)) {
				selection_.paletteGroupNames.push_back(paletteGroupList_->GetString(i));
			}
		}
		for (size_t i = 0; i < paletteList_->GetCount(); ++i) {
			if (paletteList_->IsChecked(i)) {
				selection_.paletteNames.push_back(paletteList_->GetString(i));
			}
		}

		event.Skip();
	}, wxID_OK);
}

void MaterialsWorkbenchExportDialog::RebuildLists() {
	borderList_->Clear();
	for (const BorderSetRecord &border : controller_.GetGlobalBorderSets()) {
		if (border.xmlBorderId <= 0) {
			continue;
		}
		const wxString label = wxString::Format("Border %d (%s)", border.xmlBorderId, border.borderType.IsEmpty() ? "normal" : border.borderType);
		const size_t idx = borderList_->Append(label);
		borderList_->SetClientData(idx, reinterpret_cast<void*>(static_cast<intptr_t>(border.xmlBorderId)));
	}

	brushList_->Clear();
	for (const MaterialsWorkbenchBrushGroup &group : controller_.GetBrushGroups()) {
		for (const BrushRecord &brush : group.brushes) {
			const size_t idx = brushList_->Append(BuildBrushRowLabel(brush));
			brushList_->SetClientData(idx, reinterpret_cast<void*>(static_cast<intptr_t>(brush.id)));
		}
	}
	for (const BrushRecord &brush : controller_.GetWallBrushes()) {
		const size_t idx = brushList_->Append(BuildBrushRowLabel(brush));
		brushList_->SetClientData(idx, reinterpret_cast<void*>(static_cast<intptr_t>(brush.id)));
	}

	paletteGroupList_->Clear();
	for (const PaletteGroupRecord &group : controller_.GetPaletteGroups()) {
		paletteGroupList_->Append(group.name);
	}

	paletteList_->Clear();
	for (const TilesetStorageRecord &tileset : controller_.GetTilesets()) {
		paletteList_->Append(tileset.name);
	}
}

void MaterialsWorkbenchExportDialog::OnToggleIncludeDependencies(wxCommandEvent &) {
	selection_.includeDependencies = includeDepsCtrl_->GetValue();
	UpdateSummary();
}

void MaterialsWorkbenchExportDialog::UpdateSummary() {
	const int borders = static_cast<int>(CountCheckedItems(borderList_));
	const int brushes = static_cast<int>(CountCheckedItems(brushList_));
	const int groups = static_cast<int>(CountCheckedItems(paletteGroupList_));
	const int palettes = static_cast<int>(CountCheckedItems(paletteList_));
	summaryLabel_->SetLabel(wxString::Format("Selected: %d borders, %d brushes, %d palette groups, %d palettes.", borders, brushes, groups, palettes));
}

void MaterialsWorkbenchExportDialog::UpdateOkState() {
	const bool hasAnything =
		CountCheckedItems(borderList_) > 0 ||
		CountCheckedItems(brushList_) > 0 ||
		CountCheckedItems(paletteGroupList_) > 0 ||
		CountCheckedItems(paletteList_) > 0;
	okButton_->Enable(hasAnything);
}

MaterialsWorkbenchImportDialog::MaterialsWorkbenchImportDialog(wxWindow* parent, const nlohmann::json &root, MaterialsWorkbenchController &controller) :
	wxDialog(parent, wxID_ANY, "Import Preview", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	root_(root),
	controller_(controller) {
	wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);

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

	rootSizer->Add(planList_, 1, wxEXPAND | wxALL, FromDIP(12));
	rootSizer->Add(summaryLabel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
	rootSizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

	SetSizerAndFit(rootSizer);
	SetMinSize(wxSize(FromDIP(720), FromDIP(420)));

	BuildPlan();
	UpdateSummary();
}

void MaterialsWorkbenchImportDialog::BuildPlan() {
	planList_->DeleteAllItems();
	int creates = 0;
	int updates = 0;

	if (!root_.is_object() || !root_.contains("entities") || !root_["entities"].is_array()) {
		okButton_->Enable(false);
		return;
	}

	for (const nlohmann::json &entity : root_["entities"]) {
		if (!entity.is_object() || !entity.contains("kind") || !entity["kind"].is_string()) {
			continue;
		}
		const std::string kind = entity["kind"].get<std::string>();
		wxString keyLabel = "";
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
			}
		} else if (kind == "palette") {
			if (entity.contains("palette") && entity["palette"].is_object() && entity["palette"].contains("name") && entity["palette"]["name"].is_string()) {
				const wxString name = JsonToWxStringLocal(entity["palette"]["name"]);
				keyLabel = name;
				exists = controller_.HasTilesetNamed(name);
			}
		}

		const wxString action = exists ? "update" : "create";
		if (exists) {
			++updates;
		} else {
			++creates;
		}
		const long row = planList_->InsertItem(planList_->GetItemCount(), wxString::FromUTF8(kind.c_str()));
		planList_->SetItem(row, 1, keyLabel);
		planList_->SetItem(row, 2, action);
	}

	okButton_->Enable(planList_->GetItemCount() > 0);
	summaryLabel_->SetLabel(wxString::Format("Plan: %d create, %d update.", creates, updates));
}

void MaterialsWorkbenchImportDialog::UpdateSummary() {
}
