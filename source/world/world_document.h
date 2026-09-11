#pragma once

#include "world/world_layers.hpp"

class WorldLayerDocument {
public:
	bool open(const std::filesystem::path &file, std::string &error);
	const world_layers::Project &data() const {
		return project;
	}
	bool matchesMap(const std::filesystem::path &file) const;
	bool edit(const std::string &id, const world_layers::Object &value);
	bool exchange(const std::string &id, world_layers::Object &value);
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
	bool copyForMap(const std::filesystem::path &map, std::string &error);
	uint64_t revision() const {
		return generation;
	}

	std::string selected;
	bool visible = true;

private:
	struct Change {
		std::string id;
		world_layers::Object before, after;
	};
	world_layers::Project project;
	std::string projectSource;
	std::vector<std::string> source, saved;
	std::vector<std::vector<world_layers::Object>> savedObjects;
	std::vector<Change> history;
	size_t cursor = 0;
	uint64_t generation = 0;
};
