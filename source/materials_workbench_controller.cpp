#include "main.h"

#include "materials_workbench_controller.h"

#include <map>

#include <wx/arrstr.h>

namespace {
	wxString FormatInspectorImportedFromValue(const wxString &sourceFile) {
		return sourceFile.IsEmpty() ? "Not imported from legacy XML" : sourceFile;
	}

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
		text << "Storage: materials.db\n";
		text << "Imported from: " << FormatInspectorImportedFromValue(brush.sourceFile) << "\n\n";
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

	wxString BuildBorderSetNavigationLabel(const BorderSetRecord &border) {
		if (border.xmlBorderId > 0) {
			if (!border.borderType.IsEmpty()) {
				return wxString::Format("Border %d (%s)", border.xmlBorderId, border.borderType);
			}
			return wxString::Format("Border %d", border.xmlBorderId);
		}
		if (!border.borderType.IsEmpty()) {
			return wxString::Format("Border Set #%lld (%s)", static_cast<long long>(border.id), border.borderType);
		}
		return wxString::Format("Border Set #%lld", static_cast<long long>(border.id));
	}

	wxString FormatNavigationCountLabel(const wxString &label, size_t count) {
		return wxString::Format("%s (%zu)", label, count);
	}

	wxString BuildBrushLookupKey(int64_t brushId, const wxString &brushName) {
		if (brushId > 0) {
			return wxString::Format("id:%lld", static_cast<long long>(brushId));
		}

		wxString normalizedName = brushName;
		normalizedName.MakeLower();
		return "name:" + normalizedName;
	}

	wxString BuildBrushDomainLabel(const wxString &brushType) {
		if (brushType == "ground") {
			return "Terrain";
		}
		if (brushType == "doodad") {
			return "Doodad";
		}
		if (brushType == "carpet") {
			return "Carpet";
		}
		if (brushType == "table") {
			return "Table";
		}
		return brushType;
	}

} // namespace

bool MaterialsWorkbenchController::ReloadCatalog() {
	lastError_.clear();
	if (!repository_.LoadCatalog(catalog_, lastError_)) {
		spdlog::warn("Materials Workbench failed to reload catalog from materials.db: {}", lastError_.ToStdString());
		return false;
	}

	spdlog::info(
		"Materials Workbench reloaded catalog from materials.db: brushes={} palettes={} walls={}",
		catalog_.auditReport.brushCount,
		catalog_.tilesets.size(),
		catalog_.wallBrushes.size()
	);
	return true;
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

	MaterialsWorkbenchTreeNode catalogNode;
	catalogNode.kind = MaterialsWorkbenchNodeKind::Group;
	catalogNode.label = "Catalog";
	catalogNode.contextKey = "group:catalog";

	MaterialsWorkbenchTreeNode palettesNode;
	palettesNode.kind = MaterialsWorkbenchNodeKind::Group;
	palettesNode.label = FormatNavigationCountLabel("Palettes", catalog_.tilesets.size());
	palettesNode.contextKey = "group:palettes";
	for (size_t i = 0; i < catalog_.tilesets.size(); ++i) {
		MaterialsWorkbenchTreeNode item;
		item.kind = MaterialsWorkbenchNodeKind::Tileset;
		item.label = catalog_.tilesets[i].name;
		item.contextKey = "tilesets";
		item.itemIndex = static_cast<int>(i);
		palettesNode.children.push_back(std::move(item));
	}
	catalogNode.children.push_back(std::move(palettesNode));

	MaterialsWorkbenchTreeNode brushesNode;
	brushesNode.kind = MaterialsWorkbenchNodeKind::Group;
	brushesNode.label = FormatNavigationCountLabel("Brushes", catalog_.auditReport.brushCount);
	brushesNode.contextKey = "group:brushes";

	std::map<wxString, wxString> brushCategoryByKey;
	std::vector<wxString> brushCategoryOrder;
	for (const TilesetStorageRecord &tileset : catalog_.tilesets) {
		for (const TilesetSectionRecord &section : tileset.sections) {
			const wxString categoryLabel = section.sectionType;
			if (std::find(brushCategoryOrder.begin(), brushCategoryOrder.end(), categoryLabel) == brushCategoryOrder.end()) {
				brushCategoryOrder.push_back(categoryLabel);
			}

			for (const TilesetEntryRecord &entry : section.entries) {
				if (!entry.entryKind.IsSameAs("brush", false)) {
					continue;
				}
				if (entry.brushId <= 0 && entry.brushName.IsEmpty()) {
					continue;
				}

				const wxString brushKey = BuildBrushLookupKey(entry.brushId, entry.brushName);
				if (!brushCategoryByKey.count(brushKey)) {
					brushCategoryByKey[brushKey] = categoryLabel;
				}
			}
		}
	}

	for (const MaterialsWorkbenchBrushGroup &group : catalog_.brushGroups) {
		MaterialsWorkbenchTreeNode brushGroupNode;
		brushGroupNode.kind = MaterialsWorkbenchNodeKind::Group;
		brushGroupNode.label = FormatNavigationCountLabel(BuildBrushDomainLabel(group.brushType), group.brushes.size());
		brushGroupNode.contextKey = "brush_group:" + group.brushType;

		std::map<wxString, std::vector<int>> brushIndexesByCategory;
		for (size_t i = 0; i < group.brushes.size(); ++i) {
			const BrushRecord &brush = group.brushes[i];
			const wxString brushKey = BuildBrushLookupKey(brush.id, brush.name);
			const auto categoryIt = brushCategoryByKey.find(brushKey);
			const wxString categoryLabel = categoryIt != brushCategoryByKey.end() ? categoryIt->second : "Uncategorized";
			brushIndexesByCategory[categoryLabel].push_back(static_cast<int>(i));
		}

		for (const wxString &categoryLabel : brushCategoryOrder) {
			const auto categoryIt = brushIndexesByCategory.find(categoryLabel);
			if (categoryIt == brushIndexesByCategory.end() || categoryIt->second.empty()) {
				continue;
			}

			MaterialsWorkbenchTreeNode categoryNode;
			categoryNode.kind = MaterialsWorkbenchNodeKind::Group;
			categoryNode.label = FormatNavigationCountLabel(categoryLabel, categoryIt->second.size());
			categoryNode.contextKey = "brush_category:" + group.brushType + ":" + categoryLabel;
			for (int brushIndex : categoryIt->second) {
				MaterialsWorkbenchTreeNode item;
				item.kind = MaterialsWorkbenchNodeKind::Brush;
				item.label = group.brushes[brushIndex].name;
				item.contextKey = group.brushType;
				item.itemIndex = brushIndex;
				categoryNode.children.push_back(std::move(item));
			}
			brushGroupNode.children.push_back(std::move(categoryNode));
		}

		const auto uncategorizedIt = brushIndexesByCategory.find("Uncategorized");
		if (uncategorizedIt != brushIndexesByCategory.end() && !uncategorizedIt->second.empty()) {
			MaterialsWorkbenchTreeNode categoryNode;
			categoryNode.kind = MaterialsWorkbenchNodeKind::Group;
			categoryNode.label = FormatNavigationCountLabel("Uncategorized", uncategorizedIt->second.size());
			categoryNode.contextKey = "brush_category:" + group.brushType + ":uncategorized";
			for (int brushIndex : uncategorizedIt->second) {
				MaterialsWorkbenchTreeNode item;
				item.kind = MaterialsWorkbenchNodeKind::Brush;
				item.label = group.brushes[brushIndex].name;
				item.contextKey = group.brushType;
				item.itemIndex = brushIndex;
				categoryNode.children.push_back(std::move(item));
			}
			brushGroupNode.children.push_back(std::move(categoryNode));
		}
		brushesNode.children.push_back(std::move(brushGroupNode));
	}
	catalogNode.children.push_back(std::move(brushesNode));
	nodes.push_back(std::move(catalogNode));

	MaterialsWorkbenchTreeNode editorsNode;
	editorsNode.kind = MaterialsWorkbenchNodeKind::Group;
	editorsNode.label = "Specialized Editors";
	editorsNode.contextKey = "group:specialized";

	MaterialsWorkbenchTreeNode bordersNode;
	bordersNode.kind = MaterialsWorkbenchNodeKind::Group;
	bordersNode.label = FormatNavigationCountLabel("Borders", catalog_.globalBorderSets.size() + catalog_.inlineBorderSets.size());
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
		scopeNode.label = FormatNavigationCountLabel(scope.label, scope.collection->size());
		scopeNode.contextKey = "border_scope:" + wxString::FromUTF8(scope.contextKey);
		for (size_t i = 0; i < scope.collection->size(); ++i) {
			const BorderSetRecord &record = (*scope.collection)[i];
			MaterialsWorkbenchTreeNode item;
			item.kind = MaterialsWorkbenchNodeKind::BorderSet;
			item.label = BuildBorderSetNavigationLabel(record);
			item.contextKey = wxString::FromUTF8(scope.contextKey);
			item.itemIndex = static_cast<int>(i);
			scopeNode.children.push_back(std::move(item));
		}
		bordersNode.children.push_back(std::move(scopeNode));
	}
	editorsNode.children.push_back(std::move(bordersNode));

	MaterialsWorkbenchTreeNode wallsNode;
	wallsNode.kind = MaterialsWorkbenchNodeKind::Group;
	wallsNode.label = FormatNavigationCountLabel("Walls", catalog_.wallBrushes.size());
	wallsNode.contextKey = "group:walls";
	for (size_t i = 0; i < catalog_.wallBrushes.size(); ++i) {
		MaterialsWorkbenchTreeNode item;
		item.kind = MaterialsWorkbenchNodeKind::Brush;
		item.label = catalog_.wallBrushes[i].name;
		item.contextKey = "wall";
		item.itemIndex = static_cast<int>(i);
		wallsNode.children.push_back(std::move(item));
	}
	editorsNode.children.push_back(std::move(wallsNode));
	nodes.push_back(std::move(editorsNode));

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

bool MaterialsWorkbenchController::GetBrushDetails(const wxString &contextKey, int itemIndex, BrushStorageRecord &outBrush, wxString &error) const {
	const BrushRecord* brush = FindBrushRecord(contextKey, itemIndex);
	if (!brush) {
		error = "Brush details are unavailable.";
		outBrush = BrushStorageRecord();
		return false;
	}
	return repository_.LoadBrushDetails(brush->id, outBrush, error);
}

bool MaterialsWorkbenchController::GetBorderSetDetails(const wxString &contextKey, int itemIndex, BorderSetStorageRecord &outBorderSet, wxString &error) const {
	const BorderSetRecord* borderSet = FindBorderSetRecord(contextKey, itemIndex);
	if (!borderSet) {
		error = "Border set details are unavailable.";
		outBorderSet = BorderSetStorageRecord();
		return false;
	}
	return repository_.LoadBorderSetDetails(borderSet->id, outBorderSet, error);
}

bool MaterialsWorkbenchController::SaveBrushDetails(BrushStorageRecord &brushStorage, wxString &error) {
	if (!repository_.SaveBrushDetails(brushStorage, error)) {
		spdlog::warn(
			"Materials Workbench failed to save brush details: id={} name='{}' error='{}'",
			static_cast<long long>(brushStorage.brush.id),
			brushStorage.brush.name.ToStdString(),
			error.ToStdString()
		);
		return false;
	}

	if (!ReloadCatalog()) {
		error = lastError_;
		spdlog::warn(
			"Materials Workbench saved brush but failed to reload catalog: id={} name='{}' error='{}'",
			static_cast<long long>(brushStorage.brush.id),
			brushStorage.brush.name.ToStdString(),
			error.ToStdString()
		);
		return false;
	}

	spdlog::info(
		"Materials Workbench saved brush details and reloaded catalog: id={} name='{}' type='{}'",
		static_cast<long long>(brushStorage.brush.id),
		brushStorage.brush.name.ToStdString(),
		brushStorage.brush.type.ToStdString()
	);

	return true;
}

bool MaterialsWorkbenchController::SaveWallBrushParts(BrushStorageRecord &brushStorage, wxString &error) {
	if (!repository_.SaveWallBrushParts(brushStorage, error)) {
		return false;
	}

	if (!ReloadCatalog()) {
		error = lastError_;
		return false;
	}

	return true;
}

bool MaterialsWorkbenchController::SaveBorderSet(BorderSetStorageRecord &borderSet, wxString &error) {
	if (!repository_.SaveBorderSet(borderSet, error)) {
		return false;
	}

	if (!ReloadCatalog()) {
		error = lastError_;
		return false;
	}

	return true;
}

bool MaterialsWorkbenchController::LocateBrushNode(int64_t brushId, wxString &outContextKey, int &outItemIndex) const {
	for (size_t i = 0; i < catalog_.wallBrushes.size(); ++i) {
		if (catalog_.wallBrushes[i].id == brushId) {
			outContextKey = "wall";
			outItemIndex = static_cast<int>(i);
			return true;
		}
	}

	for (const MaterialsWorkbenchBrushGroup &group : catalog_.brushGroups) {
		for (size_t i = 0; i < group.brushes.size(); ++i) {
			if (group.brushes[i].id == brushId) {
				outContextKey = group.brushType;
				outItemIndex = static_cast<int>(i);
				return true;
			}
		}
	}

	outContextKey.clear();
	outItemIndex = -1;
	return false;
}

bool MaterialsWorkbenchController::LocateBorderSetNode(int64_t borderSetId, wxString &outContextKey, int &outItemIndex) const {
	for (size_t i = 0; i < catalog_.globalBorderSets.size(); ++i) {
		if (catalog_.globalBorderSets[i].id == borderSetId) {
			outContextKey = "global";
			outItemIndex = static_cast<int>(i);
			return true;
		}
	}

	for (size_t i = 0; i < catalog_.inlineBorderSets.size(); ++i) {
		if (catalog_.inlineBorderSets[i].id == borderSetId) {
			outContextKey = "inline";
			outItemIndex = static_cast<int>(i);
			return true;
		}
	}

	outContextKey.clear();
	outItemIndex = -1;
	return false;
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
				return BuildBrushDomainLabel(brushType) + wxString::Format("\n\nLoaded %zu brushes for this domain.", group->brushes.size());
			}
		}
		if (contextKey.StartsWith("brush_category:")) {
			wxString remainder = contextKey.AfterFirst(':');
			const wxString brushType = remainder.BeforeFirst(':');
			const wxString categoryLabel = remainder.AfterFirst(':');
			const MaterialsWorkbenchBrushGroup* group = FindBrushGroup(brushType);
			if (group) {
				return wxString::Format(
					"%s -> %s\n\nBrush authoring subcategory derived from palette organization for the %s domain.",
					BuildBrushDomainLabel(brushType),
					categoryLabel,
					BuildBrushDomainLabel(brushType)
				);
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
