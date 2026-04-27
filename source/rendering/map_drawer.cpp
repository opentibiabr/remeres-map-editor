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

#include "app/main.h"

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

#include "editor/editor.h"
#include "ui/gui.h" // KNOWN_VIOLATION
#include "rendering/sprites.h"
#include "rendering/map_drawer.h"
#include <unordered_set>
#include "rendering/map_display.h"
#include "editor/copybuffer.h"
#include "live/live_socket.h"
#include "rendering/graphics.h"

#include "brushes/doodad_brush.h" // KNOWN_VIOLATION
#include "brushes/monster_brush.h" // KNOWN_VIOLATION
#include "brushes/house_exit_brush.h" // KNOWN_VIOLATION
#include "brushes/house_brush.h" // KNOWN_VIOLATION
#include "brushes/spawn_monster_brush.h" // KNOWN_VIOLATION
#include "rendering/sprite_appearances.h"
#include "brushes/npc_brush.h" // KNOWN_VIOLATION
#include "brushes/spawn_npc_brush.h" // KNOWN_VIOLATION
#include "brushes/wall_brush.h" // KNOWN_VIOLATION
#include "brushes/carpet_brush.h" // KNOWN_VIOLATION
#include "brushes/raw_brush.h" // KNOWN_VIOLATION
#include "brushes/table_brush.h" // KNOWN_VIOLATION
#include "brushes/waypoint_brush.h" // KNOWN_VIOLATION
#include "brushes/zone_brush.h" // KNOWN_VIOLATION
#include "rendering/light_drawer.h"
#include "rendering/gl_renderer.h"

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
	if (light_drawer) {
		light_drawer->clear();
	}

	renderer->flush();
}

bool MapDrawer::isSceneDirty() const {
	if (fboDirty) {
		return true;
	}
	if (view_scroll_x != prevScrollX) {
		return true;
	}
	if (view_scroll_y != prevScrollY) {
		return true;
	}
	if (zoom != prevZoom) {
		return true;
	}
	if (floor != prevFloor) {
		return true;
	}
	if (start_z != prevStartZ) {
		return true;
	}
	if (screensize_x != prevScreenW) {
		return true;
	}
	if (screensize_y != prevScreenH) {
		return true;
	}
	if (dragging || dragging_draw) {
		return true;
	}
	if (options.show_preview && zoom <= 2.0f) {
		return true;
	}
	return false;
}

void MapDrawer::Draw() {
	renderer->ensureFBO(screensize_x, screensize_y);

	if (isSceneDirty()) {
		renderer->beginFBO();

		DrawBackground();
		DrawMap();
		if (options.show_lights) {
			light_drawer->draw(start_x, start_y, end_x, end_y, view_scroll_x, view_scroll_y, renderer.get());
		}
		renderer->flush();

		renderer->endFBO();

		prevScrollX = view_scroll_x;
		prevScrollY = view_scroll_y;
		prevZoom = zoom;
		prevFloor = floor;
		prevStartZ = start_z;
		prevScreenW = screensize_x;
		prevScreenH = screensize_y;
		fboDirty = false;
	}

	if (renderer->hasFBO()) {
		float w = screensize_x * zoom;
		float h = screensize_y * zoom;
		renderer->blitFBO(w, h);
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
