#include "main.h"

#include "materials_workbench_repository.h"

#include <algorithm>

namespace {
	bool LoadBrushGroup(const wxString &label, const wxString &brushType, MaterialsWorkbenchBrushGroup &outGroup, wxString &error) {
		outGroup = MaterialsWorkbenchBrushGroup();
		outGroup.label = label;
		outGroup.brushType = brushType;
		if (!g_brush_database.listBrushesByType(brushType, outGroup.brushes)) {
			error = g_brush_database.getLastError();
			return false;
		}
		return true;
	}
} // namespace

bool MaterialsWorkbenchRepository::LoadCatalog(MaterialsWorkbenchCatalogSnapshot &outCatalog, wxString &error) const {
	outCatalog = MaterialsWorkbenchCatalogSnapshot();
	error.clear();

	if (!g_brush_database.isOpen()) {
		error = "SQLite materials database is not open.";
		return false;
	}

	if (!g_brush_database.generateAuditReport(outCatalog.auditReport)) {
		error = g_brush_database.getLastError();
		return false;
	}
	if (!g_brush_database.getAllPaletteGroups(outCatalog.paletteGroups)) {
		error = g_brush_database.getLastError();
		return false;
	}
	if (!g_brush_database.getAllTilesets(outCatalog.tilesets)) {
		error = g_brush_database.getLastError();
		return false;
	}

	static const struct BrushGroupSpec {
		const char* label;
		const char* brushType;
	} kBrushGroupSpecs[] = {
		{ "Ground Brushes", "ground" },
		{ "Doodad Brushes", "doodad" },
		{ "Carpet Brushes", "carpet" },
		{ "Table Brushes", "table" },
	};

	for (const BrushGroupSpec &spec : kBrushGroupSpecs) {
		MaterialsWorkbenchBrushGroup group;
		if (!LoadBrushGroup(spec.label, spec.brushType, group, error)) {
			return false;
		}
		outCatalog.brushGroups.push_back(std::move(group));
	}

	if (!g_brush_database.listBrushesByType("wall", outCatalog.wallBrushes)) {
		error = g_brush_database.getLastError();
		return false;
	}
	std::vector<BrushRecord> wallDecorations;
	if (!g_brush_database.listBrushesByType("wall decoration", wallDecorations)) {
		error = g_brush_database.getLastError();
		return false;
	}
	if (!wallDecorations.empty()) {
		outCatalog.wallBrushes.insert(outCatalog.wallBrushes.end(), wallDecorations.begin(), wallDecorations.end());
		std::sort(outCatalog.wallBrushes.begin(), outCatalog.wallBrushes.end(), [](const BrushRecord &a, const BrushRecord &b) {
			const int nameCompare = a.name.CmpNoCase(b.name);
			if (nameCompare != 0) {
				return nameCompare < 0;
			}
			return a.id < b.id;
		});
	}
	if (!g_brush_database.listBorderSetsByScope("global", outCatalog.globalBorderSets)) {
		error = g_brush_database.getLastError();
		return false;
	}
	if (!g_brush_database.listBorderSetsByScope("inline", outCatalog.inlineBorderSets)) {
		error = g_brush_database.getLastError();
		return false;
	}

	return true;
}

bool MaterialsWorkbenchRepository::FindBrushByNameAndType(const wxString &name, const wxString &type, BrushRecord &outBrush, wxString &error) const {
	error.clear();
	outBrush = BrushRecord();
	if (!g_brush_database.findBrushByNameAndType(name, type, outBrush)) {
		error = g_brush_database.getLastError();
		return false;
	}
	return true;
}

bool MaterialsWorkbenchRepository::SaveTileset(const TilesetStorageRecord &tileset, wxString &error) const {
	error.clear();

	if (!g_brush_database.saveTileset(tileset)) {
		error = g_brush_database.getLastError();
		return false;
	}

	return true;
}

bool MaterialsWorkbenchRepository::SaveTileset(const TilesetStorageRecord &tileset, const wxString &previousName, wxString &error) const {
	if (previousName.IsEmpty() || previousName == tileset.name) {
		return SaveTileset(tileset, error);
	}

	error.clear();
	if (!g_brush_database.runInTransaction([&]() {
			if (!g_brush_database.deleteTileset(previousName)) {
				error = g_brush_database.getLastError();
				return false;
			}
			if (!g_brush_database.saveTileset(tileset)) {
				error = g_brush_database.getLastError();
				return false;
			}
			return true;
		})) {
		if (error.IsEmpty()) {
			error = g_brush_database.getLastError();
		}
		return false;
	}

	return true;
}

bool MaterialsWorkbenchRepository::DeleteTileset(const wxString &name, wxString &error) const {
	error.clear();

	if (!g_brush_database.deleteTileset(name)) {
		error = g_brush_database.getLastError();
		return false;
	}

	return true;
}

bool MaterialsWorkbenchRepository::SavePaletteGroup(const PaletteGroupRecord &group, wxString &error) const {
	error.clear();

	if (!g_brush_database.savePaletteGroup(group)) {
		error = g_brush_database.getLastError();
		return false;
	}

	return true;
}

bool MaterialsWorkbenchRepository::DeletePaletteGroup(const wxString &name, wxString &error) const {
	error.clear();

	if (!g_brush_database.deletePaletteGroup(name)) {
		error = g_brush_database.getLastError();
		return false;
	}

	return true;
}

bool MaterialsWorkbenchRepository::DeletePaletteGroupAndReassignTilesets(const wxString &name, const wxString &destinationName, int &outMovedTilesetCount, wxString &error) const {
	outMovedTilesetCount = 0;
	error.clear();

	std::vector<TilesetStorageRecord> tilesets;
	if (!g_brush_database.getAllTilesets(tilesets)) {
		error = g_brush_database.getLastError();
		return false;
	}

	if (!g_brush_database.runInTransaction([&]() {
			for (TilesetStorageRecord &tileset : tilesets) {
				if (!tileset.paletteGroupName.IsSameAs(name, false)) {
					continue;
				}

				tileset.paletteGroupName = destinationName;
				if (!g_brush_database.saveTileset(tileset)) {
					error = g_brush_database.getLastError();
					return false;
				}
				++outMovedTilesetCount;
			}

			if (!g_brush_database.deletePaletteGroup(name)) {
				error = g_brush_database.getLastError();
				return false;
			}
			return true;
		})) {
		if (error.IsEmpty()) {
			error = g_brush_database.getLastError();
		}
		return false;
	}

	return true;
}

bool MaterialsWorkbenchRepository::LoadBrushDetails(int64_t brushId, BrushStorageRecord &outBrush, wxString &error) const {
	outBrush = BrushStorageRecord();
	error.clear();

	if (!g_brush_database.getCompleteBrushById(brushId, outBrush)) {
		error = g_brush_database.getLastError();
		return false;
	}

	return true;
}

bool MaterialsWorkbenchRepository::LoadBrushUsages(int64_t brushId, const wxString &brushName, std::vector<BrushUsageRecord> &outUsages, wxString &error) const {
	outUsages.clear();
	error.clear();

	if (!g_brush_database.listBrushUsages(brushId, brushName, outUsages)) {
		error = g_brush_database.getLastError();
		return false;
	}

	return true;
}

bool MaterialsWorkbenchRepository::LoadBorderSetUsages(int64_t borderSetId, std::vector<BorderSetUsageRecord> &outUsages, wxString &error) const {
	outUsages.clear();
	error.clear();

	if (!g_brush_database.listBorderSetUsages(borderSetId, outUsages)) {
		error = g_brush_database.getLastError();
		return false;
	}

	return true;
}

bool MaterialsWorkbenchRepository::SaveBrushDetails(BrushStorageRecord &brushStorage, wxString &error) const {
	error.clear();

	BrushRecord &brush = brushStorage.brush;
	return g_brush_database.runInTransaction([&]() {
		BrushRecord persistedBrush;
		bool hadPersistedBrush = false;
		if (brush.id > 0) {
			if (!g_brush_database.getBrushById(brush.id, persistedBrush)) {
				error = g_brush_database.getLastError();
				return false;
			}
			hadPersistedBrush = true;
		}

		if (brush.id > 0) {
			if (!g_brush_database.updateBrush(brush)) {
				error = g_brush_database.getLastError();
				return false;
			}
		} else {
			int64_t insertedBrushId = 0;
			if (!g_brush_database.upsertBrush(brush, insertedBrushId)) {
				error = g_brush_database.getLastError();
				return false;
			}
			brush.id = insertedBrushId;
		}

		if (hadPersistedBrush && persistedBrush.name != brush.name) {
			if (!g_brush_database.updateBrushReferenceNames(brush.id, persistedBrush.name, brush.name)) {
				error = g_brush_database.getLastError();
				return false;
			}
		}

		if (!g_brush_database.replaceCarpetNodes(brush.id, brushStorage.carpetNodes)) {
			error = g_brush_database.getLastError();
			return false;
		}
		if (!g_brush_database.replaceBrushItems(brush.id, brushStorage.items)) {
			error = g_brush_database.getLastError();
			return false;
		}
		if (!g_brush_database.replaceTableNodes(brush.id, brushStorage.tableNodes)) {
			error = g_brush_database.getLastError();
			return false;
		}
		if (!g_brush_database.replaceDoodadAlternatives(brush.id, brushStorage.doodadAlternatives)) {
			error = g_brush_database.getLastError();
			return false;
		}
		if (!g_brush_database.replaceBrushLinks(brush.id, brushStorage.links)) {
			error = g_brush_database.getLastError();
			return false;
		}

		return true;
	});
}

bool MaterialsWorkbenchRepository::DeleteBrush(int64_t brushId, wxString &error) const {
	error.clear();

	if (brushId <= 0) {
		error = "Brush id is invalid.";
		return false;
	}
	BrushRecord brush;
	if (!g_brush_database.getBrushById(brushId, brush)) {
		error = g_brush_database.getLastError();
		return false;
	}

	return g_brush_database.runInTransaction([&]() {
		if (!g_brush_database.replaceCarpetNodes(brushId, {})) {
			error = g_brush_database.getLastError();
			return false;
		}
		if (!g_brush_database.replaceBrushItems(brushId, {})) {
			error = g_brush_database.getLastError();
			return false;
		}
		if (!g_brush_database.replaceTableNodes(brushId, {})) {
			error = g_brush_database.getLastError();
			return false;
		}
		if (!g_brush_database.replaceDoodadAlternatives(brushId, {})) {
			error = g_brush_database.getLastError();
			return false;
		}
		if (!g_brush_database.replaceGroundBrushBorders(brushId, {})) {
			error = g_brush_database.getLastError();
			return false;
		}
		if (!g_brush_database.replaceBrushLinks(brushId, {})) {
			error = g_brush_database.getLastError();
			return false;
		}
		if (!g_brush_database.replaceWallParts(brushId, {})) {
			error = g_brush_database.getLastError();
			return false;
		}
		if (!g_brush_database.deleteOwnedBorderSetsForBrush(brushId)) {
			error = g_brush_database.getLastError();
			return false;
		}
		if (!g_brush_database.deleteBrushReferences(brushId, brush.name)) {
			error = g_brush_database.getLastError();
			return false;
		}
		if (!g_brush_database.deleteBrush(brushId)) {
			error = g_brush_database.getLastError();
			return false;
		}
		return true;
	});
}

bool MaterialsWorkbenchRepository::SaveGroundBrushBorders(int64_t brushId, const std::vector<GroundBrushBorderRecord> &borders, wxString &error) const {
	error.clear();

	if (brushId <= 0) {
		error = "Ground brush id is invalid.";
		return false;
	}
	if (!g_brush_database.replaceGroundBrushBorders(brushId, borders)) {
		error = g_brush_database.getLastError();
		return false;
	}

	return true;
}

bool MaterialsWorkbenchRepository::SaveWallBrushParts(const BrushStorageRecord &brushStorage, wxString &error) const {
	error.clear();

	const int64_t brushId = brushStorage.brush.id;
	if (brushId <= 0) {
		error = "Wall brush id is invalid.";
		return false;
	}

	return g_brush_database.runInTransaction([&]() {
		BrushRecord persistedBrush;
		if (!g_brush_database.getBrushById(brushId, persistedBrush)) {
			error = g_brush_database.getLastError();
			return false;
		}

		if (!g_brush_database.updateBrush(brushStorage.brush)) {
			error = g_brush_database.getLastError();
			return false;
		}
		if (persistedBrush.name != brushStorage.brush.name) {
			if (!g_brush_database.updateBrushReferenceNames(brushId, persistedBrush.name, brushStorage.brush.name)) {
				error = g_brush_database.getLastError();
				return false;
			}
		}

		if (!g_brush_database.replaceWallParts(brushId, brushStorage.wallParts)) {
			error = g_brush_database.getLastError();
			return false;
		}
		if (!g_brush_database.replaceBrushLinks(brushId, brushStorage.links)) {
			error = g_brush_database.getLastError();
			return false;
		}
		return true;
	});
}

bool MaterialsWorkbenchRepository::LoadBorderSetDetails(int64_t borderSetId, BorderSetStorageRecord &outBorderSet, wxString &error) const {
	outBorderSet = BorderSetStorageRecord();
	error.clear();

	if (!g_brush_database.getBorderSetById(borderSetId, outBorderSet.borderSet)) {
		error = g_brush_database.getLastError();
		return false;
	}
	if (!g_brush_database.getBorderSetItems(borderSetId, outBorderSet.items)) {
		error = g_brush_database.getLastError();
		return false;
	}

	return true;
}

bool MaterialsWorkbenchRepository::SaveBorderSet(BorderSetStorageRecord &borderSet, wxString &error) const {
	error.clear();

	int64_t borderSetId = borderSet.borderSet.id;
	if (!g_brush_database.upsertBorderSet(borderSet.borderSet, borderSetId)) {
		error = g_brush_database.getLastError();
		return false;
	}
	borderSet.borderSet.id = borderSetId;
	for (BorderSetItemRecord &item : borderSet.items) {
		item.borderSetId = borderSetId;
	}
	if (!g_brush_database.replaceBorderSetItems(borderSetId, borderSet.items)) {
		error = g_brush_database.getLastError();
		return false;
	}

	return true;
}

bool MaterialsWorkbenchRepository::DeleteBorderSet(int64_t borderSetId, wxString &error) const {
	error.clear();

	if (borderSetId <= 0) {
		error = "Border set id is invalid.";
		return false;
	}
	if (!g_brush_database.deleteBorderSet(borderSetId)) {
		error = g_brush_database.getLastError();
		return false;
	}

	return true;
}
