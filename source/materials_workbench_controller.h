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
	bool GetTilesetByIndex(int itemIndex, TilesetStorageRecord &outTileset) const;
	bool LocateTilesetNode(const wxString &name, int &outItemIndex) const;
	bool HasTilesetNamed(const wxString &name) const;
	bool HasPaletteGroupNamed(const wxString &name) const;
	bool GetBrushDetails(const wxString &contextKey, int itemIndex, BrushStorageRecord &outBrush, wxString &error) const;
	bool SaveBrushDetails(BrushStorageRecord &brushStorage, wxString &error);
	bool SaveWallBrushParts(BrushStorageRecord &brushStorage, wxString &error);
	bool LocateBrushNode(int64_t brushId, wxString &outContextKey, int &outItemIndex) const;
	bool GetBorderSetDetails(const wxString &contextKey, int itemIndex, BorderSetStorageRecord &outBorderSet, wxString &error) const;
	bool SaveBorderSet(BorderSetStorageRecord &borderSet, wxString &error);
	bool LocateBorderSetNode(int64_t borderSetId, wxString &outContextKey, int &outItemIndex) const;
	bool SaveTileset(const TilesetStorageRecord &tileset, wxString &error);
	bool SaveTileset(const TilesetStorageRecord &tileset, const wxString &previousName, wxString &error);
	bool DeleteTileset(const wxString &name, wxString &error);
	bool SavePaletteGroup(const PaletteGroupRecord &group, wxString &error);
	bool DeletePaletteGroup(const wxString &name, wxString &error);
	const std::vector<PaletteGroupRecord> &GetPaletteGroups() const;
	const std::vector<TilesetStorageRecord> &GetTilesets() const;
	const std::vector<MaterialsWorkbenchBrushGroup> &GetBrushGroups() const;
	const std::vector<BrushRecord> &GetWallBrushes() const;
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
