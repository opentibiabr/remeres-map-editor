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
#include "palette_window.h" // For BrushPalettePanel
#include "palette_brushlist.h" // For full BrushPalettePanel definition
#include "materials.h" // For g_materials

#include "brush_editor_window.h"
#include "brush_browser_grid_panel.h"
#include "browse_tile_window.h"
#include "find_item_window.h"
#include "common_windows.h"
#include "graphics.h"
#include "gui.h"
#include "artprovider.h"
#include "items.h"
#include "brush.h"
#include "raw_brush.h"
#include "ground_brush.h"
#include "client_assets.h"
#include "main_menubar.h"
#include "gui_ids.h"
#include <wx/graphics.h>
#include <wx/sizer.h>
#include <wx/gbsizer.h>
#include <wx/statline.h>
#include <wx/tglbtn.h>
#include <wx/dcbuffer.h>
#include <wx/filename.h>
#include <wx/wrapsizer.h>
#include <wx/settings.h>
#include <pugixml.hpp>
#include <wx/file.h>
#include <wx/stdpaths.h>
#include <sstream>

#define BorderEditorPanel BrushEditorPanel

#ifndef SPRITE_SIZE_32x32_SCALED
	#define SPRITE_SIZE_32x32_SCALED SPRITE_SIZE_32x32
#endif

// Helper to calculate grid metrics (Shared between BorderGridPanel and WallGridPanel)
struct GridMetrics {
	int cellSize;
	int spacing;
	int startX;
	int startY;
	int normalX, normalY;
	int cornerX, cornerY;
	int diagX, diagY;

	GridMetrics(const wxSize &size) {
		int w = size.GetWidth();
		int h = size.GetHeight();
		int margin = 5;

		// We have 3 blocks of 2x2.
		// Width = 6 * cell + 2 * spacing.
		// Let spacing = cell / 2.
		// Total width units = 7.
		// Height = 2 * cell.

		cellSize = std::min((w - 2 * margin) / 7, (h - 2 * margin) / 2);
		if (cellSize < 16) {
			cellSize = 16; // Minimum size
		}

		spacing = cellSize / 2;
		int totalWidth = (2 * cellSize) * 3 + 2 * spacing;

		startX = (w - totalWidth) / 2;
		startY = (h - 2 * cellSize) / 2;

		normalX = startX;
		normalY = startY;

		cornerX = normalX + 2 * cellSize + spacing;
		cornerY = startY;

		diagX = cornerX + 2 * cellSize + spacing;
		diagY = startY;
	}
};

#define BORDER_GRID_SIZE 32
#define BORDER_PREVIEW_SIZE 192
#define BORDER_GRID_CELL_SIZE 32
#define ID_BORDER_GRID_SELECT wxID_HIGHEST + 1
#define ID_GROUND_ITEM_LIST wxID_HIGHEST + 2

static std::vector<std::pair<wxString, wxString>> GetBrowserTilesetOptions();

static bool ReadAllText(const wxString &path, wxString &out) {
	wxFile file(path, wxFile::read);
	if (!file.IsOpened()) {
		return false;
	}
	file.ReadAll(&out);
	file.Close();
	return true;
}

static bool WriteAllText(const wxString &path, const wxString &content) {
	wxFile file;
	if (!file.Create(path, true)) {
		return false;
	}
	file.Write(content);
	file.Close();
	return true;
}

void BorderEditorPanel::LogError(const wxString &message, ErrorSeverity severity) {
	wxString severityStr;
	switch (severity) {
		case ErrorSeverity::Info:
			severityStr = "INFO";
			break;
		case ErrorSeverity::Warning:
			severityStr = "WARNING";
			break;
		case ErrorSeverity::Error:
			severityStr = "ERROR";
			break;
		case ErrorSeverity::Critical:
			severityStr = "CRITICAL";
			break;
	}
	wxLogMessage("[BrushEditor] [%s] %s", severityStr, message);
}

void BorderEditorPanel::ShowErrorDialog(const wxString &message, ErrorSeverity severity) {
	LogError(message, severity);

	long style;
	switch (severity) {
		case ErrorSeverity::Info:
			style = wxOK | wxICON_INFORMATION;
			break;
		case ErrorSeverity::Warning:
			style = wxOK | wxICON_WARNING;
			break;
		case ErrorSeverity::Error:
			style = wxOK | wxICON_ERROR;
			break;
		case ErrorSeverity::Critical:
			style = wxOK | wxICON_ERROR;
			break;
	}

	wxMessageBox(message, "Brush Editor", style, this);
}

static wxString DetectNewline(const wxString &text) {
	return (text.Find("\r\n") != wxNOT_FOUND) ? "\r\n" : "\n";
}

static wxString IndentBeforePos(const wxString &text, size_t pos) {
	if (pos > text.length()) {
		pos = text.length();
	}
	size_t lineStart = text.rfind('\n', pos);
	if (lineStart == wxString::npos) {
		lineStart = 0;
	} else {
		lineStart += 1;
	}
	size_t i = lineStart;
	while (i < pos && (text[i] == ' ' || text[i] == '\t')) {
		++i;
	}
	return text.Mid(lineStart, i - lineStart);
}

static wxString DetectChildIndent(const wxString &text, const wxString &elementName) {
	const wxString open = "<" + elementName;
	const size_t pos = text.find(open);
	if (pos == wxString::npos) {
		return "    ";
	}
	return IndentBeforePos(text, pos);
}

static bool FindXmlElementRangeSimple(const wxString &text, const wxString &elementName, const wxString &attrNeedle, size_t &outStart, size_t &outEnd) {
	const wxString open = "<" + elementName;
	const wxString close = "</" + elementName + ">";

	size_t pos = 0;
	while ((pos = text.find(open, pos)) != wxString::npos) {
		const size_t tagEnd = text.find('>', pos);
		if (tagEnd == wxString::npos) {
			return false;
		}

		const wxString startTag = text.Mid(pos, tagEnd - pos + 1);
		if (startTag.find(attrNeedle) == wxString::npos) {
			pos = tagEnd + 1;
			continue;
		}

		const size_t closePos = text.find(close, tagEnd + 1);
		if (closePos == wxString::npos) {
			return false;
		}

		size_t end = closePos + close.length();
		if (end < text.length() && text[end] == '\r') {
			if (end + 1 < text.length() && text[end + 1] == '\n') {
				end += 2;
			} else {
				end += 1;
			}
		} else if (end < text.length() && text[end] == '\n') {
			end += 1;
		}

		outStart = pos;
		outEnd = end;
		return true;
	}

	return false;
}

static void ExpandStartToIncludePrevSingleLineComment(const wxString &text, size_t &start) {
	if (start == 0) {
		return;
	}

	size_t lineStart = text.rfind('\n', start - 1);
	if (lineStart == wxString::npos) {
		lineStart = 0;
	} else {
		lineStart += 1;
	}

	wxString line = text.Mid(lineStart, start - lineStart);
	line.Trim(true);
	line.Trim(false);

	if (line.StartsWith("<!--") && line.EndsWith("-->")) {
		start = lineStart;
	}
}

static wxString PrintNodeIndented(const pugi::xml_node &node, const wxString &baseIndent, const wxString &nl) {
	std::ostringstream oss;
	node.print(oss, "  ", pugi::format_default, pugi::encoding_utf8);

	const std::string s = oss.str();
	wxString out = wxString::FromUTF8(s.c_str());

	if (out.EndsWith("\n")) {
		out.RemoveLast();
	}

	out.Replace("\n", "\n" + baseIndent);
	out = baseIndent + out;

	if (nl != "\n") {
		out.Replace("\n", nl);
	}

	if (!out.EndsWith(nl)) {
		out += nl;
	}

	return out;
}

static bool ReplaceOrInsertChildSimple(wxString &text, const wxString &elementName, const wxString &attrNeedle, const wxString &newBlock, bool includePrevSingleLineComment) {
	size_t start = 0;
	size_t end = 0;

	if (FindXmlElementRangeSimple(text, elementName, attrNeedle, start, end)) {
		if (includePrevSingleLineComment) {
			ExpandStartToIncludePrevSingleLineComment(text, start);
		}
		text = text.Mid(0, start) + newBlock + text.Mid(end);
		return true;
	}

	const size_t insertPos = text.rfind("</materials>");
	if (insertPos == wxString::npos) {
		return false;
	}

	const wxString nl = DetectNewline(text);
	wxString insertion = newBlock;
	const wxString before = text.Mid(0, insertPos);
	if (!before.EndsWith(nl)) {
		insertion = nl + insertion;
	}

	text = before + insertion + text.Mid(insertPos);
	return true;
}

// Utility functions for edge string/position conversion
BorderEdgePosition edgeStringToPosition(const std::string &edgeStr) {
	if (edgeStr == "n") {
		return EDGE_N;
	}
	if (edgeStr == "e") {
		return EDGE_E;
	}
	if (edgeStr == "s") {
		return EDGE_S;
	}
	if (edgeStr == "w") {
		return EDGE_W;
	}
	if (edgeStr == "cnw") {
		return EDGE_CNW;
	}
	if (edgeStr == "cne") {
		return EDGE_CNE;
	}
	if (edgeStr == "cse") {
		return EDGE_CSE;
	}
	if (edgeStr == "csw") {
		return EDGE_CSW;
	}
	if (edgeStr == "dnw") {
		return EDGE_DNW;
	}
	if (edgeStr == "dne") {
		return EDGE_DNE;
	}
	if (edgeStr == "dse") {
		return EDGE_DSE;
	}
	if (edgeStr == "dsw") {
		return EDGE_DSW;
	}
	return EDGE_NONE;
}

std::string edgePositionToString(BorderEdgePosition pos) {
	switch (pos) {
		case EDGE_N:
			return "n";
		case EDGE_E:
			return "e";
		case EDGE_S:
			return "s";
		case EDGE_W:
			return "w";
		case EDGE_CNW:
			return "cnw";
		case EDGE_CNE:
			return "cne";
		case EDGE_CSE:
			return "cse";
		case EDGE_CSW:
			return "csw";
		case EDGE_DNW:
			return "dnw";
		case EDGE_DNE:
			return "dne";
		case EDGE_DSE:
			return "dse";
		case EDGE_DSW:
			return "dsw";
		default:
			return "";
	}
}

// Add a helper function at the top of the file to get item ID from brush
uint16_t GetItemIDFromBrush(Brush* brush) {
	if (!brush) {
		wxLogDebug("GetItemIDFromBrush: Brush is null");

		return 0;
	}

	uint16_t id = 0;

	wxLogDebug("GetItemIDFromBrush: Checking brush type: %s", wxString(brush->getName()).c_str());

	// First prioritize RAW brush - this is the most direct approach
	if (brush->isRaw()) {
		RAWBrush* rawBrush = brush->asRaw();
		if (rawBrush) {
			id = rawBrush->getItemID();
			wxLogDebug("GetItemIDFromBrush: Found RAW brush ID: %d", id);

			if (id > 0) {
				return id;
			}
		}
	}

	// Then try getID which sometimes works directly
	id = brush->getID();
	if (id > 0) {
		wxLogDebug("GetItemIDFromBrush: Got ID from brush->getID(): %d", id);

		return id;
	}

	// Try getLookID which works for most other brush types
	id = brush->getLookID();
	if (id > 0) {
		wxLogDebug("GetItemIDFromBrush: Got ID from getLookID(): %d", id);

		return id;
	}

	// Try specific brush type methods - when all else fails
	if (brush->isGround()) {
		wxLogDebug("GetItemIDFromBrush: Detected Ground brush");

		GroundBrush* groundBrush = brush->asGround();
		if (groundBrush) {
			// For ground brush, id is usually the server_lookid from grounds.xml
			// Try to find something else
			wxLogDebug("GetItemIDFromBrush: Failed to get ID for Ground brush");
		}
	} else if (brush->isWall()) {
		wxLogDebug("GetItemIDFromBrush: Detected Wall brush");

		WallBrush* wallBrush = brush->asWall();
		if (wallBrush) {
			wxLogDebug("GetItemIDFromBrush: Failed to get ID for Wall brush");
		}
	} else if (brush->isDoodad()) {
		wxLogDebug("GetItemIDFromBrush: Detected Doodad brush");

		DoodadBrush* doodadBrush = brush->asDoodad();
		if (doodadBrush) {
			wxLogDebug("GetItemIDFromBrush: Failed to get ID for Doodad brush");
		}
	}

	if (id == 0) {
		wxLogDebug("GetItemIDFromBrush: Failed to get item ID from brush %s", wxString(brush->getName()).c_str());
	}

	return id;
}

// Event table for BorderEditorPanel
wxDEFINE_EVENT(wxEVT_GROUND_CONTAINER_CHANGE, wxCommandEvent);

static void TriggerReloadDataFiles() {
	if (!g_gui.root) {
		return;
	}

	wxCommandEvent reloadEvent(
		wxEVT_COMMAND_MENU_SELECTED,
		MAIN_FRAME_MENU + MenuBar::RELOAD_DATA
	);
	g_gui.root->GetEventHandler()->ProcessEvent(reloadEvent);
}

bool BorderEditorPanel::RestoreBrowserSelection(const wxString &selectionKey) {
	if (!m_borderBrowserList) {
		return false;
	}

	if (selectionKey.IsEmpty()) {
		m_borderBrowserList->SetSelection(wxNOT_FOUND);
		if (m_browserGrid) {
			m_browserGrid->SetSelectionByKey(wxString());
		}
		return false;
	}

	const int wanted = wxAtoi(selectionKey);

	for (unsigned int i = 0; i < m_borderBrowserList->GetCount(); ++i) {
		wxStringClientData* data = static_cast<wxStringClientData*>(m_borderBrowserList->GetClientObject(i));
		if (!data) {
			continue;
		}

		if (wxAtoi(data->GetData()) == wanted) {
			m_borderBrowserList->SetSelection(i);
			if (m_browserGrid) {
				m_browserGrid->SetSelectionByKey(selectionKey);
			}
			return true;
		}
	}

	m_borderBrowserList->SetSelection(wxNOT_FOUND);
	if (m_browserGrid) {
		m_browserGrid->SetSelectionByKey(wxString());
	}
	return false;
}

void BorderEditorPanel::ReloadCurrentBrushEditorXml(const wxString &selectionKey) {
	if (m_browserSearchCtrl) {
		m_browserSearchCtrl->Clear();
	}

	LoadTilesets();
	PopulateUnifiedList();

	RestoreBrowserSelection(selectionKey);

	const uint16_t tileId = static_cast<uint16_t>(wxAtoi(selectionKey));
	switch (m_activeTab) {
		case 0:
			LoadBorderByMainTileId(tileId);
			break;
		case 1:
			LoadGroundBrushByMainTileId(tileId);
			break;
		case 2:
			LoadWallBrushByMainTileId(tileId);
			break;
	}

	Layout();
	Refresh();
	Update();
}

BEGIN_EVENT_TABLE(BorderEditorPanel, wxPanel)
EVT_BUTTON(wxID_ADD, BorderEditorPanel::OnAddItem)
EVT_BUTTON(wxID_CLEAR, BorderEditorPanel::OnClear)
EVT_BUTTON(wxID_SAVE, BorderEditorPanel::OnSave)
EVT_BUTTON(wxID_CLOSE, BorderEditorPanel::OnClose)
EVT_BUTTON(wxID_FIND, BorderEditorPanel::OnBrowse)
EVT_COMBOBOX(wxID_ANY, BorderEditorPanel::OnLoadBorder)
EVT_NOTEBOOK_PAGE_CHANGED(wxID_ANY, BorderEditorPanel::OnPageChanged)
END_EVENT_TABLE()

// Event table for BorderItemButton
BEGIN_EVENT_TABLE(BorderItemButton, wxButton)
EVT_PAINT(BorderItemButton::OnPaint)
END_EVENT_TABLE()

// Event table for BorderGridPanel
BEGIN_EVENT_TABLE(BorderGridPanel, wxPanel)
EVT_PAINT(BorderGridPanel::OnPaint)
EVT_LEFT_UP(BorderGridPanel::OnMouseClick)
EVT_LEFT_DOWN(BorderGridPanel::OnMouseDown)
END_EVENT_TABLE()

// Event table for BorderPreviewPanel
BEGIN_EVENT_TABLE(BorderPreviewPanel, wxPanel)
EVT_PAINT(BorderPreviewPanel::OnPaint)
END_EVENT_TABLE()

BorderEditorPanel::BorderEditorPanel(wxWindow* parent) :
	wxPanel(parent, wxID_ANY),
	m_nextBorderId(1),
	m_activeTab(1),
	m_selectedBrowserTileId(0),
	m_lastInteractionTime(0) {

	// Load saved filter configuration (before creating UI)
	LoadFilterConfig();

	CreateGUIControls();

	// Default to Ground tab
	m_notebook->SetSelection(1);
	if (m_borderModeBtn) {
		m_borderModeBtn->SetValue(false);
	}
	if (m_groundModeBtn) {
		m_groundModeBtn->SetValue(true);
	}
	if (m_wallModeBtn) {
		m_wallModeBtn->SetValue(false);
	}

	// Explicitly populate the sidebar (unified list) for the initial tab
	// (OnPageChanged may not fire for the initial selection on all platforms)
	PopulateUnifiedList();
	UpdateBrowserLabel();

	// Set initial border ID
	m_currentBorderId = m_nextBorderId;

	Layout();
}

BorderEditorPanel::~BorderEditorPanel() {
	// Nothing to destroy manually
}

void BorderEditorPanel::SaveDraft(int tab, uint16_t tileId, bool markDirty) {
	if (m_isRestoringDraft) {
		return;
	}
	if (tileId == 0) {
		return;
	}

	switch (tab) {
		case 0: {
			BorderDraftState &d = m_borderDrafts[tileId];
			d.currentBorderId = m_currentBorderId;
			d.borderName = m_loadedBorderName;
			d.group = m_groupCheck ? m_groupCheck->GetValue() : false;
			d.optional = m_isOptionalCheck ? m_isOptionalCheck->GetValue() : false;
			d.ground = m_isGroundCheck ? m_isGroundCheck->GetValue() : false;
			d.items = m_borderItems;
			d.dirty = d.dirty || markDirty;
			break;
		}
		case 1: {
			GroundDraftState &d = m_groundDrafts[tileId];
			d.name = m_groundNameCtrl ? m_groundNameCtrl->GetValue() : wxString();
			d.zOrder = m_zOrderCtrl ? m_zOrderCtrl->GetValue() : 0;
			d.borderAlignmentSelection = m_borderAlignmentSelection;
			d.includeToNone = m_includeToNoneCheck ? m_includeToNoneCheck->GetValue() : true;
			d.includeInner = m_includeInnerCheck ? m_includeInnerCheck->GetValue() : false;
			d.items = m_groundGridContainer ? m_groundGridContainer->GetAllItemsWithChance() : std::vector<std::pair<uint16_t, int>>();
			d.dirty = d.dirty || markDirty;
			break;
		}
		case 2: {
			WallDraftState &d = m_wallDrafts[tileId];
			d.brushName = m_groundNameCtrl ? m_groundNameCtrl->GetValue() : wxString();
			d.wallTypes = m_wallTypes;
			d.selectedType = m_wallGridPanel ? m_wallGridPanel->GetSelectedType() : std::string();
			d.dirty = d.dirty || markDirty;
			break;
		}
		default:
			break;
	}
}

bool BorderEditorPanel::RestoreDraft(int tab, uint16_t tileId) {
	if (tileId == 0) {
		return false;
	}

	if (tab == 0) {
		auto it = m_borderDrafts.find(tileId);
		if (it == m_borderDrafts.end()) {
			return false;
		}

		m_isRestoringDraft = true;
		ClearItems(false);

		const BorderDraftState &d = it->second;
		m_currentBorderId = d.currentBorderId;
		m_loadedBorderName = d.borderName;
		if (m_groupCheck) {
			m_groupCheck->SetValue(d.group);
		}
		if (m_isOptionalCheck) {
			m_isOptionalCheck->SetValue(d.optional);
		}
		if (m_isGroundCheck) {
			m_isGroundCheck->SetValue(d.ground);
		}

		m_borderItems = d.items;
		if (m_gridPanel) {
			for (const BorderItem &item : m_borderItems) {
				m_gridPanel->SetItemId(item.position, item.itemId);
			}
		}
		UpdatePreview();
		m_isRestoringDraft = false;
		return true;
	}

	if (tab == 1) {
		auto it = m_groundDrafts.find(tileId);
		if (it == m_groundDrafts.end()) {
			return false;
		}

		m_isRestoringDraft = true;
		ClearGroundItems(false);

		const GroundDraftState &d = it->second;
		if (m_groundNameCtrl) {
			m_groundNameCtrl->SetValue(d.name);
		}
		if (m_zOrderCtrl) {
			m_zOrderCtrl->SetValue(d.zOrder);
		}
		m_borderAlignmentSelection = d.borderAlignmentSelection;
		if (m_borderAlignmentButton) {
			m_borderAlignmentButton->SetLabel((m_borderAlignmentSelection == 1 ? "Inner" : "Outer") + wxString(" \u25BC"));
		}
		if (m_includeToNoneCheck) {
			m_includeToNoneCheck->SetValue(d.includeToNone);
		}
		if (m_includeInnerCheck) {
			m_includeInnerCheck->SetValue(d.includeInner);
		}

		if (m_groundGridContainer) {
			m_groundGridContainer->Clear();
			for (const auto &item : d.items) {
				if (item.first > 0) {
					m_groundGridContainer->AddItem(item.first, item.second, true);
				}
			}
		}
		if (m_groundPreviewPanel && m_groundGridContainer) {
			m_groundPreviewPanel->UpdateFromContainer(m_groundGridContainer);
		}

		m_isRestoringDraft = false;
		return true;
	}

	if (tab == 2) {
		auto it = m_wallDrafts.find(tileId);
		if (it == m_wallDrafts.end()) {
			return false;
		}

		m_isRestoringDraft = true;
		ClearWallItems(false);

		const WallDraftState &d = it->second;
		if (m_groundNameCtrl) {
			m_groundNameCtrl->SetValue(d.brushName);
		}

		m_wallTypes = d.wallTypes;
		if (m_wallGridPanel) {
			m_wallGridPanel->SetWallTypes(m_wallTypes);
			m_wallGridPanel->SetSelectedType(d.selectedType.empty() ? std::string("vertical") : d.selectedType);
		}
		if (m_wallVisualPanel) {
			m_wallVisualPanel->SetWallItems(m_wallTypes);
		}
		UpdateWallItemsList();

		m_isRestoringDraft = false;
		return true;
	}

	return false;
}

void BorderEditorPanel::DiscardDraft(int tab, uint16_t tileId) {
	if (tileId == 0) {
		return;
	}

	switch (tab) {
		case 0:
			m_borderDrafts.erase(tileId);
			break;
		case 1:
			m_groundDrafts.erase(tileId);
			break;
		case 2:
			m_wallDrafts.erase(tileId);
			break;
		default:
			break;
	}
}

void BorderEditorPanel::SaveCurrentDraft(bool markDirty) {
	if (m_selectedBrowserTileId == 0) {
		return;
	}

	SaveDraft(m_activeTab, m_selectedBrowserTileId, markDirty);
}

void BorderEditorPanel::DiscardCurrentDraft() {
	if (m_selectedBrowserTileId == 0) {
		return;
	}

	DiscardDraft(m_activeTab, m_selectedBrowserTileId);
}

void BorderEditorPanel::OnBorderDraftChange(wxCommandEvent &event) {
	SaveCurrentDraft(true);
	event.Skip();
}

void BorderEditorPanel::OnGroundDraftChange(wxCommandEvent &event) {
	SaveCurrentDraft(true);
	event.Skip();
}

void BorderEditorPanel::OnWallDraftChange(wxCommandEvent &event) {
	SaveCurrentDraft(true);
	event.Skip();
}

void BorderEditorPanel::CreateGUIControls() {
	wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

	// Create simplebook (no visible tabs - switching via sidebar buttons)
	m_notebook = new wxSimplebook(this, wxID_ANY);

	// ========== BORDER TAB ==========
	m_borderPanel = new wxPanel(m_notebook);
	wxBoxSizer* borderSizer = new wxBoxSizer(wxVERTICAL);

	// --- Top Toolbar: Name + Checkboxes (Merged) ---
	wxBoxSizer* toolbarSizer = new wxBoxSizer(wxHORIZONTAL);

	// Checkboxes (Right side of toolbar)
	m_groupCheck = new wxCheckBox(m_borderPanel, wxID_ANY, "Group");
	m_groupCheck->SetToolTip("Check to assign this border to a group");
	m_groupCheck->Bind(wxEVT_CHECKBOX, &BorderEditorPanel::OnBorderDraftChange, this);
	toolbarSizer->Add(m_groupCheck, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);

	m_isOptionalCheck = new wxCheckBox(m_borderPanel, wxID_ANY, "Optional");
	m_isOptionalCheck->SetToolTip("Marks this border as optional");
	m_isOptionalCheck->Bind(wxEVT_CHECKBOX, &BorderEditorPanel::OnBorderDraftChange, this);
	toolbarSizer->Add(m_isOptionalCheck, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);

	m_isGroundCheck = new wxCheckBox(m_borderPanel, wxID_ANY, "Ground");
	m_isGroundCheck->SetToolTip("Marks this border as a ground border");
	m_isGroundCheck->Bind(wxEVT_CHECKBOX, &BorderEditorPanel::OnBorderDraftChange, this);
	toolbarSizer->Add(m_isGroundCheck, 0, wxALIGN_CENTER_VERTICAL);

	borderSizer->Add(toolbarSizer, 0, wxEXPAND | wxALL, 5);

	// --- Main Content: Palette (Left) + Editor/Preview (Right) ---
	wxBoxSizer* borderContentSizer = new wxBoxSizer(wxHORIZONTAL);

	// === Left Column: Raw Item Palette (takes full height) ===
	wxBoxSizer* leftColumnSizer = new wxBoxSizer(wxVERTICAL);

	// Raw Item Palette - shows all raw items for selection    // Raw Item Palette - shows all raw items for selection
	// Category Selector Row (ComboBox + Filter Button)
	wxBoxSizer* borderTilesetRowSizer = new wxBoxSizer(wxHORIZONTAL);

	wxArrayString categories;
	for (const auto &pair : g_materials.tilesets) {
		categories.Add(wxstr(pair.first));
	}
	m_rawCategoryNames = categories; // Store for menu
	m_rawCategoryButton = new wxButton(m_borderPanel, wxID_ANY, "Category ▼", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
	m_rawCategoryButton->SetToolTip("Filter by Tileset Category");
	m_rawCategoryButton->Bind(wxEVT_BUTTON, &BorderEditorPanel::OnRawCategoryButtonClick, this);
	borderTilesetRowSizer->Add(m_rawCategoryButton, 1, wxEXPAND | wxRIGHT, 2);

	// Filter button with Cut icon (scissors)
	wxBitmapButton* borderFilterBtn = new wxBitmapButton(m_borderPanel, wxID_ANY, wxArtProvider::GetBitmap(wxART_CUT, wxART_BUTTON, wxSize(16, 16)));
	borderFilterBtn->SetToolTip("Filter visible tilesets");
	borderFilterBtn->Bind(wxEVT_BUTTON, &BorderEditorPanel::OnOpenTilesetFilter, this);
	borderTilesetRowSizer->Add(borderFilterBtn, 0, wxALIGN_CENTER_VERTICAL);

	leftColumnSizer->Add(borderTilesetRowSizer, 0, wxEXPAND | wxBOTTOM, 5);

	m_itemPalettePanel = new SimpleRawPalettePanel(m_borderPanel);
	// m_itemPalettePanel->SetListType(BRUSHLIST_LARGE_ICONS);
	m_itemPalettePanel->SetMinSize(wxSize(220, 300));
	leftColumnSizer->Add(m_itemPalettePanel, 1, wxEXPAND | wxALL, 5);

	borderContentSizer->Add(leftColumnSizer, 0, wxEXPAND);

	// === Right Column: Editor + Preview ===
	wxBoxSizer* rightColumnSizer = new wxBoxSizer(wxVERTICAL);

	// Editor inside StaticBoxSizer
	wxStaticBoxSizer* editorBoxSizer = new wxStaticBoxSizer(wxVERTICAL, m_borderPanel, "Editor");
	m_gridPanel = new BorderGridPanel(editorBoxSizer->GetStaticBox());
	editorBoxSizer->Add(m_gridPanel, 0, wxALIGN_CENTER | wxALL, 5);
	rightColumnSizer->Add(editorBoxSizer, 0, wxEXPAND | wxALL, 5);

	// Preview inside StaticBoxSizer
	wxStaticBoxSizer* previewBoxSizer = new wxStaticBoxSizer(wxVERTICAL, m_borderPanel, "Preview");
	m_previewPanel = new BorderPreviewPanel(previewBoxSizer->GetStaticBox());
	m_previewPanel->SetMinSize(wxSize(200, 200));
	previewBoxSizer->Add(m_previewPanel, 1, wxEXPAND | wxALL, 5);
	rightColumnSizer->Add(previewBoxSizer, 1, wxEXPAND | wxALL, 5);

	borderContentSizer->Add(rightColumnSizer, 1, wxEXPAND | wxLEFT, 5);

	// Add main content to border sizer (takes most vertical space)
	borderSizer->Add(borderContentSizer, 1, wxEXPAND | wxALL, 5);

	// --- Bottom Action Bar ---
	wxBoxSizer* borderButtonSizer = new wxBoxSizer(wxHORIZONTAL);
	borderButtonSizer->AddStretchSpacer(1);

	m_newButton = new wxButton(m_borderPanel, wxID_CLEAR, "New Border");
	m_newButton->SetToolTip("Start creating a new border (clears current form)");
	borderButtonSizer->Add(m_newButton, 0, wxRIGHT, 5);

	borderButtonSizer->Add(new wxButton(m_borderPanel, wxID_SAVE, "Save Changes"), 0, wxRIGHT, 5);
	borderButtonSizer->Add(new wxButton(m_borderPanel, wxID_CLOSE, "Close"), 0);

	borderSizer->Add(borderButtonSizer, 0, wxEXPAND | wxALL, 5);

	m_borderPanel->SetSizer(borderSizer);

	// ========== GROUND TAB ==========
	m_groundPanel = new wxPanel(m_notebook);
	wxBoxSizer* groundSizer = new wxBoxSizer(wxVERTICAL);

	// === Left Column: Form Controls ===
	wxBoxSizer* groundLeftSizer = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* row1 = new wxBoxSizer(wxHORIZONTAL);
	row1->Add(new wxStaticText(m_groundPanel, wxID_ANY, "Name:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	m_groundNameCtrl = new wxTextCtrl(m_groundPanel, wxID_ANY, "");
	m_groundNameCtrl->Bind(wxEVT_TEXT, &BorderEditorPanel::OnGroundDraftChange, this);
	row1->Add(m_groundNameCtrl, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);
	groundLeftSizer->Add(row1, 0, wxEXPAND | wxBOTTOM, 5);

	wxBoxSizer* row2 = new wxBoxSizer(wxHORIZONTAL);
	row2->Add(new wxStaticText(m_groundPanel, wxID_ANY, "Z-Order:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	m_zOrderCtrl = new wxSpinCtrl(m_groundPanel, wxID_ANY, "0", wxDefaultPosition, wxSize(60, -1));
	m_zOrderCtrl->SetRange(-100, 100);
	m_zOrderCtrl->Bind(wxEVT_SPINCTRL, &BorderEditorPanel::OnGroundDraftChange, this);
	row2->Add(m_zOrderCtrl, 0);
	groundLeftSizer->Add(row2, 0, wxEXPAND | wxBOTTOM, 5);

	// Item Palette / Selection
	wxBoxSizer* row3 = new wxBoxSizer(wxHORIZONTAL);
	// Chance control removed
	groundLeftSizer->Add(row3, 0, wxEXPAND | wxBOTTOM, 5);

	// Preview Panel moved to right column
	wxBoxSizer* tilesetRowSizer = new wxBoxSizer(wxHORIZONTAL);
	tilesetRowSizer->Add(new wxStaticText(m_groundPanel, wxID_ANY, "Source:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

	// --- Main Body: Palette (Left) + Editor (Right) ---
	wxBoxSizer* groundContentSizer = new wxBoxSizer(wxHORIZONTAL);

	// === Left Column: Raw Item Palette ===
	// Tileset selector row (ComboBox + Gear button)

	m_groundTilesetButton = new wxButton(m_groundPanel, wxID_ANY, "Select Tileset ▼", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
	m_groundTilesetButton->SetToolTip("Select Tileset");
	m_groundTilesetButton->Bind(wxEVT_BUTTON, &BorderEditorPanel::OnGroundTilesetButtonClick, this);
	tilesetRowSizer->Add(m_groundTilesetButton, 1, wxEXPAND | wxRIGHT, 2);

	// Filter button with Cut icon (scissors)
	m_tilesetFilterBtn = new wxBitmapButton(m_groundPanel, wxID_ANY, wxArtProvider::GetBitmap(wxART_CUT, wxART_BUTTON, wxSize(16, 16)));
	m_tilesetFilterBtn->SetToolTip("Filter visible tilesets");
	m_tilesetFilterBtn->Bind(wxEVT_BUTTON, &BorderEditorPanel::OnOpenTilesetFilter, this);
	tilesetRowSizer->Add(m_tilesetFilterBtn, 0, wxALIGN_CENTER_VERTICAL);

	groundLeftSizer->Add(tilesetRowSizer, 0, wxEXPAND | wxBOTTOM, 5);

	m_groundPalette = new SimpleRawPalettePanel(m_groundPanel);
	m_groundPalette->SetMinSize(wxSize(220, 200));
	m_groundPalette->Bind(wxEVT_COMMAND_LISTBOX_SELECTED, &BorderEditorPanel::OnGroundPaletteSelect, this);
	groundLeftSizer->Add(m_groundPalette, 1, wxEXPAND | wxALL, 5);
	groundContentSizer->Add(groundLeftSizer, 0, wxEXPAND);

	// === Right Column: Editor Controls (Scrollable List) ===
	// === Right Column: Editor Controls (Scrollable List) ===
	wxBoxSizer* groundRightSizer = new wxBoxSizer(wxVERTICAL);

	// Preview (Directly in sizer for seamless look)
	m_groundPreviewPanel = new GroundPreviewPanel(m_groundPanel, wxID_ANY);
	// m_groundPreviewPanel size is set in constructor (96x96)
	groundRightSizer->Add(m_groundPreviewPanel, 0, wxALIGN_CENTER | wxALL, 10);

	wxStaticBoxSizer* groundEditorBoxSizer = new wxStaticBoxSizer(wxVERTICAL, m_groundPanel, "Variations");

	m_groundGridContainer = new GroundGridContainer(groundEditorBoxSizer->GetStaticBox(), wxID_ANY + 1); // Added ID
	m_groundGridContainer->Bind(wxEVT_COMMAND_LISTBOX_SELECTED, &BorderEditorPanel::OnGroundGridSelect, this);
	// Bind change event to update preview
	Bind(wxEVT_GROUND_CONTAINER_CHANGE, &BorderEditorPanel::OnGroundContainerChange, this);

	groundEditorBoxSizer->Add(m_groundGridContainer, 1, wxEXPAND | wxALL, 5);

	wxBoxSizer* addVarSizer = new wxBoxSizer(wxHORIZONTAL);
	addVarSizer->AddStretchSpacer(1);
	m_addVariationBtn = new wxButton(groundEditorBoxSizer->GetStaticBox(), wxID_ANY, "Add Variation");
	m_addVariationBtn->SetToolTip("Add a new empty variation at the end");
	m_addVariationBtn->Bind(wxEVT_BUTTON, &BorderEditorPanel::OnAddVariation, this);
	addVarSizer->Add(m_addVariationBtn, 0);
	groundEditorBoxSizer->Add(addVarSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

	groundRightSizer->Add(groundEditorBoxSizer, 1, wxEXPAND | wxALL, 5);

	// Border Options inside StaticBox
	wxStaticBoxSizer* borderOptionsBoxSizer = new wxStaticBoxSizer(wxVERTICAL, m_groundPanel, "Border Options");

	wxBoxSizer* borderOptionsRow = new wxBoxSizer(wxHORIZONTAL);

	borderOptionsRow->Add(new wxStaticText(borderOptionsBoxSizer->GetStaticBox(), wxID_ANY, "Alignment:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	m_borderAlignmentButton = new wxButton(borderOptionsBoxSizer->GetStaticBox(), wxID_ANY, "Outer ▼", wxDefaultPosition, wxSize(80, -1), wxBU_LEFT);
	m_borderAlignmentButton->Bind(wxEVT_BUTTON, &BorderEditorPanel::OnBorderAlignmentButtonClick, this);
	borderOptionsRow->Add(m_borderAlignmentButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);

	m_includeToNoneCheck = new wxCheckBox(borderOptionsBoxSizer->GetStaticBox(), wxID_ANY, "To None");
	m_includeToNoneCheck->SetValue(true);
	m_includeToNoneCheck->Bind(wxEVT_CHECKBOX, &BorderEditorPanel::OnGroundDraftChange, this);
	borderOptionsRow->Add(m_includeToNoneCheck, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

	m_includeInnerCheck = new wxCheckBox(borderOptionsBoxSizer->GetStaticBox(), wxID_ANY, "Inner Border");
	m_includeInnerCheck->Bind(wxEVT_CHECKBOX, &BorderEditorPanel::OnGroundDraftChange, this);
	borderOptionsRow->Add(m_includeInnerCheck, 0, wxALIGN_CENTER_VERTICAL);

	borderOptionsBoxSizer->Add(borderOptionsRow, 0, wxEXPAND | wxALL, 5);
	groundRightSizer->Add(borderOptionsBoxSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

	groundContentSizer->Add(groundRightSizer, 1, wxEXPAND | wxLEFT, 5);
	groundSizer->Add(groundContentSizer, 1, wxEXPAND | wxALL, 5);

	// --- Footer: Right-aligned buttons ---
	wxBoxSizer* groundButtonSizer = new wxBoxSizer(wxHORIZONTAL);
	groundButtonSizer->AddStretchSpacer(1);

	wxButton* groundNewButton = new wxButton(m_groundPanel, wxID_CLEAR, "New Ground");
	groundNewButton->SetToolTip("Start creating a new ground brush (clears current form)");
	groundButtonSizer->Add(groundNewButton, 0, wxRIGHT, 5);

	groundButtonSizer->Add(new wxButton(m_groundPanel, wxID_SAVE, "Save Changes"), 0, wxRIGHT, 5);
	groundButtonSizer->Add(new wxButton(m_groundPanel, wxID_CLOSE, "Close"), 0);

	groundSizer->Add(groundButtonSizer, 0, wxEXPAND | wxALL, 5);

	m_groundPanel->SetSizer(groundSizer);

	// Add pages to simplebook (no visible tabs)
	m_notebook->AddPage(m_borderPanel, "Border");
	m_notebook->AddPage(m_groundPanel, "Ground");

	// Notebook pages "Border Loop" and "Ground Brush" removed as they were duplicate pointers

	// ========== WALL TAB ==========
	m_wallPanel = new wxPanel(m_notebook);
	wxBoxSizer* wallSizer = new wxBoxSizer(wxVERTICAL);

	// --- Main Body: Palette (Left) + Editor (Right) ---
	wxBoxSizer* wallContentSizer = new wxBoxSizer(wxHORIZONTAL);

	// === Left Column: Raw Item Palette ===
	// === Left Column: Raw Item Palette ===
	wxBoxSizer* wallLeftSizer = new wxBoxSizer(wxVERTICAL);

	// Tileset selector row (ComboBox + Filter button)
	wxBoxSizer* wallTilesetRowSizer = new wxBoxSizer(wxHORIZONTAL);
	wallTilesetRowSizer->Add(new wxStaticText(m_wallPanel, wxID_ANY, "Source:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

	m_wallTilesetButton = new wxButton(m_wallPanel, wxID_ANY, "Select Tileset ▼", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
	m_wallTilesetButton->SetToolTip("Select Tileset");
	m_wallTilesetButton->Bind(wxEVT_BUTTON, &BorderEditorPanel::OnWallTilesetButtonClick, this);
	wallTilesetRowSizer->Add(m_wallTilesetButton, 1, wxEXPAND | wxRIGHT, 2);

	// Filter button
	wxBitmapButton* wallFilterBtn = new wxBitmapButton(m_wallPanel, wxID_ANY, wxArtProvider::GetBitmap(wxART_CUT, wxART_BUTTON, wxSize(16, 16)));
	wallFilterBtn->SetToolTip("Filter visible tilesets");
	wallFilterBtn->Bind(wxEVT_BUTTON, &BorderEditorPanel::OnOpenTilesetFilter, this);
	wallTilesetRowSizer->Add(wallFilterBtn, 0, wxALIGN_CENTER_VERTICAL);

	wallLeftSizer->Add(wallTilesetRowSizer, 0, wxEXPAND | wxBOTTOM, 5);

	m_wallPalette = new SimpleRawPalettePanel(m_wallPanel);
	m_wallPalette->SetMinSize(wxSize(220, 300));
	m_wallPalette->Bind(wxEVT_COMMAND_LISTBOX_SELECTED, &BorderEditorPanel::OnWallPaletteSelect, this);
	wallLeftSizer->Add(m_wallPalette, 1, wxEXPAND | wxALL, 5);

	wallContentSizer->Add(wallLeftSizer, 0, wxEXPAND);

	// === Right Column: Editor Controls ===
	wxBoxSizer* wallRightSizer = new wxBoxSizer(wxVERTICAL);

	// Wall Structure inside StaticBox
	wxStaticBoxSizer* structureSizer = new wxStaticBoxSizer(wxVERTICAL, m_wallPanel, "Wall Structure");

	// Logic Type Selector (Visual Grid)
	wxBoxSizer* wallTypeSizer = new wxBoxSizer(wxVERTICAL);
	// wallTypeSizer->Add(new wxStaticText(structureSizer->GetStaticBox(), wxID_ANY, "Select Type to Edit:"), 0, wxALIGN_LEFT | wxBOTTOM, 5); // Removed to save space

	m_wallGridPanel = new WallGridPanel(structureSizer->GetStaticBox(), wxID_ANY);
	m_wallGridPanel->SetMinSize(wxSize(280, 200)); // Increased height for 3x3 grid

	// Bind click event (reusing BUTTON_CLICKED)
	m_wallGridPanel->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &BorderEditorPanel::OnWallGridSelect, this);

	wallTypeSizer->Add(m_wallGridPanel, 0, wxEXPAND | wxBOTTOM, 5);
	structureSizer->Add(wallTypeSizer, 0, wxEXPAND | wxALL, 5);

	// List of items for this type (REMOVED per user request)
	// m_wallItemsList = new wxListBox(structureSizer->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxSize(-1, 80));
	// structureSizer->Add(m_wallItemsList, 1, wxEXPAND | wxLEFT | wxRIGHT, 5);
	m_wallItemsList = nullptr;

	// Add Item Controls row (REMOVED per user request)
	// wxBoxSizer* wallItemInputSizer = new wxBoxSizer(wxHORIZONTAL);

	// wallItemInputSizer->Add(new wxStaticText(structureSizer->GetStaticBox(), wxID_ANY, "Item ID:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	// m_wallItemIdCtrl = new wxSpinCtrl(structureSizer->GetStaticBox(), wxID_ANY, "0", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 65535);
	// wallItemInputSizer->Add(m_wallItemIdCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
	m_wallItemIdCtrl = nullptr;

	// wxButton* addWallItemBtn = new wxButton(structureSizer->GetStaticBox(), wxID_ANY, "Add", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	// addWallItemBtn->Bind(wxEVT_BUTTON, &BorderEditorPanel::OnAddWallItem, this);
	// wallItemInputSizer->Add(addWallItemBtn, 0, wxRIGHT, 5);

	// wxButton* remWallItemBtn = new wxButton(structureSizer->GetStaticBox(), wxID_ANY, "Remove", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	// remWallItemBtn->Bind(wxEVT_BUTTON, &BorderEditorPanel::OnRemoveWallItem, this);
	// wallItemInputSizer->Add(remWallItemBtn, 0);

	// structureSizer->Add(wallItemInputSizer, 0, wxEXPAND | wxALL, 5);
	wallRightSizer->Add(structureSizer, 1, wxEXPAND | wxALL, 5);

	// Preview inside StaticBox
	wxStaticBoxSizer* wallVisualSizer = new wxStaticBoxSizer(wxVERTICAL, m_wallPanel, "Preview");
	m_wallVisualPanel = new WallVisualPanel(wallVisualSizer->GetStaticBox());
	m_wallVisualPanel->SetMinSize(wxSize(150, 150));
	wallVisualSizer->Add(m_wallVisualPanel, 1, wxEXPAND | wxALL, 5);
	wallRightSizer->Add(wallVisualSizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

	wallContentSizer->Add(wallRightSizer, 1, wxEXPAND | wxLEFT, 5);
	wallSizer->Add(wallContentSizer, 1, wxEXPAND | wxALL, 5);

	// --- Footer: Right-aligned buttons ---
	wxBoxSizer* wallButtonSizer = new wxBoxSizer(wxHORIZONTAL);
	wallButtonSizer->AddStretchSpacer(1);

	wxButton* wallNewButton = new wxButton(m_wallPanel, wxID_ANY, "New Wall");
	wallNewButton->SetToolTip("Start creating a new wall brush (clears current form)");
	wallNewButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &ev) {
		DiscardCurrentDraft();
		ClearWallItems();
		if (m_groundNameCtrl) {
			m_groundNameCtrl->SetValue("");
		}
		if (m_borderBrowserList) {
			m_borderBrowserList->SetSelection(wxNOT_FOUND);
		}
	});
	wallButtonSizer->Add(wallNewButton, 0, wxRIGHT, 5);

	wxButton* saveWallButton = new wxButton(m_wallPanel, wxID_ANY, "Save Changes");
	saveWallButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &ev) { SaveWallBrush(); });
	wallButtonSizer->Add(saveWallButton, 0, wxRIGHT, 5);
	wallButtonSizer->Add(new wxButton(m_wallPanel, wxID_CLOSE, "Close"), 0);

	wallSizer->Add(wallButtonSizer, 0, wxEXPAND | wxALL, 5);

	m_wallPanel->SetSizer(wallSizer);
	m_notebook->AddPage(m_wallPanel, "Wall");

	// ========== HORIZONTAL LAYOUT: Content (Left) + Browser (Right) ==========
	wxBoxSizer* mainHorizSizer = new wxBoxSizer(wxHORIZONTAL);

	// LEFT: Add notebook (existing content)
	mainHorizSizer->Add(m_notebook, 1, wxEXPAND | wxALL, 5);

	// RIGHT: Browser Sidebar
	wxBoxSizer* browserSizer = new wxBoxSizer(wxVERTICAL);

	// Mode Switcher Buttons (replaces m_browserLabel)
	wxBoxSizer* modeSwitcherSizer = new wxBoxSizer(wxHORIZONTAL);

	m_groundModeBtn = new wxToggleButton(this, wxID_ANY, "Variation", wxDefaultPosition, wxDefaultSize);
	m_groundModeBtn->SetValue(true); // Default to Ground mode
	m_groundModeBtn->Bind(wxEVT_TOGGLEBUTTON, &BorderEditorPanel::OnModeSwitch, this);
	modeSwitcherSizer->Add(m_groundModeBtn, 1, wxEXPAND | wxRIGHT, 2);

	m_wallModeBtn = new wxToggleButton(this, wxID_ANY, "Structure", wxDefaultPosition, wxDefaultSize);
	m_wallModeBtn->Bind(wxEVT_TOGGLEBUTTON, &BorderEditorPanel::OnModeSwitch, this);
	modeSwitcherSizer->Add(m_wallModeBtn, 1, wxEXPAND | wxRIGHT, 2);

	m_borderModeBtn = new wxToggleButton(this, wxID_ANY, "Border", wxDefaultPosition, wxDefaultSize);
	m_borderModeBtn->SetValue(false);
	m_borderModeBtn->Bind(wxEVT_TOGGLEBUTTON, &BorderEditorPanel::OnModeSwitch, this);
	modeSwitcherSizer->Add(m_borderModeBtn, 1, wxEXPAND);

	browserSizer->Add(modeSwitcherSizer, 0, wxEXPAND | wxALL, 5);

	m_browserTilesetFilterButton = new wxButton(this, wxID_ANY, "Tileset: Select ▼", wxDefaultPosition, wxSize(280, -1), wxBU_LEFT);
	m_browserTilesetFilterButton->Bind(wxEVT_BUTTON, &BorderEditorPanel::OnBrowserTilesetFilterButtonClick, this);
	browserSizer->Add(m_browserTilesetFilterButton, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

	// Search control
	m_browserSearchCtrl = new wxSearchCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(280, -1));
	m_browserSearchCtrl->SetDescriptiveText("Search...");
	m_browserSearchCtrl->Bind(wxEVT_SEARCHCTRL_SEARCH_BTN, &BorderEditorPanel::OnBrowserSearch, this);
	m_browserSearchCtrl->Bind(wxEVT_TEXT, &BorderEditorPanel::OnBrowserSearch, this);
	browserSizer->Add(m_browserSearchCtrl, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);

	m_borderBrowserList = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(280, -1), 0, nullptr, wxLB_SINGLE | wxBORDER_NONE);
	m_borderBrowserList->Hide();
	m_borderBrowserList->Bind(wxEVT_LISTBOX, &BorderEditorPanel::OnBorderBrowserSelection, this);

	m_browserGrid = new BrushBrowserGridPanel(this, wxID_ANY);
	m_browserGrid->SetToolTip("Click an item to load it for editing");
	Bind(wxEVT_COMMAND_LISTBOX_SELECTED, &BorderEditorPanel::OnBrowserGridSelection, this, m_browserGrid->GetId());
	browserSizer->Add(m_browserGrid, 1, wxEXPAND | wxALL, 5);

	mainHorizSizer->Add(browserSizer, 0, wxEXPAND);

	// Add horizontal layout to top sizer
	topSizer->Add(mainHorizSizer, 1, wxEXPAND);

	SetSizer(topSizer);
	Layout();

	// Initialize Category Combo (Select Borders if available)
	// Initialize Category Button (Select Borders if available)
	if (!m_rawCategoryNames.IsEmpty()) {
		int defaults = m_rawCategoryNames.Index("Borders");
		if (defaults != wxNOT_FOUND) {
			m_rawCategorySelection = defaults;
		} else {
			m_rawCategorySelection = 0;
		}

		if (m_rawCategoryButton) {
			m_rawCategoryButton->SetLabel(m_rawCategoryNames[m_rawCategorySelection] + " \u25BC");
		}

		// Trigger load
		if (m_itemPalettePanel) {
			m_itemPalettePanel->LoadTileset(m_rawCategoryNames[m_rawCategorySelection]);
		}
	}

	// Initialize Tileset Choice (Ground Tab)
	LoadTilesets(); // Ensure m_tilesetListData is populated

	// Select first tileset if any
	if (!m_tilesetListData.IsEmpty()) {
		m_groundTilesetSelection = 0;
		if (m_groundTilesetButton) {
			m_groundTilesetButton->SetLabel(m_tilesetListData[0] + " \u25BC");
		}

		// Trigger load for ground
		if (m_groundPalette) {
			m_groundPalette->LoadTileset(m_tilesetListData[0], FILTER_GROUND);
		}
	}

	{
		const auto options = GetBrowserTilesetOptions();
		if (!options.empty() && m_browserTilesetFilter.IsEmpty()) {
			m_browserTilesetFilter = options.front().second;
			if (m_browserTilesetFilterButton) {
				m_browserTilesetFilterButton->SetLabel("Tileset: " + options.front().first + " ▼");
			}
		}
	}

	FilterBrowserList(m_browserSearchCtrl ? m_browserSearchCtrl->GetValue() : wxString());
}

void BorderEditorPanel::OnRawCategoryButtonClick(wxCommandEvent &event) {
	if (m_rawCategoryNames.IsEmpty()) {
		return;
	}

	wxMenu menu;
	for (size_t i = 0; i < m_rawCategoryNames.GetCount(); ++i) {
		menu.AppendCheckItem(1000 + i, m_rawCategoryNames[i]);
		if ((int)i == m_rawCategorySelection) {
			menu.Check(1000 + i, true);
		}
	}

	menu.Bind(wxEVT_MENU, &BorderEditorPanel::OnRawCategoryMenuSelect, this);
	const wxSize btnSize = m_rawCategoryButton->GetSize();
	m_rawCategoryButton->PopupMenu(&menu, 0, btnSize.GetHeight());
}

void BorderEditorPanel::OnRawCategoryMenuSelect(wxCommandEvent &event) {
	int id = event.GetId() - 1000;
	if (id < 0 || id >= (int)m_rawCategoryNames.GetCount()) {
		return;
	}

	m_rawCategorySelection = id;
	if (m_rawCategoryButton) {
		m_rawCategoryButton->SetLabel(m_rawCategoryNames[id] + " \u25BC");
	}

	if (m_itemPalettePanel) {
		m_itemPalettePanel->LoadTileset(m_rawCategoryNames[id]);
	}

	FilterBrowserList(m_browserSearchCtrl ? m_browserSearchCtrl->GetValue() : wxString());
}

void BorderEditorPanel::OnGroundTilesetButtonClick(wxCommandEvent &event) {
	if (m_tilesetListData.IsEmpty()) {
		return;
	}

	wxMenu menu;
	for (size_t i = 0; i < m_tilesetListData.GetCount(); ++i) {
		menu.AppendCheckItem(2000 + i, m_tilesetListData[i]);
		if ((int)i == m_groundTilesetSelection) {
			menu.Check(2000 + i, true);
		}
	}

	menu.Bind(wxEVT_MENU, &BorderEditorPanel::OnGroundTilesetMenuSelect, this);
	const wxSize btnSize = m_groundTilesetButton->GetSize();
	m_groundTilesetButton->PopupMenu(&menu, 0, btnSize.GetHeight());
}

void BorderEditorPanel::OnGroundTilesetMenuSelect(wxCommandEvent &event) {
	int id = event.GetId() - 2000;
	if (id < 0 || id >= (int)m_tilesetListData.GetCount()) {
		return;
	}

	m_groundTilesetSelection = id;
	if (m_groundTilesetButton) {
		m_groundTilesetButton->SetLabel(m_tilesetListData[id] + " \u25BC");
	}

	if (m_groundPalette) {
		m_groundPalette->LoadTileset(m_tilesetListData[id], FILTER_GROUND);
	}

	FilterBrowserList(m_browserSearchCtrl ? m_browserSearchCtrl->GetValue() : wxString());
}

void BorderEditorPanel::OnWallTilesetButtonClick(wxCommandEvent &event) {
	if (m_tilesetListData.IsEmpty()) {
		return;
	}

	wxMenu menu;
	for (size_t i = 0; i < m_tilesetListData.GetCount(); ++i) {
		menu.AppendCheckItem(3000 + i, m_tilesetListData[i]);
		if ((int)i == m_wallTilesetSelection) {
			menu.Check(3000 + i, true);
		}
	}

	menu.Bind(wxEVT_MENU, &BorderEditorPanel::OnWallTilesetMenuSelect, this);
	const wxSize btnSize = m_wallTilesetButton->GetSize();
	m_wallTilesetButton->PopupMenu(&menu, 0, btnSize.GetHeight());
}

void BorderEditorPanel::OnWallTilesetMenuSelect(wxCommandEvent &event) {
	int id = event.GetId() - 3000;
	if (id < 0 || id >= (int)m_tilesetListData.GetCount()) {
		return;
	}

	m_wallTilesetSelection = id;
	if (m_wallTilesetButton) {
		m_wallTilesetButton->SetLabel(m_tilesetListData[id] + " \u25BC");
	}

	if (m_wallPalette) {
		m_wallPalette->LoadTileset(m_tilesetListData[id], FILTER_WALL);
	}

	FilterBrowserList(m_browserSearchCtrl ? m_browserSearchCtrl->GetValue() : wxString());
}

void BorderEditorPanel::OnBorderAlignmentButtonClick(wxCommandEvent &event) {
	wxMenu menu;
	menu.AppendCheckItem(4000, "Outer");
	menu.AppendCheckItem(4001, "Inner");

	menu.Check(4000 + m_borderAlignmentSelection, true);

	menu.Bind(wxEVT_MENU, &BorderEditorPanel::OnBorderAlignmentMenuSelect, this);
	const wxSize btnSize = m_borderAlignmentButton->GetSize();
	m_borderAlignmentButton->PopupMenu(&menu, 0, btnSize.GetHeight());
}

void BorderEditorPanel::OnBorderAlignmentMenuSelect(wxCommandEvent &event) {
	int id = event.GetId() - 4000;
	if (id < 0 || id > 1) {
		return;
	}

	m_borderAlignmentSelection = id;
	if (m_borderAlignmentButton) {
		m_borderAlignmentButton->SetLabel((id == 0 ? "Outer" : "Inner") + wxString(" \u25BC"));
	}

	SaveCurrentDraft(true);
	FilterBrowserList(m_browserSearchCtrl ? m_browserSearchCtrl->GetValue() : wxString());
}

void BorderEditorPanel::OnWallPaletteSelect(wxCommandEvent &event) {
	int itemId = event.GetInt();
	if (itemId > 0) {
		m_currentWallItemId = itemId;
		// if (m_wallItemIdCtrl) m_wallItemIdCtrl->SetValue(itemId); // Ctrl removed
	}

	FilterBrowserList(m_browserSearchCtrl ? m_browserSearchCtrl->GetValue() : wxString());
}

void BorderEditorPanel::OnWallGridSelect(wxCommandEvent &event) {
	std::string type = event.GetString().ToStdString();
	if (type.empty()) {
		return;
	}

	bool changed = false;

	// Select the type
	m_wallGridPanel->SetSelectedType(type);

	// If we have a valid item selected in palette, add it to this type!
	if (m_currentWallItemId > 0) {
		WallTypeData &data = m_wallTypes[type];
		data.typeName = type;

		// Check if item already exists to avoid duplicates (optional, but good UX)
		bool exists = false;
		for (const auto &item : data.items) {
			if (item.itemId == m_currentWallItemId) {
				exists = true;
				break;
			}
		}

		if (!exists) {
			data.items.push_back(GroundItem(m_currentWallItemId, 10)); // Default chance
			changed = true;

			// Refresh previews
			m_wallVisualPanel->SetWallItems(m_wallTypes);
			m_wallGridPanel->SetWallTypes(m_wallTypes); // Updates the grid icons
		}
	}

	UpdateWallItemsList(); // Keeps internal logic sync if needed (pointer null checked inside)
	SaveCurrentDraft(changed);
}

void BorderEditorPanel::OnOpenTilesetFilter(wxCommandEvent &event) {
	// Get all available tilesets
	wxArrayString allTilesets;
	for (const auto &pair : g_materials.tilesets) {
		if (pair.second) {
			allTilesets.Add(wxString(pair.first));
		}
	}
	allTilesets.Sort();

	// Get the appropriate whitelist for the current mode
	std::set<wxString> &currentWhitelist = (m_activeTab == 0) ? m_borderEnabledTilesets : (m_activeTab == 1) ? m_groundEnabledTilesets
																											 : m_wallEnabledTilesets;

	wxString modeLabel = (m_activeTab == 0) ? "Border" : (m_activeTab == 1) ? "Ground"
																			: "Wall";

	TilesetFilterDialog dlg(this, allTilesets, currentWhitelist, modeLabel);
	if (dlg.ShowModal() == wxID_OK) {
		currentWhitelist = dlg.GetEnabledTilesets();
		SaveFilterConfig(); // Save to file immediately
		LoadTilesets(); // Refresh the ComboBox with filtered list
	}

	FilterBrowserList(m_browserSearchCtrl ? m_browserSearchCtrl->GetValue() : wxString());
}

// ============================================================================
// TilesetFilterDialog Implementation

TilesetFilterDialog::TilesetFilterDialog(wxWindow* parent, const wxArrayString &allTilesets, const std::set<wxString> &enabledTilesets, const wxString &modeLabel) : wxDialog(parent, wxID_ANY, "Filter Tilesets - " + modeLabel, wxDefaultPosition, wxSize(350, 450), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
																																									 m_allTilesets(allTilesets) {
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	// Instructions
	mainSizer->Add(new wxStaticText(this, wxID_ANY, "Select tilesets to show (click anywhere on row):"), 0, wxALL, 10);

	// Checklist
	m_tilesetList = new wxCheckListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, allTilesets);

	// Check only items that ARE in the enabled set (whitelist)
	// If whitelist is empty, all stay unchecked (default empty)
	for (unsigned int i = 0; i < allTilesets.GetCount(); ++i) {
		bool isEnabled = (enabledTilesets.find(allTilesets[i]) != enabledTilesets.end());
		m_tilesetList->Check(i, isEnabled);
	}

	// Bind click-anywhere toggle (clicking the label toggles the checkbox)
	// REMOVED: Triggers on auto-selection (focus) in GTK, causing first item to be checked automatically
	// m_tilesetList->Bind(wxEVT_LISTBOX, &TilesetFilterDialog::OnListItemClick, this);

	mainSizer->Add(m_tilesetList, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);

	// Select All / None buttons
	wxBoxSizer* selectBtnSizer = new wxBoxSizer(wxHORIZONTAL);
	wxButton* selectAllBtn = new wxButton(this, wxID_ANY, "Select All");
	wxButton* selectNoneBtn = new wxButton(this, wxID_ANY, "Select None");
	selectAllBtn->Bind(wxEVT_BUTTON, &TilesetFilterDialog::OnSelectAll, this);
	selectNoneBtn->Bind(wxEVT_BUTTON, &TilesetFilterDialog::OnSelectNone, this);
	selectBtnSizer->Add(selectAllBtn, 0, wxRIGHT, 5);
	selectBtnSizer->Add(selectNoneBtn, 0);
	mainSizer->Add(selectBtnSizer, 0, wxALL, 10);

	// Save / Cancel buttons
	wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
	btnSizer->AddStretchSpacer(1);
	wxButton* saveBtn = new wxButton(this, wxID_OK, "Save");
	wxButton* cancelBtn = new wxButton(this, wxID_CANCEL, "Cancel");
	btnSizer->Add(saveBtn, 0, wxRIGHT, 5);
	btnSizer->Add(cancelBtn, 0);
	mainSizer->Add(btnSizer, 0, wxEXPAND | wxALL, 10);

	SetSizer(mainSizer);
	CenterOnParent();
}

std::set<wxString> TilesetFilterDialog::GetEnabledTilesets() const {
	std::set<wxString> enabled;
	for (unsigned int i = 0; i < m_allTilesets.GetCount(); ++i) {
		if (m_tilesetList->IsChecked(i)) {
			enabled.insert(m_allTilesets[i]);
		}
	}
	return enabled;
}

void TilesetFilterDialog::OnListItemClick(wxCommandEvent &event) {
	// Toggle checkbox when clicking anywhere on the row
	int sel = m_tilesetList->GetSelection();
	if (sel != wxNOT_FOUND) {
		m_tilesetList->Check(sel, !m_tilesetList->IsChecked(sel));
	}
}

void TilesetFilterDialog::OnSelectAll(wxCommandEvent &event) {
	for (unsigned int i = 0; i < m_tilesetList->GetCount(); ++i) {
		m_tilesetList->Check(i, true);
	}
}

void TilesetFilterDialog::OnSelectNone(wxCommandEvent &event) {
	for (unsigned int i = 0; i < m_tilesetList->GetCount(); ++i) {
		m_tilesetList->Check(i, false);
	}
}

void BorderEditorPanel::LoadWallBrushByName(const wxString &name) {
	if (name.IsEmpty()) {
		ClearWallItems();
		return;
	}

	// Find the walls.xml file
	wxString dataDir = g_gui.GetDataDirectory();

	wxString wallsFile = dataDir + wxFileName::GetPathSeparator() + "materials" + wxFileName::GetPathSeparator() + "brushs" + wxFileName::GetPathSeparator() + "walls.xml";

	// Load XML
	pugi::xml_document doc;
	if (!doc.load_file(nstr(wallsFile).c_str())) {
		return;
	}

	pugi::xml_node materials = doc.child("materials");
	if (!materials) {
		return;
	}

	// Find the brush
	for (pugi::xml_node brushNode = materials.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
		pugi::xml_attribute nameAttr = brushNode.attribute("name");
		if (nameAttr && wxString(nameAttr.as_string()) == name) {
			ClearWallItems(false);
			if (m_groundNameCtrl) {
				m_groundNameCtrl->SetValue(name);
			}
			m_wallTypes.clear();

			// Iterate over children to find <wall> nodes
			for (pugi::xml_node wallNode = brushNode.child("wall"); wallNode; wallNode = wallNode.next_sibling("wall")) {
				pugi::xml_attribute typeAttr = wallNode.attribute("type");
				if (typeAttr) {
					std::string typeName = typeAttr.as_string();
					WallTypeData typeData;
					typeData.typeName = typeName;

					// Helper to parse items from a node
					auto parseItems = [&](pugi::xml_node parentNode) {
						for (pugi::xml_node itemNode = parentNode.child("item"); itemNode; itemNode = itemNode.next_sibling("item")) {
							pugi::xml_attribute idAttr = itemNode.attribute("id");
							pugi::xml_attribute chanceAttr = itemNode.attribute("chance");

							if (idAttr) {
								uint16_t itemId = idAttr.as_uint();
								int chance = chanceAttr ? chanceAttr.as_int() : 10;
								typeData.items.push_back(GroundItem(itemId, chance));
							}
						}
					};

					// Parse direct items
					parseItems(wallNode);

					// Parse items inside <alternate>
					for (pugi::xml_node altNode = wallNode.child("alternate"); altNode; altNode = altNode.next_sibling("alternate")) {
						parseItems(altNode);
					}

					m_wallTypes[typeName] = typeData;
				}
			}

			// Update UI to reflect loaded data
			m_wallGridPanel->SetWallTypes(m_wallTypes);
			m_wallGridPanel->SetSelectedType("vertical"); // Default
			UpdateWallItemsList();
			m_wallVisualPanel->SetWallItems(m_wallTypes);

			break;
		}
	}
}

void BorderEditorPanel::SaveWallBrush() {
	wxString name = m_groundNameCtrl ? m_groundNameCtrl->GetValue().Trim() : wxString();

	if (name.IsEmpty()) {
		wxMessageBox("Please enter a name for the wall brush.", "Error", wxICON_ERROR);
		return;
	}

	int preSave = wxMessageBox(
		"This action will save the wall brush and reload data files (F5). Continue?",
		"Confirm Save",
		wxYES_NO | wxICON_QUESTION | wxSTAY_ON_TOP,
		this
	);
	if (preSave != wxYES) {
		return;
	}

	uint16_t mainTileId = m_selectedBrowserTileId;
	if (mainTileId == 0) {
		for (const auto &pair : m_wallTypes) {
			const WallTypeData &typeData = pair.second;
			if (!typeData.items.empty()) {
				mainTileId = typeData.items.front().itemId;
				break;
			}
		}
	}

	const int serverLookId = static_cast<int>(mainTileId);

	// Find walls.xml
	wxString dataDir = g_gui.GetDataDirectory();

	wxString wallsFile = dataDir + wxFileName::GetPathSeparator() + "materials" + wxFileName::GetPathSeparator() + "brushs" + wxFileName::GetPathSeparator() + "walls.xml";

	pugi::xml_document doc;
	bool loaded = doc.load_file(nstr(wallsFile).c_str());
	if (!loaded) {
		// Create new if not exists?
		// Usually we expect it to exist.
		// If not, maybe create simple structure
		pugi::xml_node root = doc.append_child("materials");
	}

	pugi::xml_node materials = doc.child("materials");
	if (!materials) {
		materials = doc.append_child("materials");
	}

	// Check if exists and remove OLD entry to replace it completely
	pugi::xml_node existingNode;
	for (pugi::xml_node node = materials.child("brush"); node; node = node.next_sibling("brush")) {
		if (wxString(node.attribute("name").as_string()) == name) {
			existingNode = node;
			break;
		}
	}

	if (existingNode) {
		if (wxMessageBox("Wall brush '" + name + "' already exists. Overwrite?", "Confirm Overwrite", wxYES_NO | wxICON_QUESTION) != wxYES) {
			return;
		}
		materials.remove_child(existingNode);
	}

	// Create NEW brush node
	pugi::xml_node brushNode = materials.append_child("brush");
	brushNode.append_attribute("name").set_value(nstr(name).c_str());
	brushNode.append_attribute("type").set_value("wall");
	brushNode.append_attribute("server_lookid").set_value(serverLookId);

	// Iterate over our map and save <wall> nodes
	for (const auto &pair : m_wallTypes) {
		const WallTypeData &typeData = pair.second;
		if (typeData.items.empty()) {
			continue; // Skip empty types
		}

		pugi::xml_node wallNode = brushNode.append_child("wall");
		wallNode.append_attribute("type").set_value(typeData.typeName.c_str());

		for (const GroundItem &item : typeData.items) {
			pugi::xml_node itemNode = wallNode.append_child("item");
			itemNode.append_attribute("id").set_value(item.itemId);
			itemNode.append_attribute("chance").set_value(item.chance);
		}
	}

	wxString xmlText;
	if (!ReadAllText(wallsFile, xmlText) || xmlText.IsEmpty()) {
		xmlText = "<materials>\n</materials>\n";
	}

	const wxString nl = DetectNewline(xmlText);
	const wxString baseIndent = DetectChildIndent(xmlText, "brush");
	const wxString newBlock = PrintNodeIndented(brushNode, baseIndent, nl);
	const wxString attrNeedle = "name=\"" + name + "\"";

	wxString oldText = xmlText;
	if (!ReplaceOrInsertChildSimple(xmlText, "brush", attrNeedle, newBlock, false)) {
		wxMessageBox("Invalid walls.xml file: missing '</materials>'", "Error", wxICON_ERROR);
		return;
	}

	if (xmlText != oldText && !WriteAllText(wallsFile, xmlText)) {
		wxMessageBox("Failed to save walls.xml", "Error", wxICON_ERROR);
		return;
	}

	DiscardDraft(2, mainTileId);

	TriggerReloadDataFiles();
	ReloadCurrentBrushEditorXml(wxString::Format("%u", (unsigned)mainTileId));

	wxMessageBox("Wall brush saved successfully.", "Success", wxOK | wxICON_INFORMATION | wxSTAY_ON_TOP, this);
}

void BorderEditorPanel::LoadExistingWallBrushes() {
	// Legacy method - wall brushes are now populated via PopulateWallList()
	// Called from constructor for compatibility, actual population happens on tab change
}

bool BorderEditorPanel::ValidateWallBrush() {
	return true;
}

// Helper to get current selected type string

void BorderEditorPanel::ClearWallItems(bool clearBrowser) {
	if (m_wallVisualPanel) {
		m_wallVisualPanel->Clear();
	}

	if (m_wallGridPanel) {
		m_wallGridPanel->SetWallTypes({});
		m_wallGridPanel->SetSelectedType(std::string());
	}

	m_wallTypes.clear();
	m_currentWallItemId = 0;

	if (m_groundNameCtrl) {
		m_groundNameCtrl->SetValue("");
	}

	if (clearBrowser && m_borderBrowserList) {
		m_borderBrowserList->SetSelection(wxNOT_FOUND);
		m_selectedBrowserTileId = 0;
	}
}

void BorderEditorPanel::UpdateWallItemsList() {
	if (!m_wallItemsList || !m_wallGridPanel) {
		return;
	}

	m_wallItemsList->Clear();
	std::string type = m_wallGridPanel->GetSelectedType();
	if (type.empty()) {
		type = "vertical"; // Fallback
	}

	if (m_wallTypes.find(type) != m_wallTypes.end()) {
		const WallTypeData &data = m_wallTypes[type];
		for (const GroundItem &item : data.items) {
			m_wallItemsList->Append(wxString::Format("Item ID: %d", item.itemId));
		}
	}
}

void BorderEditorPanel::OnWallBrowse(wxCommandEvent &event) {
	// Reuse Ground Browse Logic or create new dialog
	// implementation pending... using manual ID for now
}

void BorderEditorPanel::OnAddWallItem(wxCommandEvent &event) {
	if (!m_wallItemIdCtrl) {
		return;
	}
	int id = m_wallItemIdCtrl->GetValue();
	if (id <= 0) {
		return;
	}

	std::string type = m_wallGridPanel->GetSelectedType();
	if (type.empty()) {
		type = "vertical";
	}

	WallTypeData &data = m_wallTypes[type];
	data.typeName = type;
	data.items.push_back(GroundItem(id, 10)); // Default chance 10

	m_wallItemsList->Append(wxString::Format("Item ID: %d", id));
	m_wallVisualPanel->SetWallItems(m_wallTypes); // Update preview
	m_wallGridPanel->SetWallTypes(m_wallTypes); // Update visual selector
}

void BorderEditorPanel::OnRemoveWallItem(wxCommandEvent &event) {
	if (!m_wallItemsList) {
		return;
	}
	int sel = m_wallItemsList->GetSelection();
	if (sel == wxNOT_FOUND) {
		return;
	}

	std::string type = m_wallGridPanel->GetSelectedType();
	if (type.empty()) {
		type = "vertical";
	}

	if (m_wallTypes.find(type) != m_wallTypes.end()) {
		WallTypeData &data = m_wallTypes[type];
		if (sel >= 0 && sel < (int)data.items.size()) {
			data.items.erase(data.items.begin() + sel);
			UpdateWallItemsList();
			m_wallVisualPanel->SetWallItems(m_wallTypes);
			m_wallGridPanel->SetWallTypes(m_wallTypes);
		}
	}
}

void BorderEditorPanel::LoadExistingBorders() {
	// Clear the browser list
	if (!m_borderBrowserList) {
		return;
	}
	m_borderBrowserList->Clear();

	// Find borders.xml file - actual borders are in the borders/ subfolder
	wxString dataDir = g_gui.GetDataDirectory();
	wxString bordersFile = dataDir + wxFileName::GetPathSeparator() + "materials" + wxFileName::GetPathSeparator() + "borders" + wxFileName::GetPathSeparator() + "borders.xml";

	if (!wxFileExists(bordersFile)) {
		return; // Silently fail - no borders yet
	}

	// Load XML
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(nstr(bordersFile).c_str());
	if (!result) {
		return;
	}

	pugi::xml_node materials = doc.child("materials");
	if (!materials) {
		return;
	}

	int maxId = 0;

	// Parse all borders
	for (pugi::xml_node borderNode = materials.child("border"); borderNode; borderNode = borderNode.next_sibling("border")) {
		pugi::xml_attribute idAttr = borderNode.attribute("id");
		if (!idAttr) {
			continue;
		}

		int id = idAttr.as_int();
		if (id > maxId) {
			maxId = id;
		}

		// Get name attribute
		wxString name;
		pugi::xml_attribute nameAttr = borderNode.attribute("name");
		if (nameAttr && wxString(nameAttr.as_string()).Trim().Length() > 0) {
			name = wxString(nameAttr.as_string());
		} else {
			name = "(no name)";
		}

		// Add to browser list: "ID - Name"
		wxString label = wxString::Format("%d - %s", id, name);
		m_borderBrowserList->Append(label, new wxStringClientData(wxString::Format("%d", id)));
	}

	// Set next border ID
	m_nextBorderId = maxId + 1;
	m_currentBorderId = m_nextBorderId;
}

// Helper method to load a border by ID
void BorderEditorPanel::LoadBorderById(int borderId) {
	wxString dataDir = g_gui.GetDataDirectory();
	wxString bordersFile = dataDir + wxFileName::GetPathSeparator() + "materials" + wxFileName::GetPathSeparator() + "borders" + wxFileName::GetPathSeparator() + "borders.xml";

	if (!wxFileExists(bordersFile)) {
		return;
	}

	pugi::xml_document doc;
	if (!doc.load_file(nstr(bordersFile).c_str())) {
		return;
	}

	ClearItems(false); // Don't clear browser selection logic

	pugi::xml_node materials = doc.child("materials");
	for (pugi::xml_node borderNode = materials.child("border"); borderNode; borderNode = borderNode.next_sibling("border")) {
		pugi::xml_attribute idAttr = borderNode.attribute("id");
		if (!idAttr || idAttr.as_int() != borderId) {
			continue;
		}

		m_currentBorderId = borderId; // Store the loaded ID

		// Name
		pugi::xml_attribute nameAttr = borderNode.attribute("name");
		m_loadedBorderName = nameAttr ? wxString(nameAttr.as_string()) : "";

		// Type
		pugi::xml_attribute typeAttr = borderNode.attribute("type");
		m_isOptionalCheck->SetValue(typeAttr && std::string(typeAttr.as_string()) == "optional");

		// Ground
		pugi::xml_attribute groundAttr = borderNode.attribute("ground");
		m_isGroundCheck->SetValue(groundAttr && groundAttr.as_bool());

		// Group (Checkbox now)
		pugi::xml_attribute groupAttr = borderNode.attribute("group");
		m_groupCheck->SetValue(groupAttr ? groupAttr.as_int() > 0 : false);

		// Border items
		for (pugi::xml_node itemNode = borderNode.child("borderitem"); itemNode; itemNode = itemNode.next_sibling("borderitem")) {
			pugi::xml_attribute edgeAttr = itemNode.attribute("edge");
			pugi::xml_attribute itemAttr = itemNode.attribute("item");
			if (!edgeAttr || !itemAttr) {
				continue;
			}

			BorderEdgePosition pos = edgeStringToPosition(edgeAttr.as_string());
			uint16_t itemId = itemAttr.as_uint();
			if (pos != EDGE_NONE && itemId > 0) {
				m_borderItems.push_back(BorderItem(pos, itemId));
				m_gridPanel->SetItemId(pos, itemId);
			}
		}
		break;
	}
	UpdatePreview();
}

// Browser sidebar selection handler - dispatches based on active tab
void BorderEditorPanel::OnBorderBrowserSelection(wxCommandEvent &event) {
	SaveCurrentDraft(false);

	if (!m_borderBrowserList) {
		return;
	}

	int selection = m_borderBrowserList->GetSelection();
	if (selection == wxNOT_FOUND) {
		return;
	}

	wxStringClientData* data = static_cast<wxStringClientData*>(m_borderBrowserList->GetClientObject(selection));
	if (!data) {
		return;
	}

	const uint16_t tileId = static_cast<uint16_t>(wxAtoi(data->GetData()));
	if (tileId == 0) {
		return;
	}

	m_selectedBrowserTileId = tileId;

	switch (m_activeTab) {
		case 0:
			LoadBorderByMainTileId(tileId);
			break;
		case 1:
			LoadGroundBrushByMainTileId(tileId);
			break;
		case 2:
			LoadWallBrushByMainTileId(tileId);
			break;
	}
}

void BorderEditorPanel::OnBrowserGridSelection(wxCommandEvent &event) {
	SaveCurrentDraft(false);

	const wxString key = event.GetString();
	if (key.IsEmpty()) {
		return;
	}

	RestoreBrowserSelection(key);

	const uint16_t tileId = static_cast<uint16_t>(wxAtoi(key));
	if (tileId == 0) {
		return;
	}

	m_selectedBrowserTileId = tileId;

	switch (m_activeTab) {
		case 0:
			LoadBorderByMainTileId(tileId);
			break;
		case 1:
			LoadGroundBrushByMainTileId(tileId);
			break;
		case 2:
			LoadWallBrushByMainTileId(tileId);
			break;
	}
}

void BorderEditorPanel::OnLoadBorder(wxCommandEvent &event) {
	// Old combo box handler - now unused as browser list handles loading
}

void BorderEditorPanel::OnItemIdChanged(wxCommandEvent &event) {
	// This event handler would update the display when an item ID is entered manually
	// but we're handling this directly in OnAddItem instead
}

void BorderEditorPanel::OnBrowse(wxCommandEvent &event) {
	// Open the Find Item dialog instead
	FindItemDialog dialog(this, "Select Border Item");

	if (dialog.ShowModal() == wxID_OK) {
		// Get the selected item ID
		uint16_t itemId = dialog.getResultID();

		// Find the item ID spin control
		// This logic is now obsolete as m_itemIdCtrl is removed.
		// The item ID should be set in the context of the current operation (e.g., OnPositionSelected).
		// For now, this function might need a different approach or be removed if no longer needed.
		// Keeping it as is for now, assuming m_itemIdCtrl might be re-introduced or handled differently.
		wxSpinCtrl* itemIdCtrl = nullptr;
		wxWindowList &children = GetChildren();
		for (wxWindowList::iterator it = children.begin(); it != children.end(); ++it) {
			wxSpinCtrl* spinCtrl = dynamic_cast<wxSpinCtrl*>(*it);
			if (spinCtrl /*&& spinCtrl != m_idCtrl*/) { // m_idCtrl is removed
				itemIdCtrl = spinCtrl;
				break;
			}
		}

		if (itemIdCtrl && itemId > 0) {
			itemIdCtrl->SetValue(itemId);
		}
	}
}

void BorderEditorPanel::OnPositionSelected(wxCommandEvent &event) {
	// Get the position from the event
	BorderEdgePosition pos = static_cast<BorderEdgePosition>(event.GetInt());
	wxLogDebug("OnPositionSelected: Position %s selected", wxstr(edgePositionToString(pos)).c_str());

	// Get the item ID from the current brush
	Brush* currentBrush = g_gui.GetCurrentBrush();
	uint16_t itemId = 0;

	if (currentBrush) {
		wxLogDebug("OnPositionSelected: Using brush: %s", wxString(currentBrush->getName()).c_str());

		// Try to get the item ID directly - check if it's a RAW brush first
		if (currentBrush->isRaw()) {
			RAWBrush* rawBrush = currentBrush->asRaw();
			if (rawBrush) {
				itemId = rawBrush->getItemID();
				wxLogDebug("OnPositionSelected: Got item ID %d directly from RAW brush", itemId);
			}
		}

		// If we didn't get an ID from the RAW brush method, try the generic method
		if (itemId == 0) {
			itemId = GetItemIDFromBrush(currentBrush);
			wxLogDebug("OnPositionSelected: Got item ID %d from GetItemIDFromBrush", itemId);
		}
	} else {
		wxLogDebug("OnPositionSelected: No current brush selected");
	}

	// If no valid ID from brush, check the manual input control
	// m_itemIdCtrl is removed, so this block is now obsolete.
	// The user is expected to select an item brush.
	/*
	if (itemId == 0 && m_itemIdCtrl) {
		itemId = m_itemIdCtrl->GetValue();
		if (itemId > 0) {
			wxLogDebug("OnPositionSelected: Using item ID %d from manual control", itemId);
		}
	}
	*/

	if (itemId > 0) {
		// Update the item ID control - keeps the UI in sync with our selection
		// m_itemIdCtrl is removed, so this block is now obsolete.
		/*
		if (m_itemIdCtrl) {
			m_itemIdCtrl->SetValue(itemId);
		}
		*/

		// Add or update the border item
		bool updated = false;
		for (size_t i = 0; i < m_borderItems.size(); i++) {
			if (m_borderItems[i].position == pos) {
				m_borderItems[i].itemId = itemId;
				updated = true;
				wxLogDebug("OnPositionSelected: Updated existing border item at position %s", wxstr(edgePositionToString(pos)).c_str());

				break;
			}
		}

		if (!updated) {
			m_borderItems.push_back(BorderItem(pos, itemId));
			wxLogDebug("OnPositionSelected: Added new border item at position %s", wxstr(edgePositionToString(pos)).c_str());
		}

		// Update the grid panel
		m_gridPanel->SetItemId(pos, itemId);
		wxLogDebug("OnPositionSelected: Set grid panel item ID for position %s to %d", wxstr(edgePositionToString(pos)).c_str(), itemId);

		// Update the preview
		UpdatePreview();
		SaveCurrentDraft(true);

		// Log the addition
		wxLogDebug("Added border item at position %s with item ID %d", wxstr(edgePositionToString(pos)).c_str(), itemId);

	} else {
		// If we couldn't get an item ID from the brush, check if there's a value in the item ID control
		// m_itemIdCtrl is removed, so this block is now obsolete.
		/*
		itemId = m_itemIdCtrl->GetValue();

		if (itemId > 0) {
			// Use the value from the control to update/add the border item
			bool updated = false;
			for (size_t i = 0; i < m_borderItems.size(); i++) {
				if (m_borderItems[i].position == pos) {
					m_borderItems[i].itemId = itemId;
					updated = true;
					break;
				}
			}

			if (!updated) {
				m_borderItems.push_back(BorderItem(pos, itemId));
			}

			// Update the grid panel
			m_gridPanel->SetItemId(pos, itemId);

			// Update the preview
			UpdatePreview();

			wxLogDebug("Used item ID %d from control for position %s",
					   itemId, wxstr(edgePositionToString(pos)).c_str());

		} else {
		*/
		wxLogDebug("No valid item ID found from current brush: %s", currentBrush ? wxString(currentBrush->getName()).c_str() : wxT("NULL"));

		ShowErrorDialog("Could not get a valid item ID from the current brush. Please select an item brush or use the Browse button to select an item manually.", ErrorSeverity::Info);
		// }
	}
}

void BorderEditorPanel::OnAddItem(wxCommandEvent &event) {
	// This function is now obsolete as manual add buttons and m_itemIdCtrl are removed.
	// If it were to be used, it would need to get the item ID from the currently selected brush.
	ShowErrorDialog("This 'Add Item' functionality is no longer available. Please select an item brush and click on the grid to add items.", ErrorSeverity::Info);
	return;

	/*
	// Get the currently selected position in the grid panel
	static BorderEdgePosition lastSelectedPos = EDGE_NONE;
	BorderEdgePosition selectedPos = m_gridPanel->GetSelectedPosition();

	// If no position is currently selected, use the last selected position
	if (selectedPos == EDGE_NONE) {
		selectedPos = lastSelectedPos;
	}

	if (selectedPos == EDGE_NONE) {
		wxMessageBox("Please select a position on the grid first by clicking on it.", "Error", wxICON_ERROR);
		return;
	}

	// Save this position for future use
	lastSelectedPos = selectedPos;

	// Get the item ID from the control (now using the class member)
	uint16_t itemId = m_itemIdCtrl->GetValue();

	if (itemId == 0) {
		wxMessageBox("Please enter a valid item ID or use the Browse button.", "Error", wxICON_ERROR);
		return;
	}

	// Add or update the border item
	bool updated = false;
	for (size_t i = 0; i < m_borderItems.size(); i++) {
		if (m_borderItems[i].position == selectedPos) {
			m_borderItems[i].itemId = itemId;
			updated = true;
			break;
		}
	}

	if (!updated) {
		m_borderItems.push_back(BorderItem(selectedPos, itemId));
	}

	// Update the grid panel
	m_gridPanel->SetItemId(selectedPos, itemId);

	// Update the preview
	UpdatePreview();

	// Log the addition for debugging
	wxLogDebug("Added item ID %d at position %s via Add button",
			   itemId, wxstr(edgePositionToString(selectedPos)).c_str());
	*/
}

void BorderEditorPanel::OnClear(wxCommandEvent &event) {
	DiscardCurrentDraft();

	if (m_activeTab == 0) {
		// Border tab
		ClearItems();
	} else {
		// Ground tab
		ClearGroundItems();
	}
}

void BorderEditorPanel::ClearItems(bool clearBrowser) {
	m_borderItems.clear();
	m_gridPanel->Clear();
	m_previewPanel->Clear();

	// Reset controls to defaults
	m_currentBorderId = m_nextBorderId; // Use m_currentBorderId instead of m_idCtrl
	m_loadedBorderName.clear();
	if (m_isOptionalCheck) {
		m_isOptionalCheck->SetValue(false);
	}
	if (m_isGroundCheck) {
		m_isGroundCheck->SetValue(false);
	}
	if (m_groupCheck) {
		m_groupCheck->SetValue(false);
	}

	// Deselect browser list
	if (clearBrowser && m_borderBrowserList) {
		m_borderBrowserList->SetSelection(wxNOT_FOUND);
		m_selectedBrowserTileId = 0;
	}
}

void BorderEditorPanel::UpdatePreview() {
	m_previewPanel->SetBorderItems(m_borderItems);
	m_previewPanel->Refresh();
}

bool BorderEditorPanel::ValidateBorder() {
	if (m_borderItems.empty()) {
		ShowErrorDialog("The border must have at least one item.", ErrorSeverity::Error);
		return false;
	}

	// Check that there are no duplicate positions
	std::set<BorderEdgePosition> positions;
	for (const BorderItem &item : m_borderItems) {
		if (positions.find(item.position) != positions.end()) {
			ShowErrorDialog("The border contains duplicate positions.", ErrorSeverity::Error);
			return false;
		}
		positions.insert(item.position);
	}

	// Check for ID validity (using m_currentBorderId)
	int id = m_currentBorderId; // Use m_currentBorderId
	if (id <= 0) {
		ShowErrorDialog("Border ID must be greater than 0.", ErrorSeverity::Error);
		return false;
	}

	return true;
}

void BorderEditorPanel::SaveBorder() {
	if (!ValidateBorder()) {
		return;
	}

	int preSave = wxMessageBox(
		"This action will save the border and reload data files (F5). Continue?",
		"Confirm Save",
		wxYES_NO | wxICON_QUESTION | wxSTAY_ON_TOP,
		this
	);
	if (preSave != wxYES) {
		return;
	}

	// Get the border properties
	int id = m_currentBorderId; // Use m_currentBorderId
	wxString name = m_loadedBorderName;
	bool isOptional = m_isOptionalCheck->GetValue();
	bool isGround = m_isGroundCheck->GetValue();
	int group = m_groupCheck->GetValue() ? 1 : 0; // Read m_groupCheck as checkbox

	// Find the borders.xml file using the same version path conversion
	wxString dataDir = g_gui.GetDataDirectory();

	// actual borders are in the borders/ subfolder
	wxString bordersFile = dataDir + wxFileName::GetPathSeparator() + "materials" + wxFileName::GetPathSeparator() + "borders" + wxFileName::GetPathSeparator() + "borders.xml";

	if (!wxFileExists(bordersFile)) {
		ShowErrorDialog("Cannot find borders.xml file in the data directory.", ErrorSeverity::Error);
		return;
	}

	// Load the XML file
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(nstr(bordersFile).c_str());

	if (!result) {
		ShowErrorDialog("Failed to load borders.xml: " + wxString(result.description()), ErrorSeverity::Error);
		return;
	}

	pugi::xml_node materials = doc.child("materials");
	if (!materials) {
		ShowErrorDialog("Invalid borders.xml file: missing 'materials' node", ErrorSeverity::Error);
		return;
	}

	// Check if a border with this ID already exists
	bool borderExists = false;
	pugi::xml_node existingBorder;

	for (pugi::xml_node borderNode = materials.child("border"); borderNode; borderNode = borderNode.next_sibling("border")) {
		pugi::xml_attribute idAttr = borderNode.attribute("id");
		if (idAttr && idAttr.as_int() == id) {
			borderExists = true;
			existingBorder = borderNode;
			break;
		}
	}

	if (borderExists) {
		// Check if there's a comment node before the existing border - we want to remove legacy comments too
		pugi::xml_node commentNode = existingBorder.previous_sibling();
		bool hadComment = (commentNode && commentNode.type() == pugi::node_comment);

		// Ask for confirmation to overwrite
		if (wxMessageBox("A border with ID " + wxString::Format("%d", id) + " already exists. Do you want to overwrite it?", "Confirm Overwrite", wxYES_NO | wxICON_QUESTION) != wxYES) {
			return;
		}

		// Remove precedent comment if it exists (legacy cleanup)
		if (hadComment) {
			materials.remove_child(commentNode);
		}

		// Remove the existing border - this ensures all old PCDATA/children are gone
		materials.remove_child(existingBorder);
	}

	// Format the name - ensure we have a fallback
	if (name.IsEmpty()) {
		name = wxString::Format("Border %d", id);
	}

	// Create the new border node
	pugi::xml_node borderNode = materials.append_child("border");
	borderNode.append_attribute("id").set_value(id);

	// RULE: Always save name as attribute, NOT as PCDATA/Comment
	borderNode.append_attribute("name").set_value(nstr(name).c_str());

	if (isOptional) {
		borderNode.append_attribute("type").set_value("optional");
	}

	if (isGround) {
		borderNode.append_attribute("ground").set_value("true");
	}

	if (group > 0) {
		borderNode.append_attribute("group").set_value(group);
	}

	// Add all border items
	for (const BorderItem &item : m_borderItems) {
		pugi::xml_node itemNode = borderNode.append_child("borderitem");
		itemNode.append_attribute("edge").set_value(edgePositionToString(item.position).c_str());
		itemNode.append_attribute("item").set_value(item.itemId);
	}

	wxString xmlText;
	if (!ReadAllText(bordersFile, xmlText) || xmlText.IsEmpty()) {
		ShowErrorDialog("Failed to read borders.xml", ErrorSeverity::Error);
		return;
	}

	const wxString nl = DetectNewline(xmlText);
	const wxString baseIndent = DetectChildIndent(xmlText, "border");
	const wxString newBlock = PrintNodeIndented(borderNode, baseIndent, nl);
	const wxString attrNeedle = wxString::Format("id=\"%d\"", id);

	wxString oldText = xmlText;
	if (!ReplaceOrInsertChildSimple(xmlText, "border", attrNeedle, newBlock, true)) {
		wxMessageBox("Invalid borders.xml file: missing '</materials>'", "Error", wxICON_ERROR);
		return;
	}

	if (xmlText != oldText && !WriteAllText(bordersFile, xmlText)) {
		ShowErrorDialog("Failed to save changes to borders.xml", ErrorSeverity::Error);
		return;
	}

	DiscardDraft(0, m_selectedBrowserTileId);

	TriggerReloadDataFiles();
	ReloadCurrentBrushEditorXml(wxString::Format("%d", id));

	wxMessageBox("Border saved successfully.", "Success", wxOK | wxICON_INFORMATION | wxSTAY_ON_TOP, this);
}

void BorderEditorPanel::OnSave(wxCommandEvent &event) {
	if (m_activeTab == 0) {
		// Border tab
		SaveBorder();
	} else if (m_activeTab == 1) {
		// Ground tab
		SaveGroundBrush();
	}
}

void BorderEditorPanel::OnClose(wxCommandEvent &event) {
	if (g_gui.aui_manager) {
		wxAuiPaneInfo &pane = g_gui.aui_manager->GetPane(this);
		if (pane.IsOk()) {
			pane.Hide();
			g_gui.aui_manager->Update();
			return;
		}
	}

	wxWindow* parent = GetParent();
	while (parent && !parent->IsTopLevel()) {
		parent = parent->GetParent();
	}

	if (parent) {
		parent->Close();
	} else {
		Destroy();
	}
}

void BorderEditorPanel::OnGridCellClicked(wxMouseEvent &event) {
	// This event is handled by the BorderGridPanel directly
	event.Skip();
}

// ============================================================================
// BorderItemButton

BorderItemButton::BorderItemButton(wxWindow* parent, BorderEdgePosition position, wxWindowID id) :
	wxButton(parent, id, "", wxDefaultPosition, wxSize(32, 32)),
	m_itemId(0),
	m_position(position) {
	// Set up the button to show sprite graphics
	SetBackgroundStyle(wxBG_STYLE_PAINT);
}

BorderItemButton::~BorderItemButton() {
	// No need to destroy anything manually
}

void BorderItemButton::SetItemId(uint16_t id) {
	m_itemId = id;
	Refresh();
}

void BorderItemButton::OnPaint(wxPaintEvent &event) {
	wxPaintDC dc(this);

	// Draw the button background
	wxRect rect = GetClientRect();
	dc.SetBrush(wxBrush(GetBackgroundColour()));
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.DrawRectangle(rect);

	// Draw the item sprite if available
	if (m_itemId > 0) {
		const ItemType &type = g_items.getItemType(m_itemId);
		if (type.id != 0) {
			Sprite* sprite = g_gui.gfx.getSprite(type.clientID);
			if (sprite) {
				sprite->DrawTo(&dc, SPRITE_SIZE_32x32_SCALED, 0, 0, rect.GetWidth(), rect.GetHeight());
			}
		}
	}

	// Draw a border around the button if it's focused
	if (HasFocus()) {
		dc.SetPen(*wxBLACK_PEN);
		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.DrawRectangle(rect);
	}
}

// ============================================================================
// SimpleRawPalettePanel

BEGIN_EVENT_TABLE(SimpleRawPalettePanel, wxScrolledWindow)
EVT_PAINT(SimpleRawPalettePanel::OnPaint)
EVT_LEFT_UP(SimpleRawPalettePanel::OnLeftUp)
EVT_MOTION(SimpleRawPalettePanel::OnMotion)
EVT_LEAVE_WINDOW(SimpleRawPalettePanel::OnLeave)
EVT_SIZE(SimpleRawPalettePanel::OnSize)
END_EVENT_TABLE()

SimpleRawPalettePanel::SimpleRawPalettePanel(wxWindow* parent, wxWindowID id) :
	wxScrolledWindow(parent, id, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_SUNKEN) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);

	// Start empty, let the parent controller load the default tileset
	// by calling LoadTileset("Borders")

	RecalculateLayout();
}

void SimpleRawPalettePanel::SetItemIds(const std::vector<uint16_t> &ids) {
	m_itemIds = ids;
	RecalculateLayout();
	Refresh();
}

void SimpleRawPalettePanel::LoadTileset(const wxString &categoryName, PaletteFilterMode filterMode) {
	m_itemIds.clear();

	// Find the specific tileset by name
	auto it = g_materials.tilesets.find(nstr(categoryName).c_str());
	if (it != g_materials.tilesets.end()) {
		Tileset* tileset = it->second;
		if (tileset) {
			// Load ONLY from RAW category
			const TilesetCategory* rawCat = tileset->getCategory(TILESET_RAW);
			if (rawCat) {
				for (Brush* brush : rawCat->brushlist) {
					if (RAWBrush* rb = dynamic_cast<RAWBrush*>(brush)) {
						uint16_t id = rb->getItemID();

						// Apply filter
						bool include = true;
						if (filterMode == FILTER_GROUND) {
							include = g_items[id].isGroundTile();
						} else if (filterMode == FILTER_WALL) {
							include = g_items[id].isWall;
						}

						if (include) {
							m_itemIds.push_back(id);
						}
					}
				}
			}
		}
	}

	// Remove duplicates and sort
	std::sort(m_itemIds.begin(), m_itemIds.end());
	m_itemIds.erase(std::unique(m_itemIds.begin(), m_itemIds.end()), m_itemIds.end());

	// Reset hover to avoid stuck highlighting
	m_hoverIndex = -1;

	RecalculateLayout();
	Refresh();
}

SimpleRawPalettePanel::~SimpleRawPalettePanel() { }

void SimpleRawPalettePanel::RecalculateLayout() {
	int w, h;
	GetClientSize(&w, &h);
	if (w < m_itemSize) {
		w = m_itemSize;
	}

	m_cols = (w - m_padding) / (m_itemSize + m_padding);
	if (m_cols < 1) {
		m_cols = 1;
	}

	// Ensure m_cols is at least 1 to avoid division by zero
	if (m_cols < 1) {
		m_cols = 1;
	}

	m_rows = (m_itemIds.size() + m_cols - 1) / m_cols;

	int unitY = m_itemSize + m_padding;
	SetVirtualSize(m_cols * (m_itemSize + m_padding) + m_padding, m_rows * unitY + m_padding);
	SetScrollRate(0, unitY); // Row-level scrolling for speed
	Refresh();
}

void SimpleRawPalettePanel::OnSize(wxSizeEvent &event) {
	RecalculateLayout();
	event.Skip();
}

void SimpleRawPalettePanel::OnPaint(wxPaintEvent &event) {
	wxAutoBufferedPaintDC dc(this);
	DoPrepareDC(dc);
	dc.SetBackground(wxBrush(wxColour(0, 0, 0)));
	dc.SetTextForeground(wxColour(255, 255, 255)); // Ensure text is visible on black (if any)
	dc.Clear();

	int startUnitX, startUnitY;
	GetViewStart(&startUnitX, &startUnitY);

	int clientW, clientH;
	GetClientSize(&clientW, &clientH);

	// Scroll unit is 1 row (itemSize + padding)
	int startRow = startUnitY;
	// Add extra buffer rows to be safe
	int endRow = startRow + (clientH / (m_itemSize + m_padding)) + 2;

	if (startRow < 0) {
		startRow = 0;
	}

	int startIndex = startRow * m_cols;
	int endIndex = endRow * m_cols;
	if (endIndex > (int)m_itemIds.size()) {
		endIndex = m_itemIds.size();
	}

	for (int i = startIndex; i < endIndex; ++i) {
		int col = i % m_cols;
		int row = i / m_cols;
		int x = m_padding + col * (m_itemSize + m_padding);
		int y = m_padding + row * (m_itemSize + m_padding);

		wxRect rect(x, y, m_itemSize, m_itemSize);

		if (i == m_hoverIndex) {
			dc.SetPen(wxPen(wxColour(180, 200, 255)));
			dc.SetBrush(wxBrush(wxColour(220, 240, 255)));
			dc.DrawRectangle(rect.Inflate(1));
		}

		uint16_t id = m_itemIds[i];
		if (id != 0) {
			const ItemType &type = g_items.getItemType(id);
			Sprite* sprite = g_gui.gfx.getSprite(type.clientID);
			if (sprite) {
				// Draw directly to the DC to preserve transparency
				sprite->DrawTo(&dc, SPRITE_SIZE_32x32_SCALED, x, y, m_itemSize, m_itemSize);
			}
		}
	}
}

void SimpleRawPalettePanel::OnMotion(wxMouseEvent &event) {
	wxPoint pos = event.GetPosition();
	CalcUnscrolledPosition(pos.x, pos.y, &pos.x, &pos.y);
	int index = HitTest(pos);
	if (index != m_hoverIndex) {
		m_hoverIndex = index;
		Refresh();

		if (index >= 0) {
			// Tooltip
			if (index < (int)m_itemIds.size()) {
				const ItemType &type = g_items.getItemType(m_itemIds[index]);
				SetToolTip(wxString::Format("%s (%d)", type.name, m_itemIds[index]));
			}
		} else {
			UnsetToolTip();
		}
	}
}

void SimpleRawPalettePanel::OnLeave(wxMouseEvent &event) {
	if (m_hoverIndex != -1) {
		m_hoverIndex = -1;
		Refresh();
	}
}

int SimpleRawPalettePanel::HitTest(const wxPoint &pt) const {
	int col = (pt.x - m_padding) / (m_itemSize + m_padding);
	int row = (pt.y - m_padding) / (m_itemSize + m_padding);

	if (col >= 0 && col < m_cols && row >= 0) {
		int index = row * m_cols + col;
		if (index < (int)m_itemIds.size()) {
			return index;
		}
	}
	return -1;
}

void SimpleRawPalettePanel::OnLeftUp(wxMouseEvent &event) {
	wxPoint pos = event.GetPosition();
	CalcUnscrolledPosition(pos.x, pos.y, &pos.x, &pos.y);
	int index = HitTest(pos);
	if (index >= 0 && index < (int)m_itemIds.size()) {
		uint16_t id = m_itemIds[index];
		// Force selection of the brush internally, bypassing the main palette's checks
		// which might fail if the item isn't in the main palette's current view.
		g_gui.SelectBrushInternal(newd RAWBrush(id));

		// Fire event so the dialog can handle it (e.g. for Ground tab)
		wxCommandEvent evt(wxEVT_COMMAND_LISTBOX_SELECTED, GetId());
		evt.SetEventObject(this);
		evt.SetInt(id);
		ProcessWindowEvent(evt);

		Refresh();
	}
}

#if 0
class BrushBrowserGridPanel : public wxScrolledWindow {
public:
    struct Entry {
        wxString label;
        wxString key;
        uint16_t previewItemId;
    };

    BrushBrowserGridPanel(wxWindow* parent, wxWindowID id = wxID_ANY) :
        wxScrolledWindow(parent, id, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_NONE),
        m_cellSize(48),
        m_padding(6),
        m_cols(1),
        m_rows(0),
        m_selected(-1)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetScrollRate(0, m_cellSize + m_padding);
    }

    void SetEntries(std::vector<Entry> entries) {
        m_entries = std::move(entries);
        if (m_selected >= (int)m_entries.size()) {
            m_selected = -1;
        }
        RecalculateLayout();
        Refresh();
    }

    void SetSelectionByKey(const wxString& key) {
        m_selected = -1;
        for (size_t i = 0; i < m_entries.size(); ++i) {
            if (m_entries[i].key == key) {
                m_selected = (int)i;
                break;
            }
        }
        Refresh();
    }

private:
    std::vector<Entry> m_entries;
    int m_cellSize;
    int m_padding;
    int m_cols;
    int m_rows;
    int m_selected;

    void RecalculateLayout() {
        int w, h;
        GetClientSize(&w, &h);
        if (w < m_cellSize) w = m_cellSize;

        m_cols = (w - m_padding) / (m_cellSize + m_padding);
        if (m_cols < 1) m_cols = 1;
        m_rows = (m_entries.size() + m_cols - 1) / m_cols;

        int unitY = m_cellSize + m_padding;
        SetVirtualSize(m_cols * (m_cellSize + m_padding) + m_padding, m_rows * unitY + m_padding);
        SetScrollRate(0, unitY);
    }

    int HitTest(const wxPoint& pt) const {
        int col = (pt.x - m_padding) / (m_cellSize + m_padding);
        int row = (pt.y - m_padding) / (m_cellSize + m_padding);

        if (col >= 0 && col < m_cols && row >= 0) {
            int index = row * m_cols + col;
            if (index >= 0 && index < (int)m_entries.size()) {
                return index;
            }
        }
        return -1;
    }

    void OnSize(wxSizeEvent& event) {
        RecalculateLayout();
        event.Skip();
    }

    void OnPaint(wxPaintEvent& event) {
        wxAutoBufferedPaintDC dc(this);
        DoPrepareDC(dc);
        dc.SetBackground(wxBrush(wxColour(0, 0, 0)));
        dc.Clear();

        int startUnitX, startUnitY;
        GetViewStart(&startUnitX, &startUnitY);

        int clientW, clientH;
        GetClientSize(&clientW, &clientH);

        int unitY = m_cellSize + m_padding;
        int startRow = startUnitY;
        int endRow = startRow + (clientH / unitY) + 2;
        if (startRow < 0) startRow = 0;

        int startIndex = startRow * m_cols;
        int endIndex = endRow * m_cols;
        if (endIndex > (int)m_entries.size()) endIndex = (int)m_entries.size();

        for (int i = startIndex; i < endIndex; ++i) {
            int col = i % m_cols;
            int row = i / m_cols;
            int x = m_padding + col * (m_cellSize + m_padding);
            int y = m_padding + row * (m_cellSize + m_padding);

            wxRect rect(x, y, m_cellSize, m_cellSize);

            if (i == m_selected) {
                dc.SetPen(wxPen(wxColour(0, 120, 215), 2));
                dc.SetBrush(*wxTRANSPARENT_BRUSH);
                dc.DrawRectangle(rect);
            }

            uint16_t itemId = m_entries[i].previewItemId;
            if (itemId > 0) {
                const ItemType& type = g_items.getItemType(itemId);
                Sprite* sprite = g_gui.gfx.getSprite(type.clientID);
                if (sprite) {
                    sprite->DrawTo(&dc, SPRITE_SIZE_32x32_SCALED, x, y, m_cellSize, m_cellSize);
                }
            }
        }
    }

    void OnLeftUp(wxMouseEvent& event) {
        wxPoint pos = event.GetPosition();
        CalcUnscrolledPosition(pos.x, pos.y, &pos.x, &pos.y);
        int index = HitTest(pos);
        if (index >= 0 && index < (int)m_entries.size()) {
            m_selected = index;
            Refresh();

            wxCommandEvent evt(wxEVT_COMMAND_LISTBOX_SELECTED, GetId());
            evt.SetEventObject(this);
            evt.SetString(m_entries[index].key);
            wxPostEvent(GetParent(), evt);
        }
    }

    DECLARE_EVENT_TABLE()
};

BEGIN_EVENT_TABLE(BrushBrowserGridPanel, wxScrolledWindow)
    EVT_PAINT(BrushBrowserGridPanel::OnPaint)
    EVT_LEFT_UP(BrushBrowserGridPanel::OnLeftUp)
    EVT_SIZE(BrushBrowserGridPanel::OnSize)
END_EVENT_TABLE()
#endif

// ============================================================================
// BorderGridPanel

BorderGridPanel::BorderGridPanel(wxWindow* parent, wxWindowID id) :
	wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_SUNKEN) {
	m_items.clear();
	m_selectedPosition = EDGE_NONE;
	SetBackgroundStyle(wxBG_STYLE_PAINT);

	// Enable Full Repaint on Resize for responsive drawing
	SetWindowStyle(GetWindowStyle() | wxFULL_REPAINT_ON_RESIZE);
}

BorderGridPanel::~BorderGridPanel() {
	// Nothing to destroy manually
}

void BorderGridPanel::SetItemId(BorderEdgePosition pos, uint16_t itemId) {
	if (pos >= 0 && pos < EDGE_COUNT) {
		m_items[pos] = itemId;
		Refresh();
	}
}

uint16_t BorderGridPanel::GetItemId(BorderEdgePosition pos) const {
	auto it = m_items.find(pos);
	if (it != m_items.end()) {
		return it->second;
	}
	return 0;
}

void BorderGridPanel::Clear() {
	for (auto &item : m_items) {
		item.second = 0;
	}
	Refresh();
}

void BorderGridPanel::SetSelectedPosition(BorderEdgePosition pos) {
	m_selectedPosition = pos;
	Refresh();
}

// Helper to calculate grid metrics
// struct GridMetrics { ... } // MOVED TO TOP OF FILE

void BorderGridPanel::OnPaint(wxPaintEvent &event) {
	wxAutoBufferedPaintDC dc(this);

	// Draw the panel background using system colors
	wxRect rect = GetClientRect();
	dc.SetBackground(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE)));
	dc.Clear();

	GridMetrics m(rect.GetSize());

	// Draw the grid layout
	dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT)));
	dc.SetBrush(*wxTRANSPARENT_BRUSH);

	// Helper function to draw a grid
	auto drawGrid = [&](int offsetX, int offsetY, int gridSize, int cellSize) {
		for (int i = 0; i <= gridSize; i++) {
			dc.DrawLine(
				offsetX + i * cellSize,
				offsetY,
				offsetX + i * cellSize,
				offsetY + gridSize * cellSize
			);

			// Horizontal lines
			dc.DrawLine(
				offsetX,
				offsetY + i * cellSize,
				offsetX + gridSize * cellSize,
				offsetY + i * cellSize
			);
		}
	};

	// Draw the three grid sections
	drawGrid(m.normalX, m.normalY, 2, m.cellSize);
	drawGrid(m.cornerX, m.cornerY, 2, m.cellSize);
	drawGrid(m.diagX, m.diagY, 2, m.cellSize);

	// Set font for position labels
	dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
	wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
	// Scale font size roughly
	font.SetPixelSize(wxSize(0, std::max(10, m.cellSize / 4)));
	dc.SetFont(font);

	int padding = 2; // Fixed small padding to maximize sprite size

	// Function to draw an item at a position
	auto drawItemAtPos = [&](BorderEdgePosition pos, int gridX, int gridY, int offsetX, int offsetY) {
		int x = offsetX + gridX * m.cellSize + padding;
		int y = offsetY + gridY * m.cellSize + padding;
		int size = m.cellSize - 2 * padding;

		// Highlight selected position
		if (pos == m_selectedPosition) {
			// "Selection Blue" border, transparent fill to keep sprite visible
			dc.SetPen(wxPen(wxColour(0, 120, 215), 2));
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.DrawRectangle(x - padding, y - padding, m.cellSize, m.cellSize);
		}

		// Draw sprite if available
		uint16_t itemId = GetItemId(pos);
		if (itemId > 0) {
			const ItemType &type = g_items.getItemType(itemId);
			if (type.id != 0) {
				Sprite* sprite = g_gui.gfx.getSprite(type.clientID);
				if (sprite) {
					// Local scaling: Draw to temp DC, extract bitmap, scale, then draw
					wxMemoryDC tempDC;
					wxBitmap tempBitmap(32, 32);
					tempDC.SelectObject(tempBitmap);
					tempDC.SetBackground(*wxTRANSPARENT_BRUSH);
					tempDC.Clear();

					// Draw sprite at native size to temp DC
					sprite->DrawTo(&tempDC, SPRITE_SIZE_32x32_SCALED, 0, 0);

					tempDC.SelectObject(wxNullBitmap);

					// Convert to image, scale, and draw
					wxImage img = tempBitmap.ConvertToImage();
					if (img.IsOk()) {
						img.Rescale(size, size, wxIMAGE_QUALITY_NEAREST);
						wxBitmap scaledBmp(img);
						dc.DrawBitmap(scaledBmp, x, y, true);
					}
				}
			}
		} else {
			// Only draw text label if NO item is present to prevent overlap
			wxString label = wxstr(edgePositionToString(pos));
			wxSize textSize = dc.GetTextExtent(label);

			// Center text
			dc.DrawText(label, x + (size - textSize.GetWidth()) / 2, y + size - textSize.GetHeight());
		}
	};

	// Draw normal direction items
	drawItemAtPos(EDGE_N, 0, 0, m.normalX, m.normalY);
	drawItemAtPos(EDGE_E, 1, 0, m.normalX, m.normalY);
	drawItemAtPos(EDGE_S, 0, 1, m.normalX, m.normalY);
	drawItemAtPos(EDGE_W, 1, 1, m.normalX, m.normalY);

	// Draw corner items
	drawItemAtPos(EDGE_CNW, 0, 0, m.cornerX, m.cornerY);
	drawItemAtPos(EDGE_CNE, 1, 0, m.cornerX, m.cornerY);
	drawItemAtPos(EDGE_CSW, 0, 1, m.cornerX, m.cornerY);
	drawItemAtPos(EDGE_CSE, 1, 1, m.cornerX, m.cornerY);

	// Draw diagonal items
	drawItemAtPos(EDGE_DNW, 0, 0, m.diagX, m.diagY);
	drawItemAtPos(EDGE_DNE, 1, 0, m.diagX, m.diagY);
	drawItemAtPos(EDGE_DSW, 0, 1, m.diagX, m.diagY);
	drawItemAtPos(EDGE_DSE, 1, 1, m.diagX, m.diagY);
}

wxPoint BorderGridPanel::GetPositionCoordinates(BorderEdgePosition pos) const {
	switch (pos) {
		case EDGE_N:
			return wxPoint(1, 0);
		case EDGE_E:
			return wxPoint(2, 1);
		case EDGE_S:
			return wxPoint(1, 2);
		case EDGE_W:
			return wxPoint(0, 1);
		case EDGE_CNW:
			return wxPoint(0, 0);
		case EDGE_CNE:
			return wxPoint(2, 0);
		case EDGE_CSE:
			return wxPoint(2, 2);
		case EDGE_CSW:
			return wxPoint(0, 2);
		case EDGE_DNW:
			return wxPoint(0.5, 0.5);
		case EDGE_DNE:
			return wxPoint(1.5, 0.5);
		case EDGE_DSE:
			return wxPoint(1.5, 1.5);
		case EDGE_DSW:
			return wxPoint(0.5, 1.5);
		default:
			return wxPoint(-1, -1);
	}
}

BorderEdgePosition BorderGridPanel::GetPositionFromCoordinates(int x, int y) const {
	GridMetrics m(GetClientSize());

	const int block_width = 2 * m.cellSize;
	const int block_height = 2 * m.cellSize;

	// Check which grid section the click is in and calculate the grid cell

	// Normal grid
	if (x >= m.normalX && x < m.normalX + block_width && y >= m.normalY && y < m.normalY + block_height) {

		int gridX = (x - m.normalX) / m.cellSize;
		int gridY = (y - m.normalY) / m.cellSize;

		if (gridX == 0 && gridY == 0) {
			return EDGE_N;
		}
		if (gridX == 1 && gridY == 0) {
			return EDGE_E;
		}
		if (gridX == 0 && gridY == 1) {
			return EDGE_S;
		}
		if (gridX == 1 && gridY == 1) {
			return EDGE_W;
		}
	}

	// Corner grid
	if (x >= m.cornerX && x < m.cornerX + block_width && y >= m.cornerY && y < m.cornerY + block_height) {

		int gridX = (x - m.cornerX) / m.cellSize;
		int gridY = (y - m.cornerY) / m.cellSize;

		if (gridX == 0 && gridY == 0) {
			return EDGE_CNW;
		}
		if (gridX == 1 && gridY == 0) {
			return EDGE_CNE;
		}
		if (gridX == 0 && gridY == 1) {
			return EDGE_CSW;
		}
		if (gridX == 1 && gridY == 1) {
			return EDGE_CSE;
		}
	}

	// Diagonal grid
	if (x >= m.diagX && x < m.diagX + block_width && y >= m.diagY && y < m.diagY + block_height) {

		int gridX = (x - m.diagX) / m.cellSize;
		int gridY = (y - m.diagY) / m.cellSize;

		if (gridX == 0 && gridY == 0) {
			return EDGE_DNW;
		}
		if (gridX == 1 && gridY == 0) {
			return EDGE_DNE;
		}
		if (gridX == 0 && gridY == 1) {
			return EDGE_DSW;
		}
		if (gridX == 1 && gridY == 1) {
			return EDGE_DSE;
		}
	}

	return EDGE_NONE;
}

wxSize BorderGridPanel::DoGetBestSize() const {
	const int grid_cell_size = 64;
	const int SECTION_SPACING = 20;
	const int MARGIN = 10;

	// 3 blocks of 2 cells each, plus 2 spacings, plus side margins
	int width = MARGIN + (2 * grid_cell_size) + SECTION_SPACING + (2 * grid_cell_size) + SECTION_SPACING + (2 * grid_cell_size) + MARGIN;

	// 1 block height plus margins
	int height = MARGIN + (2 * grid_cell_size) + MARGIN;

	return wxSize(width, height);
}

void BorderGridPanel::OnMouseClick(wxMouseEvent &event) {
	int x = event.GetX();
	int y = event.GetY();

	BorderEdgePosition pos = GetPositionFromCoordinates(x, y);
	if (pos != EDGE_NONE) {
		// Set the position as selected in the grid
		SetSelectedPosition(pos);

		// Notify the parent dialog that a position was selected
		wxCommandEvent selEvent(wxEVT_COMMAND_BUTTON_CLICKED, ID_BORDER_GRID_SELECT);
		selEvent.SetInt(static_cast<int>(pos));

		// Find the parent BorderEditorPanel
		wxWindow* parent = GetParent();
		while (parent && !dynamic_cast<BorderEditorPanel*>(parent)) {
			parent = parent->GetParent();
		}

		// Send the event to the parent dialog
		BorderEditorPanel* dialog = dynamic_cast<BorderEditorPanel*>(parent);
		if (dialog) {
			// Call the event handler directly
			wxLogDebug("BorderGridPanel::OnMouseClick: Calling OnPositionSelected directly for position %s", wxstr(edgePositionToString(pos)));
			dialog->OnPositionSelected(selEvent);
		} else {
			// If we couldn't find the parent dialog, post the event to the parent
			wxLogDebug("BorderGridPanel::OnMouseClick: Could not find BorderEditorPanel parent, posting event");
			wxPostEvent(GetParent(), selEvent);
		}
	}
}

void BorderGridPanel::OnMouseDown(wxMouseEvent &event) {
	// Get the position from the coordinates
	BorderEdgePosition pos = GetPositionFromCoordinates(event.GetX(), event.GetY());

	// Set the selected position
	SetSelectedPosition(pos);

	event.Skip();
}

// ============================================================================
// BorderPreviewPanel

BorderPreviewPanel::BorderPreviewPanel(wxWindow* parent, wxWindowID id) :
	wxPanel(parent, id, wxDefaultPosition, wxDefaultSize) { // Use default size, let sizer expand
	// Set up the panel to handle painting
	SetBackgroundStyle(wxBG_STYLE_PAINT);

	// Enable Full Repaint on Resize
	SetWindowStyle(GetWindowStyle() | wxFULL_REPAINT_ON_RESIZE);
}

BorderPreviewPanel::~BorderPreviewPanel() {
	// Nothing to destroy manually
}

void BorderPreviewPanel::SetBorderItems(const std::vector<BorderItem> &items) {
	m_borderItems = items;
	Refresh();
}

void BorderPreviewPanel::Clear() {
	m_borderItems.clear();
	Refresh();
}

void BorderPreviewPanel::OnPaint(wxPaintEvent &event) {
	wxAutoBufferedPaintDC dc(this);

	// Get panel dimensions
	wxRect rect = GetClientRect();
	int panelWidth = rect.GetWidth();
	int panelHeight = rect.GetHeight();

	// Use parent's background instead of filling entire panel with grey
	dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
	dc.Clear();

	// 5x5 Cross Test Pattern for comprehensive border verification
	const int GRID_SIZE = 5;
	const int TITLE_HEIGHT = 25; // Space for the title label
	const int MARGIN = 5; // Small margin around the grid

	// *** DYNAMIC CELL SIZE: Calculate based on available space (like BorderGridPanel) ***
	int availableWidth = panelWidth - 2 * MARGIN;
	int availableHeight = panelHeight - TITLE_HEIGHT - 2 * MARGIN;

	// Calculate cell size to fit the 5x5 grid in available space
	int cellSize = std::min(availableWidth / GRID_SIZE, availableHeight / GRID_SIZE);

	// Enforce minimum and maximum cell size for visual consistency
	if (cellSize < 32) {
		cellSize = 32; // Minimum: match native sprite size
	}
	if (cellSize > 64) {
		cellSize = 64; // Maximum: don't go too large
	}

	int totalGridSize = GRID_SIZE * cellSize;

	// Center grid in the panel
	int startX = (panelWidth - totalGridSize) / 2;
	int startY = TITLE_HEIGHT + (availableHeight - totalGridSize) / 2;

	// Ensure grid doesn't go above the title
	if (startY < TITLE_HEIGHT) {
		startY = TITLE_HEIGHT;
	}

	// Visual Polish: Add "Preview" label at the top
	dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
	wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
	font.MakeBold();
	dc.SetFont(font);

	wxString label = "Preview (5x5 Test Pattern)";
	wxSize textSize = dc.GetTextExtent(label);
	dc.DrawText(label, (panelWidth - textSize.GetWidth()) / 2, 5);

	// *** TIGHT BACKGROUND: Draw grey background exactly around the grid (1px border) ***
	const int BORDER_WIDTH = 1;
	wxRect gridBgRect(
		startX - BORDER_WIDTH,
		startY - BORDER_WIDTH,
		totalGridSize + 2 * BORDER_WIDTH,
		totalGridSize + 2 * BORDER_WIDTH
	);
	dc.SetBrush(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE)));
	dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT)));
	dc.DrawRectangle(gridBgRect);

	// Draw grid lines for the 5x5 area
	dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT)));

	// Vertical lines
	for (int i = 0; i <= GRID_SIZE; i++) {
		dc.DrawLine(startX + i * cellSize, startY, startX + i * cellSize, startY + totalGridSize);
	}

	// Horizontal lines
	for (int i = 0; i <= GRID_SIZE; i++) {
		dc.DrawLine(startX, startY + i * cellSize, startX + totalGridSize, startY + i * cellSize);
	}

	// Helper lambda: Get the target edge for a grid position (x, y)
	auto getEdgeAtPosition = [](int x, int y) -> BorderEdgePosition {
		// Row 0
		if (y == 0) {
			if (x == 1) {
				return EDGE_CSE;
			}
			if (x == 2) {
				return EDGE_S;
			}
			if (x == 3) {
				return EDGE_CSW;
			}
		}
		// Row 1
		else if (y == 1) {
			if (x == 0) {
				return EDGE_CSE;
			}
			if (x == 1) {
				return EDGE_DSE;
			}
			if (x == 3) {
				return EDGE_DSW;
			}
			if (x == 4) {
				return EDGE_CSW;
			}
		}
		// Row 2 (Center row)
		else if (y == 2) {
			if (x == 0) {
				return EDGE_E;
			}
			if (x == 4) {
				return EDGE_W;
			}
			// x=2 is center ground (empty)
		}
		// Row 3
		else if (y == 3) {
			if (x == 0) {
				return EDGE_CNE;
			}
			if (x == 1) {
				return EDGE_DNE;
			}
			if (x == 3) {
				return EDGE_DNW;
			}
			if (x == 4) {
				return EDGE_CNW;
			}
		}
		// Row 4
		else if (y == 4) {
			if (x == 1) {
				return EDGE_CNE;
			}
			if (x == 2) {
				return EDGE_N;
			}
			if (x == 3) {
				return EDGE_CNW;
			}
		}

		return static_cast<BorderEdgePosition>(-1); // No edge for this cell
	};

	// *** SPRITE SCALING: Use same technique as BorderGridPanel for consistency ***
	const int spritePadding = 2; // Small padding within each cell
	const int spriteSize = cellSize - 2 * spritePadding;

	// Draw border items based on the cross pattern
	for (int y = 0; y < GRID_SIZE; y++) {
		for (int x = 0; x < GRID_SIZE; x++) {
			BorderEdgePosition targetEdge = getEdgeAtPosition(x, y);

			if (targetEdge == static_cast<BorderEdgePosition>(-1)) {
				continue; // Empty cell
			}

			// Search for an item with this edge position
			for (const BorderItem &item : m_borderItems) {
				if (item.position == targetEdge) {
					int cellX = startX + x * cellSize + spritePadding;
					int cellY = startY + y * cellSize + spritePadding;

					// Draw the item sprite using same scaling as BorderGridPanel
					const ItemType &type = g_items.getItemType(item.itemId);
					if (type.id != 0) {
						Sprite* sprite = g_gui.gfx.getSprite(type.clientID);
						if (sprite) {
							// Create temp bitmap, draw sprite at native size, then scale
							wxMemoryDC tempDC;
							wxBitmap tempBitmap(32, 32);
							tempDC.SelectObject(tempBitmap);
							tempDC.SetBackground(*wxTRANSPARENT_BRUSH);
							tempDC.Clear();

							// Draw sprite at native 32x32 size to temp DC
							sprite->DrawTo(&tempDC, SPRITE_SIZE_32x32, 0, 0);

							tempDC.SelectObject(wxNullBitmap);

							// Convert to image, scale to fill cell, and draw
							wxImage img = tempBitmap.ConvertToImage();
							if (img.IsOk()) {
								// Scale to fit the cell (dynamic size)
								img.Rescale(spriteSize, spriteSize, wxIMAGE_QUALITY_NEAREST);
								wxBitmap scaledBmp(img);
								dc.DrawBitmap(scaledBmp, cellX, cellY, true);
							}
						}
					}
					break; // Found and drawn, move to next grid cell
				}
			}
		}
	}
}

void BorderEditorPanel::LoadExistingGroundBrushes() {
	// Legacy method - ground brushes are now populated via PopulateGroundList()
	// Called from constructor for compatibility, actual population happens on tab change
}
void BorderEditorPanel::ClearGroundItems(bool clearBrowser) {
	if (m_groundGridContainer) {
		m_groundGridContainer->Clear();
	}
	if (m_groundNameCtrl) {
		m_groundNameCtrl->SetValue("");
	}
	if (m_zOrderCtrl) {
		m_zOrderCtrl->SetValue(0);
	}

	// Reset border alignment options
	if (m_includeToNoneCheck) {
		m_includeToNoneCheck->SetValue(true);
	}
	if (m_includeInnerCheck) {
		m_includeInnerCheck->SetValue(false);
	}
	if (m_borderAlignmentButton) {
		m_borderAlignmentSelection = 0;
		m_borderAlignmentButton->SetLabel("Outer \u25BC");
	}
	if (m_groundPreviewPanel) {
		m_groundPreviewPanel->SetWeightedItems({}); // Clear preview
	}

	// Deselect browser list
	if (clearBrowser && m_borderBrowserList) {
		m_borderBrowserList->SetSelection(wxNOT_FOUND);
		m_selectedBrowserTileId = 0;
	}
}

void BorderEditorPanel::OnPageChanged(wxBookCtrlEvent &event) {
	SaveCurrentDraft(false);

	m_activeTab = event.GetSelection();

	LoadTilesets();

	if (m_browserSearchCtrl) {
		m_browserSearchCtrl->Clear();
	}

	UpdateBrowserLabel();

	if (m_newButton) {
		switch (m_activeTab) {
			case 0:
				m_newButton->SetLabel("New Border");
				break;
			case 1:
				m_newButton->SetLabel("New Ground");
				break;
			case 2:
				m_newButton->SetLabel("New Wall");
				break;
		}
	}

	FilterBrowserList(m_browserSearchCtrl ? m_browserSearchCtrl->GetValue() : wxString());

	if (m_selectedBrowserTileId != 0) {
		RestoreBrowserSelection(wxString::Format("%u", (unsigned)m_selectedBrowserTileId));
		switch (m_activeTab) {
			case 0:
				LoadBorderByMainTileId(m_selectedBrowserTileId);
				break;
			case 1:
				LoadGroundBrushByMainTileId(m_selectedBrowserTileId);
				break;
			case 2:
				LoadWallBrushByMainTileId(m_selectedBrowserTileId);
				break;
		}
	}

	event.Skip();
}

void BorderEditorPanel::UpdateBrowserLabel() {
	// No longer needed - mode is indicated by toggle buttons
}

void BorderEditorPanel::OnModeSwitch(wxCommandEvent &event) {
	SaveCurrentDraft(false);

	wxToggleButton* clickedBtn = dynamic_cast<wxToggleButton*>(event.GetEventObject());
	if (!clickedBtn) {
		return;
	}

	// Determine which mode was selected
	int newTab = 0;
	if (clickedBtn == m_borderModeBtn) {
		newTab = 0;
	} else if (clickedBtn == m_groundModeBtn) {
		newTab = 1;
	} else if (clickedBtn == m_wallModeBtn) {
		newTab = 2;
	}

	// Update toggle button states (only clicked one should be depressed)
	m_borderModeBtn->SetValue(newTab == 0);
	m_groundModeBtn->SetValue(newTab == 1);
	m_wallModeBtn->SetValue(newTab == 2);

	// Switch the notebook page to show the correct editor panel for this mode
	if (m_notebook) {
		m_notebook->ChangeSelection(newTab);
	}
	m_activeTab = newTab;

	// Refresh tileset list (and apply filters) for the new mode
	LoadTilesets();

	{
		const auto options = GetBrowserTilesetOptions();
		if (!options.empty()) {
			bool filterExists = false;
			for (const auto &opt : options) {
				if (opt.second == m_browserTilesetFilter) {
					filterExists = true;
					break;
				}
			}

			if (m_browserTilesetFilter.IsEmpty() || !filterExists) {
				m_browserTilesetFilter = options.front().second;
			}

			wxString shown = options.front().first;
			for (const auto &opt : options) {
				if (opt.second == m_browserTilesetFilter) {
					shown = opt.first;
					break;
				}
			}

			if (m_browserTilesetFilterButton) {
				m_browserTilesetFilterButton->SetLabel("Tileset: " + shown + " ▼");
			}
		} else {
			m_browserTilesetFilter.clear();
			if (m_browserTilesetFilterButton) {
				m_browserTilesetFilterButton->SetLabel("Tileset: Select ▼");
			}
		}
	}

	// Clear search and repopulate sidebar
	if (m_browserSearchCtrl) {
		m_browserSearchCtrl->Clear();
	}

	// Keep browser list unified; just re-apply search/filter
	FilterBrowserList(m_browserSearchCtrl ? m_browserSearchCtrl->GetValue() : wxString());

	bool restored = false;
	if (m_selectedBrowserTileId != 0) {
		restored = RestoreBrowserSelection(wxString::Format("%u", (unsigned)m_selectedBrowserTileId));
	}

	if (!restored) {
		switch (m_activeTab) {
			case 0:
				ClearItems(false);
				break;
			case 1:
				ClearGroundItems(false);
				break;
			case 2:
				ClearWallItems(false);
				break;
		}
		return;
	}

	switch (m_activeTab) {
		case 0:
			LoadBorderByMainTileId(m_selectedBrowserTileId);
			break;
		case 1:
			LoadGroundBrushByMainTileId(m_selectedBrowserTileId);
			break;
		case 2:
			LoadWallBrushByMainTileId(m_selectedBrowserTileId);
			break;
	}
}

void BorderEditorPanel::PopulateBorderList() {
	m_fullBrowserList.Clear();
	m_fullBrowserIds.Clear();
	m_fullBrowserPreviewIds.clear();
	m_borderBrowserList->Clear();

	wxString dataDir = g_gui.GetDataDirectory();

	wxString bordersFile = dataDir + wxFileName::GetPathSeparator() + "materials" + wxFileName::GetPathSeparator() + "borders" + wxFileName::GetPathSeparator() + "borders.xml";

	wxString groundsFile = dataDir + wxFileName::GetPathSeparator() + "materials" + wxFileName::GetPathSeparator() + "brushs" + wxFileName::GetPathSeparator() + "grounds.xml";

	if (!wxFileExists(bordersFile) || !wxFileExists(groundsFile)) {
		return;
	}

	std::map<int, wxString> borderNames;
	int maxId = 0;

	{
		pugi::xml_document doc;
		pugi::xml_parse_result result = doc.load_file(nstr(bordersFile).c_str());
		if (!result) {
			return;
		}

		pugi::xml_node materials = doc.child("materials");
		if (!materials) {
			return;
		}

		for (pugi::xml_node borderNode = materials.child("border"); borderNode; borderNode = borderNode.next_sibling("border")) {
			pugi::xml_attribute idAttr = borderNode.attribute("id");
			if (!idAttr) {
				continue;
			}

			const int id = idAttr.as_int();
			if (id > maxId) {
				maxId = id;
			}

			wxString name;
			pugi::xml_attribute nameAttr = borderNode.attribute("name");
			if (nameAttr && wxString(nameAttr.as_string()).Trim().Length() > 0) {
				name = wxString(nameAttr.as_string());
			} else {
				const char* nodeText = borderNode.child_value();
				if (nodeText && strlen(nodeText) > 0) {
					wxString textContent = wxString(nodeText).Trim(true).Trim(false);
					while (textContent.StartsWith("-") || textContent.StartsWith(" ")) {
						textContent = textContent.Mid(1);
					}
					while (textContent.EndsWith("-") || textContent.EndsWith(" ")) {
						textContent = textContent.RemoveLast();
					}
					textContent = textContent.Trim(true).Trim(false);
					if (!textContent.IsEmpty()) {
						name = textContent;
					}
				}

				if (name.IsEmpty()) {
					name = "(no name)";
				}
			}

			borderNames[id] = name;
		}
	}

	std::map<int, std::pair<uint16_t, wxString>> borderToMainTile;

	{
		pugi::xml_document doc;
		if (!doc.load_file(nstr(groundsFile).c_str())) {
			return;
		}

		pugi::xml_node materials = doc.child("materials");
		if (!materials) {
			return;
		}

		for (pugi::xml_node brushNode = materials.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
			pugi::xml_attribute typeAttr = brushNode.attribute("type");
			if (!typeAttr || std::string(typeAttr.as_string()) != "ground") {
				continue;
			}

			pugi::xml_attribute nameAttr = brushNode.attribute("name");
			if (!nameAttr) {
				continue;
			}

			uint16_t mainTileId = 0;
			if (pugi::xml_attribute serverLookIdAttr = brushNode.attribute("server_lookid")) {
				mainTileId = serverLookIdAttr.as_uint();
			} else if (pugi::xml_attribute lookIdAttr = brushNode.attribute("lookid")) {
				mainTileId = lookIdAttr.as_uint();
			}

			if (mainTileId == 0) {
				continue;
			}

			int outerBorderId = 0;
			for (pugi::xml_node borderNode = brushNode.child("border"); borderNode; borderNode = borderNode.next_sibling("border")) {
				pugi::xml_attribute idAttr = borderNode.attribute("id");
				if (!idAttr) {
					continue;
				}

				wxString align = wxString(borderNode.attribute("align").as_string());
				if (align.IsEmpty() || align == "outer") {
					outerBorderId = idAttr.as_int();
					break;
				}
			}

			if (outerBorderId <= 0) {
				continue;
			}

			if (borderToMainTile.find(outerBorderId) == borderToMainTile.end()) {
				borderToMainTile[outerBorderId] = { mainTileId, wxString(nameAttr.as_string()) };
			}
		}
	}

	for (const auto &pair : borderToMainTile) {
		const int borderId = pair.first;
		const uint16_t tileId = pair.second.first;
		const wxString brushName = pair.second.second;

		wxString borderName;
		auto itName = borderNames.find(borderId);
		if (itName != borderNames.end()) {
			borderName = itName->second;
		} else {
			borderName = wxString::Format("border %d", borderId);
		}

		wxString label = wxString::Format("%d - %s [%s]", (int)tileId, brushName, borderName);

		m_fullBrowserList.Add(label);
		m_fullBrowserIds.Add(wxString::Format("%d", borderId));
		m_fullBrowserPreviewIds.push_back(tileId);

		m_borderBrowserList->Append(label, new wxStringClientData(wxString::Format("%d", borderId)));
	}

	m_nextBorderId = maxId + 1;

	FilterBrowserList(m_browserSearchCtrl ? m_browserSearchCtrl->GetValue() : wxString());
}

void BorderEditorPanel::PopulateGroundList() {
	m_fullBrowserList.Clear();
	m_fullBrowserIds.Clear();
	m_fullBrowserPreviewIds.clear();
	m_borderBrowserList->Clear();

	wxString dataDir = g_gui.GetDataDirectory();
	wxString groundsFile = dataDir + wxFileName::GetPathSeparator() + "materials" + wxFileName::GetPathSeparator() + "brushs" + wxFileName::GetPathSeparator() + "grounds.xml";

	if (!wxFileExists(groundsFile)) {
		return;
	}

	pugi::xml_document doc;
	if (!doc.load_file(nstr(groundsFile).c_str())) {
		return;
	}

	pugi::xml_node materials = doc.child("materials");
	if (!materials) {
		return;
	}

	for (pugi::xml_node brushNode = materials.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
		pugi::xml_attribute typeAttr = brushNode.attribute("type");
		if (!typeAttr || std::string(typeAttr.as_string()) != "ground") {
			continue;
		}

		pugi::xml_attribute nameAttr = brushNode.attribute("name");
		if (!nameAttr) {
			continue;
		}

		wxString brushName = wxString(nameAttr.as_string());
		m_fullBrowserList.Add(brushName);
		m_fullBrowserIds.Add(brushName);

		uint16_t previewId = 0;
		pugi::xml_node itemNode = brushNode.child("item");
		if (itemNode) {
			previewId = itemNode.attribute("id").as_uint();
		}
		m_fullBrowserPreviewIds.push_back(previewId);

		m_borderBrowserList->Append(brushName, new wxStringClientData(brushName));
	}

	FilterBrowserList(m_browserSearchCtrl ? m_browserSearchCtrl->GetValue() : wxString());
}

void BorderEditorPanel::PopulateWallList() {
	m_fullBrowserList.Clear();
	m_fullBrowserIds.Clear();
	m_fullBrowserPreviewIds.clear();
	m_borderBrowserList->Clear();

	wxString dataDir = g_gui.GetDataDirectory();
	wxString wallsFile = dataDir + wxFileName::GetPathSeparator() + "materials" + wxFileName::GetPathSeparator() + "brushs" + wxFileName::GetPathSeparator() + "walls.xml";

	if (!wxFileExists(wallsFile)) {
		return;
	}

	pugi::xml_document doc;
	if (!doc.load_file(nstr(wallsFile).c_str())) {
		return;
	}

	pugi::xml_node materials = doc.child("materials");
	if (!materials) {
		return;
	}

	for (pugi::xml_node brushNode = materials.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
		pugi::xml_attribute nameAttr = brushNode.attribute("name");
		if (!nameAttr) {
			continue;
		}

		wxString brushName = wxString(nameAttr.as_string());
		m_fullBrowserList.Add(brushName);
		m_fullBrowserIds.Add(brushName);

		uint16_t previewId = 0;
		pugi::xml_node wallNode = brushNode.child("wall");
		if (wallNode) {
			pugi::xml_node itemNode = wallNode.child("item");
			if (itemNode) {
				previewId = itemNode.attribute("id").as_uint();
			}
		}
		m_fullBrowserPreviewIds.push_back(previewId);

		m_borderBrowserList->Append(brushName, new wxStringClientData(brushName));
	}

	FilterBrowserList(m_browserSearchCtrl ? m_browserSearchCtrl->GetValue() : wxString());
}

void BorderEditorPanel::PopulateUnifiedList() {
	m_fullBrowserList.Clear();
	m_fullBrowserIds.Clear();
	m_fullBrowserPreviewIds.clear();
	m_borderBrowserList->Clear();

	wxString dataDir = g_gui.GetDataDirectory();
	wxString groundsFile = dataDir + wxFileName::GetPathSeparator() + "materials" + wxFileName::GetPathSeparator() + "brushs" + wxFileName::GetPathSeparator() + "grounds.xml";

	wxString wallsFile = dataDir + wxFileName::GetPathSeparator() + "materials" + wxFileName::GetPathSeparator() + "brushs" + wxFileName::GetPathSeparator() + "walls.xml";

	std::set<uint16_t> seen;

	if (wxFileExists(groundsFile)) {
		pugi::xml_document doc;
		if (doc.load_file(nstr(groundsFile).c_str())) {
			pugi::xml_node materials = doc.child("materials");
			for (pugi::xml_node brushNode = materials.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
				pugi::xml_attribute typeAttr = brushNode.attribute("type");
				if (!typeAttr || std::string(typeAttr.as_string()) != "ground") {
					continue;
				}

				pugi::xml_attribute nameAttr = brushNode.attribute("name");
				if (!nameAttr) {
					continue;
				}

				uint16_t tileId = 0;
				if (pugi::xml_attribute a = brushNode.attribute("server_lookid")) {
					tileId = a.as_uint();
				} else if (pugi::xml_attribute a = brushNode.attribute("lookid")) {
					tileId = a.as_uint();
				} else if (pugi::xml_node itemNode = brushNode.child("item")) {
					tileId = itemNode.attribute("id").as_uint();
				}

				if (tileId == 0) {
					continue;
				}

				if (seen.find(tileId) != seen.end()) {
					continue;
				}
				seen.insert(tileId);

				wxString brushName = wxString(nameAttr.as_string());
				wxString label = wxString::Format("%d - %s", (int)tileId, brushName);

				m_fullBrowserList.Add(label);
				m_fullBrowserIds.Add(wxString::Format("%d", (int)tileId));
				m_fullBrowserPreviewIds.push_back(tileId);

				m_borderBrowserList->Append(label, new wxStringClientData(wxString::Format("%d", (int)tileId)));
			}
		}
	}

	if (wxFileExists(wallsFile)) {
		pugi::xml_document doc;
		if (doc.load_file(nstr(wallsFile).c_str())) {
			pugi::xml_node materials = doc.child("materials");
			for (pugi::xml_node brushNode = materials.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
				pugi::xml_attribute nameAttr = brushNode.attribute("name");
				if (!nameAttr) {
					continue;
				}

				uint16_t tileId = 0;
				if (pugi::xml_attribute a = brushNode.attribute("server_lookid")) {
					tileId = a.as_uint();
				}

				if (tileId == 0) {
					continue;
				}

				if (seen.find(tileId) != seen.end()) {
					continue;
				}
				seen.insert(tileId);

				wxString brushName = wxString(nameAttr.as_string());
				wxString label = wxString::Format("%d - %s", (int)tileId, brushName);

				m_fullBrowserList.Add(label);
				m_fullBrowserIds.Add(wxString::Format("%d", (int)tileId));
				m_fullBrowserPreviewIds.push_back(tileId);

				m_borderBrowserList->Append(label, new wxStringClientData(wxString::Format("%d", (int)tileId)));
			}
		}
	}

	FilterBrowserList(m_browserSearchCtrl ? m_browserSearchCtrl->GetValue() : wxString());
}

void BorderEditorPanel::OnBrowserSearch(wxCommandEvent &event) {
	FilterBrowserList(m_browserSearchCtrl->GetValue());
}

static wxString FindTilesetByPreferredOrContains(const wxString &preferred, const std::vector<wxString> &needlesLower) {
	if (!preferred.IsEmpty() && g_materials.tilesets.find(nstr(preferred).c_str()) != g_materials.tilesets.end()) {
		return preferred;
	}

	for (const auto &pair : g_materials.tilesets) {
		wxString ts = wxString(pair.first);
		wxString lower = ts.Lower();
		for (const wxString &needle : needlesLower) {
			if (!needle.IsEmpty() && lower.Contains(needle)) {
				return ts;
			}
		}
	}

	return wxString();
}

static std::vector<std::pair<wxString, wxString>> GetBrowserTilesetOptions() {
	std::vector<std::pair<wxString, wxString>> options;

	wxString mountains = FindTilesetByPreferredOrContains("Grounds - Mountains", { "mountain", "montain" });
	if (!mountains.IsEmpty()) {
		options.push_back({ "Mountains", mountains });
	}

	wxString nature = FindTilesetByPreferredOrContains("Grounds - Nature", { "nature" });
	if (!nature.IsEmpty()) {
		options.push_back({ "Nature", nature });
	}

	wxString ornamented = FindTilesetByPreferredOrContains("Grounds - Ornamented", { "ornament" });
	if (!ornamented.IsEmpty()) {
		options.push_back({ "Ornamented", ornamented });
	}

	wxString wall = FindTilesetByPreferredOrContains("Walls", { "wall" });
	if (!wall.IsEmpty()) {
		options.push_back({ "Wall", wall });
	}

	return options;
}

void BorderEditorPanel::OnBrowserTilesetFilterButtonClick(wxCommandEvent &event) {
	wxMenu menu;

	const auto options = GetBrowserTilesetOptions();
	if (options.empty()) {
		return;
	}

	auto addRadio = [&](const wxString &shown, const wxString &value) {
		wxMenuItem* item = menu.AppendRadioItem(wxID_ANY, shown);
		if (value == m_browserTilesetFilter) {
			item->Check(true);
		}
		menu.Bind(wxEVT_MENU, [this, shown, value](wxCommandEvent &) {
            const uint16_t keepTileId = m_selectedBrowserTileId;

            m_browserTilesetFilter = value;
            if (m_browserTilesetFilterButton) {
                m_browserTilesetFilterButton->SetLabel("Tileset: " + shown + " ▼");
            }

            FilterBrowserList(m_browserSearchCtrl ? m_browserSearchCtrl->GetValue() : wxString());

            bool restored = false;
            if (keepTileId != 0) {
                restored = RestoreBrowserSelection(wxString::Format("%u", (unsigned)keepTileId));
            }

            if (restored) {
                switch (m_activeTab) {
                    case 0: LoadBorderByMainTileId(keepTileId); break;
                    case 1: LoadGroundBrushByMainTileId(keepTileId); break;
                    case 2: LoadWallBrushByMainTileId(keepTileId); break;
                }
            } else {
                switch (m_activeTab) {
                    case 0: ClearItems(false); break;
                    case 1: ClearGroundItems(false); break;
                    case 2: ClearWallItems(false); break;
                }
            } }, item->GetId());
	};

	for (const auto &opt : options) {
		addRadio(opt.first, opt.second);
	}

	PopupMenu(&menu);
}

void BorderEditorPanel::FilterBrowserList(const wxString &query) {
	m_borderBrowserList->Clear();
	wxString lowerQuery = query.Lower();

	const auto filterOptions = GetBrowserTilesetOptions();

	const bool useTilesetFilter = !m_browserTilesetFilter.IsEmpty();
	const std::string tilesetName = useTilesetFilter ? nstr(m_browserTilesetFilter) : std::string();

	std::vector<BrushBrowserGridPanel::Entry> entries;
	entries.reserve(m_fullBrowserList.GetCount());

	for (size_t i = 0; i < m_fullBrowserList.GetCount(); i++) {
		if (!query.IsEmpty() && !m_fullBrowserList[i].Lower().Contains(lowerQuery)) {
			continue;
		}

		uint16_t previewId = 0;
		if (i < m_fullBrowserPreviewIds.size()) {
			previewId = m_fullBrowserPreviewIds[i];
		}

		if (useTilesetFilter) {
			if (previewId == 0) {
				continue;
			}

			const ItemType &type = g_items.getItemType(previewId);
			if (type.id == 0) {
				continue;
			}

			auto inTilesetFor = [&](const wxString &tilesetWx) -> bool {
				const std::string ts = nstr(tilesetWx);
				return g_materials.isInTileset(type.brush, ts) || g_materials.isInTileset(type.doodad_brush, ts) || g_materials.isInTileset(type.raw_brush, ts);
			};

			if (!inTilesetFor(m_browserTilesetFilter)) {
				continue;
			}

			if (filterOptions.size() > 1) {
				size_t selectedIndex = filterOptions.size();
				for (size_t idx = 0; idx < filterOptions.size(); ++idx) {
					if (filterOptions[idx].second == m_browserTilesetFilter) {
						selectedIndex = idx;
						break;
					}
				}

				if (selectedIndex != filterOptions.size()) {
					bool belongsToHigher = false;
					for (size_t idx = 0; idx < selectedIndex; ++idx) {
						const wxString &higherTs = filterOptions[idx].second;
						if (higherTs.IsEmpty()) {
							continue;
						}
						if (inTilesetFor(higherTs)) {
							belongsToHigher = true;
							break;
						}
					}
					if (belongsToHigher) {
						continue;
					}
				}
			}
		}

		wxString displayLabel = m_fullBrowserList[i];
		if (previewId != 0 && !filterOptions.empty()) {
			if (useTilesetFilter) {
				wxString selectedShown;
				for (const auto &opt : filterOptions) {
					if (opt.second == m_browserTilesetFilter) {
						selectedShown = opt.first;
						break;
					}
				}

				if (!selectedShown.IsEmpty()) {
					displayLabel += " [" + selectedShown + "]";
				}
			} else {
				const ItemType &type = g_items.getItemType(previewId);
				if (type.id != 0) {
					wxString tagList;
					bool first = true;

					for (const auto &opt : filterOptions) {
						const wxString &shown = opt.first;
						const wxString &tsNameWx = opt.second;
						if (tsNameWx.IsEmpty()) {
							continue;
						}

						const std::string tsName = nstr(tsNameWx);
						const bool inTileset = g_materials.isInTileset(type.brush, tsName) || g_materials.isInTileset(type.doodad_brush, tsName) || g_materials.isInTileset(type.raw_brush, tsName);

						if (inTileset) {
							if (!first) {
								tagList += ", ";
							}
							tagList += shown;
							first = false;
						}
					}

					if (!tagList.IsEmpty()) {
						displayLabel += " [" + tagList + "]";
					}
				}
			}
		}

		m_borderBrowserList->Append(displayLabel, new wxStringClientData(m_fullBrowserIds[i]));

		BrushBrowserGridPanel::Entry e;
		e.label = displayLabel;
		e.key = m_fullBrowserIds[i];
		e.previewItemId = previewId;
		entries.push_back(std::move(e));
	}

	if (m_browserGrid) {
		m_browserGrid->SetEntries(std::move(entries));
	}
}

void BorderEditorPanel::OnGroundGridSelect(wxCommandEvent &event) {
	// Selection logic logic is handled by container/cell highlighting
	// No UI updates needed here anymore as editing is via double-click modal
}

void BorderEditorPanel::LoadTilesets() {
	// Clear the choice control
	m_tilesetListData.Clear();
	m_tilesets.clear();

	// Get the appropriate whitelist for the current mode
	const std::set<wxString> &whitelist = (m_activeTab == 0) ? m_borderEnabledTilesets : (m_activeTab == 1) ? m_groundEnabledTilesets
																											: m_wallEnabledTilesets;

	// Use the already-loaded tilesets from g_materials
	// (g_materials.tilesets is populated by the Materials loader at startup)
	wxArrayString tilesetNames;
	for (const auto &pair : g_materials.tilesets) {
		if (pair.second) {
			wxString tilesetName = wxString(pair.first);
			const bool allowed = whitelist.empty() || whitelist.find(tilesetName) != whitelist.end();
			if (allowed) {
				tilesetNames.Add(tilesetName);
				m_tilesets[tilesetName] = tilesetName;
			}
		}
	}

	// Sort tileset names alphabetically
	tilesetNames.Sort();

	// Add sorted names to the tileset list
	m_tilesetListData = tilesetNames;

	// Update the appropriate ComboBox based on active tab
	// Update the appropriate Control based on active tab
	if (m_activeTab == 0 && m_rawCategoryButton) {
		// Border tab
		m_rawCategoryNames = m_tilesetListData;

		if (!m_tilesetListData.IsEmpty()) {
			m_rawCategorySelection = 0;
			m_rawCategoryButton->SetLabel(m_tilesetListData[0] + " \u25BC");

			if (m_itemPalettePanel) {
				m_itemPalettePanel->LoadTileset(m_tilesetListData[0]);
			}
		} else {
			m_rawCategoryButton->SetLabel("Select \u25BC");
			if (m_itemPalettePanel) {
				m_itemPalettePanel->LoadTileset("");
			}
		}
	} else if (m_activeTab == 1 && m_groundTilesetButton) {
		// Ground tab
		if (!m_tilesetListData.IsEmpty()) {
			m_groundTilesetSelection = 0;
			m_groundTilesetButton->SetLabel(m_tilesetListData[0] + " \u25BC");

			if (m_groundPalette) {
				m_groundPalette->LoadTileset(m_tilesetListData[0], FILTER_GROUND);
			}
		} else {
			m_groundTilesetButton->SetLabel("Select \u25BC");
		}
	} else if (m_activeTab == 2 && m_wallTilesetButton) {
		// Wall tab
		if (!m_tilesetListData.IsEmpty()) {
			m_wallTilesetSelection = 0;
			m_wallTilesetButton->SetLabel(m_tilesetListData[0] + " \u25BC");

			if (m_wallPalette) {
				m_wallPalette->LoadTileset(m_tilesetListData[0], FILTER_WALL);
			}
		} else {
			m_wallTilesetButton->SetLabel("Select \u25BC");
		}
	}
}

// ============================================================================
// Filter Config Persistence

void BorderEditorPanel::SaveFilterConfig() {
	// Save filter configuration to JSON file
	wxString configDir = wxFileName::GetHomeDir() + wxFileName::GetPathSeparator() + ".rme";
	if (!wxDirExists(configDir)) {
		if (!wxMkdir(configDir)) {
			// Failed to create directory
			return;
		}
	}

	wxString configFile = configDir + wxFileName::GetPathSeparator() + "border_editor_filters_v2.json";

	// Build JSON manually (simple format)
	wxString json = "{\n";

	// Border enabled tilesets
	json += "  \"border_enabled\": [";
	bool first = true;
	for (const wxString &ts : m_borderEnabledTilesets) {
		if (!first) {
			json += ", ";
		}
		json += "\"" + ts + "\"";
		first = false;
	}
	json += "],\n";

	// Ground enabled tilesets
	json += "  \"ground_enabled\": [";
	first = true;
	for (const wxString &ts : m_groundEnabledTilesets) {
		if (!first) {
			json += ", ";
		}
		json += "\"" + ts + "\"";
		first = false;
	}
	json += "],\n";

	// Wall enabled tilesets
	json += "  \"wall_enabled\": [";
	first = true;
	for (const wxString &ts : m_wallEnabledTilesets) {
		if (!first) {
			json += ", ";
		}
		json += "\"" + ts + "\"";
		first = false;
	}
	json += "]\n";

	json += "}\n";

	wxFile file;
	if (file.Create(configFile, true)) { // true = overwrite
		file.Write(json);
		file.Close();
	}
}

void BorderEditorPanel::LoadFilterConfig() {
	// Ensure clean state
	m_borderEnabledTilesets.clear();
	m_groundEnabledTilesets.clear();
	m_wallEnabledTilesets.clear();

	wxString configDir = wxFileName::GetHomeDir() + wxFileName::GetPathSeparator() + ".rme";
	wxString configFile = configDir + wxFileName::GetPathSeparator() + "border_editor_filters_v2.json";

	if (!wxFileExists(configFile)) {
		// No config file - whitelists stay empty (default: no filter applied / clean state)
		// This ensures "Addon and Quest Items" is NOT selected by default
		return;
	}

	wxFile file(configFile, wxFile::read);
	if (!file.IsOpened()) {
		return;
	}

	wxString content;
	file.ReadAll(&content);
	file.Close();

	// Simple JSON parsing for our specific format
	auto parseArray = [&content](const wxString &key) -> std::set<wxString> {
		std::set<wxString> result;
		int startPos = content.Find("\"" + key + "\": [");
		if (startPos == wxNOT_FOUND) {
			return result;
		}

		int arrayStart = content.find('[', startPos);
		int arrayEnd = content.find(']', arrayStart);
		if (arrayStart == wxNOT_FOUND || arrayEnd == wxNOT_FOUND) {
			return result;
		}

		wxString arrayContent = content.Mid(arrayStart + 1, arrayEnd - arrayStart - 1);

		// Parse quoted strings
		int pos = 0;
		while ((pos = arrayContent.find('"', pos)) != wxNOT_FOUND) {
			int endQuote = arrayContent.find('"', pos + 1);
			if (endQuote == wxNOT_FOUND) {
				break;
			}
			result.insert(arrayContent.Mid(pos + 1, endQuote - pos - 1));
			pos = endQuote + 1;
		}
		return result;
	};

	m_borderEnabledTilesets = parseArray("border_enabled");
	m_groundEnabledTilesets = parseArray("ground_enabled");
	m_wallEnabledTilesets = parseArray("wall_enabled");
}

// ============================================================================
// WallGridPanel - Visual Selector for Wall Types
// ============================================================================

BEGIN_EVENT_TABLE(WallGridPanel, wxPanel)
EVT_PAINT(WallGridPanel::OnPaint)
EVT_LEFT_DOWN(WallGridPanel::OnMouseClick)
END_EVENT_TABLE()

WallGridPanel::WallGridPanel(wxWindow* parent, wxWindowID id) :
	wxPanel(parent, id, wxDefaultPosition, wxDefaultSize) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
}

WallGridPanel::~WallGridPanel() {
}

void WallGridPanel::SetWallTypes(const std::map<std::string, WallTypeData> &types) {
	m_types = types;
	Refresh();
}

void WallGridPanel::SetSelectedType(const std::string &type) {
	m_selectedType = type;
	Refresh();
}

void WallGridPanel::OnPaint(wxPaintEvent &event) {
	wxAutoBufferedPaintDC dc(this);

	// Background
	dc.SetBackground(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE)));
	dc.Clear();

	wxRect rect = GetClientRect();
	int w = rect.GetWidth();
	int h = rect.GetHeight();

	// Calculate 3x3 grid metrics centered
	int padding = 2;
	int cellSize = std::min((w - 2 * 5) / 3, (h - 2 * 5) / 3);
	if (cellSize < 32) {
		cellSize = 32;
	}

	int startX = (w - (cellSize * 3)) / 2;
	int startY = (h - (cellSize * 3)) / 2;

	dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT)));
	dc.SetBrush(*wxTRANSPARENT_BRUSH);

	// Draw Grid (3x3)
	for (int i = 0; i <= 3; i++) {
		dc.DrawLine(startX + i * cellSize, startY, startX + i * cellSize, startY + 3 * cellSize);
		dc.DrawLine(startX, startY + i * cellSize, startX + 3 * cellSize, startY + i * cellSize);
	}

	auto drawTypeAt = [&](const std::string &type, int gridX, int gridY) {
		int x = startX + gridX * cellSize + padding;
		int y = startY + gridY * cellSize + padding;
		int size = cellSize - 2 * padding;

		bool isSelected = (m_selectedType == type);

		if (isSelected) {
			dc.SetPen(wxPen(wxColour(0, 120, 215), 2));
			dc.DrawRectangle(x - padding, y - padding, cellSize, cellSize);
		}

		if (m_types.find(type) != m_types.end() && !m_types[type].items.empty()) {
			uint16_t itemId = m_types[type].items[0].itemId;
			if (itemId > 0) {
				const ItemType &it = g_items.getItemType(itemId);
				if (it.id != 0) {
					Sprite* s = g_gui.gfx.getSprite(it.clientID);
					if (s) {
						wxMemoryDC tempDC;
						wxBitmap tempBitmap(32, 32);
						tempDC.SelectObject(tempBitmap);
						tempDC.SetBackground(*wxTRANSPARENT_BRUSH);
						tempDC.Clear();
						s->DrawTo(&tempDC, SPRITE_SIZE_32x32_SCALED, 0, 0);
						tempDC.SelectObject(wxNullBitmap);

						wxImage img = tempBitmap.ConvertToImage();
						if (img.IsOk()) {
							img.Rescale(size, size, wxIMAGE_QUALITY_NEAREST);
							dc.DrawBitmap(wxBitmap(img), x, y, true);
						}
					}
				}
			}
		}
	};

	// 3x3 Mapping (User configured)
	// Row 0: Pole, horizontal, horizontal (Swapped Corner/Pole)
	drawTypeAt("pole", 0, 0);
	drawTypeAt("horizontal", 1, 0);
	drawTypeAt("horizontal", 2, 0);

	// Row 1: vertical, (empty), vertical
	drawTypeAt("vertical", 0, 1);
	// (1,1) is empty/vazio
	drawTypeAt("vertical", 2, 1);

	// Row 2: vertical, horizontal, Corner (Swapped Corner/Pole)
	drawTypeAt("vertical", 0, 2);
	drawTypeAt("horizontal", 1, 2);
	drawTypeAt("corner", 2, 2);
}

void WallGridPanel::OnMouseClick(wxMouseEvent &event) {
	wxRect rect = GetClientRect();
	int w = rect.GetWidth();
	int h = rect.GetHeight();

	int cellSize = std::min((w - 2 * 5) / 3, (h - 2 * 5) / 3);
	if (cellSize < 32) {
		cellSize = 32;
	}

	int startX = (w - (cellSize * 3)) / 2;
	int startY = (h - (cellSize * 3)) / 2;

	int mouseX = event.GetX();
	int mouseY = event.GetY();

	if (mouseX >= startX && mouseX < startX + 3 * cellSize && mouseY >= startY && mouseY < startY + 3 * cellSize) {

		int gridX = (mouseX - startX) / cellSize;
		int gridY = (mouseY - startY) / cellSize;

		std::string clickedType = "";

		// Row 0
		if (gridX == 0 && gridY == 0) {
			clickedType = "pole"; // Swapped
		} else if (gridX == 1 && gridY == 0) {
			clickedType = "horizontal";
		} else if (gridX == 2 && gridY == 0) {
			clickedType = "horizontal";
		}

		// Row 1
		else if (gridX == 0 && gridY == 1) {
			clickedType = "vertical";
		}
		// (1,1) is empty
		else if (gridX == 2 && gridY == 1) {
			clickedType = "vertical";
		}

		// Row 2
		else if (gridX == 0 && gridY == 2) {
			clickedType = "vertical";
		} else if (gridX == 1 && gridY == 2) {
			clickedType = "horizontal";
		} else if (gridX == 2 && gridY == 2) {
			clickedType = "corner"; // Swapped
		}

		if (!clickedType.empty()) {
			m_selectedType = clickedType;
			Refresh();

			wxCommandEvent evt(wxEVT_COMMAND_BUTTON_CLICKED, GetId());
			evt.SetString(clickedType);
			evt.SetEventObject(this);

			// Find parent dialog and call callback directly
			wxWindow* parent = GetParent();
			while (parent && !dynamic_cast<BorderEditorPanel*>(parent)) {
				parent = parent->GetParent();
			}

			if (BorderEditorPanel* dialog = dynamic_cast<BorderEditorPanel*>(parent)) {
				dialog->OnWallGridSelect(evt);
			}
		}
	}
}

// ============================================================================
// WallVisualPanel

BEGIN_EVENT_TABLE(WallVisualPanel, wxPanel)
EVT_PAINT(WallVisualPanel::OnPaint)
END_EVENT_TABLE()

WallVisualPanel::WallVisualPanel(wxWindow* parent, wxWindowID id) :
	wxPanel(parent, id, wxDefaultPosition, wxDefaultSize) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetWindowStyle(GetWindowStyle() | wxFULL_REPAINT_ON_RESIZE);
}

WallVisualPanel::~WallVisualPanel() {
}

void WallVisualPanel::SetWallItems(const std::map<std::string, WallTypeData> &items) {
	m_items = items;
	Refresh();
}

void WallVisualPanel::Clear() {
	m_items.clear();
	Refresh();
}

void WallVisualPanel::OnPaint(wxPaintEvent &event) {
	wxAutoBufferedPaintDC dc(this);

	// Get panel dimensions
	wxRect rect = GetClientRect();
	int panelWidth = rect.GetWidth();
	int panelHeight = rect.GetHeight();

	// Use parent's background
	if (GetParent()) {
		dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
	} else {
		dc.SetBackground(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE)));
	}
	dc.Clear();

	// 5x5 Test Pattern
	const int GRID_SIZE = 5;
	const int TITLE_HEIGHT = 20;
	const int MARGIN = 5;

	int availableWidth = panelWidth - 2 * MARGIN;
	int availableHeight = panelHeight - TITLE_HEIGHT - 2 * MARGIN;

	if (availableWidth <= 0 || availableHeight <= 0) {
		return;
	}

	// Calculate cell size
	int cellSize = std::min(availableWidth, availableHeight) / GRID_SIZE;
	if (cellSize < 16) {
		cellSize = 16;
	}

	// Limits
	if (cellSize > 64) {
		cellSize = 64;
	}

	int totalGridSize = GRID_SIZE * cellSize;

	// Center grid
	int startX = (panelWidth - totalGridSize) / 2;
	int startY = TITLE_HEIGHT + (availableHeight - totalGridSize) / 2;
	if (startY < TITLE_HEIGHT) {
		startY = TITLE_HEIGHT;
	}

	// Draw "Preview" label
	dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
	wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
	font.MakeBold();
	dc.SetFont(font);

	wxString label = "Preview";
	wxSize textSize = dc.GetTextExtent(label);
	dc.DrawText(label, (panelWidth - textSize.GetWidth()) / 2, 2);

	// Background border
	const int BORDER_WIDTH = 1;
	wxRect gridBgRect(startX - BORDER_WIDTH, startY - BORDER_WIDTH, totalGridSize + 2 * BORDER_WIDTH, totalGridSize + 2 * BORDER_WIDTH);
	dc.SetBrush(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE)));
	dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT)));
	dc.DrawRectangle(gridBgRect);

	// Draw 5x5 grid lines
	dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT)));
	for (int i = 0; i <= GRID_SIZE; i++) {
		dc.DrawLine(startX + i * cellSize, startY, startX + i * cellSize, startY + totalGridSize);
		dc.DrawLine(startX, startY + i * cellSize, startX + totalGridSize, startY + i * cellSize);
	}

	// Mapping Logic: Grid Position -> Wall Type
	auto getWallTypeAt = [](int x, int y) -> std::string {
		// Layout Configured by User:
		// R0: Pole, H, H, H, H
		// R1: V, ., ., ., V
		// R2: V, ., ., ., V
		// R3: V, ., ., ., V
		// R4: V, ., ., ., Corner

		if (y == 0) {
			if (x == 0) {
				return "pole";
			}
			return "horizontal"; // x=1..4
		} else if (y >= 1 && y <= 3) {
			if (x == 0) {
				return "vertical";
			}
			if (x == 4) {
				return "vertical";
			}
			return ""; // Middle is empty
		} else if (y == 4) {
			if (x == 0) {
				return "vertical";
			}
			if (x == 4) {
				return "corner";
			}
			return "horizontal"; // Middle is horizontal
		}
		return "";
	};

	const int spritePadding = 2;

	for (int y = 0; y < GRID_SIZE; y++) {
		for (int x = 0; x < GRID_SIZE; x++) {
			std::string typeName = getWallTypeAt(x, y);
			if (typeName.empty()) {
				continue;
			}

			// Draw item
			if (m_items.find(typeName) != m_items.end() && !m_items[typeName].items.empty()) {
				uint16_t itemId = m_items[typeName].items[0].itemId;
				if (itemId > 0) {
					int drawX = startX + x * cellSize;
					int drawY = startY + y * cellSize;

					const ItemType &itemType = g_items.getItemType(itemId);
					if (itemType.id != 0) {
						Sprite* sprite = g_gui.gfx.getSprite(itemType.clientID);
						if (sprite) {
							// Correctly scale 32x32 sprite to cell size
							int targetW = cellSize - 2 * spritePadding;
							int targetH = cellSize - 2 * spritePadding;

							// Draw internal uses raw coordinates, we can use DrawTo with scaling if supported
							// Or render to bitmap and scale.
							// Using the previous method: render to bitmap then draw
							wxMemoryDC tempDC;
							wxBitmap tempBitmap(32, 32);
							tempDC.SelectObject(tempBitmap);
							tempDC.SetBackground(*wxTRANSPARENT_BRUSH);
							tempDC.Clear();
							sprite->DrawTo(&tempDC, SPRITE_SIZE_32x32_SCALED, 0, 0);
							tempDC.SelectObject(wxNullBitmap);

							wxImage img = tempBitmap.ConvertToImage();
							if (img.IsOk()) {
								img.Rescale(targetW, targetH, wxIMAGE_QUALITY_NEAREST);
								dc.DrawBitmap(wxBitmap(img), drawX + spritePadding, drawY + spritePadding, true);
							}
						}
					}
				}
			}
		}
	}
}

// ============================================================================
// GroundGridPanel (Visual Copy of BorderGridPanel)
// ============================================================================

BEGIN_EVENT_TABLE(GroundGridPanel, wxPanel)
EVT_PAINT(GroundGridPanel::OnPaint)
EVT_LEFT_DOWN(GroundGridPanel::OnMouseClick)
EVT_LEFT_DCLICK(GroundGridPanel::OnLeftDClick)
EVT_RIGHT_DOWN(GroundGridPanel::OnRightClick)
END_EVENT_TABLE()

GroundGridPanel::GroundGridPanel(wxWindow* parent, wxWindowID id) :
	wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE),
	m_itemId(0),
	m_chance(100),
	m_isSelected(false) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
}

// ============================================================================
// ChanceEditDialog - Modal for editing item chance
// ============================================================================
class ChanceEditDialog : public wxDialog {
public:
	ChanceEditDialog(wxWindow* parent, int initialValue) :
		wxDialog(parent, wxID_ANY, "Chance", wxDefaultPosition, wxSize(200, 120)) {
		wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

		m_spinCtrl = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(100, -1));
		m_spinCtrl->SetRange(1, 1000000);
		m_spinCtrl->SetValue(initialValue);

		// Center the spin control horizontally
		wxBoxSizer* centerSizer = new wxBoxSizer(wxHORIZONTAL);
		centerSizer->Add(m_spinCtrl, 1, wxALIGN_CENTER);
		mainSizer->Add(centerSizer, 1, wxEXPAND | wxALL, 10);

		// Buttons
		wxStdDialogButtonSizer* btnSizer = new wxStdDialogButtonSizer();
		btnSizer->AddButton(new wxButton(this, wxID_OK));
		btnSizer->AddButton(new wxButton(this, wxID_CANCEL));
		btnSizer->Realize();
		mainSizer->Add(btnSizer, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 10);

		SetSizer(mainSizer);
		Layout();
		CenterOnParent();

		m_spinCtrl->SetFocus();
		m_spinCtrl->SetSelection(-1, -1); // Select all text
	}

	int GetValue() const {
		return m_spinCtrl->GetValue();
	}

private:
	wxSpinCtrl* m_spinCtrl;
};

// ============================================================================
// GroundPreviewPanel
// ============================================================================

BEGIN_EVENT_TABLE(GroundPreviewPanel, wxPanel)
EVT_PAINT(GroundPreviewPanel::OnPaint)
EVT_TIMER(wxID_ANY, GroundPreviewPanel::OnTimer)
END_EVENT_TABLE()

GroundPreviewPanel::GroundPreviewPanel(wxWindow* parent, wxWindowID id) :
	wxPanel(parent, id, wxDefaultPosition, wxSize(110, 110), wxNO_BORDER),
	m_currentIndex(0),
	m_animationTimer(this),
	m_lastCycleTime(0) {
	// Use system default background
	m_animationTimer.Start(50); // 20 FPS
}

GroundPreviewPanel::~GroundPreviewPanel() {
	if (m_animationTimer.IsRunning()) {
		m_animationTimer.Stop();
	}
}

void GroundPreviewPanel::SetWeightedItems(const std::vector<std::pair<uint16_t, int>> &items) {
	m_displayItems.clear();
	for (const auto &pair : items) {
		if (pair.first != 0) {
			m_displayItems.push_back(pair.first);
		}
	}
	m_currentIndex = 0;
	Refresh();
}

void GroundPreviewPanel::UpdateFromContainer(GroundGridContainer* container) {
	if (container) {
		SetWeightedItems(container->GetAllItemsWithChance());
	}
}

void GroundPreviewPanel::OnTimer(wxTimerEvent &event) {
	// 1. Handle Animation Refresh (Redraw every tick)
	Refresh();

	// 2. Handle Item Cycling (Every 1000ms)
	long currentTime = wxGetLocalTimeMillis().GetValue();
	if (currentTime - m_lastCycleTime >= 1000) {
		if (!m_displayItems.empty()) {
			m_currentIndex = (m_currentIndex + 1) % m_displayItems.size();
		}
		m_lastCycleTime = currentTime;
	}
}

void GroundPreviewPanel::OnPaint(wxPaintEvent &event) {
	wxBufferedPaintDC dc(this);
	// Use parent's background to be seamless
	if (GetParent()) {
		dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
	} else {
		dc.SetBackground(wxBrush(GetBackgroundColour()));
	}
	dc.Clear();

	if (m_displayItems.empty()) {
		return;
	}

	// Safety check for index
	if (m_currentIndex >= m_displayItems.size()) {
		m_currentIndex = 0;
	}

	uint16_t itemId = m_displayItems[m_currentIndex];
	if (itemId == 0) {
		return;
	}

	const ItemType &itemType = g_items.getItemType(itemId);
	if (itemType.id == 0) {
		return;
	}

	Sprite* sprite = g_gui.gfx.getSprite(itemType.clientID);
	if (!sprite) {
		return;
	}

	int panelW, panelH;
	GetSize(&panelW, &panelH);

	// Draw scaled sprite
	// Create temp bitmap 32x32
	wxMemoryDC tempDC;
	wxBitmap tempBitmap(32, 32);
	tempDC.SelectObject(tempBitmap);
	tempDC.SetBackground(*wxTRANSPARENT_BRUSH); // Transparent background
	tempDC.Clear();

	// Draw sprite to temp DC (0,0)
	sprite->DrawTo(&tempDC, SPRITE_SIZE_32x32, 0, 0, 32, 32);
	tempDC.SelectObject(wxNullBitmap);

	// Convert to image and rescale
	wxImage img = tempBitmap.ConvertToImage();
	if (img.IsOk()) {
		// Scale to fit panel minus padding
		int padding = 4;
		int targetSize = std::min(panelW, panelH) - (2 * padding);
		if (targetSize < 32) {
			targetSize = 32;
		}

		img.Rescale(targetSize, targetSize, wxIMAGE_QUALITY_NEAREST);
		wxBitmap scaledBmp(img);

		// Center draw
		int x = (panelW - targetSize) / 2;
		int y = (panelH - targetSize) / 2;
		dc.DrawBitmap(scaledBmp, x, y, true);
	}
}

GroundGridPanel::~GroundGridPanel() { }

void GroundGridPanel::SetItemId(uint16_t id) {
	m_itemId = id;
	Refresh();
}

void GroundGridPanel::Clear() {
	m_itemId = 0;
	m_chance = 100;
	SetSelected(false);
	Refresh();
}

void GroundGridPanel::SetSelected(bool selected) {
	if (m_isSelected != selected) {
		m_isSelected = selected;
		Refresh();
	}
}

void GroundGridPanel::SetChance(int chance) {
	m_chance = chance;
}

wxSize GroundGridPanel::DoGetBestSize() const {
	// 64px cell + 5px margin on each side = 74x74
	return wxSize(74, 74);
}

void GroundGridPanel::OnLeftDClick(wxMouseEvent &event) {
	if (m_itemId == 0) {
		return; // Ignore empty cells
	}

	ChanceEditDialog dlg(this, m_chance);
	if (dlg.ShowModal() == wxID_OK) {
		int newChance = dlg.GetValue();
		if (newChance != m_chance) {
			m_chance = newChance;
			Refresh(); // Redraw (maybe show tooltip with chance?)
			// We could refresh tooltip if we had one
			SetToolTip(wxString::Format("Item ID: %d\nChance: %d", m_itemId, m_chance));
		}
	}
}

void GroundGridPanel::OnMouseClick(wxMouseEvent &event) {
	wxSize size = GetClientSize();
	int w = size.GetWidth();
	int h = size.GetHeight();
	int margin = 5;

	// Use fixed size for individual cells to avoid excessive spacing
	int cellSize = 64;

	// Start at top-left with margin (documentation standard)
	int gridX = margin;
	int gridY = margin;

	int clickX = event.GetX();
	int clickY = event.GetY();

	// Check if click is within the grid cell
	if (clickX >= gridX && clickX < gridX + cellSize && clickY >= gridY && clickY < gridY + cellSize) {

		// Get the currently selected brush from the GUI
		Brush* brush = g_gui.GetCurrentBrush();
		if (brush && brush->isRaw()) {
			RAWBrush* rawBrush = dynamic_cast<RAWBrush*>(brush);
			if (rawBrush) {
				m_itemId = rawBrush->getItemID();
				Refresh();

				// No auto-append: a nova variação só é criada via botão "Add Variation"
			}
		}
	}
}
void GroundGridPanel::OnRightClick(wxMouseEvent &event) {
	wxSize size = GetClientSize();
	int w = size.GetWidth();
	int h = size.GetHeight();
	int margin = 5;

	// Fixed cell size matching OnPaint
	int cellSize = 64;

	// Start at top-left with margin (matching OnPaint)
	int gridX = margin;
	int gridY = margin;

	int clickX = event.GetX();
	int clickY = event.GetY();

	// Check if click is within the grid cell
	if (clickX >= gridX && clickX < gridX + cellSize && clickY >= gridY && clickY < gridY + cellSize) {

		// If the cell is not empty, request removal from container
		if (!IsEmpty()) {
			wxWindow* parent = GetParent();
			GroundGridContainer* container = dynamic_cast<GroundGridContainer*>(parent);
			if (container) {
				container->RemoveCell(this);
			}
		}
	}
}
void GroundGridPanel::OnPaint(wxPaintEvent &event) {
	wxAutoBufferedPaintDC dc(this);

	// Use system background color like Border Editor
	dc.SetBackground(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE)));
	dc.Clear();

	wxSize size = GetClientSize();
	int w = size.GetWidth();
	int h = size.GetHeight();
	int margin = 5;

	// Match sizing of individual grid cells (standard 64px)
	int cellSize = 64;

	// Start at top-left with margin (documentation standard)
	int x = margin;
	int y = margin;

	// Use GraphicsContext for opacity (75% = 191 alpha)
	std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
	if (gc) {
		gc->SetAntialiasMode(wxANTIALIAS_NONE);

		// Draw grid lines (like Border Editor) with 75% opacity
		wxColour grayText = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
		gc->SetPen(wxPen(wxColour(grayText.Red(), grayText.Green(), grayText.Blue(), 191)));
		gc->SetBrush(*wxTRANSPARENT_BRUSH);

		// Draw 1x1 grid (rectangle)
		gc->DrawRectangle(x, y, cellSize, cellSize);

		// Draw the assigned item sprite if set
		if (m_itemId > 0) {
			const ItemType &type = g_items.getItemType(m_itemId);
			if (type.id != 0) {
				Sprite* sprite = g_gui.gfx.getSprite(type.clientID);
				if (sprite) {
					// Sprite rendering with scaling to fit cell
					const int spritePadding = 2;
					const int spriteSize = cellSize - 2 * spritePadding;

					wxMemoryDC tempDC;
					wxBitmap tempBitmap(32, 32);
					tempDC.SelectObject(tempBitmap);
					tempDC.SetBackground(*wxTRANSPARENT_BRUSH);
					tempDC.Clear();

					sprite->DrawTo(&tempDC, SPRITE_SIZE_32x32, 0, 0);
					tempDC.SelectObject(wxNullBitmap);

					wxImage img = tempBitmap.ConvertToImage();
					if (img.IsOk()) {
						img.Rescale(spriteSize, spriteSize, wxIMAGE_QUALITY_NEAREST);
						wxBitmap scaledBmp(img);

						// Draw bitmap with 75% opacity
						gc->DrawBitmap(scaledBmp, x + spritePadding, y + spritePadding, spriteSize, spriteSize);
					}
				}
			}
		}
	} else {
		// Fallback to normal DC if GC fails
		dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT)));
		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.DrawRectangle(x, y, cellSize, cellSize);

		if (m_itemId > 0) {
			const ItemType &type = g_items.getItemType(m_itemId);
			if (type.id != 0) {
				Sprite* sprite = g_gui.gfx.getSprite(type.clientID);
				if (sprite) {
					const int spritePadding = 2;
					const int spriteSize = cellSize - 2 * spritePadding;

					wxMemoryDC tempDC;
					wxBitmap tempBitmap(32, 32);
					tempDC.SelectObject(tempBitmap);
					tempDC.SetBackground(*wxTRANSPARENT_BRUSH);
					tempDC.Clear();

					sprite->DrawTo(&tempDC, SPRITE_SIZE_32x32, 0, 0);
					tempDC.SelectObject(wxNullBitmap);

					wxImage img = tempBitmap.ConvertToImage();
					if (img.IsOk()) {
						img.Rescale(spriteSize, spriteSize, wxIMAGE_QUALITY_NEAREST);
						wxBitmap scaledBmp(img);
						dc.DrawBitmap(scaledBmp, x + spritePadding, y + spritePadding, true);
					}
				}
			}
		}
	}

	// Draw Chance Value Overlay (if item exists)
	if (m_itemId > 0) {
		wxString chanceText = wxString::Format("%d", m_chance);
		wxFont font(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
		dc.SetFont(font);

		wxSize textSize = dc.GetTextExtent(chanceText);
		// Position at bottom-right of the cell
		int textX = x + cellSize - textSize.GetWidth() - 4;
		int textY = y + cellSize - textSize.GetHeight() - 4;

		// Draw shadow (black)
		dc.SetTextForeground(*wxBLACK);
		dc.DrawText(chanceText, textX + 1, textY + 1);

		// Draw text (white)
		dc.SetTextForeground(*wxWHITE);
		dc.DrawText(chanceText, textX, textY);
	}

	// Draw selection border if selected
	if (m_isSelected) {
		dc.SetPen(wxPen(*wxBLUE, 3)); // Thick blue border
		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.DrawRectangle(x, y, cellSize, cellSize);
	}
}

// ============================================================================
// GroundGridContainer - Manages multiple GroundGridPanel cells with auto-expansion
// ============================================================================

GroundGridContainer::GroundGridContainer(wxWindow* parent, wxWindowID id) :
	wxScrolledWindow(parent, id, wxDefaultPosition, wxDefaultSize, wxHSCROLL | wxBORDER_NONE),
	m_gridSizer(nullptr),
	m_selectedCell(nullptr) {
	SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE));

	// Create horizontal sizer for the grid cells
	m_gridSizer = new wxBoxSizer(wxHORIZONTAL);
	SetSizer(m_gridSizer);

	// Set scroll rate
	SetScrollRate(10, 0); // Only horizontal scrolling

	// Create first empty cell
	CreateNewCell();
}

GroundGridContainer::~GroundGridContainer() {
	m_gridCells.clear();
}

void GroundGridContainer::CreateNewCell() {
	GroundGridPanel* cell = new GroundGridPanel(this);
	cell->SetMinSize(wxSize(74, 74)); // 64px cell + 10px margins

	m_gridSizer->Add(cell, 0, wxALL | wxALIGN_TOP, 1);
	m_gridCells.push_back(cell);

	FitInside();
	Layout();
}

void GroundGridContainer::EnsureEmptyCell() {
	if (m_gridCells.empty()) {
		CreateNewCell();
	}
}

void GroundGridContainer::AppendEmptyCell() {
	CreateNewCell();
	FitInside();
	Layout();
	Refresh();
}

void GroundGridContainer::RemoveCell(GroundGridPanel* cell) {
	if (!cell) {
		return;
	}

	// Don't remove the only cell if it's already empty
	if (m_gridCells.size() == 1 && cell->IsEmpty()) {
		return;
	}

	// Find the cell in our list
	auto it = std::find(m_gridCells.begin(), m_gridCells.end(), cell);
	if (it != m_gridCells.end()) {
		// Remove from sizer
		m_gridSizer->Detach(cell);

		if (m_selectedCell == cell) {
			m_selectedCell = nullptr;
			// Notify dialog validation?
		}

		// Remove from our list
		m_gridCells.erase(it);

		// Destroy the window
		cell->Destroy();

		// Ensure we still have an empty cell at the end
		EnsureEmptyCell();

		// Refresh layout
		FitInside();
		Layout();
		Refresh();

		// Notify parent that content changed
		wxCommandEvent event(wxEVT_GROUND_CONTAINER_CHANGE, GetId());
		event.SetEventObject(this);
		wxPostEvent(GetParent(), event);
	}
}

void GroundGridContainer::AddItem(uint16_t itemId, int chance, bool allowAppend) {
	if (itemId == 0) {
		return;
	}

	for (GroundGridPanel* cell : m_gridCells) {
		if (cell->IsEmpty()) {
			cell->SetItemId(itemId);
			cell->SetChance(chance);
			return;
		}
	}

	if (!allowAppend) {
		return;
	}

	CreateNewCell();
	GroundGridPanel* last = m_gridCells.back();
	last->SetItemId(itemId);
	last->SetChance(chance);
}

void GroundGridContainer::Clear() {
	while (m_gridCells.size() > 1) {
		GroundGridPanel* cell = m_gridCells.back();
		m_gridSizer->Detach(cell);
		cell->Destroy();
		m_gridCells.pop_back();
	}

	if (!m_gridCells.empty()) {
		m_gridCells[0]->Clear();
	}

	FitInside();
	Layout();
}

std::vector<uint16_t> GroundGridContainer::GetAllItems() const {
	std::vector<uint16_t> items;
	for (const GroundGridPanel* cell : m_gridCells) {
		if (!cell->IsEmpty()) {
			items.push_back(cell->GetItemId());
		}
	}
	return items;
}

std::vector<std::pair<uint16_t, int>> GroundGridContainer::GetAllItemsWithChance() const {
	std::vector<std::pair<uint16_t, int>> items;
	for (const GroundGridPanel* cell : m_gridCells) {
		if (!cell->IsEmpty()) {
			items.push_back({ cell->GetItemId(), cell->GetChance() });
		}
	}
	return items;
}

size_t GroundGridContainer::GetFilledCount() const {
	size_t count = 0;
	for (const GroundGridPanel* cell : m_gridCells) {
		if (!cell->IsEmpty()) {
			count++;
		}
	}
	return count;
}

void GroundGridContainer::OnCellFilled(int index) {
	EnsureEmptyCell();
}

void GroundGridContainer::RebuildGrids() {
	FitInside();
	Layout();
	Refresh();
}

void GroundGridContainer::SetSelectedCell(GroundGridPanel* cell) {
	if (m_selectedCell != cell) {
		if (m_selectedCell) {
			m_selectedCell->SetSelected(false);
		}
		m_selectedCell = cell;
		if (m_selectedCell) {
			m_selectedCell->SetSelected(true);

			// Try to notify the dialog to update chance control
			// We need to walk up to find the dialog
			wxWindow* win = GetParent();
			while (win) {
				BorderEditorPanel* dialog = dynamic_cast<BorderEditorPanel*>(win);
				if (dialog) {
					wxCommandEvent evt(wxEVT_NULL); // Just update UI call
					// We can't easily fire an event to dialog from here without ID.
					// Let's assume the dialog will poll or we can implement a callback system later if needed.
					// For now, let's just make sure m_groundItemChanceCtrl updates when user interacts.
					// Actually, simpler: Fire a custom event or reuse an existing one.
					// We can reuse wxEVT_COMMAND_LISTBOX_SELECTED on the container? No.

					// Direct update is easiest if we can access public method.
					// But we don't have public access.
					// Let's use a dummy event to trigger UI update.
					wxCommandEvent event(wxEVT_BUTTON, wxID_ANY);
					event.SetEventObject(this);
					// dialog->OnGridSelectionChanged(event); // If we had it.

					// Let's implement OnGridCellClicked properly in Dialog to handle this.
					// But Wait, GroundGridPanel::OnMouseClick is calling SetSelectedCell.
					// We should dispatch an event from container to parent.

					wxCommandEvent selEvent(wxEVT_COMMAND_LISTBOX_SELECTED, GetId());
					selEvent.SetEventObject(this);
					selEvent.SetInt(cell->GetChance()); // Pass chance as int
					win->GetEventHandler()->ProcessEvent(selEvent); // Send to parent (StaticBox) -> then to Dialog
					break;
				}
				win = win->GetParent();
			}
		}
	}
}

// DISABLE Old Event Handlers
void BorderEditorPanel::OnAddVariation(wxCommandEvent &event) {
	if (m_activeTab != 1) {
		return;
	}
	if (!m_groundGridContainer) {
		return;
	}

	m_groundGridContainer->AppendEmptyCell();
}
void BorderEditorPanel::OnRemoveVariationBtn(wxCommandEvent &event) { }
void BorderEditorPanel::OnPreviewTimer(wxTimerEvent &event) { }
void BorderEditorPanel::RebuildVariationsList() { }
void BorderEditorPanel::AddVariationRow(GroundVariation &variation, int index) { }

void BorderEditorPanel::OnGroundPaletteSelect(wxCommandEvent &event) {
	// When an item is selected from the palette, add it to the container
	uint16_t itemId = static_cast<uint16_t>(event.GetInt());
	if (itemId > 0 && m_groundGridContainer) {
		m_groundGridContainer->AddItem(itemId, 100, false);
		if (m_groundPreviewPanel) {
			m_groundPreviewPanel->UpdateFromContainer(m_groundGridContainer);
		}
		SaveCurrentDraft(true);
	}
}

void BorderEditorPanel::OnGroundContainerChange(wxCommandEvent &event) {
	if (m_groundPreviewPanel && m_groundGridContainer) {
		m_groundPreviewPanel->UpdateFromContainer(m_groundGridContainer);
	}
	SaveCurrentDraft(true);
}

void BorderEditorPanel::SaveGroundBrush() {
	// Validate input
	wxString name = m_groundNameCtrl->GetValue().Trim();
	if (name.IsEmpty()) {
		ShowErrorDialog("Please enter a name for the ground brush.", ErrorSeverity::Warning);
		return;
	}

	std::vector<std::pair<uint16_t, int>> items = m_groundGridContainer ? m_groundGridContainer->GetAllItemsWithChance() : std::vector<std::pair<uint16_t, int>>();
	if (items.empty()) {
		ShowErrorDialog("Please add at least one item to the ground brush by clicking on tiles in the palette.", ErrorSeverity::Warning);
		return;
	}

	int preSave = wxMessageBox(
		"This action will save the ground brush and reload data files (F5). Continue?",
		"Confirm Save",
		wxYES_NO | wxICON_QUESTION | wxSTAY_ON_TOP,
		this
	);
	if (preSave != wxYES) {
		return;
	}

	// Get form values
	const uint16_t mainTileId = (m_selectedBrowserTileId != 0)
		? m_selectedBrowserTileId
		: (!items.empty() ? items.front().first : 0);
	int zOrder = m_zOrderCtrl->GetValue();
	wxString alignment = (m_borderAlignmentSelection == 1) ? "inner" : "outer";
	bool includeToNone = m_includeToNoneCheck->GetValue();
	bool includeInner = m_includeInnerCheck->GetValue();

	// Build XML path
	wxString dataDir = g_gui.GetDataDirectory();
	wxString groundsFile = dataDir + wxFileName::GetPathSeparator() + "materials" + wxFileName::GetPathSeparator() + "brushs" + wxFileName::GetPathSeparator() + "grounds.xml";

	// Load existing file or create new structure
	pugi::xml_document doc;
	pugi::xml_node materials;

	if (wxFileExists(groundsFile)) {
		pugi::xml_parse_result result = doc.load_file(nstr(groundsFile).c_str());
		if (result) {
			materials = doc.child("materials");
		}
	}

	if (!materials) {
		materials = doc.append_child("materials");
	}

	// Check if brush with this name already exists
	pugi::xml_node existingBrush;
	for (pugi::xml_node brushNode = materials.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
		if (wxString(brushNode.attribute("name").as_string()) == name) {
			existingBrush = brushNode;
			break;
		}
	}

	// Create or update brush node
	pugi::xml_node brushNode = existingBrush ? existingBrush : materials.append_child("brush");

	if (!existingBrush) {
		brushNode.append_attribute("type") = "ground";
	}
	brushNode.attribute("name") = nstr(name).c_str();
	if (!brushNode.attribute("name")) {
		brushNode.append_attribute("name") = nstr(name).c_str();
	}

	if (mainTileId != 0) {
		if (brushNode.attribute("server_lookid")) {
			brushNode.attribute("server_lookid") = mainTileId;
		} else {
			brushNode.append_attribute("server_lookid") = mainTileId;
		}
	}

	// Set z-order
	if (brushNode.attribute("z-order")) {
		brushNode.attribute("z-order") = zOrder;
	} else {
		brushNode.append_attribute("z-order") = zOrder;
	}

	// Remove existing item children and add new ones
	while (brushNode.child("item")) {
		brushNode.remove_child("item");
	}

	// Add all items from the container
	int totalScale = 0;

	// First pass: Calculate total weight if needed, though we save raw values usually
	// Canary format usually expects 'chance' as a relative weight or specific probability?
	// Looking at brush loading code: total_chance += chance;
	// So it's cumulative weight. We can save the raw int values user entered.

	for (const auto &itemPair : items) {
		uint16_t itemId = itemPair.first;
		int chance = itemPair.second;

		pugi::xml_node itemNode = brushNode.append_child("item");
		itemNode.append_attribute("id") = itemId;
		itemNode.append_attribute("chance") = chance;
	}

	// Add border node if not exists
	pugi::xml_node borderNode = brushNode.child("border");
	if (!borderNode) {
		borderNode = brushNode.append_child("border");
	}

	// Set border attributes
	if (borderNode.attribute("align")) {
		borderNode.attribute("align") = nstr(alignment).c_str();
	} else {
		borderNode.append_attribute("align") = nstr(alignment).c_str();
	}

	if (includeToNone) {
		if (!borderNode.attribute("to")) {
			borderNode.append_attribute("to") = "none";
		} else {
			borderNode.attribute("to") = "none";
		}
	}

	wxString xmlText;
	if (wxFileExists(groundsFile)) {
		ReadAllText(groundsFile, xmlText);
	}
	if (xmlText.IsEmpty()) {
		xmlText = "<materials>\n</materials>\n";
	}

	const wxString nl = DetectNewline(xmlText);
	const wxString baseIndent = DetectChildIndent(xmlText, "brush");
	const wxString newBlock = PrintNodeIndented(brushNode, baseIndent, nl);
	const wxString attrNeedle = "name=\"" + name + "\"";

	wxString oldText = xmlText;
	if (!ReplaceOrInsertChildSimple(xmlText, "brush", attrNeedle, newBlock, false)) {
		ShowErrorDialog("Invalid grounds.xml file: missing '</materials>'", ErrorSeverity::Error);
		return;
	}

	if (xmlText != oldText && !WriteAllText(groundsFile, xmlText)) {
		ShowErrorDialog("Failed to save ground brush file.", ErrorSeverity::Error);
		return;
	}

	DiscardDraft(1, mainTileId);

	TriggerReloadDataFiles();
	ReloadCurrentBrushEditorXml(wxString::Format("%u", (unsigned)mainTileId));

	ShowErrorDialog(
		wxString::Format("Ground brush '%s' saved successfully.", name),
		ErrorSeverity::Info
	);
}

void BorderEditorPanel::LoadBorderByMainTileId(uint16_t tileId) {
	if (RestoreDraft(0, tileId)) {
		return;
	}

	ClearItems(false);
	if (tileId == 0) {
		return;
	}

	wxString dataDir = g_gui.GetDataDirectory();
	wxString groundsFile = dataDir + wxFileName::GetPathSeparator() + "materials" + wxFileName::GetPathSeparator() + "brushs" + wxFileName::GetPathSeparator() + "grounds.xml";

	if (!wxFileExists(groundsFile)) {
		return;
	}

	pugi::xml_document doc;
	if (!doc.load_file(nstr(groundsFile).c_str())) {
		return;
	}

	pugi::xml_node materials = doc.child("materials");
	if (!materials) {
		return;
	}

	for (pugi::xml_node brushNode = materials.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
		pugi::xml_attribute typeAttr = brushNode.attribute("type");
		if (!typeAttr || std::string(typeAttr.as_string()) != "ground") {
			continue;
		}

		uint16_t mainTileId = 0;
		if (pugi::xml_attribute a = brushNode.attribute("server_lookid")) {
			mainTileId = a.as_uint();
		} else if (pugi::xml_attribute a = brushNode.attribute("lookid")) {
			mainTileId = a.as_uint();
		}

		if (mainTileId != tileId) {
			continue;
		}

		for (pugi::xml_node borderNode = brushNode.child("border"); borderNode; borderNode = borderNode.next_sibling("border")) {
			pugi::xml_attribute idAttr = borderNode.attribute("id");
			if (!idAttr) {
				continue;
			}

			wxString align = wxString(borderNode.attribute("align").as_string());
			if (align.IsEmpty() || align == "outer") {
				LoadBorderById(idAttr.as_int());
				return;
			}
		}

		return;
	}
}

void BorderEditorPanel::LoadGroundBrushByMainTileId(uint16_t tileId) {
	if (RestoreDraft(1, tileId)) {
		return;
	}

	ClearGroundItems(false);
	if (tileId == 0) {
		return;
	}

	wxString dataDir = g_gui.GetDataDirectory();
	wxString groundsFile = dataDir + wxFileName::GetPathSeparator() + "materials" + wxFileName::GetPathSeparator() + "brushs" + wxFileName::GetPathSeparator() + "grounds.xml";

	if (!wxFileExists(groundsFile)) {
		return;
	}

	pugi::xml_document doc;
	if (!doc.load_file(nstr(groundsFile).c_str())) {
		return;
	}

	pugi::xml_node materials = doc.child("materials");
	if (!materials) {
		return;
	}

	for (pugi::xml_node brushNode = materials.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
		pugi::xml_attribute typeAttr = brushNode.attribute("type");
		if (!typeAttr || std::string(typeAttr.as_string()) != "ground") {
			continue;
		}

		pugi::xml_attribute nameAttr = brushNode.attribute("name");
		if (!nameAttr) {
			continue;
		}

		uint16_t mainTileId = 0;
		if (pugi::xml_attribute a = brushNode.attribute("server_lookid")) {
			mainTileId = a.as_uint();
		} else if (pugi::xml_attribute a = brushNode.attribute("lookid")) {
			mainTileId = a.as_uint();
		}

		if (mainTileId == tileId) {
			LoadGroundBrushByName(wxString(nameAttr.as_string()));
			return;
		}
	}
}

void BorderEditorPanel::LoadWallBrushByMainTileId(uint16_t tileId) {
	if (RestoreDraft(2, tileId)) {
		return;
	}

	ClearWallItems(false);
	if (tileId == 0) {
		return;
	}

	wxString dataDir = g_gui.GetDataDirectory();
	wxString wallsFile = dataDir + wxFileName::GetPathSeparator() + "materials" + wxFileName::GetPathSeparator() + "brushs" + wxFileName::GetPathSeparator() + "walls.xml";

	if (!wxFileExists(wallsFile)) {
		return;
	}

	pugi::xml_document doc;
	if (!doc.load_file(nstr(wallsFile).c_str())) {
		return;
	}

	pugi::xml_node materials = doc.child("materials");
	if (!materials) {
		return;
	}

	for (pugi::xml_node brushNode = materials.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
		pugi::xml_attribute nameAttr = brushNode.attribute("name");
		if (!nameAttr) {
			continue;
		}

		pugi::xml_attribute lookIdAttr = brushNode.attribute("server_lookid");
		if (lookIdAttr && lookIdAttr.as_uint() == tileId) {
			LoadWallBrushByName(wxString(nameAttr.as_string()));
			return;
		}
	}
}

void BorderEditorPanel::LoadGroundBrushByName(const wxString &name) {
	if (name.IsEmpty()) {
		return;
	}

	wxString dataDir = g_gui.GetDataDirectory();
	wxString groundsFile = dataDir + wxFileName::GetPathSeparator() + "materials" + wxFileName::GetPathSeparator() + "brushs" + wxFileName::GetPathSeparator() + "grounds.xml";

	if (!wxFileExists(groundsFile)) {
		return;
	}

	pugi::xml_document doc;
	if (!doc.load_file(nstr(groundsFile).c_str())) {
		return;
	}

	ClearGroundItems(false); // Don't clear browser selection logic

	pugi::xml_node materials = doc.child("materials");
	if (!materials) {
		return;
	}

	// Find the brush by name
	for (pugi::xml_node brushNode = materials.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
		pugi::xml_attribute typeAttr = brushNode.attribute("type");
		if (!typeAttr || std::string(typeAttr.as_string()) != "ground") {
			continue;
		}

		pugi::xml_attribute nameAttr = brushNode.attribute("name");
		if (!nameAttr || wxString(nameAttr.as_string()) != name) {
			continue;
		}

		// Found the brush - populate the form
		m_groundNameCtrl->SetValue(name);

		// Z-order
		pugi::xml_attribute zOrderAttr = brushNode.attribute("z-order");
		m_zOrderCtrl->SetValue(zOrderAttr ? zOrderAttr.as_int() : 0);

		// Get first item ID and set in grid
		// Load all items into the container
		if (m_groundGridContainer) {
			m_groundGridContainer->Clear();
			for (pugi::xml_node itemNode = brushNode.child("item"); itemNode; itemNode = itemNode.next_sibling("item")) {
				uint16_t itemId = itemNode.attribute("id").as_uint();
				int chance = itemNode.attribute("chance").as_int();
				if (chance <= 0) {
					chance = 100; // Default
				}

				if (itemId > 0) {
					m_groundGridContainer->AddItem(itemId, chance, true);
				}
			}
		}

		// Border settings
		pugi::xml_node borderNode = brushNode.child("border");
		if (borderNode) {
			wxString align = wxString(borderNode.attribute("align").as_string());
			if (align == "inner") {
				m_borderAlignmentSelection = 1;
				if (m_borderAlignmentButton) {
					m_borderAlignmentButton->SetLabel("Inner \u25BC");
				}
			} else {
				m_borderAlignmentSelection = 0;
				if (m_borderAlignmentButton) {
					m_borderAlignmentButton->SetLabel("Outer \u25BC");
				}
			}

			wxString toValue = wxString(borderNode.attribute("to").as_string());
			m_includeToNoneCheck->SetValue(toValue == "none");
		}

		if (m_groundPreviewPanel && m_groundGridContainer) {
			m_groundPreviewPanel->UpdateFromContainer(m_groundGridContainer);
		}
		break;
	}
}
