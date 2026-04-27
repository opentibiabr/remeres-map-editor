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
