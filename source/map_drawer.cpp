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

#ifdef __WINDOWS__
	#include <windows.h>
	#include <psapi.h>
	#pragma comment(lib, "psapi.lib")
#else
	#include <unistd.h>
	#include <cstring>
#endif

#include <cstdint>
#include <thread>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <format>
#include <array>

#include "editor.h"
#include "gui.h"
#include "sprites.h"
#include "map_drawer.h"
#include <unordered_set>
#include "map_display.h"
#include "copybuffer.h"
#include "live_socket.h"
#include "graphics.h"

#include "doodad_brush.h"
#include "monster_brush.h"
#include "house_exit_brush.h"
#include "house_brush.h"
#include "spawn_monster_brush.h"
#include "sprite_appearances.h"
#include "npc_brush.h"
#include "spawn_npc_brush.h"
#include "wall_brush.h"
#include "carpet_brush.h"
#include "raw_brush.h"
#include "table_brush.h"
#include "waypoint_brush.h"
#include "zone_brush.h"
#include "light_drawer.h"
#include "gl_renderer.h"

DrawingOptions::DrawingOptions() {
	SetDefault();
}

void DrawingOptions::SetDefault() {
	transparent_floors = false;
	transparent_items = false;
	show_ingame_box = false;
	show_lights = false;
	show_light_strength = true;
	ingame = false;
	dragging = false;

	show_grid = 0;
	show_all_floors = true;
	show_monsters = true;
	show_spawns_monster = true;
	show_npcs = true;
	show_spawns_npc = true;
	show_houses = true;
	show_shade = true;
	show_special_tiles = true;
	show_items = true;

	highlight_items = false;
	show_blocking = false;
	show_tooltips = false;
	show_as_minimap = false;
	show_only_colors = false;
	show_only_modified = false;
	show_preview = false;
	show_hooks = false;
	show_pickupables = false;
	show_moveables = false;
	show_avoidables = false;
	hide_items_when_zoomed = true;
}

void DrawingOptions::SetIngame() {
	transparent_floors = false;
	transparent_items = false;
	show_ingame_box = false;
	show_lights = false;
	show_light_strength = false;
	ingame = true;
	dragging = false;

	show_grid = 0;
	show_all_floors = true;
	show_monsters = true;
	show_spawns_monster = false;
	show_npcs = true;
	show_spawns_npc = false;
	show_houses = false;
	show_shade = false;
	show_special_tiles = false;
	show_items = true;

	highlight_items = false;
	show_blocking = false;
	show_tooltips = false;
	show_performance_stats = false;
	show_as_minimap = false;
	show_only_colors = false;
	show_only_modified = false;
	show_preview = false;
	show_hooks = false;
	show_pickupables = false;
	show_moveables = false;
	show_avoidables = false;
	hide_items_when_zoomed = false;
}

bool DrawingOptions::isOnlyColors() const noexcept {
	return show_as_minimap || show_only_colors;
}

bool DrawingOptions::isTileIndicators() const noexcept {
	if (isOnlyColors()) {
		return false;
	}
	return show_pickupables || show_moveables || show_houses || show_spawns_monster || show_spawns_npc;
}

bool DrawingOptions::isTooltips() const noexcept {
	return show_tooltips && !isOnlyColors();
}

MapDrawer::MapDrawer(MapCanvas* canvas) :
	canvas(canvas),
	editor(canvas->editor)
#ifdef __WINDOWS__
	,
	last_cpu_time {},
	last_sys_time {},
	last_now_time {}
#endif
{
	perf_update_timer.Start();
}

MapDrawer::~MapDrawer() {
	Release();
	renderer->shutdown();
}

void MapDrawer::SetupVars() {
	canvas->MouseToMap(&mouse_map_x, &mouse_map_y);
	canvas->GetViewBox(&view_scroll_x, &view_scroll_y, &screensize_x, &screensize_y);

	dragging = canvas->dragging;
	dragging_draw = canvas->dragging_draw;

	zoom = static_cast<float>(canvas->GetZoom());
	tile_size = int(rme::TileSize / zoom); // after zoom
	floor = canvas->GetFloor();

	if (options.show_all_floors) {
		if (floor < 8) {
			start_z = rme::MapGroundLayer;
		} else {
			start_z = std::min(rme::MapMaxLayer, floor + 2);
		}
	} else {
		start_z = floor;
	}

	end_z = floor;
	superend_z = (floor > rme::MapGroundLayer ? 8 : 0);

	start_x = view_scroll_x / rme::TileSize;
	start_y = view_scroll_y / rme::TileSize;

	if (floor > rme::MapGroundLayer) {
		start_x -= 2;
		start_y -= 2;
	}

	end_x = start_x + screensize_x / tile_size + 2;
	end_y = start_y + screensize_y / tile_size + 2;
}

void MapDrawer::SetupGL() {
	glViewport(0, 0, screensize_x, screensize_y);

	renderer->init();

	std::array<int, 4> vPort {};
	glGetIntegerv(GL_VIEWPORT, vPort.data());

	renderer->setOrtho(0, vPort[2] * zoom, vPort[3] * zoom, 0);
}

void MapDrawer::Release() {
	tooltips.clear();
	if (light_drawer) {
		light_drawer->clear();
	}

	renderer->flush();
}

void MapDrawer::Draw() {
	DrawBackground();
	DrawMap();
	if (options.show_lights) {
		light_drawer->draw(start_x, start_y, end_x, end_y, view_scroll_x, view_scroll_y, renderer.get());
	}
	DrawDraggingShadow();
	DrawHigherFloors();
	if (options.dragging) {
		DrawSelectionBox();
	}
	DrawLiveCursors();
	DrawBrush();
	if (options.show_grid && zoom <= 10.f) {
		DrawGrid();
	}
	if (options.show_ingame_box) {
		DrawIngameBox();
	}
	if (options.isTooltips() || globalTooltipFade > 0.0f) {
		DrawTooltips();
	}
	if (options.show_performance_stats) {
		DrawPerformanceStats();
	}
}

void MapDrawer::DrawBackground() {
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);
}

inline int getFloorAdjustment(int floor) {
	if (floor > rme::MapGroundLayer) { // Underground
		return 0; // No adjustment
	} else {
		return rme::TileSize * (rme::MapGroundLayer - floor);
	}
}

void MapDrawer::DrawShade(int map_z) {
	if (map_z == end_z && start_z != end_z) {

		float x = screensize_x * zoom;
		float y = screensize_y * zoom;
		renderer->drawColoredQuad(0, 0, x, y, { 0, 0, 0, 128 });
	}
}

void MapDrawer::DrawMap() {
	tooltips.clear();
	bool live_client = editor.IsLiveClient();

	Brush* brush = g_gui.GetCurrentBrush();

	// The current house we're drawing
	current_house_id = 0;
	if (brush) {
		if (brush->isHouse()) {
			current_house_id = brush->asHouse()->getHouseID();
		} else if (brush->isHouseExit()) {
			current_house_id = brush->asHouseExit()->getHouseID();
		}
	}

	bool only_colors = options.isOnlyColors();
	bool tile_indicators = options.isTileIndicators();

	for (int map_z = start_z; map_z >= superend_z; map_z--) {
		if (options.show_shade) {
			DrawShade(map_z);
		}

		if (map_z >= end_z) {

			int nd_start_x = start_x & ~3;
			int nd_start_y = start_y & ~3;
			int nd_end_x = (end_x & ~3) + 4;
			int nd_end_y = (end_y & ~3) + 4;

			for (int nd_map_x = nd_start_x; nd_map_x <= nd_end_x; nd_map_x += 4) {
				for (int nd_map_y = nd_start_y; nd_map_y <= nd_end_y; nd_map_y += 4) {
					QTreeNode* nd = editor.getMap().getLeaf(nd_map_x, nd_map_y);
					if (!nd) {
						if (!live_client) {
							continue;
						}
						nd = editor.getMap().createLeaf(nd_map_x, nd_map_y);
						nd->setVisible(false, false);
					}

					if (!live_client || nd->isVisible(map_z > rme::MapGroundLayer)) {
						for (int map_x = 0; map_x < 4; ++map_x) {
							for (int map_y = 0; map_y < 4; ++map_y) {
								TileLocation* location = nd->getTile(map_x, map_y, map_z);
								DrawTile(location);
								// draw light, but only if not zoomed too far
								if (location && options.show_lights && zoom <= 10) {
									AddLight(location);
								}
							}
						}
						if (tile_indicators) {
							for (int map_x = 0; map_x < 4; ++map_x) {
								for (int map_y = 0; map_y < 4; ++map_y) {
									DrawTileIndicators(nd->getTile(map_x, map_y, map_z));
								}
							}
						}
					} else {
						if (!nd->isRequested(map_z > rme::MapGroundLayer)) {
							// Request the node
							editor.QueryNode(nd_map_x, nd_map_y, map_z > rme::MapGroundLayer);
							nd->setRequested(map_z > rme::MapGroundLayer, true);
						}
						int cy = (nd_map_y)*rme::TileSize - view_scroll_y - getFloorAdjustment(floor);
						int cx = (nd_map_x)*rme::TileSize - view_scroll_x - getFloorAdjustment(floor);

						renderer->drawColoredQuad(cx, cy, rme::TileSize * 4, rme::TileSize * 4, { 255, 0, 255, 128 });
					}
				}
			}

			DrawPositionIndicator(map_z);
		}

		// Draws the doodad preview or the paste preview (or import preview)
		DrawSecondaryMap(map_z);

		--start_x;
		--start_y;
		++end_x;
		++end_y;
	}
}

void MapDrawer::DrawSecondaryMap(int map_z) {
	if (options.ingame) {
		return;
	}

	BaseMap* secondary_map = g_gui.secondary_map;
	if (!secondary_map) {
		return;
	}

	Position normal_pos;
	Position to_pos(mouse_map_x, mouse_map_y, floor);

	if (canvas->isPasting()) {
		normal_pos = editor.copybuffer.getPosition();
	} else {
		Brush* brush = g_gui.GetCurrentBrush();
		if (brush && brush->isDoodad()) {
			normal_pos = Position(0x8000, 0x8000, 0x8);
		}
	}

	for (int map_x = start_x; map_x <= end_x; map_x++) {
		for (int map_y = start_y; map_y <= end_y; map_y++) {
			Position final_pos(map_x, map_y, map_z);
			Position pos = normal_pos + final_pos - to_pos;
			if (pos.z < 0 || pos.z >= rme::MapLayers) {
				continue;
			}

			Tile* tile = secondary_map->getTile(pos);
			if (!tile) {
				continue;
			}

			int draw_x, draw_y;
			getDrawPosition(final_pos, draw_x, draw_y);

			// Draw ground
			uint8_t r = 160, g = 160, b = 160;
			if (tile->ground) {
				if (options.show_blocking && tile->isBlocking()) {
					g = g / 3 * 2;
					b = b / 3 * 2;
				}
				if (options.show_houses && tile->isHouseTile()) {
					if (tile->getHouseID() == current_house_id) {
						r /= 2;
					} else {
						r /= 2;
						g /= 2;
					}
				} else if (options.show_special_tiles && tile->isPZ()) {
					r /= 2;
					b /= 2;
				}
				if (options.show_special_tiles && tile->getMapFlags() & TILESTATE_PVPZONE) {
					r = r / 3 * 2;
					b = r / 3 * 2;
				}
				if (options.show_special_tiles && tile->hasZone(g_gui.zone_brush->getZone())) {
					r = r / 3 * 2;
					b = b / 3 * 2;
				}
				if (options.show_special_tiles && tile->getMapFlags() & TILESTATE_NOLOGOUT) {
					b /= 2;
				}
				if (options.show_special_tiles && tile->getMapFlags() & TILESTATE_NOPVP) {
					g /= 2;
				}
				BlitItem(draw_x, draw_y, tile, tile->ground, true, r, g, b, 160);
			}

			bool hidden = options.hide_items_when_zoomed && zoom > 10.f;

			// Draw items
			if (!hidden && !tile->items.empty()) {
				for (const Item* item : tile->items) {
					if (item->isBorder()) {
						BlitItem(draw_x, draw_y, tile, item, true, 160, r, g, b);
					} else {
						BlitItem(draw_x, draw_y, tile, item, true, 160, 160, 160, 160);
					}
				}
			}

			// Monsters
			if (!hidden && options.show_monsters && !tile->monsters.empty()) {
				for (auto monster : tile->monsters) {
					BlitCreature(draw_x, draw_y, monster);
				}
			}

			// NPCS
			if (!hidden && options.show_npcs && tile->npc) {
				BlitCreature(draw_x, draw_y, tile->npc);
			}
		}
	}
}

void MapDrawer::DrawIngameBox() {
	int center_x = start_x + int(screensize_x * zoom / 64);
	int center_y = start_y + int(screensize_y * zoom / 64);

	int offset_y = 2;
	int box_start_map_x = center_x;
	int box_start_map_y = center_y + offset_y;
	int box_end_map_x = center_x + rme::ClientMapWidth;
	int box_end_map_y = center_y + rme::ClientMapHeight + offset_y;

	int box_start_x = box_start_map_x * rme::TileSize - view_scroll_x;
	int box_start_y = box_start_map_y * rme::TileSize - view_scroll_y;
	int box_end_x = box_end_map_x * rme::TileSize - view_scroll_x;
	int box_end_y = box_end_map_y * rme::TileSize - view_scroll_y;

	static wxColor side_color(0, 0, 0, 200);

	// left side
	if (box_start_map_x >= start_x) {
		drawFilledRect(0, 0, box_start_x, screensize_y * zoom, side_color);
	}

	// right side
	if (box_end_map_x < end_x) {
		drawFilledRect(box_end_x, 0, screensize_x * zoom, screensize_y * zoom, side_color);
	}

	// top side
	if (box_start_map_y >= start_y) {
		drawFilledRect(box_start_x, 0, box_end_x - box_start_x, box_start_y, side_color);
	}

	// bottom side
	if (box_end_map_y < end_y) {
		drawFilledRect(box_start_x, box_end_y, box_end_x - box_start_x, screensize_y * zoom, side_color);
	}

	float lineW = zoom > 1.0f ? zoom : 1.0f;

	// hidden tiles
	drawRect(box_start_x, box_start_y, box_end_x - box_start_x, box_end_y - box_start_y, *wxRED, lineW);

	// visible tiles
	box_start_x += rme::TileSize;
	box_start_y += rme::TileSize;
	box_end_x -= 2 * rme::TileSize;
	box_end_y -= 2 * rme::TileSize;
	drawRect(box_start_x, box_start_y, box_end_x - box_start_x, box_end_y - box_start_y, *wxGREEN, lineW);

	// player position
	box_start_x += ((rme::ClientMapWidth / 2) - 2) * rme::TileSize;
	box_start_y += ((rme::ClientMapHeight / 2) - 2) * rme::TileSize;
	box_end_x = box_start_x + rme::TileSize;
	box_end_y = box_start_y + rme::TileSize;
	drawRect(box_start_x, box_start_y, box_end_x - box_start_x, box_end_y - box_start_y, *wxGREEN, lineW);
}

void MapDrawer::DrawGrid() {

	std::vector<float> verts;
	for (int y = start_y; y < end_y; ++y) {
		auto py = static_cast<float>(y * rme::TileSize - view_scroll_y);
		verts.push_back(static_cast<float>(start_x * rme::TileSize - view_scroll_x));
		verts.push_back(py);
		verts.push_back(static_cast<float>(end_x * rme::TileSize - view_scroll_x));
		verts.push_back(py);
	}
	for (int x = start_x; x < end_x; ++x) {
		auto px = static_cast<float>(x * rme::TileSize - view_scroll_x);
		verts.push_back(px);
		verts.push_back(static_cast<float>(start_y * rme::TileSize - view_scroll_y));
		verts.push_back(px);
		verts.push_back(static_cast<float>(end_y * rme::TileSize - view_scroll_y));
	}

	if (!verts.empty()) {
		float lineWidth = zoom > 1.0f ? zoom : 1.0f;
		renderer->drawLines(verts.data(), static_cast<int>(verts.size() / 4), 255, 255, 255, 128, lineWidth);
	}
}

void MapDrawer::DrawDraggingShadow() {
	if (!dragging || options.ingame || editor.getSelection().isBusy()) {
		return;
	}

	for (Tile* tile : editor.getSelection()) {
		int move_z = canvas->drag_start_z - floor;
		int move_x = canvas->drag_start_x - mouse_map_x;
		int move_y = canvas->drag_start_y - mouse_map_y;

		if (move_x == 0 && move_y == 0 && move_z == 0) {
			continue;
		}

		const Position &position = tile->getPosition();
		int pos_z = position.z - move_z;
		if (pos_z < 0 || pos_z >= rme::MapLayers) {
			continue;
		}

		int pos_x = position.x - move_x;
		int pos_y = position.y - move_y;

		// On screen and dragging?
		if (pos_x + 2 > start_x && pos_x < end_x && pos_y + 2 > start_y && pos_y < end_y) {
			Position pos(pos_x, pos_y, pos_z);
			int draw_x, draw_y;
			getDrawPosition(pos, draw_x, draw_y);

			ItemVector items = tile->getSelectedItems();
			Tile* dest_tile = editor.getMap().getTile(pos);

			for (Item* item : items) {
				if (dest_tile) {
					BlitItem(draw_x, draw_y, dest_tile, item, true, 160, 160, 160, 160);
				} else {
					BlitItem(draw_x, draw_y, pos, item, true, 160, 160, 160, 160);
				}
			}

			if (options.show_monsters && !tile->monsters.empty()) {
				for (auto monster : tile->monsters) {
					if (!monster->isSelected()) {
						continue;
					}

					BlitCreature(draw_x, draw_y, monster);
				}
			}

			if (tile->spawnMonster && tile->spawnMonster->isSelected()) {
				DrawIndicator(draw_x, draw_y, EDITOR_SPRITE_MONSTERS, 160, 160, 160, 160);
			}

			if (options.show_npcs && tile->npc && tile->npc->isSelected()) {
				BlitCreature(draw_x, draw_y, tile->npc);
			}
			if (tile->spawnNpc && tile->spawnNpc->isSelected()) {
				DrawIndicator(draw_x, draw_y, EDITOR_SPRITE_NPCS, 160, 160, 160, 160);
			}
		}
	}
}

void MapDrawer::DrawHigherFloors() {
	if (!options.transparent_floors || floor == 0 || floor == 8) {
		return;
	}

	int map_z = floor - 1;
	for (int map_x = start_x; map_x <= end_x; map_x++) {
		for (int map_y = start_y; map_y <= end_y; map_y++) {
			Tile* tile = editor.getMap().getTile(map_x, map_y, map_z);
			if (!tile) {
				continue;
			}

			int draw_x, draw_y;
			getDrawPosition(tile->getPosition(), draw_x, draw_y);

			if (tile->ground) {
				if (tile->isPZ()) {
					BlitItem(draw_x, draw_y, tile, tile->ground, false, 128, 255, 128, 96);
				} else {
					BlitItem(draw_x, draw_y, tile, tile->ground, false, 255, 255, 255, 96);
				}
			}

			bool hidden = options.hide_items_when_zoomed && zoom > 10.f;
			if (!hidden && !tile->items.empty()) {
				for (const Item* item : tile->items) {
					BlitItem(draw_x, draw_y, tile, item, false, 255, 255, 255, 96);
				}
			}
		}
	}
}

void MapDrawer::DrawSelectionBox() {
	if (options.ingame) {
		return;
	}

	// Draw bounding box

	int last_click_rx = canvas->last_click_abs_x - view_scroll_x;
	int last_click_ry = canvas->last_click_abs_y - view_scroll_y;
	double cursor_rx = canvas->cursor_x * zoom;
	double cursor_ry = canvas->cursor_y * zoom;

	static std::array<std::array<double, 4>, 4> lines;

	lines[0][0] = last_click_rx;
	lines[0][1] = last_click_ry;
	lines[0][2] = cursor_rx;
	lines[0][3] = last_click_ry;

	lines[1][0] = cursor_rx;
	lines[1][1] = last_click_ry;
	lines[1][2] = cursor_rx;
	lines[1][3] = cursor_ry;

	lines[2][0] = cursor_rx;
	lines[2][1] = cursor_ry;
	lines[2][2] = last_click_rx;
	lines[2][3] = cursor_ry;

	lines[3][0] = last_click_rx;
	lines[3][1] = cursor_ry;
	lines[3][2] = last_click_rx;
	lines[3][3] = last_click_ry;
	std::array<float, 16> verts;
	for (int i = 0; i < 4; i++) {
		verts[i * 4] = static_cast<float>(lines[i][0]);
		verts[i * 4 + 1] = static_cast<float>(lines[i][1]);
		verts[i * 4 + 2] = static_cast<float>(lines[i][2]);
		verts[i * 4 + 3] = static_cast<float>(lines[i][3]);
	}
	renderer->drawStippledLines(verts.data(), 4, GLColor { 255, 255, 255, 255 }, 1.0f, 2, 0xAAAA);
}

void MapDrawer::DrawLiveCursors() {
	if (options.ingame || !editor.IsLive()) {
		return;
	}

	LiveSocket &live = editor.GetLive();
	for (LiveCursor &cursor : live.getCursorList()) {
		if (cursor.pos.z <= rme::MapGroundLayer && floor > rme::MapGroundLayer) {
			continue;
		}

		if (cursor.pos.z > rme::MapGroundLayer && floor <= 8) {
			continue;
		}

		if (cursor.pos.z < floor) {
			cursor.color = wxColor(
				cursor.color.Red(),
				cursor.color.Green(),
				cursor.color.Blue(),
				std::max<uint8_t>(cursor.color.Alpha() / 2, 64)
			);
		}

		int offset;
		if (cursor.pos.z <= rme::MapGroundLayer) {
			offset = (rme::MapGroundLayer - cursor.pos.z) * rme::TileSize;
		} else {
			offset = rme::TileSize * (floor - cursor.pos.z);
		}

		float draw_x = ((cursor.pos.x * rme::TileSize) - view_scroll_x) - offset;
		float draw_y = ((cursor.pos.y * rme::TileSize) - view_scroll_y) - offset;

		renderer->drawColoredQuad(draw_x, draw_y, rme::TileSize, rme::TileSize, { cursor.color.Red(), cursor.color.Green(), cursor.color.Blue(), cursor.color.Alpha() });
	}
}

void MapDrawer::DrawBrush() {
	if (options.ingame || !g_gui.IsDrawingMode() || !g_gui.GetCurrentBrush()) {
		return;
	}

	Brush* brush = g_gui.GetCurrentBrush();

	BrushColor brushColor = COLOR_BLANK;
	if (brush->isTerrain() || brush->isTable() || brush->isCarpet()) {
		brushColor = COLOR_BRUSH;
	} else if (brush->isHouse()) {
		brushColor = COLOR_HOUSE_BRUSH;
	} else if (brush->isFlag()) {
		brushColor = COLOR_FLAG_BRUSH;
	} else if (brush->isSpawnMonster()) {
		brushColor = COLOR_SPAWN_BRUSH;
	} else if (brush->isSpawnNpc()) {
		brushColor = COLOR_SPAWN_NPC_BRUSH;
	} else if (brush->isEraser()) {
		brushColor = COLOR_ERASER;
	}

	int adjustment = getFloorAdjustment(floor);

	if (dragging_draw) {
		ASSERT(brush->canDrag());

		if (brush->isWall()) {
			int last_click_start_map_x = std::min(canvas->last_click_map_x, mouse_map_x);
			int last_click_start_map_y = std::min(canvas->last_click_map_y, mouse_map_y);
			int last_click_end_map_x = std::max(canvas->last_click_map_x, mouse_map_x) + 1;
			int last_click_end_map_y = std::max(canvas->last_click_map_y, mouse_map_y) + 1;

			int last_click_start_sx = last_click_start_map_x * rme::TileSize - view_scroll_x - adjustment;
			int last_click_start_sy = last_click_start_map_y * rme::TileSize - view_scroll_y - adjustment;
			int last_click_end_sx = last_click_end_map_x * rme::TileSize - view_scroll_x - adjustment;
			int last_click_end_sy = last_click_end_map_y * rme::TileSize - view_scroll_y - adjustment;

			int delta_x = last_click_end_sx - last_click_start_sx;
			int delta_y = last_click_end_sy - last_click_start_sy;
			uint8_t cr = 0;
			uint8_t cg = 0;
			uint8_t cb = 0;
			uint8_t ca = 0;
			getBrushColor(brushColor, cr, cg, cb, ca);

			GLColor brushClr = { cr, cg, cb, ca };
			renderer->drawColoredQuad(last_click_start_sx, last_click_start_sy, delta_x, rme::TileSize, brushClr);

			if (delta_y > rme::TileSize) {
				renderer->drawColoredQuad(last_click_start_sx, last_click_start_sy + rme::TileSize, rme::TileSize, delta_y - 2 * rme::TileSize, brushClr);
			}

			if (delta_x > rme::TileSize && delta_y > rme::TileSize) {
				renderer->drawColoredQuad(last_click_end_sx - rme::TileSize, last_click_start_sy + rme::TileSize, rme::TileSize, delta_y - 2 * rme::TileSize, brushClr);
			}

			if (delta_y > rme::TileSize) {
				renderer->drawColoredQuad(last_click_start_sx, last_click_end_sy - rme::TileSize, delta_x, rme::TileSize, brushClr);
			}
		} else {

			if (g_gui.GetBrushShape() == BRUSHSHAPE_SQUARE || brush->isSpawnMonster() || brush->isSpawnNpc()) {
				if (brush->isRaw() || brush->isOptionalBorder()) {
					int start_x, end_x;
					int start_y, end_y;

					if (mouse_map_x < canvas->last_click_map_x) {
						start_x = mouse_map_x;
						end_x = canvas->last_click_map_x;
					} else {
						start_x = canvas->last_click_map_x;
						end_x = mouse_map_x;
					}
					if (mouse_map_y < canvas->last_click_map_y) {
						start_y = mouse_map_y;
						end_y = canvas->last_click_map_y;
					} else {
						start_y = canvas->last_click_map_y;
						end_y = mouse_map_y;
					}

					RAWBrush* raw_brush = nullptr;
					if (brush->isRaw()) {
						raw_brush = brush->asRaw();
					}

					for (int y = start_y; y <= end_y; y++) {
						int cy = y * rme::TileSize - view_scroll_y - adjustment;
						for (int x = start_x; x <= end_x; x++) {
							int cx = x * rme::TileSize - view_scroll_x - adjustment;
							if (brush->isOptionalBorder()) {
								uint8_t cr = 0;
								uint8_t cg = 0;
								uint8_t cb = 0;
								uint8_t ca = 0;
								getCheckColor(brush, Position(x, y, floor), cr, cg, cb, ca);
								renderer->drawColoredQuad(cx, cy, rme::TileSize, rme::TileSize, { cr, cg, cb, ca });
							} else {
								BlitSpriteType(cx, cy, raw_brush->getItemType()->sprite, 160, 160, 160, 160);
							}
						}
					}
				} else {
					int last_click_start_map_x = std::min(canvas->last_click_map_x, mouse_map_x);
					int last_click_start_map_y = std::min(canvas->last_click_map_y, mouse_map_y);
					int last_click_end_map_x = std::max(canvas->last_click_map_x, mouse_map_x) + 1;
					int last_click_end_map_y = std::max(canvas->last_click_map_y, mouse_map_y) + 1;

					int last_click_start_sx = last_click_start_map_x * rme::TileSize - view_scroll_x - adjustment;
					int last_click_start_sy = last_click_start_map_y * rme::TileSize - view_scroll_y - adjustment;
					int last_click_end_sx = last_click_end_map_x * rme::TileSize - view_scroll_x - adjustment;
					int last_click_end_sy = last_click_end_map_y * rme::TileSize - view_scroll_y - adjustment;

					uint8_t cr = 0;
					uint8_t cg = 0;
					uint8_t cb = 0;
					uint8_t ca = 0;
					getBrushColor(brushColor, cr, cg, cb, ca);
					renderer->drawColoredQuad(last_click_start_sx, last_click_start_sy, last_click_end_sx - last_click_start_sx, last_click_end_sy - last_click_start_sy, { cr, cg, cb, ca });
				}
			} else if (g_gui.GetBrushShape() == BRUSHSHAPE_CIRCLE) {
				// Calculate drawing offsets
				int start_x, end_x;
				int start_y, end_y;
				int width = std::max(
					std::abs(std::max(mouse_map_y, canvas->last_click_map_y) - std::min(mouse_map_y, canvas->last_click_map_y)),
					std::abs(std::max(mouse_map_x, canvas->last_click_map_x) - std::min(mouse_map_x, canvas->last_click_map_x))
				);

				if (mouse_map_x < canvas->last_click_map_x) {
					start_x = canvas->last_click_map_x - width;
					end_x = canvas->last_click_map_x;
				} else {
					start_x = canvas->last_click_map_x;
					end_x = canvas->last_click_map_x + width;
				}

				if (mouse_map_y < canvas->last_click_map_y) {
					start_y = canvas->last_click_map_y - width;
					end_y = canvas->last_click_map_y;
				} else {
					start_y = canvas->last_click_map_y;
					end_y = canvas->last_click_map_y + width;
				}

				int center_x = start_x + (end_x - start_x) / 2;
				int center_y = start_y + (end_y - start_y) / 2;
				float radii = width / 2.0f + 0.005f;

				RAWBrush* raw_brush = nullptr;
				if (brush->isRaw()) {
					raw_brush = brush->asRaw();
				}

				for (int y = start_y - 1; y <= end_y + 1; y++) {
					int cy = y * rme::TileSize - view_scroll_y - adjustment;
					float dy = center_y - y;
					for (int x = start_x - 1; x <= end_x + 1; x++) {
						int cx = x * rme::TileSize - view_scroll_x - adjustment;

						float dx = center_x - x;
						// printf("%f;%f\n", dx, dy);
						float distance = sqrt(dx * dx + dy * dy);
						if (distance < radii) {
							if (brush->isRaw()) {
								BlitSpriteType(cx, cy, raw_brush->getItemType()->sprite, 160, 160, 160, 160);
							} else {
								uint8_t cr = 0;
								uint8_t cg = 0;
								uint8_t cb = 0;
								uint8_t ca = 0;
								getBrushColor(brushColor, cr, cg, cb, ca);
								renderer->drawColoredQuad(cx, cy, rme::TileSize, rme::TileSize, { cr, cg, cb, ca });
							}
						}
					}
				}
			}
		}
	} else {
		if (brush->isWall()) {
			int start_map_x = mouse_map_x - g_gui.GetBrushSize();
			int start_map_y = mouse_map_y - g_gui.GetBrushSize();
			int end_map_x = mouse_map_x + g_gui.GetBrushSize() + 1;
			int end_map_y = mouse_map_y + g_gui.GetBrushSize() + 1;

			int start_sx = start_map_x * rme::TileSize - view_scroll_x - adjustment;
			int start_sy = start_map_y * rme::TileSize - view_scroll_y - adjustment;
			int end_sx = end_map_x * rme::TileSize - view_scroll_x - adjustment;
			int end_sy = end_map_y * rme::TileSize - view_scroll_y - adjustment;

			int delta_x = end_sx - start_sx;
			int delta_y = end_sy - start_sy;

			uint8_t cr = 0;
			uint8_t cg = 0;
			uint8_t cb = 0;
			uint8_t ca = 0;
			getBrushColor(brushColor, cr, cg, cb, ca);

			GLColor brushClr = { cr, cg, cb, ca };
			renderer->drawColoredQuad(start_sx, start_sy, delta_x, rme::TileSize, brushClr);

			if (delta_y > rme::TileSize) {
				renderer->drawColoredQuad(start_sx, start_sy + rme::TileSize, rme::TileSize, delta_y - 2 * rme::TileSize, brushClr);
			}

			if (delta_x > rme::TileSize && delta_y > rme::TileSize) {
				renderer->drawColoredQuad(end_sx - rme::TileSize, start_sy + rme::TileSize, rme::TileSize, delta_y - 2 * rme::TileSize, brushClr);
			}

			if (delta_y > rme::TileSize) {
				renderer->drawColoredQuad(start_sx, end_sy - rme::TileSize, delta_x, rme::TileSize, brushClr);
			}
		} else if (brush->isDoor()) {
			int cx = (mouse_map_x)*rme::TileSize - view_scroll_x - adjustment;
			int cy = (mouse_map_y)*rme::TileSize - view_scroll_y - adjustment;

			uint8_t cr = 0;
			uint8_t cg = 0;
			uint8_t cb = 0;
			uint8_t ca = 0;
			getCheckColor(brush, Position(mouse_map_x, mouse_map_y, floor), cr, cg, cb, ca);
			renderer->drawColoredQuad(cx, cy, rme::TileSize, rme::TileSize, { cr, cg, cb, ca });
		} else if (brush->isMonster()) {
			int cy = (mouse_map_y)*rme::TileSize - view_scroll_y - adjustment;
			int cx = (mouse_map_x)*rme::TileSize - view_scroll_x - adjustment;
			MonsterBrush* monster_brush = brush->asMonster();
			if (monster_brush->canDraw(&editor.getMap(), Position(mouse_map_x, mouse_map_y, floor))) {
				BlitCreature(cx, cy, monster_brush->getType()->outfit, SOUTH, 255, 255, 255, 160);
			} else {
				BlitCreature(cx, cy, monster_brush->getType()->outfit, SOUTH, 255, 64, 64, 160);
			}
		} else if (brush->isNpc()) {
			int cy = (mouse_map_y)*rme::TileSize - view_scroll_y - getFloorAdjustment(floor);
			int cx = (mouse_map_x)*rme::TileSize - view_scroll_x - getFloorAdjustment(floor);
			NpcBrush* npcBrush = brush->asNpc();
			if (npcBrush->canDraw(&editor.getMap(), Position(mouse_map_x, mouse_map_y, floor))) {
				BlitCreature(cx, cy, npcBrush->getType()->outfit, SOUTH, 255, 255, 255, 160);
			} else {
				BlitCreature(cx, cy, npcBrush->getType()->outfit, SOUTH, 255, 64, 64, 160);
			}
		} else if (!brush->isDoodad()) {
			RAWBrush* raw_brush = nullptr;
			if (brush->isRaw()) { // Textured brush
				raw_brush = brush->asRaw();
			}

			for (int y = -g_gui.GetBrushSize() - 1; y <= g_gui.GetBrushSize() + 1; y++) {
				int cy = (mouse_map_y + y) * rme::TileSize - view_scroll_y - adjustment;
				for (int x = -g_gui.GetBrushSize() - 1; x <= g_gui.GetBrushSize() + 1; x++) {
					int cx = (mouse_map_x + x) * rme::TileSize - view_scroll_x - adjustment;
					if (g_gui.GetBrushShape() == BRUSHSHAPE_SQUARE) {
						if (x >= -g_gui.GetBrushSize() && x <= g_gui.GetBrushSize() && y >= -g_gui.GetBrushSize() && y <= g_gui.GetBrushSize()) {
							if (brush->isRaw()) {
								BlitSpriteType(cx, cy, raw_brush->getItemType()->sprite, 160, 160, 160, 160);
							} else {
								if (brush->isWaypoint()) {
									uint8_t r, g, b;
									getColor(brush, Position(mouse_map_x + x, mouse_map_y + y, floor), r, g, b);
									DrawBrushIndicator(cx, cy, brush, r, g, b);
								} else {
									uint8_t cr = 0;
									uint8_t cg = 0;
									uint8_t cb = 0;
									uint8_t ca = 0;
									if (brush->isHouseExit() || brush->isOptionalBorder()) {
										getCheckColor(brush, Position(mouse_map_x + x, mouse_map_y + y, floor), cr, cg, cb, ca);
									} else {
										getBrushColor(brushColor, cr, cg, cb, ca);
									}
									renderer->drawColoredQuad(cx, cy, rme::TileSize, rme::TileSize, { cr, cg, cb, ca });
								}
							}
						}
					} else if (g_gui.GetBrushShape() == BRUSHSHAPE_CIRCLE) {
						double distance = sqrt(double(x * x) + double(y * y));
						if (distance < g_gui.GetBrushSize() + 0.005) {
							if (brush->isRaw()) {
								BlitSpriteType(cx, cy, raw_brush->getItemType()->sprite, 160, 160, 160, 160);
							} else {
								if (brush->isWaypoint()) {
									uint8_t r, g, b;
									getColor(brush, Position(mouse_map_x + x, mouse_map_y + y, floor), r, g, b);
									DrawBrushIndicator(cx, cy, brush, r, g, b);
								} else {
									uint8_t cr = 0;
									uint8_t cg = 0;
									uint8_t cb = 0;
									uint8_t ca = 0;
									if (brush->isHouseExit() || brush->isOptionalBorder()) {
										getCheckColor(brush, Position(mouse_map_x + x, mouse_map_y + y, floor), cr, cg, cb, ca);
									} else {
										getBrushColor(brushColor, cr, cg, cb, ca);
									}
									renderer->drawColoredQuad(cx, cy, rme::TileSize, rme::TileSize, { cr, cg, cb, ca });
								}
							}
						}
					}
				}
			}
		}
	}
}

void MapDrawer::BlitItem(int &draw_x, int &draw_y, const Tile* tile, const Item* item, bool ephemeral, int red, int green, int blue, int alpha) {
	const ItemType &type = g_items.getItemType(item->getID());
	if (type.id == 0) {
		glBlitSquare(draw_x, draw_y, *wxRED);
		return;
	}

	if (!options.ingame && !ephemeral && item->isSelected()) {
		red /= 2;
		blue /= 2;
		green /= 2;
	}

	// Ugly hacks. :)
	if (type.id == ITEM_STAIRS && !options.ingame) {
		glBlitSquare(draw_x, draw_y, red, green, 0, alpha / 3 * 2);
		return;
	} else if ((type.id == ITEM_NOTHING_SPECIAL || type.id == 2187) && !options.ingame) {
		glBlitSquare(draw_x, draw_y, red, 0, 0, alpha / 3 * 2);
		return;
	}

	if (type.isMetaItem()) {
		return;
	}
	if (!ephemeral && type.pickupable && !options.show_items) {
		return;
	}
	GameSprite* sprite = type.sprite;
	if (!sprite) {
		return;
	}

	int screenx = draw_x - sprite->getDrawOffset().x;
	int screeny = draw_y - sprite->getDrawOffset().y;

	const Position &pos = tile->getPosition();

	// Set the newd drawing height accordingly
	draw_x -= sprite->getDrawHeight();
	draw_y -= sprite->getDrawHeight();

	int subtype = -1;

	int pattern_x = pos.x % sprite->pattern_x;
	int pattern_y = pos.y % sprite->pattern_y;
	int pattern_z = pos.z % sprite->pattern_z;

	if (type.isSplash() || type.isFluidContainer()) {
		subtype = Item::liquidSubTypeToSpriteSubType(item->getSubtype());
	} else if (type.isHangable) {
		if (tile->hasProperty(HOOK_SOUTH)) {
			pattern_x = 1;
		} else if (tile->hasProperty(HOOK_EAST)) {
			pattern_x = 2;
		} else {
			pattern_x = 0;
		}
	} else if (type.stackable) {
		if (item->getSubtype() <= 1) {
			subtype = 0;
		} else if (item->getSubtype() <= 2) {
			subtype = 1;
		} else if (item->getSubtype() <= 3) {
			subtype = 2;
		} else if (item->getSubtype() <= 4) {
			subtype = 3;
		} else if (item->getSubtype() < 10) {
			subtype = 4;
		} else if (item->getSubtype() < 25) {
			subtype = 5;
		} else if (item->getSubtype() < 50) {
			subtype = 6;
		} else {
			subtype = 7;
		}
	}

	if (!ephemeral && options.transparent_items && (!type.isGroundTile() || sprite->getWidth() > 1 || sprite->getHeight() > 1) && !type.isSplash() && (!type.isBorder || sprite->getWidth() > 1 || sprite->getHeight() > 1)) {
		alpha /= 2;
	}

	int frame = item->getFrame();
	int texnum = sprite->getHardwareID(0, subtype, pattern_x, pattern_y, pattern_z, frame);
	int sprId = sprite->getSpriteID(0, subtype, pattern_x, pattern_y, pattern_z, frame);
	glBlitTexture(screenx, screeny, texnum, red, green, blue, alpha, false, false, {}, sprId);

	if (options.show_hooks && (type.hookSouth || type.hookEast || type.hook != ITEM_HOOK_NONE)) {
		DrawHookIndicator(draw_x, draw_y, type);
	}

	if (!options.ingame && options.show_light_strength) {
		DrawLightStrength(draw_x, draw_y, item);
	}
}

void MapDrawer::BlitItem(int &draw_x, int &draw_y, const Position &pos, const Item* item, bool ephemeral, int red, int green, int blue, int alpha) {
	const ItemType &type = g_items.getItemType(item->getID());
	if (type.id == 0) {
		return;
	}

	if (!options.ingame && !ephemeral && item->isSelected()) {
		red /= 2;
		blue /= 2;
		green /= 2;
	}

	if (type.id == ITEM_STAIRS && !options.ingame) { // Ugly hack yes?
		glBlitSquare(draw_x, draw_y, red, green, 0, alpha / 3 * 2);
		return;
	} else if ((type.id == ITEM_NOTHING_SPECIAL || type.id == 2187) && !options.ingame) { // Ugly hack yes? // Beautiful!
		glBlitSquare(draw_x, draw_y, red, 0, 0, alpha / 3 * 2);
		return;
	}

	if (type.isMetaItem()) {
		return;
	}
	if (!ephemeral && type.pickupable && options.show_items) {
		return;
	}
	GameSprite* sprite = type.sprite;
	if (!sprite) {
		return;
	}

	int screenx = draw_x - sprite->getDrawOffset().x;
	int screeny = draw_y - sprite->getDrawOffset().y;

	// Set the newd drawing height accordingly
	draw_x -= sprite->getDrawHeight();
	draw_y -= sprite->getDrawHeight();

	int subtype = -1;

	int pattern_x = pos.x % sprite->pattern_x;
	int pattern_y = pos.y % sprite->pattern_y;
	int pattern_z = pos.z % sprite->pattern_z;

	if (type.isSplash() || type.isFluidContainer()) {
		subtype = item->getSubtype();
	} else if (type.isHangable) {
		pattern_x = 0;
		/*
		if(tile->hasProperty(HOOK_SOUTH)) {
			pattern_x = 2;
		} else if(tile->hasProperty(HOOK_EAST)) {
			pattern_x = 1;
		} else {
			pattern_x = -0;
		}
		*/
	} else if (type.stackable) {
		if (item->getSubtype() <= 1) {
			subtype = 0;
		} else if (item->getSubtype() <= 2) {
			subtype = 1;
		} else if (item->getSubtype() <= 3) {
			subtype = 2;
		} else if (item->getSubtype() <= 4) {
			subtype = 3;
		} else if (item->getSubtype() < 10) {
			subtype = 4;
		} else if (item->getSubtype() < 25) {
			subtype = 5;
		} else if (item->getSubtype() < 50) {
			subtype = 6;
		} else {
			subtype = 7;
		}
	}

	if (!ephemeral && options.transparent_items && (!type.isGroundTile() || sprite->getWidth() > 1 || sprite->getHeight() > 1) && !type.isSplash() && (!type.isBorder || sprite->getWidth() > 1 || sprite->getHeight() > 1)) {
		alpha /= 2;
	}

	int frame = item->getFrame();
	int texnum = sprite->getHardwareID(0, subtype, pattern_x, pattern_y, pattern_z, frame);
	int sprId = sprite->getSpriteID(0, subtype, pattern_x, pattern_y, pattern_z, frame);
	glBlitTexture(screenx, screeny, texnum, red, green, blue, alpha, false, false, {}, sprId);

	if (options.show_hooks && (type.hookSouth || type.hookEast) && zoom <= 3.0) {
		DrawHookIndicator(draw_x, draw_y, type);
	}

	if (!options.ingame && options.show_light_strength) {
		DrawLightStrength(draw_x, draw_y, item);
	}
}

void MapDrawer::BlitSpriteType(int screenx, int screeny, uint32_t spriteid, int red, int green, int blue, int alpha) {
	const ItemType &type = g_items.getItemType(spriteid);
	if (type.id == 0) {
		return;
	}

	GameSprite* sprite = type.sprite;
	if (!sprite) {
		return;
	}

	screenx -= sprite->getDrawOffset().x;
	screeny -= sprite->getDrawOffset().y;

	int frame = 0;
	int texnum = sprite->getHardwareID(0, -1, 0, 0, 0, 0);
	int sprId = sprite->getSpriteID(0, -1, 0, 0, 0, 0);
	glBlitTexture(screenx, screeny, texnum, red, green, blue, alpha, false, false, {}, sprId);
}

void MapDrawer::BlitSpriteType(int screenx, int screeny, GameSprite* sprite, int red, int green, int blue, int alpha) {
	if (!sprite) {
		return;
	}

	screenx -= sprite->getDrawOffset().x;
	screeny -= sprite->getDrawOffset().y;

	int frame = 0;
	int texnum = sprite->getHardwareID(0, -1, 0, 0, 0, 0);
	int sprId = sprite->getSpriteID(0, -1, 0, 0, 0, 0);
	glBlitTexture(screenx, screeny, texnum, red, green, blue, alpha, false, false, {}, sprId);
}

void MapDrawer::BlitCreature(int screenx, int screeny, const Outfit &outfit, const Direction &dir, int red, int green, int blue, int alpha) {
	if (outfit.lookItem != 0) {
		const ItemType &type = g_items.getItemType(outfit.lookItem);
		BlitSpriteType(screenx, screeny, type.sprite, red, green, blue, alpha);
		return;
	}

	if (outfit.lookType == 0) {
		return;
	}

	GameSprite* spr = g_gui.gfx.getCreatureSprite(outfit.lookType);
	if (!spr || outfit.lookType == 0) {
		return;
	}

	screenx -= spr->getDrawOffset().x;
	screeny -= spr->getDrawOffset().y;

	int baseIdx = (int)dir * (int)spr->layers;
	if (baseIdx < 0 || (uint32_t)baseIdx >= spr->numsprites) {
		return;
	}
	auto spriteId = spr->spriteList[baseIdx]->id;
	auto outfitImage = spr->getOutfitImage(spriteId, baseIdx, outfit);
	if (outfitImage) {
		glBlitTexture(screenx, screeny, outfitImage->getHardwareID(), red, green, blue, alpha, false, false, outfit, spriteId);
	}

	for (int py = 1; py < (int)spr->pattern_y; ++py) {
		if (!(outfit.lookAddon & (1 << (py - 1)))) {
			continue;
		}
		int addonIdx = (py * (int)spr->pattern_x + (int)dir) * (int)spr->layers;
		if (addonIdx < 0 || (uint32_t)addonIdx >= spr->numsprites) {
			continue;
		}
		auto addonSpriteId = spr->spriteList[addonIdx]->id;
		auto addonImage = spr->getOutfitImage(addonSpriteId, addonIdx, outfit);
		if (addonImage) {
			glBlitTexture(screenx, screeny, addonImage->getHardwareID(), red, green, blue, alpha, false, false, outfit, addonSpriteId);
		}
	}
}

void MapDrawer::BlitCreature(int screenx, int screeny, const Monster* creature, int red, int green, int blue, int alpha) {
	if (!options.ingame && creature->isSelected()) {
		red /= 2;
		green /= 2;
		blue /= 2;
	}
	BlitCreature(screenx, screeny, creature->getLookType(), creature->getDirection(), red, green, blue, alpha);
}
// Npcs
void MapDrawer::BlitCreature(int screenx, int screeny, const Npc* npc, int red, int green, int blue, int alpha) {
	if (!options.ingame && npc->isSelected()) {
		red /= 2;
		green /= 2;
		blue /= 2;
	}
	BlitCreature(screenx, screeny, npc->getLookType(), npc->getDirection(), red, green, blue, alpha);
}

void MapDrawer::WriteTooltip(const Item* item, MapTooltip &tooltip) {
	if (!item) {
		return;
	}

	const uint16_t id = item->getID();
	if (id < 100) {
		return;
	}

	const uint16_t unique = item->getUniqueID();
	const uint16_t action = item->getActionID();
	const std::string &text = item->getText();
	if (unique == 0 && action == 0 && text.empty()) {
		return;
	}

	tooltip.addEntry("id: ", std::to_string(id));

	if (action > 0) {
		tooltip.addEntry("aid: ", std::to_string(action));
	}
	if (unique > 0) {
		tooltip.addEntry("uid: ", std::to_string(unique));
	}
	if (!text.empty()) {
		tooltip.addEntry("text: ", text);
	}
}

void MapDrawer::WriteTooltip(const Waypoint* waypoint, MapTooltip &tooltip) {
	tooltip.addEntry("wp: ", waypoint->name);
}

void MapDrawer::DrawTile(TileLocation* location) {
	if (!location) {
		return;
	}

	Tile* tile = location->get();
	if (!tile) {
		return;
	}

	if (options.show_only_modified && !tile->isModified()) {
		return;
	}

	const Position &position = location->getPosition();
	bool show_tooltips = options.isTooltips();

	bool has_waypoint = false;
	Waypoint* waypoint = nullptr;
	if (show_tooltips && location->getWaypointCount() > 0) {
		waypoint = canvas->editor.getMap().waypoints.getWaypoint(position);
		has_waypoint = (waypoint != nullptr);
	}

	bool only_colors = options.isOnlyColors();

	int draw_x, draw_y;
	getDrawPosition(position, draw_x, draw_y);

	uint8_t r = 255, g = 255, b = 255;
	if (only_colors || tile->hasGround()) {

		if (!options.show_as_minimap) {
			bool showspecial = options.show_only_colors || options.show_special_tiles;

			if (options.show_blocking && tile->isBlocking() && tile->size() > 0) {
				g = g / 3 * 2;
				b = b / 3 * 2;
			}

			int item_count = tile->items.size();
			if (options.highlight_items && item_count > 0 && !tile->items.back()->isBorder()) {
				static const float factor[5] = { 0.75f, 0.6f, 0.48f, 0.40f, 0.33f };
				int idx = (item_count < 5 ? item_count : 5) - 1;
				g = int(g * factor[idx]);
				r = int(r * factor[idx]);
			}

			if (options.show_spawns_monster && location->getSpawnMonsterCount() > 0) {
				float f = 1.0f;
				for (uint32_t i = 0; i < location->getSpawnMonsterCount(); ++i) {
					f *= 0.7f;
				}
				g = uint8_t(g * f);
				b = uint8_t(b * f);
			}

			if (options.show_spawns_npc && location->getSpawnNpcCount() > 0) {
				float f = 1.0f;
				for (uint32_t i = 0; i < location->getSpawnNpcCount(); ++i) {
					f *= 0.7f;
				}
				g = uint8_t(g * f);
				b = uint8_t(b * f);
			}

			if (options.show_houses && tile->isHouseTile()) {
				if ((int)tile->getHouseID() == current_house_id) {
					r /= 2;
				} else {
					r /= 2;
					g /= 2;
				}
			} else if (showspecial && tile->isPZ()) {
				r /= 2;
				b /= 2;
			}

			if (showspecial && tile->getMapFlags() & TILESTATE_PVPZONE) {
				g = r / 4;
				b = b / 3 * 2;
			}

			bool zone_active = tile->hasZone(g_gui.zone_brush->getZone());
			if (showspecial && zone_active) {
				b /= 1.3;
				r /= 1.5;
				g /= 2;
			}
			if (showspecial && ((!tile->zones.empty() && !zone_active) || tile->zones.size() > 1)) {
				r /= 1.4;
				g /= 1.6;
				b /= 1.3;
			}

			if (showspecial && tile->getMapFlags() & TILESTATE_NOLOGOUT) {
				b /= 2;
			}

			if (showspecial && tile->getMapFlags() & TILESTATE_NOPVP) {
				g /= 2;
			}
		}

		if (only_colors) {
			if (options.show_as_minimap) {
				wxColor color = colorFromEightBit(tile->getMiniMapColor());
				glBlitSquare(draw_x, draw_y, color);
			} else if (r != 255 || g != 255 || b != 255) {
				glBlitSquare(draw_x, draw_y, r, g, b, 128);
			}
		} else {
			if (options.show_preview && zoom <= 2.0) {
				tile->ground->animate();
			}

			BlitItem(draw_x, draw_y, tile, tile->ground, false, r, g, b);
		}
	}

	bool hidden = only_colors || (options.hide_items_when_zoomed && zoom > 10.f);

	if (!hidden && !tile->items.empty()) {
		for (Item* item : tile->items) {

			if (options.show_preview && zoom <= 2.0) {
				item->animate();
			}

			if (item->isBorder()) {
				BlitItem(draw_x, draw_y, tile, item, false, r, g, b);
			} else {
				BlitItem(draw_x, draw_y, tile, item);
			}
		}
	}

	if (!hidden && options.show_monsters && !tile->monsters.empty()) {
		for (auto monster : tile->monsters) {
			BlitCreature(draw_x, draw_y, monster);
		}
	}

	if (!hidden && options.show_npcs && tile->npc) {
		BlitCreature(draw_x, draw_y, tile->npc);
	}

	if (show_tooltips && position.z == floor) {
		uint8_t tr = 255;
		uint8_t tg = 255;
		uint8_t tb = 255;
		if (has_waypoint) {
			tr = 0;
			tg = 255;
			tb = 0;
		}
		auto &tip = MakeTooltip(position.x, position.y, position.z, tr, tg, tb);

		if (has_waypoint) {
			WriteTooltip(waypoint, tip);
		}

		if (tile->hasGround()) {
			WriteTooltip(tile->ground, tip);
		}

		if (!tile->items.empty()) {
			for (Item* item : tile->items) {
				WriteTooltip(item, tip);
			}
		}

		if (tip.entries.empty()) {
			tooltips.pop_back();
		}
	}
}

void MapDrawer::DrawBrushIndicator(int x, int y, [[maybe_unused]] Brush* brush, uint8_t r, uint8_t g, uint8_t b) {
	x += (rme::TileSize / 2);
	y += (rme::TileSize / 2);

	// 7----0----1
	// |         |
	// 6--5  3--2
	//     \/
	//     4
	static int vertexes[9][2] = {
		{ -15, -20 }, // 0
		{ 15, -20 }, // 1
		{ 15, -5 }, // 2
		{ 5, -5 }, // 3
		{ 0, 0 }, // 4
		{ -5, -5 }, // 5
		{ -15, -5 }, // 6
		{ -15, -20 }, // 7
		{ -15, -20 }, // 0
	};

	// circle
	std::array<float, 64> fan; // (1 center + 31 rim) * 2 floats
	fan[0] = static_cast<float>(x);
	fan[1] = static_cast<float>(y);
	for (int i = 0; i <= 30; i++) {
		float angle = i * 2.0f * rme::PI / 30;
		fan[(i + 1) * 2] = cos(angle) * (rme::TileSize / 2) + x;
		fan[(i + 1) * 2 + 1] = sin(angle) * (rme::TileSize / 2) + y;
	}
	renderer->drawTriangleFan(fan.data(), 32, 0x00, 0x00, 0x00, 0x50);

	// background
	std::array<float, 16> poly;
	for (int i = 0; i < 8; ++i) {
		poly[i * 2] = static_cast<float>(vertexes[i][0] + x);
		poly[i * 2 + 1] = static_cast<float>(vertexes[i][1] + y);
	}
	renderer->drawPolygon(poly.data(), 8, r, g, b, 0xB4);

	// borders
	std::array<float, 32> lineVerts; // 8 pairs * 4 floats
	for (int i = 0; i < 8; ++i) {
		lineVerts[i * 4] = static_cast<float>(vertexes[i][0] + x);
		lineVerts[i * 4 + 1] = static_cast<float>(vertexes[i][1] + y);
		lineVerts[i * 4 + 2] = static_cast<float>(vertexes[i + 1][0] + x);
		lineVerts[i * 4 + 3] = static_cast<float>(vertexes[i + 1][1] + y);
	}
	renderer->drawLines(lineVerts.data(), 8, 0x00, 0x00, 0x00, 0xB4, 1.0f);
}

void MapDrawer::DrawHookIndicator(int x, int y, const ItemType &type) {
	if (type.hookSouth || type.hook == ITEM_HOOK_SOUTH) {
		x -= 10;
		y += 10;
		std::array<float, 8> verts = { (float)x, (float)y, (float)(x + 10), (float)y, (float)(x + 20), (float)(y + 10), (float)(x + 10), (float)(y + 10) };
		renderer->drawPolygon(verts.data(), 4, 0, 0, 255, 200);
	} else if (type.hookEast || type.hook == ITEM_HOOK_EAST) {
		x += 10;
		y -= 10;
		std::array<float, 8> verts = { (float)x, (float)y, (float)(x + 10), (float)(y + 10), (float)(x + 10), (float)(y + 20), (float)x, (float)(y + 10) };
		renderer->drawPolygon(verts.data(), 4, 0, 0, 255, 200);
	}
}

void MapDrawer::DrawLightStrength(int x, int y, const Item*&item) {
	const SpriteLight &light = item->getLight();

	if (light.intensity <= 0) {
		return;
	}

	wxColor lightColor = colorFromEightBit(light.color);
	const uint8_t byteR = lightColor.Red();
	const uint8_t byteG = lightColor.Green();
	const uint8_t byteB = lightColor.Blue();
	constexpr uint8_t byteA = 255;

	const int startOffset = std::max<int>(16, 32 - light.intensity);
	const int sqSize = rme::TileSize - startOffset;
	glBlitSquare(x + startOffset - 2, y + startOffset - 2, 0, 0, 0, byteA, sqSize + 2);
	glBlitSquare(x + startOffset - 1, y + startOffset - 1, byteR, byteG, byteB, byteA, sqSize);
}

void MapDrawer::DrawTileIndicators(TileLocation* location) {
	if (!location) {
		return;
	}

	Tile* tile = location->get();
	if (!tile) {
		return;
	}

	int x, y;
	getDrawPosition(location->getPosition(), x, y);

	if (zoom < 10.0 && (options.show_pickupables || options.show_moveables || options.show_avoidables)) {
		uint8_t red = 0xFF, green = 0xFF, blue = 0xFF;
		if (tile->isHouseTile()) {
			green = 0x00;
			blue = 0x00;
		}

		for (const Item* item : tile->items) {
			const ItemType &type = g_items.getItemType(item->getID());
			if ((type.pickupable && options.show_pickupables) || (type.moveable && options.show_moveables)) {
				if (type.pickupable && options.show_pickupables && type.moveable && options.show_moveables) {
					DrawIndicator(x, y, EDITOR_SPRITE_PICKUPABLE_MOVEABLE_ITEM, red, green, blue);
				} else if (type.pickupable && options.show_pickupables) {
					DrawIndicator(x, y, EDITOR_SPRITE_PICKUPABLE_ITEM, red, green, blue);
				} else if (type.moveable && options.show_moveables) {
					DrawIndicator(x, y, EDITOR_SPRITE_MOVEABLE_ITEM, red, green, blue);
				}
			}

			if (type.blockPathfinder && options.show_avoidables) {
				DrawIndicator(x, y, EDITOR_SPRITE_AVOIDABLE_ITEM, red, green, blue);
			}
		}
	}

	if (options.show_avoidables && tile->ground && tile->ground->isAvoidable()) {
		DrawIndicator(x, y, EDITOR_SPRITE_AVOIDABLE_ITEM);
	}

	if (options.show_houses && tile->isHouseExit()) {
		if (tile->hasHouseExit(current_house_id)) {
			DrawIndicator(x, y, EDITOR_SPRITE_HOUSE_EXIT);
		} else {
			DrawIndicator(x, y, EDITOR_SPRITE_HOUSE_EXIT, 64, 64, 255, 128);
		}
	}

	if (options.show_spawns_monster && tile->spawnMonster) {
		if (tile->spawnMonster->isSelected()) {
			DrawIndicator(x, y, EDITOR_SPRITE_MONSTERS, 128, 128, 128);
		} else {
			DrawIndicator(x, y, EDITOR_SPRITE_MONSTERS);
		}
	}

	if (tile->spawnNpc && options.show_spawns_npc) {
		if (tile->spawnNpc->isSelected()) {
			DrawIndicator(x, y, EDITOR_SPRITE_NPCS, 128, 128, 128);
		} else {
			DrawIndicator(x, y, EDITOR_SPRITE_NPCS, 255, 255, 255);
		}
	}
}

void MapDrawer::DrawIndicator(int x, int y, int indicator, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	GameSprite* sprite = g_gui.gfx.getEditorSprite(indicator);
	if (sprite == nullptr) {
		spdlog::error("MapDrawer::DrawIndicator: sprite is nullptr");
		return;
	}

	int textureId = sprite->getHardwareID(0, 0, 0, -1, 0, 0);
	glBlitTexture(x, y, textureId, r, g, b, a, true, true);
}

void MapDrawer::DrawPositionIndicator(int z) {
	if (z != pos_indicator.z || pos_indicator.x < start_x || pos_indicator.x > end_x || pos_indicator.y < start_y || pos_indicator.y > end_y) {
		return;
	}

	const long time = GetPositionIndicatorTime();
	if (time == 0) {
		return;
	}

	int x, y;
	getDrawPosition(pos_indicator, x, y);

	int size = static_cast<int>(rme::TileSize * (0.3f + std::abs(500 - time % 1000) / 1000.f));
	int offset = (rme::TileSize - size) / 2;

	drawRect(x + offset + 2, y + offset + 2, size - 4, size - 4, *wxWHITE, 2);
	drawRect(x + offset + 1, y + offset + 1, size - 2, size - 2, *wxBLACK, 2);
}

std::pair<float, float> MapDrawer::MeasureTooltipText(const MapTooltip &tp) {
	float lineH = renderer->getLineHeight();
	float maxWidth = 0.0f;
	int totalLines = 0;

	auto getLineWidth = [this](const std::string &s) {
		float w = 0.0f;
		for (char c : s) {
			if (!iscntrl(c)) {
				w += renderer->getCharWidth(c);
			}
		}
		return w;
	};

	for (const auto &entry : tp.entries) {
		float labelW = getLineWidth(entry.label);

		std::string val = entry.value;

		size_t pos = 0;
		while (pos < val.size()) {
			size_t next_nl = val.find('\n', pos);
			std::string line = val.substr(pos, (next_nl == std::string::npos ? std::string::npos : next_nl - pos));

			float valW = getLineWidth(line);

			maxWidth = std::max(maxWidth, labelW + valW);
			totalLines++;

			if (next_nl == std::string::npos) {
				break;
			}
			pos = next_nl + 1;
		}

		if (val.empty()) {
			maxWidth = std::max(maxWidth, labelW);
			totalLines++;
		}
	}

	float width = maxWidth + 8.0f;
	float height = static_cast<float>(totalLines) * lineH + 4.0f;
	return { width, height };
}

void MapDrawer::RenderTooltipText(const MapTooltip &tp, float startx, float starty, float fade) {
	float lineH = renderer->getLineHeight();
	float x = startx + 4.0f;
	float y = starty + renderer->getAscent() + 2.0f;

	float lum = 0.299f * tp.r + 0.587f * tp.g + 0.114f * tp.b;
	uint8_t valR;
	uint8_t valG;
	uint8_t valB;
	uint8_t lblR;
	uint8_t lblG;
	uint8_t lblB;
	if (lum > 128.0f) {
		valR = valG = valB = 0;
		lblR = lblG = lblB = 100;
	} else {
		valR = valG = valB = 255;
		lblR = lblG = lblB = 180;
	}
	auto fa = static_cast<uint8_t>(fade * 255);

	auto drawString = [this](const std::string &s) {
		for (char c : s) {
			if (!iscntrl(c)) {
				renderer->drawBitmapChar(c);
			}
		}
	};

	for (const auto &entry : tp.entries) {
		renderer->setRasterPos(x, y);

		float labelW = 0.0f;
		renderer->setColor(lblR, lblG, lblB, fa);
		for (char c : entry.label) {
			labelW += renderer->getCharWidth(c);
			renderer->drawBitmapChar(c);
		}

		renderer->setColor(valR, valG, valB, fa);
		std::string val = entry.value;
		size_t pos = 0;
		bool firstLine = true;
		while (pos < val.size()) {
			size_t next_nl = val.find('\n', pos);
			std::string line = val.substr(pos, (next_nl == std::string::npos ? std::string::npos : next_nl - pos));

			if (!firstLine) {
				y += lineH;
				renderer->setRasterPos(x + labelW, y);
			}

			drawString(line);

			firstLine = false;
			if (next_nl == std::string::npos) {
				break;
			}
			pos = next_nl + 1;
		}

		y += lineH;
	}
}

static uint64_t tooltipKey(int x, int y, int z) {
	return (static_cast<uint64_t>(x) << 40) | (static_cast<uint64_t>(y) << 16) | static_cast<uint64_t>(z);
}

void MapDrawer::DrawTooltips() {
	float fadeSpeed = 0.02f;
	if (options.isTooltips()) {
		globalTooltipFade = std::min(globalTooltipFade + fadeSpeed, 1.0f);
	} else {
		globalTooltipFade = std::max(globalTooltipFade - fadeSpeed, 0.0f);
	}

	if (globalTooltipFade <= 0.0f || tooltips.empty()) {
		return;
	}

	renderer->flush();
	renderer->setOrtho(0, static_cast<float>(screensize_x), static_cast<float>(screensize_y), 0);

	for (const auto &tp : tooltips) {
		auto [width, height] = MeasureTooltipText(tp);

		int screen_x;
		int screen_y;
		getDrawPosition(Position(tp.map_x, tp.map_y, tp.map_z), screen_x, screen_y);

		float x = static_cast<float>(screen_x + rme::TileSize / 2) / zoom;
		float y = static_cast<float>(screen_y + rme::TileSize / 2) / zoom;
		float center = width / 2.0f;
		float space = 7.0f;
		float startx = x - center;
		float endx = x + center;
		float starty = y - (height + space);
		float endy = y - space;

		// drop shadow
		float radius = 4.0f;
		float shadowOff = 3.0f;
		auto shadowAlpha = static_cast<uint8_t>(globalTooltipFade * 70);
		renderer->drawRoundedRect(startx + shadowOff, starty + shadowOff, endx - startx, endy - starty, radius, { 0, 0, 0, shadowAlpha });
		std::array<float, 6> shadowArrow = { x + space + shadowOff, endy + shadowOff, x + shadowOff, y + shadowOff, x - space + shadowOff, endy + shadowOff };
		renderer->drawPolygon(shadowArrow.data(), 3, 0, 0, 0, shadowAlpha);

		// background (rounded rect body + arrow triangle)
		auto bgAlpha = static_cast<uint8_t>(globalTooltipFade * 200);
		renderer->drawRoundedRect(startx, starty, endx - startx, endy - starty, radius, { tp.r, tp.g, tp.b, bgAlpha });

		std::array<float, 6> arrow = { x + space, endy, x, y, x - space, endy };
		renderer->drawPolygon(arrow.data(), 3, tp.r, tp.g, tp.b, bgAlpha);

		// border (rounded rect outline + arrow lines)
		auto borderAlpha = static_cast<uint8_t>(globalTooltipFade * 180);
		renderer->drawRoundedRectOutline(startx, starty, endx - startx, endy - starty, radius, { 0, 0, 0, borderAlpha }, 1.0f);

		std::array<float, 16> arrowLines = {
			x + space,
			endy,
			x,
			y,
			x,
			y,
			x - space,
			endy,
		};
		renderer->drawLines(arrowLines.data(), 2, 0, 0, 0, borderAlpha, 1.0f);

		RenderTooltipText(tp, startx, starty, globalTooltipFade);
	}

	renderer->flush();

	std::array<int, 4> vPort {};
	glGetIntegerv(GL_VIEWPORT, vPort.data());
	renderer->setOrtho(0, vPort[2] * zoom, vPort[3] * zoom, 0);
}

void MapDrawer::UpdateRAMUsage() {
#ifdef __WINDOWS__
	PROCESS_MEMORY_COUNTERS pmc;
	if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
		current_ram = pmc.WorkingSetSize / (1024 * 1024);
	}
#else
	std::ifstream file("/proc/self/statm");
	if (file.is_open()) {
		uint32_t size = 0;
		uint32_t rss = 0;
		file >> size >> rss;
		current_ram = (rss * sysconf(_SC_PAGESIZE)) / (1024 * 1024);
	}
#endif
}

void MapDrawer::UpdateCPUUsage() {
#ifdef __WINDOWS__
	FILETIME ftime, fsys, fuser;
	ULARGE_INTEGER now, sys, user;

	GetSystemTimeAsFileTime(&ftime);
	memcpy(&now, &ftime, sizeof(FILETIME));

	GetProcessTimes(GetCurrentProcess(), &ftime, &ftime, &fsys, &fuser);
	memcpy(&sys, &fsys, sizeof(FILETIME));
	memcpy(&user, &fuser, sizeof(FILETIME));

	if (last_now_time.QuadPart != 0) {
		double process_diff = (double)((sys.QuadPart - last_sys_time.QuadPart) + (user.QuadPart - last_cpu_time.QuadPart));
		double system_diff = (double)(now.QuadPart - last_now_time.QuadPart);

		if (system_diff > 0) {
			current_cpu = (process_diff / system_diff) * 100.0;
			unsigned int num_cores = std::thread::hardware_concurrency();
			if (num_cores > 0) {
				current_cpu = current_cpu / num_cores;
			}
			if (current_cpu > 100.0) {
				current_cpu = 100.0;
			}
		}
	}

	last_cpu_time = user;
	last_sys_time = sys;
	last_now_time = now;
#else
	std::ifstream file("/proc/self/stat");
	if (!file.is_open()) {
		return;
	}

	std::string buffer;
	if (!std::getline(file, buffer)) {
		return;
	}

	size_t pos = buffer.find(')');
	if (pos == std::string::npos) {
		return;
	}

	uint64_t utime = 0;
	uint64_t stime = 0;
	std::istringstream iss(buffer.substr(pos + 2));
	std::string dummy;
	char state;
	int pid;
	int ppid;
	int pgrp;
	int session;
	int tty_nr;
	int tpgid;
	unsigned int flags;
	unsigned int minflt;
	unsigned int cminflt;
	unsigned int majflt;

	if (unsigned int cmajflt = 0; !(iss >> state >> pid >> ppid >> pgrp >> session >> tty_nr >> tpgid >> flags >> minflt >> cminflt >> majflt >> cmajflt >> utime >> stime)) {
		return;
	}

	uint64_t process_time = utime + stime;
	std::ifstream stat_file("/proc/stat");
	if (!stat_file.is_open()) {
		return;
	}

	uint64_t user = 0;
	uint64_t nice = 0;
	uint64_t system = 0;
	uint64_t idle = 0;
	uint64_t iowait = 0;
	uint64_t irq = 0;
	uint64_t softirq = 0;
	uint64_t steal = 0;

	std::string cpu_label;
	stat_file >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;

	if (cpu_label == "cpu") {
		uint64_t total_time = user + nice + system + idle + iowait + irq + softirq + steal;
		if (last_total_time != 0) {
			uint64_t total_diff = total_time - last_total_time;
			uint64_t process_diff = process_time - last_process_time;

			if (total_diff > 0) {
				current_cpu = (100.0 * process_diff) / total_diff;
			}
		}

		last_total_time = total_time;
		last_process_time = process_time;
	}
#endif
}

std::string MapDrawer::FormatPerformanceStats() const {
	return std::format("FPS: {:.1f} | CPU: {:.1f}% | RAM: {} MB", current_fps, current_cpu, current_ram);
}

void MapDrawer::DrawPerformanceStats() {
	frame_count++;
	long elapsed = perf_update_timer.Time();
	if (elapsed >= 500) {
		current_fps = (frame_count * 1000.0) / elapsed;
		frame_count = 0;
		UpdateRAMUsage();
		UpdateCPUUsage();
		perf_update_timer.Start();
	}

	std::string stats_text = FormatPerformanceStats();

	renderer->flush();
	renderer->setOrtho(0, static_cast<float>(screensize_x), static_cast<float>(screensize_y), 0);

	renderer->drawText(10.0f, 20.0f, stats_text, 255, 255, 0, 255);

	renderer->flush();

	std::array<int, 4> vPort {};
	glGetIntegerv(GL_VIEWPORT, vPort.data());
	renderer->setOrtho(0, vPort[2] * zoom, vPort[3] * zoom, 0);
}

void MapDrawer::DrawLight() const {
	// draw in-game light
	light_drawer->draw(start_x, start_y, end_x, end_y, view_scroll_x, view_scroll_y, renderer.get());
}

MapTooltip &MapDrawer::MakeTooltip(int map_x, int map_y, int map_z, uint8_t r, uint8_t g, uint8_t b) {
	tooltips.emplace_back(map_x, map_y, map_z, r, g, b);
	return tooltips.back();
}

void MapDrawer::AddLight(TileLocation* location) {
	if (!options.show_lights || !location) {
		return;
	}

	auto tile = location->get();
	if (!tile) {
		return;
	}

	auto &position = location->getPosition();

	if (tile->ground) {
		if (tile->ground->hasLight()) {
			light_drawer->addLight(position.x, position.y, position.z, tile->ground->getLight());
		}
	}

	bool hidden = options.hide_items_when_zoomed && zoom > 10.f;
	if (!hidden && !tile->items.empty()) {
		for (auto item : tile->items) {
			if (item->hasLight()) {
				light_drawer->addLight(position.x, position.y, position.z, item->getLight());
			}
		}
	}
}

void MapDrawer::getColor(Brush* brush, const Position &position, uint8_t &r, uint8_t &g, uint8_t &b) {
	if (brush->canDraw(&editor.getMap(), position)) {
		if (brush->isWaypoint()) {
			r = 0x00;
			g = 0xff, b = 0x00;
		} else {
			r = 0x00;
			g = 0x00, b = 0xff;
		}
	} else {
		r = 0xff;
		g = 0x00, b = 0x00;
	}
}

void MapDrawer::TakeScreenshot(uint8_t* screenshot_buffer) {
	glPixelStorei(GL_PACK_ALIGNMENT, 1); // 1 byte alignment

	for (int i = 0; i < screensize_y; ++i) {
		glReadPixels(0, screensize_y - i, screensize_x, 1, GL_RGB, GL_UNSIGNED_BYTE, (GLubyte*)(screenshot_buffer) + 3 * screensize_x * i);
	}
}

void MapDrawer::ShowPositionIndicator(const Position &position) {
	pos_indicator = position;
	pos_indicator_timer.Start();
}

void MapDrawer::glBlitTexture(int sx, int sy, int textureId, int red, int green, int blue, int alpha, bool adjustZoom, bool isEditorSprite, const Outfit &outfit, int spriteId) {
	if (textureId <= 0) {
		return;
	}

	auto width = rme::TileSize;
	auto height = rme::TileSize;
	// Adjusts the offset of normal sprites
	if (!isEditorSprite) {
		SpriteSheetPtr sheet = g_spriteAppearances.getSheetBySpriteId(spriteId > 0 ? spriteId : textureId);
		if (!sheet) {
			return;
		}

		width = sheet->getSpriteSize().width;
		height = sheet->getSpriteSize().height;

		// If the sprite is an outfit and the size is 64x64, adjust the offset
		if (width == 64 && height == 64 && (outfit.lookType > 0 || outfit.lookItem > 0)) {
			GameSprite* spr = g_gui.gfx.getCreatureSprite(outfit.lookType);
			if (spr && spr->getDrawOffset().x == 8 && spr->getDrawOffset().y == 8) {
				sx -= width / 2;
				sy -= height / 2;
			}
		}
	}

	// Adjust zoom if necessary
	if (adjustZoom) {
		if (zoom < 1.0f) {
			float offset = 10 / (10 * zoom);
			width = std::max<int>(16, static_cast<int>(width * zoom));
			height = std::max<int>(16, static_cast<int>(height * zoom));
			sx += offset;
			sy += offset;
		} else if (zoom > 1.f) {
			float offset = (10 * zoom);
			width += static_cast<int>(offset);
			height += static_cast<int>(offset);
			sx -= offset;
			sy -= offset;
		}
	}

	if (outfit.lookType > 0) {
		spdlog::debug("Blitting outfit {} at ({}, {})", outfit.name, sx, sy);
	}

	renderer->drawTexturedQuad(sx, sy, width, height, textureId, { uint8_t(red), uint8_t(green), uint8_t(blue), uint8_t(alpha) });
}

void MapDrawer::glBlitSquare(int x, int y, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha, int size /* = rme::TileSize */) const {
	renderer->drawColoredQuad(static_cast<float>(x), static_cast<float>(y), static_cast<float>(size), static_cast<float>(size), { red, green, blue, alpha });
}

void MapDrawer::glBlitSquare(int x, int y, const wxColor &color, int size /* = rme::TileSize */) const {
	renderer->drawColoredQuad(static_cast<float>(x), static_cast<float>(y), static_cast<float>(size), static_cast<float>(size), { color.Red(), color.Green(), color.Blue(), color.Alpha() });
}

void MapDrawer::getBrushColor(MapDrawer::BrushColor color, uint8_t &r, uint8_t &g, uint8_t &b, uint8_t &a) {
	switch (color) {
		case COLOR_BRUSH:
			r = g_settings.getInteger(Config::CURSOR_RED);
			g = g_settings.getInteger(Config::CURSOR_GREEN);
			b = g_settings.getInteger(Config::CURSOR_BLUE);
			a = g_settings.getInteger(Config::CURSOR_ALPHA);
			break;

		case COLOR_FLAG_BRUSH:
		case COLOR_HOUSE_BRUSH:
			r = g_settings.getInteger(Config::CURSOR_ALT_RED);
			g = g_settings.getInteger(Config::CURSOR_ALT_GREEN);
			b = g_settings.getInteger(Config::CURSOR_ALT_BLUE);
			a = g_settings.getInteger(Config::CURSOR_ALT_ALPHA);
			break;

		case COLOR_SPAWN_BRUSH:
		case COLOR_SPAWN_NPC_BRUSH:
		case COLOR_ERASER:
		case COLOR_INVALID:
			r = 166;
			g = 0;
			b = 0;
			a = 128;
			break;

		case COLOR_VALID:
			r = 0;
			g = 166;
			b = 0;
			a = 128;
			break;

		default:
			r = 255;
			g = 255;
			b = 255;
			a = 128;
			break;
	}
}

void MapDrawer::getCheckColor(Brush* brush, const Position &pos, uint8_t &r, uint8_t &g, uint8_t &b, uint8_t &a) {
	if (brush->canDraw(&editor.getMap(), pos)) {
		getBrushColor(COLOR_VALID, r, g, b, a);
	} else {
		getBrushColor(COLOR_INVALID, r, g, b, a);
	}
}

void MapDrawer::drawRect(int x, int y, int w, int h, const wxColor &color, float width) {
	renderer->drawRect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), { color.Red(), color.Green(), color.Blue(), color.Alpha() }, width);
}

void MapDrawer::drawFilledRect(int x, int y, int w, int h, const wxColor &color) {
	renderer->drawColoredQuad(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), { color.Red(), color.Green(), color.Blue(), color.Alpha() });
}

void MapDrawer::getDrawPosition(const Position &position, int &x, int &y) {
	int offset;
	if (position.z <= rme::MapGroundLayer) {
		offset = (rme::MapGroundLayer - position.z) * rme::TileSize;
	} else {
		offset = rme::TileSize * (floor - position.z);
	}

	x = ((position.x * rme::TileSize) - view_scroll_x) - offset;
	y = ((position.y * rme::TileSize) - view_scroll_y) - offset;
}
