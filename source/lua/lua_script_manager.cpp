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
#include "lua_script_manager.h"
#include "lua_api.h"
#include "lua_api_image.h"
#include "../gui.h"
#include "../tile.h"

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <algorithm>
#include <spdlog/spdlog.h>

#ifdef _WIN32
	#include <Windows.h>
	#include <shellapi.h>
#endif

LuaScriptManager &LuaScriptManager::getInstance() {
	static LuaScriptManager instance;
	return instance;
}

bool LuaScriptManager::initialize() {
	if (initialized) {
		return true;
	}

	try {
		// Initialize the Lua engine
		if (!engine.initialize()) {
			lastError = "Failed to initialize Lua engine: " + engine.getLastError();
			return false;
		}

		// Register all APIs
		registerAPIs();

		initialized = true;

		spdlog::info("Lua scripting initialized");

		// Discover scripts
		discoverScripts();
		return true;
	} catch (const std::exception &e) {
		scripts.clear();
		clearAllCallbacks();
		engine.shutdown();
		initialized = false;
		lastError = "Exception during Lua initialization: " + std::string(e.what());
		return false;
	} catch (...) {
		scripts.clear();
		clearAllCallbacks();
		engine.shutdown();
		initialized = false;
		lastError = "Unknown exception during Lua initialization";
		return false;
	}
}

void LuaScriptManager::shutdown() {
	if (!initialized) {
		return;
	}

	scripts.clear();
	clearAllCallbacks();
	engine.shutdown();
	initialized = false;
}

void LuaScriptManager::registerContextMenuItem(const std::string &label, sol::function callback) {
	ContextMenuItem item;
	item.label = label;
	item.callback = callback;
	contextMenuItems.push_back(item);
}

int LuaScriptManager::addEventListener(const std::string &eventName, sol::function callback) {
	EventListener listener;
	listener.id = nextListenerId++;
	listener.eventName = eventName;
	listener.callback = callback;
	eventListeners.push_back(listener);
	return listener.id;
}

bool LuaScriptManager::removeEventListener(int listenerId) {
	for (auto it = eventListeners.begin(); it != eventListeners.end(); ++it) {
		if (it->id == listenerId) {
			eventListeners.erase(it);
			return true;
		}
	}
	return false;
}

void LuaScriptManager::clearAllCallbacks() {
	contextMenuItems.clear();
	eventListeners.clear();
	nextListenerId = 1;
	overlayManager.clear();
}

void LuaScriptManager::registerAPIs() {
	// Register all Lua APIs
	LuaAPI::registerAll(engine.getState());
}

static wxColor parseColor(const sol::object &obj, const wxColor &fallback) {
	if (!obj.valid()) {
		return fallback;
	}

	if (obj.is<sol::table>()) {
		sol::table tbl = obj.as<sol::table>();
		int r = tbl.get_or(std::string("r"), tbl.get_or(std::string("red"), 255));
		int g = tbl.get_or(std::string("g"), tbl.get_or(std::string("green"), 255));
		int b = tbl.get_or(std::string("b"), tbl.get_or(std::string("blue"), 255));
		int a = tbl.get_or(std::string("a"), tbl.get_or(std::string("alpha"), 255));
		if (tbl[1].valid()) {
			r = tbl.get_or(1, r);
		}
		if (tbl[2].valid()) {
			g = tbl.get_or(2, g);
		}
		if (tbl[3].valid()) {
			b = tbl.get_or(3, b);
		}
		if (tbl[4].valid()) {
			a = tbl.get_or(4, a);
		}
		return wxColor(r, g, b, a);
	}

	return fallback;
}

void LuaOverlayManager::clear() {
	mapOverlays.clear();
	mapOverlayShows.clear();
	mapOverlayHover = MapOverlayHoverState {};
}

bool LuaOverlayManager::addMapOverlay(const std::string &id, sol::table options) {
	if (id.empty()) {
		return false;
	}

	MapOverlay overlay;
	overlay.id = id;
	overlay.enabled = options.get_or(std::string("enabled"), true);
	overlay.order = options.get_or(std::string("order"), 0);
	if (options["ondraw"].valid()) {
		overlay.ondraw = options["ondraw"];
	}
	if (options["onhover"].valid()) {
		overlay.onhover = options["onhover"];
	}
	for (const auto &showItem : mapOverlayShows) {
		if (showItem.overlayId == id) {
			overlay.enabled = showItem.enabled;
			break;
		}
	}

	for (auto &existing : mapOverlays) {
		if (existing.id == id) {
			existing = overlay;
			return true;
		}
	}

	mapOverlays.push_back(overlay);
	return true;
}

bool LuaOverlayManager::removeMapOverlay(std::string_view id) {
	for (auto it = mapOverlays.begin(); it != mapOverlays.end(); ++it) {
		if (it->id == id) {
			mapOverlays.erase(it);
			return true;
		}
	}
	return false;
}

bool LuaOverlayManager::setMapOverlayEnabled(std::string_view id, bool enabled) {
	auto it = std::find_if(mapOverlays.begin(), mapOverlays.end(), [&](const MapOverlay &o) { return o.id == id; });
	if (it == mapOverlays.end()) {
		return false;
	}
	it->enabled = enabled;
	for (auto &showItem : mapOverlayShows) {
		if (showItem.overlayId == id) {
			showItem.enabled = enabled;
		}
	}
	return true;
}

bool LuaOverlayManager::registerMapOverlayShow(const std::string &label, const std::string &overlayId, bool enabled, sol::function ontoggle) {
	if (label.empty() || overlayId.empty()) {
		return false;
	}

	auto refreshMenus = []() {
		if (g_gui.root) {
			g_gui.UpdateMenubar();
		}
	};

	for (auto &item : mapOverlayShows) {
		if (item.overlayId == overlayId || item.label == label) {
			item.label = label;
			item.overlayId = overlayId;
			item.enabled = enabled;
			if (ontoggle.valid()) {
				item.ontoggle = ontoggle;
			}
			setMapOverlayEnabled(overlayId, enabled);
			refreshMenus();
			return true;
		}
	}

	MapOverlayShowItem item;
	item.label = label;
	item.overlayId = overlayId;
	item.enabled = enabled;
	item.ontoggle = ontoggle;
	mapOverlayShows.push_back(item);
	setMapOverlayEnabled(overlayId, enabled);
	refreshMenus();
	return true;
}

bool LuaOverlayManager::setMapOverlayShowEnabled(const std::string &overlayId, bool enabled) {
	bool updated = setMapOverlayEnabled(overlayId, enabled);
	for (auto &item : mapOverlayShows) {
		if (item.overlayId == overlayId) {
			item.enabled = enabled;
			if (item.ontoggle.valid()) {
				try {
					item.ontoggle(enabled);
				} catch (const sol::error &e) {
					log("Overlay show '" + item.label + "' error: " + std::string(e.what()), true);
				}
			}
			if (g_gui.root) {
				g_gui.UpdateMenubar();
			}
			return updated;
		}
	}
	return updated;
}

bool LuaOverlayManager::isMapOverlayEnabled(std::string_view id) const {
	for (const auto &overlay : mapOverlays) {
		if (overlay.id == id) {
			return overlay.enabled;
		}
	}
	return false;
}

static sol::table getOptsTable(sol::variadic_args va) {
	for (size_t i = 0; i < va.size(); ++i) {
		if (va[i].is<sol::table>()) {
			sol::table t = va[i].as<sol::table>();
			if (t["view"].valid() && t["rect"].valid()) {
				continue;
			}
			return t;
		}
	}
	return sol::table();
}

void LuaOverlayManager::collectMapOverlayCommands(LuaEngine &engine, const MapViewInfo &view, std::vector<MapOverlayCommand> &out) {
	out.clear();

	sol::state &lua = engine.getState();
	sol::table ctx = lua.create_table();
	sol::table viewTable = lua.create_table();
	viewTable["x1"] = view.start_x;
	viewTable["y1"] = view.start_y;
	viewTable["x2"] = view.end_x;
	viewTable["y2"] = view.end_y;
	viewTable["z"] = view.floor;
	viewTable["zoom"] = view.zoom;
	viewTable["screenWidth"] = view.screen_width;
	viewTable["screenHeight"] = view.screen_height;
	ctx["view"] = viewTable;

	ctx["rect"] = [&](sol::variadic_args va) {
		sol::table opts = getOptsTable(va);
		if (!opts.valid()) {
			return;
		}
		MapOverlayCommand cmd;
		cmd.type = MapOverlayCommand::Type::Rect;
		cmd.screen_space = opts.get_or(std::string("screen"), false);
		cmd.filled = opts.get_or(std::string("filled"), true);
		cmd.width = opts.get_or(std::string("width"), 1);
		std::string style = opts.get_or(std::string("style"), std::string("solid"));
		cmd.dashed = (style == "dotted" || style == "dashed");
		cmd.x = opts.get_or(std::string("x"), 0);
		cmd.y = opts.get_or(std::string("y"), 0);
		cmd.z = opts.get_or(std::string("z"), view.floor);
		cmd.w = opts.get_or(std::string("w"), 1);
		cmd.h = opts.get_or(std::string("h"), 1);
		cmd.color = parseColor(opts["color"], wxColor(255, 255, 255, 128));
		out.push_back(cmd);
	};

	ctx["line"] = [&](sol::variadic_args va) {
		sol::table opts = getOptsTable(va);
		if (!opts.valid()) {
			return;
		}
		MapOverlayCommand cmd;
		cmd.type = MapOverlayCommand::Type::Line;
		cmd.screen_space = opts.get_or(std::string("screen"), false);
		cmd.width = opts.get_or(std::string("width"), 1);
		std::string style = opts.get_or(std::string("style"), std::string("solid"));
		cmd.dashed = (style == "dotted" || style == "dashed");
		cmd.x = opts.get_or(std::string("x1"), 0);
		cmd.y = opts.get_or(std::string("y1"), 0);
		cmd.z = opts.get_or(std::string("z1"), view.floor);
		cmd.x2 = opts.get_or(std::string("x2"), 0);
		cmd.y2 = opts.get_or(std::string("y2"), 0);
		cmd.z2 = opts.get_or(std::string("z2"), view.floor);
		cmd.color = parseColor(opts["color"], wxColor(255, 255, 255, 200));
		out.push_back(cmd);
	};

	ctx["text"] = [&](sol::variadic_args va) {
		sol::table opts = getOptsTable(va);
		if (!opts.valid()) {
			return;
		}
		MapOverlayCommand cmd;
		cmd.type = MapOverlayCommand::Type::Text;
		cmd.screen_space = opts.get_or(std::string("screen"), false);
		cmd.x = opts.get_or(std::string("x"), 0);
		cmd.y = opts.get_or(std::string("y"), 0);
		cmd.z = opts.get_or(std::string("z"), view.floor);
		cmd.text = opts.get_or(std::string("text"), std::string());
		cmd.color = parseColor(opts["color"], wxColor(255, 255, 255, 255));
		if (!cmd.text.empty()) {
			out.push_back(cmd);
		}
	};

	ctx["image"] = [&](sol::variadic_args va) {
		sol::table opts = getOptsTable(va);
		if (!opts.valid() || !opts["image"].valid() || !opts["image"].is<LuaAPI::LuaImage>()) {
			return;
		}
		LuaAPI::LuaImage img = opts["image"].get<LuaAPI::LuaImage>();
		if (!img.isSpriteSource()) {
			return;
		}
		MapOverlayCommand cmd;
		cmd.type = MapOverlayCommand::Type::Sprite;
		cmd.sprite_id = img.getSpriteId();
		cmd.screen_space = opts.get_or(std::string("screen"), false);
		cmd.x = opts.get_or(std::string("x"), 0);
		cmd.y = opts.get_or(std::string("y"), 0);
		cmd.z = opts.get_or(std::string("z"), view.floor);
		double opacity = opts.get_or(std::string("opacity"), 1.0);
		cmd.color = wxColor(255, 255, 255, static_cast<uint8_t>(opacity * 255));
		out.push_back(cmd);
	};

	std::vector<MapOverlay> sorted = mapOverlays;
	std::sort(sorted.begin(), sorted.end(), [](const MapOverlay &a, const MapOverlay &b) {
		return std::tie(a.order, a.id) < std::tie(b.order, b.id);
	});

	for (const auto &overlay : sorted) {
		if (!overlay.enabled || !overlay.ondraw.valid()) {
			continue;
		}
		try {
			overlay.ondraw(ctx);
		} catch (const sol::error &e) {
			log("Overlay '" + overlay.id + "' error: " + std::string(e.what()), true);
		}
	}
}

static bool parseHoverHighlight(const sol::table &table, MapOverlayCommand &highlight, int map_x, int map_y, int map_z) {
	if (!table["highlight"].valid()) {
		return false;
	}
	sol::table h = table["highlight"];
	highlight.type = MapOverlayCommand::Type::Rect;
	highlight.x = h.get_or(std::string("x"), map_x);
	highlight.y = h.get_or(std::string("y"), map_y);
	highlight.z = h.get_or(std::string("z"), map_z);
	highlight.w = h.get_or(std::string("w"), 1);
	highlight.h = h.get_or(std::string("h"), 1);
	highlight.filled = h.get_or(std::string("filled"), false);
	highlight.width = h.get_or(std::string("width"), 1);
	highlight.color = parseColor(h["color"], wxColor(255, 255, 0, 128));
	return true;
}

static bool parseHoverTooltip(const sol::table &table, MapOverlayTooltip &tooltip, int map_x, int map_y, int map_z) {
	if (!table["tooltip"].valid()) {
		return false;
	}
	if (table["tooltip"].is<std::string>()) {
		tooltip.text = table["tooltip"].get<std::string>();
		tooltip.color = wxColor(255, 255, 255, 255);
	} else if (table["tooltip"].is<sol::table>()) {
		sol::table t = table["tooltip"];
		tooltip.text = t.get_or(std::string("text"), std::string());
		tooltip.color = parseColor(t["color"], wxColor(255, 255, 255, 255));
	}
	if (tooltip.text.empty()) {
		return false;
	}
	tooltip.x = map_x;
	tooltip.y = map_y;
	tooltip.z = map_z;
	return true;
}

void LuaOverlayManager::updateMapOverlayHover(LuaEngine &engine, int map_x, int map_y, int map_z, int screen_x, int screen_y, Tile* tile, Item* topItem) {
	mapOverlayHover = MapOverlayHoverState {};
	if (map_x < 0 || map_y < 0) {
		return;
	}

	bool any = false;
	sol::state &lua = engine.getState();
	sol::table info = lua.create_table();
	sol::table pos = lua.create_table();
	pos["x"] = map_x;
	pos["y"] = map_y;
	pos["z"] = map_z;
	info["pos"] = pos;
	info["screen"] = lua.create_table_with("x", screen_x, "y", screen_y);
	if (tile) {
		info["tile"] = tile;
	}
	if (topItem) {
		info["topItem"] = topItem;
	}

	for (const auto &overlay : mapOverlays) {
		if (!overlay.enabled || !overlay.onhover.valid()) {
			continue;
		}

		try {
			sol::object result = overlay.onhover(info);
			if (!result.valid() || result.is<sol::nil_t>()) {
				continue;
			}

			MapOverlayCommand highlight;
			bool hasHighlight = false;
			MapOverlayTooltip tooltip;
			bool hasTooltip = false;

			if (result.is<std::string>()) {
				tooltip.text = result.as<std::string>();
				tooltip.color = wxColor(255, 255, 255, 255);
				tooltip.x = map_x;
				tooltip.y = map_y;
				tooltip.z = map_z;
				hasTooltip = true;
			} else if (result.is<sol::table>()) {
				sol::table table = result.as<sol::table>();
				hasHighlight = parseHoverHighlight(table, highlight, map_x, map_y, map_z);
				hasTooltip = parseHoverTooltip(table, tooltip, map_x, map_y, map_z);
			}

			if (hasHighlight) {
				mapOverlayHover.commands.push_back(highlight);
				any = true;
			}
			if (hasTooltip) {
				mapOverlayHover.tooltips.push_back(tooltip);
				any = true;
			}
		} catch (const sol::error &e) {
			log("Overlay hover '" + overlay.id + "' error: " + std::string(e.what()), true);
		}
	}

	mapOverlayHover.valid = any;
	mapOverlayHover.x = map_x;
	mapOverlayHover.y = map_y;
	mapOverlayHover.z = map_z;
}

std::string LuaScriptManager::getScriptsDirectory() const {
	// Get the executable directory
	wxFileName execPath(wxStandardPaths::Get().GetExecutablePath());
	wxString scriptsDir = execPath.GetPath() + wxFileName::GetPathSeparator() + "scripts";

	return scriptsDir.ToStdString();
}

void LuaScriptManager::discoverScripts() {
	scripts.clear();
	clearAllCallbacks();

	std::string scriptsDir = getScriptsDirectory();
	scanDirectory(scriptsDir);

	// Sort scripts by display name
	std::sort(scripts.begin(), scripts.end(), [](const std::unique_ptr<LuaScript> &a, const std::unique_ptr<LuaScript> &b) {
		return a->getDisplayName() < b->getDisplayName();
	});

	runAutoScripts();
}

void LuaScriptManager::scanDirectory(const std::string &directory) {
	wxDir dir(directory);
	if (!dir.IsOpened()) {
		return;
	}

	const std::string sep(1, wxFileName::GetPathSeparator());

	wxString name;

	// First, scan for directories with manifest.lua (package scripts)
	bool cont = dir.GetFirst(&name, wxEmptyString, wxDIR_DIRS);
	while (cont) {
		std::string subdir = directory + sep + name.ToStdString();
		std::string manifestPath = subdir + sep + "manifest.lua";

		// Check if manifest.lua exists
		if (wxFileExists(manifestPath)) {
			auto script = std::make_unique<LuaScript>(subdir, true);
			scripts.push_back(std::move(script));
		}

		cont = dir.GetNext(&name);
	}

	// Then, scan for standalone .lua files
	cont = dir.GetFirst(&name, "*.lua", wxDIR_FILES);
	while (cont) {
		if (!name.IsEmpty() && name[0] == '_') {
			cont = dir.GetNext(&name);
			continue;
		}

		std::string filepath = directory + sep + name.ToStdString();

		auto script = std::make_unique<LuaScript>(filepath);
		scripts.push_back(std::move(script));

		cont = dir.GetNext(&name);
	}
}

void LuaScriptManager::reloadScripts() {
	discoverScripts();
}

void LuaScriptManager::runAutoScripts() {
	for (const auto &script : scripts) {
		if (!script->isEnabled() || !script->shouldAutoRun()) {
			continue;
		}
		if (!executeScript(script.get())) {
			logOutput("AutoRun '" + script->getDisplayName() + "' error: " + lastError, true);
		}
	}
}

bool LuaScriptManager::executeScript(const std::string &filepath) {
	if (!initialized) {
		lastError = "Script manager not initialized";
		return false;
	}

	bool result = engine.executeFile(filepath);
	if (!result) {
		lastError = engine.getLastError();
	}
	return result;
}

bool LuaScriptManager::executeScript(const LuaScript* script) {
	if (!script) {
		lastError = "Invalid script";
		return false;
	}

	if (!script->isEnabled()) {
		lastError = "Script is disabled";
		return false;
	}

	return executeScript(script->getFilePath());
}

bool LuaScriptManager::executeScript(size_t index, std::string &errorOut) {
	if (index >= scripts.size()) {
		errorOut = "Invalid script index";
		return false;
	}

	const LuaScript* script = scripts[index].get();
	if (!script->isEnabled()) {
		errorOut = "Script is disabled";
		return false;
	}

	bool result = executeScript(script->getFilePath());
	if (!result) {
		errorOut = lastError;
	}
	return result;
}

void LuaScriptManager::setScriptEnabled(size_t index, bool enabled) {
	if (index < scripts.size()) {
		scripts[index]->setEnabled(enabled);
	}
}

bool LuaScriptManager::isScriptEnabled(size_t index) const {
	if (index < scripts.size()) {
		return scripts[index]->isEnabled();
	}
	return false;
}

void LuaScriptManager::setOutputCallback(LuaOutputCallback callback) {
	{
		std::scoped_lock lock(outputCallbackMutex);
		outputCallback = std::move(callback);
	}

	// Also set up the engine's print callback
	engine.setPrintCallback([this](const std::string &msg) {
		logOutput(msg, false);
	});
}

void LuaScriptManager::logOutput(const std::string &message, bool isError) {
	LuaOutputCallback cb;
	{
		std::scoped_lock lock(outputCallbackMutex);
		cb = outputCallback;
	}
	if (cb) {
		cb(message, isError);
	}
}

LuaScript* LuaScriptManager::getScript(std::string_view filepath) {
	for (const auto &script : scripts) {
		if (script->getFilePath() == filepath) {
			return script.get();
		}
	}
	return nullptr;
}

void LuaScriptManager::openScriptsFolder() {
	std::string scriptsDir = getScriptsDirectory();

	// Create directory if it doesn't exist
	wxFileName::Mkdir(scriptsDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

#ifdef _WIN32
	ShellExecuteA(nullptr, "explore", scriptsDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
	wxLaunchDefaultApplication(wxString(scriptsDir));
#endif
}
