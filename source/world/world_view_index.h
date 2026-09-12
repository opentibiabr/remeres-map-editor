#pragma once

#include "world/world_layers.hpp"
#include <map>
#include <optional>
#include <unordered_map>

// Editor-owned display data. No item or document pointers survive a revision.
class WorldViewIndex {
public:
	struct Entry {
		std::string id;
		world_layers::Position position;
		uint16_t itemId;
		world_layers::SourceMode mode;
		bool invalid;
	};
	void rebuild(const world_layers::Project &project, const world_layers::Diagnostics &diagnostics);
	const Entry &entry(size_t index) const;
	const Entry* find(const std::string &id) const;
	const std::vector<size_t> &at(const world_layers::Position &position) const;
	const std::vector<size_t> &visible(int floor, int left, int top, int right, int bottom) const;

private:
	struct Bounds {
		int floor, left, top, right, bottom;
		bool operator==(const Bounds &) const = default;
	};
	using Columns = std::map<int, std::vector<size_t>>;
	using Rows = std::map<int, Columns>;
	std::map<int, Rows> floors;
	std::vector<Entry> entries;
	std::unordered_map<std::string, size_t> identities;
	mutable std::optional<Bounds> bounds;
	mutable std::vector<size_t> candidates;
};
