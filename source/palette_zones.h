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

#ifndef RME_PALETTE_ZONES_H_
#define RME_PALETTE_ZONES_H_

#include <wx/listctrl.h>

#include "zones.h"
#include "palette_common.h"

class ZonesPalettePanel : public PalettePanel {
public:
	ZonesPalettePanel(wxWindow* parent, wxWindowID id = wxID_ANY);
	~ZonesPalettePanel();

	wxString GetName() const;
	PaletteType GetType() const;

	// Select the first brush
	void SelectFirstBrush();
	// Returns the currently selected brush (first brush if panel is not loaded)
	Brush* GetSelectedBrush() const;
	// Returns the currently selected brush size
	int GetSelectedBrushSize() const;
	// Select the brush in the parameter, this only changes the look of the panel
	bool SelectBrush(const Brush* whatbrush);

	// Called sometimes?
	void OnUpdate();
	// Called when this page is about to be displayed
	void OnSwitchIn();
	// Called when this page is hidden
	void OnSwitchOut();

public:
	// wxWidgets event handling
	void OnClickZone(wxListEvent &event);
	void OnRightClickZone(wxListEvent &event);
	void OnBeginEditZoneLabel(wxListEvent &event);
	void OnEditZoneLabel(wxListEvent &event);
	void OnClickAddZone(wxCommandEvent &event);
	void OnClickRemoveZone(wxCommandEvent &event);

	void SetMap(Map* map);

protected:
	Map* map;
	wxListCtrl* zone_list;
	wxButton* add_zone_button;
	wxButton* remove_zone_button;

	DECLARE_EVENT_TABLE()
};

#endif
