//////////////////////////////////////////////////////////////////////
// brush_editor_catalog_service.h - Read-only catalog builder
//////////////////////////////////////////////////////////////////////

#ifndef RME_BRUSH_EDITOR_CATALOG_SERVICE_H_
#define RME_BRUSH_EDITOR_CATALOG_SERVICE_H_

#include "brush_editor_model.h"

class BrushEditorCatalogService {
public:
	bool LoadCatalog(const wxString &dataDirectory, BrushEditorCatalog &catalog, wxArrayString &warnings) const;
};

#endif