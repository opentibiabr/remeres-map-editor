//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "mcp_tools.h"

#include "../complexitem.h"
#include "../const.h"
#include "../editor.h"
#include "../item.h"
#include "../items.h"
#include "../gui.h"
#include "../map.h"
#include "../position.h"

namespace mcp {

	ToolRegistry &ToolRegistry::get() {
		static ToolRegistry instance;
		return instance;
	}

	void ToolRegistry::add(Tool tool) {
		tools.push_back(std::move(tool));
	}

	const Tool* ToolRegistry::find(const std::string &name) const {
		for (const Tool &tool : tools) {
			if (tool.name == name) {
				return &tool;
			}
		}
		return nullptr;
	}

	void ToolRegistry::ensureRegistered() {
		if (registered) {
			return;
		}
		registered = true;
		registerGuideTools(*this);
		registerMapTools(*this);
		registerEntityTools(*this);
		registerEditTools(*this);
		registerManageTools(*this);
		registerAssetTools(*this);
		registerAnalyzeTools(*this);
		registerOpsTools(*this);
		registerBrushTools(*this);
		registerSelectTools(*this);
		registerIoTools(*this);
		registerClientViewTools(*this);
		registerTerrainTools(*this);
		registerStampTools(*this);
		registerScriptTools(*this);
	}

	// Containers can nest; stop well before anything pathological so one
	// tile_get cannot blow up the response.
	static constexpr int MAX_CONTAINER_DEPTH = 5;

	json itemToJson(const Item* item, int depth) {
		if (!item) {
			return nullptr;
		}

		json out {
			{ "id", item->getID() },
			{ "name", item->getName() }
		};

		if (item->isStackable() || item->isCharged()) {
			out["count"] = item->getCount();
		}
		if (item->getActionID() != 0) {
			out["actionId"] = item->getActionID();
		}
		if (item->getUniqueID() != 0) {
			out["uniqueId"] = item->getUniqueID();
		}

		const std::string text = item->getText();
		if (!text.empty()) {
			out["text"] = text;
		}

		// The complex subclasses carry the quest-relevant state: where a
		// portal goes, what is inside a chest, which door of a house this is.
		if (const auto* teleport = dynamic_cast<const Teleport*>(item)) {
			out["type"] = "teleport";
			out["destination"] = positionToJson(teleport->getDestination());
			if (!teleport->hasDestination()) {
				out["warning"] = "this teleport has no destination set";
			}
		} else if (const auto* door = dynamic_cast<const Door*>(item)) {
			out["type"] = "door";
			out["doorId"] = door->getDoorID();
		} else if (const auto* depot = dynamic_cast<const Depot*>(item)) {
			out["type"] = "depot";
			out["depotId"] = depot->getDepotID();
		} else if (const auto* container = dynamic_cast<const Container*>(item)) {
			out["type"] = "container";
			out["volume"] = container->getVolume();
			out["itemCount"] = container->getItemCount();

			if (depth >= MAX_CONTAINER_DEPTH) {
				out["contents"] = "omitted: nesting too deep";
			} else {
				json contents = json::array();
				for (const Item* child : const_cast<Container*>(container)->getVector()) {
					contents.push_back(itemToJson(child, depth + 1));
				}
				out["contents"] = std::move(contents);
			}
		}

		return out;
	}

	Item* buildItem(const json &spec) {
		int id = 0;
		if (spec.is_number_integer()) {
			id = spec.get<int>();
		} else if (spec.is_object() && spec.contains("id")) {
			id = spec["id"].get<int>();
		} else {
			throw McpError("an item must be an id, or an object with an id field");
		}

		if (g_items[static_cast<uint16_t>(id)].id == 0) {
			throw McpError(fmt::format("item id {} does not exist in the loaded client", id));
		}

		Item* item = Item::Create(static_cast<uint16_t>(id));
		if (!item) {
			throw McpError(fmt::format("could not create item {}", id));
		}

		if (!spec.is_object()) {
			return item;
		}

		// Anything past this point can throw, and the item is not owned by a
		// tile yet, so it has to be cleaned up by hand.
		try {
			if (spec.contains("count")) {
				item->setSubtype(static_cast<uint16_t>(spec["count"].get<int>()));
			} else if (spec.contains("subtype")) {
				item->setSubtype(static_cast<uint16_t>(spec["subtype"].get<int>()));
			}
			if (spec.contains("actionId")) {
				item->setActionID(static_cast<uint16_t>(spec["actionId"].get<int>()));
			}
			if (spec.contains("uniqueId")) {
				item->setUniqueID(static_cast<uint16_t>(spec["uniqueId"].get<int>()));
			}
			if (spec.contains("text")) {
				item->setText(spec["text"].get<std::string>());
			}

			if (spec.contains("destination")) {
				auto* teleport = dynamic_cast<Teleport*>(item);
				if (!teleport) {
					throw McpError(fmt::format("item {} is not a teleport, so it cannot have a destination", id));
				}
				teleport->setDestination(parsePosition(spec["destination"], "destination"));
			}

			if (spec.contains("doorId")) {
				auto* door = dynamic_cast<Door*>(item);
				if (!door) {
					throw McpError(fmt::format("item {} is not a door, so it cannot have a doorId", id));
				}
				door->setDoorID(static_cast<uint8_t>(spec["doorId"].get<int>()));
			}

			if (spec.contains("depotId")) {
				auto* depot = dynamic_cast<Depot*>(item);
				if (!depot) {
					throw McpError(fmt::format("item {} is not a depot, so it cannot have a depotId", id));
				}
				depot->setDepotID(static_cast<uint8_t>(spec["depotId"].get<int>()));
			}

			if (spec.contains("contents")) {
				auto* container = dynamic_cast<Container*>(item);
				if (!container) {
					throw McpError(fmt::format("item {} is not a container, so it cannot hold contents", id));
				}
				if (!spec["contents"].is_array()) {
					throw McpError("contents must be an array of items");
				}
				for (const json &child : spec["contents"]) {
					container->getVector().push_back(buildItem(child));
				}
			}
		} catch (...) {
			delete item;
			throw;
		}

		return item;
	}

	json itemSpecSchema() {
		return json {
			{ "type", "object" },
			{ "description", "an item id, or an object describing the item" },
			{ "properties", json { { "id", json { { "type", "integer" } } }, { "count", json { { "type", "integer" }, { "description", "stack size or subtype" } } }, { "actionId", json { { "type", "integer" } } }, { "uniqueId", json { { "type", "integer" } } }, { "text", json { { "type", "string" }, { "description", "for writable items" } } }, { "destination", positionSchema("teleport destination; only for teleport items") }, { "doorId", json { { "type", "integer" }, { "description", "only for door items" } } }, { "depotId", json { { "type", "integer" }, { "description", "only for depot items" } } }, { "contents", json { { "type", "array" }, { "description", "only for containers; the items inside, same shape as this one" } } } } },
			{ "required", json::array({ "id" }) }
		};
	}

	json positionSchema(const char* description) {
		return json {
			{ "type", "object" },
			{ "description", description },
			{ "properties", json { { "x", json { { "type", "integer" } } }, { "y", json { { "type", "integer" } } }, { "z", json { { "type", "integer" }, { "description", "floor, 0-15; 7 is ground level" } } } } },
			{ "required", json::array({ "x", "y", "z" }) }
		};
	}

	json positionArraySchema(const char* description) {
		return json {
			{ "type", "array" },
			{ "description", description },
			{ "items", positionSchema() }
		};
	}

	json emptySchema() {
		return json { { "type", "object" }, { "properties", json::object() } };
	}

	json textResult(const std::string &text) {
		return json {
			{ "content", json::array({ json { { "type", "text" }, { "text", text } } }) }
		};
	}

	json jsonResult(const json &value) {
		return textResult(value.dump(2));
	}

	json imageResult(const std::string &base64Data, const std::string &mimeType) {
		return json {
			{ "content", json::array({ json { { "type", "image" }, { "data", base64Data }, { "mimeType", mimeType } } }) }
		};
	}

	Editor* requireEditor() {
		Editor* editor = g_gui.GetCurrentEditor();
		if (!editor) {
			throw McpError("no map is open in the editor; open a map first");
		}
		return editor;
	}

	Position parsePosition(const json &value, const char* fieldName) {
		if (!value.is_object()) {
			throw McpError(std::string(fieldName) + " must be an object like {\"x\":1000,\"y\":1000,\"z\":7}");
		}
		if (!value.contains("x") || !value.contains("y") || !value.contains("z")) {
			throw McpError(std::string(fieldName) + " requires the x, y and z fields");
		}

		const int x = value["x"].get<int>();
		const int y = value["y"].get<int>();
		const int z = value["z"].get<int>();

		const Map &map = requireEditor()->getMap();
		if (x < 0 || y < 0 || x >= map.getWidth() || y >= map.getHeight()) {
			throw McpError(fmt::format(
				"{} ({},{}) is outside the map, which is {}x{}",
				fieldName, x, y, map.getWidth(), map.getHeight()
			));
		}
		if (z < 0 || z >= rme::MapLayers) {
			throw McpError(fmt::format("{} has floor {}, valid floors are 0..{}", fieldName, z, rme::MapLayers - 1));
		}

		return Position(x, y, z);
	}

	json positionToJson(const Position &pos) {
		return json { { "x", pos.x }, { "y", pos.y }, { "z", pos.z } };
	}

	int readInt(const json &params, const char* key, int defaultValue, int minValue, int maxValue) {
		if (!params.is_object() || !params.contains(key) || params[key].is_null()) {
			return defaultValue;
		}
		if (!params[key].is_number_integer()) {
			throw McpError(std::string(key) + " must be an integer");
		}
		const int value = params[key].get<int>();
		if (value < minValue || value > maxValue) {
			throw McpError(fmt::format("{} must be between {} and {}", key, minValue, maxValue));
		}
		return value;
	}

	std::string readString(const json &params, const char* key, const std::string &defaultValue) {
		if (!params.is_object() || !params.contains(key) || params[key].is_null()) {
			return defaultValue;
		}
		if (!params[key].is_string()) {
			throw McpError(std::string(key) + " must be a string");
		}
		return params[key].get<std::string>();
	}

} // namespace mcp
