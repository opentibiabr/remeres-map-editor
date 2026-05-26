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
#include "sprite_appearances.h"

#include <mapdata.pb.h>
#include <staticdata.pb.h>
#include <staticmapdata.pb.h>
#include <wx/image.h>
#include <lzma.h>

#include <fstream>
#include <filesystem>
#include <array>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <cmath>
#include <format>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <sstream>
#include <string_view>

typedef uint8_t attribute_t;
typedef uint32_t flags_t;

namespace {
	constexpr int HousePreviewContextMargin = 2;
	constexpr int HouseStaticMapContextMargin = 0;
	constexpr int HouseStaticMapContextTileRadius = 0;
	// CIP staticmapdata houses can carry up to 8 items per preview tile.
	// Limiting to 5 truncates border/top items and can hide south walls.
	constexpr size_t HousePreviewMaxItemsPerTile = 8;
	// When a CIP template house is available, preserve its tile item payload to
	// keep exact Cyclopedia rendering compatibility (walls/context/stack order).
	constexpr bool PreserveTemplateStaticMapHouseItems = true;
	constexpr int CyclopediaMinFloor = 0;
	constexpr int CyclopediaMaxFloor = 7;
	constexpr int CyclopediaOpaqueSeaFloor = rme::MapGroundLayer;
	constexpr int CyclopediaSatelliteBasePixelsPerSquare = 32;
	constexpr double CyclopediaMinPixelsPerSquare = 0.5;
	constexpr unsigned char CyclopediaMinimapSeaColorR = 51;
	constexpr unsigned char CyclopediaMinimapSeaColorG = 102;
	constexpr unsigned char CyclopediaMinimapSeaColorB = 153;
	constexpr unsigned char CyclopediaSatelliteSeaColorR = 39;
	constexpr unsigned char CyclopediaSatelliteSeaColorG = 77;
	constexpr unsigned char CyclopediaSatelliteSeaColorB = 166;
	constexpr std::array<char, 4> CyclopediaProgressSpinner { '|', '/', '-', '\\' };

	struct CyclopediaAssetLayerConfig {
		bool minimap = false;
		double scale = 0.0;
		int chunkSize = 0;
		double pixelsPerSquare = 1.0;
	};

	struct CyclopediaChunkArea {
		int floor = 0;
		int startX = 0;
		int startY = 0;
		int width = 0;
		int height = 0;
	};

	constexpr std::array<CyclopediaAssetLayerConfig, 3> CyclopediaMinimapLayers { {
		{ true, 1.0 / 64.0, 1024, 0.5 },
		{ true, 1.0 / 32.0, 512, 1.0 },
		{ true, 1.0 / 16.0, 256, 2.0 },
	} };

	constexpr std::array<CyclopediaAssetLayerConfig, 3> CyclopediaSatelliteLayers { {
		{ false, 1.0 / 64.0, 1024, 0.5 },
		{ false, 1.0 / 32.0, 512, 1.0 },
		{ false, 1.0 / 16.0, 256, 2.0 },
	} };

	struct HousePreviewItemData {
		uint32_t clientId = 0;
		uint8_t xPattern = 0;
		uint8_t yPattern = 0;
		uint8_t zPattern = 0;
	};

	struct HousePreviewTileData {
		uint32_t x = 0;
		uint32_t y = 0;
		uint8_t color = 0;
		bool isHouseTile = false;
		std::vector<HousePreviewItemData> items;
	};

	struct HousePreviewData {
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t floor = 0;
		std::vector<uint8_t> colors;
		std::vector<HousePreviewTileData> tiles;
	};

	struct PositionHash {
		size_t operator()(const Position &position) const noexcept {
			size_t hash = std::hash<int> {}(position.x);
			hash ^= std::hash<int> {}(position.y) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
			hash ^= std::hash<int> {}(position.z) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
			return hash;
		}
	};

	struct HousePositionKey {
		uint32_t x = 0;
		uint32_t y = 0;
		uint32_t z = 0;

		bool operator==(const HousePositionKey &other) const noexcept = default;
	};

	struct HousePositionKeyHash {
		size_t operator()(const HousePositionKey &position) const noexcept {
			size_t hash = std::hash<uint32_t> {}(position.x);
			hash ^= std::hash<uint32_t> {}(position.y) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
			hash ^= std::hash<uint32_t> {}(position.z) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
			return hash;
		}
	};

	struct StaticMapTemplateTile {
		uint32_t index = 0;
		uint32_t skip = 0;
		std::vector<uint32_t> itemValues;
	};

	struct StaticMapHouseTemplate {
		int minX = 0;
		int minY = 0;
		int minZ = 0;
		int width = 0;
		int height = 0;
		int floorCount = 0;
		std::vector<StaticMapTemplateTile> tiles;

		bool isValid() const noexcept {
			return width > 0 && height > 0 && floorCount > 0;
		}
	};

	struct HousePositionDelta {
		int dx = 0;
		int dy = 0;
		int dz = 0;
	};

	struct StaticDataTemplateMergeStats {
		size_t generatedHouses = 0;
		size_t matchedTemplateHouses = 0;
		size_t missingTemplateHouses = 0;
		size_t appendedGeneratedHouses = 0;
		size_t remappedIds = 0;
		size_t finalHouses = 0;
	};

	struct StaticMapHouseExportDebugStats {
		uint32_t housesProcessed = 0;
		uint32_t housesWithTemplate = 0;
		uint32_t housesWithoutTemplate = 0;
		uint32_t housesWithTemplateMismatch = 0;
		uint64_t tilesWritten = 0;
		uint64_t tilesWithItems = 0;
		uint64_t templateTilesCompared = 0;
		uint64_t templateTileMismatches = 0;
		uint64_t templateSouthTilesCompared = 0;
		uint64_t templateSouthTileMismatches = 0;
		uint64_t templateTilesMissingItems = 0;
		uint64_t missingMapTiles = 0;
		uint64_t droppedByCap = 0;
		uint64_t droppedInvalidItems = 0;
	};

	using StaticMapHouseTemplatesPtr = const std::unordered_map<uint32_t, StaticMapHouseTemplate>*;
	using StaticDataHouseNameFilter = std::set<std::string, std::less<>>;
	using StaticDataHouseNameFilterPtr = const StaticDataHouseNameFilter*;

	StaticMapHouseTemplatesPtr &activeStaticMapHouseTemplates() {
		static StaticMapHouseTemplatesPtr value = nullptr;
		return value;
	}

	StaticDataHouseNameFilterPtr &activeStaticDataHouseNameFilter() {
		static StaticDataHouseNameFilterPtr value = nullptr;
		return value;
	}

	StaticDataHouseNameFilterPtr &activeStaticDataDebugHouseNameFilter() {
		static StaticDataHouseNameFilterPtr value = nullptr;
		return value;
	}

	StaticMapHouseExportDebugStats &staticMapHouseExportDebugStats() {
		static StaticMapHouseExportDebugStats value;
		return value;
	}

	std::string formatHousePreviewItemValues(const std::vector<uint32_t> &values, size_t maxValues = 12) {
		if (values.empty()) {
			return "[]";
		}

		std::ostringstream output;
		output << "[";
		const size_t limit = std::min(values.size(), maxValues);
		for (size_t i = 0; i < limit; ++i) {
			if (i > 0) {
				output << ",";
			}
			output << values[i];
		}
		if (values.size() > limit) {
			output << ",...";
		}
		output << "]";
		return output.str();
	}

	void collectRawTileClientIds(const Tile* tile, std::vector<uint32_t> &rawClientIds) {
		rawClientIds.clear();
		if (!tile) {
			return;
		}

		rawClientIds.reserve(1 + tile->items.size());
		if (tile->ground) {
			const ItemType &groundType = tile->ground->getItemType();
			if (groundType.clientID != 0) {
				rawClientIds.emplace_back(groundType.clientID);
			}
		}

		for (const Item* item : tile->items) {
			if (!item) {
				continue;
			}
			const ItemType &itemType = item->getItemType();
			if (itemType.clientID != 0) {
				rawClientIds.emplace_back(itemType.clientID);
			}
		}
	}

	void resetStaticMapHouseExportDebugStats() {
		staticMapHouseExportDebugStats() = StaticMapHouseExportDebugStats {};
	}

	void logStaticMapHouseExportDebugSummary() {
		const auto &stats = staticMapHouseExportDebugStats();
		spdlog::info(
			"[house-debug] staticmapdata export summary: houses={} template_houses={} dynamic_houses={} template_mismatch_houses={} "
			"tiles_written={} tiles_with_items={} template_tiles_compared={} template_tile_mismatches={} "
			"south_tiles_compared={} south_tile_mismatches={} template_tiles_missing_items={} missing_map_tiles={} "
			"dropped_by_cap={} dropped_invalid_items={}",
			stats.housesProcessed,
			stats.housesWithTemplate,
			stats.housesWithoutTemplate,
			stats.housesWithTemplateMismatch,
			stats.tilesWritten,
			stats.tilesWithItems,
			stats.templateTilesCompared,
			stats.templateTileMismatches,
			stats.templateSouthTilesCompared,
			stats.templateSouthTileMismatches,
			stats.templateTilesMissingItems,
			stats.missingMapTiles,
			stats.droppedByCap,
			stats.droppedInvalidItems
		);
	}

	std::string normalizeHouseName(const std::string &name) {
		std::string normalized;
		normalized.reserve(name.size());

		bool previousWasSpace = true;
		for (const unsigned char rawChar : name) {
			if (std::isspace(rawChar)) {
				if (!previousWasSpace) {
					normalized.push_back(' ');
				}
				previousWasSpace = true;
				continue;
			}

			normalized.push_back(static_cast<char>(std::tolower(rawChar)));
			previousWasSpace = false;
		}

		if (!normalized.empty() && normalized.back() == ' ') {
			normalized.pop_back();
		}

		return normalized;
	}

	bool isHouseDebugTargetName(const std::string &name) {
		const std::string normalizedName = normalizeHouseName(name);
		if (normalizedName.empty()) {
			return false;
		}

		if (const auto* filter = activeStaticDataDebugHouseNameFilter(); filter && !filter->empty()) {
			return filter->contains(normalizedName);
		}
		return false;
	}

	bool isHouseDebugTarget(const House &house) {
		return isHouseDebugTargetName(house.name);
	}

	bool shouldIncludeHouseInStaticExport(const House &house) {
		const auto* filter = activeStaticDataHouseNameFilter();
		if (!filter || filter->empty()) {
			return true;
		}
		return filter->contains(normalizeHouseName(house.name));
	}

	std::string normalizeHouseNameAlias(const std::string &name) {
		static constexpr std::array<std::string_view, 9> aliasStopWords {
			"street",
			"place",
			"lane",
			"avenue",
			"road",
			"boulevard",
			"blvd",
			"square",
			"way"
		};

		const std::string normalized = normalizeHouseName(name);
		std::istringstream input(normalized);
		std::string token;
		std::string alias;
		while (input >> token) {
			if (std::ranges::find(aliasStopWords, std::string_view(token)) != aliasStopWords.end()) {
				continue;
			}
			if (!alias.empty()) {
				alias.push_back(' ');
			}
			alias += token;
		}

		return alias.empty() ? normalized : alias;
	}

	bool getHousePositionKey(const clienteditor::protobuf::staticdata::House &houseData, HousePositionKey &key) {
		if (!houseData.has_houseposition()) {
			return false;
		}

		key.x = houseData.houseposition().pos_x();
		key.y = houseData.houseposition().pos_y();
		key.z = houseData.houseposition().pos_z();
		return true;
	}

	PositionVector selectConnectedHousePreviewTiles(const House &house) {
		PositionVector selectedTiles;
		selectedTiles.reserve(house.getTiles().size());
		for (const auto &position : house.getTiles()) {
			selectedTiles.emplace_back(position);
		}
		return selectedTiles;
	}

	bool isStaticMapHousePreviewContextTile(const Position &position, const std::unordered_set<Position, PositionHash> &houseTiles) {
		if (houseTiles.contains(position)) {
			return true;
		}

		for (int dy = -HouseStaticMapContextTileRadius; dy <= HouseStaticMapContextTileRadius; ++dy) {
			for (int dx = -HouseStaticMapContextTileRadius; dx <= HouseStaticMapContextTileRadius; ++dx) {
				if (dx == 0 && dy == 0) {
					continue;
				}

				const Position nearby(position.x + dx, position.y + dy, position.z);
				if (houseTiles.contains(nearby)) {
					return true;
				}
			}
		}

		return false;
	}

	wxColour mapColorToRgb(const uint8_t color) {
		return wxColour(
			static_cast<unsigned char>((static_cast<int>(color / 36) % 6) * 51),
			static_cast<unsigned char>((static_cast<int>(color / 6) % 6) * 51),
			static_cast<unsigned char>((color % 6) * 51)
		);
	}

	constexpr size_t CyclopediaCipHeaderSize = 32;
	constexpr std::array<uint8_t, 5> CyclopediaCipLzmaSignature { 0x70, 0x0A, 0xFA, 0x80, 0x40 };
	constexpr uint32_t CyclopediaLzmaDictionarySize = 8 * 1024 * 1024;
	constexpr double CyclopediaVirtualCameraHeight = 0.175;

	struct Sha256Context {
		std::array<uint8_t, 64> data {};
		std::array<uint32_t, 8> state {
			0x6A09E667u,
			0xBB67AE85u,
			0x3C6EF372u,
			0xA54FF53Au,
			0x510E527Fu,
			0x9B05688Cu,
			0x1F83D9ABu,
			0x5BE0CD19u
		};
		size_t dataLength = 0;
		uint64_t bitLength = 0;
	};

	inline uint32_t rotr(const uint32_t value, const uint32_t bits) {
		return (value >> bits) | (value << (32 - bits));
	}

	void sha256Transform(Sha256Context &context, const uint8_t block[64]) {
		static constexpr std::array<uint32_t, 64> k {
			0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u, 0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
			0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u, 0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
			0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu, 0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
			0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u, 0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
			0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u, 0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
			0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u, 0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
			0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u, 0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
			0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u, 0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u
		};

		std::array<uint32_t, 64> words {};
		for (size_t i = 0; i < 16; ++i) {
			const size_t idx = i * 4;
			words[i] = (static_cast<uint32_t>(block[idx]) << 24)
				| (static_cast<uint32_t>(block[idx + 1]) << 16)
				| (static_cast<uint32_t>(block[idx + 2]) << 8)
				| static_cast<uint32_t>(block[idx + 3]);
		}

		for (size_t i = 16; i < words.size(); ++i) {
			const uint32_t s0 = rotr(words[i - 15], 7) ^ rotr(words[i - 15], 18) ^ (words[i - 15] >> 3);
			const uint32_t s1 = rotr(words[i - 2], 17) ^ rotr(words[i - 2], 19) ^ (words[i - 2] >> 10);
			words[i] = words[i - 16] + s0 + words[i - 7] + s1;
		}

		uint32_t a = context.state[0];
		uint32_t b = context.state[1];
		uint32_t c = context.state[2];
		uint32_t d = context.state[3];
		uint32_t e = context.state[4];
		uint32_t f = context.state[5];
		uint32_t g = context.state[6];
		uint32_t h = context.state[7];

		for (size_t i = 0; i < words.size(); ++i) {
			const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
			const uint32_t ch = (e & f) ^ ((~e) & g);
			const uint32_t temp1 = h + s1 + ch + k[i] + words[i];
			const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
			const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
			const uint32_t temp2 = s0 + maj;

			h = g;
			g = f;
			f = e;
			e = d + temp1;
			d = c;
			c = b;
			b = a;
			a = temp1 + temp2;
		}

		context.state[0] += a;
		context.state[1] += b;
		context.state[2] += c;
		context.state[3] += d;
		context.state[4] += e;
		context.state[5] += f;
		context.state[6] += g;
		context.state[7] += h;
	}

	void sha256Update(Sha256Context &context, const std::span<const uint8_t> bytes) {
		for (const uint8_t byte : bytes) {
			context.data[context.dataLength++] = byte;
			if (context.dataLength == context.data.size()) {
				sha256Transform(context, context.data.data());
				context.bitLength += 512;
				context.dataLength = 0;
			}
		}
	}

	std::array<uint8_t, 32> sha256Finalize(Sha256Context &context) {
		std::array<uint8_t, 32> hash {};
		size_t i = context.dataLength;

		if (context.dataLength < 56) {
			context.data[i++] = 0x80;
			while (i < 56) {
				context.data[i++] = 0;
			}
		} else {
			context.data[i++] = 0x80;
			while (i < 64) {
				context.data[i++] = 0;
			}
			sha256Transform(context, context.data.data());
			context.data.fill(0);
		}
		context.bitLength += static_cast<uint64_t>(context.dataLength) * 8;
		for (size_t byteIndex = 0; byteIndex < 8; ++byteIndex) {
			context.data[63 - byteIndex] = static_cast<uint8_t>((context.bitLength >> (byteIndex * 8)) & 0xFF);
		}
		sha256Transform(context, context.data.data());

		for (size_t wordIndex = 0; wordIndex < context.state.size(); ++wordIndex) {
			const uint32_t word = context.state[wordIndex];
			const size_t outIndex = wordIndex * 4;
			hash[outIndex] = static_cast<uint8_t>((word >> 24) & 0xFF);
			hash[outIndex + 1] = static_cast<uint8_t>((word >> 16) & 0xFF);
			hash[outIndex + 2] = static_cast<uint8_t>((word >> 8) & 0xFF);
			hash[outIndex + 3] = static_cast<uint8_t>(word & 0xFF);
		}
		return hash;
	}

	std::array<uint8_t, 32> sha256Hash(const std::vector<uint8_t> &bytes) {
		Sha256Context context;
		if (!bytes.empty()) {
			sha256Update(context, std::span<const uint8_t>(bytes.data(), bytes.size()));
		}
		return sha256Finalize(context);
	}

	std::string toHex(const std::array<uint8_t, 32> &bytes) {
		std::ostringstream stream;
		stream << std::hex << std::nouppercase << std::setfill('0');
		for (const uint8_t byte : bytes) {
			stream << std::setw(2) << static_cast<int>(byte);
		}
		return stream.str();
	}

	std::string sha256Hex(const std::string_view bytes) {
		Sha256Context context;
		if (!bytes.empty()) {
			sha256Update(context, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()));
		}
		return toHex(sha256Finalize(context));
	}

	std::string buildCatalogDataFilename(const std::string_view prefix, const std::string_view bytes) {
		return std::format("{}-{}.dat", prefix, sha256Hex(bytes));
	}

	void appendU16(std::vector<uint8_t> &buffer, const uint16_t value) {
		buffer.push_back(static_cast<uint8_t>(value & 0xFF));
		buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
	}

	void appendU32(std::vector<uint8_t> &buffer, const uint32_t value) {
		buffer.push_back(static_cast<uint8_t>(value & 0xFF));
		buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
		buffer.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
		buffer.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
	}

	bool encodeBmpV4(const wxImage &image, std::vector<uint8_t> &bytes) {
		if (!image.IsOk()) {
			return false;
		}

		const int width = image.GetWidth();
		const int height = image.GetHeight();
		if (width <= 0 || height <= 0) {
			return false;
		}

		const unsigned char* rgb = image.GetData();
		if (!rgb) {
			return false;
		}

		const unsigned char* alpha = image.HasAlpha() ? image.GetAlpha() : nullptr;
		const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
		const size_t pixelBytes = pixelCount * 4;
		constexpr uint32_t pixelOffset = 122;
		const size_t totalSize = static_cast<size_t>(pixelOffset) + pixelBytes;
		if (totalSize > std::numeric_limits<uint32_t>::max()) {
			return false;
		}

		bytes.clear();
		bytes.reserve(totalSize);

		const auto xPixelsPerMeter = static_cast<int32_t>(static_cast<double>(width) / CyclopediaVirtualCameraHeight);
		const auto yPixelsPerMeter = static_cast<int32_t>(static_cast<double>(height) / CyclopediaVirtualCameraHeight);

		bytes.push_back(0x42);
		bytes.push_back(0x4D);
		appendU32(bytes, static_cast<uint32_t>(totalSize));
		appendU16(bytes, 0);
		appendU16(bytes, 0);
		appendU32(bytes, pixelOffset);
		appendU32(bytes, 108);
		appendU32(bytes, static_cast<uint32_t>(width));
		appendU32(bytes, static_cast<uint32_t>(height));
		appendU16(bytes, 1);
		appendU16(bytes, 32);
		appendU32(bytes, 3);
		appendU32(bytes, 0);
		appendU32(bytes, static_cast<uint32_t>(xPixelsPerMeter));
		appendU32(bytes, static_cast<uint32_t>(yPixelsPerMeter));
		appendU32(bytes, 0);
		appendU32(bytes, 0);
		appendU32(bytes, 0x00FF0000);
		appendU32(bytes, 0x0000FF00);
		appendU32(bytes, 0x000000FF);
		appendU32(bytes, 0xFF000000);
		appendU32(bytes, 0x00000001);
		appendU32(bytes, 0);
		appendU32(bytes, 0);
		appendU32(bytes, 1);
		appendU32(bytes, 0);
		appendU32(bytes, 0);
		appendU32(bytes, 1);
		appendU32(bytes, 0);
		appendU32(bytes, 0);
		appendU32(bytes, 1);
		appendU32(bytes, 0);
		appendU32(bytes, 0);
		appendU32(bytes, 0);

		for (int y = height - 1; y >= 0; --y) {
			for (int x = 0; x < width; ++x) {
				const auto sourceIndex = static_cast<size_t>(y * width + x);
				const size_t rgbIndex = sourceIndex * rme::PixelFormatRGB;
				const uint8_t red = rgb[rgbIndex];
				const uint8_t green = rgb[rgbIndex + 1];
				const uint8_t blue = rgb[rgbIndex + 2];
				const uint8_t alphaChannel = alpha ? alpha[sourceIndex] : 255;

				bytes.push_back(blue);
				bytes.push_back(green);
				bytes.push_back(red);
				bytes.push_back(alphaChannel);
			}
		}

		return bytes.size() == totalSize;
	}

	void write7BitEncodedInt(uint32_t value, std::vector<uint8_t> &bytes) {
		do {
			auto current = static_cast<uint8_t>(value & 0x7F);
			value >>= 7;
			if (value != 0) {
				current |= 0x80;
			}
			bytes.push_back(current);
		} while (value != 0);
	}

	bool compressLzmaRaw(const std::vector<uint8_t> &input, std::vector<uint8_t> &compressedRaw) {
		compressedRaw.clear();

		lzma_options_lzma options {};
		options.dict_size = CyclopediaLzmaDictionarySize;
		options.lc = 3;
		options.lp = 0;
		options.pb = 2;
		options.mode = LZMA_MODE_FAST;
		options.nice_len = 16;
		options.mf = LZMA_MF_HC3;
		options.depth = 4;

		std::array<lzma_filter, 2> filters { { { LZMA_FILTER_LZMA1, &options },
											   { LZMA_VLI_UNKNOWN, nullptr } } };

		lzma_stream stream = LZMA_STREAM_INIT;
		lzma_ret result = lzma_raw_encoder(&stream, filters.data());
		if (result != LZMA_OK) {
			return false;
		}

		stream.next_in = input.empty() ? nullptr : input.data();
		stream.avail_in = input.size();

		constexpr size_t outputChunkSize = 1 << 20;
		result = LZMA_OK;
		while (result == LZMA_OK) {
			const size_t writeOffset = compressedRaw.size();
			compressedRaw.resize(writeOffset + outputChunkSize);
			stream.next_out = compressedRaw.data() + writeOffset;
			stream.avail_out = outputChunkSize;

			result = lzma_code(&stream, LZMA_FINISH);
			const size_t produced = outputChunkSize - stream.avail_out;
			compressedRaw.resize(writeOffset + produced);
		}

		lzma_end(&stream);
		return result == LZMA_STREAM_END;
	}

	bool encodeCipLzmaAsset(const std::vector<uint8_t> &bmpBytes, std::vector<uint8_t> &assetBytes) {
		std::vector<uint8_t> compressedRaw;
		if (!compressLzmaRaw(bmpBytes, compressedRaw)) {
			return false;
		}

		std::vector<uint8_t> lzmaPayload;
		lzmaPayload.reserve(13 + compressedRaw.size());
		const auto properties = static_cast<uint8_t>(((2 * 5) + 0) * 9 + 3);
		lzmaPayload.push_back(properties);
		appendU32(lzmaPayload, CyclopediaLzmaDictionarySize);
		for (int i = 0; i < 8; ++i) {
			lzmaPayload.push_back(0xFF);
		}
		lzmaPayload.insert(lzmaPayload.end(), compressedRaw.begin(), compressedRaw.end());

		if (lzmaPayload.size() > std::numeric_limits<uint32_t>::max()) {
			return false;
		}

		std::vector<uint8_t> encodedSize;
		write7BitEncodedInt(static_cast<uint32_t>(lzmaPayload.size()), encodedSize);
		if (CyclopediaCipLzmaSignature.size() + encodedSize.size() > CyclopediaCipHeaderSize) {
			return false;
		}

		assetBytes.assign(CyclopediaCipHeaderSize, 0);
		const size_t headerOffset = CyclopediaCipHeaderSize - CyclopediaCipLzmaSignature.size() - encodedSize.size();
		std::ranges::copy(CyclopediaCipLzmaSignature, assetBytes.begin() + headerOffset);
		std::ranges::copy(encodedSize, assetBytes.begin() + headerOffset + CyclopediaCipLzmaSignature.size());
		assetBytes.insert(assetBytes.end(), lzmaPayload.begin(), lzmaPayload.end());
		return true;
	}

	bool encodeCyclopediaAsset(const wxImage &image, std::vector<uint8_t> &assetBytes, std::string &hashHex) {
		std::vector<uint8_t> bmpBytes;
		if (!encodeBmpV4(image, bmpBytes)) {
			return false;
		}

		hashHex = toHex(sha256Hash(bmpBytes));
		return encodeCipLzmaAsset(bmpBytes, assetBytes);
	}

	std::string buildCyclopediaAssetFilename(const bool minimap, const double scale, const int assetX, const int assetY, const int floor, const std::string &hashHex) {
		const int scalePrefix = std::max(1, static_cast<int>(std::llround(1.0 / std::max(scale, 0.000001))));
		const int chunkX = std::max(assetX / 32, 0);
		const int chunkY = std::max(assetY / 32, 0);

		return std::format("{}{:02}-{:04}-{:04}-{:02}-{}.bmp.lzma", minimap ? "minimap-" : "satellite-", scalePrefix, chunkX, chunkY, floor, hashHex);
	}

	bool hasCyclopediaTileData(const Tile* tile) {
		return tile && (tile->ground || !tile->items.empty());
	}

	bool isValidMapTilePosition(const Position &position) {
		return position.x >= 0 && position.x <= rme::MapMaxWidth
			&& position.y >= 0 && position.y <= rme::MapMaxHeight
			&& position.z >= rme::MapMinLayer && position.z <= rme::MapMaxLayer;
	}

	uint32_t toCyclopediaProtoCoordinate(const int value) {
		return value >= 0 ? static_cast<uint32_t>(value) : 0;
	}

	Tile* getCyclopediaMapTile(Map &map, const Position &position) {
		if (!isValidMapTilePosition(position)) {
			return nullptr;
		}

		return map.getTile(position);
	}

	bool getCyclopediaFloorBounds(Map &map, const int floor, int &minX, int &minY, int &maxX, int &maxY) {
		minX = std::numeric_limits<int>::max();
		minY = std::numeric_limits<int>::max();
		maxX = std::numeric_limits<int>::min();
		maxY = std::numeric_limits<int>::min();

		for (MapIterator it = map.begin(); it != map.end(); ++it) {
			Tile* tile = (*it)->get();
			if (!tile || tile->getZ() != floor || !hasCyclopediaTileData(tile)) {
				continue;
			}

			minX = std::min(minX, tile->getX());
			minY = std::min(minY, tile->getY());
			maxX = std::max(maxX, tile->getX());
			maxY = std::max(maxY, tile->getY());
		}

		return minX <= maxX && minY <= maxY;
	}

	bool getCyclopediaWorldBounds(Map &map, int &minX, int &minY, int &maxX, int &maxY) {
		minX = std::numeric_limits<int>::max();
		minY = std::numeric_limits<int>::max();
		maxX = std::numeric_limits<int>::min();
		maxY = std::numeric_limits<int>::min();

		bool hasData = false;
		for (int floor = CyclopediaMinFloor; floor <= CyclopediaMaxFloor; ++floor) {
			int floorMinX = 0;
			int floorMinY = 0;
			int floorMaxX = 0;
			int floorMaxY = 0;
			if (!getCyclopediaFloorBounds(map, floor, floorMinX, floorMinY, floorMaxX, floorMaxY)) {
				continue;
			}

			hasData = true;
			minX = std::min(minX, floorMinX);
			minY = std::min(minY, floorMinY);
			maxX = std::max(maxX, floorMaxX);
			maxY = std::max(maxY, floorMaxY);
		}

		return hasData && minX <= maxX && minY <= maxY;
	}

	bool collectCyclopediaFloorChunks(Map &map, const int floor, const int minX, const int minY, const int chunkSize, std::vector<std::pair<int, int>> &chunkStarts) {
		chunkStarts.clear();

		std::unordered_set<uint64_t> seenChunks;
		seenChunks.reserve(1024);

		for (MapIterator it = map.begin(); it != map.end(); ++it) {
			Tile* tile = (*it)->get();
			if (!tile || tile->getZ() != floor || !hasCyclopediaTileData(tile)) {
				continue;
			}

			const int chunkIndexX = (tile->getX() - minX) / chunkSize;
			const int chunkIndexY = (tile->getY() - minY) / chunkSize;
			const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(chunkIndexX)) << 32)
				| static_cast<uint64_t>(static_cast<uint32_t>(chunkIndexY));
			if (!seenChunks.emplace(key).second) {
				continue;
			}

			const int chunkStartX = minX + chunkIndexX * chunkSize;
			const int chunkStartY = minY + chunkIndexY * chunkSize;
			chunkStarts.emplace_back(chunkStartX, chunkStartY);
		}

		std::sort(chunkStarts.begin(), chunkStarts.end(), [](const auto &left, const auto &right) {
			if (left.second != right.second) {
				return left.second < right.second;
			}
			return left.first < right.first;
		});

		return !chunkStarts.empty();
	}

	int computeCyclopediaChunkPixelDimension(const int tileDimension, const double pixelsPerSquare) {
		if (tileDimension <= 0) {
			return 0;
		}

		const double clampedPixelsPerSquare = std::max(pixelsPerSquare, CyclopediaMinPixelsPerSquare);
		return std::max(1, static_cast<int>(std::ceil(static_cast<double>(tileDimension) * clampedPixelsPerSquare)));
	}

	struct CyclopediaSampleRange {
		double begin = 0.0;
		double end = 0.0;
		int first = 0;
		int last = 0;
	};

	struct CyclopediaWeightedRgb {
		double red = 0.0;
		double green = 0.0;
		double blue = 0.0;
		double alpha = 0.0;
		double colorWeight = 0.0;
		double weight = 0.0;
	};

	CyclopediaSampleRange getCyclopediaSampleRange(const int targetIndex, const int targetSize, const int sourceSize) {
		CyclopediaSampleRange range;
		range.begin = (static_cast<double>(targetIndex) * sourceSize) / targetSize;
		range.end = (static_cast<double>(targetIndex + 1) * sourceSize) / targetSize;
		range.first = std::clamp(static_cast<int>(std::floor(range.begin)), 0, sourceSize - 1);
		range.last = std::clamp(static_cast<int>(std::ceil(range.end)), range.first + 1, sourceSize);
		return range;
	}

	double getCyclopediaSampleWeight(const CyclopediaSampleRange &range, const int sourceIndex) {
		return std::min(range.end, static_cast<double>(sourceIndex + 1)) - std::max(range.begin, static_cast<double>(sourceIndex));
	}

	void accumulateCyclopediaSample(
		CyclopediaWeightedRgb &sample,
		const unsigned char* sourceData,
		const unsigned char* sourceAlpha,
		const int sourceWidth,
		const int sourceX,
		const int sourceY,
		const double weight
	) {
		if (weight <= 0.0) {
			return;
		}

		const size_t sourceIndex = (static_cast<size_t>(sourceY) * static_cast<size_t>(sourceWidth) + static_cast<size_t>(sourceX)) * rme::PixelFormatRGB;
		const size_t sourcePixelIndex = static_cast<size_t>(sourceY) * static_cast<size_t>(sourceWidth) + static_cast<size_t>(sourceX);
		const double alpha = sourceAlpha ? static_cast<double>(sourceAlpha[sourcePixelIndex]) : 255.0;
		const double colorWeight = weight * (alpha / 255.0);
		sample.red += static_cast<double>(sourceData[sourceIndex]) * colorWeight;
		sample.green += static_cast<double>(sourceData[sourceIndex + 1]) * colorWeight;
		sample.blue += static_cast<double>(sourceData[sourceIndex + 2]) * colorWeight;
		sample.alpha += alpha * weight;
		sample.colorWeight += colorWeight;
		sample.weight += weight;
	}

	CyclopediaWeightedRgb sampleCyclopediaRegion(
		const unsigned char* sourceData,
		const unsigned char* sourceAlpha,
		const int sourceWidth,
		const CyclopediaSampleRange &xRange,
		const CyclopediaSampleRange &yRange
	) {
		CyclopediaWeightedRgb sample;
		for (int sourceY = yRange.first; sourceY < yRange.last; ++sourceY) {
			const double yWeight = getCyclopediaSampleWeight(yRange, sourceY);
			for (int sourceX = xRange.first; sourceX < xRange.last; ++sourceX) {
				accumulateCyclopediaSample(sample, sourceData, sourceAlpha, sourceWidth, sourceX, sourceY, yWeight * getCyclopediaSampleWeight(xRange, sourceX));
			}
		}
		return sample;
	}

	unsigned char normalizeCyclopediaChannel(const double value, const double weight) {
		if (weight <= 0.0) {
			return 0;
		}

		return static_cast<unsigned char>(std::clamp(static_cast<int>(std::lround(value / weight)), 0, 255));
	}

	void writeCyclopediaSample(
		unsigned char* targetData,
		unsigned char* targetAlpha,
		const int targetWidth,
		const int targetX,
		const int targetY,
		const CyclopediaWeightedRgb &sample
	) {
		const size_t targetPixelIndex = static_cast<size_t>(targetY) * static_cast<size_t>(targetWidth) + static_cast<size_t>(targetX);
		const size_t targetIndex = targetPixelIndex * rme::PixelFormatRGB;
		targetData[targetIndex] = normalizeCyclopediaChannel(sample.red, sample.colorWeight);
		targetData[targetIndex + 1] = normalizeCyclopediaChannel(sample.green, sample.colorWeight);
		targetData[targetIndex + 2] = normalizeCyclopediaChannel(sample.blue, sample.colorWeight);
		if (targetAlpha) {
			targetAlpha[targetPixelIndex] = normalizeCyclopediaChannel(sample.alpha, sample.weight);
		}
	}

	bool downsampleCyclopediaChunk(wxImage &image, const int targetWidth, const int targetHeight) {
		if (!image.IsOk() || targetWidth <= 0 || targetHeight <= 0) {
			return false;
		}

		const int sourceWidth = image.GetWidth();
		const int sourceHeight = image.GetHeight();
		const unsigned char* sourceData = image.GetData();
		const unsigned char* sourceAlpha = image.HasAlpha() ? image.GetAlpha() : nullptr;
		if (!sourceData || sourceWidth <= 0 || sourceHeight <= 0) {
			return false;
		}

		wxImage downsampled;
		if (!downsampled.Create(targetWidth, targetHeight, false)) {
			return false;
		}
		if (sourceAlpha) {
			downsampled.InitAlpha();
		}

		unsigned char* targetData = downsampled.GetData();
		unsigned char* targetAlpha = sourceAlpha ? downsampled.GetAlpha() : nullptr;
		if (!targetData) {
			return false;
		}
		if (sourceAlpha && !targetAlpha) {
			return false;
		}

		for (int targetY = 0; targetY < targetHeight; ++targetY) {
			const CyclopediaSampleRange yRange = getCyclopediaSampleRange(targetY, targetHeight, sourceHeight);
			for (int targetX = 0; targetX < targetWidth; ++targetX) {
				const CyclopediaSampleRange xRange = getCyclopediaSampleRange(targetX, targetWidth, sourceWidth);
				writeCyclopediaSample(targetData, targetAlpha, targetWidth, targetX, targetY, sampleCyclopediaRegion(sourceData, sourceAlpha, sourceWidth, xRange, yRange));
			}
		}

		image = downsampled;
		return image.IsOk();
	}

	bool resampleCyclopediaChunk(wxImage &image, const int tileWidth, const int tileHeight, const double pixelsPerSquare) {
		if (!image.IsOk()) {
			return false;
		}

		const int targetWidth = computeCyclopediaChunkPixelDimension(tileWidth, pixelsPerSquare);
		const int targetHeight = computeCyclopediaChunkPixelDimension(tileHeight, pixelsPerSquare);
		if (targetWidth <= 0 || targetHeight <= 0) {
			return false;
		}

		if (image.GetWidth() == targetWidth && image.GetHeight() == targetHeight) {
			return true;
		}

		if (targetWidth < image.GetWidth() || targetHeight < image.GetHeight()) {
			return downsampleCyclopediaChunk(image, targetWidth, targetHeight);
		}

		image.Rescale(targetWidth, targetHeight, wxIMAGE_QUALITY_NEAREST);
		return image.IsOk();
	}

	void fillCyclopediaSea(unsigned char* data, const int width, const int height) {
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				const size_t index = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * rme::PixelFormatRGB;
				data[index] = CyclopediaMinimapSeaColorR;
				data[index + 1] = CyclopediaMinimapSeaColorG;
				data[index + 2] = CyclopediaMinimapSeaColorB;
			}
		}
	}

	void writeCyclopediaMinimapTile(Map &map, const CyclopediaChunkArea &area, const int x, const int y, unsigned char* data, bool &hasData) {
		const Position position(area.startX + x, area.startY + y, area.floor);
		Tile* tile = getCyclopediaMapTile(map, position);
		if (!hasCyclopediaTileData(tile)) {
			return;
		}

		const uint8_t minimapColor = tile->getMiniMapColor();
		if (minimapColor == 0) {
			return;
		}

		const wxColour rgb = mapColorToRgb(minimapColor);
		const size_t index = (static_cast<size_t>(y) * static_cast<size_t>(area.width) + static_cast<size_t>(x)) * rme::PixelFormatRGB;
		data[index] = rgb.Red();
		data[index + 1] = rgb.Green();
		data[index + 2] = rgb.Blue();
		hasData = true;
	}

	bool buildCyclopediaMinimapChunk(Map &map, const CyclopediaChunkArea &area, wxImage &image, bool &hasData) {
		hasData = false;
		if (area.width <= 0 || area.height <= 0) {
			return false;
		}

		if (!image.Create(area.width, area.height, true)) {
			return false;
		}

		unsigned char* data = image.GetData();
		if (!data) {
			return false;
		}

		fillCyclopediaSea(data, area.width, area.height);

		for (int y = 0; y < area.height; ++y) {
			for (int x = 0; x < area.width; ++x) {
				writeCyclopediaMinimapTile(map, area, x, y, data, hasData);
			}
		}

		return true;
	}

	struct SatelliteTinyPixel {
		unsigned char r = 0;
		unsigned char g = 0;
		unsigned char b = 0;
		unsigned char a = 0;
	};

	using SatelliteTinySprite = std::array<SatelliteTinyPixel, CyclopediaSatelliteBasePixelsPerSquare * CyclopediaSatelliteBasePixelsPerSquare>;
	using SatelliteSampledSprite = std::vector<SatelliteTinyPixel>;

	struct SatelliteSpriteCacheKey {
		uint32_t spriteId = 0;
		int footprintOriginX = 0;
		int footprintOriginY = 0;

		bool operator==(const SatelliteSpriteCacheKey &other) const noexcept {
			return spriteId == other.spriteId
				&& footprintOriginX == other.footprintOriginX
				&& footprintOriginY == other.footprintOriginY;
		}
	};

	struct SatelliteSpriteCacheKeyHash {
		size_t operator()(const SatelliteSpriteCacheKey &key) const noexcept {
			size_t hash = static_cast<size_t>(key.spriteId);
			hash ^= static_cast<size_t>(key.footprintOriginX + 0x9E3779B9) + (hash << 6) + (hash >> 2);
			hash ^= static_cast<size_t>(key.footprintOriginY + 0x9E3779B9) + (hash << 6) + (hash >> 2);
			return hash;
		}
	};

	struct SatelliteSampledSpriteCacheKey {
		SatelliteSpriteCacheKey sprite;
		int outputPixelsPerSquare = 0;

		bool operator==(const SatelliteSampledSpriteCacheKey &other) const noexcept {
			return outputPixelsPerSquare == other.outputPixelsPerSquare && sprite == other.sprite;
		}
	};

	struct SatelliteSampledSpriteCacheKeyHash {
		size_t operator()(const SatelliteSampledSpriteCacheKey &key) const noexcept {
			size_t hash = SatelliteSpriteCacheKeyHash {}(key.sprite);
			hash ^= static_cast<size_t>(key.outputPixelsPerSquare + 0x9E3779B9) + (hash << 6) + (hash >> 2);
			return hash;
		}
	};

	int resolveStackableCyclopediaSubtype(const int subtype) {
		if (subtype <= 0) {
			return 0;
		} else if (subtype < 5) {
			return subtype - 1;
		} else if (subtype < 10) {
			return 4;
		} else if (subtype < 25) {
			return 5;
		} else if (subtype < 50) {
			return 6;
		}
		return 7;
	}

	void resolveHangableCyclopediaPattern(const Tile &tile, const int patternWidth, int &patternX, int &patternY, int &patternZ) {
		patternY = 0;
		patternZ = 0;
		if (tile.hasProperty(HOOK_SOUTH)) {
			patternX = patternWidth >= 2 ? 1 : 0;
		} else if (tile.hasProperty(HOOK_EAST)) {
			patternX = patternWidth >= 3 ? 2 : 0;
		} else {
			patternX = 0;
		}
	}

	bool resolveCyclopediaItemPatterns(const Position &position, const Tile* tile, const Item* item, int &patternX, int &patternY, int &patternZ, int &subtype, int &frame) {
		if (!tile || !item) {
			return false;
		}

		const ItemType &type = item->getItemType();
		if (type.clientID == 0 || !type.sprite) {
			return false;
		}

		const GameSprite* sprite = type.sprite;
		const int patternWidth = std::max<int>(1, static_cast<int>(sprite->pattern_x));
		const int patternHeight = std::max<int>(1, static_cast<int>(sprite->pattern_y));
		const int patternDepth = std::max<int>(1, static_cast<int>(sprite->pattern_z));

		patternX = position.x % patternWidth;
		patternY = position.y % patternHeight;
		patternZ = position.z % patternDepth;
		subtype = -1;
		frame = item->getFrame();

		if (type.isSplash() || type.isFluidContainer()) {
			subtype = static_cast<int>(Item::liquidSubTypeToSpriteSubType(static_cast<uint8_t>(item->getSubtype())));
		} else if (type.isHangable) {
			resolveHangableCyclopediaPattern(*tile, patternWidth, patternX, patternY, patternZ);
		} else if (type.stackable) {
			subtype = resolveStackableCyclopediaSubtype(static_cast<int>(item->getSubtype()));
		}

		return true;
	}

	bool resolveCyclopediaItemSpriteSample(const Position &position, const Tile* tile, const Item* item, SatelliteSpriteCacheKey &spriteKey) {
		int patternX = 0;
		int patternY = 0;
		int patternZ = 0;
		int subtype = -1;
		int frame = 0;
		if (!resolveCyclopediaItemPatterns(position, tile, item, patternX, patternY, patternZ, subtype, frame)) {
			return false;
		}

		const ItemType &type = item->getItemType();
		GameSprite* sprite = type.sprite;
		if (!sprite) {
			return false;
		}

		spriteKey.spriteId = sprite->getSpriteID(0, subtype, patternX, patternY, patternZ, frame);
		const wxPoint footprintOrigin = sprite->getDrawOffset();
		spriteKey.footprintOriginX = footprintOrigin.x;
		spriteKey.footprintOriginY = footprintOrigin.y;
		return spriteKey.spriteId != 0;
	}

	bool sampleSpriteToTiny(const wxImage &spriteImage, const int footprintOriginX, const int footprintOriginY, SatelliteTinySprite &tinySprite) {
		if (!spriteImage.IsOk()) {
			return false;
		}

		const int width = spriteImage.GetWidth();
		const int height = spriteImage.GetHeight();
		if (width <= 0 || height <= 0) {
			return false;
		}

		const auto isOpaqueBlack = [&](const int x, const int y) {
			if (x < 0 || y < 0 || x >= width || y >= height) {
				return false;
			}

			const uint8_t alpha = spriteImage.HasAlpha() ? spriteImage.GetAlpha(x, y) : 255;
			if (alpha == 0) {
				return false;
			}

			return spriteImage.GetRed(x, y) == 0
				&& spriteImage.GetGreen(x, y) == 0
				&& spriteImage.GetBlue(x, y) == 0;
		};

		// Many sprites carry opaque black padding connected to borders.
		// Treat only edge-connected black pixels as transparent, preserving interior black details.
		std::vector<uint8_t> edgeBlackMask(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
		std::vector<int> floodQueue;
		floodQueue.reserve(static_cast<size_t>(width) * 2 + static_cast<size_t>(height) * 2);

		const auto enqueueIfEdgeBlack = [&](const int x, const int y) {
			if (!isOpaqueBlack(x, y)) {
				return;
			}

			const size_t index = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
			if (edgeBlackMask[index] != 0) {
				return;
			}

			edgeBlackMask[index] = 1;
			floodQueue.emplace_back(static_cast<int>(index));
		};

		for (int x = 0; x < width; ++x) {
			enqueueIfEdgeBlack(x, 0);
			enqueueIfEdgeBlack(x, height - 1);
		}
		for (int y = 1; y < height - 1; ++y) {
			enqueueIfEdgeBlack(0, y);
			enqueueIfEdgeBlack(width - 1, y);
		}

		for (size_t head = 0; head < floodQueue.size(); ++head) {
			const int index = floodQueue[head];
			const int x = index % width;
			const int y = index / width;

			const std::array<std::array<int, 2>, 4> neighbors { {
				{ x - 1, y },
				{ x + 1, y },
				{ x, y - 1 },
				{ x, y + 1 },
			} };

			for (const auto &neighbor : neighbors) {
				const int nx = neighbor[0];
				const int ny = neighbor[1];
				if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
					continue;
				}

				if (!isOpaqueBlack(nx, ny)) {
					continue;
				}

				const size_t nIndex = static_cast<size_t>(ny) * static_cast<size_t>(width) + static_cast<size_t>(nx);
				if (edgeBlackMask[nIndex] != 0) {
					continue;
				}

				edgeBlackMask[nIndex] = 1;
				floodQueue.emplace_back(static_cast<int>(nIndex));
			}
		}

		const bool hasAlpha = spriteImage.HasAlpha();
		for (int row = 0; row < CyclopediaSatelliteBasePixelsPerSquare; ++row) {
			for (int col = 0; col < CyclopediaSatelliteBasePixelsPerSquare; ++col) {
				const int index = row * CyclopediaSatelliteBasePixelsPerSquare + col;
				const int sourceX = footprintOriginX + col;
				const int sourceY = footprintOriginY + row;
				if (sourceX < 0 || sourceY < 0 || sourceX >= width || sourceY >= height) {
					tinySprite[index] = SatelliteTinyPixel {};
					continue;
				}

				const size_t spriteIndex = static_cast<size_t>(sourceY) * static_cast<size_t>(width) + static_cast<size_t>(sourceX);
				const bool forceTransparent = edgeBlackMask[spriteIndex] != 0;
				const uint8_t alpha = forceTransparent ? 0 : (hasAlpha ? spriteImage.GetAlpha(sourceX, sourceY) : 255);
				tinySprite[index].a = alpha;
				if (alpha == 0) {
					tinySprite[index].r = 0;
					tinySprite[index].g = 0;
					tinySprite[index].b = 0;
					continue;
				}

				tinySprite[index].r = spriteImage.GetRed(sourceX, sourceY);
				tinySprite[index].g = spriteImage.GetGreen(sourceX, sourceY);
				tinySprite[index].b = spriteImage.GetBlue(sourceX, sourceY);
			}
		}

		return true;
	}

	bool getTinySpriteForSpriteId(
		const SatelliteSpriteCacheKey &spriteKey,
		std::unordered_map<SatelliteSpriteCacheKey, SatelliteTinySprite, SatelliteSpriteCacheKeyHash> &cache,
		SatelliteTinySprite &tinySprite
	) {
		if (auto it = cache.find(spriteKey); it != cache.end()) {
			tinySprite = it->second;
			return true;
		}

		const wxImage spriteImage = g_spriteAppearances.getWxImageBySpriteId(static_cast<int>(spriteKey.spriteId), true);
		if (!spriteImage.IsOk()) {
			return false;
		}

		SatelliteTinySprite sampled {};
		if (!sampleSpriteToTiny(spriteImage, spriteKey.footprintOriginX, spriteKey.footprintOriginY, sampled)) {
			return false;
		}

		cache.emplace(spriteKey, sampled);
		tinySprite = sampled;
		return true;
	}

	bool resampleTinySprite(const SatelliteTinySprite &tinySprite, const int outputPixelsPerSquare, SatelliteSampledSprite &sampledSprite) {
		if (outputPixelsPerSquare <= 0 || outputPixelsPerSquare > CyclopediaSatelliteBasePixelsPerSquare) {
			return false;
		}

		const size_t outputPixelCount = static_cast<size_t>(outputPixelsPerSquare) * static_cast<size_t>(outputPixelsPerSquare);
		sampledSprite.assign(outputPixelCount, SatelliteTinyPixel {});

		const auto clampChannel = [](const double value) {
			return static_cast<unsigned char>(std::clamp<int>(static_cast<int>(std::lround(value)), 0, 255));
		};

		for (int row = 0; row < outputPixelsPerSquare; ++row) {
			const double sy0 = (static_cast<double>(row) * CyclopediaSatelliteBasePixelsPerSquare) / outputPixelsPerSquare;
			const double sy1 = (static_cast<double>(row + 1) * CyclopediaSatelliteBasePixelsPerSquare) / outputPixelsPerSquare;
			const int syBegin = std::clamp(static_cast<int>(std::floor(sy0)), 0, CyclopediaSatelliteBasePixelsPerSquare - 1);
			const int syEnd = std::clamp(static_cast<int>(std::ceil(sy1)), syBegin + 1, CyclopediaSatelliteBasePixelsPerSquare);

			for (int col = 0; col < outputPixelsPerSquare; ++col) {
				const size_t outIndex = static_cast<size_t>(row) * static_cast<size_t>(outputPixelsPerSquare) + static_cast<size_t>(col);
				const double sx0 = (static_cast<double>(col) * CyclopediaSatelliteBasePixelsPerSquare) / outputPixelsPerSquare;
				const double sx1 = (static_cast<double>(col + 1) * CyclopediaSatelliteBasePixelsPerSquare) / outputPixelsPerSquare;
				const int sxBegin = std::clamp(static_cast<int>(std::floor(sx0)), 0, CyclopediaSatelliteBasePixelsPerSquare - 1);
				const int sxEnd = std::clamp(static_cast<int>(std::ceil(sx1)), sxBegin + 1, CyclopediaSatelliteBasePixelsPerSquare);

				const double cellArea = std::max((sx1 - sx0) * (sy1 - sy0), 0.000001);
				double weightedRed = 0.0;
				double weightedGreen = 0.0;
				double weightedBlue = 0.0;
				double weightedAlpha = 0.0;
				double weightedColorAlpha = 0.0;

				for (int sy = syBegin; sy < syEnd; ++sy) {
					const double yOverlap = std::max(0.0, std::min(sy1, static_cast<double>(sy + 1)) - std::max(sy0, static_cast<double>(sy)));
					if (yOverlap <= 0.0) {
						continue;
					}

					for (int sx = sxBegin; sx < sxEnd; ++sx) {
						const double xOverlap = std::max(0.0, std::min(sx1, static_cast<double>(sx + 1)) - std::max(sx0, static_cast<double>(sx)));
						if (xOverlap <= 0.0) {
							continue;
						}

						const double areaWeight = xOverlap * yOverlap;
						const size_t sourceIndex = static_cast<size_t>(sy) * CyclopediaSatelliteBasePixelsPerSquare + static_cast<size_t>(sx);
						const SatelliteTinyPixel &sourcePixel = tinySprite[sourceIndex];
						weightedAlpha += areaWeight * static_cast<double>(sourcePixel.a);

						if (sourcePixel.a == 0) {
							continue;
						}

						const double alphaWeight = areaWeight * (static_cast<double>(sourcePixel.a) / 255.0);
						weightedColorAlpha += alphaWeight;
						weightedRed += static_cast<double>(sourcePixel.r) * alphaWeight;
						weightedGreen += static_cast<double>(sourcePixel.g) * alphaWeight;
						weightedBlue += static_cast<double>(sourcePixel.b) * alphaWeight;
					}
				}

				SatelliteTinyPixel &outPixel = sampledSprite[outIndex];
				outPixel.a = clampChannel(weightedAlpha / cellArea);
				if (weightedColorAlpha > 0.0) {
					outPixel.r = clampChannel(weightedRed / weightedColorAlpha);
					outPixel.g = clampChannel(weightedGreen / weightedColorAlpha);
					outPixel.b = clampChannel(weightedBlue / weightedColorAlpha);
					continue;
				}

				const int fallbackX = std::clamp(static_cast<int>(std::lround((sx0 + sx1) * 0.5 - 0.5)), 0, CyclopediaSatelliteBasePixelsPerSquare - 1);
				const int fallbackY = std::clamp(static_cast<int>(std::lround((sy0 + sy1) * 0.5 - 0.5)), 0, CyclopediaSatelliteBasePixelsPerSquare - 1);
				const size_t fallbackIndex = static_cast<size_t>(fallbackY) * CyclopediaSatelliteBasePixelsPerSquare + static_cast<size_t>(fallbackX);
				outPixel.r = tinySprite[fallbackIndex].r;
				outPixel.g = tinySprite[fallbackIndex].g;
				outPixel.b = tinySprite[fallbackIndex].b;
			}
		}

		return true;
	}

	bool getSampledSpriteForSpriteId(
		const SatelliteSpriteCacheKey &spriteKey,
		const int outputPixelsPerSquare,
		std::unordered_map<SatelliteSpriteCacheKey, SatelliteTinySprite, SatelliteSpriteCacheKeyHash> &tinyCache,
		std::unordered_map<SatelliteSampledSpriteCacheKey, SatelliteSampledSprite, SatelliteSampledSpriteCacheKeyHash> &sampledCache,
		const SatelliteSampledSprite*&sampledSprite
	) {
		sampledSprite = nullptr;
		if (outputPixelsPerSquare <= 0 || outputPixelsPerSquare > CyclopediaSatelliteBasePixelsPerSquare) {
			return false;
		}

		const SatelliteSampledSpriteCacheKey cacheKey {
			spriteKey,
			outputPixelsPerSquare
		};
		if (auto it = sampledCache.find(cacheKey); it != sampledCache.end()) {
			sampledSprite = &it->second;
			return true;
		}

		SatelliteTinySprite tinySprite {};
		if (!getTinySpriteForSpriteId(spriteKey, tinyCache, tinySprite)) {
			return false;
		}

		SatelliteSampledSprite sampled;
		if (!resampleTinySprite(tinySprite, outputPixelsPerSquare, sampled)) {
			return false;
		}

		auto [it, inserted] = sampledCache.emplace(cacheKey, std::move(sampled));
		if (!inserted) {
			return false;
		}

		sampledSprite = &it->second;
		return true;
	}

	void collectCyclopediaDrawItems(const Tile* tile, std::vector<const Item*> &drawItems, const bool includeGround) {
		drawItems.clear();
		if (!tile) {
			return;
		}

		drawItems.reserve(tile->items.size() + 1);
		if (includeGround && tile->ground) {
			drawItems.push_back(tile->ground);
		}

		for (const Item* item : tile->items) {
			if (item && item->isBorder()) {
				drawItems.push_back(item);
			}
		}

		for (const Item* item : tile->items) {
			if (item && !item->isBorder()) {
				drawItems.push_back(item);
			}
		}
	}

	void blendTinyPixel(unsigned char &dstR, unsigned char &dstG, unsigned char &dstB, unsigned char &dstA, const SatelliteTinyPixel &src) {
		const int srcAlpha = static_cast<int>(src.a);
		if (srcAlpha <= 0) {
			return;
		}

		if (srcAlpha >= 255) {
			dstR = src.r;
			dstG = src.g;
			dstB = src.b;
			dstA = 255;
			return;
		}

		const int dstAlpha = static_cast<int>(dstA);
		const int invSrcAlpha = 255 - srcAlpha;
		const int outAlpha = srcAlpha + ((dstAlpha * invSrcAlpha + 127) / 255);
		if (outAlpha <= 0) {
			dstR = 0;
			dstG = 0;
			dstB = 0;
			dstA = 0;
			return;
		}

		const int dstScaledAlpha = (dstAlpha * invSrcAlpha + 127) / 255;
		const int outRed = static_cast<int>(src.r) * srcAlpha + static_cast<int>(dstR) * dstScaledAlpha;
		const int outGreen = static_cast<int>(src.g) * srcAlpha + static_cast<int>(dstG) * dstScaledAlpha;
		const int outBlue = static_cast<int>(src.b) * srcAlpha + static_cast<int>(dstB) * dstScaledAlpha;

		dstR = static_cast<unsigned char>((outRed + outAlpha / 2) / outAlpha);
		dstG = static_cast<unsigned char>((outGreen + outAlpha / 2) / outAlpha);
		dstB = static_cast<unsigned char>((outBlue + outAlpha / 2) / outAlpha);
		dstA = static_cast<unsigned char>(outAlpha);
	}

	bool isCyclopediaRedDominantColor(const unsigned char red, const unsigned char green, const unsigned char blue) {
		return red >= 180
			&& static_cast<int>(red) * 5 >= static_cast<int>(green) * 8
			&& static_cast<int>(red) * 5 >= static_cast<int>(blue) * 8;
	}

	bool resolveCyclopediaSatelliteBasePixel(const unsigned char red, const unsigned char green, const unsigned char blue, SatelliteTinyPixel &pixel) {
		pixel.a = 255;
		if (isCyclopediaRedDominantColor(red, green, blue)) {
			pixel.r = 113;
			pixel.g = 110;
			pixel.b = 110;
			return true;
		}

		if (blue >= 120 && blue >= red + 35 && green >= 70) {
			pixel.r = CyclopediaSatelliteSeaColorR;
			pixel.g = CyclopediaSatelliteSeaColorG;
			pixel.b = CyclopediaSatelliteSeaColorB;
			return true;
		}

		const int minChannel = std::min({ static_cast<int>(red), static_cast<int>(green), static_cast<int>(blue) });
		const int maxChannel = std::max({ static_cast<int>(red), static_cast<int>(green), static_cast<int>(blue) });
		if (minChannel >= 200) {
			pixel.r = 231;
			pixel.g = 240;
			pixel.b = 242;
			return true;
		}
		if (maxChannel - minChannel <= 24 && maxChannel >= 80) {
			const bool lightStone = maxChannel >= 135;
			pixel.r = lightStone ? 130 : 113;
			pixel.g = lightStone ? 131 : 110;
			pixel.b = lightStone ? 131 : 110;
			return true;
		}
		if (red >= 190 && green >= 150 && blue >= 95 && red >= green && green >= blue) {
			pixel.r = 227;
			pixel.g = 204;
			pixel.b = 141;
			return true;
		}
		if (green >= red + 35 && green >= blue + 25) {
			if (green >= 185) {
				pixel.r = 76;
				pixel.g = 109;
				pixel.b = 30;
			} else if (green >= 135) {
				pixel.r = 78;
				pixel.g = 112;
				pixel.b = 34;
			} else {
				pixel.r = 75;
				pixel.g = 101;
				pixel.b = 34;
			}
			return true;
		}
		if (red >= green && green >= blue && red >= 90) {
			pixel.r = 90;
			pixel.g = 50;
			pixel.b = 22;
			return true;
		}

		const int luminance = (static_cast<int>(red) * 30 + static_cast<int>(green) * 59 + static_cast<int>(blue) * 11 + 50) / 100;
		pixel.r = static_cast<unsigned char>(std::clamp((static_cast<int>(red) + luminance) / 4, 0, 255));
		pixel.g = static_cast<unsigned char>(std::clamp((static_cast<int>(green) + luminance * 2) / 4, 0, 255));
		pixel.b = static_cast<unsigned char>(std::clamp((static_cast<int>(blue) + luminance) / 4, 0, 255));
		return true;
	}

	bool getCyclopediaSatelliteBasePixel(
		const unsigned char* minimapData,
		const unsigned char* minimapAlpha,
		const int minimapWidth,
		const int minimapHeight,
		const int x,
		const int y,
		SatelliteTinyPixel &pixel
	) {
		if (!minimapData || x < 0 || y < 0 || x >= minimapWidth || y >= minimapHeight) {
			return false;
		}

		const size_t pixelIndex = static_cast<size_t>(y) * static_cast<size_t>(minimapWidth) + static_cast<size_t>(x);
		if (minimapAlpha && minimapAlpha[pixelIndex] == 0) {
			return false;
		}

		const size_t rgbIndex = pixelIndex * rme::PixelFormatRGB;
		return resolveCyclopediaSatelliteBasePixel(minimapData[rgbIndex], minimapData[rgbIndex + 1], minimapData[rgbIndex + 2], pixel);
	}

	void fillCyclopediaSatelliteTileBase(
		unsigned char* outData,
		unsigned char* outAlpha,
		const int outWidth,
		const int tileX,
		const int tileY,
		const int outputPixelsPerSquare,
		const SatelliteTinyPixel &pixel
	) {
		for (int py = 0; py < outputPixelsPerSquare; ++py) {
			for (int px = 0; px < outputPixelsPerSquare; ++px) {
				const size_t outPixelIndex = static_cast<size_t>(tileY * outputPixelsPerSquare + py) * static_cast<size_t>(outWidth)
					+ static_cast<size_t>(tileX * outputPixelsPerSquare + px);
				const size_t outIndex = outPixelIndex * rme::PixelFormatRGB;
				outData[outIndex] = pixel.r;
				outData[outIndex + 1] = pixel.g;
				outData[outIndex + 2] = pixel.b;
				outAlpha[outPixelIndex] = pixel.a;
			}
		}
	}

	void finalizeCyclopediaSatellitePixels(wxImage &image, const bool fillEmptyWithSea) {
		if (!image.IsOk() || !image.HasAlpha()) {
			return;
		}

		unsigned char* data = image.GetData();
		unsigned char* alpha = image.GetAlpha();
		if (!data || !alpha) {
			return;
		}

		const int width = image.GetWidth();
		const int height = image.GetHeight();
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				const size_t pixelIndex = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
				const size_t rgbIndex = pixelIndex * rme::PixelFormatRGB;
				if (alpha[pixelIndex] > 0) {
					alpha[pixelIndex] = 255;
					continue;
				}

				if (!fillEmptyWithSea) {
					continue;
				}

				data[rgbIndex] = CyclopediaSatelliteSeaColorR;
				data[rgbIndex + 1] = CyclopediaSatelliteSeaColorG;
				data[rgbIndex + 2] = CyclopediaSatelliteSeaColorB;
				alpha[pixelIndex] = 255;
			}
		}
	}

	bool buildCyclopediaSatelliteChunk(Map &map, const CyclopediaChunkArea &area, const double pixelsPerSquare, const wxImage &minimapChunk, wxImage &image) {
		if (area.width <= 0 || area.height <= 0) {
			return false;
		}

		const double requestedPixelsPerSquare = std::clamp(pixelsPerSquare, CyclopediaMinPixelsPerSquare, static_cast<double>(CyclopediaSatelliteBasePixelsPerSquare));
		const int outputPixelsPerSquare = std::clamp(static_cast<int>(std::ceil(requestedPixelsPerSquare)), 1, CyclopediaSatelliteBasePixelsPerSquare);
		const int outWidth = area.width * outputPixelsPerSquare;
		const int outHeight = area.height * outputPixelsPerSquare;
		if (!image.Create(outWidth, outHeight, false)) {
			return false;
		}
		image.InitAlpha();

		unsigned char* outData = image.GetData();
		unsigned char* outAlpha = image.GetAlpha();
		if (!outData || !outAlpha) {
			return false;
		}

		const unsigned char* minimapData = minimapChunk.IsOk() ? minimapChunk.GetData() : nullptr;
		const unsigned char* minimapAlpha = (minimapChunk.IsOk() && minimapChunk.HasAlpha()) ? minimapChunk.GetAlpha() : nullptr;
		const int minimapWidth = minimapChunk.IsOk() ? minimapChunk.GetWidth() : 0;
		const int minimapHeight = minimapChunk.IsOk() ? minimapChunk.GetHeight() : 0;
		const bool opaqueSeaBackground = area.floor == CyclopediaOpaqueSeaFloor;

		std::memset(outData, 0, static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight) * rme::PixelFormatRGB);
		std::memset(outAlpha, 0, static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight));

		std::unordered_map<SatelliteSpriteCacheKey, SatelliteTinySprite, SatelliteSpriteCacheKeyHash> spriteTinyCache;
		spriteTinyCache.reserve(4096);
		std::unordered_map<SatelliteSampledSpriteCacheKey, SatelliteSampledSprite, SatelliteSampledSpriteCacheKeyHash> spriteSampledCache;
		spriteSampledCache.reserve(8192);
		std::vector<const Item*> drawItems;
		drawItems.reserve(64);

		for (int y = 0; y < area.height; ++y) {
			for (int x = 0; x < area.width; ++x) {
				const Position position(area.startX + x, area.startY + y, area.floor);
				Tile* tile = getCyclopediaMapTile(map, position);
				if (!hasCyclopediaTileData(tile)) {
					continue;
				}

				SatelliteTinyPixel basePixel {};
				if (getCyclopediaSatelliteBasePixel(minimapData, minimapAlpha, minimapWidth, minimapHeight, x, y, basePixel)) {
					fillCyclopediaSatelliteTileBase(outData, outAlpha, outWidth, x, y, outputPixelsPerSquare, basePixel);
				}

				for (int sourceDy = 0; sourceDy <= 1; ++sourceDy) {
					for (int sourceDx = 0; sourceDx <= 1; ++sourceDx) {
						const Position sourcePosition(position.x + sourceDx, position.y + sourceDy, area.floor);
						Tile* sourceTile = getCyclopediaMapTile(map, sourcePosition);
						if (!hasCyclopediaTileData(sourceTile)) {
							continue;
						}

						collectCyclopediaDrawItems(sourceTile, drawItems, true);
						for (const Item* item : drawItems) {
							SatelliteSpriteCacheKey spriteKey {};
							if (!resolveCyclopediaItemSpriteSample(sourcePosition, sourceTile, item, spriteKey)) {
								continue;
							}

							spriteKey.footprintOriginX -= sourceDx * rme::SpritePixels;
							spriteKey.footprintOriginY -= sourceDy * rme::SpritePixels;

							const SatelliteSampledSprite* sampledSprite = nullptr;
							if (!getSampledSpriteForSpriteId(spriteKey, outputPixelsPerSquare, spriteTinyCache, spriteSampledCache, sampledSprite) || !sampledSprite) {
								continue;
							}

							for (int py = 0; py < outputPixelsPerSquare; ++py) {
								for (int px = 0; px < outputPixelsPerSquare; ++px) {
									const size_t tinyIndex = static_cast<size_t>(py) * static_cast<size_t>(outputPixelsPerSquare) + static_cast<size_t>(px);
									const SatelliteTinyPixel &tinyPixel = (*sampledSprite)[tinyIndex];

									const size_t outPixelIndex = static_cast<size_t>(y * outputPixelsPerSquare + py) * static_cast<size_t>(outWidth)
										+ static_cast<size_t>(x * outputPixelsPerSquare + px);
									const size_t outIndex = outPixelIndex * rme::PixelFormatRGB;
									blendTinyPixel(outData[outIndex], outData[outIndex + 1], outData[outIndex + 2], outAlpha[outPixelIndex], tinyPixel);
								}
							}
						}
					}
				}
			}
		}

		finalizeCyclopediaSatellitePixels(image, opaqueSeaBackground);
		if (!resampleCyclopediaChunk(image, area.width, area.height, requestedPixelsPerSquare)) {
			return false;
		}
		finalizeCyclopediaSatellitePixels(image, false);

		return true;
	}

	bool isShopHouseName(const std::string_view name) {
		return name.contains("(Shop)") || name.contains("(shop)");
	}

	bool getHousePreviewFloor(const House &house, int &floor) {
		if (house.getTiles().empty()) {
			return false;
		}

		std::unordered_map<int, uint32_t> floorCount;
		floorCount.reserve(4);
		for (const auto &position : house.getTiles()) {
			++floorCount[position.z];
		}

		const auto exit = house.getExit();
		if (auto it = floorCount.find(exit.z); it != floorCount.end()) {
			floor = exit.z;
			return true;
		}

		uint32_t maxCount = 0;
		int selectedFloor = rme::MapGroundLayer;
		for (const auto &[z, count] : floorCount) {
			if (count > maxCount) {
				maxCount = count;
				selectedFloor = z;
			}
		}

		floor = selectedFloor;
		return maxCount > 0;
	}

	bool isHousePreviewTopItem(const Item* item) {
		return item && item->isAlwaysOnBottom() && item->getTopOrder() >= 3;
	}

	bool isHousePreviewBottomItem(const Item* item) {
		return item && (item->isBorder() || (item->isAlwaysOnBottom() && item->getTopOrder() < 3));
	}

	template <typename Callback>
	void forEachHousePreviewItemInDrawOrder(const Tile &tile, Callback &&callback) {
		callback(tile.ground);

		for (const Item* item : tile.items) {
			if (isHousePreviewBottomItem(item)) {
				callback(item);
			}
		}

		for (const Item* item : tile.items) {
			if (!isHousePreviewBottomItem(item) && !isHousePreviewTopItem(item)) {
				callback(item);
			}
		}

		for (const Item* item : tile.items) {
			if (isHousePreviewTopItem(item)) {
				callback(item);
			}
		}
	}

	bool fillHousePreviewItemData(const Position &position, const Tile* tile, const Item* item, HousePreviewItemData &itemData) {
		if (!tile || !item) {
			return false;
		}

		const ItemType &type = item->getItemType();
		int patternX = 0;
		int patternY = 0;
		int patternZ = 0;
		int subtype = -1;
		int frame = 0;
		if (!resolveCyclopediaItemPatterns(position, tile, item, patternX, patternY, patternZ, subtype, frame)) {
			return false;
		}

		itemData.clientId = type.clientID;
		itemData.xPattern = static_cast<uint8_t>(std::max(0, patternX));
		itemData.yPattern = static_cast<uint8_t>(std::max(0, patternY));
		itemData.zPattern = static_cast<uint8_t>(std::max(0, patternZ));
		return true;
	}

	bool buildHousePreviewData(Map &map, const House &house, HousePreviewData &previewData) {
		int floor = rme::MapGroundLayer;
		if (!getHousePreviewFloor(house, floor)) {
			return false;
		}

		int minX = std::numeric_limits<int>::max();
		int minY = std::numeric_limits<int>::max();
		int maxX = std::numeric_limits<int>::min();
		int maxY = std::numeric_limits<int>::min();

		for (const auto &position : house.getTiles()) {
			if (position.z != floor) {
				continue;
			}

			if (position.x < minX) {
				minX = position.x;
			}
			if (position.y < minY) {
				minY = position.y;
			}
			if (position.x > maxX) {
				maxX = position.x;
			}
			if (position.y > maxY) {
				maxY = position.y;
			}
		}

		if (minX > maxX || minY > maxY) {
			return false;
		}

		minX -= HousePreviewContextMargin;
		minY -= HousePreviewContextMargin;
		maxX += HousePreviewContextMargin;
		maxY += HousePreviewContextMargin;

		const int width = maxX - minX + 1;
		const int height = maxY - minY + 1;
		if (width <= 0 || height <= 0) {
			return false;
		}

		const size_t colorsSize = static_cast<size_t>(width) * static_cast<size_t>(height);
		if (colorsSize == 0) {
			return false;
		}

		std::vector<uint8_t> colors(colorsSize, 0);
		std::vector<HousePreviewTileData> tiles;
		tiles.reserve(colorsSize);

		bool hasData = false;
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				const Position position(minX + x, minY + y, floor);
				Tile* tile = getCyclopediaMapTile(map, position);
				if (!tile || (!tile->ground && tile->items.empty())) {
					continue;
				}

				const auto index = static_cast<size_t>(y * width + x);
				colors[index] = tile->getMiniMapColor();

				HousePreviewTileData tileData;
				tileData.x = static_cast<uint32_t>(x);
				tileData.y = static_cast<uint32_t>(y);
				tileData.color = colors[index];
				tileData.isHouseTile = tile->getHouseID() == house.id;
				tileData.items.reserve(HousePreviewMaxItemsPerTile);

				const auto addPreviewItem = [&tileData, &position, tile](const Item* item) {
					if (!item || tileData.items.size() >= HousePreviewMaxItemsPerTile) {
						return;
					}

					HousePreviewItemData itemData;
					if (!fillHousePreviewItemData(position, tile, item, itemData)) {
						return;
					}

					tileData.items.emplace_back(itemData);
				};

				forEachHousePreviewItemInDrawOrder(*tile, addPreviewItem);

				tiles.emplace_back(std::move(tileData));
				hasData = true;
			}
		}

		if (!hasData) {
			return false;
		}

		previewData.width = static_cast<uint32_t>(width);
		previewData.height = static_cast<uint32_t>(height);
		previewData.floor = static_cast<uint32_t>(floor);
		previewData.colors = std::move(colors);
		previewData.tiles = std::move(tiles);
		return true;
	}

	bool collectHousePreviewClientIds(
		const Position &position,
		const Tile* tile,
		std::vector<uint32_t> &clientIds,
		size_t* droppedByCap = nullptr,
		size_t* droppedInvalid = nullptr
	) {
		clientIds.clear();
		if (droppedByCap) {
			*droppedByCap = 0;
		}
		if (droppedInvalid) {
			*droppedInvalid = 0;
		}

		if (!tile) {
			return false;
		}

		clientIds.reserve(HousePreviewMaxItemsPerTile);
		const auto addPreviewItem = [&clientIds, &position, droppedByCap, droppedInvalid, tile](const Item* item) {
			if (!item) {
				return;
			}
			if (clientIds.size() >= HousePreviewMaxItemsPerTile) {
				if (droppedByCap) {
					++(*droppedByCap);
				}
				return;
			}

			HousePreviewItemData itemData;
			if (!fillHousePreviewItemData(position, tile, item, itemData) || itemData.clientId == 0) {
				if (droppedInvalid) {
					++(*droppedInvalid);
				}
				return;
			}

			clientIds.emplace_back(itemData.clientId);
		};

		forEachHousePreviewItemInDrawOrder(*tile, addPreviewItem);

		return !clientIds.empty();
	}

	bool buildStaticMapHousePreviewData(
		Map &map,
		const House &house,
		clienteditor::protobuf::staticmapdata::HouseEntry &houseEntry,
		const StaticMapHouseTemplate* houseTemplate = nullptr
	) {
		struct SerializedTile final {
			std::vector<uint32_t> itemValues;
			uint32_t skip = 0;
		};

		std::vector<SerializedTile> serializedTiles;
		std::vector<uint32_t> tileClientIds;
		std::vector<uint32_t> altTileClientIds;
		std::vector<uint32_t> rawTileClientIds;
		std::vector<uint32_t> rawAltTileClientIds;

		int minX = 0;
		int minY = 0;
		int minZ = 0;
		int width = 0;
		int height = 0;
		int floorCount = 0;
		uint64_t totalCells = 0;
		size_t templateTilesCompared = 0;
		size_t templateTileMismatchCount = 0;
		size_t templateSouthTilesCompared = 0;
		size_t templateSouthTileMismatchCount = 0;
		size_t templateTilesMissingItems = 0;
		size_t missingMapTileCount = 0;
		size_t droppedByCapCount = 0;
		size_t droppedInvalidCount = 0;
		size_t detailedMismatchLogs = 0;
		constexpr size_t HousePreviewDetailedLogLimit = 64;
		const bool houseDebugTarget = isHouseDebugTarget(house);
		size_t currentMappingExactMatches = 0;
		size_t alternativeMappingExactMatches = 0;
		size_t alternativeMappingBetterMatches = 0;

		if (houseTemplate && houseTemplate->isValid()) {
			minX = houseTemplate->minX;
			minY = houseTemplate->minY;
			minZ = houseTemplate->minZ;
			width = houseTemplate->width;
			height = houseTemplate->height;
			floorCount = houseTemplate->floorCount;
			totalCells = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * static_cast<uint64_t>(floorCount);
			if (totalCells == 0 || totalCells > std::numeric_limits<uint32_t>::max()) {
				return false;
			}

			const int floorArea = width * height;
			serializedTiles.reserve(houseTemplate->tiles.size());
			for (const StaticMapTemplateTile &templateTile : houseTemplate->tiles) {
				if (templateTile.index >= totalCells || floorArea <= 0 || height <= 0) {
					continue;
				}

				const auto linearIndex = static_cast<int>(templateTile.index);
				const int floorOffset = linearIndex / floorArea;
				const int planeIndex = linearIndex % floorArea;
				const int xOffset = planeIndex / height;
				const int yOffset = planeIndex % height;
				const Position position(minX + xOffset, minY + yOffset, minZ + floorOffset);
				const int alternativeXOffset = planeIndex % width;
				const int alternativeYOffset = planeIndex / width;
				const Position alternativePosition(minX + alternativeXOffset, minY + alternativeYOffset, minZ + floorOffset);

				SerializedTile serializedTile;
				serializedTile.skip = templateTile.skip;
				size_t droppedByCapForTile = 0;
				size_t droppedInvalidForTile = 0;
				std::vector<uint32_t> sampledTileItemValues;
				Tile* tile = getCyclopediaMapTile(map, position);
				Tile* alternativeTile = getCyclopediaMapTile(map, alternativePosition);
				if (!tile) {
					++missingMapTileCount;
				}
				if (tile && collectHousePreviewClientIds(position, tile, tileClientIds, &droppedByCapForTile, &droppedInvalidForTile)) {
					sampledTileItemValues = tileClientIds;
				}
				droppedByCapCount += droppedByCapForTile;
				droppedInvalidCount += droppedInvalidForTile;

				std::vector<uint32_t> alternativeItemValues;
				if (alternativeYOffset >= 0 && alternativeYOffset < height) {
					size_t droppedByCapAlternative = 0;
					size_t droppedInvalidAlternative = 0;
					if (alternativeTile && collectHousePreviewClientIds(alternativePosition, alternativeTile, altTileClientIds, &droppedByCapAlternative, &droppedInvalidAlternative)) {
						alternativeItemValues = altTileClientIds;
					}
				}

				++templateTilesCompared;
				const bool isSouthEdge = (yOffset == height - 1);
				if (isSouthEdge) {
					++templateSouthTilesCompared;
				}

				const bool currentMappingExact = (templateTile.itemValues == sampledTileItemValues);
				if (currentMappingExact) {
					++currentMappingExactMatches;
				}
				const bool alternativeMappingExact = (templateTile.itemValues == alternativeItemValues);
				if (alternativeMappingExact) {
					++alternativeMappingExactMatches;
					if (!currentMappingExact) {
						++alternativeMappingBetterMatches;
					}
				}

				if (!currentMappingExact) {
					++templateTileMismatchCount;
					if (isSouthEdge) {
						++templateSouthTileMismatchCount;
					}
					if (!templateTile.itemValues.empty() && sampledTileItemValues.empty()) {
						++templateTilesMissingItems;
					}
					if (houseDebugTarget && detailedMismatchLogs < HousePreviewDetailedLogLimit) {
						collectRawTileClientIds(tile, rawTileClientIds);
						collectRawTileClientIds(getCyclopediaMapTile(map, alternativePosition), rawAltTileClientIds);
						spdlog::info(
							"[house-debug] house={} name='{}' tile_mismatch idx={} rel=({}, {}, {}) world=({}, {}, {}) south={} template_items={} sampled_items={} raw_items={} "
							"alt_rel=({}, {}, {}) alt_world=({}, {}, {}) alt_items={} alt_raw_items={} written_items={} drop_cap={} drop_invalid={} tile_exists={}",
							house.id,
							house.name,
							templateTile.index,
							xOffset,
							yOffset,
							floorOffset,
							position.x,
							position.y,
							position.z,
							isSouthEdge,
							formatHousePreviewItemValues(templateTile.itemValues),
							formatHousePreviewItemValues(sampledTileItemValues),
							formatHousePreviewItemValues(rawTileClientIds),
							alternativeXOffset,
							alternativeYOffset,
							floorOffset,
							alternativePosition.x,
							alternativePosition.y,
							alternativePosition.z,
							formatHousePreviewItemValues(alternativeItemValues),
							formatHousePreviewItemValues(rawAltTileClientIds),
							formatHousePreviewItemValues(PreserveTemplateStaticMapHouseItems ? templateTile.itemValues : sampledTileItemValues),
							droppedByCapForTile,
							droppedInvalidForTile,
							tile != nullptr
						);
						++detailedMismatchLogs;
					}
				} else if (houseDebugTarget && droppedByCapForTile > 0 && detailedMismatchLogs < HousePreviewDetailedLogLimit) {
					spdlog::info(
						"[house-debug] house={} name='{}' tile_capped idx={} rel=({}, {}, {}) world=({}, {}, {}) sampled_items={} written_items={} dropped_by_cap={}",
						house.id,
						house.name,
						templateTile.index,
						xOffset,
						yOffset,
						floorOffset,
						position.x,
						position.y,
						position.z,
						formatHousePreviewItemValues(sampledTileItemValues),
						formatHousePreviewItemValues(PreserveTemplateStaticMapHouseItems ? templateTile.itemValues : sampledTileItemValues),
						droppedByCapForTile
					);
					++detailedMismatchLogs;
				}

				serializedTile.itemValues = PreserveTemplateStaticMapHouseItems ? templateTile.itemValues : sampledTileItemValues;

				serializedTiles.emplace_back(std::move(serializedTile));
			}
		} else {
			const PositionVector houseTiles = selectConnectedHousePreviewTiles(house);
			if (houseTiles.empty()) {
				return false;
			}

			std::unordered_set<Position, PositionHash> houseTileSet;
			houseTileSet.reserve(houseTiles.size() * 2);
			for (const auto &position : houseTiles) {
				houseTileSet.insert(position);
			}

			int maxX = std::numeric_limits<int>::min();
			int maxY = std::numeric_limits<int>::min();
			int maxZ = std::numeric_limits<int>::min();
			minX = std::numeric_limits<int>::max();
			minY = std::numeric_limits<int>::max();
			minZ = std::numeric_limits<int>::max();

			for (const auto &position : houseTiles) {
				minX = std::min(minX, position.x);
				minY = std::min(minY, position.y);
				minZ = std::min(minZ, position.z);
				maxX = std::max(maxX, position.x);
				maxY = std::max(maxY, position.y);
				maxZ = std::max(maxZ, position.z);
			}

			if (minX > maxX || minY > maxY || minZ > maxZ) {
				return false;
			}

			minX -= HouseStaticMapContextMargin;
			minY -= HouseStaticMapContextMargin;
			maxX += HouseStaticMapContextMargin;
			maxY += HouseStaticMapContextMargin;

			width = maxX - minX + 1;
			height = maxY - minY + 1;
			floorCount = maxZ - minZ + 1;
			if (width <= 0 || height <= 0 || floorCount <= 0) {
				return false;
			}

			totalCells = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * static_cast<uint64_t>(floorCount);
			if (totalCells == 0 || totalCells > std::numeric_limits<uint32_t>::max()) {
				return false;
			}

			serializedTiles.reserve(static_cast<size_t>(width) * static_cast<size_t>(height));
			uint32_t pendingSkip = 0;

			for (int floor = minZ; floor <= maxZ; ++floor) {
				for (int x = minX; x <= maxX; ++x) {
					for (int y = minY; y <= maxY; ++y) {
						const Position position(x, y, floor);
						if (!isStaticMapHousePreviewContextTile(position, houseTileSet)) {
							if (pendingSkip < std::numeric_limits<uint32_t>::max()) {
								++pendingSkip;
							}
							continue;
						}

						Tile* tile = getCyclopediaMapTile(map, position);
						size_t droppedByCapForTile = 0;
						size_t droppedInvalidForTile = 0;
						const bool hasPreviewItems = tile && collectHousePreviewClientIds(position, tile, tileClientIds, &droppedByCapForTile, &droppedInvalidForTile);
						droppedByCapCount += droppedByCapForTile;
						droppedInvalidCount += droppedInvalidForTile;
						if (!tile || !hasPreviewItems) {
							if (pendingSkip < std::numeric_limits<uint32_t>::max()) {
								++pendingSkip;
							}
							continue;
						}

						if (serializedTiles.empty()) {
							if (pendingSkip > 0) {
								SerializedTile leadingEmptyTile;
								leadingEmptyTile.skip = pendingSkip - 1;
								serializedTiles.emplace_back(std::move(leadingEmptyTile));
								pendingSkip = 0;
							}
						} else {
							serializedTiles.back().skip = pendingSkip;
							pendingSkip = 0;
						}

						SerializedTile serializedTile;
						serializedTile.itemValues = tileClientIds;
						serializedTiles.emplace_back(std::move(serializedTile));
					}
				}
			}

			if (serializedTiles.empty()) {
				SerializedTile tile;
				tile.skip = static_cast<uint32_t>(totalCells - 1);
				serializedTiles.emplace_back(std::move(tile));
			} else {
				serializedTiles.back().skip = pendingSkip;
			}
		}

		if (serializedTiles.empty()) {
			SerializedTile tile;
			tile.skip = static_cast<uint32_t>(totalCells - 1);
			serializedTiles.emplace_back(std::move(tile));
		}

		size_t tilesWithItems = 0;
		size_t maxItemsInTile = 0;
		for (const SerializedTile &serializedTile : serializedTiles) {
			if (!serializedTile.itemValues.empty()) {
				++tilesWithItems;
			}
			maxItemsInTile = std::max(maxItemsInTile, serializedTile.itemValues.size());
		}

		houseEntry.set_house_id(house.id);
		auto* houseData = houseEntry.mutable_data();
		auto* origin = houseData->mutable_origin();
		origin->set_pos_x(static_cast<uint32_t>(std::max(minX, 0)));
		origin->set_pos_y(static_cast<uint32_t>(std::max(minY, 0)));
		origin->set_pos_z(static_cast<uint32_t>(std::max(minZ, 0)));

		auto* dimensions = houseData->mutable_dimensions();
		dimensions->set_pos_x(static_cast<uint32_t>(width));
		dimensions->set_pos_y(static_cast<uint32_t>(height));
		dimensions->set_pos_z(static_cast<uint32_t>(floorCount));

		auto* layer = houseData->mutable_preview()->mutable_layer();
		for (const SerializedTile &serializedTile : serializedTiles) {
			auto* tile = layer->add_tile();
			for (const uint32_t itemValue : serializedTile.itemValues) {
				auto* item = tile->add_item();
				item->set_value(itemValue);
			}
			tile->set_skip(serializedTile.skip);
		}

		auto &exportStats = staticMapHouseExportDebugStats();
		++exportStats.housesProcessed;
		exportStats.tilesWritten += serializedTiles.size();
		exportStats.tilesWithItems += tilesWithItems;
		exportStats.droppedByCap += droppedByCapCount;
		exportStats.droppedInvalidItems += droppedInvalidCount;

		if (houseTemplate && houseTemplate->isValid()) {
			++exportStats.housesWithTemplate;
			exportStats.templateTilesCompared += templateTilesCompared;
			exportStats.templateTileMismatches += templateTileMismatchCount;
			exportStats.templateSouthTilesCompared += templateSouthTilesCompared;
			exportStats.templateSouthTileMismatches += templateSouthTileMismatchCount;
			exportStats.templateTilesMissingItems += templateTilesMissingItems;
			exportStats.missingMapTiles += missingMapTileCount;
			if (templateTileMismatchCount > 0) {
				++exportStats.housesWithTemplateMismatch;
			}

			if (houseDebugTarget) {
				spdlog::info(
					"[house-debug] house={} name='{}' template_origin=({}, {}, {}) dims=({}, {}, {}) serialized_tiles={} tiles_with_items={} max_items_per_tile={} "
					"template_tiles_compared={} template_mismatches={} south_tiles_compared={} south_mismatches={} missing_template_items={} missing_map_tiles={} dropped_by_cap={} dropped_invalid={}",
					house.id,
					house.name,
					minX,
					minY,
					minZ,
					width,
					height,
					floorCount,
					serializedTiles.size(),
					tilesWithItems,
					maxItemsInTile,
					templateTilesCompared,
					templateTileMismatchCount,
					templateSouthTilesCompared,
					templateSouthTileMismatchCount,
					templateTilesMissingItems,
					missingMapTileCount,
					droppedByCapCount,
					droppedInvalidCount
				);
				spdlog::info(
					"[house-debug] house={} name='{}' mapping_eval current_exact={} alternative_exact={} alternative_better={}",
					house.id,
					house.name,
					currentMappingExactMatches,
					alternativeMappingExactMatches,
					alternativeMappingBetterMatches
				);
			}
		} else {
			++exportStats.housesWithoutTemplate;
			if (houseDebugTarget) {
				spdlog::info(
					"[house-debug] house={} name='{}' dynamic_origin=({}, {}, {}) dims=({}, {}, {}) serialized_tiles={} tiles_with_items={} max_items_per_tile={} dropped_by_cap={} dropped_invalid={}",
					house.id,
					house.name,
					minX,
					minY,
					minZ,
					width,
					height,
					floorCount,
					serializedTiles.size(),
					tilesWithItems,
					maxItemsInTile,
					droppedByCapCount,
					droppedInvalidCount
				);
			}
		}

		return true;
	}

	struct CyclopediaCatalogFiles {
		std::string mapFileName;
		std::string staticMapDataFileName;
		std::string staticDataFileName;
	};

	std::filesystem::path resolveCyclopediaCatalogBasePath(const std::filesystem::path &inputBasePath) {
		if (inputBasePath.empty()) {
			return std::filesystem::path();
		}

		if (const std::filesystem::path catalogAtRoot = inputBasePath / "catalog-content.json"; std::filesystem::exists(catalogAtRoot)) {
			return inputBasePath;
		}

		const std::filesystem::path assetsPath = inputBasePath / "assets";
		if (const std::filesystem::path catalogAtAssets = assetsPath / "catalog-content.json"; std::filesystem::exists(catalogAtAssets)) {
			return assetsPath;
		}

		return inputBasePath;
	}

	bool loadCyclopediaCatalogFiles(const std::filesystem::path &basePath, CyclopediaCatalogFiles &catalogFiles) {
		const std::filesystem::path catalogPath = basePath / "catalog-content.json";
		if (!std::filesystem::exists(catalogPath)) {
			return false;
		}

		try {
			std::ifstream catalogInput(catalogPath, std::ios::binary);
			if (!catalogInput.is_open()) {
				return false;
			}

			const nlohmann::json jsonCatalog = nlohmann::json::parse(catalogInput, nullptr, false);
			if (!jsonCatalog.is_array()) {
				return false;
			}

			for (const auto &entry : jsonCatalog) {
				if (!entry.is_object()) {
					continue;
				}

				const std::string type = entry.value("type", std::string());
				const std::string fileName = entry.value("file", std::string());
				if (type.empty() || fileName.empty()) {
					continue;
				}

				if (type == "map") {
					catalogFiles.mapFileName = fileName;
				} else if (type == "staticmapdata") {
					catalogFiles.staticMapDataFileName = fileName;
				} else if (type == "staticdata") {
					catalogFiles.staticDataFileName = fileName;
				}
			}

			return !catalogFiles.mapFileName.empty() || !catalogFiles.staticMapDataFileName.empty() || !catalogFiles.staticDataFileName.empty();
		} catch (const nlohmann::json::exception &) {
			return false;
		}
	}

	bool loadCyclopediaMapDataTemplate(const std::filesystem::path &mapDataPath, clienteditor::protobuf::mapdata::MapData &templateMapData) {
		if (!std::filesystem::exists(mapDataPath)) {
			return false;
		}

		std::ifstream mapInput(mapDataPath, std::ios::binary);
		if (!mapInput.is_open()) {
			return false;
		}

		return templateMapData.ParseFromIstream(&mapInput);
	}

	bool tryExtractSha256FromFilename(const std::filesystem::path &path, std::string &hashHex) {
		hashHex.clear();
		const std::string filename = path.filename().string();
		const size_t dashPos = filename.find('-');
		const size_t dotPos = filename.rfind('.');
		if (dashPos == std::string::npos || dotPos == std::string::npos || dotPos <= dashPos + 1) {
			return false;
		}

		const std::string candidate = filename.substr(dashPos + 1, dotPos - dashPos - 1);
		if (candidate.size() != 64) {
			return false;
		}

		for (char ch : candidate) {
			if (!std::isxdigit(static_cast<unsigned char>(ch))) {
				return false;
			}
		}

		hashHex = candidate;
		std::transform(hashHex.begin(), hashHex.end(), hashHex.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return true;
	}

	bool isSafeRelativePath(const std::filesystem::path &path) {
		if (path.empty() || path.is_absolute()) {
			return false;
		}

		for (const std::filesystem::path &part : path) {
			if (part == std::filesystem::path(".") || part == std::filesystem::path("..")) {
				return false;
			}
		}
		return true;
	}

	bool getLocalTime(const std::time_t value, std::tm &localTime) {
#ifdef _WIN32
		return localtime_s(&localTime, &value) == 0;
#else
		return localtime_r(&value, &localTime) != nullptr;
#endif
	}

	std::string buildCyclopediaBackupRunName(const std::string_view prefix) {
		const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		std::tm localTime {};
		if (!getLocalTime(now, localTime)) {
			return std::string(prefix) + "-unknown-time";
		}

		std::ostringstream name;
		name << prefix << "-" << std::put_time(&localTime, "%Y-%m-%d_%H-%M-%S");
		return name.str();
	}

	std::filesystem::path createCyclopediaBackupRunPath(const std::filesystem::path &backupRootPath, const std::string_view prefix) {
		if (backupRootPath.empty()) {
			return std::filesystem::path();
		}

		std::error_code ec;
		std::filesystem::create_directories(backupRootPath, ec);
		if (ec) {
			spdlog::warn("Failed to create backup root directory '{}': {}", backupRootPath.string(), ec.message());
			return std::filesystem::path();
		}

		const std::string baseName = buildCyclopediaBackupRunName(prefix);
		for (int attempt = 0; attempt < 1000; ++attempt) {
			std::string runName = baseName;
			if (attempt > 0) {
				runName += std::format("-{:03}", attempt + 1);
			}

			std::filesystem::path runPath = backupRootPath / runName;
			ec.clear();
			if (std::filesystem::create_directory(runPath, ec)) {
				return runPath;
			}
			if (ec) {
				spdlog::warn("Failed to create backup run directory '{}': {}", runPath.string(), ec.message());
				return std::filesystem::path();
			}
		}

		spdlog::warn("Failed to create a unique backup run directory under '{}'", backupRootPath.string());
		return std::filesystem::path();
	}

	std::filesystem::path buildCyclopediaBackupFilePath(const std::filesystem::path &backupRunPath, const std::filesystem::path &relativePath) {
		std::filesystem::path backupPath = backupRunPath / relativePath;
		backupPath += ".bkp";
		return backupPath;
	}

	std::filesystem::path makeUniqueCyclopediaBackupFilePath(const std::filesystem::path &backupPath) {
		if (!std::filesystem::exists(backupPath)) {
			return backupPath;
		}

		const std::filesystem::path parentPath = backupPath.parent_path();
		const std::string filename = backupPath.filename().string();
		constexpr std::string_view backupSuffix = ".bkp";
		std::string baseName = filename;
		std::string suffix;
		if (filename.size() > backupSuffix.size() && filename.ends_with(backupSuffix)) {
			baseName = filename.substr(0, filename.size() - backupSuffix.size());
			suffix = backupSuffix;
		}

		for (int attempt = 2; attempt < 1000; ++attempt) {
			std::filesystem::path candidate = parentPath / std::format("{}.{}{}", baseName, attempt, suffix);
			if (!std::filesystem::exists(candidate)) {
				return candidate;
			}
		}

		return std::filesystem::path();
	}

	void collectCyclopediaBackupFileCandidates(
		const std::filesystem::path &backupPath,
		const std::filesystem::path &relativePath,
		std::vector<std::filesystem::path> &sourceCandidates
	) {
		sourceCandidates.clear();
		if (backupPath.empty() || relativePath.empty()) {
			return;
		}

		const auto addCandidate = [&](const std::filesystem::path &rootPath) {
			if (rootPath.empty()) {
				return;
			}

			std::filesystem::path candidate = buildCyclopediaBackupFilePath(rootPath, relativePath);
			if (std::ranges::find(sourceCandidates, candidate) == sourceCandidates.end()) {
				sourceCandidates.emplace_back(std::move(candidate));
			}
		};

		addCandidate(backupPath);

		std::filesystem::path backupRootPath;
		if (backupPath.filename() == "bkps") {
			backupRootPath = backupPath;
		} else if (backupPath.parent_path().filename() == "bkps") {
			backupRootPath = backupPath.parent_path();
		}

		if (backupRootPath.empty() || !std::filesystem::exists(backupRootPath)) {
			return;
		}

		addCandidate(backupRootPath);

		std::vector<std::filesystem::path> runPaths;
		for (const auto &entry : std::filesystem::directory_iterator(backupRootPath)) {
			if (!entry.is_directory()) {
				continue;
			}
			runPaths.emplace_back(entry.path());
		}
		std::ranges::sort(runPaths, [](const std::filesystem::path &left, const std::filesystem::path &right) {
			std::error_code leftEc;
			std::error_code rightEc;
			const auto leftWriteTime = std::filesystem::last_write_time(left, leftEc);
			const auto rightWriteTime = std::filesystem::last_write_time(right, rightEc);
			if (!leftEc && !rightEc && leftWriteTime != rightWriteTime) {
				return leftWriteTime > rightWriteTime;
			}
			return left.generic_string() > right.generic_string();
		});

		for (const auto &runPath : runPaths) {
			addCandidate(runPath);
		}
	}

	bool readBinaryFile(const std::filesystem::path &path, std::vector<uint8_t> &bytes) {
		std::ifstream input(path, std::ios::binary);
		if (!input.is_open()) {
			return false;
		}

		input.seekg(0, std::ios::end);
		const std::streamoff size = input.tellg();
		if (size < 0) {
			return false;
		}
		input.seekg(0, std::ios::beg);

		bytes.resize(static_cast<size_t>(size));
		if (!bytes.empty()) {
			input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			if (!input.good() && !input.eof()) {
				return false;
			}
		}

		return true;
	}

	bool writeBinaryFile(const std::filesystem::path &path, const char* data, const size_t size) {
		if (size > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
			return false;
		}

		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		if (!output.is_open()) {
			return false;
		}

		if (size > 0) {
			output.write(data, static_cast<std::streamsize>(size));
			if (!output.good()) {
				return false;
			}
		}

		output.close();
		return output.good();
	}

	bool writeBinaryFile(const std::filesystem::path &path, const std::string &bytes) {
		return writeBinaryFile(path, bytes.data(), bytes.size());
	}

	bool writeBinaryFile(const std::filesystem::path &path, const std::vector<uint8_t> &bytes) {
		return writeBinaryFile(path, reinterpret_cast<const char*>(bytes.data()), bytes.size());
	}

	bool hasMatchingEmbeddedSha256(const std::filesystem::path &path) {
		std::string expectedHash;
		if (!tryExtractSha256FromFilename(path, expectedHash)) {
			// No embedded hash in filename: accept as-is.
			return true;
		}

		std::vector<uint8_t> bytes;
		if (!readBinaryFile(path, bytes)) {
			return false;
		}

		if (const std::string actualHash = toHex(sha256Hash(bytes)); actualHash != expectedHash) {
			spdlog::warn(
				"[house-debug] template hash mismatch for '{}': expected={} actual={}",
				path.string(),
				expectedHash,
				actualHash
			);
			return false;
		}

		return true;
	}

	bool readTemplateFileBytes(
		const std::filesystem::path &path,
		std::vector<uint8_t> &bytes,
		bool requireMatchingEmbeddedSha256 = false
	) {
		if (!std::filesystem::exists(path)) {
			return false;
		}

		if (!readBinaryFile(path, bytes)) {
			return false;
		}

		std::string expectedHash;
		if (!tryExtractSha256FromFilename(path, expectedHash)) {
			return true;
		}

		const std::string actualHash = toHex(sha256Hash(bytes));
		if (actualHash == expectedHash) {
			return true;
		}

		if (requireMatchingEmbeddedSha256) {
			spdlog::warn(
				"[house-debug] template hash mismatch for '{}': expected={} actual={} (rejecting)",
				path.string(),
				expectedHash,
				actualHash
			);
			return false;
		}

		spdlog::warn(
			"[house-debug] template hash mismatch for '{}': expected={} actual={} (accepting explicit template)",
			path.string(),
			expectedHash,
			actualHash
		);
		return true;
	}

	bool isHashNamedCatalogDataFile(const std::filesystem::path &path, const std::string_view prefix) {
		const std::string filename = path.filename().string();
		const std::string expectedPrefix = std::string(prefix) + "-";
		if (filename.rfind(expectedPrefix, 0) != 0 || path.extension() != ".dat") {
			return false;
		}

		std::string hashHex;
		return tryExtractSha256FromFilename(path, hashHex);
	}

	std::string getCyclopediaAssetReplacementKey(const std::filesystem::path &path) {
		const std::string filename = path.filename().string();
		if (filename.rfind("minimap-", 0) != 0 && filename.rfind("satellite-", 0) != 0) {
			return std::string();
		}

		constexpr std::string_view suffix = ".bmp.lzma";
		if (filename.size() <= suffix.size() || filename.compare(filename.size() - suffix.size(), suffix.size(), suffix.data(), suffix.size()) != 0) {
			return std::string();
		}

		const std::string stem = filename.substr(0, filename.size() - suffix.size());
		const size_t hashDashPos = stem.rfind('-');
		if (hashDashPos == std::string::npos || hashDashPos + 1 >= stem.size()) {
			return std::string();
		}

		const std::string_view hashValue(stem.data() + hashDashPos + 1, stem.size() - hashDashPos - 1);
		if (hashValue.size() != 64 || !std::ranges::all_of(hashValue, [](const char ch) {
				return std::isxdigit(static_cast<unsigned char>(ch));
			})) {
			return std::string();
		}

		return stem.substr(0, hashDashPos);
	}

	void collectCyclopediaAssetBackupKeys(
		const std::vector<std::pair<std::string, std::vector<uint8_t>>> &assets,
		std::unordered_set<std::string> &assetFileNames,
		std::unordered_set<std::string> &assetReplacementKeys
	) {
		assetFileNames.clear();
		assetReplacementKeys.clear();
		assetFileNames.reserve(assets.size() * 2);
		assetReplacementKeys.reserve(assets.size() * 2);

		for (const auto &asset : assets) {
			const std::filesystem::path assetPath(asset.first);
			assetFileNames.insert(assetPath.generic_string());
			if (const std::string replacementKey = getCyclopediaAssetReplacementKey(assetPath); !replacementKey.empty()) {
				assetReplacementKeys.insert(replacementKey);
			}
		}
	}

	std::filesystem::path findValidTemplateAssetSibling(const std::filesystem::path &basePath, const std::filesystem::path &fileName) {
		if (basePath.empty() || fileName.empty()) {
			return std::filesystem::path();
		}

		const std::filesystem::path clientRoot = basePath.parent_path();
		const std::filesystem::path clientsRoot = clientRoot.parent_path();
		if (clientsRoot.empty() || !std::filesystem::exists(clientsRoot) || !std::filesystem::is_directory(clientsRoot)) {
			return std::filesystem::path();
		}

		for (const auto &entry : std::filesystem::directory_iterator(clientsRoot)) {
			if (!entry.is_directory()) {
				continue;
			}

			const std::filesystem::path siblingRoot = entry.path();
			if (siblingRoot == clientRoot) {
				continue;
			}

			const std::array<std::filesystem::path, 2> candidates {
				siblingRoot / "assets" / fileName,
				siblingRoot / fileName
			};

			for (const auto &candidate : candidates) {
				if (!std::filesystem::exists(candidate)) {
					continue;
				}
				if (hasMatchingEmbeddedSha256(candidate)) {
					return candidate;
				}
			}
		}

		return std::filesystem::path();
	}

	bool loadStaticMapDataTemplate(const std::filesystem::path &staticMapDataPath, clienteditor::protobuf::staticmapdata::StaticMapData &templateStaticMapData) {
		std::vector<uint8_t> bytes;
		if (!readTemplateFileBytes(staticMapDataPath, bytes)) {
			return false;
		}

		return templateStaticMapData.ParseFromArray(bytes.data(), static_cast<int>(bytes.size()));
	}

	bool buildStaticMapHouseTemplate(
		const clienteditor::protobuf::staticmapdata::HouseEntry &templateEntry,
		StaticMapHouseTemplate &houseTemplate
	) {
		if (!templateEntry.has_data()) {
			return false;
		}
		const auto &data = templateEntry.data();
		if (!data.has_origin() || !data.has_dimensions() || !data.has_preview() || !data.preview().has_layer()) {
			return false;
		}

		const auto width = static_cast<int>(data.dimensions().pos_x());
		const auto height = static_cast<int>(data.dimensions().pos_y());
		const auto floorCount = static_cast<int>(data.dimensions().pos_z());
		if (width <= 0 || height <= 0 || floorCount <= 0) {
			return false;
		}

		const uint64_t totalCells = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * static_cast<uint64_t>(floorCount);
		if (totalCells == 0 || totalCells > std::numeric_limits<uint32_t>::max()) {
			return false;
		}

		StaticMapHouseTemplate parsedTemplate;
		parsedTemplate.minX = static_cast<int>(data.origin().pos_x());
		parsedTemplate.minY = static_cast<int>(data.origin().pos_y());
		parsedTemplate.minZ = static_cast<int>(data.origin().pos_z());
		parsedTemplate.width = width;
		parsedTemplate.height = height;
		parsedTemplate.floorCount = floorCount;
		parsedTemplate.tiles.reserve(data.preview().layer().tile_size());

		uint64_t linearIndex = 0;
		for (const auto &templateTile : data.preview().layer().tile()) {
			const uint32_t skip = templateTile.has_skip() ? templateTile.skip() : 0;
			if (linearIndex >= totalCells) {
				break;
			}

			StaticMapTemplateTile tile;
			tile.index = static_cast<uint32_t>(linearIndex);
			tile.skip = skip;
			tile.itemValues.reserve(templateTile.item_size());
			for (const auto &templateItem : templateTile.item()) {
				tile.itemValues.emplace_back(templateItem.value());
			}
			parsedTemplate.tiles.emplace_back(tile);

			const uint64_t nextIndex = linearIndex + static_cast<uint64_t>(skip) + 1;
			if (nextIndex < linearIndex) {
				break;
			}
			linearIndex = nextIndex;
		}

		houseTemplate = std::move(parsedTemplate);
		return true;
	}

	void buildStaticMapHouseTemplates(
		const clienteditor::protobuf::staticmapdata::StaticMapData &templateStaticMapData,
		const std::unordered_map<uint32_t, uint32_t> &houseIdRemap,
		const std::unordered_map<uint32_t, HousePositionDelta> &housePositionDeltaRemap,
		std::unordered_map<uint32_t, StaticMapHouseTemplate> &houseTemplates
	) {
		(void)housePositionDeltaRemap;
		houseTemplates.clear();
		std::unordered_map<uint32_t, const clienteditor::protobuf::staticmapdata::HouseEntry*> templateEntriesByHouseId;
		templateEntriesByHouseId.reserve(static_cast<size_t>(templateStaticMapData.house_size() * 2));
		for (const auto &templateEntry : templateStaticMapData.house()) {
			templateEntriesByHouseId[templateEntry.house_id()] = &templateEntry;
		}

		if (houseIdRemap.empty()) {
			houseTemplates.reserve(static_cast<size_t>(templateStaticMapData.house_size()));
			for (const auto &templateEntry : templateStaticMapData.house()) {
				StaticMapHouseTemplate houseTemplate;
				if (!buildStaticMapHouseTemplate(templateEntry, houseTemplate)) {
					continue;
				}
				houseTemplates[templateEntry.house_id()] = std::move(houseTemplate);
			}
			spdlog::info(
				"[house-debug] loaded staticmap template houses without remap: parsed={} source_entries={}",
				houseTemplates.size(),
				templateStaticMapData.house_size()
			);
			return;
		}

		houseTemplates.reserve(houseIdRemap.size());
		for (const auto &[sourceHouseId, targetHouseId] : houseIdRemap) {
			const auto it = templateEntriesByHouseId.find(targetHouseId);
			if (it == templateEntriesByHouseId.end() || !it->second) {
				continue;
			}

			StaticMapHouseTemplate houseTemplate;
			if (!buildStaticMapHouseTemplate(*it->second, houseTemplate)) {
				continue;
			}

			// Keep CIP framing anchors exactly as template origin. Shifting by
			// staticdata housePosition deltas drifts previews out of frame.
			houseTemplates[sourceHouseId] = std::move(houseTemplate);
		}

		spdlog::info(
			"[house-debug] loaded staticmap template houses with remap: parsed={} remap_entries={} source_entries={}",
			houseTemplates.size(),
			houseIdRemap.size(),
			templateStaticMapData.house_size()
		);
	}

	bool mergeCyclopediaTemplateMapData(
		const clienteditor::protobuf::mapdata::MapData &templateMapData,
		const std::string &generatedBuffer,
		std::string &mergedBuffer
	) {
		using namespace clienteditor::protobuf::mapdata;

		MapData generatedMapData;
		if (!generatedMapData.ParseFromString(generatedBuffer)) {
			return false;
		}

		MapData mergedMapData;
		for (const auto &areaData : templateMapData.areadata()) {
			*mergedMapData.add_areadata() = areaData;
		}
		for (const auto &npcData : templateMapData.npcdata()) {
			*mergedMapData.add_npcdata() = npcData;
		}
		for (const auto &mapAsset : templateMapData.mapassets()) {
			if (mapAsset.type() != MapAssets_AssetsType_SUBAREA) {
				continue;
			}
			*mergedMapData.add_mapassets() = mapAsset;
		}
		for (const auto &mapAsset : generatedMapData.mapassets()) {
			*mergedMapData.add_mapassets() = mapAsset;
		}

		if (generatedMapData.has_topleftedge()) {
			*mergedMapData.mutable_topleftedge() = generatedMapData.topleftedge();
		} else if (templateMapData.has_topleftedge()) {
			*mergedMapData.mutable_topleftedge() = templateMapData.topleftedge();
		}

		if (generatedMapData.has_bottomrightedge()) {
			*mergedMapData.mutable_bottomrightedge() = generatedMapData.bottomrightedge();
		} else if (templateMapData.has_bottomrightedge()) {
			*mergedMapData.mutable_bottomrightedge() = templateMapData.bottomrightedge();
		}

		return mergedMapData.SerializeToString(&mergedBuffer);
	}

	bool loadStaticDataTemplate(const std::filesystem::path &staticDataPath, clienteditor::protobuf::staticdata::StaticData &templateStaticData) {
		std::vector<uint8_t> bytes;
		if (!readTemplateFileBytes(staticDataPath, bytes)) {
			return false;
		}

		return templateStaticData.ParseFromArray(bytes.data(), static_cast<int>(bytes.size()));
	}

	template <typename TemplateData, typename Loader>
	bool loadFirstTemplateCandidate(
		const std::vector<std::filesystem::path> &candidatePaths,
		const Loader &loader,
		TemplateData &templateData,
		std::filesystem::path* loadedPath = nullptr
	) {
		if (loadedPath) {
			loadedPath->clear();
		}

		for (const auto &candidatePath : candidatePaths) {
			if (candidatePath.empty()) {
				continue;
			}

			if (!loader(candidatePath, templateData)) {
				continue;
			}

			if (loadedPath) {
				*loadedPath = candidatePath;
			}
			return true;
		}

		return false;
	}

	bool mergeStaticDataTemplate(
		const clienteditor::protobuf::staticdata::StaticData &templateStaticData,
		const std::string &generatedBuffer,
		std::string &mergedBuffer,
		std::unordered_map<uint32_t, uint32_t>* houseIdRemap = nullptr,
		std::unordered_map<uint32_t, HousePositionDelta>* housePositionDeltaRemap = nullptr,
		StaticDataTemplateMergeStats* mergeStats = nullptr
	) {
		using namespace clienteditor::protobuf::staticdata;
		using StaticHouse = clienteditor::protobuf::staticdata::House;

		if (mergeStats) {
			*mergeStats = StaticDataTemplateMergeStats {};
		}

		StaticData generatedStaticData;
		if (!generatedStaticData.ParseFromString(generatedBuffer)) {
			return false;
		}

		StaticData mergedStaticData(templateStaticData);
		mergedStaticData.clear_house();
		if (houseIdRemap) {
			houseIdRemap->clear();
		}
		if (housePositionDeltaRemap) {
			housePositionDeltaRemap->clear();
		}

		std::map<std::string, std::vector<int>, std::less<>> generatedHousesByName;
		std::map<std::string, std::vector<int>, std::less<>> generatedHousesByNormalizedName;
		std::map<std::string, std::vector<int>, std::less<>> generatedHousesByAliasName;
		std::unordered_map<HousePositionKey, std::vector<int>, HousePositionKeyHash> generatedHousesByPosition;
		std::vector<bool> generatedHouseUsed(static_cast<size_t>(generatedStaticData.house_size()), false);
		size_t matchedTemplateHouses = 0;
		size_t missingTemplateHouses = 0;
		size_t appendedGeneratedHouses = 0;
		size_t remappedIdsCount = 0;

		for (int index = 0; index < generatedStaticData.house_size(); ++index) {
			const StaticHouse &generatedHouse = generatedStaticData.house(index);
			generatedHousesByName[generatedHouse.name()].push_back(index);
			const std::string normalizedGeneratedHouseName = normalizeHouseName(generatedHouse.name());
			generatedHousesByNormalizedName[normalizedGeneratedHouseName].push_back(index);
			generatedHousesByAliasName[normalizeHouseNameAlias(normalizedGeneratedHouseName)].push_back(index);

			HousePositionKey positionKey;
			if (getHousePositionKey(generatedHouse, positionKey)) {
				generatedHousesByPosition[positionKey].push_back(index);
			}
		}

		const auto pickUnusedGeneratedIndex = [&generatedHouseUsed](const std::vector<int>* candidates) {
			if (!candidates) {
				return -1;
			}

			for (const int candidate : *candidates) {
				if (candidate < 0 || static_cast<size_t>(candidate) >= generatedHouseUsed.size()) {
					continue;
				}
				if (!generatedHouseUsed[static_cast<size_t>(candidate)]) {
					return candidate;
				}
			}
			return -1;
		};

		for (const StaticHouse &templateHouse : templateStaticData.house()) {
			int matchedGeneratedIndex = -1;

			const auto nameIt = generatedHousesByName.find(templateHouse.name());
			if (nameIt != generatedHousesByName.end()) {
				matchedGeneratedIndex = pickUnusedGeneratedIndex(&nameIt->second);
			}

			if (matchedGeneratedIndex < 0) {
				const std::string normalizedTemplateName = normalizeHouseName(templateHouse.name());
				const auto normalizedNameIt = generatedHousesByNormalizedName.find(normalizedTemplateName);
				if (normalizedNameIt != generatedHousesByNormalizedName.end()) {
					matchedGeneratedIndex = pickUnusedGeneratedIndex(&normalizedNameIt->second);
				}
			}

			if (matchedGeneratedIndex < 0) {
				const std::string aliasTemplateName = normalizeHouseNameAlias(templateHouse.name());
				const auto aliasNameIt = generatedHousesByAliasName.find(aliasTemplateName);
				if (aliasNameIt != generatedHousesByAliasName.end()) {
					matchedGeneratedIndex = pickUnusedGeneratedIndex(&aliasNameIt->second);
				}
			}

			if (matchedGeneratedIndex < 0) {
				HousePositionKey templatePositionKey;
				if (getHousePositionKey(templateHouse, templatePositionKey)) {
					const auto positionIt = generatedHousesByPosition.find(templatePositionKey);
					if (positionIt != generatedHousesByPosition.end()) {
						matchedGeneratedIndex = pickUnusedGeneratedIndex(&positionIt->second);
					}
				}
			}

			if (matchedGeneratedIndex < 0) {
				*mergedStaticData.add_house() = templateHouse;
				++missingTemplateHouses;
				if (isHouseDebugTargetName(templateHouse.name())) {
					spdlog::info(
						"[house-debug] staticdata template house without generated match: template_id={} name='{}'",
						templateHouse.house_id(),
						templateHouse.name()
					);
				}
				continue;
			}

			generatedHouseUsed[static_cast<size_t>(matchedGeneratedIndex)] = true;
			const StaticHouse &generatedHouse = generatedStaticData.house(matchedGeneratedIndex);
			++matchedTemplateHouses;
			StaticHouse* mergedHouse = mergedStaticData.add_house();
			*mergedHouse = templateHouse;

			// Keep template housePosition for matched houses. CIP uses this anchor
			// for house previews and replacing it with map exit coordinates causes
			// visible offsets in the Cyclopedia panel.
			if (!mergedHouse->has_houseposition() && generatedHouse.has_houseposition()) {
				*mergedHouse->mutable_houseposition() = generatedHouse.houseposition();
			}
			if (generatedHouse.has_beds() && generatedHouse.beds() > 0) {
				mergedHouse->set_beds(generatedHouse.beds());
			}
			if (generatedHouse.has_price() && generatedHouse.price() > 0) {
				mergedHouse->set_price(generatedHouse.price());
			}

			if (houseIdRemap) {
				(*houseIdRemap)[generatedHouse.house_id()] = templateHouse.house_id();
				if (generatedHouse.house_id() != templateHouse.house_id()) {
					++remappedIdsCount;
				}
			}
			if (housePositionDeltaRemap) {
				HousePositionDelta positionDelta;
				if (generatedHouse.has_houseposition() && templateHouse.has_houseposition()) {
					positionDelta.dx = static_cast<int>(generatedHouse.houseposition().pos_x()) - static_cast<int>(templateHouse.houseposition().pos_x());
					positionDelta.dy = static_cast<int>(generatedHouse.houseposition().pos_y()) - static_cast<int>(templateHouse.houseposition().pos_y());
					positionDelta.dz = static_cast<int>(generatedHouse.houseposition().pos_z()) - static_cast<int>(templateHouse.houseposition().pos_z());
				}
				(*housePositionDeltaRemap)[generatedHouse.house_id()] = positionDelta;

				if ((positionDelta.dx != 0 || positionDelta.dy != 0 || positionDelta.dz != 0) && isHouseDebugTargetName(templateHouse.name())) {
					spdlog::info(
						"[house-debug] staticdata matched house delta: generated_id={} template_id={} name='{}' delta=({}, {}, {}) generated_pos=({}, {}, {}) template_pos=({}, {}, {})",
						generatedHouse.house_id(),
						templateHouse.house_id(),
						templateHouse.name(),
						positionDelta.dx,
						positionDelta.dy,
						positionDelta.dz,
						generatedHouse.houseposition().pos_x(),
						generatedHouse.houseposition().pos_y(),
						generatedHouse.houseposition().pos_z(),
						templateHouse.houseposition().pos_x(),
						templateHouse.houseposition().pos_y(),
						templateHouse.houseposition().pos_z()
					);
				}
			}
		}

		std::unordered_set<uint32_t> usedHouseIds;
		usedHouseIds.reserve(static_cast<size_t>(mergedStaticData.house_size() * 2));
		for (const StaticHouse &mergedHouse : mergedStaticData.house()) {
			usedHouseIds.insert(mergedHouse.house_id());
		}

		uint32_t nextHouseId = 1;
		if (!usedHouseIds.empty()) {
			nextHouseId = *std::max_element(usedHouseIds.begin(), usedHouseIds.end()) + 1;
		}

		for (int index = 0; index < generatedStaticData.house_size(); ++index) {
			if (generatedHouseUsed[static_cast<size_t>(index)]) {
				continue;
			}

			const StaticHouse &generatedHouse = generatedStaticData.house(index);
			++appendedGeneratedHouses;
			StaticHouse* mergedHouse = mergedStaticData.add_house();
			*mergedHouse = generatedHouse;

			uint32_t targetHouseId = generatedHouse.house_id();
			if (usedHouseIds.contains(targetHouseId)) {
				while (usedHouseIds.contains(nextHouseId)) {
					++nextHouseId;
				}
				targetHouseId = nextHouseId++;
				mergedHouse->set_house_id(targetHouseId);
			}
			usedHouseIds.insert(targetHouseId);

			if (houseIdRemap) {
				(*houseIdRemap)[generatedHouse.house_id()] = targetHouseId;
				if (generatedHouse.house_id() != targetHouseId) {
					++remappedIdsCount;
				}
			}
			if (housePositionDeltaRemap) {
				(*housePositionDeltaRemap)[generatedHouse.house_id()] = HousePositionDelta {};
			}
		}

		spdlog::info(
			"[house-debug] staticdata merge summary: template_houses={} generated_houses={} matched={} template_without_match={} appended_generated={} remapped_ids={} final_houses={}",
			templateStaticData.house_size(),
			generatedStaticData.house_size(),
			matchedTemplateHouses,
			missingTemplateHouses,
			appendedGeneratedHouses,
			remappedIdsCount,
			mergedStaticData.house_size()
		);

		if (mergeStats) {
			mergeStats->generatedHouses = static_cast<size_t>(generatedStaticData.house_size());
			mergeStats->matchedTemplateHouses = matchedTemplateHouses;
			mergeStats->missingTemplateHouses = missingTemplateHouses;
			mergeStats->appendedGeneratedHouses = appendedGeneratedHouses;
			mergeStats->remappedIds = remappedIdsCount;
			mergeStats->finalHouses = static_cast<size_t>(mergedStaticData.house_size());
		}

		return mergedStaticData.SerializeToString(&mergedBuffer);
	}

	bool remapStaticMapDataHouseIds(
		const std::unordered_map<uint32_t, uint32_t> &houseIdRemap,
		const std::string &generatedBuffer,
		std::string &mergedBuffer
	) {
		using namespace clienteditor::protobuf::staticmapdata;

		if (houseIdRemap.empty()) {
			mergedBuffer = generatedBuffer;
			return true;
		}

		StaticMapData generatedStaticMapData;
		if (!generatedStaticMapData.ParseFromString(generatedBuffer)) {
			return false;
		}

		StaticMapData mergedStaticMapData;
		std::unordered_set<uint32_t> usedHouseIds;
		usedHouseIds.reserve(static_cast<size_t>(generatedStaticMapData.house_size() * 2));

		for (const HouseEntry &generatedHouseEntry : generatedStaticMapData.house()) {
			uint32_t sourceHouseId = generatedHouseEntry.house_id();
			auto remapIt = houseIdRemap.find(sourceHouseId);
			uint32_t targetHouseId = remapIt != houseIdRemap.end() ? remapIt->second : sourceHouseId;

			if (!usedHouseIds.insert(targetHouseId).second) {
				continue;
			}

			HouseEntry* mergedHouseEntry = mergedStaticMapData.add_house();
			*mergedHouseEntry = generatedHouseEntry;
			mergedHouseEntry->set_house_id(targetHouseId);
		}

		return mergedStaticMapData.SerializeToString(&mergedBuffer);
	}

	bool mergeStaticMapDataTemplate(
		const clienteditor::protobuf::staticmapdata::StaticMapData &templateStaticMapData,
		const std::string &generatedBuffer,
		std::string &mergedBuffer
	) {
		using namespace clienteditor::protobuf::staticmapdata;

		StaticMapData generatedStaticMapData;
		if (!generatedStaticMapData.ParseFromString(generatedBuffer)) {
			return false;
		}

		StaticMapData mergedStaticMapData(templateStaticMapData);
		std::unordered_map<uint32_t, int> mergedIndexByHouseId;
		mergedIndexByHouseId.reserve(static_cast<size_t>(mergedStaticMapData.house_size() * 2));
		for (int index = 0; index < mergedStaticMapData.house_size(); ++index) {
			const HouseEntry &houseEntry = mergedStaticMapData.house(index);
			mergedIndexByHouseId.emplace(houseEntry.house_id(), index);
		}

		size_t replacedEntries = 0;
		size_t appendedEntries = 0;
		for (const HouseEntry &generatedHouseEntry : generatedStaticMapData.house()) {
			const auto it = mergedIndexByHouseId.find(generatedHouseEntry.house_id());
			if (it != mergedIndexByHouseId.end()) {
				*mergedStaticMapData.mutable_house(it->second) = generatedHouseEntry;
				++replacedEntries;
				continue;
			}

			*mergedStaticMapData.add_house() = generatedHouseEntry;
			++appendedEntries;
		}

		spdlog::info(
			"[house-debug] staticmap template merge summary: generated_entries={} replaced_entries={} appended_entries={} final_entries={}",
			generatedStaticMapData.house_size(),
			replacedEntries,
			appendedEntries,
			mergedStaticMapData.house_size()
		);

		return mergedStaticMapData.SerializeToString(&mergedBuffer);
	}

	bool extractStaticDataHouseOrder(const std::string &staticDataBuffer, std::vector<uint32_t> &houseOrder) {
		using namespace clienteditor::protobuf::staticdata;

		houseOrder.clear();
		StaticData staticData;
		if (!staticData.ParseFromString(staticDataBuffer)) {
			return false;
		}

		houseOrder.reserve(static_cast<size_t>(staticData.house_size()));
		for (const auto &house : staticData.house()) {
			houseOrder.push_back(house.house_id());
		}
		return true;
	}

	bool reorderStaticMapDataHouseEntries(
		const std::string &generatedBuffer,
		const std::vector<uint32_t> &houseOrder,
		std::string &orderedBuffer
	) {
		using namespace clienteditor::protobuf::staticmapdata;

		if (houseOrder.empty()) {
			orderedBuffer = generatedBuffer;
			return true;
		}

		StaticMapData generatedStaticMapData;
		if (!generatedStaticMapData.ParseFromString(generatedBuffer)) {
			return false;
		}

		std::unordered_map<uint32_t, std::vector<const HouseEntry*>> entriesByHouseId;
		entriesByHouseId.reserve(static_cast<size_t>(generatedStaticMapData.house_size() * 2));
		for (const HouseEntry &houseEntry : generatedStaticMapData.house()) {
			entriesByHouseId[houseEntry.house_id()].push_back(&houseEntry);
		}

		StaticMapData orderedStaticMapData;
		orderedStaticMapData.mutable_house()->Reserve(generatedStaticMapData.house_size());

		std::unordered_set<const HouseEntry*> usedEntries;
		usedEntries.reserve(static_cast<size_t>(generatedStaticMapData.house_size() * 2));
		for (const uint32_t houseId : houseOrder) {
			const auto it = entriesByHouseId.find(houseId);
			if (it == entriesByHouseId.end()) {
				continue;
			}

			for (const HouseEntry* houseEntry : it->second) {
				if (!houseEntry || !usedEntries.insert(houseEntry).second) {
					continue;
				}
				*orderedStaticMapData.add_house() = *houseEntry;
			}
		}

		for (const HouseEntry &houseEntry : generatedStaticMapData.house()) {
			if (!usedEntries.insert(&houseEntry).second) {
				continue;
			}
			*orderedStaticMapData.add_house() = houseEntry;
		}

		return orderedStaticMapData.SerializeToString(&orderedBuffer);
	}

	bool backupFileIfExists(const std::filesystem::path &targetPath, const std::filesystem::path &basePath, const std::filesystem::path &backupRootPath) {
		if (!std::filesystem::exists(targetPath)) {
			return true;
		}

		std::error_code ec;
		std::filesystem::path relativePath;
		{
			std::error_code relativeEc;
			relativePath = std::filesystem::relative(targetPath, basePath, relativeEc);
			const std::string relativePathString = relativePath.generic_string();
			if (relativeEc || relativePath.empty() || relativePathString.rfind("..", 0) == 0) {
				relativePath = targetPath.filename();
			}
		}

		std::filesystem::path backupPath = makeUniqueCyclopediaBackupFilePath(buildCyclopediaBackupFilePath(backupRootPath, relativePath));
		if (backupPath.empty()) {
			spdlog::warn("Failed to allocate unique backup path for: {}", targetPath.string());
			return false;
		}
		if (!backupPath.parent_path().empty()) {
			std::filesystem::create_directories(backupPath.parent_path(), ec);
			if (ec) {
				spdlog::warn("Failed to create backup directory: {}", backupPath.parent_path().string());
				return false;
			}
		}

		std::filesystem::rename(targetPath, backupPath, ec);
		if (!ec) {
			return true;
		}

		ec.clear();
		std::filesystem::copy_file(targetPath, backupPath, std::filesystem::copy_options::none, ec);
		if (ec) {
			spdlog::warn("Failed to backup file before overwrite: {}", targetPath.string());
			return false;
		}

		ec.clear();
		std::filesystem::remove(targetPath, ec);
		if (ec) {
			spdlog::warn("Failed to remove original file after backup copy: {}", targetPath.string());
			return false;
		}
		return true;
	}

	bool backupCatalogFileIfReplaced(
		const std::filesystem::path &basePath,
		const std::filesystem::path &backupRootPath,
		const std::string &currentFileName,
		const std::string &replacementFileName
	) {
		if (currentFileName.empty() || currentFileName == replacementFileName) {
			return true;
		}

		const std::filesystem::path currentRelativePath(currentFileName);
		if (!isSafeRelativePath(currentRelativePath)) {
			return false;
		}

		return backupFileIfExists(basePath / currentRelativePath, basePath, backupRootPath);
	}

	bool backupStaleCatalogDataFiles(
		const std::filesystem::path &basePath,
		const std::filesystem::path &backupRootPath,
		const std::string_view prefix,
		const std::string &replacementFileName
	) {
		if (basePath.empty() || !std::filesystem::exists(basePath)) {
			return true;
		}

		for (const auto &entry : std::filesystem::directory_iterator(basePath)) {
			if (!entry.is_regular_file()) {
				continue;
			}

			const std::filesystem::path path = entry.path();
			if (path.filename().string() == replacementFileName || !isHashNamedCatalogDataFile(path, prefix)) {
				continue;
			}

			if (!backupFileIfExists(path, basePath, backupRootPath)) {
				return false;
			}
		}

		return true;
	}

	bool backupMapAssetsFromPreviousMapData(
		const std::filesystem::path &basePath,
		const std::filesystem::path &backupRootPath,
		const std::string &previousMapFileName,
		const std::string &replacementMapFileName,
		const std::unordered_set<std::string> &replacementAssetFileNames
	) {
		if (previousMapFileName.empty() || previousMapFileName == replacementMapFileName) {
			return true;
		}

		const std::filesystem::path previousMapRelativePath(previousMapFileName);
		if (!isSafeRelativePath(previousMapRelativePath)) {
			return false;
		}

		clienteditor::protobuf::mapdata::MapData previousMapData;
		if (!loadCyclopediaMapDataTemplate(basePath / previousMapRelativePath, previousMapData)) {
			return true;
		}

		std::unordered_set<std::string> backedUpAssets;
		backedUpAssets.reserve(static_cast<size_t>(previousMapData.mapassets_size() * 2));
		for (const auto &mapAsset : previousMapData.mapassets()) {
			if (mapAsset.type() == clienteditor::protobuf::mapdata::MapAssets_AssetsType_SUBAREA) {
				continue;
			}

			if (!mapAsset.has_filename() || mapAsset.filename().empty()) {
				continue;
			}

			const std::filesystem::path assetRelativePath(mapAsset.filename());
			if (!isSafeRelativePath(assetRelativePath)) {
				return false;
			}

			const std::string normalizedAssetFileName = assetRelativePath.generic_string();
			if (replacementAssetFileNames.contains(normalizedAssetFileName) || !backedUpAssets.insert(normalizedAssetFileName).second) {
				continue;
			}

			if (!backupFileIfExists(basePath / assetRelativePath, basePath, backupRootPath)) {
				return false;
			}
		}

		return true;
	}

	bool restoreCyclopediaMapAssetFile(
		const std::filesystem::path &basePath,
		const std::filesystem::path &backupRootPath,
		const std::filesystem::path &templateBasePath,
		const std::filesystem::path &assetRelativePath,
		const std::string &normalizedAssetFileName
	) {
		const std::filesystem::path targetPath = basePath / assetRelativePath;
		if (std::filesystem::exists(targetPath)) {
			return true;
		}

		std::vector<std::filesystem::path> sourceCandidates;
		collectCyclopediaBackupFileCandidates(backupRootPath, assetRelativePath, sourceCandidates);
		if (!templateBasePath.empty()) {
			sourceCandidates.emplace_back(templateBasePath / assetRelativePath);
		}

		for (const auto &sourcePath : sourceCandidates) {
			if (!std::filesystem::exists(sourcePath) || !std::filesystem::is_regular_file(sourcePath)) {
				continue;
			}

			std::error_code ec;
			if (!targetPath.parent_path().empty()) {
				std::filesystem::create_directories(targetPath.parent_path(), ec);
			}
			ec.clear();
			std::filesystem::copy_file(sourcePath, targetPath, std::filesystem::copy_options::overwrite_existing, ec);
			if (ec) {
				spdlog::warn("Failed to restore cyclopedia map asset '{}': {}", normalizedAssetFileName, ec.message());
				return false;
			}

			return true;
		}

		spdlog::warn("Missing cyclopedia map asset '{}'", normalizedAssetFileName);
		return false;
	}

	bool restoreCyclopediaMapAssets(
		const std::filesystem::path &basePath,
		const std::filesystem::path &backupRootPath,
		const std::filesystem::path &templateBasePath,
		const clienteditor::protobuf::mapdata::MapData &mapData,
		const bool subareaOnly
	) {
		std::unordered_set<std::string> restoredAssets;
		restoredAssets.reserve(static_cast<size_t>(mapData.mapassets_size()));
		for (const auto &mapAsset : mapData.mapassets()) {
			if (subareaOnly && mapAsset.type() != clienteditor::protobuf::mapdata::MapAssets_AssetsType_SUBAREA) {
				continue;
			}
			if (!mapAsset.has_filename() || mapAsset.filename().empty()) {
				continue;
			}

			const std::filesystem::path assetRelativePath(mapAsset.filename());
			if (!isSafeRelativePath(assetRelativePath)) {
				return false;
			}

			const std::string normalizedAssetFileName = assetRelativePath.generic_string();
			if (!restoredAssets.insert(normalizedAssetFileName).second) {
				continue;
			}

			if (!restoreCyclopediaMapAssetFile(basePath, backupRootPath, templateBasePath, assetRelativePath, normalizedAssetFileName)) {
				return false;
			}
		}

		return true;
	}

	bool backupStaleCyclopediaAssetFiles(
		const std::filesystem::path &basePath,
		const std::filesystem::path &backupRootPath,
		const std::unordered_set<std::string> &replacementAssetFileNames,
		const std::unordered_set<std::string> &replacementAssetKeys
	) {
		if (basePath.empty() || !std::filesystem::exists(basePath) || replacementAssetKeys.empty()) {
			return true;
		}

		for (const auto &entry : std::filesystem::directory_iterator(basePath)) {
			if (!entry.is_regular_file()) {
				continue;
			}

			const std::filesystem::path path = entry.path();
			const std::string normalizedFileName = path.filename().generic_string();
			if (replacementAssetFileNames.contains(normalizedFileName)) {
				continue;
			}

			const std::string replacementKey = getCyclopediaAssetReplacementKey(path);
			if (replacementKey.empty() || !replacementAssetKeys.contains(replacementKey)) {
				continue;
			}

			if (!backupFileIfExists(path, basePath, backupRootPath)) {
				return false;
			}
		}

		return true;
	}

	bool updateCyclopediaCatalogFiles(
		const std::filesystem::path &basePath,
		const CyclopediaCatalogFiles &catalogFiles,
		const std::filesystem::path &backupRootPath
	) {
		if (basePath.empty()) {
			return false;
		}

		const std::filesystem::path catalogPath = basePath / "catalog-content.json";
		nlohmann::json catalog = nlohmann::json::array();
		if (std::filesystem::exists(catalogPath)) {
			std::ifstream input(catalogPath, std::ios::binary);
			if (!input.is_open()) {
				return false;
			}

			nlohmann::json parsed = nlohmann::json::parse(input, nullptr, false);
			if (!parsed.is_array()) {
				return false;
			}
			catalog = std::move(parsed);
		}

		bool changed = false;
		const auto upsertFile = [&](const std::string &type, const std::string &fileName) {
			if (fileName.empty()) {
				return;
			}

			for (auto &entry : catalog) {
				if (!entry.is_object() || entry.value("type", std::string()) != type) {
					continue;
				}

				if (entry.value("file", std::string()) != fileName) {
					entry["file"] = fileName;
					changed = true;
				}
				return;
			}

			nlohmann::json entry;
			entry["type"] = type;
			entry["file"] = fileName;
			catalog.push_back(std::move(entry));
			changed = true;
		};

		upsertFile("map", catalogFiles.mapFileName);
		upsertFile("staticmapdata", catalogFiles.staticMapDataFileName);
		upsertFile("staticdata", catalogFiles.staticDataFileName);

		if (!changed && std::filesystem::exists(catalogPath)) {
			return true;
		}

		std::error_code ec;
		std::filesystem::create_directories(basePath, ec);
		if (ec) {
			return false;
		}

		if (!backupFileIfExists(catalogPath, basePath, backupRootPath)) {
			return false;
		}

		std::ofstream output(catalogPath, std::ios::binary | std::ios::trunc);
		if (!output.is_open()) {
			return false;
		}

		output << catalog.dump(2) << '\n';
		return output.good();
	}
}

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

namespace {
	struct FloorLookupCache {
		Floor* floor = nullptr;
		int floorX = -1;
		int floorY = -1;
		int floorZ = -1;
	};

	Tile* getCachedTile(Map &map, const Position &position, FloorLookupCache &cache) {
		const int floorX = position.x & ~3;
		const int floorY = position.y & ~3;
		if (!cache.floor || cache.floorX != floorX || cache.floorY != floorY || cache.floorZ != position.z) {
			QTreeNode* leaf = map.getLeaf(position.x, position.y);
			cache.floor = leaf ? leaf->getFloor(position.z) : nullptr;
			cache.floorX = floorX;
			cache.floorY = floorY;
			cache.floorZ = position.z;
		}

		if (!cache.floor) {
			return nullptr;
		}

		return cache.floor->locs[(position.x & 3) * 4 + (position.y & 3)].get();
	}

	bool readFileContent(const wxString &filepath, std::string &content) {
		if (!wxFileExists(filepath)) {
			return false;
		}

		wxFile file(filepath, wxFile::read);
		if (!file.IsOpened()) {
			return false;
		}

		const wxFileOffset fileSize = file.Length();
		if (fileSize < 0) {
			return false;
		}

		content.resize(static_cast<size_t>(fileSize));
		if (content.empty()) {
			return true;
		}

		const auto bytesRead = file.Read(content.data(), content.size());
		return static_cast<size_t>(bytesRead) == content.size();
	}

	bool readNormalizedLineEndingChar(const std::string &content, size_t &offset, char &out) {
		if (offset >= content.size()) {
			return false;
		}

		out = content[offset++];
		if (out == '\r') {
			if (offset < content.size() && content[offset] == '\n') {
				++offset;
			}
			out = '\n';
		}
		return true;
	}

	bool contentMatchesIgnoringLineEndings(const std::string &left, const std::string &right) {
		size_t leftOffset = 0;
		size_t rightOffset = 0;
		char leftChar = '\0';
		char rightChar = '\0';

		while (true) {
			const bool hasLeft = readNormalizedLineEndingChar(left, leftOffset, leftChar);
			if (const bool hasRight = readNormalizedLineEndingChar(right, rightOffset, rightChar); hasLeft != hasRight) {
				return false;
			}
			if (!hasLeft) {
				return true;
			}
			if (leftChar != rightChar) {
				return false;
			}
		}
	}

	bool fileMatchesXmlContent(const wxString &filepath, const std::string &content) {
		std::string existingContent;
		if (!readFileContent(filepath, existingContent)) {
			return false;
		}

		if (existingContent == content) {
			return true;
		}

		return contentMatchesIgnoringLineEndings(existingContent, content);
	}

	bool writeContentToFile(const wxString &filepath, const std::string &content) {
		wxFile file(filepath, wxFile::write);
		if (!file.IsOpened()) {
			return false;
		}

		if (!content.empty()) {
			const auto bytesWritten = file.Write(content.data(), content.size());
			if (static_cast<size_t>(bytesWritten) != content.size()) {
				return false;
			}
		}
		return file.Close();
	}

	FORCEINLINE bool isWildcardOtbmIdentifier(const uint8_t* data) {
		return data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 0;
	}

	FORCEINLINE bool hasValidOtbmPrefix(const uint8_t* data, size_t size) {
		if (size < 5) {
			return false;
		}
		if (!isWildcardOtbmIdentifier(data) && memcmp(data, "OTBM", 4) != 0) {
			return false;
		}
		return data[4] == NODE_START;
	}

#if OTGZ_SUPPORT > 0
	bool readArchiveDataFully(struct archive* archive, uint8_t* buffer, size_t targetSize, size_t &outReadBytes) {
		outReadBytes = 0;
		while (outReadBytes < targetSize) {
			const auto readNow = archive_read_data(archive, buffer + outReadBytes, targetSize - outReadBytes);
			if (readNow < 0) {
				return false;
			}
			if (readNow == 0) {
				break;
			}
			outReadBytes += static_cast<size_t>(readNow);
		}
		return true;
	}
#endif

	bool saveXmlFileIfChanged(const pugi::xml_document &doc, const wxString &filepath) {
		std::ostringstream stream;
		doc.save(stream, "\t", pugi::format_default, pugi::encoding_utf8);
		const std::string content = stream.str();

		if (fileMatchesXmlContent(filepath, content)) {
			return true;
		}

		const wxString backupPath = filepath + "~";
		if (!wxFileExists(filepath) && fileMatchesXmlContent(backupPath, content)) {
			return wxRenameFile(backupPath, filepath, false);
		}

		return writeContentToFile(filepath, content);
	}

	struct TileAreaSaveState {
		uint32_t tilesSaved = 0;
		bool firstArea = true;
		int localX = -1;
		int localY = -1;
		int localZ = -1;
	};

	FORCEINLINE void updateTileSaveProgress(const Map &map, TileAreaSaveState &state) {
		++state.tilesSaved;
		const uint64_t tileCount = map.getTileCount();
		if (tileCount != 0 && state.tilesSaved % 8192 == 0) {
			g_gui.SetLoadDone(int(state.tilesSaved / double(tileCount) * 100.0));
		}
	}

	FORCEINLINE bool needsNewTileArea(const Position &position, const TileAreaSaveState &state) {
		return position.x < state.localX || position.x >= state.localX + 256 || position.y < state.localY || position.y >= state.localY + 256 || position.z != state.localZ;
	}

	FORCEINLINE void beginTileArea(NodeFileWriteHandle &file, const Position &position, TileAreaSaveState &state) {
		if (!state.firstArea) {
			file.endNode();
		}
		state.firstArea = false;

		file.addNode(OTBM_TILE_AREA);
		state.localX = position.x & 0xFF00;
		state.localY = position.y & 0xFF00;
		state.localZ = position.z;
		file.addU16(state.localX);
		file.addU16(state.localY);
		file.addU8(state.localZ);
	}

	FORCEINLINE void saveTileGround(const IOMapOTBM &mapHandle, NodeFileWriteHandle &file, const Tile &tile) {
		if (!tile.ground) {
			return;
		}

		Item* ground = tile.ground;
		const ItemType &groundType = ground->getItemType();
		const uint16_t groundId = ground->getID();
		if (groundId == 0 || groundType.isMetaItem()) {
			return;
		}

		if (groundType.has_equivalent) {
			const bool found = std::any_of(tile.items.begin(), tile.items.end(), [groundId](const Item* item) {
				return item->getItemType().ground_equivalent == groundId;
			});
			if (!found) {
				ground->serializeItemNode_OTBM(mapHandle, file);
			}
			return;
		}

		if (ground->isComplex()) {
			ground->serializeItemNode_OTBM(mapHandle, file);
			return;
		}

		file.addByte(OTBM_ATTR_ITEM);
		ground->serializeItemCompact_OTBM(mapHandle, file);
	}

	FORCEINLINE void saveTileItems(const IOMapOTBM &mapHandle, NodeFileWriteHandle &file, const Tile &tile) {
		for (Item* item : tile.items) {
			const ItemType &itemType = item->getItemType();
			if (itemType.isMetaItem() || item->getID() == 0) {
				continue;
			}
			item->serializeItemNode_OTBM(mapHandle, file);
		}
	}

	FORCEINLINE void saveTileZones(NodeFileWriteHandle &file, const Tile &tile) {
		if (tile.zones.empty()) {
			return;
		}

		file.addNode(OTBM_TILE_ZONE);
		file.addU16(tile.zones.size());
		for (const auto &zoneId : tile.zones) {
			file.addU16(zoneId);
		}
		file.endNode();
	}

	FORCEINLINE void saveTileLocation(const IOMapOTBM &mapHandle, Map &map, NodeFileWriteHandle &file, TileAreaSaveState &state, TileLocation* tileLocation) {
		updateTileSaveProgress(map, state);

		Tile* saveTile = tileLocation->get();
		if (!saveTile || saveTile->empty()) {
			return;
		}

		const Position &position = saveTile->getPosition();
		if (needsNewTileArea(position, state)) {
			beginTileArea(file, position, state);
		}

		file.addNode(saveTile->isHouseTile() ? OTBM_HOUSETILE : OTBM_TILE);
		file.addU8(saveTile->getX() & 0xFF);
		file.addU8(saveTile->getY() & 0xFF);

		if (saveTile->isHouseTile()) {
			file.addU32(saveTile->getHouseID());
		}

		if (saveTile->getMapFlags()) {
			file.addByte(OTBM_ATTR_TILE_FLAGS);
			file.addU32(saveTile->getMapFlags());
		}

		saveTileGround(mapHandle, file, *saveTile);
		saveTileItems(mapHandle, file, *saveTile);
		saveTileZones(file, *saveTile);
		file.endNode();
	}
}

// ============================================================================
// Item

Item* Item::Create_OTBM(const IOMap &maphandle, BinaryNode* stream, const ItemType** itemType) {
	if (itemType) {
		*itemType = nullptr;
	}

	uint16_t id;
	if (!stream->getU16(id)) {
		return nullptr;
	}

	const ItemType &type = g_items.getItemType(id);
	if (itemType) {
		*itemType = &type;
	}

	uint8_t count = 0;
	if (maphandle.version.otbm == MAP_OTBM_1) {
		if (type.stackable || type.isSplash() || type.isFluidContainer()) {
			stream->getU8(count);
		}
	}
	return Item::Create(id, type, count);
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
	const ItemType &type = g_items.getItemType(id);
	if (maphandle.version.otbm >= MAP_OTBM_2) {
		if (type.stackable || type.isSplash() || type.isFluidContainer()) {
			stream.addU8(OTBM_ATTR_COUNT);
			stream.addU8(getSubtype(type));
		}
	}

	if (maphandle.version.otbm == MAP_OTBM_4 || maphandle.version.otbm > MAP_OTBM_5) {
		if (attributes && !attributes->empty()) {
			stream.addU8(OTBM_ATTR_ATTRIBUTE_MAP);
			serializeAttributeMap(maphandle, stream);
		}
	} else {
		if (type.isClientCharged() || type.isExtraCharged()) {
			stream.addU8(OTBM_ATTR_CHARGES);
			stream.addU16(getSubtype(type));
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

		const std::string* text = getStringAttribute("text");
		if (text && !text->empty()) {
			stream.addU8(OTBM_ATTR_TEXT);
			stream.addString(*text);
		}

		const std::string* description = getStringAttribute("desc");
		if (description && !description->empty()) {
			stream.addU8(OTBM_ATTR_DESC);
			stream.addString(*description);
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
			file.addU8(getSubtype(type));
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

				size_t readBytes = 0;
				if (!readArchiveDataFully(a.get(), buffer, sizeof(buffer), readBytes)) {
					return false;
				}

				if (!hasValidOtbmPrefix(buffer, readBytes)) {
					return false;
				}

				// Create a read handle on it
				std::shared_ptr<NodeFileReadHandle> f(new MemoryNodeFileReadHandle(buffer + 4, readBytes - 4));

				// Read the version info
				return getVersionInfo(f.get(), out_ver);
			}
		}

		// Didn't find OTBM file, lame
		return false;
	}
#endif

	FileReadHandle otbmProbe(nstr(filename.GetFullPath()));
	if (!otbmProbe.isOk()) {
		return false;
	}

	std::array<uint8_t, 5> otbmPrefix {};
	if (otbmProbe.size() < otbmPrefix.size()) {
		return false;
	}
	if (!otbmProbe.getRAW(otbmPrefix.data(), otbmPrefix.size())) {
		return false;
	}
	if (!hasValidOtbmPrefix(otbmPrefix.data(), otbmPrefix.size())) {
		return false;
	}

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

				size_t readBytes = 0;
				if (!readArchiveDataFully(a.get(), otbm_buffer.get(), otbm_size, readBytes)) {
					error("Could not read file.");
					return false;
				}
				if (readBytes < otbm_size) {
					error("Could not read file.");
					return false;
				}
				if (!hasValidOtbmPrefix(otbm_buffer.get(), readBytes)) {
					error("Could not read OTBM file header.");
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

	FileReadHandle otbmFile(nstr(filename.GetFullPath()));
	if (!otbmFile.isOk()) {
		error(("Couldn't open file for reading\nThe error reported was: " + wxstr(otbmFile.getErrorMessage())).wc_str());
		return false;
	}

	const size_t otbmSize = otbmFile.size();
	if (otbmSize < 5) {
		error("Could not read OTBM file header.");
		return false;
	}

	std::vector<uint8_t> otbmBuffer(otbmSize);
	if (!otbmFile.getRAW(otbmBuffer.data(), otbmBuffer.size())) {
		error(("Couldn't read file\nThe error reported was: " + wxstr(otbmFile.getErrorMessage())).wc_str());
		return false;
	}

	const bool hasKnownIdentifier = isWildcardOtbmIdentifier(otbmBuffer.data()) || memcmp(otbmBuffer.data(), "OTBM", 4) == 0;
	if (!hasKnownIdentifier) {
		error("File magic number not recognized.");
		return false;
	}
	if (otbmBuffer[4] != NODE_START) {
		error("Could not read root node.");
		return false;
	}

	MemoryNodeFileReadHandle f(otbmBuffer.data() + 4, otbmSize - 4);
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
	int32_t lastProgress = -1;
	auto lastProgressUpdate = std::chrono::steady_clock::now();
	const int64_t fileSize = std::max<int64_t>(static_cast<int64_t>(f.size()), 1);

	for (BinaryNode* mapNode = mapHeaderNode->getChild(); mapNode != nullptr; mapNode = mapNode->advance()) {
		++nodes_loaded;
		if ((nodes_loaded & 127) == 0) {
			const auto fileOffset = static_cast<int64_t>(f.tell());
			const int32_t progress = std::min<int32_t>(100, static_cast<int32_t>((fileOffset * 100) / fileSize));
			if (progress > lastProgress) {
				const auto now = std::chrono::steady_clock::now();
				const bool firstUpdate = lastProgress < 0;
				const bool enoughTimeElapsed = (now - lastProgressUpdate) >= std::chrono::milliseconds(200);
				const bool significantStep = (progress - lastProgress) >= 3;

				if (firstUpdate || enoughTimeElapsed || significantStep || progress >= 100) {
					g_gui.SetLoadDone(progress);
					lastProgress = progress;
					lastProgressUpdate = now;
				}
			}
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

			Floor* cachedFloor = nullptr;
			int cachedFloorX = -1;
			int cachedFloorY = -1;
			int cachedFloorZ = -1;
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

					const int floorX = pos.x & ~3;
					const int floorY = pos.y & ~3;
					if (!cachedFloor || cachedFloorX != floorX || cachedFloorY != floorY || cachedFloorZ != pos.z) {
						cachedFloor = map.createLeaf(pos.x, pos.y)->createFloor(pos.x, pos.y, pos.z);
						cachedFloorX = floorX;
						cachedFloorY = floorY;
						cachedFloorZ = pos.z;
					}

					TileLocation* tileLocation = &cachedFloor->locs[(pos.x & 3) * 4 + (pos.y & 3)];
					if (tileLocation->get()) {
						warning("Duplicate tile at %d:%d:%d, discarding duplicate", pos.x, pos.y, pos.z);
						continue;
					}

					tile = map.allocator(tileLocation);
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

					bool needsFullTileUpdate = false;
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
								const ItemType* itemType = nullptr;
								Item* item = Item::Create_OTBM(*this, tileNode, &itemType);
								if (item == nullptr) {
									warning("Invalid item at tile %d:%d:%d", pos.x, pos.y, pos.z);
								} else {
									if (((itemType->isGroundTile() || itemType->ground_equivalent != 0) && tile->ground) || (itemType->alwaysOnBottom && !tile->items.empty())) {
										needsFullTileUpdate = true;
									}
									tile->addLoadedItem(item, *itemType);
								}
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
							const ItemType* itemType = nullptr;
							item = Item::Create_OTBM(*this, childNode, &itemType);
							if (item) {
								if (!item->unserializeItemNode_OTBM(*this, childNode)) {
									warning("Couldn't unserialize item attributes at %d:%d:%d", pos.x, pos.y, pos.z);
								}
								// reform(&map, tile, item);
								if (((itemType->isGroundTile() || itemType->ground_equivalent != 0) && tile->ground) || (itemType->alwaysOnBottom && !tile->items.empty())) {
									needsFullTileUpdate = true;
								}
								tile->addLoadedItem(item, *itemType);
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

					if (needsFullTileUpdate) {
						tile->update();
					} else {
						tile->finalizeLoadedState();
					}
					if (house) {
						house->addTile(tile);
					}

					map.setTile(tileLocation, tile);
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

	FloorLookupCache tileCache;
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

		Tile* tile = getCachedTile(map, spawnPosition, tileCache);
		if (tile && tile->spawnMonster) {
			warning("Duplicate monster spawn on position %d:%d:%d\n", tile->getX(), tile->getY(), tile->getZ());
			continue;
		}

		SpawnMonster* spawnMonster = newd SpawnMonster(radius);
		if (!tile) {
			TileLocation* tileLocation = map.createTileL(spawnPosition);
			tile = map.allocator(tileLocation);
			map.setTile(tileLocation, tile);
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
				err << "Bad monster position data, discarding monster at spawn " << spawnPosition.x << ":" << spawnPosition.y << ":" << spawnPosition.z << " due to missing name.";
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
				monsterTile = getCachedTile(map, monsterPosition, tileCache);
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

	FloorLookupCache tileCache;
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

		Tile* spawnTile = getCachedTile(map, spawnPosition, tileCache);
		if (spawnTile && spawnTile->spawnNpc) {
			warning("Duplicate npc spawn on position %d:%d:%d\n", spawnTile->getX(), spawnTile->getY(), spawnTile->getZ());
			continue;
		}

		SpawnNpc* spawnNpc = newd SpawnNpc(radius);
		if (!spawnTile) {
			TileLocation* tileLocation = map.createTileL(spawnPosition);
			spawnTile = map.allocator(tileLocation);
			map.setTile(tileLocation, spawnTile);
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
				err << "Bad npc position data, discarding npc at spawn " << spawnPosition.x << ":" << spawnPosition.y << ":" << spawnPosition.z << " due to missing name.";
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
				npcTile = getCachedTile(map, npcPosition, tileCache);
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
			TileAreaSaveState tileAreaState;
			auto writeTileLocation = [&](TileLocation* tileLocation) {
				saveTileLocation(self, map, f, tileAreaState, tileLocation);
			};
			map.forEachTileLocation(writeTileLocation);

			// Only close the last node if one has actually been created
			if (!tileAreaState.firstArea) {
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
		return saveXmlFileIfChanged(doc, filepath);
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
		return saveXmlFileIfChanged(doc, filepath);
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
	for (const auto &[mapHouseId, house] : map.houses) {
		(void)mapHouseId;
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

std::string IOMapOTBM::getStaticDataFilename([[maybe_unused]] const Map &map) const {
	return "staticdata.dat";
}

std::string IOMapOTBM::getStaticMapDataFilename([[maybe_unused]] const Map &map) const {
	return "staticmapdata.dat";
}

bool IOMapOTBM::serializeStaticDataHouses(Map &map, std::string &buffer) {
	namespace pb_staticdata = clienteditor::protobuf::staticdata;

	pb_staticdata::StaticData staticData;
	for (const auto &[mapHouseId, house] : map.houses) {
		(void)mapHouseId;
		if (!house) {
			continue;
		}
		if (!shouldIncludeHouseInStaticExport(*house)) {
			continue;
		}

		const uint32_t houseId = house->id;
		if (houseId == 0) {
			continue;
		}

		auto* houseNode = staticData.add_house();
		houseNode->set_house_id(houseId);
		houseNode->set_name(house->name);
		houseNode->set_unknownstring("");
		houseNode->set_price(house->rent > 0 ? static_cast<uint32_t>(house->rent) : 0);
		houseNode->set_beds(house->beds > 0 ? static_cast<uint32_t>(house->beds) : 0);

		const Position &exitPosition = house->getExit();
		auto* position = houseNode->mutable_houseposition();
		position->set_pos_x(toCyclopediaProtoCoordinate(exitPosition.x));
		position->set_pos_y(toCyclopediaProtoCoordinate(exitPosition.y));
		position->set_pos_z(toCyclopediaProtoCoordinate(exitPosition.z));

		houseNode->set_size_sqm(static_cast<uint32_t>(house->size()));
		houseNode->set_guildhall(house->guildhall);

		const Town* town = map.towns.getTown(house->townid);
		houseNode->set_city(town ? town->getName() : "");
		houseNode->set_shop(isShopHouseName(house->name));
	}

	return staticData.SerializeToString(&buffer);
}

bool IOMapOTBM::serializeStaticMapDataHouses(
	Map &map,
	std::string &buffer,
	size_t* attemptedHouseCount,
	size_t* exportedHouseCount,
	std::vector<std::string>* failedHouseNames
) {
	namespace pb_staticmapdata = clienteditor::protobuf::staticmapdata;

	if (attemptedHouseCount) {
		*attemptedHouseCount = 0;
	}
	if (exportedHouseCount) {
		*exportedHouseCount = 0;
	}
	if (failedHouseNames) {
		failedHouseNames->clear();
	}

	pb_staticmapdata::StaticMapData staticMapData;
	for (const auto &[mapHouseId, house] : map.houses) {
		(void)mapHouseId;
		if (!house || house->id == 0) {
			continue;
		}
		if (!shouldIncludeHouseInStaticExport(*house)) {
			continue;
		}
		if (attemptedHouseCount) {
			++(*attemptedHouseCount);
		}

		const StaticMapHouseTemplate* houseTemplate = nullptr;
		const auto* activeTemplates = activeStaticMapHouseTemplates();
		if (activeTemplates) {
			const auto templateIt = activeTemplates->find(house->id);
			if (templateIt != activeTemplates->end()) {
				houseTemplate = &templateIt->second;
			}
		}

		auto* mapHouseEntry = staticMapData.add_house();
		if (!buildStaticMapHousePreviewData(map, *house, *mapHouseEntry, houseTemplate)) {
			if (isHouseDebugTarget(*house)) {
				spdlog::warn("[house-debug] failed to build staticmapdata preview for house={} name='{}'", house->id, house->name);
			}
			if (failedHouseNames) {
				failedHouseNames->push_back(house->name);
			}
			staticMapData.mutable_house()->RemoveLast();
			continue;
		}
		if (exportedHouseCount) {
			++(*exportedHouseCount);
		}
	}

	return staticMapData.SerializeToString(&buffer);
}

bool IOMapOTBM::saveStaticData(Map &map, const FileName &dir, const std::vector<std::string> &houseNamesFilter) {
	namespace pb_staticdata = clienteditor::protobuf::staticdata;
	namespace pb_staticmapdata = clienteditor::protobuf::staticmapdata;

	auto &lastStaticHouseExportReport = staticHouseExportReport_;
	lastStaticHouseExportReport = StaticHouseExportReport {};

	const std::filesystem::path requestedOutputPath = nstr(dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME));
	const std::filesystem::path basePath = resolveCyclopediaCatalogBasePath(requestedOutputPath);
	const std::filesystem::path backupRootBasePath = basePath / "bkps";
	std::filesystem::path backupRootPath;
	lastStaticHouseExportReport.outputBasePath = basePath.string();
	StaticDataHouseNameFilter normalizedHouseNameFilter;
	for (const std::string &houseName : houseNamesFilter) {
		const std::string normalizedHouseName = normalizeHouseName(houseName);
		if (!normalizedHouseName.empty()) {
			normalizedHouseNameFilter.insert(normalizedHouseName);
		}
	}
	const bool hasHouseNameFilter = !normalizedHouseNameFilter.empty();
	lastStaticHouseExportReport.filtered = hasHouseNameFilter;
	lastStaticHouseExportReport.selectedFilterCount = normalizedHouseNameFilter.size();

	size_t mapHouseCount = 0;
	size_t matchedFilteredHouseCount = 0;
	for (const auto &[mapHouseId, house] : map.houses) {
		(void)mapHouseId;
		if (!house || house->id == 0) {
			continue;
		}
		++mapHouseCount;
		if (!hasHouseNameFilter) {
			continue;
		}

		if (normalizedHouseNameFilter.contains(normalizeHouseName(house->name))) {
			++matchedFilteredHouseCount;
		}
	}
	lastStaticHouseExportReport.mapHousesTotal = mapHouseCount;
	lastStaticHouseExportReport.matchedFilterCount = matchedFilteredHouseCount;

	const auto registerExportError = [&](const std::string &errorMessage) {
		lastStaticHouseExportReport.errors.push_back(errorMessage);
		warning("%s", wxstr(errorMessage));
	};

	struct StaticHouseExportFilterScopeGuard final {
		StaticDataHouseNameFilterPtr previousFilter = nullptr;
		StaticHouseExportFilterScopeGuard() = default;
		StaticHouseExportFilterScopeGuard(const StaticHouseExportFilterScopeGuard &) = delete;
		StaticHouseExportFilterScopeGuard &operator=(const StaticHouseExportFilterScopeGuard &) = delete;
		~StaticHouseExportFilterScopeGuard() {
			activeStaticDataHouseNameFilter() = previousFilter;
		}
	};
	StaticHouseExportFilterScopeGuard staticHouseExportFilterScopeGuard;
	staticHouseExportFilterScopeGuard.previousFilter = activeStaticDataHouseNameFilter();
	activeStaticDataHouseNameFilter() = hasHouseNameFilter ? &normalizedHouseNameFilter : nullptr;

	struct StaticHouseDebugFilterScopeGuard final {
		StaticDataHouseNameFilterPtr previousFilter = nullptr;
		StaticHouseDebugFilterScopeGuard() = default;
		StaticHouseDebugFilterScopeGuard(const StaticHouseDebugFilterScopeGuard &) = delete;
		StaticHouseDebugFilterScopeGuard &operator=(const StaticHouseDebugFilterScopeGuard &) = delete;
		~StaticHouseDebugFilterScopeGuard() {
			activeStaticDataDebugHouseNameFilter() = previousFilter;
		}
	};
	StaticHouseDebugFilterScopeGuard staticHouseDebugFilterScopeGuard;
	staticHouseDebugFilterScopeGuard.previousFilter = activeStaticDataDebugHouseNameFilter();
	activeStaticDataDebugHouseNameFilter() = hasHouseNameFilter ? &normalizedHouseNameFilter : nullptr;

	std::error_code ec;
	std::filesystem::create_directories(basePath, ec);
	backupRootPath = createCyclopediaBackupRunPath(backupRootBasePath, "export");
	if (backupRootPath.empty()) {
		registerExportError("Failed to create backup snapshot directory.");
		return false;
	}
	spdlog::info(
		"[house-debug] saveStaticData begin: requested='{}' resolved_base='{}' backup_root='{}' filter_enabled={} filter_count={}",
		requestedOutputPath.string(),
		basePath.string(),
		backupRootPath.string(),
		hasHouseNameFilter,
		normalizedHouseNameFilter.size()
	);

	CyclopediaCatalogFiles outputCatalogFiles;
	loadCyclopediaCatalogFiles(basePath, outputCatalogFiles);

	const wxString clientAssetsRoot = ClientAssets::getPath();
	const std::filesystem::path clientAssetsPath = clientAssetsRoot.empty() ? std::filesystem::path() : resolveCyclopediaCatalogBasePath(std::filesystem::path(nstr(clientAssetsRoot)));
	CyclopediaCatalogFiles sourceCatalogFiles;
	if (!clientAssetsPath.empty()) {
		loadCyclopediaCatalogFiles(clientAssetsPath, sourceCatalogFiles);
	}

	std::filesystem::path staticDataTemplateFileName = outputCatalogFiles.staticDataFileName;
	if (staticDataTemplateFileName.empty()) {
		staticDataTemplateFileName = sourceCatalogFiles.staticDataFileName;
	}
	if (staticDataTemplateFileName.empty()) {
		staticDataTemplateFileName = getStaticDataFilename(map);
	}

	std::string buffer;
	if (!serializeStaticDataHouses(map, buffer)) {
		registerExportError("Failed to serialize staticdata protobuf.");
		return false;
	}

	{
		pb_staticdata::StaticData generatedStaticData;
		if (generatedStaticData.ParseFromString(buffer)) {
			lastStaticHouseExportReport.staticDataGeneratedHouses = static_cast<size_t>(generatedStaticData.house_size());
		}
	}
	if (hasHouseNameFilter) {
		pb_staticdata::StaticData filteredGeneratedStaticData;
		if (!filteredGeneratedStaticData.ParseFromString(buffer) || filteredGeneratedStaticData.house_size() == 0) {
			registerExportError("House filter export did not match any house name in current map.");
			return false;
		}
	}

	std::unordered_map<uint32_t, uint32_t> houseIdRemap;
	std::unordered_map<uint32_t, HousePositionDelta> housePositionDeltaRemap;
	pb_staticdata::StaticData templateStaticData;
	std::filesystem::path loadedStaticDataTemplatePath;
	std::vector<std::filesystem::path> staticDataTemplateCandidates;
	staticDataTemplateCandidates.reserve(2);
	staticDataTemplateCandidates.emplace_back(basePath / staticDataTemplateFileName);
	if (!sourceCatalogFiles.staticDataFileName.empty() && !clientAssetsPath.empty()) {
		staticDataTemplateCandidates.emplace_back(clientAssetsPath / sourceCatalogFiles.staticDataFileName);
	}
	const bool hasTemplateStaticData = loadFirstTemplateCandidate(
		staticDataTemplateCandidates,
		loadStaticDataTemplate,
		templateStaticData,
		&loadedStaticDataTemplatePath
	);
	if (!hasTemplateStaticData) {
		const auto siblingTemplatePath = findValidTemplateAssetSibling(basePath, staticDataTemplateFileName);
		if (!siblingTemplatePath.empty()) {
			if (loadStaticDataTemplate(siblingTemplatePath, templateStaticData)) {
				loadedStaticDataTemplatePath = siblingTemplatePath;
				spdlog::info("[house-debug] using sibling staticdata template '{}'", siblingTemplatePath.string());
			}
		}
	}

	bool useStaticDataTemplate = false;
	if (!loadedStaticDataTemplatePath.empty()) {
		std::string mergedBuffer;
		StaticDataTemplateMergeStats mergeStats;
		if (mergeStaticDataTemplate(templateStaticData, buffer, mergedBuffer, &houseIdRemap, &housePositionDeltaRemap, &mergeStats)) {
			if (mergeStats.matchedTemplateHouses > 0) {
				buffer = std::move(mergedBuffer);
				useStaticDataTemplate = true;
			} else {
				houseIdRemap.clear();
				housePositionDeltaRemap.clear();
				registerExportError("Staticdata template did not match any current map house; exporting current map houses without CIP template merge.");
			}
		} else {
			houseIdRemap.clear();
			housePositionDeltaRemap.clear();
			registerExportError("Failed to merge template staticdata protobuf content; exporting current map houses without CIP template merge.");
		}
	} else {
		registerExportError("No valid staticdata template file found; exporting current map houses without CIP template merge.");
	}
	spdlog::info(
		"[house-debug] staticdata stage: template='{}' template_used={} remap_size={} position_delta_size={}",
		loadedStaticDataTemplatePath.string(),
		useStaticDataTemplate,
		houseIdRemap.size(),
		housePositionDeltaRemap.size()
	);

	std::vector<uint32_t> staticDataHouseOrder;
	if (!extractStaticDataHouseOrder(buffer, staticDataHouseOrder)) {
		registerExportError("Failed to read staticdata house order for staticmapdata sorting.");
	}

	{
		pb_staticdata::StaticData finalStaticData;
		if (finalStaticData.ParseFromString(buffer)) {
			lastStaticHouseExportReport.staticDataFinalHouses = static_cast<size_t>(finalStaticData.house_size());
		}
	}

	std::string staticDataFileName = buildCatalogDataFilename("staticdata", buffer);
	lastStaticHouseExportReport.staticDataFileName = staticDataFileName;

	const std::filesystem::path outputPath = basePath / staticDataFileName;
	if (!outputPath.parent_path().empty()) {
		std::filesystem::create_directories(outputPath.parent_path(), ec);
	}
	if (!backupCatalogFileIfReplaced(basePath, backupRootPath, outputCatalogFiles.staticDataFileName, staticDataFileName)) {
		registerExportError("Failed to backup previous staticdata file.");
		return false;
	}
	if (!backupStaleCatalogDataFiles(basePath, backupRootPath, "staticdata", staticDataFileName)) {
		registerExportError("Failed to backup stale staticdata files.");
		return false;
	}
	if (!backupFileIfExists(outputPath, basePath, backupRootPath)) {
		registerExportError("Failed to create backup for staticdata file.");
		return false;
	}

	if (!writeBinaryFile(outputPath, buffer)) {
		registerExportError("Failed to save staticdata protobuf file.");
		return false;
	}

	std::filesystem::path staticMapDataTemplateFileName = outputCatalogFiles.staticMapDataFileName;
	if (staticMapDataTemplateFileName.empty()) {
		staticMapDataTemplateFileName = sourceCatalogFiles.staticMapDataFileName;
	}
	if (staticMapDataTemplateFileName.empty()) {
		staticMapDataTemplateFileName = getStaticMapDataFilename(map);
	}

	pb_staticmapdata::StaticMapData templateStaticMapData;
	std::filesystem::path loadedStaticMapDataTemplatePath;
	bool useStaticMapDataTemplate = false;
	if (useStaticDataTemplate) {
		std::vector<std::filesystem::path> staticMapDataTemplateCandidates;
		staticMapDataTemplateCandidates.reserve(2);
		staticMapDataTemplateCandidates.emplace_back(basePath / staticMapDataTemplateFileName);
		if (!sourceCatalogFiles.staticMapDataFileName.empty() && !clientAssetsPath.empty()) {
			staticMapDataTemplateCandidates.emplace_back(clientAssetsPath / sourceCatalogFiles.staticMapDataFileName);
		}
		const bool hasTemplateStaticMapData = loadFirstTemplateCandidate(
			staticMapDataTemplateCandidates,
			loadStaticMapDataTemplate,
			templateStaticMapData,
			&loadedStaticMapDataTemplatePath
		);
		if (!hasTemplateStaticMapData) {
			const auto siblingTemplatePath = findValidTemplateAssetSibling(basePath, staticMapDataTemplateFileName);
			if (!siblingTemplatePath.empty()) {
				if (loadStaticMapDataTemplate(siblingTemplatePath, templateStaticMapData)) {
					loadedStaticMapDataTemplatePath = siblingTemplatePath;
					spdlog::info("[house-debug] using sibling staticmapdata template '{}'", siblingTemplatePath.string());
				}
			}
		}
		if (!loadedStaticMapDataTemplatePath.empty()) {
			useStaticMapDataTemplate = true;
		} else {
			registerExportError("No valid staticmapdata template file found; exporting dynamic staticmapdata from current map.");
		}
	}
	spdlog::info(
		"[house-debug] staticmap template stage: template='{}' template_used={} template_houses={} filename='{}'",
		loadedStaticMapDataTemplatePath.string(),
		useStaticMapDataTemplate,
		templateStaticMapData.house_size(),
		staticMapDataTemplateFileName.string()
	);

	std::unordered_map<uint32_t, StaticMapHouseTemplate> staticMapHouseTemplates;
	if (useStaticMapDataTemplate) {
		buildStaticMapHouseTemplates(templateStaticMapData, houseIdRemap, housePositionDeltaRemap, staticMapHouseTemplates);
	}

	struct StaticMapTemplateScopeGuard final {
		StaticMapTemplateScopeGuard() = default;
		StaticMapTemplateScopeGuard(const StaticMapTemplateScopeGuard &) = delete;
		StaticMapTemplateScopeGuard &operator=(const StaticMapTemplateScopeGuard &) = delete;
		~StaticMapTemplateScopeGuard() {
			activeStaticMapHouseTemplates() = nullptr;
		}
	};
	StaticMapTemplateScopeGuard staticMapTemplateScopeGuard;
	activeStaticMapHouseTemplates() = staticMapHouseTemplates.empty() ? nullptr : &staticMapHouseTemplates;
	resetStaticMapHouseExportDebugStats();
	spdlog::info(
		"[house-debug] staticmap export stage: active_templates={} total_templates={}",
		activeStaticMapHouseTemplates() != nullptr,
		staticMapHouseTemplates.size()
	);

	std::string staticMapDataBuffer;
	size_t attemptedStaticMapHouses = 0;
	size_t exportedStaticMapHouses = 0;
	std::vector<std::string> failedStaticMapHouses;
	if (!serializeStaticMapDataHouses(map, staticMapDataBuffer, &attemptedStaticMapHouses, &exportedStaticMapHouses, &failedStaticMapHouses)) {
		registerExportError("Failed to serialize staticmapdata protobuf.");
		return false;
	}
	lastStaticHouseExportReport.staticMapAttemptedHouses = attemptedStaticMapHouses;
	lastStaticHouseExportReport.staticMapGeneratedHouses = exportedStaticMapHouses;
	lastStaticHouseExportReport.failedStaticMapHouses = std::move(failedStaticMapHouses);
	if (!houseIdRemap.empty()) {
		std::string remappedStaticMapDataBuffer;
		if (remapStaticMapDataHouseIds(houseIdRemap, staticMapDataBuffer, remappedStaticMapDataBuffer)) {
			staticMapDataBuffer = std::move(remappedStaticMapDataBuffer);
		} else {
			registerExportError("Failed to remap staticmapdata house ids to template house ids.");
		}
	}
	if (hasHouseNameFilter && useStaticMapDataTemplate) {
		std::string mergedStaticMapDataBuffer;
		if (mergeStaticMapDataTemplate(templateStaticMapData, staticMapDataBuffer, mergedStaticMapDataBuffer)) {
			staticMapDataBuffer = std::move(mergedStaticMapDataBuffer);
		} else {
			registerExportError("Failed to merge staticmapdata house filter with template data.");
			return false;
		}
	}
	if (!staticDataHouseOrder.empty()) {
		std::string orderedStaticMapDataBuffer;
		if (reorderStaticMapDataHouseEntries(staticMapDataBuffer, staticDataHouseOrder, orderedStaticMapDataBuffer)) {
			staticMapDataBuffer = std::move(orderedStaticMapDataBuffer);
		} else {
			registerExportError("Failed to sort staticmapdata houses to staticdata order.");
		}
	}

	{
		pb_staticmapdata::StaticMapData finalStaticMapData;
		if (finalStaticMapData.ParseFromString(staticMapDataBuffer)) {
			lastStaticHouseExportReport.staticMapFinalHouses = static_cast<size_t>(finalStaticMapData.house_size());
		}
	}

	std::string staticMapDataFileName = buildCatalogDataFilename("staticmapdata", staticMapDataBuffer);
	lastStaticHouseExportReport.staticMapDataFileName = staticMapDataFileName;

	const std::filesystem::path staticMapDataPath = basePath / staticMapDataFileName;
	if (!staticMapDataPath.parent_path().empty()) {
		std::filesystem::create_directories(staticMapDataPath.parent_path(), ec);
	}
	if (!backupCatalogFileIfReplaced(basePath, backupRootPath, outputCatalogFiles.staticMapDataFileName, staticMapDataFileName)) {
		registerExportError("Failed to backup previous staticmapdata file.");
		return false;
	}
	if (!backupStaleCatalogDataFiles(basePath, backupRootPath, "staticmapdata", staticMapDataFileName)) {
		registerExportError("Failed to backup stale staticmapdata files.");
		return false;
	}
	if (!backupFileIfExists(staticMapDataPath, basePath, backupRootPath)) {
		registerExportError("Failed to create backup for staticmapdata file.");
		return false;
	}

	if (!writeBinaryFile(staticMapDataPath, staticMapDataBuffer)) {
		registerExportError("Failed to save staticmapdata protobuf file.");
		return false;
	}
	logStaticMapHouseExportDebugSummary();

	CyclopediaCatalogFiles updatedCatalogFiles = outputCatalogFiles;
	updatedCatalogFiles.staticDataFileName = staticDataFileName;
	updatedCatalogFiles.staticMapDataFileName = staticMapDataFileName;
	if (!updateCyclopediaCatalogFiles(basePath, updatedCatalogFiles, backupRootPath)) {
		registerExportError("Failed to update catalog-content.json for static house export.");
		return false;
	}

	lastStaticHouseExportReport.success = true;
	return true;
}

std::string IOMapOTBM::getCyclopediaMapDataFilename([[maybe_unused]] const Map &map) const {
	return "map.dat";
}

bool IOMapOTBM::serializeCyclopediaMapData(Map &map, std::string &buffer, std::vector<std::pair<std::string, std::vector<uint8_t>>> &assets, const CyclopediaExportProgressFn &progress, int satellitePixelsPerSquare) {
	using namespace clienteditor::protobuf::mapdata;
	(void)satellitePixelsPerSquare;

	assets.clear();
	buffer.clear();
	MapData mapData;
	auto reportProgress = [&](const int32_t done, const std::string &message = std::string()) {
		if (!progress) {
			return true;
		}
		return progress(std::max<int32_t>(0, std::min<int32_t>(100, done)), message);
	};
	if (!reportProgress(0, "Scanning map floors... [|]")) {
		return false;
	}

	int minX = 0;
	int minY = 0;
	int maxX = 0;
	int maxY = 0;
	if (!getCyclopediaWorldBounds(map, minX, minY, maxX, maxY)) {
		return false;
	}

	auto* topLeftEdge = mapData.mutable_topleftedge();
	topLeftEdge->set_posx(static_cast<uint32_t>(std::max(minX, 0)));
	topLeftEdge->set_posy(static_cast<uint32_t>(std::max(minY, 0)));
	topLeftEdge->set_posz(CyclopediaMinFloor);

	auto* bottomRightEdge = mapData.mutable_bottomrightedge();
	bottomRightEdge->set_posx(static_cast<uint32_t>(std::max(maxX, 0)));
	bottomRightEdge->set_posy(static_cast<uint32_t>(std::max(maxY, 0)));
	bottomRightEdge->set_posz(CyclopediaMaxFloor);

	struct CyclopediaRenderJob {
		CyclopediaAssetLayerConfig config;
		CyclopediaChunkArea area;
	};

	std::vector<CyclopediaRenderJob> jobs;
	jobs.reserve(4096);

	for (int floor = CyclopediaMinFloor; floor <= CyclopediaMaxFloor; ++floor) {
		int floorMinX = 0;
		int floorMinY = 0;
		int floorMaxX = 0;
		int floorMaxY = 0;
		if (!getCyclopediaFloorBounds(map, floor, floorMinX, floorMinY, floorMaxX, floorMaxY)) {
			continue;
		}

		const auto addFloorLayerJobs = [&](const CyclopediaAssetLayerConfig &config) {
			std::vector<std::pair<int, int>> chunkStarts;
			if (!collectCyclopediaFloorChunks(map, floor, floorMinX, floorMinY, config.chunkSize, chunkStarts)) {
				return;
			}

			for (const auto &[chunkStartX, chunkStartY] : chunkStarts) {
				const int chunkWidth = std::min(config.chunkSize, floorMaxX - chunkStartX + 1);
				const int chunkHeight = std::min(config.chunkSize, floorMaxY - chunkStartY + 1);
				if (chunkWidth <= 0 || chunkHeight <= 0) {
					continue;
				}

				jobs.emplace_back(
					config,
					CyclopediaChunkArea { floor, chunkStartX, chunkStartY, chunkWidth, chunkHeight }
				);
			}
		};

		for (const auto &config : CyclopediaMinimapLayers) {
			addFloorLayerJobs(config);
		}
		for (const auto &config : CyclopediaSatelliteLayers) {
			addFloorLayerJobs(config);
		}
	}

	if (jobs.empty()) {
		return false;
	}

	int processedChunks = 0;
	const auto totalChunks = static_cast<int>(jobs.size());

	bool hasAnyAsset = false;

	for (const auto &job : jobs) {
		++processedChunks;
		const auto done = static_cast<int32_t>((processedChunks * 100LL) / totalChunks);
		const char spinner = CyclopediaProgressSpinner[processedChunks % CyclopediaProgressSpinner.size()];
		const std::string chunkMessage = std::format("Rendering chunks... ({}/{}) [{}]", processedChunks, totalChunks, spinner);
		if (!reportProgress(done, chunkMessage)) {
			return false;
		}

		wxImage minimapChunk;
		bool chunkHasData = false;
		if (!buildCyclopediaMinimapChunk(map, job.area, minimapChunk, chunkHasData) || !chunkHasData) {
			continue;
		}

		wxImage outputAssetChunk;
		if (job.config.minimap) {
			outputAssetChunk = minimapChunk.Copy();
			if (!resampleCyclopediaChunk(outputAssetChunk, job.area.width, job.area.height, job.config.pixelsPerSquare)) {
				continue;
			}
		} else {
			wxImage satelliteChunk;
			if (!buildCyclopediaSatelliteChunk(map, job.area, job.config.pixelsPerSquare, minimapChunk, satelliteChunk)) {
				satelliteChunk = minimapChunk.Copy();
				if (!resampleCyclopediaChunk(satelliteChunk, job.area.width, job.area.height, job.config.pixelsPerSquare)) {
					continue;
				}
			}
			outputAssetChunk = satelliteChunk.Copy();
		}

		if (!outputAssetChunk.IsOk() || outputAssetChunk.GetWidth() <= 0 || outputAssetChunk.GetHeight() <= 0) {
			continue;
		}

		std::vector<uint8_t> assetBytes;
		std::string assetHash;
		if (!reportProgress(done, std::format("Encoding assets... ({}/{}) [{}]", processedChunks, totalChunks, spinner))) {
			return false;
		}
		if (!encodeCyclopediaAsset(outputAssetChunk, assetBytes, assetHash)) {
			continue;
		}

		const std::string assetFilename = buildCyclopediaAssetFilename(job.config.minimap, job.config.scale, job.area.startX, job.area.startY, job.area.floor, assetHash);
		auto* mapAsset = mapData.add_mapassets();
		mapAsset->set_type(job.config.minimap ? MapAssets_AssetsType_MINIMAP : MapAssets_AssetsType_SATELLITE);
		mapAsset->set_filename(assetFilename);
		mapAsset->set_widthsquare(static_cast<uint32_t>(job.area.width));
		mapAsset->set_heightsquare(static_cast<uint32_t>(job.area.height));
		mapAsset->set_areaid(0);
		mapAsset->set_scale(job.config.scale);
		auto* topLeft = mapAsset->mutable_topleft();
		topLeft->set_posx(static_cast<uint32_t>(std::max(job.area.startX, 0)));
		topLeft->set_posy(static_cast<uint32_t>(std::max(job.area.startY, 0)));
		topLeft->set_posz(static_cast<uint32_t>(std::max(job.area.floor, 0)));

		assets.emplace_back(assetFilename, std::move(assetBytes));
		hasAnyAsset = true;
	}

	if (!hasAnyAsset) {
		return false;
	}

	if (!reportProgress(100, "Rendering chunks... done [|]")) {
		return false;
	}
	return mapData.SerializeToString(&buffer);
}

bool IOMapOTBM::saveCyclopediaMapData(Map &map, const FileName &dir, const CyclopediaExportProgressFn &progress, int satellitePixelsPerSquare) {
	using namespace clienteditor::protobuf::mapdata;

	auto reportProgress = [&](const int32_t done, const std::string &message = std::string()) {
		if (!progress) {
			return true;
		}
		return progress(std::max<int32_t>(0, std::min<int32_t>(99, done)), message);
	};

	if (!reportProgress(0, "Preparing cyclopedia export... [|]")) {
		return false;
	}

	const std::filesystem::path requestedOutputPath = nstr(dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME));
	const std::filesystem::path basePath = resolveCyclopediaCatalogBasePath(requestedOutputPath);
	const std::filesystem::path backupRootBasePath = basePath / "bkps";
	std::error_code ec;
	std::filesystem::create_directories(basePath, ec);
	const std::filesystem::path backupRootPath = createCyclopediaBackupRunPath(backupRootBasePath, "export");
	if (backupRootPath.empty()) {
		warning("Failed to create cyclopedia backup snapshot directory.");
		return false;
	}

	CyclopediaCatalogFiles outputCatalogFiles;
	loadCyclopediaCatalogFiles(basePath, outputCatalogFiles);

	const wxString clientAssetsRoot = ClientAssets::getPath();
	const std::filesystem::path clientAssetsPath = clientAssetsRoot.empty() ? std::filesystem::path() : resolveCyclopediaCatalogBasePath(std::filesystem::path(nstr(clientAssetsRoot)));
	CyclopediaCatalogFiles sourceCatalogFiles;
	if (!clientAssetsPath.empty()) {
		loadCyclopediaCatalogFiles(clientAssetsPath, sourceCatalogFiles);
	}

	std::string mapDataTemplateFileName = !outputCatalogFiles.mapFileName.empty() ? outputCatalogFiles.mapFileName : getCyclopediaMapDataFilename(map);
	if (mapDataTemplateFileName == getCyclopediaMapDataFilename(map) && !sourceCatalogFiles.mapFileName.empty()) {
		mapDataTemplateFileName = sourceCatalogFiles.mapFileName;
	}

	std::string mapDataBuffer;
	std::vector<std::pair<std::string, std::vector<uint8_t>>> assets;
	if (!serializeCyclopediaMapData(
			map, mapDataBuffer, assets, [&reportProgress](const int32_t done, const std::string &message) {
				return reportProgress(std::min<int32_t>(95, done), message);
			},
			satellitePixelsPerSquare
		)) {
		return false;
	}

	MapData templateMapData;
	bool hasTemplateMapData = false;
	std::filesystem::path templateMapDataBasePath;
	if (!mapDataTemplateFileName.empty()) {
		hasTemplateMapData = loadCyclopediaMapDataTemplate(basePath / mapDataTemplateFileName, templateMapData);
		if (hasTemplateMapData) {
			templateMapDataBasePath = basePath;
		}
	}
	if (!hasTemplateMapData && !sourceCatalogFiles.mapFileName.empty() && !clientAssetsPath.empty()) {
		hasTemplateMapData = loadCyclopediaMapDataTemplate(clientAssetsPath / sourceCatalogFiles.mapFileName, templateMapData);
		if (hasTemplateMapData) {
			templateMapDataBasePath = clientAssetsPath;
		}
	}
	if (hasTemplateMapData) {
		std::string mergedMapDataBuffer;
		if (mergeCyclopediaTemplateMapData(templateMapData, mapDataBuffer, mergedMapDataBuffer)) {
			mapDataBuffer = std::move(mergedMapDataBuffer);
			if (!restoreCyclopediaMapAssets(basePath, backupRootPath, templateMapDataBasePath, templateMapData, true)) {
				warning("Failed to restore preserved cyclopedia subarea asset files.");
				return false;
			}
		} else {
			warning("Failed to merge template mapdata protobuf content.");
		}
	}

	const std::string mapDataFileName = buildCatalogDataFilename("map", mapDataBuffer);
	std::unordered_set<std::string> assetFileNames;
	std::unordered_set<std::string> assetReplacementKeys;
	collectCyclopediaAssetBackupKeys(assets, assetFileNames, assetReplacementKeys);
	if (!reportProgress(96, "Writing map protobuf... [|]")) {
		return false;
	}
	const std::filesystem::path mapDataPath = basePath / mapDataFileName;
	if (!mapDataPath.parent_path().empty()) {
		std::filesystem::create_directories(mapDataPath.parent_path(), ec);
	}
	if (!backupMapAssetsFromPreviousMapData(basePath, backupRootPath, outputCatalogFiles.mapFileName, mapDataFileName, assetFileNames)) {
		warning("Failed to backup previous cyclopedia map asset files.");
		return false;
	}
	if (!backupCatalogFileIfReplaced(basePath, backupRootPath, outputCatalogFiles.mapFileName, mapDataFileName)) {
		warning("Failed to backup previous cyclopedia mapdata protobuf file.");
		return false;
	}
	if (!backupStaleCatalogDataFiles(basePath, backupRootPath, "map", mapDataFileName)) {
		warning("Failed to backup stale cyclopedia mapdata protobuf files.");
		return false;
	}
	if (!backupStaleCyclopediaAssetFiles(basePath, backupRootPath, assetFileNames, assetReplacementKeys)) {
		warning("Failed to backup stale cyclopedia asset files.");
		return false;
	}
	if (!backupFileIfExists(mapDataPath, basePath, backupRootPath)) {
		warning("Failed to create backup for cyclopedia mapdata protobuf file.");
		return false;
	}
	if (!writeBinaryFile(mapDataPath, mapDataBuffer)) {
		warning("Failed to save cyclopedia mapdata protobuf file.");
		return false;
	}

	const int totalAssets = std::max<int>(1, static_cast<int>(assets.size()));
	int writtenAssets = 0;
	for (const auto &[relativePath, fileBytes] : assets) {
		const std::filesystem::path targetPath = basePath / relativePath;
		const std::filesystem::path targetDir = targetPath.parent_path();
		if (!targetDir.empty()) {
			std::filesystem::create_directories(targetDir, ec);
		}
		if (!backupFileIfExists(targetPath, basePath, backupRootPath)) {
			warning("Failed to create backup for cyclopedia asset file.");
			return false;
		}

		if (!writeBinaryFile(targetPath, fileBytes)) {
			warning("Failed to save cyclopedia asset file.");
			return false;
		}

		++writtenAssets;
		const int32_t done = 96 + static_cast<int32_t>((writtenAssets * 3LL) / totalAssets);
		const char spinner = CyclopediaProgressSpinner[writtenAssets % CyclopediaProgressSpinner.size()];
		if (!reportProgress(done, std::format("Writing assets... ({}/{}) [{}]", writtenAssets, totalAssets, spinner))) {
			return false;
		}
	}

	MapData writtenMapData;
	if (!writtenMapData.ParseFromString(mapDataBuffer)) {
		warning("Failed to validate written cyclopedia mapdata protobuf content.");
		return false;
	}
	if (!restoreCyclopediaMapAssets(basePath, backupRootPath, templateMapDataBasePath, writtenMapData, false)) {
		warning("Failed to restore referenced cyclopedia map asset files.");
		return false;
	}

	std::string staticMapDataFileName = outputCatalogFiles.staticMapDataFileName;
	if (staticMapDataFileName.empty()) {
		staticMapDataFileName = sourceCatalogFiles.staticMapDataFileName;
		if (!staticMapDataFileName.empty() && !clientAssetsPath.empty()) {
			const std::filesystem::path sourceStaticMapDataPath = clientAssetsPath / sourceCatalogFiles.staticMapDataFileName;
			const std::filesystem::path targetStaticMapDataPath = basePath / staticMapDataFileName;
			if (std::filesystem::exists(sourceStaticMapDataPath)) {
				bool samePath = false;
				std::error_code compareEc;
				samePath = std::filesystem::equivalent(sourceStaticMapDataPath, targetStaticMapDataPath, compareEc);
				if (!samePath) {
					if (!targetStaticMapDataPath.parent_path().empty()) {
						std::filesystem::create_directories(targetStaticMapDataPath.parent_path(), ec);
					}
					if (!backupFileIfExists(targetStaticMapDataPath, basePath, backupRootPath)) {
						warning("Failed to create backup for staticmapdata file.");
						return false;
					}
					ec.clear();
					std::filesystem::copy_file(sourceStaticMapDataPath, targetStaticMapDataPath, std::filesystem::copy_options::overwrite_existing, ec);
					if (ec) {
						warning("Failed to copy staticmapdata file.");
						return false;
					}
				}
			}
		}
	}

	CyclopediaCatalogFiles updatedCatalogFiles = outputCatalogFiles;
	updatedCatalogFiles.mapFileName = mapDataFileName;
	if (!staticMapDataFileName.empty()) {
		updatedCatalogFiles.staticMapDataFileName = staticMapDataFileName;
	}
	if (!updateCyclopediaCatalogFiles(basePath, updatedCatalogFiles, backupRootPath)) {
		warning("Failed to update catalog-content.json for cyclopedia map export.");
		return false;
	}

	if (!reportProgress(99, "Cyclopedia export completed. [|]")) {
		return false;
	}
	return true;
}

bool IOMapOTBM::saveZones(Map &map, const FileName &dir) {
	wxString filepath = dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME);
	filepath += wxString(map.zonefile.c_str(), wxConvUTF8);

	// Create the XML file
	pugi::xml_document doc;
	if (saveZones(map, doc)) {
		return saveXmlFileIfChanged(doc, filepath);
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
		return saveXmlFileIfChanged(doc, filepath);
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
