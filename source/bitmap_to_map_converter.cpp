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
#include "bitmap_to_map_converter.h"
#include "editor.h"
#include "map.h"
#include "tile.h"
#include "item.h"
#include "ground_brush.h"
#include "brush.h"
#include "action.h"
#include "gui.h"
#include "settings.h"

BitmapToMapConverter::BitmapToMapConverter(Editor &editor) :
	editor(editor) {
}

static float rgbToHue(uint8_t r, uint8_t g, uint8_t b) {
	float rf = r / 255.0f;
	float gf = g / 255.0f;
	float bf = b / 255.0f;
	float maxC = std::max({ rf, gf, bf });
	float minC = std::min({ rf, gf, bf });
	float delta = maxC - minC;

	if (delta < kAchromaticDelta) {
		return -1.0f;
	}

	float hue = 0.0f;
	if (maxC == rf) {
		hue = 60.0f * fmod((gf - bf) / delta, 6.0f);
	} else if (maxC == gf) {
		hue = 60.0f * ((bf - rf) / delta + 2.0f);
	} else {
		hue = 60.0f * ((rf - gf) / delta + 4.0f);
	}

	if (hue < 0.0f) {
		hue += 360.0f;
	}

	return hue;
}

const ColorMapping* BitmapToMapConverter::findMatchingColor(
	uint8_t r, uint8_t g, uint8_t b,
	const std::vector<ColorMapping> &mappings,
	int tolerance,
	MatchMode matchMode
) const {
	const ColorMapping* bestMatch = nullptr;
	int bestDistance = tolerance + 1;

	for (const auto &mapping : mappings) {
		if (mapping.ignore || mapping.brushName.empty()) {
			continue;
		}

		int distance = -1;
		if (matchMode == MatchMode::MATCH_HUE_HSL) {
			float pixelHue = rgbToHue(r, g, b);
			float mappingHue = rgbToHue(mapping.r, mapping.g, mapping.b);

			if (pixelHue < 0.0f || mappingHue < 0.0f) {
				distance = std::abs((int)r - (int)mapping.r)
					+ std::abs((int)g - (int)mapping.g)
					+ std::abs((int)b - (int)mapping.b);
			} else {
				float hueDiff = fabs(pixelHue - mappingHue);
				hueDiff = (hueDiff > 180.0f) ? 360.0f - hueDiff : hueDiff;
				distance = static_cast<int>(hueDiff);
			}
		} else {
			distance = std::abs((int)r - (int)mapping.r)
				+ std::abs((int)g - (int)mapping.g)
				+ std::abs((int)b - (int)mapping.b);
		}

		if (distance >= 0 && distance <= tolerance && distance < bestDistance) {
			bestDistance = distance;
			bestMatch = &mapping;
		}
	}
	return bestMatch;
}

bool BitmapToMapConverter::isValidMapPosition(int x, int y, int z) const {
	return x >= 0 && y >= 0 && x <= rme::MapMaxWidth && y <= rme::MapMaxHeight && z >= 0 && z <= rme::MapMaxLayer;
}

void BitmapToMapConverter::trackBorderNeighbors(int mapX, int mapY, int mapZ, std::set<Position> &borderPositions) const {
	for (int dy = -1; dy <= 1; dy++) {
		for (int dx = -1; dx <= 1; dx++) {
			int bx = mapX + dx;
			int by = mapY + dy;
			if (isValidMapPosition(bx, by, mapZ)) {
				borderPositions.insert(Position(bx, by, mapZ));
			}
		}
	}
}

void BitmapToMapConverter::placeGroundTiles(
	const ConvertParams &params,
	BatchAction* batch,
	std::set<Position> &borderPositions,
	ConvertResult &result
) {
	Map &map = editor.getMap();
	int imgWidth = params.image.GetWidth();
	int imgHeight = params.image.GetHeight();
	int totalPixels = imgWidth * imgHeight;

	Action* action = editor.createAction(batch);

	const unsigned char* imgData = params.image.GetData();
	bool hasAlpha = params.image.HasAlpha();
	const unsigned char* alphaData = hasAlpha ? params.image.GetAlpha() : nullptr;

	int pixelsDone = 0;
	for (int py = 0; py < imgHeight; py++) {
		for (int px = 0; px < imgWidth; px++) {
			if (pixelsDone % 4096 == 0) {
				g_gui.SetLoadDone(static_cast<int32_t>(50.0 * pixelsDone / totalPixels));
			}
			pixelsDone++;

			if (hasAlpha && alphaData[py * imgWidth + px] < 128) {
				result.tilesSkipped++;
				continue;
			}

			int idx = (py * imgWidth + px) * 3;
			uint8_t r = imgData[idx];
			uint8_t g_color = imgData[idx + 1];
			uint8_t b_color = imgData[idx + 2];

			const ColorMapping* mapping = findMatchingColor(r, g_color, b_color, params.mappings, params.tolerance, params.matchMode);
			if (!mapping) {
				result.tilesSkipped++;
				continue;
			}

			Brush* brush = g_brushes.getBrush(mapping->brushName);
			if (!brush || !brush->isGround()) {
				result.tilesSkipped++;
				continue;
			}

			int mapX = px + params.offsetX;
			int mapY = py + params.offsetY;
			int mapZ = params.offsetZ;

			if (!isValidMapPosition(mapX, mapY, mapZ)) {
				result.tilesSkipped++;
				continue;
			}

			Position pos(mapX, mapY, mapZ);
			TileLocation* location = map.createTileL(pos);
			Tile* tile = location->get();
			Tile* new_tile = nullptr;

			if (tile) {
				new_tile = tile->deepCopy(map);
				new_tile->cleanBorders();
			} else {
				new_tile = map.allocator(location);
			}

			brush->asGround()->draw(&map, new_tile, nullptr);
			action->addChange(newd Change(new_tile));
			result.tilesPlaced++;

			trackBorderNeighbors(mapX, mapY, mapZ, borderPositions);
		}
	}

	batch->addAndCommitAction(action);
}

void BitmapToMapConverter::borderizeTiles(
	const std::set<Position> &borderPositions,
	BatchAction* batch
) {
	if (borderPositions.empty()) {
		return;
	}

	Map &map = editor.getMap();
	Action* action = editor.createAction(batch);

	int bordersDone = 0;
	auto totalBorders = static_cast<int>(borderPositions.size());

	for (const Position &pos : borderPositions) {
		if (bordersDone % 4096 == 0) {
			g_gui.SetLoadDone(static_cast<int32_t>(50 + 49.0 * bordersDone / totalBorders));
		}
		bordersDone++;

		TileLocation* location = map.createTileL(pos);
		Tile* tile = location->get();

		if (tile) {
			Tile* new_tile = tile->deepCopy(map);
			new_tile->borderize(&map);
			action->addChange(newd Change(new_tile));
			continue;
		}

		std::unique_ptr<Tile> new_tile(map.allocator(location));
		new_tile->borderize(&map);
		if (!new_tile->empty()) {
			action->addChange(newd Change(new_tile.release()));
		}
	}

	batch->addAndCommitAction(action);
}

ConvertResult BitmapToMapConverter::convert(
	const wxImage &image,
	const std::vector<ColorMapping> &mappings,
	int tolerance,
	MatchMode matchMode,
	int offsetX, int offsetY, int offsetZ
) {
	ConvertResult result;
	result.tilesPlaced = 0;
	result.tilesSkipped = 0;
	result.success = false;

	if (!image.IsOk()) {
		result.errorMessage = "Invalid image.";
		return result;
	}

	if (mappings.empty()) {
		result.errorMessage = "No color mappings defined.";
		return result;
	}

	g_gui.CreateLoadBar("Generating map from bitmap...");

	BatchAction* batch = editor.createBatch(ACTION_DRAW);
	std::set<Position> borderPositions;

	ConvertParams params { image, mappings, tolerance, matchMode, offsetX, offsetY, offsetZ };
	placeGroundTiles(params, batch, borderPositions, result);
	borderizeTiles(borderPositions, batch);

	editor.addBatch(batch);
	editor.updateActions();

	g_gui.DestroyLoadBar();
	g_gui.RefreshView();

	result.success = true;
	return result;
}
