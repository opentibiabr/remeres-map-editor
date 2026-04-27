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

BEGIN_EVENT_TABLE(MapCanvas, wxGLCanvas)
EVT_KEY_DOWN(MapCanvas::OnKeyDown)
EVT_KEY_DOWN(MapCanvas::OnKeyUp)

// Mouse events
EVT_MOTION(MapCanvas::OnMouseMove)
EVT_LEFT_UP(MapCanvas::OnMouseLeftRelease)
EVT_LEFT_DOWN(MapCanvas::OnMouseLeftClick)
EVT_LEFT_DCLICK(MapCanvas::OnMouseLeftDoubleClick)
EVT_MIDDLE_DOWN(MapCanvas::OnMouseCenterClick)
EVT_MIDDLE_UP(MapCanvas::OnMouseCenterRelease)
EVT_RIGHT_DOWN(MapCanvas::OnMouseRightClick)
EVT_RIGHT_UP(MapCanvas::OnMouseRightRelease)
EVT_MOUSEWHEEL(MapCanvas::OnWheel)
EVT_ENTER_WINDOW(MapCanvas::OnGainMouse)
EVT_LEAVE_WINDOW(MapCanvas::OnLoseMouse)

// Drawing events
EVT_PAINT(MapCanvas::OnPaint)
EVT_ERASE_BACKGROUND(MapCanvas::OnEraseBackground)

// Menu events
EVT_MENU(MAP_POPUP_MENU_CUT, MapCanvas::OnCut)
EVT_MENU(MAP_POPUP_MENU_COPY, MapCanvas::OnCopy)
EVT_MENU(MAP_POPUP_MENU_COPY_POSITION, MapCanvas::OnCopyPosition)
EVT_MENU(MAP_POPUP_MENU_PASTE, MapCanvas::OnPaste)
EVT_MENU(MAP_POPUP_MENU_DELETE, MapCanvas::OnDelete)
//----
EVT_MENU(MAP_POPUP_MENU_COPY_ITEM_ID, MapCanvas::OnCopyItemId)
EVT_MENU(MAP_POPUP_MENU_COPY_NAME, MapCanvas::OnCopyName)
// ----
EVT_MENU(MAP_POPUP_MENU_ROTATE, MapCanvas::OnRotateItem)
EVT_MENU(MAP_POPUP_MENU_GOTO, MapCanvas::OnGotoDestination)
EVT_MENU(MAP_POPUP_MENU_COPY_DESTINATION, MapCanvas::OnCopyDestination)
EVT_MENU(MAP_POPUP_MENU_SWITCH_DOOR, MapCanvas::OnSwitchDoor)
// ----
EVT_MENU(MAP_POPUP_MENU_SELECT_RAW_BRUSH, MapCanvas::OnSelectRAWBrush)
EVT_MENU(MAP_POPUP_MENU_SELECT_GROUND_BRUSH, MapCanvas::OnSelectGroundBrush)
EVT_MENU(MAP_POPUP_MENU_SELECT_DOODAD_BRUSH, MapCanvas::OnSelectDoodadBrush)
EVT_MENU(MAP_POPUP_MENU_SELECT_DOOR_BRUSH, MapCanvas::OnSelectDoorBrush)
EVT_MENU(MAP_POPUP_MENU_SELECT_WALL_BRUSH, MapCanvas::OnSelectWallBrush)
EVT_MENU(MAP_POPUP_MENU_SELECT_CARPET_BRUSH, MapCanvas::OnSelectCarpetBrush)
EVT_MENU(MAP_POPUP_MENU_SELECT_TABLE_BRUSH, MapCanvas::OnSelectTableBrush)
EVT_MENU(MAP_POPUP_MENU_SELECT_MONSTER_BRUSH, MapCanvas::OnSelectMonsterBrush)
EVT_MENU(MAP_POPUP_MENU_SELECT_SPAWN_BRUSH, MapCanvas::OnSelectSpawnBrush)
EVT_MENU(MAP_POPUP_MENU_SELECT_NPC_BRUSH, MapCanvas::OnSelectNpcBrush)
EVT_MENU(MAP_POPUP_MENU_SELECT_SPAWN_NPC_BRUSH, MapCanvas::OnSelectSpawnNpcBrush)
EVT_MENU(MAP_POPUP_MENU_SELECT_HOUSE_BRUSH, MapCanvas::OnSelectHouseBrush)
EVT_MENU(MAP_POPUP_MENU_MOVE_TO_TILESET, MapCanvas::OnSelectMoveTo)
// ----
EVT_MENU(MAP_POPUP_MENU_PROPERTIES, MapCanvas::OnProperties)
// ----
EVT_MENU(MAP_POPUP_MENU_BROWSE_TILE, MapCanvas::OnBrowseTile)
END_EVENT_TABLE()

bool MapCanvas::processed[] = { 0 };

MapCanvas::MapCanvas(MapWindow* parent, Editor &editor, int* attriblist) :
	wxGLCanvas(parent, wxID_ANY, attriblist, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS),
	editor(editor),
	floor(rme::MapGroundLayer),
	zoom(1.0),
	cursor_x(-1),
	cursor_y(-1),
	dragging(false),
	boundbox_selection(false),
	screendragging(false),
	drawing(false),
	dragging_draw(false),
	replace_dragging(false),

	screenshot_buffer(nullptr),

	drag_start_x(-1),
	drag_start_y(-1),
	drag_start_z(-1),

	last_cursor_map_x(-1),
	last_cursor_map_y(-1),
	last_cursor_map_z(-1),

	last_click_map_x(-1),
	last_click_map_y(-1),
	last_click_map_z(-1),
	last_click_abs_x(-1),
	last_click_abs_y(-1),
	last_click_x(-1),
	last_click_y(-1),

	last_mmb_click_x(-1),
	last_mmb_click_y(-1) {
	popup_menu = newd MapPopupMenu(editor);
	animation_timer = newd AnimationTimer(this);
	drawer = new MapDrawer(this);
	keyCode = WXK_NONE;
}

MapCanvas::~MapCanvas() {
	delete popup_menu;
	delete animation_timer;
	delete drawer;
	free(screenshot_buffer);
}

void MapCanvas::Refresh() {
	drawer->markDirty();
	if (refresh_watch.Time() > g_settings.getInteger(Config::HARD_REFRESH_RATE)) {
		refresh_watch.Start();
		wxGLCanvas::Update();
	}
	wxGLCanvas::Refresh();
}

void MapCanvas::SetZoom(double value) {
	if (value < 0.125) {
		value = 0.125;
	}

	if (value > 25.00) {
		value = 25.0;
	}

	if (zoom != value) {
		int center_x, center_y;
		GetScreenCenter(&center_x, &center_y);

		zoom = value;
		GetMapWindow()->SetScreenCenterPosition(Position(center_x, center_y, floor));

		UpdatePositionStatus();
		UpdateZoomStatus();
		Refresh();
	}
}

void MapCanvas::GetViewBox(int* view_scroll_x, int* view_scroll_y, int* screensize_x, int* screensize_y) const {
	MapWindow* window = GetMapWindow();
	window->GetViewSize(screensize_x, screensize_y);
	window->GetViewStart(view_scroll_x, view_scroll_y);
}

void MapCanvas::OnPaint(wxPaintEvent &event) {
	if (!IsShownOnScreen()) {
		return;
	}
	SetCurrent(*g_gui.GetGLContext(this));

	if (g_gui.IsRenderingEnabled()) {
		DrawingOptions &options = drawer->getOptions();
		if (screenshot_buffer) {
			options.SetIngame();
		} else {
			options.transparent_floors = g_settings.getBoolean(Config::TRANSPARENT_FLOORS);
			options.transparent_items = g_settings.getBoolean(Config::TRANSPARENT_ITEMS);
			options.show_ingame_box = g_settings.getBoolean(Config::SHOW_INGAME_BOX);
			options.show_lights = g_settings.getBoolean(Config::SHOW_LIGHTS);
			options.show_light_strength = g_settings.getBoolean(Config::SHOW_LIGHT_STRENGTH);
			options.show_grid = g_settings.getInteger(Config::SHOW_GRID);
			options.ingame = !g_settings.getBoolean(Config::SHOW_EXTRA);
			options.show_all_floors = g_settings.getBoolean(Config::SHOW_ALL_FLOORS);
			options.show_monsters = g_settings.getBoolean(Config::SHOW_MONSTERS);
			options.show_spawns_monster = g_settings.getBoolean(Config::SHOW_SPAWNS_MONSTER);
			options.show_npcs = g_settings.getBoolean(Config::SHOW_NPCS);
			options.show_spawns_npc = g_settings.getBoolean(Config::SHOW_SPAWNS_NPC);
			options.show_houses = g_settings.getBoolean(Config::SHOW_HOUSES);
			options.show_shade = g_settings.getBoolean(Config::SHOW_SHADE);
			options.show_special_tiles = g_settings.getBoolean(Config::SHOW_SPECIAL_TILES);
			options.show_items = g_settings.getBoolean(Config::SHOW_ITEMS);
			options.highlight_items = g_settings.getBoolean(Config::HIGHLIGHT_ITEMS);
			options.show_blocking = g_settings.getBoolean(Config::SHOW_BLOCKING);
			options.show_tooltips = g_settings.getBoolean(Config::SHOW_TOOLTIPS);
			options.show_performance_stats = g_settings.getBoolean(Config::SHOW_PERFORMANCE_STATS);
			options.show_as_minimap = g_settings.getBoolean(Config::SHOW_AS_MINIMAP);
			options.show_only_colors = g_settings.getBoolean(Config::SHOW_ONLY_TILEFLAGS);
			options.show_only_modified = g_settings.getBoolean(Config::SHOW_ONLY_MODIFIED_TILES);
			options.show_preview = g_settings.getBoolean(Config::SHOW_PREVIEW);
			options.show_hooks = g_settings.getBoolean(Config::SHOW_WALL_HOOKS);
			options.show_pickupables = g_settings.getBoolean(Config::SHOW_PICKUPABLES);
			options.show_moveables = g_settings.getBoolean(Config::SHOW_MOVEABLES);
			options.show_avoidables = g_settings.getBoolean(Config::SHOW_AVOIDABLES);
			options.hide_items_when_zoomed = g_settings.getBoolean(Config::HIDE_ITEMS_WHEN_ZOOMED);
		}

		options.dragging = boundbox_selection;

		if (options.show_preview || drawer->GetPositionIndicatorTime() != 0 || options.show_performance_stats) {
			animation_timer->Start();
		} else {
			animation_timer->Stop();
		}

		drawer->SetupVars();
		drawer->SetupGL();
		drawer->Draw();

		if (screenshot_buffer) {
			drawer->TakeScreenshot(screenshot_buffer);
		}

		drawer->Release();
	}

	// Clean unused textures
	g_gui.gfx.garbageCollection();

	// Swap buffer
	SwapBuffers();

	// Send newd node requests
	editor.SendNodeRequests();
}

void MapCanvas::ShowPositionIndicator(const Position &position) {
	if (drawer) {
		drawer->ShowPositionIndicator(position);
		if (!animation_timer->IsRunning()) {
			Update();
		}
	}
}

void MapCanvas::TakeScreenshot(wxFileName path, wxString format) {
	int screensize_x, screensize_y;
	GetViewBox(&view_scroll_x, &view_scroll_y, &screensize_x, &screensize_y);

	delete[] screenshot_buffer;
	screenshot_buffer = newd uint8_t[3 * screensize_x * screensize_y];

	// Draw the window
	Refresh();
	wxGLCanvas::Update(); // Forces immediate redraws the window.

	// screenshot_buffer should now contain the screenbuffer
	if (screenshot_buffer == nullptr) {
		g_gui.PopupDialog("Capture failed", "Image capture failed. Old Video Driver?", wxOK);
	} else {
		// We got the shit
		int screensize_x, screensize_y;
		GetMapWindow()->GetViewSize(&screensize_x, &screensize_y);
		wxImage screenshot(screensize_x, screensize_y, screenshot_buffer);

		time_t t = time(nullptr);
		struct tm current_time;
#ifdef _WIN32
		localtime_s(&current_time, &t);
#else
		localtime_r(&t, &current_time);
#endif

		wxString date;
		date << "screenshot_";
		date << (1900 + current_time.tm_year);
		if (current_time.tm_mon < 9) {
			date << "-0" << (current_time.tm_mon + 1);
		} else {
			date << "-" << (current_time.tm_mon + 1);
		}
		date << "-" << current_time.tm_mday;
		date << "-" << current_time.tm_hour;
		date << "-" << current_time.tm_min;
		date << "-" << current_time.tm_sec;

		ASSERT(&current_time != nullptr);

		int type = 0;
		path.SetName(date);
		if (format == "bmp") {
			path.SetExt(format);
			type = wxBITMAP_TYPE_BMP;
		} else if (format == "png") {
			path.SetExt(format);
			type = wxBITMAP_TYPE_PNG;
		} else if (format == "jpg" || format == "jpeg") {
			path.SetExt(format);
			type = wxBITMAP_TYPE_JPEG;
		} else if (format == "tga") {
			path.SetExt(format);
			type = wxBITMAP_TYPE_TGA;
		} else {
			g_gui.SetStatusText("Unknown screenshot format \'" + format + "\", switching to default (png)");
			path.SetExt("png");
			;
			type = wxBITMAP_TYPE_PNG;
		}

		path.Mkdir(0755, wxPATH_MKDIR_FULL);
		wxFileOutputStream of(path.GetFullPath());
		if (of.IsOk()) {
			if (screenshot.SaveFile(of, static_cast<wxBitmapType>(type))) {
				g_gui.SetStatusText("Took screenshot and saved as " + path.GetFullName());
			} else {
				g_gui.PopupDialog("File error", "Couldn't save image file correctly.", wxOK);
			}
		} else {
			g_gui.PopupDialog("File error", "Couldn't open file " + path.GetFullPath() + " for writing.", wxOK);
		}
	}

	Refresh();

	screenshot_buffer = nullptr;
}

void MapCanvas::ScreenToMap(int screen_x, int screen_y, int* map_x, int* map_y) {
	int start_x, start_y;
	GetMapWindow()->GetViewStart(&start_x, &start_y);

	screen_x *= GetContentScaleFactor();
	screen_y *= GetContentScaleFactor();

	if (screen_x < 0) {
		*map_x = (start_x + screen_x) / rme::TileSize;
	} else {
		*map_x = int(start_x + (screen_x * zoom)) / rme::TileSize;
	}

	if (screen_y < 0) {
		*map_y = (start_y + screen_y) / rme::TileSize;
	} else {
		*map_y = int(start_y + (screen_y * zoom)) / rme::TileSize;
	}

	if (floor <= rme::MapGroundLayer) {
		*map_x += rme::MapGroundLayer - floor;
		*map_y += rme::MapGroundLayer - floor;
	} /* else {
		 *map_x += rme::MapMaxLayer - floor;
		 *map_y += rme::MapMaxLayer - floor;
	 }*/
}

MapWindow* MapCanvas::GetMapWindow() const {
	wxWindow* window = GetParent();
	if (window) {
		return static_cast<MapWindow*>(window);
	}
	return nullptr;
}

void MapCanvas::GetScreenCenter(int* map_x, int* map_y) {
	int width, height;
	GetMapWindow()->GetViewSize(&width, &height);
	return ScreenToMap(width / 2, height / 2, map_x, map_y);
}

Position MapCanvas::GetCursorPosition() const {
	return Position(last_cursor_map_x, last_cursor_map_y, floor);
}

void MapCanvas::UpdatePositionStatus(int x, int y) {

	x == -1 ? cursor_x : x;
	y == -1 ? cursor_y : y;

	int map_x, map_y;
	ScreenToMap(x, y, &map_x, &map_y);

	g_gui.root->SetStatusText(fmt::format("x: {} y: {} z: {}", map_x, map_y, floor), 2);

	const auto tile = editor.getMap().getTile(map_x, map_y, floor);

	std::string description = "Nothing";

	if (editor.IsLive()) {
		editor.GetLive().updateCursor(Position(map_x, map_y, floor));
	}

	if (!tile) {
		g_gui.root->SetStatusText(description, 1);
		return;
	}

	description.clear();
	if (tile->spawnMonster && g_settings.getInteger(Config::SHOW_SPAWNS_MONSTER)) {
		description = fmt::format("Monster spawn radius: {}", tile->spawnMonster->getSize());
	} else if (!tile->monsters.empty() && g_settings.getInteger(Config::SHOW_MONSTERS)) {
		std::vector<std::string> texts;
		for (const auto monster : tile->monsters) {
			const auto monsterWeight = tile->monsters.size() > 1 ? std::to_string(monster->getWeight()) : "0";
			texts.emplace_back(fmt::format("Monster \"{}\", spawntime: {}, weight: {}", monster->getName(), monster->getSpawnMonsterTime(), monsterWeight));
		}
		description = fmt::format("{}", fmt::join(texts, " - "));
	} else if (tile->spawnNpc && g_settings.getInteger(Config::SHOW_SPAWNS_NPC)) {
		description = fmt::format("Npc spawn radius: {}", tile->spawnNpc->getSize());
	} else if (tile->npc && g_settings.getInteger(Config::SHOW_NPCS)) {
		description = fmt::format("NPC \"{}\", spawntime: {}", tile->npc->getName(), tile->npc->getSpawnNpcTime());
	} else if (const auto item = tile->getTopItem()) {
		description = fmt::format("Item \"{}\", id: {}", item->getName(), item->getID());

		description = item->getUniqueID() ? fmt::format("{}, uid: {}", description, item->getUniqueID()) : description;

		description = item->getActionID() ? fmt::format("{}, aid: {}", description, item->getActionID()) : description;

		description = item->hasWeight() ? fmt::format("{}, weight: {:.2f}", description, item->getWeight()) : description;
	} else {
		description = "Nothing";
	}

	g_gui.root->SetStatusText(description, 1);
}

void MapCanvas::UpdateZoomStatus() {
	int percentage = (int)((1.0 / zoom) * 100);
	wxString ss;
	ss << "zoom: " << percentage << "%";
	g_gui.root->SetStatusText(ss, 3);
}

void MapCanvas::ChangeFloor(int new_floor) {
	ASSERT(new_floor >= 0 || new_floor <= rme::MapMaxLayer);
	int old_floor = floor;
	floor = new_floor;
	if (old_floor != new_floor) {
		UpdatePositionStatus();
		g_gui.root->UpdateFloorMenu();
		g_gui.UpdateMinimap(true);
	}
	Refresh();
}

void MapCanvas::EnterDrawingMode() {
	dragging = false;
	boundbox_selection = false;
	EndPasting();
	Refresh();
}

void MapCanvas::EnterSelectionMode() {
	drawing = false;
	dragging_draw = false;
	replace_dragging = false;
	editor.replace_brush = nullptr;
	Refresh();
}

bool MapCanvas::isPasting() const {
	return g_gui.IsPasting();
}

void MapCanvas::StartPasting() {
	g_gui.StartPasting();
}

void MapCanvas::EndPasting() {
	g_gui.EndPasting();
}

void MapCanvas::Reset() {
	cursor_x = 0;
	cursor_y = 0;

	zoom = 1.0;
	floor = rme::MapGroundLayer;

	dragging = false;
	boundbox_selection = false;
	screendragging = false;
	drawing = false;
	dragging_draw = false;

	replace_dragging = false;
	editor.replace_brush = nullptr;

	drag_start_x = -1;
	drag_start_y = -1;
	drag_start_z = -1;

	last_click_map_x = -1;
	last_click_map_y = -1;
	last_click_map_z = -1;

	last_mmb_click_x = -1;
	last_mmb_click_y = -1;

	editor.getSelection().clear();
	editor.clearActions();
}

void MapCanvas::getTilesToDraw(int mouse_map_x, int mouse_map_y, int floor, PositionVector* tilestodraw, PositionVector* tilestoborder, bool fill /*= false*/) {
	if (fill) {
		Brush* brush = g_gui.GetCurrentBrush();
		if (!brush || !brush->isGround()) {
			return;
		}

		GroundBrush* newBrush = brush->asGround();
		Position position(mouse_map_x, mouse_map_y, floor);

		Tile* tile = editor.getMap().getTile(position);
		GroundBrush* oldBrush = nullptr;
		if (tile) {
			oldBrush = tile->getGroundBrush();
		}

		if (oldBrush && oldBrush->getID() == newBrush->getID()) {
			return;
		}

		if ((tile && tile->ground && !oldBrush) || (!tile && oldBrush)) {
			return;
		}

		if (tile && oldBrush) {
			GroundBrush* groundBrush = tile->getGroundBrush();
			if (!groundBrush || groundBrush->getID() != oldBrush->getID()) {
				return;
			}
		}

		std::fill(std::begin(processed), std::end(processed), false);
		floodFill(&editor.getMap(), position, BLOCK_SIZE / 2, BLOCK_SIZE / 2, oldBrush, tilestodraw);

	} else {
		for (int y = -g_gui.GetBrushSize() - 1; y <= g_gui.GetBrushSize() + 1; y++) {
			for (int x = -g_gui.GetBrushSize() - 1; x <= g_gui.GetBrushSize() + 1; x++) {
				if (g_gui.GetBrushShape() == BRUSHSHAPE_SQUARE) {
					if (x >= -g_gui.GetBrushSize() && x <= g_gui.GetBrushSize() && y >= -g_gui.GetBrushSize() && y <= g_gui.GetBrushSize()) {
						if (tilestodraw) {
							tilestodraw->push_back(Position(mouse_map_x + x, mouse_map_y + y, floor));
						}
					}
					if (std::abs(x) - g_gui.GetBrushSize() < 2 && std::abs(y) - g_gui.GetBrushSize() < 2) {
						if (tilestoborder) {
							tilestoborder->push_back(Position(mouse_map_x + x, mouse_map_y + y, floor));
						}
					}
				} else if (g_gui.GetBrushShape() == BRUSHSHAPE_CIRCLE) {
					double distance = sqrt(double(x * x) + double(y * y));
					if (distance < g_gui.GetBrushSize() + 0.005) {
						if (tilestodraw) {
							tilestodraw->push_back(Position(mouse_map_x + x, mouse_map_y + y, floor));
						}
					}
					if (std::abs(distance - g_gui.GetBrushSize()) < 1.5) {
						if (tilestoborder) {
							tilestoborder->push_back(Position(mouse_map_x + x, mouse_map_y + y, floor));
						}
					}
				}
			}
		}
	}
}

bool MapCanvas::floodFill(Map* map, const Position &center, int x, int y, GroundBrush* brush, PositionVector* positions) {
	if (x < 0 || y < 0 || x > BLOCK_SIZE || y > BLOCK_SIZE) {
		return false;
	}

	processed[getFillIndex(x, y)] = true;

	int px = (center.x + x) - (BLOCK_SIZE / 2);
	int py = (center.y + y) - (BLOCK_SIZE / 2);
	if (px <= 0 || py <= 0 || px >= map->getWidth() || py >= map->getHeight()) {
		return false;
	}

	Tile* tile = map->getTile(px, py, center.z);
	if ((tile && tile->ground && !brush) || (!tile && brush)) {
		return false;
	}

	if (tile && brush) {
		GroundBrush* groundBrush = tile->getGroundBrush();
		if (!groundBrush || groundBrush->getID() != brush->getID()) {
			return false;
		}
	}

	positions->push_back(Position(px, py, center.z));

	bool deny = false;
	if (!processed[getFillIndex(x - 1, y)]) {
		deny = floodFill(map, center, x - 1, y, brush, positions);
	}

	if (!deny && !processed[getFillIndex(x, y - 1)]) {
		deny = floodFill(map, center, x, y - 1, brush, positions);
	}

	if (!deny && !processed[getFillIndex(x + 1, y)]) {
		deny = floodFill(map, center, x + 1, y, brush, positions);
	}

	if (!deny && !processed[getFillIndex(x, y + 1)]) {
		deny = floodFill(map, center, x, y + 1, brush, positions);
	}

	return deny;
}

// ============================================================================
// AnimationTimer

AnimationTimer::AnimationTimer(MapCanvas* canvas) :
	wxTimer(),
	map_canvas(canvas),
	started(false) {
		////
	};

void AnimationTimer::Notify() {
	map_canvas->Refresh();
}

void AnimationTimer::Start() {
	if (!started) {
		started = true;
		wxTimer::Start(16);
	}
};

void AnimationTimer::Stop() {
	if (started) {
		started = false;
		wxTimer::Stop();
	}
};
