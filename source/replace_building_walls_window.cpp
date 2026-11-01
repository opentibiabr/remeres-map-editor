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

#include <wx/checklst.h>
#include <wx/dcmemory.h>
#include <wx/statbmp.h>
#include <wx/tglbtn.h>

#include "replace_building_walls_window.h"

#include "brush.h"
#include "editor.h"
#include "graphics.h"
#include "gui.h"
#include "items.h"
#include "map_drawer.h"
#include "settings.h"
#include "wall_brush.h"

namespace {
	constexpr int previewPadding = 4;
	constexpr int dragThreshold = 3;
	constexpr size_t maxOpeningTypes = 7;

	int glAttributes[] = {
		WX_GL_RGBA,
		WX_GL_DOUBLEBUFFER,
		WX_GL_CORE_PROFILE,
		WX_GL_MAJOR_VERSION,
		3,
		WX_GL_MINOR_VERSION,
		3,
		0,
	};

	void drawItem(wxDC &dc, uint16_t id, int x, int y) {
		if (GameSprite* sprite = g_items.getItemType(id).sprite) {
			sprite->DrawTo(&dc, SPRITE_SIZE_32x32, x, y, rme::SpritePixels, rme::SpritePixels);
		}
	}

	wxBitmap makeItemBitmap(wxWindow* window, uint16_t id) {
		wxBitmap bitmap(rme::SpritePixels, rme::SpritePixels);
		wxMemoryDC dc(bitmap);
		dc.SetBackground(wxBrush(window->GetBackgroundColour()));
		dc.Clear();
		drawItem(dc, id, 0, 0);
		dc.SelectObject(wxNullBitmap);
		return bitmap;
	}

	wxStaticText* createHeader(wxWindow* parent, const wxString &text) {
		wxStaticText* header = newd wxStaticText(parent, wxID_ANY, text);
		header->SetFont(header->GetFont().Bold());
		return header;
	}

	wxString countOf(size_t count, const wxString &singular) {
		return wxString::Format("%zu %s%s", count, singular, count == 1 ? "" : "s");
	}

	wxString describeOpeningCount(const OpeningGroup &group) {
		return wxString::Format("%zu %s", group.count, getOpeningName(group.type, group.count != 1));
	}
}

WallStyleList::WallStyleList(wxWindow* parent) :
	wxVListBox(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLB_SINGLE) {
	////
}

void WallStyleList::setEntries(std::vector<WallStyleEntry> newEntries) {
	const WallBrush* selected = getSelectedBrush();

	entries = std::move(newEntries);
	SetItemCount(entries.size());

	int selection = wxNOT_FOUND;
	for (size_t index = 0; index < entries.size(); ++index) {
		if (entries[index].style.brush == selected) {
			selection = static_cast<int>(index);
			break;
		}
	}
	SetSelection(selection);
	Refresh();
}

WallBrush* WallStyleList::getSelectedBrush() const {
	const int selection = GetSelection();
	if (selection == wxNOT_FOUND || static_cast<size_t>(selection) >= entries.size()) {
		return nullptr;
	}
	return entries[selection].style.brush;
}

void WallStyleList::OnDrawItem(wxDC &dc, const wxRect &rect, size_t index) const {
	if (index >= entries.size()) {
		return;
	}

	const WallStyleEntry &entry = entries[index];
	const int padding = FromDIP(6);
	const int spriteTop = rect.GetY() + (rect.GetHeight() - rme::SpritePixels) / 2;

	drawItem(dc, entry.style.cornerItem, rect.GetX() + padding, spriteTop);
	drawItem(dc, entry.style.runItem, rect.GetX() + padding + rme::SpritePixels, spriteTop);

	// Without the focus the list marks its selection in a light color, where light text can not be read
	const bool selected = IsSelected(index) && HasFocus();
	const int textX = rect.GetX() + padding * 2 + rme::SpritePixels * 2;
	const wxColour text = wxSystemSettings::GetColour(selected ? wxSYS_COLOUR_HIGHLIGHTTEXT : wxSYS_COLOUR_LISTBOXTEXT);

	dc.SetFont(GetFont());
	dc.SetTextForeground(text);
	const wxString name = wxstr(entry.style.brush->getName());
	const int nameHeight = dc.GetTextExtent(name).GetHeight();
	const int textTop = rect.GetY() + (rect.GetHeight() - nameHeight * 2) / 2;
	dc.DrawText(name, textX, textTop);

	dc.SetTextForeground(selected ? text : wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
	dc.DrawText(entry.note, textX, textTop + nameHeight);
}

wxCoord WallStyleList::OnMeasureItem(size_t WXUNUSED(index)) const {
	return rme::SpritePixels + FromDIP(12);
}

BEGIN_EVENT_TABLE(BuildingPreviewCanvas, MapCanvas)
EVT_SIZE(BuildingPreviewCanvas::OnSize)
EVT_KEY_DOWN(BuildingPreviewCanvas::OnIgnoredKey)
EVT_KEY_UP(BuildingPreviewCanvas::OnIgnoredKey)
EVT_MOTION(BuildingPreviewCanvas::OnPreviewDrag)
EVT_LEFT_DOWN(BuildingPreviewCanvas::OnMouseDown)
EVT_LEFT_UP(BuildingPreviewCanvas::OnLeftUp)
EVT_LEFT_DCLICK(BuildingPreviewCanvas::OnIgnoredMouse)
EVT_MIDDLE_DOWN(BuildingPreviewCanvas::OnMouseDown)
EVT_MIDDLE_UP(BuildingPreviewCanvas::OnMouseUp)
EVT_RIGHT_DOWN(BuildingPreviewCanvas::OnMouseDown)
EVT_RIGHT_UP(BuildingPreviewCanvas::OnMouseUp)
EVT_MOUSEWHEEL(BuildingPreviewCanvas::OnPreviewZoom)
EVT_MOUSE_CAPTURE_LOST(BuildingPreviewCanvas::OnCaptureLost)
EVT_ENTER_WINDOW(BuildingPreviewCanvas::OnIgnoredMouse)
EVT_LEAVE_WINDOW(BuildingPreviewCanvas::OnIgnoredMouse)
EVT_PAINT(MapCanvas::OnPaint)
EVT_ERASE_BACKGROUND(MapCanvas::OnEraseBackground)
END_EVENT_TABLE()

BuildingPreviewCanvas::BuildingPreviewCanvas(wxWindow* parent, Editor &editor, const Position &origin, std::function<void(const Position &)> onWallClick) :
	MapCanvas(parent, editor, glAttributes),
	onWallClick(std::move(onWallClick)) {
	SetMinSize(FromDIP(wxSize(420, 420)));
	SetPreviewMap(&previewMap);
	CenterViewOnPosition(origin);
	ChangeFloor(origin.z);
}

void BuildingPreviewCanvas::setComparing(bool comparing) {
	SetPreviewMap(comparing ? nullptr : &previewMap);
}

void BuildingPreviewCanvas::fitTiles(const std::set<Position> &tiles) {
	if (tiles.empty()) {
		return;
	}

	const wxSize size = GetClientSize();
	if (size.GetWidth() <= 0 || size.GetHeight() <= 0) {
		// The window has no size until the dialog is laid out
		pendingFit = tiles;
		return;
	}
	pendingFit.clear();

	int minX = tiles.begin()->x;
	int minY = tiles.begin()->y;
	int maxX = minX;
	int maxY = minY;
	for (const Position &position : tiles) {
		minX = std::min(minX, position.x);
		minY = std::min(minY, position.y);
		maxX = std::max(maxX, position.x);
		maxY = std::max(maxY, position.y);
	}

	const double scale = GetContentScaleFactor();
	const double widthZoom = static_cast<double>((maxX - minX + previewPadding) * rme::TileSize) / (size.GetWidth() * scale);
	const double heightZoom = static_cast<double>((maxY - minY + previewPadding) * rme::TileSize) / (size.GetHeight() * scale);
	SetZoom(std::max(1.0, std::max(widthZoom, heightZoom)));

	CenterViewOnPosition(Position((minX + maxX) / 2, (minY + maxY) / 2, GetFloor()));
	Refresh();
}

void BuildingPreviewCanvas::showFloor(int floor) {
	// Floors are drawn with an offset, the view has to follow it to stay on the same tiles
	int centerX;
	int centerY;
	GetScreenCenter(&centerX, &centerY);
	ChangeFloor(floor);
	CenterViewOnPosition(Position(centerX, centerY, floor));
	Refresh();
}

void BuildingPreviewCanvas::ScreenToMap(int screen_x, int screen_y, int* map_x, int* map_y) {
	const double scale = GetContentScaleFactor();
	*map_x = static_cast<int>(viewX + screen_x * scale * GetZoom()) / rme::TileSize;
	*map_y = static_cast<int>(viewY + screen_y * scale * GetZoom()) / rme::TileSize;
	if (GetFloor() <= rme::MapGroundLayer) {
		*map_x += rme::MapGroundLayer - GetFloor();
		*map_y += rme::MapGroundLayer - GetFloor();
	}
}

void BuildingPreviewCanvas::GetScreenCenter(int* map_x, int* map_y) {
	const wxSize size = GetClientSize();
	ScreenToMap(size.GetWidth() / 2, size.GetHeight() / 2, map_x, map_y);
}

void BuildingPreviewCanvas::GetViewBox(int* view_scroll_x, int* view_scroll_y, int* screensize_x, int* screensize_y) const {
	const wxSize size = GetClientSize();
	*screensize_x = size.GetWidth();
	*screensize_y = size.GetHeight();
	*view_scroll_x = viewX;
	*view_scroll_y = viewY;
}

void BuildingPreviewCanvas::UpdatePositionStatus(int WXUNUSED(x), int WXUNUSED(y)) {
	////
}

void BuildingPreviewCanvas::UpdateZoomStatus() {
	////
}

void BuildingPreviewCanvas::ConfigureDrawingOptions(DrawingOptions &options) {
	options.SetIngame();
	options.show_monsters = false;
	options.show_npcs = false;
}

void BuildingPreviewCanvas::CenterViewOnPosition(const Position &position) {
	const wxSize size = GetClientSize();
	const double scale = GetContentScaleFactor();
	const int floorOffset = position.z <= rme::MapGroundLayer ? (rme::MapGroundLayer - position.z) * rme::TileSize : 0;
	viewX = position.x * rme::TileSize - floorOffset - static_cast<int>(size.GetWidth() * scale * GetZoom() / 2.0);
	viewY = position.y * rme::TileSize - floorOffset - static_cast<int>(size.GetHeight() * scale * GetZoom() / 2.0);
}

void BuildingPreviewCanvas::OnFloorChanged() {
	////
}

void BuildingPreviewCanvas::OnSize(wxSizeEvent &event) {
	if (!pendingFit.empty()) {
		fitTiles(std::set<Position>(pendingFit));
	}
	event.Skip();
}

void BuildingPreviewCanvas::OnPreviewDrag(wxMouseEvent &event) {
	if (!event.LeftIsDown() && !event.MiddleIsDown() && !event.RightIsDown()) {
		return;
	}

	const wxPoint point = event.GetPosition();
	const double step = GetContentScaleFactor() * GetZoom();
	viewX -= static_cast<int>((point.x - lastPoint.x) * step);
	viewY -= static_cast<int>((point.y - lastPoint.y) * step);
	lastPoint = point;

	if (std::abs(point.x - pressPoint.x) > dragThreshold || std::abs(point.y - pressPoint.y) > dragThreshold) {
		dragged = true;
	}
	Refresh();
}

void BuildingPreviewCanvas::OnPreviewZoom(wxMouseEvent &event) {
	const double oldZoom = GetZoom();
	const double nextZoom = oldZoom - event.GetWheelRotation() * g_settings.getFloat(Config::ZOOM_SPEED) / 640.0;
	const double scale = GetContentScaleFactor();
	const double anchorX = viewX + event.GetX() * scale * oldZoom;
	const double anchorY = viewY + event.GetY() * scale * oldZoom;
	SetZoom(nextZoom);
	viewX = static_cast<int>(anchorX - event.GetX() * scale * GetZoom());
	viewY = static_cast<int>(anchorY - event.GetY() * scale * GetZoom());
	Refresh();
}

void BuildingPreviewCanvas::OnMouseDown(wxMouseEvent &event) {
	pressPoint = event.GetPosition();
	lastPoint = pressPoint;
	dragged = false;
	if (!HasCapture()) {
		CaptureMouse();
	}
}

void BuildingPreviewCanvas::OnLeftUp(wxMouseEvent &event) {
	if (HasCapture()) {
		ReleaseMouse();
	}
	if (!dragged) {
		int x;
		int y;
		ScreenToMap(event.GetX(), event.GetY(), &x, &y);
		onWallClick(Position(x, y, GetFloor()));
	}
}

void BuildingPreviewCanvas::OnMouseUp(wxMouseEvent &WXUNUSED(event)) {
	if (HasCapture()) {
		ReleaseMouse();
	}
}

void BuildingPreviewCanvas::OnCaptureLost(wxMouseCaptureLostEvent &WXUNUSED(event)) {
	dragged = true;
}

void BuildingPreviewCanvas::OnIgnoredMouse(wxMouseEvent &WXUNUSED(event)) {
	////
}

void BuildingPreviewCanvas::OnIgnoredKey(wxKeyEvent &WXUNUSED(event)) {
	// Never skipped: the handlers of the base canvas would edit the map behind the dialog
}

ReplaceBuildingWallsDialog::ReplaceBuildingWallsDialog(wxWindow* parent, Editor &editor, const Position &origin) :
	wxDialog(parent, wxID_ANY, "Replace Building Walls", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMAXIMIZE_BOX),
	editor(editor),
	origin(origin) {
	wxBoxSizer* content = newd wxBoxSizer(wxHORIZONTAL);
	content->Add(createStylePanel(), 0, wxEXPAND | wxLEFT | wxTOP, FromDIP(12));
	content->Add(createPreviewPanel(), 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
	content->Add(createBuildingPanel(), 0, wxEXPAND | wxRIGHT | wxTOP, FromDIP(12));

	summary = newd wxStaticText(this, wxID_ANY, wxEmptyString);

	applyButton = newd wxButton(this, wxID_OK, "Replace Walls");
	applyButton->SetDefault();
	applyButton->Enable(false);

	wxBoxSizer* footer = newd wxBoxSizer(wxHORIZONTAL);
	footer->Add(summary, 1, wxALIGN_CENTER_VERTICAL);
	footer->Add(newd wxButton(this, wxID_CANCEL, "Cancel"), 0, wxLEFT, FromDIP(8));
	footer->Add(applyButton, 0, wxLEFT, FromDIP(8));

	wxBoxSizer* root = newd wxBoxSizer(wxVERTICAL);
	root->Add(content, 1, wxEXPAND);
	root->Add(footer, 0, wxEXPAND | wxALL, FromDIP(12));
	SetSizer(root);

	SetMinSize(FromDIP(wxSize(1000, 620)));
	SetSize(FromDIP(wxSize(1240, 780)));
	CentreOnParent();

	Bind(wxEVT_CHAR_HOOK, &ReplaceBuildingWallsDialog::OnCharHook, this);
	applyButton->Bind(wxEVT_BUTTON, &ReplaceBuildingWallsDialog::OnApply, this);

	rescan();
	styleList->SetFocus();
}

wxPanel* ReplaceBuildingWallsDialog::createStylePanel() {
	stylePanel = newd wxPanel(this);
	wxBoxSizer* sizer = newd wxBoxSizer(wxVERTICAL);

	sizer->Add(createHeader(stylePanel, "New wall style"), 0, wxBOTTOM, FromDIP(6));

	search = newd wxTextCtrl(stylePanel, wxID_ANY);
	search->SetHint("Search styles");
	sizer->Add(search, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

	completeOnly = newd wxCheckBox(stylePanel, wxID_ANY, "Only styles with every door and window");
	completeOnly->SetValue(true);
	sizer->Add(completeOnly, 0, wxBOTTOM, FromDIP(8));

	styleList = newd WallStyleList(stylePanel);
	styleList->SetMinSize(FromDIP(wxSize(290, 200)));
	sizer->Add(styleList, 1, wxEXPAND);

	emptyState = newd wxPanel(stylePanel);
	wxBoxSizer* emptySizer = newd wxBoxSizer(wxVERTICAL);
	emptyText = newd wxStaticText(emptyState, wxID_ANY, wxEmptyString);
	emptySizer->Add(emptyText, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
	emptyAction = newd wxButton(emptyState, wxID_ANY, wxEmptyString);
	emptySizer->Add(emptyAction, 0);
	emptyState->SetSizer(emptySizer);
	emptyState->Hide();
	sizer->Add(emptyState, 1, wxEXPAND);

	stylePanel->SetSizer(sizer);

	search->Bind(wxEVT_TEXT, &ReplaceBuildingWallsDialog::OnFilterChanged, this);
	completeOnly->Bind(wxEVT_CHECKBOX, &ReplaceBuildingWallsDialog::OnFilterChanged, this);
	emptyAction->Bind(wxEVT_BUTTON, &ReplaceBuildingWallsDialog::OnEmptyAction, this);
	styleList->Bind(wxEVT_LISTBOX, &ReplaceBuildingWallsDialog::OnStyleSelected, this);
	return stylePanel;
}

wxPanel* ReplaceBuildingWallsDialog::createPreviewPanel() {
	wxPanel* panel = newd wxPanel(this);
	wxBoxSizer* sizer = newd wxBoxSizer(wxVERTICAL);

	floorHeader = createHeader(panel, wxEmptyString);

	wxBitmapButton* up = newd wxBitmapButton(panel, wxID_ANY, wxArtProvider::GetBitmap(wxART_GO_UP, wxART_BUTTON, FromDIP(wxSize(16, 16))));
	up->SetToolTip("Show the floor above (+)");
	wxBitmapButton* down = newd wxBitmapButton(panel, wxID_ANY, wxArtProvider::GetBitmap(wxART_GO_DOWN, wxART_BUTTON, FromDIP(wxSize(16, 16))));
	down->SetToolTip("Show the floor below (-)");

	compareButton = newd wxToggleButton(panel, wxID_ANY, "Show Current Walls");
	compareButton->SetToolTip("Compare with the walls on the map (C)");
	wxButton* fit = newd wxButton(panel, wxID_ANY, "Fit Building");
	fit->SetToolTip("Show the whole building (Home)");

	wxBoxSizer* toolbar = newd wxBoxSizer(wxHORIZONTAL);
	toolbar->Add(floorHeader, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
	toolbar->Add(up, 0, wxRIGHT, FromDIP(2));
	toolbar->Add(down, 0);
	toolbar->AddStretchSpacer();
	toolbar->Add(compareButton, 0, wxRIGHT, FromDIP(6));
	toolbar->Add(fit, 0);
	sizer->Add(toolbar, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

	preview = newd BuildingPreviewCanvas(panel, editor, origin, [this](const Position &clicked) {
		toggleWall(clicked);
	});
	sizer->Add(preview, 1, wxEXPAND);

	wxStaticText* hint = newd wxStaticText(panel, wxID_ANY, "Click a wall to include or leave it out. Drag to move, scroll to zoom.");
	hint->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
	sizer->Add(hint, 0, wxTOP, FromDIP(6));

	panel->SetSizer(sizer);

	up->Bind(wxEVT_BUTTON, &ReplaceBuildingWallsDialog::OnFloorUp, this);
	down->Bind(wxEVT_BUTTON, &ReplaceBuildingWallsDialog::OnFloorDown, this);
	compareButton->Bind(wxEVT_TOGGLEBUTTON, &ReplaceBuildingWallsDialog::OnCompare, this);
	fit->Bind(wxEVT_BUTTON, &ReplaceBuildingWallsDialog::OnFit, this);
	return panel;
}

wxPanel* ReplaceBuildingWallsDialog::createBuildingPanel() {
	buildingPanel = newd wxPanel(this);
	buildingPanel->SetMinSize(FromDIP(wxSize(250, -1)));
	wxBoxSizer* sizer = newd wxBoxSizer(wxVERTICAL);

	const wxString scopes[] = { "This room", "This house", "This building", "All connected walls" };
	scopeChoice = newd wxRadioBox(buildingPanel, wxID_ANY, "Replace in", wxDefaultPosition, wxDefaultSize, 4, scopes, 1, wxRA_SPECIFY_COLS);
	scopeChoice->SetSelection(std::to_underlying(scope));
	sizer->Add(scopeChoice, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

	otherSideButton = newd wxButton(buildingPanel, wxID_ANY, "Use the Other Side of This Wall");
	otherSideButton->SetToolTip("This wall separates two rooms. Switch to the room on its other side.");
	sizer->Add(otherSideButton, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

	scopeNote = newd wxStaticText(buildingPanel, wxID_ANY, wxEmptyString);
	scopeNote->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
	sizer->Add(scopeNote, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

	sizer->Add(createHeader(buildingPanel, "Floors"), 0, wxTOP | wxBOTTOM, FromDIP(6));
	floorList = newd wxCheckListBox(buildingPanel, wxID_ANY);
	floorList->SetMinSize(FromDIP(wxSize(-1, 80)));
	sizer->Add(floorList, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

	sourceHeader = createHeader(buildingPanel, "Walls to replace");
	sizer->Add(sourceHeader, 0, wxTOP | wxBOTTOM, FromDIP(6));
	sourceList = newd wxCheckListBox(buildingPanel, wxID_ANY);
	sourceList->SetMinSize(FromDIP(wxSize(-1, 64)));
	sizer->Add(sourceList, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

	sharedCheck = newd wxCheckBox(buildingPanel, wxID_ANY, "Include walls shared with neighbors");
	sharedCheck->SetValue(true);
	sizer->Add(sharedCheck, 0, wxTOP | wxBOTTOM, FromDIP(6));

	openingHeader = createHeader(buildingPanel, "Doors and windows");
	sizer->Add(openingHeader, 0, wxTOP | wxBOTTOM, FromDIP(6));

	for (size_t index = 0; index < maxOpeningTypes; ++index) {
		OpeningRow row;
		row.icon = newd wxStaticBitmap(buildingPanel, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(rme::SpritePixels, rme::SpritePixels));
		row.label = newd wxStaticText(buildingPanel, wxID_ANY, wxEmptyString);
		row.action = newd wxChoice(buildingPanel, wxID_ANY);
		row.action->Bind(wxEVT_CHOICE, &ReplaceBuildingWallsDialog::OnOpeningAction, this);

		wxBoxSizer* text = newd wxBoxSizer(wxVERTICAL);
		text->Add(row.label, 0, wxBOTTOM, FromDIP(2));
		text->Add(row.action, 0, wxEXPAND);

		wxBoxSizer* line = newd wxBoxSizer(wxHORIZONTAL);
		line->Add(row.icon, 0, wxRIGHT, FromDIP(6));
		line->Add(text, 1, wxALIGN_CENTER_VERTICAL);
		sizer->Add(line, 0, wxEXPAND | wxBOTTOM, FromDIP(4));

		openingRows.push_back(std::move(row));
	}

	railingHeader = createHeader(buildingPanel, "Railings");
	sizer->Add(railingHeader, 0, wxTOP | wxBOTTOM, FromDIP(6));

	railingIcon = newd wxStaticBitmap(buildingPanel, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(rme::SpritePixels, rme::SpritePixels));
	railingLabel = newd wxStaticText(buildingPanel, wxID_ANY, wxEmptyString);
	railingAction = newd wxChoice(buildingPanel, wxID_ANY);
	railingAction->Bind(wxEVT_CHOICE, &ReplaceBuildingWallsDialog::OnRailingAction, this);

	wxBoxSizer* railingText = newd wxBoxSizer(wxVERTICAL);
	railingText->Add(railingLabel, 0, wxBOTTOM, FromDIP(2));
	railingText->Add(railingAction, 0, wxEXPAND);

	wxBoxSizer* railingLine = newd wxBoxSizer(wxHORIZONTAL);
	railingLine->Add(railingIcon, 0, wxRIGHT, FromDIP(6));
	railingLine->Add(railingText, 1, wxALIGN_CENTER_VERTICAL);
	sizer->Add(railingLine, 0, wxEXPAND | wxBOTTOM, FromDIP(4));

	buildingPanel->SetSizer(sizer);

	scopeChoice->Bind(wxEVT_RADIOBOX, &ReplaceBuildingWallsDialog::OnScopeChanged, this);
	otherSideButton->Bind(wxEVT_BUTTON, &ReplaceBuildingWallsDialog::OnOtherSide, this);
	floorList->Bind(wxEVT_LISTBOX, &ReplaceBuildingWallsDialog::OnFloorSelected, this);
	floorList->Bind(wxEVT_CHECKLISTBOX, &ReplaceBuildingWallsDialog::OnSelectionChanged, this);
	sourceList->Bind(wxEVT_CHECKLISTBOX, &ReplaceBuildingWallsDialog::OnSelectionChanged, this);
	sharedCheck->Bind(wxEVT_CHECKBOX, &ReplaceBuildingWallsDialog::OnSelectionChanged, this);
	return buildingPanel;
}

void ReplaceBuildingWallsDialog::rescan() {
	scan = scanBuilding(editor.getMap(), origin, scope, side);
	excluded.clear();

	houseFound = houseFound || scan.house != 0;
	if (scope == BuildingScope::House && scan.house == 0) {
		scope = BuildingScope::Building;
	}
	scopeChoice->Enable(static_cast<unsigned int>(std::to_underlying(BuildingScope::House)), houseFound);
	scopeChoice->SetSelection(std::to_underlying(scope));

	if (styles.empty()) {
		styles = collectWallStyles(scan.originBrush);
	}

	rebuildFloorList();
	rebuildSourceList();
	updateScopeNote();

	bool hasShared = false;
	for (const BuildingFloor &floor : scan.floors) {
		hasShared = hasShared || std::any_of(floor.walls.begin(), floor.walls.end(), [](const BuildingWall &wall) { return wall.shared; });
	}
	sharedCheck->Show(hasShared);

	refreshStyles();
	updateFloorHeader();
	preview->showFloor(scan.floors.empty() ? origin.z : scan.floors[shownFloor].z);
	preview->fitTiles(scan.tiles);
	buildingPanel->Layout();
}

void ReplaceBuildingWallsDialog::rebuildFloorList() {
	floorList->Clear();
	shownFloor = 0;

	for (size_t index = 0; index < scan.floors.size(); ++index) {
		const BuildingFloor &floor = scan.floors[index];
		floorList->Append(wxString::Format("Floor %d  (%s)", floor.z, countOf(floor.walls.size(), "piece")));
		floorList->Check(static_cast<unsigned int>(index), true);
		if (floor.z == origin.z) {
			shownFloor = static_cast<int>(index);
		}
	}

	if (!scan.floors.empty()) {
		floorList->SetSelection(shownFloor);
	}
}

void ReplaceBuildingWallsDialog::rebuildSourceList() {
	const bool lowOrigin = isLowWallStyle(scan.originBrush);

	std::map<WallBrush*, size_t> counts;
	std::set<WallBrush*> ownStyles = { scan.originBrush };
	for (const BuildingFloor &floor : scan.floors) {
		for (const BuildingWall &wall : floor.walls) {
			if (wall.low == lowOrigin) {
				++counts[wall.brush];
				if (!wall.shared) {
					ownStyles.insert(wall.brush);
				}
			}
		}
	}

	sourceBrushes.clear();
	sourceList->Clear();
	for (const auto &entry : counts) {
		sourceBrushes.push_back(entry.first);
		sourceList->Append(wxString::Format("%s  (%zu)", wxstr(entry.first->getName()), entry.second));
		// A style found only on walls shared with neighbours is theirs until someone says otherwise
		sourceList->Check(sourceList->GetCount() - 1, ownStyles.contains(entry.first));
	}

	const bool several = sourceBrushes.size() > 1;
	sourceHeader->Show(several);
	sourceList->Show(several);
}

WallReplacementRequest ReplaceBuildingWallsDialog::makeRequest(WallBrush* target) const {
	WallReplacementRequest request;
	request.target = target;
	request.excluded = excluded;
	request.includeShared = sharedCheck->GetValue() || !sharedCheck->IsShown();
	request.replaceRailings = !keepRailings;
	request.openingActions = openingActions;

	for (size_t index = 0; index < scan.floors.size(); ++index) {
		if (floorList->IsChecked(static_cast<unsigned int>(index))) {
			request.floors.insert(scan.floors[index].z);
		}
	}
	for (size_t index = 0; index < sourceBrushes.size(); ++index) {
		if (sourceList->IsChecked(static_cast<unsigned int>(index))) {
			request.sources.insert(sourceBrushes[index]);
		}
	}
	return request;
}

void ReplaceBuildingWallsDialog::refreshStyles() {
	const std::string filter = as_lower_str(nstr(search->GetValue()));
	const bool onlyComplete = completeOnly->IsChecked();

	std::vector<WallStyleEntry> complete;
	std::vector<WallStyleEntry> partial;
	for (const WallStyle &style : styles) {
		if (style.brush == scan.originBrush && sourceBrushes.size() == 1) {
			continue;
		}
		if (!filter.empty() && !as_lower_str(style.brush->getName()).contains(filter)) {
			continue;
		}

		const WallReplacementPlan candidate = planWallReplacement(scan, makeRequest(style.brush));
		if (!candidate.isUsable()) {
			continue;
		}

		if (candidate.isComplete()) {
			complete.push_back({ style, candidate.openings.empty() ? "Fits every wall" : "Has every door and window" });
		} else if (!onlyComplete) {
			partial.push_back({ style, wxString::Format("%s without a match", countOf(candidate.missingOpenings, "opening")) });
		}
	}

	complete.insert(complete.end(), partial.begin(), partial.end());
	const bool hasEntries = !complete.empty();
	styleList->setEntries(std::move(complete));

	if (!hasEntries) {
		if (!filter.empty()) {
			emptyText->SetLabel("No wall style has that name.");
			emptyAction->SetLabel("Clear Search");
		} else if (onlyComplete) {
			emptyText->SetLabel("No style has every door and window of this building.");
			emptyAction->SetLabel("Show Styles With Missing Openings");
		} else {
			emptyText->SetLabel("No style has the wall shapes of this building. Try a smaller area.");
		}
		emptyText->Wrap(FromDIP(280));
		emptyAction->Show(!filter.empty() || onlyComplete);
	}

	styleList->Show(hasEntries);
	emptyState->Show(!hasEntries);
	emptyState->Layout();
	stylePanel->Layout();

	refreshPlan();
}

void ReplaceBuildingWallsDialog::refreshPlan() {
	const WallReplacementRequest request = makeRequest(styleList->getSelectedBrush());
	plan = planWallReplacement(scan, request);

	writeWallPreview(editor.getMap(), plan, preview->getPreviewMap());
	updateLitTiles(request);
	updateOpenings();
	updateRailings();
	updateSummary();
	preview->Refresh();
}

void ReplaceBuildingWallsDialog::updateLitTiles(const WallReplacementRequest &request) {
	litTiles = scan.tiles;
	const bool lowOrigin = isLowWallStyle(scan.originBrush);

	for (const BuildingFloor &floor : scan.floors) {
		const bool floorSelected = request.floors.contains(floor.z);
		for (const BuildingWall &wall : floor.walls) {
			// Railings have no entry in the list of walls, and they are part of the building either way
			const bool listed = wall.low != lowOrigin || request.sources.contains(wall.brush);
			if (!floorSelected || !listed || request.excluded.contains(wall.position) || (wall.shared && !request.includeShared)) {
				litTiles.erase(wall.position);
			}
		}
	}

	for (auto it = litTiles.begin(); it != litTiles.end();) {
		it = !request.floors.contains(it->z) ? litTiles.erase(it) : std::next(it);
	}

	preview->SetPreviewFocus(&litTiles);
}

std::vector<OpeningGroup> ReplaceBuildingWallsDialog::collectOpenings() const {
	// The openings exist before a style is chosen, so they are always listed
	std::map<::DoorType, size_t> counts;
	std::map<::DoorType, uint16_t> samples;
	const WallReplacementRequest request = makeRequest(nullptr);
	for (const BuildingFloor &floor : scan.floors) {
		if (!request.floors.contains(floor.z)) {
			continue;
		}
		for (const BuildingWall &wall : floor.walls) {
			if (wall.opening != WALL_UNDEFINED && request.sources.contains(wall.brush) && !request.excluded.contains(wall.position)) {
				++counts[wall.opening];
				samples[wall.opening] = wall.itemId;
			}
		}
	}

	std::vector<OpeningGroup> groups = plan.openings;
	for (const auto &entry : counts) {
		const bool planned = std::any_of(groups.begin(), groups.end(), [&entry](const OpeningGroup &group) { return group.type == entry.first; });
		if (!planned) {
			OpeningGroup group;
			group.type = entry.first;
			group.count = entry.second;
			group.sampleItem = samples[entry.first];
			groups.push_back(group);
		}
	}
	return groups;
}

void ReplaceBuildingWallsDialog::updateOpeningRow(OpeningRow &row, const OpeningGroup &group) {
	row.type = group.type;
	row.icon->SetBitmap(makeItemBitmap(buildingPanel, group.sampleItem));
	row.label->SetLabel(group.missing ? wxString::Format("%s, no match", describeOpeningCount(group)) : describeOpeningCount(group));

	if (!group.missing) {
		return;
	}

	row.actions = { OpeningAction::KeepCurrent };
	row.action->Clear();
	row.action->Append("Keep as they are");
	if (group.closestType != WALL_UNDEFINED) {
		row.actions.push_back(OpeningAction::UseClosest);
		row.action->Append(wxString::Format("Use %s", getOpeningName(group.closestType, true)));
	}
	row.actions.push_back(OpeningAction::ReplaceWithWall);
	row.action->Append("Close with a wall");

	const auto choice = openingActions.find(group.type);
	const auto selected = std::find(row.actions.begin(), row.actions.end(), choice != openingActions.end() ? choice->second : OpeningAction::KeepCurrent);
	row.action->SetSelection(selected != row.actions.end() ? static_cast<int>(selected - row.actions.begin()) : 0);
}

void ReplaceBuildingWallsDialog::updateOpenings() {
	const std::vector<OpeningGroup> groups = collectOpenings();

	openingHeader->Show(!groups.empty());
	for (size_t index = 0; index < openingRows.size(); ++index) {
		OpeningRow &row = openingRows[index];
		const bool visible = index < groups.size();
		row.icon->Show(visible);
		row.label->Show(visible);
		row.action->Show(visible && groups[index].missing);
		if (visible) {
			updateOpeningRow(row, groups[index]);
		}
	}
	buildingPanel->Layout();
}

void ReplaceBuildingWallsDialog::updateRailings() {
	const bool visible = plan.railingCount > 0;
	railingHeader->Show(visible);
	railingIcon->Show(visible);
	railingLabel->Show(visible);
	railingAction->Show(visible && plan.railing != nullptr);

	if (visible) {
		const uint16_t sample = plan.railing ? plan.railing->getWallItemID(WALL_HORIZONTAL, 0) : plan.railingSample;
		railingIcon->SetBitmap(makeItemBitmap(buildingPanel, sample));

		if (plan.railing) {
			railingLabel->SetLabel(countOf(plan.railingCount, "railing"));
			railingAction->Clear();
			railingAction->Append(wxString::Format("Use %s", wxstr(plan.railing->getName())));
			railingAction->Append("Keep as they are");
			railingAction->SetSelection(keepRailings ? 1 : 0);
		} else {
			railingLabel->SetLabel(wxString::Format("%s stay, no match", countOf(plan.railingCount, "railing")));
		}
	}
	buildingPanel->Layout();
}

void ReplaceBuildingWallsDialog::updateSummary() {
	const WallBrush* target = styleList->getSelectedBrush();
	const size_t total = plan.changes.size();

	wxString text;
	if (!target) {
		text = "Choose a style to see it on the building.";
	} else if (!plan.isUsable()) {
		text = plan.problem;
	} else if (total == 0) {
		text = "Nothing to replace with this selection.";
	} else {
		wxArrayString parts;
		if (plan.wallCount > 0) {
			parts.push_back(countOf(plan.wallCount, "wall"));
		}
		if (plan.doorCount > 0) {
			parts.push_back(countOf(plan.doorCount, "door"));
		}
		if (plan.windowCount > 0) {
			parts.push_back(countOf(plan.windowCount, "window"));
		}
		if (plan.railingsChanged > 0) {
			parts.push_back(countOf(plan.railingsChanged, "railing"));
		}
		text = wxString::Format("%s on %s", wxJoin(parts, ','), countOf(plan.floorCount, "floor"));
		text.Replace(",", ", ");
	}

	summary->SetLabel(text);
	applyButton->SetLabel(total > 0 ? wxString::Format("Replace %s", countOf(total, "Piece")) : wxString("Replace Walls"));
	applyButton->Enable(total > 0);
	Layout();
}

void ReplaceBuildingWallsDialog::updateFloorHeader() {
	floorHeader->SetLabel(scan.floors.empty() ? wxString("Preview") : wxString::Format("Floor %d", scan.floors[shownFloor].z));
}

void ReplaceBuildingWallsDialog::updateScopeNote() {
	otherSideButton->Show(scope != BuildingScope::ConnectedWalls && scan.sideCount > 1);

	wxString note;
	if (scope != BuildingScope::ConnectedWalls && !scan.enclosed) {
		note = "These walls close no room, so all the walls connected to them are selected.";
	} else if (scope == BuildingScope::House && !scan.houseName.empty()) {
		note = wxString::Format("Only the walls of %s.", wxstr(scan.houseName));
	} else if (scan.openToStreet) {
		note = "This building is open to the street, so its roof shows where it ends.";
	} else if (scan.unknownWalls > 0) {
		note = wxString::Format("%s without a wall style will stay as they are.", countOf(scan.unknownWalls, "piece"));
	}

	scopeNote->SetLabel(note);
	scopeNote->Wrap(FromDIP(240));
	scopeNote->Show(!note.empty());
}

void ReplaceBuildingWallsDialog::moveDisplayedFloor(int offset) {
	const int index = shownFloor + offset;
	if (index < 0 || index >= static_cast<int>(scan.floors.size())) {
		return;
	}

	shownFloor = index;
	floorList->SetSelection(shownFloor);
	updateFloorHeader();
	preview->showFloor(scan.floors[shownFloor].z);
}

void ReplaceBuildingWallsDialog::toggleWall(const Position &clicked) {
	if (compareButton->GetValue()) {
		return;
	}

	// The sprite of a wall reaches over the tile to its north west
	for (const Position &position : { clicked, clicked + Position(1, 1, 0) }) {
		if (!hasBuildingWall(editor.getMap().getTile(position))) {
			continue;
		}

		if (!scan.tiles.contains(position)) {
			addWallToScan(scan, editor.getMap(), position);
			rebuildSourceList();
		} else if (excluded.erase(position) == 0) {
			excluded.insert(position);
		}

		refreshStyles();
		return;
	}
}

void ReplaceBuildingWallsDialog::OnCharHook(wxKeyEvent &event) {
	// Wall names can have a hyphen, so the search box keeps the character keys
	const bool typing = FindFocus() == search;

	switch (event.GetKeyCode()) {
		case WXK_NUMPAD_ADD:
		case WXK_PAGEUP:
			moveDisplayedFloor(-1);
			return;
		case WXK_NUMPAD_SUBTRACT:
		case WXK_PAGEDOWN:
			moveDisplayedFloor(1);
			return;
		case '+':
		case '=':
			if (!typing) {
				moveDisplayedFloor(-1);
				return;
			}
			break;
		case '-':
			if (!typing) {
				moveDisplayedFloor(1);
				return;
			}
			break;
		case 'C':
			if (!typing) {
				compareButton->SetValue(!compareButton->GetValue());
				preview->setComparing(compareButton->GetValue());
				return;
			}
			break;
		case 'F':
			if (event.ControlDown()) {
				search->SetFocus();
				search->SelectAll();
				return;
			}
			break;
		case WXK_HOME:
			if (!typing) {
				preview->fitTiles(scan.tiles);
				return;
			}
			break;
		default:
			break;
	}
	event.Skip();
}

void ReplaceBuildingWallsDialog::OnScopeChanged(wxCommandEvent &WXUNUSED(event)) {
	scope = static_cast<BuildingScope>(scopeChoice->GetSelection());
	side = -1;
	rescan();
}

void ReplaceBuildingWallsDialog::OnOtherSide(wxCommandEvent &WXUNUSED(event)) {
	side = scan.side + 1;
	rescan();
}

void ReplaceBuildingWallsDialog::OnFilterChanged(wxCommandEvent &WXUNUSED(event)) {
	refreshStyles();
}

void ReplaceBuildingWallsDialog::OnEmptyAction(wxCommandEvent &WXUNUSED(event)) {
	if (!search->IsEmpty()) {
		search->Clear();
	} else {
		completeOnly->SetValue(false);
		refreshStyles();
	}
}

void ReplaceBuildingWallsDialog::OnStyleSelected(wxCommandEvent &WXUNUSED(event)) {
	refreshPlan();
}

void ReplaceBuildingWallsDialog::OnFloorSelected(wxCommandEvent &WXUNUSED(event)) {
	const int selection = floorList->GetSelection();
	if (selection != wxNOT_FOUND) {
		moveDisplayedFloor(selection - shownFloor);
	}
}

void ReplaceBuildingWallsDialog::OnSelectionChanged(wxCommandEvent &WXUNUSED(event)) {
	refreshStyles();
}

void ReplaceBuildingWallsDialog::OnOpeningAction(wxCommandEvent &event) {
	for (const OpeningRow &row : openingRows) {
		const int selection = row.action->GetSelection();
		if (row.action == event.GetEventObject() && selection != wxNOT_FOUND && static_cast<size_t>(selection) < row.actions.size()) {
			openingActions[row.type] = row.actions[selection];
			refreshPlan();
			return;
		}
	}
}

void ReplaceBuildingWallsDialog::OnRailingAction(wxCommandEvent &WXUNUSED(event)) {
	keepRailings = railingAction->GetSelection() == 1;
	refreshPlan();
}

void ReplaceBuildingWallsDialog::OnCompare(wxCommandEvent &WXUNUSED(event)) {
	preview->setComparing(compareButton->GetValue());
}

void ReplaceBuildingWallsDialog::OnFit(wxCommandEvent &WXUNUSED(event)) {
	preview->fitTiles(scan.tiles);
}

void ReplaceBuildingWallsDialog::OnFloorUp(wxCommandEvent &WXUNUSED(event)) {
	moveDisplayedFloor(-1);
}

void ReplaceBuildingWallsDialog::OnFloorDown(wxCommandEvent &WXUNUSED(event)) {
	moveDisplayedFloor(1);
}

void ReplaceBuildingWallsDialog::OnApply(wxCommandEvent &WXUNUSED(event)) {
	const size_t total = plan.changes.size();
	if (!commitWallReplacement(editor, plan)) {
		return;
	}

	g_gui.SetStatusText(wxString::Format("Replaced %s.", countOf(total, "piece")));
	EndModal(wxID_OK);
}
