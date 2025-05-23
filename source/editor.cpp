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

#include "editor.h"
#include "materials.h"
#include "map.h"
#include "client_assets.h"
#include "complexitem.h"
#include "settings.h"
#include "gui.h"
#include "map_display.h"
#include "brush.h"
#include "ground_brush.h"
#include "wall_brush.h"
#include "table_brush.h"
#include "carpet_brush.h"
#include "waypoint_brush.h"
#include "house_exit_brush.h"
#include "doodad_brush.h"
#include "monster_brush.h"
#include "npc_brush.h"
#include "spawn_monster_brush.h"
#include "spawn_npc_brush.h"
#include "preferences.h"

#include "live_server.h"
#include "live_client.h"
#include "live_action.h"

#include <filesystem>
#include <chrono>
#include <iostream>

namespace fs = std::filesystem;

Editor::Editor(CopyBuffer &copybuffer) :
	live_server(nullptr),
	live_client(nullptr),
	actionQueue(newd ActionQueue(*this)),
	selection(*this),
	copybuffer(copybuffer),
	replace_brush(nullptr) {
	wxString error;
	wxArrayString warnings;
	if (!g_gui.loadMapWindow(error, warnings)) {
		g_gui.PopupDialog("Error", error, wxOK);
		g_gui.ListDialog("Warnings", warnings);
	}

	MapVersion version;
	map.convert(version);

	map.height = 2048;
	map.width = 2048;

	static int unnamed_counter = 0;

	std::string sname = "Untitled-" + i2s(++unnamed_counter);
	map.name = sname + ".otbm";
	map.spawnmonsterfile = sname + "-monster.xml";
	map.spawnnpcfile = sname + "-npc.xml";
	map.housefile = sname + "-house.xml";
	map.zonefile = sname + "-zones.xml";
	map.description = "No map description available.";
	map.unnamed = true;

	map.doChange();
}

// Used for loading a new map from "open map" menu
Editor::Editor(CopyBuffer &copybuffer, const FileName &fn) :
	live_server(nullptr),
	live_client(nullptr),
	actionQueue(newd ActionQueue(*this)),
	selection(*this),
	copybuffer(copybuffer),
	replace_brush(nullptr) {
	MapVersion ver;
	if (!IOMapOTBM::getVersionInfo(fn, ver)) {
		spdlog::error("Could not open file {}. This is not a valid OTBM file or it does not exist.", nstr(fn.GetFullPath()));
		throw std::runtime_error("Could not open file \"" + nstr(fn.GetFullPath()) + "\".\nThis is not a valid OTBM file or it does not exist.");
	}

	if (ver.otbm != g_gui.getLoadedMapVersion().otbm) {
		auto result = g_gui.PopupDialog("Map error", "The loaded map appears to be a OTBM format that is not supported by the editor. Do you still want to attempt to load the map? Caution: this will close your current map!", wxYES | wxNO);
		if (result == wxID_YES) {
			if (!g_gui.CloseAllEditors()) {
				spdlog::error("All maps of different versions were not closed.");
				throw std::runtime_error("All maps of different versions were not closed.");
			}
		} else if (result == wxID_NO) {
			throw std::runtime_error("Maps of different versions can't be loaded at same time. Save and close your current map and try again.");
		}
	}

	bool success = true;
	wxString error;
	wxArrayString warnings;

	success = g_gui.loadMapWindow(error, warnings);
	if (!success) {
		g_gui.PopupDialog("Error", error, wxOK);
		auto clientDirectory = ClientAssets::getPath().ToStdString() + "/";
		if (!wxDirExists(wxString(clientDirectory))) {
			PreferencesWindow dialog(nullptr);
			dialog.getBookCtrl().SetSelection(4);
			dialog.ShowModal();
			dialog.Destroy();
		}
	} else {
		g_gui.ListDialog("Warnings", warnings);
	}

	if (success) {
		ScopedLoadingBar LoadingBar("Loading OTBM map...");
		success = map.open(nstr(fn.GetFullPath()));
	}
}

Editor::Editor(CopyBuffer &copybuffer, LiveClient* client) :
	live_server(nullptr),
	live_client(client),
	actionQueue(newd NetworkedActionQueue(*this)),
	selection(*this),
	copybuffer(copybuffer),
	replace_brush(nullptr) { }

Editor::~Editor() {
	if (IsLive()) {
		CloseLiveServer();
	}

	UnnamedRenderingLock();
	selection.clear();
	delete actionQueue;
}

Action* Editor::createAction(ActionIdentifier type) {
	return actionQueue->createAction(type);
}

Action* Editor::createAction(BatchAction* parent) {
	return actionQueue->createAction(parent);
}

BatchAction* Editor::createBatch(ActionIdentifier type) {
	return actionQueue->createBatch(type);
}

void Editor::addBatch(BatchAction* action, int stacking_delay) {
	actionQueue->addBatch(action, stacking_delay);
}

void Editor::addAction(Action* action, int stacking_delay) {
	actionQueue->addAction(action, stacking_delay);
}

bool Editor::canUndo() const {
	return actionQueue->canUndo();
}

bool Editor::canRedo() const {
	return actionQueue->canRedo();
}

void Editor::undo(int indexes) {
	if (indexes <= 0 || !actionQueue->canUndo()) {
		return;
	}

	while (indexes > 0) {
		if (!actionQueue->undo()) {
			break;
		}
		indexes--;
	}
	g_gui.UpdateActions();
	g_gui.RefreshView();
}

void Editor::redo(int indexes) {
	if (indexes <= 0 || !actionQueue->canRedo()) {
		return;
	}

	while (indexes > 0) {
		if (!actionQueue->redo()) {
			break;
		}
		indexes--;
	}
	g_gui.UpdateActions();
	g_gui.RefreshView();
}

void Editor::updateActions() {
	actionQueue->generateLabels();
	g_gui.UpdateMenus();
	g_gui.UpdateActions();
}

void Editor::resetActionsTimer() {
	actionQueue->resetTimer();
}

void Editor::clearActions() {
	actionQueue->clear();
	g_gui.UpdateActions();
}

bool Editor::hasChanges() const {
	if (map.hasChanged()) {
		if (map.getTileCount() == 0) {
			return actionQueue->hasChanges();
		}
		return true;
	}
	return false;
}

void Editor::clearChanges() {
	map.clearChanges();
}

void Editor::saveMap(FileName filename, bool showdialog) {
	std::string savefile = filename.GetFullPath().mb_str(wxConvUTF8).data();
	bool save_as = false;
	bool save_otgz = false;

	if (savefile.empty()) {
		savefile = map.filename;

		FileName c1(wxstr(savefile));
		FileName c2(wxstr(map.filename));
		save_as = c1 != c2;
	}

	// If not named yet, propagate the file name to the auxilliary files
	if (map.unnamed) {
		FileName _name(filename);
		_name.SetExt("xml");

		_name.SetName(filename.GetName() + "-monster");
		map.spawnmonsterfile = nstr(_name.GetFullName());
		_name.SetName(filename.GetName() + "-npc");
		map.spawnnpcfile = nstr(_name.GetFullName());
		_name.SetName(filename.GetName() + "-house");
		map.housefile = nstr(_name.GetFullName());
		_name.SetName(filename.GetName() + "-zones");
		map.zonefile = nstr(_name.GetFullName());

		map.unnamed = false;
	}

	// File object to convert between local paths etc.
	FileName converter;
	converter.Assign(wxstr(savefile));
	std::string map_path = nstr(converter.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME));

	// Make temporary backups
	// converter.Assign(wxstr(savefile));
	std::string backup_otbm, backup_house, backup_spawn, backup_spawn_npc, backup_zones;

	if (converter.GetExt() == "otgz") {
		save_otgz = true;
		if (converter.FileExists()) {
			backup_otbm = map_path + nstr(converter.GetName()) + ".otgz~";
			std::remove(backup_otbm.c_str());
			std::rename(savefile.c_str(), backup_otbm.c_str());
		}
	} else {
		if (converter.FileExists()) {
			backup_otbm = map_path + nstr(converter.GetName()) + ".otbm~";
			std::remove(backup_otbm.c_str());
			std::rename(savefile.c_str(), backup_otbm.c_str());
		}

		converter.SetFullName(wxstr(map.housefile));
		if (converter.FileExists()) {
			backup_house = map_path + nstr(converter.GetName()) + ".xml~";
			std::remove(backup_house.c_str());
			std::rename((map_path + map.housefile).c_str(), backup_house.c_str());
		}

		converter.SetFullName(wxstr(map.spawnmonsterfile));
		if (converter.FileExists()) {
			backup_spawn = map_path + nstr(converter.GetName()) + ".xml~";
			std::remove(backup_spawn.c_str());
			std::rename((map_path + map.spawnmonsterfile).c_str(), backup_spawn.c_str());
		}

		converter.SetFullName(wxstr(map.spawnnpcfile));
		if (converter.FileExists()) {
			backup_spawn_npc = map_path + nstr(converter.GetName()) + ".xml~";
			std::remove(backup_spawn_npc.c_str());
			std::rename((map_path + map.spawnnpcfile).c_str(), backup_spawn_npc.c_str());
		}

		converter.SetFullName(wxstr(map.zonefile));
		if (converter.FileExists()) {
			backup_zones = map_path + nstr(converter.GetName()) + ".xml~";
			std::remove(backup_zones.c_str());
			std::rename((map_path + map.zonefile).c_str(), backup_zones.c_str());
		}
	}

	// Save the map
	{
		std::string n = nstr(g_gui.GetLocalDataDirectory()) + ".saving.txt";
		std::ofstream f(n.c_str(), std::ios::trunc | std::ios::out);
		f << backup_otbm << std::endl
		  << backup_house << std::endl
		  << backup_spawn << std::endl
		  << backup_spawn_npc << std::endl;
	}

	{

		// Set up the Map paths
		wxFileName fn = wxstr(savefile);
		map.filename = fn.GetFullPath().mb_str(wxConvUTF8);
		map.name = fn.GetFullName().mb_str(wxConvUTF8);

		if (showdialog) {
			g_gui.CreateLoadBar("Saving OTBM map...");
		}

		// Perform the actual save
		IOMapOTBM mapsaver(map.getVersion());
		bool success = mapsaver.saveMap(map, fn);

		if (showdialog) {
			g_gui.DestroyLoadBar();
		}

		// Check for errors...
		if (!success) {
			// Rename the temporary backup files back to their previous names
			if (!backup_otbm.empty()) {
				converter.SetFullName(wxstr(savefile));
				std::string otbm_filename = map_path + nstr(converter.GetName());
				std::rename(backup_otbm.c_str(), std::string(otbm_filename + (save_otgz ? ".otgz" : ".otbm")).c_str());
			}

			if (!backup_house.empty()) {
				converter.SetFullName(wxstr(map.housefile));
				std::string house_filename = map_path + nstr(converter.GetName());
				std::rename(backup_house.c_str(), std::string(house_filename + ".xml").c_str());
			}

			if (!backup_spawn.empty()) {
				converter.SetFullName(wxstr(map.spawnmonsterfile));
				std::string spawn_filename = map_path + nstr(converter.GetName());
				std::rename(backup_spawn.c_str(), std::string(spawn_filename + ".xml").c_str());
			}

			if (!backup_spawn_npc.empty()) {
				converter.SetFullName(wxstr(map.spawnnpcfile));
				std::string spawnnpc_filename = map_path + nstr(converter.GetName());
				std::rename(backup_spawn_npc.c_str(), std::string(spawnnpc_filename + ".xml").c_str());
			}

			if (!backup_zones.empty()) {
				converter.SetFullName(wxstr(map.zonefile));
				std::string zones_filename = map_path + nstr(converter.GetName());
				std::rename(backup_zones.c_str(), std::string(zones_filename + ".xml").c_str());
			}

			// Display the error
			g_gui.PopupDialog("Error", "Could not save, unable to open target for writing.", wxOK);
		}

		// Remove temporary save runfile
		{
			std::string n = nstr(g_gui.GetLocalDataDirectory()) + ".saving.txt";
			std::remove(n.c_str());
		}

		// If failure, don't run the rest of the function
		if (!success) {
			return;
		}
	}

	// Move to permanent backup
	if (!save_as && g_settings.getInteger(Config::ALWAYS_MAKE_BACKUP)) {
		std::string backup_path = map_path + "backups/";
		ensureBackupDirectoryExists(backup_path);
		// Move temporary backups to their proper files
		time_t t = time(nullptr);
		tm* current_time = localtime(&t);
		ASSERT(current_time);

		std::ostringstream date;
		date << (1900 + current_time->tm_year);
		if (current_time->tm_mon < 9) {
			date << "-"
				 << "0" << current_time->tm_mon + 1;
		} else {
			date << "-" << current_time->tm_mon + 1;
		}
		date << "-" << current_time->tm_mday;
		date << "-" << current_time->tm_hour;
		date << "-" << current_time->tm_min;
		date << "-" << current_time->tm_sec;

		if (!backup_otbm.empty()) {
			converter.SetFullName(wxstr(savefile));
			std::string otbm_filename = backup_path + nstr(converter.GetName());
			std::rename(backup_otbm.c_str(), std::string(otbm_filename + "." + date.str() + (save_otgz ? ".otgz" : ".otbm")).c_str());
		}

		if (!backup_house.empty()) {
			converter.SetFullName(wxstr(map.housefile));
			std::string house_filename = backup_path + nstr(converter.GetName());
			std::rename(backup_house.c_str(), std::string(house_filename + "." + date.str() + ".xml").c_str());
		}

		if (!backup_spawn.empty()) {
			converter.SetFullName(wxstr(map.spawnmonsterfile));
			std::string spawn_filename = backup_path + nstr(converter.GetName());
			std::rename(backup_spawn.c_str(), std::string(spawn_filename + "." + date.str() + ".xml").c_str());
		}

		if (!backup_spawn_npc.empty()) {
			converter.SetFullName(wxstr(map.spawnnpcfile));
			std::string spawnnpc_filename = backup_path + nstr(converter.GetName());
			std::rename(backup_spawn_npc.c_str(), std::string(spawnnpc_filename + "." + date.str() + ".xml").c_str());
		}

		if (!backup_zones.empty()) {
			converter.SetFullName(wxstr(map.zonefile));
			std::string zones_filename = backup_path + nstr(converter.GetName());
			std::rename(backup_zones.c_str(), std::string(zones_filename + "." + date.str() + ".xml").c_str());
		}
	} else {
		// Delete the temporary files
		std::remove(backup_otbm.c_str());
		std::remove(backup_house.c_str());
		std::remove(backup_spawn.c_str());
		std::remove(backup_spawn_npc.c_str());
		std::remove(backup_zones.c_str());
	}

	deleteOldBackups(map_path + "backups/");

	clearChanges();
}

bool Editor::importMiniMap(FileName filename, int import, int import_x_offset, int import_y_offset, int import_z_offset) {
	return false;
}

bool Editor::importMap(FileName filename, int import_x_offset, int import_y_offset, int import_z_offset, ImportType house_import_type, ImportType spawn_import_type, ImportType spawn_npc_import_type) {
	selection.clear();
	actionQueue->clear();

	Map imported_map;
	bool loaded = imported_map.open(nstr(filename.GetFullPath()));

	if (!loaded) {
		g_gui.PopupDialog("Error", "Error loading map!\n" + imported_map.getError(), wxOK | wxICON_INFORMATION);
		return false;
	}
	g_gui.ListDialog("Warning", imported_map.getWarnings());

	Position offset(import_x_offset, import_y_offset, import_z_offset);

	bool resizemap = false;
	bool resize_asked = false;
	int newsize_x = map.getWidth(), newsize_y = map.getHeight();
	int discarded_tiles = 0;

	g_gui.CreateLoadBar("Merging maps...");

	std::map<uint32_t, uint32_t> town_id_map;
	std::map<uint32_t, uint32_t> house_id_map;

	if (house_import_type != IMPORT_DONT) {
		for (TownMap::iterator tit = imported_map.towns.begin(); tit != imported_map.towns.end();) {
			Town* imported_town = tit->second;
			Town* current_town = map.towns.getTown(imported_town->getID());

			Position oldexit = imported_town->getTemplePosition();
			Position newexit = oldexit + offset;
			if (newexit.isValid()) {
				imported_town->setTemplePosition(newexit);
			}

			switch (house_import_type) {
				case IMPORT_MERGE: {
					town_id_map[imported_town->getID()] = imported_town->getID();
					if (current_town) {
						++tit;
						continue;
					}
					break;
				}
				case IMPORT_SMART_MERGE: {
					if (current_town) {
						// Compare and insert/merge depending on parameters
						if (current_town->getName() == imported_town->getName() && current_town->getID() == imported_town->getID()) {
							// Just add to map
							town_id_map[imported_town->getID()] = current_town->getID();
							++tit;
							continue;
						} else {
							// Conflict! Find a newd id and replace old
							uint32_t new_id = map.towns.getEmptyID();
							imported_town->setID(new_id);
							town_id_map[imported_town->getID()] = new_id;
						}
					} else {
						town_id_map[imported_town->getID()] = imported_town->getID();
					}
					break;
				}
				case IMPORT_INSERT: {
					// Find a newd id and replace old
					uint32_t new_id = map.towns.getEmptyID();
					imported_town->setID(new_id);
					town_id_map[imported_town->getID()] = new_id;
					break;
				}
				case IMPORT_DONT: {
					++tit;
					continue; // Should never happend..?
					break; // Continue or break ?
				}
			}

			map.towns.addTown(imported_town);

			tit = imported_map.towns.erase(tit);
		}

		for (HouseMap::iterator hit = imported_map.houses.begin(); hit != imported_map.houses.end();) {
			House* imported_house = hit->second;
			House* current_house = map.houses.getHouse(imported_house->id);
			imported_house->townid = town_id_map[imported_house->townid];

			const Position &oldexit = imported_house->getExit();
			imported_house->setExit(nullptr, Position()); // Reset it

			switch (house_import_type) {
				case IMPORT_MERGE: {
					house_id_map[imported_house->id] = imported_house->id;
					if (current_house) {
						++hit;
						Position newexit = oldexit + offset;
						if (newexit.isValid()) {
							current_house->setExit(&map, newexit);
						}
						continue;
					}
					break;
				}
				case IMPORT_SMART_MERGE: {
					if (current_house) {
						// Compare and insert/merge depending on parameters
						if (current_house->name == imported_house->name && current_house->townid == imported_house->townid) {
							// Just add to map
							house_id_map[imported_house->id] = current_house->id;
							++hit;
							Position newexit = oldexit + offset;
							if (newexit.isValid()) {
								imported_house->setExit(&map, newexit);
							}
							continue;
						} else {
							// Conflict! Find a newd id and replace old
							uint32_t new_id = map.houses.getEmptyID();
							house_id_map[imported_house->id] = new_id;
							imported_house->id = new_id;
						}
					} else {
						house_id_map[imported_house->id] = imported_house->id;
					}
					break;
				}
				case IMPORT_INSERT: {
					// Find a newd id and replace old
					uint32_t new_id = map.houses.getEmptyID();
					house_id_map[imported_house->id] = new_id;
					imported_house->id = new_id;
					break;
				}
				case IMPORT_DONT: {
					++hit;
					Position newexit = oldexit + offset;
					if (newexit.isValid()) {
						imported_house->setExit(&map, newexit);
					}
					continue; // Should never happend..?
					break;
				}
			}

			Position newexit = oldexit + offset;
			if (newexit.isValid()) {
				imported_house->setExit(&map, newexit);
			}
			map.houses.addHouse(imported_house);

			hit = imported_map.houses.erase(hit);
		}
	}

	// Monster spawn import
	std::map<Position, SpawnMonster*> spawn_monster_map;
	if (spawn_import_type != IMPORT_DONT) {
		for (SpawnNpcPositionList::iterator siter = imported_map.spawnsMonster.begin(); siter != imported_map.spawnsMonster.end();) {
			Position oldSpawnMonsterPos = *siter;
			Position newSpawnMonsterPos = *siter + offset;
			switch (spawn_import_type) {
				case IMPORT_SMART_MERGE:
				case IMPORT_INSERT:
				case IMPORT_MERGE: {
					Tile* imported_tile = imported_map.getTile(oldSpawnMonsterPos);
					if (imported_tile) {
						ASSERT(imported_tile->spawnMonster);
						spawn_monster_map[newSpawnMonsterPos] = imported_tile->spawnMonster;

						SpawnNpcPositionList::iterator next = siter;
						bool cont = true;
						Position next_spawn_monster;

						++next;
						if (next == imported_map.spawnsMonster.end()) {
							cont = false;
						} else {
							next_spawn_monster = *next;
						}
						imported_map.spawnsMonster.erase(siter);
						if (cont) {
							siter = imported_map.spawnsMonster.find(next_spawn_monster);
						} else {
							siter = imported_map.spawnsMonster.end();
						}
					}
					break;
				}
				case IMPORT_DONT: {
					++siter;
					break;
				}
			}
		}
	}

	// Npc spawn import
	std::map<Position, SpawnNpc*> spawn_npc_map;
	if (spawn_npc_import_type != IMPORT_DONT) {
		for (SpawnNpcPositionList::iterator siter = imported_map.spawnsNpc.begin(); siter != imported_map.spawnsNpc.end();) {
			Position oldSpawnNpcPos = *siter;
			Position newSpawnNpcPos = *siter + offset;
			switch (spawn_npc_import_type) {
				case IMPORT_SMART_MERGE:
				case IMPORT_INSERT:
				case IMPORT_MERGE: {
					Tile* importedTile = imported_map.getTile(oldSpawnNpcPos);
					if (importedTile) {
						ASSERT(importedTile->spawnNpc);
						spawn_npc_map[newSpawnNpcPos] = importedTile->spawnNpc;

						SpawnNpcPositionList::iterator next = siter;
						bool cont = true;
						Position next_spawn_npc;

						++next;
						if (next == imported_map.spawnsNpc.end()) {
							cont = false;
						} else {
							next_spawn_npc = *next;
						}
						imported_map.spawnsNpc.erase(siter);
						if (cont) {
							siter = imported_map.spawnsNpc.find(next_spawn_npc);
						} else {
							siter = imported_map.spawnsNpc.end();
						}
					}
					break;
				}
				case IMPORT_DONT: {
					++siter;
					break;
				}
			}
		}
	}

	// Plain merge of waypoints, very simple! :)
	for (WaypointMap::iterator iter = imported_map.waypoints.begin(); iter != imported_map.waypoints.end(); ++iter) {
		iter->second->pos += offset;
	}

	map.waypoints.waypoints.insert(imported_map.waypoints.begin(), imported_map.waypoints.end());
	imported_map.waypoints.waypoints.clear();

	uint64_t tiles_merged = 0;
	uint64_t tiles_to_import = imported_map.tilecount;
	for (MapIterator mit = imported_map.begin(); mit != imported_map.end(); ++mit) {
		if (tiles_merged % 8092 == 0) {
			g_gui.SetLoadDone(int(100.0 * tiles_merged / tiles_to_import));
		}
		++tiles_merged;

		Tile* import_tile = (*mit)->get();
		Position new_pos = import_tile->getPosition() + offset;
		if (!new_pos.isValid()) {
			++discarded_tiles;
			continue;
		}

		if (!resizemap && (new_pos.x > map.getWidth() || new_pos.y > map.getHeight())) {
			if (resize_asked) {
				++discarded_tiles;
				continue;
			} else {
				resize_asked = true;
				int ret = g_gui.PopupDialog("Collision", "The imported tiles are outside the current map scope. Do you want to resize the map? (Else additional tiles will be removed)", wxYES | wxNO);

				if (ret == wxID_YES) {
					// ...
					resizemap = true;
				} else {
					++discarded_tiles;
					continue;
				}
			}
		}

		if (new_pos.x > newsize_x) {
			newsize_x = new_pos.x;
		}
		if (new_pos.y > newsize_y) {
			newsize_y = new_pos.y;
		}

		imported_map.setTile(import_tile->getPosition(), nullptr);
		TileLocation* location = map.createTileL(new_pos);

		// Check if we should update any houses
		int new_houseid = house_id_map[import_tile->getHouseID()];
		House* house = map.houses.getHouse(new_houseid);
		if (import_tile->isHouseTile() && house_import_type != IMPORT_DONT && house) {
			// We need to notify houses of the tile moving
			house->removeTile(import_tile);
			import_tile->setLocation(location);
			house->addTile(import_tile);
		} else {
			import_tile->setLocation(location);
		}

		if (offset != Position(0, 0, 0)) {
			for (ItemVector::iterator iter = import_tile->items.begin(); iter != import_tile->items.end(); ++iter) {
				Item* item = *iter;
				if (Teleport* teleport = dynamic_cast<Teleport*>(item)) {
					teleport->setDestination(teleport->getDestination() + offset);
				}
			}
		}

		Tile* old_tile = map.getTile(new_pos);
		if (old_tile) {
			map.removeSpawnMonster(old_tile);
		}
		import_tile->spawnMonster = nullptr;

		map.setTile(new_pos, import_tile, true);
	}

	for (std::map<Position, SpawnMonster*>::iterator spawn_monster_iter = spawn_monster_map.begin(); spawn_monster_iter != spawn_monster_map.end(); ++spawn_monster_iter) {
		Position pos = spawn_monster_iter->first;
		TileLocation* location = map.createTileL(pos);
		Tile* tile = location->get();
		if (!tile) {
			tile = map.allocator(location);
			map.setTile(pos, tile);
		} else if (tile->spawnMonster) {
			map.removeSpawnMonsterInternal(tile);
			delete tile->spawnMonster;
		}
		tile->spawnMonster = spawn_monster_iter->second;

		map.addSpawnMonster(tile);
	}

	for (std::map<Position, SpawnNpc*>::iterator spawn_npc_iter = spawn_npc_map.begin(); spawn_npc_iter != spawn_npc_map.end(); ++spawn_npc_iter) {
		Position pos = spawn_npc_iter->first;
		TileLocation* location = map.createTileL(pos);
		Tile* tile = location->get();
		if (!tile) {
			tile = map.allocator(location);
			map.setTile(pos, tile);
		} else if (tile->spawnNpc) {
			map.removeSpawnNpcInternal(tile);
			delete tile->spawnNpc;
		}
		tile->spawnNpc = spawn_npc_iter->second;

		map.addSpawnNpc(tile);
	}

	g_gui.DestroyLoadBar();

	map.setWidth(newsize_x);
	map.setHeight(newsize_y);
	g_gui.PopupDialog("Success", "Map imported successfully, " + i2ws(discarded_tiles) + " tiles were discarded as invalid.", wxOK);

	g_gui.RefreshPalettes();
	g_gui.FitViewToMap();

	return true;
}

void Editor::borderizeSelection() {
	if (selection.empty()) {
		g_gui.SetStatusText("No items selected. Can't borderize.");
		return;
	}

	Action* action = actionQueue->createAction(ACTION_BORDERIZE);
	for (const Tile* tile : selection) {
		Tile* new_tile = tile->deepCopy(map);
		new_tile->borderize(&map);
		new_tile->select();
		action->addChange(new Change(new_tile));
	}
	addAction(action);
	updateActions();
}

void Editor::borderizeMap(bool showdialog) {
	if (showdialog) {
		g_gui.CreateLoadBar("Borderizing map...");
	}

	uint64_t tiles_done = 0;
	for (TileLocation* tileLocation : map) {
		if (showdialog && tiles_done % 4096 == 0) {
			g_gui.SetLoadDone(static_cast<int32_t>(tiles_done / double(map.tilecount) * 100.0));
		}

		Tile* tile = tileLocation->get();
		ASSERT(tile);

		tile->borderize(&map);
		++tiles_done;
	}

	if (showdialog) {
		g_gui.DestroyLoadBar();
	}
}

void Editor::randomizeSelection() {
	if (selection.empty()) {
		g_gui.SetStatusText("No items selected. Can't randomize.");
		return;
	}

	Action* action = actionQueue->createAction(ACTION_RANDOMIZE);
	for (const Tile* tile : selection) {
		Tile* new_tile = tile->deepCopy(map);
		GroundBrush* brush = new_tile->getGroundBrush();
		if (brush && brush->isReRandomizable()) {
			brush->draw(&map, new_tile, nullptr);

			Item* old_ground = tile->ground;
			Item* new_ground = new_tile->ground;
			if (old_ground && new_ground) {
				new_ground->setActionID(old_ground->getActionID());
				new_ground->setUniqueID(old_ground->getUniqueID());
			}

			new_tile->select();
			action->addChange(new Change(new_tile));
		}
	}
	addAction(action);
	updateActions();
}

void Editor::randomizeMap(bool showdialog) {
	if (showdialog) {
		g_gui.CreateLoadBar("Randomizing map...");
	}

	uint64_t tiles_done = 0;
	for (TileLocation* tileLocation : map) {
		if (showdialog && tiles_done % 4096 == 0) {
			g_gui.SetLoadDone(static_cast<int32_t>(tiles_done / double(map.tilecount) * 100.0));
		}

		Tile* tile = tileLocation->get();
		ASSERT(tile);

		GroundBrush* groundBrush = tile->getGroundBrush();
		if (groundBrush) {
			Item* oldGround = tile->ground;

			uint16_t actionId, uniqueId;
			if (oldGround) {
				actionId = oldGround->getActionID();
				uniqueId = oldGround->getUniqueID();
			} else {
				actionId = 0;
				uniqueId = 0;
			}
			groundBrush->draw(&map, tile, nullptr);

			Item* newGround = tile->ground;
			if (newGround) {
				newGround->setActionID(actionId);
				newGround->setUniqueID(uniqueId);
			}
			tile->update();
		}
		++tiles_done;
	}

	if (showdialog) {
		g_gui.DestroyLoadBar();
	}
}

void Editor::clearInvalidHouseTiles(bool showdialog) {
	if (showdialog) {
		g_gui.CreateLoadBar("Clearing invalid house tiles...");
	}

	Houses &houses = map.houses;

	HouseMap::iterator iter = houses.begin();
	while (iter != houses.end()) {
		House* h = iter->second;
		if (map.towns.getTown(h->townid) == nullptr) {
			iter = houses.erase(iter);
		} else {
			++iter;
		}
	}

	uint64_t tiles_done = 0;
	for (MapIterator map_iter = map.begin(); map_iter != map.end(); ++map_iter) {
		if (showdialog && tiles_done % 4096 == 0) {
			g_gui.SetLoadDone(int(tiles_done / double(map.tilecount) * 100.0));
		}

		Tile* tile = (*map_iter)->get();
		ASSERT(tile);
		if (tile->isHouseTile()) {
			if (houses.getHouse(tile->getHouseID()) == nullptr) {
				tile->setHouse(nullptr);
			}
		}
		++tiles_done;
	}

	if (showdialog) {
		g_gui.DestroyLoadBar();
	}
}

void Editor::clearModifiedTileState(bool showdialog) {
	if (showdialog) {
		g_gui.CreateLoadBar("Clearing modified state from all tiles...");
	}

	uint64_t tiles_done = 0;
	for (MapIterator map_iter = map.begin(); map_iter != map.end(); ++map_iter) {
		if (showdialog && tiles_done % 4096 == 0) {
			g_gui.SetLoadDone(int(tiles_done / double(map.tilecount) * 100.0));
		}

		Tile* tile = (*map_iter)->get();
		ASSERT(tile);
		tile->unmodify();
		++tiles_done;
	}

	if (showdialog) {
		g_gui.DestroyLoadBar();
	}
}

void Editor::moveSelection(const Position &offset) {
	if (!CanEdit() || !hasSelection()) {
		return;
	}

	bool borderize = false;
	int drag_threshold = g_settings.getInteger(Config::BORDERIZE_DRAG_THRESHOLD);
	bool create_borders = g_settings.getInteger(Config::USE_AUTOMAGIC)
		&& g_settings.getInteger(Config::BORDERIZE_DRAG);

	TileSet storage;
	BatchAction* batch_action = actionQueue->createBatch(ACTION_MOVE);
	Action* action = actionQueue->createAction(batch_action);

	// Update the tiles with the new positions
	for (Tile* tile : selection) {
		Tile* new_tile = tile->deepCopy(map);
		Tile* storage_tile = map.allocator(tile->getLocation());

		ItemVector selected_items = new_tile->popSelectedItems();
		for (Item* item : selected_items) {
			storage_tile->addItem(item);
		}

		// Move monster spawns
		if (new_tile->spawnMonster && new_tile->spawnMonster->isSelected()) {
			storage_tile->spawnMonster = new_tile->spawnMonster;
			new_tile->spawnMonster = nullptr;
		}
		// Move monster
		const auto monstersSelection = new_tile->popSelectedMonsters();
		std::ranges::for_each(monstersSelection, [&](const auto monster) {
			storage_tile->addMonster(monster);
		});
		// Move npc
		if (new_tile->npc && new_tile->npc->isSelected()) {
			storage_tile->npc = new_tile->npc;
			new_tile->npc = nullptr;
		}
		// Move npc spawns
		if (new_tile->spawnNpc && new_tile->spawnNpc->isSelected()) {
			storage_tile->spawnNpc = new_tile->spawnNpc;
			new_tile->spawnNpc = nullptr;
		}

		if (storage_tile->ground) {
			storage_tile->house_id = new_tile->house_id;
			new_tile->house_id = 0;
			storage_tile->setMapFlags(new_tile->getMapFlags());
			new_tile->setMapFlags(TILESTATE_NONE);
			borderize = true;
		}

		storage.insert(storage_tile);
		action->addChange(new Change(new_tile));
	}
	batch_action->addAndCommitAction(action);

	// Remove old borders (and create some new?)
	if (create_borders && selection.size() < static_cast<size_t>(drag_threshold)) {
		action = actionQueue->createAction(batch_action);
		TileList borderize_tiles;
		// Go through all modified (selected) tiles (might be slow)
		for (const Tile* tile : storage) {
			const Position &pos = tile->getPosition();
			// Go through all neighbours
			Tile* t;
			t = map.getTile(pos.x, pos.y, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = map.getTile(pos.x - 1, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = map.getTile(pos.x, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = map.getTile(pos.x + 1, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = map.getTile(pos.x - 1, pos.y, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = map.getTile(pos.x + 1, pos.y, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = map.getTile(pos.x - 1, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = map.getTile(pos.x, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = map.getTile(pos.x + 1, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
		}

		// Remove duplicates
		borderize_tiles.sort();
		borderize_tiles.unique();

		// Create borders
		for (const Tile* tile : borderize_tiles) {
			Tile* new_tile = tile->deepCopy(map);
			if (borderize) {
				new_tile->borderize(&map);
			}
			new_tile->wallize(&map);
			new_tile->tableize(&map);
			new_tile->carpetize(&map);
			if (tile->ground && tile->ground->isSelected()) {
				new_tile->selectGround();
			}
			action->addChange(new Change(new_tile));
		}
		batch_action->addAndCommitAction(action);
	}

	// New action for adding the destination tiles
	action = actionQueue->createAction(batch_action);
	for (Tile* tile : storage) {
		const Position &old_pos = tile->getPosition();
		Position new_pos = old_pos - offset;
		if (new_pos.z < rme::MapMinLayer && new_pos.z > rme::MapMaxLayer) {
			delete tile;
			continue;
		}

		TileLocation* location = map.createTileL(new_pos);
		Tile* old_dest_tile = location->get();
		Tile* new_dest_tile = nullptr;

		if (!tile->ground || g_settings.getInteger(Config::MERGE_MOVE)) {
			// Move items
			if (old_dest_tile) {
				new_dest_tile = old_dest_tile->deepCopy(map);
			} else {
				new_dest_tile = map.allocator(location);
			}
			new_dest_tile->merge(tile);
			delete tile;
		} else {
			// Replace tile instead of just merge
			tile->setLocation(location);
			new_dest_tile = tile;
		}
		action->addChange(new Change(new_dest_tile));
	}
	batch_action->addAndCommitAction(action);

	if (create_borders && selection.size() < static_cast<size_t>(drag_threshold)) {
		action = actionQueue->createAction(batch_action);
		TileList borderize_tiles;
		// Go through all modified (selected) tiles (might be slow)
		for (Tile* tile : selection) {
			bool add_me = false; // If this tile is touched
			const Position &pos = tile->getPosition();
			// Go through all neighbours
			Tile* t;
			t = map.getTile(pos.x - 1, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = map.getTile(pos.x - 1, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = map.getTile(pos.x, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = map.getTile(pos.x + 1, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = map.getTile(pos.x - 1, pos.y, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = map.getTile(pos.x + 1, pos.y, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = map.getTile(pos.x - 1, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = map.getTile(pos.x, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = map.getTile(pos.x + 1, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			if (add_me) {
				borderize_tiles.push_back(tile);
			}
		}

		// Remove duplicates
		borderize_tiles.sort();
		borderize_tiles.unique();

		// Create borders
		for (const Tile* tile : borderize_tiles) {
			if (!tile || !tile->ground) {
				continue;
			}
			if (tile->ground->getGroundBrush()) {
				Tile* new_tile = tile->deepCopy(map);
				if (borderize) {
					new_tile->borderize(&map);
				}
				new_tile->wallize(&map);
				new_tile->tableize(&map);
				new_tile->carpetize(&map);
				if (tile->ground->isSelected()) {
					new_tile->selectGround();
				}
				action->addChange(new Change(new_tile));
			}
		}
		batch_action->addAndCommitAction(action);
	}

	// Store the action for undo
	addBatch(batch_action);
	updateActions();
	selection.updateSelectionCount();
}

void Editor::destroySelection() {
	if (selection.size() == 0) {
		g_gui.SetStatusText("No selected items to delete.");
	} else {
		int tile_count = 0;
		int item_count = 0;
		int monsterCount = 0;
		PositionList tilestoborder;

		BatchAction* batch = actionQueue->createBatch(ACTION_DELETE_TILES);
		Action* action = actionQueue->createAction(batch);

		for (TileSet::iterator it = selection.begin(); it != selection.end(); ++it) {
			tile_count++;

			Tile* tile = *it;
			Tile* newtile = tile->deepCopy(map);

			ItemVector tile_selection = newtile->popSelectedItems();
			for (ItemVector::iterator iit = tile_selection.begin(); iit != tile_selection.end(); ++iit) {
				++item_count;
				// Delete the items from the tile
				delete *iit;
			}

			auto monstersSelection = newtile->popSelectedMonsters();
			std::ranges::for_each(monstersSelection, [&](auto monster) {
				++monsterCount;
				delete monster;
			});
			// Clear the vector to avoid being used anywhere else in this block with nullptrs
			monstersSelection.clear();

			/*
			for (auto monsterIt = monstersSelection.begin(); monsterIt != monstersSelection.end(); ++monsterIt) {
				++monsterCount;
				// Delete the monsters from the tile
				delete *monsterIt;
			}
			*/

			if (newtile->spawnMonster && newtile->spawnMonster->isSelected()) {
				delete newtile->spawnMonster;
				newtile->spawnMonster = nullptr;
			}
			// Npc
			if (newtile->npc && newtile->npc->isSelected()) {
				delete newtile->npc;
				newtile->npc = nullptr;
			}

			if (newtile->spawnNpc && newtile->spawnNpc->isSelected()) {
				delete newtile->spawnNpc;
				newtile->spawnNpc = nullptr;
			}

			if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
				for (int y = -1; y <= 1; y++) {
					for (int x = -1; x <= 1; x++) {
						const Position &position = tile->getPosition();
						tilestoborder.push_back(Position(position.x + x, position.y + y, position.z));
					}
				}
			}
			action->addChange(newd Change(newtile));
		}

		batch->addAndCommitAction(action);

		if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
			// Remove duplicates
			tilestoborder.sort();
			tilestoborder.unique();

			action = actionQueue->createAction(batch);
			for (PositionList::iterator it = tilestoborder.begin(); it != tilestoborder.end(); ++it) {
				TileLocation* location = map.createTileL(*it);
				Tile* tile = location->get();

				if (tile) {
					Tile* new_tile = tile->deepCopy(map);
					new_tile->borderize(&map);
					new_tile->wallize(&map);
					new_tile->tableize(&map);
					new_tile->carpetize(&map);
					action->addChange(newd Change(new_tile));
				} else {
					Tile* new_tile = map.allocator(location);
					new_tile->borderize(&map);
					if (new_tile->size()) {
						action->addChange(newd Change(new_tile));
					} else {
						delete new_tile;
					}
				}
			}

			batch->addAndCommitAction(action);
		}

		addBatch(batch);
		updateActions();

		wxString ss;
		ss << "Deleted " << tile_count << " tile" << (tile_count > 1 ? "s" : "") << " (" << item_count << " item" << (item_count > 1 ? "s" : "") << ")";
		g_gui.SetStatusText(ss);
	}
}

// Macro to avoid useless code repetition
void doSurroundingBorders(DoodadBrush* doodad_brush, PositionList &tilestoborder, Tile* buffer_tile, Tile* new_tile) {
	if (doodad_brush->doNewBorders() && g_settings.getInteger(Config::USE_AUTOMAGIC)) {
		const Position &position = new_tile->getPosition();
		tilestoborder.push_back(Position(position));
		if (buffer_tile->hasGround()) {
			for (int y = -1; y <= 1; y++) {
				for (int x = -1; x <= 1; x++) {
					tilestoborder.push_back(Position(position.x + x, position.y + y, position.z));
				}
			}
		} else if (buffer_tile->hasWall()) {
			tilestoborder.push_back(Position(position.x, position.y - 1, position.z));
			tilestoborder.push_back(Position(position.x - 1, position.y, position.z));
			tilestoborder.push_back(Position(position.x + 1, position.y, position.z));
			tilestoborder.push_back(Position(position.x, position.y + 1, position.z));
		}
	}
}

void removeDuplicateWalls(Tile* buffer, Tile* tile) {
	if (!buffer || buffer->items.empty() || !tile || tile->items.empty()) {
		return;
	}

	for (const Item* item : buffer->items) {
		if (item) {
			WallBrush* brush = item->getWallBrush();
			if (brush) {
				tile->cleanWalls(brush);
			}
		}
	}
}

void Editor::drawInternal(Position offset, bool alt, bool dodraw) {
	if (!CanEdit()) {
		return;
	}

	Brush* brush = g_gui.GetCurrentBrush();
	if (!brush) {
		return;
	}

	if (brush->isDoodad()) {
		BatchAction* batch = actionQueue->createBatch(ACTION_DRAW);
		Action* action = actionQueue->createAction(batch);
		BaseMap* buffer_map = g_gui.doodad_buffer_map;

		Position delta_pos = offset - Position(0x8000, 0x8000, 0x8);
		PositionList tilestoborder;

		for (MapIterator it = buffer_map->begin(); it != buffer_map->end(); ++it) {
			Tile* buffer_tile = (*it)->get();
			Position pos = buffer_tile->getPosition() + delta_pos;
			if (!pos.isValid()) {
				continue;
			}

			TileLocation* location = map.createTileL(pos);
			Tile* tile = location->get();
			DoodadBrush* doodad_brush = brush->asDoodad();

			if (doodad_brush->placeOnBlocking() || alt) {
				if (tile) {
					bool place = true;
					if (!doodad_brush->placeOnDuplicate() && !alt) {
						for (ItemVector::const_iterator iter = tile->items.begin(); iter != tile->items.end(); ++iter) {
							if (doodad_brush->ownsItem(*iter)) {
								place = false;
								break;
							}
						}
					}
					if (place) {
						Tile* new_tile = tile->deepCopy(map);
						removeDuplicateWalls(buffer_tile, new_tile);
						doSurroundingBorders(doodad_brush, tilestoborder, buffer_tile, new_tile);
						new_tile->merge(buffer_tile);
						action->addChange(newd Change(new_tile));
					}
				} else {
					Tile* new_tile = map.allocator(location);
					removeDuplicateWalls(buffer_tile, new_tile);
					doSurroundingBorders(doodad_brush, tilestoborder, buffer_tile, new_tile);
					new_tile->merge(buffer_tile);
					action->addChange(newd Change(new_tile));
				}
			} else {
				if (tile && !tile->isBlocking()) {
					bool place = true;
					if (!doodad_brush->placeOnDuplicate() && !alt) {
						for (ItemVector::const_iterator iter = tile->items.begin(); iter != tile->items.end(); ++iter) {
							if (doodad_brush->ownsItem(*iter)) {
								place = false;
								break;
							}
						}
					}
					if (place) {
						Tile* new_tile = tile->deepCopy(map);
						removeDuplicateWalls(buffer_tile, new_tile);
						doSurroundingBorders(doodad_brush, tilestoborder, buffer_tile, new_tile);
						new_tile->merge(buffer_tile);
						action->addChange(newd Change(new_tile));
					}
				}
			}
		}
		batch->addAndCommitAction(action);

		if (tilestoborder.size() > 0) {
			Action* action = actionQueue->createAction(batch);

			// Remove duplicates
			tilestoborder.sort();
			tilestoborder.unique();

			for (PositionList::const_iterator it = tilestoborder.begin(); it != tilestoborder.end(); ++it) {
				Tile* tile = map.getTile(*it);
				if (tile) {
					Tile* new_tile = tile->deepCopy(map);
					new_tile->borderize(&map);
					new_tile->wallize(&map);
					action->addChange(newd Change(new_tile));
				}
			}
			batch->addAndCommitAction(action);
		}
		addBatch(batch, 2);
	} else if (brush->isHouseExit()) {
		HouseExitBrush* house_exit_brush = brush->asHouseExit();
		if (!house_exit_brush->canDraw(&map, offset)) {
			return;
		}

		House* house = map.houses.getHouse(house_exit_brush->getHouseID());
		if (!house) {
			return;
		}

		BatchAction* batch = actionQueue->createBatch(ACTION_DRAW);
		Action* action = actionQueue->createAction(batch);
		action->addChange(Change::Create(house, offset));
		batch->addAndCommitAction(action);
		addBatch(batch, 2);
	} else if (brush->isWaypoint()) {
		WaypointBrush* waypoint_brush = brush->asWaypoint();
		if (!waypoint_brush->canDraw(&map, offset)) {
			return;
		}

		Waypoint* waypoint = map.waypoints.getWaypoint(waypoint_brush->getWaypoint());
		if (!waypoint || waypoint->pos == offset) {
			return;
		}

		BatchAction* batch = actionQueue->createBatch(ACTION_DRAW);
		Action* action = actionQueue->createAction(batch);
		action->addChange(Change::Create(waypoint, offset));
		batch->addAndCommitAction(action);
		addBatch(batch, 2);
	} else if (brush->isWall()) {
		BatchAction* batch = actionQueue->createBatch(dodraw ? ACTION_DRAW : ACTION_ERASE);
		Action* action = actionQueue->createAction(batch);
		// This will only occur with a size 0, when clicking on a tile (not drawing)
		Tile* tile = map.getTile(offset);
		Tile* new_tile = nullptr;
		if (tile) {
			new_tile = tile->deepCopy(map);
		} else {
			new_tile = map.allocator(map.createTileL(offset));
		}

		if (dodraw) {
			bool b = true;
			brush->asWall()->draw(&map, new_tile, &b);
		} else {
			brush->asWall()->undraw(&map, new_tile);
		}
		action->addChange(newd Change(new_tile));
		batch->addAndCommitAction(action);
		addBatch(batch, 2);
	} else if (brush->isSpawnMonster() || brush->isMonster()) {
		BatchAction* batch = actionQueue->createBatch(dodraw ? ACTION_DRAW : ACTION_ERASE);
		Action* action = actionQueue->createAction(batch);

		Tile* tile = map.getTile(offset);
		Tile* new_tile = nullptr;
		if (tile) {
			new_tile = tile->deepCopy(map);
		} else {
			new_tile = map.allocator(map.createTileL(offset));
		}
		int param;
		if (brush->isMonster()) {
			param = g_gui.GetSpawnMonsterTime();
		} else {
			param = g_gui.GetBrushSize();
		}
		if (dodraw) {
			brush->draw(&map, new_tile, &param);
		} else {
			brush->undraw(&map, new_tile);
		}
		action->addChange(newd Change(new_tile));
		batch->addAndCommitAction(action);
		addBatch(batch, 2);
	} else if (brush->isSpawnNpc() || brush->isNpc()) {
		BatchAction* batch = actionQueue->createBatch(ACTION_DRAW);
		Action* action = actionQueue->createAction(batch);

		Tile* tile = map.getTile(offset);
		Tile* new_tile = nullptr;
		if (tile) {
			new_tile = tile->deepCopy(map);
		} else {
			new_tile = map.allocator(map.createTileL(offset));
		}
		int param;
		if (brush->isNpc()) {
			param = g_gui.GetSpawnNpcTime();
		} else {
			param = g_gui.GetBrushSize();
		}
		if (dodraw) {
			brush->draw(&map, new_tile, &param);
		} else {
			brush->undraw(&map, new_tile);
		}
		action->addChange(newd Change(new_tile));
		batch->addAndCommitAction(action);
		addBatch(batch, 2);
	}
}

void Editor::drawInternal(const PositionVector &tilestodraw, bool alt, bool dodraw) {
	if (!CanEdit()) {
		return;
	}

	Brush* brush = g_gui.GetCurrentBrush();
	if (!brush) {
		return;
	}

#ifdef __DEBUG__
	if (brush->isGround() || brush->isWall()) {
		// Wrong function, end call
		return;
	}
#endif

	Action* action = actionQueue->createAction(dodraw ? ACTION_DRAW : ACTION_ERASE);

	if (brush->isOptionalBorder()) {
		// We actually need to do borders, but on the same tiles we draw to
		for (PositionVector::const_iterator it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
			TileLocation* location = map.createTileL(*it);
			Tile* tile = location->get();
			if (tile) {
				if (dodraw) {
					Tile* new_tile = tile->deepCopy(map);
					brush->draw(&map, new_tile);
					new_tile->borderize(&map);
					action->addChange(newd Change(new_tile));
				} else if (!dodraw && tile->hasOptionalBorder()) {
					Tile* new_tile = tile->deepCopy(map);
					brush->undraw(&map, new_tile);
					new_tile->borderize(&map);
					action->addChange(newd Change(new_tile));
				}
			} else if (dodraw) {
				Tile* new_tile = map.allocator(location);
				brush->draw(&map, new_tile);
				new_tile->borderize(&map);
				if (new_tile->size() == 0) {
					delete new_tile;
					continue;
				}
				action->addChange(newd Change(new_tile));
			}
		}
	} else {

		for (PositionVector::const_iterator it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
			TileLocation* location = map.createTileL(*it);
			Tile* tile = location->get();
			if (tile) {
				Tile* new_tile = tile->deepCopy(map);
				if (dodraw) {
					brush->draw(&map, new_tile, &alt);
				} else {
					brush->undraw(&map, new_tile);
				}
				action->addChange(newd Change(new_tile));
			} else if (dodraw) {
				Tile* new_tile = map.allocator(location);
				brush->draw(&map, new_tile, &alt);
				action->addChange(newd Change(new_tile));
			}
		}
	}
	addAction(action, 2);
}

void Editor::drawInternal(const PositionVector &tilestodraw, PositionVector &tilestoborder, bool alt, bool dodraw) {
	if (!CanEdit()) {
		return;
	}

	Brush* brush = g_gui.GetCurrentBrush();
	if (!brush) {
		return;
	}

	if (brush->isGround() || brush->isEraser()) {
		ActionIdentifier identifier = (dodraw && !brush->isEraser()) ? ACTION_DRAW : ACTION_ERASE;
		BatchAction* batch = actionQueue->createBatch(identifier);
		Action* action = actionQueue->createAction(batch);

		for (PositionVector::const_iterator it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
			TileLocation* location = map.createTileL(*it);
			Tile* tile = location->get();
			if (tile) {
				Tile* new_tile = tile->deepCopy(map);
				if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
					new_tile->cleanBorders();
				}
				if (dodraw) {
					if (brush->isGround() && alt) {
						std::pair<bool, GroundBrush*> param;
						if (replace_brush) {
							param.first = false;
							param.second = replace_brush;
						} else {
							param.first = true;
							param.second = nullptr;
						}
						g_gui.GetCurrentBrush()->draw(&map, new_tile, &param);
					} else {
						g_gui.GetCurrentBrush()->draw(&map, new_tile, nullptr);
					}
				} else {
					g_gui.GetCurrentBrush()->undraw(&map, new_tile);
					tilestoborder.push_back(*it);
				}
				action->addChange(newd Change(new_tile));
			} else if (dodraw) {
				Tile* new_tile = map.allocator(location);
				if (brush->isGround() && alt) {
					std::pair<bool, GroundBrush*> param;
					if (replace_brush) {
						param.first = false;
						param.second = replace_brush;
					} else {
						param.first = true;
						param.second = nullptr;
					}
					g_gui.GetCurrentBrush()->draw(&map, new_tile, &param);
				} else {
					g_gui.GetCurrentBrush()->draw(&map, new_tile, nullptr);
				}
				action->addChange(newd Change(new_tile));
			}
		}

		// Commit changes to map
		batch->addAndCommitAction(action);

		if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
			// Do borders!
			action = actionQueue->createAction(batch);
			for (PositionVector::const_iterator it = tilestoborder.begin(); it != tilestoborder.end(); ++it) {
				TileLocation* location = map.createTileL(*it);
				Tile* tile = location->get();
				if (tile) {
					Tile* new_tile = tile->deepCopy(map);
					if (brush->isEraser()) {
						new_tile->wallize(&map);
						new_tile->tableize(&map);
						new_tile->carpetize(&map);
					}
					new_tile->borderize(&map);
					action->addChange(newd Change(new_tile));
				} else {
					Tile* new_tile = map.allocator(location);
					if (brush->isEraser()) {
						// There are no carpets/tables/walls on empty tiles...
						// new_tile->wallize(map);
						// new_tile->tableize(map);
						// new_tile->carpetize(map);
					}
					new_tile->borderize(&map);
					if (new_tile->size() > 0) {
						action->addChange(newd Change(new_tile));
					} else {
						delete new_tile;
					}
				}
			}
			batch->addAndCommitAction(action);
		}

		addBatch(batch, 2);
	} else if (brush->isTable() || brush->isCarpet()) {
		BatchAction* batch = actionQueue->createBatch(ACTION_DRAW);
		Action* action = actionQueue->createAction(batch);

		for (PositionVector::const_iterator it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
			TileLocation* location = map.createTileL(*it);
			Tile* tile = location->get();
			if (tile) {
				Tile* new_tile = tile->deepCopy(map);
				if (dodraw) {
					g_gui.GetCurrentBrush()->draw(&map, new_tile, nullptr);
				} else {
					g_gui.GetCurrentBrush()->undraw(&map, new_tile);
				}
				action->addChange(newd Change(new_tile));
			} else if (dodraw) {
				Tile* new_tile = map.allocator(location);
				g_gui.GetCurrentBrush()->draw(&map, new_tile, nullptr);
				action->addChange(newd Change(new_tile));
			}
		}

		// Commit changes to map
		batch->addAndCommitAction(action);

		// Do borders!
		action = actionQueue->createAction(batch);
		for (PositionVector::const_iterator it = tilestoborder.begin(); it != tilestoborder.end(); ++it) {
			Tile* tile = map.getTile(*it);
			if (brush->isTable()) {
				if (tile && tile->hasTable()) {
					Tile* new_tile = tile->deepCopy(map);
					new_tile->tableize(&map);
					action->addChange(newd Change(new_tile));
				}
			} else if (brush->isCarpet()) {
				if (tile && tile->hasCarpet()) {
					Tile* new_tile = tile->deepCopy(map);
					new_tile->carpetize(&map);
					action->addChange(newd Change(new_tile));
				}
			}
		}
		batch->addAndCommitAction(action);

		addBatch(batch, 2);
	} else if (brush->isWall()) {
		BatchAction* batch = actionQueue->createBatch(ACTION_DRAW);
		Action* action = actionQueue->createAction(batch);

		if (alt && dodraw) {
			// This is exempt from USE_AUTOMAGIC
			g_gui.doodad_buffer_map->clear();
			BaseMap* draw_map = g_gui.doodad_buffer_map;

			for (PositionVector::const_iterator it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
				TileLocation* location = map.createTileL(*it);
				Tile* tile = location->get();
				if (tile) {
					Tile* new_tile = tile->deepCopy(map);
					new_tile->cleanWalls(brush->isWall());
					g_gui.GetCurrentBrush()->draw(draw_map, new_tile);
					draw_map->setTile(*it, new_tile, true);
				} else if (dodraw) {
					Tile* new_tile = map.allocator(location);
					g_gui.GetCurrentBrush()->draw(draw_map, new_tile);
					draw_map->setTile(*it, new_tile, true);
				}
			}
			for (PositionVector::const_iterator it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
				// Get the correct tiles from the draw map instead of the editor map
				Tile* tile = draw_map->getTile(*it);
				if (tile) {
					tile->wallize(draw_map);
					action->addChange(newd Change(tile));
				}
			}
			draw_map->clear(false);
			// Commit
			batch->addAndCommitAction(action);
		} else {
			for (PositionVector::const_iterator it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
				TileLocation* location = map.createTileL(*it);
				Tile* tile = location->get();
				if (tile) {
					Tile* new_tile = tile->deepCopy(map);
					// Wall cleaning is exempt from automagic
					new_tile->cleanWalls(brush->isWall());
					if (dodraw) {
						g_gui.GetCurrentBrush()->draw(&map, new_tile);
					} else {
						g_gui.GetCurrentBrush()->undraw(&map, new_tile);
					}
					action->addChange(newd Change(new_tile));
				} else if (dodraw) {
					Tile* new_tile = map.allocator(location);
					g_gui.GetCurrentBrush()->draw(&map, new_tile);
					action->addChange(newd Change(new_tile));
				}
			}

			// Commit changes to map
			batch->addAndCommitAction(action);

			if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
				// Do borders!
				action = actionQueue->createAction(batch);
				for (PositionVector::const_iterator it = tilestoborder.begin(); it != tilestoborder.end(); ++it) {
					Tile* tile = map.getTile(*it);
					if (tile) {
						Tile* new_tile = tile->deepCopy(map);
						new_tile->wallize(&map);
						// if(*tile == *new_tile) delete new_tile;
						action->addChange(newd Change(new_tile));
					}
				}
				batch->addAndCommitAction(action);
			}
		}

		actionQueue->addBatch(batch, 2);
	} else if (brush->isDoor()) {
		BatchAction* batch = actionQueue->createBatch(ACTION_DRAW);
		Action* action = actionQueue->createAction(batch);
		DoorBrush* door_brush = brush->asDoor();

		// Loop is kind of redundant since there will only ever be one index.
		for (PositionVector::const_iterator it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
			TileLocation* location = map.createTileL(*it);
			Tile* tile = location->get();
			if (tile) {
				Tile* new_tile = tile->deepCopy(map);
				// Wall cleaning is exempt from automagic
				if (brush->isWall()) {
					new_tile->cleanWalls(brush->asWall());
				}
				if (dodraw) {
					door_brush->draw(&map, new_tile, &alt);
				} else {
					door_brush->undraw(&map, new_tile);
				}
				action->addChange(newd Change(new_tile));
			} else if (dodraw) {
				Tile* new_tile = map.allocator(location);
				door_brush->draw(&map, new_tile, &alt);
				action->addChange(newd Change(new_tile));
			}
		}

		// Commit changes to map
		batch->addAndCommitAction(action);

		if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
			// Do borders!
			action = actionQueue->createAction(batch);
			for (PositionVector::const_iterator it = tilestoborder.begin(); it != tilestoborder.end(); ++it) {
				Tile* tile = map.getTile(*it);
				if (tile) {
					Tile* new_tile = tile->deepCopy(map);
					new_tile->wallize(&map);
					// if(*tile == *new_tile) delete new_tile;
					action->addChange(newd Change(new_tile));
				}
			}
			batch->addAndCommitAction(action);
		}

		addBatch(batch, 2);
	} else {
		Action* action = actionQueue->createAction(ACTION_DRAW);
		for (PositionVector::const_iterator it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
			TileLocation* location = map.createTileL(*it);
			Tile* tile = location->get();
			if (tile) {
				Tile* new_tile = tile->deepCopy(map);
				if (dodraw) {
					g_gui.GetCurrentBrush()->draw(&map, new_tile);
				} else {
					g_gui.GetCurrentBrush()->undraw(&map, new_tile);
				}
				action->addChange(newd Change(new_tile));
			} else if (dodraw) {
				Tile* new_tile = map.allocator(location);
				g_gui.GetCurrentBrush()->draw(&map, new_tile);
				action->addChange(newd Change(new_tile));
			}
		}
		addAction(action, 2);
	}
}

///////////////////////////////////////////////////////////////////////////////
// Live!

bool Editor::IsLiveClient() const {
	return live_client != nullptr;
}

bool Editor::IsLiveServer() const {
	return live_server != nullptr;
}

bool Editor::IsLive() const {
	return IsLiveClient() || IsLiveServer();
}

bool Editor::IsLocal() const {
	return !IsLive();
}

LiveClient* Editor::GetLiveClient() const {
	return live_client;
}

LiveServer* Editor::GetLiveServer() const {
	return live_server;
}

LiveSocket &Editor::GetLive() const {
	if (live_server) {
		return *live_server;
	}
	return *live_client;
}

LiveServer* Editor::StartLiveServer() {
	ASSERT(IsLocal());
	live_server = newd LiveServer(*this);

	delete actionQueue;
	actionQueue = newd NetworkedActionQueue(*this);

	return live_server;
}

void Editor::BroadcastNodes(DirtyList &dirtyList) {
	if (IsLiveClient()) {
		live_client->sendChanges(dirtyList);
	} else {
		live_server->broadcastNodes(dirtyList);
	}
}

void Editor::CloseLiveServer() {
	ASSERT(IsLive());
	if (live_client) {
		live_client->close();

		delete live_client;
		live_client = nullptr;
	}

	if (live_server) {
		live_server->close();

		delete live_server;
		live_server = nullptr;

		delete actionQueue;
		actionQueue = newd ActionQueue(*this);
	}

	NetworkConnection &connection = NetworkConnection::getInstance();
	connection.stop();
}

void Editor::QueryNode(int ndx, int ndy, bool underground) {
	ASSERT(live_client);
	live_client->queryNode(ndx, ndy, underground);
}

void Editor::SendNodeRequests() {
	if (live_client) {
		live_client->sendNodeRequests();
	}
}

void Editor::deleteOldBackups(const std::string &backup_path) {
	int days_to_delete = g_settings.getInteger(Config::DELETE_BACKUP_DAYS);
	if (days_to_delete <= 0) {
		return; // Se o valor é zero ou negativo, não deletar backups
	}

	try {
		auto now = std::chrono::system_clock::now();
		for (const auto &entry : fs::directory_iterator(backup_path)) {
			if (fs::is_regular_file(entry.status())) {
				auto file_time = fs::last_write_time(entry);
				auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(file_time - fs::file_time_type::clock::now() + now);
				auto file_age = std::chrono::duration_cast<std::chrono::hours>(now - sctp).count() / 24;

				if (file_age > days_to_delete) {
					fs::remove(entry);
					std::cout << "Deleted old backup: " << entry.path() << std::endl;
				}
			}
		}
	} catch (const fs::filesystem_error &e) {
		std::cerr << "Error: " << e.what() << std::endl;
	}
}
