#ifndef RME_MATERIALS_WORKBENCH_BORDER_PANEL_H_
#define RME_MATERIALS_WORKBENCH_BORDER_PANEL_H_

#include <functional>
#include <map>

#include <wx/panel.h>
#include <wx/string.h>

#include "brush_database.h"

class ItemButton;
class ItemToggleButton;
class MaterialsWorkbenchController;
class wxChoice;
class wxSpinCtrl;
class wxStaticText;
class wxTextCtrl;

class MaterialsWorkbenchBorderPanel : public wxPanel {
public:
	MaterialsWorkbenchBorderPanel(wxWindow* parent, MaterialsWorkbenchController &controller);

	void ClearWorkspace(const wxString &message);
	bool LoadBorderSet(const wxString &contextKey, int itemIndex);
	void SetOnBorderSetSaved(std::function<void(int64_t)> callback);

private:
	void BuildLayout();
	void PopulateFields();
	void RefreshSlotGrid();
	void RefreshPreviewGrid();
	void SelectEdge(const wxString &edge);
	void UpdateSelectedEdgeEditor();
	void SetStatusMessage(const wxString &message);
	void SetFieldsEnabled(bool enabled);
	bool SaveCurrentBorderSet();

	void OnApplyToSlot(wxCommandEvent &event);
	void OnClearSlot(wxCommandEvent &event);
	void OnPickItem(wxCommandEvent &event);
	void OnSave(wxCommandEvent &event);
	void OnRevert(wxCommandEvent &event);
	void OnSelectedItemIdChanged(wxCommandEvent &event);
	void OnSelectedItemIdSpin(wxSpinEvent &event);

	MaterialsWorkbenchController &controller_;
	std::function<void(int64_t)> onBorderSetSaved_;
	BorderSetStorageRecord borderSetStorage_;
	wxString currentContextKey_;
	int currentItemIndex_ = -1;
	bool hasBorderSet_ = false;
	wxString selectedEdge_;
	std::map<wxString, int> slotItemIds_;
	std::map<wxString, ItemToggleButton*> slotButtons_;
	std::map<wxString, wxStaticText*> slotValueLabels_;
	std::map<wxString, ItemButton*> previewButtons_;

	wxStaticText* titleLabel_ = nullptr;
	wxStaticText* subtitleLabel_ = nullptr;
	wxStaticText* summaryLabel_ = nullptr;
	wxTextCtrl* idCtrl_ = nullptr;
	wxSpinCtrl* xmlBorderIdCtrl_ = nullptr;
	wxChoice* scopeChoice_ = nullptr;
	wxTextCtrl* typeCtrl_ = nullptr;
	wxSpinCtrl* borderGroupCtrl_ = nullptr;
	wxSpinCtrl* groundEquivalentCtrl_ = nullptr;
	wxTextCtrl* ownerBrushIdCtrl_ = nullptr;
	wxTextCtrl* sourceCtrl_ = nullptr;
	wxStaticText* selectedEdgeLabel_ = nullptr;
	wxSpinCtrl* selectedItemIdCtrl_ = nullptr;
	ItemButton* selectedItemPreview_ = nullptr;
	wxStaticText* statusLabel_ = nullptr;
};

#endif
