#include "main.h"

#include "materials_workbench_window.h"

#include <wx/splitter.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/treectrl.h>

namespace {
	MaterialsWorkbenchWindow* g_materials_workbench_window = nullptr;

	wxPanel* CreateSidebarPanel(wxWindow* parent, wxTreeCtrl*& outTree) {
		wxPanel* panel = new wxPanel(parent, wxID_ANY);
		wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

		wxStaticText* title = new wxStaticText(panel, wxID_ANY, "Workspaces");
		outTree = new wxTreeCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_SINGLE);

		sizer->Add(title, 0, wxEXPAND | wxALL, panel->FromDIP(8));
		sizer->Add(new wxStaticLine(panel), 0, wxEXPAND | wxLEFT | wxRIGHT, panel->FromDIP(8));
		sizer->Add(outTree, 1, wxEXPAND | wxALL, panel->FromDIP(8));
		panel->SetSizer(sizer);
		return panel;
	}

	wxPanel* CreateOverviewPanel(wxWindow* parent, const MaterialsWorkbenchController &controller, wxTextCtrl*& outOverview) {
		wxPanel* panel = new wxPanel(parent, wxID_ANY);
		wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

		wxStaticText* title = new wxStaticText(panel, wxID_ANY, controller.GetWindowTitle());
		wxFont titleFont = title->GetFont();
		titleFont.SetPointSize(titleFont.GetPointSize() + 4);
		titleFont.SetWeight(wxFONTWEIGHT_BOLD);
		title->SetFont(titleFont);

		wxStaticText* subtitle = new wxStaticText(panel, wxID_ANY, "Primary workspace for authoring materials data backed by materials.db");
		outOverview = new wxTextCtrl(panel, wxID_ANY, controller.GetOverviewText(), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE);

		sizer->Add(title, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, panel->FromDIP(14));
		sizer->Add(subtitle, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, panel->FromDIP(14));
		sizer->Add(new wxStaticLine(panel), 0, wxEXPAND | wxLEFT | wxRIGHT, panel->FromDIP(14));
		sizer->Add(outOverview, 1, wxEXPAND | wxALL, panel->FromDIP(14));
		panel->SetSizer(sizer);
		return panel;
	}

	wxPanel* CreateInspectorPanel(wxWindow* parent, const MaterialsWorkbenchController &controller, wxTextCtrl*& outInspector) {
		wxPanel* panel = new wxPanel(parent, wxID_ANY);
		wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

		wxStaticText* title = new wxStaticText(panel, wxID_ANY, "Inspector");
		outInspector = new wxTextCtrl(panel, wxID_ANY, controller.GetInspectorText(), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE);

		sizer->Add(title, 0, wxEXPAND | wxALL, panel->FromDIP(10));
		sizer->Add(new wxStaticLine(panel), 0, wxEXPAND | wxLEFT | wxRIGHT, panel->FromDIP(10));
		sizer->Add(outInspector, 1, wxEXPAND | wxALL, panel->FromDIP(10));
		panel->SetSizer(sizer);
		return panel;
	}
} // namespace

BEGIN_EVENT_TABLE(MaterialsWorkbenchWindow, wxFrame)
	EVT_CLOSE(MaterialsWorkbenchWindow::OnClose)
END_EVENT_TABLE()

void MaterialsWorkbenchWindow::Open(wxWindow* parent) {
	if (g_materials_workbench_window) {
		if (g_materials_workbench_window->IsIconized()) {
			g_materials_workbench_window->Iconize(false);
		}
		g_materials_workbench_window->Show();
		g_materials_workbench_window->Raise();
		g_materials_workbench_window->Maximize(true);
		return;
	}

	g_materials_workbench_window = newd MaterialsWorkbenchWindow(parent);
	g_materials_workbench_window->Show();
	g_materials_workbench_window->Maximize(true);
	g_materials_workbench_window->Raise();
}

MaterialsWorkbenchWindow::MaterialsWorkbenchWindow(wxWindow* parent) :
	wxFrame(parent, wxID_ANY, "Materials Workbench", wxDefaultPosition, wxSize(1400, 900), wxDEFAULT_FRAME_STYLE | wxRESIZE_BORDER | wxCLIP_CHILDREN) {
	BuildLayout();
	PopulateNavigation();
	BindEvents();
}

void MaterialsWorkbenchWindow::BuildLayout() {
	wxSplitterWindow* rootSplitter = new wxSplitterWindow(this, wxID_ANY);
	rootSplitter->SetSashGravity(0.16);
	rootSplitter->SetMinimumPaneSize(FromDIP(220));

	wxSplitterWindow* contentSplitter = new wxSplitterWindow(rootSplitter, wxID_ANY);
	contentSplitter->SetSashGravity(0.78);
	contentSplitter->SetMinimumPaneSize(FromDIP(260));

	wxPanel* sidebarPanel = CreateSidebarPanel(rootSplitter, navigationTree_);
	wxPanel* overviewPanel = CreateOverviewPanel(contentSplitter, controller_, overviewText_);
	wxPanel* inspectorPanel = CreateInspectorPanel(contentSplitter, controller_, inspectorText_);

	contentSplitter->SplitVertically(overviewPanel, inspectorPanel, FromDIP(1020));
	rootSplitter->SplitVertically(sidebarPanel, contentSplitter, FromDIP(260));

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(rootSplitter, 1, wxEXPAND);
	SetSizer(sizer);
}

void MaterialsWorkbenchWindow::PopulateNavigation() {
	wxTreeItemId root = navigationTree_->AddRoot("Materials Workbench");
	for (const wxString &section : controller_.GetNavigationSections()) {
		navigationTree_->AppendItem(root, section);
	}
	if (navigationTree_->GetCount() > 0) {
		wxTreeItemIdValue cookie;
		wxTreeItemId firstChild = navigationTree_->GetFirstChild(root, cookie);
		if (firstChild.IsOk()) {
			navigationTree_->SelectItem(firstChild);
		}
	}
}

void MaterialsWorkbenchWindow::BindEvents() {
	navigationTree_->Bind(wxEVT_TREE_SEL_CHANGED, [this](wxTreeEvent &event) {
		const wxTreeItemId item = event.GetItem();
		if (!item.IsOk()) {
			return;
		}

		const wxString selection = navigationTree_->GetItemText(item);
		if (selection.IsEmpty()) {
			return;
		}

		overviewText_->SetValue(
			controller_.GetOverviewText() + "\n\nCurrent workspace selection: " + selection
		);
		inspectorText_->SetValue(
			"Inspector placeholder\n\nSelected workspace: " + selection +
			"\n\nSubsequent stages will replace this placeholder with contextual properties, validation and editing controls."
		);
	});
}

void MaterialsWorkbenchWindow::OnClose(wxCloseEvent &event) {
	g_materials_workbench_window = nullptr;
	Destroy();
	event.Skip(false);
}
