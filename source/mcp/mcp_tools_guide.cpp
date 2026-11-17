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
// Sixty tools with no stated order is a maze. This hands back the workflow
// that actually produces a good map, plus the brush-pairing knowledge that
// otherwise has to be learned by making mistakes.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "mcp_tools.h"

#include "../brush.h"
#include "../common.h"
#include "../ground_brush.h"
#include "../gui.h"
#include "../materials.h"
#include "../tile.h"
#include "../tileset.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace mcp {

	namespace {

		constexpr const char* WORKFLOW = R"(BUILDING A MAP FROM A DESCRIPTION

Work in this order. Each step catches mistakes that are far more expensive to
fix once the next step is done.

1. ORIENT
   map_info, then map_read_region mode=summary over the area you were given.
   Never start writing before you know what is already there.

2. CHOOSE BRUSHES - do not skip this
   tileset_list to browse the palette by tileset and category, then
   brush_preview on each candidate to see what it actually lays down.
   Guessing brush names by their sound is the single most common way to produce
   terrain that looks nothing like the description.
   terrain_pairing tells you which brushes have a real transition defined
   between them, so you pick pairs that blend instead of pairs that clash.

3. LAY TERRAIN WITH BRUSHES, NEVER RAW IDS. RELIES ON RAW IDS ONLY IF THE USER SPECIFY IT.
   brush_apply for shapes you can describe as positions.
   map_from_bitmap when the shape is organic - coastlines, rivers, lakes,
   forest edges - because one pixel becomes one tile and you can paint the mask.
   Ground placed as a raw item id through tile_edit belongs to no brush, so
   autoborder can never touch it and the result will always look wrong.
   Check editor_settings that automagic is on, or brushes will not border.

4. CARVE INTERIORS AND STRUCTURE
   Walls with brush_apply so corners connect. Repeat structures with
   stamp_manage capture and stamp_place; region_transform for symmetry.

5. DETAIL
   tile_edit for specific items, and for the things that carry state: teleport
   destinations, container contents, door ids, action and unique ids.
   spawn_manage for creatures, with radius, spawnTime and facing direction.
   house_manage, town_manage, zone_manage, waypoint_manage for the metadata.

6. VERIFY - this is where a generated map becomes a usable one
   border_check      hard seams between grounds, and ground no brush owns
   client_view_scan  places where a player would see unmapped space
   path_check        is every area actually reachable, is the arena sealed
   floor_transitions do the stairs lead somewhere real
   teleport_graph    do the portals lead somewhere real
   map_validate      houses, spawns, duplicate ids, everything else
   Fix what they report, then run them again.

7. LOOK AT IT
   map_render_region mode=sprites for a region, client_view render=true for
   what a player standing somewhere sees. Compare against the description you
   were given before declaring it done.

WRITING
   Batch. One tile_edit with 500 tiles is one undo step and one round trip;
   500 calls are neither. Everything except map_cleanup, map_import and
   map_from_bitmap oversized is undoable, so the user can always revert you.

WHEN A TOOL IS NOT ENOUGH
   run_lua reaches the editor's whole scripting API, including the noise, geo
   and algo modules for procedural generation. Wrap edits in app.transaction.)";

		json toolGenerationGuide(const json &) {
			return jsonResult(json {
				{ "workflow", WORKFLOW },
				{ "verifyTools", json::array({ "border_check", "client_view_scan", "path_check", "floor_transitions", "teleport_graph", "map_validate" }) },
				{ "reminder", "lay ground with brush_apply or map_from_bitmap, never with raw item ids in tile_edit" } });
		}

		// Which ground brushes have a real transition defined against a given
		// one. GroundBrush::getBrushTo answers it, but BorderBlock is a
		// protected nested type, so the presence of a border is probed instead
		// by asking the brush what borders it declares.
		json toolTerrainPairing(const json &params) {
			const std::string name = readString(params, "name");
			if (name.empty()) {
				throw McpError("name is required: the ground brush you want to pair");
			}

			Brush* brush = g_brushes.getBrush(name);
			if (!brush) {
				throw McpError(fmt::format("no brush named '{}'; use tileset_list to browse the palette", name));
			}
			if (!brush->isGround()) {
				throw McpError(fmt::format("'{}' is not a ground brush, so it has no ground transitions", name));
			}

			auto* ground = static_cast<GroundBrush*>(brush);

			// Collect every ground brush in the palette, and report how each
			// one relates to this brush by z-order, which is what decides who
			// draws the border over whom.
			std::vector<std::pair<std::string, GroundBrush*>> grounds;
			for (const auto &[tilesetName, tileset] : g_materials.tilesets) {
				if (!tileset) {
					continue;
				}
				const TilesetCategory* terrain = tileset->getCategory(TILESET_TERRAIN);
				if (!terrain) {
					continue;
				}
				for (Brush* candidate : terrain->brushlist) {
					if (candidate && candidate->isGround() && candidate != brush) {
						grounds.emplace_back(candidate->getName(), static_cast<GroundBrush*>(candidate));
					}
				}
			}

			std::sort(grounds.begin(), grounds.end());
			grounds.erase(std::unique(grounds.begin(), grounds.end()), grounds.end());

			const int limit = readInt(params, "limit", 60, 1, 500);
			const int myZ = ground->getZ();

			json pairs = json::array();
			for (const auto &[candidateName, candidate] : grounds) {
				if (static_cast<int>(pairs.size()) >= limit) {
					break;
				}
				const int otherZ = candidate->getZ();
				pairs.push_back(json {
					{ "brush", candidateName },
					{ "zOrder", otherZ },
					{ "drawsBorderOver", otherZ > myZ ? candidateName : name },
					{ "hasOuterBorder", candidate->hasOuterBorder() },
					{ "hasInnerBorder", candidate->hasInnerBorder() } });
			}

			return jsonResult(json {
				{ "brush", name },
				{ "zOrder", myZ },
				{ "hasOuterBorder", ground->hasOuterBorder() },
				{ "hasInnerBorder", ground->hasInnerBorder() },
				{ "hasOptionalBorder", ground->hasOptionalBorder() },
				{ "groundBrushes", std::move(pairs) },
				{ "howToRead",
				  "The brush with the higher zOrder draws its border over the lower one. A brush with hasOuterBorder=false "
				  "declares no outer border, so where it meets another ground you will get a hard edge no matter what - "
				  "prefer a partner that has one. Verify any pair by painting a small patch and running border_check." } });
		}

	} // namespace

	void registerGuideTools(ToolRegistry &registry) {
		registry.add({ "generation_guide",
					   "The working order for building or editing a map from a description: how to pick brushes, lay terrain, "
					   "carve interiors, add detail, and which tools verify the result. "
					   "Call this before generating anything larger than a few tiles - it will save you from the mistakes that are "
					   "expensive to undo, above all laying ground as raw item ids instead of through brushes.",
					   emptySchema(),
					   false,
					   toolGenerationGuide });

		registry.add({ "terrain_pairing",
					   "How a ground brush relates to the other ground brushes: z-order, which of the pair draws the border, and "
					   "whether each declares outer and inner borders. Use it to choose terrain pairs that actually blend, "
					   "instead of discovering a hard seam afterwards with border_check.",
					   json {
						   { "type", "object" },
						   { "properties", json { { "name", json { { "type", "string" }, { "description", "the ground brush to pair" } } }, { "limit", json { { "type", "integer" }, { "description", "default 60" } } } } },
						   { "required", json::array({ "name" }) } },
					   false,
					   toolTerrainPairing });
	}

} // namespace mcp
