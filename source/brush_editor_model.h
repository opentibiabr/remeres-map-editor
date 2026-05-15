//////////////////////////////////////////////////////////////////////
// brush_editor_model.h - Unified domain model for brush/palette editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_BRUSH_EDITOR_MODEL_H_
#define RME_BRUSH_EDITOR_MODEL_H_

#include "main.h"
#include "tileset.h"
#include <unordered_map>

enum class BrushEditorBrushKind {
	Unknown,
	Ground,
	Wall,
	Border,
	Raw,
	Doodad,
	Item,
	Creature,
	Spawn,
	House,
	Waypoint,
	Zone,
};

enum BrushEditorCapability : uint32_t {
	BRUSH_CAP_NONE = 0,
	BRUSH_CAP_GENERAL = 1 << 0,
	BRUSH_CAP_VARIATIONS = 1 << 1,
	BRUSH_CAP_STRUCTURE = 1 << 2,
	BRUSH_CAP_BORDERS = 1 << 3,
	BRUSH_CAP_FLAGS = 1 << 4,
	BRUSH_CAP_PALETTES = 1 << 5,
};

struct BrushEditorStorageLocation {
	wxString filePath;
	wxString xmlElementName;
	wxString logicalId;

	bool IsValid() const;
};

struct BrushEditorPaletteMembership {
	wxString tilesetName;
	TilesetCategoryType categoryType = TILESET_UNKNOWN;
	int orderHint = -1;

	bool IsValid() const;
};

struct BrushEditorCompositeTile {
	int x = 0;
	int y = 0;
	uint16_t itemId = 0;
	int chance = 1;

	bool IsValid() const;
};

struct BrushEditorVariation {
	wxString label;
	int chance = 1;
	std::vector<BrushEditorCompositeTile> tiles;

	bool IsValid() const;
};

struct BrushEditorWallPart {
	wxString typeName;
	std::vector<std::pair<uint16_t, int>> items;

	bool IsValid() const;
};

struct BrushEditorBorderConfig {
	int borderId = 0;
	wxString alignment = "outer";
	bool includeToNone = true;
	bool includeInner = false;
	bool optional = false;
	bool ground = false;
	int group = 0;

	bool IsValid() const;
};

class BrushEditorBrushDefinition {
public:
	wxString name;
	BrushEditorBrushKind kind = BrushEditorBrushKind::Unknown;
	uint32_t capabilities = BRUSH_CAP_NONE;

	uint16_t previewItemId = 0;
	uint16_t serverLookId = 0;
	int zOrder = 0;

	bool optional = false;
	bool ground = false;
	int group = 0;
	bool deleted = false;

	std::vector<BrushEditorPaletteMembership> memberships;
	std::vector<BrushEditorVariation> variations;
	std::map<wxString, BrushEditorWallPart> wallParts;
	std::vector<BrushEditorBorderConfig> borders;
	std::vector<BrushEditorStorageLocation> storageLocations;

	bool HasCapability(BrushEditorCapability capability) const;
	void AddCapability(BrushEditorCapability capability);
	void RemoveCapability(BrushEditorCapability capability);

	bool IsPersisted() const;
	bool IsValid() const;

	const BrushEditorStorageLocation* GetPrimaryStorage() const;
	BrushEditorStorageLocation* GetPrimaryStorage();
};

class BrushEditorPaletteDefinition {
public:
	wxString name;
	TilesetCategoryType categoryType = TILESET_UNKNOWN;
	wxString sourceFile;
	bool builtin = true;
	bool deleted = false;

	std::vector<wxString> brushNames;

	bool ContainsBrush(const wxString &brushName) const;
	bool AddBrush(const wxString &brushName);
	bool RemoveBrush(const wxString &brushName);
	bool IsValid() const;
};

class BrushEditorCatalog {
public:
	void Clear();

	BrushEditorBrushDefinition* FindBrushByName(const wxString &name);
	const BrushEditorBrushDefinition* FindBrushByName(const wxString &name) const;

	BrushEditorPaletteDefinition* FindPaletteByName(const wxString &name);
	const BrushEditorPaletteDefinition* FindPaletteByName(const wxString &name) const;

	BrushEditorBrushDefinition& GetOrCreateBrush(const wxString &name);
	BrushEditorPaletteDefinition& GetOrCreatePalette(const wxString &name);

	void Sort();

	std::vector<BrushEditorBrushDefinition> brushes;
	std::vector<BrushEditorPaletteDefinition> palettes;

private:
	void InvalidateBrushIndex();
	void RebuildBrushIndex() const;

	mutable bool m_brushIndexDirty = true;
	mutable std::unordered_map<std::string, size_t> m_brushIndexByName;
};

wxString BrushEditorBrushKindToString(BrushEditorBrushKind kind);
BrushEditorBrushKind BrushEditorBrushKindFromString(const wxString &value);

#endif