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

#include "editor/hotkey_manager.h"
#include "ui/menubar/main_menubar.h" // KNOWN_VIOLATION

void MapCanvas::OnKeyDown(wxKeyEvent &event) {
// wxGTK does not propagate keyboard events from wxGLCanvas
// to the frame's accelerator table, so we dispatch manually.
#ifdef __LINUX__
	if (DispatchMenuShortcut(event)) {
		return;
	}
#endif
	MapWindow* window = GetMapWindow();

	switch (event.GetKeyCode()) {
		case WXK_NUMPAD_ADD:
		case WXK_PAGEUP: {
			g_gui.ChangeFloor(floor - 1);
			break;
		}
		case WXK_NUMPAD_SUBTRACT:
		case WXK_PAGEDOWN: {
			g_gui.ChangeFloor(floor + 1);
			break;
		}
		case WXK_NUMPAD_MULTIPLY: {
			double diff = -0.3;

			double oldzoom = zoom;
			zoom += diff;

			if (zoom < 0.125) {
				diff = 0.125 - oldzoom;
				zoom = 0.125;
			}

			int screensize_x, screensize_y;
			window->GetViewSize(&screensize_x, &screensize_y);

			// This took a day to figure out!
			int scroll_x = int(screensize_x * diff * (std::max(cursor_x, 1) / double(screensize_x)));
			int scroll_y = int(screensize_y * diff * (std::max(cursor_y, 1) / double(screensize_y)));

			window->ScrollRelative(-scroll_x, -scroll_y);

			UpdatePositionStatus();
			UpdateZoomStatus();
			Refresh();
			break;
		}
		case WXK_NUMPAD_DIVIDE: {
			double diff = 0.3;
			double oldzoom = zoom;
			zoom += diff;

			if (zoom > 25.00) {
				diff = 25.00 - oldzoom;
				zoom = 25.0;
			}

			int screensize_x, screensize_y;
			window->GetViewSize(&screensize_x, &screensize_y);

			// This took a day to figure out!
			int scroll_x = int(screensize_x * diff * (std::max(cursor_x, 1) / double(screensize_x)));
			int scroll_y = int(screensize_y * diff * (std::max(cursor_y, 1) / double(screensize_y)));
			window->ScrollRelative(-scroll_x, -scroll_y);

			UpdatePositionStatus();
			UpdateZoomStatus();
			Refresh();
			break;
		}
		// This will work like crap with non-us layouts, well, sucks for them until there is another solution.
		case '[':
		case '+': {
			g_gui.IncreaseBrushSize();
			Refresh();
			break;
		}
		case ']':
		case '-': {
			g_gui.DecreaseBrushSize();
			Refresh();
			break;
		}
		case WXK_NUMPAD_UP:
		case WXK_UP: {
			int start_x, start_y;
			window->GetViewStart(&start_x, &start_y);

			int tiles = 3;
			if (event.ControlDown()) {
				tiles = 10;
			} else if (zoom == 1.0) {
				tiles = 1;
			}

			window->Scroll(start_x, int(start_y - rme::TileSize * tiles * zoom));
			UpdatePositionStatus();
			Refresh();
			break;
		}
		case WXK_NUMPAD_DOWN:
		case WXK_DOWN: {
			int start_x, start_y;
			window->GetViewStart(&start_x, &start_y);

			int tiles = 3;
			if (event.ControlDown()) {
				tiles = 10;
			} else if (zoom == 1.0) {
				tiles = 1;
			}

			window->Scroll(start_x, int(start_y + rme::TileSize * tiles * zoom));
			UpdatePositionStatus();
			Refresh();
			break;
		}
		case WXK_NUMPAD_LEFT:
		case WXK_LEFT: {
			int start_x, start_y;
			window->GetViewStart(&start_x, &start_y);

			int tiles = 3;
			if (event.ControlDown()) {
				tiles = 10;
			} else if (zoom == 1.0) {
				tiles = 1;
			}

			window->Scroll(int(start_x - rme::TileSize * tiles * zoom), start_y);
			UpdatePositionStatus();
			Refresh();
			break;
		}
		case WXK_NUMPAD_RIGHT:
		case WXK_RIGHT: {
			int start_x, start_y;
			window->GetViewStart(&start_x, &start_y);

			int tiles = 3;
			if (event.ControlDown()) {
				tiles = 10;
			} else if (zoom == 1.0) {
				tiles = 1;
			}

			window->Scroll(int(start_x + rme::TileSize * tiles * zoom), start_y);
			UpdatePositionStatus();
			Refresh();
			break;
		}
		case WXK_SPACE: { // Utility keys
			if (event.ControlDown()) {
				g_gui.FillDoodadPreviewBuffer();
				g_gui.RefreshView();
			} else {
				g_gui.SwitchMode();
			}
			break;
		}
		case WXK_TAB: { // Tab switch
			if (event.ShiftDown()) {
				g_gui.CycleTab(false);
			} else {
				g_gui.CycleTab(true);
			}
			break;
		}
		case WXK_DELETE: { // Delete
			editor.destroySelection();
			g_gui.RefreshView();
			break;
		}
		case 'z':
		case 'Z': { // Rotate counterclockwise (actually shift variaton, but whatever... :P)
			int nv = g_gui.GetBrushVariation();
			--nv;
			if (nv < 0) {
				nv = std::max(0, (g_gui.GetCurrentBrush() ? g_gui.GetCurrentBrush()->getMaxVariation() - 1 : 0));
			}
			g_gui.SetBrushVariation(nv);
			g_gui.RefreshView();
			break;
		}
		case 'x':
		case 'X': { // Rotate clockwise (actually shift variaton, but whatever... :P)
			int nv = g_gui.GetBrushVariation();
			++nv;
			if (nv >= (g_gui.GetCurrentBrush() ? g_gui.GetCurrentBrush()->getMaxVariation() : 0)) {
				nv = 0;
			}
			g_gui.SetBrushVariation(nv);
			g_gui.RefreshView();
			break;
		}
		case 'q':
		case 'Q': {
			int fullId = static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SHOW_SHADE);
			wxMenuBar* mb = g_gui.root->GetMenuBar();
			if (mb) {
				wxMenuItem* item = mb->FindItem(fullId);
				if (item && item->IsCheckable()) {
					item->Check(!item->IsChecked());
				}
			}
			wxCommandEvent evt(wxEVT_COMMAND_MENU_SELECTED, fullId);
			g_gui.root->GetEventHandler()->ProcessEvent(evt);
			break;
		}
		// Hotkeys
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9': {
			int index = event.GetKeyCode() - '0';
			if (event.ControlDown()) {
				Hotkey hk;
				if (g_gui.IsSelectionMode()) {
					int view_start_x, view_start_y;
					window->GetViewStart(&view_start_x, &view_start_y);
					int view_start_map_x = view_start_x / rme::TileSize, view_start_map_y = view_start_y / rme::TileSize;

					int view_screensize_x, view_screensize_y;
					window->GetViewSize(&view_screensize_x, &view_screensize_y);

					int map_x = int(view_start_map_x + (view_screensize_x * zoom) / rme::TileSize / 2);
					int map_y = int(view_start_map_y + (view_screensize_y * zoom) / rme::TileSize / 2);

					hk = Hotkey(Position(map_x, map_y, floor));
				} else if (g_gui.GetCurrentBrush()) {
					// Drawing mode
					hk = Hotkey(g_gui.GetCurrentBrush());
				} else {
					break;
				}
				g_hotkeys.SetHotkey(index, hk);
				g_gui.SetStatusText("Set hotkey " + i2ws(index) + ".");
			} else {
				// Click hotkey
				Hotkey hk = g_hotkeys.GetHotkey(index);
				if (hk.IsPosition()) {
					g_gui.SetSelectionMode();

					int map_x = hk.GetPosition().x;
					int map_y = hk.GetPosition().y;
					int map_z = hk.GetPosition().z;

					window->Scroll(rme::TileSize * map_x, rme::TileSize * map_y, true);
					floor = map_z;

					g_gui.SetStatusText("Used hotkey " + i2ws(index));
					g_gui.RefreshView();
				} else if (hk.IsBrush()) {
					g_gui.SetDrawingMode();

					std::string name = hk.GetBrushname();
					Brush* brush = g_brushes.getBrush(name);
					if (brush == nullptr) {
						g_gui.SetStatusText("Brush \"" + wxstr(name) + "\" not found");
						return;
					}

					if (!g_gui.SelectBrush(brush)) {
						g_gui.SetStatusText("Brush \"" + wxstr(name) + "\" is not in any palette");
						return;
					}

					g_gui.SetStatusText("Used hotkey " + i2ws(index));
					g_gui.RefreshView();
				} else {
					g_gui.SetStatusText("Unassigned hotkey " + i2ws(index));
				}
			}
			break;
		}
		case 'd':
		case 'D': {
			keyCode = WXK_CONTROL_D;
			break;
		}
		case 'b':
		case 'B': {
			g_gui.SelectPreviousBrush();
			break;
		}
		default: {
			event.Skip();
			break;
		}
	}
}

#ifdef __LINUX__
bool MapCanvas::DispatchMenuShortcut(wxKeyEvent &event) {
	int key = event.GetKeyCode();
	if (key == WXK_NONE || key == 0) {
		key = static_cast<int>(event.GetUnicodeKey());
	}
	bool ctrl = event.ControlDown();
	bool shift = event.ShiftDown();

	if (key >= 'a' && key <= 'z') {
		key = key - 'a' + 'A';
	}
	if (key >= 1 && key <= 26) {
		key = key + 'A' - 1;
		ctrl = true;
	}

	int menuId = -1;

	if (ctrl && shift) {
		switch (key) {
			case 'Z':
				menuId = static_cast<int>(MenuBar::REDO);
				break;
			case 'F':
				menuId = static_cast<int>(MenuBar::REPLACE_ITEMS);
				break;
		}
	} else if (ctrl) {
		switch (key) {
			case 'Z':
				menuId = static_cast<int>(MenuBar::UNDO);
				break;
			case 'F':
				menuId = static_cast<int>(MenuBar::FIND_ITEM);
				break;
			case 'B':
				menuId = static_cast<int>(MenuBar::BORDERIZE_SELECTION);
				break;
			case 'G':
				menuId = static_cast<int>(MenuBar::GOTO_POSITION);
				break;
			case 'X':
				menuId = static_cast<int>(MenuBar::CUT);
				break;
			case 'C':
				menuId = static_cast<int>(MenuBar::COPY);
				break;
			case 'V':
				menuId = static_cast<int>(MenuBar::PASTE);
				break;
			case '=':
				menuId = static_cast<int>(MenuBar::ZOOM_IN);
				break;
			case '-':
				menuId = static_cast<int>(MenuBar::ZOOM_OUT);
				break;
			case '0':
				menuId = static_cast<int>(MenuBar::ZOOM_NORMAL);
				break;
			case 'W':
				menuId = static_cast<int>(MenuBar::SHOW_ALL_FLOORS);
				break;
			case 'L':
				menuId = static_cast<int>(MenuBar::GHOST_HIGHER_FLOORS);
				break;
			case 'E':
				menuId = static_cast<int>(MenuBar::SHOW_ONLY_COLORS);
				break;
			case 'M':
				menuId = static_cast<int>(MenuBar::SHOW_ONLY_MODIFIED);
				break;
			case 'H':
				menuId = static_cast<int>(MenuBar::SHOW_HOUSES);
				break;
		}
	} else if (shift) {
		switch (key) {
			case 'I':
				menuId = static_cast<int>(MenuBar::SHOW_INGAME_BOX);
				break;
			case 'L':
				menuId = static_cast<int>(MenuBar::SHOW_LIGHTS);
				break;
			case 'K':
				menuId = static_cast<int>(MenuBar::SHOW_LIGHT_STRENGTH);
				break;
			case 'G':
				menuId = static_cast<int>(MenuBar::SHOW_GRID);
				break;
			case 'E':
				menuId = static_cast<int>(MenuBar::SHOW_AS_MINIMAP);
				break;
			case 'N':
				menuId = static_cast<int>(MenuBar::SHOW_NPCS);
				break;
		}
	} else {
		switch (key) {
			case 'A':
				menuId = static_cast<int>(MenuBar::AUTOMAGIC);
				break;
			case 'P':
				menuId = static_cast<int>(MenuBar::GOTO_PREVIOUS_POSITION);
				break;
			case 'J':
				menuId = static_cast<int>(MenuBar::JUMP_TO_BRUSH);
				break;
			case 'V':
				menuId = static_cast<int>(MenuBar::HIGHLIGHT_ITEMS);
				break;
			case 'F':
				menuId = static_cast<int>(MenuBar::SHOW_MONSTERS);
				break;
			case 'S':
				menuId = static_cast<int>(MenuBar::SHOW_SPAWNS_MONSTER);
				break;
			case 'U':
				menuId = static_cast<int>(MenuBar::SHOW_SPAWNS_NPC);
				break;
			case 'E':
				menuId = static_cast<int>(MenuBar::SHOW_SPECIAL);
				break;
			case 'O':
				menuId = static_cast<int>(MenuBar::SHOW_PATHING);
				break;
			case 'Y':
				menuId = static_cast<int>(MenuBar::SHOW_TOOLTIPS);
				break;
			case 'L':
				menuId = static_cast<int>(MenuBar::SHOW_PREVIEW);
				break;
			case 'K':
				menuId = static_cast<int>(MenuBar::SHOW_WALL_HOOKS);
				break;
			case 'M':
				menuId = static_cast<int>(MenuBar::WIN_MINIMAP);
				break;
			case 'T':
				menuId = static_cast<int>(MenuBar::SELECT_TERRAIN);
				break;
			case 'I':
				menuId = static_cast<int>(MenuBar::SELECT_ITEM);
				break;
			case 'H':
				menuId = static_cast<int>(MenuBar::SELECT_HOUSE);
				break;
			case 'C':
				menuId = static_cast<int>(MenuBar::SELECT_MONSTER);
				break;
			case 'N':
				menuId = static_cast<int>(MenuBar::SELECT_NPC);
				break;
			case 'W':
				menuId = static_cast<int>(MenuBar::SELECT_WAYPOINT);
				break;
			case 'R':
				menuId = static_cast<int>(MenuBar::SELECT_RAW);
				break;
		}
	}

	if (menuId >= 0) {
		int fullId = static_cast<int>(MAIN_FRAME_MENU) + menuId;

		wxMenuBar* mb = g_gui.root->GetMenuBar();
		if (mb) {
			wxMenuItem* item = mb->FindItem(fullId);
			if (item && item->IsCheckable()) {
				item->Check(!item->IsChecked());
			}
		}

		wxCommandEvent evt(wxEVT_COMMAND_MENU_SELECTED, fullId);
		g_gui.root->GetEventHandler()->ProcessEvent(evt);
		return true;
	}
	return false;
}
#endif

void MapCanvas::OnKeyUp(wxKeyEvent &event) {
	keyCode = WXK_NONE;
}
