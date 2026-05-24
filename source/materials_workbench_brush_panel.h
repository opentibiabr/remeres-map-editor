#ifndef RME_MATERIALS_WORKBENCH_BRUSH_PANEL_H_
#define RME_MATERIALS_WORKBENCH_BRUSH_PANEL_H_

#include <functional>

#include <wx/panel.h>

#include "brush_database.h"

class MaterialsWorkbenchController;
class wxButton;
class wxCheckBox;
class wxListBox;
class wxNotebook;
class wxPanel;
class wxSimplebook;
class wxSpinCtrl;
class wxStaticText;
class wxTextCtrl;

class MaterialsWorkbenchBrushPanel : public wxPanel {
public:
	MaterialsWorkbenchBrushPanel(wxWindow* parent, MaterialsWorkbenchController &controller);

	void ClearWorkspace(const wxString &message);
	bool LoadBrush(const wxString &contextKey, int itemIndex);
	void SetOnBrushSaved(std::function<void(int64_t, const wxString&, const wxString&)> callback);
	void SetOnBrushStateChanged(std::function<void()> callback);
	bool HasPendingChanges() const;
	bool IsCurrentBrushSelection(const wxString &contextKey, int itemIndex) const;
	wxString GetCurrentBrushDisplayName() const;
	wxString GetCurrentBrushInspectorText() const;
	bool ResolvePendingChangesBeforeSwitch(wxWindow* parent, const wxString &targetLabel);

private:
	struct VariationEditorState {
		bool valid = false;
		int workspaceTabSelection = 0;
		int groundItemIndex = -1;
		int alignedNodeIndex = -1;
		int alignedItemIndex = -1;
		int doodadAlternativeIndex = -1;
		int doodadSingleItemIndex = -1;
		int doodadCompositeIndex = -1;
		int doodadTileIndex = -1;
		int doodadTileItemIndex = -1;
		int groundTopItem = -1;
		int alignedNodesTopItem = -1;
		int alignedItemsTopItem = -1;
		int doodadAlternativesTopItem = -1;
		int doodadSingleItemsTopItem = -1;
		int doodadCompositesTopItem = -1;
		int doodadTilesTopItem = -1;
		int doodadTileItemsTopItem = -1;
	};

	void BuildLayout();
	wxPanel* BuildMetadataPage(wxNotebook* notebook);
	wxPanel* BuildVariationsPage(wxNotebook* notebook);
	wxPanel* BuildUnsupportedVariationsPage(wxSimplebook* book);
	wxPanel* BuildGroundVariationsPage(wxSimplebook* book);
	wxPanel* BuildAlignedVariationsPage(wxSimplebook* book);
	wxPanel* BuildDoodadVariationsPage(wxSimplebook* book);
	void PopulateFields();
	void PopulateMetadataFields();
	void UpdateSummary();
	void SetStatusMessage(const wxString &message);
	void SetFieldsEnabled(bool enabled);
	void ResetVariationSelection();
	void CommitVariationEditorState();
	void RefreshVariationEditor();
	void RefreshGroundItemList();
	void RefreshGroundSelection();
	void RefreshAlignedNodeList();
	void RefreshAlignedItemList();
	void RefreshAlignedSelection();
	void RefreshDoodadAlternativeList();
	void RefreshDoodadSingleItemList();
	void RefreshDoodadCompositeList();
	void RefreshDoodadTileList();
	void RefreshDoodadTileItemList();
	void RefreshDoodadSelection();
	void NormalizeVariationSortOrders();
	BrushStorageRecord BuildEditableStorageFromCurrentState() const;
	void RefreshDirtyState();
	void NotifyBrushStateChanged();
	void UpdateWorkspaceHeader();
	void UpdateActionButtons();
	void UpdateModifiedHighlights();
	void UpdateMetadataModifiedHighlights(const BrushRecord &editableBrush);
	void UpdateVariationModifiedHighlights(const BrushStorageRecord &editableStorage);
	VariationEditorState CaptureVariationEditorState() const;
	void RestoreVariationEditorState(const VariationEditorState &state);
	bool SaveCurrentBrush();
	bool ValidateBrushStorage(wxString &error) const;
	wxString GetEffectiveBrushType() const;
	bool UsesGroundVariationEditor() const;
	bool UsesAlignedVariationEditor() const;
	bool UsesDoodadVariationEditor() const;

	void OnSave(wxCommandEvent &event);
	void OnRevert(wxCommandEvent &event);
	void OnAddGroundItem(wxCommandEvent &event);
	void OnRemoveGroundItem(wxCommandEvent &event);
	void OnGroundItemSelected(wxCommandEvent &event);
	void OnGroundItemValueChanged(wxCommandEvent &event);
	void OnAddAlignedNode(wxCommandEvent &event);
	void OnRemoveAlignedNode(wxCommandEvent &event);
	void OnAlignedNodeSelected(wxCommandEvent &event);
	void OnAlignedNodeAlignChanged(wxCommandEvent &event);
	void OnAddAlignedItem(wxCommandEvent &event);
	void OnRemoveAlignedItem(wxCommandEvent &event);
	void OnAlignedItemSelected(wxCommandEvent &event);
	void OnAlignedItemValueChanged(wxCommandEvent &event);
	void OnAddDoodadAlternative(wxCommandEvent &event);
	void OnRemoveDoodadAlternative(wxCommandEvent &event);
	void OnDoodadAlternativeSelected(wxCommandEvent &event);
	void OnAddDoodadSingleItem(wxCommandEvent &event);
	void OnRemoveDoodadSingleItem(wxCommandEvent &event);
	void OnDoodadSingleItemSelected(wxCommandEvent &event);
	void OnDoodadSingleItemValueChanged(wxCommandEvent &event);
	void OnAddDoodadComposite(wxCommandEvent &event);
	void OnRemoveDoodadComposite(wxCommandEvent &event);
	void OnDoodadCompositeSelected(wxCommandEvent &event);
	void OnDoodadCompositeChanceChanged(wxCommandEvent &event);
	void OnAddDoodadTile(wxCommandEvent &event);
	void OnRemoveDoodadTile(wxCommandEvent &event);
	void OnDoodadTileSelected(wxCommandEvent &event);
	void OnDoodadTileOffsetChanged(wxCommandEvent &event);
	void OnAddDoodadTileItem(wxCommandEvent &event);
	void OnRemoveDoodadTileItem(wxCommandEvent &event);
	void OnDoodadTileItemSelected(wxCommandEvent &event);
	void OnDoodadTileItemValueChanged(wxCommandEvent &event);
	void OnMetadataFieldChanged(wxCommandEvent &event);

	MaterialsWorkbenchController &controller_;
	std::function<void(int64_t, const wxString&, const wxString&)> onBrushSaved_;
	std::function<void()> onBrushStateChanged_;
	BrushStorageRecord brushStorage_;
	BrushStorageRecord loadedBrushStorage_;
	wxString currentContextKey_;
	int currentItemIndex_ = -1;
	bool hasBrush_ = false;
	bool internalUpdate_ = false;
	bool dirty_ = false;

	wxNotebook* workspaceTabs_ = nullptr;
	wxStaticText* titleLabel_ = nullptr;
	wxStaticText* subtitleLabel_ = nullptr;
	wxStaticText* summaryLabel_ = nullptr;
	wxStaticText* variationsStatusLabel_ = nullptr;
	wxButton* saveButton_ = nullptr;
	wxButton* revertButton_ = nullptr;
	wxTextCtrl* nameCtrl_ = nullptr;
	wxTextCtrl* typeCtrl_ = nullptr;
	wxTextCtrl* idCtrl_ = nullptr;
	wxTextCtrl* storageCtrl_ = nullptr;
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
	wxSimplebook* variationsBook_ = nullptr;
	wxStaticText* variationsEmptyLabel_ = nullptr;
	wxListBox* groundItemsList_ = nullptr;
	wxSpinCtrl* groundItemIdCtrl_ = nullptr;
	wxSpinCtrl* groundItemChanceCtrl_ = nullptr;
	int groundItemIndex_ = -1;
	wxStaticText* alignedSectionLabel_ = nullptr;
	wxListBox* alignedNodesList_ = nullptr;
	wxTextCtrl* alignedNodeAlignCtrl_ = nullptr;
	wxListBox* alignedItemsList_ = nullptr;
	wxSpinCtrl* alignedItemIdCtrl_ = nullptr;
	wxSpinCtrl* alignedItemChanceCtrl_ = nullptr;
	int alignedNodeIndex_ = -1;
	int alignedItemIndex_ = -1;
	wxListBox* doodadAlternativesList_ = nullptr;
	wxListBox* doodadSingleItemsList_ = nullptr;
	wxSpinCtrl* doodadSingleItemIdCtrl_ = nullptr;
	wxSpinCtrl* doodadSingleItemChanceCtrl_ = nullptr;
	wxListBox* doodadCompositesList_ = nullptr;
	wxSpinCtrl* doodadCompositeChanceCtrl_ = nullptr;
	wxListBox* doodadTilesList_ = nullptr;
	wxSpinCtrl* doodadTileOffsetXCtrl_ = nullptr;
	wxSpinCtrl* doodadTileOffsetYCtrl_ = nullptr;
	wxSpinCtrl* doodadTileOffsetZCtrl_ = nullptr;
	wxListBox* doodadTileItemsList_ = nullptr;
	wxSpinCtrl* doodadTileItemIdCtrl_ = nullptr;
	int doodadAlternativeIndex_ = -1;
	int doodadSingleItemIndex_ = -1;
	int doodadCompositeIndex_ = -1;
	int doodadTileIndex_ = -1;
	int doodadTileItemIndex_ = -1;
	wxStaticText* statusLabel_ = nullptr;
};

#endif
