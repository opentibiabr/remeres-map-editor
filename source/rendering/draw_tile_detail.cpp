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

#ifdef __WINDOWS__
	#include <windows.h>
	#include <psapi.h>
	#pragma comment(lib, "psapi.lib")
#else
	#include <unistd.h>
	#include <cstring>
#endif

#include <cstdint>
#include <thread>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <format>
#include <array>

#include "editor/editor.h"
#include "ui/gui.h" // KNOWN_VIOLATION
#include "rendering/sprites.h"
#include "rendering/map_drawer.h"
#include <unordered_set>
#include "rendering/map_display.h"
#include "editor/copybuffer.h"
#include "live/live_socket.h"
#include "rendering/graphics.h"

#include "brushes/doodad_brush.h" // KNOWN_VIOLATION
#include "brushes/monster_brush.h" // KNOWN_VIOLATION
#include "brushes/house_exit_brush.h" // KNOWN_VIOLATION
#include "brushes/house_brush.h" // KNOWN_VIOLATION
#include "brushes/spawn_monster_brush.h" // KNOWN_VIOLATION
#include "rendering/sprite_appearances.h"
#include "brushes/npc_brush.h" // KNOWN_VIOLATION
#include "brushes/spawn_npc_brush.h" // KNOWN_VIOLATION
#include "brushes/wall_brush.h" // KNOWN_VIOLATION
#include "brushes/carpet_brush.h" // KNOWN_VIOLATION
#include "brushes/raw_brush.h" // KNOWN_VIOLATION
#include "brushes/table_brush.h" // KNOWN_VIOLATION
#include "brushes/waypoint_brush.h" // KNOWN_VIOLATION
#include "brushes/zone_brush.h" // KNOWN_VIOLATION
#include "rendering/light_drawer.h"
#include "rendering/gl_renderer.h"

void MapDrawer::BlitItem(int &draw_x, int &draw_y, const Tile* tile, const Item* item, bool ephemeral, int red, int green, int blue, int alpha) {
	const ItemType &type = g_items.getItemType(item->getID());
	if (type.id == 0) {
		glBlitSquare(draw_x, draw_y, *wxRED);
		return;
	}

	if (!options.ingame && !ephemeral && item->isSelected()) {
		red /= 2;
		blue /= 2;
		green /= 2;
	}

	// Ugly hacks. :)
	if (type.id == ITEM_STAIRS && !options.ingame) {
		glBlitSquare(draw_x, draw_y, red, green, 0, alpha / 3 * 2);
		return;
	} else if ((type.id == ITEM_NOTHING_SPECIAL || type.id == 2187) && !options.ingame) {
		glBlitSquare(draw_x, draw_y, red, 0, 0, alpha / 3 * 2);
		return;
	}

	if (type.isMetaItem()) {
		return;
	}
	if (!ephemeral && type.pickupable && !options.show_items) {
		return;
	}
	GameSprite* sprite = type.sprite;
	if (!sprite) {
		return;
	}

	int screenx = draw_x - sprite->getDrawOffset().x;
	int screeny = draw_y - sprite->getDrawOffset().y;

	const Position &pos = tile->getPosition();

	// Set the newd drawing height accordingly
	draw_x -= sprite->getDrawHeight();
	draw_y -= sprite->getDrawHeight();

	int subtype = -1;

	int pattern_x = pos.x % sprite->pattern_x;
	int pattern_y = pos.y % sprite->pattern_y;
	int pattern_z = pos.z % sprite->pattern_z;

	if (type.isSplash() || type.isFluidContainer()) {
		subtype = Item::liquidSubTypeToSpriteSubType(item->getSubtype());
	} else if (type.isHangable) {
		if (tile->hasProperty(HOOK_SOUTH)) {
			pattern_x = 1;
		} else if (tile->hasProperty(HOOK_EAST)) {
			pattern_x = 2;
		} else {
			pattern_x = 0;
		}
	} else if (type.stackable) {
		if (item->getSubtype() <= 1) {
			subtype = 0;
		} else if (item->getSubtype() <= 2) {
			subtype = 1;
		} else if (item->getSubtype() <= 3) {
			subtype = 2;
		} else if (item->getSubtype() <= 4) {
			subtype = 3;
		} else if (item->getSubtype() < 10) {
			subtype = 4;
		} else if (item->getSubtype() < 25) {
			subtype = 5;
		} else if (item->getSubtype() < 50) {
			subtype = 6;
		} else {
			subtype = 7;
		}
	}

	if (!ephemeral && options.transparent_items && (!type.isGroundTile() || sprite->getWidth() > 1 || sprite->getHeight() > 1) && !type.isSplash() && (!type.isBorder || sprite->getWidth() > 1 || sprite->getHeight() > 1)) {
		alpha /= 2;
	}

	int frame = item->getFrame();
	int texnum = sprite->getHardwareID(0, subtype, pattern_x, pattern_y, pattern_z, frame);
	int sprId = sprite->getSpriteID(0, subtype, pattern_x, pattern_y, pattern_z, frame);
	auto uvs = sprite->getAtlasUVs(0, subtype, pattern_x, pattern_y, pattern_z, frame);
	glBlitTexture(screenx, screeny, texnum, GLColor { uint8_t(red), uint8_t(green), uint8_t(blue), uint8_t(alpha) }, BlitOptions { .spriteId = sprId, .uv = uvs });

	if (options.show_hooks && (type.hookSouth || type.hookEast || type.hook != ITEM_HOOK_NONE)) {
		DrawHookIndicator(draw_x, draw_y, type);
	}

	if (!options.ingame && options.show_light_strength) {
		DrawLightStrength(draw_x, draw_y, item);
	}
}

void MapDrawer::BlitItem(int &draw_x, int &draw_y, const Position &pos, const Item* item, bool ephemeral, int red, int green, int blue, int alpha) {
	const ItemType &type = g_items.getItemType(item->getID());
	if (type.id == 0) {
		return;
	}

	if (!options.ingame && !ephemeral && item->isSelected()) {
		red /= 2;
		blue /= 2;
		green /= 2;
	}

	if (type.id == ITEM_STAIRS && !options.ingame) { // Ugly hack yes?
		glBlitSquare(draw_x, draw_y, red, green, 0, alpha / 3 * 2);
		return;
	} else if ((type.id == ITEM_NOTHING_SPECIAL || type.id == 2187) && !options.ingame) { // Ugly hack yes? // Beautiful!
		glBlitSquare(draw_x, draw_y, red, 0, 0, alpha / 3 * 2);
		return;
	}

	if (type.isMetaItem()) {
		return;
	}
	if (!ephemeral && type.pickupable && options.show_items) {
		return;
	}
	GameSprite* sprite = type.sprite;
	if (!sprite) {
		return;
	}

	int screenx = draw_x - sprite->getDrawOffset().x;
	int screeny = draw_y - sprite->getDrawOffset().y;

	// Set the newd drawing height accordingly
	draw_x -= sprite->getDrawHeight();
	draw_y -= sprite->getDrawHeight();

	int subtype = -1;

	int pattern_x = pos.x % sprite->pattern_x;
	int pattern_y = pos.y % sprite->pattern_y;
	int pattern_z = pos.z % sprite->pattern_z;

	if (type.isSplash() || type.isFluidContainer()) {
		subtype = item->getSubtype();
	} else if (type.isHangable) {
		pattern_x = 0;
		/*
		if(tile->hasProperty(HOOK_SOUTH)) {
			pattern_x = 2;
		} else if(tile->hasProperty(HOOK_EAST)) {
			pattern_x = 1;
		} else {
			pattern_x = -0;
		}
		*/
	} else if (type.stackable) {
		if (item->getSubtype() <= 1) {
			subtype = 0;
		} else if (item->getSubtype() <= 2) {
			subtype = 1;
		} else if (item->getSubtype() <= 3) {
			subtype = 2;
		} else if (item->getSubtype() <= 4) {
			subtype = 3;
		} else if (item->getSubtype() < 10) {
			subtype = 4;
		} else if (item->getSubtype() < 25) {
			subtype = 5;
		} else if (item->getSubtype() < 50) {
			subtype = 6;
		} else {
			subtype = 7;
		}
	}

	if (!ephemeral && options.transparent_items && (!type.isGroundTile() || sprite->getWidth() > 1 || sprite->getHeight() > 1) && !type.isSplash() && (!type.isBorder || sprite->getWidth() > 1 || sprite->getHeight() > 1)) {
		alpha /= 2;
	}

	int frame = item->getFrame();
	int texnum = sprite->getHardwareID(0, subtype, pattern_x, pattern_y, pattern_z, frame);
	int sprId = sprite->getSpriteID(0, subtype, pattern_x, pattern_y, pattern_z, frame);
	auto uvs = sprite->getAtlasUVs(0, subtype, pattern_x, pattern_y, pattern_z, frame);
	glBlitTexture(screenx, screeny, texnum, GLColor { uint8_t(red), uint8_t(green), uint8_t(blue), uint8_t(alpha) }, BlitOptions { .spriteId = sprId, .uv = uvs });

	if (options.show_hooks && (type.hookSouth || type.hookEast) && zoom <= 3.0) {
		DrawHookIndicator(draw_x, draw_y, type);
	}

	if (!options.ingame && options.show_light_strength) {
		DrawLightStrength(draw_x, draw_y, item);
	}
}

void MapDrawer::BlitSpriteType(int screenx, int screeny, uint32_t spriteid, int red, int green, int blue, int alpha) {
	const ItemType &type = g_items.getItemType(spriteid);
	if (type.id == 0) {
		return;
	}

	GameSprite* sprite = type.sprite;
	if (!sprite) {
		return;
	}

	screenx -= sprite->getDrawOffset().x;
	screeny -= sprite->getDrawOffset().y;

	int frame = 0;
	int texnum = sprite->getHardwareID(0, -1, 0, 0, 0, 0);
	int sprId = sprite->getSpriteID(0, -1, 0, 0, 0, 0);
	auto uvs = sprite->getAtlasUVs(0, -1, 0, 0, 0, 0);
	glBlitTexture(screenx, screeny, texnum, GLColor { uint8_t(red), uint8_t(green), uint8_t(blue), uint8_t(alpha) }, BlitOptions { .spriteId = sprId, .uv = uvs });
}

void MapDrawer::BlitSpriteType(int screenx, int screeny, GameSprite* sprite, int red, int green, int blue, int alpha) {
	if (!sprite) {
		return;
	}

	screenx -= sprite->getDrawOffset().x;
	screeny -= sprite->getDrawOffset().y;

	int frame = 0;
	int texnum = sprite->getHardwareID(0, -1, 0, 0, 0, 0);
	int sprId = sprite->getSpriteID(0, -1, 0, 0, 0, 0);
	auto uvs = sprite->getAtlasUVs(0, -1, 0, 0, 0, 0);
	glBlitTexture(screenx, screeny, texnum, GLColor { uint8_t(red), uint8_t(green), uint8_t(blue), uint8_t(alpha) }, BlitOptions { .spriteId = sprId, .uv = uvs });
}

void MapDrawer::BlitCreature(int screenx, int screeny, const Outfit &outfit, const Direction &dir, int red, int green, int blue, int alpha) {
	if (outfit.lookItem != 0) {
		const ItemType &type = g_items.getItemType(outfit.lookItem);
		BlitSpriteType(screenx, screeny, type.sprite, red, green, blue, alpha);
		return;
	}

	if (outfit.lookType == 0) {
		Outfit fallback;
		fallback.lookType = 197; // This looktype is a tribute to our beloved Carl-bot from OpenTibiaBR Discord.
		BlitCreature(screenx, screeny, fallback, dir, red, green, blue, alpha);
		return;
	}

	GameSprite* spr = g_gui.gfx.getCreatureSprite(outfit.lookType);
	if (!spr || outfit.lookType == 0) {
		return;
	}

	screenx -= spr->getDrawOffset().x;
	screeny -= spr->getDrawOffset().y;

	int baseIdx = (int)dir * (int)spr->layers;
	if (baseIdx < 0 || (uint32_t)baseIdx >= spr->numsprites) {
		return;
	}
	auto spriteId = spr->spriteList[baseIdx]->id;
	auto outfitImage = spr->getOutfitImage(spriteId, baseIdx, outfit);
	if (outfitImage) {
		glBlitTexture(screenx, screeny, outfitImage->getHardwareID(), GLColor { uint8_t(red), uint8_t(green), uint8_t(blue), uint8_t(alpha) }, BlitOptions { .outfit = outfit, .spriteId = static_cast<int>(spriteId) });
	}

	for (int py = 1; py < (int)spr->pattern_y; ++py) {
		if (!(outfit.lookAddon & (1 << (py - 1)))) {
			continue;
		}
		int addonIdx = (py * (int)spr->pattern_x + (int)dir) * (int)spr->layers;
		if (addonIdx < 0 || (uint32_t)addonIdx >= spr->numsprites) {
			continue;
		}
		auto addonSpriteId = spr->spriteList[addonIdx]->id;
		auto addonImage = spr->getOutfitImage(addonSpriteId, addonIdx, outfit);
		if (addonImage) {
			glBlitTexture(screenx, screeny, addonImage->getHardwareID(), GLColor { uint8_t(red), uint8_t(green), uint8_t(blue), uint8_t(alpha) }, BlitOptions { .outfit = outfit, .spriteId = static_cast<int>(addonSpriteId) });
		}
	}
}

void MapDrawer::BlitCreature(int screenx, int screeny, const Monster* creature, int red, int green, int blue, int alpha) {
	if (!options.ingame && creature->isSelected()) {
		red /= 2;
		green /= 2;
		blue /= 2;
	}
	BlitCreature(screenx, screeny, creature->getLookType(), creature->getDirection(), red, green, blue, alpha);
}
// Npcs
void MapDrawer::BlitCreature(int screenx, int screeny, const Npc* npc, int red, int green, int blue, int alpha) {
	if (!options.ingame && npc->isSelected()) {
		red /= 2;
		green /= 2;
		blue /= 2;
	}
	BlitCreature(screenx, screeny, npc->getLookType(), npc->getDirection(), red, green, blue, alpha);
}

void MapDrawer::WriteTooltip(const Item* item, MapTooltip &tooltip) {
	if (!item) {
		return;
	}

	const uint16_t id = item->getID();
	if (id < 100) {
		return;
	}

	const uint16_t unique = item->getUniqueID();
	const uint16_t action = item->getActionID();
	const std::string &text = item->getText();
	if (unique == 0 && action == 0 && text.empty()) {
		return;
	}

	tooltip.addEntry("id: ", std::to_string(id));

	if (action > 0) {
		tooltip.addEntry("aid: ", std::to_string(action));
	}
	if (unique > 0) {
		tooltip.addEntry("uid: ", std::to_string(unique));
	}
	if (!text.empty()) {
		tooltip.addEntry("text: ", text);
	}
}

void MapDrawer::WriteTooltip(const Waypoint* waypoint, MapTooltip &tooltip) {
	tooltip.addEntry("wp: ", waypoint->name);
}

void MapDrawer::DrawTile(TileLocation* location) {
	if (!location) {
		return;
	}

	Tile* tile = location->get();
	if (!tile) {
		return;
	}

	if (options.show_only_modified && !tile->isModified()) {
		return;
	}

	const Position &position = location->getPosition();
	bool show_tooltips = options.isTooltips();

	bool has_waypoint = false;
	Waypoint* waypoint = nullptr;
	if (show_tooltips && location->getWaypointCount() > 0) {
		waypoint = canvas->editor.getMap().waypoints.getWaypoint(position);
		has_waypoint = (waypoint != nullptr);
	}

	bool only_colors = options.isOnlyColors();

	int draw_x, draw_y;
	getDrawPosition(position, draw_x, draw_y);

	uint8_t r = 255, g = 255, b = 255;
	if (only_colors || tile->hasGround()) {

		if (!options.show_as_minimap) {
			bool showspecial = options.show_only_colors || options.show_special_tiles;

			if (options.show_blocking && tile->isBlocking() && tile->size() > 0) {
				g = g / 3 * 2;
				b = b / 3 * 2;
			}

			int item_count = tile->items.size();
			if (options.highlight_items && item_count > 0 && !tile->items.back()->isBorder()) {
				static const float factor[5] = { 0.75f, 0.6f, 0.48f, 0.40f, 0.33f };
				int idx = (item_count < 5 ? item_count : 5) - 1;
				g = int(g * factor[idx]);
				r = int(r * factor[idx]);
			}

			if (options.show_spawns_monster && location->getSpawnMonsterCount() > 0) {
				float f = 1.0f;
				for (uint32_t i = 0; i < location->getSpawnMonsterCount(); ++i) {
					f *= 0.7f;
				}
				g = uint8_t(g * f);
				b = uint8_t(b * f);
			}

			if (options.show_spawns_npc && location->getSpawnNpcCount() > 0) {
				float f = 1.0f;
				for (uint32_t i = 0; i < location->getSpawnNpcCount(); ++i) {
					f *= 0.7f;
				}
				g = uint8_t(g * f);
				b = uint8_t(b * f);
			}

			if (options.show_houses && tile->isHouseTile()) {
				if ((int)tile->getHouseID() == current_house_id) {
					r /= 2;
				} else {
					r /= 2;
					g /= 2;
				}
			} else if (showspecial && tile->isPZ()) {
				r /= 2;
				b /= 2;
			}

			if (showspecial && tile->getMapFlags() & TILESTATE_PVPZONE) {
				g = r / 4;
				b = b / 3 * 2;
			}

			bool zone_active = tile->hasZone(g_gui.zone_brush->getZone());
			if (showspecial && zone_active) {
				b /= 1.3;
				r /= 1.5;
				g /= 2;
			}
			if (showspecial && ((!tile->zones.empty() && !zone_active) || tile->zones.size() > 1)) {
				r /= 1.4;
				g /= 1.6;
				b /= 1.3;
			}

			if (showspecial && tile->getMapFlags() & TILESTATE_NOLOGOUT) {
				b /= 2;
			}

			if (showspecial && tile->getMapFlags() & TILESTATE_NOPVP) {
				g /= 2;
			}
		}

		if (only_colors) {
			if (options.show_as_minimap) {
				wxColor color = colorFromEightBit(tile->getMiniMapColor());
				glBlitSquare(draw_x, draw_y, color);
			} else if (r != 255 || g != 255 || b != 255) {
				glBlitSquare(draw_x, draw_y, r, g, b, 128);
			}
		} else {
			if (options.show_preview && zoom <= 2.0) {
				tile->ground->animate();
			}

			BlitItem(draw_x, draw_y, tile, tile->ground, false, r, g, b);
		}
	}

	bool hidden = only_colors || (options.hide_items_when_zoomed && zoom > 10.f);

	if (!hidden && !tile->items.empty()) {
		for (Item* item : tile->items) {

			if (options.show_preview && zoom <= 2.0) {
				item->animate();
			}

			if (item->isBorder()) {
				BlitItem(draw_x, draw_y, tile, item, false, r, g, b);
			} else {
				BlitItem(draw_x, draw_y, tile, item);
			}
		}
	}

	if (!hidden && options.show_monsters && !tile->monsters.empty()) {
		for (auto monster : tile->monsters) {
			BlitCreature(draw_x, draw_y, monster);
		}
	}

	if (!hidden && options.show_npcs && tile->npc) {
		BlitCreature(draw_x, draw_y, tile->npc);
	}

	if (show_tooltips && position.z == floor) {
		uint8_t tr = 255;
		uint8_t tg = 255;
		uint8_t tb = 255;
		if (has_waypoint) {
			tr = 0;
			tg = 255;
			tb = 0;
		}
		auto &tip = MakeTooltip(position.x, position.y, position.z, tr, tg, tb);

		if (has_waypoint) {
			WriteTooltip(waypoint, tip);
		}

		if (tile->hasGround()) {
			WriteTooltip(tile->ground, tip);
		}

		if (!tile->items.empty()) {
			for (Item* item : tile->items) {
				WriteTooltip(item, tip);
			}
		}

		if (tip.entries.empty()) {
			tooltips.pop_back();
		}
	}
}

void MapDrawer::DrawBrushIndicator(int x, int y, [[maybe_unused]] Brush* brush, uint8_t r, uint8_t g, uint8_t b) {
	x += (rme::TileSize / 2);
	y += (rme::TileSize / 2);

	// 7----0----1
	// |         |
	// 6--5  3--2
	//     \/
	//     4
	static int vertexes[9][2] = {
		{ -15, -20 }, // 0
		{ 15, -20 }, // 1
		{ 15, -5 }, // 2
		{ 5, -5 }, // 3
		{ 0, 0 }, // 4
		{ -5, -5 }, // 5
		{ -15, -5 }, // 6
		{ -15, -20 }, // 7
		{ -15, -20 }, // 0
	};

	// circle
	std::array<float, 64> fan; // (1 center + 31 rim) * 2 floats
	fan[0] = static_cast<float>(x);
	fan[1] = static_cast<float>(y);
	for (int i = 0; i <= 30; i++) {
		float angle = i * 2.0f * rme::PI / 30;
		fan[(i + 1) * 2] = cos(angle) * (rme::TileSize / 2) + x;
		fan[(i + 1) * 2 + 1] = sin(angle) * (rme::TileSize / 2) + y;
	}
	renderer->drawTriangleFan(fan.data(), 32, 0x00, 0x00, 0x00, 0x50);

	// background
	std::array<float, 16> poly;
	for (int i = 0; i < 8; ++i) {
		poly[i * 2] = static_cast<float>(vertexes[i][0] + x);
		poly[i * 2 + 1] = static_cast<float>(vertexes[i][1] + y);
	}
	renderer->drawPolygon(poly.data(), 8, r, g, b, 0xB4);

	// borders
	std::array<float, 32> lineVerts; // 8 pairs * 4 floats
	for (int i = 0; i < 8; ++i) {
		lineVerts[i * 4] = static_cast<float>(vertexes[i][0] + x);
		lineVerts[i * 4 + 1] = static_cast<float>(vertexes[i][1] + y);
		lineVerts[i * 4 + 2] = static_cast<float>(vertexes[i + 1][0] + x);
		lineVerts[i * 4 + 3] = static_cast<float>(vertexes[i + 1][1] + y);
	}
	renderer->drawLines(lineVerts.data(), 8, 0x00, 0x00, 0x00, 0xB4, 1.0f);
}

void MapDrawer::DrawHookIndicator(int x, int y, const ItemType &type) {
	if (type.hookSouth || type.hook == ITEM_HOOK_SOUTH) {
		x -= 10;
		y += 10;
		std::array<float, 8> verts = { (float)x, (float)y, (float)(x + 10), (float)y, (float)(x + 20), (float)(y + 10), (float)(x + 10), (float)(y + 10) };
		renderer->drawPolygon(verts.data(), 4, 0, 0, 255, 200);
	} else if (type.hookEast || type.hook == ITEM_HOOK_EAST) {
		x += 10;
		y -= 10;
		std::array<float, 8> verts = { (float)x, (float)y, (float)(x + 10), (float)(y + 10), (float)(x + 10), (float)(y + 20), (float)x, (float)(y + 10) };
		renderer->drawPolygon(verts.data(), 4, 0, 0, 255, 200);
	}
}

void MapDrawer::DrawLightStrength(int x, int y, const Item*&item) {
	const SpriteLight &light = item->getLight();

	if (light.intensity <= 0) {
		return;
	}

	wxColor lightColor = colorFromEightBit(light.color);
	const uint8_t byteR = lightColor.Red();
	const uint8_t byteG = lightColor.Green();
	const uint8_t byteB = lightColor.Blue();
	constexpr uint8_t byteA = 255;

	const int startOffset = std::max<int>(16, 32 - light.intensity);
	const int sqSize = rme::TileSize - startOffset;
	glBlitSquare(x + startOffset - 2, y + startOffset - 2, 0, 0, 0, byteA, sqSize + 2);
	glBlitSquare(x + startOffset - 1, y + startOffset - 1, byteR, byteG, byteB, byteA, sqSize);
}

void MapDrawer::DrawTileIndicators(TileLocation* location) {
	if (!location) {
		return;
	}

	Tile* tile = location->get();
	if (!tile) {
		return;
	}

	int x, y;
	getDrawPosition(location->getPosition(), x, y);

	if (zoom < 10.0 && (options.show_pickupables || options.show_moveables || options.show_avoidables)) {
		uint8_t red = 0xFF, green = 0xFF, blue = 0xFF;
		if (tile->isHouseTile()) {
			green = 0x00;
			blue = 0x00;
		}

		for (const Item* item : tile->items) {
			const ItemType &type = g_items.getItemType(item->getID());
			if ((type.pickupable && options.show_pickupables) || (type.moveable && options.show_moveables)) {
				if (type.pickupable && options.show_pickupables && type.moveable && options.show_moveables) {
					DrawIndicator(x, y, EDITOR_SPRITE_PICKUPABLE_MOVEABLE_ITEM, red, green, blue);
				} else if (type.pickupable && options.show_pickupables) {
					DrawIndicator(x, y, EDITOR_SPRITE_PICKUPABLE_ITEM, red, green, blue);
				} else if (type.moveable && options.show_moveables) {
					DrawIndicator(x, y, EDITOR_SPRITE_MOVEABLE_ITEM, red, green, blue);
				}
			}

			if (type.blockPathfinder && options.show_avoidables) {
				DrawIndicator(x, y, EDITOR_SPRITE_AVOIDABLE_ITEM, red, green, blue);
			}
		}
	}

	if (options.show_avoidables && tile->ground && tile->ground->isAvoidable()) {
		DrawIndicator(x, y, EDITOR_SPRITE_AVOIDABLE_ITEM);
	}

	if (options.show_houses && tile->isHouseExit()) {
		if (tile->hasHouseExit(current_house_id)) {
			DrawIndicator(x, y, EDITOR_SPRITE_HOUSE_EXIT);
		} else {
			DrawIndicator(x, y, EDITOR_SPRITE_HOUSE_EXIT, 64, 64, 255, 128);
		}
	}

	if (options.show_spawns_monster && tile->spawnMonster) {
		if (tile->spawnMonster->isSelected()) {
			DrawIndicator(x, y, EDITOR_SPRITE_MONSTERS, 128, 128, 128);
		} else {
			DrawIndicator(x, y, EDITOR_SPRITE_MONSTERS);
		}
	}

	if (tile->spawnNpc && options.show_spawns_npc) {
		if (tile->spawnNpc->isSelected()) {
			DrawIndicator(x, y, EDITOR_SPRITE_NPCS, 128, 128, 128);
		} else {
			DrawIndicator(x, y, EDITOR_SPRITE_NPCS, 255, 255, 255);
		}
	}
}

void MapDrawer::DrawIndicator(int x, int y, int indicator, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	GameSprite* sprite = g_gui.gfx.getEditorSprite(indicator);
	if (sprite == nullptr) {
		spdlog::error("MapDrawer::DrawIndicator: sprite is nullptr");
		return;
	}

	int textureId = sprite->getHardwareID(0, 0, 0, -1, 0, 0);
	glBlitTexture(x, y, textureId, GLColor { r, g, b, a }, BlitOptions { .adjustZoom = true, .isEditorSprite = true });
}

void MapDrawer::DrawPositionIndicator(int z) {
	if (z != pos_indicator.z || pos_indicator.x < start_x || pos_indicator.x > end_x || pos_indicator.y < start_y || pos_indicator.y > end_y) {
		return;
	}

	const long time = GetPositionIndicatorTime();
	if (time == 0) {
		return;
	}

	int x, y;
	getDrawPosition(pos_indicator, x, y);

	int size = static_cast<int>(rme::TileSize * (0.3f + std::abs(500 - time % 1000) / 1000.f));
	int offset = (rme::TileSize - size) / 2;

	drawRect(x + offset + 2, y + offset + 2, size - 4, size - 4, *wxWHITE, 2);
	drawRect(x + offset + 1, y + offset + 1, size - 2, size - 2, *wxBLACK, 2);
}
