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

namespace {
	wxString ResolvePaletteGroupName(const Tileset* tileset) {
		if (!tileset->paletteGroupName.empty()) {
			return wxString::FromUTF8(tileset->paletteGroupName.c_str());
		}
		if (const TilesetCategory* terrainCategory = tileset->getCategory(TILESET_TERRAIN); terrainCategory && !terrainCategory->brushlist.empty()) {
			return "terrain";
		}
		if (const TilesetCategory* doodadCategory = tileset->getCategory(TILESET_DOODAD); doodadCategory && !doodadCategory->brushlist.empty()) {
			return "doodad";
		}
		if (const TilesetCategory* itemCategory = tileset->getCategory(TILESET_ITEM); itemCategory && !itemCategory->brushlist.empty()) {
			return "item";
		}
		return "other";
	}

	PaletteType ResolvePaletteTypeFromGroupName(const wxString &groupName) {
		if (groupName.IsSameAs("terrain", false)) {
			return TILESET_TERRAIN;
		}
		if (groupName.IsSameAs("doodad", false)) {
			return TILESET_DOODAD;
		}
		if (groupName.IsSameAs("item", false)) {
			return TILESET_ITEM;
		}
		if (groupName.IsSameAs("other", false)) {
			return TILESET_RAW;
		}
		return TILESET_UNKNOWN;
	}

	bool HasDisplayablePaletteContents(const Tileset* tileset, PaletteType paletteType) {
		if (!tileset) {
			return false;
		}
		if (paletteType == TILESET_UNKNOWN) {
			static const PaletteType kCandidates[] = { TILESET_TERRAIN, TILESET_DOODAD, TILESET_ITEM, TILESET_RAW };
			for (PaletteType candidate : kCandidates) {
				if (const TilesetCategory* category = tileset->getCategory(candidate); category && !category->brushlist.empty()) {
					return true;
				}
			}
			return false;
		}

		const TilesetCategory* category = tileset->getCategory(paletteType);
		return category && !category->brushlist.empty();
	}

	wxString BuildRuntimePaletteDisplayName(const wxString &groupName) {
		if (groupName.IsSameAs("terrain", false)) {
			return "Terrain";
		}
		if (groupName.IsSameAs("doodad", false)) {
			return "Doodad";
		}
		if (groupName.IsSameAs("item", false)) {
			return "Item";
		}
		if (groupName.IsSameAs("other", false)) {
			return "Other";
		}
		return groupName;
	}
}

BEGIN_EVENT_TABLE(PaletteWindow, wxPanel)
EVT_CHOICEBOOK_PAGE_CHANGING(PALETTE_CHOICEBOOK, PaletteWindow::OnSwitchingPage)
EVT_CHOICEBOOK_PAGE_CHANGED(PALETTE_CHOICEBOOK, PaletteWindow::OnPageChanged)
EVT_CLOSE(PaletteWindow::OnClose)

EVT_KEY_DOWN(PaletteWindow::OnKey)
END_EVENT_TABLE()

PaletteWindow::PaletteWindow(wxWindow* parent, const TilesetContainer &tilesets) :
	wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(230, 250)) {
	SetMinSize(wxSize(225, 250));

	for (const RuntimePaletteSpec &spec : BuildRuntimePaletteSpecs(tilesets)) {
		BrushPalettePanel* panel = nullptr;
		switch (spec.paletteType) {
			case TILESET_TERRAIN:
				panel = CreateTerrainPalette(choicebook, tilesets, spec.displayName, spec.groupName);
				if (spec.groupName.IsSameAs("terrain", false)) {
					terrainPalette = panel;
				}
				break;
			case TILESET_DOODAD:
				panel = CreateDoodadPalette(choicebook, tilesets, spec.displayName, spec.groupName);
				if (spec.groupName.IsSameAs("doodad", false)) {
					doodadPalette = panel;
				}
				break;
			case TILESET_ITEM:
				panel = CreateItemPalette(choicebook, tilesets, spec.displayName, spec.groupName);
				if (spec.groupName.IsSameAs("item", false)) {
					itemPalette = panel;
				}
				break;
			case TILESET_RAW:
				panel = CreateRAWPalette(choicebook, tilesets, spec.displayName, spec.groupName);
				if (spec.groupName.IsSameAs("other", false)) {
					rawPalette = panel;
				}
				break;
			case TILESET_UNKNOWN:
				panel = CreateGroupPalette(choicebook, tilesets, spec.displayName, spec.groupName);
				break;
			default:
				break;
		}

		if (panel) {
			AddRuntimePalettePage(panel);
		}
	}

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

std::vector<PaletteWindow::RuntimePaletteSpec> PaletteWindow::BuildRuntimePaletteSpecs(const TilesetContainer &tilesets) {
	std::vector<RuntimePaletteSpec> specs;
	for (auto it = tilesets.begin(); it != tilesets.end(); ++it) {
		const Tileset* tileset = it->second;
		if (!tileset) {
			continue;
		}

		const wxString groupName = ResolvePaletteGroupName(tileset);
		const PaletteType paletteType = ResolvePaletteTypeFromGroupName(groupName);
		if (!HasDisplayablePaletteContents(tileset, paletteType)) {
			continue;
		}

		auto existing = std::find_if(specs.begin(), specs.end(), [&](const RuntimePaletteSpec &spec) {
			return spec.groupName.IsSameAs(groupName, false);
		});
		if (existing != specs.end()) {
			if (tileset->paletteGroupSortOrder < existing->sortOrder) {
				existing->sortOrder = tileset->paletteGroupSortOrder;
			}
			continue;
		}

		RuntimePaletteSpec spec;
		spec.groupName = groupName;
		spec.displayName = BuildRuntimePaletteDisplayName(groupName);
		spec.paletteType = paletteType;
		spec.sortOrder = tileset->paletteGroupSortOrder;
		specs.push_back(std::move(spec));
	}

	std::sort(specs.begin(), specs.end(), [](const RuntimePaletteSpec &lhs, const RuntimePaletteSpec &rhs) {
		if (lhs.sortOrder != rhs.sortOrder) {
			return lhs.sortOrder < rhs.sortOrder;
		}
		return lhs.displayName.CmpNoCase(rhs.displayName) < 0;
	});

	return specs;
}

void PaletteWindow::ConfigureRuntimePalettePanel(BrushPalettePanel* panel) {
	if (!panel) {
		return;
	}

	switch (panel->GetType()) {
		case TILESET_TERRAIN:
			panel->SetListType(wxstr(g_settings.getString(Config::PALETTE_TERRAIN_STYLE)));
			AddBrushToolPanel(panel, Config::USE_LARGE_TERRAIN_TOOLBAR);
			AddBrushSizePanel(panel, Config::USE_LARGE_TERRAIN_TOOLBAR);
			break;
		case TILESET_DOODAD:
			panel->SetListType(wxstr(g_settings.getString(Config::PALETTE_DOODAD_STYLE)));
			panel->AddToolPanel(newd BrushThicknessPanel(panel));
			AddBrushSizePanel(panel, Config::USE_LARGE_DOODAD_SIZEBAR);
			break;
		case TILESET_ITEM:
			panel->SetListType(wxstr(g_settings.getString(Config::PALETTE_ITEM_STYLE)));
			AddBrushSizePanel(panel, Config::USE_LARGE_ITEM_SIZEBAR);
			break;
		case TILESET_RAW:
			panel->SetListType(wxstr(g_settings.getString(Config::PALETTE_RAW_STYLE)));
			AddBrushSizePanel(panel, Config::USE_LARGE_RAW_SIZEBAR);
			break;
		case TILESET_UNKNOWN:
			panel->SetListType(wxstr(g_settings.getString(Config::PALETTE_RAW_STYLE)));
			AddBrushSizePanel(panel, Config::USE_LARGE_RAW_SIZEBAR);
			break;
		default:
			break;
	}
}

void PaletteWindow::AddRuntimePalettePage(BrushPalettePanel* panel) {
	if (!panel) {
		return;
	}
	if (!panel->HasPages()) {
		panel->Destroy();
		return;
	}

	runtimeBrushPalettes_.push_back(panel);
	choicebook->AddPage(panel, panel->GetName());
}

BrushPalettePanel* PaletteWindow::CreateTerrainPalette(wxWindow* parent, const TilesetContainer &tilesets, const wxString &displayName, const wxString &paletteGroupFilter) {
	const auto panel = newd BrushPalettePanel(parent, tilesets, TILESET_TERRAIN, displayName, paletteGroupFilter);
	ConfigureRuntimePalettePanel(panel);
	return panel;
}

BrushPalettePanel* PaletteWindow::CreateDoodadPalette(wxWindow* parent, const TilesetContainer &tilesets, const wxString &displayName, const wxString &paletteGroupFilter) {
	const auto panel = newd BrushPalettePanel(parent, tilesets, TILESET_DOODAD, displayName, paletteGroupFilter);
	ConfigureRuntimePalettePanel(panel);
	return panel;
}

BrushPalettePanel* PaletteWindow::CreateItemPalette(wxWindow* parent, const TilesetContainer &tilesets, const wxString &displayName, const wxString &paletteGroupFilter) {
	const auto panel = newd BrushPalettePanel(parent, tilesets, TILESET_ITEM, displayName, paletteGroupFilter);
	ConfigureRuntimePalettePanel(panel);
	return panel;
}

BrushPalettePanel* PaletteWindow::CreateGroupPalette(wxWindow* parent, const TilesetContainer &tilesets, const wxString &displayName, const wxString &paletteGroupFilter) {
	const auto panel = newd BrushPalettePanel(parent, tilesets, TILESET_UNKNOWN, displayName, paletteGroupFilter);
	ConfigureRuntimePalettePanel(panel);
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

BrushPalettePanel* PaletteWindow::CreateRAWPalette(wxWindow* parent, const TilesetContainer &tilesets, const wxString &displayName, const wxString &paletteGroupFilter) {
	const auto panel = newd BrushPalettePanel(parent, tilesets, TILESET_RAW, displayName, paletteGroupFilter);
	ConfigureRuntimePalettePanel(panel);
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

bool PaletteWindow::TrySelectRuntimeBrush(const Brush* whatBrush, PaletteType preferredType, bool onlyPreferredType) {
	for (BrushPalettePanel* panel : runtimeBrushPalettes_) {
		if (!panel) {
			continue;
		}
		if (preferredType != TILESET_UNKNOWN && panel->GetType() != preferredType) {
			continue;
		}
		if (panel->SelectBrush(whatBrush)) {
			const int pageIndex = choicebook->FindPage(panel);
			if (pageIndex != wxNOT_FOUND) {
				choicebook->SetSelection(pageIndex);
			}
			return true;
		}
	}

	if (onlyPreferredType) {
		return false;
	}

	for (BrushPalettePanel* panel : runtimeBrushPalettes_) {
		if (!panel) {
			continue;
		}
		if (preferredType != TILESET_UNKNOWN && panel->GetType() == preferredType) {
			continue;
		}
		if (panel->SelectBrush(whatBrush)) {
			const int pageIndex = choicebook->FindPage(panel);
			if (pageIndex != wxNOT_FOUND) {
				choicebook->SetSelection(pageIndex);
			}
			return true;
		}
	}

	return false;
}

void PaletteWindow::ReloadSettings(Map* map) {
	for (BrushPalettePanel* panel : runtimeBrushPalettes_) {
		if (!panel) {
			continue;
		}
		switch (panel->GetType()) {
			case TILESET_TERRAIN:
				panel->SetListType(wxstr(g_settings.getString(Config::PALETTE_TERRAIN_STYLE)));
				panel->SetToolbarIconSize(g_settings.getBoolean(Config::USE_LARGE_TERRAIN_TOOLBAR));
				break;
			case TILESET_DOODAD:
				panel->SetListType(wxstr(g_settings.getString(Config::PALETTE_DOODAD_STYLE)));
				panel->SetToolbarIconSize(g_settings.getBoolean(Config::USE_LARGE_DOODAD_SIZEBAR));
				break;
			case TILESET_ITEM:
				panel->SetListType(wxstr(g_settings.getString(Config::PALETTE_ITEM_STYLE)));
				panel->SetToolbarIconSize(g_settings.getBoolean(Config::USE_LARGE_ITEM_SIZEBAR));
				break;
			case TILESET_RAW:
				panel->SetListType(wxstr(g_settings.getString(Config::PALETTE_RAW_STYLE)));
				panel->SetToolbarIconSize(g_settings.getBoolean(Config::USE_LARGE_RAW_SIZEBAR));
				break;
			default:
				break;
		}
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
			if (TrySelectRuntimeBrush(whatBrush, TILESET_TERRAIN, true)) {
				return true;
			}
			break;
		}
		case TILESET_DOODAD: {
			if (TrySelectRuntimeBrush(whatBrush, TILESET_DOODAD, true)) {
				return true;
			}
			break;
		}
		case TILESET_ITEM: {
			if (TrySelectRuntimeBrush(whatBrush, TILESET_ITEM, true)) {
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
			if (TrySelectRuntimeBrush(whatBrush, TILESET_RAW, true)) {
				return true;
			}
			break;
		}
		default:
			break;
	}

	if (TrySelectRuntimeBrush(whatBrush, primary, false)) {
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
