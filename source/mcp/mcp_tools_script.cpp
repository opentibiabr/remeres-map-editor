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

#include "../gui.h"
#include "../lua/lua_engine.h"
#include "../lua/lua_script_manager.h"

#include <string>

namespace mcp {

	namespace {

		// The editor already exposes a full scripting surface through sol2, so
		// writes go through that rather than being reimplemented as dozens of
		// typed tools. Everything the Lua console can do is reachable here,
		// including app.transaction, which keeps edits on the undo stack.
		constexpr const char* RUN_LUA_DESCRIPTION = "Run a Lua script inside the editor and get its print() output back. This is the write path: "
													"it is refused while the MCP panel is in read-only mode.\n"
													"\n"
													"Always wrap map edits in app.transaction(function() ... end) so they land on the undo stack "
													"as a single step the user can revert with Ctrl+Z.\n"
													"\n"
													"Available API (partial):\n"
													"  app.map            - .name .width .height .tileCount, :getTile(x,y,z), :getOrCreateTile(x,y,z), .tiles iterator\n"
													"  app.selection      - .tiles, :add(tile), :remove(tile), :clear(), :bounds()\n"
													"  app.editor         - :undo() :redo() .canUndo .canRedo :getHistory()\n"
													"  app.transaction(f) - run f as one undoable action\n"
													"  app.refresh()      - redraw the map view after edits\n"
													"  app.brush, app.brushSize, app.selectRaw(itemId), app.setCameraPosition(x,y,z)\n"
													"  Tile              - .ground .items .position .houseId :addItem(item) :removeItem(item)\n"
													"                      :applyBrush(brush) :borderize() :wallize() :setSpawn(radius) :setCreature(name)\n"
													"                      .isPZ .isPvpZone .isNoPvp .isNoLogout .isBlocking .isHouseTile\n"
													"  Item.get(id), Items.findByName(s), Items.findIdByName(s), Items.getInfo(id)\n"
													"  Brushes.get(name), Brushes.getNames()\n"
													"  json.encode / json.decode\n"
													"\n"
													"Example - place a stone floor over a 5x5 area:\n"
													"  app.transaction(function()\n"
													"    for x = 1000, 1004 do\n"
													"      for y = 1000, 1004 do\n"
													"        local tile = app.map:getOrCreateTile(x, y, 7)\n"
													"        tile.ground = Item.get(431)\n"
													"      end\n"
													"    end\n"
													"  end)\n"
													"  app.refresh()\n"
													"  print('done')";

		json toolRunLua(const json &params) {
			const std::string code = readString(params, "code");
			if (code.empty()) {
				throw McpError("code is required");
			}

			if (!g_luaScripts.isInitialized() && !g_luaScripts.initialize()) {
				throw McpError("the Lua engine failed to initialize");
			}

			// Capture print() for the duration of this call only, then restore
			// whatever the Script Manager panel had installed. The guard makes
			// that hold even if the script throws.
			std::string output;
			LuaEngine &engine = g_luaScripts.getEngine();

			struct PrintCallbackGuard {
				LuaEngine &engine;
				LuaEngine::PrintCallback previous;
				~PrintCallbackGuard() {
					engine.setPrintCallback(previous);
				}
			} guard { engine, engine.getPrintCallback() };

			engine.setPrintCallback([&output](const std::string &line) {
				output += line;
				output += '\n';
			});

			const bool ok = engine.executeString(code, "mcp");
			const std::string error = ok ? std::string() : engine.getLastError();

			json out {
				{ "ok", ok },
				{ "output", output }
			};
			if (!ok) {
				out["error"] = error;
			}
			return jsonResult(out);
		}

	} // namespace

	void registerScriptTools(ToolRegistry &registry) {
		registry.add({ "run_lua",
					   RUN_LUA_DESCRIPTION,
					   json {
						   { "type", "object" },
						   { "properties", json { { "code", json { { "type", "string" }, { "description", "Lua source to execute in the editor" } } } } },
						   { "required", json::array({ "code" }) } },
					   true,
					   toolRunLua });
	}

} // namespace mcp
