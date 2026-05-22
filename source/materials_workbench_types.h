#ifndef RME_MATERIALS_WORKBENCH_TYPES_H_
#define RME_MATERIALS_WORKBENCH_TYPES_H_

#include <vector>

#include <wx/string.h>

#include "brush_database.h"

enum class MaterialsWorkbenchNodeKind {
	Group,
	Tileset,
	Brush,
	BorderSet,
};

struct MaterialsWorkbenchTreeNode {
	MaterialsWorkbenchNodeKind kind = MaterialsWorkbenchNodeKind::Group;
	wxString label;
	wxString contextKey;
	int itemIndex = -1;
	std::vector<MaterialsWorkbenchTreeNode> children;
};

struct MaterialsWorkbenchBrushGroup {
	wxString label;
	wxString brushType;
	std::vector<BrushRecord> brushes;
};

struct MaterialsWorkbenchCatalogSnapshot {
	MaterialsDatabaseAuditReport auditReport;
	std::vector<TilesetStorageRecord> tilesets;
	std::vector<MaterialsWorkbenchBrushGroup> brushGroups;
	std::vector<BrushRecord> wallBrushes;
	std::vector<BorderSetRecord> globalBorderSets;
	std::vector<BorderSetRecord> inlineBorderSets;
};

#endif
