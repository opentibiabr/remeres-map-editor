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

#include "gui.h"
#include "main_menubar.h"

#include "editor.h"
#include "brush.h"
#include "map.h"
#include "sprites.h"
#include "materials.h"
#include "doodad_brush.h"
#include "spawn_monster_brush.h"

#include "common_windows.h"
#include "result_window.h"
#include "minimap_window.h"
#include "palette_window.h"
#include "map_display.h"
#include "application.h"
#include "welcome_dialog.h"
#include "spawn_npc_brush.h"
#include "actions_history_window.h"

#include "live_client.h"
#include "live_tab.h"
#include "live_server.h"

#ifdef __WXOSX__
	#include <AGL/agl.h>
#endif

const wxEventType EVT_UPDATE_MENUS = wxNewEventType();
const wxEventType EVT_UPDATE_ACTIONS = wxNewEventType();

// Global GUI instance
GUI g_gui;

GUI::~GUI() {
	delete doodadBufferMap;
	delete g_gui.auiManager;
	delete OGLContext;
}

wxGLContext* GUI::GetGLContext(wxGLCanvas* win) {
	if (OGLContext == nullptr) {
#ifdef __WXOSX__
		/*
		wxGLContext(AGLPixelFormat fmt, wxGLCanvas *win,
					const wxPalette& WXUNUSED(palette),
					const wxGLContext *other
					);
		*/
		OGLContext = new wxGLContext(win, nullptr);
#else
		OGLContext = newd wxGLContext(win);
#endif
	}

	return OGLContext;
}

wxString GUI::GetDataDirectory() {
	const auto &configDataDirectory = g_settings.getString(Config::DATA_DIRECTORY);
	if (!configDataDirectory.empty()) {
		FileName dir;
		dir.Assign(wxstr(configDataDirectory));
		if (dir.DirExists()) {
			return dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
		}
	}

	// Silently reset directory
	FileName executableDirectory = GetExecFileName();
	executableDirectory.AppendDir("data");
	return executableDirectory.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
}

FileName GUI::GetExecFileName() {
	// Silently reset directory
	FileName executableDirectory;
	try {
		executableDirectory = dynamic_cast<wxStandardPaths &>(wxStandardPaths::Get()).GetExecutablePath();
	} catch (const std::bad_cast &) {
		wxLogError("Could not fetch executable directory.");
	}

	return executableDirectory;
}

wxString GUI::GetExecDirectory() {
	return GetExecFileName().GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
}

wxString GUI::GetLocalDataDirectory() {
	if (g_settings.getInteger(Config::INDIRECTORY_INSTALLATION)) {
		FileName dir = GetDataDirectory();
		dir.AppendDir("user");
		dir.AppendDir("data");
		dir.Mkdir(0755, wxPATH_MKDIR_FULL);
		return dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
		;
	} else {
		FileName dir = dynamic_cast<wxStandardPaths &>(wxStandardPaths::Get()).GetUserDataDir();
#ifdef __WINDOWS__
		dir.AppendDir("Remere's Map Editor");
#else
		dir.AppendDir(".rme");
#endif
		dir.AppendDir("data");
		dir.Mkdir(0755, wxPATH_MKDIR_FULL);
		return dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
	}
}

wxString GUI::GetLocalDirectory() {
	if (g_settings.getInteger(Config::INDIRECTORY_INSTALLATION)) {
		FileName dir = GetDataDirectory();
		dir.AppendDir("user");
		dir.Mkdir(0755, wxPATH_MKDIR_FULL);
		return dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
		;
	} else {
		FileName dir = dynamic_cast<wxStandardPaths &>(wxStandardPaths::Get()).GetUserDataDir();
#ifdef __WINDOWS__
		dir.AppendDir("Remere's Map Editor");
#else
		dir.AppendDir(".rme");
#endif
		dir.Mkdir(0755, wxPATH_MKDIR_FULL);
		return dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
	}
}

wxString GUI::GetExtensionsDirectory() {
	const auto &configDataDirectory = g_settings.getString(Config::EXTENSIONS_DIRECTORY);
	if (!configDataDirectory.empty()) {
		FileName dir;
		dir.Assign(wxstr(configDataDirectory));
		if (dir.DirExists()) {
			return dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
		}
	}

	// Silently reset directory
	FileName localDirectory = GetLocalDirectory();
	localDirectory.AppendDir("extensions");
	localDirectory.Mkdir(0755, wxPATH_MKDIR_FULL);
	return localDirectory.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
}

void GUI::discoverDataDirectory(const wxString &existentFile) {
	wxString currentDirectory = wxGetCwd();
	wxString execDirectory = GetExecDirectory();

	wxString possiblePaths[] = {
		execDirectory,
		currentDirectory + "/",

		// these are used usually when running from build directories
		execDirectory + "/../",
		execDirectory + "/../../",
		execDirectory + "/../../../",
		currentDirectory + "/../",
	};

	auto found = false;
	for (const auto &path : possiblePaths) {
		if (wxFileName(wxString::Format("%sdata/%s", path, existentFile)).FileExists()) {
			m_dataDirectory = wxString::Format("%sdata/", path);
			found = true;
			break;
		}
	}

	if (!found) {
		wxLogError(wxString::Format("Could not find data directory.\n"));
	}
}

bool GUI::LoadVersion(ClientVersionID version, wxString &error, wxArrayString &warnings, bool force) {
	if (ClientVersion::get(version) == nullptr) {
		error = "Unsupported client version! (8)";
		return false;
	}

	if (version != loadedVersion || force) {
		if (getLoadedVersion() != nullptr) {
			// There is another version loaded right now, save window layout
			g_gui.SavePerspective();
		}

		// Disable all rendering so the data is not accessed while reloading
		UnnamedRenderingLock();
		DestroyPalettes();
		DestroyMinimap();

		// Destroy the previous version
		UnloadVersion();

		loadedVersion = version;
		if (!getLoadedVersion()->hasValidPaths()) {
			if (!getLoadedVersion()->loadValidPaths()) {
				error = "Couldn't load relevant asset files";
				loadedVersion = CLIENT_VERSION_NONE;
				return false;
			}
		}

		const auto ret = LoadDataFiles(error, warnings);
		if (ret) {
			g_gui.LoadPerspective();
		} else {
			loadedVersion = CLIENT_VERSION_NONE;
		}

		return ret;
	}
	return true;
}

ClientVersionID GUI::GetCurrentVersionID() const {
	if (loadedVersion != CLIENT_VERSION_NONE) {
		return getLoadedVersion()->getID();
	}
	return CLIENT_VERSION_NONE;
}

const ClientVersion &GUI::GetCurrentVersion() const {
	assert(loadedVersion);
	return *getLoadedVersion();
}

void GUI::CycleTab(bool forward) {
	tabbook->CycleTab(forward);
}

bool GUI::LoadDataFiles(wxString &error, wxArrayString &warnings) {
	const auto &dataPath = getLoadedVersion()->getDataPath();
	const auto &clientPath = getLoadedVersion()->getClientPath();
	const auto &extensionPath = GetExtensionsDirectory();

	FileName executableDirectory;
	try {
		executableDirectory = dynamic_cast<wxStandardPaths &>(wxStandardPaths::Get()).GetExecutablePath();
	} catch (std::bad_cast &) {
		error = "Couldn't establish working directory...";
		return false;
	}

	g_gui.gfx.client_version = getLoadedVersion();

	if (!g_gui.gfx.loadOTFI(clientPath.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR), error, warnings)) {
		error = wxString::Format("Couldn't load otfi file: %s", error);
		g_gui.DestroyLoadBar();
		UnloadVersion();
		return false;
	}

	g_gui.CreateLoadBar("Loading asset files");
	g_gui.SetLoadDone(0, "Loading metadata file...");

	const auto &metadataPath = g_gui.gfx.getMetadataFileName();
	if (!g_gui.gfx.loadSpriteMetadata(metadataPath, error, warnings)) {
		error = wxString::Format("Couldn't load metadata: %s", error);
		g_gui.DestroyLoadBar();
		UnloadVersion();
		return false;
	}

	g_gui.SetLoadDone(10, "Loading sprites file...");

	const auto &spritesPath = g_gui.gfx.getSpritesFileName();
	if (!g_gui.gfx.loadSpriteData(spritesPath.GetFullPath(), error, warnings)) {
		error = wxString::Format("Couldn't load sprites: %s", error);
		g_gui.DestroyLoadBar();
		UnloadVersion();
		return false;
	}

	g_gui.SetLoadDone(20, "Loading items.otb file...");
	if (!g_items.loadFromOtb(wxString("data/items/items.otb"), error, warnings)) {
		error = wxString::Format("Couldn't load items.otb: %s", error);
		g_gui.DestroyLoadBar();
		UnloadVersion();
		return false;
	}

	g_gui.SetLoadDone(30, "Loading items.xml ...");
	if (!g_items.loadFromGameXml(wxString("data/items/items.xml"), error, warnings)) {
		warnings.push_back(wxString::Format("Couldn't load items.xml: %s", error));
	}

	g_gui.SetLoadDone(45, "Loading monsters.xml ...");
	if (!g_monsters.loadFromXML(wxString("data/creatures/monsters.xml"), true, error, warnings)) {
		warnings.push_back(wxString::Format("Couldn't load monsters.xml: %s", error));
	}

	g_gui.SetLoadDone(45, "Loading user monsters.xml ...");
	{
		FileName monstersDataPath = getLoadedVersion()->getLocalDataPath();
		monstersDataPath.SetFullName("monsters.xml");
		wxString monstersError;
		wxArrayString monstersWarning;
		g_monsters.loadFromXML(monstersDataPath, false, monstersError, monstersWarning);
	}

	g_gui.SetLoadDone(45, "Loading npcs.xml ...");
	if (!g_npcs.loadFromXML(wxString("data/creatures/npcs.xml"), true, error, warnings)) {
		warnings.push_back(wxString::Format("Couldn't load npcs.xml: %s", error));
	}

	g_gui.SetLoadDone(45, "Loading user npcs.xml ...");
	{
		FileName npcsDataPath = getLoadedVersion()->getLocalDataPath();
		npcsDataPath.SetFullName("npcs.xml");
		wxString npcsError;
		wxArrayString npcWarnings;
		g_npcs.loadFromXML(npcsDataPath, false, npcsError, npcWarnings);
	}

	g_gui.SetLoadDone(50, "Loading materials.xml ...");
	if (!g_materials.loadMaterials(wxString::Format("%smaterials.xml", dataPath.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR)), error, warnings)) {
		warnings.push_back(wxString::Format("Couldn't load materials.xml: %s", error));
	}

	g_gui.SetLoadDone(70, "Loading extensions...");
	if (!g_materials.loadExtensions(extensionPath, error, warnings)) {
		// warnings.push_back("Couldn't load extensions: " + error);
	}

	g_gui.SetLoadDone(90, "Finishing...");
	g_brushes.init();
	g_materials.createOtherTileset();
	g_materials.createNpcTileset();

	g_gui.DestroyLoadBar();
	return true;
}

void GUI::UnloadVersion() {
	UnnamedRenderingLock();
	gfx.clear();
	currentBrush = nullptr;
	previousBrush = nullptr;

	houseBrush = nullptr;
	houseExitBrush = nullptr;
	waypointBrush = nullptr;
	optionalBrush = nullptr;
	eraser = nullptr;
	normalDoorBrush = nullptr;
	lockedDoorBrush = nullptr;
	magicDoorBrush = nullptr;
	questDoorBrush = nullptr;
	hatchDoorBrush = nullptr;
	windowDoorBrush = nullptr;

	if (loadedVersion != CLIENT_VERSION_NONE) {
		// g_gui.UnloadVersion();
		g_materials.clear();
		g_brushes.clear();
		g_items.clear();
		gfx.clear();

		FileName localDataPath = getLoadedVersion()->getLocalDataPath();
		localDataPath.SetFullName("monsters.xml");
		g_monsters.saveToXML(localDataPath);
		g_monsters.clear();

		localDataPath.SetFullName("npcs.xml");
		g_npcs.saveToXML(localDataPath);
		g_npcs.clear();

		loadedVersion = CLIENT_VERSION_NONE;
	}
}

void GUI::SaveCurrentMap(FileName filename, bool showdialog) {
	const auto &mapTab = GetCurrentMapTab();
	if (mapTab) {
		const auto &editor = mapTab->GetEditor();
		if (editor) {
			editor->saveMap(filename, showdialog);

			const auto &mapName = editor->getMap().getFilename();
			const auto &position = mapTab->GetScreenCenterPosition();
			g_settings.setString(Config::RECENT_EDITED_MAP_PATH, mapName);
			g_settings.setString(Config::RECENT_EDITED_MAP_POSITION, position.toString());
		}
	}

	UpdateTitle();
	root->UpdateMenubar();
	root->Refresh();
}

bool GUI::IsEditorOpen() const {
	return tabbook != nullptr && GetCurrentMapTab();
}

double GUI::GetCurrentZoom() {
	const auto &tab = GetCurrentMapTab();
	if (tab) {
		return tab->GetCanvas()->GetZoom();
	}
	return 1.0;
}

void GUI::SetCurrentZoom(double zoom) {
	const auto &tab = GetCurrentMapTab();
	if (tab) {
		tab->GetCanvas()->SetZoom(zoom);
	}
}

void GUI::FitViewToMap() {
	for (int index = 0; index < tabbook->GetTabCount(); ++index) {
		if (const auto &tab = dynamic_cast<MapTab*>(tabbook->GetTab(index))) {
			tab->GetView()->FitToMap();
		}
	}
}

void GUI::FitViewToMap(MapTab* mt) {
	for (int index = 0; index < tabbook->GetTabCount(); ++index) {
		if (const auto &tab = dynamic_cast<MapTab*>(tabbook->GetTab(index))) {
			if (tab->HasSameReference(mt)) {
				tab->GetView()->FitToMap();
			}
		}
	}
}

bool GUI::NewMap() {
	FinishWelcomeDialog();

	Editor* editor;
	try {
		editor = newd Editor(copybuffer);
	} catch (const std::runtime_error &e) {
		PopupDialog(root, "Error!", wxString(e.what(), wxConvUTF8), wxOK);
		return false;
	}

	const auto mapTab = newd MapTab(tabbook, editor);
	mapTab->OnSwitchEditorMode(mode);
	editor->clearChanges();

	SetStatusText("Created new map");
	UpdateTitle();
	RefreshPalettes();
	root->UpdateMenubar();
	root->Refresh();
	return true;
}

void GUI::OpenMap() {
	const auto &wildcard = g_settings.getInteger(Config::USE_OTGZ) != 0 ? MAP_LOAD_FILE_WILDCARD_OTGZ : MAP_LOAD_FILE_WILDCARD;
	wxFileDialog dialog(root, "Open map file", wxEmptyString, wxEmptyString, wildcard, wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if (dialog.ShowModal() == wxID_OK) {
		LoadMap(dialog.GetPath());
	}
}

void GUI::SaveMap() {
	if (!IsEditorOpen()) {
		return;
	}

	if (GetCurrentMap().hasFile()) {
		SaveCurrentMap(true);
	} else {
		const auto &wildcard = g_settings.getInteger(Config::USE_OTGZ) != 0 ? MAP_SAVE_FILE_WILDCARD_OTGZ : MAP_SAVE_FILE_WILDCARD;
		wxFileDialog dialog(root, "Save...", wxEmptyString, wxEmptyString, wildcard, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

		if (dialog.ShowModal() == wxID_OK) {
			SaveCurrentMap(dialog.GetPath(), true);
		}
	}
}

void GUI::SaveMapAs() {
	if (!IsEditorOpen()) {
		return;
	}

	const auto &wildcard = g_settings.getInteger(Config::USE_OTGZ) != 0 ? MAP_SAVE_FILE_WILDCARD_OTGZ : MAP_SAVE_FILE_WILDCARD;
	wxFileDialog dialog(root, "Save As...", "", "", wildcard, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

	if (dialog.ShowModal() == wxID_OK) {
		SaveCurrentMap(dialog.GetPath(), true);
		UpdateTitle();
		root->menu_bar->AddRecentFile(dialog.GetPath());
		root->UpdateMenubar();
	}
}

bool GUI::LoadMap(const FileName &fileName) {
	FinishWelcomeDialog();

	if (GetCurrentEditor() && !GetCurrentMap().hasChanged() && !GetCurrentMap().hasFile()) {
		g_gui.CloseCurrentEditor();
	}

	Editor* editor;
	try {
		editor = newd Editor(copybuffer, fileName);
	} catch (const std::runtime_error &e) {
		PopupDialog(root, "Error!", wxString(e.what(), wxConvUTF8), wxOK);
		return false;
	}

	const auto mapTab = newd MapTab(tabbook, editor);
	mapTab->OnSwitchEditorMode(mode);

	root->AddRecentFile(fileName);

	mapTab->GetView()->FitToMap();
	UpdateTitle();
	ListDialog("Map loader errors", mapTab->GetMap()->getWarnings());
	// Npc and monsters
	root->DoQueryImportCreatures();

	FitViewToMap(mapTab);
	root->UpdateMenubar();

	std::string path = g_settings.getString(Config::RECENT_EDITED_MAP_PATH);
	if (!path.empty()) {
		FileName file(path);
		if (file == fileName) {
			Position position = Position(g_settings.getString(Config::RECENT_EDITED_MAP_POSITION));
			mapTab->SetScreenCenterPosition(position);
		}
	}
	return true;
}

Editor* GUI::GetCurrentEditor() {
	const auto &mapTab = GetCurrentMapTab();
	if (mapTab) {
		return mapTab->GetEditor();
	}
	return nullptr;
}

EditorTab* GUI::GetTab(int idx) {
	return tabbook->GetTab(idx);
}

int GUI::GetTabCount() const {
	return tabbook->GetTabCount();
}

EditorTab* GUI::GetCurrentTab() {
	return tabbook->GetCurrentTab();
}

MapTab* GUI::GetCurrentMapTab() const {
	if (tabbook && tabbook->GetTabCount() > 0) {
		const auto &editorTab = tabbook->GetCurrentTab();
		const auto &mapTab = dynamic_cast<MapTab*>(editorTab);
		return mapTab;
	}
	return nullptr;
}

Map &GUI::GetCurrentMap() {
	const auto &editor = GetCurrentEditor();
	ASSERT(editor);
	return editor->getMap();
}

int GUI::GetOpenMapCount() {
	std::set<Map*> openMaps;

	for (int i = 0; i < tabbook->GetTabCount(); ++i) {
		const auto &tab = dynamic_cast<MapTab*>(tabbook->GetTab(i));
		if (tab) {
			openMaps.insert(openMaps.begin(), tab->GetMap());
		}
	}

	return static_cast<int>(openMaps.size());
}

bool GUI::ShouldSave() {
	const auto &editor = GetCurrentEditor();
	ASSERT(editor);
	return editor->hasChanges();
}

void GUI::AddPendingCanvasEvent(wxEvent &event) {
	const auto &mapTab = GetCurrentMapTab();
	if (mapTab) {
		mapTab->GetCanvas()->GetEventHandler()->AddPendingEvent(event);
	}
}

void GUI::CloseCurrentEditor() {
	RefreshPalettes();
	tabbook->DeleteTab(tabbook->GetSelection());
	root->UpdateMenubar();
}

bool GUI::CloseLiveEditors(LiveSocket* sock) {
	for (auto tabIndex = 0; tabIndex < tabbook->GetTabCount(); ++tabIndex) {
		const auto &mapTab = dynamic_cast<MapTab*>(tabbook->GetTab(tabIndex));
		if (mapTab) {
			const auto &editor = mapTab->GetEditor();
			if (editor->GetLiveClient() == sock) {
				tabbook->DeleteTab(tabIndex--);
			}
		}
		const auto &liveLogTab = dynamic_cast<LiveLogTab*>(tabbook->GetTab(tabIndex));
		if (liveLogTab) {
			if (liveLogTab->GetSocket() == sock) {
				liveLogTab->Disconnect();
				tabbook->DeleteTab(tabIndex--);
			}
		}
	}
	root->UpdateMenubar();
	return true;
}

bool GUI::CloseAllEditors() {
	for (auto tabIndex = 0; tabIndex < tabbook->GetTabCount(); ++tabIndex) {
		const auto &mapTab = dynamic_cast<MapTab*>(tabbook->GetTab(tabIndex));
		if (mapTab) {
			if (mapTab->IsUniqueReference() && mapTab->GetMap() && mapTab->GetMap()->hasChanged()) {
				tabbook->SetFocusedTab(tabIndex);
				if (!root->DoQuerySave(false)) {
					return false;
				} else {
					RefreshPalettes();
					tabbook->DeleteTab(tabIndex--);
				}
			} else {
				tabbook->DeleteTab(tabIndex--);
			}
		}
	}
	if (root) {
		root->UpdateMenubar();
	}
	return true;
}

void GUI::NewMapView() {
	const auto &mapTab = GetCurrentMapTab();
	if (mapTab) {
		const auto newMapTab = newd MapTab(mapTab);
		newMapTab->OnSwitchEditorMode(mode);

		SetStatusText("Created new view");
		UpdateTitle();
		RefreshPalettes();
		root->UpdateMenubar();
		root->Refresh();
	}
}

void GUI::LoadPerspective() {
	if (!IsVersionLoaded()) {
		if (g_settings.getInteger(Config::WINDOW_MAXIMIZED)) {
			root->Maximize();
		} else {
			root->SetSize(wxSize(
				g_settings.getInteger(Config::WINDOW_WIDTH),
				g_settings.getInteger(Config::WINDOW_HEIGHT)
			));
		}
	} else {
		std::string tmp;
		std::string layout = g_settings.getString(Config::PALETTE_LAYOUT);

		std::vector<std::string> paletteList;
		for (const auto &c : layout) {
			if (c == '|') {
				paletteList.push_back(tmp);
				tmp.clear();
			} else {
				tmp.push_back(c);
			}
		}

		if (!tmp.empty()) {
			paletteList.push_back(tmp);
		}

		for (const auto &name : paletteList) {
			const auto &palette = CreatePalette();

			auto &info = auiManager->GetPane(palette);
			auiManager->LoadPaneInfo(wxstr(name), info);

			if (info.IsFloatable()) {
				bool offScreen = true;
				for (auto index = 0; index < wxDisplay::GetCount(); ++index) {
					wxDisplay display(index);
					const auto &rect = display.GetClientArea();
					if (rect.Contains(info.floating_pos)) {
						offScreen = false;
						break;
					}
				}

				if (offScreen) {
					info.Dock();
				}
			}
		}

		if (g_settings.getInteger(Config::MINIMAP_VISIBLE)) {
			if (!minimap) {
				wxAuiPaneInfo info;

				const auto &data = wxstr(g_settings.getString(Config::MINIMAP_LAYOUT));
				auiManager->LoadPaneInfo(data, info);

				minimap = newd MinimapWindow(root);
				auiManager->AddPane(minimap, info);
			} else {
				auto &info = auiManager->GetPane(minimap);

				const auto &data = wxstr(g_settings.getString(Config::MINIMAP_LAYOUT));
				auiManager->LoadPaneInfo(data, info);
			}

			auto &info = auiManager->GetPane(minimap);
			if (info.IsFloatable()) {
				bool offscreen = true;
				for (auto index = 0; index < wxDisplay::GetCount(); ++index) {
					wxDisplay display(index);
					const auto &rect = display.GetClientArea();
					if (rect.Contains(info.floating_pos)) {
						offscreen = false;
						break;
					}
				}

				if (offscreen) {
					info.Dock();
				}
			}
		}

		if (g_settings.getInteger(Config::ACTIONS_HISTORY_VISIBLE)) {
			if (!actionsHistoryWindow) {
				wxAuiPaneInfo info;

				const auto &data = wxstr(g_settings.getString(Config::ACTIONS_HISTORY_LAYOUT));
				auiManager->LoadPaneInfo(data, info);

				actionsHistoryWindow = new ActionsHistoryWindow(root);
				auiManager->AddPane(actionsHistoryWindow, info);
			} else {
				auto &info = auiManager->GetPane(actionsHistoryWindow);
				const auto &data = wxstr(g_settings.getString(Config::ACTIONS_HISTORY_LAYOUT));
				auiManager->LoadPaneInfo(data, info);
			}

			auto &info = auiManager->GetPane(actionsHistoryWindow);
			if (info.IsFloatable()) {
				bool offscreen = true;
				for (auto index = 0; index < wxDisplay::GetCount(); ++index) {
					wxDisplay display(index);
					const auto &rect = display.GetClientArea();
					if (rect.Contains(info.floating_pos)) {
						offscreen = false;
						break;
					}
				}

				if (offscreen) {
					info.Dock();
				}
			}
		}

		auiManager->Update();
		root->UpdateMenubar();
	}

	root->GetAuiToolBar()->LoadPerspective();
}

void GUI::SavePerspective() {
	g_settings.setInteger(Config::WINDOW_MAXIMIZED, root->IsMaximized());
	g_settings.setInteger(Config::WINDOW_WIDTH, root->GetSize().GetWidth());
	g_settings.setInteger(Config::WINDOW_HEIGHT, root->GetSize().GetHeight());
	g_settings.setInteger(Config::MINIMAP_VISIBLE, minimap ? 1 : 0);
	g_settings.setInteger(Config::ACTIONS_HISTORY_VISIBLE, actionsHistoryWindow ? 1 : 0);

	wxString panelInfo;
	for (const auto &palette : palettes) {
		if (auiManager->GetPane(palette).IsShown()) {
			panelInfo << auiManager->SavePaneInfo(auiManager->GetPane(palette)) << "|";
		}
	}
	g_settings.setString(Config::PALETTE_LAYOUT, nstr(panelInfo));

	if (minimap) {
		wxString minimapInfo = auiManager->SavePaneInfo(auiManager->GetPane(minimap));
		g_settings.setString(Config::MINIMAP_LAYOUT, nstr(minimapInfo));
	}

	if (actionsHistoryWindow) {
		wxString actionsHistoryInfo = auiManager->SavePaneInfo(auiManager->GetPane(actionsHistoryWindow));
		g_settings.setString(Config::ACTIONS_HISTORY_LAYOUT, nstr(actionsHistoryInfo));
	}

	root->GetAuiToolBar()->SavePerspective();
}

void GUI::HideSearchWindow() {
	if (searchResultWindow) {
		auiManager->GetPane(searchResultWindow).Show(false);
		auiManager->Update();
	}
}

SearchResultWindow* GUI::ShowSearchWindow() {
	if (searchResultWindow == nullptr) {
		searchResultWindow = newd SearchResultWindow(root);
		auiManager->AddPane(searchResultWindow, wxAuiPaneInfo().Caption("Search Results"));
	} else {
		auiManager->GetPane(searchResultWindow).Show();
	}
	auiManager->Update();
	return searchResultWindow;
}

ActionsHistoryWindow* GUI::ShowActionsWindow() {
	if (!actionsHistoryWindow) {
		actionsHistoryWindow = new ActionsHistoryWindow(root);
		auiManager->AddPane(actionsHistoryWindow, wxAuiPaneInfo().Caption("Actions History"));
	} else {
		auiManager->GetPane(actionsHistoryWindow).Show();
	}

	auiManager->Update();
	actionsHistoryWindow->RefreshActions();
	return actionsHistoryWindow;
}

void GUI::HideActionsWindow() {
	if (actionsHistoryWindow) {
		auiManager->GetPane(actionsHistoryWindow).Show(false);
		auiManager->Update();
	}
}

//=============================================================================
// Palette Window Interface implementation

PaletteWindow* GUI::GetPalette() {
	if (palettes.empty()) {
		return nullptr;
	}
	return palettes.front();
}

PaletteWindow* GUI::NewPalette() {
	return CreatePalette();
}

void GUI::RefreshPalettes(Map* map, bool usedefault) {
	for (const auto &palette : palettes) {
		palette->OnUpdate(map ? map : (usedefault ? (IsEditorOpen() ? &GetCurrentMap() : nullptr) : nullptr));
	}
	SelectBrush();

	RefreshActions();
}

void GUI::RefreshOtherPalettes(PaletteWindow* paletteWindow) {
	for (const auto &palette : palettes) {
		if (palette != paletteWindow) {
			palette->OnUpdate(IsEditorOpen() ? &GetCurrentMap() : nullptr);
		}
	}
	SelectBrush();
}

PaletteWindow* GUI::CreatePalette() {
	if (!IsVersionLoaded()) {
		return nullptr;
	}

	const auto palette = newd PaletteWindow(root, g_materials.tilesets);
	auiManager->AddPane(palette, wxAuiPaneInfo().Caption("Palette").TopDockable(false).BottomDockable(false));
	auiManager->Update();

	// Make us the active palette
	palettes.push_front(palette);
	// Select brush from this palette
	SelectBrushInternal(palette->GetSelectedBrush());

	return palette;
}

void GUI::ActivatePalette(PaletteWindow* paletteWindow) {
	palettes.erase(std::find(palettes.begin(), palettes.end(), paletteWindow));
	palettes.push_front(paletteWindow);
}

void GUI::DestroyPalettes() {
	for (auto &palette : palettes) {
		auiManager->DetachPane(palette);
		palette->Destroy();
		palette = nullptr;
	}
	palettes.clear();
	auiManager->Update();
}

void GUI::RebuildPalettes() {
	// Palette lits might be modified due to active palette changes
	// Use a temporary list for iterating
	const auto &tmp = palettes;
	for (const auto &piter : tmp) {
		piter->ReloadSettings(IsEditorOpen() ? &GetCurrentMap() : nullptr);
	}
	auiManager->Update();
}

void GUI::ShowPalette() {
	if (palettes.empty()) {
		return;
	}

	for (const auto &palette : palettes) {
		if (auiManager->GetPane(palette).IsShown()) {
			return;
		}
	}

	auiManager->GetPane(palettes.front()).Show(true);
	auiManager->Update();
}

void GUI::SelectPalettePage(PaletteType paletteType) {
	if (palettes.empty()) {
		CreatePalette();
	}
	const auto &paletteWindow = GetPalette();
	if (!paletteWindow) {
		return;
	}

	ShowPalette();
	paletteWindow->SelectPage(paletteType);
	auiManager->Update();
	SelectBrushInternal(paletteWindow->GetSelectedBrush());
}

//=============================================================================
// Minimap Window Interface Implementation

void GUI::CreateMinimap() {
	if (!IsVersionLoaded()) {
		return;
	}

	if (minimap) {
		auiManager->GetPane(minimap).Show(true);
	} else {
		minimap = newd MinimapWindow(root);
		minimap->Show(true);
		auiManager->AddPane(minimap, wxAuiPaneInfo().Caption("Minimap"));
	}
	auiManager->Update();
}

void GUI::HideMinimap() {
	if (minimap) {
		auiManager->GetPane(minimap).Show(false);
		auiManager->Update();
	}
}

void GUI::DestroyMinimap() {
	if (minimap) {
		auiManager->DetachPane(minimap);
		auiManager->Update();
		minimap->Destroy();
		minimap = nullptr;
	}
}

void GUI::UpdateMinimap(bool immediate) {
	if (IsMinimapVisible()) {
		if (immediate) {
			minimap->Refresh();
		} else {
			minimap->DelayedUpdate();
		}
	}
}

bool GUI::IsMinimapVisible() const {
	if (minimap) {
		const wxAuiPaneInfo &pi = auiManager->GetPane(minimap);
		if (pi.IsShown()) {
			return true;
		}
	}
	return false;
}

//=============================================================================

void GUI::RefreshView() {
	const auto &editorTab = GetCurrentTab();
	if (!editorTab) {
		return;
	}

	if (!dynamic_cast<MapTab*>(editorTab)) {
		editorTab->GetWindow()->Refresh();
		return;
	}

	std::vector<EditorTab*> editorTabs;
	for (auto index = 0; index < tabbook->GetTabCount(); ++index) {
		const auto &mapTab = dynamic_cast<MapTab*>(tabbook->GetTab(index));
		if (mapTab) {
			editorTabs.push_back(mapTab);
		}
	}

	for (const auto &editorTab : editorTabs) {
		editorTab->GetWindow()->Refresh();
	}
}

void GUI::CreateLoadBar(const wxString &message, bool canCancel /* = false */) {
	progressText = message;

	progressFrom = 0;
	progressTo = 100;
	currentProgress = -1;

	progressBar = newd wxGenericProgressDialog("Loading", progressText + " (0%)", 100, root, wxPD_APP_MODAL | wxPD_SMOOTH | (canCancel ? wxPD_CAN_ABORT : 0));
	progressBar->SetSize(280, -1);
	progressBar->Show(true);

	for (int tabIndex = 0; tabIndex < tabbook->GetTabCount(); ++tabIndex) {
		const auto &mapTab = dynamic_cast<MapTab*>(tabbook->GetTab(tabIndex));
		if (mapTab && mapTab->GetEditor()->IsLiveServer()) {
			mapTab->GetEditor()->GetLiveServer()->startOperation(progressText);
		}
	}
	progressBar->Update(0);
}

void GUI::SetLoadScale(int32_t from, int32_t to) {
	progressFrom = from;
	progressTo = to;
}

bool GUI::SetLoadDone(int32_t done, const wxString &newMessage) {
	if (done == 100) {
		DestroyLoadBar();
		return true;
	} else if (done == currentProgress) {
		return true;
	}

	if (!newMessage.empty()) {
		progressText = newMessage;
	}

	auto newProgress = progressFrom + static_cast<int32_t>((done / 100.f) * (progressTo - progressFrom));
	newProgress = std::max<int32_t>(0, std::min<int32_t>(100, newProgress));

	bool skip = false;
	if (progressBar) {
		progressBar->Update(
			newProgress,
			wxString::Format("%s (%d%%)", progressText, newProgress),
			&skip
		);
		currentProgress = newProgress;
	}

	for (auto index = 0; index < tabbook->GetTabCount(); ++index) {
		const auto &mapTab = dynamic_cast<MapTab*>(tabbook->GetTab(index));
		if (mapTab && mapTab->GetEditor()) {
			const auto &server = mapTab->GetEditor()->GetLiveServer();
			if (server) {
				server->updateOperation(newProgress);
			}
		}
	}

	return skip;
}

void GUI::DestroyLoadBar() {
	if (progressBar) {
		progressBar->Show(false);
		currentProgress = -1;

		progressBar->Destroy();
		progressBar = nullptr;

		if (root->IsActive()) {
			root->Raise();
		} else {
			root->RequestUserAttention();
		}
	}
}

void GUI::ShowWelcomeDialog(const wxBitmap &icon) {
	const auto &recentFiles = root->GetRecentFiles();
	welcomeDialog = newd WelcomeDialog(__W_RME_APPLICATION_NAME__, "Version " + __W_RME_VERSION__, FROM_DIP(root, wxSize(800, 480)), icon, recentFiles);
	welcomeDialog->Bind(wxEVT_CLOSE_WINDOW, &GUI::OnWelcomeDialogClosed, this);
	welcomeDialog->Bind(WELCOME_DIALOG_ACTION, &GUI::OnWelcomeDialogAction, this);
	welcomeDialog->Show();
	UpdateMenubar();
}

void GUI::FinishWelcomeDialog() {
	if (welcomeDialog != nullptr) {
		welcomeDialog->Hide();
		root->Show();
		welcomeDialog->Destroy();
		welcomeDialog = nullptr;
	}
}

bool GUI::IsWelcomeDialogShown() {
	return welcomeDialog != nullptr && welcomeDialog->IsShown();
}

void GUI::OnWelcomeDialogClosed(wxCloseEvent &event) {
	welcomeDialog->Destroy();
	root->Close();
}

void GUI::OnWelcomeDialogAction(wxCommandEvent &event) {
	if (event.GetId() == wxID_NEW) {
		NewMap();
	} else if (event.GetId() == wxID_OPEN) {
		LoadMap(FileName(event.GetString()));
	}
}

void GUI::UpdateMenubar() {
	root->UpdateMenubar();
}

void GUI::SetScreenCenterPosition(const Position &position, bool showIndicator) {
	const auto &mapTab = GetCurrentMapTab();
	if (mapTab) {
		mapTab->SetScreenCenterPosition(position, showIndicator);
	}
}

void GUI::DoCut() {
	if (!IsSelectionMode()) {
		return;
	}

	const auto &editor = GetCurrentEditor();
	if (!editor) {
		return;
	}

	editor->copybuffer.cut(*editor, GetCurrentFloor());
	RefreshView();
	root->UpdateMenubar();
}

void GUI::DoCopy() {
	if (!IsSelectionMode()) {
		return;
	}

	const auto &editor = GetCurrentEditor();
	if (!editor) {
		return;
	}

	editor->copybuffer.copy(*editor, GetCurrentFloor());
	RefreshView();
	root->UpdateMenubar();
}

void GUI::DoPaste() {
	const auto &mapTab = GetCurrentMapTab();
	if (mapTab) {
		copybuffer.paste(*mapTab->GetEditor(), mapTab->GetCanvas()->GetCursorPosition());
	}
}

void GUI::PreparePaste() {
	const auto &editor = GetCurrentEditor();
	if (editor) {
		SetSelectionMode();
		auto &selection = editor->getSelection();
		selection.start();
		selection.clear();
		selection.finish();
		StartPasting();
		RefreshView();
	}
}

void GUI::StartPasting() {
	if (GetCurrentEditor()) {
		pasting = true;
		secondaryMap = &copybuffer.getBufferMap();
	}
}

void GUI::EndPasting() {
	if (pasting) {
		pasting = false;
		secondaryMap = nullptr;
	}
}

bool GUI::CanUndo() {
	const auto &editor = GetCurrentEditor();
	return (editor && editor->canUndo());
}

bool GUI::CanRedo() {
	const auto &editor = GetCurrentEditor();
	return (editor && editor->canRedo());
}

bool GUI::DoUndo() {
	const auto &editor = GetCurrentEditor();
	if (editor && editor->canUndo()) {
		editor->undo();
		if (editor->hasSelection()) {
			SetSelectionMode();
		}
		SetStatusText("Undo action");
		UpdateMinimap();
		root->UpdateMenubar();
		root->Refresh();
		return true;
	}
	return false;
}

bool GUI::DoRedo() {
	const auto &editor = GetCurrentEditor();
	if (editor && editor->canRedo()) {
		editor->redo();
		if (editor->hasSelection()) {
			SetSelectionMode();
		}
		SetStatusText("Redo action");
		UpdateMinimap();
		root->UpdateMenubar();
		root->Refresh();
		return true;
	}
	return false;
}

int GUI::GetCurrentFloor() {
	const auto &tab = GetCurrentMapTab();
	ASSERT(tab);
	return tab->GetCanvas()->GetFloor();
}

void GUI::ChangeFloor(int newFloor) {
	const auto &tab = GetCurrentMapTab();
	if (tab) {
		int oldFloor = GetCurrentFloor();
		if (newFloor < rme::MapMinLayer || newFloor > rme::MapMaxLayer) {
			return;
		}

		if (oldFloor != newFloor) {
			tab->GetCanvas()->ChangeFloor(newFloor);
		}
	}
}

void GUI::SetStatusText(const wxString &text) {
	g_gui.root->SetStatusText(text, 0);
}

void GUI::SetTitle(wxString title) {
	if (g_gui.root == nullptr) {
		return;
	}

#ifdef NIGHTLY_BUILD
	#ifdef SVN_BUILD
		#define TITLE_APPEND (wxString(" (Nightly Build #") << i2ws(SVN_BUILD) << ")")
	#else
		#define TITLE_APPEND (wxString(" (Nightly Build)"))
	#endif
#else
	#ifdef SVN_BUILD
		#define TITLE_APPEND (wxString(" (Build #") << i2ws(SVN_BUILD) << ")")
	#else
		#define TITLE_APPEND (wxString(""))
	#endif
#endif
#ifdef __EXPERIMENTAL__
	if (title != "") {
		g_gui.root->SetTitle(title << " - Remere's Map Editor BETA" << TITLE_APPEND);
	} else {
		g_gui.root->SetTitle(wxString("Remere's Map Editor BETA") << TITLE_APPEND);
	}
#elif __SNAPSHOT__
	if (title != "") {
		g_gui.root->SetTitle(title << " - Remere's Map Editor - SNAPSHOT" << TITLE_APPEND);
	} else {
		g_gui.root->SetTitle(wxString("Remere's Map Editor - SNAPSHOT") << TITLE_APPEND);
	}
#else
	if (!title.empty()) {
		g_gui.root->SetTitle(title << " - Remere's Map Editor" << TITLE_APPEND);
	} else {
		g_gui.root->SetTitle(wxString("Remere's Map Editor") << TITLE_APPEND);
	}
#endif
}

void GUI::UpdateTitle() {
	if (tabbook->GetTabCount() > 0) {
		SetTitle(tabbook->GetCurrentTab()->GetTitle());
		for (auto tabIndex = 0; tabIndex < tabbook->GetTabCount(); ++tabIndex) {
			if (tabbook->GetTab(tabIndex)) {
				tabbook->SetTabLabel(tabIndex, tabbook->GetTab(tabIndex)->GetTitle());
			}
		}
	} else {
		SetTitle("");
	}
}

void GUI::UpdateMenus() {
	wxCommandEvent evt(EVT_UPDATE_MENUS);
	g_gui.root->AddPendingEvent(evt);
}

void GUI::UpdateActions() {
	wxCommandEvent evt(EVT_UPDATE_ACTIONS);
	g_gui.root->AddPendingEvent(evt);
}

void GUI::RefreshActions() {
	if (actionsHistoryWindow) {
		actionsHistoryWindow->RefreshActions();
	}
}

void GUI::ShowToolbar(ToolBarID id, bool show) {
	if (root && root->GetAuiToolBar()) {
		root->GetAuiToolBar()->Show(id, show);
	}
}

void GUI::SwitchMode() {
	if (mode == DRAWING_MODE) {
		SetSelectionMode();
	} else {
		SetDrawingMode();
	}
}

void GUI::SetSelectionMode() {
	if (mode == SELECTION_MODE) {
		return;
	}

	if (currentBrush && currentBrush->isDoodad()) {
		secondaryMap = nullptr;
	}

	tabbook->OnSwitchEditorMode(SELECTION_MODE);
	mode = SELECTION_MODE;
}

void GUI::SetDrawingMode() {
	if (mode == DRAWING_MODE) {
		return;
	}

	std::set<MapTab*> mapTabs;
	for (int idx = 0; idx < tabbook->GetTabCount(); ++idx) {
		const auto &editorTab = tabbook->GetTab(idx);
		if (const auto &mapTab = dynamic_cast<MapTab*>(editorTab)) {
			if (mapTabs.find(mapTab) != mapTabs.end()) {
				continue;
			}

			const auto &editor = mapTab->GetEditor();
			auto &selection = editor->getSelection();
			selection.start(Selection::NONE, ACTION_UNSELECT);
			selection.clear();
			selection.finish();
			selection.updateSelectionCount();
			mapTabs.insert(mapTab);
		}
	}

	if (currentBrush && currentBrush->isDoodad()) {
		secondaryMap = doodadBufferMap;
	} else {
		secondaryMap = nullptr;
	}

	tabbook->OnSwitchEditorMode(DRAWING_MODE);
	mode = DRAWING_MODE;
}

void GUI::SetBrushSizeInternal(uint8_t newBrushSize) {
	if (newBrushSize != brushSize && currentBrush && currentBrush->isDoodad() && !currentBrush->oneSizeFitsAll()) {
		brushSize = newBrushSize;
		FillDoodadPreviewBuffer();
		secondaryMap = doodadBufferMap;
	} else {
		brushSize = newBrushSize;
	}
}

void GUI::SetBrushSize(uint8_t newBrushSize) {
	SetBrushSizeInternal(newBrushSize);

	for (const auto &palette : palettes) {
		palette->OnUpdateBrushSize(brushShape, brushSize);
	}

	root->GetAuiToolBar()->UpdateBrushSize(brushShape, brushSize);
}

void GUI::SetBrushVariation(int newBrushVariation) {
	if (newBrushVariation != brushVariation && currentBrush && currentBrush->isDoodad()) {
		brushVariation = newBrushVariation;
		FillDoodadPreviewBuffer();
		secondaryMap = doodadBufferMap;
	}
}

void GUI::SetBrushShape(BrushShape newBrushShape) {
	if (newBrushShape != brushShape && currentBrush && currentBrush->isDoodad() && !currentBrush->oneSizeFitsAll()) {
		brushShape = newBrushShape;
		FillDoodadPreviewBuffer();
		secondaryMap = doodadBufferMap;
	}
	brushShape = newBrushShape;

	for (const auto &palette : palettes) {
		palette->OnUpdateBrushSize(brushShape, brushSize);
	}

	root->GetAuiToolBar()->UpdateBrushSize(brushShape, brushSize);
}

void GUI::SetBrushThickness(bool on, int x, int y) {
	useCustomThickness = on;

	if (x != -1 || y != -1) {
		customThicknessMod = std::max<float>(x, 1.f) / std::max<float>(y, 1.f);
	}

	if (currentBrush && currentBrush->isDoodad()) {
		FillDoodadPreviewBuffer();
	}

	RefreshView();
}

void GUI::SetBrushThickness(int low, int ceil) {
	useCustomThickness = std::max<float>(low, 1.f) / std::max<float>(ceil, 1.f);

	if (useCustomThickness && currentBrush && currentBrush->isDoodad()) {
		FillDoodadPreviewBuffer();
	}

	RefreshView();
}

void GUI::DecreaseBrushSize(bool wrap) {
	switch (brushSize) {
		case 0: {
			if (wrap) {
				SetBrushSize(11);
			}
			break;
		}
		case 1: {
			SetBrushSize(0);
			break;
		}
		case 2:
		case 3: {
			SetBrushSize(1);
			break;
		}
		case 4:
		case 5: {
			SetBrushSize(2);
			break;
		}
		case 6:
		case 7: {
			SetBrushSize(4);
			break;
		}
		case 8:
		case 9:
		case 10: {
			SetBrushSize(6);
			break;
		}
		case 11:
		default: {
			SetBrushSize(8);
			break;
		}
	}
}

void GUI::IncreaseBrushSize(bool wrap) {
	switch (brushSize) {
		case 0: {
			SetBrushSize(1);
			break;
		}
		case 1: {
			SetBrushSize(2);
			break;
		}
		case 2:
		case 3: {
			SetBrushSize(4);
			break;
		}
		case 4:
		case 5: {
			SetBrushSize(6);
			break;
		}
		case 6:
		case 7: {
			SetBrushSize(8);
			break;
		}
		case 8:
		case 9:
		case 10: {
			SetBrushSize(11);
			break;
		}
		case 11:
		default: {
			if (wrap) {
				SetBrushSize(0);
			}
			break;
		}
	}
}

Brush* GUI::GetCurrentBrush() const noexcept {
	return currentBrush;
}

BrushShape GUI::GetBrushShape() const noexcept {
	if (currentBrush == spawnBrush) {
		return BRUSHSHAPE_SQUARE;
	}

	if (currentBrush == spawnNpcBrush) {
		return BRUSHSHAPE_SQUARE;
	}

	return brushShape;
}

uint8_t GUI::GetBrushSize() const noexcept {
	return brushSize;
}

int GUI::GetBrushVariation() const noexcept {
	return brushVariation;
}

int GUI::GetSpawnMonsterTime() const noexcept {
	return monsterSpawntime;
}

int GUI::GetSpawnNpcTime() const noexcept {
	return npcSpawntime;
}

void GUI::SelectBrush() {
	if (palettes.empty()) {
		return;
	}

	SelectBrushInternal(palettes.front()->GetSelectedBrush());

	RefreshView();
}

bool GUI::SelectBrush(const Brush* whatBrush, PaletteType paletteType /* = TILESET_UNKNOWN */) {
	if (palettes.empty()) {
		if (!CreatePalette()) {
			return false;
		}
	}

	if (!palettes.front()->OnSelectBrush(whatBrush, paletteType)) {
		return false;
	}

	SelectBrushInternal(const_cast<Brush*>(whatBrush));
	root->GetAuiToolBar()->UpdateBrushButtons();
	return true;
}

void GUI::SelectBrushInternal(Brush* brush) {
	// Fear no evil don't you say no evil
	if (currentBrush != brush && brush) {
		previousBrush = currentBrush;
	}

	currentBrush = brush;
	if (!currentBrush) {
		return;
	}

	brushVariation = std::min(brushVariation, brush->getMaxVariation());
	FillDoodadPreviewBuffer();
	if (brush->isDoodad()) {
		secondaryMap = doodadBufferMap;
	}

	SetDrawingMode();
	RefreshView();
}

void GUI::SelectPreviousBrush() {
	if (previousBrush) {
		SelectBrush(previousBrush);
	}
}

void GUI::FillDoodadPreviewBuffer() {
	if (!currentBrush || !currentBrush->isDoodad()) {
		return;
	}

	doodadBufferMap->clear();

	const auto &brush = currentBrush->asDoodad();
	if (brush->isEmpty(GetBrushVariation())) {
		return;
	}

	auto objectCount = 0;
	int area;
	if (GetBrushShape() == BRUSHSHAPE_SQUARE) {
		area = 2 * GetBrushSize();
		area = area * area + 1;
	} else {
		if (GetBrushSize() == 1) {
			// There is a huge deviation here with the other formula.
			area = 5;
		} else {
			area = int(0.5 + GetBrushSize() * GetBrushSize() * rme::PI);
		}
	}
	const auto objectRange = (useCustomThickness ? int(area * customThicknessMod) : brush->getThickness() * area / std::max(1, brush->getThicknessCeiling()));
	const auto finalObjectCount = std::max(1, objectRange + random(objectRange));

	Position centerPos(0x8000, 0x8000, 0x8);

	if (brushSize > 0 && !brush->oneSizeFitsAll()) {
		while (objectCount < finalObjectCount) {
			auto retries = 0;
			auto exit = false;

			// Try to place objects 5 times
			while (retries < 5 && !exit) {

				auto posRetries = 0;
				auto xPos = 0, yPos = 0;
				auto foundPos = false;
				if (GetBrushShape() == BRUSHSHAPE_CIRCLE) {
					while (posRetries < 5 && !foundPos) {
						xPos = random(-brushSize, brushSize);
						yPos = random(-brushSize, brushSize);
						float distance = sqrt(float(xPos * xPos) + float(yPos * yPos));
						if (distance < g_gui.GetBrushSize() + 0.005) {
							foundPos = true;
						} else {
							++posRetries;
						}
					}
				} else {
					foundPos = true;
					xPos = random(-brushSize, brushSize);
					yPos = random(-brushSize, brushSize);
				}

				if (!foundPos) {
					++retries;
					continue;
				}

				// Decide whether the zone should have a composite or several single objects.
				auto fail = false;
				if (random(brush->getTotalChance(GetBrushVariation())) <= brush->getCompositeChance(GetBrushVariation())) {
					// Composite
					const auto &composites = brush->getComposite(GetBrushVariation());

					// Figure out if the placement is valid
					for (const auto &composite : composites) {
						const auto &pos = centerPos + composite.first + Position(xPos, yPos, 0);
						if (const auto &tile = doodadBufferMap->getTile(pos)) {
							if (!tile->empty()) {
								fail = true;
								break;
							}
						}
					}
					if (fail) {
						++retries;
						break;
					}

					// Transfer items to the stack
					for (const auto &composite : composites) {
						const auto &pos = centerPos + composite.first + Position(xPos, yPos, 0);
						const auto &items = composite.second;
						auto tile = doodadBufferMap->getTile(pos);

						if (!tile) {
							tile = doodadBufferMap->allocator(doodadBufferMap->createTileL(pos));
						}

						for (const auto &item : items) {
							tile->addItem(item->deepCopy());
						}
						doodadBufferMap->setTile(tile->getPosition(), tile);
					}
					exit = true;
				} else if (brush->hasSingleObjects(GetBrushVariation())) {
					const auto &pos = centerPos + Position(xPos, yPos, 0);
					auto tile = doodadBufferMap->getTile(pos);
					if (tile) {
						if (!tile->empty()) {
							fail = true;
							break;
						}
					} else {
						tile = doodadBufferMap->allocator(doodadBufferMap->createTileL(pos));
					}
					auto variation = GetBrushVariation();
					brush->draw(doodadBufferMap, tile, &variation);
					// std::cout << "\tpos: " << tile->getPosition() << std::endl;
					doodadBufferMap->setTile(tile->getPosition(), tile);
					exit = true;
				}
				if (fail) {
					++retries;
					break;
				}
			}
			++objectCount;
		}
	} else {
		if (brush->hasCompositeObjects(GetBrushVariation()) && random(brush->getTotalChance(GetBrushVariation())) <= brush->getCompositeChance(GetBrushVariation())) {
			// Composite
			const auto &composites = brush->getComposite(GetBrushVariation());

			// All placement is valid...

			// Transfer items to the buffer
			for (const auto &composite : composites) {
				const auto &position = centerPos + composite.first;
				const auto &items = composite.second;
				const auto &tile = doodadBufferMap->allocator(doodadBufferMap->createTileL(position));
				// std::cout << pos << " = " << center_pos << " + " << buffer_tile->getPosition() << std::endl;

				for (const auto &item : items) {
					tile->addItem(item->deepCopy());
				}
				doodadBufferMap->setTile(tile->getPosition(), tile);
			}
		} else if (brush->hasSingleObjects(GetBrushVariation())) {
			const auto &tile = doodadBufferMap->allocator(doodadBufferMap->createTileL(centerPos));
			int variation = GetBrushVariation();
			brush->draw(doodadBufferMap, tile, &variation);
			doodadBufferMap->setTile(centerPos, tile);
		}
	}
}

long GUI::PopupDialog(wxWindow* parent, const wxString &title, const wxString &text, long style, const wxString &configSaveName, uint32_t configSaveValue) {
	if (text.empty()) {
		return wxID_ANY;
	}

	wxMessageDialog dialog(parent, text, title, style);
	return dialog.ShowModal();
}

long GUI::PopupDialog(const wxString &title, const wxString &text, long style, const wxString &configSaveName, uint32_t configSaveValue) {
	return g_gui.PopupDialog(g_gui.root, title, text, style, configSaveName, configSaveValue);
}

void GUI::ListDialog(wxWindow* parent, const wxString &title, const wxArrayString &strings) {
	if (strings.empty()) {
		return;
	}

	wxArrayString listItems(strings);

	// Create the window
	const auto dialog = newd wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxRESIZE_BORDER | wxCAPTION | wxCLOSE_BOX);

	const auto sizer = newd wxBoxSizer(wxVERTICAL);
	const auto listBox = newd wxListBox(dialog, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_SINGLE);
	listBox->SetMinSize(wxSize(500, 300));

	for (auto i = 0; i != listItems.GetCount(); ++i) {
		auto string = listItems[i];
		auto position = string.find("\n");
		if (position != wxString::npos) {
			// Split string!
			listBox->Append(string.substr(0, position));
			listItems[i] = string.substr(position + 1);
			continue;
		}
		listBox->Append(listItems[i]);
		++i;
	}
	sizer->Add(listBox, 1, wxEXPAND);

	const auto stdSizer = newd wxBoxSizer(wxHORIZONTAL);
	stdSizer->Add(newd wxButton(dialog, wxID_OK, "OK"), wxSizerFlags(1).Center());
	sizer->Add(stdSizer, wxSizerFlags(0).Center());

	dialog->SetSizerAndFit(sizer);

	// Show the window
	dialog->ShowModal();
	delete dialog;
}

void GUI::ShowTextBox(wxWindow* parent, wxString title, wxString content) {
	const auto dialog = newd wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxRESIZE_BORDER | wxCAPTION | wxCLOSE_BOX);
	const auto topSizer = newd wxBoxSizer(wxVERTICAL);
	const auto textField = newd wxTextCtrl(dialog, wxID_ANY, content, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
	textField->SetMinSize(wxSize(400, 550));
	topSizer->Add(textField, wxSizerFlags(5).Expand());

	const auto choiceSizer = newd wxBoxSizer(wxHORIZONTAL);
	choiceSizer->Add(newd wxButton(dialog, wxID_CANCEL, "OK"), wxSizerFlags(1).Center());
	topSizer->Add(choiceSizer, wxSizerFlags(0).Center());
	dialog->SetSizerAndFit(topSizer);

	dialog->ShowModal();
}

void GUI::SetHotkey(int index, Hotkey &hotkey) {
	ASSERT(index >= 0 && index <= 9);
	hotkeys[index] = hotkey;
	SetStatusText("Set hotkey " + i2ws(index) + ".");
}

const Hotkey &GUI::GetHotkey(int index) const {
	ASSERT(index >= 0 && index <= 9);
	return hotkeys[index];
}

void GUI::SaveHotkeys() const {
	std::ostringstream os;
	for (const auto &hotkey : hotkeys) {
		os << hotkey << '\n';
	}
	g_settings.setString(Config::NUMERICAL_HOTKEYS, os.str());
}

void GUI::LoadHotkeys() {
	std::istringstream is;
	is.str(g_settings.getString(Config::NUMERICAL_HOTKEYS));

	std::string line;
	int index = 0;
	while (getline(is, line)) {
		std::istringstream line_is;
		line_is.str(line);
		line_is >> hotkeys[index];

		++index;
	}
}

Hotkey::Hotkey(const Position &position) :
	type(POSITION), position(position) {
	////
}

Hotkey::Hotkey(const Brush* brush) :
	type(BRUSH), brushName(brush->getName()) {
	////
}

Hotkey::Hotkey(const std::string &name) :
	type(BRUSH), brushName(name) {
	////
}

std::ostream &operator<<(std::ostream &os, const Hotkey &hotkey) {
	switch (hotkey.type) {
		case Hotkey::POSITION: {
			os << "pos:{" << hotkey.position << "}";
		} break;
		case Hotkey::BRUSH: {
			if (hotkey.brushName.find('{') != std::string::npos || hotkey.brushName.find('}') != std::string::npos) {
				break;
			}
			os << "brush:{" << hotkey.brushName << "}";
		} break;
		default: {
			os << "none:{}";
		} break;
	}
	return os;
}

std::istream &operator>>(std::istream &is, Hotkey &hotkey) {
	std::string type;
	getline(is, type, ':');
	if (type == "none") {
		is.ignore(2); // ignore "{}"
	} else if (type == "pos") {
		is.ignore(1); // ignore "{"
		Position position;
		is >> position;
		hotkey = Hotkey(position);
		is.ignore(1); // ignore "}"
	} else if (type == "brush") {
		is.ignore(1); // ignore "{"
		std::string brushName;
		getline(is, brushName, '}');
		hotkey = Hotkey(brushName);
	} else {
		// Do nothing...
	}

	return is;
}

void SetWindowToolTip(wxWindow* window, const wxString &tip) {
	window->SetToolTip(tip);
}

void SetWindowToolTip(wxWindow* firstWindow, wxWindow* secondWindow, const wxString &tip) {
	firstWindow->SetToolTip(tip);
	secondWindow->SetToolTip(tip);
}
