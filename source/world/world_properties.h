#pragma once

#include <memory>
#include <string>

class wxNotebook;
class Map;
namespace world_layers {
	struct Object;
	struct Project;
	struct MapItem;
}

// Pages hosted by the normal item properties dialog. All values remain in the
// dialog's draft until its caller creates a single native undo action.
class WorldProperties {
public:
	WorldProperties(wxNotebook* notebook, world_layers::Object &object, world_layers::Project &project, const world_layers::MapItem &base, const Map* map);
	~WorldProperties();
	bool read(std::string &error);

private:
	struct State;
	std::unique_ptr<State> state;
};
