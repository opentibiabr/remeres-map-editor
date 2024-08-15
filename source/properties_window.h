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

#ifndef RME_PROPERTIES_WINDOW_H_
#define RME_PROPERTIES_WINDOW_H_

#include "main.h"

#include "common_windows.h"
#include "map.h"

class ContainerItemButton;
class ContainerItemPopupMenu;
class ItemAttribute;

class PropertiesWindow : public ObjectPropertiesWindowBase {
public:
	PropertiesWindow(wxWindow* parent, const Map* map, const Tile* tile, Item* item, wxPoint position = wxDefaultPosition);
	~PropertiesWindow();

	void OnClipboardText(wxClipboardTextEvent &evt);
	void OnKillFocus(wxFocusEvent &evt);
	void OnTextPosition(wxCommandEvent &);
	void OnDepotChoice(wxCommandEvent &);
	void OnLiquidChoice(wxCommandEvent &);
	void OnClickOK(wxCommandEvent &);
	void OnClickCancel(wxCommandEvent &);
	void OnClickAddAttribute(wxCommandEvent &);
	void OnClickRemoveAttribute(wxCommandEvent &);
	void OnSpinArrowAttributeUpdate(wxSpinEvent &event);

	void OnResize(wxSizeEvent &);
	void OnNotebookPageChanged(wxNotebookEvent &);
	void OnGridValueChanged(wxGridEvent &);

	void OnDestinationUpdate(const wxWindowID &ctrlId, int value);

	void Update();

protected:
	// Simple pane
	wxWindow* createGeneralPanel(wxWindow* parent);
	void SetAdvancedPropertyNumberData(const wxString &attributeName, int value);
	void createDepotIdChoiceCtrl(wxPanel* panel, wxFlexGridSizer* gridsizer);
	void createDoorIdCtrl(wxPanel* panel, wxFlexGridSizer* gridsizer);
	void createTeleportDestinationCtrl(wxPanel* panel, wxFlexGridSizer* gridsizer);
	void createLiquidChoiceCtrl(wxPanel* panel, wxFlexGridSizer* gridsizer);
	void saveGeneralPanel();

	// Container pane
	std::vector<ContainerItemButton*> container_items;
	wxWindow* createContainerPanel(wxWindow* parent);
	void saveContainerPanel();

	// Advanced pane
	wxGrid* attributesGrid;
	wxWindow* createAttributesPanel(wxWindow* parent);
	void saveAttributesPanel();
	void SetGridValue(wxGrid* grid, int rowIndex, std::string name, const ItemAttribute &attr);

	// Attributes
	void setBasicAttributes();
	void setTeleportAttributes(const std::string &key, int value);
	void setDepotAttributes(const std::string &key, int value);
	void setDoorAttributes(const std::string &key, int value);
	void setLiquidAttributes(const std::string &key, int value);

protected:
	const static wxArrayString types;

	wxPanel* panel = nullptr;

	wxSpinCtrl* simpleActionIdField = nullptr;
	wxSpinCtrl* simpleUniqueIdField = nullptr;
	wxSpinCtrl* doorIdField = nullptr;
	wxChoice* depotIdField = nullptr;
	wxChoice* liquidTypeField = nullptr;

	NumberTextCtrl* destinationXField = nullptr;
	NumberTextCtrl* destinationYField = nullptr;
	NumberTextCtrl* destinationZField = nullptr;

	wxNotebook* notebook;
	wxWindow* currentPanel;

	DECLARE_EVENT_TABLE()
};

#endif
