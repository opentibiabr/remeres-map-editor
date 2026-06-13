#include "main.h"

#include "materials_workbench_window.h"

#include <algorithm>

#include <wx/button.h>
#include <wx/srchctrl.h>
#include <wx/simplebook.h>
#include <wx/splitter.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/settings.h>
#include <wx/textctrl.h>
#include <wx/treectrl.h>

#include "gui.h"
#include "materials_workbench_border_panel.h"
#include "materials_workbench_brush_panel.h"
#include "materials_workbench_inspector_dialog.h"
#include "materials_workbench_palette_panel.h"
#include "materials_workbench_wall_panel.h"

namespace {
	MaterialsWorkbenchWindow* g_materials_workbench_window = nullptr;

	void RefreshRuntimeMaterialPalettes(const char* reason) {
		wxString error;
		wxArrayString warnings;
		if (!g_gui.ReloadMaterialPalettesFromDatabase(error, warnings)) {
			spdlog::warn(
				"Materials Workbench runtime palette refresh failed after {}: {}",
				reason,
				error.ToStdString()
			);
		}
		for (const wxString &warning : warnings) {
			spdlog::warn(
				"Materials Workbench runtime palette refresh warning after {}: {}",
				reason,
				warning.ToStdString()
			);
		}
	}

	class MaterialsWorkbenchTreeItemData : public wxTreeItemData {
	public:
		MaterialsWorkbenchTreeItemData(MaterialsWorkbenchNodeKind kind, wxString contextKey, int itemIndex, wxString baseLabel) :
			kind(kind),
			contextKey(std::move(contextKey)),
			itemIndex(itemIndex),
			baseLabel(std::move(baseLabel)) {
		}

		MaterialsWorkbenchNodeKind kind;
		wxString contextKey;
		int itemIndex = -1;
		wxString baseLabel;
	};

	wxString BuildNavigationNodeKey(MaterialsWorkbenchNodeKind kind, const wxString &contextKey, int itemIndex) {
		return wxString::Format("%d|%s|%d", static_cast<int>(kind), contextKey, itemIndex);
	}

	MaterialsWorkbenchWindow::NavigationTreeState CaptureNavigationTreeState(wxTreeCtrl* tree) {
		MaterialsWorkbenchWindow::NavigationTreeState state;
		if (!tree) {
			return state;
		}
		state.valid = true;

		const wxTreeItemId selection = tree->GetSelection();
		if (selection.IsOk()) {
			if (auto* itemData = dynamic_cast<MaterialsWorkbenchTreeItemData*>(tree->GetItemData(selection))) {
				state.hasSelection = true;
				state.selectedKind = itemData->kind;
				state.selectedContextKey = itemData->contextKey;
				state.selectedItemIndex = itemData->itemIndex;
			}
		}

		const wxTreeItemId firstVisible = tree->GetFirstVisibleItem();
		if (firstVisible.IsOk()) {
			if (auto* itemData = dynamic_cast<MaterialsWorkbenchTreeItemData*>(tree->GetItemData(firstVisible))) {
				state.firstVisibleNodeKey = BuildNavigationNodeKey(itemData->kind, itemData->contextKey, itemData->itemIndex);
			}
		}

		const auto captureExpanded = [&](auto &&self, const wxTreeItemId &parent) -> void {
			wxTreeItemIdValue cookie;
			for (wxTreeItemId child = tree->GetFirstChild(parent, cookie); child.IsOk(); child = tree->GetNextChild(parent, cookie)) {
				if (tree->IsExpanded(child)) {
					if (auto* itemData = dynamic_cast<MaterialsWorkbenchTreeItemData*>(tree->GetItemData(child))) {
						state.expandedNodeKeys.push_back(BuildNavigationNodeKey(itemData->kind, itemData->contextKey, itemData->itemIndex));
					}
				}
				self(self, child);
			}
		};

		const wxTreeItemId root = tree->GetRootItem();
		if (root.IsOk()) {
			captureExpanded(captureExpanded, root);
		}

		return state;
	}

	bool NavigationStateContainsExpandedKey(const MaterialsWorkbenchWindow::NavigationTreeState &state, const wxString &key) {
		return std::find(state.expandedNodeKeys.begin(), state.expandedNodeKeys.end(), key) != state.expandedNodeKeys.end();
	}

	wxString NormalizeNavigationFilterQuery(const wxString &value) {
		wxString normalized = value;
		normalized.Trim(true);
		normalized.Trim(false);
		return normalized.Lower();
	}

	bool NavigationLabelMatchesFilter(const wxString &label, const wxString &normalizedQuery) {
		if (normalizedQuery.IsEmpty()) {
			return true;
		}
		return label.Lower().Find(normalizedQuery) != wxNOT_FOUND;
	}

	bool BuildFilteredNavigationNode(
		const MaterialsWorkbenchTreeNode &source,
		const wxString &normalizedQuery,
		MaterialsWorkbenchTreeNode &outNode
	) {
		const bool selfMatches = NavigationLabelMatchesFilter(source.label, normalizedQuery);
		if (selfMatches) {
			outNode = source;
			return true;
		}

		outNode = source;
		outNode.children.clear();
		for (const MaterialsWorkbenchTreeNode &child : source.children) {
			MaterialsWorkbenchTreeNode filteredChild;
			if (BuildFilteredNavigationNode(child, normalizedQuery, filteredChild)) {
				outNode.children.push_back(std::move(filteredChild));
			}
		}
		return !outNode.children.empty();
	}

	std::vector<MaterialsWorkbenchTreeNode> BuildFilteredNavigationTree(
		const std::vector<MaterialsWorkbenchTreeNode> &nodes,
		const wxString &normalizedQuery
	) {
		if (normalizedQuery.IsEmpty()) {
			return nodes;
		}

		std::vector<MaterialsWorkbenchTreeNode> filteredNodes;
		filteredNodes.reserve(nodes.size());
		for (const MaterialsWorkbenchTreeNode &node : nodes) {
			MaterialsWorkbenchTreeNode filteredNode;
			if (BuildFilteredNavigationNode(node, normalizedQuery, filteredNode)) {
				filteredNodes.push_back(std::move(filteredNode));
			}
		}
		return filteredNodes;
	}

	bool FindNavigationNodeByKeyRecursive(
		wxTreeCtrl* tree,
		const wxTreeItemId &parent,
		const wxString &targetKey,
		wxTreeItemId &outItem
	) {
		wxTreeItemIdValue cookie;
		for (wxTreeItemId child = tree->GetFirstChild(parent, cookie); child.IsOk(); child = tree->GetNextChild(parent, cookie)) {
			if (auto* itemData = dynamic_cast<MaterialsWorkbenchTreeItemData*>(tree->GetItemData(child))) {
				if (BuildNavigationNodeKey(itemData->kind, itemData->contextKey, itemData->itemIndex) == targetKey) {
					outItem = child;
					return true;
				}
			}
			if (FindNavigationNodeByKeyRecursive(tree, child, targetKey, outItem)) {
				return true;
			}
		}
		return false;
	}

	wxPanel* CreateSidebarPanel(wxWindow* parent, wxSearchCtrl*& outFilter, wxTreeCtrl*& outTree, wxButton*& outInspectorButton) {
		wxPanel* panel = new wxPanel(parent, wxID_ANY);
		wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

		wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);
		wxStaticText* title = new wxStaticText(panel, wxID_ANY, "Catalog");
		outInspectorButton = new wxButton(panel, wxID_ANY, "Inspector");
		headerSizer->Add(title, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, panel->FromDIP(8));
		headerSizer->AddStretchSpacer(1);
		headerSizer->Add(outInspectorButton, 0, wxALIGN_CENTER_VERTICAL);

		wxStaticText* subtitle = new wxStaticText(panel, wxID_ANY, "Palette categories organize palettes. Brushes define behavior.");
		subtitle->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
		outFilter = new wxSearchCtrl(panel, wxID_ANY);
		outFilter->ShowSearchButton(false);
		outFilter->ShowCancelButton(true);
		outFilter->SetDescriptiveText("Filter catalog");
		outTree = new wxTreeCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_SINGLE);

		sizer->Add(headerSizer, 0, wxEXPAND | wxALL, panel->FromDIP(8));
		sizer->Add(subtitle, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, panel->FromDIP(8));
		sizer->Add(outFilter, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, panel->FromDIP(8));
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

	bool SelectMatchingNodeRecursive(
		wxTreeCtrl* tree,
		const wxTreeItemId &parent,
		MaterialsWorkbenchNodeKind kind,
		const wxString &contextKey,
		int itemIndex
	) {
		wxTreeItemIdValue cookie;
		for (wxTreeItemId child = tree->GetFirstChild(parent, cookie); child.IsOk(); child = tree->GetNextChild(parent, cookie)) {
			auto* itemData = dynamic_cast<MaterialsWorkbenchTreeItemData*>(tree->GetItemData(child));
			if (itemData && itemData->kind == kind && itemData->contextKey == contextKey && itemData->itemIndex == itemIndex) {
				tree->SelectItem(child);
				return true;
			}
			if (SelectMatchingNodeRecursive(tree, child, kind, contextKey, itemIndex)) {
				return true;
			}
		}
		return false;
	}

	wxString BuildNavigationItemTooltip(MaterialsWorkbenchController &controller, const MaterialsWorkbenchTreeItemData &itemData) {
		if (itemData.kind == MaterialsWorkbenchNodeKind::Group) {
			if (itemData.contextKey == "group:catalog") {
				return "Catalog\nOpen the Workbench overview.\nClick the label to expand or collapse.";
			}
			if (itemData.contextKey == "group:palettes") {
				return "Palette Categories\nBrowse categories, then open a palette to edit entries.\nClick the label to expand or collapse.";
			}
			if (itemData.contextKey.StartsWith("palette_group:")) {
				return "Palette Category\nBrowse palettes in this category.\nClick the label to expand or collapse.";
			}
			if (itemData.contextKey == "group:brushes") {
				return "Brushes\nOpen brush domains and palette buckets.\nClick the label to expand or collapse.";
			}
			if (itemData.contextKey.StartsWith("brush_family:")) {
				return "Brush Family\nNarrow the brush list to one authoring domain.\nClick the label to expand or collapse.";
			}
			if (itemData.contextKey.StartsWith("brush_palette:")) {
				return "Brush Bucket\nOpen brushes aligned with this palette context.\nClick the label to expand or collapse.";
			}
			if (itemData.contextKey == "group:specialized") {
				return "Specialized Editors\nOpen borders and walls.\nClick the label to expand or collapse.";
			}
			if (itemData.contextKey == "group:borders") {
				return "Borders\nOpen border scopes and border sets.\nClick the label to expand or collapse.";
			}
			if (itemData.contextKey.StartsWith("border_scope:")) {
				return "Border Scope\nOpen border sets in this scope.\nClick the label to expand or collapse.";
			}
			if (itemData.contextKey == "group:walls") {
				return "Walls\nOpen wall brushes.\nClick the label to expand or collapse.";
			}
			return itemData.baseLabel;
		}

		if (itemData.kind == MaterialsWorkbenchNodeKind::Tileset) {
			TilesetStorageRecord tileset;
			if (controller.GetTilesetByIndex(itemData.itemIndex, tileset)) {
				wxString text;
				text << "Palette\n";
				text << "Name: " << tileset.name << "\n";
				text << "Category: " << (tileset.paletteGroupName.IsEmpty() ? wxString("other") : tileset.paletteGroupName) << "\n";
				text << "Storage: " << (tileset.sourceFile.IsEmpty() ? wxString("materials.db") : tileset.sourceFile) << "\n";
				text << "Action: Open Palette Workspace.";
				return text;
			}
			return "Palette\nOpen Palette Workspace.";
		}

		if (itemData.kind == MaterialsWorkbenchNodeKind::Brush) {
			if (itemData.contextKey == "wall") {
				return "Wall Brush\nOpen Wall Workspace to edit wall-specific composition and metadata.";
			}
			return "Brush\nOpen Brush Workspace to edit behavior, IDs, and metadata.";
		}

		if (itemData.kind == MaterialsWorkbenchNodeKind::BorderSet) {
			return "Border Set\nOpen Border Workspace to edit slots, items, and runtime-facing identifiers.";
		}

		return itemData.baseLabel;
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

void MaterialsWorkbenchWindow::OpenSqliteInspector(wxWindow* parent) {
	MaterialsWorkbenchWindow::Open(parent);
	if (!g_materials_workbench_window) {
		return;
	}
	g_materials_workbench_window->OpenInspector();
	if (g_materials_workbench_window->inspectorDialog_) {
		g_materials_workbench_window->inspectorDialog_->SelectSqliteTab();
	}
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
	rootSplitter->SetMinimumPaneSize(FromDIP(120));

	wxPanel* sidebarPanel = CreateSidebarPanel(rootSplitter, navigationFilterCtrl_, navigationTree_, inspectorButton_);
	workspaceBook_ = new wxSimplebook(rootSplitter, wxID_ANY);
	wxPanel* overviewPanel = CreateOverviewTextPanel(workspaceBook_, controller_, overviewText_);
	palettePanel_ = new MaterialsWorkbenchPalettePanel(workspaceBook_, controller_);
	palettePanel_->SetOnPaletteSaved([this](const wxString &paletteName) {
		CallAfter([this, paletteName]() {
			HandlePaletteSaved(paletteName);
		});
	});
	borderPanel_ = new MaterialsWorkbenchBorderPanel(workspaceBook_, controller_);
	borderPanel_->SetOnBorderSetStateChanged([this]() {
		UpdateBrushNavigationBadge();
		RefreshInspectorForCurrentSelection();
	});
	borderPanel_->SetOnBorderSetSaved([this](int64_t borderSetId) {
		CallAfter([this, borderSetId]() {
			HandleBorderSetSaved(borderSetId);
		});
	});
	borderPanel_->SetOnBorderSetDeleted([this](int64_t borderSetId, const wxString &scope) {
		CallAfter([this, borderSetId, scope]() {
			HandleBorderSetDeleted(borderSetId, scope);
		});
	});
	borderPanel_->SetOnOpenLinkedBrush([this](int64_t brushId) {
		wxString contextKey;
		int itemIndex = -1;
		if (controller_.LocateBrushNode(brushId, contextKey, itemIndex)) {
			SelectNavigationNode(MaterialsWorkbenchNodeKind::Brush, contextKey, itemIndex);
		}
	});
	brushPanel_ = new MaterialsWorkbenchBrushPanel(workspaceBook_, controller_);
	brushPanel_->SetOnBrushStateChanged([this]() {
		UpdateBrushNavigationBadge();
		RefreshInspectorForCurrentSelection();
	});
	brushPanel_->SetOnBrushSaved([this](int64_t brushId, const wxString &oldName, const wxString &newName) {
		HandleBrushSaved(brushId, oldName, newName);
	});
	brushPanel_->SetOnBrushDeleted([this](int64_t brushId) {
		CallAfter([this, brushId]() {
			HandleBrushDeleted(brushId);
		});
	});
	brushPanel_->SetOnOpenLinkedBrush([this](int64_t brushId) {
		wxString contextKey;
		int itemIndex = -1;
		if (controller_.LocateBrushNode(brushId, contextKey, itemIndex)) {
			SelectNavigationNode(MaterialsWorkbenchNodeKind::Brush, contextKey, itemIndex);
		}
	});
	brushPanel_->SetOnOpenLinkedTileset([this](const wxString &paletteName) {
		int itemIndex = -1;
		if (controller_.LocateTilesetNode(paletteName, itemIndex)) {
			SelectNavigationNode(MaterialsWorkbenchNodeKind::Tileset, "", itemIndex);
		}
	});
	wallPanel_ = new MaterialsWorkbenchWallPanel(workspaceBook_, controller_);
	wallPanel_->SetOnWallBrushStateChanged([this]() {
		UpdateBrushNavigationBadge();
	});
	wallPanel_->SetOnWallBrushSaved([this](int64_t brushId) {
		CallAfter([this, brushId]() {
			HandleWallBrushSaved(brushId);
		});
	});
	workspaceBook_->AddPage(overviewPanel, "Overview");
	workspaceBook_->AddPage(palettePanel_, "Palette");
	workspaceBook_->AddPage(borderPanel_, "Border");
	workspaceBook_->AddPage(brushPanel_, "Brush");
	workspaceBook_->AddPage(wallPanel_, "Wall");
	workspaceBook_->SetSelection(0);
	rootSplitter->SplitVertically(sidebarPanel, workspaceBook_, FromDIP(120));

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(rootSplitter, 1, wxEXPAND);
	SetSizer(sizer);
}

void MaterialsWorkbenchWindow::RefreshWorkbenchState() {
	controller_.ReloadCatalog();
	overviewText_->SetValue(controller_.GetOverviewText());
}

void MaterialsWorkbenchWindow::RefreshInspectorForCurrentSelection() {
	// The side inspector was removed from the Workbench layout.
}

void MaterialsWorkbenchWindow::OpenInspector() {
	if (inspectorDialog_ && inspectorDialog_->IsShown()) {
		inspectorDialog_->Raise();
		inspectorDialog_->SetFocus();
		return;
	}

	inspectorDialog_ = new MaterialsWorkbenchInspectorDialog(this, [this](const wxString &entityKind, int64_t entityId, const wxString &entityName) {
		return GoToEntity(entityKind, entityId, entityName);
	});

	inspectorDialog_->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) {
		if (!inspectorDialog_) {
			return;
		}
		MaterialsWorkbenchInspectorDialog* dialog = inspectorDialog_;
		inspectorDialog_ = nullptr;
		dialog->Destroy();
	});

	inspectorDialog_->Show();
	inspectorDialog_->Raise();
}

bool MaterialsWorkbenchWindow::GoToEntity(const wxString &entityKind, int64_t entityId, const wxString &entityName) {
	wxString contextKey;
	int itemIndex = -1;

	if (entityKind == "brush" || entityKind == "wall_brush") {
		if (entityId > 0 && controller_.LocateBrushNode(entityId, contextKey, itemIndex)) {
			return SelectNavigationNode(MaterialsWorkbenchNodeKind::Brush, contextKey, itemIndex);
		}
		return false;
	}

	if (entityKind == "border_set") {
		if (entityId > 0 && controller_.LocateBorderSetNode(entityId, contextKey, itemIndex)) {
			return SelectNavigationNode(MaterialsWorkbenchNodeKind::BorderSet, contextKey, itemIndex);
		}
		return false;
	}

	if (entityKind == "palette") {
		int tilesetIndex = -1;
		if (!entityName.IsEmpty() && controller_.LocateTilesetNode(entityName, tilesetIndex)) {
			return SelectNavigationNode(MaterialsWorkbenchNodeKind::Tileset, "tilesets", tilesetIndex);
		}
		return false;
	}

	return false;
}

void MaterialsWorkbenchWindow::HandlePaletteSaved(const wxString &paletteName) {
	RefreshRuntimeMaterialPalettes("palette save");
	RefreshWorkbenchState();
	PopulateNavigation();
	if (!paletteName.IsEmpty()) {
		int tilesetIndex = -1;
		if (controller_.LocateTilesetNode(paletteName, tilesetIndex)) {
			SelectNavigationNode(MaterialsWorkbenchNodeKind::Tileset, "tilesets", tilesetIndex);
		}
	}
	RefreshInspectorForCurrentSelection();
}

void MaterialsWorkbenchWindow::HandleBorderSetSaved(int64_t borderSetId) {
	wxArrayString runtimeWarnings;
	wxString runtimeError;
	if (!g_brushes.reloadBorderSetFromDatabase(borderSetId, runtimeWarnings, runtimeError)) {
		spdlog::warn(
			"Materials Workbench runtime border refresh failed after border save: id={} error='{}'",
			static_cast<long long>(borderSetId),
			runtimeError.ToStdString()
		);
	}
	for (const wxString &warning : runtimeWarnings) {
		spdlog::warn(
			"Materials Workbench runtime border refresh warning after border save: id={} warning='{}'",
			static_cast<long long>(borderSetId),
			warning.ToStdString()
		);
	}

	RefreshRuntimeMaterialPalettes("border set save");
	RefreshWorkbenchState();
	PopulateNavigation();

	wxString contextKey;
	int itemIndex = -1;
	if (controller_.LocateBorderSetNode(borderSetId, contextKey, itemIndex)) {
		SelectNavigationNode(MaterialsWorkbenchNodeKind::BorderSet, contextKey, itemIndex);
	}
	RefreshInspectorForCurrentSelection();
}

void MaterialsWorkbenchWindow::HandleBorderSetDeleted(int64_t borderSetId, const wxString &scope) {
	RefreshRuntimeMaterialPalettes("border set delete");
	RefreshWorkbenchState();
	PopulateNavigation();
	if (!scope.IsEmpty()) {
		SelectNavigationNode(MaterialsWorkbenchNodeKind::Group, "border_scope:" + scope, -1);
	}
	RefreshInspectorForCurrentSelection();
}

void MaterialsWorkbenchWindow::HandleBrushSaved(int64_t brushId, const wxString &oldName, const wxString &newName) {
	wxString contextKey;
	int itemIndex = -1;
	uint16_t effectiveLookId = 0;
	if (controller_.LocateBrushNode(brushId, contextKey, itemIndex)) {
		BrushStorageRecord savedBrush;
		wxString error;
		if (controller_.GetBrushDetails(contextKey, itemIndex, savedBrush, error)) {
			effectiveLookId = static_cast<uint16_t>(savedBrush.brush.serverLookId > 0 ? savedBrush.brush.serverLookId : savedBrush.brush.lookId);
		} else {
			spdlog::warn("Materials Workbench could not reload saved brush metadata for palette sync: {}", error.ToStdString());
		}
	}

	if (oldName != newName) {
		if (Brush* runtimeBrush = g_brushes.getBrush(oldName.ToStdString())) {
			g_brushes.renameBrush(runtimeBrush, oldName.ToStdString(), newName.ToStdString());
		}
	}

	wxArrayString warnings;
	wxString reloadError;
	if (!g_brushes.reloadBrushFromDatabase(brushId, warnings, reloadError)) {
		spdlog::warn(
			"Materials Workbench runtime brush refresh failed after brush save: id={} error='{}'",
			static_cast<long long>(brushId),
			reloadError.ToStdString()
		);
	}
	for (const wxString &warning : warnings) {
		spdlog::warn(
			"Materials Workbench runtime brush refresh warning after brush save: id={} warning='{}'",
			static_cast<long long>(brushId),
			warning.ToStdString()
		);
	}

	if (!g_gui.SyncBrushInPalettes(oldName, newName, effectiveLookId)) {
		spdlog::warn(
			"Materials Workbench runtime brush palette sync skipped: old='{}' new='{}' lookId={}",
			oldName.ToStdString(),
			newName.ToStdString(),
			effectiveLookId
		);
	}

	RefreshWorkbenchState();
	PopulateNavigation();

	if (controller_.LocateBrushNode(brushId, contextKey, itemIndex)) {
		SelectNavigationNode(MaterialsWorkbenchNodeKind::Brush, contextKey, itemIndex);
	}
	RefreshInspectorForCurrentSelection();
}

void MaterialsWorkbenchWindow::HandleBrushDeleted(int64_t brushId) {
	RefreshRuntimeMaterialPalettes("brush delete");
	RefreshWorkbenchState();
	PopulateNavigation();
	SelectNavigationNode(MaterialsWorkbenchNodeKind::Group, "group:brushes", -1);
	RefreshInspectorForCurrentSelection();
	spdlog::info("Materials Workbench deleted brush and refreshed navigation: id={}", static_cast<long long>(brushId));
}

void MaterialsWorkbenchWindow::HandleWallBrushSaved(int64_t brushId) {
	wxArrayString warnings;
	wxString error;
	if (!g_brushes.reloadBrushFromDatabase(brushId, warnings, error)) {
		spdlog::warn(
			"Materials Workbench runtime wall brush refresh failed after wall save: id={} error='{}'",
			static_cast<long long>(brushId),
			error.ToStdString()
		);
	}
	for (const wxString &warning : warnings) {
		spdlog::warn(
			"Materials Workbench runtime wall brush refresh warning after wall save: id={} warning='{}'",
			static_cast<long long>(brushId),
			warning.ToStdString()
		);
	}

	RefreshWorkbenchState();
	PopulateNavigation();

	wxString contextKey;
	int itemIndex = -1;
	if (controller_.LocateBrushNode(brushId, contextKey, itemIndex)) {
		SelectNavigationNode(MaterialsWorkbenchNodeKind::Brush, contextKey, itemIndex);
	}
	RefreshInspectorForCurrentSelection();
}

void MaterialsWorkbenchWindow::UpdateBrushNavigationBadge() {
	if (!navigationTree_) {
		return;
	}

	const wxTreeItemId root = navigationTree_->GetRootItem();
	if (!root.IsOk()) {
		return;
	}

	const bool hasDirtyBrush = brushPanel_ && brushPanel_->HasPendingChanges();
	const wxString dirtyBrushName = hasDirtyBrush ? brushPanel_->GetCurrentBrushDisplayName() : "";
	const bool hasDirtyBorder = borderPanel_ && borderPanel_->HasPendingChanges();
	const wxString dirtyBorderName = hasDirtyBorder ? borderPanel_->GetCurrentBorderSetDisplayName() : "";
	const bool hasDirtyWall = wallPanel_ && wallPanel_->HasPendingChanges();
	const wxString dirtyWallName = hasDirtyWall ? wallPanel_->GetCurrentWallDisplayName() : "";
	const wxColour defaultTextColour = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
	const wxColour modifiedTextColour(176, 102, 0);

	const auto applyBadge = [&](auto &&self, const wxTreeItemId &parentItem) -> void {
		wxTreeItemIdValue cookie;
		for (wxTreeItemId child = navigationTree_->GetFirstChild(parentItem, cookie); child.IsOk(); child = navigationTree_->GetNextChild(parentItem, cookie)) {
			auto* itemData = dynamic_cast<MaterialsWorkbenchTreeItemData*>(navigationTree_->GetItemData(child));
			if (itemData) {
				const bool isModifiedBrush = hasDirtyBrush &&
					itemData->kind == MaterialsWorkbenchNodeKind::Brush &&
					brushPanel_->IsCurrentBrushSelection(itemData->contextKey, itemData->itemIndex);
				const bool isModifiedWall = hasDirtyWall &&
					itemData->kind == MaterialsWorkbenchNodeKind::Brush &&
					wallPanel_->IsCurrentWallSelection(itemData->contextKey, itemData->itemIndex);
				const bool isModifiedBorder = hasDirtyBorder &&
					itemData->kind == MaterialsWorkbenchNodeKind::BorderSet &&
					borderPanel_->IsCurrentBorderSelection(itemData->contextKey, itemData->itemIndex);
				const bool isModified = isModifiedBrush || isModifiedBorder || isModifiedWall;
				const wxString displayLabel =
					isModifiedBrush && !dirtyBrushName.IsEmpty() ? dirtyBrushName :
					(isModifiedBorder && !dirtyBorderName.IsEmpty() ? dirtyBorderName :
					(isModifiedWall && !dirtyWallName.IsEmpty() ? dirtyWallName : itemData->baseLabel));
				navigationTree_->SetItemText(child, isModified ? displayLabel + " [modified]" : itemData->baseLabel);
				navigationTree_->SetItemTextColour(child, isModified ? modifiedTextColour : defaultTextColour);
			}

			self(self, child);
		}
	};

	applyBadge(applyBadge, root);
}

void MaterialsWorkbenchWindow::PopulateNavigation() {
	const wxString normalizedFilterQuery = NormalizeNavigationFilterQuery(navigationFilterQuery_);
	const bool filterActive = !normalizedFilterQuery.IsEmpty();

	NavigationTreeState previousState;
	if (!filterActive && navigationFilterActive_ && navigationStateBeforeFilter_.valid) {
		previousState = navigationStateBeforeFilter_;
		navigationStateBeforeFilter_ = NavigationTreeState();
	} else {
		previousState = CaptureNavigationTreeState(navigationTree_);
		if (!navigationFilterActive_ && filterActive) {
			navigationStateBeforeFilter_ = previousState;
		}
	}
	navigationFilterActive_ = filterActive;

	navigationTree_->DeleteAllItems();
	wxTreeItemId root = navigationTree_->AddRoot("Materials Workbench");
	const std::vector<MaterialsWorkbenchTreeNode> filteredNodes =
		BuildFilteredNavigationTree(controller_.BuildNavigationTree(), normalizedFilterQuery);
	const auto appendNodes = [&](auto &&self, const wxTreeItemId &parentItem, const std::vector<MaterialsWorkbenchTreeNode> &nodes) -> void {
		for (const MaterialsWorkbenchTreeNode &node : nodes) {
			wxTreeItemId item = navigationTree_->AppendItem(
				parentItem,
				node.label,
				-1,
				-1,
				newd MaterialsWorkbenchTreeItemData(node.kind, node.contextKey, node.itemIndex, node.label)
			);
			if (!node.children.empty()) {
				self(self, item, node.children);
			}

			if (filterActive ||
				NavigationStateContainsExpandedKey(previousState, BuildNavigationNodeKey(node.kind, node.contextKey, node.itemIndex))) {
				navigationTree_->Expand(item);
			}
		}
	};

	appendNodes(appendNodes, root, filteredNodes);

	if (filterActive && filteredNodes.empty()) {
		navigationTree_->AppendItem(root, wxString::Format("No matches for \"%s\".", navigationFilterQuery_));
	}

	if (!previousState.hasSelection ||
		!SelectNavigationNode(previousState.selectedKind, previousState.selectedContextKey, previousState.selectedItemIndex)) {
		wxTreeItemIdValue cookie;
		wxTreeItemId firstChild = navigationTree_->GetFirstChild(root, cookie);
		if (firstChild.IsOk()) {
			navigationTree_->SelectItem(firstChild);
		}
	}

	if (!previousState.firstVisibleNodeKey.IsEmpty()) {
		wxTreeItemId firstVisibleItem;
		if (FindNavigationNodeByKeyRecursive(navigationTree_, root, previousState.firstVisibleNodeKey, firstVisibleItem)) {
			navigationTree_->ScrollTo(firstVisibleItem);
		}
	}

	UpdateBrushNavigationBadge();
}

void MaterialsWorkbenchWindow::BindEvents() {
	if (inspectorButton_) {
		inspectorButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			OpenInspector();
		});
	}

	if (navigationFilterCtrl_) {
		navigationFilterCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent &event) {
			navigationFilterQuery_ = event.GetString();
			PopulateNavigation();
		});
		navigationFilterCtrl_->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN, [this](wxCommandEvent &WXUNUSED(event)) {
			navigationFilterQuery_.clear();
			navigationFilterCtrl_->ChangeValue("");
			PopulateNavigation();
		});
	}

	navigationTree_->Bind(wxEVT_MOTION, [this](wxMouseEvent &event) {
		int flags = 0;
		const wxTreeItemId item = navigationTree_->HitTest(event.GetPosition(), flags);
		if (!item.IsOk()) {
			if (!hoveredNavigationTooltipKey_.IsEmpty()) {
				hoveredNavigationTooltipKey_.clear();
				navigationTree_->UnsetToolTip();
			}
			event.Skip();
			return;
		}

		auto* itemData = dynamic_cast<MaterialsWorkbenchTreeItemData*>(navigationTree_->GetItemData(item));
		if (!itemData) {
			if (!hoveredNavigationTooltipKey_.IsEmpty()) {
				hoveredNavigationTooltipKey_.clear();
				navigationTree_->UnsetToolTip();
			}
			event.Skip();
			return;
		}

		const wxString tooltipKey = BuildNavigationNodeKey(itemData->kind, itemData->contextKey, itemData->itemIndex);
		if (tooltipKey != hoveredNavigationTooltipKey_) {
			hoveredNavigationTooltipKey_ = tooltipKey;
			navigationTree_->SetToolTip(BuildNavigationItemTooltip(controller_, *itemData));
		}

		event.Skip();
	});

	navigationTree_->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &event) {
		int flags = 0;
		const wxTreeItemId item = navigationTree_->HitTest(event.GetPosition(), flags);
		const bool clickedLabel = item.IsOk() && (flags & (wxTREE_HITTEST_ONITEMLABEL | wxTREE_HITTEST_ONITEMICON));
		if (!clickedLabel || !navigationTree_->ItemHasChildren(item)) {
			event.Skip();
			return;
		}

		auto* itemData = dynamic_cast<MaterialsWorkbenchTreeItemData*>(navigationTree_->GetItemData(item));
		if (!itemData) {
			event.Skip();
			return;
		}

		const wxString targetKey = BuildNavigationNodeKey(itemData->kind, itemData->contextKey, itemData->itemIndex);
		event.Skip();
		CallAfter([this, targetKey]() {
			if (!navigationTree_) {
				return;
			}

			const wxTreeItemId root = navigationTree_->GetRootItem();
			if (!root.IsOk()) {
				return;
			}

			wxTreeItemId targetItem;
			if (!FindNavigationNodeByKeyRecursive(navigationTree_, root, targetKey, targetItem)) {
				return;
			}

			if (!targetItem.IsOk() || navigationTree_->GetSelection() != targetItem || !navigationTree_->ItemHasChildren(targetItem)) {
				return;
			}

			if (navigationTree_->IsExpanded(targetItem)) {
				navigationTree_->Collapse(targetItem);
			} else {
				navigationTree_->Expand(targetItem);
			}
		});
	});

	navigationTree_->Bind(wxEVT_TREE_SEL_CHANGING, [this](wxTreeEvent &event) {
		const wxTreeItemId item = event.GetItem();
		if (!item.IsOk()) {
			return;
		}

		auto* itemData = dynamic_cast<MaterialsWorkbenchTreeItemData*>(navigationTree_->GetItemData(item));
		const bool isSameBrushSelection =
			itemData &&
			itemData->kind == MaterialsWorkbenchNodeKind::Brush &&
			brushPanel_->IsCurrentBrushSelection(itemData->contextKey, itemData->itemIndex);
		const bool isSameBorderSelection =
			itemData &&
			itemData->kind == MaterialsWorkbenchNodeKind::BorderSet &&
			borderPanel_->IsCurrentBorderSelection(itemData->contextKey, itemData->itemIndex);
		const bool isSameWallSelection =
			itemData &&
			itemData->kind == MaterialsWorkbenchNodeKind::Brush &&
			wallPanel_->IsCurrentWallSelection(itemData->contextKey, itemData->itemIndex);

		if (brushPanel_->HasPendingChanges() && !isSameBrushSelection) {
			if (!brushPanel_->ResolvePendingChangesBeforeSwitch(this, navigationTree_->GetItemText(item))) {
				event.Veto();
			}
			return;
		}

		if (borderPanel_->HasPendingChanges() && !isSameBorderSelection) {
			if (!borderPanel_->ResolvePendingChangesBeforeSwitch(this, navigationTree_->GetItemText(item))) {
				event.Veto();
			}
			return;
		}

		if (wallPanel_->HasPendingChanges() && !isSameWallSelection) {
			if (!wallPanel_->ResolvePendingChangesBeforeSwitch(this, navigationTree_->GetItemText(item))) {
				event.Veto();
			}
		}
	});

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
			return;
		}

		overviewText_->SetValue(controller_.BuildSelectionOverview(itemData->kind, itemData->contextKey, itemData->itemIndex));

		if (itemData->kind == MaterialsWorkbenchNodeKind::Tileset) {
			TilesetStorageRecord tileset;
			if (controller_.GetTilesetByIndex(itemData->itemIndex, tileset) && palettePanel_->LoadPalette(tileset)) {
				workspaceBook_->SetSelection(1);
				return;
			}
			palettePanel_->ClearWorkspace("Failed to load the selected palette workspace.");
		}

		if (itemData->kind == MaterialsWorkbenchNodeKind::Brush) {
			if (itemData->contextKey == "wall") {
				if (wallPanel_->LoadWallBrush(itemData->contextKey, itemData->itemIndex)) {
					workspaceBook_->SetSelection(4);
					return;
				}
				wallPanel_->ClearWorkspace("Failed to load the selected wall workspace.");
			}

			if (brushPanel_->LoadBrush(itemData->contextKey, itemData->itemIndex)) {
				workspaceBook_->SetSelection(3);
				return;
			}
			brushPanel_->ClearWorkspace("Failed to load the selected brush workspace.");
		}

		if (itemData->kind == MaterialsWorkbenchNodeKind::BorderSet) {
			if (borderPanel_->LoadBorderSet(itemData->contextKey, itemData->itemIndex)) {
				workspaceBook_->SetSelection(2);
				return;
			}
			borderPanel_->ClearWorkspace("Failed to load the selected border workspace.");
		}

		workspaceBook_->SetSelection(0);
	});
}

bool MaterialsWorkbenchWindow::SelectNavigationNode(MaterialsWorkbenchNodeKind kind, const wxString &contextKey, int itemIndex) {
	if (!navigationTree_) {
		return false;
	}

	const wxTreeItemId root = navigationTree_->GetRootItem();
	if (!root.IsOk()) {
		return false;
	}

	return SelectMatchingNodeRecursive(navigationTree_, root, kind, contextKey, itemIndex);
}

void MaterialsWorkbenchWindow::OnClose(wxCloseEvent &event) {
	if (event.CanVeto()) {
		if (brushPanel_ && brushPanel_->HasPendingChanges() &&
			!brushPanel_->ResolvePendingChangesBeforeSwitch(this, "closing the Materials Workbench window")) {
			event.Veto();
			return;
		}
		if (borderPanel_ && borderPanel_->HasPendingChanges() &&
			!borderPanel_->ResolvePendingChangesBeforeSwitch(this, "closing the Materials Workbench window")) {
			event.Veto();
			return;
		}
		if (wallPanel_ && wallPanel_->HasPendingChanges() &&
			!wallPanel_->ResolvePendingChangesBeforeSwitch(this, "closing the Materials Workbench window")) {
			event.Veto();
			return;
		}
	}

	if (inspectorDialog_) {
		MaterialsWorkbenchInspectorDialog* dialog = inspectorDialog_;
		inspectorDialog_ = nullptr;
		dialog->Destroy();
	}

	g_materials_workbench_window = nullptr;
	Destroy();
	event.Skip(false);
}
