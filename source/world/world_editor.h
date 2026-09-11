#pragma once

#include "world/world_document.h"
#include "world/world_validation.hpp"
#include <memory>

class Editor;
class Item;

class WorldLayerEditor {
public:
	WorldLayerEditor(Editor &editor, WorldLayerDocument document);
	~WorldLayerEditor();
	void validate();
	void refresh();
	bool save();
	bool suppressed(const Item* item) const;
	const Item* sprite(uint16_t id) const;
	std::string at(const world_layers::Position &position) const;
	world_layers::Position position(const std::string &id) const;
	void finishDrag(bool commit);

	WorldLayerDocument document;
	world_layers::Diagnostics diagnostics;
	std::optional<world_layers::Position> drag;

private:
	Editor &editor;
	world_layers::ApplicationPlan plan;
	world_layers::Diagnostics catalogDiagnostics;
	std::vector<world_layers::UniqueOccurrence> uniqueIds;
	std::unordered_map<uint16_t, std::unique_ptr<Item>> sprites;
};

void ShowWorldLayerPanel(bool show = true);
bool IsWorldLayerPanelShown();
