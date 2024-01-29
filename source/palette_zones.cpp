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

// ============================================================================
// Zone palette

#include "main.h"

#include "gui.h"
#include "palette_zones.h"
#include "zone_brush.h"
#include "map.h"

BEGIN_EVENT_TABLE(ZonesPalettePanel, PalettePanel)
EVT_BUTTON(PALETTE_ZONES_ADD_ZONE, ZonesPalettePanel::OnClickAddZone)
EVT_BUTTON(PALETTE_ZONES_REMOVE_ZONE, ZonesPalettePanel::OnClickRemoveZone)

EVT_LIST_BEGIN_LABEL_EDIT(PALETTE_ZONES_LISTBOX, ZonesPalettePanel::OnBeginEditZoneLabel)
EVT_LIST_END_LABEL_EDIT(PALETTE_ZONES_LISTBOX, ZonesPalettePanel::OnEditZoneLabel)
EVT_LIST_ITEM_SELECTED(PALETTE_ZONES_LISTBOX, ZonesPalettePanel::OnClickZone)
EVT_LIST_ITEM_RIGHT_CLICK(PALETTE_ZONES_LISTBOX, ZonesPalettePanel::OnRightClickZone)
END_EVENT_TABLE()

ZonesPalettePanel::ZonesPalettePanel(wxWindow* parent, wxWindowID id) :
	PalettePanel(parent, id),
	map(nullptr) {
	wxSizer* sidesizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Zones");

	zone_list = newd wxListCtrl(this, PALETTE_ZONES_LISTBOX, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_EDIT_LABELS | wxLC_NO_HEADER);
	zone_list->InsertColumn(0, "UNNAMED", wxLIST_FORMAT_LEFT, 200);
	sidesizer->Add(zone_list, 1, wxEXPAND);

	wxSizer* tmpsizer = newd wxBoxSizer(wxHORIZONTAL);
	tmpsizer->Add(add_zone_button = newd wxButton(this, PALETTE_ZONES_ADD_ZONE, "Add", wxDefaultPosition, wxSize(50, -1)), 1, wxEXPAND);
	tmpsizer->Add(remove_zone_button = newd wxButton(this, PALETTE_ZONES_REMOVE_ZONE, "Remove", wxDefaultPosition, wxSize(70, -1)), 1, wxEXPAND);
	sidesizer->Add(tmpsizer, 0, wxEXPAND);

	SetSizerAndFit(sidesizer);
}

ZonesPalettePanel::~ZonesPalettePanel() {
	////
}

void ZonesPalettePanel::OnSwitchIn() {
	PalettePanel::OnSwitchIn();
}

void ZonesPalettePanel::OnSwitchOut() {
	PalettePanel::OnSwitchOut();
}

void ZonesPalettePanel::SetMap(Map* m) {
	map = m;
	this->Enable(m && m->getVersion().otbm >= MAP_OTBM_3);
}

void ZonesPalettePanel::SelectFirstBrush() {
	// SelectZoneBrush();
}

Brush* ZonesPalettePanel::GetSelectedBrush() const {
	long item = zone_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	g_gui.zone_brush->setZone(
		item == -1 || !map->zones.hasZone(nstr(zone_list->GetItemText(item))) ? 0 : map->zones.getZoneID(nstr(zone_list->GetItemText(item)))
	);
	return g_gui.zone_brush;
}

bool ZonesPalettePanel::SelectBrush(const Brush* whatbrush) {
	ASSERT(whatbrush == g_gui.zone_brush);
	return false;
}

int ZonesPalettePanel::GetSelectedBrushSize() const {
	return 0;
}

PaletteType ZonesPalettePanel::GetType() const {
	return TILESET_ZONES;
}

wxString ZonesPalettePanel::GetName() const {
	return "Zone Palette";
}

void ZonesPalettePanel::OnUpdate() {
	if (wxTextCtrl* tc = zone_list->GetEditControl()) {
		std::string name = nstr(tc->GetValue());
		if (map->zones.hasZone(name)) {
			map->zones.removeZone(name);
			map->cleanDeletedZones();
		}
	}
	zone_list->DeleteAllItems();

	if (!map) {
		zone_list->Enable(false);
		add_zone_button->Enable(false);
		remove_zone_button->Enable(false);
	} else {
		zone_list->Enable(true);
		add_zone_button->Enable(true);
		remove_zone_button->Enable(true);

		Zones &zones = map->zones;

		for (ZoneMap::const_iterator iter = zones.begin(); iter != zones.end(); ++iter) {
			zone_list->InsertItem(0, wxstr(iter->first));
		}
	}
}

void ZonesPalettePanel::OnClickZone(wxListEvent &event) {
	if (!map) {
		return;
	}

	std::string name = nstr(event.GetText());
	if (map->zones.hasZone(name)) {
		auto zoneId = map->zones.getZoneID(name);
		g_gui.zone_brush->setZone(zoneId);
	}
}

void ZonesPalettePanel::OnRightClickZone(wxListEvent &event) {
	if (!map) {
		return;
	}

	std::string name = nstr(event.GetText());
	if (map->zones.hasZone(name)) {
		auto zoneId = map->zones.getZoneID(name);
		g_gui.zone_brush->setZone(zoneId);
		g_gui.SetScreenCenterPosition(map->getZonePosition(zoneId));
	}
}

void ZonesPalettePanel::OnBeginEditZoneLabel(wxListEvent &event) {
	// We need to disable all hotkeys, so we can type properly
	g_gui.DisableHotkeys();
}

void ZonesPalettePanel::OnEditZoneLabel(wxListEvent &event) {
	std::string name = nstr(event.GetLabel());
	std::string oldName = nstr(zone_list->GetItemText(event.GetIndex()));

	if (event.IsEditCancelled()) {
		return;
	}

	if (name == "") {
		map->zones.removeZone(oldName);
		g_gui.RefreshPalettes();
	} else {
		if (name == oldName) {
			; // do nothing
		} else {
			if (map->zones.hasZone(name)) {
				// Already exists a zone with this name!
				g_gui.SetStatusText("There already is a zone with this name.");
				event.Veto();
				if (oldName == "") {
					map->zones.removeZone(oldName);
					g_gui.RefreshPalettes();
				}
			} else {
				map->zones.removeZone(oldName);
				map->zones.addZone(name);
				auto zoneId = map->zones.getZoneID(name);
				g_gui.zone_brush->setZone(zoneId);

				// Refresh other palettes
				refresh_timer.Start(300, true);
			}
		}
	}

	if (event.IsAllowed()) {
		g_gui.EnableHotkeys();
	}
}

void ZonesPalettePanel::OnClickAddZone(wxCommandEvent &event) {
	if (map) {
		if (map->zones.addZone("")) {
			long i = zone_list->InsertItem(0, "");
			zone_list->EditLabel(i);
		} else {
			long i = zone_list->FindItem(-1, "");
			zone_list->EditLabel(i);
		}

		// g_gui.RefreshPalettes();
	}
}

void ZonesPalettePanel::OnClickRemoveZone(wxCommandEvent &event) {
	if (!map) {
		return;
	}

	long item = zone_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (item != -1) {
		std::string name = nstr(zone_list->GetItemText(item));
		if (map->zones.hasZone(name)) {
			map->zones.removeZone(name);
			map->cleanDeletedZones();
		}
		zone_list->DeleteItem(item);
		refresh_timer.Start(300, true);
	}
}
