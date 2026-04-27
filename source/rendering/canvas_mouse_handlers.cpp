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
#include "editor/hotkey_manager.h"
#include <ranges>
#include <wx/clipbrd.h>
#include <wx/wfstream.h>
#include <wx/dcbuffer.h>
#include <fmt/ranges.h>
#include <array>

#include "ui/gui.h" // KNOWN_VIOLATION
#include "editor/editor.h"
#include "brushes/brush.h" // KNOWN_VIOLATION
#include "rendering/sprites.h"
#include "map/map.h"
#include "map/tile.h"
#include "ui/dialogs/old_properties_window.h" // KNOWN_VIOLATION
#include "ui/dialogs/properties_window.h" // KNOWN_VIOLATION
#include "ui/windows/tileset_window.h" // KNOWN_VIOLATION
#include "ui/palette/palette_window.h" // KNOWN_VIOLATION
#include "rendering/map_display.h"
#include "rendering/map_drawer.h"
#include "app/application.h"
#include "live/live_server.h"
#include "ui/windows/browse_tile_window.h" // KNOWN_VIOLATION

#include "ui/menubar/main_menubar.h" // KNOWN_VIOLATION

#include "brushes/doodad_brush.h" // KNOWN_VIOLATION
#include "brushes/house_exit_brush.h" // KNOWN_VIOLATION
#include "brushes/house_brush.h" // KNOWN_VIOLATION
#include "brushes/wall_brush.h" // KNOWN_VIOLATION
#include "brushes/spawn_monster_brush.h" // KNOWN_VIOLATION
#include "brushes/monster_brush.h" // KNOWN_VIOLATION
#include "brushes/ground_brush.h" // KNOWN_VIOLATION
#include "brushes/waypoint_brush.h" // KNOWN_VIOLATION
#include "brushes/raw_brush.h" // KNOWN_VIOLATION
#include "brushes/carpet_brush.h" // KNOWN_VIOLATION
#include "brushes/table_brush.h" // KNOWN_VIOLATION
#include "brushes/spawn_npc_brush.h" // KNOWN_VIOLATION
#include "brushes/npc_brush.h" // KNOWN_VIOLATION

void MapCanvas::OnMouseMove(wxMouseEvent &event) {
	if (screendragging) {
		GetMapWindow()->ScrollRelative(int(g_settings.getFloat(Config::SCROLL_SPEED) * zoom * (event.GetX() - cursor_x)), int(g_settings.getFloat(Config::SCROLL_SPEED) * zoom * (event.GetY() - cursor_y)));
		Refresh();
	}

	cursor_x = event.GetX();
	cursor_y = event.GetY();

	int mouse_map_x, mouse_map_y;
	MouseToMap(&mouse_map_x, &mouse_map_y);

	bool map_update = false;
	if (last_cursor_map_x != mouse_map_x || last_cursor_map_y != mouse_map_y || last_cursor_map_z != floor) {
		map_update = true;
	}
	last_cursor_map_x = mouse_map_x;
	last_cursor_map_y = mouse_map_y;
	last_cursor_map_z = floor;

	if (map_update) {
		UpdatePositionStatus(cursor_x, cursor_y);
		UpdateZoomStatus();
	}

	if (g_gui.IsSelectionMode()) {
		if (map_update && isPasting()) {
			Refresh();
		} else if (map_update && dragging) {
			wxString ss;

			int move_x = drag_start_x - mouse_map_x;
			int move_y = drag_start_y - mouse_map_y;
			int move_z = drag_start_z - floor;
			ss << "Dragging " << -move_x << "," << -move_y << "," << -move_z;
			g_gui.SetStatusText(ss);

			Refresh();
		} else if (boundbox_selection) {
			if (map_update) {
				wxString ss;

				int move_x = std::abs(last_click_map_x - mouse_map_x);
				int move_y = std::abs(last_click_map_y - mouse_map_y);
				ss << "Selection " << move_x + 1 << ":" << move_y + 1;
				g_gui.SetStatusText(ss);
			}

			Refresh();
		}
	} else { // Drawing mode
		Brush* brush = g_gui.GetCurrentBrush();
		if (map_update && drawing && brush) {
			if (brush->isDoodad()) {
				if (event.ControlDown()) {
					PositionVector tilestodraw;
					getTilesToDraw(mouse_map_x, mouse_map_y, floor, &tilestodraw, nullptr);
					editor.undraw(tilestodraw, event.ShiftDown() || event.AltDown());
				} else {
					editor.draw(Position(mouse_map_x, mouse_map_y, floor), event.ShiftDown() || event.AltDown());
				}
			} else if (brush->isDoor()) {
				if (!brush->canDraw(&editor.getMap(), Position(mouse_map_x, mouse_map_y, floor))) {
					// We don't have to waste an action in this case...
				} else {
					PositionVector tilestodraw;
					PositionVector tilestoborder;

					tilestodraw.push_back(Position(mouse_map_x, mouse_map_y, floor));

					tilestoborder.push_back(Position(mouse_map_x, mouse_map_y - 1, floor));
					tilestoborder.push_back(Position(mouse_map_x - 1, mouse_map_y, floor));
					tilestoborder.push_back(Position(mouse_map_x, mouse_map_y + 1, floor));
					tilestoborder.push_back(Position(mouse_map_x + 1, mouse_map_y, floor));

					if (event.ControlDown()) {
						editor.undraw(tilestodraw, tilestoborder, event.AltDown());
					} else {
						editor.draw(tilestodraw, tilestoborder, event.AltDown());
					}
				}
			} else if (brush->needBorders()) {
				PositionVector tilestodraw, tilestoborder;

				getTilesToDraw(mouse_map_x, mouse_map_y, floor, &tilestodraw, &tilestoborder);

				if (event.ControlDown()) {
					editor.undraw(tilestodraw, tilestoborder, event.AltDown());
				} else {
					editor.draw(tilestodraw, tilestoborder, event.AltDown());
				}
			} else if (brush->oneSizeFitsAll()) {
				drawing = true;
				PositionVector tilestodraw;
				tilestodraw.push_back(Position(mouse_map_x, mouse_map_y, floor));

				if (event.ControlDown()) {
					editor.undraw(tilestodraw, event.AltDown());
				} else {
					editor.draw(tilestodraw, event.AltDown());
				}
			} else { // No borders
				PositionVector tilestodraw;

				for (int y = -g_gui.GetBrushSize(); y <= g_gui.GetBrushSize(); y++) {
					for (int x = -g_gui.GetBrushSize(); x <= g_gui.GetBrushSize(); x++) {
						if (g_gui.GetBrushShape() == BRUSHSHAPE_SQUARE) {
							tilestodraw.push_back(Position(mouse_map_x + x, mouse_map_y + y, floor));
						} else if (g_gui.GetBrushShape() == BRUSHSHAPE_CIRCLE) {
							double distance = sqrt(double(x * x) + double(y * y));
							if (distance < g_gui.GetBrushSize() + 0.005) {
								tilestodraw.push_back(Position(mouse_map_x + x, mouse_map_y + y, floor));
							}
						}
					}
				}
				if (event.ControlDown()) {
					editor.undraw(tilestodraw, event.AltDown());
				} else {
					editor.draw(tilestodraw, event.AltDown());
				}
			}

			// Create newd doodad layout (does nothing if a non-doodad brush is selected)
			g_gui.FillDoodadPreviewBuffer();

			g_gui.RefreshView();
		} else if (dragging_draw) {
			g_gui.RefreshView();
		} else if (map_update && brush) {
			Refresh();
		}
	}
}

void MapCanvas::OnMouseLeftRelease(wxMouseEvent &event) {
	OnMouseActionRelease(event);
}

void MapCanvas::OnMouseLeftClick(wxMouseEvent &event) {
	OnMouseActionClick(event);
}

void MapCanvas::OnMouseLeftDoubleClick(wxMouseEvent &event) {
	if (!g_settings.getInteger(Config::DOUBLECLICK_PROPERTIES)) {
		return;
	}

	Map &map = editor.getMap();
	int mouse_map_x, mouse_map_y;
	ScreenToMap(event.GetX(), event.GetY(), &mouse_map_x, &mouse_map_y);
	const Tile* tile = map.getTile(mouse_map_x, mouse_map_y, floor);

	if (tile && tile->size() > 0) {
		Tile* new_tile = tile->deepCopy(map);
		wxDialog* dialog = nullptr;
		// Show monster spawn
		if (new_tile->spawnMonster && g_settings.getInteger(Config::SHOW_SPAWNS_MONSTER)) {
			dialog = newd OldPropertiesWindow(g_gui.root, &editor.getMap(), new_tile, new_tile->spawnMonster);
		}
		// Show monster
		else if (const auto monster = new_tile->getTopMonster(); monster && g_settings.getInteger(Config::SHOW_MONSTERS)) {
			dialog = newd OldPropertiesWindow(g_gui.root, &editor.getMap(), new_tile, monster);
		}
		// Show npc
		else if (new_tile->npc && g_settings.getInteger(Config::SHOW_NPCS)) {
			dialog = newd OldPropertiesWindow(g_gui.root, &editor.getMap(), new_tile, new_tile->npc);
		}
		// Show npc spawn
		else if (new_tile->spawnNpc && g_settings.getInteger(Config::SHOW_SPAWNS_NPC)) {
			dialog = newd OldPropertiesWindow(g_gui.root, &editor.getMap(), new_tile, new_tile->spawnNpc);
		} else if (Item* item = new_tile->getTopItem()) {
			if (!g_settings.getInteger(Config::USE_OLD_ITEM_PROPERTIES_WINDOW)) {
				dialog = newd PropertiesWindow(g_gui.root, &editor.getMap(), new_tile, item);
			} else {
				dialog = newd OldPropertiesWindow(g_gui.root, &editor.getMap(), new_tile, item);
			}
		} else {
			delete new_tile;
			return;
		}

		int ret = dialog->ShowModal();
		if (ret != 0) {
			Action* action = editor.createAction(ACTION_CHANGE_PROPERTIES);
			action->addChange(newd Change(new_tile));
			editor.addAction(action);
		} else {
			// Cancel!
			delete new_tile;
		}
		dialog->Destroy();
	}
}

void MapCanvas::OnMouseCenterClick(wxMouseEvent &event) {
	if (g_settings.getInteger(Config::SWITCH_MOUSEBUTTONS)) {
		OnMousePropertiesClick(event);
	} else {
		OnMouseCameraClick(event);
	}
}

void MapCanvas::OnMouseCenterRelease(wxMouseEvent &event) {
	if (g_settings.getInteger(Config::SWITCH_MOUSEBUTTONS)) {
		OnMousePropertiesRelease(event);
	} else {
		OnMouseCameraRelease(event);
	}
}

void MapCanvas::OnMouseRightClick(wxMouseEvent &event) {
	if (g_settings.getInteger(Config::SWITCH_MOUSEBUTTONS)) {
		OnMouseCameraClick(event);
	} else {
		OnMousePropertiesClick(event);
	}
}

void MapCanvas::OnMouseRightRelease(wxMouseEvent &event) {
	if (g_settings.getInteger(Config::SWITCH_MOUSEBUTTONS)) {
		OnMouseCameraRelease(event);
	} else {
		OnMousePropertiesRelease(event);
	}
}

void MapCanvas::OnMouseActionClick(wxMouseEvent &event) {
	SetFocus();

	int mouse_map_x, mouse_map_y;
	ScreenToMap(event.GetX(), event.GetY(), &mouse_map_x, &mouse_map_y);

	if (event.ControlDown() && event.AltDown()) {
		Tile* tile = editor.getMap().getTile(mouse_map_x, mouse_map_y, floor);
		if (tile && tile->size() > 0) {
			Item* item = tile->getTopItem();
			if (item && item->getRAWBrush()) {
				g_gui.SelectBrush(item->getRAWBrush(), TILESET_RAW);
			}
		}
	} else if (event.AltDown()) {
		OnMouseCameraClick(event);
	} else if (g_gui.IsSelectionMode()) {
		Selection &selection = editor.getSelection();
		if (isPasting()) {
			// Set paste to false (no rendering etc.)
			EndPasting();

			// Paste to the map
			editor.copybuffer.paste(editor, Position(mouse_map_x, mouse_map_y, floor));

			// Start dragging
			dragging = true;
			drag_start_x = mouse_map_x;
			drag_start_y = mouse_map_y;
			drag_start_z = floor;
		} else {
			boundbox_selection = false;
			if (event.ShiftDown()) {
				boundbox_selection = true;

				if (!event.ControlDown()) {
					selection.start(Selection::NONE, ACTION_UNSELECT); // Start selection session
					selection.clear(); // Clear out selection
					selection.finish(); // End selection session
					selection.updateSelectionCount();
				}
			} else if (event.ControlDown()) {
				Tile* tile = editor.getMap().getTile(mouse_map_x, mouse_map_y, floor);
				if (tile) {
					const auto monster = tile->getTopMonster();
					// Show monster spawn
					if (tile->spawnMonster && g_settings.getInteger(Config::SHOW_SPAWNS_MONSTER)) {
						selection.start(); // Start selection session
						if (tile->spawnMonster->isSelected()) {
							selection.remove(tile, tile->spawnMonster);
						} else {
							selection.add(tile, tile->spawnMonster);
						}
						selection.finish(); // Finish selection session
						selection.updateSelectionCount();
						// Show monsters
					} else if (monster && g_settings.getInteger(Config::SHOW_MONSTERS)) {
						selection.start(); // Start selection session
						if (monster->isSelected()) {
							selection.remove(tile, monster);
						} else {
							selection.add(tile, monster);
						}
						selection.finish();
						selection.updateSelectionCount();
					} else if (tile->spawnNpc && g_settings.getInteger(Config::SHOW_SPAWNS_NPC)) {
						selection.start(); // Start selection session
						if (tile->spawnNpc->isSelected()) {
							selection.remove(tile, tile->spawnNpc);
						} else {
							selection.add(tile, tile->spawnNpc);
						}
						selection.finish(); // Finish selection session
						selection.updateSelectionCount();
					} else if (tile->npc && g_settings.getInteger(Config::SHOW_NPCS)) {
						selection.start(); // Start selection session
						if (tile->npc->isSelected()) {
							selection.remove(tile, tile->npc);
						} else {
							selection.add(tile, tile->npc);
						}
						selection.finish(); // Finish selection session
						selection.updateSelectionCount();
						// Show npc spawn
					} else {
						Item* item = tile->getTopItem();
						if (item) {
							selection.start(); // Start selection session
							if (item->isSelected()) {
								selection.remove(tile, item);
							} else {
								selection.add(tile, item);
							}
							selection.finish(); // Finish selection session
							selection.updateSelectionCount();
						}
					}
				}
			} else {
				Tile* tile = editor.getMap().getTile(mouse_map_x, mouse_map_y, floor);
				if (!tile) {
					selection.start(Selection::NONE, ACTION_UNSELECT); // Start selection session
					selection.clear(); // Clear out selection
					selection.finish(); // End selection session
					selection.updateSelectionCount();
				} else if (tile->isSelected()) {
					dragging = true;
					drag_start_x = mouse_map_x;
					drag_start_y = mouse_map_y;
					drag_start_z = floor;
				} else {
					selection.start(); // Start a selection session
					selection.clear();
					selection.commit();
					// Show monster spawn
					if (tile->spawnMonster && g_settings.getInteger(Config::SHOW_SPAWNS_MONSTER)) {
						selection.add(tile, tile->spawnMonster);
						dragging = true;
						drag_start_x = mouse_map_x;
						drag_start_y = mouse_map_y;
						drag_start_z = floor;
						// Show monsters
					} else if (const auto monster = tile->getTopMonster(); monster && g_settings.getInteger(Config::SHOW_MONSTERS)) {
						selection.add(tile, monster);
						dragging = true;
						drag_start_x = mouse_map_x;
						drag_start_y = mouse_map_y;
						drag_start_z = floor;
						// Show npc spawns
					} else if (tile->spawnNpc && g_settings.getInteger(Config::SHOW_SPAWNS_NPC)) {
						selection.add(tile, tile->spawnNpc);
						dragging = true;
						drag_start_x = mouse_map_x;
						drag_start_y = mouse_map_y;
						drag_start_z = floor;
						// Show npcs
					} else if (tile->npc && g_settings.getInteger(Config::SHOW_NPCS)) {
						selection.add(tile, tile->npc);
						dragging = true;
						drag_start_x = mouse_map_x;
						drag_start_y = mouse_map_y;
						drag_start_z = floor;
					} else {
						Item* item = tile->getTopItem();
						if (item) {
							selection.add(tile, item);
							dragging = true;
							drag_start_x = mouse_map_x;
							drag_start_y = mouse_map_y;
							drag_start_z = floor;
						}
					}
					selection.finish(); // Finish the selection session
					selection.updateSelectionCount();
				}
			}
		}
	} else if (g_gui.GetCurrentBrush()) { // Drawing mode
		Brush* brush = g_gui.GetCurrentBrush();
		if (event.ShiftDown() && brush->canDrag()) {
			dragging_draw = true;
		} else {
			if (g_gui.GetBrushSize() == 0 && !brush->oneSizeFitsAll()) {
				drawing = true;
			} else {
				drawing = g_gui.GetCurrentBrush()->canSmear();
			}
			if (brush->isWall()) {
				if (event.AltDown() && g_gui.GetBrushSize() == 0) {
					// z0mg, just clicked a tile, shift variaton.
					if (event.ControlDown()) {
						editor.undraw(Position(mouse_map_x, mouse_map_y, floor), event.AltDown());
					} else {
						editor.draw(Position(mouse_map_x, mouse_map_y, floor), event.AltDown());
					}
				} else {
					PositionVector tilestodraw;
					PositionVector tilestoborder;

					int start_map_x = mouse_map_x - g_gui.GetBrushSize();
					int start_map_y = mouse_map_y - g_gui.GetBrushSize();
					int end_map_x = mouse_map_x + g_gui.GetBrushSize();
					int end_map_y = mouse_map_y + g_gui.GetBrushSize();

					for (int y = start_map_y - 1; y <= end_map_y + 1; ++y) {
						for (int x = start_map_x - 1; x <= end_map_x + 1; ++x) {
							if ((x <= start_map_x + 1 || x >= end_map_x - 1) || (y <= start_map_y + 1 || y >= end_map_y - 1)) {
								tilestoborder.push_back(Position(x, y, floor));
							}
							if (((x == start_map_x || x == end_map_x) || (y == start_map_y || y == end_map_y)) && ((x >= start_map_x && x <= end_map_x) && (y >= start_map_y && y <= end_map_y))) {
								tilestodraw.push_back(Position(x, y, floor));
							}
						}
					}
					if (event.ControlDown()) {
						editor.undraw(tilestodraw, tilestoborder, event.AltDown());
					} else {
						editor.draw(tilestodraw, tilestoborder, event.AltDown());
					}
				}
			} else if (brush->isDoor()) {
				PositionVector tilestodraw;
				PositionVector tilestoborder;

				tilestodraw.push_back(Position(mouse_map_x, mouse_map_y, floor));

				tilestoborder.push_back(Position(mouse_map_x, mouse_map_y - 1, floor));
				tilestoborder.push_back(Position(mouse_map_x - 1, mouse_map_y, floor));
				tilestoborder.push_back(Position(mouse_map_x, mouse_map_y + 1, floor));
				tilestoborder.push_back(Position(mouse_map_x + 1, mouse_map_y, floor));

				if (event.ControlDown()) {
					editor.undraw(tilestodraw, tilestoborder, event.AltDown());
				} else {
					editor.draw(tilestodraw, tilestoborder, event.AltDown());
				}
			} else if (brush->isDoodad() || brush->isSpawnMonster() || brush->isMonster()) {
				if (event.ControlDown()) {
					if (brush->isDoodad()) {
						PositionVector tilestodraw;
						getTilesToDraw(mouse_map_x, mouse_map_y, floor, &tilestodraw, nullptr);
						editor.undraw(tilestodraw, event.AltDown());
					} else {
						editor.undraw(Position(mouse_map_x, mouse_map_y, floor), event.ShiftDown() || event.AltDown());
					}
				} else {
					bool will_show_spawn = false;
					if (brush->isSpawnMonster() || brush->isMonster()) {
						if (!g_settings.getBoolean(Config::SHOW_SPAWNS_MONSTER)) {
							Tile* tile = editor.getMap().getTile(mouse_map_x, mouse_map_y, floor);
							if (!tile || !tile->spawnMonster) {
								will_show_spawn = true;
							}
						}
					}

					editor.draw(Position(mouse_map_x, mouse_map_y, floor), event.ShiftDown() || event.AltDown());

					if (will_show_spawn) {
						Tile* tile = editor.getMap().getTile(mouse_map_x, mouse_map_y, floor);
						if (tile && tile->spawnMonster) {
							g_settings.setInteger(Config::SHOW_SPAWNS_MONSTER, true);
							g_gui.UpdateMenubar();
						}
					}
				}
			} else if (brush->isDoodad() || brush->isSpawnNpc() || brush->isNpc()) {
				if (event.ControlDown()) {
					if (brush->isDoodad()) {
						PositionVector tilestodraw;
						getTilesToDraw(mouse_map_x, mouse_map_y, floor, &tilestodraw, nullptr);
						editor.undraw(tilestodraw, event.AltDown());
					} else {
						editor.undraw(Position(mouse_map_x, mouse_map_y, floor), event.ShiftDown() || event.AltDown());
					}
				} else {
					bool will_show_spawn_npc = false;
					if (brush->isSpawnNpc() || brush->isNpc()) {
						if (!g_settings.getBoolean(Config::SHOW_SPAWNS_NPC)) {
							Tile* tile = editor.getMap().getTile(mouse_map_x, mouse_map_y, floor);
							if (!tile || !tile->spawnNpc) {
								will_show_spawn_npc = true;
							}
						}
					}

					editor.draw(Position(mouse_map_x, mouse_map_y, floor), event.ShiftDown() || event.AltDown());

					if (will_show_spawn_npc) {
						Tile* tile = editor.getMap().getTile(mouse_map_x, mouse_map_y, floor);
						if (tile && tile->spawnNpc) {
							g_settings.setInteger(Config::SHOW_SPAWNS_NPC, true);
							g_gui.UpdateMenubar();
						}
					}
				}
			} else {
				if (brush->isGround() && event.AltDown()) {
					replace_dragging = true;
					Tile* draw_tile = editor.getMap().getTile(mouse_map_x, mouse_map_y, floor);
					if (draw_tile) {
						editor.replace_brush = draw_tile->getGroundBrush();
					} else {
						editor.replace_brush = nullptr;
					}
				}

				if (brush->needBorders()) {
					PositionVector tilestodraw;
					PositionVector tilestoborder;

					bool fill = keyCode == WXK_CONTROL_D && event.ControlDown() && brush->isGround();
					getTilesToDraw(mouse_map_x, mouse_map_y, floor, &tilestodraw, &tilestoborder, fill);

					if (!fill && event.ControlDown()) {
						editor.undraw(tilestodraw, tilestoborder, event.AltDown());
					} else {
						editor.draw(tilestodraw, tilestoborder, event.AltDown());
					}
				} else if (brush->oneSizeFitsAll()) {
					if (brush->isHouseExit() || brush->isWaypoint()) {
						editor.draw(Position(mouse_map_x, mouse_map_y, floor), event.AltDown());
					} else {
						PositionVector tilestodraw;
						tilestodraw.push_back(Position(mouse_map_x, mouse_map_y, floor));
						if (event.ControlDown()) {
							editor.undraw(tilestodraw, event.AltDown());
						} else {
							editor.draw(tilestodraw, event.AltDown());
						}
					}
				} else {
					PositionVector tilestodraw;

					getTilesToDraw(mouse_map_x, mouse_map_y, floor, &tilestodraw, nullptr);

					if (event.ControlDown()) {
						editor.undraw(tilestodraw, event.AltDown());
					} else {
						editor.draw(tilestodraw, event.AltDown());
					}
				}
			}
			// Change the doodad layout brush
			g_gui.FillDoodadPreviewBuffer();
		}
	}
	last_click_x = int(event.GetX() * zoom);
	last_click_y = int(event.GetY() * zoom);

	int start_x, start_y;
	GetMapWindow()->GetViewStart(&start_x, &start_y);
	last_click_abs_x = last_click_x + start_x;
	last_click_abs_y = last_click_y + start_y;

	last_click_map_x = mouse_map_x;
	last_click_map_y = mouse_map_y;
	last_click_map_z = floor;
	g_gui.RefreshView();
	g_gui.UpdateMinimap();
}

void MapCanvas::OnMouseActionRelease(wxMouseEvent &event) {
	int mouse_map_x, mouse_map_y;
	ScreenToMap(event.GetX(), event.GetY(), &mouse_map_x, &mouse_map_y);

	int move_x = last_click_map_x - mouse_map_x;
	int move_y = last_click_map_y - mouse_map_y;
	int move_z = last_click_map_z - floor;
	OnMouseCameraRelease(event);

	if (g_gui.IsSelectionMode()) {
		if (dragging && (move_x != 0 || move_y != 0 || move_z != 0)) {
			editor.moveSelection(Position(move_x, move_y, move_z));
		} else {
			Selection &selection = editor.getSelection();
			if (boundbox_selection) {
				if (mouse_map_x == last_click_map_x && mouse_map_y == last_click_map_y && event.ControlDown()) {
					// Mouse hasn't moved, do control+shift thingy!
					Tile* tile = editor.getMap().getTile(mouse_map_x, mouse_map_y, floor);
					if (tile) {
						selection.start(); // Start a selection session
						if (tile->isSelected()) {
							selection.remove(tile);
						} else {
							selection.add(tile);
						}
						selection.finish(); // Finish the selection session
						selection.updateSelectionCount();
					}
				} else {
					// The cursor has moved, do some boundboxing!
					if (last_click_map_x > mouse_map_x) {
						int tmp = mouse_map_x;
						mouse_map_x = last_click_map_x;
						last_click_map_x = tmp;
					}
					if (last_click_map_y > mouse_map_y) {
						int tmp = mouse_map_y;
						mouse_map_y = last_click_map_y;
						last_click_map_y = tmp;
					}

					int numtiles = 0;
					int threadcount = std::max(g_settings.getInteger(Config::WORKER_THREADS), 1);

					int start_x = 0, start_y = 0, start_z = 0;
					int end_x = 0, end_y = 0, end_z = 0;

					switch (g_settings.getInteger(Config::SELECTION_TYPE)) {
						case SELECT_CURRENT_FLOOR: {
							start_z = end_z = floor;
							start_x = last_click_map_x;
							start_y = last_click_map_y;
							end_x = mouse_map_x;
							end_y = mouse_map_y;
							break;
						}
						case SELECT_ALL_FLOORS: {
							start_x = last_click_map_x;
							start_y = last_click_map_y;
							start_z = rme::MapMaxLayer;
							end_x = mouse_map_x;
							end_y = mouse_map_y;
							end_z = floor;

							if (g_settings.getInteger(Config::COMPENSATED_SELECT)) {
								start_x -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
								start_y -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);

								end_x -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
								end_y -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
							}

							numtiles = (start_z - end_z) * (end_x - start_x) * (end_y - start_y);
							break;
						}
						case SELECT_VISIBLE_FLOORS: {
							start_x = last_click_map_x;
							start_y = last_click_map_y;
							if (floor < 8) {
								start_z = rme::MapGroundLayer;
							} else {
								start_z = std::min(rme::MapMaxLayer, floor + 2);
							}
							end_x = mouse_map_x;
							end_y = mouse_map_y;
							end_z = floor;

							if (g_settings.getInteger(Config::COMPENSATED_SELECT)) {
								start_x -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
								start_y -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);

								end_x -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
								end_y -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
							}
							break;
						}
					}

					if (numtiles < 500) {
						// No point in threading for such a small set.
						threadcount = 1;
					}
					// Subdivide the selection area
					// We know it's a square, just split it into several areas
					int width = end_x - start_x;
					if (width < threadcount) {
						threadcount = std::min(1, width);
					}
					// Let's divide!
					int remainder = width;
					int cleared = 0;
					std::vector<SelectionThread*> threads;
					if (width == 0) {
						threads.push_back(newd SelectionThread(editor, Position(start_x, start_y, start_z), Position(start_x, end_y, end_z)));
					} else {
						for (int i = 0; i < threadcount; ++i) {
							int chunksize = width / threadcount;
							// The last threads takes all the remainder
							if (i == threadcount - 1) {
								chunksize = remainder;
							}
							threads.push_back(newd SelectionThread(editor, Position(start_x + cleared, start_y, start_z), Position(start_x + cleared + chunksize, end_y, end_z)));
							cleared += chunksize;
							remainder -= chunksize;
						}
					}
					ASSERT(cleared == width);
					ASSERT(remainder == 0);

					selection.start(); // Start a selection session
					for (SelectionThread* thread : threads) {
						thread->Execute();
					}
					for (SelectionThread* thread : threads) {
						selection.join(thread);
					}
					selection.finish(); // Finish the selection session
					selection.updateSelectionCount();
				}
			} else if (event.ControlDown()) {
				////
			} else {
				// User hasn't moved anything, meaning selection/deselection
				Tile* tile = editor.getMap().getTile(mouse_map_x, mouse_map_y, floor);
				if (tile) {
					if (tile->spawnMonster && g_settings.getInteger(Config::SHOW_SPAWNS_MONSTER)) {
						if (!tile->spawnMonster->isSelected()) {
							selection.start(); // Start a selection session
							selection.add(tile, tile->spawnMonster);
							selection.finish(); // Finish the selection session
							selection.updateSelectionCount();
						}
					} else if (const auto monster = tile->getTopMonster(); monster && g_settings.getInteger(Config::SHOW_MONSTERS)) {
						if (!monster->isSelected()) {
							selection.start(); // Start a selection session
							selection.add(tile, monster);
							selection.finish(); // Finish the selection session
							selection.updateSelectionCount();
						}
					} else if (tile->spawnNpc && g_settings.getInteger(Config::SHOW_SPAWNS_NPC)) {
						if (!tile->spawnNpc->isSelected()) {
							selection.start(); // Start a selection session
							selection.add(tile, tile->spawnNpc);
							selection.finish(); // Finish the selection session
							selection.updateSelectionCount();
						}
					} else if (tile->npc && g_settings.getInteger(Config::SHOW_NPCS)) {
						if (!tile->npc->isSelected()) {
							selection.start(); // Start a selection session
							selection.add(tile, tile->npc);
							selection.finish(); // Finish the selection session
							selection.updateSelectionCount();
						}
					} else {
						Item* item = tile->getTopItem();
						if (item && !item->isSelected()) {
							selection.start(); // Start a selection session
							selection.add(tile, item);
							selection.finish(); // Finish the selection session
							selection.updateSelectionCount();
						}
					}
				}
			}
		}
		editor.resetActionsTimer();
		editor.updateActions();
		dragging = false;
		boundbox_selection = false;
	} else if (g_gui.GetCurrentBrush()) { // Drawing mode
		Brush* brush = g_gui.GetCurrentBrush();
		if (dragging_draw) {
			if (brush->isSpawnMonster()) {
				int start_map_x = std::min(last_click_map_x, mouse_map_x);
				int start_map_y = std::min(last_click_map_y, mouse_map_y);
				int end_map_x = std::max(last_click_map_x, mouse_map_x);
				int end_map_y = std::max(last_click_map_y, mouse_map_y);

				int map_x = start_map_x + (end_map_x - start_map_x) / 2;
				int map_y = start_map_y + (end_map_y - start_map_y) / 2;

				int width = std::min(g_settings.getInteger(Config::MAX_SPAWN_MONSTER_RADIUS), ((end_map_x - start_map_x) / 2 + (end_map_y - start_map_y) / 2) / 2);
				int old = g_gui.GetBrushSize();
				g_gui.SetBrushSize(width);
				editor.draw(Position(map_x, map_y, floor), event.AltDown());
				g_gui.SetBrushSize(old);
			} else if (brush->isSpawnNpc()) {
				int start_map_x = std::min(last_click_map_x, mouse_map_x);
				int start_map_y = std::min(last_click_map_y, mouse_map_y);
				int end_map_x = std::max(last_click_map_x, mouse_map_x);
				int end_map_y = std::max(last_click_map_y, mouse_map_y);

				int map_x = start_map_x + (end_map_x - start_map_x) / 2;
				int map_y = start_map_y + (end_map_y - start_map_y) / 2;

				int width = std::min(g_settings.getInteger(Config::MAX_SPAWN_NPC_RADIUS), ((end_map_x - start_map_x) / 2 + (end_map_y - start_map_y) / 2) / 2);
				int old = g_gui.GetBrushSize();
				g_gui.SetBrushSize(width);
				editor.draw(Position(map_x, map_y, floor), event.AltDown());
				g_gui.SetBrushSize(old);
			} else {
				PositionVector tilestodraw;
				PositionVector tilestoborder;
				if (brush->isWall()) {
					int start_map_x = std::min(last_click_map_x, mouse_map_x);
					int start_map_y = std::min(last_click_map_y, mouse_map_y);
					int end_map_x = std::max(last_click_map_x, mouse_map_x);
					int end_map_y = std::max(last_click_map_y, mouse_map_y);

					for (int y = start_map_y - 1; y <= end_map_y + 1; y++) {
						for (int x = start_map_x - 1; x <= end_map_x + 1; x++) {
							if ((x <= start_map_x + 1 || x >= end_map_x - 1) || (y <= start_map_y + 1 || y >= end_map_y - 1)) {
								tilestoborder.push_back(Position(x, y, floor));
							}
							if (((x == start_map_x || x == end_map_x) || (y == start_map_y || y == end_map_y)) && ((x >= start_map_x && x <= end_map_x) && (y >= start_map_y && y <= end_map_y))) {
								tilestodraw.push_back(Position(x, y, floor));
							}
						}
					}
				} else {
					if (g_gui.GetBrushShape() == BRUSHSHAPE_SQUARE) {
						if (last_click_map_x > mouse_map_x) {
							int tmp = mouse_map_x;
							mouse_map_x = last_click_map_x;
							last_click_map_x = tmp;
						}
						if (last_click_map_y > mouse_map_y) {
							int tmp = mouse_map_y;
							mouse_map_y = last_click_map_y;
							last_click_map_y = tmp;
						}

						for (int x = last_click_map_x - 1; x <= mouse_map_x + 1; x++) {
							for (int y = last_click_map_y - 1; y <= mouse_map_y + 1; y++) {
								if ((x <= last_click_map_x || x >= mouse_map_x) || (y <= last_click_map_y || y >= mouse_map_y)) {
									tilestoborder.push_back(Position(x, y, floor));
								}
								if ((x >= last_click_map_x && x <= mouse_map_x) && (y >= last_click_map_y && y <= mouse_map_y)) {
									tilestodraw.push_back(Position(x, y, floor));
								}
							}
						}
					} else {
						int start_x, end_x;
						int start_y, end_y;
						int width = std::max(
							std::abs(
								std::max(mouse_map_y, last_click_map_y) - std::min(mouse_map_y, last_click_map_y)
							),
							std::abs(
								std::max(mouse_map_x, last_click_map_x) - std::min(mouse_map_x, last_click_map_x)
							)
						);
						if (mouse_map_x < last_click_map_x) {
							start_x = last_click_map_x - width;
							end_x = last_click_map_x;
						} else {
							start_x = last_click_map_x;
							end_x = last_click_map_x + width;
						}
						if (mouse_map_y < last_click_map_y) {
							start_y = last_click_map_y - width;
							end_y = last_click_map_y;
						} else {
							start_y = last_click_map_y;
							end_y = last_click_map_y + width;
						}

						int center_x = start_x + (end_x - start_x) / 2;
						int center_y = start_y + (end_y - start_y) / 2;
						float radii = width / 2.0f + 0.005f;

						for (int y = start_y - 1; y <= end_y + 1; y++) {
							float dy = center_y - y;
							for (int x = start_x - 1; x <= end_x + 1; x++) {
								float dx = center_x - x;
								// printf("%f;%f\n", dx, dy);
								float distance = sqrt(dx * dx + dy * dy);
								if (distance < radii) {
									tilestodraw.push_back(Position(x, y, floor));
								}
								if (std::abs(distance - radii) < 1.5) {
									tilestoborder.push_back(Position(x, y, floor));
								}
							}
						}
					}
				}
				if (event.ControlDown()) {
					editor.undraw(tilestodraw, tilestoborder, event.AltDown());
				} else {
					editor.draw(tilestodraw, tilestoborder, event.AltDown());
				}
			}
		}
		editor.resetActionsTimer();
		editor.updateActions();
		drawing = false;
		dragging_draw = false;
		replace_dragging = false;
		editor.replace_brush = nullptr;
	}
	g_gui.RefreshView();
	g_gui.UpdateMinimap();
}

void MapCanvas::OnMouseCameraClick(wxMouseEvent &event) {
	SetFocus();

	last_mmb_click_x = event.GetX();
	last_mmb_click_y = event.GetY();

	if (event.ControlDown()) {
		int screensize_x, screensize_y;
		MapWindow* window = GetMapWindow();
		window->GetViewSize(&screensize_x, &screensize_y);
		window->ScrollRelative(
			int(-screensize_x * (1.0 - zoom) * (std::max(cursor_x, 1) / double(screensize_x))),
			int(-screensize_y * (1.0 - zoom) * (std::max(cursor_y, 1) / double(screensize_y)))
		);
		zoom = 1.0;
		Refresh();
	} else {
		screendragging = true;
	}
}

void MapCanvas::OnMouseCameraRelease(wxMouseEvent &event) {
	SetFocus();
	screendragging = false;
	if (event.ControlDown()) {
		// ...
		// Haven't moved much, it's a click!
	} else if (last_mmb_click_x > event.GetX() - 3 && last_mmb_click_x < event.GetX() + 3 && last_mmb_click_y > event.GetY() - 3 && last_mmb_click_y < event.GetY() + 3) {
		int screensize_x, screensize_y;
		MapWindow* window = GetMapWindow();
		window->GetViewSize(&screensize_x, &screensize_y);
		window->ScrollRelative(
			int(zoom * (2 * cursor_x - screensize_x)),
			int(zoom * (2 * cursor_y - screensize_y))
		);
		Refresh();
	}
}

void MapCanvas::OnMousePropertiesClick(wxMouseEvent &event) {
	SetFocus();

	int mouse_map_x, mouse_map_y;
	ScreenToMap(event.GetX(), event.GetY(), &mouse_map_x, &mouse_map_y);
	Tile* tile = editor.getMap().getTile(mouse_map_x, mouse_map_y, floor);

	if (g_gui.IsDrawingMode()) {
		g_gui.SetSelectionMode();
	}

	EndPasting();

	Selection &selection = editor.getSelection();

	boundbox_selection = false;
	if (event.ShiftDown()) {
		boundbox_selection = true;

		if (!event.ControlDown()) {
			selection.start(); // Start selection session
			selection.clear(); // Clear out selection
			selection.finish(); // End selection session
			selection.updateSelectionCount();
		}
	} else if (!tile) {
		selection.start(); // Start selection session
		selection.clear(); // Clear out selection
		selection.finish(); // End selection session
		selection.updateSelectionCount();
	} else if (tile->isSelected()) {
		// Do nothing!
	} else {
		selection.start(); // Start a selection session
		selection.clear();
		selection.commit();
		if (tile->spawnMonster && g_settings.getInteger(Config::SHOW_SPAWNS_MONSTER)) {
			selection.add(tile, tile->spawnMonster);
		} else if (const auto monster = tile->getTopMonster(); monster && g_settings.getInteger(Config::SHOW_MONSTERS)) {
			selection.add(tile, monster);
		} else if (tile->npc && g_settings.getInteger(Config::SHOW_NPCS)) {
			selection.add(tile, tile->npc);
		} else if (tile->spawnNpc && g_settings.getInteger(Config::SHOW_SPAWNS_NPC)) {
			selection.add(tile, tile->spawnNpc);
		} else {
			Item* item = tile->getTopItem();
			if (item) {
				selection.add(tile, item);
			}
		}
		selection.finish(); // Finish the selection session
		selection.updateSelectionCount();
	}

	last_click_x = int(event.GetX() * zoom);
	last_click_y = int(event.GetY() * zoom);

	int start_x, start_y;
	GetMapWindow()->GetViewStart(&start_x, &start_y);
	last_click_abs_x = last_click_x + start_x;
	last_click_abs_y = last_click_y + start_y;

	last_click_map_x = mouse_map_x;
	last_click_map_y = mouse_map_y;
	g_gui.RefreshView();
}

void MapCanvas::OnMousePropertiesRelease(wxMouseEvent &event) {
	int mouse_map_x, mouse_map_y;
	ScreenToMap(event.GetX(), event.GetY(), &mouse_map_x, &mouse_map_y);

	if (g_gui.IsDrawingMode()) {
		g_gui.SetSelectionMode();
	}

	if (boundbox_selection) {
		Selection &selection = editor.getSelection();
		if (mouse_map_x == last_click_map_x && mouse_map_y == last_click_map_y && event.ControlDown()) {
			// Mouse hasn't move, do control+shift thingy!
			Tile* tile = editor.getMap().getTile(mouse_map_x, mouse_map_y, floor);
			if (tile) {
				selection.start(); // Start a selection session
				if (tile->isSelected()) {
					selection.remove(tile);
				} else {
					selection.add(tile);
				}
				selection.finish(); // Finish the selection session
				selection.updateSelectionCount();
			}
		} else {
			// The cursor has moved, do some boundboxing!
			if (last_click_map_x > mouse_map_x) {
				int tmp = mouse_map_x;
				mouse_map_x = last_click_map_x;
				last_click_map_x = tmp;
			}
			if (last_click_map_y > mouse_map_y) {
				int tmp = mouse_map_y;
				mouse_map_y = last_click_map_y;
				last_click_map_y = tmp;
			}

			selection.start(); // Start a selection session
			switch (g_settings.getInteger(Config::SELECTION_TYPE)) {
				case SELECT_CURRENT_FLOOR: {
					for (int x = last_click_map_x; x <= mouse_map_x; x++) {
						for (int y = last_click_map_y; y <= mouse_map_y; y++) {
							Tile* tile = editor.getMap().getTile(x, y, floor);
							if (!tile) {
								continue;
							}
							selection.add(tile);
						}
					}
					break;
				}
				case SELECT_ALL_FLOORS: {
					int start_x, start_y, start_z;
					int end_x, end_y, end_z;

					start_x = last_click_map_x;
					start_y = last_click_map_y;
					start_z = rme::MapMaxLayer;
					end_x = mouse_map_x;
					end_y = mouse_map_y;
					end_z = floor;

					if (g_settings.getInteger(Config::COMPENSATED_SELECT)) {
						start_x -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
						start_y -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);

						end_x -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
						end_y -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
					}

					for (int z = start_z; z >= end_z; z--) {
						for (int x = start_x; x <= end_x; x++) {
							for (int y = start_y; y <= end_y; y++) {
								Tile* tile = editor.getMap().getTile(x, y, z);
								if (!tile) {
									continue;
								}
								selection.add(tile);
							}
						}
						if (z <= rme::MapGroundLayer && g_settings.getInteger(Config::COMPENSATED_SELECT)) {
							start_x++;
							start_y++;
							end_x++;
							end_y++;
						}
					}
					break;
				}
				case SELECT_VISIBLE_FLOORS: {
					int start_x, start_y, start_z;
					int end_x, end_y, end_z;

					start_x = last_click_map_x;
					start_y = last_click_map_y;
					if (floor < 8) {
						start_z = rme::MapGroundLayer;
					} else {
						start_z = std::min(rme::MapMaxLayer, floor + 2);
					}
					end_x = mouse_map_x;
					end_y = mouse_map_y;
					end_z = floor;

					if (g_settings.getInteger(Config::COMPENSATED_SELECT)) {
						start_x -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
						start_y -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);

						end_x -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
						end_y -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
					}

					for (int z = start_z; z >= end_z; z--) {
						for (int x = start_x; x <= end_x; x++) {
							for (int y = start_y; y <= end_y; y++) {
								Tile* tile = editor.getMap().getTile(x, y, z);
								if (!tile) {
									continue;
								}
								selection.add(tile);
							}
						}
						if (z <= rme::MapGroundLayer && g_settings.getInteger(Config::COMPENSATED_SELECT)) {
							start_x++;
							start_y++;
							end_x++;
							end_y++;
						}
					}
					break;
				}
			}
			selection.finish(); // Finish the selection session
			selection.updateSelectionCount();
		}
	} else if (event.ControlDown()) {
		// Nothing
	}

	popup_menu->Update();
	PopupMenu(popup_menu);

	editor.resetActionsTimer();
	dragging = false;
	boundbox_selection = false;

	last_cursor_map_x = mouse_map_x;
	last_cursor_map_y = mouse_map_y;
	last_cursor_map_z = floor;

	g_gui.RefreshView();
}

void MapCanvas::OnWheel(wxMouseEvent &event) {
	if (event.ControlDown()) {
		static double diff = 0.0;
		diff += event.GetWheelRotation();
		if (diff <= 1.0 || diff >= 1.0) {
			if (diff < 0.0) {
				g_gui.ChangeFloor(floor - 1);
			} else {
				g_gui.ChangeFloor(floor + 1);
			}
			diff = 0.0;
		}
		UpdatePositionStatus();
	} else if (event.AltDown()) {
		static double diff = 0.0;
		diff += event.GetWheelRotation();
		if (diff <= 1.0 || diff >= 1.0) {
			if (diff < 0.0) {
				g_gui.IncreaseBrushSize();
			} else {
				g_gui.DecreaseBrushSize();
			}
			diff = 0.0;
		}
	} else {
		double diff = -event.GetWheelRotation() * g_settings.getFloat(Config::ZOOM_SPEED) / 640.0;
		double oldzoom = zoom;
		zoom += diff;

		if (zoom < 0.125) {
			diff = 0.125 - oldzoom;
			zoom = 0.125;
		}
		if (zoom > 25.00) {
			diff = 25.00 - oldzoom;
			zoom = 25.0;
		}

		UpdateZoomStatus();

		int screensize_x, screensize_y;
		MapWindow* window = GetMapWindow();
		window->GetViewSize(&screensize_x, &screensize_y);

		// This took a day to figure out!
		int scroll_x = int(screensize_x * diff * (std::max(cursor_x, 1) / double(screensize_x))) * GetContentScaleFactor();
		int scroll_y = int(screensize_y * diff * (std::max(cursor_y, 1) / double(screensize_y))) * GetContentScaleFactor();

		window->ScrollRelative(-scroll_x, -scroll_y);
	}

	Refresh();
}

void MapCanvas::OnLoseMouse(wxMouseEvent &event) {
	Refresh();
}

void MapCanvas::OnGainMouse(wxMouseEvent &event) {
	if (!event.LeftIsDown()) {
		dragging = false;
		boundbox_selection = false;
		drawing = false;
	}
	if (!event.MiddleIsDown()) {
		screendragging = false;
	}

	Refresh();
}
