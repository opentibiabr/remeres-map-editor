#pragma once

#include <memory>
#include <string>

class wxComboBox;
class wxNotebook;
class wxWindow;
class Map;
namespace world_layers {
	struct Object;
	struct Project;
	struct MapItem;
}

wxComboBox* CreateWorldReferenceChoice(wxWindow* parent, const world_layers::Project &project, const std::string &value, bool containersOnly = false);

// Pages hosted by the normal item properties dialog. All values remain in the
// dialog's draft until its caller creates a single native undo action.
class WorldProperties {
public:
	WorldProperties(wxNotebook* notebook, world_layers::Object &object, const world_layers::Project &project, world_layers::Project* draft, const world_layers::MapItem &base, const Map* map);
	~WorldProperties();
	bool read(std::string &error);

private:
	struct State;
	std::unique_ptr<State> state;
};
