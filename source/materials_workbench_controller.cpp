#include "main.h"

#include "materials_workbench_controller.h"

#include <wx/arrstr.h>

namespace {
	wxString FormatAuditSummary(const MaterialsDatabaseAuditReport &audit) {
		wxString text;
		text << "SQLite materials catalog summary\n\n";
		text << "Brushes: " << audit.brushCount << "\n";
		text << "Border sets: " << audit.borderSetCount << "\n";
		text << "Tilesets: " << audit.tilesetCount << "\n";
		text << "Tileset sections: " << audit.tilesetSectionCount << "\n";
		text << "Tileset entries: " << audit.tilesetEntryCount << "\n\n";
		text << "Unresolved references\n";
		text << "  ground targets: " << audit.unresolvedGroundTargets << "\n";
		text << "  brush links: " << audit.unresolvedBrushLinks << "\n";
		text << "  tileset entries: " << audit.unresolvedTilesetEntries << "\n";
		return text;
	}

	wxString FormatTilesetOverview(const TilesetStorageRecord &tileset) {
		wxString text;
		text << "Palette: " << tileset.name << "\n";
		text << "Source: " << tileset.sourceFile << "\n";
		text << "Sections: " << tileset.sections.size() << "\n";
		return text;
	}

	wxString FormatTilesetInspector(const TilesetStorageRecord &tileset) {
		wxString text = FormatTilesetOverview(tileset);
		text << "\nEntries by section\n";
		for (const TilesetSectionRecord &section : tileset.sections) {
			text << "  - " << section.sectionType << ": " << section.entries.size() << "\n";
		}
		return text;
	}

	wxString FormatBrushOverview(const BrushStorageRecord &storage) {
		const BrushRecord &brush = storage.brush;
		wxString text;
		text << "Brush: " << brush.name << "\n";
		text << "Type: " << brush.type << "\n";
		text << "ID: " << brush.id << "\n";
		text << "Source: " << brush.sourceFile << "\n\n";
		text << "lookId: " << brush.lookId << "\n";
		text << "serverLookId: " << brush.serverLookId << "\n";
		text << "zOrder: " << brush.zOrder << "\n";
		return text;
	}

	wxString FormatBrushInspector(const BrushStorageRecord &storage) {
		const BrushRecord &brush = storage.brush;
		wxString text = FormatBrushOverview(storage);
		text << "\nFlags\n";
		text << "  draggable: " << (brush.draggable ? "yes" : "no") << "\n";
		text << "  onBlocking: " << (brush.onBlocking ? "yes" : "no") << "\n";
		text << "  onDuplicate: " << (brush.onDuplicate ? "yes" : "no") << "\n";
		text << "  redoBorders: " << (brush.redoBorders ? "yes" : "no") << "\n";
		text << "  randomize: " << (brush.randomize ? "yes" : "no") << "\n";
		text << "  oneSize: " << (brush.oneSize ? "yes" : "no") << "\n";
		text << "  soloOptional: " << (brush.soloOptional ? "yes" : "no") << "\n";
		text << "\nBrush items: " << storage.items.size() << "\n";
		text << "Ground borders: " << storage.borders.size() << "\n";
		text << "Links: " << storage.links.size() << "\n";
		text << "Wall parts: " << storage.wallParts.size() << "\n";
		text << "Carpet nodes: " << storage.carpetNodes.size() << "\n";
		text << "Table nodes: " << storage.tableNodes.size() << "\n";
		text << "Doodad alternatives: " << storage.doodadAlternatives.size() << "\n";
		return text;
	}

	wxString FormatBorderSetOverview(const BorderSetStorageRecord &storage) {
		const BorderSetRecord &border = storage.borderSet;
		wxString text;
		text << "Border set ID: " << border.id << "\n";
		text << "Scope: " << border.borderScope << "\n";
		text << "XML border ID: " << border.xmlBorderId << "\n";
		text << "Ground equivalent: " << border.groundEquivalent << "\n";
		text << "Group: " << border.borderGroup << "\n";
		return text;
	}

	wxString FormatBorderSetInspector(const BorderSetStorageRecord &storage) {
		const BorderSetRecord &border = storage.borderSet;
		wxString text = FormatBorderSetOverview(storage);
		text << "Type: " << border.borderType << "\n";
		text << "Owner brush ID: " << border.ownerBrushId << "\n";
		text << "Items: " << storage.items.size() << "\n";
		for (const BorderSetItemRecord &item : storage.items) {
			text << "  - edge=" << item.edge << " item=" << item.itemId << " sort=" << item.sortOrder << "\n";
		}
		return text;
	}
} // namespace

bool MaterialsWorkbenchController::ReloadCatalog() {
	lastError_.clear();
	return repository_.LoadCatalog(catalog_, lastError_);
}

wxString MaterialsWorkbenchController::GetWindowTitle() const {
	return "Materials Workbench";
}

wxString MaterialsWorkbenchController::GetOverviewText() const {
	if (!lastError_.IsEmpty()) {
		return "Materials Workbench could not load the SQLite catalog.\n\nError: " + lastError_;
	}
	return FormatAuditSummary(catalog_.auditReport);
}

wxString MaterialsWorkbenchController::GetInspectorText() const {
	if (!lastError_.IsEmpty()) {
		return "Inspector unavailable while the catalog failed to load.";
	}

	return "Catalog loaded from materials.db.\n\n"
		   "Select a palette, brush, border set or wall entry in the navigation tree to inspect its SQLite-backed metadata.";
}

std::vector<MaterialsWorkbenchTreeNode> MaterialsWorkbenchController::BuildNavigationTree() const {
	std::vector<MaterialsWorkbenchTreeNode> nodes;

	MaterialsWorkbenchTreeNode palettesNode;
	palettesNode.kind = MaterialsWorkbenchNodeKind::Group;
	palettesNode.label = "Palettes";
	palettesNode.contextKey = "group:palettes";
	for (size_t i = 0; i < catalog_.tilesets.size(); ++i) {
		MaterialsWorkbenchTreeNode item;
		item.kind = MaterialsWorkbenchNodeKind::Tileset;
		item.label = catalog_.tilesets[i].name;
		item.contextKey = "tilesets";
		item.itemIndex = static_cast<int>(i);
		palettesNode.children.push_back(std::move(item));
	}
	nodes.push_back(std::move(palettesNode));

	MaterialsWorkbenchTreeNode brushesNode;
	brushesNode.kind = MaterialsWorkbenchNodeKind::Group;
	brushesNode.label = "Brushes";
	brushesNode.contextKey = "group:brushes";
	for (const MaterialsWorkbenchBrushGroup &group : catalog_.brushGroups) {
		MaterialsWorkbenchTreeNode brushGroupNode;
		brushGroupNode.kind = MaterialsWorkbenchNodeKind::Group;
		brushGroupNode.label = group.label;
		brushGroupNode.contextKey = "brush_group:" + group.brushType;
		for (size_t i = 0; i < group.brushes.size(); ++i) {
			MaterialsWorkbenchTreeNode item;
			item.kind = MaterialsWorkbenchNodeKind::Brush;
			item.label = group.brushes[i].name;
			item.contextKey = group.brushType;
			item.itemIndex = static_cast<int>(i);
			brushGroupNode.children.push_back(std::move(item));
		}
		brushesNode.children.push_back(std::move(brushGroupNode));
	}
	nodes.push_back(std::move(brushesNode));

	MaterialsWorkbenchTreeNode bordersNode;
	bordersNode.kind = MaterialsWorkbenchNodeKind::Group;
	bordersNode.label = "Borders";
	bordersNode.contextKey = "group:borders";
	const struct BorderScopeNode {
		const char* label;
		const char* contextKey;
		const std::vector<BorderSetRecord>* collection;
	} borderScopes[] = {
		{ "Global Border Sets", "global", &catalog_.globalBorderSets },
		{ "Inline Border Sets", "inline", &catalog_.inlineBorderSets },
	};
	for (const BorderScopeNode &scope : borderScopes) {
		MaterialsWorkbenchTreeNode scopeNode;
		scopeNode.kind = MaterialsWorkbenchNodeKind::Group;
		scopeNode.label = scope.label;
		scopeNode.contextKey = "border_scope:" + wxString::FromUTF8(scope.contextKey);
		for (size_t i = 0; i < scope.collection->size(); ++i) {
			const BorderSetRecord &record = (*scope.collection)[i];
			MaterialsWorkbenchTreeNode item;
			item.kind = MaterialsWorkbenchNodeKind::BorderSet;
			item.label = wxString::Format("Border Set #%lld", static_cast<long long>(record.id));
			item.contextKey = wxString::FromUTF8(scope.contextKey);
			item.itemIndex = static_cast<int>(i);
			scopeNode.children.push_back(std::move(item));
		}
		bordersNode.children.push_back(std::move(scopeNode));
	}
	nodes.push_back(std::move(bordersNode));

	MaterialsWorkbenchTreeNode wallsNode;
	wallsNode.kind = MaterialsWorkbenchNodeKind::Group;
	wallsNode.label = "Walls";
	wallsNode.contextKey = "group:walls";
	for (size_t i = 0; i < catalog_.wallBrushes.size(); ++i) {
		MaterialsWorkbenchTreeNode item;
		item.kind = MaterialsWorkbenchNodeKind::Brush;
		item.label = catalog_.wallBrushes[i].name;
		item.contextKey = "wall";
		item.itemIndex = static_cast<int>(i);
		wallsNode.children.push_back(std::move(item));
	}
	nodes.push_back(std::move(wallsNode));

	return nodes;
}

bool MaterialsWorkbenchController::GetTilesetByIndex(int itemIndex, TilesetStorageRecord &outTileset) const {
	outTileset = TilesetStorageRecord();
	if (itemIndex < 0 || itemIndex >= static_cast<int>(catalog_.tilesets.size())) {
		return false;
	}
	outTileset = catalog_.tilesets[itemIndex];
	return true;
}

bool MaterialsWorkbenchController::SaveTileset(const TilesetStorageRecord &tileset, wxString &error) {
	if (!repository_.SaveTileset(tileset, error)) {
		return false;
	}

	for (TilesetStorageRecord &storedTileset : catalog_.tilesets) {
		if (storedTileset.name == tileset.name) {
			storedTileset = tileset;
			return true;
		}
	}

	catalog_.tilesets.push_back(tileset);
	return true;
}

const std::vector<MaterialsWorkbenchBrushGroup> &MaterialsWorkbenchController::GetBrushGroups() const {
	return catalog_.brushGroups;
}

const std::vector<BrushRecord> &MaterialsWorkbenchController::GetWallBrushes() const {
	return catalog_.wallBrushes;
}

const MaterialsWorkbenchBrushGroup* MaterialsWorkbenchController::FindBrushGroup(const wxString &contextKey) const {
	for (const MaterialsWorkbenchBrushGroup &group : catalog_.brushGroups) {
		if (group.brushType == contextKey) {
			return &group;
		}
	}
	return nullptr;
}

const BrushRecord* MaterialsWorkbenchController::FindBrushRecord(const wxString &contextKey, int itemIndex) const {
	if (contextKey == "wall") {
		if (itemIndex >= 0 && itemIndex < static_cast<int>(catalog_.wallBrushes.size())) {
			return &catalog_.wallBrushes[itemIndex];
		}
		return nullptr;
	}

	const MaterialsWorkbenchBrushGroup* group = FindBrushGroup(contextKey);
	if (!group) {
		return nullptr;
	}
	if (itemIndex < 0 || itemIndex >= static_cast<int>(group->brushes.size())) {
		return nullptr;
	}
	return &group->brushes[itemIndex];
}

const BorderSetRecord* MaterialsWorkbenchController::FindBorderSetRecord(const wxString &contextKey, int itemIndex) const {
	const std::vector<BorderSetRecord>* collection = nullptr;
	if (contextKey == "global") {
		collection = &catalog_.globalBorderSets;
	} else if (contextKey == "inline") {
		collection = &catalog_.inlineBorderSets;
	}
	if (!collection || itemIndex < 0 || itemIndex >= static_cast<int>(collection->size())) {
		return nullptr;
	}
	return &(*collection)[itemIndex];
}

wxString MaterialsWorkbenchController::BuildSelectionOverview(MaterialsWorkbenchNodeKind kind, const wxString &contextKey, int itemIndex) const {
	if (!lastError_.IsEmpty()) {
		return GetOverviewText();
	}

	if (kind == MaterialsWorkbenchNodeKind::Group) {
		if (contextKey == "group:palettes") {
			return wxString::Format("Palettes workspace\n\nLoaded %zu palettes from SQLite.", catalog_.tilesets.size());
		}
		if (contextKey == "group:brushes") {
			size_t brushCount = 0;
			for (const MaterialsWorkbenchBrushGroup &group : catalog_.brushGroups) {
				brushCount += group.brushes.size();
			}
			return wxString::Format("Brushes workspace\n\nLoaded %zu editable brushes across %zu groups.", brushCount, catalog_.brushGroups.size());
		}
		if (contextKey == "group:borders") {
			return wxString::Format("Borders workspace\n\nLoaded %zu global and %zu inline border sets.", catalog_.globalBorderSets.size(), catalog_.inlineBorderSets.size());
		}
		if (contextKey == "group:walls") {
			return wxString::Format("Walls workspace\n\nLoaded %zu wall brushes.", catalog_.wallBrushes.size());
		}
		if (contextKey.StartsWith("brush_group:")) {
			const wxString brushType = contextKey.AfterFirst(':');
			const MaterialsWorkbenchBrushGroup* group = FindBrushGroup(brushType);
			if (group) {
				return group->label + wxString::Format("\n\nLoaded %zu brushes of type \"%s\".", group->brushes.size(), brushType);
			}
		}
		if (contextKey.StartsWith("border_scope:")) {
			const wxString scope = contextKey.AfterFirst(':');
			const size_t count = scope == "global" ? catalog_.globalBorderSets.size() : catalog_.inlineBorderSets.size();
			return wxString::Format("Border scope: %s\n\nLoaded %zu border sets.", scope, count);
		}
		return FormatAuditSummary(catalog_.auditReport);
	}

	if (kind == MaterialsWorkbenchNodeKind::Tileset) {
		if (itemIndex >= 0 && itemIndex < static_cast<int>(catalog_.tilesets.size())) {
			return FormatTilesetOverview(catalog_.tilesets[itemIndex]);
		}
	}

	if (kind == MaterialsWorkbenchNodeKind::Brush) {
		const BrushRecord* brush = FindBrushRecord(contextKey, itemIndex);
		if (!brush) {
			return "Brush details are unavailable.";
		}
		BrushStorageRecord storage;
		wxString error;
		if (!repository_.LoadBrushDetails(brush->id, storage, error)) {
			return "Failed to load brush details: " + error;
		}
		return FormatBrushOverview(storage);
	}

	if (kind == MaterialsWorkbenchNodeKind::BorderSet) {
		const BorderSetRecord* border = FindBorderSetRecord(contextKey, itemIndex);
		if (!border) {
			return "Border set details are unavailable.";
		}
		BorderSetStorageRecord storage;
		wxString error;
		if (!repository_.LoadBorderSetDetails(border->id, storage, error)) {
			return "Failed to load border set details: " + error;
		}
		return FormatBorderSetOverview(storage);
	}

	return FormatAuditSummary(catalog_.auditReport);
}

wxString MaterialsWorkbenchController::BuildSelectionInspector(MaterialsWorkbenchNodeKind kind, const wxString &contextKey, int itemIndex) const {
	if (!lastError_.IsEmpty()) {
		return GetInspectorText();
	}

	if (kind == MaterialsWorkbenchNodeKind::Tileset) {
		if (itemIndex >= 0 && itemIndex < static_cast<int>(catalog_.tilesets.size())) {
			return FormatTilesetInspector(catalog_.tilesets[itemIndex]);
		}
		return "Palette details are unavailable.";
	}

	if (kind == MaterialsWorkbenchNodeKind::Brush) {
		const BrushRecord* brush = FindBrushRecord(contextKey, itemIndex);
		if (!brush) {
			return "Brush details are unavailable.";
		}
		BrushStorageRecord storage;
		wxString error;
		if (!repository_.LoadBrushDetails(brush->id, storage, error)) {
			return "Failed to load brush details: " + error;
		}
		return FormatBrushInspector(storage);
	}

	if (kind == MaterialsWorkbenchNodeKind::BorderSet) {
		const BorderSetRecord* border = FindBorderSetRecord(contextKey, itemIndex);
		if (!border) {
			return "Border set details are unavailable.";
		}
		BorderSetStorageRecord storage;
		wxString error;
		if (!repository_.LoadBorderSetDetails(border->id, storage, error)) {
			return "Failed to load border set details: " + error;
		}
		return FormatBorderSetInspector(storage);
	}

	return GetInspectorText();
}
