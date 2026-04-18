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

#ifndef RME_LUA_SCRIPT_MANAGER_H
#define RME_LUA_SCRIPT_MANAGER_H

#include "lua_engine.h"
#include "lua_script.h"

#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <map>
#include <functional>
#include <tuple>

#include "../map_overlay.h"
#include "lua_overlay_manager.h"

class Tile;
class Item;

// Callback type for output messages
using LuaOutputCallback = std::function<void(const std::string &message, bool isError)>;

class LuaScriptManager {
public:
	static LuaScriptManager &getInstance();

	// Lifecycle
	bool initialize();
	void shutdown();
	bool isInitialized() const {
		return initialized;
	}

	// Script discovery and management
	void discoverScripts();
	void reloadScripts();

	// Script execution
	bool executeScript(const std::string &filepath);
	bool executeScript(const LuaScript* script);
	bool executeScript(size_t index, std::string &errorOut);

	// Access scripts
	const std::vector<std::unique_ptr<LuaScript>> &getScripts() const {
		return scripts;
	}
	LuaScript* getScript(std::string_view filepath);

	// Enable/disable scripts
	void setScriptEnabled(size_t index, bool enabled);
	bool isScriptEnabled(size_t index) const;

	// Engine access
	LuaEngine &getEngine() {
		return engine;
	}

	// Error handling
	std::string getLastError() const {
		return lastError;
	}

	// Scripts directory
	std::string getScriptsDirectory() const;
	void openScriptsFolder();

	// Output callback for Script Manager window
	void setOutputCallback(LuaOutputCallback callback);
	void logOutput(const std::string &message, bool isError = false);

	// Dynamic Context Menu
	struct ContextMenuItem {
		std::string label;
		sol::function callback;
	};
	void registerContextMenuItem(const std::string &label, sol::function callback);
	const std::vector<ContextMenuItem> &getContextMenuItems() const {
		return contextMenuItems;
	}

	// Event System
	struct EventListener {
		int id;
		std::string eventName;
		sol::function callback;
	};
	int addEventListener(const std::string &eventName, sol::function callback);
	bool removeEventListener(int listenerId);

	template <typename... Args>
	void emit(const std::string &eventName, Args &&... args) {
		if (!initialized) {
			return;
		}

		// Capture args into a stable copy to avoid move-from on second iteration
		auto argsCopy = std::make_tuple(std::decay_t<Args>(std::forward<Args>(args))...);

		// Iterate over a copy to allow callbacks to modify the listener list safely
		std::vector<EventListener> listenersCopy = eventListeners;
		for (const auto &listener : listenersCopy) {
			if (listener.eventName == eventName && listener.callback.valid()) {
				try {
					std::apply(listener.callback, argsCopy);
				} catch (const sol::error &e) {
					logOutput("Event '" + eventName + "' error: " + std::string(e.what()), true);
				}
			}
		}
	}

	template <typename... Args>
	bool emitCancellable(const std::string &eventName, Args &&... args) {
		if (!initialized) {
			return false;
		}

		// Capture args into a stable copy to avoid move-from on second iteration
		auto argsCopy = std::make_tuple(std::decay_t<Args>(std::forward<Args>(args))...);

		// Iterate over a copy to allow callbacks to modify the listener list safely
		std::vector<EventListener> listenersCopy = eventListeners;
		bool consumed = false;
		for (const auto &listener : listenersCopy) {
			if (listener.eventName == eventName && listener.callback.valid()) {
				try {
					sol::object result = std::apply(listener.callback, argsCopy);
					if (result.valid() && result.is<bool>() && result.as<bool>()) {
						consumed = true;
						break; // Stop propagation
					}
				} catch (const sol::error &e) {
					logOutput("Event '" + eventName + "' error: " + std::string(e.what()), true);
				}
			}
		}
		return consumed;
	}

	// Clear all registered callbacks (called before script reload)
	void clearAllCallbacks();

	// Map Overlay System (delegated to LuaOverlayManager)
	LuaOverlayManager &getOverlayManager() {
		return overlayManager;
	}
	const LuaOverlayManager &getOverlayManager() const {
		return overlayManager;
	}

private:
	LuaScriptManager() = default;
	~LuaScriptManager() = default;
	LuaScriptManager(const LuaScriptManager &) = delete;
	LuaScriptManager &operator=(const LuaScriptManager &) = delete;

	LuaEngine engine;
	std::vector<std::unique_ptr<LuaScript>> scripts;
	std::string lastError;
	bool initialized = false;
	LuaOutputCallback outputCallback;
	mutable std::mutex outputCallbackMutex;
	std::vector<ContextMenuItem> contextMenuItems;
	std::vector<EventListener> eventListeners;
	int nextListenerId = 1;
	LuaOverlayManager overlayManager;

	void registerAPIs();
	void scanDirectory(const std::string &directory);
	void runAutoScripts();
};

// Global accessor macro
inline LuaScriptManager &getLuaManager() {
	return LuaScriptManager::getInstance();
}
#define g_luaScripts (getLuaManager())

#endif // RME_LUA_SCRIPT_MANAGER_H
