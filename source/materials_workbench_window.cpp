#include "main.h"

#include "materials_workbench_window.h"

#include <algorithm>
#include <functional>
#include <fstream>
#include <set>
#include <vector>

#include <wx/button.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/progdlg.h>
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
#include "materials_workbench_exchange.h"
#include "materials_workbench_exchange_dialog.h"
#include "materials_workbench_inspector_dialog.h"
#include "materials_workbench_palette_panel.h"
#include "materials_workbench_wall_panel.h"

namespace {
	MaterialsWorkbenchWindow*& GetMaterialsWorkbenchWindowInstance() {
		static MaterialsWorkbenchWindow* window = nullptr;
		return window;
	}

	void RefreshRuntimeMaterialPalettes(const char* reason, bool reloadBrushes) {
		wxString error;
		wxArrayString warnings;
		const bool ok = reloadBrushes
			? g_gui.ReloadMaterialPalettesAndBrushesFromDatabase(error, warnings)
			: g_gui.ReloadMaterialPalettesFromDatabase(error, warnings);
		if (!ok) {
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

		std::function<void(const wxTreeItemId&)> captureExpanded;
		captureExpanded = [&](const wxTreeItemId &parent) {
			wxTreeItemIdValue cookie;
			for (wxTreeItemId child = tree->GetFirstChild(parent, cookie); child.IsOk(); child = tree->GetNextChild(parent, cookie)) {
				if (tree->IsExpanded(child)) {
					if (auto* itemData = dynamic_cast<MaterialsWorkbenchTreeItemData*>(tree->GetItemData(child))) {
						state.expandedNodeKeys.push_back(BuildNavigationNodeKey(itemData->kind, itemData->contextKey, itemData->itemIndex));
					}
				}
				captureExpanded(child);
			}
		};

		const wxTreeItemId root = tree->GetRootItem();
		if (root.IsOk()) {
			captureExpanded(root);
		}

		return state;
	}

	bool NavigationStateContainsExpandedKey(const MaterialsWorkbenchWindow::NavigationTreeState &state, const wxString &key) {
		return std::ranges::find(state.expandedNodeKeys, key) != state.expandedNodeKeys.end();
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

	wxPanel* CreateSidebarPanel(wxWindow* parent, wxSearchCtrl*&outFilter, wxTreeCtrl*&outTree, wxButton*&outInspectorButton, wxButton*&outExportButton, wxButton*&outImportButton) {
		wxPanel* panel = new wxPanel(parent, wxID_ANY);
		wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

		wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);
		outInspectorButton = new wxButton(panel, wxID_ANY, "Inspector");
		outExportButton = new wxButton(panel, wxID_ANY, "Export");
		outImportButton = new wxButton(panel, wxID_ANY, "Import");
		const int compactHeight = panel->FromDIP(20);
		outInspectorButton->SetWindowVariant(wxWINDOW_VARIANT_SMALL);
		outExportButton->SetWindowVariant(wxWINDOW_VARIANT_SMALL);
		outImportButton->SetWindowVariant(wxWINDOW_VARIANT_SMALL);
		outInspectorButton->SetMinSize(wxSize(-1, compactHeight));
		outExportButton->SetMinSize(wxSize(-1, compactHeight));
		outImportButton->SetMinSize(wxSize(-1, compactHeight));

		headerSizer->AddStretchSpacer(1);
		headerSizer->Add(outExportButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, panel->FromDIP(2));
		headerSizer->Add(outImportButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, panel->FromDIP(10));
		headerSizer->Add(outInspectorButton, 0, wxALIGN_CENTER_VERTICAL);

		outFilter = new wxSearchCtrl(panel, wxID_ANY);
		outFilter->ShowSearchButton(false);
		outFilter->ShowCancelButton(true);
		outFilter->SetDescriptiveText("Filter catalog");
		outTree = new wxTreeCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_SINGLE);

		sizer->Add(headerSizer, 0, wxEXPAND | wxALL, panel->FromDIP(8));
		sizer->Add(outFilter, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, panel->FromDIP(8));
		sizer->Add(new wxStaticLine(panel), 0, wxEXPAND | wxLEFT | wxRIGHT, panel->FromDIP(8));
		sizer->Add(outTree, 1, wxEXPAND | wxALL, panel->FromDIP(8));
		panel->SetSizer(sizer);
		return panel;
	}

	wxPanel* CreateOverviewTextPanel(wxWindow* parent, const MaterialsWorkbenchController &controller, wxTextCtrl*&outOverview) {
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
	auto*& window = GetMaterialsWorkbenchWindowInstance();
	if (window) {
		if (window->IsIconized()) {
			window->Iconize(false);
		}
		window->Show();
		window->Raise();
		window->Maximize(true);
		return;
	}

	window = newd MaterialsWorkbenchWindow(parent);
	window->Show();
	window->Maximize(true);
	window->Raise();
}

void MaterialsWorkbenchWindow::OpenSqliteInspector(wxWindow* parent) {
	MaterialsWorkbenchWindow::Open(parent);
	auto*& window = GetMaterialsWorkbenchWindowInstance();
	if (!window) {
		return;
	}
	window->OpenInspector();
	if (window->inspectorDialog_) {
		window->inspectorDialog_->SelectSqliteTab();
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

	wxPanel* sidebarPanel = CreateSidebarPanel(rootSplitter, navigationFilterCtrl_, navigationTree_, inspectorButton_, exportButton_, importButton_);
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
		CallAfter([this, brushId]() {
			wxString contextKey;
			int itemIndex = -1;
			if (controller_.LocateBrushNode(brushId, contextKey, itemIndex)) {
				SelectNavigationNode(MaterialsWorkbenchNodeKind::Brush, contextKey, itemIndex);
			}
		});
	});
	brushPanel_ = new MaterialsWorkbenchBrushPanel(workspaceBook_, controller_);
	brushPanel_->SetOnBrushStateChanged([this]() {
		UpdateBrushNavigationBadge();
		RefreshInspectorForCurrentSelection();
	});
	brushPanel_->SetOnBrushSaved([this](int64_t brushId, const wxString &oldName, const wxString &newName) {
		CallAfter([this, brushId, oldName, newName]() {
			HandleBrushSaved(brushId, oldName, newName);
		});
	});
	brushPanel_->SetOnBrushDeleted([this](int64_t brushId) {
		CallAfter([this, brushId]() {
			HandleBrushDeleted(brushId);
		});
	});
	brushPanel_->SetOnOpenLinkedBrush([this](int64_t brushId) {
		CallAfter([this, brushId]() {
			wxString contextKey;
			int itemIndex = -1;
			if (controller_.LocateBrushNode(brushId, contextKey, itemIndex)) {
				SelectNavigationNode(MaterialsWorkbenchNodeKind::Brush, contextKey, itemIndex);
			}
		});
	});
	brushPanel_->SetOnOpenLinkedBorderSet([this](int64_t borderSetId) {
		CallAfter([this, borderSetId]() {
			wxString contextKey;
			int itemIndex = -1;
			if (controller_.LocateBorderSetNode(borderSetId, contextKey, itemIndex)) {
				SelectNavigationNode(MaterialsWorkbenchNodeKind::BorderSet, contextKey, itemIndex);
			}
		});
	});
	brushPanel_->SetOnOpenLinkedTileset([this](const wxString &paletteName) {
		const wxString paletteNameCopy = paletteName;
		CallAfter([this, paletteNameCopy]() {
			int itemIndex = -1;
			if (controller_.LocateTilesetNode(paletteNameCopy, itemIndex)) {
				SelectNavigationNode(MaterialsWorkbenchNodeKind::Tileset, "tilesets", itemIndex);
			}
		});
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
	wallPanel_->SetOnOpenLinkedBrush([this](int64_t brushId) {
		CallAfter([this, brushId]() {
			wxString contextKey;
			int itemIndex = -1;
			if (controller_.LocateBrushNode(brushId, contextKey, itemIndex)) {
				SelectNavigationNode(MaterialsWorkbenchNodeKind::Brush, contextKey, itemIndex);
			}
		});
	});
	wallPanel_->SetOnOpenLinkedBorderSet([this](int64_t borderSetId) {
		CallAfter([this, borderSetId]() {
			wxString contextKey;
			int itemIndex = -1;
			if (controller_.LocateBorderSetNode(borderSetId, contextKey, itemIndex)) {
				SelectNavigationNode(MaterialsWorkbenchNodeKind::BorderSet, contextKey, itemIndex);
			}
		});
	});
	wallPanel_->SetOnOpenLinkedTileset([this](const wxString &paletteName) {
		const wxString paletteNameCopy = paletteName;
		CallAfter([this, paletteNameCopy]() {
			int itemIndex = -1;
			if (controller_.LocateTilesetNode(paletteNameCopy, itemIndex)) {
				SelectNavigationNode(MaterialsWorkbenchNodeKind::Tileset, "tilesets", itemIndex);
			}
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

	inspectorDialog_->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) {
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

void MaterialsWorkbenchWindow::OnExportMaterials(wxCommandEvent &) {
	MaterialsWorkbenchExportDialog dialog(this, controller_);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	wxString error;
	const nlohmann::json root = BuildMaterialsWorkbenchExportJson(controller_, dialog.GetSelection(), error);
	if (!error.IsEmpty()) {
		wxMessageBox(error, "Export Materials", wxOK | wxICON_ERROR, this);
		return;
	}

	wxFileDialog fileDialog(
		this,
		"Export Materials",
		"",
		"materials.rme-materials.json",
		"RME Materials JSON (*.rme-materials.json)|*.rme-materials.json|JSON (*.json)|*.json|All files (*.*)|*.*",
		wxFD_SAVE | wxFD_OVERWRITE_PROMPT
	);
	if (fileDialog.ShowModal() != wxID_OK) {
		return;
	}

	const wxString path = fileDialog.GetPath();
	wxCharBuffer utf8 = path.ToUTF8();
	std::ofstream out(utf8.data(), std::ios::binary | std::ios::trunc);
	if (!out.is_open()) {
		wxMessageBox("Failed to write the export file.", "Export Materials", wxOK | wxICON_ERROR, this);
		return;
	}

	out << root.dump(2);
	out.close();
	wxMessageBox("Exported materials to JSON.", "Export Materials", wxOK | wxICON_INFORMATION, this);
}

void MaterialsWorkbenchWindow::OnImportMaterials(wxCommandEvent &) {
	if ((borderPanel_ && borderPanel_->HasPendingChanges()) || (brushPanel_ && brushPanel_->HasPendingChanges()) || (wallPanel_ && wallPanel_->HasPendingChanges())) {
		const int result = wxMessageBox(
			"You have unsaved changes in an editor. Import can overwrite materials.db data.\n\nSave or revert your changes before importing.\n\nContinue anyway?",
			"Import Materials",
			wxYES_NO | wxICON_WARNING,
			this
		);
		if (result != wxYES) {
			return;
		}
	}

	wxFileDialog fileDialog(
		this,
		"Import Materials",
		"",
		"",
		"RME Materials JSON (*.rme-materials.json)|*.rme-materials.json|JSON (*.json)|*.json|All files (*.*)|*.*",
		wxFD_OPEN | wxFD_FILE_MUST_EXIST
	);
	if (fileDialog.ShowModal() != wxID_OK) {
		return;
	}

	const wxString path = fileDialog.GetPath();
	wxCharBuffer utf8 = path.ToUTF8();
	std::ifstream in(utf8.data(), std::ios::binary);
	if (!in.is_open()) {
		wxMessageBox("Failed to open the import file.", "Import Materials", wxOK | wxICON_ERROR, this);
		return;
	}

	nlohmann::json root;
	try {
		std::string contents;
		{
			wxProgressDialog prepProgress(
				"Import Materials",
				"Reading file...",
				100,
				this,
				wxPD_APP_MODAL | wxPD_SMOOTH | wxPD_AUTO_HIDE
			);

			in.seekg(0, std::ios::end);
			const std::streamoff fileSize = in.tellg();
			in.seekg(0, std::ios::beg);

			if (fileSize > 0) {
				contents.reserve(static_cast<size_t>(fileSize));
			}
			static constexpr size_t kChunkSize = 1024 * 1024;
			std::vector<char> buffer(kChunkSize);
			std::streamoff bytesRead = 0;
			while (in.good()) {
				in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
				const std::streamsize got = in.gcount();
				if (got <= 0) {
					break;
				}
				contents.append(buffer.data(), static_cast<size_t>(got));
				bytesRead += got;

				if (fileSize > 0) {
					const int pct = static_cast<int>(std::min<std::streamoff>(60, (bytesRead * 60) / fileSize));
					prepProgress.Update(pct, "Reading file...");
				} else {
					prepProgress.Pulse("Reading file...");
				}
				wxYieldIfNeeded();
			}

			prepProgress.Update(75, "Parsing JSON...");
			wxYieldIfNeeded();
			root = nlohmann::json::parse(contents);
			prepProgress.Update(100, "Ready");
			wxYieldIfNeeded();
		}

		MaterialsWorkbenchImportDialog preview(this, root, controller_);
		{
			wxProgressDialog planProgress(
				"Import Materials",
				"Building import preview...",
				100,
				this,
				wxPD_APP_MODAL | wxPD_SMOOTH | wxPD_AUTO_HIDE
			);
			preview.BuildPlanWithProgress(&planProgress, 0, 100);
			planProgress.Update(100, "Ready");
			wxYieldIfNeeded();
		}

		if (preview.ShowModal() != wxID_OK) {
			return;
		}

		MaterialsWorkbenchImportReport report;
		wxString error;
		{
			wxProgressDialog applyProgress(
				"Import Materials",
				"Applying changes...",
				100,
				this,
				wxPD_APP_MODAL | wxPD_SMOOTH | wxPD_AUTO_HIDE
			);

			applyProgress.Update(0, "Applying changes...");
			wxYieldIfNeeded();

			const auto onProgress = [&](int current, int total, const wxString &stage) {
				wxString message = stage;
				if (total > 0) {
					message += wxString::Format(" (%d/%d)", current, total);
					const int pct = 5 + static_cast<int>((static_cast<double>(current) / std::max(1, total)) * 80.0);
					applyProgress.Update(std::min(85, pct), message);
				} else {
					applyProgress.Pulse(message);
				}
				wxYieldIfNeeded();
				return true;
			};

			if (!ApplyMaterialsWorkbenchImportJsonWithProgress(controller_, preview.GetJson(), preview.GetOptions(), onProgress, report, error)) {
				if (error.IsEmpty()) {
					error = "Import failed.";
				}
				g_gui.PopupDialog(this, "Import failed", error, wxOK | wxICON_ERROR);
				return;
			}

			applyProgress.Update(90, "Refreshing runtime...");
			wxYieldIfNeeded();
			{
				std::set<int64_t> brushIds(report.importedBrushIds.begin(), report.importedBrushIds.end());
				std::set<int64_t> borderSetIds(report.importedBorderSetIds.begin(), report.importedBorderSetIds.end());

				int done = 0;
				const int total = static_cast<int>(brushIds.size() + borderSetIds.size());

				wxArrayString runtimeWarnings;
				for (int64_t brushId : brushIds) {
					wxArrayString warnings;
					wxString reloadError;
					if (!g_brushes.reloadBrushFromDatabase(brushId, warnings, reloadError)) {
						runtimeWarnings.push_back(wxString::Format("Runtime brush reload failed for id=%lld: %s", static_cast<long long>(brushId), reloadError));
					} else {
						runtimeWarnings.insert(runtimeWarnings.end(), warnings.begin(), warnings.end());
					}
					++done;
					if (total > 0) {
						const int pct = 90 + static_cast<int>((static_cast<double>(done) / total) * 6.0);
						applyProgress.Update(std::min(96, pct), "Refreshing runtime brushes...");
					} else {
						applyProgress.Pulse("Refreshing runtime brushes...");
					}
					wxYieldIfNeeded();
				}

				for (int64_t borderSetId : borderSetIds) {
					wxArrayString warnings;
					wxString reloadError;
					if (!g_brushes.reloadBorderSetFromDatabase(borderSetId, warnings, reloadError)) {
						runtimeWarnings.push_back(wxString::Format("Runtime border reload failed for id=%lld: %s", static_cast<long long>(borderSetId), reloadError));
					} else {
						runtimeWarnings.insert(runtimeWarnings.end(), warnings.begin(), warnings.end());
					}
					++done;
					if (total > 0) {
						const int pct = 96 + static_cast<int>((static_cast<double>(done) / total) * 2.0);
						applyProgress.Update(std::min(98, pct), "Refreshing runtime borders...");
					} else {
						applyProgress.Pulse("Refreshing runtime borders...");
					}
					wxYieldIfNeeded();
				}

				applyProgress.Update(98, "Refreshing runtime palettes...");
				wxYieldIfNeeded();
				{
					wxString palettesError;
					wxArrayString palettesWarnings;
					if (!g_gui.ReloadMaterialPalettesFromDatabase(palettesError, palettesWarnings)) {
						spdlog::warn(
							"Materials Workbench runtime palette refresh failed after materials import: {}",
							palettesError.ToStdString()
						);
					}
					runtimeWarnings.insert(runtimeWarnings.end(), palettesWarnings.begin(), palettesWarnings.end());
				}

				for (const wxString &warning : runtimeWarnings) {
					spdlog::warn(
						"Materials Workbench runtime refresh warning after materials import: {}",
						warning.ToStdString()
					);
				}
			}

			applyProgress.Update(99, "Refreshing workbench...");
			wxYieldIfNeeded();

			RefreshWorkbenchState();
			PopulateNavigation();

			applyProgress.Update(100, "Done");
			wxYieldIfNeeded();
		}

		if (!error.IsEmpty()) {
			wxMessageBox(error, "Import Materials", wxOK | wxICON_ERROR, this);
			return;
		}

		wxMessageBox(
			wxString::Format("Import complete.\n\nCreated: %d\nUpdated: %d\nSkipped: %d", report.created, report.updated, report.skipped),
			"Import Materials",
			wxOK | wxICON_INFORMATION,
			this
		);
		return;
	} catch (const nlohmann::json::parse_error &) {
		wxMessageBox("Invalid JSON file.", "Import Materials", wxOK | wxICON_ERROR, this);
		return;
	}
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
	RefreshRuntimeMaterialPalettes("palette save", false);
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

	RefreshRuntimeMaterialPalettes("border set save", false);
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
	RefreshRuntimeMaterialPalettes("border set delete", false);
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
	RefreshRuntimeMaterialPalettes("brush delete", false);
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
	const wxString dirtyBrushName = hasDirtyBrush ? brushPanel_->GetCurrentBrushDisplayName() : wxString();
	const bool hasDirtyBorder = borderPanel_ && borderPanel_->HasPendingChanges();
	const wxString dirtyBorderName = hasDirtyBorder ? borderPanel_->GetCurrentBorderSetDisplayName() : wxString();
	const bool hasDirtyWall = wallPanel_ && wallPanel_->HasPendingChanges();
	const wxString dirtyWallName = hasDirtyWall ? wallPanel_->GetCurrentWallDisplayName() : wxString();
	const wxColour defaultTextColour = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
	const wxColour modifiedTextColour(176, 102, 0);

	std::function<void(const wxTreeItemId&)> applyBadge;
	applyBadge = [&](const wxTreeItemId &parentItem) {
		wxTreeItemIdValue cookie;
		for (wxTreeItemId child = navigationTree_->GetFirstChild(parentItem, cookie); child.IsOk(); child = navigationTree_->GetNextChild(parentItem, cookie)) {
			auto* itemData = dynamic_cast<MaterialsWorkbenchTreeItemData*>(navigationTree_->GetItemData(child));
			if (itemData) {
				const bool isModifiedBrush = hasDirtyBrush && itemData->kind == MaterialsWorkbenchNodeKind::Brush && brushPanel_->IsCurrentBrushSelection(itemData->contextKey, itemData->itemIndex);
				const bool isModifiedWall = hasDirtyWall && itemData->kind == MaterialsWorkbenchNodeKind::Brush && wallPanel_->IsCurrentWallSelection(itemData->contextKey, itemData->itemIndex);
				const bool isModifiedBorder = hasDirtyBorder && itemData->kind == MaterialsWorkbenchNodeKind::BorderSet && borderPanel_->IsCurrentBorderSelection(itemData->contextKey, itemData->itemIndex);
				const bool isModified = isModifiedBrush || isModifiedBorder || isModifiedWall;
				const wxString displayLabel = isModifiedBrush && !dirtyBrushName.IsEmpty() ? dirtyBrushName : (isModifiedBorder && !dirtyBorderName.IsEmpty() ? dirtyBorderName : (isModifiedWall && !dirtyWallName.IsEmpty() ? dirtyWallName : itemData->baseLabel));
				navigationTree_->SetItemText(child, isModified ? displayLabel + " [modified]" : itemData->baseLabel);
				navigationTree_->SetItemTextColour(child, isModified ? modifiedTextColour : defaultTextColour);
			}

			applyBadge(child);
		}
	};

	applyBadge(root);
}

void MaterialsWorkbenchWindow::PopulateNavigation() {
	if (!navigationTree_) {
		return;
	}
	struct NavigationPopulateGuard {
		MaterialsWorkbenchWindow* window = nullptr;
		wxTreeCtrl* tree = nullptr;
		bool wasEnabled = true;
		explicit NavigationPopulateGuard(MaterialsWorkbenchWindow* window, wxTreeCtrl* tree) :
			window(window), tree(tree) {
			if (window) {
				window->navigationPopulating_ = true;
			}
			if (tree) {
				wasEnabled = tree->IsEnabled();
				tree->Freeze();
				tree->Enable(false);
			}
		}
		~NavigationPopulateGuard() {
			if (tree) {
				tree->Enable(wasEnabled);
				tree->Thaw();
			}
			if (window) {
				window->navigationPopulating_ = false;
			}
		}
	};
	NavigationPopulateGuard populateGuard(this, navigationTree_);

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
	const std::vector<MaterialsWorkbenchTreeNode> filteredNodes = BuildFilteredNavigationTree(controller_.BuildNavigationTree(), normalizedFilterQuery);
	std::function<void(const wxTreeItemId&, const std::vector<MaterialsWorkbenchTreeNode>&)> appendNodes;
	appendNodes = [&](const wxTreeItemId &parentItem, const std::vector<MaterialsWorkbenchTreeNode> &nodes) {
		for (const MaterialsWorkbenchTreeNode &node : nodes) {
			wxTreeItemId item = navigationTree_->AppendItem(
				parentItem,
				node.label,
				-1,
				-1,
				newd MaterialsWorkbenchTreeItemData(node.kind, node.contextKey, node.itemIndex, node.label)
			);
			if (!node.children.empty()) {
				appendNodes(item, node.children);
			}

			if (filterActive || NavigationStateContainsExpandedKey(previousState, BuildNavigationNodeKey(node.kind, node.contextKey, node.itemIndex))) {
				navigationTree_->Expand(item);
			}
		}
	};

	appendNodes(root, filteredNodes);

	if (filterActive && filteredNodes.empty()) {
		navigationTree_->AppendItem(root, wxString::Format("No matches for \"%s\".", navigationFilterQuery_));
	}

	if (!previousState.hasSelection || !SelectNavigationNode(previousState.selectedKind, previousState.selectedContextKey, previousState.selectedItemIndex)) {
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
		inspectorButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
			OpenInspector();
		});
	}
	if (exportButton_) {
		exportButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchWindow::OnExportMaterials, this);
	}
	if (importButton_) {
		importButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchWindow::OnImportMaterials, this);
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
		if (navigationPopulating_) {
			event.Skip();
			return;
		}
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
		if (navigationPopulating_) {
			event.Skip();
			return;
		}
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
		if (navigationPopulating_) {
			return;
		}
		const wxTreeItemId item = event.GetItem();
		if (!item.IsOk()) {
			return;
		}

		auto* itemData = dynamic_cast<MaterialsWorkbenchTreeItemData*>(navigationTree_->GetItemData(item));
		const bool isSameBrushSelection = itemData && itemData->kind == MaterialsWorkbenchNodeKind::Brush && brushPanel_->IsCurrentBrushSelection(itemData->contextKey, itemData->itemIndex);
		const bool isSameBorderSelection = itemData && itemData->kind == MaterialsWorkbenchNodeKind::BorderSet && borderPanel_->IsCurrentBorderSelection(itemData->contextKey, itemData->itemIndex);
		const bool isSameWallSelection = itemData && itemData->kind == MaterialsWorkbenchNodeKind::Brush && wallPanel_->IsCurrentWallSelection(itemData->contextKey, itemData->itemIndex);

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
		if (navigationPopulating_) {
			return;
		}
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
		if (brushPanel_ && brushPanel_->HasPendingChanges() && !brushPanel_->ResolvePendingChangesBeforeSwitch(this, "closing the Materials Workbench window")) {
			event.Veto();
			return;
		}
		if (borderPanel_ && borderPanel_->HasPendingChanges() && !borderPanel_->ResolvePendingChangesBeforeSwitch(this, "closing the Materials Workbench window")) {
			event.Veto();
			return;
		}
		if (wallPanel_ && wallPanel_->HasPendingChanges() && !wallPanel_->ResolvePendingChangesBeforeSwitch(this, "closing the Materials Workbench window")) {
			event.Veto();
			return;
		}
	}

	if (inspectorDialog_) {
		MaterialsWorkbenchInspectorDialog* dialog = inspectorDialog_;
		inspectorDialog_ = nullptr;
		dialog->Destroy();
	}

	GetMaterialsWorkbenchWindowInstance() = nullptr;
	Destroy();
	event.Skip(false);
}
