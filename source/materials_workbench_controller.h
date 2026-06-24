#ifndef RME_MATERIALS_WORKBENCH_CONTROLLER_H_
#define RME_MATERIALS_WORKBENCH_CONTROLLER_H_

#include <wx/string.h>

#include "materials_workbench_repository.h"

class MaterialsWorkbenchControllerState {
public:
	MaterialsWorkbenchControllerState() = default;

	MaterialsWorkbenchRepository &repository() {
		return repository_;
	}

	const MaterialsWorkbenchRepository &repository() const {
		return repository_;
	}

	MaterialsWorkbenchCatalogSnapshot &catalog() {
		return catalog_;
	}

	const MaterialsWorkbenchCatalogSnapshot &catalog() const {
		return catalog_;
	}

	wxString &lastError() {
		return lastError_;
	}

	const wxString &lastError() const {
		return lastError_;
	}

private:
	MaterialsWorkbenchRepository repository_;
	MaterialsWorkbenchCatalogSnapshot catalog_;
	wxString lastError_;
};

class MaterialsWorkbenchControllerCoreApi {
public:
	explicit MaterialsWorkbenchControllerCoreApi(MaterialsWorkbenchControllerState &state);

	bool ReloadCatalog();
	wxString GetWindowTitle() const;
	wxString GetOverviewText() const;
	wxString GetInspectorText() const;

protected:
	MaterialsWorkbenchControllerState &state_;
};

class MaterialsWorkbenchControllerNavigationApi {
public:
	explicit MaterialsWorkbenchControllerNavigationApi(MaterialsWorkbenchControllerState &state);

	std::vector<MaterialsWorkbenchTreeNode> BuildNavigationTree() const;
	wxString BuildSelectionOverview(MaterialsWorkbenchNodeKind kind, const wxString &contextKey, int itemIndex) const;
	wxString BuildSelectionInspector(MaterialsWorkbenchNodeKind kind, const wxString &contextKey, int itemIndex) const;

private:
	MaterialsWorkbenchControllerState &state_;
};

class MaterialsWorkbenchControllerTilesetsApi {
public:
	explicit MaterialsWorkbenchControllerTilesetsApi(MaterialsWorkbenchControllerState &state);

	bool GetTilesetByIndex(int itemIndex, TilesetStorageRecord &outTileset) const;
	bool LocateTilesetNode(const wxString &name, int &outItemIndex) const;
	bool HasTilesetNamed(const wxString &name) const;
	bool HasPaletteGroupNamed(const wxString &name) const;
	bool SaveTileset(const TilesetStorageRecord &tileset, wxString &error);
	bool SaveTileset(const TilesetStorageRecord &tileset, const wxString &previousName, wxString &error);
	bool SaveTilesetWithoutReload(const TilesetStorageRecord &tileset, wxString &error);
	bool DeleteTileset(const wxString &name, wxString &error);
	bool SavePaletteGroup(const PaletteGroupRecord &group, wxString &error);
	bool SavePaletteGroupWithoutReload(const PaletteGroupRecord &group, wxString &error);
	bool DeletePaletteGroup(const wxString &name, wxString &error);
	bool DeletePaletteGroupAndReassignPalettes(const wxString &name, const wxString &destinationName, int &outMovedPaletteCount, wxString &error);
	const std::vector<PaletteGroupRecord> &GetPaletteGroups() const;
	const std::vector<TilesetStorageRecord> &GetTilesets() const;

private:
	MaterialsWorkbenchControllerState &state_;
};

class MaterialsWorkbenchControllerBrushesApi {
public:
	explicit MaterialsWorkbenchControllerBrushesApi(MaterialsWorkbenchControllerState &state);

	bool GetBrushDetails(const wxString &contextKey, int itemIndex, BrushStorageRecord &outBrush, wxString &error) const;
	bool SaveBrushDetails(BrushStorageRecord &brushStorage, wxString &error);
	bool DeleteBrush(int64_t brushId, wxString &error);
	bool SaveGroundBrushBorders(int64_t brushId, const std::vector<GroundBrushBorderRecord> &borders, wxString &error);
	bool SaveWallBrushParts(BrushStorageRecord &brushStorage, wxString &error);
	bool LocateBrushNode(int64_t brushId, wxString &outContextKey, int &outItemIndex) const;
	bool ResolveBrushIdByNameAndType(const wxString &name, const wxString &type, int64_t &outBrushId, wxString &error) const;
	bool GetBrushUsages(int64_t brushId, const wxString &brushName, std::vector<BrushUsageRecord> &outUsages, wxString &error) const;
	const std::vector<MaterialsWorkbenchBrushGroup> &GetBrushGroups() const;
	const std::vector<BrushRecord> &GetWallBrushes() const;

private:
	MaterialsWorkbenchControllerState &state_;
};

class MaterialsWorkbenchControllerBordersApi {
public:
	explicit MaterialsWorkbenchControllerBordersApi(MaterialsWorkbenchControllerState &state);

	bool GetBorderSetDetails(const wxString &contextKey, int itemIndex, BorderSetStorageRecord &outBorderSet, wxString &error) const;
	bool GetBorderSetUsages(int64_t borderSetId, std::vector<BorderSetUsageRecord> &outUsages, wxString &error) const;
	bool SaveBorderSet(BorderSetStorageRecord &borderSet, wxString &error);
	bool DeleteBorderSet(int64_t borderSetId, wxString &error);
	int SuggestNextBorderId() const;
	bool LocateBorderSetNode(int64_t borderSetId, wxString &outContextKey, int &outItemIndex) const;
	const std::vector<BorderSetRecord> &GetGlobalBorderSets() const;

private:
	MaterialsWorkbenchControllerState &state_;
};

class MaterialsWorkbenchController : public MaterialsWorkbenchControllerState,
									public MaterialsWorkbenchControllerCoreApi,
									public MaterialsWorkbenchControllerNavigationApi,
									public MaterialsWorkbenchControllerTilesetsApi,
									public MaterialsWorkbenchControllerBrushesApi,
									public MaterialsWorkbenchControllerBordersApi {
public:
	MaterialsWorkbenchController();
};

#endif
