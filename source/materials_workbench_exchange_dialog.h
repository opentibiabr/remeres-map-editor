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
class wxChoice;
class wxListCtrl;
class wxNotebook;
class wxProgressDialog;
class wxSearchCtrl;
class wxStaticText;
class wxTextCtrl;

class MaterialsWorkbenchExportDialog final : public wxDialog {
public:
	MaterialsWorkbenchExportDialog(wxWindow* parent, MaterialsWorkbenchController &controller);

	const MaterialsWorkbenchExportSelection &GetSelection() const { return selection_; }

private:
	void RebuildData();
	void RebuildBorderList();
	void RebuildBrushList();
	void RebuildPaletteGroupList();
	void RebuildPaletteList();
	void UpdateSummary();
	void UpdateOkState();
	void OnToggleIncludeDependencies(wxCommandEvent &event);
	void OnSelectAllShown(wxCheckListBox* list);
	void OnClearAllShown(wxCheckListBox* list);
	void OnListToggled(wxCheckListBox* list, int index);

	MaterialsWorkbenchController &controller_;
	MaterialsWorkbenchExportSelection selection_;

	struct BorderRow {
		int xmlBorderId = 0;
		wxString label;
	};
	struct BrushRow {
		int64_t brushId = 0;
		wxString brushType;
		wxString brushName;
		wxString label;
	};

	std::vector<BorderRow> allBorders_;
	std::vector<BrushRow> allBrushes_;
	std::vector<wxString> allPaletteGroups_;
	std::vector<wxString> allPalettes_;

	std::vector<wxString> brushTypeChoices_;

	wxCheckBox* includeDepsCtrl_ = nullptr;
	wxNotebook* notebook_ = nullptr;

	wxSearchCtrl* borderFilterCtrl_ = nullptr;
	wxCheckListBox* borderList_ = nullptr;

	wxSearchCtrl* brushFilterCtrl_ = nullptr;
	wxChoice* brushTypeChoiceCtrl_ = nullptr;
	wxCheckListBox* brushList_ = nullptr;

	wxSearchCtrl* paletteGroupFilterCtrl_ = nullptr;
	wxCheckListBox* paletteGroupList_ = nullptr;

	wxSearchCtrl* paletteFilterCtrl_ = nullptr;
	wxCheckListBox* paletteList_ = nullptr;

	wxStaticText* summaryLabel_ = nullptr;
	wxTextCtrl* depsPreview_ = nullptr;
	wxButton* okButton_ = nullptr;
};

class MaterialsWorkbenchImportDialog final : public wxDialog {
public:
	MaterialsWorkbenchImportDialog(wxWindow* parent, const nlohmann::json &root, MaterialsWorkbenchController &controller);

	const nlohmann::json &GetJson() const { return root_; }
	MaterialsWorkbenchImportOptions GetOptions() const { return options_; }
	void BuildPlanWithProgress(wxProgressDialog* progress = nullptr, int progressStart = 0, int progressSpan = 0);

private:
	void BuildPlan(wxProgressDialog* progress, int progressStart, int progressSpan);
	void UpdateSummary();

	nlohmann::json root_;
	MaterialsWorkbenchController &controller_;
	MaterialsWorkbenchImportOptions options_;

	wxListCtrl* planList_ = nullptr;
	wxChoice* conflictChoice_ = nullptr;
	wxStaticText* summaryLabel_ = nullptr;
	wxButton* okButton_ = nullptr;
};

#endif
