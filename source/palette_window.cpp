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

#include "settings.h"
#include "gui.h"
#include "brush.h"
#include "map_display.h"

#include "palette_window.h"
#include "palette_brushlist.h"
#include "palette_house.h"
#include "palette_monster.h"
#include "palette_npc.h"
#include "palette_waypoints.h"
#include "palette_zones.h"

#include "house_brush.h"
#include "map.h"

// ============================================================================
// Palette window

BEGIN_EVENT_TABLE(PaletteWindow, wxPanel)
EVT_CHOICEBOOK_PAGE_CHANGING(PALETTE_CHOICEBOOK, PaletteWindow::OnSwitchingPage)
EVT_CHOICEBOOK_PAGE_CHANGED(PALETTE_CHOICEBOOK, PaletteWindow::OnPageChanged)
EVT_CLOSE(PaletteWindow::OnClose)

EVT_KEY_DOWN(PaletteWindow::OnKey)
END_EVENT_TABLE()

PaletteWindow::PaletteWindow(wxWindow* parent, const TilesetContainer &tilesets) :
	wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(230, 250)) {
	SetMinSize(wxSize(225, 250));

	terrainPalette = static_cast<BrushPalettePanel*>(CreateTerrainPalette(choicebook, tilesets));
	choicebook->AddPage(terrainPalette, terrainPalette->GetName());

	doodadPalette = static_cast<BrushPalettePanel*>(CreateDoodadPalette(choicebook, tilesets));
	choicebook->AddPage(doodadPalette, doodadPalette->GetName());

	itemPalette = static_cast<BrushPalettePanel*>(CreateItemPalette(choicebook, tilesets));
	choicebook->AddPage(itemPalette, itemPalette->GetName());

	housePalette = static_cast<HousePalettePanel*>(CreateHousePalette(choicebook, tilesets));
	choicebook->AddPage(housePalette, housePalette->GetName());

	waypointPalette = static_cast<WaypointPalettePanel*>(CreateWaypointPalette(choicebook, tilesets));
	choicebook->AddPage(waypointPalette, waypointPalette->GetName());

	zonesPalette = static_cast<ZonesPalettePanel*>(CreateZonesPalette(choicebook, tilesets));
	choicebook->AddPage(zonesPalette, zonesPalette->GetName());

	monsterPalette = static_cast<MonsterPalettePanel*>(CreateMonsterPalette(choicebook, tilesets));
	choicebook->AddPage(monsterPalette, monsterPalette->GetName());

	npcPalette = static_cast<NpcPalettePanel*>(CreateNpcPalette(choicebook, tilesets));
	choicebook->AddPage(npcPalette, npcPalette->GetName());

	rawPalette = static_cast<BrushPalettePanel*>(CreateRAWPalette(choicebook, tilesets));
	choicebook->AddPage(rawPalette, rawPalette->GetName());

	// Setup sizers
	const auto sizer = newd wxBoxSizer(wxVERTICAL);
	choicebook->SetMinSize(wxSize(225, 300));
	sizer->Add(choicebook, 1, wxEXPAND);
	SetSizer(sizer);

	// Load first page
	LoadCurrentContents();

	Fit();
}

void PaletteWindow::AddBrushToolPanel(PalettePanel* panel, const Config::Key config) {
	const auto toolPanel = newd BrushToolPanel(panel);
	toolPanel->SetToolbarIconSize(g_settings.getBoolean(config));
	panel->AddToolPanel(toolPanel);
}

void PaletteWindow::AddBrushSizePanel(PalettePanel* panel, const Config::Key config) {
	const auto sizePanel = newd BrushSizePanel(panel);
	sizePanel->SetToolbarIconSize(g_settings.getBoolean(config));
	panel->AddToolPanel(sizePanel);
}

PalettePanel* PaletteWindow::CreateTerrainPalette(wxWindow* parent, const TilesetContainer &tilesets) {
	const auto panel = newd BrushPalettePanel(parent, tilesets, TILESET_TERRAIN);
	panel->SetListType(wxstr(g_settings.getString(Config::PALETTE_TERRAIN_STYLE)));

	AddBrushToolPanel(panel, Config::USE_LARGE_TERRAIN_TOOLBAR);

	AddBrushSizePanel(panel, Config::USE_LARGE_TERRAIN_TOOLBAR);

	return panel;
}

PalettePanel* PaletteWindow::CreateDoodadPalette(wxWindow* parent, const TilesetContainer &tilesets) {
	const auto panel = newd BrushPalettePanel(parent, tilesets, TILESET_DOODAD);
	panel->SetListType(wxstr(g_settings.getString(Config::PALETTE_DOODAD_STYLE)));

	panel->AddToolPanel(newd BrushThicknessPanel(panel));

	AddBrushSizePanel(panel, Config::USE_LARGE_DOODAD_SIZEBAR);

	return panel;
}

PalettePanel* PaletteWindow::CreateItemPalette(wxWindow* parent, const TilesetContainer &tilesets) {
	const auto panel = newd BrushPalettePanel(parent, tilesets, TILESET_ITEM);
	panel->SetListType(wxstr(g_settings.getString(Config::PALETTE_ITEM_STYLE)));

	AddBrushSizePanel(panel, Config::USE_LARGE_ITEM_SIZEBAR);

	return panel;
}

PalettePanel* PaletteWindow::CreateHousePalette(wxWindow* parent, const TilesetContainer &tilesets) {
	const auto panel = newd HousePalettePanel(parent);

	AddBrushSizePanel(panel, Config::USE_LARGE_HOUSE_SIZEBAR);

	return panel;
}

PalettePanel* PaletteWindow::CreateWaypointPalette(wxWindow* parent, const TilesetContainer &tilesets) {
	const auto panel = newd WaypointPalettePanel(parent);
	return panel;
}

PalettePanel* PaletteWindow::CreateZonesPalette(wxWindow* parent, const TilesetContainer &tilesets) {
	const auto panel = newd ZonesPalettePanel(parent);
	return panel;
}

PalettePanel* PaletteWindow::CreateMonsterPalette(wxWindow* parent, const TilesetContainer &tilesets) {
	const auto panel = newd MonsterPalettePanel(parent);
	return panel;
}

PalettePanel* PaletteWindow::CreateNpcPalette(wxWindow* parent, const TilesetContainer &tilesets) {
	const auto panel = newd NpcPalettePanel(parent);
	return panel;
}

PalettePanel* PaletteWindow::CreateRAWPalette(wxWindow* parent, const TilesetContainer &tilesets) {
	const auto panel = newd BrushPalettePanel(parent, tilesets, TILESET_RAW);
	panel->SetListType(wxstr(g_settings.getString(Config::PALETTE_RAW_STYLE)));

	AddBrushSizePanel(panel, Config::USE_LARGE_RAW_SIZEBAR);

	return panel;
}

bool PaletteWindow::CanSelectHouseBrush(PalettePanel* palette, const Brush* whatBrush) {
	if (!palette || !whatBrush->isHouse()) {
		return false;
	}

	return true;
}

bool PaletteWindow::CanSelectBrush(PalettePanel* palette, const Brush* whatBrush) {
	if (!palette) {
		return false;
	}

	return palette->SelectBrush(whatBrush);
}

void PaletteWindow::ReloadSettings(Map* map) {
	if (terrainPalette) {
		terrainPalette->SetListType(wxstr(g_settings.getString(Config::PALETTE_TERRAIN_STYLE)));
		terrainPalette->SetToolbarIconSize(g_settings.getBoolean(Config::USE_LARGE_TERRAIN_TOOLBAR));
	}
	if (doodadPalette) {
		doodadPalette->SetListType(wxstr(g_settings.getString(Config::PALETTE_DOODAD_STYLE)));
		doodadPalette->SetToolbarIconSize(g_settings.getBoolean(Config::USE_LARGE_DOODAD_SIZEBAR));
	}
	if (housePalette) {
		housePalette->SetMap(map);
		housePalette->SetToolbarIconSize(g_settings.getBoolean(Config::USE_LARGE_HOUSE_SIZEBAR));
	}
	if (waypointPalette) {
		waypointPalette->SetMap(map);
	}
	if (zonesPalette) {
		zonesPalette->SetMap(map);
	}
	if (itemPalette) {
		itemPalette->SetListType(wxstr(g_settings.getString(Config::PALETTE_ITEM_STYLE)));
		itemPalette->SetToolbarIconSize(g_settings.getBoolean(Config::USE_LARGE_ITEM_SIZEBAR));
	}
	if (rawPalette) {
		rawPalette->SetListType(wxstr(g_settings.getString(Config::PALETTE_RAW_STYLE)));
		rawPalette->SetToolbarIconSize(g_settings.getBoolean(Config::USE_LARGE_RAW_SIZEBAR));
	}
	InvalidateContents();
}

void PaletteWindow::LoadCurrentContents() const {
	if (!choicebook) {
		return;
	}

	const auto panel = dynamic_cast<PalettePanel*>(choicebook->GetCurrentPage());

	if (panel == nullptr) {
		return;
	}

	panel->LoadCurrentContents();

	// WASTE OF TIME? IT SEEMS THAT DOESN'T HAVE NO EFFECT.
	// Fit();
	// Refresh();
	// Update();
}

void PaletteWindow::InvalidateContents() {
	if (!choicebook) {
		return;
	}
	for (auto pageIndex = 0; pageIndex < choicebook->GetPageCount(); ++pageIndex) {
		const auto panel = dynamic_cast<PalettePanel*>(choicebook->GetPage(pageIndex));
		if (panel != nullptr) {
			panel->InvalidateContents();
		}
	}
	LoadCurrentContents();
	if (monsterPalette) {
		monsterPalette->OnUpdate();
	}
	if (npcPalette) {
		npcPalette->OnUpdate();
	}
	if (housePalette) {
		housePalette->OnUpdate();
	}
	if (waypointPalette) {
		waypointPalette->OnUpdate();
	}
	if (zonesPalette) {
		zonesPalette->OnUpdate();
	}
}

void PaletteWindow::SelectPage(PaletteType id) {
	if (!choicebook) {
		return;
	}
	if (id == GetSelectedPage()) {
		return;
	}

	for (auto pageIndex = 0; pageIndex < choicebook->GetPageCount(); ++pageIndex) {
		const auto panel = dynamic_cast<PalettePanel*>(choicebook->GetPage(pageIndex));
		if (panel == nullptr) {
			return;
		}

		if (panel->GetType() == id) {
			choicebook->SetSelection(pageIndex);
			// LoadCurrentContents();
			break;
		}
	}
}

Brush* PaletteWindow::GetSelectedBrush() const {
	if (!choicebook) {
		return nullptr;
	}

	const auto panel = dynamic_cast<PalettePanel*>(choicebook->GetCurrentPage());

	if (panel == nullptr) {
		return nullptr;
	}

	return panel->GetSelectedBrush();
}

int PaletteWindow::GetSelectedBrushSize() const {
	if (!choicebook) {
		return 0;
	}
	const auto panel = dynamic_cast<PalettePanel*>(choicebook->GetCurrentPage());

	if (panel == nullptr) {
		return 0;
	}

	return panel->GetSelectedBrushSize();
}

PaletteType PaletteWindow::GetSelectedPage() const {
	if (!choicebook) {
		return TILESET_UNKNOWN;
	}
	const auto panel = dynamic_cast<PalettePanel*>(choicebook->GetCurrentPage());

	ASSERT(panel);
	if (panel == nullptr) {
		return TILESET_UNKNOWN;
	}

	return panel->GetType();
}

bool PaletteWindow::OnSelectBrush(const Brush* whatBrush, PaletteType primary) {
	if (!choicebook || !whatBrush) {
		return false;
	}

	if (CanSelectHouseBrush(housePalette, whatBrush)) {
		housePalette->SelectBrush(whatBrush);
		SelectPage(TILESET_HOUSE);
		return true;
	}

	switch (primary) {
		case TILESET_TERRAIN: {
			// This is already searched first
			break;
		}
		case TILESET_DOODAD: {
			// Ok, search doodad before terrain
			if (CanSelectBrush(doodadPalette, whatBrush)) {
				SelectPage(TILESET_DOODAD);
				return true;
			}
			break;
		}
		case TILESET_ITEM: {
			if (CanSelectBrush(itemPalette, whatBrush)) {
				SelectPage(TILESET_ITEM);
				return true;
			}
			break;
		}
		case TILESET_MONSTER: {
			if (CanSelectBrush(monsterPalette, whatBrush)) {
				SelectPage(TILESET_MONSTER);
				return true;
			}
			break;
		}
		case TILESET_NPC: {
			if (CanSelectBrush(npcPalette, whatBrush)) {
				SelectPage(TILESET_NPC);
				return true;
			}
			break;
		}
		case TILESET_RAW: {
			if (CanSelectBrush(rawPalette, whatBrush)) {
				SelectPage(TILESET_RAW);
				return true;
			}
			break;
		}
		default:
			break;
	}

	// Test if it's a terrain brush
	if (CanSelectBrush(terrainPalette, whatBrush)) {
		SelectPage(TILESET_TERRAIN);
		return true;
	}

	// Test if it's a doodad brush
	if (primary != TILESET_DOODAD && CanSelectBrush(doodadPalette, whatBrush)) {
		SelectPage(TILESET_DOODAD);
		return true;
	}

	// Test if it's an item brush
	if (primary != TILESET_ITEM && CanSelectBrush(itemPalette, whatBrush)) {
		SelectPage(TILESET_ITEM);
		return true;
	}

	// Test if it's a monster brush
	if (primary != TILESET_MONSTER && CanSelectBrush(monsterPalette, whatBrush)) {
		SelectPage(TILESET_MONSTER);
		return true;
	}

	// Test if it's a npc brush
	if (primary != TILESET_NPC && CanSelectBrush(npcPalette, whatBrush)) {
		SelectPage(TILESET_NPC);
		return true;
	}

	// Test if it's a raw brush
	if (primary != TILESET_RAW && CanSelectBrush(rawPalette, whatBrush)) {
		SelectPage(TILESET_RAW);
		return true;
	}

	return false;
}

void PaletteWindow::OnSwitchingPage(wxChoicebookEvent &event) {
	event.Skip();
	if (!choicebook) {
		return;
	}

	const auto oldPage = choicebook->GetPage(choicebook->GetSelection());
	const auto oldPanel = dynamic_cast<PalettePanel*>(oldPage);
	if (oldPanel) {
		oldPanel->OnSwitchOut();
	}

	const auto selectedPage = choicebook->GetPage(event.GetSelection());
	const auto selectedPanel = dynamic_cast<PalettePanel*>(selectedPage);
	if (selectedPanel) {
		selectedPanel->OnSwitchIn();
	}
}

void PaletteWindow::OnPageChanged(wxChoicebookEvent &event) {
	if (!choicebook) {
		return;
	}
	g_gui.SelectBrush();
}

void PaletteWindow::OnUpdateBrushSize(BrushShape shape, int size) {
	if (!choicebook) {
		return;
	}
	const auto page = dynamic_cast<PalettePanel*>(choicebook->GetCurrentPage());

	ASSERT(page);

	if (page == nullptr) {
		return;
	}

	page->OnUpdateBrushSize(shape, size);
}

void PaletteWindow::OnUpdate(Map* map) {
	if (monsterPalette) {
		monsterPalette->OnUpdate();
	}
	if (npcPalette) {
		npcPalette->OnUpdate();
	}
	if (housePalette) {
		housePalette->SetMap(map);
	}
	if (waypointPalette) {
		waypointPalette->SetMap(map);
		waypointPalette->OnUpdate();
	}
	if (zonesPalette) {
		zonesPalette->SetMap(map);
		zonesPalette->OnUpdate();
	}
}

void PaletteWindow::OnKey(wxKeyEvent &event) {
	if (g_gui.GetCurrentTab() != nullptr) {
		g_gui.GetCurrentMapTab()->GetEventHandler()->AddPendingEvent(event);
	}
}

void PaletteWindow::OnClose(wxCloseEvent &event) {
	if (!event.CanVeto()) {
		// We can't do anything! This sucks!
		// (application is closed, we have to destroy ourselves)
		Destroy();
	} else {
		Show(false);
		event.Veto(true);
	}
}
