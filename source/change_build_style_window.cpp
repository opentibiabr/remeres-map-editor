//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include <cctype>

#include "change_build_style_window.h"

#include "brush.h"
#include "editor.h"
#include "graphics.h"
#include "gui.h"
#include "items.h"
#include "map_drawer.h"
#include "settings.h"
#include "wall_brush.h"

namespace {
std::string lowercase(std::string text) {
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return text;
}

bool railingOrFence(const std::string &name) {
	const std::string lowered = lowercase(name);
	return lowered.find("railing") != std::string::npos || lowered.find("fence") != std::string::npos || lowered.find("palisade") != std::string::npos || lowered.find("bars") != std::string::npos;
}
}

ChangeBuildStyleListBox::ChangeBuildStyleListBox(wxWindow* parent) :
	wxVListBox(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLB_SINGLE) {
}

void ChangeBuildStyleListBox::setBrushes(const std::vector<WallBrush*> &newBrushes) {
	brushes = newBrushes;
	SetItemCount(brushes.size());
	if (!brushes.empty()) {
		SetSelection(0);
	}
	Refresh();
}

WallBrush* ChangeBuildStyleListBox::getSelectedBrush() const {
	const int selection = GetSelection();
	if (selection == wxNOT_FOUND || static_cast<size_t>(selection) >= brushes.size()) {
		return nullptr;
	}
	return brushes[selection];
}

void ChangeBuildStyleListBox::OnDrawItem(wxDC &dc, const wxRect &rect, size_t index) const {
	if (index >= brushes.size()) {
		return;
	}
	WallBrush* brush = brushes[index];
	if (IsSelected(index)) {
		dc.SetTextForeground(HasFocus() ? wxColour(255, 255, 255) : wxColour(0, 0, 255));
	} else {
		dc.SetTextForeground(wxColour(0, 0, 0));
	}
	if (Sprite* sprite = g_gui.gfx.getSprite(brush->getLookID())) {
		sprite->DrawTo(&dc, SPRITE_SIZE_32x32, rect.GetX() + 3, rect.GetY() + 3, rect.GetWidth(), rect.GetHeight());
	}
	dc.DrawText(wxstr(brush->getName()), rect.GetX() + 42, rect.GetY() + 12);
}

wxCoord ChangeBuildStyleListBox::OnMeasureItem(size_t WXUNUSED(index)) const {
	return 38;
}

BEGIN_EVENT_TABLE(ChangeBuildStylePreview, MapCanvas)
EVT_KEY_DOWN(ChangeBuildStylePreview::OnIgnoredKey)
EVT_KEY_UP(ChangeBuildStylePreview::OnIgnoredKey)
EVT_MOTION(ChangeBuildStylePreview::OnMouseMove)
EVT_LEFT_DOWN(ChangeBuildStylePreview::OnIgnoredMouseButton)
EVT_LEFT_UP(ChangeBuildStylePreview::OnIgnoredMouseButton)
EVT_LEFT_DCLICK(ChangeBuildStylePreview::OnIgnoredMouseButton)
EVT_MIDDLE_DOWN(ChangeBuildStylePreview::OnMouseCenterClick)
EVT_MIDDLE_UP(ChangeBuildStylePreview::OnMouseCenterRelease)
EVT_RIGHT_DOWN(ChangeBuildStylePreview::OnMouseRightClick)
EVT_RIGHT_UP(ChangeBuildStylePreview::OnMouseRightRelease)
EVT_MOUSEWHEEL(ChangeBuildStylePreview::OnWheel)
EVT_ENTER_WINDOW(ChangeBuildStylePreview::OnGainMouse)
EVT_LEAVE_WINDOW(ChangeBuildStylePreview::OnLoseMouse)
EVT_PAINT(MapCanvas::OnPaint)
EVT_ERASE_BACKGROUND(MapCanvas::OnEraseBackground)
END_EVENT_TABLE()

ChangeBuildStylePreview::ChangeBuildStylePreview(wxWindow* parent, Editor &editor, const Position &origin) :
	MapCanvas(parent, editor, nullptr),
	viewX(0),
	viewY(0),
	panning(false),
	panAnchor(wxDefaultPosition) {
	SetMinSize(wxSize(430, 430));
	SetPreviewMap(&previewMap);
	centerOn(origin);
}

void ChangeBuildStylePreview::centerOn(const Position &position) {
	CenterViewOnPosition(position);
	ChangeFloor(position.z);
}

void ChangeBuildStylePreview::fitBuilding(const PositionVector &positions) {
	if (positions.empty()) {
		return;
	}
	int minX = positions.front().x;
	int minY = positions.front().y;
	int maxX = minX;
	int maxY = minY;
	for (const Position &position : positions) {
		minX = std::min(minX, position.x);
		minY = std::min(minY, position.y);
		maxX = std::max(maxX, position.x);
		maxY = std::max(maxY, position.y);
	}
	const int width = std::max(GetClientSize().GetWidth(), 430);
	const int height = std::max(GetClientSize().GetHeight(), 430);
	const double widthZoom = static_cast<double>((maxX - minX + 5) * rme::TileSize) / width;
	const double heightZoom = static_cast<double>((maxY - minY + 5) * rme::TileSize) / height;
	SetZoom(std::max(1.0, std::max(widthZoom, heightZoom)));
}

void ChangeBuildStylePreview::showFloor(int floor) {
	ChangeFloor(floor);
}

void ChangeBuildStylePreview::ScreenToMap(int screen_x, int screen_y, int* map_x, int* map_y) {
	*map_x = static_cast<int>(viewX + screen_x * GetZoom()) / rme::TileSize;
	*map_y = static_cast<int>(viewY + screen_y * GetZoom()) / rme::TileSize;
	if (GetFloor() <= rme::MapGroundLayer) {
		*map_x += rme::MapGroundLayer - GetFloor();
		*map_y += rme::MapGroundLayer - GetFloor();
	}
}

void ChangeBuildStylePreview::GetScreenCenter(int* map_x, int* map_y) {
	const wxSize size = GetClientSize();
	const int width = size.GetWidth() > 0 ? size.GetWidth() : 430;
	const int height = size.GetHeight() > 0 ? size.GetHeight() : 430;
	ScreenToMap(width / 2, height / 2, map_x, map_y);
}

void ChangeBuildStylePreview::GetViewBox(int* view_scroll_x, int* view_scroll_y, int* screensize_x, int* screensize_y) const {
	const wxSize size = GetClientSize();
	*screensize_x = size.GetWidth();
	*screensize_y = size.GetHeight();
	*view_scroll_x = viewX;
	*view_scroll_y = viewY;
}

void ChangeBuildStylePreview::UpdatePositionStatus(int WXUNUSED(x), int WXUNUSED(y)) {
}

void ChangeBuildStylePreview::UpdateZoomStatus() {
}

void ChangeBuildStylePreview::ConfigureDrawingOptions(DrawingOptions &options) {
	options.SetIngame();
}

void ChangeBuildStylePreview::CenterViewOnPosition(const Position &position) {
	wxSize size = GetClientSize();
	const int width = size.GetWidth() > 0 ? size.GetWidth() : 430;
	const int height = size.GetHeight() > 0 ? size.GetHeight() : 430;
	const int floorOffset = position.z < 8 ? (rme::MapGroundLayer - position.z) * rme::TileSize : 0;
	viewX = position.x * rme::TileSize - floorOffset - static_cast<int>(width * GetZoom() / 2.0);
	viewY = position.y * rme::TileSize - floorOffset - static_cast<int>(height * GetZoom() / 2.0);
}

void ChangeBuildStylePreview::OnFloorChanged() {
}

void ChangeBuildStylePreview::OnMouseMove(wxMouseEvent &event) {
	if (panning) {
		const float speed = g_settings.getFloat(Config::SCROLL_SPEED);
		viewX += static_cast<int>(speed * GetZoom() * (event.GetX() - panAnchor.x));
		viewY += static_cast<int>(speed * GetZoom() * (event.GetY() - panAnchor.y));
		panAnchor = event.GetPosition();
		Refresh();
	}
}

void ChangeBuildStylePreview::OnWheel(wxMouseEvent &event) {
	if (!event.ControlDown()) {
		return;
	}

	const double oldZoom = GetZoom();
	const double nextZoom = oldZoom - event.GetWheelRotation() * g_settings.getFloat(Config::ZOOM_SPEED) / 640.0;
	const double anchorX = viewX + event.GetX() * oldZoom;
	const double anchorY = viewY + event.GetY() * oldZoom;
	SetZoom(nextZoom);
	viewX = static_cast<int>(anchorX - event.GetX() * GetZoom());
	viewY = static_cast<int>(anchorY - event.GetY() * GetZoom());
	Refresh();
}

void ChangeBuildStylePreview::startPan(const wxMouseEvent &event, bool rightButton) {
	if (rightButton != g_settings.getBoolean(Config::SWITCH_MOUSEBUTTONS)) {
		return;
	}
	SetFocus();
	panning = true;
	panAnchor = event.GetPosition();
	if (!HasCapture()) {
		CaptureMouse();
	}
}

void ChangeBuildStylePreview::stopPan(const wxMouseEvent &WXUNUSED(event), bool rightButton) {
	if (rightButton != g_settings.getBoolean(Config::SWITCH_MOUSEBUTTONS)) {
		return;
	}
	panning = false;
	if (HasCapture()) {
		ReleaseMouse();
	}
}

void ChangeBuildStylePreview::OnMouseCenterClick(wxMouseEvent &event) {
	startPan(event, false);
}

void ChangeBuildStylePreview::OnMouseCenterRelease(wxMouseEvent &event) {
	stopPan(event, false);
}

void ChangeBuildStylePreview::OnMouseRightClick(wxMouseEvent &event) {
	startPan(event, true);
}

void ChangeBuildStylePreview::OnMouseRightRelease(wxMouseEvent &event) {
	stopPan(event, true);
}

void ChangeBuildStylePreview::OnIgnoredMouseButton(wxMouseEvent &WXUNUSED(event)) {
	SetFocus();
}

void ChangeBuildStylePreview::OnIgnoredKey(wxKeyEvent &WXUNUSED(event)) {
}

void ChangeBuildStylePreview::OnGainMouse(wxMouseEvent &WXUNUSED(event)) {
	Refresh();
}

void ChangeBuildStylePreview::OnLoseMouse(wxMouseEvent &event) {
	if (panning && !event.MiddleIsDown() && !event.RightIsDown()) {
		panning = false;
		if (HasCapture()) {
			ReleaseMouse();
		}
	}
	Refresh();
}

ChangeBuildStyleDialog::ChangeBuildStyleDialog(wxWindow* parent, Editor &editor, const Position &origin) :
	wxDialog(parent, wxID_ANY, "Change Build Style", wxDefaultPosition, wxSize(920, 600), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	service(editor, origin),
	displayFloorIndex(0) {
	std::set<WallBrush*> uniqueStyles;
	for (const auto &brushEntry : g_brushes.getMap()) {
		Brush* brush = brushEntry.second;
		if (!brush || !brush->isWall() || brush->isWallDecoration()) {
			continue;
		}
		WallBrush* wall = brush->asWall();
		if (wall && wall != service.getSourceBrush() && uniqueStyles.insert(wall).second) {
			allStyles.push_back(wall);
		}
	}
	std::sort(allStyles.begin(), allStyles.end(), [](WallBrush* left, WallBrush* right) {
		return lowercase(left->getName()) < lowercase(right->getName());
	});

	const auto &detectedFloors = service.getFloors();
	for (size_t index = 0; index < detectedFloors.size(); ++index) {
		if (detectedFloors[index].z == origin.z) {
			displayFloorIndex = static_cast<int>(index);
			break;
		}
	}
	Position previewCenter = origin;
	if (!detectedFloors.empty() && !detectedFloors[displayFloorIndex].positions.empty()) {
		const PositionVector &positions = detectedFloors[displayFloorIndex].positions;
		int minX = positions.front().x;
		int minY = positions.front().y;
		int maxX = minX;
		int maxY = minY;
		for (const Position &position : positions) {
			minX = std::min(minX, position.x);
			minY = std::min(minY, position.y);
			maxX = std::max(maxX, position.x);
			maxY = std::max(maxY, position.y);
		}
		previewCenter.x = minX + (maxX - minX) / 2;
		previewCenter.y = minY + (maxY - minY) / 2;
	}

	wxBoxSizer* root = newd wxBoxSizer(wxVERTICAL);
	wxBoxSizer* content = newd wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* left = newd wxBoxSizer(wxVERTICAL);
	wxBoxSizer* right = newd wxBoxSizer(wxVERTICAL);

	wxBoxSizer* filterRow = newd wxBoxSizer(wxHORIZONTAL);
	search = newd wxTextCtrl(this, wxID_ANY);
	search->SetHint("Search wall style");
	filterRow->Add(search, 1, wxRIGHT | wxEXPAND, 6);
	category = newd wxChoice(this, wxID_ANY);
	category->Append("All styles");
	category->Append("Walls");
	category->Append("Railings / fences");
	category->SetSelection(0);
	filterRow->Add(category, 0, wxEXPAND);
	left->Add(filterRow, 0, wxBOTTOM | wxEXPAND, 8);

	styleList = newd ChangeBuildStyleListBox(this);
	styleList->SetMinSize(wxSize(360, 300));
	left->Add(styleList, 1, wxBOTTOM | wxEXPAND, 10);

	left->Add(newd wxStaticText(this, wxID_ANY, "Detected floors"), 0, wxBOTTOM, 4);
	floorList = newd wxCheckListBox(this, wxID_ANY);
	floorList->SetMinSize(wxSize(360, 90));
	for (const ChangeBuildStyleFloor &floor : detectedFloors) {
		floorList->Append(wxString::Format("Floor %d - %zu structural pieces", floor.z, floor.positions.size()));
		floorList->Check(floorList->GetCount() - 1, true);
	}
	left->Add(floorList, 0, wxBOTTOM | wxEXPAND, 6);
	onlyCurrentFloor = newd wxCheckBox(this, wxID_ANY, "Only current floor");
	left->Add(onlyCurrentFloor, 0, wxEXPAND);

	wxBoxSizer* previewHeader = newd wxBoxSizer(wxHORIZONTAL);
	previewFloorLabel = newd wxStaticText(this, wxID_ANY, wxEmptyString);
	previewHeader->Add(previewFloorLabel, 1, wxALIGN_CENTER_VERTICAL);
	wxBitmapButton* up = newd wxBitmapButton(this, wxID_ANY, wxArtProvider::GetBitmap(wxART_GO_UP, wxART_BUTTON, wxSize(16, 16)));
	up->SetToolTip("Show upper floor");
	previewHeader->Add(up, 0, wxLEFT, 4);
	wxBitmapButton* down = newd wxBitmapButton(this, wxID_ANY, wxArtProvider::GetBitmap(wxART_GO_DOWN, wxART_BUTTON, wxSize(16, 16)));
	down->SetToolTip("Show lower floor");
	previewHeader->Add(down, 0, wxLEFT, 4);
	right->Add(previewHeader, 0, wxBOTTOM | wxEXPAND, 8);
	preview = newd ChangeBuildStylePreview(this, editor, previewCenter);
	if (!detectedFloors.empty()) {
		preview->fitBuilding(detectedFloors[displayFloorIndex].positions);
	}
	right->Add(preview, 1, wxEXPAND);

	content->Add(left, 1, wxALL | wxEXPAND, 12);
	content->Add(right, 1, wxTOP | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
	root->Add(content, 1, wxEXPAND);

	status = newd wxStaticText(this, wxID_ANY, wxEmptyString);
	root->Add(status, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 12);
	wxStdDialogButtonSizer* buttons = newd wxStdDialogButtonSizer();
	applyButton = newd wxButton(this, wxID_OK, "Apply style");
	applyButton->Enable(false);
	buttons->AddButton(applyButton);
	buttons->AddButton(newd wxButton(this, wxID_CANCEL, "Cancel"));
	buttons->Realize();
	root->Add(buttons, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxALIGN_RIGHT, 12);

	SetSizer(root);
	Centre(wxBOTH);

	search->Bind(wxEVT_TEXT, &ChangeBuildStyleDialog::OnFilterChanged, this);
	category->Bind(wxEVT_CHOICE, &ChangeBuildStyleDialog::OnFilterChanged, this);
	styleList->Bind(wxEVT_LISTBOX, &ChangeBuildStyleDialog::OnStyleSelected, this);
	floorList->Bind(wxEVT_CHECKLISTBOX, &ChangeBuildStyleDialog::OnFloorsChanged, this);
	onlyCurrentFloor->Bind(wxEVT_CHECKBOX, &ChangeBuildStyleDialog::OnOnlyCurrentFloor, this);
	up->Bind(wxEVT_BUTTON, &ChangeBuildStyleDialog::OnFloorUp, this);
	down->Bind(wxEVT_BUTTON, &ChangeBuildStyleDialog::OnFloorDown, this);
	applyButton->Bind(wxEVT_BUTTON, &ChangeBuildStyleDialog::OnApply, this);

	refreshStyles();
	updateFloorControls();
	refreshPreview();
}

void ChangeBuildStyleDialog::refreshStyles() {
	const std::string filter = lowercase(std::string(search->GetValue().mb_str()));
	const int selectedCategory = category->GetSelection();
	std::vector<WallBrush*> filtered;
	for (WallBrush* style : allStyles) {
		const std::string name = lowercase(style->getName());
		if (!filter.empty() && name.find(filter) == std::string::npos) {
			continue;
		}
		const bool isRailing = railingOrFence(name);
		if (selectedCategory == 1 && isRailing) {
			continue;
		}
		if (selectedCategory == 2 && !isRailing) {
			continue;
		}
		filtered.push_back(style);
	}
	styleList->setBrushes(filtered);
	refreshPreview();
}

std::set<int> ChangeBuildStyleDialog::selectedFloors() const {
	if (onlyCurrentFloor->IsChecked()) {
		return { service.getOrigin().z };
	}
	std::set<int> selected;
	const auto &detectedFloors = service.getFloors();
	for (size_t index = 0; index < detectedFloors.size(); ++index) {
		if (floorList->IsChecked(static_cast<unsigned int>(index))) {
			selected.insert(detectedFloors[index].z);
		}
	}
	return selected;
}

void ChangeBuildStyleDialog::refreshPreview() {
	if (!service.isValid()) {
		status->SetLabel("Select a structural automagic wall before using this action.");
		applyButton->Enable(false);
		return;
	}
	WallBrush* target = styleList->getSelectedBrush();
	if (!target) {
		status->SetLabel("No wall style matches the filter.");
		applyButton->Enable(false);
		preview->getPreviewMap().clear();
		preview->Refresh();
		return;
	}

	const ChangeBuildStyleCompatibility compatibility = service.checkCompatibility(target, selectedFloors());
	if (!compatibility.compatible) {
		status->SetLabel(compatibility.reason);
		applyButton->Enable(false);
		preview->getPreviewMap().clear();
		preview->Refresh();
		return;
	}

	wxString reason;
	if (!service.buildPreview(target, selectedFloors(), preview->getPreviewMap(), reason)) {
		status->SetLabel(reason);
		applyButton->Enable(false);
		preview->Refresh();
		return;
	}
	status->SetLabel(wxString::Format("Ready: %s", wxstr(target->getName()).c_str()));
	applyButton->Enable(true);
	preview->Refresh();
}

void ChangeBuildStyleDialog::updateFloorControls() {
	const auto &detectedFloors = service.getFloors();
	if (detectedFloors.empty()) {
		previewFloorLabel->SetLabel("Preview");
		return;
	}
	displayFloorIndex = std::max(0, std::min(displayFloorIndex, static_cast<int>(detectedFloors.size()) - 1));
	previewFloorLabel->SetLabel(wxString::Format("Preview - floor %d", detectedFloors[displayFloorIndex].z));
	floorList->Enable(!onlyCurrentFloor->IsChecked());
	preview->showFloor(detectedFloors[displayFloorIndex].z);
}

void ChangeBuildStyleDialog::OnFilterChanged(wxCommandEvent &WXUNUSED(event)) {
	refreshStyles();
}

void ChangeBuildStyleDialog::OnStyleSelected(wxCommandEvent &WXUNUSED(event)) {
	refreshPreview();
}

void ChangeBuildStyleDialog::OnFloorsChanged(wxCommandEvent &WXUNUSED(event)) {
	refreshPreview();
}

void ChangeBuildStyleDialog::OnOnlyCurrentFloor(wxCommandEvent &WXUNUSED(event)) {
	if (onlyCurrentFloor->IsChecked()) {
		const auto &detectedFloors = service.getFloors();
		for (size_t index = 0; index < detectedFloors.size(); ++index) {
			if (detectedFloors[index].z == service.getOrigin().z) {
				displayFloorIndex = static_cast<int>(index);
				break;
			}
		}
	}
	updateFloorControls();
	refreshPreview();
}

void ChangeBuildStyleDialog::OnFloorUp(wxCommandEvent &WXUNUSED(event)) {
	if (displayFloorIndex > 0) {
		--displayFloorIndex;
		updateFloorControls();
		refreshPreview();
	}
}

void ChangeBuildStyleDialog::OnFloorDown(wxCommandEvent &WXUNUSED(event)) {
	if (displayFloorIndex + 1 < static_cast<int>(service.getFloors().size())) {
		++displayFloorIndex;
		updateFloorControls();
		refreshPreview();
	}
}

void ChangeBuildStyleDialog::OnApply(wxCommandEvent &WXUNUSED(event)) {
	wxString reason;
	if (!service.apply(styleList->getSelectedBrush(), selectedFloors(), reason)) {
		status->SetLabel(reason);
		applyButton->Enable(false);
		return;
	}
	EndModal(wxID_OK);
}
