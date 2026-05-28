#include "main.h"

#include "materials_workbench_window.h"

#include <algorithm>

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

	struct NavigationTreeState {
		bool hasSelection = false;
		MaterialsWorkbenchNodeKind selectedKind = MaterialsWorkbenchNodeKind::Group;
		wxString selectedContextKey;
		int selectedItemIndex = -1;
		wxString firstVisibleNodeKey;
		std::vector<wxString> expandedNodeKeys;
	};

	wxString BuildNavigationNodeKey(MaterialsWorkbenchNodeKind kind, const wxString &contextKey, int itemIndex) {
		return wxString::Format("%d|%s|%d", static_cast<int>(kind), contextKey, itemIndex);
	}

	NavigationTreeState CaptureNavigationTreeState(wxTreeCtrl* tree) {
		NavigationTreeState state;
		if (!tree) {
			return state;
		}

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

	bool NavigationStateContainsExpandedKey(const NavigationTreeState &state, const wxString &key) {
		return std::find(state.expandedNodeKeys.begin(), state.expandedNodeKeys.end(), key) != state.expandedNodeKeys.end();
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

	wxPanel* CreateSidebarPanel(wxWindow* parent, wxTreeCtrl*& outTree) {
		wxPanel* panel = new wxPanel(parent, wxID_ANY);
		wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

		wxStaticText* title = new wxStaticText(panel, wxID_ANY, "Catalog");
		wxStaticText* subtitle = new wxStaticText(panel, wxID_ANY, "Palette categories organize palettes. Brushes define behavior.");
		subtitle->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
		outTree = new wxTreeCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_SINGLE);

		sizer->Add(title, 0, wxEXPAND | wxALL, panel->FromDIP(8));
		sizer->Add(subtitle, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, panel->FromDIP(8));
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

	wxPanel* sidebarPanel = CreateSidebarPanel(rootSplitter, navigationTree_);
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
	brushPanel_ = new MaterialsWorkbenchBrushPanel(workspaceBook_, controller_);
	brushPanel_->SetOnBrushStateChanged([this]() {
		UpdateBrushNavigationBadge();
		RefreshInspectorForCurrentSelection();
	});
	brushPanel_->SetOnBrushSaved([this](int64_t brushId, const wxString &oldName, const wxString &newName) {
		HandleBrushSaved(brushId, oldName, newName);
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
	rootSplitter->SplitVertically(sidebarPanel, workspaceBook_, FromDIP(230));

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
	const NavigationTreeState previousState = CaptureNavigationTreeState(navigationTree_);
	navigationTree_->DeleteAllItems();
	wxTreeItemId root = navigationTree_->AddRoot("Materials Workbench");
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

			if (NavigationStateContainsExpandedKey(previousState, BuildNavigationNodeKey(node.kind, node.contextKey, node.itemIndex))) {
				navigationTree_->Expand(item);
			}
		}
	};

	appendNodes(appendNodes, root, controller_.BuildNavigationTree());

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

	g_materials_workbench_window = nullptr;
	Destroy();
	event.Skip(false);
}
