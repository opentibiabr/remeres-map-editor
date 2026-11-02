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

#ifndef RME_MCP_TOOLS_H
#define RME_MCP_TOOLS_H

#include "mcp_common.h"

#include <wx/image.h>

#include <functional>
#include <string>
#include <vector>

namespace mcp {

	struct Tool {
		std::string name;
		std::string description;
		json inputSchema;
		// Mutating tools are refused while the panel is in read-only mode.
		bool mutates = false;
		// Called on the GUI thread. Returns the MCP result object, normally
		// {"content": [...]}. Throw McpError for a user-visible failure.
		std::function<json(const json &arguments)> handler;
	};

	class ToolRegistry {
	public:
		static ToolRegistry &get();

		void add(Tool tool);
		const std::vector<Tool> &all() const noexcept {
			return tools;
		}
		const Tool* find(const std::string &name) const;

		// Populated lazily on first access so the registry does not depend on
		// static initialization order across translation units.
		void ensureRegistered();

	private:
		std::vector<Tool> tools;
		bool registered = false;
	};

	// Implemented per domain, called by ensureRegistered().
	void registerMapTools(ToolRegistry &registry);
	void registerEntityTools(ToolRegistry &registry);
	void registerScriptTools(ToolRegistry &registry);
	// Lives in mcp_render.cpp; called from registerMapTools().
	void registerRenderTool(ToolRegistry &registry);
	void registerEditTools(ToolRegistry &registry);
	void registerManageTools(ToolRegistry &registry);
	void registerAssetTools(ToolRegistry &registry);
	void registerAnalyzeTools(ToolRegistry &registry);
	void registerOpsTools(ToolRegistry &registry);
	void registerBrushTools(ToolRegistry &registry);
	void registerSelectTools(ToolRegistry &registry);
	void registerIoTools(ToolRegistry &registry);
	void registerClientViewTools(ToolRegistry &registry);
	void registerTerrainTools(ToolRegistry &registry);
	void registerStampTools(ToolRegistry &registry);
	void registerGuideTools(ToolRegistry &registry);

	// Composites a region of any map into an image using the client's real
	// sprites (mcp_render.cpp). Works on a scratch BaseMap too, which is how
	// brush_preview draws a brush without touching the open map.
	wxImage renderTileRegion(BaseMap &map, const Position &origin, int width, int height, int pixelsPerTile);

	// Encodes a wxImage as a base64 PNG (mcp_render.cpp). Throws McpError if
	// the image cannot be encoded.
	std::string encodePngBase64(const wxImage &image);

	// Item serialization and construction, shared by the reading and writing
	// tools. Covers the complex subclasses (teleport, container, door, depot),
	// which is what makes quest content legible and editable.
	json itemToJson(const Item* item, int depth = 0);
	Item* buildItem(const json &spec);
	// The `addItems`/`ground` entry shape, reused across tool schemas.
	json itemSpecSchema();

	// Shared input-schema fragments. These live here rather than in each tool
	// file because the unity build merges those anonymous namespaces into one,
	// where duplicate definitions collide.
	json positionSchema(const char* description = "a tile position");
	json positionArraySchema(const char* description);
	json emptySchema();

	// Helpers for building MCP result payloads.
	json textResult(const std::string &text);
	json jsonResult(const json &value);
	json imageResult(const std::string &base64Data, const std::string &mimeType);

} // namespace mcp

#endif // RME_MCP_TOOLS_H
