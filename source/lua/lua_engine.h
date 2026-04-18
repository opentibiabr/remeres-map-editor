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

#ifndef RME_LUA_ENGINE_H
#define RME_LUA_ENGINE_H

#include <string>
#include <functional>

#include <sol/sol.hpp>

class LuaEngine {
public:
	using PrintCallback = std::function<void(const std::string &)>;
	LuaEngine() = default;
	~LuaEngine();

	bool initialize();
	void shutdown();
	bool isInitialized() const {
		return initialized;
	}

	void setPrintCallback(PrintCallback callback);

	bool executeFile(const std::string &filepath);
	bool executeString(const std::string &code, const std::string &chunkName = "chunk");

	sol::state &getState() {
		return lua;
	}
	std::string getLastError() const {
		return lastError;
	}

private:
	sol::state lua;
	bool initialized = false;
	std::string lastError;
	PrintCallback printCallback;
	std::string currentScriptDir;

	void handlePrint(sol::variadic_args va);
	void setupSandbox();
	void registerBaseLibraries();
	std::string sanitizeScriptPath(const std::string &filename);
};

#endif // RME_LUA_ENGINE_H
