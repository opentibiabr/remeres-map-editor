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
		text << "Group: " << (tileset.paletteGroupName.IsEmpty() ? wxString("other") : tileset.paletteGroupName) << "\n";
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
		text << "Look ID: " << brush.lookId << "\n";
		text << "Server look ID: " << brush.serverLookId << "\n";
		text << "Z order: " << brush.zOrder << "\n";
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
		text << "Internal ID: " << border.id << "\n";
		text << "Scope: " << border.borderScope << "\n";
		if (border.borderScope == "global") {
			text << "Global Border ID: " << border.xmlBorderId << "\n";
		}
		text << "Center Tile: " << border.groundEquivalent << "\n";
		text << "Autoborder Group: " << (border.borderGroup <= 0 ? wxString("None") : wxString::Format("Group %d", border.borderGroup)) << "\n";
		return text;
	}

	wxString FormatBorderSetInspector(const BorderSetStorageRecord &storage) {
		const BorderSetRecord &border = storage.borderSet;
		wxString text = FormatBorderSetOverview(storage);
		text << "Border Style: " << border.borderType << "\n";
		text << "Owner Brush ID: " << border.ownerBrushId << "\n";
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

	wxString DerivePaletteGroupKeyFromSectionType(const wxString &sectionType) {
		if (sectionType.IsSameAs("terrain", false) || sectionType.IsSameAs("terrain_and_raw", false)) {
			return "terrain";
		}
		if (sectionType.IsSameAs("doodad", false) || sectionType.IsSameAs("doodad_and_raw", false)) {
			return "doodad";
		}
		if (sectionType.IsSameAs("item", false) || sectionType.IsSameAs("items", false) || sectionType.IsSameAs("items_and_raw", false)) {
			return "item";
		}
		return "other";
	}

	wxString BuildPaletteGroupKey(const TilesetStorageRecord &tileset) {
		if (!tileset.paletteGroupName.IsEmpty()) {
			return tileset.paletteGroupName;
		}
		if (!tileset.sections.empty()) {
			return DerivePaletteGroupKeyFromSectionType(tileset.sections.front().sectionType);
		}
		return "other";
	}

	wxString BuildPaletteGroupLabel(const wxString &groupName) {
		if (groupName.IsSameAs("terrain", false)) {
			return "Terrain";
		}
		if (groupName.IsSameAs("doodad", false)) {
			return "Doodad";
		}
		if (groupName.IsSameAs("item", false)) {
			return "Item";
		}
		if (groupName.IsSameAs("other", false)) {
			return "Other";
		}
		return groupName;
	}

	wxString BuildBrushFamilyKeyFromBrushType(const wxString &brushType) {
		if (brushType == "ground") {
			return "terrain";
		}
		if (brushType == "doodad") {
			return "doodad";
		}
		if (brushType == "carpet" || brushType == "table") {
			return "item";
		}
		return "other";
	}

	wxString BuildBrushFamilyLabel(const wxString &familyKey) {
		return BuildPaletteGroupLabel(familyKey);
	}

	std::map<wxString, size_t> CountPalettesByCategory(const MaterialsWorkbenchCatalogSnapshot &catalog) {
		std::map<wxString, size_t> counts;
		for (const TilesetStorageRecord &tileset : catalog.tilesets) {
			++counts[BuildPaletteGroupKey(tileset)];
		}
		return counts;
	}

	size_t CountBrushesInFamily(const MaterialsWorkbenchCatalogSnapshot &catalog, const wxString &familyKey) {
		size_t count = 0;
		for (const MaterialsWorkbenchBrushGroup &group : catalog.brushGroups) {
			for (const BrushRecord &brush : group.brushes) {
				if (BuildBrushFamilyKeyFromBrushType(brush.type) == familyKey) {
					++count;
				}
			}
		}
		return count;
	}

	wxString FormatCatalogOverviewCard(const MaterialsWorkbenchCatalogSnapshot &catalog) {
		const auto paletteCounts = CountPalettesByCategory(catalog);
		wxString text;
		text << "Catalog\n\n";
		text << "Author materials data from one SQLite-backed workspace.\n\n";
		text << "Workspace model\n";
		text << "  Palette Categories organize where palettes live.\n";
		text << "  Palettes define composition: which entries belong together and in what order.\n";
		text << "  Brushes define behavior, metadata, and runtime logic.\n";
		text << "  Specialized Editors cover borders and walls that need dedicated tooling.\n\n";
		text << "Recommended workflow\n";
		text << "  1. Open Palette Categories when you want to change palette composition.\n";
		text << "  2. Open Brushes when you want to change behavior, look IDs, or brush metadata.\n";
		text << "  3. Open Specialized Editors for border slots, wall parts, and related structures.\n\n";
		text << "Current catalog\n";
		text << "  Categories: " << paletteCounts.size() << "\n";
		text << "  Palettes: " << catalog.tilesets.size() << "\n";
		text << "  Brushes: " << catalog.auditReport.brushCount << "\n";
		text << "  Borders: " << (catalog.globalBorderSets.size() + catalog.inlineBorderSets.size()) << "\n";
		text << "  Walls: " << catalog.wallBrushes.size() << "\n\n";
		text << "Quality checks\n";
		text << "  Unresolved ground targets: " << catalog.auditReport.unresolvedGroundTargets << "\n";
		text << "  Unresolved brush links: " << catalog.auditReport.unresolvedBrushLinks << "\n";
		text << "  Unresolved tileset entries: " << catalog.auditReport.unresolvedTilesetEntries << "\n";
		text << "  Unresolved border case match_border ids: " << catalog.auditReport.unresolvedCaseMatchBorderIds << "\n";
		text << "  Unresolved border case replace_border target ids: " << catalog.auditReport.unresolvedCaseReplaceBorderTargetIds << "\n";
		text << "  Border case match_border edges without borderitem: " << catalog.auditReport.caseMatchBorderEdgesWithoutItem << "\n";
		text << "  Border case replace_border edges without borderitem: " << catalog.auditReport.caseReplaceBorderEdgesWithoutItem << "\n";
		return text;
	}

	wxString FormatPaletteCategoriesOverviewCard(const MaterialsWorkbenchCatalogSnapshot &catalog) {
		const auto paletteCounts = CountPalettesByCategory(catalog);
		wxString text;
		text << "Palette Categories\n\n";
		text << "Use this area to choose the palette category first, then the exact palette you want to edit.\n\n";
		text << "How to work here\n";
		text << "  1. Pick a category such as Terrain, Doodad, Item, or a custom category.\n";
		text << "  2. Pick a palette inside that category.\n";
		text << "  3. Edit entries, order, and move destinations inside the Palette Workspace.\n\n";
		text << "Current categories\n";
		for (const auto &[groupKey, count] : paletteCounts) {
			text << "  " << BuildPaletteGroupLabel(groupKey) << ": " << count << " palettes\n";
		}
		if (paletteCounts.empty()) {
			text << "  No palette categories available.\n";
		}
		text << "\nGood to know\n";
		text << "  Categories are organizational and appear in both Workbench and runtime palette trees.\n";
		text << "  Palette editing happens one palette at a time after you open it from this tree.\n";
		return text;
	}

	wxString FormatPaletteCategoryOverviewCard(const MaterialsWorkbenchCatalogSnapshot &catalog, const wxString &groupName) {
		size_t paletteCount = 0;
		size_t entryCount = 0;
		for (const TilesetStorageRecord &tileset : catalog.tilesets) {
			if (!BuildPaletteGroupKey(tileset).IsSameAs(groupName, false)) {
				continue;
			}
			++paletteCount;
			for (const TilesetSectionRecord &section : tileset.sections) {
				entryCount += section.entries.size();
			}
		}

		wxString text;
		text << BuildPaletteGroupLabel(groupName) << "\n\n";
		text << "Open a palette from this category when you want to edit composition inside the same domain.\n\n";
		text << "What belongs here\n";
		text << "  Palettes in this category are grouped together for authoring and runtime browsing.\n";
		text << "  Moves between palettes usually start from this organizational layer.\n\n";
		text << "Contents\n";
		text << "  Palettes: " << paletteCount << "\n";
		text << "  Stored entries: " << entryCount << "\n\n";
		text << "Recommended next step\n";
		text << "  Open a palette below to edit entries, order, and destination settings.\n";
		return text;
	}

	wxString FormatBrushesOverviewCard(const MaterialsWorkbenchCatalogSnapshot &catalog) {
		wxString text;
		text << "Brushes\n\n";
		text << "Edit brush behavior, metadata, and runtime-facing properties from here.\n\n";
		text << "When to use this area\n";
		text << "  Open Brushes when composition is already correct but the brush itself needs changes.\n";
		text << "  Typical edits here include look IDs, behavior flags, links, and brush-specific metadata.\n\n";
		text << "Brush domains\n";
		text << "  Terrain: " << CountBrushesInFamily(catalog, "terrain") << "\n";
		text << "  Doodad: " << CountBrushesInFamily(catalog, "doodad") << "\n";
		text << "  Item: " << CountBrushesInFamily(catalog, "item") << "\n";
		text << "  Other: " << CountBrushesInFamily(catalog, "other") << "\n\n";
		text << "Good to know\n";
		text << "  This tree mirrors the same palette-oriented context used during authoring.\n";
		text << "  If you need to add or move membership between palettes, use Palette Categories instead.\n";
		return text;
	}

	wxString FormatBrushFamilyOverviewCard(const MaterialsWorkbenchCatalogSnapshot &catalog, const wxString &familyKey) {
		wxString text;
		text << BuildBrushFamilyLabel(familyKey) << "\n\n";
		text << "This family narrows the brush list to one authoring domain.\n\n";
		text << "Use this area to\n";
		text << "  Open a palette bucket below for a smaller, more relevant brush list.\n";
		text << "  Open a brush directly when you already know what you want to change.\n\n";
		text << "Brush count\n";
		text << "  " << CountBrushesInFamily(catalog, familyKey) << " brushes\n";
		return text;
	}

	wxString FormatBrushPaletteOverviewCard(const MaterialsWorkbenchCatalogSnapshot &catalog, const wxString &familyKey, const wxString &paletteLabel) {
		size_t brushCount = 0;
		for (const MaterialsWorkbenchBrushGroup &group : catalog.brushGroups) {
			for (const BrushRecord &brush : group.brushes) {
				if (BuildBrushFamilyKeyFromBrushType(brush.type) != familyKey) {
					continue;
				}
				++brushCount;
			}
		}

		wxString text;
		text << BuildBrushFamilyLabel(familyKey) << " -> " << paletteLabel << "\n\n";
		text << "This bucket shows brushes related to the selected palette context.\n\n";
		text << "Use this area to\n";
		text << "  Open a brush from the list below.\n";
		text << "  Compare nearby brushes before editing behavior or metadata.\n\n";
		text << "Visible family total\n";
		text << "  " << brushCount << " brushes in this family\n";
		return text;
	}

	wxString FormatSpecializedEditorsOverviewCard(const MaterialsWorkbenchCatalogSnapshot &catalog) {
		wxString text;
		text << "Specialized Editors\n\n";
		text << "Use these editors for structures that need dedicated tooling beyond normal palette composition.\n\n";
		text << "When to come here\n";
		text << "  Use Borders for slot-based border sets and edge mappings.\n";
		text << "  Use Walls for wall brushes and wall-part composition.\n";
		text << "  Stay in Palette Categories when the task is only membership or ordering inside a palette.\n\n";
		text << "Available editors\n";
		text << "  Borders: " << (catalog.globalBorderSets.size() + catalog.inlineBorderSets.size()) << "\n";
		text << "  Walls: " << catalog.wallBrushes.size() << "\n\n";
		text << "Good to know\n";
		text << "  These editors affect runtime-facing structures with their own save and refresh rules.\n";
		return text;
	}

	wxString FormatBordersOverviewCard(const MaterialsWorkbenchCatalogSnapshot &catalog) {
		wxString text;
		text << "Borders\n\n";
		text << "Edit global and inline border sets, slot mappings, and border-specific relationships.\n\n";
		text << "Available border sets\n";
		text << "  Global: " << catalog.globalBorderSets.size() << "\n";
		text << "  Inline: " << catalog.inlineBorderSets.size() << "\n\n";
		text << "Recommended workflow\n";
		text << "  1. Choose a scope.\n";
		text << "  2. Open a border set.\n";
		text << "  3. Edit slot items and review runtime-facing identifiers carefully.\n";
		return text;
	}

	wxString FormatBorderScopeOverviewCard(const MaterialsWorkbenchCatalogSnapshot &catalog, const wxString &scope) {
		const size_t count = scope == "global" ? catalog.globalBorderSets.size() : catalog.inlineBorderSets.size();
		wxString text;
		text << "Border Scope: " << scope << "\n\n";
		text << "This scope filters the border sets shown below.\n\n";
		text << "Use this area to\n";
		text << "  Open a border set from this scope.\n";
		text << "  Keep global and inline data separated while editing.\n\n";
		text << "Count\n";
		text << "  " << count << " border sets\n";
		return text;
	}

	wxString FormatWallsOverviewCard(const MaterialsWorkbenchCatalogSnapshot &catalog) {
		wxString text;
		text << "Walls\n\n";
		text << "Edit wall brushes and their wall-part composition from here.\n\n";
		text << "Use this area to\n";
		text << "  Open a wall brush from the list below.\n";
		text << "  Adjust wall-specific metadata and related composition data.\n\n";
		text << "Available wall brushes\n";
		text << "  " << catalog.wallBrushes.size() << " wall brushes\n";
		return text;
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
	return FormatCatalogOverviewCard(catalog_);
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
	palettesNode.label = FormatNavigationCountLabel("Palette Categories", catalog_.tilesets.size());
	palettesNode.contextKey = "group:palettes";

	std::map<wxString, std::vector<int>> paletteIndexesByGroup;
	for (size_t i = 0; i < catalog_.tilesets.size(); ++i) {
		paletteIndexesByGroup[BuildPaletteGroupKey(catalog_.tilesets[i])].push_back(static_cast<int>(i));
	}

	for (const PaletteGroupRecord &group : catalog_.paletteGroups) {
		const auto groupIt = paletteIndexesByGroup.find(group.name);
		const std::vector<int> empty;
		const std::vector<int> &paletteIndexes = groupIt != paletteIndexesByGroup.end() ? groupIt->second : empty;

		MaterialsWorkbenchTreeNode groupNode;
		groupNode.kind = MaterialsWorkbenchNodeKind::Group;
		groupNode.label = FormatNavigationCountLabel(BuildPaletteGroupLabel(group.name), paletteIndexes.size());
		groupNode.contextKey = "palette_group:" + group.name;
		for (int tilesetIndex : paletteIndexes) {
			MaterialsWorkbenchTreeNode item;
			item.kind = MaterialsWorkbenchNodeKind::Tileset;
			item.label = catalog_.tilesets[tilesetIndex].name;
			item.contextKey = "tilesets";
			item.itemIndex = tilesetIndex;
			groupNode.children.push_back(std::move(item));
		}
		palettesNode.children.push_back(std::move(groupNode));
	}
	catalogNode.children.push_back(std::move(palettesNode));

	MaterialsWorkbenchTreeNode brushesNode;
	brushesNode.kind = MaterialsWorkbenchNodeKind::Group;
	brushesNode.label = FormatNavigationCountLabel("Brushes", catalog_.auditReport.brushCount);
	brushesNode.contextKey = "group:brushes";

	struct BrushNavigationPlacement {
		wxString familyKey;
		wxString paletteLabel;
	};

	std::map<wxString, BrushNavigationPlacement> brushPlacementByKey;
	std::map<wxString, std::vector<wxString>> paletteOrderByFamily;
	for (const TilesetStorageRecord &tileset : catalog_.tilesets) {
		const wxString familyKey = BuildPaletteGroupKey(tileset);
		auto &familyPaletteOrder = paletteOrderByFamily[familyKey];
		if (std::find(familyPaletteOrder.begin(), familyPaletteOrder.end(), tileset.name) == familyPaletteOrder.end()) {
			familyPaletteOrder.push_back(tileset.name);
		}

		for (const TilesetSectionRecord &section : tileset.sections) {
			for (const TilesetEntryRecord &entry : section.entries) {
				if (!entry.entryKind.IsSameAs("brush", false)) {
					continue;
				}
				if (entry.brushId <= 0 && entry.brushName.IsEmpty()) {
					continue;
				}

				const wxString brushKey = BuildBrushLookupKey(entry.brushId, entry.brushName);
				if (!brushPlacementByKey.count(brushKey)) {
					brushPlacementByKey[brushKey] = { familyKey, tileset.name };
				}
			}
		}
	}

	std::map<wxString, std::vector<wxString>> brushPaletteLabelsByFamily;
	std::map<wxString, std::map<wxString, std::vector<MaterialsWorkbenchTreeNode>>> brushNodesByFamilyAndPalette;
	std::map<wxString, size_t> brushCountByFamily;
	for (const MaterialsWorkbenchBrushGroup &group : catalog_.brushGroups) {
		for (size_t i = 0; i < group.brushes.size(); ++i) {
			const BrushRecord &brush = group.brushes[i];
			const wxString brushKey = BuildBrushLookupKey(brush.id, brush.name);
			const auto placementIt = brushPlacementByKey.find(brushKey);
			const wxString familyKey = placementIt != brushPlacementByKey.end() ? placementIt->second.familyKey : BuildBrushFamilyKeyFromBrushType(group.brushType);
			const wxString paletteLabel = placementIt != brushPlacementByKey.end() ? placementIt->second.paletteLabel : "Uncategorized";

			auto &familyPaletteLabels = brushPaletteLabelsByFamily[familyKey];
			if (std::find(familyPaletteLabels.begin(), familyPaletteLabels.end(), paletteLabel) == familyPaletteLabels.end()) {
				familyPaletteLabels.push_back(paletteLabel);
			}

			MaterialsWorkbenchTreeNode item;
			item.kind = MaterialsWorkbenchNodeKind::Brush;
			item.label = brush.name;
			item.contextKey = group.brushType;
			item.itemIndex = static_cast<int>(i);
			brushNodesByFamilyAndPalette[familyKey][paletteLabel].push_back(std::move(item));
			++brushCountByFamily[familyKey];
		}
	}

	std::vector<wxString> brushFamilyOrder;
	brushFamilyOrder.reserve(8);

	const auto pushFamilyIfNeeded = [&](const wxString &familyKey) {
		if (brushCountByFamily[familyKey] == 0) {
			return;
		}
		if (std::find(brushFamilyOrder.begin(), brushFamilyOrder.end(), familyKey) != brushFamilyOrder.end()) {
			return;
		}
		brushFamilyOrder.push_back(familyKey);
	};

	pushFamilyIfNeeded("terrain");
	pushFamilyIfNeeded("doodad");
	pushFamilyIfNeeded("item");
	pushFamilyIfNeeded("other");

	std::vector<PaletteGroupRecord> sortedPaletteGroups = catalog_.paletteGroups;
	std::sort(sortedPaletteGroups.begin(), sortedPaletteGroups.end(), [](const PaletteGroupRecord &left, const PaletteGroupRecord &right) {
		if (left.sortOrder != right.sortOrder) {
			return left.sortOrder < right.sortOrder;
		}
		return left.name.CmpNoCase(right.name) < 0;
	});
	for (const PaletteGroupRecord &group : sortedPaletteGroups) {
		if (group.name.IsSameAs("terrain", false) ||
			group.name.IsSameAs("doodad", false) ||
			group.name.IsSameAs("item", false) ||
			group.name.IsSameAs("other", false)) {
			continue;
		}
		pushFamilyIfNeeded(group.name);
	}

	std::vector<wxString> remainingFamilyKeys;
	for (const auto &entry : brushCountByFamily) {
		if (entry.second == 0) {
			continue;
		}
		if (std::find(brushFamilyOrder.begin(), brushFamilyOrder.end(), entry.first) != brushFamilyOrder.end()) {
			continue;
		}
		remainingFamilyKeys.push_back(entry.first);
	}
	std::sort(remainingFamilyKeys.begin(), remainingFamilyKeys.end(), [](const wxString &left, const wxString &right) {
		return left.CmpNoCase(right) < 0;
	});
	for (const wxString &familyKey : remainingFamilyKeys) {
		pushFamilyIfNeeded(familyKey);
	}

	for (const wxString &familyKey : brushFamilyOrder) {
		const size_t familyCount = brushCountByFamily[familyKey];
		if (familyCount == 0) {
			continue;
		}

		MaterialsWorkbenchTreeNode familyNode;
		familyNode.kind = MaterialsWorkbenchNodeKind::Group;
		familyNode.label = FormatNavigationCountLabel(BuildBrushFamilyLabel(familyKey), familyCount);
		familyNode.contextKey = "brush_family:" + familyKey;

		std::vector<wxString> paletteLabels = paletteOrderByFamily[familyKey];
		for (const wxString &paletteLabel : brushPaletteLabelsByFamily[familyKey]) {
			if (std::find(paletteLabels.begin(), paletteLabels.end(), paletteLabel) == paletteLabels.end()) {
				paletteLabels.push_back(paletteLabel);
			}
		}

		for (const wxString &paletteLabel : paletteLabels) {
			auto familyIt = brushNodesByFamilyAndPalette.find(familyKey);
			if (familyIt == brushNodesByFamilyAndPalette.end()) {
				continue;
			}
			auto paletteIt = familyIt->second.find(paletteLabel);
			if (paletteIt == familyIt->second.end() || paletteIt->second.empty()) {
				continue;
			}

			MaterialsWorkbenchTreeNode paletteNode;
			paletteNode.kind = MaterialsWorkbenchNodeKind::Group;
			paletteNode.label = FormatNavigationCountLabel(paletteLabel, paletteIt->second.size());
			paletteNode.contextKey = "brush_palette:" + familyKey + ":" + paletteLabel;
			paletteNode.children = paletteIt->second;
			familyNode.children.push_back(std::move(paletteNode));
		}

		brushesNode.children.push_back(std::move(familyNode));
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

bool MaterialsWorkbenchController::LocateTilesetNode(const wxString &name, int &outItemIndex) const {
	for (size_t i = 0; i < catalog_.tilesets.size(); ++i) {
		if (catalog_.tilesets[i].name == name) {
			outItemIndex = static_cast<int>(i);
			return true;
		}
	}

	outItemIndex = -1;
	return false;
}

bool MaterialsWorkbenchController::HasTilesetNamed(const wxString &name) const {
	for (const TilesetStorageRecord &tileset : catalog_.tilesets) {
		if (tileset.name.IsSameAs(name, false)) {
			return true;
		}
	}
	return false;
}

bool MaterialsWorkbenchController::HasPaletteGroupNamed(const wxString &name) const {
	for (const PaletteGroupRecord &group : catalog_.paletteGroups) {
		if (group.name.IsSameAs(name, false)) {
			return true;
		}
	}
	return false;
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

bool MaterialsWorkbenchController::GetBorderSetUsages(int64_t borderSetId, std::vector<BorderSetUsageRecord> &outUsages, wxString &error) const {
	return repository_.LoadBorderSetUsages(borderSetId, outUsages, error);
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

bool MaterialsWorkbenchController::DeleteBrush(int64_t brushId, wxString &error) {
	if (!repository_.DeleteBrush(brushId, error)) {
		return false;
	}
	if (!ReloadCatalog()) {
		error = lastError_;
		return false;
	}
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

bool MaterialsWorkbenchController::SaveGroundBrushBorders(int64_t brushId, const std::vector<GroundBrushBorderRecord> &borders, wxString &error) {
	return repository_.SaveGroundBrushBorders(brushId, borders, error);
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

bool MaterialsWorkbenchController::DeleteBorderSet(int64_t borderSetId, wxString &error) {
	if (!repository_.DeleteBorderSet(borderSetId, error)) {
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

bool MaterialsWorkbenchController::GetBrushUsages(int64_t brushId, const wxString &brushName, std::vector<BrushUsageRecord> &outUsages, wxString &error) const {
	return repository_.LoadBrushUsages(brushId, brushName, outUsages, error);
}

int MaterialsWorkbenchController::SuggestNextBorderId() const {
	int maxBorderId = 0;
	for (const BorderSetRecord &border : catalog_.globalBorderSets) {
		maxBorderId = std::max(maxBorderId, border.xmlBorderId);
	}
	for (const BorderSetRecord &border : catalog_.inlineBorderSets) {
		maxBorderId = std::max(maxBorderId, border.xmlBorderId);
	}
	return maxBorderId + 1;
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
	return SaveTileset(tileset, wxString(), error);
}

bool MaterialsWorkbenchController::SaveTileset(const TilesetStorageRecord &tileset, const wxString &previousName, wxString &error) {
	if (!repository_.SaveTileset(tileset, previousName, error)) {
		return false;
	}

	if (!ReloadCatalog()) {
		error = lastError_;
		return false;
	}

	return true;
}

bool MaterialsWorkbenchController::SaveTilesetWithoutReload(const TilesetStorageRecord &tileset, wxString &error) {
	return repository_.SaveTileset(tileset, error);
}

bool MaterialsWorkbenchController::DeleteTileset(const wxString &name, wxString &error) {
	if (!repository_.DeleteTileset(name, error)) {
		return false;
	}

	if (!ReloadCatalog()) {
		error = lastError_;
		return false;
	}

	return true;
}

bool MaterialsWorkbenchController::SavePaletteGroup(const PaletteGroupRecord &group, wxString &error) {
	if (!repository_.SavePaletteGroup(group, error)) {
		return false;
	}

	if (!ReloadCatalog()) {
		error = lastError_;
		return false;
	}

	return true;
}

bool MaterialsWorkbenchController::SavePaletteGroupWithoutReload(const PaletteGroupRecord &group, wxString &error) {
	return repository_.SavePaletteGroup(group, error);
}

bool MaterialsWorkbenchController::DeletePaletteGroup(const wxString &name, wxString &error) {
	if (!repository_.DeletePaletteGroup(name, error)) {
		return false;
	}

	if (!ReloadCatalog()) {
		error = lastError_;
		return false;
	}

	return true;
}

bool MaterialsWorkbenchController::DeletePaletteGroupAndReassignPalettes(const wxString &name, const wxString &destinationName, int &outMovedPaletteCount, wxString &error) {
	outMovedPaletteCount = 0;
	error.clear();

	if (name.IsSameAs(destinationName, false)) {
		error = "Choose a different destination category before deleting this one.";
		return false;
	}

	const auto destinationIt = std::find_if(catalog_.paletteGroups.begin(), catalog_.paletteGroups.end(), [&](const PaletteGroupRecord &group) {
		return group.name.IsSameAs(destinationName, false);
	});
	if (destinationIt == catalog_.paletteGroups.end()) {
		error = "The destination category no longer exists.";
		return false;
	}

	if (!repository_.DeletePaletteGroupAndReassignTilesets(name, destinationName, outMovedPaletteCount, error)) {
		return false;
	}

	if (!ReloadCatalog()) {
		error = lastError_;
		return false;
	}

	return true;
}

const std::vector<PaletteGroupRecord> &MaterialsWorkbenchController::GetPaletteGroups() const {
	return catalog_.paletteGroups;
}

const std::vector<TilesetStorageRecord> &MaterialsWorkbenchController::GetTilesets() const {
	return catalog_.tilesets;
}

const std::vector<MaterialsWorkbenchBrushGroup> &MaterialsWorkbenchController::GetBrushGroups() const {
	return catalog_.brushGroups;
}

const std::vector<BrushRecord> &MaterialsWorkbenchController::GetWallBrushes() const {
	return catalog_.wallBrushes;
}

const std::vector<BorderSetRecord> &MaterialsWorkbenchController::GetGlobalBorderSets() const {
	return catalog_.globalBorderSets;
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
		if (contextKey == "group:catalog") {
			return FormatCatalogOverviewCard(catalog_);
		}
		if (contextKey == "group:palettes") {
			return FormatPaletteCategoriesOverviewCard(catalog_);
		}
		if (contextKey.StartsWith("palette_group:")) {
			const wxString groupName = contextKey.AfterFirst(':');
			return FormatPaletteCategoryOverviewCard(catalog_, groupName);
		}
		if (contextKey == "group:brushes") {
			return FormatBrushesOverviewCard(catalog_);
		}
		if (contextKey == "group:specialized") {
			return FormatSpecializedEditorsOverviewCard(catalog_);
		}
		if (contextKey == "group:borders") {
			return FormatBordersOverviewCard(catalog_);
		}
		if (contextKey == "group:walls") {
			return FormatWallsOverviewCard(catalog_);
		}
		if (contextKey.StartsWith("brush_family:")) {
			const wxString familyKey = contextKey.AfterFirst(':');
			return FormatBrushFamilyOverviewCard(catalog_, familyKey);
		}
		if (contextKey.StartsWith("brush_palette:")) {
			wxString remainder = contextKey.AfterFirst(':');
			const wxString familyKey = remainder.BeforeFirst(':');
			const wxString paletteLabel = remainder.AfterFirst(':');
			return FormatBrushPaletteOverviewCard(catalog_, familyKey, paletteLabel);
		}
		if (contextKey.StartsWith("border_scope:")) {
			const wxString scope = contextKey.AfterFirst(':');
			return FormatBorderScopeOverviewCard(catalog_, scope);
		}
		return FormatCatalogOverviewCard(catalog_);
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

	return FormatCatalogOverviewCard(catalog_);
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
