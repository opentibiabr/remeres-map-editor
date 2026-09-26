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

#include "mcp_tools.h"

#include "../basemap.h"
#include "../common.h"
#include "../const.h"
#include "../editor.h"
#include "../graphics.h"
#include "../item.h"
#include "../items.h"
#include "../map.h"
#include "../position.h"
#include "../sprite_appearances.h"
#include "../tile.h"

#include <wx/base64.h>
#include <wx/image.h>
#include <wx/mstream.h>

#include <algorithm>
#include <map>
#include <vector>

namespace mcp {

	namespace {

		// Largest PNG we will hand back, in pixels. A 2048x2048 image is around
		// a megabyte of base64 - past that the model pays more than it learns.
		constexpr int MAX_IMAGE_DIMENSION = 2048;

		json toolRenderRegion(const json &params) {
			Editor* editor = requireEditor();
			Map &map = editor->getMap();

			if (!params.contains("from") || !params.contains("to")) {
				throw McpError("map_render_region requires the from and to fields, each {\"x\":..,\"y\":..,\"z\":..}");
			}
			const Position from = parsePosition(params["from"], "from");
			const Position to = parsePosition(params["to"], "to");

			if (from.z != to.z) {
				throw McpError("map_render_region draws a single floor: from.z and to.z must match");
			}

			const int minX = std::min(from.x, to.x);
			const int maxX = std::max(from.x, to.x);
			const int minY = std::min(from.y, to.y);
			const int maxY = std::max(from.y, to.y);
			const int z = from.z;

			const int tilesWide = maxX - minX + 1;
			const int tilesHigh = maxY - minY + 1;

			const std::string mode = readString(params, "mode", "minimap");
			if (mode != "minimap" && mode != "sprites") {
				throw McpError("mode must be minimap or sprites");
			}

			if (mode == "sprites") {
				// Real client sprites, composited offscreen - independent of
				// the editor window, unlike map_screenshot.
				const int pixelsPerTile = readInt(params, "pixelsPerTile", 32, 4, 64);
				if (static_cast<int64_t>(tilesWide) * pixelsPerTile > MAX_IMAGE_DIMENSION || static_cast<int64_t>(tilesHigh) * pixelsPerTile > MAX_IMAGE_DIMENSION) {
					throw McpError(fmt::format(
						"a {}x{} tile region at {} pixels per tile exceeds the {}px image limit; lower pixelsPerTile or narrow the region",
						tilesWide, tilesHigh, pixelsPerTile, MAX_IMAGE_DIMENSION
					));
				}
				const wxImage rendered = renderTileRegion(map, Position(minX, minY, z), tilesWide, tilesHigh, pixelsPerTile);
				if (!rendered.IsOk()) {
					throw McpError("could not render the region; are the client sprites loaded?");
				}
				return imageResult(encodePngBase64(rendered), "image/png");
			}

			const int scale = readInt(params, "scale", 4, 1, 32);
			if (static_cast<int64_t>(tilesWide) * scale > MAX_IMAGE_DIMENSION || static_cast<int64_t>(tilesHigh) * scale > MAX_IMAGE_DIMENSION) {
				throw McpError(fmt::format(
					"a {}x{} tile region at scale {} exceeds the {}px image limit; lower scale or narrow the region",
					tilesWide, tilesHigh, scale, MAX_IMAGE_DIMENSION
				));
			}

			wxImage image(tilesWide * scale, tilesHigh * scale);
			unsigned char* rgb = image.GetData();

			// Empty tiles stay black so the drawn area reads as a silhouette.
			std::fill_n(rgb, static_cast<size_t>(image.GetWidth()) * image.GetHeight() * 3, 0);

			const int rowStride = image.GetWidth() * 3;
			int renderedTiles = 0;

			for (int y = minY; y <= maxY; ++y) {
				for (int x = minX; x <= maxX; ++x) {
					const Tile* tile = map.getTile(x, y, z);
					if (!tile || tile->empty()) {
						continue;
					}
					const uint8_t colorIndex = tile->getMiniMapColor();
					if (colorIndex == INVALID_MINIMAP_COLOR) {
						continue;
					}

					++renderedTiles;
					// Same palette the editor's minimap paints with, so the
					// image matches what the user sees on screen.
					const wxColor color = colorFromEightBit(colorIndex);
					const unsigned char r = color.Red();
					const unsigned char g = color.Green();
					const unsigned char b = color.Blue();

					const int pixelX = (x - minX) * scale;
					const int pixelY = (y - minY) * scale;
					for (int dy = 0; dy < scale; ++dy) {
						unsigned char* row = rgb + static_cast<size_t>(pixelY + dy) * rowStride + static_cast<size_t>(pixelX) * 3;
						for (int dx = 0; dx < scale; ++dx) {
							*row++ = r;
							*row++ = g;
							*row++ = b;
						}
					}
				}
			}

			return imageResult(encodePngBase64(image), "image/png");
		}

	} // namespace

	// ponytail: draws each item's base sprite (layer 0, frame 0) only - no
	// animation phase, pattern variation or per-layer compositing. That covers
	// ground, borders and walls, which is what region and brush previews are
	// for. The full-fidelity reference is buildCyclopediaSatelliteChunk in
	// iomap_otbm.cpp; reach for that if previews ever need to match the
	// exporter exactly.
	wxImage renderTileRegion(BaseMap &map, const Position &origin, int width, int height, int pixelsPerTile) {
		if (width <= 0 || height <= 0) {
			return wxImage();
		}

		constexpr int TILE = rme::SpritePixels;
		const int canvasWidth = width * TILE;
		const int canvasHeight = height * TILE;

		wxImage canvas(canvasWidth, canvasHeight);
		canvas.InitAlpha();
		std::fill_n(canvas.GetData(), static_cast<size_t>(canvasWidth) * canvasHeight * 3, 0);
		std::fill_n(canvas.GetAlpha(), static_cast<size_t>(canvasWidth) * canvasHeight, 0);

		// Decoding a sprite sheet is expensive and a region repeats the same
		// grass tile thousands of times.
		std::map<uint32_t, wxImage> spriteCache;

		auto spriteFor = [&spriteCache](const Item* item) -> const wxImage* {
			if (!item) {
				return nullptr;
			}
			const ItemType &type = item->getItemType();
			if (!type.sprite) {
				return nullptr;
			}
			// getSpriteID is the asset identity; getHardwareID is a GL texture
			// id and must never be used here (docs/static-data.md rule 9).
			const uint32_t spriteId = type.sprite->getSpriteID(0, 0, 0, 0, 0, 0);
			if (spriteId == 0) {
				return nullptr;
			}

			auto cached = spriteCache.find(spriteId);
			if (cached == spriteCache.end()) {
				wxImage image = g_spriteAppearances.getWxImageBySpriteId(static_cast<int>(spriteId), true);
				cached = spriteCache.emplace(spriteId, std::move(image)).first;
			}
			return cached->second.IsOk() ? &cached->second : nullptr;
		};

		unsigned char* dstRgb = canvas.GetData();
		unsigned char* dstAlpha = canvas.GetAlpha();

		auto blit = [&](const wxImage &sprite, int left, int top) {
			const int sw = sprite.GetWidth();
			const int sh = sprite.GetHeight();
			const unsigned char* srcRgb = sprite.GetData();
			const unsigned char* srcAlpha = sprite.HasAlpha() ? sprite.GetAlpha() : nullptr;

			for (int sy = 0; sy < sh; ++sy) {
				const int dy = top + sy;
				if (dy < 0 || dy >= canvasHeight) {
					continue;
				}
				for (int sx = 0; sx < sw; ++sx) {
					const int dx = left + sx;
					if (dx < 0 || dx >= canvasWidth) {
						continue;
					}

					const size_t srcIndex = (static_cast<size_t>(sy) * sw + sx);
					const unsigned char a = srcAlpha ? srcAlpha[srcIndex] : 255;
					if (a == 0) {
						continue;
					}

					const size_t dstIndex = (static_cast<size_t>(dy) * canvasWidth + dx);
					if (a == 255) {
						dstRgb[dstIndex * 3 + 0] = srcRgb[srcIndex * 3 + 0];
						dstRgb[dstIndex * 3 + 1] = srcRgb[srcIndex * 3 + 1];
						dstRgb[dstIndex * 3 + 2] = srcRgb[srcIndex * 3 + 2];
						dstAlpha[dstIndex] = 255;
						continue;
					}

					// Straight alpha-over, so half-transparent water and
					// shadows land on top of the ground instead of replacing it.
					for (int channel = 0; channel < 3; ++channel) {
						const int src = srcRgb[srcIndex * 3 + channel];
						const int dst = dstRgb[dstIndex * 3 + channel];
						dstRgb[dstIndex * 3 + channel] = static_cast<unsigned char>((src * a + dst * (255 - a)) / 255);
					}
					dstAlpha[dstIndex] = static_cast<unsigned char>(std::max<int>(dstAlpha[dstIndex], a));
				}
			}
		};

		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				const Tile* tile = map.getTile(origin.x + x, origin.y + y, origin.z);
				if (!tile) {
					continue;
				}

				auto drawItem = [&](const Item* item) {
					const wxImage* sprite = spriteFor(item);
					if (!sprite) {
						return;
					}
					const GameSprite* gameSprite = item->getItemType().sprite;
					// Sprites larger than one tile hang up and to the left, and
					// the draw offset nudges them further.
					const int left = x * TILE + TILE - sprite->GetWidth() - (gameSprite ? gameSprite->draw_offset.x : 0);
					const int top = y * TILE + TILE - sprite->GetHeight() - (gameSprite ? gameSprite->draw_offset.y : 0);
					blit(*sprite, left, top);
				};

				drawItem(tile->ground);
				for (const Item* item : tile->items) {
					drawItem(item);
				}
			}
		}

		if (pixelsPerTile != TILE) {
			const int scaledWidth = width * pixelsPerTile;
			const int scaledHeight = height * pixelsPerTile;
			// Nearest keeps tile-aligned pixels crisp when magnifying; the
			// smooth filter avoids aliasing when shrinking.
			canvas = canvas.Scale(scaledWidth, scaledHeight, pixelsPerTile > TILE ? wxIMAGE_QUALITY_NEAREST : wxIMAGE_QUALITY_HIGH);
		}

		return canvas;
	}

	std::string encodePngBase64(const wxImage &image) {
		if (!image.IsOk()) {
			throw McpError("the image is empty or invalid");
		}

		wxMemoryOutputStream stream;
		if (!image.SaveFile(stream, wxBITMAP_TYPE_PNG)) {
			throw McpError("failed to encode the image as PNG");
		}

		const size_t size = stream.GetSize();
		std::vector<unsigned char> buffer(size);
		stream.CopyTo(buffer.data(), size);

		const wxString base64 = wxBase64Encode(buffer.data(), size);
		return std::string(base64.mb_str());
	}

	void registerRenderTool(ToolRegistry &registry) {
		registry.add({ "map_render_region",
					   "Render one floor of a rectangular region as a PNG image. "
					   "mode=sprites draws the real client sprites, which is what you want to actually see an area - "
					   "it works offscreen, so unlike map_screenshot it is not limited to what the editor window is showing. "
					   "mode=minimap (default) paints minimap colours instead: far cheaper, good for large areas and overall layout. "
					   "Pair either with map_read_region mode=summary for the numbers behind the picture.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "from", json { { "type", "object" }, { "description", "one corner of the region" }, { "properties", json { { "x", json { { "type", "integer" } } }, { "y", json { { "type", "integer" } } }, { "z", json { { "type", "integer" }, { "description", "floor, 0-15" } } } } }, { "required", json::array({ "x", "y", "z" }) } } }, { "to", json { { "type", "object" }, { "description", "the opposite corner; z must equal from.z" }, { "properties", json { { "x", json { { "type", "integer" } } }, { "y", json { { "type", "integer" } } }, { "z", json { { "type", "integer" } } } } }, { "required", json::array({ "x", "y", "z" }) } } }, { "mode", json { { "type", "string" }, { "enum", json::array({ "minimap", "sprites" }) }, { "description", "default minimap" } } }, { "scale", json { { "type", "integer" }, { "description", "minimap mode: pixels per tile, 1-32, default 4" } } }, { "pixelsPerTile", json { { "type", "integer" }, { "description", "sprites mode: 4-64, default 32 (native sprite size)" } } } } },
						   { "required", json::array({ "from", "to" }) } },
					   false,
					   toolRenderRegion });
	}

} // namespace mcp
