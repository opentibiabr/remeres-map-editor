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
#include <unordered_map>
#include <map>
#include <tuple>
#include "bitmap_to_map_window.h"
#include "bitmap_to_map_converter.h"
#include "editor.h"
#include "brush.h"
#include "ground_brush.h"
#include "items.h"
#include "gui.h"
#include "common.h"
#include <wx/dcgraph.h>
#include <cmath>
#include <algorithm>
#include <array>

BEGIN_EVENT_TABLE(BitmapToMapWindow, wxDialog)
EVT_BUTTON(toWxId(BitmapToMapId::BROWSE), BitmapToMapWindow::OnClickBrowse)
EVT_BUTTON(toWxId(BitmapToMapId::GENERATE), BitmapToMapWindow::OnClickGenerate)
EVT_BUTTON(toWxId(BitmapToMapId::ROTATE_LEFT), BitmapToMapWindow::OnClickRotateLeft)
EVT_BUTTON(toWxId(BitmapToMapId::ROTATE_RIGHT), BitmapToMapWindow::OnClickRotateRight)
EVT_BUTTON(toWxId(BitmapToMapId::FLIP), BitmapToMapWindow::OnClickFlip)
EVT_BUTTON(toWxId(BitmapToMapId::CROP), BitmapToMapWindow::OnClickCrop)
EVT_BUTTON(toWxId(BitmapToMapId::SAVE_PRESET), BitmapToMapWindow::OnClickSavePreset)
EVT_BUTTON(toWxId(BitmapToMapId::LOAD_PRESET), BitmapToMapWindow::OnClickLoadPreset)
EVT_BUTTON(toWxId(BitmapToMapId::DELETE_COLOR), BitmapToMapWindow::OnClickDeleteColor)
EVT_TEXT(toWxId(BitmapToMapId::FILTER), BitmapToMapWindow::OnFilterColors)
EVT_LIST_ITEM_ACTIVATED(toWxId(BitmapToMapId::COLOR_LIST), BitmapToMapWindow::OnColorListActivated)
EVT_CHOICE(toWxId(BitmapToMapId::MATCH_MODE), BitmapToMapWindow::OnMatchModeChanged)
EVT_SPINCTRL(toWxId(BitmapToMapId::TOLERANCE), BitmapToMapWindow::OnToleranceChanged)
END_EVENT_TABLE()

BitmapToMapWindow::BitmapToMapWindow(wxWindow* parent, Editor &editor) :
	wxDialog(parent, wxID_ANY, "Bitmap to Map", wxDefaultPosition, wxSize(900, 650)),
	editor(editor),
	imageLoaded(false),
	imagePreview(nullptr),
	previewPanel(nullptr),
	colorListCtrl(nullptr),
	filterCtrl(nullptr),
	toleranceCtrl(nullptr),
	xOffsetCtrl(nullptr),
	yOffsetCtrl(nullptr),
	zOffsetCtrl(nullptr),
	imageInfoLabel(nullptr),
	progressBar(nullptr),
	scaleChoice(nullptr),
	pixelInfoLabel(nullptr),
	cropButton(nullptr) {
	wxBoxSizer* mainSizer = newd wxBoxSizer(wxHORIZONTAL);

	// === Left panel: image preview ===
	wxBoxSizer* leftSizer = newd wxBoxSizer(wxVERTICAL);

	imageInfoLabel = newd wxStaticText(this, wxID_ANY, "No image loaded");
	leftSizer->Add(imageInfoLabel, 0, wxALL, 5);

	previewPanel = newd wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxSize(400, 400));
	previewPanel->SetBackgroundColour(*wxBLACK);
	imagePreview = newd wxStaticBitmap(previewPanel, wxID_ANY, wxNullBitmap);
	leftSizer->Add(previewPanel, 1, wxEXPAND | wxALL, 5);

	// Pixel info label
	pixelInfoLabel = newd wxStaticText(this, wxID_ANY, "Hover over image for pixel info");
	leftSizer->Add(pixelInfoLabel, 0, wxALL, 5);

	// Bind mouse events on preview panel
	imagePreview->Bind(wxEVT_LEFT_DOWN, &BitmapToMapWindow::OnPreviewLeftDown, this);
	imagePreview->Bind(wxEVT_LEFT_UP, &BitmapToMapWindow::OnPreviewLeftUp, this);
	imagePreview->Bind(wxEVT_MOTION, &BitmapToMapWindow::OnPreviewMouseMove, this);

	// Image manipulation buttons
	wxBoxSizer* imgBtnSizer = newd wxBoxSizer(wxHORIZONTAL);
	imgBtnSizer->Add(newd wxButton(this, toWxId(BitmapToMapId::ROTATE_LEFT), "Rotate L"), 0, wxALL, 2);
	imgBtnSizer->Add(newd wxButton(this, toWxId(BitmapToMapId::ROTATE_RIGHT), "Rotate R"), 0, wxALL, 2);
	imgBtnSizer->Add(newd wxButton(this, toWxId(BitmapToMapId::FLIP), "Flip H"), 0, wxALL, 2);
	cropButton = newd wxButton(this, toWxId(BitmapToMapId::CROP), "Crop");
	imgBtnSizer->Add(cropButton, 0, wxALL, 2);
	leftSizer->Add(imgBtnSizer, 0, wxALIGN_CENTER);

	wxBoxSizer* matchSizer = newd wxBoxSizer(wxHORIZONTAL);
	wxStaticText* matchLabel = newd wxStaticText(this, wxID_ANY, "Match Mode:");
	matchModeChoice = newd wxChoice(this, toWxId(BitmapToMapId::MATCH_MODE));
	matchModeChoice->Append("Pixel (RGB)");
	matchModeChoice->Append("Hue (HSL)");
	matchModeChoice->SetSelection(0);
	matchSizer->Add(matchLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
	matchSizer->Add(matchModeChoice, 0, wxALL, 2);
	leftSizer->Add(matchSizer, 0, wxEXPAND);

	mainSizer->Add(leftSizer, 1, wxEXPAND);

	// === Right panel: controls + color list ===
	wxBoxSizer* rightSizer = newd wxBoxSizer(wxVERTICAL);

	// Browse
	wxBoxSizer* browseSizer = newd wxBoxSizer(wxHORIZONTAL);
	browseSizer->Add(newd wxButton(this, toWxId(BitmapToMapId::BROWSE), "Browse Image..."), 0, wxALL, 5);
	rightSizer->Add(browseSizer, 0, wxEXPAND);

	// Offsets
	wxStaticBoxSizer* offsetBox = newd wxStaticBoxSizer(wxHORIZONTAL, this, "Offsets");
	offsetBox->Add(newd wxStaticText(offsetBox->GetStaticBox(), wxID_ANY, "X:"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
	xOffsetCtrl = newd wxSpinCtrl(offsetBox->GetStaticBox(), wxID_ANY, "0", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 65000, 0);
	offsetBox->Add(xOffsetCtrl, 0, wxALL, 2);
	offsetBox->Add(newd wxStaticText(offsetBox->GetStaticBox(), wxID_ANY, "Y:"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
	yOffsetCtrl = newd wxSpinCtrl(offsetBox->GetStaticBox(), wxID_ANY, "0", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 65000, 0);
	offsetBox->Add(yOffsetCtrl, 0, wxALL, 2);
	offsetBox->Add(newd wxStaticText(offsetBox->GetStaticBox(), wxID_ANY, "Z:"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
	zOffsetCtrl = newd wxSpinCtrl(offsetBox->GetStaticBox(), wxID_ANY, "7", wxDefaultPosition, wxSize(50, -1), wxSP_ARROW_KEYS, 0, 15, 7);
	offsetBox->Add(zOffsetCtrl, 0, wxALL, 2);
	rightSizer->Add(offsetBox, 0, wxEXPAND | wxALL, 5);

	// Tolerance
	wxBoxSizer* tolSizer = newd wxBoxSizer(wxHORIZONTAL);
	tolSizer->Add(newd wxStaticText(this, wxID_ANY, "Tolerance:"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
	toleranceCtrl = newd wxSpinCtrl(this, toWxId(BitmapToMapId::TOLERANCE), "30", wxDefaultPosition, wxSize(60, -1), wxSP_ARROW_KEYS, 0, 765, 30);
	tolSizer->Add(toleranceCtrl, 0, wxALL, 2);
	rightSizer->Add(tolSizer, 0, wxEXPAND);

	// Scale
	wxBoxSizer* scaleSizer = newd wxBoxSizer(wxHORIZONTAL);
	scaleSizer->Add(newd wxStaticText(this, wxID_ANY, "Scale:"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
	wxArrayString scaleOptions;
	scaleOptions.Add("0.25x");
	scaleOptions.Add("0.5x");
	scaleOptions.Add("1x");
	scaleOptions.Add("2x");
	scaleOptions.Add("4x");
	scaleChoice = newd wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(80, -1), scaleOptions);
	scaleChoice->SetSelection(2); // default 1x
	scaleSizer->Add(scaleChoice, 0, wxALL, 2);
	rightSizer->Add(scaleSizer, 0, wxEXPAND);

	// Filter
	wxBoxSizer* filterSizer = newd wxBoxSizer(wxHORIZONTAL);
	filterSizer->Add(newd wxStaticText(this, wxID_ANY, "Filter:"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
	filterCtrl = newd wxTextCtrl(this, toWxId(BitmapToMapId::FILTER), "", wxDefaultPosition, wxSize(150, -1));
	filterSizer->Add(filterCtrl, 1, wxALL, 2);
	rightSizer->Add(filterSizer, 0, wxEXPAND);

	// Color list
	colorListCtrl = newd wxListCtrl(this, toWxId(BitmapToMapId::COLOR_LIST), wxDefaultPosition, wxSize(450, 250), wxLC_REPORT | wxLC_SINGLE_SEL);
	colorListCtrl->InsertColumn(0, "Color", wxLIST_FORMAT_LEFT, 80);
	colorListCtrl->InsertColumn(1, "Pixels", wxLIST_FORMAT_RIGHT, 60);
	colorListCtrl->InsertColumn(2, "Brush", wxLIST_FORMAT_LEFT, 150);
	colorListCtrl->InsertColumn(3, "Ignore", wxLIST_FORMAT_CENTER, 50);
	colorListCtrl->InsertColumn(4, "Hex", wxLIST_FORMAT_LEFT, 80);
	rightSizer->Add(colorListCtrl, 1, wxEXPAND | wxALL, 5);

	// Color list buttons
	wxBoxSizer* colorBtnSizer = newd wxBoxSizer(wxHORIZONTAL);
	colorBtnSizer->Add(newd wxButton(this, toWxId(BitmapToMapId::DELETE_COLOR), "Delete Color"), 0, wxALL, 2);
	rightSizer->Add(colorBtnSizer, 0, wxALIGN_CENTER);

	// Action buttons
	wxBoxSizer* actionSizer = newd wxBoxSizer(wxHORIZONTAL);
	actionSizer->Add(newd wxButton(this, toWxId(BitmapToMapId::GENERATE), "Generate"), 0, wxALL, 5);
	rightSizer->Add(actionSizer, 0, wxALIGN_CENTER);

	// Preset buttons
	wxBoxSizer* presetSizer = newd wxBoxSizer(wxHORIZONTAL);
	presetSizer->Add(newd wxButton(this, toWxId(BitmapToMapId::SAVE_PRESET), "Save Preset"), 0, wxALL, 2);
	presetSizer->Add(newd wxButton(this, toWxId(BitmapToMapId::LOAD_PRESET), "Load Preset"), 0, wxALL, 2);
	rightSizer->Add(presetSizer, 0, wxALIGN_CENTER);

	// Progress bar
	progressBar = newd wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(-1, 20));
	rightSizer->Add(progressBar, 0, wxEXPAND | wxALL, 5);

	mainSizer->Add(rightSizer, 1, wxEXPAND);

	SetSizer(mainSizer);
	Layout();
	Centre(wxBOTH);
}

void BitmapToMapWindow::OnClickBrowse(wxCommandEvent &event) {
	wxFileDialog dlg(this, "Select Image", "", "", "Image files (*.png;*.bmp;*.jpg;*.jpeg)|*.png;*.bmp;*.jpg;*.jpeg", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	wxString path = dlg.GetPath();
	wxImage img;
	if (!img.LoadFile(path)) {
		wxMessageBox("Failed to load image: " + path, "Error", wxOK | wxICON_ERROR, this);
		return;
	}

	loadedImage = img;
	originalImage = img.Copy();
	imageLoaded = true;

	// Reset crop state
	cropState.selectionMode = false;
	cropState.dragging = false;
	cropState.start = wxPoint(0, 0);
	cropState.end = wxPoint(0, 0);
	cropButton->SetLabel("Crop");
	imagePreview->SetCursor(wxNullCursor);
	cropState.baseBitmap = wxBitmap();

	// Clear previous colors
	detectedColors.clear();

	// Update info label
	imageInfoLabel->SetLabel(wxString::Format("Size: %dx%d", img.GetWidth(), img.GetHeight()));

	updatePreview();

	// Detect colors
	detectColors();
}

static int computeBucketKey(uint8_t r, uint8_t g, uint8_t b, bool useHue, int tolerance) {
	if (useHue) {
		float rf = r / 255.0f;
		float gf = g / 255.0f;
		float bf = b / 255.0f;
		float maxC = std::max({ rf, gf, bf });
		float minC = std::min({ rf, gf, bf });
		float delta = maxC - minC;

		if (delta < kAchromaticDelta) {
			if (int brightness = (r + g + b) / 3; brightness < 64) {
				return 1000;
			} else if (brightness < 192) {
				return 1001;
			}
			return 1002;
		}

		float hue = 0.0f;
		if (maxC == rf) {
			hue = 60.0f * fmod((gf - bf) / delta, 6.0f);
		} else if (maxC == gf) {
			hue = 60.0f * ((bf - rf) / delta + 2.0f);
		} else {
			hue = 60.0f * ((rf - gf) / delta + 4.0f);
		}
		if (hue < 0.0f) {
			hue += 360.0f;
		}

		int bucketSize = std::max(tolerance, 1);
		return static_cast<int>(hue / bucketSize);
	}

	int q = std::max(tolerance, 1);
	int rq = (r / q) * q;
	int gq = (g / q) * q;
	int bq = (b / q) * q;
	return (rq << 16) | (gq << 8) | bq;
}

void BitmapToMapWindow::detectColors() {
	if (!imageLoaded) {
		return;
	}

	detectedColors.clear();

	unsigned char* data = loadedImage.GetData();
	bool hasAlpha = loadedImage.HasAlpha();
	unsigned char* alpha = hasAlpha ? loadedImage.GetAlpha() : nullptr;
	int w = loadedImage.GetWidth();
	int h = loadedImage.GetHeight();
	int tolerance = toleranceCtrl->GetValue();
	bool useHue = (matchModeChoice->GetSelection() == 1);

	std::map<int, std::tuple<long long, long long, long long, int>> buckets;

	for (int i = 0; i < w * h; i++) {
		if (hasAlpha && alpha[i] < 128) {
			continue;
		}

		uint8_t r = data[i * 3];
		uint8_t g = data[i * 3 + 1];
		uint8_t b_val = data[i * 3 + 2];

		int key = computeBucketKey(r, g, b_val, useHue, tolerance);

		auto it = buckets.find(key);
		if (it != buckets.end()) {
			auto &[sumR, sumG, sumB, count] = it->second;
			sumR += r;
			sumG += g;
			sumB += b_val;
			count += 1;
		} else {
			buckets[key] = std::make_tuple((long long)r, (long long)g, (long long)b_val, 1);
		}

		if (i % 50000 == 0) {
			wxSafeYield(this, true);
		}
	}

	for (auto &[key, bucket] : buckets) {
		auto &[sumR, sumG, sumB, count] = bucket;
		DetectedColor dc;
		dc.r = static_cast<uint8_t>(sumR / count);
		dc.g = static_cast<uint8_t>(sumG / count);
		dc.b = static_cast<uint8_t>(sumB / count);
		dc.pixelCount = count;
		dc.ignore = false;
		detectedColors.push_back(dc);
	}

	// Sort by pixel count descending
	std::sort(detectedColors.begin(), detectedColors.end(), [](const DetectedColor &a, const DetectedColor &b) {
		return a.pixelCount > b.pixelCount;
	});

	// Limit
	if ((int)detectedColors.size() > MAX_COLORS) {
		detectedColors.resize(MAX_COLORS);
	}

	autoSuggestBrushes();
	populateColorList();
}

void BitmapToMapWindow::autoSuggestBrushes() {
	wxBusyCursor wait;
	struct BrushColor {
		std::string name;
		uint8_t r;
		uint8_t g;
		uint8_t b;
	};

	std::vector<BrushColor> brushColors;

	const BrushMap &brushMap = g_brushes.getMap();
	for (auto it = brushMap.begin(); it != brushMap.end(); ++it) {
		Brush* brush = it->second;
		if (!brush->isGround()) {
			continue;
		}

		int lookId = brush->getLookID();
		if (lookId <= 0) {
			continue;
		}

		const ItemType &type = g_items.getItemType(lookId);
		if (type.id == 0 || !type.sprite) {
			continue;
		}

		uint16_t mc = type.sprite->getMiniMapColor();
		if (mc == 0) {
			continue;
		}

		wxColor rgb = colorFromEightBit(mc);
		brushColors.push_back({ brush->getName(), rgb.Red(), rgb.Green(), rgb.Blue() });
	}

	for (auto &dc : detectedColors) {
		if (dc.r == 0 && dc.g == 0 && dc.b == 0) {
			dc.ignore = true;
			continue;
		}

		int bestDist = 766;
		std::string bestBrush;

		for (const auto &bc : brushColors) {
			int dist = std::abs((int)dc.r - bc.r)
				+ std::abs((int)dc.g - bc.g)
				+ std::abs((int)dc.b - bc.b);
			if (dist < bestDist) {
				bestDist = dist;
				bestBrush = bc.name;
			}
		}

		if (!bestBrush.empty()) {
			dc.suggestedBrush = wxString(bestBrush);
		}
	}
}

void BitmapToMapWindow::populateColorList() {
	colorListCtrl->DeleteAllItems();

	wxString filter = filterCtrl->GetValue().Lower();

	for (size_t i = 0; i < detectedColors.size(); ++i) {
		const auto &dc = detectedColors[i];

		if (!filter.IsEmpty()) {
			wxString hex = dc.toHex().Lower();
			wxString brush = dc.suggestedBrush.Lower();
			if (hex.Find(filter) == wxNOT_FOUND && brush.Find(filter) == wxNOT_FOUND) {
				continue;
			}
		}

		long idx = colorListCtrl->InsertItem(colorListCtrl->GetItemCount(), "");
		colorListCtrl->SetItemData(idx, (long)i);

		colorListCtrl->SetItem(idx, 0, wxString::Format("(%d,%d,%d)", dc.r, dc.g, dc.b));
		colorListCtrl->SetItem(idx, 1, wxString::Format("%d", dc.pixelCount));
		colorListCtrl->SetItem(idx, 2, dc.suggestedBrush.IsEmpty() ? wxString("(none)") : dc.suggestedBrush);
		colorListCtrl->SetItem(idx, 3, dc.ignore ? "Yes" : "No");
		colorListCtrl->SetItem(idx, 4, dc.toHex());

		colorListCtrl->SetItemBackgroundColour(idx, wxColour(dc.r, dc.g, dc.b));
		int brightness = (dc.r * 299 + dc.g * 587 + dc.b * 114) / 1000;
		colorListCtrl->SetItemTextColour(idx, brightness > 128 ? *wxBLACK : *wxWHITE);
	}
}

void BitmapToMapWindow::OnColorListActivated(wxListEvent &event) {
	long sel = event.GetIndex();
	if (sel < 0) {
		return;
	}

	auto dataIdx = static_cast<size_t>(colorListCtrl->GetItemData(sel));
	if (dataIdx >= detectedColors.size()) {
		return;
	}

	auto &dc = detectedColors[dataIdx];

	wxArrayString brushNames;
	brushNames.Add("(ignore)");
	brushNames.Add("(none)");

	const BrushMap &brushMap = g_brushes.getMap();
	for (auto it = brushMap.begin(); it != brushMap.end(); ++it) {
		if (it->second->isGround()) {
			brushNames.Add(wxString(it->second->getName()));
		}
	}

	wxSingleChoiceDialog chooser(this, "Select brush for color " + dc.toHex(), "Choose Brush", brushNames);

	if (!dc.suggestedBrush.IsEmpty()) {
		int found = brushNames.Index(dc.suggestedBrush);
		if (found != wxNOT_FOUND) {
			chooser.SetSelection(found);
		}
	}

	if (chooser.ShowModal() == wxID_OK) {
		wxString chosen = chooser.GetStringSelection();
		if (chosen == "(ignore)") {
			dc.ignore = true;
			dc.suggestedBrush = "";
		} else if (chosen == "(none)") {
			dc.ignore = false;
			dc.suggestedBrush = "";
		} else {
			dc.ignore = false;
			dc.suggestedBrush = chosen;
		}
		populateColorList();
	}
}

void BitmapToMapWindow::OnClickDeleteColor(wxCommandEvent &event) {
	long sel = colorListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel < 0) {
		return;
	}

	auto dataIdx = static_cast<size_t>(colorListCtrl->GetItemData(sel));
	if (dataIdx >= detectedColors.size()) {
		return;
	}

	detectedColors.erase(detectedColors.begin() + dataIdx);
	populateColorList();
}

void BitmapToMapWindow::OnFilterColors(wxCommandEvent &event) {
	populateColorList();
}

void BitmapToMapWindow::OnClickRotateLeft(wxCommandEvent &event) {
	if (!imageLoaded) {
		return;
	}
	loadedImage = loadedImage.Rotate90(false);
	originalImage = loadedImage.Copy();
	cropState.selectionMode = false;
	cropState.dragging = false;
	updatePreview();
	detectColors();
}

void BitmapToMapWindow::OnClickRotateRight(wxCommandEvent &event) {
	if (!imageLoaded) {
		return;
	}
	loadedImage = loadedImage.Rotate90(true);
	originalImage = loadedImage.Copy();
	cropState.selectionMode = false;
	cropState.dragging = false;
	updatePreview();
	detectColors();
}

void BitmapToMapWindow::OnClickFlip(wxCommandEvent &event) {
	if (!imageLoaded) {
		return;
	}
	loadedImage = loadedImage.Mirror(true);
	originalImage = loadedImage.Copy();
	cropState.selectionMode = false;
	cropState.dragging = false;
	updatePreview();
}

void BitmapToMapWindow::OnClickCrop(wxCommandEvent &event) {
	if (!imageLoaded) {
		wxMessageBox("No image loaded.", "Bitmap to Map", wxOK | wxICON_WARNING);
		return;
	}

	if (cropState.selectionMode) {
		// Cancel crop mode
		cropState.selectionMode = false;
		cropState.dragging = false;
		cropButton->SetLabel("Crop");
		previewPanel->SetCursor(wxNullCursor);
		imagePreview->SetCursor(wxNullCursor);
		updatePreview();
		return;
	}

	cropState.selectionMode = true;
	cropState.dragging = false;
	cropButton->SetLabel("Cancel Crop");
	previewPanel->SetCursor(wxCursor(wxCURSOR_CROSS));
	imagePreview->SetCursor(wxCursor(wxCURSOR_CROSS));
}

void BitmapToMapWindow::OnClickGenerate(wxCommandEvent &event) {
	if (!imageLoaded) {
		wxMessageBox("Please load an image first.", "Error", wxOK | wxICON_ERROR, this);
		return;
	}

	// Build color mappings from detected colors
	std::vector<ColorMapping> mappings;
	for (const auto &dc : detectedColors) {
		ColorMapping cm;
		cm.r = dc.r;
		cm.g = dc.g;
		cm.b = dc.b;
		cm.ignore = dc.ignore;
		cm.brushName = dc.suggestedBrush.IsEmpty() ? "" : dc.suggestedBrush.ToStdString();
		mappings.push_back(cm);
	}

	int tolerance = toleranceCtrl->GetValue();
	int offX = xOffsetCtrl->GetValue();
	int offY = yOffsetCtrl->GetValue();
	int offZ = zOffsetCtrl->GetValue();

	MatchMode mode = (matchModeChoice->GetSelection() == 1) ? MatchMode::MATCH_HUE_HSL : MatchMode::MATCH_PIXEL_RGB;

	// Apply scale
	wxImage imageToConvert = loadedImage.Copy();
	int scaleIndex = scaleChoice->GetSelection();
	constexpr std::array<double, 5> scaleFactors = { 0.25, 0.5, 1.0, 2.0, 4.0 };
	if (scaleIndex >= 0 && scaleIndex < static_cast<int>(scaleFactors.size())) {
		double factor = scaleFactors[scaleIndex];
		if (factor != 1.0) {
			int newW = std::max(1, (int)std::lround(imageToConvert.GetWidth() * factor));
			int newH = std::max(1, (int)std::lround(imageToConvert.GetHeight() * factor));
			imageToConvert.Rescale(newW, newH, wxIMAGE_QUALITY_NEAREST);
		}
	}

	BitmapToMapConverter converter(editor);
	ConvertResult result = converter.convert(imageToConvert, mappings, tolerance, mode, offX, offY, offZ);

	if (result.success) {
		wxMessageBox(
			wxString::Format("Map generated!\n\nTiles placed: %d\nPixels skipped: %d", result.tilesPlaced, result.tilesSkipped),
			"Bitmap to Map", wxOK | wxICON_INFORMATION, this
		);
	} else {
		wxMessageBox(result.errorMessage, "Error", wxOK | wxICON_ERROR, this);
	}
}

void BitmapToMapWindow::updatePreview() {
	if (!imageLoaded) {
		return;
	}

	wxImage scaled = loadedImage.Copy();
	int pw = previewPanel->GetClientSize().GetWidth();
	int ph = previewPanel->GetClientSize().GetHeight();
	if (pw > 0 && ph > 0) {
		double scaleX = (double)pw / scaled.GetWidth();
		double scaleY = (double)ph / scaled.GetHeight();
		double fitScale = std::min(scaleX, scaleY);
		int newW = std::max(1, (int)(scaled.GetWidth() * fitScale));
		int newH = std::max(1, (int)(scaled.GetHeight() * fitScale));
		scaled.Rescale(newW, newH, wxIMAGE_QUALITY_NEAREST);
	}
	imagePreview->SetBitmap(wxBitmap(scaled));

	if (cropState.selectionMode) {
		imagePreview->SetCursor(wxCursor(wxCURSOR_CROSS));
	}
}

void BitmapToMapWindow::OnPreviewMouseMove(wxMouseEvent &event) {
	if (cropState.dragging && cropState.selectionMode) {
		cropState.end = event.GetPosition();

		wxBitmap bmp(cropState.baseBitmap);
		if (bmp.IsOk()) {
			wxMemoryDC memDC(bmp);
			wxGCDC dc(memDC);

			int x1 = std::min(cropState.start.x, cropState.end.x);
			int y1 = std::min(cropState.start.y, cropState.end.y);
			int x2 = std::max(cropState.start.x, cropState.end.x);
			int y2 = std::max(cropState.start.y, cropState.end.y);

			// Darken outside selection
			dc.SetBrush(wxBrush(wxColour(0, 0, 0, 120)));
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.DrawRectangle(0, 0, bmp.GetWidth(), y1);
			dc.DrawRectangle(0, y2, bmp.GetWidth(), bmp.GetHeight() - y2);
			dc.DrawRectangle(0, y1, x1, y2 - y1);
			dc.DrawRectangle(x2, y1, bmp.GetWidth() - x2, y2 - y1);

			// Green rectangle
			dc.SetPen(wxPen(wxColour(0, 255, 0), 2));
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.DrawRectangle(x1, y1, x2 - x1, y2 - y1);

			memDC.SelectObject(wxNullBitmap);

			imagePreview->SetBitmap(bmp);
		}
		return;
	}

	// Pixel info (when not crop-dragging)
	if (!imageLoaded) {
		event.Skip();
		return;
	}

	wxBitmap currentBmp = imagePreview->GetBitmap();
	if (!currentBmp.IsOk()) {
		event.Skip();
		return;
	}

	int bmpW = currentBmp.GetWidth();
	int bmpH = currentBmp.GetHeight();
	int imgW = loadedImage.GetWidth();
	int imgH = loadedImage.GetHeight();

	wxPoint pos = event.GetPosition();

	int imgX = pos.x * imgW / bmpW;
	int imgY = pos.y * imgH / bmpH;

	if (imgX < 0 || imgX >= imgW || imgY < 0 || imgY >= imgH) {
		pixelInfoLabel->SetLabel("");
		event.Skip();
		return;
	}

	uint8_t r = loadedImage.GetRed(imgX, imgY);
	uint8_t g = loadedImage.GetGreen(imgX, imgY);
	uint8_t b_val = loadedImage.GetBlue(imgX, imgY);

	wxString brushName = "none";
	int tolerance = toleranceCtrl->GetValue();
	for (const auto &dc : detectedColors) {
		if (dc.ignore) {
			continue;
		}
		if (dc.matches(r, g, b_val, tolerance) && !dc.suggestedBrush.IsEmpty()) {
			brushName = dc.suggestedBrush;
			break;
		}
	}

	pixelInfoLabel->SetLabel(wxString::Format("Pos: %d,%d | RGB: %d,%d,%d | #%02X%02X%02X | Brush: %s", imgX, imgY, r, g, b_val, r, g, b_val, brushName));
	event.Skip();
}

void BitmapToMapWindow::OnClickSavePreset(wxCommandEvent &event) {
	wxFileDialog dlg(this, "Save Preset", "", "", "XML files (*.xml)|*.xml", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	std::string filepath = nstr(dlg.GetPath());

	pugi::xml_document doc;
	pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
	decl.append_attribute("version") = "1.0";

	pugi::xml_node root = doc.append_child("bitmap_to_map_preset");
	root.append_attribute("tolerance") = toleranceCtrl->GetValue();
	root.append_attribute("offset_x") = xOffsetCtrl->GetValue();
	root.append_attribute("offset_y") = yOffsetCtrl->GetValue();
	root.append_attribute("offset_z") = zOffsetCtrl->GetValue();
	root.append_attribute("scale") = scaleChoice->GetSelection();
	root.append_attribute("match_mode") = matchModeChoice->GetSelection();

	for (const auto &dc : detectedColors) {
		pugi::xml_node colorNode = root.append_child("color");
		colorNode.append_attribute("r") = dc.r;
		colorNode.append_attribute("g") = dc.g;
		colorNode.append_attribute("b") = dc.b;
		colorNode.append_attribute("brush") = dc.suggestedBrush.ToStdString().c_str();
		colorNode.append_attribute("ignore") = dc.ignore;
	}

	if (doc.save_file(filepath.c_str(), "\t", pugi::format_default, pugi::encoding_utf8)) {
		wxMessageBox("Preset saved successfully.", "Bitmap to Map", wxOK | wxICON_INFORMATION);
	} else {
		wxMessageBox("Failed to save preset.", "Error", wxOK | wxICON_ERROR);
	}
}

void BitmapToMapWindow::OnClickLoadPreset(wxCommandEvent &event) {
	wxFileDialog dlg(this, "Load Preset", "", "", "XML files (*.xml)|*.xml", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	std::string filepath = nstr(dlg.GetPath());

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(filepath.c_str());
	if (!result) {
		wxMessageBox("Failed to load preset: Invalid XML format.", "Error", wxOK | wxICON_ERROR);
		return;
	}

	pugi::xml_node root = doc.child("bitmap_to_map_preset");
	if (!root) {
		wxMessageBox("Invalid preset file: missing <bitmap_to_map_preset> root.", "Error", wxOK | wxICON_ERROR);
		return;
	}

	// Restore settings
	toleranceCtrl->SetValue(root.attribute("tolerance").as_int(30));
	xOffsetCtrl->SetValue(root.attribute("offset_x").as_int(0));
	yOffsetCtrl->SetValue(root.attribute("offset_y").as_int(0));
	zOffsetCtrl->SetValue(root.attribute("offset_z").as_int(7));
	int scaleIdx = root.attribute("scale").as_int(2);
	scaleChoice->SetSelection(std::clamp(scaleIdx, 0, static_cast<int>(scaleChoice->GetCount()) - 1));
	int matchIdx = root.attribute("match_mode").as_int(0);
	matchModeChoice->SetSelection(std::clamp(matchIdx, 0, static_cast<int>(matchModeChoice->GetCount()) - 1));

	// Restore color mappings
	detectedColors.clear();

	for (pugi::xml_node colorNode : root.children("color")) {
		DetectedColor dc;
		dc.r = static_cast<uint8_t>(colorNode.attribute("r").as_uint(0));
		dc.g = static_cast<uint8_t>(colorNode.attribute("g").as_uint(0));
		dc.b = static_cast<uint8_t>(colorNode.attribute("b").as_uint(0));
		dc.suggestedBrush = wxString(colorNode.attribute("brush").as_string(""));
		dc.ignore = colorNode.attribute("ignore").as_bool(false);
		dc.pixelCount = 0;
		detectedColors.push_back(dc);
	}

	// Recalculate pixel counts if image is loaded
	if (imageLoaded) {
		recalculatePixelCounts();
	} else {
		populateColorList();
	}

	wxMessageBox(wxString::Format("Preset loaded: %zu colors.", detectedColors.size()), "Bitmap to Map", wxOK | wxICON_INFORMATION);
}

void BitmapToMapWindow::recalculatePixelCounts() {
	if (!imageLoaded) {
		return;
	}

	for (auto &dc : detectedColors) {
		dc.pixelCount = 0;
	}

	unsigned char* data = loadedImage.GetData();
	bool hasAlpha = loadedImage.HasAlpha();
	unsigned char* alpha = hasAlpha ? loadedImage.GetAlpha() : nullptr;
	int w = loadedImage.GetWidth();
	int h = loadedImage.GetHeight();
	int tolerance = toleranceCtrl->GetValue();

	for (int i = 0; i < w * h; i++) {
		if (hasAlpha && alpha[i] < 128) {
			continue;
		}

		uint8_t r = data[i * 3];
		uint8_t g = data[i * 3 + 1];
		uint8_t b = data[i * 3 + 2];

		for (auto &dc : detectedColors) {
			if (dc.matches(r, g, b, tolerance)) {
				dc.pixelCount++;
				break;
			}
		}
	}

	populateColorList();
}

void BitmapToMapWindow::OnToleranceChanged(wxSpinEvent &event) {
	if (!imageLoaded) {
		return;
	}
	detectColors();
}

void BitmapToMapWindow::OnMatchModeChanged(wxCommandEvent &event) {
	if (!imageLoaded) {
		return;
	}
	detectColors();
}

void BitmapToMapWindow::OnPreviewLeftDown(wxMouseEvent &event) {
	if (!cropState.selectionMode || !imageLoaded) {
		event.Skip();
		return;
	}

	cropState.dragging = true;
	cropState.start = event.GetPosition();
	cropState.end = cropState.start;
	cropState.baseBitmap = imagePreview->GetBitmap();
}

void BitmapToMapWindow::OnPreviewLeftUp(wxMouseEvent &event) {
	if (!cropState.dragging || !cropState.selectionMode) {
		event.Skip();
		return;
	}

	cropState.dragging = false;
	cropState.end = event.GetPosition();

	wxBitmap currentBmp = cropState.baseBitmap;
	if (!currentBmp.IsOk()) {
		cropState.selectionMode = false;
		cropButton->SetLabel("Crop");
		previewPanel->SetCursor(wxNullCursor);
		imagePreview->SetCursor(wxNullCursor);
		updatePreview();
		return;
	}

	int bmpW = currentBmp.GetWidth();
	int bmpH = currentBmp.GetHeight();
	int imgW = loadedImage.GetWidth();
	int imgH = loadedImage.GetHeight();

	// Convert display coordinates to image coordinates
	int dispX1 = std::min(cropState.start.x, cropState.end.x);
	int dispY1 = std::min(cropState.start.y, cropState.end.y);
	int dispX2 = std::max(cropState.start.x, cropState.end.x);
	int dispY2 = std::max(cropState.start.y, cropState.end.y);

	int imgX = dispX1 * imgW / bmpW;
	int imgY = dispY1 * imgH / bmpH;
	int imgX2 = dispX2 * imgW / bmpW;
	int imgY2 = dispY2 * imgH / bmpH;

	int cropW = imgX2 - imgX;
	int cropH = imgY2 - imgY;

	if (cropW < 2 || cropH < 2) {
		cropState.selectionMode = false;
		cropButton->SetLabel("Crop");
		previewPanel->SetCursor(wxNullCursor);
		imagePreview->SetCursor(wxNullCursor);
		updatePreview();
		return;
	}

	// Clamp to image bounds
	imgX = std::max(0, std::min(imgX, imgW - 1));
	imgY = std::max(0, std::min(imgY, imgH - 1));
	cropW = std::min(cropW, imgW - imgX);
	cropH = std::min(cropH, imgH - imgY);

	int answer = wxMessageBox(
		wxString::Format("Crop to selection (%d x %d pixels)?", cropW, cropH),
		"Crop", wxYES_NO | wxICON_QUESTION, this
	);

	if (answer == wxYES) {
		loadedImage = loadedImage.GetSubImage(wxRect(imgX, imgY, cropW, cropH));
		originalImage = loadedImage.Copy();
		imageInfoLabel->SetLabel(wxString::Format("Size: %d x %d", loadedImage.GetWidth(), loadedImage.GetHeight()));
		detectColors();
	}

	cropState.selectionMode = false;
	cropButton->SetLabel("Crop");
	previewPanel->SetCursor(wxNullCursor);
	imagePreview->SetCursor(wxNullCursor);
	updatePreview();
}
