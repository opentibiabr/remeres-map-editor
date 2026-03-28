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
#include "lua_api_app.h"
#include "lua_script_manager.h"
#include "../gui.h"
#include "../editor.h"
#include "../map.h"
#include "../brush.h"
#include "../action.h"
#include "../tile.h"
#include "../selection.h"
#include "../house.h"
#include "../items.h"
#include "../raw_brush.h"

#include <wx/msgdlg.h>
#include <wx/app.h>
#include <fstream>
#include <filesystem>
#include <unordered_set>
#include <unordered_map>
#include <memory>
#include <cstdio>
#include <print>
#include <thread>
#include <chrono>

namespace LuaAPI {

	// ============================================================================
	// Transaction System for Undo/Redo Support
	// ============================================================================

	// Transaction implementation

	// Helper to sync map metadata when swapping tiles
	static void updateTileMetadata(Editor* editor, Tile* tile, bool adding) {
		if (!editor || !tile) {
			return;
		}
		Map &map = editor->getMap();

		if (adding) {
			if (tile->spawnMonster) {
				map.spawnsMonster.addSpawnMonster(tile);
			}
			if (tile->getHouseID()) {
				House* h = map.houses.getHouse(tile->getHouseID());
				if (h) {
					h->addTile(tile);
				}
			}
		} else {
			if (tile->spawnMonster) {
				map.spawnsMonster.removeSpawnMonster(tile);
			}
			if (tile->getHouseID()) {
				House* h = map.houses.getHouse(tile->getHouseID());
				if (h) {
					h->removeTile(tile);
				}
			}
			// Also clean up selection if removing
			if (tile->isSelected()) {
				editor->getSelection().start(Selection::INTERNAL);
				editor->getSelection().removeInternal(tile);
				editor->getSelection().finish(Selection::INTERNAL);
			}
		}
	}

	class LuaTransaction {
		bool active = false;
		Editor* editor = nullptr;
		std::unique_ptr<BatchAction> batch;
		std::unique_ptr<Action> action;
		std::unordered_map<uint64_t, std::unique_ptr<Tile>> originalTiles;

		uint64_t positionKey(const Position &pos) const {
			return (static_cast<uint64_t>(pos.x) << 32) | (static_cast<uint64_t>(pos.y) << 16) | static_cast<uint64_t>(pos.z);
		}

	public:
		static LuaTransaction &getInstance() {
			static LuaTransaction instance;
			return instance;
		}

		LuaTransaction() = default;

		void begin(Editor* ed) {
			if (active) {
				throw sol::error("Transaction already in progress");
			}

			editor = ed;
			if (!editor) {
				throw sol::error("No editor or action queue available");
			}

			active = true;
			batch.reset(editor->getActionQueue()->createBatch(ACTION_LUA_SCRIPT));
			action.reset(editor->getActionQueue()->createAction(ACTION_LUA_SCRIPT));
			originalTiles.clear();
		}

		void commit() {
			if (!active) {
				return;
			}

			// Process each modified tile
			for (auto &[key, originalTile] : originalTiles) {
				Position pos = originalTile->getPosition();

				Tile* modifiedTile = editor->getMap().getTile(pos);
				if (modifiedTile) {
					Tile* modifiedCopy = modifiedTile->deepCopy(editor->getMap());

					std::unique_ptr<Tile> swappedOut(editor->getMap().swapTile(pos, originalTile.release()));

					updateTileMetadata(editor, swappedOut.get(), false);
					updateTileMetadata(editor, editor->getMap().getTile(pos), true);

					action->addChange(new Change(modifiedCopy));
				}
			}

			// Clear - ownership has been transferred
			originalTiles.clear();

			if (!action->empty()) {
				batch->addAndCommitAction(action.release());
				editor->addBatch(batch.release());
				editor->getMap().doChange();
				g_gui.RefreshView();
			}

			cleanup();
		}

		void rollback() {
			if (!active) {
				return;
			}

			for (auto &[key, originalTile] : originalTiles) {
				if (originalTile) {
					Position pos = originalTile->getPosition();
					std::unique_ptr<Tile> modifiedTile(editor->getMap().swapTile(pos, originalTile.release()));

					updateTileMetadata(editor, modifiedTile.get(), false);
					updateTileMetadata(editor, editor->getMap().getTile(pos), true);
				}
			}
			originalTiles.clear();

			cleanup();
		}

		void markTileModified(Tile* tile) {
			if (!active || !tile) {
				return;
			}

			Position pos = tile->getPosition();
			uint64_t key = positionKey(pos);

			// Only snapshot the tile once per transaction (first time it's modified)
			if (!originalTiles.contains(key)) {
				originalTiles[key].reset(tile->deepCopy(editor->getMap()));
			}
		}

		bool isActive() const {
			return active;
		}
		Editor* getEditor() const {
			return editor;
		}

	private:
		void cleanup() {
			active = false;
			editor = nullptr;
			batch.reset();
			action.reset();
		}
	};

	// Global accessor for tile modification tracking (used by lua_api_tile.cpp)
	void markTileForUndo(Tile* tile) {
		if (LuaTransaction::getInstance().isActive()) {
			LuaTransaction::getInstance().markTileModified(tile);
		}
	}

	// ============================================================================
	// Helper Functions
	// ============================================================================

	struct AlertOptions {
		std::string title = "Script";
		std::string message;
		std::vector<std::string> buttons;
	};

	static AlertOptions parseAlertOptions(sol::this_state ts, sol::object arg) {
		sol::state_view lua(ts);
		AlertOptions opts;

		if (arg.is<std::string>()) {
			opts.message = arg.as<std::string>();
			opts.buttons.emplace_back("OK");
			return opts;
		}

		if (!arg.is<sol::table>()) {
			sol::function tostring = lua["tostring"];
			opts.message = tostring(arg);
			opts.buttons.emplace_back("OK");
			return opts;
		}

		sol::table tbl = arg.as<sol::table>();
		if (tbl["title"].valid()) {
			opts.title = tbl["title"].get<std::string>();
		}
		if (tbl["text"].valid()) {
			opts.message = tbl["text"].get<std::string>();
		}
		if (tbl["buttons"].valid()) {
			sol::table btns = tbl["buttons"];
			for (size_t i = 1; i <= btns.size(); ++i) {
				if (!btns[i].valid()) {
					continue;
				}
				opts.buttons.push_back(btns[i].get<std::string>());
			}
		}
		if (opts.buttons.empty()) {
			opts.buttons.emplace_back("OK");
		}
		return opts;
	}

	static long getDialogStyle(const std::vector<std::string> &buttons) {
		long style = wxCENTRE;
		if (buttons.size() == 1) {
			return style | wxOK;
		}
		if (buttons.size() >= 3) {
			return style | wxYES_NO | wxCANCEL;
		}
		if (buttons.size() != 2) {
			return style | wxOK;
		}

		std::string btn1 = buttons[0];
		std::string btn2 = buttons[1];
		std::transform(btn1.begin(), btn1.end(), btn1.begin(), ::tolower);
		std::transform(btn2.begin(), btn2.end(), btn2.begin(), ::tolower);

		if ((btn1 == "ok" && btn2 == "cancel") || (btn1 == "cancel" && btn2 == "ok")) {
			return style | wxOK | wxCANCEL;
		}
		if ((btn1 == "yes" && btn2 == "no") || (btn1 == "no" && btn2 == "yes")) {
			return style | wxYES_NO;
		}
		return style | wxOK | wxCANCEL;
	}

	static int mapDialogResult(int result, size_t buttonCount) {
		switch (result) {
			case wxID_OK:
			case wxID_YES:
				return 1;
			case wxID_NO:
				return 2;
			case wxID_CANCEL:
				return buttonCount >= 3 ? 3 : 2;
			default:
				return 0;
		}
	}

	static int showAlert(sol::this_state ts, sol::object arg) {
		AlertOptions opts = parseAlertOptions(ts, arg);
		long style = getDialogStyle(opts.buttons);

		wxWindow* parent = g_gui.root;
		wxMessageDialog dlg(parent, wxString(opts.message), wxString(opts.title), style);

		return mapDialogResult(dlg.ShowModal(), opts.buttons.size());
	}

	// Check if a map is currently open
	static bool hasMap() {
		Editor* editor = g_gui.GetCurrentEditor();
		return editor != nullptr;
	}

	// Refresh the map view
	static void refresh() {
		g_gui.RefreshView();
	}

	// Get the current Map object
	static Map* getMap() {
		Editor* editor = g_gui.GetCurrentEditor();
		if (!editor) {
			return nullptr;
		}
		return &editor->getMap();
	}

	// Get the current Selection object
	static Selection* getSelection() {
		Editor* editor = g_gui.GetCurrentEditor();
		if (!editor) {
			return nullptr;
		}
		return &editor->getSelection();
	}

	static void setClipboard(const std::string &text) {
		if (wxTheClipboard->Open()) {
			wxTheClipboard->SetData(new wxTextDataObject(text));
			wxTheClipboard->Close();
		}
	}

	// Transaction function with undo/redo support
	static void transaction(const std::string &name, sol::function func) {
		Editor* editor = g_gui.GetCurrentEditor();
		if (!editor) {
			throw sol::error("No map open");
		}

		LuaTransaction &trans = LuaTransaction::getInstance();

		try {
			trans.begin(editor);
			func();
			trans.commit();
			g_gui.RefreshView();
		} catch (const sol::error &) {
			trans.rollback();
			throw; // Re-throw to show error to user
		} catch (const std::exception &e) {
			trans.rollback();
			throw sol::error(std::string("Transaction failed: ") + e.what());
		}
	}

	static sol::object getBorders(sol::this_state ts) {
		sol::state_view lua(ts);
		sol::table bordersTable = lua.create_table();
		return bordersTable;
	}

	static std::string getDataDirectory() {
		return GUI::GetDataDirectory().ToStdString();
	}

	static bool saveStorageData(const std::string &path, sol::this_state ts2, sol::object first, sol::object second) {
		sol::state_view lua(ts2);
		std::string content;

		sol::object data = (second.valid() && !second.is<sol::nil_t>()) ? second : first;
		if (!data.valid() || data.is<sol::nil_t>()) {
			return false;
		}

		if (data.is<std::string>()) {
			content = data.as<std::string>();
		} else {
			sol::table json = lua["json"];
			if (!json.valid() || !json["encode_pretty"].valid()) {
				return false;
			}
			try {
				sol::function encode = json["encode_pretty"];
				sol::protected_function_result result = encode(data);
				if (!result.valid()) {
					return false;
				}
				content = result.get<std::string>();
			} catch (const sol::error &) {
				return false;
			}
		}

		std::ofstream file(path, std::ios::trunc);
		if (!file.is_open()) {
			return false;
		}
		file << content;
		file.close();
		return true;
	}

	static sol::table storageForScript(sol::this_state ts, const std::string &name) {
		sol::state_view lua(ts);
		sol::table storage = lua.create_table();

		std::string scriptDir = ".";
		if (lua["SCRIPT_DIR"].valid()) {
			scriptDir = lua["SCRIPT_DIR"];
		}

		std::string filename = name;

		// Security check: Prevent path traversal and absolute paths
		if (filename.contains("..") || std::filesystem::path(filename).is_absolute()) {
			std::println("[Lua Security] Blocked unsafe path in app.storage: {}", filename);
			return lua.create_table();
		}

		if (!filename.contains('.')) {
			filename += ".json";
		}
		std::string path = scriptDir + "/" + filename;
		storage["path"] = path;

		storage["load"] = [path](sol::this_state ts2, sol::object) -> sol::object {
			sol::state_view lua(ts2);
			std::ifstream file(path);
			if (!file.is_open()) {
				return sol::make_object(lua, sol::nil);
			}

			std::stringstream buffer;
			buffer << file.rdbuf();
			file.close();

			std::string content = buffer.str();
			if (content.empty()) {
				return sol::make_object(lua, sol::nil);
			}

			sol::table json = lua["json"];
			if (!json.valid() || !json["decode"].valid()) {
				return sol::make_object(lua, sol::nil);
			}

			try {
				sol::function decode = json["decode"];
				sol::protected_function_result result = decode(content);
				if (!result.valid()) {
					return sol::make_object(lua, sol::nil);
				}
				sol::object decoded = result;
				return sol::make_object(lua, decoded);
			} catch (const sol::error &) {
				return sol::make_object(lua, sol::nil);
			}
		};

		storage["save"] = [path](sol::this_state ts2, sol::object first, sol::object second) {
			return saveStorageData(path, ts2, first, second);
		};

		storage["clear"] = [path](sol::object) {
			return std::remove(path.c_str()) == 0;
		};

		return storage;
	}

	// ============================================================================
	// Register App API
	// ============================================================================

	static sol::object appPropertyGetter(sol::this_state ts, sol::table self, std::string key) {
		sol::state_view lua(ts);

		if (key == "map") {
			Map* map = getMap();
			if (map) {
				return sol::make_object(lua, map);
			}
			return sol::nil;
		}
		if (key == "selection") {
			Selection* sel = getSelection();
			if (sel) {
				return sol::make_object(lua, sel);
			}
			return sol::nil;
		}
		if (key == "borders") {
			return getBorders(ts);
		}
		if (key == "editor") {
			Editor* editor = g_gui.GetCurrentEditor();
			if (editor) {
				return sol::make_object(lua, editor);
			}
			return sol::nil;
		}
		if (key == "brush") {
			Brush* b = g_gui.GetCurrentBrush();
			if (b) {
				return sol::make_object(lua, b);
			}
			return sol::nil;
		}
		if (key == "brushSize") {
			return sol::make_object(lua, g_gui.GetBrushSize());
		}
		if (key == "brushShape") {
			return sol::make_object(lua, g_gui.GetBrushShape() == BRUSHSHAPE_CIRCLE ? "circle" : "square");
		}
		if (key == "brushVariation") {
			return sol::make_object(lua, g_gui.GetBrushVariation());
		}
		if (key == "spawnTime") {
			return sol::make_object(lua, 0);
		}
		return sol::nil;
	}

	static void appPropertySetter(sol::this_state ts, sol::table self, std::string key, sol::object value) {
		if (key == "brushSize") {
			if (value.is<int>()) {
				g_gui.SetBrushSize(value.as<int>());
			}
		} else if (key == "brushShape") {
			if (value.is<std::string>()) {
				std::string s = value.as<std::string>();
				if (s == "circle") {
					g_gui.SetBrushShape(BRUSHSHAPE_CIRCLE);
				} else if (s == "square") {
					g_gui.SetBrushShape(BRUSHSHAPE_SQUARE);
				}
			}
		} else if (key == "brushVariation") {
			if (value.is<int>()) {
				g_gui.SetBrushVariation(value.as<int>());
			}
		}
	}

	static bool handleRegisterShow(sol::variadic_args va) {
		std::string label;
		std::string overlayId;
		bool enabled = true;
		sol::function ontoggle;

		if (va.size() == 2 && va[0].is<std::string>() && va[1].is<std::string>()) {
			label = va[0].as<std::string>();
			overlayId = va[1].as<std::string>();
		} else if (va.size() >= 3) {
			if (va[0].is<sol::table>()) {
				if (va[1].is<std::string>()) {
					label = va[1].as<std::string>();
				}
				if (va[2].is<std::string>()) {
					overlayId = va[2].as<std::string>();
				}
			} else if (va[0].is<std::string>() && va[1].is<std::string>()) {
				label = va[0].as<std::string>();
				overlayId = va[1].as<std::string>();
			}
		}

		if (va.size() >= 3 && va[va.size() - 1].is<sol::table>()) {
			sol::table opts = va[va.size() - 1].as<sol::table>();
			enabled = opts.get_or(std::string("enabled"), enabled);
			if (opts["ontoggle"].valid()) {
				ontoggle = opts["ontoggle"];
			}
		} else if (va.size() >= 3 && va[va.size() - 1].is<bool>()) {
			enabled = va[va.size() - 1].as<bool>();
		}

		if (label.empty() || overlayId.empty()) {
			return false;
		}

		return g_luaScripts.getOverlayManager().registerMapOverlayShow(label, overlayId, enabled, ontoggle);
	}

	static void setupMapView(sol::table &app) {
		sol::state_view lua(app.lua_state());
		sol::table mapView = lua.create_table();
		mapView["addOverlay"] = [](sol::variadic_args va) -> bool {
			if (va.size() == 2 && va[0].is<std::string>() && va[1].is<sol::table>()) {
				return g_luaScripts.getOverlayManager().addMapOverlay(va[0].as<std::string>(), va[1].as<sol::table>());
			}
			if (va.size() == 3 && va[1].is<std::string>() && va[2].is<sol::table>()) {
				return g_luaScripts.getOverlayManager().addMapOverlay(va[1].as<std::string>(), va[2].as<sol::table>());
			}
			return false;
		};
		mapView["removeOverlay"] = [](sol::variadic_args va) -> bool {
			if (va.size() == 1 && va[0].is<std::string>()) {
				return g_luaScripts.getOverlayManager().removeMapOverlay(va[0].as<std::string>());
			}
			if (va.size() == 2 && va[1].is<std::string>()) {
				return g_luaScripts.getOverlayManager().removeMapOverlay(va[1].as<std::string>());
			}
			return false;
		};
		mapView["setEnabled"] = [](sol::variadic_args va) -> bool {
			if (va.size() == 2 && va[0].is<std::string>() && va[1].is<bool>()) {
				return g_luaScripts.getOverlayManager().setMapOverlayEnabled(va[0].as<std::string>(), va[1].as<bool>());
			}
			if (va.size() == 3 && va[1].is<std::string>() && va[2].is<bool>()) {
				return g_luaScripts.getOverlayManager().setMapOverlayEnabled(va[1].as<std::string>(), va[2].as<bool>());
			}
			return false;
		};
		mapView["registerShow"] = handleRegisterShow;
		app["mapView"] = mapView;
	}

	static void editorGoToHistory(Editor* editor, int targetIndex) {
		if (!editor || !editor->getActionQueue()) {
			return;
		}

		int current = editor->getActionQueue()->getCurrentIndex();
		int target = targetIndex;

		if (target < 0) {
			target = 0;
		}
		if (target > (int)editor->getActionQueue()->size()) {
			target = (int)editor->getActionQueue()->size();
		}

		int diff = target - current;

		if (diff > 0) {
			for (int i = 0; i < diff; ++i) {
				if (editor->canRedo()) {
					editor->redo();
				}
			}
		} else if (diff < 0) {
			for (int i = 0; i < -diff; ++i) {
				if (editor->canUndo()) {
					editor->undo();
				}
			}
		}
		g_gui.RefreshView();
	}

	static void registerEditorUsertype(sol::state &lua) {
		lua.new_usertype<Editor>(
			"Editor",
			sol::no_constructor,

			"undo", [](Editor* editor) {  
				if (editor && editor->canUndo()) {  
					editor->undo();  
					g_gui.RefreshView();  
				} },
			"redo", [](Editor* editor) {  
				if (editor && editor->canRedo()) {  
					editor->redo();  
					g_gui.RefreshView();  
				} },
			"canUndo", [](Editor* editor) -> bool { return editor && editor->canUndo(); },
			"canRedo", [](Editor* editor) -> bool { return editor && editor->canRedo(); },

			"historyIndex", sol::property([](Editor* editor) -> int {
				if (editor && editor->getActionQueue()) {
					return (int)editor->getActionQueue()->getCurrentIndex();
				}
				return 0;
			}),
			"historySize", sol::property([](Editor* editor) -> int {
				if (editor && editor->getActionQueue()) {
					return (int)editor->getActionQueue()->size();
				}
				return 0;
			}),

			"getHistory", [](Editor* editor, sol::this_state ts) -> sol::table {  
				sol::state_view lua(ts);  
				sol::table history = lua.create_table();  
  
				if (editor && editor->getActionQueue()) {  
					size_t size = editor->getActionQueue()->size();  
					for (size_t i = 0; i < size; ++i) {  
						sol::table input = lua.create_table();  
						input["index"] = (int)(i + 1);  
						const BatchAction* ba = editor->getActionQueue()->getAction(i);  
						input["name"] = ba ? ba->getLabel().ToStdString() : std::string("");  
						history[i + 1] = input;  
					}  
				}  
				return history; },

			"goToHistory", editorGoToHistory
		);
	}

	void registerApp(sol::state &lua) {
		sol::table app = lua.create_named_table("app");

		app["version"] = __RME_VERSION__;
		app["apiVersion"] = 1;

		app["alert"] = showAlert;
		app["hasMap"] = hasMap;
		app["refresh"] = refresh;
		app["transaction"] = transaction;
		app["setClipboard"] = setClipboard;
		app["getDataDirectory"] = getDataDirectory;
		app["addContextMenu"] = [](const std::string &label, sol::function callback) {
			g_luaScripts.registerContextMenuItem(label, callback);
		};
		app["selectRaw"] = [](int itemId) {
			if (g_items[itemId].id != 0) {
				ItemType &it = g_items[itemId];
				if (it.raw_brush) {
					g_gui.SelectBrush(it.raw_brush, TILESET_RAW);
				}
			}
		};

		app["setCameraPosition"] = [](int x, int y, int z) {
			g_gui.SetScreenCenterPosition(Position(x, y, z));
		};
		app["storage"] = storageForScript;

		app["yield"] = []() {
			if (wxTheApp) {
				wxTheApp->Yield(true);
			}
		};

		app["sleep"] = [](int milliseconds) {
			if (milliseconds > 0 && milliseconds <= 10000) {
				std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
			}
		};

		app["getTime"] = []() -> long {
			return g_gui.gfx.getElapsedTime();
		};

		sol::table events = lua.create_table();
		events["on"] = [](sol::this_state ts, sol::table self, const std::string &eventName, sol::function callback) -> int {
			return g_luaScripts.addEventListener(eventName, callback);
		};
		events["off"] = [](sol::this_state ts, sol::table self, int listenerId) {
			return g_luaScripts.removeEventListener(listenerId);
		};
		app["events"] = events;

		sol::table mt = lua.create_table();
		mt[sol::meta_function::index] = appPropertyGetter;
		mt[sol::meta_function::new_index] = appPropertySetter;

		sol::table keyboard = lua.create_table();
		keyboard["isCtrlDown"] = []() -> bool { return wxGetKeyState(WXK_CONTROL); };
		keyboard["isShiftDown"] = []() -> bool { return wxGetKeyState(WXK_SHIFT); };
		keyboard["isAltDown"] = []() -> bool { return wxGetKeyState(WXK_ALT); };
		app["keyboard"] = keyboard;

		app["copy"] = []() { g_gui.DoCopy(); };
		app["cut"] = []() { g_gui.DoCut(); };
		app["paste"] = []() { g_gui.DoPaste(); };

		setupMapView(app);

		app[sol::metatable_key] = mt;

		registerEditorUsertype(lua);
	}

} // namespace LuaAPI
