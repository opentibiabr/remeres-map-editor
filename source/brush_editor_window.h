// brush_editor_window.h - Provides a visual editor for brushs

#ifndef RME_BRUSH_EDITOR_WINDOW_H_
#define RME_BRUSH_EDITOR_WINDOW_H_

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/listbox.h>
#include <wx/spinctrl.h>
#include <wx/bmpbuttn.h>
#include <wx/artprov.h>
#include <wx/grid.h>
#include <wx/panel.h>
#include <wx/checkbox.h>
#include <wx/combobox.h>
#include <wx/notebook.h>
#include <wx/simplebook.h>
#include <wx/tglbtn.h>
#include <wx/choice.h>
#include <wx/srchctrl.h>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>
#include <wx/treectrl.h>
#include <wx/checklst.h>

// Forward declarations
class BorderItemButton;
class BorderGridPanel;
class GroundGridPanel;
class GroundGridContainer;
class BorderPreviewPanel;
class GroundPreviewPanel;
class WallVisualPanel;
class BrushPalettePanel;
class BrushBrowserGridPanel;

// Represents a border edge position
enum BorderEdgePosition {
	EDGE_NONE = -1,
	EDGE_N, // North
	EDGE_E, // East
	EDGE_S, // South
	EDGE_W, // West
	EDGE_CNW, // Corner Northwest
	EDGE_CNE, // Corner Northeast
	EDGE_CSE, // Corner Southeast
	EDGE_CSW, // Corner Southwest
	EDGE_DNW, // Diagonal Northwest
	EDGE_DNE, // Diagonal Northeast
	EDGE_DSE, // Diagonal Southeast
	EDGE_DSW, // Diagonal Southwest
	EDGE_COUNT
};

// Alignment options for borders
enum BorderAlignment {
	ALIGN_OUTER,
	ALIGN_INNER
};

// Utility function to convert border edge string to position
BorderEdgePosition edgeStringToPosition(const std::string &edgeStr);

// Utility function to convert border position to string
std::string edgePositionToString(BorderEdgePosition pos);

// Represents a border item
struct BorderItem {
	BorderEdgePosition position;
	uint16_t itemId;

	BorderItem() : position(EDGE_NONE), itemId(0) { }
	BorderItem(BorderEdgePosition pos, uint16_t id) : position(pos), itemId(id) { }

	bool operator==(const BorderItem &other) const {
		return position == other.position && itemId == other.itemId;
	}

	bool operator!=(const BorderItem &other) const {
		return !(*this == other);
	}
};

// Represents a ground item with chance
struct GroundItem {
	uint16_t itemId;
	int chance;

	GroundItem() : itemId(0), chance(10) { }
	GroundItem(uint16_t id, int c) : itemId(id), chance(c) { }

	bool operator==(const GroundItem &other) const {
		return itemId == other.itemId && chance == other.chance;
	}

	bool operator!=(const GroundItem &other) const {
		return !(*this == other);
	}
};

// Represents a wall type group (e.g., "vertical", "horizontal")
struct WallTypeData {
	std::string typeName;
	std::vector<GroundItem> items; // Reuse GroundItem as it has {itemId, chance}
};

// Represents a wall item position
struct WallItem {
	int x; // Grid X (0-2)
	int y; // Grid Y (0-2)
	uint16_t itemId;

	WallItem() : x(-1), y(-1), itemId(0) { }
	WallItem(int _x, int _y, uint16_t id) : x(_x), y(_y), itemId(id) { }
};

// Tree item data to store asset info
class BorderAssetItemData : public wxTreeItemData {
public:
	enum AssetType {
		FOLDER,
		BORDER,
		GROUND,
		WALL
	};

	BorderAssetItemData(AssetType type, int id = 0, const wxString &name = "") : m_type(type), m_id(id), m_name(name) { }

	AssetType GetType() const {
		return m_type;
	}
	int GetId() const {
		return m_id;
	}
	wxString GetName() const {
		return m_name;
	}

private:
	AssetType m_type;
	int m_id;
	wxString m_name;
};

// Represents a single tile in a composite variation
struct CompositeTile {
	int x, y;
	uint16_t itemId;

	bool operator<(const CompositeTile &other) const {
		if (x != other.x) {
			return x < other.x;
		}
		return y < other.y;
	}
};

// Represents a variation (composite block)
struct GroundVariation {
	int chance;
	std::vector<CompositeTile> tiles;

	GroundVariation() : chance(10) { }
};

class SimpleRawPalettePanel;

// Dialog for filtering visible tilesets
class TilesetFilterDialog : public wxDialog {
public:
	TilesetFilterDialog(wxWindow* parent, const wxArrayString &allTilesets, const std::set<wxString> &enabledTilesets, const wxString &modeLabel);

	std::set<wxString> GetEnabledTilesets() const;

private:
	wxCheckListBox* m_tilesetList;
	wxArrayString m_allTilesets;

	void OnSelectAll(wxCommandEvent &event);
	void OnSelectNone(wxCommandEvent &event);
	void OnListItemClick(wxCommandEvent &event);
};

wxDECLARE_EVENT(wxEVT_GROUND_CONTAINER_CHANGE, wxCommandEvent);

// Forward declarations
class WallGridPanel;
class WallVisualPanel;

class BrushEditorPanel : public wxPanel {
public:
	BrushEditorPanel(wxWindow* parent);
	virtual ~BrushEditorPanel();

	// Event handlers
	void OnItemIdChanged(wxCommandEvent &event);
	void OnPositionSelected(wxCommandEvent &event);
	void OnAddItem(wxCommandEvent &event);
	void OnClear(wxCommandEvent &event);
	void OnSave(wxCommandEvent &event);
	void OnClose(wxCommandEvent &event);
	void OnBrowse(wxCommandEvent &event);
	void OnLoadBorder(wxCommandEvent &event);
	void OnGridCellClicked(wxMouseEvent &event);
	void OnWallGridSelect(wxCommandEvent &event);
	void OnPageChanged(wxBookCtrlEvent &event);
	void OnAddGroundItem(wxCommandEvent &event);
	void OnRemoveGroundItem(wxCommandEvent &event);
	void OnGroundBrowse(wxCommandEvent &event);
	void OnGroundTilesetSelect(wxCommandEvent &event);

	// Wall events
	void OnWallBrowse(wxCommandEvent &event);

	// Restored Ground events
	void OnGroundPaletteSelect(wxCommandEvent &event);
	void OnOpenTilesetFilter(wxCommandEvent &event);
	void OnGroundGridSelect(wxCommandEvent &event);
	void OnGroundTilesetListSelect(wxCommandEvent &event);

	// Button + Menu Handlers
	void OnRawCategoryButtonClick(wxCommandEvent &event);
	void OnRawCategoryMenuSelect(wxCommandEvent &event);

	void OnGroundTilesetButtonClick(wxCommandEvent &event);
	void OnGroundTilesetMenuSelect(wxCommandEvent &event);

	void OnWallTilesetButtonClick(wxCommandEvent &event);
	void OnWallTilesetMenuSelect(wxCommandEvent &event);

	void OnBorderAlignmentButtonClick(wxCommandEvent &event);
	void OnBorderAlignmentMenuSelect(wxCommandEvent &event);

	void OnAddWallItem(wxCommandEvent &event);
	void OnRemoveWallItem(wxCommandEvent &event);

	// Sidebar events
	void OnAssetTreeSelection(wxTreeEvent &event);
	void OnAssetSearch(wxCommandEvent &event);

	// Browser sidebar events
	void OnBorderBrowserSelection(wxCommandEvent &event);
	void OnBrowserGridSelection(wxCommandEvent &event);
	void OnBrowserSearch(wxCommandEvent &event);
	void OnBrowserTilesetFilterButtonClick(wxCommandEvent &event);

	// Mode switcher events
	void OnModeSwitch(wxCommandEvent &event);

protected:
	void CreateGUIControls();
	void LoadExistingBorders();
	void LoadExistingGroundBrushes();
	void LoadTilesets();
	void SaveBorder();
	void SaveGroundBrush();
	bool ValidateBorder();
	void LoadBorderById(int borderId);
	void LoadGroundBrushByName(const wxString &name);
	void LoadWallBrushByName(const wxString &name);
	bool ValidateGroundBrush();
	bool ValidateWallBrush();
	void LoadExistingWallBrushes();
	void SaveWallBrush();
	void ClearWallItems(bool clearBrowser = true);
	void UpdateWallItemsList();
	void UpdatePreview();
	void ClearItems(bool clearBrowser = true);
	void ClearGroundItems(bool clearBrowser = true);
	void UpdateGroundItemsList();

	// Sidebar helpers
	void PopulateAssetTree();
	void FilterAssetTree(const wxString &query);

	// Browser population methods
	void PopulateBorderList();
	void PopulateGroundList();
	void PopulateWallList();
	void PopulateUnifiedList();
	void FilterBrowserList(const wxString &query);
	void UpdateBrowserLabel();
	bool RestoreBrowserSelection(const wxString &selectionKey);
	void ReloadCurrentBrushEditorXml(const wxString &selectionKey);

	void LoadBorderByMainTileId(uint16_t tileId);
	void LoadGroundBrushByMainTileId(uint16_t tileId);
	void LoadWallBrushByMainTileId(uint16_t tileId);

private:
	struct BorderDraftState {
		bool dirty = false;
		int currentBorderId = 0;
		wxString borderName;
		bool group = false;
		bool optional = false;
		bool ground = false;
		std::vector<BorderItem> items;
	};

	struct GroundDraftState {
		bool dirty = false;
		wxString name;
		int zOrder = 0;
		int borderAlignmentSelection = 0;
		bool includeToNone = true;
		bool includeInner = false;
		std::vector<std::pair<uint16_t, int>> items;
	};

	struct WallDraftState {
		bool dirty = false;
		wxString brushName;
		std::map<std::string, WallTypeData> wallTypes;
		std::string selectedType;
	};

	void SaveDraft(int tab, uint16_t tileId, bool markDirty);
	bool RestoreDraft(int tab, uint16_t tileId);
	void DiscardDraft(int tab, uint16_t tileId);
	void SaveCurrentDraft(bool markDirty);
	void DiscardCurrentDraft();

	void OnBorderDraftChange(wxCommandEvent &event);
	void OnGroundDraftChange(wxCommandEvent &event);
	void OnWallDraftChange(wxCommandEvent &event);

	bool m_isRestoringDraft = false;
	std::unordered_map<uint16_t, BorderDraftState> m_borderDrafts;
	std::unordered_map<uint16_t, GroundDraftState> m_groundDrafts;
	std::unordered_map<uint16_t, WallDraftState> m_wallDrafts;

	// ===== State =====
	int m_maxBorderId;
	int m_nextBorderId;
	int m_currentBorderId; // Currently edited border ID (replaces m_idCtrl)
	int m_activeTab; // 0 = border, 1 = ground, 2 = wall
	uint16_t m_selectedBrowserTileId;

	// Storage for unfiltered browser list items
	wxArrayString m_fullBrowserList;
	wxArrayString m_fullBrowserIds;
	std::vector<uint16_t> m_fullBrowserPreviewIds;

	// Animation timer for preview
	wxTimer* m_previewTimer;

	// ===== UI Controls =====
	wxSimplebook* m_notebook;

	// Mode switcher buttons
	wxToggleButton* m_borderModeBtn;
	wxToggleButton* m_groundModeBtn;
	wxToggleButton* m_wallModeBtn;

	// Common controls
	wxTextCtrl* m_groundNameCtrl;
	wxButton* m_newButton; // "New Border/Ground/Wall" button with dynamic label

	wxString m_loadedBorderName;

	// Sidebar (Tree view - legacy, may not be used)
	wxTextCtrl* m_searchCtrl;
	wxTreeCtrl* m_assetTree;
	wxTreeItemId m_rootId;
	wxTreeItemId m_bordersRootId;
	wxTreeItemId m_groundsRootId;
	wxTreeItemId m_wallsRootId;

	// ===== Border Tab =====
	wxPanel* m_borderPanel;
	wxListBox* m_borderBrowserList; // Sidebar browser for existing items (hidden)
	BrushBrowserGridPanel* m_browserGrid;
	wxButton* m_browserTilesetFilterButton;
	wxString m_browserTilesetFilter;
	wxSearchCtrl* m_browserSearchCtrl; // Search control for sidebar
	wxStaticText* m_browserLabel; // Dynamic label for sidebar
	wxCheckBox* m_isOptionalCheck;
	wxCheckBox* m_isGroundCheck;
	wxCheckBox* m_groupCheck; // NEW: Replaces m_groupCtrl SpinCtrl
	BorderGridPanel* m_gridPanel;
	BorderPreviewPanel* m_previewPanel;
	// Palette panels
	SimpleRawPalettePanel* m_itemPalettePanel;
	SimpleRawPalettePanel* m_groundPalette;
	GroundGridContainer* m_groundGridContainer;
	GroundPreviewPanel* m_groundPreviewPanel;
	SimpleRawPalettePanel* m_wallPalette;
	wxButton* m_wallTilesetButton;

	// Border items data
	std::vector<BorderItem> m_borderItems;
	std::map<BorderEdgePosition, BorderItemButton*> m_borderButtons;

	// ===== Ground Tab =====
	wxPanel* m_groundPanel;
	wxSpinCtrl* m_zOrderCtrl;
	wxButton* m_borderAlignmentButton;
	wxCheckBox* m_includeToNoneCheck;

	wxCheckBox* m_includeInnerCheck;
	wxButton* m_groundTilesetButton;
	wxArrayString m_tilesetListData;
	wxButton* m_rawCategoryButton;

	// Button state data
	wxArrayString m_rawCategoryNames;
	int m_rawCategorySelection = 0;
	int m_groundTilesetSelection = 0;
	int m_wallTilesetSelection = 0;
	int m_borderAlignmentSelection = 0; // 0=Outer, 1=Inner

	// Ground items data
	std::vector<GroundItem> m_groundItems;
	std::map<wxString, wxString> m_tilesets;
	long long m_lastInteractionTime;

	// Composite/Variation support
	std::vector<GroundVariation> m_groundVariations;
	int m_currentVariationIndex;

	void UpdateVariationControls();
	void RenderCurrentVariation();

	// Composite/Variation support
	// UI for Scrollable List of Variations
	wxScrolledWindow* m_variationsScrolledWindow;
	wxBoxSizer* m_variationsSizer;
	class VariationPreviewPanel* m_variationPreview;
	wxButton* m_addVariationBtn;

	// Helper to rebuild the list UI from m_groundVariations
	void RebuildVariationsList();
	void OnRemoveVariationBtn(wxCommandEvent &event); // Handle removal from specific row

	// Internal helper to add one UI row
	void AddVariationRow(GroundVariation &variation, int index);

	void OnPreviewTimer(wxTimerEvent &event);

	void OnAddVariation(wxCommandEvent &event);

	// Tileset filter whitelists (per-mode) - only enabled tilesets
	std::set<wxString> m_borderEnabledTilesets;
	std::set<wxString> m_groundEnabledTilesets;
	std::set<wxString> m_wallEnabledTilesets;
	wxBitmapButton* m_tilesetFilterBtn; // Filter button

	// Filter config persistence
	void SaveFilterConfig();
	void LoadFilterConfig();

	// Error Handling
	enum class ErrorSeverity { Info,
							   Warning,
							   Error,
							   Critical };
	void LogError(const wxString &message, ErrorSeverity severity);
	void ShowErrorDialog(const wxString &message, ErrorSeverity severity);

	// XML Cache
	struct XmlCacheEntry {
		pugi::xml_document doc;
		wxDateTime lastModified;
	};
	std::unordered_map<wxString, XmlCacheEntry> m_xmlCache;
	bool LoadXmlWithCache(const wxString &path, pugi::xml_document &doc);
	void InvalidateXmlCache(const wxString &path);

	// BrushPalettePanel* m_groundPalette;  // Removed duplicate

	// ===== Wall Tab =====
	wxPanel* m_wallPanel;
	wxSpinCtrl* m_wallGroupCtrl;
	wxSpinCtrl* m_wallZOrderCtrl;
	wxSpinCtrl* m_wallItemIdCtrl;
	wxSpinCtrl* m_wallItemChanceCtrl;
	wxListBox* m_wallItemsList;
	wxCheckBox* m_wallIsOptionalCheck;
	wxCheckBox* m_wallIsGroundCheck;
	WallVisualPanel* m_wallVisualPanel;
	WallGridPanel* m_wallGridPanel; // Visual selector for wall types

	// Wall items data
	std::map<std::string, WallTypeData> m_wallTypes;
	wxString m_currentWallType;
	int m_currentWallItemId = 0; // Stores currently selected palette item ID
	void OnWallTilesetSelect(wxCommandEvent &event);
	void OnWallPaletteSelect(wxCommandEvent &event);
	void OnGroundContainerChange(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};

// Custom button to represent a border item
class BorderItemButton : public wxButton {
public:
	BorderItemButton(wxWindow* parent, BorderEdgePosition position, wxWindowID id = wxID_ANY);
	virtual ~BorderItemButton();

	void SetItemId(uint16_t id);
	uint16_t GetItemId() const {
		return m_itemId;
	}
	BorderEdgePosition GetPosition() const {
		return m_position;
	}

	void OnPaint(wxPaintEvent &event);

private:
	uint16_t m_itemId;
	BorderEdgePosition m_position;

	DECLARE_EVENT_TABLE()
};

// Palette Filtering
enum PaletteFilterMode {
	FILTER_NONE = 0,
	FILTER_GROUND = 1,
	FILTER_WALL = 2
};

// Grid panel to visually show border item positions
class SimpleRawPalettePanel : public wxScrolledWindow {
public:
	SimpleRawPalettePanel(wxWindow* parent, wxWindowID id = wxID_ANY);
	virtual ~SimpleRawPalettePanel();

	void LoadTileset(const wxString &categoryName, PaletteFilterMode filterMode = FILTER_NONE);
	void SetItemIds(const std::vector<uint16_t> &ids);
	void OnPaint(wxPaintEvent &event);
	void OnLeftUp(wxMouseEvent &event);
	void OnMotion(wxMouseEvent &event);
	void OnLeave(wxMouseEvent &event);
	void OnSize(wxSizeEvent &event);

private:
	std::vector<uint16_t> m_itemIds;
	int m_cols = 1;
	int m_rows = 1;
	int m_itemSize = 32;
	int m_padding = 2;
	int m_hoverIndex = -1;

	void RecalculateLayout();
	int HitTest(const wxPoint &pt) const;

	DECLARE_EVENT_TABLE()
};

// Grid panel to visually show border item positions
class BorderGridPanel : public wxPanel {
public:
	BorderGridPanel(wxWindow* parent, wxWindowID id = wxID_ANY);
	virtual ~BorderGridPanel();

	void SetItemId(BorderEdgePosition pos, uint16_t itemId);
	uint16_t GetItemId(BorderEdgePosition pos) const;
	void Clear();

	// Selection management
	void SetSelectedPosition(BorderEdgePosition pos);
	BorderEdgePosition GetSelectedPosition() const {
		return m_selectedPosition;
	}

	// Override to ensure correct sizing
	virtual wxSize DoGetBestSize() const override;

	void OnPaint(wxPaintEvent &event);
	void OnMouseClick(wxMouseEvent &event);
	void OnMouseDown(wxMouseEvent &event);

	// Made public so it can be accessed from other components
	wxPoint GetPositionCoordinates(BorderEdgePosition pos) const;
	BorderEdgePosition GetPositionFromCoordinates(int x, int y) const;

private:
	std::map<BorderEdgePosition, uint16_t> m_items;
	BorderEdgePosition m_selectedPosition;

	DECLARE_EVENT_TABLE()
};

// Panel to preview how the border would look
class BorderPreviewPanel : public wxPanel {
public:
	BorderPreviewPanel(wxWindow* parent, wxWindowID id = wxID_ANY);
	virtual ~BorderPreviewPanel();

	void SetBorderItems(const std::vector<BorderItem> &items);
	void Clear();

	void OnPaint(wxPaintEvent &event);

private:
	std::vector<BorderItem> m_borderItems;

	DECLARE_EVENT_TABLE()
};

// Panel to select wall types (Visual Grid)
class WallGridPanel : public wxPanel {
public:
	WallGridPanel(wxWindow* parent, wxWindowID id = wxID_ANY);
	virtual ~WallGridPanel();

	void SetWallTypes(const std::map<std::string, WallTypeData> &types);
	void SetSelectedType(const std::string &type);
	std::string GetSelectedType() const {
		return m_selectedType;
	}

	void OnPaint(wxPaintEvent &event);
	void OnMouseClick(wxMouseEvent &event);

private:
	std::map<std::string, WallTypeData> m_types;
	std::string m_selectedType;

	DECLARE_EVENT_TABLE()
};

// Panel to visually edit wall brushes
class WallVisualPanel : public wxPanel {
public:
	WallVisualPanel(wxWindow* parent, wxWindowID id = wxID_ANY);
	virtual ~WallVisualPanel();

	void SetWallItems(const std::map<std::string, WallTypeData> &items);
	void Clear();

	void OnPaint(wxPaintEvent &event);

private:
	std::map<std::string, WallTypeData> m_items;

	DECLARE_EVENT_TABLE()
};

// ============================================================================
// GroundPreviewPanel - Animates and cycles through selected items
// ============================================================================
class GroundPreviewPanel : public wxPanel {
public:
	GroundPreviewPanel(wxWindow* parent, wxWindowID id = wxID_ANY);
	virtual ~GroundPreviewPanel();

	void SetWeightedItems(const std::vector<std::pair<uint16_t, int>> &items);
	void UpdateFromContainer(GroundGridContainer* container);

protected:
	void OnPaint(wxPaintEvent &event);
	void OnTimer(wxTimerEvent &event);

private:
	std::vector<uint16_t> m_displayItems;
	size_t m_currentIndex;
	wxTimer m_animationTimer;
	long m_lastCycleTime;

	DECLARE_EVENT_TABLE()
};

// ============================================================================
// GroundGridPanel (Visual Replica of BorderGridPanel)
// ============================================================================
class GroundGridPanel : public wxPanel {
public:
	GroundGridPanel(wxWindow* parent, wxWindowID id = wxID_ANY);
	virtual ~GroundGridPanel();

	// Item management
	void SetItemId(uint16_t id);
	uint16_t GetItemId() const {
		return m_itemId;
	}
	void Clear();
	bool IsEmpty() const {
		return m_itemId == 0;
	}

	// Event handlers
	void OnPaint(wxPaintEvent &event);
	void OnMouseClick(wxMouseEvent &event);
	void OnRightClick(wxMouseEvent &event);
	void OnLeftDClick(wxMouseEvent &event);

	// Layout helper
	wxSize DoGetBestSize() const override;

	// Selection & Chance
	void SetSelected(bool selected);
	bool IsSelected() const {
		return m_isSelected;
	}
	void SetChance(int chance);
	int GetChance() const {
		return m_chance;
	}

private:
	uint16_t m_itemId; // Currently assigned item ID
	int m_chance; // Spawn chance for this item
	bool m_isSelected; // Selection state

	DECLARE_EVENT_TABLE()
};

// ============================================================================
// GroundGridContainer - Manages multiple GroundGridPanel cells with auto-expansion
// ============================================================================
class GroundGridContainer : public wxScrolledWindow {
public:
	GroundGridContainer(wxWindow* parent, wxWindowID id = wxID_ANY);
	virtual ~GroundGridContainer();

	// Item management
	void AddItem(uint16_t itemId, int chance = 100, bool allowAppend = false);
	void RemoveCell(GroundGridPanel* cell);
	void Clear();

	// Get all filled items
	std::vector<uint16_t> GetAllItems() const;
	// Get all items with their chances
	std::vector<std::pair<uint16_t, int>> GetAllItemsWithChance() const;
	size_t GetFilledCount() const;

	// Selection
	void SetSelectedCell(GroundGridPanel* cell);
	GroundGridPanel* GetSelectedCell() const {
		return m_selectedCell;
	}

	// Called when a cell is filled - auto-adds new empty cell
	// Called when a cell is filled - auto-adds new empty cell
	void OnCellFilled(int index);

	// Make sure there's always one empty cell at the end
	void EnsureEmptyCell();

	// Adds a new empty cell at the end (explicit "Add variation")
	void AppendEmptyCell();

	// Rebuild grid layout after changes
	void RebuildGrids();

private:
	std::vector<GroundGridPanel*> m_gridCells;
	wxBoxSizer* m_gridSizer;
	GroundGridPanel* m_selectedCell;

	void CreateNewCell(); // Create a new grid cell
};

#endif // RME_BRUSH_EDITOR_WINDOW_H_
