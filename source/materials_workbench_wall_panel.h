#ifndef RME_MATERIALS_WORKBENCH_WALL_PANEL_H_
#define RME_MATERIALS_WORKBENCH_WALL_PANEL_H_

#include <functional>
#include <vector>

#include <wx/panel.h>

#include "brush_database.h"

class ItemButton;
class ItemToggleButton;
class MaterialsWorkbenchController;
class wxCheckBox;
class wxChoice;
class wxScrolledWindow;
class wxSpinCtrl;
class wxStaticText;
class wxTextCtrl;
class wxWrapSizer;

class MaterialsWorkbenchWallPanel : public wxPanel {
public:
	MaterialsWorkbenchWallPanel(wxWindow* parent, MaterialsWorkbenchController &controller);

	void ClearWorkspace(const wxString &message);
	bool LoadWallBrush(const wxString &contextKey, int itemIndex);
	void SetOnWallBrushSaved(std::function<void(int64_t)> callback);

private:
	const WallPartRecord* GetSelectedPart() const;
	WallPartRecord* GetSelectedPart();
	void BuildLayout();
	void PopulateFields();
	void RefreshPartChoice();
	void RefreshSelectedPart();
	void RefreshItemGrid();
	void RefreshDoorGrid();
	void SyncSelectedItemEditor();
	void SyncSelectedDoorEditor();
	void NormalizeWallParts();
	void SetStatusMessage(const wxString &message);
	void SetFieldsEnabled(bool enabled);
	bool SaveCurrentWallBrush();

	void OnPartChanged(wxCommandEvent &event);
	void OnPickItem(wxCommandEvent &event);
	void OnApplyItem(wxCommandEvent &event);
	void OnRemoveItem(wxCommandEvent &event);
	void OnPickDoorItem(wxCommandEvent &event);
	void OnApplyDoor(wxCommandEvent &event);
	void OnRemoveDoor(wxCommandEvent &event);
	void OnSave(wxCommandEvent &event);
	void OnRevert(wxCommandEvent &event);
	void OnItemIdChanged(wxCommandEvent &event);
	void OnItemIdSpin(wxSpinEvent &event);
	void OnDoorItemIdChanged(wxCommandEvent &event);
	void OnDoorItemIdSpin(wxSpinEvent &event);

	MaterialsWorkbenchController &controller_;
	std::function<void(int64_t)> onWallBrushSaved_;
	BrushStorageRecord wallBrushStorage_;
	wxString currentContextKey_;
	int currentItemIndex_ = -1;
	int selectedPartIndex_ = -1;
	int selectedItemIndex_ = -1;
	int selectedDoorIndex_ = -1;
	bool hasWallBrush_ = false;

	wxStaticText* titleLabel_ = nullptr;
	wxStaticText* subtitleLabel_ = nullptr;
	wxStaticText* summaryLabel_ = nullptr;
	wxTextCtrl* brushIdCtrl_ = nullptr;
	wxTextCtrl* brushNameCtrl_ = nullptr;
	wxTextCtrl* brushSourceCtrl_ = nullptr;
	wxChoice* partChoice_ = nullptr;
	wxStaticText* partSummaryLabel_ = nullptr;
	wxScrolledWindow* itemGridScroll_ = nullptr;
	wxScrolledWindow* doorGridScroll_ = nullptr;
	wxWrapSizer* itemGridSizer_ = nullptr;
	wxWrapSizer* doorGridSizer_ = nullptr;
	std::vector<ItemToggleButton*> itemButtons_;
	std::vector<ItemToggleButton*> doorButtons_;
	wxStaticText* selectedItemLabel_ = nullptr;
	wxSpinCtrl* itemIdCtrl_ = nullptr;
	wxSpinCtrl* itemChanceCtrl_ = nullptr;
	ItemButton* itemPreviewButton_ = nullptr;
	wxStaticText* selectedDoorLabel_ = nullptr;
	wxSpinCtrl* doorItemIdCtrl_ = nullptr;
	wxChoice* doorTypeChoice_ = nullptr;
	wxCheckBox* doorOpenCtrl_ = nullptr;
	wxCheckBox* doorHateCtrl_ = nullptr;
	ItemButton* doorPreviewButton_ = nullptr;
	wxStaticText* statusLabel_ = nullptr;
};

#endif
