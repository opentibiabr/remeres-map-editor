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
#if OTGZ_SUPPORT > 0
	#if OTGZ_SUPPORT > 0
		#include <archive.h>
		#include <archive_entry.h>
	#endif
#endif

#include "io/otbm/iomap_otbm.h"

#include "app/settings.h"
#include "ui/gui.h" // KNOWN_VIOLATION - Loadbar
#include "client_assets.h"
#include "game/monsters.h"
#include "game/monster.h"
#include "game/npcs.h"
#include "game/npc.h"
#include "map/map.h"
#include "map/tile.h"
#include "game/item.h"
#include "game/complexitem.h"
#include "game/town.h"

// H4X

/*
	OTBM_ROOTV1
	|
	|--- OTBM_MAP_DATA
	|	|
	|	|--- OTBM_TILE_AREA
	|	|	|--- OTBM_TILE
	|	|	|--- OTBM_TILE_SQUARE (not implemented)
	|	|	|--- OTBM_TILE_REF (not implemented)
	|	|	|--- OTBM_HOUSETILE
	|	|
	|	|--- OTBM_SPAWNS (not implemented)
	|	|	|--- OTBM_SPAWN_AREA (not implemented)
	|	|	|--- OTBM_MONSTER (not implemented)
	|	|
	|	|--- OTBM_TOWNS
	|		|--- OTBM_TOWN
	|
	|--- OTBM_ITEM_DEF (not implemented)
*/

IOMapOTBM::IOMapOTBM(MapVersion ver) {
	version = ver;
}

bool IOMapOTBM::getVersionInfo(const FileName &filename, MapVersion &out_ver) {
#if OTGZ_SUPPORT > 0
	if (filename.GetExt() == "otgz") {
		// Open the archive
		std::shared_ptr<struct archive> a(archive_read_new(), archive_read_free);
		archive_read_support_filter_all(a.get());
		archive_read_support_format_all(a.get());
		if (archive_read_open_filename(a.get(), nstr(filename.GetFullPath()).c_str(), 10240) != ARCHIVE_OK) {
			return false;
		}

		// Loop over the archive entries until we find the otbm file
		struct archive_entry* entry;
		while (archive_read_next_header(a.get(), &entry) == ARCHIVE_OK) {
			std::string entryName = archive_entry_pathname(entry);

			if (entryName == "world/map.otbm") {
				// Read the OTBM header into temporary memory
				uint8_t buffer[8096];
				memset(buffer, 0, 8096);

				// Read from the archive
				int read_bytes = archive_read_data(a.get(), buffer, 8096);

				// Check so it at least contains the 4-byte file id
				if (read_bytes < 4) {
					return false;
				}

				// Create a read handle on it
				std::shared_ptr<NodeFileReadHandle> f(new MemoryNodeFileReadHandle(buffer + 4, read_bytes - 4));

				// Read the version info
				return getVersionInfo(f.get(), out_ver);
			}
		}

		// Didn't find OTBM file, lame
		return false;
	}
#endif

	// Just open a disk-based read handle
	DiskNodeFileReadHandle f(nstr(filename.GetFullPath()), StringVector(1, "OTBM"));
	if (!f.isOk()) {
		return false;
	}
	return getVersionInfo(&f, out_ver);
}

bool IOMapOTBM::getVersionInfo(NodeFileReadHandle* f, MapVersion &out_ver) {
	BinaryNode* root = f->getRootNode();
	if (!root) {
		return false;
	}

	root->skip(1); // Skip the type byte

	uint16_t u16;
	uint32_t u32;

	if (!root->getU32(u32)) { // Version
		return false;
	}

	out_ver.otbm = (MapVersionID)u32;

	root->getU16(u16);
	root->getU16(u16);
	root->getU32(u32);
	root->skip(4); // Skip the otb version (deprecated)

	return true;
}

bool IOMapOTBM::loadMap(Map &map, const FileName &filename) {
#if OTGZ_SUPPORT > 0
	if (filename.GetExt() == "otgz") {
		// Open the archive
		std::shared_ptr<struct archive> a(archive_read_new(), archive_read_free);
		archive_read_support_filter_all(a.get());
		archive_read_support_format_all(a.get());
		if (archive_read_open_filename(a.get(), nstr(filename.GetFullPath()).c_str(), 10240) != ARCHIVE_OK) {
			return false;
		}

		// Memory buffers for the houses & monsters & npcs
		std::shared_ptr<uint8_t> house_buffer;
		std::shared_ptr<uint8_t> spawn_monster_buffer;
		std::shared_ptr<uint8_t> spawn_npc_buffer;
		size_t house_buffer_size = 0;
		size_t spawn_monster_buffer_size = 0;
		size_t spawn_npc_buffer_size = 0;

		// See if the otbm file has been loaded
		bool otbm_loaded = false;

		// Loop over the archive entries until we find the otbm file
		g_gui.SetLoadDone(0, "Decompressing archive...");
		struct archive_entry* entry;
		while (archive_read_next_header(a.get(), &entry) == ARCHIVE_OK) {
			std::string entryName = archive_entry_pathname(entry);

			if (entryName == "world/map.otbm") {
				// Read the entire OTBM file into a memory region
				size_t otbm_size = archive_entry_size(entry);
				std::shared_ptr<uint8_t> otbm_buffer(new uint8_t[otbm_size], [](uint8_t* p) { delete[] p; });

				// Read from the archive
				size_t read_bytes = archive_read_data(a.get(), otbm_buffer.get(), otbm_size);

				// Check so it at least contains the 4-byte file id
				if (read_bytes < 4) {
					return false;
				}

				if (read_bytes < otbm_size) {
					error("Could not read file.");
					return false;
				}

				g_gui.SetLoadDone(0, "Loading OTBM map...");

				// Create a read handle on it
				std::shared_ptr<NodeFileReadHandle> f(
					new MemoryNodeFileReadHandle(otbm_buffer.get() + 4, otbm_size - 4)
				);

				// Read the version info
				if (!loadMap(map, *f.get())) {
					error("Could not load OTBM file inside archive");
					return false;
				}

				otbm_loaded = true;
			} else if (entryName == "world/houses.xml") {
				house_buffer_size = archive_entry_size(entry);
				house_buffer.reset(new uint8_t[house_buffer_size]);

				// Read from the archive
				size_t read_bytes = archive_read_data(a.get(), house_buffer.get(), house_buffer_size);

				// Check so it at least contains the 4-byte file id
				if (read_bytes < house_buffer_size) {
					house_buffer.reset();
					house_buffer_size = 0;
					warning("Failed to decompress houses.");
				}
			} else if (entryName == "world/monsters.xml") {
				spawn_monster_buffer_size = archive_entry_size(entry);
				spawn_monster_buffer.reset(new uint8_t[spawn_monster_buffer_size]);

				// Read from the archive
				size_t read_bytes = archive_read_data(a.get(), spawn_monster_buffer.get(), spawn_monster_buffer_size);

				// Check so it at least contains the 4-byte file id
				if (read_bytes < spawn_monster_buffer_size) {
					spawn_monster_buffer.reset();
					spawn_monster_buffer_size = 0;
					warning("Failed to decompress monsters spawns.");
				}
			} else if (entryName == "world/npcs.xml") {
				spawn_npc_buffer_size = archive_entry_size(entry);
				spawn_npc_buffer.reset(new uint8_t[spawn_npc_buffer_size]);

				// Read from the archive
				size_t read_bytes = archive_read_data(a.get(), spawn_npc_buffer.get(), spawn_npc_buffer_size);

				// Check so it at least contains the 4-byte file id
				if (read_bytes < spawn_npc_buffer_size) {
					spawn_npc_buffer.reset();
					spawn_npc_buffer_size = 0;
					warning("Failed to decompress npcs spawns.");
				}
			}
		}

		if (!otbm_loaded) {
			error("OTBM file not found inside archive.");
			return false;
		}

		// Load the houses from the stored buffer
		if (house_buffer.get() && house_buffer_size > 0) {
			pugi::xml_document doc;
			pugi::xml_parse_result result = doc.load_buffer(house_buffer.get(), house_buffer_size);
			if (result) {
				if (!loadHouses(map, doc)) {
					warning("Failed to load houses.");
				}
			} else {
				warning("Failed to load houses due to XML parse error.");
			}
		}

		// Load the monster spawns from the stored buffer
		if (spawn_monster_buffer.get() && spawn_monster_buffer_size > 0) {
			pugi::xml_document doc;
			pugi::xml_parse_result result = doc.load_buffer(spawn_monster_buffer.get(), spawn_monster_buffer_size);
			if (result) {
				if (!loadSpawnsMonster(map, doc)) {
					warning("Failed to load monsters spawns.");
				}
			} else {
				warning("Failed to load monsters spawns due to XML parse error.");
			}
		}

		// Load the npcs from the stored buffer
		if (spawn_npc_buffer.get() && spawn_npc_buffer_size > 0) {
			pugi::xml_document doc;
			pugi::xml_parse_result result = doc.load_buffer(spawn_npc_buffer.get(), spawn_npc_buffer_size);
			if (result) {
				if (!loadSpawnsNpc(map, doc)) {
					warning("Failed to load npcs spawns.");
				}
			} else {
				warning("Failed to load npcs spawns due to XML parse error.");
			}
		}

		return true;
	}
#endif

	DiskNodeFileReadHandle f(nstr(filename.GetFullPath()), StringVector(1, "OTBM"));
	if (!f.isOk()) {
		error(("Couldn't open file for reading\nThe error reported was: " + wxstr(f.getErrorMessage())).wc_str());
		return false;
	}

	if (!loadMap(map, f)) {
		return false;
	}

	// Read auxilliary files
	if (!loadHouses(map, filename)) {
		warning("Failed to load houses.");
		map.housefile = nstr(filename.GetName()) + "-house.xml";
	}
	if (!loadZones(map, filename)) {
		warning("Failed to load zones.");
		map.zonefile = nstr(filename.GetName()) + "-zones.xml";
	}
	if (!loadSpawnsMonster(map, filename)) {
		warning("Failed to load monsters spawns.");
		map.spawnmonsterfile = nstr(filename.GetName()) + "-monster.xml";
	}
	if (!loadSpawnsNpc(map, filename)) {
		warning("Failed to load npcs spawns.");
		map.spawnnpcfile = nstr(filename.GetName()) + "-npc.xml";
	}
	return true;
}

bool IOMapOTBM::loadMap(Map &map, NodeFileReadHandle &f) {
	BinaryNode* root = f.getRootNode();
	if (!root) {
		error("Could not read root node.");
		return false;
	}
	root->skip(1); // Skip the type byte

	uint8_t u8;
	uint16_t u16;
	uint32_t u32;

	if (!root->getU32(u32)) {
		return false;
	}

	version.otbm = (MapVersionID)u32;

	if (version.otbm > MAP_OTBM_LAST_VERSION) {
		// Failed to read version
		if (g_gui.PopupDialog("Map error", "The loaded map appears to be a OTBM format that is not supported by the editor."
										   "Do you still want to attempt to load the map?",
							  wxYES | wxNO)
			== wxID_YES) {
			warning("Unsupported or damaged map version");
		} else {
			error("Unsupported OTBM version, could not load map");
			return false;
		}
	}

	if (!root->getU16(u16)) {
		return false;
	}

	map.width = u16;
	if (!root->getU16(u16)) {
		return false;
	}

	map.height = u16;

	BinaryNode* mapHeaderNode = root->getChild();
	if (mapHeaderNode == nullptr || !mapHeaderNode->getByte(u8) || u8 != OTBM_MAP_DATA) {
		error("Could not get root child node. Cannot recover from fatal error!");
		return false;
	}

	uint8_t attribute;
	while (mapHeaderNode->getU8(attribute)) {
		switch (attribute) {
			case OTBM_ATTR_DESCRIPTION: {
				if (!mapHeaderNode->getString(map.description)) {
					warning("Invalid map description tag");
				}
				// std::cout << "Map description: " << mapDescription << std::endl;
				break;
			}
			case OTBM_ATTR_EXT_SPAWN_MONSTER_FILE: {
				if (!mapHeaderNode->getString(map.spawnmonsterfile)) {
					warning("Invalid map spawnmonsterfile tag");
				}
				break;
			}
			case OTBM_ATTR_EXT_HOUSE_FILE: {
				if (!mapHeaderNode->getString(map.housefile)) {
					warning("Invalid map housefile tag");
				}
				break;
			}
			case OTBM_ATTR_EXT_ZONE_FILE: {
				if (!mapHeaderNode->getString(map.zonefile)) {
					warning("Invalid map zonefile tag");
				}
				break;
			}
			case OTBM_ATTR_EXT_SPAWN_NPC_FILE: {
				if (!mapHeaderNode->getString(map.spawnnpcfile)) {
					warning("Invalid map spawnnpcfile tag");
				}
				break;
			}
			default: {
				warning("Unknown header node.");
				break;
			}
		}
	}

	int nodes_loaded = 0;

	for (BinaryNode* mapNode = mapHeaderNode->getChild(); mapNode != nullptr; mapNode = mapNode->advance()) {
		++nodes_loaded;
		if (nodes_loaded % 15 == 0) {
			g_gui.SetLoadDone(static_cast<int32_t>(100.0 * f.tell() / f.size()));
		}

		uint8_t node_type;
		if (!mapNode->getByte(node_type)) {
			warning("Invalid map node");
			continue;
		}
		if (node_type == OTBM_TILE_AREA) {
			uint16_t base_x, base_y;
			uint8_t base_z;
			if (!mapNode->getU16(base_x) || !mapNode->getU16(base_y) || !mapNode->getU8(base_z)) {
				warning("Invalid map node (type %d), no base coordinate", node_type);
				continue;
			}

			for (BinaryNode* tileNode = mapNode->getChild(); tileNode != nullptr; tileNode = tileNode->advance()) {
				Tile* tile = nullptr;
				uint8_t tile_type;
				if (!tileNode->getByte(tile_type)) {
					warning("Invalid tile type in area %d:%d:%d", base_x, base_y, base_z);
					continue;
				}
				if (tile_type == OTBM_TILE || tile_type == OTBM_HOUSETILE) {
					// printf("Start\n");
					uint8_t x_offset, y_offset;
					if (!tileNode->getU8(x_offset) || !tileNode->getU8(y_offset)) {
						warning("Could not read position of tile in area %d:%d:%d", base_x, base_y, base_z);
						continue;
					}
					const Position pos(base_x + x_offset, base_y + y_offset, base_z);

					if (map.getTile(pos)) {
						warning("Duplicate tile at %d:%d:%d, discarding duplicate", pos.x, pos.y, pos.z);
						continue;
					}

					tile = map.allocator(map.createTileL(pos));
					House* house = nullptr;
					if (tile_type == OTBM_HOUSETILE) {
						uint32_t house_id;
						if (!tileNode->getU32(house_id)) {
							warning("House tile without house data, discarding tile");
							delete tile;
							continue;
						}
						if (house_id) {
							house = map.houses.getHouse(house_id);
							if (!house) {
								house = newd House(map);
								house->id = house_id;
								map.houses.addHouse(house);
							}
						} else {
							warning("Invalid house id from tile %d:%d:%d", pos.x, pos.y, pos.z);
						}
					}

					// printf("So far so good\n");

					uint8_t attribute;
					while (tileNode->getU8(attribute)) {
						switch (attribute) {
							case OTBM_ATTR_TILE_FLAGS: {
								uint32_t flags = 0;
								if (!tileNode->getU32(flags)) {
									warning("Invalid tile flags of tile on %d:%d:%d", pos.x, pos.y, pos.z);
								}
								tile->setMapFlags(flags);
								break;
							}
							case OTBM_ATTR_ITEM: {
								Item* item = Item::Create_OTBM(*this, tileNode);
								if (item == nullptr) {
									warning("Invalid item at tile %d:%d:%d", pos.x, pos.y, pos.z);
								}
								tile->addItem(item);
								break;
							}
							default: {
								warning("Unknown tile attribute at %d:%d:%d", pos.x, pos.y, pos.z);
								break;
							}
						}
					}

					// printf("Didn't die in loop\n");

					for (BinaryNode* childNode = tileNode->getChild(); childNode != nullptr; childNode = childNode->advance()) {
						Item* item = nullptr;
						uint8_t node_type;
						if (!childNode->getByte(node_type)) {
							warning("Unknown item type %d:%d:%d", pos.x, pos.y, pos.z);
							continue;
						}
						if (node_type == OTBM_ITEM) {
							item = Item::Create_OTBM(*this, childNode);
							if (item) {
								if (!item->unserializeItemNode_OTBM(*this, childNode)) {
									warning("Couldn't unserialize item attributes at %d:%d:%d", pos.x, pos.y, pos.z);
								}
								// reform(&map, tile, item);
								tile->addItem(item);
							}
						} else if (node_type == OTBM_TILE_ZONE) {
							uint16_t zone_count;
							if (!childNode->getU16(zone_count)) {
								warning("Invalid zone count at %d:%d:%d", pos.x, pos.y, pos.z);
								continue;
							}
							for (uint16_t i = 0; i < zone_count; ++i) {
								uint16_t zone_id;
								if (!childNode->getU16(zone_id)) {
									warning("Invalid zone id at %d:%d:%d", pos.x, pos.y, pos.z);
									continue;
								}
								tile->addZone(zone_id);
							}
						} else {
							warning("Unknown type of tile child node");
						}
					}

					tile->update();
					if (house) {
						house->addTile(tile);
					}

					map.setTile(pos.x, pos.y, pos.z, tile);
				} else {
					warning("Unknown type of tile node");
				}
			}
		} else if (node_type == OTBM_TOWNS) {
			for (BinaryNode* townNode = mapNode->getChild(); townNode != nullptr; townNode = townNode->advance()) {
				Town* town = nullptr;
				uint8_t town_type;
				if (!townNode->getByte(town_type)) {
					warning("Invalid town type (1)");
					continue;
				}
				if (town_type != OTBM_TOWN) {
					warning("Invalid town type (2)");
					continue;
				}
				uint32_t town_id;
				if (!townNode->getU32(town_id)) {
					warning("Invalid town id");
					continue;
				}

				town = map.towns.getTown(town_id);
				if (town) {
					warning("Duplicate town id %d, discarding duplicate", town_id);
					continue;
				} else {
					town = newd Town(town_id);
					if (!map.towns.addTown(town)) {
						delete town;
						continue;
					}
				}
				std::string town_name;
				if (!townNode->getString(town_name)) {
					warning("Invalid town name");
					continue;
				}
				town->setName(town_name);
				Position pos;
				uint16_t x;
				uint16_t y;
				uint8_t z;
				if (!townNode->getU16(x) || !townNode->getU16(y) || !townNode->getU8(z)) {
					warning("Invalid town temple position");
					continue;
				}
				pos.x = x;
				pos.y = y;
				pos.z = z;
				town->setTemplePosition(pos);
			}
		} else if (node_type == OTBM_WAYPOINTS) {
			for (BinaryNode* waypointNode = mapNode->getChild(); waypointNode != nullptr; waypointNode = waypointNode->advance()) {
				uint8_t waypoint_type;
				if (!waypointNode->getByte(waypoint_type)) {
					warning("Invalid waypoint type (1)");
					continue;
				}
				if (waypoint_type != OTBM_WAYPOINT) {
					warning("Invalid waypoint type (2)");
					continue;
				}

				Waypoint wp;

				if (!waypointNode->getString(wp.name)) {
					warning("Invalid waypoint name");
					continue;
				}
				uint16_t x;
				uint16_t y;
				uint8_t z;
				if (!waypointNode->getU16(x) || !waypointNode->getU16(y) || !waypointNode->getU8(z)) {
					warning("Invalid waypoint position");
					continue;
				}
				wp.pos.x = x;
				wp.pos.y = y;
				wp.pos.z = z;

				map.waypoints.addWaypoint(newd Waypoint(wp));
			}
		}
	}

	if (!f.isOk()) {
		warning("OTBM loading error: %s (at file position %zu of %zu bytes)", wxstr(f.getErrorMessage()).wc_str(), f.tell(), f.size());
	}
	return true;
}

bool IOMapOTBM::saveMap(Map &map, const FileName &identifier) {
#if OTGZ_SUPPORT > 0
	if (identifier.GetExt() == "otgz") {
		// Create the archive
		struct archive* a = archive_write_new();
		struct archive_entry* entry = nullptr;
		std::ostringstream streamData;

		archive_write_set_compression_gzip(a);
		archive_write_set_format_pax_restricted(a);
		archive_write_open_filename(a, nstr(identifier.GetFullPath()).c_str());

		g_gui.SetLoadDone(0, "Saving monsters...");

		pugi::xml_document spawnDoc;
		if (saveSpawns(map, spawnDoc)) {
			// Write the data
			spawnDoc.save(streamData, "", pugi::format_raw, pugi::encoding_utf8);
			std::string xmlData = streamData.str();

			// Write to the arhive
			entry = archive_entry_new();
			archive_entry_set_pathname(entry, "world/monsters.xml");
			archive_entry_set_size(entry, xmlData.size());
			archive_entry_set_filetype(entry, AE_IFREG);
			archive_entry_set_perm(entry, 0644);

			// Write to the archive
			archive_write_header(a, entry);
			archive_write_data(a, xmlData.data(), xmlData.size());

			// Free the entry
			archive_entry_free(entry);
			streamData.str("");
		}

		g_gui.SetLoadDone(0, "Saving houses...");

		pugi::xml_document houseDoc;
		if (saveHouses(map, houseDoc)) {
			// Write the data
			houseDoc.save(streamData, "", pugi::format_raw, pugi::encoding_utf8);
			std::string xmlData = streamData.str();

			// Write to the arhive
			entry = archive_entry_new();
			archive_entry_set_pathname(entry, "world/houses.xml");
			archive_entry_set_size(entry, xmlData.size());
			archive_entry_set_filetype(entry, AE_IFREG);
			archive_entry_set_perm(entry, 0644);

			// Write to the archive
			archive_write_header(a, entry);
			archive_write_data(a, xmlData.data(), xmlData.size());

			// Free the entry
			archive_entry_free(entry);
			streamData.str("");
		}

		g_gui.SetLoadDone(0, "Saving npcs...");

		pugi::xml_document npcDoc;
		if (saveSpawnsNpc(map, npcDoc)) {
			// Write the data
			npcDoc.save(streamData, "", pugi::format_raw, pugi::encoding_utf8);
			std::string xmlData = streamData.str();

			// Write to the arhive
			entry = archive_entry_new();
			archive_entry_set_pathname(entry, "world/npcs.xml");
			archive_entry_set_size(entry, xmlData.size());
			archive_entry_set_filetype(entry, AE_IFREG);
			archive_entry_set_perm(entry, 0644);

			// Write to the archive
			archive_write_header(a, entry);
			archive_write_data(a, xmlData.data(), xmlData.size());

			// Free the entry
			archive_entry_free(entry);
			streamData.str("");
		}

		g_gui.SetLoadDone(0, "Saving OTBM map...");

		MemoryNodeFileWriteHandle otbmWriter;
		saveMap(map, otbmWriter);

		g_gui.SetLoadDone(75, "Compressing...");

		// Create an archive entry for the otbm file
		entry = archive_entry_new();
		archive_entry_set_pathname(entry, "world/map.otbm");
		archive_entry_set_size(entry, otbmWriter.getSize() + 4); // 4 bytes extra for header
		archive_entry_set_filetype(entry, AE_IFREG);
		archive_entry_set_perm(entry, 0644);
		archive_write_header(a, entry);

		// Write the version header
		char otbm_identifier[] = "OTBM";
		archive_write_data(a, otbm_identifier, 4);

		// Write the OTBM data
		archive_write_data(a, otbmWriter.getMemory(), otbmWriter.getSize());
		archive_entry_free(entry);

		// Free / close the archive
		archive_write_close(a);
		archive_write_free(a);

		g_gui.DestroyLoadBar();
		return true;
	}
#endif

	DiskNodeFileWriteHandle f(
		nstr(identifier.GetFullPath()),
		(g_settings.getInteger(Config::SAVE_WITH_OTB_MAGIC_NUMBER) ? "OTBM" : std::string(4, '\0'))
	);

	if (!f.isOk()) {
		error("Can not open file %s for writing", (const char*)identifier.GetFullPath().mb_str(wxConvUTF8));
		return false;
	}

	if (!saveMap(map, f)) {
		return false;
	}

	g_gui.SetLoadDone(99, "Saving monster spawns...");
	saveSpawns(map, identifier);

	g_gui.SetLoadDone(99, "Saving houses...");
	saveHouses(map, identifier);

	g_gui.SetLoadDone(99, "Saving zones...");
	saveZones(map, identifier);

	g_gui.SetLoadDone(99, "Saving npcs spawns...");
	saveSpawnsNpc(map, identifier);
	return true;
}

bool IOMapOTBM::saveMap(Map &map, NodeFileWriteHandle &f) {
	/* STOP!
	 * Before you even think about modifying this, please reconsider.
	 * while adding stuff to the binary format may be "cool", you'll
	 * inevitably make it incompatible with any future releases of
	 * the map editor, meaning you cannot reuse your map. Before you
	 * try to modify this, PLEASE consider using an external file
	 * like monster.xml or house.xml, as that will be MUCH easier
	 * to port to newer versions of the editor than a custom binary
	 * format.
	 */

	const IOMapOTBM &self = *this;

	FileName tmpName;
	f.addNode(0);
	{
		const auto mapVersion = map.mapVersion.otbm < MapVersionID::MAP_OTBM_5 ? MapVersionID::MAP_OTBM_5 : map.mapVersion.otbm;
		f.addU32(mapVersion); // Map version

		f.addU16(map.width);
		f.addU16(map.height);

		f.addU32(4); // Major otb version (deprecated)
		f.addU32(4); // Minor otb version (deprecated)

		f.addNode(OTBM_MAP_DATA);
		{
			f.addByte(OTBM_ATTR_DESCRIPTION);
			// Neither SimOne's nor OpenTibia cares for additional description tags
			f.addString("Saved with Canary's Map Editor " + __RME_VERSION__);

			f.addU8(OTBM_ATTR_DESCRIPTION);
			f.addString(map.description);

			tmpName.Assign(wxstr(map.spawnmonsterfile));
			f.addU8(OTBM_ATTR_EXT_SPAWN_MONSTER_FILE);
			f.addString(nstr(tmpName.GetFullName()));

			tmpName.Assign(wxstr(map.spawnnpcfile));
			f.addU8(OTBM_ATTR_EXT_SPAWN_NPC_FILE);
			f.addString(nstr(tmpName.GetFullName()));

			tmpName.Assign(wxstr(map.housefile));
			f.addU8(OTBM_ATTR_EXT_HOUSE_FILE);
			f.addString(nstr(tmpName.GetFullName()));

			tmpName.Assign(wxstr(map.zonefile));
			f.addU8(OTBM_ATTR_EXT_ZONE_FILE);
			f.addString(nstr(tmpName.GetFullName()));

			// Start writing tiles
			uint32_t tiles_saved = 0;
			bool first = true;

			int local_x = -1, local_y = -1, local_z = -1;

			MapIterator map_iterator = map.begin();
			while (map_iterator != map.end()) {
				// Update progressbar
				++tiles_saved;
				if (tiles_saved % 8192 == 0) {
					g_gui.SetLoadDone(int(tiles_saved / double(map.getTileCount()) * 100.0));
				}

				// Get tile
				Tile* save_tile = (*map_iterator)->get();

				// Is it an empty tile that we can skip? (Leftovers...)
				if (!save_tile || save_tile->size() == 0) {
					++map_iterator;
					continue;
				}

				const Position &pos = save_tile->getPosition();

				// Decide if newd node should be created
				if (pos.x < local_x || pos.x >= local_x + 256 || pos.y < local_y || pos.y >= local_y + 256 || pos.z != local_z) {
					// End last node
					if (!first) {
						f.endNode();
					}
					first = false;

					// Start newd node
					f.addNode(OTBM_TILE_AREA);
					f.addU16(local_x = pos.x & 0xFF00);
					f.addU16(local_y = pos.y & 0xFF00);
					f.addU8(local_z = pos.z);
				}
				f.addNode(save_tile->isHouseTile() ? OTBM_HOUSETILE : OTBM_TILE);

				f.addU8(save_tile->getX() & 0xFF);
				f.addU8(save_tile->getY() & 0xFF);

				if (save_tile->isHouseTile()) {
					f.addU32(save_tile->getHouseID());
				}

				if (save_tile->getMapFlags()) {
					f.addByte(OTBM_ATTR_TILE_FLAGS);
					f.addU32(save_tile->getMapFlags());
				}

				if (save_tile->ground) {
					Item* ground = save_tile->ground;
					if (ground->isMetaItem()) {
						// Do nothing, we don't save metaitems...
					} else if (ground->hasBorderEquivalent()) {
						bool found = false;
						for (Item* item : save_tile->items) {
							if (item->getGroundEquivalent() == ground->getID()) {
								// Do nothing
								// Found equivalent
								found = true;
								break;
							}
						}

						if (!found) {
							if (ground->getID() == 0) {
								continue;
							}
							ground->serializeItemNode_OTBM(self, f);
						}
					} else if (ground->isComplex()) {
						if (ground->getID() == 0) {
							continue;
						}
						ground->serializeItemNode_OTBM(self, f);
					} else {
						f.addByte(OTBM_ATTR_ITEM);
						if (ground->getID() == 0) {
							continue;
						}
						ground->serializeItemCompact_OTBM(self, f);
					}
				}

				for (Item* item : save_tile->items) {
					if (!item->isMetaItem()) {
						if (item->getID() == 0) {
							continue;
						}
						item->serializeItemNode_OTBM(self, f);
					}
				}
				if (!save_tile->zones.empty()) {
					f.addNode(OTBM_TILE_ZONE);
					f.addU16(save_tile->zones.size());
					for (const auto &zoneId : save_tile->zones) {
						f.addU16(zoneId);
					}
					f.endNode();
				}

				f.endNode();
				++map_iterator;
			}

			// Only close the last node if one has actually been created
			if (!first) {
				f.endNode();
			}

			f.addNode(OTBM_TOWNS);
			for (const auto &townEntry : map.towns) {
				Town* town = townEntry.second;
				const Position &townPosition = town->getTemplePosition();
				f.addNode(OTBM_TOWN);
				f.addU32(town->getID());
				f.addString(town->getName());
				f.addU16(townPosition.x);
				f.addU16(townPosition.y);
				f.addU8(townPosition.z);
				f.endNode();
			}
			f.endNode();

			if (version.otbm >= MAP_OTBM_3) {
				f.addNode(OTBM_WAYPOINTS);
				for (const auto &waypointEntry : map.waypoints) {
					Waypoint* waypoint = waypointEntry.second;
					f.addNode(OTBM_WAYPOINT);
					f.addString(waypoint->name);
					f.addU16(waypoint->pos.x);
					f.addU16(waypoint->pos.y);
					f.addU8(waypoint->pos.z);
					f.endNode();
				}
				f.endNode();
			}
		}
		f.endNode();
	}
	f.endNode();
	return true;
}
