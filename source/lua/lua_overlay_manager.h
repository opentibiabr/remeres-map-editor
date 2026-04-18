#ifndef RME_LUA_OVERLAY_MANAGER_H
#define RME_LUA_OVERLAY_MANAGER_H

#include "lua_engine.h"
#include "../map_overlay.h"
#include <string>
#include <string_view>
#include <vector>
#include <functional>

class Tile;
class Item;

using LuaLogFunc = std::function<void(const std::string &, bool)>;

class LuaOverlayManager {
public:
	struct MapOverlay {
		std::string id;
		bool enabled = true;
		int order = 0;
		sol::function ondraw;
		sol::function onhover;
	};
	struct MapOverlayShowItem {
		std::string label;
		std::string overlayId;
		bool enabled = true;
		sol::function ontoggle;
	};

	bool addMapOverlay(const std::string &id, sol::table options);
	bool removeMapOverlay(std::string_view id);
	bool setMapOverlayEnabled(std::string_view id, bool enabled);
	bool registerMapOverlayShow(const std::string &label, const std::string &overlayId, bool enabled, sol::function ontoggle);
	bool setMapOverlayShowEnabled(const std::string &overlayId, bool enabled);
	bool isMapOverlayEnabled(std::string_view id) const;
	const std::vector<MapOverlayShowItem> &getMapOverlayShows() const {
		return mapOverlayShows;
	}
	void collectMapOverlayCommands(LuaEngine &engine, const MapViewInfo &view, std::vector<MapOverlayCommand> &out);
	void updateMapOverlayHover(LuaEngine &engine, int map_x, int map_y, int map_z, int screen_x, int screen_y, Tile* tile, Item* topItem);
	const MapOverlayHoverState &getMapOverlayHover() const {
		return mapOverlayHover;
	}

	void clear();
	void setLogFunc(LuaLogFunc func) {
		logFunc = std::move(func);
	}

private:
	std::vector<MapOverlay> mapOverlays;
	std::vector<MapOverlayShowItem> mapOverlayShows;
	MapOverlayHoverState mapOverlayHover;
	LuaLogFunc logFunc;

	void log(const std::string &msg, bool isError) {
		if (logFunc) {
			logFunc(msg, isError);
		}
	}
};

#endif
