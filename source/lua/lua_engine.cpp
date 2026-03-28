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
#include "lua_engine.h"

#include <fstream>
#include <sstream>

LuaEngine::~LuaEngine() {
	try {
		shutdown();
	} catch (const std::exception &e) {
		std::cerr << "[LuaEngine] Critical exception during destructor shutdown: "
				  << e.what() << std::endl;
	} catch (...) {
		std::cerr << "[LuaEngine] Unknown critical exception during destructor shutdown." << std::endl;
	}
}

bool LuaEngine::initialize() {
	if (initialized) {
		return true;
	}

	try {
		lua.open_libraries(
			sol::lib::base,
			sol::lib::package,
			sol::lib::coroutine,
			sol::lib::string,
			sol::lib::table,
			sol::lib::math,
			sol::lib::utf8,
			sol::lib::os
		);

		setupSandbox();
		registerBaseLibraries();

		initialized = true;
		return true;
	} catch (const sol::error &e) {
		lastError = std::string("Failed to initialize Lua engine: ") + e.what();
		return false;
	} catch (const std::exception &e) {
		lastError = std::string("Failed to initialize Lua engine: ") + e.what();
		return false;
	} catch (...) {
		lastError = "Failed to initialize Lua engine: Unknown error";
		return false;
	}
}

void LuaEngine::shutdown() {
	if (!initialized) {
		return;
	}

	printCallback = nullptr;
	lua = sol::state();
	initialized = false;
}

std::string LuaEngine::sanitizeScriptPath(const std::string &filename) {
	if (currentScriptDir.empty()) {
		throw sol::error("Script directory not set. Cannot resolve relative path.");
	}

	if (filename.contains(":") || (filename.size() > 0 && (filename[0] == '/' || filename[0] == '\\'))) {
		throw sol::error("Absolute paths are not allowed. Use paths relative to the script.");
	}

	std::string cleanFilename = filename;
	if (cleanFilename.starts_with("./") || cleanFilename.starts_with(".\\")) {
		cleanFilename = cleanFilename.substr(2);
	}

	// Validate '..' per path segment
	size_t pos = 0;
	size_t lastSep = 0;
	while (pos <= cleanFilename.length()) {
		if (pos == cleanFilename.length() || cleanFilename[pos] == '/' || cleanFilename[pos] == '\\') {
			std::string segment = cleanFilename.substr(lastSep, pos - lastSep);
			if (segment == "..") {
				throw sol::error("Directory traversal ('..') is not allowed.");
			}
			lastSep = pos + 1;
		}
		pos++;
	}

	return cleanFilename;
}

void LuaEngine::setupSandbox() {
	if (lua["os"].valid()) {
		lua["os"]["execute"] = sol::nil;
		lua["os"]["exit"] = sol::nil;
		lua["os"]["remove"] = sol::nil;
		lua["os"]["rename"] = sol::nil;
		lua["os"]["tmpname"] = sol::nil;
		lua["os"]["getenv"] = sol::nil;
		lua["os"]["setlocale"] = sol::nil;
	}

	lua["io"] = sol::nil;

	if (lua["package"].valid()) {
		lua["package"]["loadlib"] = sol::nil;
		lua["package"]["path"] = sol::nil;
		lua["package"]["cpath"] = sol::nil;
		lua["package"]["searchpath"] = sol::nil;

		if (lua["package"]["searchers"].valid()) {
			sol::table searchers = lua["package"]["searchers"];
			searchers[2] = sol::nil; // Disable Lua file searcher
			searchers[3] = sol::nil;
			searchers[4] = sol::nil;
		}
	}

	lua["dofile"] = [this](const std::string &filename, sol::this_state s) {
		std::string cleanFilename = this->sanitizeScriptPath(filename);
		std::string fullPath = this->currentScriptDir + "/" + cleanFilename;
		return this->executeFile(fullPath);
	};

	lua["loadfile"] = [this](const std::string &filename, sol::this_state s) -> sol::object {
		sol::state_view lua(s);

		std::string cleanFilename = this->sanitizeScriptPath(filename);
		std::string fullPath = this->currentScriptDir + "/" + cleanFilename;

		// Extract directory for the chunk
		std::string chunkDir;
		size_t lastSlash = fullPath.find_last_of("/\\");
		if (lastSlash != std::string::npos) {
			chunkDir = fullPath.substr(0, lastSlash);
		} else {
			chunkDir = ".";
		}

		// Load the file
		sol::load_result loaded = lua.load_file(fullPath);
		if (!loaded.valid()) {
			sol::error err = loaded;
			throw err;
		}

		// C++ wrapper managing currentScriptDir without exposing LuaEngine
		sol::protected_function chunk = loaded;
		std::string capturedDir = chunkDir;

		auto wrapper = [this, chunk, capturedDir](sol::variadic_args va) -> sol::object {
			std::string savedDir = this->currentScriptDir;
			this->currentScriptDir = capturedDir;

			struct Guard {
				LuaEngine* e;
				std::string d;
				~Guard() noexcept {
					try {
						e->currentScriptDir = d;
					} catch (const std::exception &err) {
						std::cerr << "[LuaEngine] Guard failed to restore script directory: "
								  << err.what() << std::endl;
						e->currentScriptDir.clear();
					}
				}
			} g { this, savedDir };

			sol::protected_function_result result = chunk(va);
			if (!result.valid()) {
				sol::error err = result;
				throw err;
			}

			// Return first value (compatible with standard loadfile)
			if (result.return_count() > 0) {
				return result[0];
			}
			return sol::nil;
		};

		return sol::make_object(this->lua, wrapper);
	};

	try {
		lua.script(R"(
			local old_load = load
			_G.load = function(chunk, chunkname, mode, env)
				if mode and mode ~= "t" then
					error("Secure Mode: Binary chunks are disabled.")
				end
				return old_load(chunk, chunkname, "t", env)
			end
		)");
	} catch (const std::exception &e) {
		lastError = std::string("Failed to install secure load patch: ") + e.what();
		throw;
	} catch (...) {
		lastError = "Failed to install secure load patch: Unknown error";
		throw;
	}
}

void LuaEngine::handlePrint(sol::variadic_args va) {
	std::ostringstream oss;
	bool first = true;
	for (auto v : va) {
		if (!first) {
			oss << "\t";
		}
		first = false;

		sol::state_view lua(v.lua_state());
		sol::function tostring = lua["tostring"];
		std::string str = tostring(v);
		oss << str;
	}

	std::string output = oss.str();

	if (printCallback) {
		printCallback(output);
	} else {
		std::cout << "[Lua] " << output << std::endl;
	}
}

void LuaEngine::registerBaseLibraries() {
	lua["print"] = [this](sol::variadic_args va) {
		handlePrint(va);
	};
}

void LuaEngine::setPrintCallback(PrintCallback callback) {
	printCallback = callback;

	lua["print"] = [this](sol::variadic_args va) {
		handlePrint(va);
	};
}

bool LuaEngine::executeFile(const std::string &filepath) {
	if (!initialized) {
		lastError = "Lua engine not initialized";
		return false;
	}

	// Save current script directory for RAII restoration
	std::string savedScriptDir = currentScriptDir;

	// RAII guard to restore currentScriptDir on all exit paths
	struct ScopeGuard {
		LuaEngine* engine;
		std::string savedDir;

		ScopeGuard(LuaEngine* e, std::string dir) :
			engine(e), savedDir(std::move(dir)) { }

		ScopeGuard(const ScopeGuard &) = delete;
		ScopeGuard &operator=(const ScopeGuard &) = delete;
		ScopeGuard(ScopeGuard &&) = delete;
		ScopeGuard &operator=(ScopeGuard &&) = delete;

		~ScopeGuard() noexcept {
			try {
				engine->currentScriptDir = savedDir;
			} catch (const std::exception &err) {
				std::cerr << "[LuaEngine] ScopeGuard failed to restore script directory: "
						  << err.what() << std::endl;
				engine->currentScriptDir.clear();
			} catch (...) {
				std::cerr << "[LuaEngine] ScopeGuard failed to restore script directory: Unknown fatal error" << std::endl;
				engine->currentScriptDir.clear();
			}
		}
	};
	ScopeGuard guard { this, savedScriptDir };

	try {
		std::string scriptDir;
		size_t lastSlash = filepath.find_last_of("/\\");
		if (lastSlash != std::string::npos) {
			scriptDir = filepath.substr(0, lastSlash);
		} else {
			scriptDir = ".";
		}
		currentScriptDir = scriptDir;

		sol::load_result loaded = lua.load_file(filepath);
		if (!loaded.valid()) {
			sol::error err = loaded;
			lastError = std::string("Failed to load script '") + filepath + "': " + err.what();
			return false;
		}

		sol::protected_function script = loaded;
		sol::protected_function_result result = script();

		if (!result.valid()) {
			sol::error err = result;
			lastError = std::string("Error executing script '") + filepath + "': " + err.what();
			return false;
		}

		return true;
	} catch (const sol::error &e) {
		lastError = std::string("Exception executing script '") + filepath + "': " + e.what();
		return false;
	} catch (const std::exception &e) {
		lastError = std::string("Exception executing script '") + filepath + "': " + e.what();
		return false;
	} catch (...) {
		lastError = std::string("Unknown exception executing script '") + filepath + "'";
		return false;
	}
}

bool LuaEngine::executeString(const std::string &code, const std::string &chunkName) {
	if (!initialized) {
		lastError = "Lua engine not initialized";
		return false;
	}

	try {
		sol::load_result loaded = lua.load(code, chunkName, sol::load_mode::text);
		if (!loaded.valid()) {
			sol::error err = loaded;
			lastError = std::string("Failed to load code: ") + err.what();
			return false;
		}

		sol::protected_function script = loaded;
		sol::protected_function_result result = script();

		if (!result.valid()) {
			sol::error err = result;
			lastError = std::string("Error executing code: ") + err.what();
			return false;
		}

		return true;
	} catch (const sol::error &e) {
		lastError = std::string("Exception executing code: ") + e.what();
		return false;
	} catch (const std::exception &e) {
		lastError = std::string("Exception executing code: ") + e.what();
		return false;
	} catch (...) {
		lastError = "Unknown exception executing code";
		return false;
	}
}
