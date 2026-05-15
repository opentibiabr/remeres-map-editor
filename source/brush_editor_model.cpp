//////////////////////////////////////////////////////////////////////
// brush_editor_model.cpp - Unified domain model for brush/palette editor
//////////////////////////////////////////////////////////////////////

#include "main.h"
#include "brush_editor_model.h"

bool BrushEditorStorageLocation::IsValid() const {
	return !filePath.IsEmpty() && !xmlElementName.IsEmpty();
}

bool BrushEditorPaletteMembership::IsValid() const {
	return !tilesetName.IsEmpty() && categoryType != TILESET_UNKNOWN;
}

bool BrushEditorCompositeTile::IsValid() const {
	return itemId != 0;
}

bool BrushEditorVariation::IsValid() const {
	if (chance <= 0 || tiles.empty()) {
		return false;
	}

	for (const BrushEditorCompositeTile &tile : tiles) {
		if (!tile.IsValid()) {
			return false;
		}
	}

	return true;
}

bool BrushEditorWallPart::IsValid() const {
	if (typeName.IsEmpty() || items.empty()) {
		return false;
	}

	for (const auto &item : items) {
		if (item.first == 0 || item.second <= 0) {
			return false;
		}
	}

	return true;
}

bool BrushEditorBorderConfig::IsValid() const {
	if (borderId < 0) {
		return false;
	}

	if (alignment.IsEmpty()) {
		return false;
	}

	return true;
}

bool BrushEditorBrushDefinition::HasCapability(BrushEditorCapability capability) const {
	return (capabilities & static_cast<uint32_t>(capability)) != 0;
}

void BrushEditorBrushDefinition::AddCapability(BrushEditorCapability capability) {
	capabilities |= static_cast<uint32_t>(capability);
}

void BrushEditorBrushDefinition::RemoveCapability(BrushEditorCapability capability) {
	capabilities &= ~static_cast<uint32_t>(capability);
}

bool BrushEditorBrushDefinition::IsPersisted() const {
	for (const BrushEditorStorageLocation &storage : storageLocations) {
		if (storage.IsValid()) {
			return true;
		}
	}
	return false;
}

bool BrushEditorBrushDefinition::IsValid() const {
	if (name.IsEmpty() || deleted) {
		return false;
	}

	if (kind == BrushEditorBrushKind::Unknown) {
		return false;
	}

	if (HasCapability(BRUSH_CAP_VARIATIONS) && variations.empty()) {
		return false;
	}

	if (HasCapability(BRUSH_CAP_STRUCTURE) && wallParts.empty()) {
		return false;
	}

	if (HasCapability(BRUSH_CAP_BORDERS) && borders.empty()) {
		return false;
	}

	for (const BrushEditorVariation &variation : variations) {
		if (!variation.IsValid()) {
			return false;
		}
	}

	for (const auto &entry : wallParts) {
		if (!entry.second.IsValid()) {
			return false;
		}
	}

	for (const BrushEditorBorderConfig &border : borders) {
		if (!border.IsValid()) {
			return false;
		}
	}

	return true;
}

const BrushEditorStorageLocation* BrushEditorBrushDefinition::GetPrimaryStorage() const {
	if (storageLocations.empty()) {
		return nullptr;
	}
	return &storageLocations.front();
}

BrushEditorStorageLocation* BrushEditorBrushDefinition::GetPrimaryStorage() {
	if (storageLocations.empty()) {
		return nullptr;
	}
	return &storageLocations.front();
}

bool BrushEditorPaletteDefinition::ContainsBrush(const wxString &brushName) const {
	return std::find(brushNames.begin(), brushNames.end(), brushName) != brushNames.end();
}

bool BrushEditorPaletteDefinition::AddBrush(const wxString &brushName) {
	if (brushName.IsEmpty() || ContainsBrush(brushName)) {
		return false;
	}

	brushNames.push_back(brushName);
	return true;
}

bool BrushEditorPaletteDefinition::RemoveBrush(const wxString &brushName) {
	const auto it = std::remove(brushNames.begin(), brushNames.end(), brushName);
	if (it == brushNames.end()) {
		return false;
	}

	brushNames.erase(it, brushNames.end());
	return true;
}

bool BrushEditorPaletteDefinition::IsValid() const {
	return !name.IsEmpty() && categoryType != TILESET_UNKNOWN && !deleted;
}

void BrushEditorCatalog::Clear() {
	brushes.clear();
	palettes.clear();
	InvalidateBrushIndex();
}

void BrushEditorCatalog::InvalidateBrushIndex() {
	m_brushIndexDirty = true;
	m_brushIndexByName.clear();
}

void BrushEditorCatalog::RebuildBrushIndex() const {
	m_brushIndexByName.clear();
	m_brushIndexByName.reserve(brushes.size());
	for (size_t i = 0; i < brushes.size(); ++i) {
		m_brushIndexByName[nstr(brushes[i].name)] = i;
	}
	m_brushIndexDirty = false;
}

BrushEditorBrushDefinition* BrushEditorCatalog::FindBrushByName(const wxString &name) {
	const BrushEditorCatalog &self = *this;
	const BrushEditorBrushDefinition* found = self.FindBrushByName(name);
	return const_cast<BrushEditorBrushDefinition*>(found);
}

const BrushEditorBrushDefinition* BrushEditorCatalog::FindBrushByName(const wxString &name) const {
	if (m_brushIndexDirty) {
		RebuildBrushIndex();
	}
	const auto it = m_brushIndexByName.find(nstr(name));
	if (it == m_brushIndexByName.end() || it->second >= brushes.size()) {
		return nullptr;
	}
	return &brushes[it->second];
}

BrushEditorPaletteDefinition* BrushEditorCatalog::FindPaletteByName(const wxString &name) {
	for (BrushEditorPaletteDefinition &palette : palettes) {
		if (palette.name == name) {
			return &palette;
		}
	}
	return nullptr;
}

const BrushEditorPaletteDefinition* BrushEditorCatalog::FindPaletteByName(const wxString &name) const {
	for (const BrushEditorPaletteDefinition &palette : palettes) {
		if (palette.name == name) {
			return &palette;
		}
	}
	return nullptr;
}

BrushEditorBrushDefinition& BrushEditorCatalog::GetOrCreateBrush(const wxString &name) {
	if (BrushEditorBrushDefinition* existing = FindBrushByName(name)) {
		return *existing;
	}

	BrushEditorBrushDefinition brush;
	brush.name = name;
	brush.AddCapability(BRUSH_CAP_GENERAL);
	brushes.push_back(brush);
	InvalidateBrushIndex();
	return brushes.back();
}

BrushEditorPaletteDefinition& BrushEditorCatalog::GetOrCreatePalette(const wxString &name) {
	if (BrushEditorPaletteDefinition* existing = FindPaletteByName(name)) {
		return *existing;
	}

	BrushEditorPaletteDefinition palette;
	palette.name = name;
	palettes.push_back(palette);
	return palettes.back();
}

void BrushEditorCatalog::Sort() {
	std::sort(brushes.begin(), brushes.end(), [](const BrushEditorBrushDefinition &a, const BrushEditorBrushDefinition &b) {
		return a.name.CmpNoCase(b.name) < 0;
	});

	std::sort(palettes.begin(), palettes.end(), [](const BrushEditorPaletteDefinition &a, const BrushEditorPaletteDefinition &b) {
		return a.name.CmpNoCase(b.name) < 0;
	});

	InvalidateBrushIndex();
}

wxString BrushEditorBrushKindToString(BrushEditorBrushKind kind) {
	switch (kind) {
		case BrushEditorBrushKind::Ground:
			return "ground";
		case BrushEditorBrushKind::Wall:
			return "wall";
		case BrushEditorBrushKind::Border:
			return "border";
		case BrushEditorBrushKind::Raw:
			return "raw";
		case BrushEditorBrushKind::Doodad:
			return "doodad";
		case BrushEditorBrushKind::Item:
			return "item";
		case BrushEditorBrushKind::Creature:
			return "creature";
		case BrushEditorBrushKind::Spawn:
			return "spawn";
		case BrushEditorBrushKind::House:
			return "house";
		case BrushEditorBrushKind::Waypoint:
			return "waypoint";
		case BrushEditorBrushKind::Zone:
			return "zone";
		case BrushEditorBrushKind::Unknown:
		default:
			return "unknown";
	}
}

BrushEditorBrushKind BrushEditorBrushKindFromString(const wxString &value) {
	const wxString lower = value.Lower();

	if (lower == "ground") {
		return BrushEditorBrushKind::Ground;
	}
	if (lower == "wall") {
		return BrushEditorBrushKind::Wall;
	}
	if (lower == "border") {
		return BrushEditorBrushKind::Border;
	}
	if (lower == "raw") {
		return BrushEditorBrushKind::Raw;
	}
	if (lower == "doodad") {
		return BrushEditorBrushKind::Doodad;
	}
	if (lower == "item") {
		return BrushEditorBrushKind::Item;
	}
	if (lower == "creature") {
		return BrushEditorBrushKind::Creature;
	}
	if (lower == "spawn") {
		return BrushEditorBrushKind::Spawn;
	}
	if (lower == "house") {
		return BrushEditorBrushKind::House;
	}
	if (lower == "waypoint") {
		return BrushEditorBrushKind::Waypoint;
	}
	if (lower == "zone") {
		return BrushEditorBrushKind::Zone;
	}

	return BrushEditorBrushKind::Unknown;
}