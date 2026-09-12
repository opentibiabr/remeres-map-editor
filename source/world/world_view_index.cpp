#include "world/world_view_index.h"

#include <algorithm>
#include <unordered_set>

void WorldViewIndex::rebuild(const world_layers::Project &project, const world_layers::Diagnostics &diagnostics) {
	floors.clear();
	entries.clear();
	identities.clear();
	bounds.reset();
	candidates.clear();
	std::unordered_set<std::string> invalid;
	for (const auto &diagnostic : diagnostics) {
		invalid.insert(diagnostic.object);
	}
	const bool invalidProject = invalid.contains("");
	for (const auto &layer : project.layers) {
		if (!layer.enabled) {
			continue;
		}
		for (const auto &object : layer.objects) {
			if (!object.container.empty() || (object.selector && !object.selector->container.empty())) {
				continue;
			}
			const auto position = world_layers::objectPosition(project, object);
			if (!position || !world_layers::isValidPosition(*position)) {
				continue;
			}
			auto id = world_layers::objectId(layer, object);
			const auto index = entries.size();
			floors[position->z][position->y][position->x].push_back(index);
			identities.emplace(id, index);
			const bool hasError = invalidProject || invalid.contains(id);
			entries.push_back({ std::move(id), *position, object.itemId, object.mode, hasError });
		}
	}
}

const WorldViewIndex::Entry &WorldViewIndex::entry(size_t index) const {
	return entries[index];
}

const WorldViewIndex::Entry* WorldViewIndex::find(const std::string &id) const {
	const auto found = identities.find(id);
	return found == identities.end() ? nullptr : &entries[found->second];
}

const std::vector<size_t> &WorldViewIndex::at(const world_layers::Position &position) const {
	static const std::vector<size_t> empty;
	const auto floor = floors.find(position.z);
	if (floor == floors.end()) {
		return empty;
	}
	const auto row = floor->second.find(position.y);
	if (row == floor->second.end()) {
		return empty;
	}
	const auto column = row->second.find(position.x);
	return column == row->second.end() ? empty : column->second;
}

const std::vector<size_t> &WorldViewIndex::visible(int floor, int left, int top, int right, int bottom) const {
	const Bounds next { floor, left, top, right, bottom };
	if (bounds && *bounds == next) {
		return candidates;
	}
	bounds = next;
	candidates.clear();
	const auto level = floors.find(floor);
	if (level == floors.end() || left > right || top > bottom) {
		return candidates;
	}
	// Only occupied rows and columns inside the viewport are visited, even at
	// the widest zoom. Empty map space and other floors incur no scan.
	for (auto row = level->second.lower_bound(top); row != level->second.end() && row->first <= bottom; ++row) {
		for (auto column = row->second.lower_bound(left); column != row->second.end() && column->first <= right; ++column) {
			candidates.insert(candidates.end(), column->second.begin(), column->second.end());
		}
	}
	// Retain catalog/layer order for items sharing a tile and sprite overlap.
	std::sort(candidates.begin(), candidates.end());
	return candidates;
}
