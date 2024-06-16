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

#ifndef RME_PALETTE_H_
#define RME_PALETTE_H_

#include "palette_common.h"

class BrushPalettePanel;
class MonsterPalettePanel;
class NpcPalettePanel;
class HousePalettePanel;
class WaypointPalettePanel;
class ZonesPalettePanel;

class PaletteWindow : public wxPanel {
public:
	PaletteWindow(wxWindow* parent, const TilesetContainer &tilesets);
	~PaletteWindow() = default;

	// Interface
	// Reloads layout g_settings from g_settings (and using map)
	void ReloadSettings(Map* from);
	// Flushes all pages and forces them to be reloaded from the palette data again
	void InvalidateContents();
	// (Re)Loads all currently displayed data, called from InvalidateContents implicitly
	void LoadCurrentContents();
	// Goes to the selected page and selects any brush there
	void SelectPage(PaletteType palette);
	// The currently selected brush in this palette
	Brush* GetSelectedBrush() const;
	// The currently selected brush size in this palette
	int GetSelectedBrushSize() const;
	// The currently selected page (terrain, doodad...)
	PaletteType GetSelectedPage() const;

	// Custom Event handlers (something has changed?)
	// Finds the brush pointed to by whatbrush and selects it as the current brush (also changes page)
	// Returns if the brush was found in this palette
	virtual bool OnSelectBrush(const Brush* whatBrush, PaletteType primary = TILESET_UNKNOWN);
	// Updates the palette window to use the current brush size
	virtual void OnUpdateBrushSize(BrushShape shape, int size);
	// Updates the content of the palette (eg. houses, monsters)
	virtual void OnUpdate(Map* map);

	// wxWidgets Event Handlers
	void OnSwitchingPage(wxChoicebookEvent &event);
	void OnPageChanged(wxChoicebookEvent &event);
	// Forward key events to the parent window (The Map Window)
	void OnKey(wxKeyEvent &event);
	void OnClose(wxCloseEvent &);

protected:
	static void AddBrushToolPanel(PalettePanel* panel, const Config::Key config);
	static void AddBrushSizePanel(PalettePanel* panel, const Config::Key config);

	static PalettePanel* CreateTerrainPalette(wxWindow* parent, const TilesetContainer &tilesets);
	static PalettePanel* CreateDoodadPalette(wxWindow* parent, const TilesetContainer &tilesets);
	static PalettePanel* CreateItemPalette(wxWindow* parent, const TilesetContainer &tilesets);
	static PalettePanel* CreateMonsterPalette(wxWindow* parent, const TilesetContainer &tilesets);
	static PalettePanel* CreateNpcPalette(wxWindow* parent, const TilesetContainer &tilesets);
	static PalettePanel* CreateHousePalette(wxWindow* parent, const TilesetContainer &tilesets);
	static PalettePanel* CreateWaypointPalette(wxWindow* parent, const TilesetContainer &tilesets);
	static PalettePanel* CreateZonesPalette(wxWindow* parent, const TilesetContainer &tilesets);
	static PalettePanel* CreateRAWPalette(wxWindow* parent, const TilesetContainer &tilesets);

	wxChoicebook* choicebook = newd wxChoicebook(this, PALETTE_CHOICEBOOK, wxDefaultPosition, wxSize(230, 250));

	BrushPalettePanel* terrainPalette = nullptr;
	BrushPalettePanel* doodadPalette = nullptr;
	BrushPalettePanel* itemPalette = nullptr;
	MonsterPalettePanel* monsterPalette = nullptr;
	NpcPalettePanel* npcPalette = nullptr;
	HousePalettePanel* housePalette = nullptr;
	WaypointPalettePanel* waypointPalette = nullptr;
	ZonesPalettePanel* zonesPalette = nullptr;
	BrushPalettePanel* rawPalette = nullptr;

	DECLARE_EVENT_TABLE()
};

#endif
