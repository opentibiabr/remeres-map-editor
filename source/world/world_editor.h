#pragma once

#include "world/world_document.h"
#include "world/world_validation.hpp"
#include <memory>

class Editor;
class Item;
class Position;
class PalettePanel;
class wxWindow;
class WorldFileMonitor;

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
	bool editProject(const world_layers::Project &value, const std::string &selection = {});
	void removeSelected();
	world_layers::MapItem baseItem(const std::string &id) const;
	bool moveBaseItem(const std::string &id, const world_layers::Object &value, const world_layers::Project* draft = nullptr);
	bool ensureV2();
	void manageLayers();
	void manageDescriptors();
	void createObject(world_layers::ObjectKind kind, world_layers::SourceMode mode);
	void renameSelected();
	void moveSelectedToLayer();
	void refresh();
	void checkExternal(bool interactive);
	void saveDraft();
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
	std::string externalStatus;

private:
	Editor &editor;
	world_layers::ApplicationPlan plan;
	world_layers::Diagnostics catalogDiagnostics;
	std::unordered_map<uint64_t, std::vector<world_layers::UniqueOccurrence>> uniqueIds;
	uint64_t mapRevision = 0;
	std::unordered_map<uint16_t, std::unique_ptr<Item>> sprites;
	std::optional<std::filesystem::path> chooseLayer();
	std::unique_ptr<WorldFileMonitor> fileMonitor;
};

PalettePanel* CreateWorldPalette(wxWindow* parent);
void ShowWorldPalette();
void CreateWorldCatalog();
bool RecoverWorldPublication(const std::filesystem::path &catalog, wxWindow* parent);
