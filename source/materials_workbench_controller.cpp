#include "main.h"

#include "materials_workbench_controller.h"

#include <map>

#include <wx/arrstr.h>

namespace {
	wxString FormatInspectorImportedFromValue(const wxString &sourceFile) {
		return sourceFile.IsEmpty() ? wxString::FromUTF8("Not imported from legacy XML") : sourceFile;
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

MaterialsWorkbenchControllerCoreApi::MaterialsWorkbenchControllerCoreApi(MaterialsWorkbenchControllerState &state) :
	state_(state) {
}

MaterialsWorkbenchControllerNavigationApi::MaterialsWorkbenchControllerNavigationApi(MaterialsWorkbenchControllerState &state) :
	state_(state) {
}

MaterialsWorkbenchControllerTilesetsApi::MaterialsWorkbenchControllerTilesetsApi(MaterialsWorkbenchControllerState &state) :
	state_(state) {
}

MaterialsWorkbenchControllerBrushesApi::MaterialsWorkbenchControllerBrushesApi(MaterialsWorkbenchControllerState &state) :
	state_(state) {
}

MaterialsWorkbenchControllerBordersApi::MaterialsWorkbenchControllerBordersApi(MaterialsWorkbenchControllerState &state) :
	state_(state) {
}

MaterialsWorkbenchController::MaterialsWorkbenchController() :
	MaterialsWorkbenchControllerState(),
	MaterialsWorkbenchControllerCoreApi(static_cast<MaterialsWorkbenchControllerState&>(*this)),
	MaterialsWorkbenchControllerNavigationApi(static_cast<MaterialsWorkbenchControllerState&>(*this)),
	MaterialsWorkbenchControllerTilesetsApi(static_cast<MaterialsWorkbenchControllerState&>(*this)),
	MaterialsWorkbenchControllerBrushesApi(static_cast<MaterialsWorkbenchControllerState&>(*this)),
	MaterialsWorkbenchControllerBordersApi(static_cast<MaterialsWorkbenchControllerState&>(*this)) {
}

namespace {
	bool ReloadCatalogSnapshot(MaterialsWorkbenchControllerState &state) {
		state.lastError().clear();
		if (!state.repository().LoadCatalog(state.catalog(), state.lastError())) {
			spdlog::warn("Materials Workbench failed to reload catalog from materials.db: {}", state.lastError().ToStdString());
			return false;
		}

		spdlog::info(
			"Materials Workbench reloaded catalog from materials.db: brushes={} palettes={} walls={}",
			state.catalog().auditReport.brushCount,
			state.catalog().tilesets.size(),
			state.catalog().wallBrushes.size()
		);
		return true;
	}
} // namespace

bool MaterialsWorkbenchControllerCoreApi::ReloadCatalog() {
	return ReloadCatalogSnapshot(state_);
}

wxString MaterialsWorkbenchControllerCoreApi::GetWindowTitle() const {
	return "Materials Workbench";
}

wxString MaterialsWorkbenchControllerCoreApi::GetOverviewText() const {
	if (!state_.lastError().IsEmpty()) {
		return "Materials Workbench could not load the SQLite catalog.\n\nError: " + state_.lastError();
	}
	return FormatCatalogOverviewCard(state_.catalog());
}

wxString MaterialsWorkbenchControllerCoreApi::GetInspectorText() const {
	if (!state_.lastError().IsEmpty()) {
		return "Inspector unavailable while the catalog failed to load.";
	}

	return "Catalog loaded from materials.db.\n\n"
		   "Select a palette, brush, border set or wall entry in the navigation tree to inspect its SQLite-backed metadata.";
}

namespace {
	MaterialsWorkbenchTreeNode MakeGroupNode(const wxString &label, const wxString &contextKey) {
		MaterialsWorkbenchTreeNode node;
		node.kind = MaterialsWorkbenchNodeKind::Group;
		node.label = label;
		node.contextKey = contextKey;
		return node;
	}

	MaterialsWorkbenchTreeNode MakeItemNode(MaterialsWorkbenchNodeKind kind, const wxString &label, const wxString &contextKey, int itemIndex) {
		MaterialsWorkbenchTreeNode node;
		node.kind = kind;
		node.label = label;
		node.contextKey = contextKey;
		node.itemIndex = itemIndex;
		return node;
	}

	MaterialsWorkbenchTreeNode BuildPalettesNavigationNode(const MaterialsWorkbenchCatalogSnapshot &catalog) {
		MaterialsWorkbenchTreeNode palettesNode = MakeGroupNode(
			FormatNavigationCountLabel("Palette Categories", catalog.tilesets.size()),
			"group:palettes"
		);

		std::map<wxString, std::vector<int>> paletteIndexesByGroup;
		for (size_t i = 0; i < catalog.tilesets.size(); ++i) {
			paletteIndexesByGroup[BuildPaletteGroupKey(catalog.tilesets[i])].push_back(static_cast<int>(i));
		}

		for (const PaletteGroupRecord &group : catalog.paletteGroups) {
			const auto groupIt = paletteIndexesByGroup.find(group.name);
			const std::vector<int> empty;
			const std::vector<int> &paletteIndexes = groupIt != paletteIndexesByGroup.end() ? groupIt->second : empty;

			MaterialsWorkbenchTreeNode groupNode = MakeGroupNode(
				FormatNavigationCountLabel(BuildPaletteGroupLabel(group.name), paletteIndexes.size()),
				"palette_group:" + group.name
			);
			for (int tilesetIndex : paletteIndexes) {
				groupNode.children.push_back(MakeItemNode(
					MaterialsWorkbenchNodeKind::Tileset,
					catalog.tilesets[tilesetIndex].name,
					"tilesets",
					tilesetIndex
				));
			}
			palettesNode.children.push_back(std::move(groupNode));
		}
		return palettesNode;
	}

	struct BrushNavigationPlacement {
		wxString familyKey;
		wxString paletteLabel;
	};

	std::map<wxString, BrushNavigationPlacement> BuildBrushPlacementByKey(
		const MaterialsWorkbenchCatalogSnapshot &catalog,
		std::map<wxString, std::vector<wxString>> &outPaletteOrderByFamily
	) {
		std::map<wxString, BrushNavigationPlacement> brushPlacementByKey;
		for (const TilesetStorageRecord &tileset : catalog.tilesets) {
			const wxString familyKey = BuildPaletteGroupKey(tileset);
			auto &familyPaletteOrder = outPaletteOrderByFamily[familyKey];
			if (std::find(familyPaletteOrder.begin(), familyPaletteOrder.end(), tileset.name) == familyPaletteOrder.end()) {
				familyPaletteOrder.push_back(tileset.name);
			}

			for (const TilesetSectionRecord &section : tileset.sections) {
				for (const TilesetEntryRecord &entry : section.entries) {
					if (!entry.entryKind.IsSameAs("brush", false) || (entry.brushId <= 0 && entry.brushName.IsEmpty())) {
						continue;
					}

					const wxString brushKey = BuildBrushLookupKey(entry.brushId, entry.brushName);
					if (!brushPlacementByKey.count(brushKey)) {
						brushPlacementByKey[brushKey] = { familyKey, tileset.name };
					}
				}
			}
		}
		return brushPlacementByKey;
	}

	void CollectBrushNavigationBuckets(
		const MaterialsWorkbenchCatalogSnapshot &catalog,
		const std::map<wxString, BrushNavigationPlacement> &brushPlacementByKey,
		std::map<wxString, std::vector<wxString>> &outBrushPaletteLabelsByFamily,
		std::map<wxString, std::map<wxString, std::vector<MaterialsWorkbenchTreeNode>>> &outBrushNodesByFamilyAndPalette,
		std::map<wxString, size_t> &outBrushCountByFamily
	) {
		for (const MaterialsWorkbenchBrushGroup &group : catalog.brushGroups) {
			for (size_t i = 0; i < group.brushes.size(); ++i) {
				const BrushRecord &brush = group.brushes[i];
				const wxString brushKey = BuildBrushLookupKey(brush.id, brush.name);
				const auto placementIt = brushPlacementByKey.find(brushKey);
				const wxString familyKey = placementIt != brushPlacementByKey.end()
					? placementIt->second.familyKey
					: BuildBrushFamilyKeyFromBrushType(group.brushType);
				const wxString paletteLabel = placementIt != brushPlacementByKey.end()
					? placementIt->second.paletteLabel
					: wxString::FromUTF8("Uncategorized");

				auto &familyPaletteLabels = outBrushPaletteLabelsByFamily[familyKey];
				if (std::find(familyPaletteLabels.begin(), familyPaletteLabels.end(), paletteLabel) == familyPaletteLabels.end()) {
					familyPaletteLabels.push_back(paletteLabel);
				}

				outBrushNodesByFamilyAndPalette[familyKey][paletteLabel].push_back(MakeItemNode(
					MaterialsWorkbenchNodeKind::Brush,
					brush.name,
					group.brushType,
					static_cast<int>(i)
				));
				++outBrushCountByFamily[familyKey];
			}
		}
	}

	void PushFamilyIfNeeded(const std::map<wxString, size_t> &brushCountByFamily, const wxString &familyKey, std::vector<wxString> &inOutOrder) {
		const auto it = brushCountByFamily.find(familyKey);
		if (it == brushCountByFamily.end() || it->second == 0) {
			return;
		}
		if (std::find(inOutOrder.begin(), inOutOrder.end(), familyKey) != inOutOrder.end()) {
			return;
		}
		inOutOrder.push_back(familyKey);
	}

	std::vector<wxString> BuildBrushFamilyOrder(const MaterialsWorkbenchCatalogSnapshot &catalog, const std::map<wxString, size_t> &brushCountByFamily) {
		std::vector<wxString> order;
		order.reserve(8);

		PushFamilyIfNeeded(brushCountByFamily, "terrain", order);
		PushFamilyIfNeeded(brushCountByFamily, "doodad", order);
		PushFamilyIfNeeded(brushCountByFamily, "item", order);
		PushFamilyIfNeeded(brushCountByFamily, "other", order);

		std::vector<PaletteGroupRecord> sortedPaletteGroups = catalog.paletteGroups;
		std::sort(sortedPaletteGroups.begin(), sortedPaletteGroups.end(), [](const PaletteGroupRecord &left, const PaletteGroupRecord &right) {
			if (left.sortOrder != right.sortOrder) {
				return left.sortOrder < right.sortOrder;
			}
			return left.name.CmpNoCase(right.name) < 0;
		});
		for (const PaletteGroupRecord &group : sortedPaletteGroups) {
			if (group.name.IsSameAs("terrain", false) || group.name.IsSameAs("doodad", false) || group.name.IsSameAs("item", false) || group.name.IsSameAs("other", false)) {
				continue;
			}
			PushFamilyIfNeeded(brushCountByFamily, group.name, order);
		}

		std::vector<wxString> remaining;
		for (const auto &entry : brushCountByFamily) {
			if (entry.second == 0) {
				continue;
			}
			if (std::find(order.begin(), order.end(), entry.first) != order.end()) {
				continue;
			}
			remaining.push_back(entry.first);
		}
		std::sort(remaining.begin(), remaining.end(), [](const wxString &left, const wxString &right) {
			return left.CmpNoCase(right) < 0;
		});
		for (const wxString &familyKey : remaining) {
			PushFamilyIfNeeded(brushCountByFamily, familyKey, order);
		}
		return order;
	}

	std::vector<wxString> BuildPaletteLabelOrder(
		const wxString &familyKey,
		const std::map<wxString, std::vector<wxString>> &paletteOrderByFamily,
		const std::map<wxString, std::vector<wxString>> &brushPaletteLabelsByFamily
	) {
		std::vector<wxString> paletteLabels = paletteOrderByFamily.count(familyKey) > 0 ? paletteOrderByFamily.at(familyKey) : std::vector<wxString>();
		const auto it = brushPaletteLabelsByFamily.find(familyKey);
		if (it != brushPaletteLabelsByFamily.end()) {
			for (const wxString &paletteLabel : it->second) {
				if (std::find(paletteLabels.begin(), paletteLabels.end(), paletteLabel) == paletteLabels.end()) {
					paletteLabels.push_back(paletteLabel);
				}
			}
		}
		return paletteLabels;
	}

	MaterialsWorkbenchTreeNode BuildBrushesNavigationNode(const MaterialsWorkbenchCatalogSnapshot &catalog) {
		MaterialsWorkbenchTreeNode brushesNode = MakeGroupNode(
			FormatNavigationCountLabel("Brushes", catalog.auditReport.brushCount),
			"group:brushes"
		);

		std::map<wxString, std::vector<wxString>> paletteOrderByFamily;
		const auto brushPlacementByKey = BuildBrushPlacementByKey(catalog, paletteOrderByFamily);

		std::map<wxString, std::vector<wxString>> brushPaletteLabelsByFamily;
		std::map<wxString, std::map<wxString, std::vector<MaterialsWorkbenchTreeNode>>> brushNodesByFamilyAndPalette;
		std::map<wxString, size_t> brushCountByFamily;
		CollectBrushNavigationBuckets(catalog, brushPlacementByKey, brushPaletteLabelsByFamily, brushNodesByFamilyAndPalette, brushCountByFamily);

		const std::vector<wxString> familyOrder = BuildBrushFamilyOrder(catalog, brushCountByFamily);
		for (const wxString &familyKey : familyOrder) {
			const auto familyCountIt = brushCountByFamily.find(familyKey);
			const size_t familyCount = familyCountIt != brushCountByFamily.end() ? familyCountIt->second : 0;
			if (familyCount == 0) {
				continue;
			}

			MaterialsWorkbenchTreeNode familyNode = MakeGroupNode(
				FormatNavigationCountLabel(BuildBrushFamilyLabel(familyKey), familyCount),
				"brush_family:" + familyKey
			);

			const auto paletteLabels = BuildPaletteLabelOrder(familyKey, paletteOrderByFamily, brushPaletteLabelsByFamily);
			for (const wxString &paletteLabel : paletteLabels) {
				const auto familyIt = brushNodesByFamilyAndPalette.find(familyKey);
				if (familyIt == brushNodesByFamilyAndPalette.end()) {
					continue;
				}
				const auto paletteIt = familyIt->second.find(paletteLabel);
				if (paletteIt == familyIt->second.end() || paletteIt->second.empty()) {
					continue;
				}

				MaterialsWorkbenchTreeNode paletteNode = MakeGroupNode(
					FormatNavigationCountLabel(paletteLabel, paletteIt->second.size()),
					"brush_palette:" + familyKey + ":" + paletteLabel
				);
				paletteNode.children = paletteIt->second;
				familyNode.children.push_back(std::move(paletteNode));
			}

			brushesNode.children.push_back(std::move(familyNode));
		}

		return brushesNode;
	}

	MaterialsWorkbenchTreeNode BuildBordersNavigationNode(const MaterialsWorkbenchCatalogSnapshot &catalog) {
		MaterialsWorkbenchTreeNode bordersNode = MakeGroupNode(
			FormatNavigationCountLabel("Borders", catalog.globalBorderSets.size() + catalog.inlineBorderSets.size()),
			"group:borders"
		);

		const struct BorderScopeNode {
			const char* label;
			const char* contextKey;
			const std::vector<BorderSetRecord>* collection;
		} borderScopes[] = {
			{ "Inline Border Sets", "inline", &catalog.inlineBorderSets },
			{ "Global Border Sets", "global", &catalog.globalBorderSets },
		};

		for (const BorderScopeNode &scope : borderScopes) {
			MaterialsWorkbenchTreeNode scopeNode = MakeGroupNode(
				FormatNavigationCountLabel(scope.label, scope.collection->size()),
				"border_scope:" + wxString::FromUTF8(scope.contextKey)
			);
			for (size_t i = 0; i < scope.collection->size(); ++i) {
				const BorderSetRecord &record = (*scope.collection)[i];
				scopeNode.children.push_back(MakeItemNode(
					MaterialsWorkbenchNodeKind::BorderSet,
					BuildBorderSetNavigationLabel(record),
					wxString::FromUTF8(scope.contextKey),
					static_cast<int>(i)
				));
			}
			bordersNode.children.push_back(std::move(scopeNode));
		}

		return bordersNode;
	}

	MaterialsWorkbenchTreeNode BuildWallsNavigationNode(const MaterialsWorkbenchCatalogSnapshot &catalog) {
		MaterialsWorkbenchTreeNode wallsNode = MakeGroupNode(
			FormatNavigationCountLabel("Walls", catalog.wallBrushes.size()),
			"group:walls"
		);
		for (size_t i = 0; i < catalog.wallBrushes.size(); ++i) {
			wallsNode.children.push_back(MakeItemNode(
				MaterialsWorkbenchNodeKind::Brush,
				catalog.wallBrushes[i].name,
				"wall",
				static_cast<int>(i)
			));
		}
		return wallsNode;
	}
} // namespace

std::vector<MaterialsWorkbenchTreeNode> MaterialsWorkbenchControllerNavigationApi::BuildNavigationTree() const {
	std::vector<MaterialsWorkbenchTreeNode> nodes;

	const MaterialsWorkbenchCatalogSnapshot &catalog = state_.catalog();
	MaterialsWorkbenchTreeNode catalogNode = MakeGroupNode("Catalog", "group:catalog");
	catalogNode.children.push_back(BuildPalettesNavigationNode(catalog));
	catalogNode.children.push_back(BuildBrushesNavigationNode(catalog));
	nodes.push_back(std::move(catalogNode));

	MaterialsWorkbenchTreeNode editorsNode = MakeGroupNode("Specialized Editors", "group:specialized");
	editorsNode.children.push_back(BuildBordersNavigationNode(catalog));
	editorsNode.children.push_back(BuildWallsNavigationNode(catalog));
	nodes.push_back(std::move(editorsNode));

	return nodes;
}

namespace {
	const MaterialsWorkbenchBrushGroup* FindBrushGroupInCatalog(const MaterialsWorkbenchCatalogSnapshot &catalog, const wxString &contextKey) {
		for (const MaterialsWorkbenchBrushGroup &group : catalog.brushGroups) {
			if (group.brushType.IsSameAs(contextKey, false)) {
				return &group;
			}
		}
		return nullptr;
	}

	const BrushRecord* FindBrushRecordInCatalog(const MaterialsWorkbenchCatalogSnapshot &catalog, const wxString &contextKey, int itemIndex) {
		if (contextKey.IsSameAs("wall", false)) {
			if (itemIndex < 0 || itemIndex >= static_cast<int>(catalog.wallBrushes.size())) {
				return nullptr;
			}
			return &catalog.wallBrushes[itemIndex];
		}
		const MaterialsWorkbenchBrushGroup* group = FindBrushGroupInCatalog(catalog, contextKey);
		if (!group) {
			return nullptr;
		}
		if (itemIndex < 0 || itemIndex >= static_cast<int>(group->brushes.size())) {
			return nullptr;
		}
		return &group->brushes[itemIndex];
	}

	const BorderSetRecord* FindBorderSetRecordInCatalog(const MaterialsWorkbenchCatalogSnapshot &catalog, const wxString &contextKey, int itemIndex) {
		const bool isGlobal = contextKey.IsSameAs("global", false);
		const bool isInline = contextKey.IsSameAs("inline", false);
		const auto &collection = isInline ? catalog.inlineBorderSets : catalog.globalBorderSets;
		if (!isInline && !isGlobal) {
			return nullptr;
		}
		if (itemIndex < 0 || itemIndex >= static_cast<int>(collection.size())) {
			return nullptr;
		}
		return &collection[itemIndex];
	}
} // namespace

bool MaterialsWorkbenchControllerTilesetsApi::GetTilesetByIndex(int itemIndex, TilesetStorageRecord &outTileset) const {
	outTileset = TilesetStorageRecord();
	if (itemIndex < 0 || itemIndex >= static_cast<int>(state_.catalog().tilesets.size())) {
		return false;
	}
	outTileset = state_.catalog().tilesets[itemIndex];
	return true;
}

bool MaterialsWorkbenchControllerTilesetsApi::LocateTilesetNode(const wxString &name, int &outItemIndex) const {
	for (size_t i = 0; i < state_.catalog().tilesets.size(); ++i) {
		if (state_.catalog().tilesets[i].name.IsSameAs(name, false)) {
			outItemIndex = static_cast<int>(i);
			return true;
		}
	}

	outItemIndex = -1;
	return false;
}

bool MaterialsWorkbenchControllerTilesetsApi::HasTilesetNamed(const wxString &name) const {
	for (const TilesetStorageRecord &tileset : state_.catalog().tilesets) {
		if (tileset.name.IsSameAs(name, false)) {
			return true;
		}
	}
	return false;
}

bool MaterialsWorkbenchControllerTilesetsApi::HasPaletteGroupNamed(const wxString &name) const {
	for (const PaletteGroupRecord &group : state_.catalog().paletteGroups) {
		if (group.name.IsSameAs(name, false)) {
			return true;
		}
	}
	return false;
}

bool MaterialsWorkbenchControllerBrushesApi::GetBrushDetails(const wxString &contextKey, int itemIndex, BrushStorageRecord &outBrush, wxString &error) const {
	const BrushRecord* brush = FindBrushRecordInCatalog(state_.catalog(), contextKey, itemIndex);
	if (!brush) {
		error = "Brush details are unavailable.";
		outBrush = BrushStorageRecord();
		return false;
	}
	return state_.repository().LoadBrushDetails(brush->id, outBrush, error);
}

bool MaterialsWorkbenchControllerBordersApi::GetBorderSetDetails(const wxString &contextKey, int itemIndex, BorderSetStorageRecord &outBorderSet, wxString &error) const {
	const BorderSetRecord* borderSet = FindBorderSetRecordInCatalog(state_.catalog(), contextKey, itemIndex);
	if (!borderSet) {
		error = "Border set details are unavailable.";
		outBorderSet = BorderSetStorageRecord();
		return false;
	}
	return state_.repository().LoadBorderSetDetails(borderSet->id, outBorderSet, error);
}

bool MaterialsWorkbenchControllerBordersApi::GetBorderSetUsages(int64_t borderSetId, std::vector<BorderSetUsageRecord> &outUsages, wxString &error) const {
	return state_.repository().LoadBorderSetUsages(borderSetId, outUsages, error);
}

bool MaterialsWorkbenchControllerBrushesApi::SaveBrushDetails(BrushStorageRecord &brushStorage, wxString &error) {
	if (!state_.repository().SaveBrushDetails(brushStorage, error)) {
		spdlog::warn(
			"Materials Workbench failed to save brush details: id={} name='{}' error='{}'",
			static_cast<long long>(brushStorage.brush.id),
			brushStorage.brush.name.ToStdString(),
			error.ToStdString()
		);
		return false;
	}

	if (!ReloadCatalogSnapshot(state_)) {
		error = state_.lastError();
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

bool MaterialsWorkbenchControllerBrushesApi::DeleteBrush(int64_t brushId, wxString &error) {
	if (!state_.repository().DeleteBrush(brushId, error)) {
		return false;
	}
	if (!ReloadCatalogSnapshot(state_)) {
		error = state_.lastError();
		return false;
	}
	return true;
}

bool MaterialsWorkbenchControllerBrushesApi::SaveWallBrushParts(BrushStorageRecord &brushStorage, wxString &error) {
	if (!state_.repository().SaveWallBrushParts(brushStorage, error)) {
		return false;
	}

	if (!ReloadCatalogSnapshot(state_)) {
		error = state_.lastError();
		return false;
	}

	return true;
}

bool MaterialsWorkbenchControllerBrushesApi::SaveGroundBrushBorders(int64_t brushId, const std::vector<GroundBrushBorderRecord> &borders, wxString &error) {
	return state_.repository().SaveGroundBrushBorders(brushId, borders, error);
}

bool MaterialsWorkbenchControllerBordersApi::SaveBorderSet(BorderSetStorageRecord &borderSet, wxString &error) {
	if (!state_.repository().SaveBorderSet(borderSet, error)) {
		return false;
	}

	if (!ReloadCatalogSnapshot(state_)) {
		error = state_.lastError();
		return false;
	}

	return true;
}

bool MaterialsWorkbenchControllerBordersApi::DeleteBorderSet(int64_t borderSetId, wxString &error) {
	if (!state_.repository().DeleteBorderSet(borderSetId, error)) {
		return false;
	}

	if (!ReloadCatalogSnapshot(state_)) {
		error = state_.lastError();
		return false;
	}

	return true;
}

bool MaterialsWorkbenchControllerBrushesApi::LocateBrushNode(int64_t brushId, wxString &outContextKey, int &outItemIndex) const {
	for (size_t i = 0; i < state_.catalog().wallBrushes.size(); ++i) {
		if (state_.catalog().wallBrushes[i].id == brushId) {
			outContextKey = "wall";
			outItemIndex = static_cast<int>(i);
			return true;
		}
	}

	for (const MaterialsWorkbenchBrushGroup &group : state_.catalog().brushGroups) {
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

bool MaterialsWorkbenchControllerBrushesApi::ResolveBrushIdByNameAndType(const wxString &name, const wxString &type, int64_t &outBrushId, wxString &error) const {
	outBrushId = 0;
	error.clear();
	BrushRecord brush;
	if (!state_.repository().FindBrushByNameAndType(name, type, brush, error)) {
		outBrushId = 0;
		return false;
	}
	outBrushId = brush.id;
	if (outBrushId <= 0) {
		error = "Brush id could not be resolved.";
		return false;
	}
	return true;
}

bool MaterialsWorkbenchControllerBrushesApi::GetBrushUsages(int64_t brushId, const wxString &brushName, std::vector<BrushUsageRecord> &outUsages, wxString &error) const {
	return state_.repository().LoadBrushUsages(brushId, brushName, outUsages, error);
}

int MaterialsWorkbenchControllerBordersApi::SuggestNextBorderId() const {
	int maxBorderId = 0;
	for (const BorderSetRecord &border : state_.catalog().globalBorderSets) {
		maxBorderId = std::max(maxBorderId, border.xmlBorderId);
	}
	for (const BorderSetRecord &border : state_.catalog().inlineBorderSets) {
		maxBorderId = std::max(maxBorderId, border.xmlBorderId);
	}
	return maxBorderId + 1;
}

bool MaterialsWorkbenchControllerBordersApi::LocateBorderSetNode(int64_t borderSetId, wxString &outContextKey, int &outItemIndex) const {
	for (size_t i = 0; i < state_.catalog().globalBorderSets.size(); ++i) {
		if (state_.catalog().globalBorderSets[i].id == borderSetId) {
			outContextKey = "global";
			outItemIndex = static_cast<int>(i);
			return true;
		}
	}

	for (size_t i = 0; i < state_.catalog().inlineBorderSets.size(); ++i) {
		if (state_.catalog().inlineBorderSets[i].id == borderSetId) {
			outContextKey = "inline";
			outItemIndex = static_cast<int>(i);
			return true;
		}
	}

	outContextKey.clear();
	outItemIndex = -1;
	return false;
}

bool MaterialsWorkbenchControllerTilesetsApi::SaveTileset(const TilesetStorageRecord &tileset, wxString &error) {
	return SaveTileset(tileset, wxString(), error);
}

bool MaterialsWorkbenchControllerTilesetsApi::SaveTileset(const TilesetStorageRecord &tileset, const wxString &previousName, wxString &error) {
	if (!state_.repository().SaveTileset(tileset, previousName, error)) {
		return false;
	}

	if (!ReloadCatalogSnapshot(state_)) {
		error = state_.lastError();
		return false;
	}

	return true;
}

bool MaterialsWorkbenchControllerTilesetsApi::SaveTilesetWithoutReload(const TilesetStorageRecord &tileset, wxString &error) {
	return state_.repository().SaveTileset(tileset, error);
}

bool MaterialsWorkbenchControllerTilesetsApi::DeleteTileset(const wxString &name, wxString &error) {
	if (!state_.repository().DeleteTileset(name, error)) {
		return false;
	}

	if (!ReloadCatalogSnapshot(state_)) {
		error = state_.lastError();
		return false;
	}

	return true;
}

bool MaterialsWorkbenchControllerTilesetsApi::SavePaletteGroup(const PaletteGroupRecord &group, wxString &error) {
	if (!state_.repository().SavePaletteGroup(group, error)) {
		return false;
	}

	if (!ReloadCatalogSnapshot(state_)) {
		error = state_.lastError();
		return false;
	}

	return true;
}

bool MaterialsWorkbenchControllerTilesetsApi::SavePaletteGroupWithoutReload(const PaletteGroupRecord &group, wxString &error) {
	return state_.repository().SavePaletteGroup(group, error);
}

bool MaterialsWorkbenchControllerTilesetsApi::DeletePaletteGroup(const wxString &name, wxString &error) {
	if (!state_.repository().DeletePaletteGroup(name, error)) {
		return false;
	}

	if (!ReloadCatalogSnapshot(state_)) {
		error = state_.lastError();
		return false;
	}

	return true;
}

bool MaterialsWorkbenchControllerTilesetsApi::DeletePaletteGroupAndReassignPalettes(const wxString &name, const wxString &destinationName, int &outMovedPaletteCount, wxString &error) {
	outMovedPaletteCount = 0;
	error.clear();

	if (name.IsSameAs(destinationName, false)) {
		error = "Choose a different destination category before deleting this one.";
		return false;
	}

	const auto destinationIt = std::find_if(state_.catalog().paletteGroups.begin(), state_.catalog().paletteGroups.end(), [&](const PaletteGroupRecord &group) {
		return group.name.IsSameAs(destinationName, false);
	});
	if (destinationIt == state_.catalog().paletteGroups.end()) {
		error = "The destination category no longer exists.";
		return false;
	}

	if (!state_.repository().DeletePaletteGroupAndReassignTilesets(name, destinationName, outMovedPaletteCount, error)) {
		return false;
	}

	if (!ReloadCatalogSnapshot(state_)) {
		error = state_.lastError();
		return false;
	}

	return true;
}

const std::vector<PaletteGroupRecord> &MaterialsWorkbenchControllerTilesetsApi::GetPaletteGroups() const {
	return state_.catalog().paletteGroups;
}

const std::vector<TilesetStorageRecord> &MaterialsWorkbenchControllerTilesetsApi::GetTilesets() const {
	return state_.catalog().tilesets;
}

const std::vector<MaterialsWorkbenchBrushGroup> &MaterialsWorkbenchControllerBrushesApi::GetBrushGroups() const {
	return state_.catalog().brushGroups;
}

const std::vector<BrushRecord> &MaterialsWorkbenchControllerBrushesApi::GetWallBrushes() const {
	return state_.catalog().wallBrushes;
}

const std::vector<BorderSetRecord> &MaterialsWorkbenchControllerBordersApi::GetGlobalBorderSets() const {
	return state_.catalog().globalBorderSets;
}

namespace {
	wxString BuildFailedCatalogOverviewText(const wxString &error) {
		return "Materials Workbench could not load the SQLite catalog.\n\nError: " + error;
	}

	wxString BuildDefaultInspectorText() {
		return "Catalog loaded from materials.db.\n\n"
			   "Select a palette, brush, border set or wall entry in the navigation tree to inspect its SQLite-backed metadata.";
	}

	wxString BuildGroupSelectionOverview(const MaterialsWorkbenchCatalogSnapshot &catalog, const wxString &contextKey) {
		if (contextKey == "group:catalog") {
			return FormatCatalogOverviewCard(catalog);
		}
		if (contextKey == "group:palettes") {
			return FormatPaletteCategoriesOverviewCard(catalog);
		}
		if (contextKey == "group:brushes") {
			return FormatBrushesOverviewCard(catalog);
		}
		if (contextKey == "group:specialized") {
			return FormatSpecializedEditorsOverviewCard(catalog);
		}
		if (contextKey == "group:borders") {
			return FormatBordersOverviewCard(catalog);
		}
		if (contextKey == "group:walls") {
			return FormatWallsOverviewCard(catalog);
		}
		if (contextKey.StartsWith("palette_group:")) {
			return FormatPaletteCategoryOverviewCard(catalog, contextKey.AfterFirst(':'));
		}
		if (contextKey.StartsWith("brush_family:")) {
			return FormatBrushFamilyOverviewCard(catalog, contextKey.AfterFirst(':'));
		}
		if (contextKey.StartsWith("brush_palette:")) {
			wxString remainder = contextKey.AfterFirst(':');
			const wxString familyKey = remainder.BeforeFirst(':');
			const wxString paletteLabel = remainder.AfterFirst(':');
			return FormatBrushPaletteOverviewCard(catalog, familyKey, paletteLabel);
		}
		if (contextKey.StartsWith("border_scope:")) {
			return FormatBorderScopeOverviewCard(catalog, contextKey.AfterFirst(':'));
		}
		return FormatCatalogOverviewCard(catalog);
	}

	wxString BuildBrushSelectionOverview(const MaterialsWorkbenchControllerState &state, const wxString &contextKey, int itemIndex) {
		const BrushRecord* brush = FindBrushRecordInCatalog(state.catalog(), contextKey, itemIndex);
		if (!brush) {
			return "Brush details are unavailable.";
		}
		BrushStorageRecord storage;
		wxString error;
		if (!state.repository().LoadBrushDetails(brush->id, storage, error)) {
			return "Failed to load brush details: " + error;
		}
		return FormatBrushOverview(storage);
	}

	wxString BuildBorderSetSelectionOverview(const MaterialsWorkbenchControllerState &state, const wxString &contextKey, int itemIndex) {
		const BorderSetRecord* border = FindBorderSetRecordInCatalog(state.catalog(), contextKey, itemIndex);
		if (!border) {
			return "Border set details are unavailable.";
		}
		BorderSetStorageRecord storage;
		wxString error;
		if (!state.repository().LoadBorderSetDetails(border->id, storage, error)) {
			return "Failed to load border set details: " + error;
		}
		return FormatBorderSetOverview(storage);
	}

	wxString BuildBrushSelectionInspector(const MaterialsWorkbenchControllerState &state, const wxString &contextKey, int itemIndex) {
		const BrushRecord* brush = FindBrushRecordInCatalog(state.catalog(), contextKey, itemIndex);
		if (!brush) {
			return "Brush details are unavailable.";
		}
		BrushStorageRecord storage;
		wxString error;
		if (!state.repository().LoadBrushDetails(brush->id, storage, error)) {
			return "Failed to load brush details: " + error;
		}
		return FormatBrushInspector(storage);
	}

	wxString BuildBorderSetSelectionInspector(const MaterialsWorkbenchControllerState &state, const wxString &contextKey, int itemIndex) {
		const BorderSetRecord* border = FindBorderSetRecordInCatalog(state.catalog(), contextKey, itemIndex);
		if (!border) {
			return "Border set details are unavailable.";
		}
		BorderSetStorageRecord storage;
		wxString error;
		if (!state.repository().LoadBorderSetDetails(border->id, storage, error)) {
			return "Failed to load border set details: " + error;
		}
		return FormatBorderSetInspector(storage);
	}
} // namespace

wxString MaterialsWorkbenchControllerNavigationApi::BuildSelectionOverview(MaterialsWorkbenchNodeKind kind, const wxString &contextKey, int itemIndex) const {
	if (!state_.lastError().IsEmpty()) {
		return BuildFailedCatalogOverviewText(state_.lastError());
	}

	const MaterialsWorkbenchCatalogSnapshot &catalog = state_.catalog();
	switch (kind) {
		case MaterialsWorkbenchNodeKind::Group:
			return BuildGroupSelectionOverview(catalog, contextKey);
		case MaterialsWorkbenchNodeKind::Tileset:
			return itemIndex >= 0 && itemIndex < static_cast<int>(catalog.tilesets.size())
				? FormatTilesetOverview(catalog.tilesets[itemIndex])
				: FormatCatalogOverviewCard(catalog);
		case MaterialsWorkbenchNodeKind::Brush:
			return BuildBrushSelectionOverview(state_, contextKey, itemIndex);
		case MaterialsWorkbenchNodeKind::BorderSet:
			return BuildBorderSetSelectionOverview(state_, contextKey, itemIndex);
		default:
			return FormatCatalogOverviewCard(catalog);
	}
}

wxString MaterialsWorkbenchControllerNavigationApi::BuildSelectionInspector(MaterialsWorkbenchNodeKind kind, const wxString &contextKey, int itemIndex) const {
	if (!state_.lastError().IsEmpty()) {
		return "Inspector unavailable while the catalog failed to load.";
	}

	const MaterialsWorkbenchCatalogSnapshot &catalog = state_.catalog();
	switch (kind) {
		case MaterialsWorkbenchNodeKind::Tileset:
			return itemIndex >= 0 && itemIndex < static_cast<int>(catalog.tilesets.size())
				? FormatTilesetInspector(catalog.tilesets[itemIndex])
				: wxString::FromUTF8("Palette details are unavailable.");
		case MaterialsWorkbenchNodeKind::Brush:
			return BuildBrushSelectionInspector(state_, contextKey, itemIndex);
		case MaterialsWorkbenchNodeKind::BorderSet:
			return BuildBorderSetSelectionInspector(state_, contextKey, itemIndex);
		default:
			return BuildDefaultInspectorText();
	}
}
