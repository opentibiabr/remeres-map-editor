#ifndef RME_MATERIALS_WORKBENCH_CONTROLLER_H_
#define RME_MATERIALS_WORKBENCH_CONTROLLER_H_

#include <vector>

#include <wx/string.h>

class MaterialsWorkbenchController {
public:
	MaterialsWorkbenchController() = default;

	wxString GetWindowTitle() const;
	wxString GetOverviewText() const;
	wxString GetInspectorText() const;
	std::vector<wxString> GetNavigationSections() const;
};

#endif
