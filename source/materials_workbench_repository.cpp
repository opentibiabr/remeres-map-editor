#include "main.h"

#include "materials_workbench_repository.h"

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

bool MaterialsWorkbenchRepository::SaveTileset(const TilesetStorageRecord &tileset, wxString &error) const {
	error.clear();

	if (!g_brush_database.saveTileset(tileset)) {
		error = g_brush_database.getLastError();
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

bool MaterialsWorkbenchRepository::SaveBrushDetails(BrushStorageRecord &brushStorage, wxString &error) const {
	error.clear();

	BrushRecord &brush = brushStorage.brush;
	return g_brush_database.runInTransaction([&]() {
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

		return true;
	});
}

bool MaterialsWorkbenchRepository::SaveWallBrushParts(const BrushStorageRecord &brushStorage, wxString &error) const {
	error.clear();

	if (brushStorage.brush.id <= 0) {
		error = "Wall brush id is invalid.";
		return false;
	}
	if (!g_brush_database.replaceWallParts(brushStorage.brush.id, brushStorage.wallParts)) {
		error = g_brush_database.getLastError();
		return false;
	}

	return true;
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
	if (!g_brush_database.replaceBorderSetItems(borderSetId, borderSet.items)) {
		error = g_brush_database.getLastError();
		return false;
	}

	borderSet.borderSet.id = borderSetId;
	for (BorderSetItemRecord &item : borderSet.items) {
		item.borderSetId = borderSetId;
	}
	return true;
}
