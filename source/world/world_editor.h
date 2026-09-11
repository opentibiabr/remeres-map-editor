#pragma once

#include "world/world_document.h"
#include "world/world_validation.hpp"
#include <memory>

class Editor;
class Item;
class Position;
class PalettePanel;
class wxWindow;

class WorldLayerEditor {
public:
	WorldLayerEditor(Editor &editor, WorldLayerDocument document);
	~WorldLayerEditor();
	void validate();
	void synchronizeMap();
	void acknowledgeMapChange();
	void updateTile(const Position &position);
	void select(const std::string &id);
	void editProperties(wxWindow* parent);
	void edit(const std::string &id, const world_layers::Object &value);
	void refresh();
	bool save();
	bool suppressed(const Item* item) const;
	const Item* sprite(uint16_t id) const;
	std::string at(const world_layers::Position &position) const;
	world_layers::Position position(const std::string &id) const;
	void finishDrag(bool commit);

	WorldLayerDocument document;
	world_layers::Diagnostics diagnostics;
	uint64_t validationRevision = 0;
	std::optional<world_layers::Position> drag;

private:
	Editor &editor;
	world_layers::ApplicationPlan plan;
	world_layers::Diagnostics catalogDiagnostics;
	std::unordered_map<uint64_t, std::vector<world_layers::UniqueOccurrence>> uniqueIds;
	uint64_t mapRevision = 0;
	std::unordered_map<uint16_t, std::unique_ptr<Item>> sprites;
};

PalettePanel* CreateWorldPalette(wxWindow* parent);
void ShowWorldPalette();
