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

#include "main_menubar.h"
#include "application.h"
#include "preferences.h"
#include "about_window.h"
#include "minimap_window.h"
#include "dat_debug_view.h"
#include "result_window.h"
#include "extension_window.h"
#include "find_item_window.h"
#include "settings.h"

#include "gui.h"

#include <wx/chartype.h>

#include "items.h"
#include "editor.h"
#include "materials.h"
#include "live_client.h"
#include "live_server.h"

BEGIN_EVENT_TABLE(MainMenuBar, wxEvtHandler)
END_EVENT_TABLE()

MainMenuBar::MainMenuBar(MainFrame* frame) :
	frame(frame) {
	using namespace MenuBar;
	checking_programmaticly = false;

#define MAKE_ACTION(id, kind, handler) actions[#id] = new MenuBar::Action(#id, id, kind, wxCommandEventFunction(&MainMenuBar::handler))
#define MAKE_SET_ACTION(id, kind, setting_, handler)                                                  \
	actions[#id] = new MenuBar::Action(#id, id, kind, wxCommandEventFunction(&MainMenuBar::handler)); \
	actions[#id].setting = setting_

	MAKE_ACTION(NEW, wxITEM_NORMAL, OnNew);
	MAKE_ACTION(OPEN, wxITEM_NORMAL, OnOpen);
	MAKE_ACTION(SAVE, wxITEM_NORMAL, OnSave);
	MAKE_ACTION(SAVE_AS, wxITEM_NORMAL, OnSaveAs);
	MAKE_ACTION(GENERATE_MAP, wxITEM_NORMAL, OnGenerateMap);
	MAKE_ACTION(CLOSE, wxITEM_NORMAL, OnClose);

	MAKE_ACTION(IMPORT_MAP, wxITEM_NORMAL, OnImportMap);
	MAKE_ACTION(IMPORT_MONSTERS, wxITEM_NORMAL, OnImportMonsterData);
	MAKE_ACTION(IMPORT_NPCS, wxITEM_NORMAL, OnImportNpcData);
	MAKE_ACTION(IMPORT_MINIMAP, wxITEM_NORMAL, OnImportMinimap);
	MAKE_ACTION(EXPORT_MINIMAP, wxITEM_NORMAL, OnExportMinimap);
	MAKE_ACTION(EXPORT_TILESETS, wxITEM_NORMAL, OnExportTilesets);

	MAKE_ACTION(RELOAD_DATA, wxITEM_NORMAL, OnReloadDataFiles);
	// MAKE_ACTION(RECENT_FILES, wxITEM_NORMAL, OnRecent);
	MAKE_ACTION(PREFERENCES, wxITEM_NORMAL, OnPreferences);
	MAKE_ACTION(EXIT, wxITEM_NORMAL, OnQuit);

	MAKE_ACTION(UNDO, wxITEM_NORMAL, OnUndo);
	MAKE_ACTION(REDO, wxITEM_NORMAL, OnRedo);

	MAKE_ACTION(FIND_ITEM, wxITEM_NORMAL, OnSearchForItem);
	MAKE_ACTION(REPLACE_ITEMS, wxITEM_NORMAL, OnReplaceItems);
	MAKE_ACTION(SEARCH_ON_MAP_EVERYTHING, wxITEM_NORMAL, OnSearchForStuffOnMap);
	MAKE_ACTION(SEARCH_ON_MAP_UNIQUE, wxITEM_NORMAL, OnSearchForUniqueOnMap);
	MAKE_ACTION(SEARCH_ON_MAP_ACTION, wxITEM_NORMAL, OnSearchForActionOnMap);
	MAKE_ACTION(SEARCH_ON_MAP_CONTAINER, wxITEM_NORMAL, OnSearchForContainerOnMap);
	MAKE_ACTION(SEARCH_ON_MAP_WRITEABLE, wxITEM_NORMAL, OnSearchForWriteableOnMap);
	MAKE_ACTION(SEARCH_ON_SELECTION_EVERYTHING, wxITEM_NORMAL, OnSearchForStuffOnSelection);
	MAKE_ACTION(SEARCH_ON_SELECTION_UNIQUE, wxITEM_NORMAL, OnSearchForUniqueOnSelection);
	MAKE_ACTION(SEARCH_ON_SELECTION_ACTION, wxITEM_NORMAL, OnSearchForActionOnSelection);
	MAKE_ACTION(SEARCH_ON_SELECTION_CONTAINER, wxITEM_NORMAL, OnSearchForContainerOnSelection);
	MAKE_ACTION(SEARCH_ON_SELECTION_WRITEABLE, wxITEM_NORMAL, OnSearchForWriteableOnSelection);
	MAKE_ACTION(SEARCH_ON_SELECTION_ITEM, wxITEM_NORMAL, OnSearchForItemOnSelection);
	MAKE_ACTION(REPLACE_ON_SELECTION_ITEMS, wxITEM_NORMAL, OnReplaceItemsOnSelection);
	MAKE_ACTION(REMOVE_ON_SELECTION_ITEM, wxITEM_NORMAL, OnRemoveItemOnSelection);
	MAKE_ACTION(REMOVE_ON_SELECTION_MONSTER, wxITEM_NORMAL, OnRemoveMonstersOnSelection);
	MAKE_ACTION(SELECT_MODE_COMPENSATE, wxITEM_RADIO, OnSelectionTypeChange);
	MAKE_ACTION(SELECT_MODE_LOWER, wxITEM_RADIO, OnSelectionTypeChange);
	MAKE_ACTION(SELECT_MODE_CURRENT, wxITEM_RADIO, OnSelectionTypeChange);
	MAKE_ACTION(SELECT_MODE_VISIBLE, wxITEM_RADIO, OnSelectionTypeChange);

	MAKE_ACTION(AUTOMAGIC, wxITEM_CHECK, OnToggleAutomagic);
	MAKE_ACTION(BORDERIZE_SELECTION, wxITEM_NORMAL, OnBorderizeSelection);
	MAKE_ACTION(BORDERIZE_MAP, wxITEM_NORMAL, OnBorderizeMap);
	MAKE_ACTION(RANDOMIZE_SELECTION, wxITEM_NORMAL, OnRandomizeSelection);
	MAKE_ACTION(RANDOMIZE_MAP, wxITEM_NORMAL, OnRandomizeMap);
	MAKE_ACTION(GOTO_PREVIOUS_POSITION, wxITEM_NORMAL, OnGotoPreviousPosition);
	MAKE_ACTION(GOTO_POSITION, wxITEM_NORMAL, OnGotoPosition);
	MAKE_ACTION(JUMP_TO_BRUSH, wxITEM_NORMAL, OnJumpToBrush);
	MAKE_ACTION(JUMP_TO_ITEM_BRUSH, wxITEM_NORMAL, OnJumpToItemBrush);

	MAKE_ACTION(CUT, wxITEM_NORMAL, OnCut);
	MAKE_ACTION(COPY, wxITEM_NORMAL, OnCopy);
	MAKE_ACTION(PASTE, wxITEM_NORMAL, OnPaste);

	MAKE_ACTION(EDIT_TOWNS, wxITEM_NORMAL, OnMapEditTowns);
	MAKE_ACTION(EDIT_ITEMS, wxITEM_NORMAL, OnMapEditItems);
	MAKE_ACTION(EDIT_MONSTERS, wxITEM_NORMAL, OnMapEditMonsters);

	MAKE_ACTION(CLEAR_INVALID_HOUSES, wxITEM_NORMAL, OnClearHouseTiles);
	MAKE_ACTION(CLEAR_MODIFIED_STATE, wxITEM_NORMAL, OnClearModifiedState);
	MAKE_ACTION(MAP_REMOVE_ITEMS, wxITEM_NORMAL, OnMapRemoveItems);
	MAKE_ACTION(MAP_REMOVE_CORPSES, wxITEM_NORMAL, OnMapRemoveCorpses);
	MAKE_ACTION(MAP_REMOVE_UNREACHABLE_TILES, wxITEM_NORMAL, OnMapRemoveUnreachable);
	MAKE_ACTION(MAP_REMOVE_EMPTY_MONSTERS_SPAWNS, wxITEM_NORMAL, OnMapRemoveEmptyMonsterSpawns);
	MAKE_ACTION(MAP_REMOVE_EMPTY_NPCS_SPAWNS, wxITEM_NORMAL, OnMapRemoveEmptyNpcSpawns);
	MAKE_ACTION(MAP_CLEANUP, wxITEM_NORMAL, OnMapCleanup);
	MAKE_ACTION(MAP_CLEAN_HOUSE_ITEMS, wxITEM_NORMAL, OnMapCleanHouseItems);
	MAKE_ACTION(MAP_PROPERTIES, wxITEM_NORMAL, OnMapProperties);
	MAKE_ACTION(MAP_STATISTICS, wxITEM_NORMAL, OnMapStatistics);

	MAKE_ACTION(VIEW_TOOLBARS_BRUSHES, wxITEM_CHECK, OnToolbars);
	MAKE_ACTION(VIEW_TOOLBARS_POSITION, wxITEM_CHECK, OnToolbars);
	MAKE_ACTION(VIEW_TOOLBARS_SIZES, wxITEM_CHECK, OnToolbars);
	MAKE_ACTION(VIEW_TOOLBARS_INDICATORS, wxITEM_CHECK, OnToolbars);
	MAKE_ACTION(VIEW_TOOLBARS_STANDARD, wxITEM_CHECK, OnToolbars);
	MAKE_ACTION(NEW_VIEW, wxITEM_NORMAL, OnNewView);
	MAKE_ACTION(TOGGLE_FULLSCREEN, wxITEM_NORMAL, OnToggleFullscreen);

	MAKE_ACTION(ZOOM_IN, wxITEM_NORMAL, OnZoomIn);
	MAKE_ACTION(ZOOM_OUT, wxITEM_NORMAL, OnZoomOut);
	MAKE_ACTION(ZOOM_NORMAL, wxITEM_NORMAL, OnZoomNormal);

	MAKE_ACTION(SHOW_SHADE, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_ALL_FLOORS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(GHOST_ITEMS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(GHOST_HIGHER_FLOORS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(HIGHLIGHT_ITEMS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_EXTRA, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_INGAME_BOX, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_LIGHTS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_LIGHT_STRENGTH, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_GRID, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_MONSTERS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_SPAWNS_MONSTER, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_NPCS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_SPAWNS_NPC, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_SPECIAL, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_AS_MINIMAP, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_ONLY_COLORS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_ONLY_MODIFIED, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_HOUSES, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_PATHING, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_TOOLTIPS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_PREVIEW, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_WALL_HOOKS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_PICKUPABLES, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_MOVEABLES, wxITEM_CHECK, OnChangeViewSettings);

	MAKE_ACTION(WIN_MINIMAP, wxITEM_NORMAL, OnMinimapWindow);
	MAKE_ACTION(WIN_ACTIONS_HISTORY, wxITEM_NORMAL, OnActionsHistoryWindow);
	MAKE_ACTION(NEW_PALETTE, wxITEM_NORMAL, OnNewPalette);
	MAKE_ACTION(TAKE_SCREENSHOT, wxITEM_NORMAL, OnTakeScreenshot);

	MAKE_ACTION(LIVE_START, wxITEM_NORMAL, OnStartLive);
	MAKE_ACTION(LIVE_JOIN, wxITEM_NORMAL, OnJoinLive);
	MAKE_ACTION(LIVE_CLOSE, wxITEM_NORMAL, OnCloseLive);

	MAKE_ACTION(SELECT_TERRAIN, wxITEM_NORMAL, OnSelectTerrainPalette);
	MAKE_ACTION(SELECT_DOODAD, wxITEM_NORMAL, OnSelectDoodadPalette);
	MAKE_ACTION(SELECT_ITEM, wxITEM_NORMAL, OnSelectItemPalette);
	MAKE_ACTION(SELECT_MONSTER, wxITEM_NORMAL, OnSelectMonsterPalette);
	MAKE_ACTION(SELECT_NPC, wxITEM_NORMAL, OnSelectNpcPalette);
	MAKE_ACTION(SELECT_HOUSE, wxITEM_NORMAL, OnSelectHousePalette);
	MAKE_ACTION(SELECT_WAYPOINT, wxITEM_NORMAL, OnSelectWaypointPalette);
	MAKE_ACTION(SELECT_ZONES, wxITEM_NORMAL, OnSelectZonesPalette);
	MAKE_ACTION(SELECT_RAW, wxITEM_NORMAL, OnSelectRawPalette);

	MAKE_ACTION(FLOOR_0, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_1, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_2, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_3, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_4, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_5, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_6, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_7, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_8, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_9, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_10, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_11, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_12, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_13, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_14, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_15, wxITEM_RADIO, OnChangeFloor);

	MAKE_ACTION(DEBUG_VIEW_DAT, wxITEM_NORMAL, OnDebugViewDat);
	MAKE_ACTION(EXTENSIONS, wxITEM_NORMAL, OnListExtensions);
	MAKE_ACTION(GOTO_WEBSITE, wxITEM_NORMAL, OnGotoWebsite);
	MAKE_ACTION(ABOUT, wxITEM_NORMAL, OnAbout);

	MAKE_ACTION(SEARCH_ON_MAP_DUPLICATE, wxITEM_NORMAL, OnSearchForDuplicateItemsOnMap);
	MAKE_ACTION(SEARCH_ON_SELECTION_DUPLICATE, wxITEM_NORMAL, OnSearchForDuplicateItemsOnSelection);
	MAKE_ACTION(REMOVE_ON_MAP_DUPLICATE_ITEMS, wxITEM_NORMAL, OnRemoveForDuplicateItemsOnMap);
	MAKE_ACTION(REMOVE_ON_SELECTION_DUPLICATE_ITEMS, wxITEM_NORMAL, OnRemoveForDuplicateItemsOnSelection);

	MAKE_ACTION(SEARCH_ON_MAP_WALLS_UPON_WALLS, wxITEM_NORMAL, OnSearchForWallsUponWallsOnMap);
	MAKE_ACTION(SEARCH_ON_SELECTION_WALLS_UPON_WALLS, wxITEM_NORMAL, OnSearchForWallsUponWallsOnSelection);

	// A deleter, this way the frame does not need
	// to bother deleting us.
	class CustomMenuBar : public wxMenuBar {
	public:
		CustomMenuBar(MainMenuBar* mb) :
			mb(mb) { }
		~CustomMenuBar() {
			delete mb;
		}

	private:
		MainMenuBar* mb;
	};

	menubar = newd CustomMenuBar(this);
	frame->SetMenuBar(menubar);

	// Tie all events to this handler!

	for (std::map<std::string, MenuBar::Action*>::iterator ai = actions.begin(); ai != actions.end(); ++ai) {
		frame->Connect(MAIN_FRAME_MENU + ai->second->id, wxEVT_COMMAND_MENU_SELECTED, (wxObjectEventFunction)(wxEventFunction)(ai->second->handler), nullptr, this);
	}
	for (size_t i = 0; i < 10; ++i) {
		frame->Connect(recentFiles.GetBaseId() + i, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(MainMenuBar::OnOpenRecent), nullptr, this);
	}
}

MainMenuBar::~MainMenuBar() {
	// Don't need to delete menubar, it's owned by the frame

	for (std::map<std::string, MenuBar::Action*>::iterator ai = actions.begin(); ai != actions.end(); ++ai) {
		delete ai->second;
	}
}

namespace OnMapRemoveItems {
	struct RemoveItemCondition {
		RemoveItemCondition(uint16_t itemId) :
			itemId(itemId) { }

		uint16_t itemId;

		bool operator()(Map &map, Item* item, int64_t removed, int64_t done) {
			if (done % 0x8000 == 0) {
				g_gui.SetLoadDone((uint32_t)(100 * done / map.getTileCount()));
			}
			return item->getID() == itemId && !item->isComplex();
		}
	};
}

void MainMenuBar::EnableItem(MenuBar::ActionID id, bool enable) {
	std::map<MenuBar::ActionID, std::list<wxMenuItem*>>::iterator fi = items.find(id);
	if (fi == items.end()) {
		return;
	}

	std::list<wxMenuItem*> &li = fi->second;

	for (std::list<wxMenuItem*>::iterator i = li.begin(); i != li.end(); ++i) {
		(*i)->Enable(enable);
	}
}

void MainMenuBar::CheckItem(MenuBar::ActionID id, bool enable) {
	std::map<MenuBar::ActionID, std::list<wxMenuItem*>>::iterator fi = items.find(id);
	if (fi == items.end()) {
		return;
	}

	std::list<wxMenuItem*> &li = fi->second;

	checking_programmaticly = true;
	for (std::list<wxMenuItem*>::iterator i = li.begin(); i != li.end(); ++i) {
		(*i)->Check(enable);
	}
	checking_programmaticly = false;
}

bool MainMenuBar::IsItemChecked(MenuBar::ActionID id) const {
	std::map<MenuBar::ActionID, std::list<wxMenuItem*>>::const_iterator fi = items.find(id);
	if (fi == items.end()) {
		return false;
	}

	const std::list<wxMenuItem*> &li = fi->second;

	for (std::list<wxMenuItem*>::const_iterator i = li.begin(); i != li.end(); ++i) {
		if ((*i)->IsChecked()) {
			return true;
		}
	}

	return false;
}

void MainMenuBar::Update() {
	using namespace MenuBar;
	// This updates all buttons and sets them to proper enabled/disabled state

	bool enable = !g_gui.IsWelcomeDialogShown();
	menubar->Enable(enable);
	if (!enable) {
		return;
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if (editor) {
		EnableItem(UNDO, editor->canUndo());
		EnableItem(REDO, editor->canRedo());
		EnableItem(PASTE, editor->copybuffer.canPaste());
	} else {
		EnableItem(UNDO, false);
		EnableItem(REDO, false);
		EnableItem(PASTE, false);
	}

	bool loaded = g_gui.IsVersionLoaded();
	bool has_map = editor != nullptr;
	bool has_selection = editor && editor->hasSelection();
	bool is_live = editor && editor->IsLive();
	bool is_host = has_map && !editor->IsLiveClient();
	bool is_local = has_map && !is_live;

	EnableItem(CLOSE, is_local);
	EnableItem(SAVE, is_host);
	EnableItem(SAVE_AS, is_host);
	EnableItem(GENERATE_MAP, false);

	EnableItem(IMPORT_MAP, is_local);
	EnableItem(IMPORT_MONSTERS, is_local);
	EnableItem(IMPORT_MINIMAP, false);
	EnableItem(EXPORT_MINIMAP, is_local);
	EnableItem(EXPORT_TILESETS, loaded);

	EnableItem(FIND_ITEM, is_host);
	EnableItem(REPLACE_ITEMS, is_local);
	EnableItem(SEARCH_ON_MAP_EVERYTHING, is_host);
	EnableItem(SEARCH_ON_MAP_UNIQUE, is_host);
	EnableItem(SEARCH_ON_MAP_ACTION, is_host);
	EnableItem(SEARCH_ON_MAP_CONTAINER, is_host);
	EnableItem(SEARCH_ON_MAP_WRITEABLE, is_host);
	EnableItem(SEARCH_ON_SELECTION_EVERYTHING, has_selection && is_host);
	EnableItem(SEARCH_ON_SELECTION_UNIQUE, has_selection && is_host);
	EnableItem(SEARCH_ON_SELECTION_ACTION, has_selection && is_host);
	EnableItem(SEARCH_ON_SELECTION_CONTAINER, has_selection && is_host);
	EnableItem(SEARCH_ON_SELECTION_WRITEABLE, has_selection && is_host);
	EnableItem(SEARCH_ON_SELECTION_ITEM, has_selection && is_host);
	EnableItem(REPLACE_ON_SELECTION_ITEMS, has_selection && is_host);
	EnableItem(REMOVE_ON_SELECTION_ITEM, has_selection && is_host);
	EnableItem(REMOVE_ON_SELECTION_MONSTER, has_selection && is_host);

	EnableItem(CUT, has_map);
	EnableItem(COPY, has_map);

	EnableItem(BORDERIZE_SELECTION, has_map && has_selection);
	EnableItem(BORDERIZE_MAP, is_local);
	EnableItem(RANDOMIZE_SELECTION, has_map && has_selection);
	EnableItem(RANDOMIZE_MAP, is_local);

	EnableItem(GOTO_PREVIOUS_POSITION, has_map);
	EnableItem(GOTO_POSITION, has_map);
	EnableItem(JUMP_TO_BRUSH, loaded);
	EnableItem(JUMP_TO_ITEM_BRUSH, loaded);

	EnableItem(MAP_REMOVE_ITEMS, is_host);
	EnableItem(MAP_REMOVE_CORPSES, is_local);
	EnableItem(MAP_REMOVE_UNREACHABLE_TILES, is_local);
	EnableItem(MAP_REMOVE_EMPTY_MONSTERS_SPAWNS, is_local);
	EnableItem(MAP_REMOVE_EMPTY_NPCS_SPAWNS, is_local);
	EnableItem(CLEAR_INVALID_HOUSES, is_local);
	EnableItem(CLEAR_MODIFIED_STATE, is_local);

	EnableItem(EDIT_TOWNS, is_local);
	EnableItem(EDIT_ITEMS, false);
	EnableItem(EDIT_MONSTERS, false);

	EnableItem(MAP_CLEANUP, is_local);
	EnableItem(MAP_PROPERTIES, is_local);
	EnableItem(MAP_STATISTICS, is_local);

	EnableItem(NEW_VIEW, has_map);
	EnableItem(ZOOM_IN, has_map);
	EnableItem(ZOOM_OUT, has_map);
	EnableItem(ZOOM_NORMAL, has_map);

	if (has_map) {
		CheckItem(SHOW_SPAWNS_MONSTER, g_settings.getBoolean(Config::SHOW_SPAWNS_MONSTER));
	}
	CheckItem(SHOW_SPAWNS_NPC, g_settings.getBoolean(Config::SHOW_SPAWNS_NPC));

	EnableItem(WIN_MINIMAP, loaded);
	EnableItem(NEW_PALETTE, loaded);
	EnableItem(SELECT_TERRAIN, loaded);
	EnableItem(SELECT_DOODAD, loaded);
	EnableItem(SELECT_ITEM, loaded);
	EnableItem(SELECT_HOUSE, loaded);
	EnableItem(SELECT_MONSTER, loaded);
	EnableItem(SELECT_NPC, loaded);
	EnableItem(SELECT_WAYPOINT, loaded);
	EnableItem(SELECT_ZONES, loaded);
	EnableItem(SELECT_RAW, loaded);

	EnableItem(LIVE_START, is_local);
	EnableItem(LIVE_JOIN, loaded);
	EnableItem(LIVE_CLOSE, is_live);

	EnableItem(DEBUG_VIEW_DAT, loaded);

	EnableItem(SEARCH_ON_MAP_DUPLICATE, is_host);
	EnableItem(SEARCH_ON_SELECTION_DUPLICATE, has_selection && is_host);
	EnableItem(REMOVE_ON_MAP_DUPLICATE_ITEMS, is_local);
	EnableItem(REMOVE_ON_SELECTION_DUPLICATE_ITEMS, is_local && has_selection);

	EnableItem(SEARCH_ON_MAP_WALLS_UPON_WALLS, is_host);
	EnableItem(SEARCH_ON_SELECTION_WALLS_UPON_WALLS, is_host && has_selection);

	UpdateFloorMenu();
	UpdateIndicatorsMenu();
}

void MainMenuBar::LoadValues() {
	using namespace MenuBar;

	CheckItem(VIEW_TOOLBARS_BRUSHES, g_settings.getBoolean(Config::SHOW_TOOLBAR_BRUSHES));
	CheckItem(VIEW_TOOLBARS_POSITION, g_settings.getBoolean(Config::SHOW_TOOLBAR_POSITION));
	CheckItem(VIEW_TOOLBARS_SIZES, g_settings.getBoolean(Config::SHOW_TOOLBAR_SIZES));
	CheckItem(VIEW_TOOLBARS_INDICATORS, g_settings.getBoolean(Config::SHOW_TOOLBAR_INDICATORS));
	CheckItem(VIEW_TOOLBARS_STANDARD, g_settings.getBoolean(Config::SHOW_TOOLBAR_STANDARD));

	CheckItem(SELECT_MODE_COMPENSATE, g_settings.getBoolean(Config::COMPENSATED_SELECT));

	if (IsItemChecked(MenuBar::SELECT_MODE_CURRENT)) {
		g_settings.setInteger(Config::SELECTION_TYPE, SELECT_CURRENT_FLOOR);
	} else if (IsItemChecked(MenuBar::SELECT_MODE_LOWER)) {
		g_settings.setInteger(Config::SELECTION_TYPE, SELECT_ALL_FLOORS);
	} else if (IsItemChecked(MenuBar::SELECT_MODE_VISIBLE)) {
		g_settings.setInteger(Config::SELECTION_TYPE, SELECT_VISIBLE_FLOORS);
	}

	switch (g_settings.getInteger(Config::SELECTION_TYPE)) {
		case SELECT_CURRENT_FLOOR:
			CheckItem(SELECT_MODE_CURRENT, true);
			break;
		case SELECT_ALL_FLOORS:
			CheckItem(SELECT_MODE_LOWER, true);
			break;
		default:
		case SELECT_VISIBLE_FLOORS:
			CheckItem(SELECT_MODE_VISIBLE, true);
			break;
	}

	CheckItem(AUTOMAGIC, g_settings.getBoolean(Config::USE_AUTOMAGIC));

	CheckItem(SHOW_SHADE, g_settings.getBoolean(Config::SHOW_SHADE));
	CheckItem(SHOW_INGAME_BOX, g_settings.getBoolean(Config::SHOW_INGAME_BOX));
	CheckItem(SHOW_LIGHTS, g_settings.getBoolean(Config::SHOW_LIGHTS));
	CheckItem(SHOW_LIGHT_STRENGTH, g_settings.getBoolean(Config::SHOW_LIGHT_STRENGTH));
	CheckItem(SHOW_ALL_FLOORS, g_settings.getBoolean(Config::SHOW_ALL_FLOORS));
	CheckItem(GHOST_ITEMS, g_settings.getBoolean(Config::TRANSPARENT_ITEMS));
	CheckItem(GHOST_HIGHER_FLOORS, g_settings.getBoolean(Config::TRANSPARENT_FLOORS));
	CheckItem(SHOW_EXTRA, !g_settings.getBoolean(Config::SHOW_EXTRA));
	CheckItem(SHOW_GRID, g_settings.getBoolean(Config::SHOW_GRID));
	CheckItem(HIGHLIGHT_ITEMS, g_settings.getBoolean(Config::HIGHLIGHT_ITEMS));
	CheckItem(SHOW_MONSTERS, g_settings.getBoolean(Config::SHOW_MONSTERS));
	CheckItem(SHOW_SPAWNS_MONSTER, g_settings.getBoolean(Config::SHOW_SPAWNS_MONSTER));
	CheckItem(SHOW_NPCS, g_settings.getBoolean(Config::SHOW_NPCS));
	CheckItem(SHOW_SPAWNS_NPC, g_settings.getBoolean(Config::SHOW_SPAWNS_NPC));
	CheckItem(SHOW_SPECIAL, g_settings.getBoolean(Config::SHOW_SPECIAL_TILES));
	CheckItem(SHOW_AS_MINIMAP, g_settings.getBoolean(Config::SHOW_AS_MINIMAP));
	CheckItem(SHOW_ONLY_COLORS, g_settings.getBoolean(Config::SHOW_ONLY_TILEFLAGS));
	CheckItem(SHOW_ONLY_MODIFIED, g_settings.getBoolean(Config::SHOW_ONLY_MODIFIED_TILES));
	CheckItem(SHOW_HOUSES, g_settings.getBoolean(Config::SHOW_HOUSES));
	CheckItem(SHOW_PATHING, g_settings.getBoolean(Config::SHOW_BLOCKING));
	CheckItem(SHOW_TOOLTIPS, g_settings.getBoolean(Config::SHOW_TOOLTIPS));
	CheckItem(SHOW_PREVIEW, g_settings.getBoolean(Config::SHOW_PREVIEW));
	CheckItem(SHOW_WALL_HOOKS, g_settings.getBoolean(Config::SHOW_WALL_HOOKS));
	CheckItem(SHOW_PICKUPABLES, g_settings.getBoolean(Config::SHOW_PICKUPABLES));
	CheckItem(SHOW_MOVEABLES, g_settings.getBoolean(Config::SHOW_MOVEABLES));
}

void MainMenuBar::LoadRecentFiles() {
	recentFiles.Load(g_settings.getConfigObject());
}

void MainMenuBar::SaveRecentFiles() {
	recentFiles.Save(g_settings.getConfigObject());
}

void MainMenuBar::AddRecentFile(FileName file) {
	recentFiles.AddFileToHistory(file.GetFullPath());
}

std::vector<wxString> MainMenuBar::GetRecentFiles() {
	std::vector<wxString> files(recentFiles.GetCount());
	for (size_t i = 0; i < recentFiles.GetCount(); ++i) {
		files[i] = recentFiles.GetHistoryFile(i);
	}
	return files;
}

void MainMenuBar::UpdateFloorMenu() {
	using namespace MenuBar;

	if (!g_gui.IsEditorOpen()) {
		return;
	}

	for (int i = 0; i < rme::MapLayers; ++i) {
		CheckItem(static_cast<ActionID>(MenuBar::FLOOR_0 + i), false);
	}

	CheckItem(static_cast<ActionID>(MenuBar::FLOOR_0 + g_gui.GetCurrentFloor()), true);
}

void MainMenuBar::UpdateIndicatorsMenu() {
	using namespace MenuBar;

	if (!g_gui.IsEditorOpen()) {
		return;
	}

	CheckItem(SHOW_WALL_HOOKS, g_settings.getBoolean(Config::SHOW_WALL_HOOKS));
	CheckItem(SHOW_PICKUPABLES, g_settings.getBoolean(Config::SHOW_PICKUPABLES));
	CheckItem(SHOW_MOVEABLES, g_settings.getBoolean(Config::SHOW_MOVEABLES));
}

bool MainMenuBar::Load(const FileName &path, wxArrayString &warnings, wxString &error) {
	// Open the XML file
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(path.GetFullPath().mb_str());
	if (!result) {
		error = "Could not open " + path.GetFullName() + " (file not found or syntax error)";
		return false;
	}

	pugi::xml_node node = doc.child("menubar");
	if (!node) {
		error = path.GetFullName() + ": Invalid rootheader.";
		return false;
	}

	// Clear the menu
	while (menubar->GetMenuCount() > 0) {
		menubar->Remove(0);
	}

	// Load succeded
	for (pugi::xml_node menuNode = node.first_child(); menuNode; menuNode = menuNode.next_sibling()) {
		// For each child node, load it
		wxObject* i = LoadItem(menuNode, nullptr, warnings, error);
		wxMenu* m = dynamic_cast<wxMenu*>(i);
		if (m) {
			menubar->Append(m, m->GetTitle());
#ifdef __APPLE__
			m->SetTitle(m->GetTitle());
#else
			m->SetTitle("");
#endif
		} else if (i) {
			delete i;
			warnings.push_back(path.GetFullName() + ": Only menus can be subitems of main menu");
		}
	}

#ifdef __LINUX__
	const int count = 47;
	wxAcceleratorEntry entries[count];
	// Edit
	entries[0].Set(wxACCEL_CTRL, (int)'Z', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::UNDO));
	entries[1].Set(wxACCEL_CTRL | wxACCEL_SHIFT, (int)'Z', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::REDO));
	entries[2].Set(wxACCEL_CTRL, (int)'F', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::FIND_ITEM));
	entries[3].Set(wxACCEL_CTRL | wxACCEL_SHIFT, (int)'F', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::REPLACE_ITEMS));
	entries[4].Set(wxACCEL_NORMAL, (int)'A', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::AUTOMAGIC));
	entries[5].Set(wxACCEL_CTRL, (int)'B', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::BORDERIZE_SELECTION));
	entries[6].Set(wxACCEL_NORMAL, (int)'P', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::GOTO_PREVIOUS_POSITION));
	entries[7].Set(wxACCEL_CTRL, (int)'G', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::GOTO_POSITION));
	entries[8].Set(wxACCEL_NORMAL, (int)'J', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::JUMP_TO_BRUSH));
	entries[9].Set(wxACCEL_CTRL, (int)'X', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::CUT));
	entries[10].Set(wxACCEL_CTRL, (int)'C', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::COPY));
	entries[11].Set(wxACCEL_CTRL, (int)'V', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::PASTE));

	// View
	entries[12].Set(wxACCEL_CTRL, (int)'=', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::ZOOM_IN));
	entries[13].Set(wxACCEL_CTRL, (int)'-', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::ZOOM_OUT));
	entries[14].Set(wxACCEL_CTRL, (int)'0', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::ZOOM_NORMAL));
	entries[15].Set(wxACCEL_NORMAL, (int)'Q', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SHOW_SHADE));
	entries[16].Set(wxACCEL_CTRL, (int)'W', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SHOW_ALL_FLOORS));
	entries[17].Set(wxACCEL_NORMAL, (int)'Q', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::GHOST_ITEMS));
	entries[18].Set(wxACCEL_CTRL, (int)'L', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::GHOST_HIGHER_FLOORS));
	entries[19].Set(wxACCEL_SHIFT, (int)'I', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SHOW_INGAME_BOX));
	entries[20].Set(wxACCEL_SHIFT, (int)'L', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SHOW_LIGHTS));
	entries[21].Set(wxACCEL_SHIFT, (int)'K', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SHOW_LIGHT_STRENGTH));
	entries[22].Set(wxACCEL_SHIFT, (int)'G', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SHOW_GRID));
	entries[23].Set(wxACCEL_NORMAL, (int)'V', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::HIGHLIGHT_ITEMS));
	entries[24].Set(wxACCEL_NORMAL, (int)'F', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SHOW_MONSTERS));
	entries[25].Set(wxACCEL_NORMAL, (int)'S', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SHOW_SPAWNS_MONSTER));
	entries[26].Set(wxACCEL_NORMAL, (int)'X', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SHOW_NPCS));
	entries[27].Set(wxACCEL_NORMAL, (int)'U', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SHOW_SPAWNS_NPC));
	entries[28].Set(wxACCEL_NORMAL, (int)'E', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SHOW_SPECIAL));
	entries[29].Set(wxACCEL_SHIFT, (int)'E', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SHOW_AS_MINIMAP));
	entries[30].Set(wxACCEL_CTRL, (int)'E', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SHOW_ONLY_COLORS));
	entries[31].Set(wxACCEL_CTRL, (int)'M', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SHOW_ONLY_MODIFIED));
	entries[32].Set(wxACCEL_CTRL, (int)'H', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SHOW_HOUSES));
	entries[33].Set(wxACCEL_NORMAL, (int)'O', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SHOW_PATHING));
	entries[34].Set(wxACCEL_NORMAL, (int)'Y', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SHOW_TOOLTIPS));
	entries[35].Set(wxACCEL_NORMAL, (int)'L', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SHOW_PREVIEW));
	entries[36].Set(wxACCEL_NORMAL, (int)'K', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SHOW_WALL_HOOKS));

	// Window
	entries[37].Set(wxACCEL_NORMAL, (int)'M', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::WIN_MINIMAP));
	entries[38].Set(wxACCEL_NORMAL, (int)'T', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SELECT_TERRAIN));
	entries[39].Set(wxACCEL_NORMAL, (int)'D', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SELECT_DOODAD));
	entries[40].Set(wxACCEL_NORMAL, (int)'I', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SELECT_ITEM));
	entries[41].Set(wxACCEL_NORMAL, (int)'H', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SELECT_HOUSE));
	entries[42].Set(wxACCEL_NORMAL, (int)'C', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SELECT_MONSTER));
	entries[43].Set(wxACCEL_NORMAL, (int)'N', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SELECT_NPC));
	entries[44].Set(wxACCEL_NORMAL, (int)'W', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SELECT_WAYPOINT));
	entries[45].Set(wxACCEL_NORMAL, (int)'Z', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SELECT_ZONES));
	entries[46].Set(wxACCEL_NORMAL, (int)'R', static_cast<int>(MAIN_FRAME_MENU) + static_cast<int>(MenuBar::SELECT_RAW));

	wxAcceleratorTable accelerator(count, entries);
	frame->SetAcceleratorTable(accelerator);
#endif

	/*
	// Create accelerator table
	accelerator_table = newd wxAcceleratorTable(accelerators.size(), &accelerators[0]);

	// Tell all clients of the renewed accelerators
	RenewClients();
	*/

	recentFiles.AddFilesToMenu();
	Update();
	LoadValues();
	return true;
}

wxObject* MainMenuBar::LoadItem(pugi::xml_node node, wxMenu* parent, wxArrayString &warnings, wxString &error) {
	pugi::xml_attribute attribute;

	const std::string &nodeName = as_lower_str(node.name());
	if (nodeName == "menu") {
		if (!(attribute = node.attribute("name"))) {
			return nullptr;
		}

		std::string name = attribute.as_string();
		std::replace(name.begin(), name.end(), '$', '&');

		wxMenu* menu = newd wxMenu;
		if ((attribute = node.attribute("special")) && std::string(attribute.as_string()) == "RECENT_FILES") {
			recentFiles.UseMenu(menu);
		} else {
			for (pugi::xml_node menuNode = node.first_child(); menuNode; menuNode = menuNode.next_sibling()) {
				// Load an add each item in order
				LoadItem(menuNode, menu, warnings, error);
			}
		}

		// If we have a parent, add ourselves.
		// If not, we just return the item and the parent function
		// is responsible for adding us to wherever
		if (parent) {
			parent->AppendSubMenu(menu, wxstr(name));
		} else {
			menu->SetTitle((name));
		}
		return menu;
	} else if (nodeName == "item") {
		// We must have a parent when loading items
		if (!parent) {
			return nullptr;
		} else if (!(attribute = node.attribute("name"))) {
			return nullptr;
		}

		std::string name = attribute.as_string();
		std::replace(name.begin(), name.end(), '$', '&');
		if (!(attribute = node.attribute("action"))) {
			return nullptr;
		}

		const std::string &action = attribute.as_string();
		std::string hotkey = node.attribute("hotkey").as_string();
		if (!hotkey.empty()) {
			hotkey = '\t' + hotkey;
		}

		const std::string &help = node.attribute("help").as_string();
		name += hotkey;

		auto it = actions.find(action);
		if (it == actions.end()) {
			warnings.push_back("Invalid action type '" + wxstr(action) + "'.");
			return nullptr;
		}

		const MenuBar::Action &act = *it->second;
		wxAcceleratorEntry* entry = wxAcceleratorEntry::Create(wxstr(hotkey));
		if (entry) {
			delete entry; // accelerators.push_back(entry);
		} else {
			warnings.push_back("Invalid hotkey.");
		}

		wxMenuItem* tmp = parent->Append(
			MAIN_FRAME_MENU + act.id, // ID
			wxstr(name), // Title of button
			wxstr(help), // Help text
			act.kind // Kind of item
		);
		items[MenuBar::ActionID(act.id)].push_back(tmp);
		return tmp;
	} else if (nodeName == "separator") {
		// We must have a parent when loading items
		if (!parent) {
			return nullptr;
		}
		return parent->AppendSeparator();
	}
	return nullptr;
}

void MainMenuBar::OnNew(wxCommandEvent &WXUNUSED(event)) {
	g_gui.NewMap();
}

void MainMenuBar::OnGenerateMap(wxCommandEvent &WXUNUSED(event)) {
	/*
	if(!DoQuerySave()) return;

	std::ostringstream os;
	os << "Untitled-" << untitled_counter << ".otbm";
	++untitled_counter;

	editor.generateMap(wxstr(os.str()));

	g_gui.SetStatusText("Generated newd map");

	g_gui.UpdateTitle();
	g_gui.RefreshPalettes();
	g_gui.UpdateMinimap();
	g_gui.FitViewToMap();
	UpdateMenubar();
	Refresh();
	*/
}

void MainMenuBar::OnOpenRecent(wxCommandEvent &event) {
	FileName fn(recentFiles.GetHistoryFile(event.GetId() - recentFiles.GetBaseId()));
	frame->LoadMap(fn);
}

void MainMenuBar::OnOpen(wxCommandEvent &WXUNUSED(event)) {
	g_gui.OpenMap();
}

void MainMenuBar::OnClose(wxCommandEvent &WXUNUSED(event)) {
	frame->DoQuerySave(true); // It closes the editor too
}

void MainMenuBar::OnSave(wxCommandEvent &WXUNUSED(event)) {
	g_gui.SaveMap();
}

void MainMenuBar::OnSaveAs(wxCommandEvent &WXUNUSED(event)) {
	g_gui.SaveMapAs();
}

void MainMenuBar::OnPreferences(wxCommandEvent &WXUNUSED(event)) {
	PreferencesWindow dialog(frame);
	dialog.ShowModal();
	dialog.Destroy();
}

void MainMenuBar::OnQuit(wxCommandEvent &WXUNUSED(event)) {
	/*
	while(g_gui.IsEditorOpen())
		if(!frame->DoQuerySave(true))
			return;
			*/
	//((Application*)wxTheApp)->Unload();
	g_gui.root->Close();
}

void MainMenuBar::OnImportMap(wxCommandEvent &WXUNUSED(event)) {
	ASSERT(g_gui.GetCurrentEditor());
	wxDialog* importmap = newd ImportMapWindow(frame, *g_gui.GetCurrentEditor());
	importmap->ShowModal();
}

void MainMenuBar::OnImportMonsterData(wxCommandEvent &WXUNUSED(event)) {
	wxFileDialog dlg(g_gui.root, "Import monster file", "", "", "*.xml", wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
	if (dlg.ShowModal() == wxID_OK) {
		wxArrayString paths;
		dlg.GetPaths(paths);
		for (uint32_t i = 0; i < paths.GetCount(); ++i) {
			wxString error;
			wxArrayString warnings;
			bool ok = g_monsters.importXMLFromOT(FileName(paths[i]), error, warnings);
			if (ok) {
				g_gui.ListDialog("Monster loader errors", warnings);
			} else {
				wxMessageBox("Error OT data file \"" + paths[i] + "\".\n" + error, "Error", wxOK | wxICON_INFORMATION, g_gui.root);
			}
		}
	}
}

void MainMenuBar::OnImportNpcData(wxCommandEvent &WXUNUSED(event)) {
	wxFileDialog dlg(g_gui.root, "Import npc file", "", "", "*.xml", wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
	if (dlg.ShowModal() == wxID_OK) {
		wxArrayString paths;
		dlg.GetPaths(paths);
		for (uint32_t i = 0; i < paths.GetCount(); ++i) {
			wxString error;
			wxArrayString warnings;
			bool ok = g_npcs.importXMLFromOT(FileName(paths[i]), error, warnings);
			if (ok) {
				g_gui.ListDialog("Monster loader errors", warnings);
			} else {
				wxMessageBox("Error OT data file \"" + paths[i] + "\".\n" + error, "Error", wxOK | wxICON_INFORMATION, g_gui.root);
			}
		}
	}
}

void MainMenuBar::OnImportMinimap(wxCommandEvent &WXUNUSED(event)) {
	ASSERT(g_gui.IsEditorOpen());
	// wxDialog* importmap = newd ImportMapWindow();
	// importmap->ShowModal();
}

void MainMenuBar::OnExportMinimap(wxCommandEvent &WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	ExportMiniMapWindow dialog(frame, *g_gui.GetCurrentEditor());
	dialog.ShowModal();
}

void MainMenuBar::OnExportTilesets(wxCommandEvent &WXUNUSED(event)) {
	if (g_gui.GetCurrentEditor()) {
		ExportTilesetsWindow dlg(frame, *g_gui.GetCurrentEditor());
		dlg.ShowModal();
		dlg.Destroy();
	}
}

void MainMenuBar::OnDebugViewDat(wxCommandEvent &WXUNUSED(event)) {
	wxDialog dlg(frame, wxID_ANY, "Debug .dat file", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	new DatDebugView(&dlg);
	dlg.ShowModal();
}

void MainMenuBar::OnReloadDataFiles(wxCommandEvent &WXUNUSED(event)) {
	wxString error;
	wxArrayString warnings;
	g_gui.LoadVersion(g_gui.GetCurrentVersionID(), error, warnings, true);
	g_gui.PopupDialog("Error", error, wxOK);
	g_gui.ListDialog("Warnings", warnings);
}

void MainMenuBar::OnListExtensions(wxCommandEvent &WXUNUSED(event)) {
	ExtensionsDialog exts(frame);
	exts.ShowModal();
}

void MainMenuBar::OnGotoWebsite(wxCommandEvent &WXUNUSED(event)) {
	::wxLaunchDefaultBrowser("http://www.remeresmapeditor.com/", wxBROWSER_NEW_WINDOW);
}

void MainMenuBar::OnAbout(wxCommandEvent &WXUNUSED(event)) {
	AboutWindow about(frame);
	about.ShowModal();
}

void MainMenuBar::OnUndo(wxCommandEvent &WXUNUSED(event)) {
	g_gui.DoUndo();
}

void MainMenuBar::OnRedo(wxCommandEvent &WXUNUSED(event)) {
	g_gui.DoRedo();
}

namespace OnSearchForItem {
	struct Finder {
		Finder(uint16_t itemId, uint32_t maxCount, bool findTile = false) :
			itemId(itemId), maxCount(maxCount), findTile(findTile) { }

		bool findTile = false;
		uint16_t itemId;
		uint32_t maxCount;
		std::vector<std::pair<Tile*, Item*>> result;

		bool limitReached() const {
			return result.size() >= (size_t)maxCount;
		}

		void operator()(Map &map, Tile* tile, Item* item, long long done) {
			if (result.size() >= (size_t)maxCount) {
				return;
			}

			if (done % 0x8000 == 0) {
				g_gui.SetLoadDone((unsigned int)(100 * done / map.getTileCount()));
			}

			if (item->getID() == itemId) {
				result.push_back(std::make_pair(tile, item));
			}

			if (!findTile) {
				return;
			}

			if (tile->isHouseTile()) {
				return;
			}

			const auto &tileSearchType = static_cast<FindItemDialog::SearchTileType>(g_settings.getInteger(Config::FIND_TILE_TYPE));
			if (tileSearchType == FindItemDialog::SearchTileType::NoLogout && !tile->isNoLogout()) {
				return;
			}

			if (tileSearchType == FindItemDialog::SearchTileType::PlayerVsPlayer && !tile->isPVP()) {
				return;
			}

			if (tileSearchType == FindItemDialog::SearchTileType::NoPlayerVsPlayer && !tile->isNoPVP()) {
				return;
			}

			if (tileSearchType == FindItemDialog::SearchTileType::ProtectionZone && !tile->isPZ()) {
				return;
			}

			const auto it = std::ranges::find_if(result, [&tile](const auto &pair) {
				return pair.first == tile;
			});

			if (it != result.end()) {
				return;
			}

			result.push_back(std::make_pair(tile, nullptr));
		}
	};
}

void MainMenuBar::OnSearchForItem(wxCommandEvent &WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	const auto &searchMode = static_cast<FindItemDialog::SearchMode>(g_settings.getInteger(Config::FIND_ITEM_MODE));

	FindItemDialog dialog(frame, "Search for Item");
	dialog.setSearchMode(searchMode);
	if (dialog.ShowModal() == wxID_OK) {
		g_settings.setInteger(Config::FIND_ITEM_MODE, static_cast<int>(dialog.getSearchMode()));
		g_settings.setInteger(Config::FIND_TILE_TYPE, static_cast<int>(dialog.getSearchTileType()));

		OnSearchForItem::Finder finder(dialog.getResultID(), (uint32_t)g_settings.getInteger(Config::REPLACE_SIZE), dialog.getSearchMode() == FindItemDialog::SearchMode::TileTypes);

		g_gui.CreateLoadBar("Searching map...");

		foreach_ItemOnMap(g_gui.GetCurrentMap(), finder, false);
		std::vector<std::pair<Tile*, Item*>> &result = finder.result;

		g_gui.DestroyLoadBar();

		if (finder.limitReached()) {
			wxString msg;
			msg << "The configured limit has been reached. Only " << finder.maxCount << " results will be displayed.";
			g_gui.PopupDialog("Notice", msg, wxOK);
		}

		SearchResultWindow* window = g_gui.ShowSearchWindow();
		window->Clear();

		const auto &searchTileType = dialog.getSearchTileType();

		for (auto it = result.begin(); it != result.end(); ++it) {
			const auto &tile = it->first;

			if (dialog.getSearchMode() == FindItemDialog::SearchMode::TileTypes) {
				wxString tileType;

				if (tile->isNoLogout() && searchTileType == FindItemDialog::SearchTileType::NoLogout) {
					tileType = "No Logout";
				} else if (tile->isPVP() && searchTileType == FindItemDialog::SearchTileType::PlayerVsPlayer) {
					tileType = "PVP";
				} else if (tile->isNoPVP() && searchTileType == FindItemDialog::SearchTileType::NoPlayerVsPlayer) {
					tileType = "No PVP";
				} else if (tile->isPZ() && searchTileType == FindItemDialog::SearchTileType::ProtectionZone) {
					tileType = "PZ";
				}

				window->AddPosition(tileType, tile->getPosition());
			} else {
				window->AddPosition(wxstr(it->second->getName()), tile->getPosition());
			}
		}
	}
	dialog.Destroy();
}

void MainMenuBar::OnReplaceItems(wxCommandEvent &WXUNUSED(event)) {
	if (!g_gui.IsVersionLoaded()) {
		return;
	}

	if (MapTab* tab = g_gui.GetCurrentMapTab()) {
		if (MapWindow* window = tab->GetView()) {
			window->ShowReplaceItemsDialog(false);
		}
	}
}

namespace OnSearchForStuff {
	struct Searcher {
		Searcher() :
			search_unique(false),
			search_action(false),
			search_container(false),
			search_writeable(false) { }

		bool search_unique;
		bool search_action;
		bool search_container;
		bool search_writeable;
		std::vector<std::pair<Tile*, Item*>> found;

		void operator()(Map &map, Tile* tile, Item* item, long long done) {
			if (done % 0x8000 == 0) {
				g_gui.SetLoadDone((unsigned int)(100 * done / map.getTileCount()));
			}
			Container* container;
			if ((search_unique && item->getUniqueID() > 0) || (search_action && item->getActionID() > 0) || (search_container && ((container = dynamic_cast<Container*>(item)) && container->getItemCount())) || (search_writeable && item && item->getText().length() > 0)) {
				found.push_back(std::make_pair(tile, item));
			}
		}

		wxString desc(Item* item) {
			wxString label;
			if (item->getUniqueID() > 0) {
				label << "UID:" << item->getUniqueID() << " ";
			}

			if (item->getActionID() > 0) {
				label << "AID:" << item->getActionID() << " ";
			}

			label << wxstr(item->getName());

			if (dynamic_cast<Container*>(item)) {
				label << " (Container) ";
			}

			if (item->getText().length() > 0) {
				label << " (Text: " << wxstr(item->getText()) << ") ";
			}

			return label;
		}

		void sort() {
			if (search_unique || search_action) {
				std::sort(found.begin(), found.end(), Searcher::compare);
			}
		}

		static bool compare(const std::pair<Tile*, Item*> &pair1, const std::pair<Tile*, Item*> &pair2) {
			const Item* item1 = pair1.second;
			const Item* item2 = pair2.second;

			if (item1->getActionID() != 0 || item2->getActionID() != 0) {
				return item1->getActionID() < item2->getActionID();
			} else if (item1->getUniqueID() != 0 || item2->getUniqueID() != 0) {
				return item1->getUniqueID() < item2->getUniqueID();
			}

			return false;
		}
	};
}

void MainMenuBar::OnSearchForStuffOnMap(wxCommandEvent &WXUNUSED(event)) {
	SearchItems(true, true, true, true);
}

void MainMenuBar::OnSearchForUniqueOnMap(wxCommandEvent &WXUNUSED(event)) {
	SearchItems(true, false, false, false);
}

void MainMenuBar::OnSearchForActionOnMap(wxCommandEvent &WXUNUSED(event)) {
	SearchItems(false, true, false, false);
}

void MainMenuBar::OnSearchForContainerOnMap(wxCommandEvent &WXUNUSED(event)) {
	SearchItems(false, false, true, false);
}

void MainMenuBar::OnSearchForWriteableOnMap(wxCommandEvent &WXUNUSED(event)) {
	SearchItems(false, false, false, true);
}

void MainMenuBar::OnSearchForStuffOnSelection(wxCommandEvent &WXUNUSED(event)) {
	SearchItems(true, true, true, true, true);
}

void MainMenuBar::OnSearchForUniqueOnSelection(wxCommandEvent &WXUNUSED(event)) {
	SearchItems(true, false, false, false, true);
}

void MainMenuBar::OnSearchForActionOnSelection(wxCommandEvent &WXUNUSED(event)) {
	SearchItems(false, true, false, false, true);
}

void MainMenuBar::OnSearchForContainerOnSelection(wxCommandEvent &WXUNUSED(event)) {
	SearchItems(false, false, true, false, true);
}

void MainMenuBar::OnSearchForWriteableOnSelection(wxCommandEvent &WXUNUSED(event)) {
	SearchItems(false, false, false, true, true);
}

void MainMenuBar::OnSearchForItemOnSelection(wxCommandEvent &WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	FindItemDialog dialog(frame, "Search on Selection");
	dialog.setSearchMode((FindItemDialog::SearchMode)g_settings.getInteger(Config::FIND_ITEM_MODE));
	if (dialog.ShowModal() == wxID_OK) {
		OnSearchForItem::Finder finder(dialog.getResultID(), (uint32_t)g_settings.getInteger(Config::REPLACE_SIZE));
		g_gui.CreateLoadBar("Searching on selected area...");

		foreach_ItemOnMap(g_gui.GetCurrentMap(), finder, true);
		std::vector<std::pair<Tile*, Item*>> &result = finder.result;

		g_gui.DestroyLoadBar();

		if (finder.limitReached()) {
			wxString msg;
			msg << "The configured limit has been reached. Only " << finder.maxCount << " results will be displayed.";
			g_gui.PopupDialog("Notice", msg, wxOK);
		}

		SearchResultWindow* window = g_gui.ShowSearchWindow();
		window->Clear();
		for (std::vector<std::pair<Tile*, Item*>>::const_iterator iter = result.begin(); iter != result.end(); ++iter) {
			Tile* tile = iter->first;
			Item* item = iter->second;
			window->AddPosition(wxstr(item->getName()), tile->getPosition());
		}

		g_settings.setInteger(Config::FIND_ITEM_MODE, (int)dialog.getSearchMode());
	}

	dialog.Destroy();
}

void MainMenuBar::OnReplaceItemsOnSelection(wxCommandEvent &WXUNUSED(event)) {
	if (!g_gui.IsVersionLoaded()) {
		return;
	}

	if (MapTab* tab = g_gui.GetCurrentMapTab()) {
		if (MapWindow* window = tab->GetView()) {
			window->ShowReplaceItemsDialog(true);
		}
	}
}

void MainMenuBar::OnRemoveItemOnSelection(wxCommandEvent &WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	FindItemDialog dialog(frame, "Remove Item on Selection");
	if (dialog.ShowModal() == wxID_OK) {
		g_gui.GetCurrentEditor()->clearActions();
		g_gui.CreateLoadBar("Searching item on selection to remove...");
		OnMapRemoveItems::RemoveItemCondition condition(dialog.getResultID());
		int64_t count = RemoveItemOnMap(g_gui.GetCurrentMap(), condition, true);
		g_gui.DestroyLoadBar();

		wxString msg;
		msg << count << " items removed.";
		g_gui.PopupDialog("Remove Item", msg, wxOK);
		g_gui.GetCurrentMap().doChange();
		g_gui.RefreshView();
	}
	dialog.Destroy();
}

void MainMenuBar::OnRemoveMonstersOnSelection(wxCommandEvent &WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	g_gui.GetCurrentEditor()->clearActions();
	g_gui.CreateLoadBar("Searching monsters on selection to remove...");
	int64_t count = RemoveMonstersOnMap(g_gui.GetCurrentMap(), true);
	g_gui.DestroyLoadBar();

	wxString msg;
	msg << count << " monsters removed.";
	g_gui.PopupDialog("Remove Monsters", msg, wxOK);
	g_gui.GetCurrentMap().doChange();
	g_gui.RefreshView();
}

void MainMenuBar::OnSelectionTypeChange(wxCommandEvent &WXUNUSED(event)) {
	g_settings.setInteger(Config::COMPENSATED_SELECT, IsItemChecked(MenuBar::SELECT_MODE_COMPENSATE));

	if (IsItemChecked(MenuBar::SELECT_MODE_CURRENT)) {
		g_settings.setInteger(Config::SELECTION_TYPE, SELECT_CURRENT_FLOOR);
	} else if (IsItemChecked(MenuBar::SELECT_MODE_LOWER)) {
		g_settings.setInteger(Config::SELECTION_TYPE, SELECT_ALL_FLOORS);
	} else if (IsItemChecked(MenuBar::SELECT_MODE_VISIBLE)) {
		g_settings.setInteger(Config::SELECTION_TYPE, SELECT_VISIBLE_FLOORS);
	}
}

void MainMenuBar::OnCopy(wxCommandEvent &WXUNUSED(event)) {
	g_gui.DoCopy();
}

void MainMenuBar::OnCut(wxCommandEvent &WXUNUSED(event)) {
	g_gui.DoCut();
}

void MainMenuBar::OnPaste(wxCommandEvent &WXUNUSED(event)) {
	g_gui.PreparePaste();
}

void MainMenuBar::OnToggleAutomagic(wxCommandEvent &WXUNUSED(event)) {
	g_settings.setInteger(Config::USE_AUTOMAGIC, IsItemChecked(MenuBar::AUTOMAGIC));
	g_settings.setInteger(Config::BORDER_IS_GROUND, IsItemChecked(MenuBar::AUTOMAGIC));
	if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
		g_gui.SetStatusText("Automagic enabled.");
	} else {
		g_gui.SetStatusText("Automagic disabled.");
	}
}

void MainMenuBar::OnBorderizeSelection(wxCommandEvent &WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	g_gui.GetCurrentEditor()->borderizeSelection();
	g_gui.RefreshView();
}

void MainMenuBar::OnBorderizeMap(wxCommandEvent &WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	int ret = g_gui.PopupDialog("Borderize Map", "Are you sure you want to borderize the entire map (this action cannot be undone)?", wxYES | wxNO);
	if (ret == wxID_YES) {
		g_gui.GetCurrentEditor()->borderizeMap(true);
	}

	g_gui.RefreshView();
}

void MainMenuBar::OnRandomizeSelection(wxCommandEvent &WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	g_gui.GetCurrentEditor()->randomizeSelection();
	g_gui.RefreshView();
}

void MainMenuBar::OnRandomizeMap(wxCommandEvent &WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	int ret = g_gui.PopupDialog("Randomize Map", "Are you sure you want to randomize the entire map (this action cannot be undone)?", wxYES | wxNO);
	if (ret == wxID_YES) {
		g_gui.GetCurrentEditor()->randomizeMap(true);
	}

	g_gui.RefreshView();
}

void MainMenuBar::OnJumpToBrush(wxCommandEvent &WXUNUSED(event)) {
	if (!g_gui.IsVersionLoaded()) {
		return;
	}

	// Create the jump to dialog
	FindDialog* dlg = newd FindBrushDialog(frame);

	// Display dialog to user
	dlg->ShowModal();

	// Retrieve result, if null user canceled
	const Brush* brush = dlg->getResult();
	if (brush) {
		g_gui.SelectBrush(brush, TILESET_UNKNOWN);
	}
	delete dlg;
}

void MainMenuBar::OnJumpToItemBrush(wxCommandEvent &WXUNUSED(event)) {
	if (!g_gui.IsVersionLoaded()) {
		return;
	}

	// Create the jump to dialog
	FindItemDialog dialog(frame, "Jump to Item");
	dialog.setSearchMode((FindItemDialog::SearchMode)g_settings.getInteger(Config::JUMP_TO_ITEM_MODE));
	if (dialog.ShowModal() == wxID_OK) {
		// Retrieve result, if null user canceled
		const Brush* brush = dialog.getResult();
		if (brush) {
			g_gui.SelectBrush(brush, TILESET_RAW);
		}
		g_settings.setInteger(Config::JUMP_TO_ITEM_MODE, (int)dialog.getSearchMode());
	}
	dialog.Destroy();
}

void MainMenuBar::OnGotoPreviousPosition(wxCommandEvent &WXUNUSED(event)) {
	MapTab* mapTab = g_gui.GetCurrentMapTab();
	if (mapTab) {
		mapTab->GoToPreviousCenterPosition();
	}
}

void MainMenuBar::OnGotoPosition(wxCommandEvent &WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	// Display dialog, it also controls the actual jump
	GotoPositionDialog dlg(frame, *g_gui.GetCurrentEditor());
	dlg.ShowModal();
}

void MainMenuBar::OnMapRemoveItems(wxCommandEvent &WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	FindItemDialog dialog(frame, "Item Type to Remove");
	if (dialog.ShowModal() == wxID_OK) {
		uint16_t itemid = dialog.getResultID();

		g_gui.GetCurrentEditor()->getSelection().clear();
		g_gui.GetCurrentEditor()->clearActions();

		OnMapRemoveItems::RemoveItemCondition condition(itemid);
		g_gui.CreateLoadBar("Searching map for items to remove...");

		int64_t count = RemoveItemOnMap(g_gui.GetCurrentMap(), condition, false);

		g_gui.DestroyLoadBar();

		wxString msg;
		msg << count << " items deleted.";

		g_gui.PopupDialog("Search completed", msg, wxOK);
		g_gui.GetCurrentMap().doChange();
		g_gui.RefreshView();
	}
	dialog.Destroy();
}

namespace OnMapRemoveCorpses {
	struct condition {
		condition() { }

		bool operator()(Map &map, Item* item, long long removed, long long done) {
			if (done % 0x800 == 0) {
				g_gui.SetLoadDone((unsigned int)(100 * done / map.getTileCount()));
			}

			return g_materials.isInTileset(item, "Corpses") && !item->isComplex();
		}
	};
}

void MainMenuBar::OnMapRemoveCorpses(wxCommandEvent &WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	int ok = g_gui.PopupDialog("Remove Corpses", "Do you want to remove all corpses from the map?", wxYES | wxNO);

	if (ok == wxID_YES) {
		g_gui.GetCurrentEditor()->getSelection().clear();
		g_gui.GetCurrentEditor()->clearActions();

		OnMapRemoveCorpses::condition func;
		g_gui.CreateLoadBar("Searching map for items to remove...");

		int64_t count = RemoveItemOnMap(g_gui.GetCurrentMap(), func, false);

		g_gui.DestroyLoadBar();

		wxString msg;
		msg << count << " items deleted.";
		g_gui.PopupDialog("Search completed", msg, wxOK);
		g_gui.GetCurrentMap().doChange();
	}
}

namespace OnMapRemoveUnreachable {
	struct condition {
		condition() { }

		bool isReachable(Tile* tile) {
			if (tile == nullptr) {
				return false;
			}
			if (!tile->isBlocking()) {
				return true;
			}
			return false;
		}

		bool operator()(Map &map, Tile* tile, long long removed, long long done, long long total) {
			if (done % 0x1000 == 0) {
				g_gui.SetLoadDone((unsigned int)(100 * done / total));
			}

			const Position &pos = tile->getPosition();
			int sx = std::max(pos.x - 10, 0);
			int ex = std::min(pos.x + 10, 65535);
			int sy = std::max(pos.y - 8, 0);
			int ey = std::min(pos.y + 8, 65535);
			int sz, ez;

			if (pos.z < 8) {
				sz = 0;
				ez = 9;
			} else {
				// underground
				sz = std::max(pos.z - 2, rme::MapGroundLayer);
				ez = std::min(pos.z + 2, rme::MapMaxLayer);
			}

			for (int z = sz; z <= ez; ++z) {
				for (int y = sy; y <= ey; ++y) {
					for (int x = sx; x <= ex; ++x) {
						if (isReachable(map.getTile(x, y, z))) {
							return false;
						}
					}
				}
			}
			return true;
		}
	};
}

void MainMenuBar::OnMapRemoveUnreachable(wxCommandEvent &WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	int ok = g_gui.PopupDialog("Remove Unreachable Tiles", "Do you want to remove all unreachable items from the map?", wxYES | wxNO);

	if (ok == wxID_YES) {
		g_gui.GetCurrentEditor()->getSelection().clear();
		g_gui.GetCurrentEditor()->clearActions();

		OnMapRemoveUnreachable::condition func;
		g_gui.CreateLoadBar("Searching map for tiles to remove...");

		long long removed = remove_if_TileOnMap(g_gui.GetCurrentMap(), func);

		g_gui.DestroyLoadBar();

		wxString msg;
		msg << removed << " tiles deleted.";

		g_gui.PopupDialog("Search completed", msg, wxOK);

		g_gui.GetCurrentMap().doChange();
	}
}

void MainMenuBar::OnMapRemoveEmptyMonsterSpawns(wxCommandEvent &WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	int ok = g_gui.PopupDialog("Remove Empty Monsters Spawns", "Do you want to remove all empty monsters spawns from the map?", wxYES | wxNO);
	if (ok == wxID_YES) {
		Editor* editor = g_gui.GetCurrentEditor();
		editor->getSelection().clear();

		g_gui.CreateLoadBar("Searching map for empty monsters spawns to remove...");

		Map &map = g_gui.GetCurrentMap();
		MonsterVector monster;
		TileVector toDeleteSpawns;
		for (const auto &spawnPosition : map.spawnsMonster) {
			Tile* tile = map.getTile(spawnPosition);
			if (!tile || !tile->spawnMonster) {
				continue;
			}

			const int32_t radius = tile->spawnMonster->getSize();

			bool empty = true;
			for (int32_t y = -radius; y <= radius; ++y) {
				for (int32_t x = -radius; x <= radius; ++x) {
					Tile* creature_tile = map.getTile(spawnPosition + Position(x, y, 0));
					if (creature_tile && creature_tile->monster && !creature_tile->monster->isSaved()) {
						creature_tile->monster->save();
						monster.push_back(creature_tile->monster);
						empty = false;
					}
				}
			}

			if (empty) {
				toDeleteSpawns.push_back(tile);
			}
		}

		for (Monster* monster : monster) {
			monster->reset();
		}

		BatchAction* batch = editor->createBatch(ACTION_DELETE_TILES);
		Action* action = editor->createAction(batch);

		const size_t count = toDeleteSpawns.size();
		size_t removed = 0;
		for (const auto &tile : toDeleteSpawns) {
			Tile* newtile = tile->deepCopy(map);
			map.removeSpawnMonster(newtile);
			delete newtile->spawnMonster;
			newtile->spawnMonster = nullptr;
			if (++removed % 5 == 0) {
				// update progress bar for each 5 spawns removed
				g_gui.SetLoadDone(100 * removed / count);
			}
			action->addChange(newd Change(newtile));
		}

		batch->addAndCommitAction(action);
		editor->addBatch(batch);

		g_gui.DestroyLoadBar();

		wxString msg;
		msg << removed << " empty monsters spawns removed.";
		g_gui.PopupDialog("Search completed", msg, wxOK);
		g_gui.GetCurrentMap().doChange();
	}
}

void MainMenuBar::OnMapRemoveEmptyNpcSpawns(wxCommandEvent &WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	int ok = g_gui.PopupDialog("Remove Empty Npcs Spawns", "Do you want to remove all empty npcs spawns from the map?", wxYES | wxNO);
	if (ok == wxID_YES) {
		Editor* editor = g_gui.GetCurrentEditor();
		editor->getSelection().clear();

		g_gui.CreateLoadBar("Searching map for empty npcs spawns to remove...");

		Map &map = g_gui.GetCurrentMap();
		NpcVector npc;
		TileVector toDeleteSpawns;
		for (const auto &spawnPosition : map.spawnsNpc) {
			Tile* tile = map.getTile(spawnPosition);
			if (!tile || !tile->spawnNpc) {
				continue;
			}

			const int32_t radius = tile->spawnNpc->getSize();

			bool empty = true;
			for (int32_t y = -radius; y <= radius; ++y) {
				for (int32_t x = -radius; x <= radius; ++x) {
					Tile* creature_tile = map.getTile(spawnPosition + Position(x, y, 0));
					if (creature_tile && creature_tile->npc && !creature_tile->npc->isSaved()) {
						creature_tile->npc->save();
						npc.push_back(creature_tile->npc);
						empty = false;
					}
				}
			}

			if (empty) {
				toDeleteSpawns.push_back(tile);
			}
		}

		for (Npc* npc : npc) {
			npc->reset();
		}

		BatchAction* batch = editor->getHistoryActions()->createBatch(ACTION_DELETE_TILES);
		Action* action = editor->getHistoryActions()->createAction(batch);

		const size_t count = toDeleteSpawns.size();
		size_t removed = 0;
		for (const auto &tile : toDeleteSpawns) {
			Tile* newtile = tile->deepCopy(map);
			map.removeSpawnNpc(newtile);
			delete newtile->spawnNpc;
			newtile->spawnNpc = nullptr;
			if (++removed % 5 == 0) {
				// update progress bar for each 5 spawns removed
				g_gui.SetLoadDone(100 * removed / count);
			}
			action->addChange(newd Change(newtile));
		}

		batch->addAndCommitAction(action);
		editor->addBatch(batch);

		g_gui.DestroyLoadBar();

		wxString msg;
		msg << removed << " empty npcs spawns removed.";
		g_gui.PopupDialog("Search completed", msg, wxOK);
		g_gui.GetCurrentMap().doChange();
	}
}

void MainMenuBar::OnClearHouseTiles(wxCommandEvent &WXUNUSED(event)) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		return;
	}

	int ret = g_gui.PopupDialog(
		"Clear Invalid House Tiles",
		"Are you sure you want to remove all house tiles that do not belong to a house (this action cannot be undone)?",
		wxYES | wxNO
	);

	if (ret == wxID_YES) {
		// Editor will do the work
		editor->clearInvalidHouseTiles(true);
	}

	g_gui.RefreshView();
}

void MainMenuBar::OnClearModifiedState(wxCommandEvent &WXUNUSED(event)) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		return;
	}

	int ret = g_gui.PopupDialog(
		"Clear Modified State",
		"This will have the same effect as closing the map and opening it again. Do you want to proceed?",
		wxYES | wxNO
	);

	if (ret == wxID_YES) {
		// Editor will do the work
		editor->clearModifiedTileState(true);
	}

	g_gui.RefreshView();
}

void MainMenuBar::OnMapCleanHouseItems(wxCommandEvent &WXUNUSED(event)) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		return;
	}

	int ret = g_gui.PopupDialog(
		"Clear Moveable House Items",
		"Are you sure you want to remove all items inside houses that can be moved (this action cannot be undone)?",
		wxYES | wxNO
	);

	if (ret == wxID_YES) {
		// Editor will do the work
		// editor->removeHouseItems(true);
	}

	g_gui.RefreshView();
}

void MainMenuBar::OnMapEditTowns(wxCommandEvent &WXUNUSED(event)) {
	if (g_gui.GetCurrentEditor()) {
		wxDialog* town_dialog = newd EditTownsDialog(frame, *g_gui.GetCurrentEditor());
		town_dialog->ShowModal();
		town_dialog->Destroy();
	}
}

void MainMenuBar::OnMapEditItems(wxCommandEvent &WXUNUSED(event)) {
	;
}

void MainMenuBar::OnMapEditMonsters(wxCommandEvent &WXUNUSED(event)) {
	;
}

void MainMenuBar::OnMapStatistics(wxCommandEvent &WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	g_gui.CreateLoadBar("Collecting data...");

	Map* map = &g_gui.GetCurrentMap();

	int load_counter = 0;

	uint64_t tile_count = 0;
	uint64_t detailed_tile_count = 0;
	uint64_t blocking_tile_count = 0;
	uint64_t walkable_tile_count = 0;
	double percent_pathable = 0.0;
	double percent_detailed = 0.0;
	uint64_t spawn_monster_count = 0;
	uint64_t spawn_npc_count = 0;
	uint64_t monster_count = 0;
	uint64_t npc_count = 0;
	double monsters_per_spawn = 0.0;
	double npcs_per_spawn = 0.0;

	uint64_t item_count = 0;
	uint64_t loose_item_count = 0;
	uint64_t depot_count = 0;
	uint64_t action_item_count = 0;
	uint64_t unique_item_count = 0;
	uint64_t container_count = 0; // Only includes containers containing more than 1 item

	int town_count = map->towns.count();
	int house_count = map->houses.count();
	std::map<uint32_t, uint32_t> town_sqm_count;
	const Town* largest_town = nullptr;
	uint64_t largest_town_size = 0;
	uint64_t total_house_sqm = 0;
	const House* largest_house = nullptr;
	uint64_t largest_house_size = 0;
	double houses_per_town = 0.0;
	double sqm_per_house = 0.0;
	double sqm_per_town = 0.0;

	for (MapIterator mit = map->begin(); mit != map->end(); ++mit) {
		Tile* tile = (*mit)->get();
		if (load_counter % 8192 == 0) {
			g_gui.SetLoadDone((unsigned int)(int64_t(load_counter) * 95ll / int64_t(map->getTileCount())));
		}

		if (tile->empty()) {
			continue;
		}

		tile_count += 1;

		bool is_detailed = false;
#define ANALYZE_ITEM(_item)                                             \
	{                                                                   \
		item_count += 1;                                                \
		if (!(_item)->isGroundTile() && !(_item)->isBorder()) {         \
			is_detailed = true;                                         \
			const ItemType &it = g_items.getItemType((_item)->getID()); \
			if (it.moveable) {                                          \
				loose_item_count += 1;                                  \
			}                                                           \
			if (it.isDepot()) {                                         \
				depot_count += 1;                                       \
			}                                                           \
			if ((_item)->getActionID() > 0) {                           \
				action_item_count += 1;                                 \
			}                                                           \
			if ((_item)->getUniqueID() > 0) {                           \
				unique_item_count += 1;                                 \
			}                                                           \
			if (Container* c = dynamic_cast<Container*>((_item))) {     \
				if (c->getVector().size()) {                            \
					container_count += 1;                               \
				}                                                       \
			}                                                           \
		}                                                               \
	}
		if (tile->ground) {
			ANALYZE_ITEM(tile->ground);
		}

		for (Item* item : tile->items) {
			ANALYZE_ITEM(item);
		}
#undef ANALYZE_ITEM

		if (tile->spawnMonster) {
			spawn_monster_count += 1;
		}

		if (tile->spawnNpc) {
			spawn_npc_count += 1;
		}

		if (tile->monster) {
			monster_count += 1;
		}

		if (tile->npc) {
			npc_count += 1;
		}

		if (tile->isBlocking()) {
			blocking_tile_count += 1;
		} else {
			walkable_tile_count += 1;
		}

		if (is_detailed) {
			detailed_tile_count += 1;
		}

		load_counter += 1;
	}

	monsters_per_spawn = (spawn_monster_count != 0 ? double(monster_count) / double(spawn_monster_count) : -1.0);
	npcs_per_spawn = (spawn_npc_count != 0 ? double(npc_count) / double(spawn_npc_count) : -1.0);
	percent_pathable = 100.0 * (tile_count != 0 ? double(walkable_tile_count) / double(tile_count) : -1.0);
	percent_detailed = 100.0 * (tile_count != 0 ? double(detailed_tile_count) / double(tile_count) : -1.0);

	load_counter = 0;
	Houses &houses = map->houses;
	for (HouseMap::const_iterator hit = houses.begin(); hit != houses.end(); ++hit) {
		const House* house = hit->second;

		if (load_counter % 64) {
			g_gui.SetLoadDone((unsigned int)(95ll + int64_t(load_counter) * 5ll / int64_t(house_count)));
		}

		if (house->size() > largest_house_size) {
			largest_house = house;
			largest_house_size = house->size();
		}
		total_house_sqm += house->size();
		town_sqm_count[house->townid] += house->size();
	}

	houses_per_town = (town_count != 0 ? double(house_count) / double(town_count) : -1.0);
	sqm_per_house = (house_count != 0 ? double(total_house_sqm) / double(house_count) : -1.0);
	sqm_per_town = (town_count != 0 ? double(total_house_sqm) / double(town_count) : -1.0);

	Towns &towns = map->towns;
	for (std::map<uint32_t, uint32_t>::iterator town_iter = town_sqm_count.begin();
		 town_iter != town_sqm_count.end();
		 ++town_iter) {
		// No load bar for this, load is non-existant
		uint32_t town_id = town_iter->first;
		uint32_t town_sqm = town_iter->second;
		Town* town = towns.getTown(town_id);
		if (town && town_sqm > largest_town_size) {
			largest_town = town;
			largest_town_size = town_sqm;
		} else {
			// Non-existant town!
		}
	}

	g_gui.DestroyLoadBar();

	std::ostringstream os;
	os.setf(std::ios::fixed, std::ios::floatfield);
	os.precision(2);
	os << "Map statistics for the map \"" << map->getMapDescription() << "\"\n";
	os << "\tTile data:\n";
	os << "\t\tTotal number of tiles: " << tile_count << "\n";
	os << "\t\tNumber of pathable tiles: " << walkable_tile_count << "\n";
	os << "\t\tNumber of unpathable tiles: " << blocking_tile_count << "\n";
	if (percent_pathable >= 0.0) {
		os << "\t\tPercent walkable tiles: " << percent_pathable << "%\n";
	}
	os << "\t\tDetailed tiles: " << detailed_tile_count << "\n";
	if (percent_detailed >= 0.0) {
		os << "\t\tPercent detailed tiles: " << percent_detailed << "%\n";
	}

	os << "\tItem data:\n";
	os << "\t\tTotal number of items: " << item_count << "\n";
	os << "\t\tNumber of moveable tiles: " << loose_item_count << "\n";
	os << "\t\tNumber of depots: " << depot_count << "\n";
	os << "\t\tNumber of containers: " << container_count << "\n";
	os << "\t\tNumber of items with Action ID: " << action_item_count << "\n";
	os << "\t\tNumber of items with Unique ID: " << unique_item_count << "\n";

	os << "\tMonster data:\n";
	os << "\t\tTotal monster count: " << monster_count << "\n";
	os << "\t\tTotal monster spawn count: " << spawn_monster_count << "\n";
	os << "\t\tTotal npc count: " << npc_count << "\n";
	os << "\t\tTotal npc spawn count: " << spawn_npc_count << "\n";
	if (monsters_per_spawn >= 0) {
		os << "\t\tMean monsters per spawn: " << monsters_per_spawn << "\n";
	}

	if (npcs_per_spawn >= 0) {
		os << "\t\tMean npcs per spawn: " << npcs_per_spawn << "\n";
	}

	os << "\tTown/House data:\n";
	os << "\t\tTotal number of towns: " << town_count << "\n";
	os << "\t\tTotal number of houses: " << house_count << "\n";
	if (houses_per_town >= 0) {
		os << "\t\tMean houses per town: " << houses_per_town << "\n";
	}
	os << "\t\tTotal amount of housetiles: " << total_house_sqm << "\n";
	if (sqm_per_house >= 0) {
		os << "\t\tMean tiles per house: " << sqm_per_house << "\n";
	}
	if (sqm_per_town >= 0) {
		os << "\t\tMean tiles per town: " << sqm_per_town << "\n";
	}

	if (largest_town) {
		os << "\t\tLargest Town: \"" << largest_town->getName() << "\" (" << largest_town_size << " sqm)\n";
	}
	if (largest_house) {
		os << "\t\tLargest House: \"" << largest_house->name << "\" (" << largest_house_size << " sqm)\n";
	}

	os << "\n";
	os << "Generated by Remere's Map Editor version " + __RME_VERSION__ + "\n";

	wxDialog* dg = newd wxDialog(frame, wxID_ANY, "Map Statistics", wxDefaultPosition, wxDefaultSize, wxRESIZE_BORDER | wxCAPTION | wxCLOSE_BOX);
	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);
	wxTextCtrl* text_field = newd wxTextCtrl(dg, wxID_ANY, wxstr(os.str()), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
	text_field->SetMinSize(wxSize(400, 300));
	topsizer->Add(text_field, wxSizerFlags(5).Expand());

	wxSizer* choicesizer = newd wxBoxSizer(wxHORIZONTAL);
	wxButton* export_button = newd wxButton(dg, wxID_OK, "Export as XML");
	choicesizer->Add(export_button, wxSizerFlags(1).Center());
	export_button->Enable(false);
	choicesizer->Add(newd wxButton(dg, wxID_CANCEL, "OK"), wxSizerFlags(1).Center());
	topsizer->Add(choicesizer, wxSizerFlags(1).Center());
	dg->SetSizerAndFit(topsizer);
	dg->Centre(wxBOTH);

	int ret = dg->ShowModal();

	if (ret == wxID_OK) {
		// std::cout << "XML EXPORT";
	} else if (ret == wxID_CANCEL) {
		// std::cout << "OK";
	}
}

void MainMenuBar::OnMapCleanup(wxCommandEvent &WXUNUSED(event)) {
	int ok = g_gui.PopupDialog("Clean map", "Do you want to remove all invalid items from the map?", wxYES | wxNO);

	if (ok == wxID_YES) {
		g_gui.GetCurrentMap().cleanInvalidTiles(true);
	}
}

void MainMenuBar::OnMapProperties(wxCommandEvent &WXUNUSED(event)) {
	wxDialog* properties = newd MapPropertiesWindow(
		frame,
		static_cast<MapTab*>(g_gui.GetCurrentTab()),
		*g_gui.GetCurrentEditor()
	);

	if (properties->ShowModal() == 0) {
		// FAIL!
		g_gui.CloseAllEditors();
	}
	properties->Destroy();
}

void MainMenuBar::OnToolbars(wxCommandEvent &event) {
	using namespace MenuBar;

	ActionID id = static_cast<ActionID>(event.GetId() - (wxID_HIGHEST + 1));
	switch (id) {
		case VIEW_TOOLBARS_BRUSHES:
			g_gui.ShowToolbar(TOOLBAR_BRUSHES, event.IsChecked());
			g_settings.setInteger(Config::SHOW_TOOLBAR_BRUSHES, event.IsChecked());
			break;
		case VIEW_TOOLBARS_POSITION:
			g_gui.ShowToolbar(TOOLBAR_POSITION, event.IsChecked());
			g_settings.setInteger(Config::SHOW_TOOLBAR_POSITION, event.IsChecked());
			break;
		case VIEW_TOOLBARS_SIZES:
			g_gui.ShowToolbar(TOOLBAR_SIZES, event.IsChecked());
			g_settings.setInteger(Config::SHOW_TOOLBAR_SIZES, event.IsChecked());
			break;
		case VIEW_TOOLBARS_INDICATORS:
			g_gui.ShowToolbar(TOOLBAR_INDICATORS, event.IsChecked());
			g_settings.setInteger(Config::SHOW_TOOLBAR_INDICATORS, event.IsChecked());
			break;
		case VIEW_TOOLBARS_STANDARD:
			g_gui.ShowToolbar(TOOLBAR_STANDARD, event.IsChecked());
			g_settings.setInteger(Config::SHOW_TOOLBAR_STANDARD, event.IsChecked());
			break;
		default:
			break;
	}
}

void MainMenuBar::OnNewView(wxCommandEvent &WXUNUSED(event)) {
	g_gui.NewMapView();
}

void MainMenuBar::OnToggleFullscreen(wxCommandEvent &WXUNUSED(event)) {
	if (frame->IsFullScreen()) {
		frame->ShowFullScreen(false);
	} else {
		frame->ShowFullScreen(true, wxFULLSCREEN_NOBORDER | wxFULLSCREEN_NOCAPTION);
	}
}

void MainMenuBar::OnTakeScreenshot(wxCommandEvent &WXUNUSED(event)) {
	auto path = g_settings.getString(Config::SCREENSHOT_DIRECTORY);
	if (path.size() > 0 && (path.back() == '/' || path.back() == '\\')) {
		path = path + "/";
	}

	g_gui.GetCurrentMapTab()->GetView()->GetCanvas()->TakeScreenshot(
		wxstr(path), wxstr(g_settings.getString(Config::SCREENSHOT_FORMAT))
	);
}

void MainMenuBar::OnZoomIn(wxCommandEvent &event) {
	double zoom = g_gui.GetCurrentZoom();
	g_gui.SetCurrentZoom(zoom - 0.1);
}

void MainMenuBar::OnZoomOut(wxCommandEvent &event) {
	double zoom = g_gui.GetCurrentZoom();
	g_gui.SetCurrentZoom(zoom + 0.1);
}

void MainMenuBar::OnZoomNormal(wxCommandEvent &event) {
	g_gui.SetCurrentZoom(1.0);
}

void MainMenuBar::OnChangeViewSettings(wxCommandEvent &event) {
	g_settings.setInteger(Config::SHOW_ALL_FLOORS, IsItemChecked(MenuBar::SHOW_ALL_FLOORS));
	if (IsItemChecked(MenuBar::SHOW_ALL_FLOORS)) {
		EnableItem(MenuBar::SELECT_MODE_VISIBLE, true);
		EnableItem(MenuBar::SELECT_MODE_LOWER, true);
	} else {
		EnableItem(MenuBar::SELECT_MODE_VISIBLE, false);
		EnableItem(MenuBar::SELECT_MODE_LOWER, false);
		CheckItem(MenuBar::SELECT_MODE_CURRENT, true);
		g_settings.setInteger(Config::SELECTION_TYPE, SELECT_CURRENT_FLOOR);
	}
	g_settings.setInteger(Config::TRANSPARENT_FLOORS, IsItemChecked(MenuBar::GHOST_HIGHER_FLOORS));
	g_settings.setInteger(Config::TRANSPARENT_ITEMS, IsItemChecked(MenuBar::GHOST_ITEMS));
	g_settings.setInteger(Config::SHOW_INGAME_BOX, IsItemChecked(MenuBar::SHOW_INGAME_BOX));
	g_settings.setInteger(Config::SHOW_LIGHTS, IsItemChecked(MenuBar::SHOW_LIGHTS));
	g_settings.setInteger(Config::SHOW_LIGHT_STRENGTH, IsItemChecked(MenuBar::SHOW_LIGHT_STRENGTH));
	g_settings.setInteger(Config::SHOW_GRID, IsItemChecked(MenuBar::SHOW_GRID));
	g_settings.setInteger(Config::SHOW_EXTRA, !IsItemChecked(MenuBar::SHOW_EXTRA));

	g_settings.setInteger(Config::SHOW_SHADE, IsItemChecked(MenuBar::SHOW_SHADE));
	g_settings.setInteger(Config::SHOW_SPECIAL_TILES, IsItemChecked(MenuBar::SHOW_SPECIAL));
	g_settings.setInteger(Config::SHOW_AS_MINIMAP, IsItemChecked(MenuBar::SHOW_AS_MINIMAP));
	g_settings.setInteger(Config::SHOW_ONLY_TILEFLAGS, IsItemChecked(MenuBar::SHOW_ONLY_COLORS));
	g_settings.setInteger(Config::SHOW_ONLY_MODIFIED_TILES, IsItemChecked(MenuBar::SHOW_ONLY_MODIFIED));
	g_settings.setInteger(Config::SHOW_MONSTERS, IsItemChecked(MenuBar::SHOW_MONSTERS));
	g_settings.setInteger(Config::SHOW_SPAWNS_MONSTER, IsItemChecked(MenuBar::SHOW_SPAWNS_MONSTER));
	g_settings.setInteger(Config::SHOW_NPCS, IsItemChecked(MenuBar::SHOW_NPCS));
	g_settings.setInteger(Config::SHOW_SPAWNS_NPC, IsItemChecked(MenuBar::SHOW_SPAWNS_NPC));
	g_settings.setInteger(Config::SHOW_HOUSES, IsItemChecked(MenuBar::SHOW_HOUSES));
	g_settings.setInteger(Config::HIGHLIGHT_ITEMS, IsItemChecked(MenuBar::HIGHLIGHT_ITEMS));
	g_settings.setInteger(Config::SHOW_BLOCKING, IsItemChecked(MenuBar::SHOW_PATHING));
	g_settings.setInteger(Config::SHOW_TOOLTIPS, IsItemChecked(MenuBar::SHOW_TOOLTIPS));
	g_settings.setInteger(Config::SHOW_PREVIEW, IsItemChecked(MenuBar::SHOW_PREVIEW));
	g_settings.setInteger(Config::SHOW_WALL_HOOKS, IsItemChecked(MenuBar::SHOW_WALL_HOOKS));
	g_settings.setInteger(Config::SHOW_PICKUPABLES, IsItemChecked(MenuBar::SHOW_PICKUPABLES));
	g_settings.setInteger(Config::SHOW_MOVEABLES, IsItemChecked(MenuBar::SHOW_MOVEABLES));

	g_gui.RefreshView();
	g_gui.root->GetAuiToolBar()->UpdateIndicators();
}

void MainMenuBar::OnChangeFloor(wxCommandEvent &event) {
	// Workaround to stop events from looping
	if (checking_programmaticly) {
		return;
	}

	for (int i = 0; i < 16; ++i) {
		if (IsItemChecked(MenuBar::ActionID(MenuBar::FLOOR_0 + i))) {
			g_gui.ChangeFloor(i);
		}
	}
}

void MainMenuBar::OnMinimapWindow(wxCommandEvent &event) {
	g_gui.CreateMinimap();
}

void MainMenuBar::OnActionsHistoryWindow(wxCommandEvent &WXUNUSED(event)) {
	g_gui.ShowActionsWindow();
}

void MainMenuBar::OnNewPalette(wxCommandEvent &event) {
	g_gui.NewPalette();
}

void MainMenuBar::OnSelectTerrainPalette(wxCommandEvent &WXUNUSED(event)) {
	g_gui.SelectPalettePage(TILESET_TERRAIN);
}

void MainMenuBar::OnSelectDoodadPalette(wxCommandEvent &WXUNUSED(event)) {
	g_gui.SelectPalettePage(TILESET_DOODAD);
}

void MainMenuBar::OnSelectItemPalette(wxCommandEvent &WXUNUSED(event)) {
	g_gui.SelectPalettePage(TILESET_ITEM);
}

void MainMenuBar::OnSelectHousePalette(wxCommandEvent &WXUNUSED(event)) {
	g_gui.SelectPalettePage(TILESET_HOUSE);
}

void MainMenuBar::OnSelectMonsterPalette(wxCommandEvent &WXUNUSED(event)) {
	g_gui.SelectPalettePage(TILESET_MONSTER);
}

void MainMenuBar::OnSelectNpcPalette(wxCommandEvent &WXUNUSED(event)) {
	g_gui.SelectPalettePage(TILESET_NPC);
}

void MainMenuBar::OnSelectWaypointPalette(wxCommandEvent &WXUNUSED(event)) {
	g_gui.SelectPalettePage(TILESET_WAYPOINT);
}

void MainMenuBar::OnSelectZonesPalette(wxCommandEvent &WXUNUSED(event)) {
	g_gui.SelectPalettePage(TILESET_ZONES);
}

void MainMenuBar::OnSelectRawPalette(wxCommandEvent &WXUNUSED(event)) {
	g_gui.SelectPalettePage(TILESET_RAW);
}

void MainMenuBar::OnStartLive(wxCommandEvent &event) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		g_gui.PopupDialog("Error", "You need to have a map open to start a live mapping session.", wxOK);
		return;
	}
	if (editor->IsLive()) {
		g_gui.PopupDialog("Error", "You can not start two live servers on the same map (or a server using a remote map).", wxOK);
		return;
	}

	wxDialog* live_host_dlg = newd wxDialog(frame, wxID_ANY, "Host Live Server", wxDefaultPosition, wxDefaultSize);

	wxSizer* top_sizer = newd wxBoxSizer(wxVERTICAL);
	wxFlexGridSizer* gsizer = newd wxFlexGridSizer(2, 10, 10);
	gsizer->AddGrowableCol(0, 2);
	gsizer->AddGrowableCol(1, 3);

	// Data fields
	wxTextCtrl* hostname;
	wxSpinCtrl* port;
	wxTextCtrl* password;
	wxCheckBox* allow_copy;

	gsizer->Add(newd wxStaticText(live_host_dlg, wxID_ANY, "Server Name:"));
	gsizer->Add(hostname = newd wxTextCtrl(live_host_dlg, wxID_ANY, "RME Live Server"), 0, wxEXPAND);

	gsizer->Add(newd wxStaticText(live_host_dlg, wxID_ANY, "Port:"));
	gsizer->Add(port = newd wxSpinCtrl(live_host_dlg, wxID_ANY, "31313", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 65535, 31313), 0, wxEXPAND);

	gsizer->Add(newd wxStaticText(live_host_dlg, wxID_ANY, "Password:"));
	gsizer->Add(password = newd wxTextCtrl(live_host_dlg, wxID_ANY), 0, wxEXPAND);

	top_sizer->Add(gsizer, 0, wxALL, 20);

	top_sizer->Add(allow_copy = newd wxCheckBox(live_host_dlg, wxID_ANY, "Allow copy & paste between maps."), 0, wxRIGHT | wxLEFT, 20);
	allow_copy->SetToolTip("Allows remote clients to copy & paste from the hosted map to local maps.");

	wxSizer* ok_sizer = newd wxBoxSizer(wxHORIZONTAL);
	ok_sizer->Add(newd wxButton(live_host_dlg, wxID_OK, "OK"), 1, wxCENTER);
	ok_sizer->Add(newd wxButton(live_host_dlg, wxID_CANCEL, "Cancel"), wxCENTER, 1);
	top_sizer->Add(ok_sizer, 0, wxCENTER | wxALL, 20);

	live_host_dlg->SetSizerAndFit(top_sizer);

	while (true) {
		int ret = live_host_dlg->ShowModal();
		if (ret == wxID_OK) {
			LiveServer* liveServer = editor->StartLiveServer();
			liveServer->setName(hostname->GetValue());
			liveServer->setPassword(password->GetValue());
			liveServer->setPort(port->GetValue());

			const wxString &error = liveServer->getLastError();
			if (!error.empty()) {
				g_gui.PopupDialog(live_host_dlg, "Error", error, wxOK);
				editor->CloseLiveServer();
				continue;
			}

			if (!liveServer->bind()) {
				g_gui.PopupDialog("Socket Error", "Could not bind socket! Try another port?", wxOK);
				editor->CloseLiveServer();
			} else {
				liveServer->createLogWindow(g_gui.tabbook);
			}
			break;
		} else {
			break;
		}
	}
	live_host_dlg->Destroy();
	Update();
}

void MainMenuBar::OnJoinLive(wxCommandEvent &event) {
	wxDialog* live_join_dlg = newd wxDialog(frame, wxID_ANY, "Join Live Server", wxDefaultPosition, wxDefaultSize);

	wxSizer* top_sizer = newd wxBoxSizer(wxVERTICAL);
	wxFlexGridSizer* gsizer = newd wxFlexGridSizer(2, 10, 10);
	gsizer->AddGrowableCol(0, 2);
	gsizer->AddGrowableCol(1, 3);

	// Data fields
	wxTextCtrl* name;
	wxTextCtrl* ip;
	wxSpinCtrl* port;
	wxTextCtrl* password;

	gsizer->Add(newd wxStaticText(live_join_dlg, wxID_ANY, "Name:"));
	gsizer->Add(name = newd wxTextCtrl(live_join_dlg, wxID_ANY, ""), 0, wxEXPAND);

	gsizer->Add(newd wxStaticText(live_join_dlg, wxID_ANY, "IP:"));
	gsizer->Add(ip = newd wxTextCtrl(live_join_dlg, wxID_ANY, "localhost"), 0, wxEXPAND);

	gsizer->Add(newd wxStaticText(live_join_dlg, wxID_ANY, "Port:"));
	gsizer->Add(port = newd wxSpinCtrl(live_join_dlg, wxID_ANY, "31313", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 65535, 31313), 0, wxEXPAND);

	gsizer->Add(newd wxStaticText(live_join_dlg, wxID_ANY, "Password:"));
	gsizer->Add(password = newd wxTextCtrl(live_join_dlg, wxID_ANY), 0, wxEXPAND);

	top_sizer->Add(gsizer, 0, wxALL, 20);

	wxSizer* ok_sizer = newd wxBoxSizer(wxHORIZONTAL);
	ok_sizer->Add(newd wxButton(live_join_dlg, wxID_OK, "OK"), 1, wxRIGHT);
	ok_sizer->Add(newd wxButton(live_join_dlg, wxID_CANCEL, "Cancel"), 1, wxRIGHT);
	top_sizer->Add(ok_sizer, 0, wxCENTER | wxALL, 20);

	live_join_dlg->SetSizerAndFit(top_sizer);

	while (true) {
		int ret = live_join_dlg->ShowModal();
		if (ret == wxID_OK) {
			LiveClient* liveClient = newd LiveClient();
			liveClient->setPassword(password->GetValue());

			wxString tmp = name->GetValue();
			if (tmp.empty()) {
				tmp = "User";
			}
			liveClient->setName(tmp);

			const wxString &error = liveClient->getLastError();
			if (!error.empty()) {
				g_gui.PopupDialog(live_join_dlg, "Error", error, wxOK);
				delete liveClient;
				continue;
			}

			const wxString &address = ip->GetValue();
			int32_t portNumber = port->GetValue();

			liveClient->createLogWindow(g_gui.tabbook);
			if (!liveClient->connect(nstr(address), portNumber)) {
				g_gui.PopupDialog("Connection Error", liveClient->getLastError(), wxOK);
				delete liveClient;
			}

			break;
		} else {
			break;
		}
	}
	live_join_dlg->Destroy();
	Update();
}

void MainMenuBar::OnCloseLive(wxCommandEvent &event) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (editor && editor->IsLive()) {
		g_gui.CloseLiveEditors(&editor->GetLive());
	}

	Update();
}

void MainMenuBar::SearchItems(bool unique, bool action, bool container, bool writable, bool onSelection /* = false*/) {
	if (!unique && !action && !container && !writable) {
		return;
	}

	if (!g_gui.IsEditorOpen()) {
		return;
	}

	if (onSelection) {
		g_gui.CreateLoadBar("Searching on selected area...");
	} else {
		g_gui.CreateLoadBar("Searching on map...");
	}

	OnSearchForStuff::Searcher searcher;
	searcher.search_unique = unique;
	searcher.search_action = action;
	searcher.search_container = container;
	searcher.search_writeable = writable;

	foreach_ItemOnMap(g_gui.GetCurrentMap(), searcher, onSelection);
	searcher.sort();
	std::vector<std::pair<Tile*, Item*>> &found = searcher.found;

	g_gui.DestroyLoadBar();

	SearchResultWindow* result = g_gui.ShowSearchWindow();
	result->Clear();

	for (std::vector<std::pair<Tile*, Item*>>::iterator iter = found.begin(); iter != found.end(); ++iter) {
		result->AddPosition(searcher.desc(iter->second), iter->first->getPosition());
	}
}

void MainMenuBar::OnSearchForDuplicateItemsOnMap(wxCommandEvent &WXUNUSED(event)) {
	SearchDuplicatedItems(false);
}

void MainMenuBar::OnSearchForDuplicateItemsOnSelection(wxCommandEvent &WXUNUSED(event)) {
	SearchDuplicatedItems(true);
}

void MainMenuBar::OnRemoveForDuplicateItemsOnMap(wxCommandEvent &WXUNUSED(event)) {
	RemoveDuplicatesItems(false);
}

void MainMenuBar::OnRemoveForDuplicateItemsOnSelection(wxCommandEvent &WXUNUSED(event)) {
	RemoveDuplicatesItems(true);
}

void MainMenuBar::OnSearchForWallsUponWallsOnMap(wxCommandEvent &WXUNUSED(event)) {
	SearchWallsUponWalls(false);
}

void MainMenuBar::OnSearchForWallsUponWallsOnSelection(wxCommandEvent &WXUNUSED(event)) {
	SearchWallsUponWalls(true);
}

namespace SearchDuplicatedItems {
	struct condition {
		std::unordered_set<Tile*> foundTiles;

		void operator()(Map &map, Tile* tile, Item* item, long long done) {
			if (done % 0x8000 == 0) {
				g_gui.SetLoadDone((unsigned int)(100 * done / map.getTileCount()));
			}

			if (!tile) {
				return;
			}

			if (!item) {
				return;
			}

			if (item->isGroundTile()) {
				return;
			}

			if (foundTiles.count(tile) == 0) {
				std::unordered_set<int> itemIDs;
				for (Item* existingItem : tile->items) {
					if (itemIDs.count(existingItem->getID()) > 0) {
						foundTiles.insert(tile);
						break;
					}
					itemIDs.insert(existingItem->getID());
				}
			}
		}
	};
}

void MainMenuBar::SearchDuplicatedItems(bool onSelection /* = false*/) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	if (onSelection) {
		g_gui.CreateLoadBar("Searching on selected area...");
	} else {
		g_gui.CreateLoadBar("Searching on map...");
	}

	SearchDuplicatedItems::condition finder;

	foreach_ItemOnMap(g_gui.GetCurrentMap(), finder, onSelection);
	std::unordered_set<Tile*> &foundTiles = finder.foundTiles;

	g_gui.DestroyLoadBar();

	size_t setSize = foundTiles.size();

	wxString msg;
	msg << setSize << " duplicate items founded.";

	g_gui.PopupDialog("Search completed", msg, wxOK);

	SearchResultWindow* result = g_gui.ShowSearchWindow();
	result->Clear();
	for (const Tile* tile : foundTiles) {
		result->AddPosition("Duplicate items", tile->getPosition());
	}
}

namespace RemoveDuplicatesItems {
	struct condition {
		bool operator()(Map &map, Tile* tile, Item* item, long long removed, long long done) {
			if (done % 0x8000 == 0) {
				g_gui.SetLoadDone((unsigned int)(100 * done / map.getTileCount()));
			}

			if (!tile) {
				return false;
			}

			if (!item) {
				return false;
			}

			if (item->isGroundTile()) {
				return false;
			}

			if (item->isMoveable() && item->hasElevation()) {
				return false;
			}

			if (item->getActionID() > 0 || item->getUniqueID() > 0) {
				return false;
			}

			std::unordered_set<int> itemIDsDuplicates;
			for (Item* itemInTile : tile->items) {
				if (itemInTile && itemInTile->getID() == item->getID()) {
					if (itemIDsDuplicates.count(itemInTile->getID()) > 0) {
						itemIDsDuplicates.clear();
						return true;
					}
					itemIDsDuplicates.insert(itemInTile->getID());
				}
			}

			itemIDsDuplicates.clear();
			return false;
		}
	};
}

void MainMenuBar::RemoveDuplicatesItems(bool onSelection /* = false*/) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	int ok = g_gui.PopupDialog("Remove Duplicate Items", "Do you want to remove all duplicates items from the map?", wxYES | wxNO);

	if (ok == wxID_YES) {
		g_gui.GetCurrentEditor()->getSelection().clear();
		g_gui.GetCurrentEditor()->clearActions();

		RemoveDuplicatesItems::condition func;

		if (onSelection) {
			g_gui.CreateLoadBar("Searching on selected area for items to remove...");
		} else {
			g_gui.CreateLoadBar("Searching on map for items to remove...");
		}

		long long removed = RemoveItemDuplicateOnMap(g_gui.GetCurrentMap(), func, onSelection);

		g_gui.DestroyLoadBar();

		wxString msg;
		msg << removed << " duplicate items deleted.";

		g_gui.PopupDialog("Search completed", msg, wxOK);

		g_gui.GetCurrentMap().doChange();
	}
}

namespace SearchWallsUponWalls {
	struct condition {
		std::unordered_set<Tile*> foundTiles;

		void operator()(const Map &map, Tile* tile, const Item* item, long long done) {
			if (done % 0x8000 == 0) {
				g_gui.SetLoadDone(static_cast<unsigned int>(100 * done / map.getTileCount()));
			}

			if (!tile) {
				return;
			}

			if (!item) {
				return;
			}

			if (!item->isBlockMissiles()) {
				return;
			}

			if (!item->isWall() && !item->isDoor()) {
				return;
			}

			std::unordered_set<int> itemIDs;
			for (const Item* itemInTile : tile->items) {
				if (!itemInTile || (!itemInTile->isWall() && !itemInTile->isDoor())) {
					continue;
				}

				if (item->getID() != itemInTile->getID()) {
					itemIDs.insert(itemInTile->getID());
				}
			}

			if (!itemIDs.empty()) {
				foundTiles.insert(tile);
			}

			itemIDs.clear();
		}
	};
}

void MainMenuBar::SearchWallsUponWalls(bool onSelection /* = false*/) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	if (onSelection) {
		g_gui.CreateLoadBar("Searching on selected area...");
	} else {
		g_gui.CreateLoadBar("Searching on map...");
	}

	SearchWallsUponWalls::condition finder;

	foreach_ItemOnMap(g_gui.GetCurrentMap(), finder, onSelection);

	const std::unordered_set<Tile*> &foundTiles = finder.foundTiles;

	g_gui.DestroyLoadBar();

	size_t setSize = foundTiles.size();

	wxString msg;
	msg << setSize << " items under walls and doors founded.";

	g_gui.PopupDialog("Search completed", msg, wxOK);

	SearchResultWindow* result = g_gui.ShowSearchWindow();
	result->Clear();
	for (const Tile* tile : foundTiles) {
		result->AddPosition("Item Under", tile->getPosition());
	}
}
