#ifndef RME_MATERIALS_WORKBENCH_WINDOW_H_
#define RME_MATERIALS_WORKBENCH_WINDOW_H_

#include <vector>

#include <wx/frame.h>

#include "materials_workbench_controller.h"

class wxPanel;
class wxButton;
class wxCommandEvent;
class wxSearchCtrl;
class wxSplitterWindow;
class wxSimplebook;
class wxTextCtrl;
class wxTreeCtrl;
class MaterialsWorkbenchBorderPanel;
class MaterialsWorkbenchBrushPanel;
class MaterialsWorkbenchPalettePanel;
class MaterialsWorkbenchWallPanel;
class MaterialsWorkbenchInspectorDialog;

class MaterialsWorkbenchWindow : public wxFrame {
public:
	static void Open(wxWindow* parent);
	static void OpenSqliteInspector(wxWindow* parent);

	explicit MaterialsWorkbenchWindow(wxWindow* parent);

	struct NavigationTreeState {
		bool valid = false;
		bool hasSelection = false;
		MaterialsWorkbenchNodeKind selectedKind = MaterialsWorkbenchNodeKind::Group;
		wxString selectedContextKey;
		int selectedItemIndex = -1;
		wxString firstVisibleNodeKey;
		std::vector<wxString> expandedNodeKeys;
	};

private:
	void BuildLayout();
	void PopulateNavigation();
	void RefreshWorkbenchState();
	void BindEvents();
	void BindSidebarEvents();
	void BindNavigationFilterEvents();
	void BindNavigationHoverEvents();
	void BindNavigationClickEvents();
	void BindNavigationSelectionEvents();
	void RefreshInspectorForCurrentSelection();
	void UpdateBrushNavigationBadge();
	void OpenInspector();
	void OnExportMaterials(wxCommandEvent &event);
	void OnImportMaterials(wxCommandEvent &event);
	bool GoToEntity(const wxString &entityKind, int64_t entityId, const wxString &entityName);
	void HandlePaletteSaved(const wxString &paletteName);
	void HandleBorderSetSaved(int64_t borderSetId);
	void HandleBorderSetDeleted(int64_t borderSetId, const wxString &scope);
	void HandleBrushSaved(int64_t brushId, const wxString &oldName, const wxString &newName);
	void HandleBrushDeleted(int64_t brushId);
	void HandleWallBrushSaved(int64_t brushId);
	bool SelectNavigationNode(MaterialsWorkbenchNodeKind kind, const wxString &contextKey, int itemIndex);
	void OnClose(wxCloseEvent &event);

	MaterialsWorkbenchController controller_;
	wxSearchCtrl* navigationFilterCtrl_ = nullptr;
	wxTreeCtrl* navigationTree_ = nullptr;
	wxSimplebook* workspaceBook_ = nullptr;
	wxTextCtrl* overviewText_ = nullptr;
	wxButton* inspectorButton_ = nullptr;
	wxButton* exportButton_ = nullptr;
	wxButton* importButton_ = nullptr;
	MaterialsWorkbenchInspectorDialog* inspectorDialog_ = nullptr;
	wxString hoveredNavigationTooltipKey_;
	wxString navigationFilterQuery_;
	bool navigationFilterActive_ = false;
	bool navigationPopulating_ = false;
	NavigationTreeState navigationStateBeforeFilter_;
	MaterialsWorkbenchPalettePanel* palettePanel_ = nullptr;
	MaterialsWorkbenchBorderPanel* borderPanel_ = nullptr;
	MaterialsWorkbenchBrushPanel* brushPanel_ = nullptr;
	MaterialsWorkbenchWallPanel* wallPanel_ = nullptr;

	DECLARE_EVENT_TABLE()
};

#endif
