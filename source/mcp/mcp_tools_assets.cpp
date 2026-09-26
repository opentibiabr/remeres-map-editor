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
//
// Read-only access to the loaded client's assets: appearances, sprites, and
// the Cyclopedia data files the editor exports.
//
// Nothing here writes. docs/static-data.md describes a CipSoft-compatible
// contract for staticdata / staticmapdata / mapdata and their hash-named
// filenames; exporting stays exclusively with IOMapOTBM and the editor menu,
// and these tools only parse what is already on disk.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "mcp_tools.h"

#include "../client_assets.h"
#include "../common.h"
#include "../graphics.h"
#include "../gui.h"
#include "../items.h"
#include "../sprite_appearances.h"

#include <appearances.pb.h>
#include <mapdata.pb.h>
#include <staticdata.pb.h>
#include <staticmapdata.pb.h>

#include <google/protobuf/util/json_util.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace mcp {

	namespace {

		namespace pb_appearances = canary::protobuf::appearances;
		namespace pb_staticdata = clienteditor::protobuf::staticdata;
		namespace pb_staticmapdata = clienteditor::protobuf::staticmapdata;
		namespace pb_mapdata = clienteditor::protobuf::mapdata;

		// AppearanceFlags alone has ~48 fields and grows with every client
		// version. Hand-mapping it would rot; protobuf's own JSON printer keeps
		// the output exactly in step with the .proto.
		json protoToJson(const google::protobuf::Message &message) {
			google::protobuf::util::JsonPrintOptions options;
			options.add_whitespace = false;
			options.preserve_proto_field_names = true;

			std::string out;
			const auto status = google::protobuf::util::MessageToJsonString(message, &out, options);
			if (!status.ok()) {
				throw McpError(std::string("could not convert the protobuf message to JSON: ") + std::string(status.message()));
			}

			return json::parse(out);
		}

		pb_appearances::Appearances &requireAppearances() {
			if (!g_gui.m_appearancesPtr) {
				throw McpError("no client assets are loaded; open a client version in the editor first");
			}
			return *g_gui.m_appearancesPtr;
		}

		// The four parallel appearance lists in the protobuf.
		const google::protobuf::RepeatedPtrField<pb_appearances::Appearance> &appearanceList(
			pb_appearances::Appearances &appearances, const std::string &type
		) {
			if (type == "object") {
				return appearances.object();
			}
			if (type == "outfit") {
				return appearances.outfit();
			}
			if (type == "effect") {
				return appearances.effect();
			}
			if (type == "missile") {
				return appearances.missile();
			}
			throw McpError("type must be one of: object, outfit, effect, missile");
		}

		std::filesystem::path assetsDirectory() {
			const wxString path = ClientAssets::getPath();
			if (path.IsEmpty()) {
				throw McpError("no client assets directory is configured");
			}
			return std::filesystem::path(path.ToStdString());
		}

		// ------------------------------------------------------------------
		// client_info
		// ------------------------------------------------------------------

		json readCatalog(const std::filesystem::path &directory) {
			const std::filesystem::path catalogPath = directory / "catalog-content.json";
			std::ifstream file(catalogPath, std::ios::binary);
			if (!file.is_open()) {
				return json { { "found", false }, { "path", catalogPath.string() } };
			}

			json catalog;
			try {
				file >> catalog;
			} catch (const std::exception &e) {
				return json { { "found", true }, { "path", catalogPath.string() }, { "parseError", e.what() } };
			}

			// The catalog is mostly sprite sheet entries; summarise by type and
			// list only the data files, which is what matters here.
			json byType = json::object();
			json dataFiles = json::array();
			if (catalog.is_array()) {
				for (const json &entry : catalog) {
					if (!entry.is_object()) {
						continue;
					}
					const std::string type = entry.value("type", std::string("unknown"));
					byType[type] = byType.value(type, 0) + 1;
					if (type == "staticdata" || type == "staticmapdata" || type == "map" || type == "appearances") {
						dataFiles.push_back(entry);
					}
				}
			}

			return json {
				{ "found", true },
				{ "path", catalogPath.string() },
				{ "entryCount", catalog.is_array() ? catalog.size() : 0 },
				{ "entriesByType", std::move(byType) },
				{ "dataFiles", std::move(dataFiles) }
			};
		}

		json toolClientInfo(const json &) {
			const bool loaded = ClientAssets::isLoaded();

			json out {
				{ "loaded", loaded },
				{ "versionName", ClientAssets::getVersionName() },
				{ "assetsPath", ClientAssets::getPath().ToStdString() },
				{ "spriteCount", g_spriteAppearances.getSpritesCount() },
				{ "spriteSheets", g_spriteAppearances.getSheets().size() },
				{ "appearanceFile", g_spriteAppearances.getAppearanceFileName() },
				{ "itemIdRange", json { { "min", g_items.getMinID() }, { "max", g_items.getMaxID() } } }
			};

			if (g_gui.m_appearancesPtr) {
				const auto &appearances = *g_gui.m_appearancesPtr;
				out["appearanceCounts"] = json {
					{ "object", appearances.object_size() },
					{ "outfit", appearances.outfit_size() },
					{ "effect", appearances.effect_size() },
					{ "missile", appearances.missile_size() }
				};
			} else {
				out["appearanceCounts"] = nullptr;
			}

			try {
				out["catalog"] = readCatalog(assetsDirectory());
			} catch (const McpError &) {
				out["catalog"] = nullptr;
			}

			return jsonResult(out);
		}

		// ------------------------------------------------------------------
		// appearance_get / appearance_search
		// ------------------------------------------------------------------

		json toolAppearanceGet(const json &params) {
			pb_appearances::Appearances &appearances = requireAppearances();
			const std::string type = readString(params, "type", "object");
			const auto id = static_cast<uint32_t>(readInt(params, "id", 0, 0, 0x7FFFFFFF));
			if (id == 0) {
				throw McpError("id is required");
			}

			const auto &list = appearanceList(appearances, type);
			for (const pb_appearances::Appearance &appearance : list) {
				if (appearance.id() == id) {
					return jsonResult(json {
						{ "type", type },
						{ "id", id },
						{ "appearance", protoToJson(appearance) } });
				}
			}

			throw McpError(fmt::format("no {} appearance with id {}", type, id));
		}

		json toolAppearanceSearch(const json &params) {
			pb_appearances::Appearances &appearances = requireAppearances();
			const std::string type = readString(params, "type", "object");
			const std::string needle = as_lower_str(readString(params, "name"));
			const std::string flag = readString(params, "hasFlag");
			const int limit = readInt(params, "limit", 100, 1, 2000);

			if (needle.empty() && flag.empty()) {
				throw McpError("give a name substring, a hasFlag name, or both");
			}

			const auto &list = appearanceList(appearances, type);
			json matches = json::array();
			int64_t total = 0;

			for (const pb_appearances::Appearance &appearance : list) {
				if (!needle.empty()) {
					const std::string name = as_lower_str(appearance.name());
					const std::string description = as_lower_str(appearance.description());
					if (name.find(needle) == std::string::npos && description.find(needle) == std::string::npos) {
						continue;
					}
				}

				if (!flag.empty()) {
					if (!appearance.has_flags()) {
						continue;
					}
					// Ask the reflection layer whether this named flag is set,
					// so any flag in the .proto works without a lookup table.
					const auto* reflection = appearance.flags().GetReflection();
					const auto* descriptor = appearance.flags().GetDescriptor()->FindFieldByName(flag);
					if (!descriptor) {
						throw McpError(fmt::format("no appearance flag named '{}'", flag));
					}
					if (descriptor->is_repeated()) {
						if (reflection->FieldSize(appearance.flags(), descriptor) == 0) {
							continue;
						}
					} else if (!reflection->HasField(appearance.flags(), descriptor)) {
						continue;
					} else if (descriptor->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_BOOL && !reflection->GetBool(appearance.flags(), descriptor)) {
						continue;
					}
				}

				++total;
				if (static_cast<int>(matches.size()) >= limit) {
					continue;
				}
				matches.push_back(json {
					{ "id", appearance.id() },
					{ "name", appearance.name() },
					{ "description", appearance.description() } });
			}

			return jsonResult(json {
				{ "type", type },
				{ "total", total },
				{ "returned", matches.size() },
				{ "matches", std::move(matches) } });
		}

		// ------------------------------------------------------------------
		// sprite_render / item_sprite_info
		// ------------------------------------------------------------------

		// docs/static-data.md rule 9: GameSprite::getSpriteID is the asset
		// identity; getHardwareID is a GL texture/atlas id and must never be
		// handed out as a sprite id.
		uint32_t firstSpriteIdOf(const ItemType &type) {
			if (!type.sprite) {
				return 0;
			}
			return type.sprite->getSpriteID(0, 0, 0, 0, 0, 0);
		}

		json toolSpriteRender(const json &params) {
			int spriteId = readInt(params, "spriteId", 0, 0, 0x7FFFFFFF);

			if (spriteId == 0) {
				const int itemId = readInt(params, "itemId", 0, 0, 0xFFFF);
				if (itemId == 0) {
					throw McpError("give a spriteId, or an itemId to resolve one from");
				}
				const ItemType &type = g_items[static_cast<uint16_t>(itemId)];
				if (type.id == 0) {
					throw McpError(fmt::format("item id {} does not exist in the loaded client", itemId));
				}
				spriteId = static_cast<int>(firstSpriteIdOf(type));
				if (spriteId == 0) {
					throw McpError(fmt::format("item {} has no sprite loaded", itemId));
				}
			}

			wxImage image = g_spriteAppearances.getWxImageBySpriteId(spriteId, true);
			if (!image.IsOk()) {
				throw McpError(fmt::format("sprite {} could not be loaded from the client assets", spriteId));
			}

			const int scale = readInt(params, "scale", 1, 1, 8);
			if (scale > 1) {
				image = image.Scale(image.GetWidth() * scale, image.GetHeight() * scale, wxIMAGE_QUALITY_NEAREST);
			}

			return imageResult(encodePngBase64(image), "image/png");
		}

		json toolItemSpriteInfo(const json &params) {
			const int itemId = readInt(params, "itemId", 0, 1, 0xFFFF);
			if (itemId == 0) {
				throw McpError("itemId is required");
			}

			const ItemType &type = g_items[static_cast<uint16_t>(itemId)];
			if (type.id == 0) {
				throw McpError(fmt::format("item id {} does not exist in the loaded client", itemId));
			}

			json out {
				{ "itemId", type.id },
				{ "clientId", type.clientID },
				{ "name", type.name }
			};

			GameSprite* sprite = type.sprite;
			if (!sprite) {
				out["sprite"] = nullptr;
				out["note"] = "this item type has no sprite loaded";
				return jsonResult(out);
			}

			out["sprite"] = json {
				{ "width", sprite->width },
				{ "height", sprite->height },
				{ "layers", sprite->layers },
				{ "patternX", sprite->pattern_x },
				{ "patternY", sprite->pattern_y },
				{ "patternZ", sprite->pattern_z },
				{ "animationPhases", sprite->sprite_phase_size },
				{ "spriteCount", sprite->numsprites },
				{ "drawHeight", sprite->draw_height },
				{ "drawOffset", json { { "x", sprite->draw_offset.x }, { "y", sprite->draw_offset.y } } },
				{ "groundSpeed", sprite->ground_speed },
				{ "minimapColor", sprite->minimap_color },
				{ "hasLight", sprite->has_light }
			};

			// Enumerate the real sprite ids, capped so a heavily animated
			// outfit cannot flood the response.
			const int limit = readInt(params, "limit", 64, 1, 512);
			json spriteIds = json::array();
			int64_t total = 0;

			for (int layer = 0; layer < sprite->layers; ++layer) {
				for (int px = 0; px < sprite->pattern_x; ++px) {
					for (int py = 0; py < sprite->pattern_y; ++py) {
						for (int pz = 0; pz < sprite->pattern_z; ++pz) {
							for (int frame = 0; frame < sprite->sprite_phase_size; ++frame) {
								++total;
								if (static_cast<int>(spriteIds.size()) >= limit) {
									continue;
								}
								spriteIds.push_back(json {
									{ "layer", layer },
									{ "patternX", px },
									{ "patternY", py },
									{ "patternZ", pz },
									{ "frame", frame },
									{ "spriteId", sprite->getSpriteID(layer, 0, px, py, pz, frame) } });
							}
						}
					}
				}
			}

			out["spriteIdCount"] = total;
			out["spriteIds"] = std::move(spriteIds);
			out["note"] = "spriteId values are client asset sprite ids; feed one to sprite_render";
			return jsonResult(out);
		}

		// ------------------------------------------------------------------
		// clientdata_read
		// ------------------------------------------------------------------

		// The exporter writes hash-named files; the plain names are the default
		// template names. Prefer an explicit filename, else find the newest
		// match for the kind.
		std::filesystem::path resolveDataFile(const std::filesystem::path &directory, const std::string &kind, const std::string &filename) {
			if (!filename.empty()) {
				const std::filesystem::path explicitPath = directory / filename;
				if (!std::filesystem::exists(explicitPath)) {
					throw McpError(fmt::format("{} does not exist in the assets directory", filename));
				}
				return explicitPath;
			}

			const std::string prefix = kind == "mapdata" ? "map" : kind;

			std::filesystem::path newest;
			std::filesystem::file_time_type newestTime {};
			std::error_code error;

			for (const auto &entry : std::filesystem::directory_iterator(directory, error)) {
				if (error || !entry.is_regular_file()) {
					continue;
				}
				const std::string name = entry.path().filename().string();
				if (name.rfind(prefix, 0) != 0 || entry.path().extension() != ".dat") {
					continue;
				}
				// "map-..." must not match "mapdata..." and vice versa.
				const char after = name.size() > prefix.size() ? name[prefix.size()] : '\0';
				if (after != '-' && after != '.') {
					continue;
				}
				const auto written = entry.last_write_time(error);
				if (error) {
					continue;
				}
				if (newest.empty() || written > newestTime) {
					newest = entry.path();
					newestTime = written;
				}
			}

			if (newest.empty()) {
				throw McpError(fmt::format(
					"no {} file found in {}. Export it from File > Export first, or pass an explicit filename.",
					kind, directory.string()
				));
			}
			return newest;
		}

		template <typename Message>
		Message parseDataFile(const std::filesystem::path &path) {
			std::ifstream file(path, std::ios::binary);
			if (!file.is_open()) {
				throw McpError(fmt::format("could not open {}", path.string()));
			}

			Message message;
			if (!message.ParseFromIstream(&file)) {
				throw McpError(fmt::format("{} is not a valid {} protobuf file", path.filename().string(), Message::descriptor()->name()));
			}
			return message;
		}

		// Section 4 of docs/static-data.md: tiles are serialized linearly over a
		// width*height*floors volume, advancing skip+1 per entry.
		json decodeHousePreview(const pb_staticmapdata::HouseEntry &entry) {
			if (!entry.has_data()) {
				return json { { "tiles", json::array() }, { "note", "entry has no preview data" } };
			}

			const auto &data = entry.data();
			const int width = data.has_dimensions() ? static_cast<int>(data.dimensions().pos_x()) : 0;
			const int height = data.has_dimensions() ? static_cast<int>(data.dimensions().pos_y()) : 0;
			const int floors = data.has_dimensions() ? static_cast<int>(data.dimensions().pos_z()) : 0;

			const int originX = data.has_origin() ? static_cast<int>(data.origin().pos_x()) : 0;
			const int originY = data.has_origin() ? static_cast<int>(data.origin().pos_y()) : 0;
			const int originZ = data.has_origin() ? static_cast<int>(data.origin().pos_z()) : 0;

			json tiles = json::array();
			if (width <= 0 || height <= 0) {
				return json { { "tiles", std::move(tiles) }, { "note", "preview has no usable dimensions" } };
			}

			const int floorArea = width * height;
			int64_t linearIndex = 0;

			for (const auto &tile : data.preview().layer().tile()) {
				const int64_t floor = linearIndex / floorArea;
				const int64_t planeIndex = linearIndex % floorArea;
				const int64_t x = planeIndex / height;
				const int64_t y = planeIndex % height;

				json items = json::array();
				for (const auto &item : tile.item()) {
					items.push_back(item.value());
				}

				tiles.push_back(json {
					{ "position", json { { "x", originX + x }, { "y", originY + y }, { "z", originZ + floor } } },
					{ "clientItemIds", std::move(items) },
					{ "isHouseTile", tile.is_house_tile() } });

				linearIndex += static_cast<int64_t>(tile.skip()) + 1;
			}

			return json {
				{ "origin", json { { "x", originX }, { "y", originY }, { "z", originZ } } },
				{ "dimensions", json { { "width", width }, { "height", height }, { "floors", floors } } },
				{ "decodedTiles", tiles.size() },
				{ "tiles", std::move(tiles) }
			};
		}

		json toolClientDataRead(const json &params) {
			const std::string kind = readString(params, "kind");
			if (kind != "staticdata" && kind != "staticmapdata" && kind != "mapdata") {
				throw McpError("kind must be one of: staticdata, staticmapdata, mapdata");
			}

			const std::filesystem::path directory = assetsDirectory();
			const std::filesystem::path path = resolveDataFile(directory, kind, readString(params, "filename"));

			std::error_code error;
			json out {
				{ "kind", kind },
				{ "file", path.filename().string() },
				{ "path", path.string() },
				{ "sizeBytes", static_cast<uint64_t>(std::filesystem::file_size(path, error)) },
				{ "note", "read-only; exporting these files stays with the editor menu" }
			};

			if (kind == "staticdata") {
				const auto message = parseDataFile<pb_staticdata::StaticData>(path);
				out["data"] = protoToJson(message);
				return jsonResult(out);
			}

			if (kind == "mapdata") {
				const auto message = parseDataFile<pb_mapdata::MapData>(path);
				out["data"] = protoToJson(message);
				return jsonResult(out);
			}

			const auto message = parseDataFile<pb_staticmapdata::StaticMapData>(path);
			const int houseFilter = readInt(params, "houseId", 0, 0, 0x7FFFFFFF);
			const bool decode = params.value("decodePreview", true);

			json houses = json::array();
			for (const auto &entry : message.house()) {
				if (houseFilter != 0 && static_cast<int>(entry.house_id()) != houseFilter) {
					continue;
				}
				json house { { "houseId", entry.house_id() } };
				if (decode) {
					house["preview"] = decodeHousePreview(entry);
				}
				houses.push_back(std::move(house));
			}

			if (houseFilter != 0 && houses.empty()) {
				throw McpError(fmt::format("house {} is not present in {}", houseFilter, path.filename().string()));
			}

			out["houseCount"] = message.house_size();
			out["houses"] = std::move(houses);
			return jsonResult(out);
		}

	} // namespace

	void registerAssetTools(ToolRegistry &registry) {
		registry.add({ "client_info",
					   "What client assets the editor has loaded: version, assets directory, sprite and appearance counts, "
					   "the valid item id range, and a summary of catalog-content.json including the exported data files. "
					   "Call this before the other asset tools.",
					   json { { "type", "object" }, { "properties", json::object() } },
					   false,
					   toolClientInfo });

		registry.add({ "appearance_get",
					   "The complete appearance record for one id, straight from the client's appearances protobuf: "
					   "every flag, frame group, sprite info, animation, bounding box, market and cyclopedia data. "
					   "This is the authoritative description of what an item, outfit, effect or missile actually is.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "id", json { { "type", "integer" }, { "description", "client id of the appearance" } } }, { "type", json { { "type", "string" }, { "enum", json::array({ "object", "outfit", "effect", "missile" }) }, { "description", "default object" } } } } },
						   { "required", json::array({ "id" }) } },
					   false,
					   toolAppearanceGet });

		registry.add({ "appearance_search",
					   "Find appearances by name or description substring, and/or by a flag being set. "
					   "hasFlag accepts any field name from AppearanceFlags, for example container, unpass, hang, corpse, market, cyclopediaitem.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "name", json { { "type", "string" }, { "description", "substring of the name or description" } } }, { "hasFlag", json { { "type", "string" }, { "description", "an AppearanceFlags field name that must be present/true" } } }, { "type", json { { "type", "string" }, { "enum", json::array({ "object", "outfit", "effect", "missile" }) }, { "description", "default object" } } }, { "limit", json { { "type", "integer" }, { "description", "default 100" } } } } } },
					   false,
					   toolAppearanceSearch });

		registry.add({ "sprite_render",
					   "Render a client sprite as a PNG, either by sprite id or by item id. Use it to actually see what an item looks like.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "spriteId", json { { "type", "integer" }, { "description", "client asset sprite id" } } }, { "itemId", json { { "type", "integer" }, { "description", "server item id; its first sprite is used" } } }, { "scale", json { { "type", "integer" }, { "description", "nearest-neighbour upscale, 1-8, default 1" } } } } } },
					   false,
					   toolSpriteRender });

		registry.add({ "item_sprite_info",
					   "How an item is drawn: client id, sprite dimensions, layers, pattern counts, animation phases, "
					   "draw offset and height, minimap colour, plus the sprite ids per layer/pattern/frame for sprite_render.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "itemId", json { { "type", "integer" } } }, { "limit", json { { "type", "integer" }, { "description", "max sprite ids listed, default 64" } } } } },
						   { "required", json::array({ "itemId" }) } },
					   false,
					   toolItemSpriteInfo });

		registry.add({ "clientdata_read",
					   "Read one of the Cyclopedia data files the editor exports - staticdata (house metadata), "
					   "staticmapdata (house previews) or mapdata (map bounds and minimap/satellite asset descriptors) - "
					   "and return it as JSON. staticmapdata previews are decoded into absolute world positions. "
					   "Read-only: these files are only ever written by the editor's own export.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "kind", json { { "type", "string" }, { "enum", json::array({ "staticdata", "staticmapdata", "mapdata" }) } } }, { "filename", json { { "type", "string" }, { "description", "specific file in the assets directory; defaults to the newest matching one" } } }, { "houseId", json { { "type", "integer" }, { "description", "staticmapdata only: just this house" } } }, { "decodePreview", json { { "type", "boolean" }, { "description", "staticmapdata only: decode tiles to positions, default true" } } } } },
						   { "required", json::array({ "kind" }) } },
					   false,
					   toolClientDataRead });
	}

} // namespace mcp
