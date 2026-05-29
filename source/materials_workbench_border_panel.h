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
class wxButton;
class wxChoice;
class wxListBox;
class wxPanel;
class wxSpinCtrl;
class wxStaticText;
class wxTextCtrl;

class MaterialsWorkbenchBorderPanel : public wxPanel {
public:
	MaterialsWorkbenchBorderPanel(wxWindow* parent, MaterialsWorkbenchController &controller);

	void ClearWorkspace(const wxString &message);
	bool LoadBorderSet(const wxString &contextKey, int itemIndex);
	void SetOnBorderSetSaved(std::function<void(int64_t)> callback);
	void SetOnBorderSetDeleted(std::function<void(const wxString &)> callback);
	void SetOnBorderSetStateChanged(std::function<void()> callback);
	void SetOnOpenLinkedBrush(std::function<void(int64_t)> callback);
	bool HasPendingChanges() const;
	bool IsCurrentBorderSelection(const wxString &contextKey, int itemIndex) const;
	wxString GetCurrentBorderSetDisplayName() const;
	bool ResolvePendingChangesBeforeSwitch(wxWindow* parent, const wxString &targetLabel);

private:
	void BuildLayout();
	void PopulateFields();
	void UpdateSummaryLabels();
	void RefreshSlotGrid();
	void RefreshPreviewGrid();
	void RefreshPreviewSelectionState();
	void PopulateUsageContextList();
	void UpdateUsageContextControls();
	const BorderSetUsageRecord* GetSelectedUsageContext() const;
	int ResolveCenterPreviewItemId() const;
	wxString ResolveCenterSourceLabel() const;
	wxString BuildOwnerBrushDisplayLabel(int64_t ownerBrushId) const;
	void HandleUsageContextChanged();
	void RefreshScopeSpecificLayout();
	void RefreshUsageDetails();
	bool ReloadBorderSetById(int64_t borderSetId);
	bool LoadBrushStorageById(int64_t brushId, BrushStorageRecord &outBrush, wxString &error) const;
	int FindMatchingGroundBorderIndex(const BrushStorageRecord &brushStorage, const BorderSetUsageRecord &usage) const;
	int SuggestNextBorderId() const;
	void SelectEdge(const wxString &edge);
	void SaveCurrentBorderEditorState();
	void RestoreCurrentBorderEditorState();
	void UpdateSelectedEdgeEditor();
	void SyncSelectedSlotFromEditor(bool updateStatus);
	void SetStatusMessage(const wxString &message);
	void SetFieldsEnabled(bool enabled);
	BorderSetStorageRecord BuildComparableStorageFromCurrentState() const;
	bool ValidateBorderSetStorage(const BorderSetStorageRecord &storage, wxString &error) const;
	void RefreshDirtyState();
	void NotifyBorderSetStateChanged();
	void UpdateWorkspaceHeader();
	void UpdateActionButtons();
	bool SaveCurrentBorderSet();

	void OnApplyToSlot(wxCommandEvent &event);
	void OnClearSlot(wxCommandEvent &event);
	void OnPickItem(wxCommandEvent &event);
	void OnSave(wxCommandEvent &event);
	void OnRevert(wxCommandEvent &event);
	void OnSelectedItemIdChanged(wxCommandEvent &event);
	void OnSelectedItemIdSpin(wxSpinEvent &event);
	void OnMetadataFieldChanged(wxCommandEvent &event);
	void OnUsageContextChanged(wxCommandEvent &event);
	void OnUsageSearchChanged(wxCommandEvent &event);
	void OnOpenLinkedBrush(wxCommandEvent &event);
	void OnCreateBorder(wxCommandEvent &event);
	void OnDeleteBorder(wxCommandEvent &event);
	void OnAddUsageContext(wxCommandEvent &event);
	void OnEditUsageContext(wxCommandEvent &event);
	void OnRemoveUsageContext(wxCommandEvent &event);

	MaterialsWorkbenchController &controller_;
	std::function<void(int64_t)> onBorderSetSaved_;
	std::function<void(const wxString &)> onBorderSetDeleted_;
	std::function<void()> onBorderSetStateChanged_;
	std::function<void(int64_t)> onOpenLinkedBrush_;
	BorderSetStorageRecord borderSetStorage_;
	BorderSetStorageRecord loadedBorderSetStorage_;
	std::vector<BorderSetUsageRecord> borderSetUsages_;
	wxString currentContextKey_;
	int currentItemIndex_ = -1;
	int selectedUsageIndex_ = wxNOT_FOUND;
	std::vector<int> filteredUsageIndexes_;
	bool hasBorderSet_ = false;
	bool dirty_ = false;
	bool internalUpdate_ = false;
	wxString selectedEdge_;
	std::map<wxString, int> slotItemIds_;
	std::map<int64_t, wxString> borderSetSelectedEdges_;
	std::map<wxString, ItemToggleButton*> slotButtons_;
	std::map<wxString, wxStaticText*> slotValueLabels_;

	wxStaticText* titleLabel_ = nullptr;
	wxStaticText* subtitleLabel_ = nullptr;
	wxStaticText* summaryLabel_ = nullptr;
	wxStaticText* identityLabel_ = nullptr;
	wxButton* saveButton_ = nullptr;
	wxButton* revertButton_ = nullptr;
	wxButton* createBorderButton_ = nullptr;
	wxButton* deleteBorderButton_ = nullptr;
	wxTextCtrl* idCtrl_ = nullptr;
	wxSpinCtrl* xmlBorderIdCtrl_ = nullptr;
	wxChoice* scopeChoice_ = nullptr;
	wxTextCtrl* typeCtrl_ = nullptr;
	wxSpinCtrl* borderGroupCtrl_ = nullptr;
	wxSpinCtrl* groundEquivalentCtrl_ = nullptr;
	wxTextCtrl* ownerBrushCtrl_ = nullptr;
	wxTextCtrl* sourceCtrl_ = nullptr;
	wxPanel* inlineDetailsPanel_ = nullptr;
	wxPanel* globalDetailsPanel_ = nullptr;
	wxTextCtrl* usageSearchCtrl_ = nullptr;
	wxStaticText* usageSummaryLabel_ = nullptr;
	wxListBox* usageListBox_ = nullptr;
	ItemButton* usagePreviewItem_ = nullptr;
	wxStaticText* usageBrushLabel_ = nullptr;
	wxStaticText* usageAlignLabel_ = nullptr;
	wxStaticText* usageRoleLabel_ = nullptr;
	wxStaticText* usageCenterLabel_ = nullptr;
	wxButton* openLinkedBrushButton_ = nullptr;
	wxButton* addUsageContextButton_ = nullptr;
	wxButton* editUsageContextButton_ = nullptr;
	wxButton* removeUsageContextButton_ = nullptr;
	wxStaticText* selectedEdgeLabel_ = nullptr;
	wxSpinCtrl* selectedItemIdCtrl_ = nullptr;
	ItemButton* selectedItemPreview_ = nullptr;
	ItemButton* centerGroundSlotPreview_ = nullptr;
	wxStaticText* centerGroundSlotValueLabel_ = nullptr;
	wxPanel* previewMatrixPanel_ = nullptr;
	wxStaticText* statusLabel_ = nullptr;
};

#endif
