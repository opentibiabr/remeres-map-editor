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

#include "graphics.h"

#include "client_assets.h"
#include "artprovider.h"
#include "filehandle.h"
#include "settings.h"
#include "gui.h"
#include "otml.h"
#include "sprite_appearances.h"
#include "sprites.h"
#include "pngfiles.h"

#include <wx/rawbmp.h>

#include <appearances.pb.h>

GraphicManager g_graphics;
GameSprite g_gameSprite;

// All 133 template colors
static uint32_t TemplateOutfitLookupTable[] = {
	0xFFFFFF,
	0xFFD4BF,
	0xFFE9BF,
	0xFFFFBF,
	0xE9FFBF,
	0xD4FFBF,
	0xBFFFBF,
	0xBFFFD4,
	0xBFFFE9,
	0xBFFFFF,
	0xBFE9FF,
	0xBFD4FF,
	0xBFBFFF,
	0xD4BFFF,
	0xE9BFFF,
	0xFFBFFF,
	0xFFBFE9,
	0xFFBFD4,
	0xFFBFBF,
	0xDADADA,
	0xBF9F8F,
	0xBFAF8F,
	0xBFBF8F,
	0xAFBF8F,
	0x9FBF8F,
	0x8FBF8F,
	0x8FBF9F,
	0x8FBFAF,
	0x8FBFBF,
	0x8FAFBF,
	0x8F9FBF,
	0x8F8FBF,
	0x9F8FBF,
	0xAF8FBF,
	0xBF8FBF,
	0xBF8FAF,
	0xBF8F9F,
	0xBF8F8F,
	0xB6B6B6,
	0xBF7F5F,
	0xBFAF8F,
	0xBFBF5F,
	0x9FBF5F,
	0x7FBF5F,
	0x5FBF5F,
	0x5FBF7F,
	0x5FBF9F,
	0x5FBFBF,
	0x5F9FBF,
	0x5F7FBF,
	0x5F5FBF,
	0x7F5FBF,
	0x9F5FBF,
	0xBF5FBF,
	0xBF5F9F,
	0xBF5F7F,
	0xBF5F5F,
	0x919191,
	0xBF6A3F,
	0xBF943F,
	0xBFBF3F,
	0x94BF3F,
	0x6ABF3F,
	0x3FBF3F,
	0x3FBF6A,
	0x3FBF94,
	0x3FBFBF,
	0x3F94BF,
	0x3F6ABF,
	0x3F3FBF,
	0x6A3FBF,
	0x943FBF,
	0xBF3FBF,
	0xBF3F94,
	0xBF3F6A,
	0xBF3F3F,
	0x6D6D6D,
	0xFF5500,
	0xFFAA00,
	0xFFFF00,
	0xAAFF00,
	0x54FF00,
	0x00FF00,
	0x00FF54,
	0x00FFAA,
	0x00FFFF,
	0x00A9FF,
	0x0055FF,
	0x0000FF,
	0x5500FF,
	0xA900FF,
	0xFE00FF,
	0xFF00AA,
	0xFF0055,
	0xFF0000,
	0x484848,
	0xBF3F00,
	0xBF7F00,
	0xBFBF00,
	0x7FBF00,
	0x3FBF00,
	0x00BF00,
	0x00BF3F,
	0x00BF7F,
	0x00BFBF,
	0x007FBF,
	0x003FBF,
	0x0000BF,
	0x3F00BF,
	0x7F00BF,
	0xBF00BF,
	0xBF007F,
	0xBF003F,
	0xBF0000,
	0x242424,
	0x7F2A00,
	0x7F5500,
	0x7F7F00,
	0x557F00,
	0x2A7F00,
	0x007F00,
	0x007F2A,
	0x007F55,
	0x007F7F,
	0x00547F,
	0x002A7F,
	0x00007F,
	0x2A007F,
	0x54007F,
	0x7F007F,
	0x7F0055,
	0x7F002A,
	0x7F0000,
};

GraphicManager::GraphicManager() :
	otfi_found(false),
	is_extended(false),
	has_frame_durations(false),
	has_frame_groups(false),
	loaded_textures(0),
	lastclean(0) {
	animation_timer = newd wxStopWatch();
	animation_timer->Start();
}

GraphicManager::~GraphicManager() {
	for (SpriteMap::iterator iter = sprite_space.begin(); iter != sprite_space.end(); ++iter) {
		delete iter->second;
		iter->second = nullptr;
	}

	for (ImageMap::iterator iter = image_space.begin(); iter != image_space.end(); ++iter) {
		delete iter->second;
		iter->second = nullptr;
	}

	sprite_space.clear();
	image_space.clear();

	delete animation_timer;
	animation_timer = nullptr;
}

GLuint GraphicManager::getFreeTextureID() {
	static GLuint id_counter = 0x10000000;
	return id_counter++; // This should (hopefully) never run out
}

void GraphicManager::clear() {
	SpriteMap new_sprite_space;
	for (SpriteMap::iterator iter = sprite_space.begin(); iter != sprite_space.end(); ++iter) {
		if (iter->first >= 0) { // Don't clean internal sprites
			delete iter->second;
			iter->second = nullptr;
		} else {
			new_sprite_space.insert(std::make_pair(iter->first, iter->second));
		}
	}

	for (ImageMap::iterator iter = image_space.begin(); iter != image_space.end(); ++iter) {
		delete iter->second;
		iter->second = nullptr;
	}

	sprite_space.swap(new_sprite_space);
	image_space.clear();
	cleanup_list.clear();

	item_count = 0;
	creature_count = 0;
	loaded_textures = 0;
	lastclean = time(nullptr);
}

void GraphicManager::cleanSoftwareSprites() {
	for (SpriteMap::iterator iter = sprite_space.begin(); iter != sprite_space.end(); ++iter) {
		if (iter->first >= 0) { // Don't clean internal sprites
			iter->second->unloadDC();
		}
	}
}

Sprite* GraphicManager::getSprite(int id) {
	SpriteMap::iterator it = sprite_space.find(id);
	if (it != sprite_space.end()) {
		return it->second;
	}
	return nullptr;
}

GameSprite* GraphicManager::getCreatureSprite(int id) {
	if (id < 0) {
		return nullptr;
	}

	SpriteMap::iterator it = sprite_space.find(id + getItemSpriteMaxID());
	if (it != sprite_space.end()) {
		return static_cast<GameSprite*>(it->second);
	}
	return nullptr;
}

uint16_t GraphicManager::getItemSpriteMaxID() const {
	return item_count;
}

uint16_t GraphicManager::getCreatureSpriteMaxID() const {
	return creature_count;
}

GameSprite* GraphicManager::getEditorSprite(int id) {
	if (id >= 0) {
		return nullptr;
	}

	SpriteMap::iterator it = sprite_space.find(id);
	if (it != sprite_space.end()) {
		return dynamic_cast<GameSprite*>(it->second);
	}
	return nullptr;
}

#define loadPNGFile(name) _wxGetBitmapFromMemory(name, sizeof(name))
inline wxBitmap* _wxGetBitmapFromMemory(const unsigned char* data, int length) {
	wxMemoryInputStream is(data, length);
	wxImage img(is, "image/png");
	if (!img.IsOk()) {
		return nullptr;
	}
	return newd wxBitmap(img, -1);
}

bool GraphicManager::loadEditorSprites() {
	// Unused graphics MIGHT be loaded here, but it's a neglectable loss
	sprite_space[EDITOR_SPRITE_SELECTION_MARKER] = newd EditorSprite(
		newd wxBitmap(selection_marker_xpm16x16),
		newd wxBitmap(selection_marker_xpm32x32)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_CD_1x1] = newd EditorSprite(
		loadPNGFile(circular_1_small_png),
		loadPNGFile(circular_1_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_CD_3x3] = newd EditorSprite(
		loadPNGFile(circular_2_small_png),
		loadPNGFile(circular_2_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_CD_5x5] = newd EditorSprite(
		loadPNGFile(circular_3_small_png),
		loadPNGFile(circular_3_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_CD_7x7] = newd EditorSprite(
		loadPNGFile(circular_4_small_png),
		loadPNGFile(circular_4_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_CD_9x9] = newd EditorSprite(
		loadPNGFile(circular_5_small_png),
		loadPNGFile(circular_5_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_CD_15x15] = newd EditorSprite(
		loadPNGFile(circular_6_small_png),
		loadPNGFile(circular_6_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_CD_19x19] = newd EditorSprite(
		loadPNGFile(circular_7_small_png),
		loadPNGFile(circular_7_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_SD_1x1] = newd EditorSprite(
		loadPNGFile(rectangular_1_small_png),
		loadPNGFile(rectangular_1_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_SD_3x3] = newd EditorSprite(
		loadPNGFile(rectangular_2_small_png),
		loadPNGFile(rectangular_2_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_SD_5x5] = newd EditorSprite(
		loadPNGFile(rectangular_3_small_png),
		loadPNGFile(rectangular_3_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_SD_7x7] = newd EditorSprite(
		loadPNGFile(rectangular_4_small_png),
		loadPNGFile(rectangular_4_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_SD_9x9] = newd EditorSprite(
		loadPNGFile(rectangular_5_small_png),
		loadPNGFile(rectangular_5_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_SD_15x15] = newd EditorSprite(
		loadPNGFile(rectangular_6_small_png),
		loadPNGFile(rectangular_6_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_SD_19x19] = newd EditorSprite(
		loadPNGFile(rectangular_7_small_png),
		loadPNGFile(rectangular_7_png)
	);

	sprite_space[EDITOR_SPRITE_OPTIONAL_BORDER_TOOL] = newd EditorSprite(
		loadPNGFile(optional_border_small_png),
		loadPNGFile(optional_border_png)
	);
	sprite_space[EDITOR_SPRITE_ERASER] = newd EditorSprite(
		loadPNGFile(eraser_small_png),
		loadPNGFile(eraser_png)
	);
	sprite_space[EDITOR_SPRITE_PZ_TOOL] = newd EditorSprite(
		loadPNGFile(protection_zone_small_png),
		loadPNGFile(protection_zone_png)
	);
	sprite_space[EDITOR_SPRITE_PVPZ_TOOL] = newd EditorSprite(
		loadPNGFile(pvp_zone_small_png),
		loadPNGFile(pvp_zone_png)
	);
	sprite_space[EDITOR_SPRITE_NOLOG_TOOL] = newd EditorSprite(
		loadPNGFile(no_logout_small_png),
		loadPNGFile(no_logout_png)
	);
	sprite_space[EDITOR_SPRITE_NOPVP_TOOL] = newd EditorSprite(
		loadPNGFile(no_pvp_small_png),
		loadPNGFile(no_pvp_png)
	);

	sprite_space[EDITOR_SPRITE_DOOR_NORMAL] = newd EditorSprite(
		loadPNGFile(door_normal_small_png),
		loadPNGFile(door_normal_png)
	);
	sprite_space[EDITOR_SPRITE_DOOR_LOCKED] = newd EditorSprite(
		loadPNGFile(door_locked_small_png),
		loadPNGFile(door_locked_png)
	);
	sprite_space[EDITOR_SPRITE_DOOR_MAGIC] = newd EditorSprite(
		loadPNGFile(door_magic_small_png),
		loadPNGFile(door_magic_png)
	);
	sprite_space[EDITOR_SPRITE_DOOR_QUEST] = newd EditorSprite(
		loadPNGFile(door_quest_small_png),
		loadPNGFile(door_quest_png)
	);
	sprite_space[EDITOR_SPRITE_WINDOW_NORMAL] = newd EditorSprite(
		loadPNGFile(window_normal_small_png),
		loadPNGFile(window_normal_png)
	);
	sprite_space[EDITOR_SPRITE_WINDOW_HATCH] = newd EditorSprite(
		loadPNGFile(window_hatch_small_png),
		loadPNGFile(window_hatch_png)
	);

	sprite_space[EDITOR_SPRITE_SELECTION_GEM] = newd EditorSprite(
		loadPNGFile(gem_edit_png),
		nullptr
	);
	sprite_space[EDITOR_SPRITE_DRAWING_GEM] = newd EditorSprite(
		loadPNGFile(gem_move_png),
		nullptr
	);

	sprite_space[EDITOR_SPRITE_MONSTERS] = GameSprite::createFromBitmap(ART_MONSTERS);
	sprite_space[EDITOR_SPRITE_NPCS] = GameSprite::createFromBitmap(ART_NPCS);
	sprite_space[EDITOR_SPRITE_HOUSE_EXIT] = GameSprite::createFromBitmap(ART_HOUSE_EXIT);
	sprite_space[EDITOR_SPRITE_PICKUPABLE_ITEM] = GameSprite::createFromBitmap(ART_PICKUPABLE);
	sprite_space[EDITOR_SPRITE_MOVEABLE_ITEM] = GameSprite::createFromBitmap(ART_MOVEABLE);
	sprite_space[EDITOR_SPRITE_PICKUPABLE_MOVEABLE_ITEM] = GameSprite::createFromBitmap(ART_PICKUPABLE_MOVEABLE);
	sprite_space[EDITOR_SPRITE_AVOIDABLE_ITEM] = GameSprite::createFromBitmap(ART_AVOIDABLE);

	return true;
}

#if CLIENT_VERSION < 1100
bool GraphicManager::loadOTFI(const FileName &filename, wxString &error, wxArrayString &warnings) {
	wxDir dir(filename.GetFullPath());
	wxString otfi_file;

	otfi_found = false;

	if (dir.GetFirst(&otfi_file, "*.otfi", wxDIR_FILES)) {
		wxFileName otfi(filename.GetFullPath(), otfi_file);
		OTMLDocumentPtr doc = OTMLDocument::parse(otfi.GetFullPath().ToStdString());
		if (doc->size() == 0 || !doc->hasChildAt("DatSpr")) {
			error += "'DatSpr' tag not found";
			return false;
		}

		OTMLNodePtr node = doc->get("DatSpr");
		is_extended = node->valueAt<bool>("extended");
		has_transparency = node->valueAt<bool>("transparency");
		has_frame_durations = node->valueAt<bool>("frame-durations");
		has_frame_groups = node->valueAt<bool>("frame-groups");
		std::string metadata = node->valueAt<std::string>("metadata-file", std::string(ASSETS_NAME) + ".dat");
		std::string sprites = node->valueAt<std::string>("sprites-file", std::string(ASSETS_NAME) + ".spr");
		metadata_file = wxFileName(filename.GetFullPath(), wxString(metadata));
		sprites_file = wxFileName(filename.GetFullPath(), wxString(sprites));
		otfi_found = true;
	}

	if (!otfi_found) {
		is_extended = false;
		has_transparency = false;
		has_frame_durations = false;
		has_frame_groups = false;
		metadata_file = wxFileName(filename.GetFullPath(), wxString(ASSETS_NAME) + ".dat");
		sprites_file = wxFileName(filename.GetFullPath(), wxString(ASSETS_NAME) + ".spr");
	}

	return true;
}

bool GraphicManager::loadSpriteMetadata(const FileName &datafile, wxString &error, wxArrayString &warnings) {
	// items.otb has most of the info we need. This only loads the GameSprite metadata
	FileReadHandle file(nstr(datafile.GetFullPath()));

	if (!file.isOk()) {
		error += "Failed to open " + datafile.GetFullPath() + " for reading\nThe error reported was:" + wxstr(file.getErrorMessage());
		return false;
	}

	uint16_t effect_count, distance_count;

	uint32_t datSignature;
	file.getU32(datSignature);
	// get max id
	file.getU16(item_count);
	file.getU16(creature_count);
	file.getU16(effect_count);
	file.getU16(distance_count);

	uint32_t minID = 100; // items start with id 100
	// We don't load distance/effects, if we would, just add effect_count & distance_count here
	uint32_t maxID = item_count + creature_count;

	dat_format = client_version->getDatFormatForSignature(datSignature);

	if (dat_format == DAT_FORMAT_UNKNOWN) {
		error += "Failed to open " + datafile.GetFullPath() + " for reading\nCould not locate datSignature (0x" << wxString::Format(wxT("%02x"), datSignature) << ") compatible with client version " << client_version->getName();
		return false;
	}

	if (!otfi_found) {
		is_extended = dat_format >= DAT_FORMAT_11;
		has_frame_durations = dat_format >= DAT_FORMAT_11;
		has_frame_groups = dat_format >= DAT_FORMAT_11;
	}

	uint16_t id = minID;
	// loop through all ItemDatabase until we reach the end of file
	while (id <= maxID) {
		GameSprite* sType = newd GameSprite();
		sprite_space[id] = sType;

		sType->id = id;

		// Load the sprite flags
		if (!loadSpriteMetadataFlags(file, sType, error, warnings)) {
			wxString msg;
			msg << "Failed to load flags for sprite " << sType->id;
			warnings.push_back(msg);
		}

		// Reads the group count
		uint8_t group_count = 1;
		if (has_frame_groups && id > item_count) {
			file.getU8(group_count);
		}

		for (uint32_t k = 0; k < group_count; ++k) {
			// Skipping the group type
			if (has_frame_groups && id > item_count) {
				file.skip(1);
			}

			// Size and GameSprite data
			file.getByte(sType->width);
			file.getByte(sType->height);

			// Skipping the exact size
			if ((sType->width > 1) || (sType->height > 1)) {
				file.skip(1);
			}

			file.getU8(sType->layers); // Number of blendframes (some sprites consist of several merged sprites)
			file.getU8(sType->pattern_x);
			file.getU8(sType->pattern_y);
			file.getU8(sType->pattern_z);
			file.getU8(sType->frames); // Length of animation

			if (sType->frames > 1) {
				uint8_t async = 0;
				int loop_count = 0;
				int8_t start_frame = 0;
				if (has_frame_durations) {
					file.getByte(async);
					file.get32(loop_count);
					file.getSByte(start_frame);
				}
				sType->animator = newd Animator(sType->frames, start_frame, loop_count, async == 1);
				if (has_frame_durations) {
					for (int i = 0; i < sType->frames; i++) {
						uint32_t min;
						uint32_t max;
						file.getU32(min);
						file.getU32(max);
						FrameDuration* frame_duration = sType->animator->getFrameDuration(i);
						frame_duration->setValues(int(min), int(max));
					}
					sType->animator->reset();
				}
			}

			sType->numsprites = (int)sType->width * (int)sType->height * (int)sType->layers * (int)sType->pattern_x * (int)sType->pattern_y * sType->pattern_z * (int)sType->frames;

			// Read the sprite ids
			for (uint32_t i = 0; i < sType->numsprites; ++i) {
				uint32_t sprite_id;
				if (is_extended) {
					file.getU32(sprite_id);
				} else {
					uint16_t u16 = 0;
					file.getU16(u16);
					sprite_id = u16;
				}

				if (image_space[sprite_id] == nullptr) {
					GameSprite::NormalImage* img = newd GameSprite::NormalImage();
					img->id = sprite_id;
					image_space[sprite_id] = img;
				}
				sType->spriteList.push_back(static_cast<GameSprite::NormalImage*>(image_space[sprite_id]));
			}
		}
		++id;
	}

	return true;
}

bool GraphicManager::loadSpriteMetadataFlags(FileReadHandle &file, GameSprite* sType, wxString &error, wxArrayString &warnings) {
	uint8_t prev_flag = 0;
	uint8_t flag = DatFlagLast;

	for (int i = 0; i < DatFlagLast; ++i) {
		prev_flag = flag;
		file.getU8(flag);

		if (flag == DatFlagLast) {
			return true;
		}
		if (dat_format >= DAT_FORMAT_11) {
			/* In 10.10+ all attributes from 16 and up were
			 * incremented by 1 to make space for 16 as
			 * "No Movement Animation" flag.
			 */
			if (flag == 16) {
				flag = DatFlagNoMoveAnimation;
			} else if (flag > 16) {
				flag -= 1;
			}
		}

		switch (flag) {
			case DatFlagGroundBorder:
			case DatFlagOnBottom:
			case DatFlagOnTop:
			case DatFlagContainer:
			case DatFlagStackable:
			case DatFlagForceUse:
			case DatFlagMultiUse:
			case DatFlagFluidContainer:
			case DatFlagSplash:
			case DatFlagNotWalkable:
			case DatFlagNotMoveable:
			case DatFlagBlockProjectile:
			case DatFlagNotPathable:
			case DatFlagPickupable:
			case DatFlagHangable:
			case DatFlagHookSouth:
			case DatFlagHookEast:
			case DatFlagRotateable:
			case DatFlagDontHide:
			case DatFlagTranslucent:
			case DatFlagLyingCorpse:
			case DatFlagAnimateAlways:
			case DatFlagFullGround:
			case DatFlagLook:
			case DatFlagWrappable:
			case DatFlagUnwrappable:
			case DatFlagTopEffect:
			case DatFlagFloorChange:
			case DatFlagNoMoveAnimation:
			case DatFlagChargeable:
				break;

			case DatFlagWritable:
			case DatFlagWritableOnce:
			case DatFlagCloth:
			case DatFlagLensHelp:
			case DatFlagUsable:
				file.skip(2);
				break;

			case DatFlagGround:
				uint16_t speed;
				file.getU16(speed);
				sType->ground_speed = speed;
				break;

			case DatFlagLight: {
				SpriteLight light;
				uint16_t intensity;
				uint16_t color;
				file.getU16(intensity);
				file.getU16(color);
				sType->has_light = true;
				sType->light = SpriteLight { static_cast<uint8_t>(intensity), static_cast<uint8_t>(color) };
				break;
			}

			case DatFlagDisplacement: {
				if (dat_format >= DAT_FORMAT_11) {
					uint16_t offset_x;
					uint16_t offset_y;
					file.getU16(offset_x);
					file.getU16(offset_y);

					sType->draw_offset = wxPoint(offset_x, offset_y);
				} else {
					sType->draw_offset = wxPoint(8, 8);
				}
				break;
			}

			case DatFlagElevation: {
				uint16_t draw_height;
				file.getU16(draw_height);
				sType->draw_height = draw_height;
				break;
			}

			case DatFlagMinimapColor: {
				uint16_t minimap_color;
				file.getU16(minimap_color);
				sType->minimap_color = minimap_color;
				break;
			}

			case DatFlagMarket: {
				file.skip(6);
				std::string marketName;
				file.getString(marketName);
				file.skip(4);
				break;
			}

			default: {
				wxString err;
				err << "Metadata: Unknown flag: " << i2ws(flag) << ". Previous flag: " << i2ws(prev_flag) << ".";
				warnings.push_back(err);
				break;
			}
		}
	}

	return true;
}

bool GraphicManager::loadSpriteData(const FileName &datafile, wxString &error, wxArrayString &warnings) {
	FileReadHandle fh(nstr(datafile.GetFullPath()));

	if (!fh.isOk()) {
		error = "Failed to open file for reading";
		return false;
	}

	#define safe_get(func, ...)                      \
		do {                                         \
			if (!fh.get##func(__VA_ARGS__)) {        \
				error = wxstr(fh.getErrorMessage()); \
				return false;                        \
			}                                        \
		} while (false)

	uint32_t sprSignature;
	safe_get(U32, sprSignature);

	uint32_t total_pics = 0;
	if (is_extended) {
		safe_get(U32, total_pics);
	} else {
		uint16_t u16 = 0;
		safe_get(U16, u16);
		total_pics = u16;
	}

	if (!g_settings.getInteger(Config::USE_MEMCACHED_SPRITES)) {
		spritefile = nstr(datafile.GetFullPath());
		unloaded = false;
		return true;
	}

	std::vector<uint32_t> sprite_indexes;
	for (uint32_t i = 0; i < total_pics; ++i) {
		uint32_t index;
		safe_get(U32, index);
		sprite_indexes.push_back(index);
	}

	// Now read individual sprites
	int id = 1;
	for (std::vector<uint32_t>::iterator sprite_iter = sprite_indexes.begin(); sprite_iter != sprite_indexes.end(); ++sprite_iter, ++id) {
		uint32_t index = *sprite_iter + 3;
		fh.seek(index);
		uint16_t size;
		safe_get(U16, size);

		ImageMap::iterator it = image_space.find(id);
		if (it != image_space.end()) {
			GameSprite::NormalImage* spr = dynamic_cast<GameSprite::NormalImage*>(it->second);
			if (spr && size > 0) {
				if (spr->size > 0) {
					wxString ss;
					ss << "items.spr: Duplicate GameSprite id " << id;
					warnings.push_back(ss);
					fh.skip(size);
				} else {
					spr->id = id;
					spr->size = size;
					spr->m_cachedData = newd uint8_t[size];
					if (!fh.getRAW(spr->m_cachedData, size)) {
						error = wxstr(fh.getErrorMessage());
						return false;
					}
				}
			}
		} else {
			fh.skip(size);
		}
	}
	#undef safe_get
	unloaded = false;
	return true;
}

bool GraphicManager::loadSpriteDump(uint8_t*&target, uint16_t &size, int sprite_id) {
	if (g_settings.getInteger(Config::USE_MEMCACHED_SPRITES)) {
		return false;
	}

	if (sprite_id == 0) {
		// Empty GameSprite
		size = 0;
		target = nullptr;
		return true;
	}

	FileReadHandle fh(spritefile);
	if (!fh.isOk()) {
		return false;
	}
	unloaded = false;

	if (!fh.seek((is_extended ? 4 : 2) + sprite_id * sizeof(uint32_t))) {
		return false;
	}

	uint32_t to_seek = 0;
	if (fh.getU32(to_seek)) {
		fh.seek(to_seek + 3);
		uint16_t sprite_size;
		if (fh.getU16(sprite_size)) {
			target = newd uint8_t[sprite_size];
			if (fh.getRAW(target, sprite_size)) {
				size = sprite_size;
				return true;
			}
			delete[] target;
			target = nullptr;
		}
	}
	return false;
}
#endif

bool GraphicManager::loadItemSpriteMetadata(const std::shared_ptr<ItemType> &t, wxString &error, wxArrayString &warnings) {
	GameSprite* sType = newd GameSprite();
	sType->id = t->id;
	sprite_space[t->id] = sType;
	item_count = std::max<uint16_t>(item_count, t->id);

	// Number of blendframes (some sprites consist of several merged sprites
	sType->layers = t->layers;
	sType->pattern_x = t->pattern_width;
	sType->pattern_y = t->pattern_height;
	sType->pattern_z = t->pattern_depth;

	// Length of animation
	sType->sprite_phase_size = t->m_animationPhases.size();
	has_frame_durations = t->m_animationPhases.size() > 0;

	if (sType->sprite_phase_size > 0) {
		sType->animator = newd Animator(sType->sprite_phase_size, t->start_frame, t->loop_count, t->async_animation);
		if (has_frame_durations) {
			int frameIndex = 0;
			for (const auto phase : t->m_animationPhases) {
				FrameDuration* frame_duration = sType->animator->getFrameDuration(frameIndex);
				frame_duration->setValues(phase.first, phase.second);
				frameIndex++;
			}
			sType->animator->reset();
		}
	}

	sType->numsprites = (int)sType->layers * (int)sType->pattern_x * (int)sType->pattern_y * sType->pattern_z * std::max<int>(1, sType->sprite_phase_size);

	// Read the sprite ids
	for (uint32_t i = 0; i < sType->numsprites; ++i) {
		uint32_t sprite_id = t->m_sprites[i];

		if (image_space[sprite_id] == nullptr) {
			GameSprite::NormalImage* img = newd GameSprite::NormalImage();
			img->id = sprite_id;
			image_space[sprite_id] = img;
		}
		sType->spriteList.push_back(static_cast<GameSprite::NormalImage*>(image_space[sprite_id]));
	}
	return true;
}

bool GraphicManager::loadOutfitSpriteMetadata(canary::protobuf::appearances::Appearance outfit, wxString &error, wxArrayString &warnings) {
	GameSprite* sType = newd GameSprite();
	sType->id = outfit.id() + getItemSpriteMaxID();
	sprite_space[outfit.id() + getItemSpriteMaxID()] = sType;
	creature_count = std::max<uint16_t>(creature_count, outfit.id());

	// We dont need to worry about IDLE or MOVING frame group
	const auto &frameGroup = outfit.frame_group().Get(0);
	const auto &spriteInfo = frameGroup.sprite_info();
	const auto &animation = spriteInfo.animation();

	// Number of blendframes (some sprites consist of several merged sprites
	sType->layers = spriteInfo.layers();
	sType->pattern_x = spriteInfo.pattern_width();
	sType->pattern_y = spriteInfo.pattern_height();
	sType->pattern_z = spriteInfo.pattern_depth();

	// Length of animation
	sType->sprite_phase_size = animation.sprite_phase().size();
	has_frame_durations = animation.sprite_phase().size() > 0;

	if (sType->sprite_phase_size > 0) {
		sType->animator = newd Animator(sType->sprite_phase_size, animation.default_start_phase(), animation.loop_count(), !animation.synchronized());
		if (has_frame_durations) {
			int frameIndex = 0;
			for (const auto &phase : animation.sprite_phase()) {
				FrameDuration* frame_duration = sType->animator->getFrameDuration(frameIndex);
				frame_duration->setValues(phase.duration_min(), phase.duration_max());
				frameIndex++;
			}
			sType->animator->reset();
		}
	}

	sType->numsprites = (int)sType->layers * (int)sType->pattern_x * (int)sType->pattern_y * sType->pattern_z * std::max<int>(1, sType->sprite_phase_size);

	sType->minimap_color = outfit.flags().has_automap() ? static_cast<uint16_t>(outfit.flags().automap().color()) : 0;
	sType->draw_height = outfit.flags().has_height() ? static_cast<uint16_t>(outfit.flags().height().elevation()) : 0;
	if (outfit.flags().has_shift()) {
		wxPoint drawOffset(0, 0);
		if (sType->width > 32 || sType->height > 32) {
			drawOffset = sType->getDrawOffset();
		}
		drawOffset.x += outfit.flags().shift().x();
		drawOffset.y += outfit.flags().shift().y();
		sType->draw_offset = drawOffset;
	}

	// Read the sprite ids
	for (uint32_t i = 0; i < sType->numsprites; ++i) {
		uint32_t sprite_id = spriteInfo.sprite_id().Get(i);

		if (image_space[sprite_id] == nullptr) {
			GameSprite::NormalImage* img = newd GameSprite::NormalImage();
			img->id = sprite_id;
			image_space[sprite_id] = img;
		}
		sType->spriteList.push_back(static_cast<GameSprite::NormalImage*>(image_space[sprite_id]));
	}
	return true;
}

bool GraphicManager::loadSpriteDump(uint8_t*&target, uint16_t &size, int sprite_id) {
	// Empty GameSprite
	if (sprite_id == 0) {
		size = 0;
		target = nullptr;
		return true;
	}

	const auto &spritePtr = g_spriteAppearances.getSprite(sprite_id);
	if (!spritePtr) {
		return false;
	}

	size = spritePtr->pixels.size();
	target = spritePtr->pixels.data();
	return true;
}

void GraphicManager::addSpriteToCleanup(GameSprite* spr) {
	cleanup_list.push_back(spr);
	// Clean if needed
	if (cleanup_list.size() > std::max<uint32_t>(100, g_settings.getInteger(Config::SOFTWARE_CLEAN_THRESHOLD))) {
		for (int i = 0; i < g_settings.getInteger(Config::SOFTWARE_CLEAN_SIZE) && static_cast<uint32_t>(i) < cleanup_list.size(); ++i) {
			cleanup_list.front()->unloadDC();
			cleanup_list.pop_front();
		}
	}
}

void GraphicManager::garbageCollection() {
	if (g_settings.getInteger(Config::TEXTURE_MANAGEMENT)) {
		int t = time(nullptr);
		if (loaded_textures > g_settings.getInteger(Config::TEXTURE_CLEAN_THRESHOLD) && t - lastclean > g_settings.getInteger(Config::TEXTURE_CLEAN_PULSE)) {
			ImageMap::iterator iit = image_space.begin();
			while (iit != image_space.end()) {
				iit->second->clean(t);
				++iit;
			}
			lastclean = t;
		}
	}
}

EditorSprite::EditorSprite(wxBitmap* b16x16, wxBitmap* b32x32) {
	bm[SPRITE_SIZE_16x16] = b16x16;
	bm[SPRITE_SIZE_32x32] = b32x32;
}

EditorSprite::~EditorSprite() {
	unloadDC();
}

void EditorSprite::DrawTo(wxDC* dc, SpriteSize sz, int start_x, int start_y, int width, int height) {
	wxBitmap* sp = bm[sz];
	if (sp) {
		dc->DrawBitmap(*sp, start_x, start_y, true);
	}
}

void EditorSprite::unloadDC() {
	delete bm[SPRITE_SIZE_16x16];
	delete bm[SPRITE_SIZE_32x32];
	bm[SPRITE_SIZE_16x16] = nullptr;
	bm[SPRITE_SIZE_32x32] = nullptr;
}

GameSprite::GameSprite() :
	id(0),
	height(0),
	width(0),
	layers(0),
	pattern_x(0),
	pattern_y(0),
	pattern_z(0),
	sprite_phase_size(0),
	numsprites(0),
	animator(nullptr),
	draw_height(0),
	minimap_color(0) {
	m_wxMemoryDc[SPRITE_SIZE_16x16] = nullptr;
	m_wxMemoryDc[SPRITE_SIZE_32x32] = nullptr;
}

GameSprite::~GameSprite() {
	unloadDC();
	delete animator;
	animator = nullptr;
}

uint16_t GameSprite::getDrawHeight() const {
	return draw_height;
}

wxPoint GameSprite::getDrawOffset() {
	if (!isDrawOffsetLoaded && !spriteList.empty()) {
		const auto &sheet = g_spriteAppearances.getSheetBySpriteId(spriteList[0]->getHardwareID());
		if (!sheet) {
			return wxPoint(0, 0);
		}

		draw_offset.x += sheet->getSpriteSize().width - 32;
		draw_offset.y += sheet->getSpriteSize().height - 32;
		isDrawOffsetLoaded = true;
	}

	return draw_offset;
}

uint8_t GameSprite::getWidth() {
	if (width <= 0) {
		const auto &sheet = g_spriteAppearances.getSheetBySpriteId(spriteList[0]->getHardwareID(), false);
		if (sheet) {
			width = sheet->getSpriteSize().width;
			height = sheet->getSpriteSize().height;
		}
	}

	return width;
}

uint8_t GameSprite::getHeight() {
	if (height <= 0) {
		const auto &sheet = g_spriteAppearances.getSheetBySpriteId(spriteList[0]->getHardwareID(), false);
		if (sheet) {
			width = sheet->getSpriteSize().width;
			height = sheet->getSpriteSize().height;
		}
	}

	return height;
}

void GameSprite::unloadDC() {
	delete m_wxMemoryDc[SPRITE_SIZE_16x16];
	delete m_wxMemoryDc[SPRITE_SIZE_32x32];
	m_wxMemoryDc[SPRITE_SIZE_16x16] = nullptr;
	m_wxMemoryDc[SPRITE_SIZE_32x32] = nullptr;
}

uint8_t GameSprite::getMiniMapColor() const {
	return minimap_color;
}

int GameSprite::getIndex(int width, int height, int layer, int pattern_x, int pattern_y, int pattern_z, int frame) const {
	return ((((frame % this->sprite_phase_size) * this->pattern_z + pattern_z) * this->pattern_y + pattern_y) * this->pattern_x + pattern_x) * this->layers + layer;
}

GLuint GameSprite::getHardwareID(int _layer, int _count, int _pattern_x, int _pattern_y, int _pattern_z, int _frame) {
	uint32_t v;
	if (_count >= 0) {
		v = _count;
	} else {
		v = (((_frame)*pattern_y + _pattern_y) * pattern_x + _pattern_x) * layers + _layer;
	}
	if (v >= numsprites) {
		if (numsprites == 1) {
			v = 0;
		} else {
			v %= numsprites;
		}
	}
	return spriteList[v]->getHardwareID();
}

std::shared_ptr<GameSprite::OutfitImage> GameSprite::getOutfitImage(int spriteId, Direction direction, const Outfit &outfit) {
	uint32_t spriteIndex = direction * layers;
	if (layers > 1 && spriteIndex >= numsprites) {
		if (numsprites == 1) {
			spriteIndex = 0;
		} else {
			spriteIndex %= numsprites;
		}
	}

	for (auto &img : instanced_templates) {
		if (img->m_spriteId == spriteId && img->m_spriteIndex == spriteIndex) {
			const auto &outfit = img->m_outfit;
			uint32_t lookHash = outfit.lookHead << 24 | outfit.lookBody << 16 | outfit.lookLegs << 8 | outfit.lookFeet;
			if (outfit.getColorHash() == lookHash) {
				return img;
			}
		}
	}

	auto img = std::make_shared<GameSprite::OutfitImage>(this, spriteIndex, spriteId, outfit);
	instanced_templates.push_back(img);
	return img;
}

wxMemoryDC* GameSprite::getDC(SpriteSize spriteSize) {
	ASSERT(spriteSize == SPRITE_SIZE_16x16 || spriteSize == SPRITE_SIZE_32x32);

	if (!width && !height) {
		// Initialize default draw offset
		const auto &sheet = g_spriteAppearances.getSheetBySpriteId(spriteList[0]->getHardwareID(), false);
		if (sheet) {
			width = sheet->getSpriteSize().width;
			height = sheet->getSpriteSize().height;
		}
	}

	if (!m_wxMemoryDc[spriteSize]) {
		ASSERT(width >= 1 && height >= 1);

		wxImage background(getWidth(), getHeight());
		auto backgroundBmp = wxBitmap(background);
		m_wxMemoryDc[spriteSize] = new wxMemoryDC(backgroundBmp);
		m_wxMemoryDc[spriteSize]->SelectObject(wxNullBitmap);

		auto spriteId = spriteList[0]->getHardwareID();
		wxImage wxImage = g_spriteAppearances.getWxImageBySpriteId(spriteId);

		// Resize the image to rme::SpritePixels x rme::SpritePixels, if necessary
		if (getWidth() > rme::SpritePixels || getHeight() > rme::SpritePixels) {
			wxImage.Rescale(rme::SpritePixels, rme::SpritePixels);
		}

		// Create a bitmap with the sprite image
		auto bitMap = wxBitmap(wxImage);
		m_wxMemoryDc[spriteSize]->SelectObject(bitMap);
		g_gui.gfx.addSpriteToCleanup(this);
	}

	return m_wxMemoryDc[spriteSize];
}

void GameSprite::DrawTo(wxDC* dcWindow, SpriteSize spriteSize, int start_x, int start_y, int sizeWidth, int sizeHeight) {
	if (sizeWidth == -1 || sizeHeight == -1) {
		if (spriteList.size() == 0) {
			return;
		}

		const auto &sheet = g_spriteAppearances.getSheetBySpriteId(spriteList[0]->getHardwareID());
		if (!sheet) {
			return;
		}

		sizeWidth = sheet->getSpriteSize().width;
		sizeHeight = sheet->getSpriteSize().height;
	}
	wxMemoryDC* sdc = getDC(spriteSize);
	if (sdc) {
		dcWindow->Blit(start_x, start_y, sizeWidth, sizeHeight, sdc, 0, 0, wxCOPY, true);
	} else {
		const wxBrush &b = dcWindow->GetBrush();
		dcWindow->SetBrush(*wxRED_BRUSH);
		dcWindow->DrawRectangle(start_x, start_y, sizeWidth, sizeHeight);
		dcWindow->SetBrush(b);
	}
}

uint8_t* GameSprite::invertGLColors(int spriteHeight, int spriteWidth, uint8_t* rgba) {
	uint8_t* rgba_inverted = new uint8_t[spriteWidth * spriteHeight * 4];
	for (int i = 0; i < spriteWidth * spriteHeight; i++) {
		rgba_inverted[i * 4 + 0] = rgba[i * 4 + 2]; // R -> B
		rgba_inverted[i * 4 + 1] = rgba[i * 4 + 1]; // G
		rgba_inverted[i * 4 + 2] = rgba[i * 4 + 0]; // B -> R
		rgba_inverted[i * 4 + 3] = rgba[i * 4 + 3]; // A
	}

	return rgba_inverted;
}

GameSprite::Image::Image() :
	isGLLoaded(false),
	lastaccess(0) {
	////
}

GameSprite::Image::~Image() {
	unloadGLTexture(0);
}

void GameSprite::Image::createGLTexture(GLuint textureId) {
	ASSERT(!isGLLoaded);

	uint8_t* rgba = getRGBAData();
	if (!rgba) {
		return;
	}

	const auto &sheet = g_spriteAppearances.getSheetBySpriteId(textureId);
	if (!sheet) {
		return;
	}

	auto spriteWidth = sheet->getSpriteSize().width;
	auto spriteHeight = sheet->getSpriteSize().height;
	auto invertedBuffer = invertGLColors(spriteHeight, spriteWidth, rgba);

	isGLLoaded = true;
	g_gui.gfx.loaded_textures += 1;

	glBindTexture(GL_TEXTURE_2D, textureId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); // Nearest Filtering
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); // Nearest Filtering
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F); // GL_CLAMP_TO_EDGE
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F); // GL_CLAMP_TO_EDGE
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, spriteWidth, spriteHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, invertedBuffer);
}

void GameSprite::Image::unloadGLTexture(GLuint textureId) {
	isGLLoaded = false;
	g_gui.gfx.loaded_textures -= 1;
	glDeleteTextures(1, &textureId);
}

void GameSprite::Image::visit() {
	lastaccess = time(nullptr);
}

void GameSprite::Image::clean(int time) {
	if (isGLLoaded && time - lastaccess > g_settings.getInteger(Config::TEXTURE_LONGEVITY)) {
		unloadGLTexture(0);
	}
}

GameSprite::NormalImage::NormalImage() :
	id(0),
	size(0),
	m_cachedData(nullptr) {
	////
}

GameSprite::NormalImage::~NormalImage() {
	m_cachedData = nullptr;
}

void GameSprite::NormalImage::clean(int time) {
	Image::clean(time);
	// We keep dumps around for 5 seconds.
	if (time - lastaccess > 5) {
		m_cachedData = nullptr;
	}
}

uint8_t* GameSprite::NormalImage::getRGBAData() {
	if (!m_cachedData) {
		if (!g_gui.gfx.loadSpriteDump(m_cachedData, size, id)) {
			spdlog::error("[GameSprite::NormalImage::getRGBAData] - Failed when parsing sprite id {}", id);
			return nullptr;
		}
	}

	return m_cachedData;
}

GLuint GameSprite::NormalImage::getHardwareID() {
	if (!isGLLoaded) {
		createGLTexture(id);
	}
	visit();
	return id;
}

void GameSprite::NormalImage::createGLTexture(GLuint) {
	Image::createGLTexture(id);
}

void GameSprite::NormalImage::unloadGLTexture(GLuint) {
	Image::unloadGLTexture(id);
}

GameSprite::EditorImage::EditorImage(const wxArtID &bitmapId) :
	NormalImage(),
	bitmapId(bitmapId) { }

void GameSprite::EditorImage::createGLTexture(GLuint textureId) {
	ASSERT(!isGLLoaded);

	wxSize size(rme::SpritePixels, rme::SpritePixels);
	wxBitmap bitmap = wxArtProvider::GetBitmap(bitmapId, wxART_OTHER, size);

	wxNativePixelData data(bitmap);
	if (!data) {
		return;
	}

	const int imageSize = rme::SpritePixelsSize * 4;
	GLubyte* imageData = new GLubyte[imageSize];
	int write = 0;

	wxNativePixelData::Iterator it(data);
	it.Offset(data, 0, 0);

	for (size_t y = 0; y < rme::SpritePixels; ++y) {
		wxNativePixelData::Iterator row_start = it;

		for (size_t x = 0; x < rme::SpritePixels; ++x, it++) {
			uint8_t red = it.Red();
			uint8_t green = it.Green();
			uint8_t blue = it.Blue();
			bool transparent = red == 0xFF && green == 0x00 && blue == 0xFF;

			imageData[write + 0] = red;
			imageData[write + 1] = green;
			imageData[write + 2] = blue;
			imageData[write + 3] = transparent ? 0x00 : 0xFF;
			write += 4;
		}

		it = row_start;
		it.OffsetY(data, 1);
	}

	isGLLoaded = true;
	id = g_gui.gfx.getFreeTextureID();
	g_gui.gfx.loaded_textures += 1;

	glBindTexture(GL_TEXTURE_2D, id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); // Nearest Filtering
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); // Nearest Filtering
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F); // GL_CLAMP_TO_EDGE
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F); // GL_CLAMP_TO_EDGE
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rme::SpritePixels, rme::SpritePixels, 0, GL_RGBA, GL_UNSIGNED_BYTE, imageData);
	delete[] imageData;
}

void GameSprite::EditorImage::unloadGLTexture(GLuint textureId) {
	Image::unloadGLTexture(id);
}

// OutfitImage
GameSprite::OutfitImage::OutfitImage(GameSprite* initParent, int initSpriteIndex, GLuint initSpriteId, const Outfit &initOutfit) :
	m_spriteId(initSpriteId),
	m_spriteIndex(initSpriteIndex),
	m_parent(initParent),
	m_outfit(initOutfit) { }

GameSprite::OutfitImage::~OutfitImage() {
	m_cachedOutfitData = nullptr;
}

void GameSprite::OutfitImage::unloadGLTexture(GLuint) {
	Image::unloadGLTexture(m_spriteId);
}

void GameSprite::OutfitImage::colorizePixel(uint8_t color, uint8_t &red, uint8_t &green, uint8_t &blue) {
	uint8_t ro = (TemplateOutfitLookupTable[color] & 0xFF0000) >> 16; // rgb outfit
	uint8_t go = (TemplateOutfitLookupTable[color] & 0xFF00) >> 8;
	uint8_t bo = (TemplateOutfitLookupTable[color] & 0xFF);

	red = (uint8_t)(red * ((float)ro / 255.f));
	green = (uint8_t)(green * ((float)go / 255.f));
	blue = (uint8_t)(blue * ((float)bo / 255.f));
}

uint8_t* GameSprite::OutfitImage::getRGBAData() {
	if (m_cachedOutfitData) {
		return m_cachedOutfitData;
	}

	const auto &sprite = g_spriteAppearances.getSprite(m_parent->spriteList[m_spriteIndex]->getHardwareID());
	if (!sprite) {
		return nullptr;
	}

	const auto offBounds = m_parent->spriteList.size() <= m_spriteIndex + 1;
	const auto templateIndex = offBounds ? m_spriteIndex : m_spriteIndex + 1;
	const auto &spriteTemplate = g_spriteAppearances.getSprite(m_parent->spriteList[templateIndex]->getHardwareID());
	if (!spriteTemplate) {
		return nullptr;
	}

	uint8_t* rgbadata = sprite->pixels.data();
	uint8_t* template_rgbadata = spriteTemplate->pixels.data();

	if (m_outfit.lookHead > (sizeof(TemplateOutfitLookupTable) / sizeof(TemplateOutfitLookupTable[0]))) {
		m_outfit.lookHead = 0;
	}
	if (m_outfit.lookBody > (sizeof(TemplateOutfitLookupTable) / sizeof(TemplateOutfitLookupTable[0]))) {
		m_outfit.lookBody = 0;
	}
	if (m_outfit.lookLegs > (sizeof(TemplateOutfitLookupTable) / sizeof(TemplateOutfitLookupTable[0]))) {
		m_outfit.lookLegs = 0;
	}
	if (m_outfit.lookFeet > (sizeof(TemplateOutfitLookupTable) / sizeof(TemplateOutfitLookupTable[0]))) {
		m_outfit.lookFeet = 0;
	}

	int height = sprite->size.height;
	int width = sprite->size.width;
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const int index = (y * width + x) * 4;
			uint8_t &red = rgbadata[index + 2];
			uint8_t &green = rgbadata[index + 1];
			uint8_t &blue = rgbadata[index];

			const uint8_t &tred = template_rgbadata[index + 2];
			const uint8_t &tgreen = template_rgbadata[index + 1];
			const uint8_t &tblue = template_rgbadata[index];

			if (tred && tgreen && !tblue) { // yellow => head
				colorizePixel(m_outfit.lookHead, red, green, blue);
			} else if (tred && !tgreen && !tblue) { // red => body
				colorizePixel(m_outfit.lookBody, red, green, blue);
			} else if (!tred && tgreen && !tblue) { // green => legs
				colorizePixel(m_outfit.lookLegs, red, green, blue);
			} else if (!tred && !tgreen && tblue) { // blue => feet
				colorizePixel(m_outfit.lookFeet, red, green, blue);
			}
		}
	}

	spdlog::debug("outfit name: {}, pattern_x: {}, pattern_y: {}, pattern_z: {}, sprite_phase_size: {}, layers: {}, draw height: {}, drawx: {}, drawy: {}", m_outfit.name, m_parent->pattern_x, m_parent->pattern_y, m_parent->pattern_z, m_parent->sprite_phase_size, m_parent->layers, m_parent->draw_height, m_parent->getDrawOffset().x, m_parent->getDrawOffset().y);

	m_cachedOutfitData = rgbadata;
	return m_cachedOutfitData;
}

GLuint GameSprite::OutfitImage::getHardwareID() {
	if (!m_isGLLoaded) {
		if (m_textureId == 0) {
			m_textureId = g_gui.gfx.getFreeTextureID();
		}
		createGLTexture(m_spriteId, m_textureId);
		if (!m_isGLLoaded) {
			return 0;
		}
	}

	return m_textureId;
}

void GameSprite::OutfitImage::createGLTexture(GLuint spriteId, GLuint textureId) {
	ASSERT(!m_isGLLoaded);

	uint8_t* rgba = getRGBAData();
	if (!rgba) {
		return;
	}

	const auto &sheet = g_spriteAppearances.getSheetBySpriteId(spriteId);
	if (!sheet) {
		return;
	}

	auto spriteWidth = sheet->getSpriteSize().width;
	auto spriteHeight = sheet->getSpriteSize().height;
	auto invertedBuffer = m_parent->invertGLColors(spriteHeight, spriteWidth, rgba);

	m_isGLLoaded = true;
	g_gui.gfx.loaded_textures += 1;

	glBindTexture(GL_TEXTURE_2D, textureId > 0 ? textureId : spriteId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); // Nearest Filtering
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); // Nearest Filtering
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F); // GL_CLAMP_TO_EDGE
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F); // GL_CLAMP_TO_EDGE
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, spriteWidth, spriteHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, invertedBuffer);
}

GameSprite* GameSprite::createFromBitmap(const wxArtID &bitmapId) {
	GameSprite::EditorImage* image = new GameSprite::EditorImage(bitmapId);

	GameSprite* sprite = new GameSprite();
	sprite->width = 1;
	sprite->height = 1;
	sprite->layers = 1;
	sprite->pattern_x = 1;
	sprite->pattern_y = 1;
	sprite->pattern_z = 1;
	sprite->sprite_phase_size = 1;
	sprite->numsprites = 1;
	sprite->spriteList.push_back(image);
	return sprite;
}

// ============================================================================
// Animator

Animator::Animator(int frame_count, int start_frame, int loop_count, bool async) :
	frame_count(frame_count),
	start_frame(start_frame),
	loop_count(loop_count),
	async(async),
	current_frame(0),
	current_loop(0),
	current_duration(0),
	total_duration(0),
	direction(ANIMATION_FORWARD),
	last_time(0),
	is_complete(false) {
	ASSERT(start_frame >= -1 && start_frame < frame_count);

	for (int i = 0; i < frame_count; i++) {
		durations.push_back(newd FrameDuration(ITEM_FRAME_DURATION, ITEM_FRAME_DURATION));
	}

	reset();
}

Animator::~Animator() {
	for (int i = 0; i < frame_count; i++) {
		delete durations[i];
		durations[i] = nullptr;
	}
	durations.clear();
}

int Animator::getStartFrame() const {
	if (start_frame > -1) {
		return start_frame;
	}
	return uniform_random(0, frame_count - 1);
}

FrameDuration* Animator::getFrameDuration(int frame) {
	ASSERT(frame >= 0 && frame < frame_count);
	return durations[frame];
}

int Animator::getFrame() {
	long time = g_gui.gfx.getElapsedTime();
	if (time != last_time && !is_complete) {
		long elapsed = time - last_time;
		if (elapsed >= current_duration) {
			int frame = 0;
			if (loop_count < 0) {
				frame = getPingPongFrame();
			} else {
				frame = getLoopFrame();
			}

			if (current_frame != frame) {
				int duration = getDuration(frame) - (elapsed - current_duration);
				if (duration < 0 && !async) {
					calculateSynchronous();
				} else {
					current_frame = frame;
					current_duration = std::max<int>(0, duration);
				}
			} else {
				is_complete = true;
			}
		} else {
			current_duration -= elapsed;
		}

		last_time = time;
	}
	return current_frame;
}

void Animator::setFrame(int frame) {
	ASSERT(frame == -1 || frame == 255 || frame == 254 || (frame >= 0 && frame < frame_count));

	if (current_frame == frame) {
		return;
	}

	if (async) {
		if (frame == 255) { // Async mode
			current_frame = 0;
		} else if (frame == 254) { // Random mode
			current_frame = uniform_random(0, frame_count - 1);
		} else if (frame >= 0 && frame < frame_count) {
			current_frame = frame;
		} else {
			current_frame = getStartFrame();
		}

		is_complete = false;
		last_time = g_gui.gfx.getElapsedTime();
		current_duration = getDuration(current_frame);
		current_loop = 0;
	} else {
		calculateSynchronous();
	}
}

void Animator::reset() {
	total_duration = 0;
	for (int i = 0; i < frame_count; i++) {
		total_duration += durations[i]->max;
	}

	is_complete = false;
	direction = ANIMATION_FORWARD;
	current_loop = 0;
	async = false;
	setFrame(-1);
}

int Animator::getDuration(int frame) const {
	ASSERT(frame >= 0 && frame < frame_count);
	return durations[frame]->getDuration();
}

int Animator::getPingPongFrame() {
	int count = direction == ANIMATION_FORWARD ? 1 : -1;
	int next_frame = current_frame + count;
	if (next_frame < 0 || next_frame >= frame_count) {
		direction = direction == ANIMATION_FORWARD ? ANIMATION_BACKWARD : ANIMATION_FORWARD;
		count *= -1;
	}
	return current_frame + count;
}

int Animator::getLoopFrame() {
	int next_phase = current_frame + 1;
	if (next_phase < frame_count) {
		return next_phase;
	}

	if (loop_count == 0) {
		return 0;
	}

	if (current_loop < (loop_count - 1)) {
		current_loop++;
		return 0;
	}
	return current_frame;
}

void Animator::calculateSynchronous() {
	long time = g_gui.gfx.getElapsedTime();
	if (time > 0 && total_duration > 0) {
		long elapsed = time % total_duration;
		int total_time = 0;
		for (int i = 0; i < frame_count; i++) {
			int duration = getDuration(i);
			if (elapsed >= total_time && elapsed < total_time + duration) {
				current_frame = i;
				current_duration = duration - (elapsed - total_time);
				break;
			}
			total_time += duration;
		}
		last_time = time;
	}
}
