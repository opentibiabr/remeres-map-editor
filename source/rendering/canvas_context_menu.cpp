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

void MapCanvas::OnCopy(wxCommandEvent &WXUNUSED(event)) {
	if (g_gui.IsSelectionMode()) {
		editor.copybuffer.copy(editor, GetFloor());
	}
}

void MapCanvas::OnCut(wxCommandEvent &WXUNUSED(event)) {
	if (g_gui.IsSelectionMode()) {
		editor.copybuffer.cut(editor, GetFloor());
	}
	g_gui.RefreshView();
}

void MapCanvas::OnPaste(wxCommandEvent &WXUNUSED(event)) {
	g_gui.DoPaste();
	g_gui.RefreshView();
}

void MapCanvas::OnDelete(wxCommandEvent &WXUNUSED(event)) {
	editor.destroySelection();
	g_gui.RefreshView();
}

void MapCanvas::OnCopyPosition(wxCommandEvent &WXUNUSED(event)) {
	if (editor.hasSelection()) {
		auto minPos = editor.getSelection().minPosition();
		auto maxPos = editor.getSelection().maxPosition();
		if (minPos != maxPos) {
			posToClipboard(minPos.x, minPos.y, minPos.z, maxPos.x, maxPos.y, maxPos.z, g_settings.getInteger(Config::COPY_POSITION_FORMAT));
			return;
		}
	}

	MapTab* tab = g_gui.GetCurrentMapTab();
	if (tab) {
		MapCanvas* canvas = tab->GetCanvas();
		int x, y;
		int z = canvas->GetFloor();
		canvas->MouseToMap(&x, &y);
		posToClipboard(x, y, z, g_settings.getInteger(Config::COPY_POSITION_FORMAT));
	}
}

void MapCanvas::OnCopyItemId(wxCommandEvent &WXUNUSED(event)) {
	ASSERT(editor.getSelection().size() == 1);

	if (wxTheClipboard->Open()) {
		Tile* tile = editor.getSelection().getSelectedTile();
		ItemVector selected_items = tile->getSelectedItems();
		ASSERT(selected_items.size() == 1);

		const Item* item = selected_items.front();

		wxTextDataObject* obj = new wxTextDataObject();
		obj->SetText(i2ws(item->getID()));
		wxTheClipboard->SetData(obj);

		wxTheClipboard->Close();
	}
}

void MapCanvas::OnCopyName(wxCommandEvent &WXUNUSED(event)) {
	ASSERT(editor.getSelection().size() == 1);

	if (wxTheClipboard->Open()) {
		Tile* tile = editor.getSelection().getSelectedTile();
		ItemVector selected_items = tile->getSelectedItems();
		ASSERT(selected_items.size() == 1);

		const Item* item = selected_items.front();

		wxTextDataObject* obj = new wxTextDataObject();
		obj->SetText(wxstr(item->getName()));
		wxTheClipboard->SetData(obj);

		wxTheClipboard->Close();
	}
}

void MapCanvas::OnBrowseTile(wxCommandEvent &WXUNUSED(event)) {
	if (editor.getSelection().size() != 1) {
		return;
	}

	Tile* tile = editor.getSelection().getSelectedTile();
	if (!tile) {
		return;
	}
	ASSERT(tile->isSelected());
	Tile* new_tile = tile->deepCopy(editor.getMap());

	wxDialog* w = new BrowseTileWindow(g_gui.root, new_tile, wxPoint(cursor_x, cursor_y));

	int ret = w->ShowModal();
	if (ret != 0) {
		Action* action = editor.createAction(ACTION_DELETE_TILES);
		action->addChange(newd Change(new_tile));
		editor.addAction(action);
		editor.updateActions();
	} else {
		// Cancel
		delete new_tile;
	}

	w->Destroy();
}

void MapCanvas::OnRotateItem(wxCommandEvent &WXUNUSED(event)) {
	if (!editor.hasSelection()) {
		return;
	}

	Selection &selection = editor.getSelection();
	if (selection.size() != 1) {
		return;
	}

	Tile* tile = selection.getSelectedTile();
	ItemVector items = tile->getSelectedItems();
	if (items.empty()) {
		return;
	}

	Item* item = items.front();
	if (!item || !item->isRoteable()) {
		return;
	}

	Action* action = editor.createAction(ACTION_ROTATE_ITEM);
	Tile* new_tile = tile->deepCopy(editor.getMap());
	Item* new_item = new_tile->getSelectedItems().front();
	new_item->doRotate();
	action->addChange(new Change(new_tile));

	editor.addAction(action);
	editor.updateActions();
	g_gui.RefreshView();
}

void MapCanvas::OnGotoDestination(wxCommandEvent &WXUNUSED(event)) {
	Tile* tile = editor.getSelection().getSelectedTile();
	ItemVector selected_items = tile->getSelectedItems();
	ASSERT(selected_items.size() > 0);
	Teleport* teleport = dynamic_cast<Teleport*>(selected_items.front());
	if (teleport) {
		Position pos = teleport->getDestination();
		g_gui.SetScreenCenterPosition(pos);
	}
}

void MapCanvas::OnCopyDestination(wxCommandEvent &WXUNUSED(event)) {
	Tile* tile = editor.getSelection().getSelectedTile();
	ItemVector selected_items = tile->getSelectedItems();
	ASSERT(selected_items.size() > 0);

	Teleport* teleport = dynamic_cast<Teleport*>(selected_items.front());
	if (teleport) {
		const Position &destination = teleport->getDestination();
		int format = g_settings.getInteger(Config::COPY_POSITION_FORMAT);
		posToClipboard(destination.x, destination.y, destination.z, format);
	}
}

void MapCanvas::OnSwitchDoor(wxCommandEvent &WXUNUSED(event)) {
	Tile* tile = editor.getSelection().getSelectedTile();

	Action* action = editor.createAction(ACTION_SWITCHDOOR);

	Tile* new_tile = tile->deepCopy(editor.getMap());

	ItemVector selected_items = new_tile->getSelectedItems();
	ASSERT(selected_items.size() > 0);

	DoorBrush::switchDoor(selected_items.front());

	action->addChange(newd Change(new_tile));

	editor.addAction(action);
	editor.updateActions();
	g_gui.RefreshView();
}

void MapCanvas::OnSelectRAWBrush(wxCommandEvent &WXUNUSED(event)) {
	if (editor.getSelection().size() != 1) {
		return;
	}
	Tile* tile = editor.getSelection().getSelectedTile();
	if (!tile) {
		return;
	}
	Item* item = tile->getTopSelectedItem();

	if (item && item->getRAWBrush()) {
		g_gui.SelectBrush(item->getRAWBrush(), TILESET_RAW);
	}
}

void MapCanvas::OnSelectGroundBrush(wxCommandEvent &WXUNUSED(event)) {
	if (editor.getSelection().size() != 1) {
		return;
	}
	Tile* tile = editor.getSelection().getSelectedTile();
	if (!tile) {
		return;
	}
	GroundBrush* bb = tile->getGroundBrush();

	if (bb) {
		g_gui.SelectBrush(bb, TILESET_TERRAIN);
	}
}

void MapCanvas::OnSelectDoodadBrush(wxCommandEvent &WXUNUSED(event)) {
	if (editor.getSelection().size() != 1) {
		return;
	}
	Tile* tile = editor.getSelection().getSelectedTile();
	if (!tile) {
		return;
	}
	Item* item = tile->getTopSelectedItem();

	if (item) {
		g_gui.SelectBrush(item->getDoodadBrush(), TILESET_DOODAD);
	}
}

void MapCanvas::OnSelectDoorBrush(wxCommandEvent &WXUNUSED(event)) {
	if (editor.getSelection().size() != 1) {
		return;
	}
	Tile* tile = editor.getSelection().getSelectedTile();
	if (!tile) {
		return;
	}
	Item* item = tile->getTopSelectedItem();

	if (item) {
		g_gui.SelectBrush(item->getDoorBrush(), TILESET_TERRAIN);
	}
}

void MapCanvas::OnSelectWallBrush(wxCommandEvent &WXUNUSED(event)) {
	if (editor.getSelection().size() != 1) {
		return;
	}
	Tile* tile = editor.getSelection().getSelectedTile();
	if (!tile) {
		return;
	}
	Item* wall = tile->getWall();
	WallBrush* wb = wall->getWallBrush();

	if (wb) {
		g_gui.SelectBrush(wb, TILESET_TERRAIN);
	}
}

void MapCanvas::OnSelectCarpetBrush(wxCommandEvent &WXUNUSED(event)) {
	if (editor.getSelection().size() != 1) {
		return;
	}
	Tile* tile = editor.getSelection().getSelectedTile();
	if (!tile) {
		return;
	}
	Item* wall = tile->getCarpet();
	CarpetBrush* cb = wall->getCarpetBrush();

	if (cb) {
		g_gui.SelectBrush(cb);
	}
}

void MapCanvas::OnSelectTableBrush(wxCommandEvent &WXUNUSED(event)) {
	if (editor.getSelection().size() != 1) {
		return;
	}
	Tile* tile = editor.getSelection().getSelectedTile();
	if (!tile) {
		return;
	}
	Item* wall = tile->getTable();
	TableBrush* tb = wall->getTableBrush();

	if (tb) {
		g_gui.SelectBrush(tb);
	}
}

void MapCanvas::OnSelectHouseBrush(wxCommandEvent &WXUNUSED(event)) {
	Tile* tile = editor.getSelection().getSelectedTile();
	if (!tile) {
		return;
	}

	if (tile->isHouseTile()) {
		House* house = editor.getMap().houses.getHouse(tile->getHouseID());
		if (house) {
			g_gui.house_brush->setHouse(house);
			g_gui.SelectBrush(g_gui.house_brush, TILESET_HOUSE);
		}
	}
}

void MapCanvas::OnSelectMonsterBrush(wxCommandEvent &WXUNUSED(event)) {
	Tile* tile = editor.getSelection().getSelectedTile();
	if (!tile) {
		return;
	}

	if (const auto monster = tile->getTopMonster(); monster) {
		g_gui.SelectBrush(monster->getBrush(), TILESET_MONSTER);
	}
}

void MapCanvas::OnSelectSpawnBrush(wxCommandEvent &WXUNUSED(event)) {
	g_gui.SelectBrush(g_gui.spawn_brush, TILESET_MONSTER);
}

void MapCanvas::OnSelectNpcBrush(wxCommandEvent &WXUNUSED(event)) {
	Tile* tile = editor.getSelection().getSelectedTile();
	if (!tile) {
		return;
	}

	if (tile->npc) {
		g_gui.SelectBrush(tile->npc->getBrush(), TILESET_NPC);
	}
}

void MapCanvas::OnSelectSpawnNpcBrush(wxCommandEvent &WXUNUSED(event)) {
	g_gui.SelectBrush(g_gui.spawn_npc_brush, TILESET_NPC);
}

void MapCanvas::OnSelectMoveTo(wxCommandEvent &WXUNUSED(event)) {
	if (editor.selection.size() != 1) {
		return;
	}

	Tile* tile = editor.selection.getSelectedTile();
	if (!tile) {
		return;
	}
	ASSERT(tile->isSelected());
	Tile* new_tile = tile->deepCopy(editor.map);

	wxDialog* w = nullptr;

	ItemVector selected_items = new_tile->getSelectedItems();

	Item* item = nullptr;
	int count = 0;
	for (ItemVector::iterator it = selected_items.begin(); it != selected_items.end(); ++it) {
		++count;
		if ((*it)->isSelected()) {
			item = *it;
		}
	}

	if (item) {
		w = newd TilesetWindow(g_gui.root, &editor.map, new_tile, item);
	} else {
		return;
	}

	int ret = w->ShowModal();
	if (ret != 0) {
		Action* action = editor.actionQueue->createAction(ACTION_CHANGE_PROPERTIES);
		action->addChange(newd Change(new_tile));
		editor.addAction(action);

		g_gui.RebuildPalettes();
	} else {
		// Cancel!
		delete new_tile;
	}
	w->Destroy();
}

void MapCanvas::OnProperties(wxCommandEvent &WXUNUSED(event)) {
	if (editor.getSelection().size() != 1) {
		return;
	}

	const auto tile = editor.getSelection().getSelectedTile();
	if (!tile) {
		return;
	}
	ASSERT(tile->isSelected());
	const auto newTile = tile->deepCopy(editor.getMap());

	wxDialog* w = nullptr;

	if (newTile->spawnMonster && g_settings.getInteger(Config::SHOW_SPAWNS_MONSTER)) {
		w = newd OldPropertiesWindow(g_gui.root, &editor.getMap(), newTile, newTile->spawnMonster);
	} else if (!newTile->monsters.empty() && g_settings.getInteger(Config::SHOW_MONSTERS)) {
		std::vector<Monster*> selectedMonsters = newTile->getSelectedMonsters();

		const auto it = std::ranges::find_if(selectedMonsters | std::views::reverse, [&](const auto itMonster) {
			return itMonster->isSelected();
		});

		if (it == selectedMonsters.rend()) {
			return;
		}

		w = newd OldPropertiesWindow(g_gui.root, &editor.getMap(), newTile, *it);
	} else if (newTile->npc && g_settings.getInteger(Config::SHOW_NPCS)) {
		w = newd OldPropertiesWindow(g_gui.root, &editor.getMap(), newTile, newTile->npc);
	} else if (newTile->spawnNpc && g_settings.getInteger(Config::SHOW_SPAWNS_NPC)) {
		w = newd OldPropertiesWindow(g_gui.root, &editor.getMap(), newTile, newTile->spawnNpc);
	} else {
		const auto selectedItems = newTile->getSelectedItems();

		const auto it = std::ranges::find_if(selectedItems | std::views::reverse, [&](const auto itItem) {
			return itItem->isSelected();
		});

		if (it == selectedItems.rend()) {
			return;
		}

		if (!g_settings.getInteger(Config::USE_OLD_ITEM_PROPERTIES_WINDOW)) {
			w = newd PropertiesWindow(g_gui.root, &editor.getMap(), newTile, *it);
		} else {
			w = newd OldPropertiesWindow(g_gui.root, &editor.getMap(), newTile, *it);
		}
	}

	if (w->ShowModal() != 0) {
		const auto action = editor.createAction(ACTION_CHANGE_PROPERTIES);
		action->addChange(newd Change(newTile));
		editor.addAction(action);
	} else {
		// Cancel!
		delete newTile;
	}
	w->Destroy();
}

MapPopupMenu::MapPopupMenu(Editor &editor) :
	wxMenu(""), editor(editor) {
	////
}

MapPopupMenu::~MapPopupMenu() {
	////
}

void MapPopupMenu::Update() {
	// Clear the menu of all items
	while (GetMenuItemCount() != 0) {
		wxMenuItem* m_item = FindItemByPosition(0);
		// If you add a submenu, this won't delete it.
		Delete(m_item);
	}

	bool anything_selected = editor.hasSelection();

	wxMenuItem* cutItem = Append(MAP_POPUP_MENU_CUT, "&Cut\tCTRL+X", "Cut out all selected items");
	cutItem->Enable(anything_selected);

	wxMenuItem* copyItem = Append(MAP_POPUP_MENU_COPY, "&Copy\tCTRL+C", "Copy all selected items");
	copyItem->Enable(anything_selected);

	wxMenuItem* copyPositionItem = Append(MAP_POPUP_MENU_COPY_POSITION, "&Copy Position", "Copy the position as a lua table");
	copyPositionItem->Enable(true);

	wxMenuItem* pasteItem = Append(MAP_POPUP_MENU_PASTE, "&Paste\tCTRL+V", "Paste items in the copybuffer here");
	pasteItem->Enable(editor.copybuffer.canPaste());

	wxMenuItem* deleteItem = Append(MAP_POPUP_MENU_DELETE, "&Delete\tDEL", "Removes all seleceted items");
	deleteItem->Enable(anything_selected);

	if (anything_selected) {
		if (editor.getSelection().size() == 1) {
			Tile* tile = editor.getSelection().getSelectedTile();
			ItemVector selected_items = tile->getSelectedItems();
			std::vector<Monster*> selectedMonsters = tile->getSelectedMonsters();

			bool hasWall = false;
			bool hasCarpet = false;
			bool hasTable = false;
			Item* topItem = nullptr;
			Item* topSelectedItem = (selected_items.size() == 1 ? selected_items.back() : nullptr);
			Monster* topMonster = nullptr;
			Monster* topSelectedMonster = (selectedMonsters.size() == 1 ? selectedMonsters.back() : nullptr);
			SpawnMonster* topSpawnMonster = tile->spawnMonster;
			Npc* topNpc = tile->npc;
			SpawnNpc* topSpawnNpc = tile->spawnNpc;

			for (auto* item : tile->items) {
				if (item->isWall()) {
					Brush* wb = item->getWallBrush();
					if (wb && wb->visibleInPalette()) {
						hasWall = true;
					}
				}
				if (item->isTable()) {
					Brush* tb = item->getTableBrush();
					if (tb && tb->visibleInPalette()) {
						hasTable = true;
					}
				}
				if (item->isCarpet()) {
					Brush* cb = item->getCarpetBrush();
					if (cb && cb->visibleInPalette()) {
						hasCarpet = true;
					}
				}
				if (item->isSelected()) {
					topItem = item;
				}
			}
			if (!topItem) {
				topItem = tile->ground;
			}

			AppendSeparator();

			if (topSelectedItem) {
				Append(MAP_POPUP_MENU_COPY_ITEM_ID, "Copy Item Id", "Copy the id of this item");
				Append(MAP_POPUP_MENU_COPY_NAME, "Copy Item Name", "Copy the name of this item");
				AppendSeparator();
			}

			if (topSelectedItem || topMonster || topSelectedMonster || topNpc || topItem) {
				Teleport* teleport = dynamic_cast<Teleport*>(topSelectedItem);
				if (topSelectedItem && (topSelectedItem->isBrushDoor() || topSelectedItem->isRoteable() || teleport)) {

					if (topSelectedItem->isRoteable()) {
						Append(MAP_POPUP_MENU_ROTATE, "&Rotate item", "Rotate this item");
					}

					if (teleport) {
						bool enabled = teleport->hasDestination();
						wxMenuItem* goto_menu = Append(MAP_POPUP_MENU_GOTO, "&Go To Destination", "Go to the destination of this teleport");
						goto_menu->Enable(enabled);
						wxMenuItem* dest_menu = Append(MAP_POPUP_MENU_COPY_DESTINATION, "Copy &Destination", "Copy the destination of this teleport");
						dest_menu->Enable(enabled);
						AppendSeparator();
					}

					if (topSelectedItem->isDoor()) {
						if (topSelectedItem->isOpen()) {
							Append(MAP_POPUP_MENU_SWITCH_DOOR, "&Close door", "Close this door");
						} else {
							Append(MAP_POPUP_MENU_SWITCH_DOOR, "&Open door", "Open this door");
						}
						AppendSeparator();
					}
				}

				if (topMonster || topSelectedMonster) {
					Append(MAP_POPUP_MENU_SELECT_MONSTER_BRUSH, "Select Monster", "Uses the current monster as a monster brush");
				}

				if (topSpawnMonster) {
					Append(MAP_POPUP_MENU_SELECT_SPAWN_BRUSH, "Select Monster Spawn", "Select the npc brush");
				}

				if (topNpc) {
					Append(MAP_POPUP_MENU_SELECT_NPC_BRUSH, "Select Npc", "Uses the current npc as a npc brush");
				}

				if (topSpawnNpc) {
					Append(MAP_POPUP_MENU_SELECT_SPAWN_NPC_BRUSH, "Select Npc Spawn", "Select the npc brush");
				}

				Append(MAP_POPUP_MENU_SELECT_RAW_BRUSH, "Select RAW", "Uses the top item as a RAW brush");

				if (g_settings.getBoolean(Config::SHOW_TILESET_EDITOR)) {
					Append(MAP_POPUP_MENU_MOVE_TO_TILESET, "Move To Tileset", "Move this item to any tileset");
				}

				if (hasWall) {
					Append(MAP_POPUP_MENU_SELECT_WALL_BRUSH, "Select Wallbrush", "Uses the current item as a wallbrush");
				}

				if (hasCarpet) {
					Append(MAP_POPUP_MENU_SELECT_CARPET_BRUSH, "Select Carpetbrush", "Uses the current item as a carpetbrush");
				}

				if (hasTable) {
					Append(MAP_POPUP_MENU_SELECT_TABLE_BRUSH, "Select Tablebrush", "Uses the current item as a tablebrush");
				}

				if (topSelectedItem && topSelectedItem->getDoodadBrush() && topSelectedItem->getDoodadBrush()->visibleInPalette()) {
					Append(MAP_POPUP_MENU_SELECT_DOODAD_BRUSH, "Select Doodadbrush", "Use this doodad brush");
				}

				if (topSelectedItem && topSelectedItem->isBrushDoor() && topSelectedItem->getDoorBrush()) {
					Append(MAP_POPUP_MENU_SELECT_DOOR_BRUSH, "Select Doorbrush", "Use this door brush");
				}

				if (tile->hasGround() && tile->getGroundBrush() && tile->getGroundBrush()->visibleInPalette()) {
					Append(MAP_POPUP_MENU_SELECT_GROUND_BRUSH, "Select Groundbrush", "Uses the current item as a groundbrush");
				}

				if (tile->isHouseTile()) {
					Append(MAP_POPUP_MENU_SELECT_HOUSE_BRUSH, "Select House", "Draw with the house on this tile.");
				}

				AppendSeparator();
				Append(MAP_POPUP_MENU_PROPERTIES, "&Properties", "Properties for the current object");
			} else {

				if (topMonster) {
					Append(MAP_POPUP_MENU_SELECT_MONSTER_BRUSH, "Select Monster", "Uses the current monster as a monster brush");
				}

				if (topSpawnMonster) {
					Append(MAP_POPUP_MENU_SELECT_SPAWN_BRUSH, "Select Monster", "Select the monster brush");
				}

				if (topNpc) {
					Append(MAP_POPUP_MENU_SELECT_NPC_BRUSH, "Select Npc", "Uses the current npc as a npc brush");
				}

				if (topSpawnNpc) {
					Append(MAP_POPUP_MENU_SELECT_SPAWN_NPC_BRUSH, "Select Npc", "Select the npc brush");
				}

				Append(MAP_POPUP_MENU_SELECT_RAW_BRUSH, "Select RAW", "Uses the top item as a RAW brush");
				if (hasWall) {
					Append(MAP_POPUP_MENU_SELECT_WALL_BRUSH, "Select Wallbrush", "Uses the current item as a wallbrush");
				}
				if (tile->hasGround() && tile->getGroundBrush() && tile->getGroundBrush()->visibleInPalette()) {
					Append(MAP_POPUP_MENU_SELECT_GROUND_BRUSH, "Select Groundbrush", "Uses the current tile as a groundbrush");
				}

				if (tile->isHouseTile()) {
					Append(MAP_POPUP_MENU_SELECT_HOUSE_BRUSH, "Select House", "Draw with the house on this tile.");
				}

				if (tile->hasGround() || topMonster || topSpawnMonster || topNpc || topSpawnNpc) {
					AppendSeparator();
					Append(MAP_POPUP_MENU_PROPERTIES, "&Properties", "Properties for the current object");
				}
			}

			AppendSeparator();

			wxMenuItem* browseTile = Append(MAP_POPUP_MENU_BROWSE_TILE, "Browse Field", "Navigate from tile items");
			browseTile->Enable(anything_selected);
		}
	}
}
