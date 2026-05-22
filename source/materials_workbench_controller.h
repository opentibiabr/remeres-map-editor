#ifndef RME_MATERIALS_WORKBENCH_CONTROLLER_H_
#define RME_MATERIALS_WORKBENCH_CONTROLLER_H_

#include <wx/string.h>

#include "materials_workbench_repository.h"

class MaterialsWorkbenchController {
public:
	MaterialsWorkbenchController() = default;

	bool ReloadCatalog();
	wxString GetWindowTitle() const;
	wxString GetOverviewText() const;
	wxString GetInspectorText() const;
	std::vector<MaterialsWorkbenchTreeNode> BuildNavigationTree() const;
	wxString BuildSelectionOverview(MaterialsWorkbenchNodeKind kind, const wxString &contextKey, int itemIndex) const;
	wxString BuildSelectionInspector(MaterialsWorkbenchNodeKind kind, const wxString &contextKey, int itemIndex) const;

private:
	const MaterialsWorkbenchBrushGroup* FindBrushGroup(const wxString &contextKey) const;
	const BrushRecord* FindBrushRecord(const wxString &contextKey, int itemIndex) const;
	const BorderSetRecord* FindBorderSetRecord(const wxString &contextKey, int itemIndex) const;

	MaterialsWorkbenchRepository repository_;
	MaterialsWorkbenchCatalogSnapshot catalog_;
	wxString lastError_;
};

#endif
