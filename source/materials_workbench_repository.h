#ifndef RME_MATERIALS_WORKBENCH_REPOSITORY_H_
#define RME_MATERIALS_WORKBENCH_REPOSITORY_H_

#include "materials_workbench_types.h"

struct BorderSetStorageRecord;
struct BrushStorageRecord;

class MaterialsWorkbenchRepository {
public:
	bool LoadCatalog(MaterialsWorkbenchCatalogSnapshot &outCatalog, wxString &error) const;
	bool SaveTileset(const TilesetStorageRecord &tileset, wxString &error) const;
	bool LoadBrushDetails(int64_t brushId, BrushStorageRecord &outBrush, wxString &error) const;
	bool LoadBorderSetDetails(int64_t borderSetId, BorderSetStorageRecord &outBorderSet, wxString &error) const;
};

#endif
