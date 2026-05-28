#ifndef RME_MATERIALS_WORKBENCH_REPOSITORY_H_
#define RME_MATERIALS_WORKBENCH_REPOSITORY_H_

#include "materials_workbench_types.h"

struct BorderSetStorageRecord;
struct BrushStorageRecord;

class MaterialsWorkbenchRepository {
public:
	bool LoadCatalog(MaterialsWorkbenchCatalogSnapshot &outCatalog, wxString &error) const;
	bool SaveTileset(const TilesetStorageRecord &tileset, wxString &error) const;
	bool SaveTileset(const TilesetStorageRecord &tileset, const wxString &previousName, wxString &error) const;
	bool DeleteTileset(const wxString &name, wxString &error) const;
	bool SavePaletteGroup(const PaletteGroupRecord &group, wxString &error) const;
	bool DeletePaletteGroup(const wxString &name, wxString &error) const;
	bool LoadBrushDetails(int64_t brushId, BrushStorageRecord &outBrush, wxString &error) const;
	bool SaveBrushDetails(BrushStorageRecord &brushStorage, wxString &error) const;
	bool SaveWallBrushParts(const BrushStorageRecord &brushStorage, wxString &error) const;
	bool LoadBorderSetDetails(int64_t borderSetId, BorderSetStorageRecord &outBorderSet, wxString &error) const;
	bool SaveBorderSet(BorderSetStorageRecord &borderSet, wxString &error) const;
};

#endif
