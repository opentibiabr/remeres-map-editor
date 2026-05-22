#include "main.h"

#include "materials_workbench_window.h"

#include <wx/simplebook.h>
#include <wx/splitter.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/treectrl.h>

#include "materials_workbench_palette_panel.h"

namespace {
	MaterialsWorkbenchWindow* g_materials_workbench_window = nullptr;

	class MaterialsWorkbenchTreeItemData : public wxTreeItemData {
	public:
		MaterialsWorkbenchTreeItemData(MaterialsWorkbenchNodeKind kind, wxString contextKey, int itemIndex) :
			kind(kind),
			contextKey(std::move(contextKey)),
			itemIndex(itemIndex) {
		}

		MaterialsWorkbenchNodeKind kind;
		wxString contextKey;
		int itemIndex = -1;
	};

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

	wxPanel* CreateOverviewTextPanel(wxWindow* parent, const MaterialsWorkbenchController &controller, wxTextCtrl*& outOverview) {
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
	RefreshWorkbenchState();
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
	workspaceBook_ = new wxSimplebook(contentSplitter, wxID_ANY);
	wxPanel* overviewPanel = CreateOverviewTextPanel(workspaceBook_, controller_, overviewText_);
	palettePanel_ = new MaterialsWorkbenchPalettePanel(workspaceBook_, controller_);
	palettePanel_->SetOnPaletteSaved([this]() {
		auto* itemData = dynamic_cast<MaterialsWorkbenchTreeItemData*>(navigationTree_->GetItemData(navigationTree_->GetSelection()));
		if (!itemData) {
			return;
		}
		inspectorText_->SetValue(controller_.BuildSelectionInspector(itemData->kind, itemData->contextKey, itemData->itemIndex));
	});
	workspaceBook_->AddPage(overviewPanel, "Overview");
	workspaceBook_->AddPage(palettePanel_, "Palette");
	workspaceBook_->SetSelection(0);
	wxPanel* inspectorPanel = CreateInspectorPanel(contentSplitter, controller_, inspectorText_);

	contentSplitter->SplitVertically(workspaceBook_, inspectorPanel, FromDIP(1020));
	rootSplitter->SplitVertically(sidebarPanel, contentSplitter, FromDIP(260));

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(rootSplitter, 1, wxEXPAND);
	SetSizer(sizer);
}

void MaterialsWorkbenchWindow::RefreshWorkbenchState() {
	controller_.ReloadCatalog();
	overviewText_->SetValue(controller_.GetOverviewText());
	inspectorText_->SetValue(controller_.GetInspectorText());
}

void MaterialsWorkbenchWindow::PopulateNavigation() {
	navigationTree_->DeleteAllItems();
	wxTreeItemId root = navigationTree_->AddRoot("Materials Workbench");
	const auto appendNodes = [&](auto &&self, const wxTreeItemId &parentItem, const std::vector<MaterialsWorkbenchTreeNode> &nodes) -> void {
		for (const MaterialsWorkbenchTreeNode &node : nodes) {
			wxTreeItemId item = navigationTree_->AppendItem(
				parentItem,
				node.label,
				-1,
				-1,
				newd MaterialsWorkbenchTreeItemData(node.kind, node.contextKey, node.itemIndex)
			);
			if (!node.children.empty()) {
				self(self, item, node.children);
			}
		}
	};

	appendNodes(appendNodes, root, controller_.BuildNavigationTree());

	wxTreeItemIdValue cookie;
	wxTreeItemId firstChild = navigationTree_->GetFirstChild(root, cookie);
	if (firstChild.IsOk()) {
		navigationTree_->SelectItem(firstChild);
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

		auto* itemData = dynamic_cast<MaterialsWorkbenchTreeItemData*>(navigationTree_->GetItemData(item));
		if (!itemData) {
			workspaceBook_->SetSelection(0);
			overviewText_->SetValue(controller_.GetOverviewText());
			inspectorText_->SetValue(controller_.GetInspectorText());
			return;
		}

		overviewText_->SetValue(controller_.BuildSelectionOverview(itemData->kind, itemData->contextKey, itemData->itemIndex));
		inspectorText_->SetValue(controller_.BuildSelectionInspector(itemData->kind, itemData->contextKey, itemData->itemIndex));

		if (itemData->kind == MaterialsWorkbenchNodeKind::Tileset) {
			TilesetStorageRecord tileset;
			if (controller_.GetTilesetByIndex(itemData->itemIndex, tileset) && palettePanel_->LoadPalette(tileset)) {
				workspaceBook_->SetSelection(1);
				return;
			}
			palettePanel_->ClearWorkspace("Failed to load the selected palette workspace.");
		}

		workspaceBook_->SetSelection(0);
	});
}

void MaterialsWorkbenchWindow::OnClose(wxCloseEvent &event) {
	g_materials_workbench_window = nullptr;
	Destroy();
	event.Skip(false);
}
