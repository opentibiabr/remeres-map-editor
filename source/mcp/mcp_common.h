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

#ifndef RME_MCP_COMMON_H
#define RME_MCP_COMMON_H

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>

class BaseMap;
class Editor;
class Item;
class Position;

namespace mcp {

	using json = nlohmann::json;

	// Thrown by tool handlers. The message is returned to the LLM as a tool
	// error result (isError=true), not as a JSON-RPC protocol error, so the
	// model can read it and correct its next call.
	class McpError : public std::runtime_error {
	public:
		explicit McpError(const std::string &message) :
			std::runtime_error(message) { }
	};

	// Largest region volume any single tool call may scan. 512*512 = one full
	// floor of a decently sized town; beyond that the caller must narrow the
	// region or use a summary mode.
	inline constexpr int64_t MAX_REGION_VOLUME = 262144;

	// Returns the editor for the map tab currently in focus, or throws if no
	// map is open. Mirrors the guard the Lua layer uses (lua_api_app.cpp).
	// MUST be called on the GUI thread.
	Editor* requireEditor();

	// Parses {"x":..,"y":..,"z":..} and validates it against the open map's
	// bounds. Throws McpError with a readable message on bad input.
	Position parsePosition(const json &value, const char* fieldName);

	json positionToJson(const Position &pos);

	// Reads an optional integer field, clamping it into [minValue, maxValue].
	int readInt(const json &params, const char* key, int defaultValue, int minValue, int maxValue);

	std::string readString(const json &params, const char* key, const std::string &defaultValue = "");

} // namespace mcp

#endif // RME_MCP_COMMON_H
