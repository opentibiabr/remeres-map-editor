#ifndef RME_MATERIALS_WORKBENCH_EXCHANGE_DIALOG_H_
#define RME_MATERIALS_WORKBENCH_EXCHANGE_DIALOG_H_

#include <vector>

#include <wx/dialog.h>
#include <wx/string.h>

#include "materials_workbench_exchange.h"

class MaterialsWorkbenchController;
class wxButton;
class wxCheckBox;
class wxCheckListBox;
class wxListCtrl;
class wxStaticText;

class MaterialsWorkbenchExportDialog final : public wxDialog {
public:
	MaterialsWorkbenchExportDialog(wxWindow* parent, MaterialsWorkbenchController &controller);

	const MaterialsWorkbenchExportSelection &GetSelection() const { return selection_; }

private:
	void RebuildLists();
	void UpdateSummary();
	void UpdateOkState();
	void OnToggleIncludeDependencies(wxCommandEvent &event);

	MaterialsWorkbenchController &controller_;
	MaterialsWorkbenchExportSelection selection_;

	wxCheckBox* includeDepsCtrl_ = nullptr;
	wxCheckListBox* borderList_ = nullptr;
	wxCheckListBox* brushList_ = nullptr;
	wxCheckListBox* paletteGroupList_ = nullptr;
	wxCheckListBox* paletteList_ = nullptr;
	wxStaticText* summaryLabel_ = nullptr;
	wxButton* okButton_ = nullptr;
};

class MaterialsWorkbenchImportDialog final : public wxDialog {
public:
	MaterialsWorkbenchImportDialog(wxWindow* parent, const nlohmann::json &root, MaterialsWorkbenchController &controller);

	const nlohmann::json &GetJson() const { return root_; }

private:
	void BuildPlan();
	void UpdateSummary();

	nlohmann::json root_;
	MaterialsWorkbenchController &controller_;

	wxListCtrl* planList_ = nullptr;
	wxStaticText* summaryLabel_ = nullptr;
	wxButton* okButton_ = nullptr;
};

#endif
