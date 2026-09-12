#pragma once

#include "world/world_layers.hpp"
#include "world/world_files.hpp"
#include <set>

namespace world_layers {
	class MapView;
	struct ApplicationPlan;
}

// Synchronous provenance tracking across the native map editor's tile copies.
// Keys are never retained after the move has either committed or rolled back.
class WorldBaseMove {
public:
	WorldBaseMove(const world_layers::Project &project, const world_layers::ApplicationPlan &plan, world_layers::MapView &map, const world_layers::Position &offset, const std::set<uint64_t> &selected);
	void copied(uint64_t before, uint64_t after);
	bool finish(world_layers::MapView &map, world_layers::Project &project, std::string &error) const;

private:
	struct Binding {
		std::string id, fingerprint;
		uint64_t key;
		world_layers::Position position;
	};
	std::vector<Binding> bindings;
	std::map<uint64_t, std::vector<size_t>> keys;
};

struct WorldExternalChange {
	std::filesystem::path file;
	world_files::Revision base, local, disk;
};
enum class WorldExternalResult { Unchanged,
	                             Reloaded,
	                             Conflict,
	                             Invalid };

struct WorldDocumentChange {
	struct Layer {
		std::optional<world_layers::Layer> expected, replacement;
	};
	struct Catalog {
		world_layers::Project expected, replacement; // headers only; no object copies
	};
	struct Migration {
		world_layers::MigrationRecord expected, replacement;
	};
	std::map<std::filesystem::path, Layer> layers;
	std::optional<Catalog> catalog;
	std::map<std::filesystem::path, Migration> migrations;
	std::map<std::filesystem::path, uint64_t> revisions;
	std::string selected;
	size_t memorySize() const;
};

class WorldLayerDocument {
public:
	bool open(const std::filesystem::path &file, std::string &error);
	bool create(const std::filesystem::path &file, const std::filesystem::path &map, const std::filesystem::path &items, const std::string &id, std::string &error);
	bool observe(const std::filesystem::path &file, std::string &error);
	bool externalChanges(std::vector<WorldExternalChange> &changes, std::string &error) const;
	WorldExternalResult reconcileExternal(bool discardConflicts, std::vector<WorldExternalChange> &changes, std::string &error);
	std::vector<std::filesystem::path> observedFiles() const;
	bool saveDraft(const std::filesystem::path &directory, std::filesystem::path &catalog, std::string &error) const;
	const world_layers::Project &data() const {
		return project;
	}
	bool matchesMap(const std::filesystem::path &file) const;
	bool edit(const std::string &id, const world_layers::Object &value);
	bool exchange(const std::string &id, world_layers::Object &value);
	bool makeChange(const world_layers::Project &value, WorldDocumentChange &change, std::string &error) const;
	bool canExchange(const WorldDocumentChange &change, std::string &error) const;
	bool exchange(WorldDocumentChange &change, std::string &error);
	bool editProject(const world_layers::Project &value, std::string &error);
	static bool renameObject(world_layers::Project &value, const std::string &id, const std::string &replacement, std::string &error);
	static std::vector<std::string> dependents(const world_layers::Project &value, const std::string &id);
	static bool removeObject(world_layers::Project &value, const std::string &id, std::string &error);
	static bool moveToLayer(world_layers::Project &value, const std::string &id, const std::filesystem::path &layer, std::string &error);
	bool undo();
	bool redo();
	bool canUndo() const {
		return cursor != 0;
	}
	bool canRedo() const {
		return cursor < history.size();
	}
	bool dirty() const;
	bool save(std::string &error);
	bool canCopyForMap(const std::filesystem::path &map, std::string &error) const;
	bool copyForMap(const std::filesystem::path &map, std::string &error, const std::vector<WorldDocumentChange*> &editorHistory = {});
	uint64_t revision() const {
		return generation;
	}

	std::string selected;
	bool visible = true;

private:
	world_layers::Project project;
	std::map<std::filesystem::path, std::string> source, saved;
	std::map<std::filesystem::path, uint64_t> fileRevisions;
	std::vector<WorldDocumentChange> history;
	size_t cursor = 0;
	uint64_t generation = 0;
	bool unsaved = false;
};
