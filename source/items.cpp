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

#include "materials.h"
#include "gui.h"

#include "items.h"
#include "item.h"
#include "sprite_appearances.h"

#include <appearances.pb.h>

ItemDatabase g_items;

bool ItemType::isFloorChange() const noexcept {
	return floorChange
		|| floorChangeDown
		|| floorChangeNorth
		|| floorChangeSouth
		|| floorChangeEast
		|| floorChangeWest;
}

ItemDatabase::~ItemDatabase() {
	clear();
}

ItemType &ItemDatabase::operator[](uint16_t id) {
	return getItemType(id);
}

void ItemDatabase::clear() {
	for (size_t i = 0; i < items.size(); i++) {
		items[i].reset();
		items.set(i, nullptr);
	}
}

#if CLIENT_VERSION < 1100
bool ItemDatabase::loadGroupByOtbVersion(const std::shared_ptr<ItemType> &item, wxArrayString &warnings) const {
	switch (item->group) {
		case ITEM_GROUP_NONE:
		case ITEM_GROUP_GROUND:
		case ITEM_GROUP_SPLASH:
		case ITEM_GROUP_FLUID:
		case ITEM_GROUP_WEAPON:
		case ITEM_GROUP_AMMUNITION:
		case ITEM_GROUP_ARMOR:
		case ITEM_GROUP_WRITEABLE:
		case ITEM_GROUP_KEY:
			break;
		case ITEM_GROUP_DOOR:
			if (MajorVersion < 3) {
				item->type = ITEM_TYPE_DOOR;
			}
			break;
		case ITEM_GROUP_CONTAINER:
			item->type = ITEM_TYPE_CONTAINER;
			break;
		case ITEM_GROUP_RUNE:
			if (MajorVersion < 3) {
				item->client_chargeable = true;
			}
			break;
		case ITEM_GROUP_TELEPORT:
			if (MajorVersion < 3) {
				item->type = ITEM_TYPE_TELEPORT;
			}
			break;
		case ITEM_GROUP_MAGICFIELD:
			if (MajorVersion < 3) {
				item->type = ITEM_TYPE_MAGICFIELD;
			}
			break;
		default:
			warnings.push_back("Unknown item group declaration");
			break;
	}

	return true;
}

bool ItemDatabase::loadFlagsByOtbVersion(const std::shared_ptr<ItemType> &item, BinaryNode* itemNode) const {
	uint32_t flags;
	if (itemNode->getU32(flags)) {
		item->unpassable = ((flags & FLAG_UNPASSABLE) == FLAG_UNPASSABLE);
		item->blockMissiles = ((flags & FLAG_BLOCK_MISSILES) == FLAG_BLOCK_MISSILES);
		item->blockPathfinder = ((flags & FLAG_BLOCK_PATHFINDER) == FLAG_BLOCK_PATHFINDER);
		item->hasElevation = ((flags & FLAG_HAS_ELEVATION) == FLAG_HAS_ELEVATION);
		// t->useable = ((flags & FLAG_USEABLE) == FLAG_USEABLE);
		item->pickupable = ((flags & FLAG_PICKUPABLE) == FLAG_PICKUPABLE);
		item->moveable = ((flags & FLAG_MOVEABLE) == FLAG_MOVEABLE);
		item->stackable = ((flags & FLAG_STACKABLE) == FLAG_STACKABLE);
		item->floorChangeDown = ((flags & FLAG_FLOORCHANGEDOWN) == FLAG_FLOORCHANGEDOWN);
		item->floorChangeNorth = ((flags & FLAG_FLOORCHANGENORTH) == FLAG_FLOORCHANGENORTH);
		item->floorChangeEast = ((flags & FLAG_FLOORCHANGEEAST) == FLAG_FLOORCHANGEEAST);
		item->floorChangeSouth = ((flags & FLAG_FLOORCHANGESOUTH) == FLAG_FLOORCHANGESOUTH);
		item->floorChangeWest = ((flags & FLAG_FLOORCHANGEWEST) == FLAG_FLOORCHANGEWEST);
		item->floorChange = item->floorChangeDown || item->floorChangeNorth || item->floorChangeEast || item->floorChangeSouth || item->floorChangeWest;
		// Now this is confusing, just accept that the ALWAYSONTOP flag means it's always on bottom, got it?!
		item->alwaysOnBottom = ((flags & FLAG_ALWAYSONTOP) == FLAG_ALWAYSONTOP);
		item->isHangable = ((flags & FLAG_HANGABLE) == FLAG_HANGABLE);
		item->hookEast = ((flags & FLAG_HOOK_EAST) == FLAG_HOOK_EAST);
		item->hookSouth = ((flags & FLAG_HOOK_SOUTH) == FLAG_HOOK_SOUTH);
		item->allowDistRead = ((flags & FLAG_ALLOWDISTREAD) == FLAG_ALLOWDISTREAD);
		item->rotable = ((flags & FLAG_ROTABLE) == FLAG_ROTABLE);
		item->canReadText = ((flags & FLAG_READABLE) == FLAG_READABLE);
		if (MajorVersion == 3) {
			item->client_chargeable = ((flags & FLAG_CLIENTCHARGES) == FLAG_CLIENTCHARGES);
			item->ignoreLook = ((flags & FLAG_IGNORE_LOOK) == FLAG_IGNORE_LOOK);
		}
	}

	return true;
}

bool ItemDatabase::handleAttributes(const std::shared_ptr<ItemType> &item, uint8_t &attribute, uint16_t &datalen, BinaryNode* itemNode, wxString &error, wxArrayString &warnings) {
	switch (attribute) {
		case ITEM_ATTR_SERVERID: {
			if (datalen != sizeof(uint16_t)) {
				error = "items.otb: Unexpected data length of server id block (Should be 2 bytes)";
				return false;
			}
			if (!itemNode->getU16(item->id)) {
				warnings.push_back("Invalid item type property (2)");
			}

			if (maxItemId < item->id) {
				maxItemId = item->id;
			}
			break;
		}

		case ITEM_ATTR_CLIENTID: {
			if (datalen != sizeof(uint16_t)) {
				error = "items.otb: Unexpected data length of client id block (Should be 2 bytes)";
				return false;
			}

			if (!itemNode->getU16(item->clientID)) {
				warnings.push_back("Invalid item type property (2)");
			}

			item->sprite = static_cast<GameSprite*>(g_gui.gfx.getSprite(item->clientID));
			break;
		}

		case ITEM_ATTR_SPEED: {
			if (datalen != sizeof(uint16_t)) {
				error = "items.otb: Unexpected data length of speed block (Should be 2 bytes)";
				return false;
			}

			// t->speed = itemNode->getU16();
			if (!itemNode->skip(2)) { // Just skip two bytes, we don't need speed
				warnings.push_back("Invalid item type property (3)");
			}
			break;
		}

		case ITEM_ATTR_LIGHT2: {
			if (datalen != sizeof(lightBlock2)) {
				warnings.push_back(wxString::Format("items.otb: Unexpected data length of item light (2) block (Should be %d bytes)", sizeof(lightBlock2)));
				break;
			}

			if (!itemNode->skip(4)) { // Just skip two bytes, we don't need light
				warnings.push_back("Invalid item type property (4)");
			}

			// t->lightLevel = node->getU16();
			// t->lightColor = node->getU16();
			break;
		}

		case ITEM_ATTR_TOPORDER: {
			if (datalen != sizeof(uint8_t)) {
				warnings.push_back("items.otb: Unexpected data length of item toporder block (Should be 1 byte)");
				break;
			}

			uint8_t value = 0;
			if (!itemNode->getU8(value)) {
				warnings.push_back("Invalid item type property (5)");
			}

			item->alwaysOnTopOrder = value;
			break;
		}

		case ITEM_ATTR_NAME: {
			if (MajorVersion != 1) {
				break;
			}

			if (datalen >= 128) {
				warnings.push_back("items.otb: Unexpected data length of item name block (Should be 128 bytes)");
				break;
			}

			uint8_t name[128];
			memset(&name, 0, 128);

			if (!itemNode->getRAW(name, datalen)) {
				warnings.push_back("Invalid item type property (6)");
				break;
			}
			item->name = (char*)name;
			break;
		}

		case ITEM_ATTR_DESCR: {
			if (MajorVersion != 1) {
				break;
			}

			if (datalen >= 128) {
				warnings.push_back("items.otb: Unexpected data length of item descr block (Should be 128 bytes)");
				break;
			}

			uint8_t description[128];
			memset(&description, 0, 128);

			if (!itemNode->getRAW(description, datalen)) {
				warnings.push_back("Invalid item type property (7)");
				break;
			}

			item->description = (char*)description;
			break;
		}

		case ITEM_ATTR_MAXITEMS: {
			if (MajorVersion != 1) {
				break;
			}

			if (datalen != sizeof(unsigned short)) {
				warnings.push_back("items.otb: Unexpected data length of item volume block (Should be 2 bytes)");
				break;
			}

			if (!itemNode->getU16(item->volume)) {
				warnings.push_back("Invalid item type property (8)");
			}
			break;
		}

		case ITEM_ATTR_WEIGHT: {
			if (MajorVersion != 1) {
				break;
			}

			if (datalen != sizeof(double)) {
				warnings.push_back("items.otb: Unexpected data length of item weight block (Should be 8 bytes)");
				break;
			}

			uint8_t weight[sizeof(double)];
			if (!itemNode->getRAW(weight, sizeof(double))) {
				warnings.push_back("Invalid item type property (7)");
				break;
			}

			double actualWeight;
			memcpy(&actualWeight, weight, sizeof(actualWeight));

			item->weight = actualWeight;
			break;
		}

		case ITEM_ATTR_ROTATETO: {
			if (MajorVersion != 1) {
				break;
			}

			if (datalen != sizeof(unsigned short)) {
				warnings.push_back("items.otb: Unexpected data length of item rotateTo block (Should be 2 bytes)");
				break;
			}

			uint16_t rotate;
			if (!itemNode->getU16(rotate)) {
				warnings.push_back("Invalid item type property (8)");
				break;
			}

			item->rotateTo = rotate;
			break;
		}

		case ITEM_ATTR_WRITEABLE3: {
			if (MajorVersion != 1) {
				break;
			}

			if (datalen != sizeof(writeableBlock3)) {
				warnings.push_back("items.otb: Unexpected data length of item toporder block (Should be 1 byte)");
				break;
			}

			uint16_t readOnlyID;
			uint16_t maxTextLen;

			if (!itemNode->getU16(readOnlyID)) {
				warnings.push_back("Invalid item type property (9)");
				break;
			}

			if (!itemNode->getU16(maxTextLen)) {
				warnings.push_back("Invalid item type property (10)");
				break;
			}

			// t->readOnlyId = wb3->readOnlyId;
			item->maxTextLen = maxTextLen;
			break;
		}

		default: {
			// skip unknown attributes
			itemNode->skip(datalen);
			// warnings.push_back("items.otb: Skipped unknown attribute");
			break;
		}
	}

	return true;
}

bool ItemDatabase::loadAttributesByOtbVersion(const std::shared_ptr<ItemType> &item, BinaryNode* itemNode, wxString &error, wxArrayString &warnings) {
	uint8_t attribute;
	while (itemNode->getU8(attribute)) {
		uint16_t datalen;
		if (!itemNode->getU16(datalen)) {
			warnings.push_back("Invalid item type property");
			break;
		}

		if (!handleAttributes(item, attribute, datalen, itemNode, error, warnings)) {
			return false;
		}
	}

	return true;
}

bool ItemDatabase::loadFromOtb(const FileName &datafile, wxString &error, wxArrayString &warnings) {
	auto filename = nstr((datafile.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR) + datafile.GetFullName()));
	DiskNodeFileReadHandle f(filename, StringVector(1, "OTBI"));

	if (!f.isOk()) {
		error = wxString::Format("Couldn't open file \"%s\":%s", filename, f.getErrorMessage());
		return false;
	}

	auto root = f.getRootNode();

	#define safe_get(node, func, ...)               \
		do {                                        \
			if (!node->get##func(__VA_ARGS__)) {    \
				error = wxstr(f.getErrorMessage()); \
				return false;                       \
			}                                       \
		} while (false)

	// Read root flags
	root->skip(1); // Type info
	// uint32_t flags =

	root->skip(4); // Unused?

	uint8_t attr;
	safe_get(root, U8, attr);
	if (attr == ROOT_ATTR_VERSION) {
		uint16_t datalen;
		if (!root->getU16(datalen) || datalen != 4 + 4 + 4 + 1 * 128) {
			error = "items.otb: Size of version header is invalid, updated .otb version?";
			return false;
		}
		safe_get(root, U32, MajorVersion); // items otb format file version
		safe_get(root, U32, MinorVersion); // client version
		safe_get(root, U32, BuildNumber); // revision
		std::string csd;
		csd.resize(128);

		if (!root->getRAW((uint8_t*)csd.data(), 128)) { // CSDVersion ??
			error = wxstr(f.getErrorMessage());
			return false;
		}
	} else {
		error = "Expected ROOT_ATTR_VERSION as first node of items.otb!";
	}

	if (g_settings.getInteger(Config::CHECK_SIGNATURES)) {
		if (g_gui.GetCurrentVersion().getOTBVersion().format_version != MajorVersion) {
			error = wxString::Format("Unsupported items.otb version (version %d)", MajorVersion);
			return false;
		}
	}

	uint8_t group;
	for (auto itemNode = root->getChild(); itemNode != nullptr; itemNode = itemNode->advance()) {
		if (!itemNode->getU8(group)) {
			// Invalid!
			warnings.push_back("Invalid item type encountered...");
			continue;
		}

		if (group == ITEM_GROUP_DEPRECATED) {
			continue;
		}

		auto item = std::make_shared<ItemType>();
		item->group = static_cast<ItemGroup_t>(group);

		if (!loadGroupByOtbVersion(item, warnings)) {
			return false;
		}

		if (!loadFlagsByOtbVersion(item, itemNode)) {
			return false;
		}

		if (!loadAttributesByOtbVersion(item, itemNode, error, warnings)) {
			return false;
		}

		if (items[item->id]) {
			warnings.push_back("items.otb: Duplicate items");
			items[item->id].reset();
		}
		items.set(item->id, item);
	}
	return true;
}
#endif

bool ItemDatabase::loadFromProtobuf(wxString &error, wxArrayString &warnings, canary::protobuf::appearances::Appearances &appearances) {
	using namespace canary::protobuf::appearances;

	for (uint16_t it = 0; it < static_cast<uint16_t>(appearances.object_size()); ++it) {
		Appearance object = appearances.object(it);

		// This scenario should never happen but on custom assets this can break the loader.
		if (!object.has_flags()) {
			spdlog::error("[ItemDatabase::loadFromProtobuf] - Item with id {} is invalid and was ignored.", object.id());
			wxLogError("[ItemDatabase::loadFromProtobuf] - Item with id %i is invalid and was ignored.", object.id());
			continue;
		}

		if (object.id() >= items.size()) {
			items.resize(object.id() + 1);
		}

		if (!object.has_id()) {
			continue;
		}

		auto t = std::make_shared<ItemType>();
		t->id = static_cast<uint16_t>(object.id());
		// Save max item id from the object size iteraction
		if (maxItemId < t->id) {
			maxItemId = t->id;
			spdlog::debug("[ItemDatabase::loadFromProtobuf] - Loading item with id {}.", t->id);
		}
		t->clientID = static_cast<uint16_t>(object.id());
		t->name = object.name();
		t->description = object.description();

		if (object.flags().container()) {
			t->type = ITEM_TYPE_CONTAINER;
			t->group = ITEM_GROUP_CONTAINER;
		} else if (object.flags().has_bank()) {
			t->group = ITEM_GROUP_GROUND;
		} else if (object.flags().liquidcontainer()) {
			t->group = ITEM_GROUP_FLUID;
		} else if (object.flags().liquidpool()) {
			t->group = ITEM_GROUP_SPLASH;
		}

		if (object.flags().has_clip() || object.flags().has_top() || object.flags().has_bottom()) {
			t->alwaysOnBottom = true;
		}

		if (object.flags().clip()) {
			t->alwaysOnTopOrder = 1;
		} else if (object.flags().top()) {
			t->alwaysOnTopOrder = 3;
		} else if (object.flags().bottom()) {
			t->alwaysOnTopOrder = 2;
		}

		// now lets parse sprite data
		t->m_animationPhases.clear();

		for (const auto &framegroup : object.frame_group()) {
			const auto &frameGroupType = framegroup.fixed_frame_group();
			const auto &spriteInfo = framegroup.sprite_info();
			const auto &animation = spriteInfo.animation();
			const auto &sprites = spriteInfo.sprite_id();

			t->pattern_width = spriteInfo.pattern_width();
			t->pattern_height = spriteInfo.pattern_height();
			t->pattern_depth = spriteInfo.pattern_depth();
			t->layers = spriteInfo.layers();

			if (animation.sprite_phase().size() > 0) {
				const auto &spritesPhases = animation.sprite_phase();
				t->start_frame = animation.default_start_phase();
				t->loop_count = animation.loop_count();
				t->async_animation = !animation.synchronized();
				for (int k = 0; k < spritesPhases.size(); k++) {
					t->m_animationPhases.push_back(std::pair<int, int>(static_cast<int>(spritesPhases[k].duration_min()), static_cast<int>(spritesPhases[k].duration_max())));
				}
			}

			t->sprite_id = spriteInfo.sprite_id(0);

			t->m_sprites.clear();
			t->m_sprites.resize(sprites.size());
			for (int i = 0; i < sprites.size(); i++) {
				t->m_sprites[i] = sprites[i];
			}
		}

		t->noMoveAnimation = object.flags().no_movement_animation();
		t->isCorpse = object.flags().corpse() || object.flags().player_corpse();
		t->forceUse = object.flags().forceuse();
		t->hasHeight = object.flags().has_height();
		t->unpassable = object.flags().unpass();
		t->blockMissiles = object.flags().unsight();
		t->blockPathfinder = object.flags().avoid();
		t->pickupable = object.flags().take();
		t->moveable = object.flags().unmove() == false;
		t->canReadText = (object.flags().has_lenshelp() && object.flags().lenshelp().id() == 1112) || (object.flags().has_write() && object.flags().write().max_text_length() != 0) || (object.flags().has_write_once() && object.flags().write_once().max_text_length_once() != 0);
		t->canReadText = object.flags().has_write() || object.flags().has_write_once();
		t->isHangable = object.flags().hang();
		t->stackable = object.flags().cumulative();
		t->isPodium = object.flags().show_off_socket();
		t->rotable = object.flags().rotate();
		t->ignoreLook = object.flags().ignore_look();
		t->hasElevation = object.flags().has_height();

		if (object.flags().has_hook()) {
			t->hook = object.flags().hook().direction() == HOOK_TYPE_SOUTH ? ITEM_HOOK_SOUTH : ITEM_HOOK_EAST;
		}

		g_gui.gfx.loadItemSpriteMetadata(t, error, warnings);
		t->sprite = static_cast<GameSprite*>(g_gui.gfx.getSprite(t->id));
		if (t->sprite) {
			t->sprite->minimap_color = object.flags().has_automap() ? static_cast<uint16_t>(object.flags().automap().color()) : 0;
			t->sprite->draw_height = object.flags().has_height() ? static_cast<uint16_t>(object.flags().height().elevation()) : 0;
			if (object.flags().has_shift()) {
				t->sprite->draw_offset = wxPoint(object.flags().shift().x(), object.flags().shift().y());
			}

			if (object.flags().has_light()) {
				t->sprite->light.color = object.flags().light().color();
				t->sprite->light.intensity = object.flags().light().brightness();
				t->sprite->has_light = true;
			}
		}

		if (t) {
			if (items[t->id]) {
				wxLogWarning("appearances.dat: Duplicate items");
				items[t->id].reset();
			}
			items.set(t->id, t);
		}
	}

	spdlog::debug("[ItemDatabase::loadFromProtobuf] - Last loaded item: {}", maxItemId);
	return true;
}

bool ItemDatabase::loadItemFromGameXml(pugi::xml_node itemNode, uint16_t id) {
	if (!(id >= LIQUID_FIRST && id <= LIQUID_LAST) && !isValidID(id)) {
		return false;
	}

	auto &item = getItemType(id);
	item.name = itemNode.attribute("name").as_string();
	item.editorsuffix = itemNode.attribute("editorsuffix").as_string();

	pugi::xml_attribute attribute;
	for (auto itemAttributesNode = itemNode.first_child(); itemAttributesNode; itemAttributesNode = itemAttributesNode.next_sibling()) {
		if (!(attribute = itemAttributesNode.attribute("key"))) {
			continue;
		}

		std::string key = attribute.as_string();
		to_lower_str(key);
		if (key == "type") {
			if (!(attribute = itemAttributesNode.attribute("value"))) {
				continue;
			}

			std::string typeValue = attribute.as_string();
			to_lower_str(key);
			if (typeValue == "depot") {
				item.type = ITEM_TYPE_DEPOT;
			} else if (typeValue == "mailbox") {
				item.type = ITEM_TYPE_MAILBOX;
			} else if (typeValue == "trashholder") {
				item.type = ITEM_TYPE_TRASHHOLDER;
			} else if (typeValue == "container") {
				item.type = ITEM_TYPE_CONTAINER;
			} else if (typeValue == "door") {
				item.type = ITEM_TYPE_DOOR;
			} else if (typeValue == "magicfield") {
				item.group = ITEM_GROUP_MAGICFIELD;
				item.type = ITEM_TYPE_MAGICFIELD;
			} else if (typeValue == "teleport") {
				item.type = ITEM_TYPE_TELEPORT;
			} else if (typeValue == "bed") {
				item.type = ITEM_TYPE_BED;
			} else if (typeValue == "key") {
				item.type = ITEM_TYPE_KEY;
			}
		} else if (key == "name") {
			if ((attribute = itemAttributesNode.attribute("value"))) {
				item.name = attribute.as_string();
			}
		} else if (key == "description") {
			if ((attribute = itemAttributesNode.attribute("value"))) {
				item.description = attribute.as_string();
			}
		} else if (key == "runespellName") {
			/*if((attribute = itemAttributesNode.attribute("value"))) {
				it.runeSpellName = attribute.as_string();
			}*/
		} else if (key == "weight") {
			if ((attribute = itemAttributesNode.attribute("value"))) {
				item.weight = attribute.as_int() / 100.f;
			}
		} else if (key == "armor") {
			if ((attribute = itemAttributesNode.attribute("value"))) {
				item.armor = attribute.as_int();
			}
		} else if (key == "defense") {
			if ((attribute = itemAttributesNode.attribute("value"))) {
				item.defense = attribute.as_int();
			}
		} else if (key == "rotateto") {
			if ((attribute = itemAttributesNode.attribute("value"))) {
				item.rotateTo = attribute.as_uint();
			}
		} else if (key == "containersize") {
			if ((attribute = itemAttributesNode.attribute("value"))) {
				item.volume = attribute.as_uint();
			}
		} else if (key == "readable") {
			if ((attribute = itemAttributesNode.attribute("value"))) {
				item.canReadText = attribute.as_bool();
			}
		} else if (key == "writeable") {
			if ((attribute = itemAttributesNode.attribute("value"))) {
				item.canWriteText = item.canReadText = attribute.as_bool();
			}
		} else if (key == "decayto") {
			item.decays = true;
		} else if (key == "maxtextlen" || key == "maxtextlength") {
			if ((attribute = itemAttributesNode.attribute("value"))) {
				item.maxTextLen = attribute.as_uint();
				item.canReadText = item.maxTextLen > 0;
			}
		} else if (key == "writeonceitemid") {
			/*if((attribute = itemAttributesNode.attribute("value"))) {
				it.writeOnceItemId = pugi::cast<int32_t>(attribute.value());
			}*/
		} else if (key == "allowdistread") {
			if ((attribute = itemAttributesNode.attribute("value"))) {
				item.allowDistRead = attribute.as_bool();
			}
		} else if (key == "charges") {
			if ((attribute = itemAttributesNode.attribute("value"))) {
				item.charges = attribute.as_uint();
				item.extra_chargeable = true;
			}
		} else if (key == "floorchange") {
			if ((attribute = itemAttributesNode.attribute("value"))) {
				std::string value = attribute.as_string();
				if (value == "down") {
					item.floorChangeDown = true;
					item.floorChange = true;
				} else if (value == "north") {
					item.floorChangeNorth = true;
					item.floorChange = true;
				} else if (value == "south") {
					item.floorChangeSouth = true;
					item.floorChange = true;
				} else if (value == "west") {
					item.floorChangeWest = true;
					item.floorChange = true;
				} else if (value == "east") {
					item.floorChangeEast = true;
					item.floorChange = true;
				} else if (value == "northex") {
					item.floorChange = true;
				} else if (value == "southex") {
					item.floorChange = true;
				} else if (value == "westex") {
					item.floorChange = true;
				} else if (value == "eastex") {
					item.floorChange = true;
				} else if (value == "southalt") {
					item.floorChange = true;
				} else if (value == "eastalt") {
					item.floorChange = true;
				}
			}
		}
	}
	return true;
}

bool ItemDatabase::loadFromGameXml(const FileName &identifier, wxString &error, wxArrayString &warnings) {
	pugi::xml_document doc;
	const auto result = doc.load_file(identifier.GetFullPath().mb_str());
	if (!result) {
		error = "Could not load items.xml (Syntax error?)";
		return false;
	}

	const auto node = doc.child("items");
	if (!node) {
		error = "items.xml, invalid root node.";
		return false;
	}

	for (auto itemNode = node.first_child(); itemNode; itemNode = itemNode.next_sibling()) {
		if (as_lower_str(itemNode.name()) != "item") {
			continue;
		}

		uint16_t fromId = 0;
		uint16_t toId = 0;
		if (const auto attribute = itemNode.attribute("id")) {
			fromId = toId = attribute.as_uint();
		} else {
			fromId = itemNode.attribute("fromid").as_uint();
			toId = itemNode.attribute("toid").as_uint();
		}

		if (fromId == 0 || toId == 0) {
			error = wxString::Format("Could not read item id from item node, fromid %d, toid %d.", fromId, toId);
			return false;
		}

		for (auto id = fromId; id <= toId; ++id) {
			if (!loadItemFromGameXml(itemNode, id)) {
				error = wxString::Format("Could not load item id %d. Item id not found.", id);
				return false;
			}
		}
	}

	return true;
}

bool ItemDatabase::loadMetaItem(pugi::xml_node node) {
	if (const pugi::xml_attribute attribute = node.attribute("id")) {
		const uint16_t id = attribute.as_uint();
		if (id == 0 || items[id]) {
			return false;
		}

		auto item = std::make_shared<ItemType>();
		item->is_metaitem = true;
		item->id = id;
		items.set(id, item);
		return true;
	}
	return false;
}

ItemType &ItemDatabase::getItemType(uint16_t id) {
	if (id == 0 || id > maxItemId) {
		return dummy;
	}

	auto type = items[id];
	if (type) {
		return *type;
	}

	return dummy;
}

std::shared_ptr<ItemType> ItemDatabase::getRawItemType(uint16_t id) {
	if (id == 0 || id > maxItemId) {
		return nullptr;
	}
	return items[id];
}

bool ItemDatabase::isValidID(uint16_t id) const {
	if (id == 0 || id > maxItemId) {
		return false;
	}
	return items[id] != nullptr;
}
