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

#ifndef RME_BITMAP_TO_MAP_WINDOW_H_
#define RME_BITMAP_TO_MAP_WINDOW_H_

#include "main.h"
#include <wx/listctrl.h>
#include <wx/spinctrl.h>
#include <utility>

class Editor;

struct DetectedColor {
	uint8_t r = 0;
	uint8_t g = 0;
	uint8_t b = 0;
	int pixelCount = 0;
	wxString suggestedBrush;
	bool ignore = false;

	DetectedColor() = default;
	DetectedColor(uint8_t r, uint8_t g, uint8_t b, int count) :
		r(r), g(g), b(b), pixelCount(count) { }

	wxString toHex() const {
		return wxString::Format("#%02X%02X%02X", r, g, b);
	}

	bool matches(uint8_t or_, uint8_t og, uint8_t ob, int tolerance) const {
		int dist = std::abs((int)r - or_) + std::abs((int)g - og) + std::abs((int)b - ob);
		return dist <= tolerance;
	}
};

enum class BitmapToMapId : int {
	BROWSE = wxID_HIGHEST + 1,
	GENERATE,
	ROTATE_LEFT,
	ROTATE_RIGHT,
	FLIP,
	CROP,
	SAVE_PRESET,
	LOAD_PRESET,
	DELETE_COLOR,
	FILTER,
	MATCH_MODE,
	TOLERANCE,
	COLOR_LIST,
};

inline int toWxId(BitmapToMapId id) {
	return std::to_underlying(id);
}

class BitmapToMapWindow : public wxDialog {
public:
	BitmapToMapWindow(wxWindow* parent, Editor &editor);

private:
	void OnClickBrowse(wxCommandEvent &event);
	void OnClickGenerate(wxCommandEvent &event);
	void OnClickRotateLeft(wxCommandEvent &event);
	void OnClickRotateRight(wxCommandEvent &event);
	void OnClickFlip(wxCommandEvent &event);
	void OnClickCrop(wxCommandEvent &event);
	void OnPreviewLeftDown(wxMouseEvent &event);
	void OnPreviewLeftUp(wxMouseEvent &event);
	void OnPreviewMouseMove(wxMouseEvent &event);
	void updatePreview();
	void OnClickSavePreset(wxCommandEvent &event);
	void OnClickLoadPreset(wxCommandEvent &event);
	void OnClickDeleteColor(wxCommandEvent &event);
	void OnMatchModeChanged(wxCommandEvent &event);
	void OnToleranceChanged(wxSpinEvent &event);
	void OnFilterColors(wxCommandEvent &event);
	void OnColorListActivated(wxListEvent &event);

	void detectColors();
	void autoSuggestBrushes();
	void populateColorList();
	void recalculatePixelCounts();

	Editor &editor;
	wxImage loadedImage;
	bool imageLoaded;

	// Controls
	wxStaticBitmap* imagePreview;
	wxScrolledWindow* previewPanel;
	wxListCtrl* colorListCtrl;
	wxTextCtrl* filterCtrl;
	wxSpinCtrl* toleranceCtrl;
	wxSpinCtrl* xOffsetCtrl;
	wxSpinCtrl* yOffsetCtrl;
	wxSpinCtrl* zOffsetCtrl;
	wxStaticText* imageInfoLabel;
	wxGauge* progressBar;
	wxChoice* matchModeChoice;
	wxButton* cropButton;
	wxChoice* scaleChoice;
	wxStaticText* pixelInfoLabel;
	wxImage originalImage;

	struct CropState {
		bool selectionMode = false;
		bool dragging = false;
		wxPoint start;
		wxPoint end;
		wxBitmap baseBitmap;
	};

	CropState cropState;

	// Data
	std::vector<DetectedColor> detectedColors;
	static const int MAX_COLORS = 256;

	DECLARE_EVENT_TABLE()
};

#endif // RME_BITMAP_TO_MAP_WINDOW_H_
