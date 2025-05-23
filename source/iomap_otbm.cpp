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

#include "iomap_otbm.h"

#include "settings.h"
#include "gui.h" // Loadbar
#include "client_assets.h"
#include "monsters.h"
#include "monster.h"
#include "npcs.h"
#include "npc.h"
#include "map.h"
#include "tile.h"
#include "item.h"
#include "complexitem.h"
#include "town.h"

typedef uint8_t attribute_t;
typedef uint32_t flags_t;

// H4X
void reform(Map* map, Tile* tile, Item* item) {
	/*
	int aid = item->getActionID();
	int id = item->getID();
	int uid = item->getUniqueID();

	if(item->isDoor()) {
		item->eraseAttribute("aid");
		item->setAttribute("keyid", aid);
	}

	if((item->isDoor()) && tile && tile->getHouseID()) {
		Door* self = static_cast<Door*>(item);
		House* house = map->houses.getHouse(tile->getHouseID());
		self->setDoorID(house->getEmptyDoorID());
	}
	*/
}

// ============================================================================
// Item

Item* Item::Create_OTBM(const IOMap &maphandle, BinaryNode* stream) {
	uint16_t id;
	if (!stream->getU16(id)) {
		return nullptr;
	}

	const ItemType &type = g_items.getItemType(id);
	uint8_t count = 0;
	if (maphandle.version.otbm == MAP_OTBM_1) {
		if (type.stackable || type.isSplash() || type.isFluidContainer()) {
			stream->getU8(count);
		}
	}
	return Item::Create(id, count);
}

bool Item::readItemAttribute_OTBM(const IOMap &maphandle, OTBM_ItemAttribute attr, BinaryNode* stream) {
	switch (attr) {
		case OTBM_ATTR_COUNT: {
			uint8_t subtype;
			if (!stream->getU8(subtype)) {
				return false;
			}
			setSubtype(subtype);
			break;
		}
		case OTBM_ATTR_ACTION_ID: {
			uint16_t aid;
			if (!stream->getU16(aid)) {
				return false;
			}
			setActionID(aid);
			break;
		}
		case OTBM_ATTR_UNIQUE_ID: {
			uint16_t uid;
			if (!stream->getU16(uid)) {
				return false;
			}
			setUniqueID(uid);
			break;
		}
		case OTBM_ATTR_CHARGES: {
			uint16_t charges;
			if (!stream->getU16(charges)) {
				return false;
			}
			setSubtype(charges);
			break;
		}
		case OTBM_ATTR_TEXT: {
			std::string text;
			if (!stream->getString(text)) {
				return false;
			}
			setText(text);
			break;
		}
		case OTBM_ATTR_DESC: {
			std::string text;
			if (!stream->getString(text)) {
				return false;
			}
			setDescription(text);
			break;
		}
		case OTBM_ATTR_RUNE_CHARGES: {
			uint8_t subtype;
			if (!stream->getU8(subtype)) {
				return false;
			}
			setSubtype(subtype);
			break;
		}

		// The following *should* be handled in the derived classes
		// However, we still need to handle them here since otherwise things
		// will break horribly
		case OTBM_ATTR_DEPOT_ID:
			return stream->skip(2);
		case OTBM_ATTR_HOUSEDOORID:
			return stream->skip(1);
		case OTBM_ATTR_TELE_DEST:
			return stream->skip(5);
		default:
			return false;
	}
	return true;
}

bool Item::unserializeAttributes_OTBM(const IOMap &maphandle, BinaryNode* stream) {
	uint8_t attribute;
	while (stream->getU8(attribute)) {
		if (attribute == OTBM_ATTR_ATTRIBUTE_MAP) {
			if (!ItemAttributes::unserializeAttributeMap(maphandle, stream)) {
				return false;
			}
		} else if (!readItemAttribute_OTBM(maphandle, static_cast<OTBM_ItemAttribute>(attribute), stream)) {
			return false;
		}
	}
	return true;
}

bool Item::unserializeItemNode_OTBM(const IOMap &maphandle, BinaryNode* node) {
	return unserializeAttributes_OTBM(maphandle, node);
}

void Item::serializeItemAttributes_OTBM(const IOMap &maphandle, NodeFileWriteHandle &stream) const {
	if (maphandle.version.otbm >= MAP_OTBM_2) {
		const ItemType &type = g_items.getItemType(id);
		if (type.stackable || type.isSplash() || type.isFluidContainer()) {
			stream.addU8(OTBM_ATTR_COUNT);
			stream.addU8(getSubtype());
		}
	}

	if (maphandle.version.otbm == MAP_OTBM_4 || maphandle.version.otbm > MAP_OTBM_5) {
		if (attributes && !attributes->empty()) {
			stream.addU8(OTBM_ATTR_ATTRIBUTE_MAP);
			serializeAttributeMap(maphandle, stream);
		}
	} else {
		if (isCharged()) {
			stream.addU8(OTBM_ATTR_CHARGES);
			stream.addU16(getSubtype());
		}

		const auto actionId = getActionID();
		if (actionId > 0) {
			stream.addU8(OTBM_ATTR_ACTION_ID);
			stream.addU16(actionId);
		}

		const auto uniqueId = getUniqueID();
		if (uniqueId > 0) {
			stream.addU8(OTBM_ATTR_UNIQUE_ID);
			stream.addU16(uniqueId);
		}

		const std::string &text = getText();
		if (!text.empty()) {
			stream.addU8(OTBM_ATTR_TEXT);
			stream.addString(text);
		}

		const std::string &description = getDescription();
		if (!description.empty()) {
			stream.addU8(OTBM_ATTR_DESC);
			stream.addString(description);
		}
	}
}

void Item::serializeItemCompact_OTBM(const IOMap &maphandle, NodeFileWriteHandle &stream) const {
	stream.addU16(id);

	/* This is impossible
	const ItemType& iType = g_items[id];

	if(iType.stackable || iType.isSplash() || iType.isFluidContainer()){
		stream.addU8(getSubtype());
	}
	*/
}

bool Item::serializeItemNode_OTBM(const IOMap &maphandle, NodeFileWriteHandle &file) const {
	file.addNode(OTBM_ITEM);
	file.addU16(id);
	if (maphandle.version.otbm == MAP_OTBM_1) {
		const ItemType &type = g_items.getItemType(id);
		if (type.stackable || type.isSplash() || type.isFluidContainer()) {
			file.addU8(getSubtype());
		}
	}
	serializeItemAttributes_OTBM(maphandle, file);
	file.endNode();
	return true;
}

// ============================================================================
// Teleport

bool Teleport::readItemAttribute_OTBM(const IOMap &maphandle, OTBM_ItemAttribute attribute, BinaryNode* stream) {
	if (OTBM_ATTR_TELE_DEST == attribute) {
		uint16_t x, y;
		uint8_t z;
		if (!stream->getU16(x) || !stream->getU16(y) || !stream->getU8(z)) {
			return false;
		}
		setDestination(Position(x, y, z));
		return true;
	} else {
		return Item::readItemAttribute_OTBM(maphandle, attribute, stream);
	}
}

void Teleport::serializeItemAttributes_OTBM(const IOMap &maphandle, NodeFileWriteHandle &stream) const {
	Item::serializeItemAttributes_OTBM(maphandle, stream);

	stream.addByte(OTBM_ATTR_TELE_DEST);
	stream.addU16(destination.x);
	stream.addU16(destination.y);
	stream.addU8(destination.z);
}

// ============================================================================
// Door

bool Door::readItemAttribute_OTBM(const IOMap &maphandle, OTBM_ItemAttribute attribute, BinaryNode* stream) {
	if (OTBM_ATTR_HOUSEDOORID == attribute) {
		uint8_t id = 0;
		if (!stream->getU8(id)) {
			return false;
		}
		setDoorID(id);
		return true;
	} else {
		return Item::readItemAttribute_OTBM(maphandle, attribute, stream);
	}
}

void Door::serializeItemAttributes_OTBM(const IOMap &maphandle, NodeFileWriteHandle &stream) const {
	Item::serializeItemAttributes_OTBM(maphandle, stream);
	if (doorId) {
		stream.addByte(OTBM_ATTR_HOUSEDOORID);
		stream.addU8(doorId);
	}
}

// ============================================================================
// Depots

bool Depot::readItemAttribute_OTBM(const IOMap &maphandle, OTBM_ItemAttribute attribute, BinaryNode* stream) {
	if (OTBM_ATTR_DEPOT_ID == attribute) {
		uint16_t id = 0;
		const auto read = stream->getU16(id);
		setDepotID(id);
		return read;
	} else {
		return Item::readItemAttribute_OTBM(maphandle, attribute, stream);
	}
}

void Depot::serializeItemAttributes_OTBM(const IOMap &maphandle, NodeFileWriteHandle &stream) const {
	Item::serializeItemAttributes_OTBM(maphandle, stream);
	if (depotId) {
		stream.addByte(OTBM_ATTR_DEPOT_ID);
		stream.addU16(depotId);
	}
}

// ============================================================================
// Container

bool Container::unserializeItemNode_OTBM(const IOMap &maphandle, BinaryNode* node) {
	if (!Item::unserializeAttributes_OTBM(maphandle, node)) {
		return false;
	}

	BinaryNode* child = node->getChild();
	if (child) {
		do {
			uint8_t type;
			if (!child->getByte(type)) {
				return false;
			}

			if (type != OTBM_ITEM) {
				return false;
			}

			Item* item = Item::Create_OTBM(maphandle, child);
			if (!item) {
				return false;
			}

			if (!item->unserializeItemNode_OTBM(maphandle, child)) {
				delete item;
				return false;
			}

			contents.push_back(item);
		} while (child->advance());
	}
	return true;
}

bool Container::serializeItemNode_OTBM(const IOMap &maphandle, NodeFileWriteHandle &file) const {
	file.addNode(OTBM_ITEM);
	file.addU16(id);
	if (maphandle.version.otbm == MAP_OTBM_1) {
		// In the ludicrous event that an item is a container AND stackable, we have to do this. :p
		const ItemType &type = g_items.getItemType(id);
		if (type.stackable || type.isSplash() || type.isFluidContainer()) {
			file.addU8(getSubtype());
		}
	}

	serializeItemAttributes_OTBM(maphandle, file);
	for (Item* item : contents) {
		item->serializeItemNode_OTBM(maphandle, file);
	}

	file.endNode();
	return true;
}

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
				warning("Invalid map node, no base coordinate");
				continue;
			}

			for (BinaryNode* tileNode = mapNode->getChild(); tileNode != nullptr; tileNode = tileNode->advance()) {
				Tile* tile = nullptr;
				uint8_t tile_type;
				if (!tileNode->getByte(tile_type)) {
					warning("Invalid tile type");
					continue;
				}
				if (tile_type == OTBM_TILE || tile_type == OTBM_HOUSETILE) {
					// printf("Start\n");
					uint8_t x_offset, y_offset;
					if (!tileNode->getU8(x_offset) || !tileNode->getU8(y_offset)) {
						warning("Could not read position of tile");
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
		warning(wxstr(f.getErrorMessage()).wc_str());
	}
	return true;
}

bool IOMapOTBM::loadSpawnsMonster(Map &map, const FileName &dir) {
	std::string fn = (const char*)(dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME).mb_str(wxConvUTF8));
	fn += map.spawnmonsterfile;

	FileName filename(wxstr(fn));
	if (!filename.FileExists()) {
		return false;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(fn.c_str());
	if (!result) {
		return false;
	}
	return loadSpawnsMonster(map, doc);
}

bool IOMapOTBM::loadSpawnsMonster(Map &map, pugi::xml_document &doc) {
	pugi::xml_node node = doc.child("monsters");
	if (!node) {
		warnings.push_back("IOMapOTBM::loadSpawnsMonster: Invalid rootheader.");
		return false;
	}

	for (pugi::xml_node spawnNode = node.first_child(); spawnNode; spawnNode = spawnNode.next_sibling()) {
		if (as_lower_str(spawnNode.name()) != "monster") {
			continue;
		}

		Position spawnPosition;
		spawnPosition.x = spawnNode.attribute("centerx").as_int();
		spawnPosition.y = spawnNode.attribute("centery").as_int();
		spawnPosition.z = spawnNode.attribute("centerz").as_int();

		if (spawnPosition.x == 0 || spawnPosition.y == 0) {
			warning("Bad position data on one monster spawn, discarding...");
			continue;
		}

		int32_t radius = spawnNode.attribute("radius").as_int();
		if (radius < 1) {
			warning("Couldn't read radius of monster spawn.. discarding spawn...");
			continue;
		}

		Tile* tile = map.getTile(spawnPosition);
		if (tile && tile->spawnMonster) {
			warning("Duplicate monster spawn on position %d:%d:%d\n", tile->getX(), tile->getY(), tile->getZ());
			continue;
		}

		SpawnMonster* spawnMonster = newd SpawnMonster(radius);
		if (!tile) {
			tile = map.allocator(map.createTileL(spawnPosition));
			map.setTile(spawnPosition, tile);
		}

		tile->spawnMonster = spawnMonster;
		map.addSpawnMonster(tile);

		for (pugi::xml_node monsterNode = spawnNode.first_child(); monsterNode; monsterNode = monsterNode.next_sibling()) {
			const std::string &monsterNodeName = as_lower_str(monsterNode.name());
			if (monsterNodeName != "monster") {
				continue;
			}

			const std::string &name = monsterNode.attribute("name").as_string();
			if (name.empty()) {
				wxString err;
				err << "Bad monster position data, discarding monster at spawn " << spawnPosition.x << ":" << spawnPosition.y << ":" << spawnPosition.z << " due missing name.";
				warnings.Add(err);
				break;
			}

			uint16_t spawntime = monsterNode.attribute("spawntime").as_uint(0);
			if (spawntime == 0) {
				spawntime = g_settings.getInteger(Config::DEFAULT_SPAWN_MONSTER_TIME);
			}

			auto weight = static_cast<uint8_t>(monsterNode.attribute("weight").as_uint());
			if (weight == 0) {
				weight = static_cast<uint8_t>(g_settings.getInteger(Config::MONSTER_DEFAULT_WEIGHT));
			}

			Direction direction = NORTH;
			int dir = monsterNode.attribute("direction").as_int(-1);
			if (dir >= DIRECTION_FIRST && dir <= DIRECTION_LAST) {
				direction = (Direction)dir;
			}

			Position monsterPosition(spawnPosition);

			pugi::xml_attribute xAttribute = monsterNode.attribute("x");
			pugi::xml_attribute yAttribute = monsterNode.attribute("y");
			if (!xAttribute || !yAttribute) {
				wxString err;
				err << "Bad monster position data, discarding monster \"" << name << "\" at spawn " << monsterPosition.x << ":" << monsterPosition.y << ":" << monsterPosition.z << " due to invalid position.";
				warnings.Add(err);
				break;
			}

			monsterPosition.x += xAttribute.as_int();
			monsterPosition.y += yAttribute.as_int();

			radius = std::max<int32_t>(radius, std::abs(monsterPosition.x - spawnPosition.x));
			radius = std::max<int32_t>(radius, std::abs(monsterPosition.y - spawnPosition.y));
			radius = std::min<int32_t>(radius, g_settings.getInteger(Config::MAX_SPAWN_MONSTER_RADIUS));

			Tile* monsterTile;
			if (monsterPosition == spawnPosition) {
				monsterTile = tile;
			} else {
				monsterTile = map.getTile(monsterPosition);
			}

			if (!monsterTile) {
				const auto error = fmt::format("Discarding monster \"{}\" at {}:{}:{} due to invalid position", name, monsterPosition.x, monsterPosition.y, monsterPosition.z);
				warnings.Add(error);
				break;
			}

			if (monsterTile->isMonsterRepeated(name)) {
				const auto error = fmt::format("Duplicate monster \"{}\" at {}:{}:{} was discarded.", name, monsterPosition.x, monsterPosition.y, monsterPosition.z);
				warnings.Add(error);
				break;
			}

			MonsterType* type = g_monsters[name];
			if (!type) {
				type = g_monsters.addMissingMonsterType(name);
			}

			Monster* monster = newd Monster(type);
			monster->setDirection(direction);
			monster->setSpawnMonsterTime(spawntime);
			monster->setWeight(weight);
			monsterTile->monsters.emplace_back(monster);

			if (monsterTile->getLocation()->getSpawnMonsterCount() == 0) {
				// No monster spawn, create a newd one
				ASSERT(monsterTile->spawnMonster == nullptr);
				SpawnMonster* spawnMonster = newd SpawnMonster(1);
				monsterTile->spawnMonster = spawnMonster;
				map.addSpawnMonster(monsterTile);
			}
		}
	}
	return true;
}

bool IOMapOTBM::loadHouses(Map &map, const FileName &dir) {
	std::string fn = (const char*)(dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME).mb_str(wxConvUTF8));
	fn += map.housefile;
	FileName filename(wxstr(fn));
	if (!filename.FileExists()) {
		return false;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(fn.c_str());
	if (!result) {
		return false;
	}
	return loadHouses(map, doc);
}

bool IOMapOTBM::loadHouses(Map &map, pugi::xml_document &doc) {
	pugi::xml_node node = doc.child("houses");
	if (!node) {
		warnings.push_back("IOMapOTBM::loadHouses: Invalid rootheader.");
		return false;
	}

	pugi::xml_attribute attribute;
	for (pugi::xml_node houseNode = node.first_child(); houseNode; houseNode = houseNode.next_sibling()) {
		if (as_lower_str(houseNode.name()) != "house") {
			continue;
		}

		House* house = nullptr;
		const auto houseIdAttribute = houseNode.attribute("houseid");
		if (!houseIdAttribute) {
			warnings.push_back(fmt::format("IOMapOTBM::loadHouses: Could not load house, missing 'houseid' attribute."));
			continue;
		}

		house = map.houses.getHouse(houseIdAttribute.as_uint());

		if (!house) {
			warnings.push_back(fmt::format("IOMapOTBM::loadHouses: Could not load house #{}", houseIdAttribute.as_uint()));
			continue;
		}

		if (house != nullptr) {
			if ((attribute = houseNode.attribute("name"))) {
				house->name = attribute.as_string();
			} else {
				house->name = "House #" + std::to_string(house->id);
			}
		}

		Position exitPosition(
			houseNode.attribute("entryx").as_int(),
			houseNode.attribute("entryy").as_int(),
			houseNode.attribute("entryz").as_int()
		);
		if (exitPosition.x != 0 && exitPosition.y != 0 && exitPosition.z != 0) {
			house->setExit(exitPosition);
		}

		if ((attribute = houseNode.attribute("rent"))) {
			house->rent = attribute.as_int();
		}

		if ((attribute = houseNode.attribute("guildhall"))) {
			house->guildhall = attribute.as_bool();
		}

		if ((attribute = houseNode.attribute("townid"))) {
			house->townid = attribute.as_uint();
		} else {
			warning("House %d has no town! House was removed.", house->id);
			map.houses.removeHouse(house);
		}

		if ((attribute = houseNode.attribute("clientid"))) {
			house->clientid = attribute.as_uint();
		}

		if ((attribute = houseNode.attribute("beds"))) {
			house->beds = attribute.as_uint();
		}
	}
	return true;
}

bool IOMapOTBM::loadZones(Map &map, const FileName &dir) {
	std::string fn = (const char*)(dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME).mb_str(wxConvUTF8));
	fn += map.zonefile;
	FileName filename(wxstr(fn));
	if (!filename.FileExists()) {
		return false;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(fn.c_str());
	if (!result) {
		return false;
	}
	return loadZones(map, doc);
}

bool IOMapOTBM::loadZones(Map &map, pugi::xml_document &doc) {
	pugi::xml_node node = doc.child("zones");
	if (!node) {
		warnings.push_back("IOMapOTBM::loadZones: Invalid rootheader.");
		return false;
	}

	pugi::xml_attribute attribute;
	for (pugi::xml_node zoneNode = node.first_child(); zoneNode; zoneNode = zoneNode.next_sibling()) {
		if (as_lower_str(zoneNode.name()) != "zone") {
			continue;
		}

		std::string name = zoneNode.attribute("name").as_string();
		unsigned int id = zoneNode.attribute("zoneid").as_uint();
		map.zones.addZone(name, id);
	}
	return true;
}

bool IOMapOTBM::loadSpawnsNpc(Map &map, const FileName &dir) {
	std::string fn = (const char*)(dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME).mb_str(wxConvUTF8));
	fn += map.spawnnpcfile;

	FileName filename(wxstr(fn));
	if (!filename.FileExists()) {
		return false;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(fn.c_str());
	if (!result) {
		return false;
	}
	return loadSpawnsNpc(map, doc);
}

bool IOMapOTBM::loadSpawnsNpc(Map &map, pugi::xml_document &doc) {
	pugi::xml_node node = doc.child("npcs");
	if (!node) {
		warnings.push_back("IOMapOTBM::loadSpawnsNpc: Invalid rootheader.");
		return false;
	}

	for (pugi::xml_node spawnNpcNode = node.first_child(); spawnNpcNode; spawnNpcNode = spawnNpcNode.next_sibling()) {
		if (as_lower_str(spawnNpcNode.name()) != "npc") {
			continue;
		}

		Position spawnPosition;
		spawnPosition.x = spawnNpcNode.attribute("centerx").as_int();
		spawnPosition.y = spawnNpcNode.attribute("centery").as_int();
		spawnPosition.z = spawnNpcNode.attribute("centerz").as_int();

		if (spawnPosition.x == 0 || spawnPosition.y == 0) {
			warning("Bad position data on one npc spawn, discarding...");
			continue;
		}

		int32_t radius = spawnNpcNode.attribute("radius").as_int();
		if (radius < 1) {
			warning("Couldn't read radius of npc spawn.. discarding spawn...");
			continue;
		}

		Tile* spawnTile = map.getTile(spawnPosition);
		if (spawnTile && spawnTile->spawnNpc) {
			warning("Duplicate npc spawn on position %d:%d:%d\n", spawnTile->getX(), spawnTile->getY(), spawnTile->getZ());
			continue;
		}

		SpawnNpc* spawnNpc = newd SpawnNpc(radius);
		if (!spawnTile) {
			spawnTile = map.allocator(map.createTileL(spawnPosition));
			map.setTile(spawnPosition, spawnTile);
		}

		spawnTile->spawnNpc = spawnNpc;
		map.addSpawnNpc(spawnTile);

		for (pugi::xml_node npcNode = spawnNpcNode.first_child(); npcNode; npcNode = npcNode.next_sibling()) {
			const std::string &npcNodeName = as_lower_str(npcNode.name());
			if (npcNodeName != "npc") {
				continue;
			}

			const std::string &name = npcNode.attribute("name").as_string();
			if (name.empty()) {
				wxString err;
				err << "Bad npc position data, discarding npc at spawn " << spawnPosition.x << ":" << spawnPosition.y << ":" << spawnPosition.z << " due missing name.";
				warnings.Add(err);
				break;
			}

			int32_t spawntime = npcNode.attribute("spawntime").as_int();
			if (spawntime == 0) {
				spawntime = g_settings.getInteger(Config::DEFAULT_SPAWN_NPC_TIME);
			}

			Direction direction = NORTH;
			int dir = npcNode.attribute("direction").as_int(-1);
			if (dir >= DIRECTION_FIRST && dir <= DIRECTION_LAST) {
				direction = (Direction)dir;
			}

			Position npcPosition(spawnPosition);

			pugi::xml_attribute xAttribute = npcNode.attribute("x");
			pugi::xml_attribute yAttribute = npcNode.attribute("y");
			if (!xAttribute || !yAttribute) {
				wxString err;
				err << "Bad npc position data, discarding npc \"" << name << "\" at spawn " << npcPosition.x << ":" << npcPosition.y << ":" << npcPosition.z << " due to invalid position.";
				warnings.Add(err);
				break;
			}

			npcPosition.x += xAttribute.as_int();
			npcPosition.y += yAttribute.as_int();

			radius = std::max<int32_t>(radius, std::abs(npcPosition.x - spawnPosition.x));
			radius = std::max<int32_t>(radius, std::abs(npcPosition.y - spawnPosition.y));
			radius = std::min<int32_t>(radius, g_settings.getInteger(Config::MAX_SPAWN_NPC_RADIUS));

			Tile* npcTile;
			if (npcPosition == spawnPosition) {
				npcTile = spawnTile;
			} else {
				npcTile = map.getTile(npcPosition);
			}

			if (!npcTile) {
				wxString err;
				err << "Discarding npc \"" << name << "\" at " << npcPosition.x << ":" << npcPosition.y << ":" << npcPosition.z << " due to invalid position.";
				warnings.Add(err);
				break;
			}

			if (npcTile->npc) {
				wxString err;
				err << "Duplicate npc \"" << name << "\" at " << npcPosition.x << ":" << npcPosition.y << ":" << npcPosition.z << " was discarded.";
				warnings.Add(err);
				break;
			}

			NpcType* type = g_npcs[name];
			if (!type) {
				type = g_npcs.addMissingNpcType(name);
			}

			Npc* npc = newd Npc(type);
			npc->setDirection(direction);
			npc->setSpawnNpcTime(spawntime);
			npcTile->npc = npc;

			if (npcTile->getLocation()->getSpawnNpcCount() == 0) {
				// No npc spawn, create a newd one
				ASSERT(npcTile->spawnNpc == nullptr);
				SpawnNpc* spawnNpc = newd SpawnNpc(1);
				npcTile->spawnNpc = spawnNpc;
				map.addSpawnNpc(npcTile);
			}
		}
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

bool IOMapOTBM::saveSpawns(Map &map, const FileName &dir) {
	wxString filepath = dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME);
	filepath += wxString(map.spawnmonsterfile.c_str(), wxConvUTF8);

	// Create the XML file
	pugi::xml_document doc;
	if (saveSpawns(map, doc)) {
		return doc.save_file(filepath.wc_str(), "\t", pugi::format_default, pugi::encoding_utf8);
	}
	return false;
}

bool IOMapOTBM::saveSpawns(Map &map, pugi::xml_document &doc) {
	pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
	if (!decl) {
		return false;
	}

	decl.append_attribute("version") = "1.0";

	MonsterList monsterList;

	pugi::xml_node spawnNodes = doc.append_child("monsters");
	for (const auto &spawnPosition : map.spawnsMonster) {
		Tile* tile = map.getTile(spawnPosition);
		if (tile == nullptr) {
			continue;
		}

		SpawnMonster* spawnMonster = tile->spawnMonster;
		ASSERT(spawnMonster);

		pugi::xml_node spawnNode = spawnNodes.append_child("monster");
		spawnNode.append_attribute("centerx") = spawnPosition.x;
		spawnNode.append_attribute("centery") = spawnPosition.y;
		spawnNode.append_attribute("centerz") = spawnPosition.z;

		int32_t radius = spawnMonster->getSize();
		spawnNode.append_attribute("radius") = radius;

		for (auto y = -radius; y <= radius; ++y) {
			for (auto x = -radius; x <= radius; ++x) {
				const auto monsterTile = map.getTile(spawnPosition + Position(x, y, 0));
				if (monsterTile) {
					for (const auto monster : monsterTile->monsters) {
						if (monster && !monster->isSaved()) {
							pugi::xml_node monsterNode = spawnNode.append_child("monster");
							monsterNode.append_attribute("name") = monster->getName().c_str();
							monsterNode.append_attribute("x") = x;
							monsterNode.append_attribute("y") = y;
							monsterNode.append_attribute("z") = spawnPosition.z;
							monsterNode.append_attribute("spawntime") = monster->getSpawnMonsterTime();
							if (monster->getDirection() != NORTH) {
								monsterNode.append_attribute("direction") = monster->getDirection();
							}

							if (monsterTile->monsters.size() > 1) {
								const auto weight = monster->getWeight();
								monsterNode.append_attribute("weight") = weight > 0 ? weight : g_settings.getInteger(Config::MONSTER_DEFAULT_WEIGHT);
							}

							// Mark as saved
							monster->save();
							monsterList.push_back(monster);
						}
					}
				}
			}
		}
	}

	for (Monster* monster : monsterList) {
		monster->reset();
	}
	return true;
}

bool IOMapOTBM::saveHouses(Map &map, const FileName &dir) {
	wxString filepath = dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME);
	filepath += wxString(map.housefile.c_str(), wxConvUTF8);

	// Create the XML file
	pugi::xml_document doc;
	if (saveHouses(map, doc)) {
		return doc.save_file(filepath.wc_str(), "\t", pugi::format_default, pugi::encoding_utf8);
	}
	return false;
}

bool IOMapOTBM::saveHouses(Map &map, pugi::xml_document &doc) {
	pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
	if (!decl) {
		return false;
	}

	decl.append_attribute("version") = "1.0";

	pugi::xml_node houseNodes = doc.append_child("houses");
	for (const auto &houseEntry : map.houses) {
		const House* house = houseEntry.second;
		pugi::xml_node houseNode = houseNodes.append_child("house");

		houseNode.append_attribute("name") = house->name.c_str();
		houseNode.append_attribute("houseid") = house->id;

		const Position &exitPosition = house->getExit();
		houseNode.append_attribute("entryx") = exitPosition.x;
		houseNode.append_attribute("entryy") = exitPosition.y;
		houseNode.append_attribute("entryz") = exitPosition.z;

		houseNode.append_attribute("rent") = house->rent;
		if (house->guildhall) {
			houseNode.append_attribute("guildhall") = true;
		}

		houseNode.append_attribute("townid") = house->townid;
		houseNode.append_attribute("size") = static_cast<int32_t>(house->size());
		houseNode.append_attribute("clientid") = house->clientid;
		houseNode.append_attribute("beds") = house->beds;
	}
	return true;
}

bool IOMapOTBM::saveZones(Map &map, const FileName &dir) {
	wxString filepath = dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME);
	filepath += wxString(map.zonefile.c_str(), wxConvUTF8);

	// Create the XML file
	pugi::xml_document doc;
	if (saveZones(map, doc)) {
		return doc.save_file(filepath.wc_str(), "\t", pugi::format_default, pugi::encoding_utf8);
	}
	return false;
}

bool IOMapOTBM::saveZones(Map &map, pugi::xml_document &doc) {
	pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
	if (!decl) {
		return false;
	}

	decl.append_attribute("version") = "1.0";

	pugi::xml_node zoneNodes = doc.append_child("zones");
	for (const auto &[name, id] : map.zones) {
		if (id <= 0) {
			continue;
		}
		pugi::xml_node zoneNode = zoneNodes.append_child("zone");

		zoneNode.append_attribute("name") = name.c_str();
		zoneNode.append_attribute("zoneid") = id;
	}
	return true;
}

bool IOMapOTBM::saveSpawnsNpc(Map &map, const FileName &dir) {
	wxString filepath = dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME);
	filepath += wxString(map.spawnnpcfile.c_str(), wxConvUTF8);

	// Create the XML file
	pugi::xml_document doc;
	if (saveSpawnsNpc(map, doc)) {
		return doc.save_file(filepath.wc_str(), "\t", pugi::format_default, pugi::encoding_utf8);
	}
	return false;
}

bool IOMapOTBM::saveSpawnsNpc(Map &map, pugi::xml_document &doc) {
	pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
	if (!decl) {
		return false;
	}

	decl.append_attribute("version") = "1.0";

	NpcList npcList;

	pugi::xml_node spawnNodes = doc.append_child("npcs");
	for (const auto &spawnPosition : map.spawnsNpc) {
		Tile* tile = map.getTile(spawnPosition);
		if (tile == nullptr) {
			continue;
		}

		SpawnNpc* spawnNpc = tile->spawnNpc;
		ASSERT(spawnNpc);

		pugi::xml_node spawnNpcNode = spawnNodes.append_child("npc");
		spawnNpcNode.append_attribute("centerx") = spawnPosition.x;
		spawnNpcNode.append_attribute("centery") = spawnPosition.y;
		spawnNpcNode.append_attribute("centerz") = spawnPosition.z;

		int32_t radius = spawnNpc->getSize();
		spawnNpcNode.append_attribute("radius") = radius;

		for (int32_t y = -radius; y <= radius; ++y) {
			for (int32_t x = -radius; x <= radius; ++x) {
				Tile* npcTile = map.getTile(spawnPosition + Position(x, y, 0));
				if (npcTile) {
					Npc* npc = npcTile->npc;
					if (npc && !npc->isSaved()) {
						pugi::xml_node npcNode = spawnNpcNode.append_child("npc");
						npcNode.append_attribute("name") = npc->getName().c_str();
						npcNode.append_attribute("x") = x;
						npcNode.append_attribute("y") = y;
						npcNode.append_attribute("z") = spawnPosition.z;
						npcNode.append_attribute("spawntime") = npc->getSpawnNpcTime();
						if (npc->getDirection() != NORTH) {
							npcNode.append_attribute("direction") = npc->getDirection();
						}

						// Mark as saved
						npc->save();
						npcList.push_back(npc);
					}
				}
			}
		}
	}

	for (Npc* npc : npcList) {
		npc->reset();
	}
	return true;
}
