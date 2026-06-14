#ifndef RME_MATERIALS_WORKBENCH_EXCHANGE_H_
#define RME_MATERIALS_WORKBENCH_EXCHANGE_H_

#include <cstdint>
#include <vector>

#include <wx/string.h>

#include <nlohmann/json.hpp>

struct MaterialsWorkbenchExportSelection {
	std::vector<int64_t> brushIds;
	std::vector<int> globalBorderXmlIds;
	std::vector<wxString> paletteNames;
	std::vector<wxString> paletteGroupNames;
	bool includeDependencies = true;
};

struct MaterialsWorkbenchImportReport {
	int created = 0;
	int updated = 0;
	std::vector<wxString> notes;
};

class MaterialsWorkbenchController;

nlohmann::json BuildMaterialsWorkbenchExportJson(
	MaterialsWorkbenchController &controller,
	const MaterialsWorkbenchExportSelection &selection,
	wxString &error
);

bool ApplyMaterialsWorkbenchImportJson(
	MaterialsWorkbenchController &controller,
	const nlohmann::json &root,
	MaterialsWorkbenchImportReport &outReport,
	wxString &error
);

#endif
