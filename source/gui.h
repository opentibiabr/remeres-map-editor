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

#ifndef RME_GUI_H_
#define RME_GUI_H_

#include "graphics.h"
#include "position.h"

#include "copybuffer.h"
#include "dcbutton.h"
#include "brush_enums.h"
#include "gui_ids.h"
#include "editor_tabs.h"
#include "map_tab.h"
#include "palette_window.h"
#include "client_version.h"
#include "zone_brush.h"

class BaseMap;
class Map;

class Editor;
class Brush;
class HouseBrush;
class HouseExitBrush;
class WaypointBrush;
class OptionalBorderBrush;
class EraserBrush;
class SpawnMonsterBrush;
class SpawnNpcBrush;
class DoorBrush;
class FlagBrush;

class MainFrame;
class WelcomeDialog;
class MapWindow;
class MapCanvas;

class SearchResultWindow;
class MinimapWindow;
class ActionsHistoryWindow;
class PaletteWindow;
class OldPropertiesWindow;
class TilesetWindow;
class EditTownsDialog;
class ItemButton;

class LiveSocket;

extern const wxEventType EVT_UPDATE_MENUS;
extern const wxEventType EVT_UPDATE_ACTIONS;

#define EVT_ON_UPDATE_MENUS(id, fn)                                                             \
	DECLARE_EVENT_TABLE_ENTRY(                                                                  \
		EVT_UPDATE_MENUS, id, wxID_ANY,                                                         \
		(wxObjectEventFunction)(wxEventFunction)wxStaticCastEvent(wxCommandEventFunction, &fn), \
		(wxObject*)nullptr                                                                      \
	),

#define EVT_ON_UPDATE_ACTIONS(id, fn)                                                           \
	DECLARE_EVENT_TABLE_ENTRY(                                                                  \
		EVT_UPDATE_ACTIONS, id, wxID_ANY,                                                       \
		(wxObjectEventFunction)(wxEventFunction)wxStaticCastEvent(wxCommandEventFunction, &fn), \
		(wxObject*)nullptr                                                                      \
	),

class Hotkey {
public:
	Hotkey() = default;
	Hotkey(const Position &position);
	Hotkey(const Brush* brush);
	Hotkey(const std::string &brushName);
	~Hotkey() = default;

	bool IsPosition() const noexcept {
		return type == POSITION;
	}
	bool IsBrush() const noexcept {
		return type == BRUSH;
	}
	const Position &GetPosition() const {
		ASSERT(IsPosition());
		return position;
	}
	std::string GetBrushname() const {
		ASSERT(IsBrush());
		return brushName;
	}

private:
	enum {
		NONE,
		POSITION,
		BRUSH,
	} type = NONE;

	Position position;
	std::string brushName = "";

	friend std::ostream &operator<<(std::ostream &os, const Hotkey &hotkey);
	friend std::istream &operator>>(std::istream &os, Hotkey &hotkey);
};

std::ostream &operator<<(std::ostream &os, const Hotkey &hotkey);
std::istream &operator>>(std::istream &os, Hotkey &hotkey);

class GUI {
public: // dtor and ctor
	GUI() = default;
	~GUI();

private:
	GUI(const GUI &g_gui); // Don't copy me
	GUI &operator=(const GUI &g_gui); // Don't assign me
	bool operator==(const GUI &g_gui); // Don't compare me

public:
	/**
	 * Saves the perspective to the configuration file
	 * This is the position of all windows etc. in the editor
	 */
	void SavePerspective();

	/**
	 * Loads the stored perspective from the configuration file
	 */
	void LoadPerspective();

	/**
	 * Creates a loading bar with the specified message, title is always "Loading"
	 * The default scale is 0 - 100
	 */
	void CreateLoadBar(const wxString &message, bool canCancel = false);

	/**
	 * Sets how much of the load has completed, the scale can be set with
	 * SetLoadScale.
	 * If this returns false, the user has hit the quit button and you should
	 * abort the loading.
	 */
	bool SetLoadDone(int32_t done, const wxString &newMessage = "");

	/**
	 * Sets the scale of the loading bar.
	 * Calling this with (50, 80) means that setting 50 as 'done',
	 * it will display as 0% loaded, 80 will display as 100% loaded.
	 */
	void SetLoadScale(int32_t from, int32_t to);

	void ShowWelcomeDialog(const wxBitmap &icon);
	void FinishWelcomeDialog();
	bool IsWelcomeDialogShown();

	/**
	 * Destroys (hides) the current loading bar.
	 */
	void DestroyLoadBar();

	void UpdateMenubar();

	bool IsRenderingEnabled() const noexcept {
		return disabledCounter == 0;
	}

	void EnableHotkeys() noexcept {
		hotkeysEnabled = true;
	}

	void DisableHotkeys() noexcept {
		hotkeysEnabled = false;
	}

	bool AreHotkeysEnabled() const noexcept {
		return hotkeysEnabled;
	}

	// This sends the event to the main window (redirecting from other controls)
	void AddPendingCanvasEvent(wxEvent &event);

	void OnWelcomeDialogClosed(wxCloseEvent &event);
	void OnWelcomeDialogAction(wxCommandEvent &event);

protected:
	void DisableRendering() noexcept {
		++disabledCounter;
	}
	void EnableRendering() noexcept {
		--disabledCounter;
	}

public:
	void SetTitle(wxString newtitle);
	void UpdateTitle();
	void UpdateMenus();
	void UpdateActions();
	void RefreshActions();
	void ShowToolbar(ToolBarID id, bool show);
	void SetStatusText(const wxString &text);

	long PopupDialog(wxWindow* parent, const wxString &title, const wxString &text, long style, const wxString &configSaveName = wxEmptyString, uint32_t configSaveValue = 0);
	long PopupDialog(const wxString &title, const wxString &text, long style, const wxString &configSaveName = wxEmptyString, uint32_t configSaveValue = 0);

	void ListDialog(wxWindow* parent, const wxString &title, const wxArrayString &strings);
	void ListDialog(const wxString &title, const wxArrayString &strings) {
		ListDialog(nullptr, title, strings);
	}

	void ShowTextBox(wxWindow* parent, wxString title, wxString contents);
	void ShowTextBox(const wxString &title, const wxString &contents) {
		ShowTextBox(nullptr, title, contents);
	}

	// Get the current GL context
	// Param is required if the context is to be created.
	wxGLContext* GetGLContext(wxGLCanvas* win);

	// Search Results
	SearchResultWindow* ShowSearchWindow();
	void HideSearchWindow();

	ActionsHistoryWindow* ShowActionsWindow();
	void HideActionsWindow();

	// Minimap
	void CreateMinimap();
	void HideMinimap();
	void DestroyMinimap();
	void UpdateMinimap(bool immediate = false);
	bool IsMinimapVisible() const;

	int GetCurrentFloor();
	void ChangeFloor(int newFloor);

	double GetCurrentZoom();
	void SetCurrentZoom(double zoom);

	void SwitchMode();
	void SetSelectionMode();
	void SetDrawingMode();
	bool IsSelectionMode() const noexcept {
		return mode == SELECTION_MODE;
	}
	bool IsDrawingMode() const noexcept {
		return mode == DRAWING_MODE;
	}

	void SetHotkey(int index, Hotkey &hotkey);
	const Hotkey &GetHotkey(int index) const;
	void SaveHotkeys() const;
	void LoadHotkeys();

	// Brushes
	void FillDoodadPreviewBuffer();
	// Selects the currently seleceted brush in the active palette
	void SelectBrush();
	// Updates the palette AND selects the brush, second parameter is first palette to look in
	// Returns true if the brush was found and selected
	bool SelectBrush(const Brush* brush, PaletteType paletteType = TILESET_UNKNOWN);
	// Selects the brush selected before the current brush
	void SelectPreviousBrush();
	// Only selects the brush, doesn't update the palette
	void SelectBrushInternal(Brush* brush);
	// Get different brush parameters
	Brush* GetCurrentBrush() const noexcept;
	BrushShape GetBrushShape() const noexcept;
	uint8_t GetBrushSize() const noexcept;
	int GetBrushVariation() const noexcept;
	int GetSpawnMonsterTime() const noexcept;
	int GetSpawnNpcTime() const noexcept;

	// Additional brush parameters
	void SetSpawnMonsterTime(int time) noexcept {
		monsterSpawntime = time;
	}
	void SetSpawnNpcTime(int time) noexcept {
		npcSpawntime = time;
	}
	void SetBrushSize(uint8_t newBrushSize);
	void SetBrushSizeInternal(uint8_t newBrushSize);
	void SetBrushShape(BrushShape newBrushShape);
	void SetBrushVariation(int nz);
	void SetBrushThickness(int low, int ceil);
	void SetBrushThickness(bool on, int low = -1, int ceil = -1);
	// Helper functions for size
	void DecreaseBrushSize(bool wrap = false);
	void IncreaseBrushSize(bool wrap = false);

	// Fetch different useful directories
	static FileName GetExecFileName();
	static wxString GetExecDirectory();
	static wxString GetDataDirectory();
	static wxString GetLocalDataDirectory();
	static wxString GetLocalDirectory();
	static wxString GetExtensionsDirectory();

	void discoverDataDirectory(const wxString &existentFile);
	const wxString &getFoundDataDirectory() const {
		return m_dataDirectory;
	}

	// Load/unload a client version (takes care of dialogs aswell)
	void UnloadVersion();
	bool LoadVersion(ClientVersionID ver, wxString &error, wxArrayString &warnings, bool force = false);
	// The current version loaded (returns CLIENT_VERSION_NONE if no version is loaded)
	const ClientVersion &GetCurrentVersion() const;
	ClientVersionID GetCurrentVersionID() const;
	// If any version is loaded at all
	bool IsVersionLoaded() const noexcept {
		return loadedVersion != CLIENT_VERSION_NONE;
	}

	// Centers current view on position
	void SetScreenCenterPosition(const Position &position, bool showIndicator = true);
	// Refresh the view canvas
	void RefreshView();
	// Fit all/specified current map view to map dimensions
	void FitViewToMap();
	void FitViewToMap(MapTab* mt);

	void DoCut();
	void DoCopy();
	void DoPaste();
	void PreparePaste();
	void StartPasting();
	void EndPasting();
	bool IsPasting() const noexcept {
		return pasting;
	}

	bool CanUndo();
	bool CanRedo();
	bool DoUndo();
	bool DoRedo();

	// Editor interface
	wxAuiManager* GetAuiManager() const noexcept {
		return auiManager;
	}
	EditorTab* GetCurrentTab();
	EditorTab* GetTab(int idx);
	int GetTabCount() const;
	bool IsAnyEditorOpen() const;
	bool IsEditorOpen() const;
	void CloseCurrentEditor();
	Editor* GetCurrentEditor();
	MapTab* GetCurrentMapTab() const;
	void CycleTab(bool forward = true);
	bool CloseLiveEditors(LiveSocket* sock);
	bool CloseAllEditors();
	void NewMapView();

	// Map
	Map &GetCurrentMap();
	int GetOpenMapCount();
	bool ShouldSave();
	void SaveCurrentMap(FileName filename, bool showdialog); // "" means default filename
	void SaveCurrentMap(bool showdialog = true) {
		SaveCurrentMap(wxString(""), showdialog);
	}
	bool NewMap();
	void OpenMap();
	void SaveMap();
	void SaveMapAs();
	bool LoadMap(const FileName &fileName);

protected:
	bool LoadDataFiles(wxString &error, wxArrayString &warnings);
	ClientVersion* getLoadedVersion() const {
		return loadedVersion == CLIENT_VERSION_NONE ? nullptr : ClientVersion::get(loadedVersion);
	}

	//=========================================================================
	// Palette Interface
public:
	// Spawn a newd palette
	PaletteWindow* NewPalette();
	// Bring this palette to the front (as the 'active' palette)
	void ActivatePalette(PaletteWindow* paletteWindow);
	// Rebuild forces palette to reload the entire contents
	void RebuildPalettes();
	// Refresh only updates the content (such as house/waypoint list)
	void RefreshPalettes(Map* map = nullptr, bool usedfault = true);
	// Won't refresh the palette in the parameter
	void RefreshOtherPalettes(PaletteWindow* paletteWindow);
	// If no palette is shown, this displays the primary palette
	// else does nothing.
	void ShowPalette();
	// Select a particular page on the primary palette
	void SelectPalettePage(PaletteType paletteType);

	// Returns primary palette
	PaletteWindow* GetPalette();
	// Returns list of all palette, first in the list is primary
	const std::list<PaletteWindow*> &GetPalettes();

	void DestroyPalettes();
	// Hidden from public view
protected:
	PaletteWindow* CreatePalette();

	//=========================================================================
	// Public members
	//=========================================================================
public:
	wxString m_dataDirectory;
	wxAuiManager* auiManager = nullptr;
	MapTabbook* tabbook;
	MainFrame* root = nullptr; // The main frame
	WelcomeDialog* welcomeDialog;
	CopyBuffer copybuffer;

	MinimapWindow* minimap = nullptr;
	DCButton* gem = nullptr; // The small gem in the lower-right corner
	SearchResultWindow* searchResultWindow = nullptr;
	ActionsHistoryWindow* actionsHistoryWindow = nullptr;
	GraphicManager gfx;

	BaseMap* secondaryMap = nullptr; // A buffer map
	BaseMap* doodadBufferMap = newd BaseMap(); // The map in which doodads are temporarily stored

	//=========================================================================
	// Brush references
	//=========================================================================

	HouseBrush* houseBrush = nullptr;
	HouseExitBrush* houseExitBrush = nullptr;
	WaypointBrush* waypointBrush = nullptr;
	OptionalBorderBrush* optionalBrush = nullptr;
	EraserBrush* eraser = nullptr;
	SpawnMonsterBrush* spawnBrush = nullptr;
	SpawnNpcBrush* spawnNpcBrush = nullptr;
	DoorBrush* normalDoorBrush = nullptr;
	DoorBrush* lockedDoorBrush = nullptr;
	DoorBrush* magicDoorBrush = nullptr;
	DoorBrush* questDoorBrush = nullptr;
	DoorBrush* hatchDoorBrush = nullptr;
	DoorBrush* windowDoorBrush = nullptr;
	FlagBrush* pzBrush = nullptr;
	FlagBrush* rookBrush = nullptr;
	FlagBrush* noLogoutBrush = nullptr;
	FlagBrush* pvpBrush = nullptr;
	ZoneBrush* zoneBrush = nullptr;

protected:
	//=========================================================================
	// Global GUI state
	//=========================================================================
	typedef std::list<PaletteWindow*> PaletteList;
	PaletteList palettes;

	wxGLContext* OGLContext = nullptr;

	ClientVersionID loadedVersion = CLIENT_VERSION_NONE;
	EditorMode mode = SELECTION_MODE;
	bool pasting = false;

	Hotkey hotkeys[10];
	bool hotkeysEnabled = true;

	//=========================================================================
	// Internal brush data
	//=========================================================================
	Brush* currentBrush = nullptr;
	Brush* previousBrush = nullptr;
	BrushShape brushShape = BRUSHSHAPE_SQUARE;
	uint8_t brushSize = 0;
	int brushVariation = 0;
	int monsterSpawntime = 0;
	int npcSpawntime = 0;

	bool useCustomThickness = false;
	float customThicknessMod = 0.0;

	//=========================================================================
	// Progress bar tracking
	//=========================================================================
	wxString progressText;
	wxGenericProgressDialog* progressBar = nullptr;

	int32_t progressFrom;
	int32_t progressTo;
	int32_t currentProgress;

	wxWindowDisabler* winDisabler;
	int disabledCounter = 0;

	friend class RenderingLock;
	friend class IOMinimap;
	friend MapTab::MapTab(MapTabbook*, Editor*);
	friend MapTab::MapTab(const MapTab*);
};

extern GUI g_gui;

class RenderingLock {
	bool acquired = true;

public:
	RenderingLock() noexcept {
		g_gui.DisableRendering();
	}
	~RenderingLock() noexcept {
		release();
	}
	void release() noexcept {
		g_gui.EnableRendering();
		acquired = false;
	}
};

/**
 * Will push a loading bar when it is constructed
 * which will the be popped when it destructs.
 * Look in the GUI class for documentation of what the methods mean.
 */
class ScopedLoadingBar {
public:
	ScopedLoadingBar(const wxString &message, bool canCancel = false) {
		g_gui.CreateLoadBar(message, canCancel);
	}
	~ScopedLoadingBar() {
		g_gui.DestroyLoadBar();
	}

	void SetLoadDone(int32_t done, const wxString &newmessage = wxEmptyString) {
		g_gui.SetLoadDone(done, newmessage);
	}

	void SetLoadScale(int32_t from, int32_t to) {
		g_gui.SetLoadScale(from, to);
	}
};

#define UnnamedRenderingLock() RenderingLock __unnamed_rendering_lock_##__LINE__

void SetWindowToolTip(wxWindow* window, const wxString &tip);
void SetWindowToolTip(wxWindow* firstWindow, wxWindow* secondWindow, const wxString &tip);

#endif
