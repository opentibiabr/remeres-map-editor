#ifndef RME_MATERIALS_WORKBENCH_EXCHANGE_H_
#define RME_MATERIALS_WORKBENCH_EXCHANGE_H_

#include <cstdint>
#include <functional>
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

struct MaterialsWorkbenchResolvedExportSelection {
	std::vector<int64_t> brushIds;
	std::vector<int> globalBorderXmlIds;
	std::vector<wxString> paletteNames;
	std::vector<wxString> paletteGroupNames;
};

struct MaterialsWorkbenchImportReport {
	int created = 0;
	int updated = 0;
	int skipped = 0;
	std::vector<wxString> notes;
	std::vector<int64_t> importedBrushIds;
	std::vector<int64_t> importedBorderSetIds;
	std::vector<wxString> importedPaletteNames;
};

enum class MaterialsWorkbenchImportConflictStrategy {
	UpdateExisting,
	SkipExisting,
	RenameWithSuffix,
};

struct MaterialsWorkbenchImportOptions {
	MaterialsWorkbenchImportConflictStrategy onConflict = MaterialsWorkbenchImportConflictStrategy::UpdateExisting;
};

class MaterialsWorkbenchController;

nlohmann::json BuildMaterialsWorkbenchExportJson(
	MaterialsWorkbenchController &controller,
	const MaterialsWorkbenchExportSelection &selection,
	wxString &error
);

bool ResolveMaterialsWorkbenchExportSelection(
	MaterialsWorkbenchController &controller,
	const MaterialsWorkbenchExportSelection &selection,
	MaterialsWorkbenchResolvedExportSelection &outResolved,
	wxString &error
);

using MaterialsWorkbenchImportProgressCallback = std::function<bool(int current, int total, const wxString &stage)>;

bool ApplyMaterialsWorkbenchImportJson(
	MaterialsWorkbenchController &controller,
	const nlohmann::json &root,
	MaterialsWorkbenchImportReport &outReport,
	wxString &error
);

bool ApplyMaterialsWorkbenchImportJsonWithProgress(
	MaterialsWorkbenchController &controller,
	const nlohmann::json &root,
	const MaterialsWorkbenchImportProgressCallback &progress,
	MaterialsWorkbenchImportReport &outReport,
	wxString &error
);

bool ApplyMaterialsWorkbenchImportJson(
	MaterialsWorkbenchController &controller,
	const nlohmann::json &root,
	const MaterialsWorkbenchImportOptions &options,
	MaterialsWorkbenchImportReport &outReport,
	wxString &error
);

bool ApplyMaterialsWorkbenchImportJsonWithProgress(
	MaterialsWorkbenchController &controller,
	const nlohmann::json &root,
	const MaterialsWorkbenchImportOptions &options,
	const MaterialsWorkbenchImportProgressCallback &progress,
	MaterialsWorkbenchImportReport &outReport,
	wxString &error
);

#endif
