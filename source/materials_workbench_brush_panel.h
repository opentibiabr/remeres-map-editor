#ifndef RME_MATERIALS_WORKBENCH_BRUSH_PANEL_H_
#define RME_MATERIALS_WORKBENCH_BRUSH_PANEL_H_

#include <functional>

#include <wx/panel.h>

#include "brush_database.h"

class MaterialsWorkbenchController;
class wxCheckBox;
class wxSpinCtrl;
class wxStaticText;
class wxTextCtrl;

class MaterialsWorkbenchBrushPanel : public wxPanel {
public:
	MaterialsWorkbenchBrushPanel(wxWindow* parent, MaterialsWorkbenchController &controller);

	void ClearWorkspace(const wxString &message);
	bool LoadBrush(const wxString &contextKey, int itemIndex);
	void SetOnBrushSaved(std::function<void(int64_t)> callback);

private:
	void BuildLayout();
	void PopulateFields();
	void UpdateSummary();
	void SetStatusMessage(const wxString &message);
	void SetFieldsEnabled(bool enabled);
	bool SaveCurrentBrush();

	void OnSave(wxCommandEvent &event);
	void OnRevert(wxCommandEvent &event);

	MaterialsWorkbenchController &controller_;
	std::function<void(int64_t)> onBrushSaved_;
	BrushStorageRecord brushStorage_;
	wxString currentContextKey_;
	int currentItemIndex_ = -1;
	bool hasBrush_ = false;

	wxStaticText* titleLabel_ = nullptr;
	wxStaticText* subtitleLabel_ = nullptr;
	wxStaticText* summaryLabel_ = nullptr;
	wxTextCtrl* nameCtrl_ = nullptr;
	wxTextCtrl* typeCtrl_ = nullptr;
	wxTextCtrl* idCtrl_ = nullptr;
	wxTextCtrl* sourceCtrl_ = nullptr;
	wxSpinCtrl* lookIdCtrl_ = nullptr;
	wxSpinCtrl* serverLookIdCtrl_ = nullptr;
	wxSpinCtrl* zOrderCtrl_ = nullptr;
	wxSpinCtrl* thicknessCtrl_ = nullptr;
	wxSpinCtrl* thicknessCeilingCtrl_ = nullptr;
	wxCheckBox* draggableCtrl_ = nullptr;
	wxCheckBox* onBlockingCtrl_ = nullptr;
	wxCheckBox* onDuplicateCtrl_ = nullptr;
	wxCheckBox* redoBordersCtrl_ = nullptr;
	wxCheckBox* randomizeCtrl_ = nullptr;
	wxCheckBox* oneSizeCtrl_ = nullptr;
	wxCheckBox* soloOptionalCtrl_ = nullptr;
	wxStaticText* statusLabel_ = nullptr;
};

#endif
