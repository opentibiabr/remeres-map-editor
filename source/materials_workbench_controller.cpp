#include "main.h"

#include "materials_workbench_controller.h"

wxString MaterialsWorkbenchController::GetWindowTitle() const {
	return "Materials Workbench";
}

wxString MaterialsWorkbenchController::GetOverviewText() const {
	return "Materials Workbench is the primary authoring surface for palettes, brushes, borders and walls.\n\n"
		   "This first stage establishes the full-area shell and the future workspace layout. Data loading, editing and validation will be introduced in subsequent stages.";
}

wxString MaterialsWorkbenchController::GetInspectorText() const {
	return "Inspector placeholder\n\n"
		   "Upcoming stages will expose brush, palette, border and wall properties here.";
}

std::vector<wxString> MaterialsWorkbenchController::GetNavigationSections() const {
	return {
		"Palettes",
		"Brushes",
		"Borders",
		"Walls",
		"Validation",
	};
}
