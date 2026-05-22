#ifndef RME_MATERIALS_WORKBENCH_WINDOW_H_
#define RME_MATERIALS_WORKBENCH_WINDOW_H_

#include <wx/frame.h>

#include "materials_workbench_controller.h"

class wxPanel;
class wxSplitterWindow;
class wxSimplebook;
class wxTextCtrl;
class wxTreeCtrl;
class MaterialsWorkbenchBorderPanel;
class MaterialsWorkbenchBrushPanel;
class MaterialsWorkbenchPalettePanel;

class MaterialsWorkbenchWindow : public wxFrame {
public:
	static void Open(wxWindow* parent);

	explicit MaterialsWorkbenchWindow(wxWindow* parent);

private:
	void BuildLayout();
	void PopulateNavigation();
	void RefreshWorkbenchState();
	void BindEvents();
	bool SelectNavigationNode(MaterialsWorkbenchNodeKind kind, const wxString &contextKey, int itemIndex);
	void OnClose(wxCloseEvent &event);

	MaterialsWorkbenchController controller_;
	wxTreeCtrl* navigationTree_ = nullptr;
	wxSimplebook* workspaceBook_ = nullptr;
	wxTextCtrl* overviewText_ = nullptr;
	wxTextCtrl* inspectorText_ = nullptr;
	MaterialsWorkbenchPalettePanel* palettePanel_ = nullptr;
	MaterialsWorkbenchBorderPanel* borderPanel_ = nullptr;
	MaterialsWorkbenchBrushPanel* brushPanel_ = nullptr;

	DECLARE_EVENT_TABLE()
};

#endif
