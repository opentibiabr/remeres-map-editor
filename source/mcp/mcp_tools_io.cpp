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
// Bringing data in and out: painting a map from an image, loading creature
// definitions, and writing minimap images.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "mcp_tools.h"

#include "../bitmap_to_map_converter.h"
#include "../brush.h"
#include "../common.h"
#include "../editor.h"
#include "../gui.h"
#include "../iominimap.h"
#include "../map.h"
#include "../monsters.h"
#include "../npcs.h"

#include <wx/base64.h>
#include <wx/buffer.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/mstream.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace mcp {

	namespace {

		// ------------------------------------------------------------------
		// map_from_bitmap
		// ------------------------------------------------------------------

		// The image may come from disk, or inline so the model can paint its
		// own mask and feed it straight back without touching the filesystem.
		wxImage loadInputImage(const json &params) {
			const std::string path = readString(params, "path");
			const std::string inlineData = readString(params, "imageBase64");

			if (path.empty() == inlineData.empty()) {
				throw McpError("give exactly one of path or imageBase64");
			}

			wxImage image;
			if (!path.empty()) {
				const wxString filename = wxString::FromUTF8(path);
				if (!wxFileName::FileExists(filename)) {
					throw McpError(fmt::format("{} does not exist", path));
				}
				if (!image.LoadFile(filename)) {
					throw McpError(fmt::format("could not read {} as an image", path));
				}
			} else {
				const wxMemoryBuffer decoded = wxBase64Decode(inlineData.data(), inlineData.size());
				if (decoded.GetDataLen() == 0) {
					throw McpError("imageBase64 is not valid base64 data");
				}
				wxMemoryInputStream stream(decoded.GetData(), decoded.GetDataLen());
				if (!image.LoadFile(stream, wxBITMAP_TYPE_ANY)) {
					throw McpError("could not decode imageBase64 as an image");
				}
			}

			if (!image.IsOk()) {
				throw McpError("the image could not be loaded");
			}
			return image;
		}

		bool parseHexColor(const std::string &text, uint8_t &r, uint8_t &g, uint8_t &b) {
			std::string hex = text;
			if (!hex.empty() && hex.front() == '#') {
				hex.erase(hex.begin());
			}
			if (hex.size() != 6) {
				return false;
			}
			try {
				const auto value = static_cast<uint32_t>(std::stoul(hex, nullptr, 16));
				r = static_cast<uint8_t>((value >> 16) & 0xFF);
				g = static_cast<uint8_t>((value >> 8) & 0xFF);
				b = static_cast<uint8_t>(value & 0xFF);
			} catch (...) {
				return false;
			}
			return true;
		}

		MatchMode parseMatchMode(const std::string &name) {
			if (name.empty() || name == "rgb") {
				return MatchMode::MATCH_PIXEL_RGB;
			}
			if (name == "hue") {
				return MatchMode::MATCH_HUE_HSL;
			}
			throw McpError("matchMode must be rgb or hue");
		}

		std::vector<ColorMapping> parseMappings(const json &params, MatchMode defaultMode) {
			if (!params.contains("mappings") || !params["mappings"].is_array() || params["mappings"].empty()) {
				throw McpError("mappings must be a non-empty array of colour to brush entries");
			}

			std::vector<ColorMapping> mappings;
			for (const json &entry : params["mappings"]) {
				if (!entry.is_object() || !entry.contains("color")) {
					throw McpError("every mapping needs a color");
				}

				ColorMapping mapping {};
				const json &color = entry["color"];
				if (color.is_string()) {
					if (!parseHexColor(color.get<std::string>(), mapping.r, mapping.g, mapping.b)) {
						throw McpError("color must be a hex string like \"#3366cc\", or an object with r, g and b");
					}
				} else if (color.is_object() && color.contains("r") && color.contains("g") && color.contains("b")) {
					mapping.r = static_cast<uint8_t>(color["r"].get<int>());
					mapping.g = static_cast<uint8_t>(color["g"].get<int>());
					mapping.b = static_cast<uint8_t>(color["b"].get<int>());
				} else {
					throw McpError("color must be a hex string like \"#3366cc\", or an object with r, g and b");
				}

				mapping.ignore = entry.value("ignore", false);
				mapping.matchMode = entry.contains("matchMode")
					? parseMatchMode(as_lower_str(entry["matchMode"].get<std::string>()))
					: defaultMode;

				if (!mapping.ignore) {
					mapping.brushName = entry.value("brush", std::string());
					if (mapping.brushName.empty()) {
						throw McpError("a mapping needs a brush, unless it is marked ignore");
					}
					// Fail before touching the map rather than half way through.
					if (!g_brushes.getBrush(mapping.brushName)) {
						throw McpError(fmt::format("no brush named '{}'; use tileset_list or brush_list to find one", mapping.brushName));
					}
				}

				mappings.push_back(std::move(mapping));
			}
			return mappings;
		}

		json toolMapFromBitmap(const json &params) {
			Editor* editor = requireEditor();

			const wxImage image = loadInputImage(params);
			const MatchMode defaultMode = parseMatchMode(as_lower_str(readString(params, "matchMode", "rgb")));
			const std::vector<ColorMapping> mappings = parseMappings(params, defaultMode);

			const int tolerance = readInt(params, "tolerance", 0, 0, 255);
			const int offsetX = readInt(params, "offsetX", 0, 0, 65000);
			const int offsetY = readInt(params, "offsetY", 0, 0, 65000);
			const int offsetZ = readInt(params, "offsetZ", rme::MapGroundLayer, 0, rme::MapLayers - 1);

			// One pixel becomes one tile, so a large image rewrites a large
			// area. Make the caller see the size before it happens.
			const int64_t tiles = static_cast<int64_t>(image.GetWidth()) * image.GetHeight();
			if (tiles > MAX_REGION_VOLUME && !params.value("confirm", false)) {
				throw McpError(fmt::format(
					"this image is {}x{} = {} tiles, above the {} guard. "
					"Scale the image down, or pass confirm=true if that is really intended.",
					image.GetWidth(), image.GetHeight(), tiles, MAX_REGION_VOLUME
				));
			}

			BitmapToMapConverter converter(*editor);
			const ConvertResult result = converter.convert(image, mappings, tolerance, defaultMode, offsetX, offsetY, offsetZ);

			if (!result.success) {
				throw McpError(std::string("the conversion failed: ") + result.errorMessage.ToStdString());
			}

			editor->getMap().doChange();
			g_gui.RefreshView();

			return jsonResult(json {
				{ "imageSize", json { { "width", image.GetWidth() }, { "height", image.GetHeight() } } },
				{ "origin", json { { "x", offsetX }, { "y", offsetY }, { "z", offsetZ } } },
				{ "tilesPlaced", result.tilesPlaced },
				{ "tilesSkipped", result.tilesSkipped },
				{ "undoable", true },
				{ "note", "brushes were applied, so borders were generated; unmapped colours were skipped" } });
		}

		// ------------------------------------------------------------------
		// import_creatures
		// ------------------------------------------------------------------

		json toolImportCreatures(const json &params) {
			const std::string kind = readString(params, "kind", "monster");
			if (kind != "monster" && kind != "npc") {
				throw McpError("kind must be monster or npc");
			}

			const std::string path = readString(params, "path");
			if (path.empty()) {
				throw McpError("path is required: an .xml file, or a directory of Lua creature scripts");
			}

			const wxString wxPath = wxString::FromUTF8(path);
			const bool isDirectory = wxFileName::DirExists(wxPath);
			if (!isDirectory && !wxFileName::FileExists(wxPath)) {
				throw McpError(fmt::format("{} does not exist", path));
			}

			const bool monsters = kind == "monster";
			// Count before and after: the loaders report warnings, not how many
			// definitions actually landed.
			const auto countTypes = [monsters]() -> int64_t {
				int64_t total = 0;
				if (monsters) {
					for ([[maybe_unused]] const auto &entry : g_monsters) {
						++total;
					}
				} else {
					for ([[maybe_unused]] const auto &entry : g_npcs) {
						++total;
					}
				}
				return total;
			};

			const int64_t before = countTypes();

			wxString error;
			wxArrayString warnings;
			bool ok = false;

			if (isDirectory) {
				// Canary ships creatures as Lua scripts in a folder.
				ok = monsters
					? g_monsters.loadFromLuaDir(wxPath, error, warnings)
					: g_npcs.loadFromLuaDir(wxPath, error, warnings);
			} else {
				ok = monsters
					? g_monsters.importXMLFromOT(FileName(wxPath), error, warnings)
					: g_npcs.importXMLFromOT(FileName(wxPath), error, warnings);
			}

			if (!ok) {
				throw McpError(fmt::format("could not import {}: {}", path, error.ToStdString()));
			}

			json warningList = json::array();
			for (size_t i = 0; i < warnings.GetCount() && i < 50; ++i) {
				warningList.push_back(warnings[i].ToStdString());
			}

			const int64_t after = countTypes();

			// Refresh the palette so the newly loaded creatures are usable.
			g_gui.RebuildPalettes();

			return jsonResult(json {
				{ "kind", kind },
				{ "source", path },
				{ "sourceKind", isDirectory ? "lua directory" : "xml file" },
				{ "typesBefore", before },
				{ "typesAfter", after },
				{ "typesAdded", after > before ? after - before : 0 },
				{ "warningCount", warnings.GetCount() },
				{ "warnings", std::move(warningList) } });
		}

		// ------------------------------------------------------------------
		// export_minimap
		// ------------------------------------------------------------------

		json toolExportMinimap(const json &params) {
			Editor* editor = requireEditor();

			const std::string directory = readString(params, "directory");
			if (directory.empty()) {
				throw McpError("directory is required: where the image files should be written");
			}
			if (!wxFileName::DirExists(wxString::FromUTF8(directory))) {
				throw McpError(fmt::format("{} is not an existing directory", directory));
			}

			const std::string formatName = as_lower_str(readString(params, "format", "png"));
			MinimapExportFormat format;
			if (formatName == "png") {
				format = MinimapExportFormat::Png;
			} else if (formatName == "bmp") {
				format = MinimapExportFormat::Bmp;
			} else if (formatName == "otmm") {
				format = MinimapExportFormat::Otmm;
			} else {
				throw McpError("format must be png, bmp or otmm");
			}

			const std::string modeName = as_lower_str(readString(params, "mode", "ground_floor"));
			MinimapExportMode mode;
			int floor = -1;
			if (modeName == "all_floors") {
				mode = MinimapExportMode::AllFloors;
			} else if (modeName == "ground_floor") {
				mode = MinimapExportMode::GroundFloor;
			} else if (modeName == "selection") {
				mode = MinimapExportMode::SelectedArea;
				if (editor->getSelection().size() == 0) {
					throw McpError("mode=selection needs something selected; use selection_op select_region first");
				}
			} else if (modeName == "floor") {
				mode = MinimapExportMode::SpecificFloor;
				floor = readInt(params, "floor", rme::MapGroundLayer, 0, rme::MapLayers - 1);
			} else {
				throw McpError("mode must be all_floors, ground_floor, floor or selection");
			}

			const std::string name = readString(params, "name", editor->getMap().getName().empty() ? std::string("minimap") : editor->getMap().getName());
			const int imageSize = readInt(params, "imageSize", 1024, 128, 8192);

			// updateLoadbar=false: there is no user watching a progress dialog
			// on an MCP call, and the load bar would pump events reentrantly.
			IOMinimap exporter(editor, format, mode, false, imageSize);
			if (!exporter.saveMinimap(directory, name, floor)) {
				throw McpError(fmt::format("the minimap export failed: {}", exporter.getError()));
			}

			json out {
				{ "directory", directory },
				{ "name", name },
				{ "format", formatName },
				{ "mode", modeName },
				{ "imageSize", imageSize }
			};
			if (mode == MinimapExportMode::SpecificFloor) {
				out["floor"] = floor;
			}
			out["note"] = "files were written to disk; read them back with your own file tools";
			return jsonResult(out);
		}

	} // namespace

	void registerIoTools(ToolRegistry &registry) {
		registry.add({ "map_from_bitmap",
					   "Paint terrain from an image: one pixel becomes one tile, each colour mapped to a brush. "
					   "Because it applies brushes, borders and transitions are generated properly - this is the way to lay out "
					   "coastlines, rivers, lakes, forests and roads at scale. "
					   "Supply the image as a file path, or inline as base64 so you can generate the mask yourself. Undoable.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "path", json { { "type", "string" }, { "description", "image file to read; mutually exclusive with imageBase64" } } }, { "imageBase64", json { { "type", "string" }, { "description", "the image inline, base64 encoded (png or bmp)" } } }, { "mappings", json { { "type", "array" }, { "description", "colour to brush entries" }, { "items", json { { "type", "object" }, { "properties", json { { "color", json { { "description", "hex like \"#3366cc\", or {r,g,b}" } } }, { "brush", json { { "type", "string" }, { "description", "brush name; required unless ignore is true" } } }, { "ignore", json { { "type", "boolean" }, { "description", "leave pixels of this colour untouched" } } }, { "matchMode", json { { "type", "string" }, { "enum", json::array({ "rgb", "hue" }) } } } } }, { "required", json::array({ "color" }) } } } } }, { "tolerance", json { { "type", "integer" }, { "description", "colour distance allowed when matching, 0-255, default 0" } } }, { "matchMode", json { { "type", "string" }, { "enum", json::array({ "rgb", "hue" }) }, { "description", "default for mappings that do not set their own" } } }, { "offsetX", json { { "type", "integer" }, { "description", "map x of the image top-left" } } }, { "offsetY", json { { "type", "integer" }, { "description", "map y of the image top-left" } } }, { "offsetZ", json { { "type", "integer" }, { "description", "floor, default 7" } } }, { "confirm", json { { "type", "boolean" }, { "description", "required for images larger than the region guard" } } } } },
						   { "required", json::array({ "mappings" }) } },
					   true,
					   toolMapFromBitmap });

		registry.add({ "import_creatures",
					   "Load monster or npc definitions into this editor installation, from an OT .xml file or a directory of "
					   "Lua creature scripts. Use it when map_validate reports creatures the installation cannot resolve, "
					   "or before placing a creature that is not in monster_types_list yet.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "kind", json { { "type", "string" }, { "enum", json::array({ "monster", "npc" }) }, { "description", "default monster" } } }, { "path", json { { "type", "string" }, { "description", "an .xml file, or a directory of Lua creature scripts" } } } } },
						   { "required", json::array({ "path" }) } },
					   true,
					   toolImportCreatures });

		registry.add({ "export_minimap",
					   "Write minimap images of the map to a directory, as png, bmp or the editor's otmm format. "
					   "Covers all floors, the ground floor, one specific floor, or the current selection.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "directory", json { { "type", "string" }, { "description", "an existing directory to write into" } } }, { "name", json { { "type", "string" }, { "description", "base filename, defaults to the map name" } } }, { "format", json { { "type", "string" }, { "enum", json::array({ "png", "bmp", "otmm" }) }, { "description", "default png" } } }, { "mode", json { { "type", "string" }, { "enum", json::array({ "all_floors", "ground_floor", "floor", "selection" }) }, { "description", "default ground_floor" } } }, { "floor", json { { "type", "integer" }, { "description", "for mode=floor" } } }, { "imageSize", json { { "type", "integer" }, { "description", "max image edge, default 1024" } } } } },
						   { "required", json::array({ "directory" }) } },
					   true,
					   toolExportMinimap });
	}

} // namespace mcp
