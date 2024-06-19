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

#include "palette_brushlist.h"
#include "gui.h"
#include "brush.h"
#include "add_tileset_window.h"
#include "add_item_window.h"
#include "materials.h"

// ============================================================================
// Brush Palette Panel
// A common class for terrain/doodad/item/raw palette

BEGIN_EVENT_TABLE(BrushPalettePanel, PalettePanel)
EVT_BUTTON(wxID_ADD, BrushPalettePanel::OnClickAddItemToTileset)
EVT_BUTTON(wxID_NEW, BrushPalettePanel::OnClickAddTileset)
EVT_CHOICEBOOK_PAGE_CHANGING(wxID_ANY, BrushPalettePanel::OnSwitchingPage)
EVT_CHOICEBOOK_PAGE_CHANGED(wxID_ANY, BrushPalettePanel::OnPageChanged)
END_EVENT_TABLE()

BrushPalettePanel::BrushPalettePanel(wxWindow* parent, const TilesetContainer &tilesets, TilesetCategoryType category, wxWindowID id) :
	PalettePanel(parent, id),
	paletteType(category) {
	const auto topsizer = newd wxBoxSizer(wxVERTICAL);

	// Create the tileset panel
	const auto tsSizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Tileset");
	choicebook = newd wxChoicebook(this, wxID_ANY, wxDefaultPosition, wxSize(180, 250));
	tsSizer->Add(choicebook, 1, wxEXPAND);
	topsizer->Add(tsSizer, 1, wxEXPAND);

	if (g_settings.getBoolean(Config::SHOW_TILESET_EDITOR)) {
		AddTilesetEditor(topsizer);
	}

	for (auto it = tilesets.begin(); it != tilesets.end(); ++it) {
		const auto tilesetCategory = it->second->getCategory(category);
		if (tilesetCategory && !tilesetCategory->brushlist.empty()) {
			const auto panel = newd BrushPanel(choicebook, tilesetCategory);
			choicebook->AddPage(panel, wxstr(it->second->name));
		}
	}

	SetSizerAndFit(topsizer);
}

void BrushPalettePanel::AddTilesetEditor(wxSizer* sizer) {
	const auto tmpsizer = newd wxBoxSizer(wxHORIZONTAL);
	const auto buttonAddTileset = newd wxButton(this, wxID_NEW, "Add new Tileset");
	tmpsizer->Add(buttonAddTileset, wxSizerFlags(0).Center());

	const auto buttonAddItemToTileset = newd wxButton(this, wxID_ADD, "Add new Item");
	tmpsizer->Add(buttonAddItemToTileset, wxSizerFlags(0).Center());

	sizer->Add(tmpsizer, 0, wxCENTER, 10);
}

void BrushPalettePanel::InvalidateContents() {
	for (auto pageIndex = 0; pageIndex < choicebook->GetPageCount(); ++pageIndex) {
		const auto panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(pageIndex));
		panel->InvalidateContents();
	}
	PalettePanel::InvalidateContents();
}

void BrushPalettePanel::LoadCurrentContents() {
	const auto page = choicebook->GetCurrentPage();
	const auto panel = dynamic_cast<BrushPanel*>(page);
	if (panel) {
		panel->OnSwitchIn();
	}
	PalettePanel::LoadCurrentContents();
}

void BrushPalettePanel::LoadAllContents() {
	for (auto pageIndex = 0; pageIndex < choicebook->GetPageCount(); ++pageIndex) {
		const auto panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(pageIndex));
		panel->LoadContents();
	}
	PalettePanel::LoadAllContents();
}

PaletteType BrushPalettePanel::GetType() const {
	return paletteType;
}

void BrushPalettePanel::SetListType(BrushListType newListType) const {
	if (!choicebook) {
		return;
	}
	for (auto pageIndex = 0; pageIndex < choicebook->GetPageCount(); ++pageIndex) {
		const auto panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(pageIndex));
		panel->SetListType(newListType);
	}
}

void BrushPalettePanel::SetListType(const wxString &newListType) const {
	if (!choicebook) {
		return;
	}
	for (auto pageIndex = 0; pageIndex < choicebook->GetPageCount(); ++pageIndex) {
		const auto panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(pageIndex));
		panel->SetListType(newListType);
	}
}

Brush* BrushPalettePanel::GetSelectedBrush() const {
	if (!choicebook) {
		return nullptr;
	}
	const auto page = choicebook->GetCurrentPage();
	const auto panel = dynamic_cast<BrushPanel*>(page);
	Brush* brush = nullptr;
	if (panel) {
		for (const auto &palettePanel : tool_bars) {
			brush = palettePanel->GetSelectedBrush();
			if (brush) {
				return brush;
			}
		}
		brush = panel->GetSelectedBrush();
	}
	return brush;
}

void BrushPalettePanel::SelectFirstBrush() {
	if (!choicebook) {
		return;
	}
	const auto page = choicebook->GetCurrentPage();
	const auto panel = dynamic_cast<BrushPanel*>(page);
	panel->SelectFirstBrush();
}

bool BrushPalettePanel::SelectBrush(const Brush* whatBrush) {
	if (!choicebook) {
		return false;
	}

	auto panel = dynamic_cast<BrushPanel*>(choicebook->GetCurrentPage());
	if (!panel) {
		return false;
	}

	if (panel->SelectBrush(whatBrush)) {
		for (const auto palettePanel : tool_bars) {
			palettePanel->SelectBrush(nullptr);
		}
		return true;
	}

	for (const auto palettePanel : tool_bars) {
		if (palettePanel->SelectBrush(whatBrush)) {
			panel->SelectBrush(nullptr);
			return true;
		}
	}

	for (auto pageIndex = 0; pageIndex < choicebook->GetPageCount(); ++pageIndex) {
		if (pageIndex == choicebook->GetSelection()) {
			continue;
		}

		panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(pageIndex));
		if (panel && panel->SelectBrush(whatBrush)) {
			choicebook->ChangeSelection(pageIndex);
			for (const auto palettePanel : tool_bars) {
				palettePanel->SelectBrush(nullptr);
			}
			return true;
		}
	}
	return false;
}

void BrushPalettePanel::OnSwitchingPage(wxChoicebookEvent &event) {
	event.Skip();
	if (!choicebook) {
		return;
	}
	if (const auto oldPanel = dynamic_cast<BrushPanel*>(choicebook->GetCurrentPage()); oldPanel) {
		oldPanel->OnSwitchOut();
		for (const auto palettePanel : tool_bars) {
			const auto brush = palettePanel->GetSelectedBrush();
			if (brush) {
				rememberedBrushes[oldPanel] = brush;
			}
		}
	}

	const auto page = choicebook->GetPage(event.GetSelection());
	const auto panel = dynamic_cast<BrushPanel*>(page);
	if (panel) {
		panel->OnSwitchIn();
		for (const auto palettePanel : tool_bars) {
			palettePanel->SelectBrush(rememberedBrushes[panel]);
		}
	}
}

void BrushPalettePanel::OnPageChanged(wxChoicebookEvent &event) {
	if (!choicebook) {
		return;
	}
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush();
}

void BrushPalettePanel::OnSwitchIn() {
	LoadCurrentContents();
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SetBrushSizeInternal(last_brush_size);
	OnUpdateBrushSize(g_gui.GetBrushShape(), last_brush_size);
}

void BrushPalettePanel::OnClickAddTileset(wxCommandEvent &WXUNUSED(event)) {
	if (!choicebook) {
		return;
	}

	const auto window = newd AddTilesetWindow(g_gui.root, paletteType);
	const auto result = window->ShowModal();
	window->Destroy();

	if (result != 0) {
		g_gui.DestroyPalettes();
		g_gui.NewPalette();
	}
}

void BrushPalettePanel::OnClickAddItemToTileset(wxCommandEvent &WXUNUSED(event)) {
	if (!choicebook) {
		return;
	}
	const auto &tilesetName = choicebook->GetPageText(choicebook->GetSelection()).ToStdString();

	const auto it = g_materials.tilesets.find(tilesetName);

	if (it != g_materials.tilesets.end()) {
		const auto window = newd AddItemWindow(g_gui.root, paletteType, it->second);
		const auto result = window->ShowModal();
		window->Destroy();

		if (result != 0) {
			g_gui.RebuildPalettes();
		}
	}
}

// ============================================================================
// Brush Panel
// A container of brush buttons

BEGIN_EVENT_TABLE(BrushPanel, wxPanel)
// Listbox style
EVT_LISTBOX(wxID_ANY, BrushPanel::OnClickListBoxRow)
END_EVENT_TABLE()

BrushPanel::BrushPanel(wxWindow* parent, const TilesetCategory* tileset) :
	wxPanel(parent, wxID_ANY), tileset(tileset) {
	SetSizerAndFit(sizer);
}

void BrushPanel::AssignTileset(const TilesetCategory* newTileset) {
	if (newTileset != tileset) {
		InvalidateContents();
		tileset = newTileset;
	}
}

void BrushPanel::SetListType(BrushListType newListType) {
	if (listType != newListType) {
		InvalidateContents();
		listType = newListType;
	}
}

void BrushPanel::SetListType(const wxString &newListType) {
	const auto it = listTypeMap.find(newListType);
	if (it != listTypeMap.end()) {
		SetListType(it->second);
	}
}

void BrushPanel::InvalidateContents() {
	sizer->Clear(true);
	loaded = false;
	brushbox = nullptr;
}

void BrushPanel::LoadContents() {
	if (loaded) {
		return;
	}
	loaded = true;
	ASSERT(tileset != nullptr);
	switch (listType) {
		case BRUSHLIST_LARGE_ICONS:
			brushbox = newd BrushIconBox(this, tileset, RENDER_SIZE_32x32);
			break;
		case BRUSHLIST_SMALL_ICONS:
			brushbox = newd BrushIconBox(this, tileset, RENDER_SIZE_16x16);
			break;
		case BRUSHLIST_LISTBOX:
			brushbox = newd BrushListBox(this, tileset);
			break;
		default:
			break;
	}
	ASSERT(brushbox != nullptr);
	sizer->Add(brushbox->GetSelfWindow(), 1, wxEXPAND);
	Fit();
	brushbox->SelectFirstBrush();
}

void BrushPanel::SelectFirstBrush() {
	if (loaded) {
		ASSERT(brushbox != nullptr);
		brushbox->SelectFirstBrush();
	}
}

Brush* BrushPanel::GetSelectedBrush() const {
	if (loaded) {
		ASSERT(brushbox != nullptr);
		return brushbox->GetSelectedBrush();
	}

	if (tileset && tileset->size() > 0) {
		return tileset->brushlist[0];
	}
	return nullptr;
}

bool BrushPanel::SelectBrush(const Brush* whatBrush) {
	if (loaded) {
		// std::cout << loaded << std::endl;
		// std::cout << brushbox << std::endl;
		ASSERT(brushbox != nullptr);
		return brushbox->SelectBrush(whatBrush);
	}

	for (const auto brush : tileset->brushlist) {
		if (brush == whatBrush) {
			LoadContents();
			return brushbox->SelectBrush(whatBrush);
		}
	}
	return false;
}

void BrushPanel::OnSwitchIn() {
	LoadContents();
}

void BrushPanel::OnSwitchOut() {
	////
}

void BrushPanel::OnClickListBoxRow(wxCommandEvent &event) {
	ASSERT(tileset->getType() >= TILESET_UNKNOWN && tileset->getType() <= TILESET_HOUSE);
	// We just notify the GUI of the action, it will take care of everything else
	ASSERT(brushbox);
	const auto index = event.GetSelection();

	if (const auto paletteWindow = g_gui.GetParentWindowByType<PaletteWindow*>(this); paletteWindow != nullptr) {
		g_gui.ActivatePalette(paletteWindow);
	}

	g_gui.SelectBrush(tileset->brushlist[index], tileset->getType());
}

// ============================================================================
// BrushIconBox

BEGIN_EVENT_TABLE(BrushIconBox, wxScrolledWindow)
// Listbox style
EVT_TOGGLEBUTTON(wxID_ANY, BrushIconBox::OnClickBrushButton)
END_EVENT_TABLE()

BrushIconBox::BrushIconBox(wxWindow* parent, const TilesetCategory* tileset, RenderSize rsz) :
	wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL),
	BrushBoxInterface(tileset),
	iconSize(rsz) {
	ASSERT(tileset->getType() >= TILESET_UNKNOWN && tileset->getType() <= TILESET_HOUSE);
	const auto width = iconSize == RENDER_SIZE_32x32 ? std::max(g_settings.getInteger(Config::PALETTE_COL_COUNT) / 2 + 1, 1) : std::max(g_settings.getInteger(Config::PALETTE_COL_COUNT) + 1, 1);

	// Create buttons
	stacksizer = newd wxBoxSizer(wxVERTICAL);
	SetSizer(stacksizer);

	SetScrollbars(20, 20, 8, tileset->brushlist.size() / width, 0, 0, false);

	auto rowsizer = newd wxBoxSizer(wxHORIZONTAL);

	for (const auto brush : tileset->brushlist) {
		const auto brushButton = newd BrushButton(this, brush, rsz);
		rowsizer->Add(brushButton);
		brushButtons.emplace_back(brushButton);

		if (brushButtons.size() % width == 0) { // new row
			stacksizer->Add(rowsizer);
			rowsizers.emplace_back(rowsizer);
			rowsizer = newd wxBoxSizer(wxHORIZONTAL);
		}
	}

	if (rowsizers.size() <= 0 || rowsizer != rowsizers.back()) {
		stacksizer->Add(rowsizer);
	}
}

void BrushIconBox::SelectFirstBrush() {
	if (tileset && tileset->size() > 0) {
		Select(brushButtons[0]);
	}
}

Brush* BrushIconBox::GetSelectedBrush() const {
	if (!tileset) {
		return nullptr;
	}

	return selectedButton ? selectedButton->brush : nullptr;
}

bool BrushIconBox::SelectBrush(const Brush* whatBrush) {
	Deselect();

	if (!whatBrush) {
		return false;
	}

	const auto it = std::ranges::find_if(brushButtons.begin(), brushButtons.end(), [&](const auto brushButton) {
		return brushButton->brush == whatBrush;
	});

	if (it != brushButtons.end()) {
		Select(*it);
		return true;
	}

	return false;
}

void BrushIconBox::Select(BrushButton* brushButton) {
	Deselect();
	selectedButton = brushButton;
	selectedButton->SetValue(true);
	EnsureVisible(selectedButton);
}

void BrushIconBox::Deselect() {
	if (selectedButton != nullptr) {
		selectedButton->SetValue(false);
		selectedButton = nullptr;
	}
}

void BrushIconBox::EnsureVisible(const BrushButton* whatBrush) {
	int windowSizeX, windowSizeY;
	GetVirtualSize(&windowSizeX, &windowSizeY);

	int scrollUnitX;
	int scrollUnitY;
	GetScrollPixelsPerUnit(&scrollUnitX, &scrollUnitY);

	const auto &rect = whatBrush->GetRect();
	int y;
	CalcUnscrolledPosition(0, rect.y, nullptr, &y);

	const auto maxScrollPos = windowSizeY / scrollUnitY;
	const auto scrollPosY = std::min(maxScrollPos, (y / scrollUnitY));

	int startScrollPosY;
	GetViewStart(nullptr, &startScrollPosY);

	int clientSizeX, clientSizeY;
	GetClientSize(&clientSizeX, &clientSizeY);
	const auto endScrollPosY = startScrollPosY + clientSizeY / scrollUnitY;

	if (scrollPosY < startScrollPosY || scrollPosY > endScrollPosY) {
		// only scroll if the button isnt visible
		Scroll(-1, scrollPosY);
	}
}

void BrushIconBox::OnClickBrushButton(wxCommandEvent &event) {
	const auto eventObject = event.GetEventObject();
	const auto brushButton = dynamic_cast<BrushButton*>(eventObject);
	if (brushButton) {
		if (const auto paletteWindow = g_gui.GetParentWindowByType<PaletteWindow*>(this); paletteWindow) {
			g_gui.ActivatePalette(paletteWindow);
		}
		g_gui.SelectBrush(brushButton->brush, tileset->getType());
	}
}

// ============================================================================
// BrushListBox

BEGIN_EVENT_TABLE(BrushListBox, wxVListBox)
EVT_KEY_DOWN(BrushListBox::OnKey)
END_EVENT_TABLE()

BrushListBox::BrushListBox(wxWindow* parent, const TilesetCategory* tileset) :
	wxVListBox(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLB_SINGLE),
	BrushBoxInterface(tileset) {
	SetItemCount(tileset->size());
}

void BrushListBox::SelectFirstBrush() {
	SetSelection(0);
	wxWindow::ScrollLines(-1);
}

Brush* BrushListBox::GetSelectedBrush() const {
	if (!tileset) {
		return nullptr;
	}

	if (const auto index = GetSelection(); index != wxNOT_FOUND) {
		return tileset->brushlist[index];
	} else if (tileset->size() > 0) {
		return tileset->brushlist[0];
	}
	return nullptr;
}

bool BrushListBox::SelectBrush(const Brush* whatBrush) {
	for (auto index = 0; index < tileset->brushlist.size(); ++index) {
		if (tileset->brushlist[index] == whatBrush) {
			SetSelection(index);
			return true;
		}
	}
	return false;
}

void BrushListBox::OnDrawItem(wxDC &dc, const wxRect &rect, size_t index) const {
	ASSERT(index < tileset->size());
	if (const auto sprite = g_gui.gfx.getSprite(tileset->brushlist[index]->getLookID()); sprite) {
		sprite->DrawTo(&dc, SPRITE_SIZE_32x32, rect.GetX(), rect.GetY(), rect.GetWidth(), rect.GetHeight());
	}
	if (IsSelected(index)) {
		if (HasFocus()) {
			dc.SetTextForeground(wxColor(0xFF, 0xFF, 0xFF));
		} else {
			dc.SetTextForeground(wxColor(0x00, 0x00, 0xFF));
		}
	} else {
		dc.SetTextForeground(wxColor(0x00, 0x00, 0x00));
	}
	dc.DrawText(wxstr(tileset->brushlist[index]->getName()), rect.GetX() + 40, rect.GetY() + 6);
}

wxCoord BrushListBox::OnMeasureItem(size_t index) const {
	return 32;
}

void BrushListBox::OnKey(wxKeyEvent &event) {
	switch (event.GetKeyCode()) {
		case WXK_UP:
		case WXK_DOWN:
		case WXK_LEFT:
		case WXK_RIGHT:
			if (g_settings.getInteger(Config::LISTBOX_EATS_ALL_EVENTS)) {
				case WXK_PAGEUP:
				case WXK_PAGEDOWN:
				case WXK_HOME:
				case WXK_END:
					event.Skip(true);
			} else {
				[[fallthrough]];
				default:
					if (g_gui.GetCurrentTab() != nullptr) {
						g_gui.GetCurrentMapTab()->GetEventHandler()->AddPendingEvent(event);
					}
			}
	}
}
