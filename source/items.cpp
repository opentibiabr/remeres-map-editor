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
#include <string.h> // memcpy
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "items.h"
#include "item.h"
#include "sprite_appearances.h"

ItemDatabase g_items;

ItemType::ItemType() :
	sprite(nullptr),
	id(0),
	clientID(0),
	brush(nullptr),
	doodad_brush(nullptr),
	raw_brush(nullptr),
	is_metaitem(false),
	has_raw(false),
	in_other_tileset(false),
	group(ITEM_GROUP_NONE),
	type(ITEM_TYPE_NONE),
	volume(0),
	maxTextLen(0),
	//writeOnceItemID(0),
	ground_equivalent(0),
	border_group(0),
	has_equivalent(false),
	wall_hate_me(false),
	name(""),
	description(""),
	weight(0.0f),
	attack(0),
	defense(0),
	armor(0),
	charges(0),
	client_chargeable(false),
	extra_chargeable(false),
	ignoreLook(false),

	isHangable(false),
	hookEast(false),
	hookSouth(false),
	canReadText(false),
	canWriteText(false),
	replaceable(true),
	decays(false),
	stackable(false),
	moveable(true),
	alwaysOnBottom(false),
	pickupable(false),
	rotable(false),
	isBorder(false),
	isOptionalBorder(false),
	isWall(false),
	isBrushDoor(false),
	isOpen(false),
	isTable(false),
	isCarpet(false),

	floorChangeDown(false),
	floorChangeNorth(false),
	floorChangeSouth(false),
	floorChangeEast(false),
	floorChangeWest(false),
	floorChange(false),

	blockSolid(false),
	blockPickupable(false),
	blockProjectile(false),
	blockPathFind(false),
	hasElevation(false),

	alwaysOnTopOrder(0),
	rotateTo(0),
	border_alignment(BORDER_NONE)
{
	////
}

ItemType::~ItemType()
{
	////
}

bool ItemType::isFloorChange() const
{
	return floorChange || floorChangeDown || floorChangeNorth || floorChangeSouth || floorChangeEast || floorChangeWest;
}

ItemDatabase::ItemDatabase() :
	// Version information
	MajorVersion(0),
	MinorVersion(0),
	BuildNumber(0),

	// Count of GameSprite types
	item_count(0),
	effect_count(0),
	monster_count(0),
	distance_count(0),

	minclientID(0),
	maxclientID(0),

	max_item_id(0)
{
	////
}

ItemDatabase::~ItemDatabase()
{
	clear();
}

void ItemDatabase::clear()
{
	for(uint32_t i = 0; i < items.size(); i++) {
		delete items[i];
		items.set(i, nullptr);
	}
}

bool ItemDatabase::loadFromProtobuf(wxString &error, wxArrayString &warnings)
{
	using namespace remeres::protobuf::appearances;

	for (uint32_t it = 0; it < static_cast<uint32_t>(appearances.object_size()); ++it)
	{
		Appearance object = appearances.object(it);

		// This scenario should never happen but on custom assets this can break the loader.
		if (!object.has_flags())
		{
			spdlog::error("[ItemDatabase::loadFromProtobuf] - Item with id {} is invalid and was ignored.", object.id());
			wxLogError("[ItemDatabase::loadFromProtobuf] - Item with id %i is invalid and was ignored.", object.id());
			continue;
		}

		if (object.id() >= items.size())
		{
			items.resize(object.id() + 1);
		}

		ItemType *t = newd ItemType();
		if (object.flags().container())
		{
			t->type = ITEM_TYPE_CONTAINER;
			t->group = ITEM_GROUP_CONTAINER;
		}
		else if (object.flags().has_bank())
		{
			t->group = ITEM_GROUP_GROUND;
		}
		else if (object.flags().liquidcontainer())
		{
			t->group = ITEM_GROUP_FLUID;
		}
		else if (object.flags().liquidpool())
		{
			t->group = ITEM_GROUP_SPLASH;
		}

		if (object.flags().clip())
		{
			t->alwaysOnTopOrder = 1;
		}
		else if (object.flags().top())
		{
			t->alwaysOnTopOrder = 3;
		}
		else if (object.flags().bottom())
		{
			t->alwaysOnTopOrder = 2;
		}

		if (object.flags().no_movement_animation())
		{
			t->noMoveAnimation = object.flags().no_movement_animation();
		}

		t->id = static_cast<uint16_t>(object.id());
		t->clientID = static_cast<uint16_t>(object.id());

		t->sprite = static_cast<GameSprite*>(g_gui.gfx.getSprite(t->clientID));

		// now lets parse sprite data
		t->m_animationPhases = 0;
		int totalSpritesCount = 0;

		std::vector<SpritesSize> sizes;
		std::vector<int> total_sprites;

		for (const auto &framegroup : object.frame_group())
		{
			int frameGroupType = framegroup.fixed_frame_group();
			const auto &spriteInfo = framegroup.sprite_info();
			const auto &animation = spriteInfo.animation();
			const auto &sprites = spriteInfo.sprite_id();
			const auto &spritesPhases = animation.sprite_phase();

			t->pattern_width = spriteInfo.pattern_width();
			t->pattern_height = spriteInfo.pattern_height();
			t->pattern_depth = spriteInfo.pattern_depth();
			t->layers = spriteInfo.layers();

			t->m_animationPhases += std::max<int>(1, spritesPhases.size());
			t->sprite_id = spriteInfo.sprite_id(0);
			SpritePtr sprite = g_spriteAppearances.getSprite(t->sprite_id);
			if (!sprite) {
				spdlog::error("unknown sprite id");
				throw std::exception("unknown sprite id");
			}
			//spdlog::info("sprite->size.width: {}", sprite->size.width);
			//spdlog::info("sprite->size.height: {}", sprite->size.height);

			SpriteSheetPtr sheet = g_spriteAppearances.getSheetBySpriteId(spriteInfo.sprite_id(0), false);
			if (sheet)
			{
				t->m_size = sheet->getSpriteSize();
				sizes.push_back(t->m_size);
			}

			/*// animations
			if (spritesPhases.size() > 1)
			{
				auto animator = AnimatorPtr(new Animator);
				animator->unserializeAppearance(animation);

				if (frameGroupType == FrameGroupMoving)
					m_animator = animator;
				else if (frameGroupType == FrameGroupIdle || frameGroupType == FrameGroupInitial)
					m_idleAnimator = animator;
			}*/

			const int totalSprites = t->layers * t->pattern_width * t->pattern_height * t->pattern_depth * std::max<int>(1, spritesPhases.size());
			total_sprites.push_back(totalSprites);

			if (totalSpritesCount + totalSprites > 4096)
				spdlog::error("a thing type has more than 4096 sprites");

			t->m_spritesIndex.resize(totalSpritesCount + totalSprites);
			for (int j = totalSpritesCount, spriteId = 0; j < (totalSpritesCount + totalSprites); ++j, ++spriteId)
			{
				t->m_spritesIndex[j] = spriteInfo.sprite_id(spriteId);
			}

			totalSpritesCount += totalSprites;
		}

		if (sizes.size() > 1)
		{
			// correction for some sprites
			for (auto &s : sizes)
			{
				//t->m_size->setWidth(s.width());
				//t->m_size.setHeight(s.height());
				spdlog::info("setWidth {}", s.width);
				spdlog::info("setHeight {}", s.height);
			}
			size_t expectedSize = t->m_size.area() * t->layers * t->pattern_width * t->pattern_height * t->pattern_depth * t->m_animationPhases;
			if (expectedSize != t->m_spritesIndex.size())
			{
				std::vector<int> sprites(std::move(t->m_spritesIndex));
				t->m_spritesIndex.clear();
				t->m_spritesIndex.reserve(expectedSize);
				for (size_t i = 0, idx = 0; i < sizes.size(); ++i)
				{
					int totalSprites = total_sprites[i];
					//if (t->m_size == sizes[i])
					{
						for (int j = 0; j < totalSprites; ++j)
						{
							t->m_spritesIndex.push_back(sprites[idx++]);
						}
						continue;
					}
					size_t patterns = (totalSprites / sizes[i].area());
					/*for (size_t p = 0; p < patterns; ++p)
					{
						for (int x = 0; x < t->m_size.width(); ++x)
						{
							for (int y = 0; y < t->m_size.height(); ++y)
							{
								if (x < sizes[i].width() && y < sizes[i].height())
								{
									t->m_spritesIndex.push_back(sprites[idx++]);
									continue;
								}
								t->m_spritesIndex.push_back(0);
							}
						}
					}*/
				}
			}
		}

		// Sprite
		if (object.frame_group_size() > 0)
		{
			if (FrameGroup frameGroup = object.frame_group(0);
				frameGroup.has_sprite_info())
			{
				const SpriteInfo &spriteInfo = frameGroup.sprite_info();
				t->pattern_width = spriteInfo.pattern_width();
				t->pattern_height = spriteInfo.pattern_height();
				t->pattern_depth = spriteInfo.pattern_depth();
				t->layers = spriteInfo.layers();
				t->sprite = static_cast<GameSprite*>(g_gui.gfx.getSprite(spriteInfo.sprite_id(0)));
			}
		}

		g_graphics.loadSpriteMetadata(t, error, warnings);

		if (object.flags().has_clothes())
		{
			// t->slotPosition |= static_cast<SlotPositionBits>(1 << (object.flags().clothes().slot() - 1));
		}

		if (object.flags().has_market())
		{
			t->type = static_cast<ItemTypes_t>(object.flags().market().category());
		}

		t->name = object.name();
		t->description = object.description();

		// t->upgradeClassification = object.flags().has_upgradeclassification() ? static_cast<uint8_t>(object.flags().upgradeclassification().upgrade_classification()) : 0;
		// t->lightLevel = object.flags().has_light() ? static_cast<uint8_t>(object.flags().light().brightness()) : 0;
		// t->lightColor = object.flags().has_light() ? static_cast<uint8_t>(object.flags().light().color()) : 0;

		// t->speed = object.flags().has_bank() ? static_cast<uint16_t>(object.flags().bank().waypoints()) : 0;
		// t->wareId = object.flags().has_market() ? static_cast<uint16_t>(object.flags().market().trade_as_object_id()) : 0;

		t->isCorpse = object.flags().corpse() || object.flags().player_corpse();
		t->forceUse = object.flags().forceuse();
		t->hasHeight = object.flags().has_height();
		t->blockSolid = object.flags().unpass();
		t->blockProjectile = object.flags().unsight();
		t->blockPathFind = object.flags().avoid();
		t->pickupable = object.flags().take();
		// t->rotatable = object.flags().rotate();
		// t->wrapContainer = object.flags().wrap() || object.flags().unwrap();
		// t->multiUse = object.flags().multiuse();
		t->moveable = object.flags().unmove() == false;
		t->canReadText = (object.flags().has_lenshelp() && object.flags().lenshelp().id() == 1112) || (object.flags().has_write() && object.flags().write().max_text_length() != 0) || (object.flags().has_write_once() && object.flags().write_once().max_text_length_once() != 0);
		t->canReadText = object.flags().has_write() || object.flags().has_write_once();
		t->isVertical = object.flags().has_hook() && object.flags().hook().south();
		t->isHorizontal = object.flags().has_hook() && object.flags().hook().east();
		t->isHangable = object.flags().hang();
		// t->lookThrough = object.flags().ignore_look();
		t->stackable = object.flags().cumulative();
		t->isPodium = object.flags().show_off_socket();

		if (t)
		{
			if (items[t->id])
			{
				wxLogWarning("appearances.dat: Duplicate items");
				delete items[t->id];
			}
			items.set(t->id, t);
		}
	}
	return true;
}

bool ItemDatabase::loadAppearanceProtobuf(wxString& error, wxArrayString& warnings)
{
	using namespace remeres::protobuf::appearances;
	using json = nlohmann::json;
	std::stringstream ss;
	ss << g_gui.GetDataDirectory();
	const std::string& assetsDirectory = ss.str() + "assets/";
	const std::string& catalogContentFile = assetsDirectory + "catalog-content.json";

	if (!g_spriteAppearances.loadCatalogContent(assetsDirectory, true)) {
		spdlog::error("[ItemDatabase::loadAppearanceProtobuf] - Cannot open catalog content file");
		return false;
	}

	const std::string appearanceFileName = g_spriteAppearances.getAppearanceFileName();
	spdlog::info("appearance file {}", appearanceFileName);
	spdlog::info("data directory + appearance file {}", assetsDirectory + appearanceFileName);

	//g_spriteAppearances.setSpritesCount(spritesCount + 1);

	std::fstream fileStream(assetsDirectory + appearanceFileName, std::ios::in | std::ios::binary);
	if (!fileStream.is_open()) {
		error = "Failed to load "+ appearanceFileName +", file cannot be oppened";
		spdlog::error("[ItemDatabase::loadAppearanceProtobuf] - Failed to load %s, file cannot be oppened", appearanceFileName);
		fileStream.close();
		return false;
	}

	// Verify that the version of the library that we linked against is
	// compatible with the version of the headers we compiled against.
	GOOGLE_PROTOBUF_VERIFY_VERSION;
	appearances = Appearances();
	if (!appearances.ParseFromIstream(&fileStream)) {
		error = "Failed to parse binary file "+ appearanceFileName +", file is invalid";
		spdlog::error("[ItemDatabase::loadAppearanceProtobuf] - Failed to parse binary file {}, file is invalid", appearanceFileName);
		fileStream.close();
		return false;
	}

	// Parsing all items into ItemType
	loadFromProtobuf(error, warnings);

	fileStream.close();

	// Disposing allocated objects.
	google::protobuf::ShutdownProtobufLibrary();
	return true;
}

bool ItemDatabase::loadItemFromGameXml(pugi::xml_node itemNode, int id)
{
	ClientVersionID clientVersion = g_gui.GetCurrentVersionID();
	if(clientVersion < CLIENT_VERSION_980 && id > 20000 && id < 20100) {
		itemNode = itemNode.next_sibling();
		return true;
	} else if(id > 30000 && id < 30100) {
		itemNode = itemNode.next_sibling();
		return true;
	}

	ItemType& it = getItemType(id);

	it.name = itemNode.attribute("name").as_string();
	it.editorsuffix = itemNode.attribute("editorsuffix").as_string();

	pugi::xml_attribute attribute;
	for(pugi::xml_node itemAttributesNode = itemNode.first_child(); itemAttributesNode; itemAttributesNode = itemAttributesNode.next_sibling()) {
		if(!(attribute = itemAttributesNode.attribute("key"))) {
			continue;
		}

		std::string key = attribute.as_string();
		to_lower_str(key);
		if(key == "type") {
			if(!(attribute = itemAttributesNode.attribute("value"))) {
				continue;
			}

			std::string typeValue = attribute.as_string();
			to_lower_str(key);
			if(typeValue == "depot") {
				it.type = ITEM_TYPE_DEPOT;
			} else if(typeValue == "mailbox") {
				it.type = ITEM_TYPE_MAILBOX;
			} else if(typeValue == "trashholder") {
				it.type = ITEM_TYPE_TRASHHOLDER;
			} else if (typeValue == "container") {
				it.type = ITEM_TYPE_CONTAINER;
			} else if (typeValue == "door") {
				it.type = ITEM_TYPE_DOOR;
			} else if (typeValue == "magicfield") {
				it.group = ITEM_GROUP_MAGICFIELD;
				it.type = ITEM_TYPE_MAGICFIELD;
			} else if (typeValue == "teleport") {
				it.type = ITEM_TYPE_TELEPORT;
			} else if (typeValue == "bed") {
				it.type = ITEM_TYPE_BED;
			} else if (typeValue == "key") {
				it.type = ITEM_TYPE_KEY;
			}
		} else if(key == "name") {
			if((attribute = itemAttributesNode.attribute("value"))) {
				it.name = attribute.as_string();
			}
		} else if(key == "description") {
			if((attribute = itemAttributesNode.attribute("value"))) {
				it.description = attribute.as_string();
			}
		}else if(key == "runespellName") {
			/*if((attribute = itemAttributesNode.attribute("value"))) {
				it.runeSpellName = attribute.as_string();
			}*/
		} else if(key == "weight") {
			if((attribute = itemAttributesNode.attribute("value"))) {
				it.weight = attribute.as_int() / 100.f;
			}
		} else if(key == "armor") {
			if((attribute = itemAttributesNode.attribute("value"))) {
				it.armor = attribute.as_int();
			}
		} else if(key == "defense") {
			if((attribute = itemAttributesNode.attribute("value"))) {
				it.defense = attribute.as_int();
			}
		} else if(key == "rotateto") {
			if((attribute = itemAttributesNode.attribute("value"))) {
				it.rotateTo = attribute.as_ushort();
			}
		} else if(key == "containersize") {
			if((attribute = itemAttributesNode.attribute("value"))) {
				it.volume = attribute.as_ushort();
			}
		} else if(key == "readable") {
			if((attribute = itemAttributesNode.attribute("value"))) {
				it.canReadText = attribute.as_bool();
			}
		} else if(key == "writeable") {
			if((attribute = itemAttributesNode.attribute("value"))) {
				it.canWriteText = it.canReadText = attribute.as_bool();
			}
		} else if(key == "decayto") {
			it.decays = true;
		} else if(key == "maxtextlen" || key == "maxtextlength") {
			if((attribute = itemAttributesNode.attribute("value"))) {
				it.maxTextLen = attribute.as_ushort();
				it.canReadText = it.maxTextLen > 0;
			}
		} else if(key == "writeonceitemid") {
			/*if((attribute = itemAttributesNode.attribute("value"))) {
				it.writeOnceItemId = pugi::cast<int32_t>(attribute.value());
			}*/
		} else if(key == "allowdistread") {
			if((attribute = itemAttributesNode.attribute("value"))) {
				it.allowDistRead = attribute.as_bool();
			}
		} else if(key == "charges") {
			if((attribute = itemAttributesNode.attribute("value"))) {
				it.charges = attribute.as_uint();
				it.extra_chargeable = true;
			}
		} else if(key == "floorchange") {
			if ((attribute = itemAttributesNode.attribute("value"))) {
				std::string value = attribute.as_string();
				if(value == "down") {
					it.floorChangeDown = true;
					it.floorChange = true;
				} else if (value == "north") {
					it.floorChangeNorth = true;
					it.floorChange = true;
				} else if (value == "south") {
					it.floorChangeSouth = true;
					it.floorChange = true;
				} else if (value == "west") {
					it.floorChangeWest = true;
					it.floorChange = true;
				} else if (value == "east") {
					it.floorChangeEast = true;
					it.floorChange = true;
				} else if(value == "northex")
					it.floorChange = true;
				else if(value == "southex")
					it.floorChange = true;
				else if(value == "westex")
					it.floorChange = true;
				else if(value == "eastex")
					it.floorChange = true;
				else if (value == "southalt")
					it.floorChange = true;
				else if (value == "eastalt")
					it.floorChange = true;
			}
		}
	}
	return true;
}

bool ItemDatabase::loadFromGameXml(const FileName& identifier, wxString& error, wxArrayString& warnings)
{
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(identifier.GetFullPath().mb_str());
	if(!result) {
		error = "Could not load items.xml (Syntax error?)";
		return false;
	}

	pugi::xml_node node = doc.child("items");
	if(!node) {
		error = "items.xml, invalid root node.";
		return false;
	}

	for(pugi::xml_node itemNode = node.first_child(); itemNode; itemNode = itemNode.next_sibling()) {
		if(as_lower_str(itemNode.name()) != "item") {
			continue;
		}

		uint16_t fromId = 0;
		uint16_t toId = 0;
		if(const pugi::xml_attribute attribute = itemNode.attribute("id")) {
			fromId = toId = attribute.as_ushort();
		} else {
			fromId = itemNode.attribute("fromid").as_ushort();
			toId = itemNode.attribute("toid").as_ushort();
		}

		if(fromId == 0 || toId == 0) {
			error = "Could not read item id from item node, fromid "+ std::to_string(fromId) +", toid "+ std::to_string(toId) +".";
			return false;
		}

		for(uint16_t id = fromId; id <= toId; ++id) {
			if(!loadItemFromGameXml(itemNode, id)) {
				return false;
			}
		}
	}
	return true;
}

bool ItemDatabase::loadMetaItem(pugi::xml_node node)
{
	if(const pugi::xml_attribute attribute = node.attribute("id")) {
		const uint16_t id = attribute.as_ushort();
		if(id == 0 || items[id]) {
			return false;
		}
		items.set(id, newd ItemType());
		items[id]->is_metaitem = true;
		items[id]->id = id;
		return true;
	}
	return false;
}

ItemType& ItemDatabase::getItemType(int id)
{
	ItemType* it = items[id];
	if(it)
		return *it;
	else {
		static ItemType dummyItemType; // use this for invalid ids
		return dummyItemType;
	}
}

bool ItemDatabase::typeExists(int id) const
{
	ItemType* it = items[id];
	return it != nullptr;
}
