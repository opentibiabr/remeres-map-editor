#ifndef RME_DRAWING_OPTIONS_H
#define RME_DRAWING_OPTIONS_H

#include <cstdint>
#include <string>
#include <vector>

#include "util/outfit.h"
#include "util/sprite_types.h"

struct TooltipEntry {
	std::string label;
	std::string value;
};

struct MapTooltip {
	enum class Limits {
		MAX_VALUE_DISPLAY = 1024,
		MAX_WIDTH = 1024,
	};

	MapTooltip(int map_x, int map_y, int map_z, uint8_t r, uint8_t g, uint8_t b) :
		map_x(map_x), map_y(map_y), map_z(map_z), r(r), g(g), b(b) { }

	void addEntry(const std::string &label, const std::string &value) {
		std::string val = value;
		if (val.size() > static_cast<size_t>(std::to_underlying(Limits::MAX_VALUE_DISPLAY))) {
			val = val.substr(0, static_cast<size_t>(std::to_underlying(Limits::MAX_VALUE_DISPLAY))) + "...";
		}
		entries.emplace_back(label, val);
	}

	int map_x;
	int map_y;
	int map_z;
	uint8_t r, g, b;
	std::vector<TooltipEntry> entries;
};

struct DrawingOptions {
public:
	DrawingOptions();

	void SetIngame();
	void SetDefault();

	bool isOnlyColors() const noexcept;
	bool isTileIndicators() const noexcept;
	bool isTooltips() const noexcept;

	bool transparent_floors;
	bool transparent_items;
	bool show_ingame_box;
	bool show_light_strength;
	bool show_lights;
	bool ingame;
	bool dragging;

	int show_grid;
	bool show_all_floors;
	bool show_monsters;
	bool show_spawns_monster;
	bool show_npcs;
	bool show_spawns_npc;
	bool show_houses;
	bool show_shade;
	bool show_special_tiles;
	bool show_items;

	bool highlight_items;
	bool show_blocking;
	bool show_tooltips;
	bool show_performance_stats;
	bool show_as_minimap;
	bool show_only_colors;
	bool show_only_modified;
	bool show_preview;
	bool show_hooks;
	bool show_pickupables;
	bool show_moveables;
	bool show_avoidables;
	bool hide_items_when_zoomed;
};

struct BlitOptions {
	bool adjustZoom = false;
	bool isEditorSprite = false;
	Outfit outfit = {};
	int spriteId = 0;
	SpriteUV uv = { 0.f, 0.f, 1.f, 1.f };
};

#endif
