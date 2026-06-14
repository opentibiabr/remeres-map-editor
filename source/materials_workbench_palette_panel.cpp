#include "main.h"

#include "materials_workbench_palette_panel.h"

#include <algorithm>
#include <set>
#include <unordered_map>
#include <utility>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/choicdlg.h>
#include <wx/control.h>
#include <wx/dcbuffer.h>
#include <wx/msgdlg.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/textdlg.h>
#include <wx/wrapsizer.h>

#include "brush.h"
#include "gui.h"
#include "items.h"
#include "materials_workbench_controller.h"
#include "palette_common.h"
#include "raw_brush.h"

namespace {
	void StylePaletteWorkspaceActionButton(wxButton* button, const wxString &tooltip) {
		button->SetMinSize(wxSize(-1, button->GetParent()->FromDIP(20)));
		button->SetToolTip(tooltip);
	}
} // namespace

struct BrushGridItem {
	wxString label;
	wxString tooltip;
	wxString badge;
	Brush* brush = nullptr;
	int lookId = 0;
	int index = -1;
};

struct BrushGridTile {
	wxPanel* panel = nullptr;
	BrushButton* button = nullptr;
	wxStaticText* label = nullptr;
	int itemIndex = -1;
};

class MaterialsWorkbenchBrushGridPanel : public wxScrolledWindow {
public:
	explicit MaterialsWorkbenchBrushGridPanel(wxWindow* parent) :
		wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxHSCROLL | wxBORDER_THEME) {
		SetScrollRate(FromDIP(8), FromDIP(8));
		SetBackgroundStyle(wxBG_STYLE_PAINT);

		Bind(wxEVT_PAINT, &MaterialsWorkbenchBrushGridPanel::OnPaint, this);
		Bind(wxEVT_LEFT_DOWN, &MaterialsWorkbenchBrushGridPanel::OnLeftDown, this);
		Bind(wxEVT_LEFT_DCLICK, &MaterialsWorkbenchBrushGridPanel::OnLeftDClick, this);
		Bind(wxEVT_SIZE, &MaterialsWorkbenchBrushGridPanel::OnSize, this);
		Bind(wxEVT_MOTION, &MaterialsWorkbenchBrushGridPanel::OnMouseMove, this);
		UpdateVirtualSize();
	}

	void SetSelectionChangedHandler(std::function<void(int)> handler) {
		onSelectionChanged_ = std::move(handler);
	}

	void SetSelectionActivatedHandler(std::function<void(int)> handler) {
		onSelectionActivated_ = std::move(handler);
	}

	void SetEmptyMessage(const wxString &message) {
		emptyMessage_ = message;
		if (items_.empty()) {
			Refresh();
		}
	}

	void SetItems(const std::vector<BrushGridItem> &items, int selectedIndex = -1, bool preserveViewStart = false) {
		const wxPoint previousViewStart = preserveViewStart ? GetViewStart() : wxPoint(0, 0);
		const int previousSelectedIndex = selectedIndex_;

		items_ = items;
		hoveredTooltipItemPosition_ = wxNOT_FOUND;
		UnsetToolTip();
		RebuildItemPositions();
		UpdateVirtualSize();

		selectedIndex_ = -1;
		if (!items_.empty()) {
			if (HasItemIndex(selectedIndex)) {
				selectedIndex_ = selectedIndex;
			} else {
				selectedIndex_ = items_.front().index;
			}
		}

		if (preserveViewStart) {
			RestoreViewStart(previousViewStart);
		}

		Refresh();
		if (selectedIndex_ != previousSelectedIndex && onSelectionChanged_) {
			onSelectionChanged_(selectedIndex_);
		}
	}

	void Clear() {
		SetItems({});
	}

	void SelectIndex(int index) {
		if (selectedIndex_ == index) {
			return;
		}
		const int previousSelectedIndex = selectedIndex_;
		selectedIndex_ = index;
		RefreshSelectionTiles(previousSelectedIndex, selectedIndex_);
		if (onSelectionChanged_) {
			onSelectionChanged_(selectedIndex_);
		}
	}

private:
	int GetSpacing() const {
		return FromDIP(6);
	}

	int GetTileWidth() const {
		return FromDIP(96);
	}

	int GetTileHeight() const {
		return FromDIP(84);
	}

	int GetPreviewBitmapSize() const {
		return FromDIP(30);
	}

	int GetColumnCount() const {
		const int availableWidth = std::max(GetClientSize().GetWidth() - GetSpacing(), GetTileWidth());
		return std::max(1, availableWidth / (GetTileWidth() + GetSpacing()));
	}

	wxRect GetTileRect(size_t itemPosition) const {
		const int columns = GetColumnCount();
		const int spacing = GetSpacing();
		const int row = static_cast<int>(itemPosition) / columns;
		const int column = static_cast<int>(itemPosition) % columns;
		return wxRect(
			spacing + column * (GetTileWidth() + spacing),
			spacing + row * (GetTileHeight() + spacing),
			GetTileWidth(),
			GetTileHeight()
		);
	}

	void UpdateVirtualSize() {
		const int spacing = GetSpacing();
		const int columns = GetColumnCount();
		const int rows = items_.empty() ? 1 : static_cast<int>((items_.size() + columns - 1) / columns);
		const int width = spacing + columns * (GetTileWidth() + spacing);
		const int height = spacing + rows * (GetTileHeight() + spacing);
		SetVirtualSize(wxSize(width, std::max(height, FromDIP(144))));
	}

	int HitTestItem(const wxPoint &position) const {
		wxPoint logical = position;
		CalcUnscrolledPosition(position.x, position.y, &logical.x, &logical.y);
		return GetItemPositionFromLogicalPoint(logical);
	}

	wxString BuildDisplayLabel(wxDC &dc, const wxString &label, int width) const {
		return wxControl::Ellipsize(label, dc, wxELLIPSIZE_END, width);
	}

	void DrawTile(wxDC &dc, size_t itemPosition) {
		const BrushGridItem &item = items_[itemPosition];
		const wxRect tileRect = GetTileRect(itemPosition);
		const bool isSelected = item.index == selectedIndex_;
		const wxColour baseColour = wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX);
		const wxColour borderColour = isSelected ? wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT) : wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW);
		const wxColour fillColour = isSelected ? wxSystemSettings::GetColour(wxSYS_COLOUR_INFOBK) : baseColour;
		const wxColour previewFillColour = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);

		dc.SetPen(wxPen(borderColour, isSelected ? 2 : 1));
		dc.SetBrush(wxBrush(fillColour));
		dc.DrawRectangle(tileRect);

		const wxRect iconRect(
			tileRect.x + (tileRect.width - FromDIP(32)) / 2,
			tileRect.y + FromDIP(6),
			FromDIP(32),
			FromDIP(32)
		);

		dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW)));
		dc.SetBrush(wxBrush(previewFillColour));
		dc.DrawRectangle(iconRect);

		const int lookId = item.brush ? item.brush->getLookID() : item.lookId;
		if (lookId > 0) {
			const wxBitmap* cachedPreview = GetOrCreatePreviewBitmap(lookId);
			if (cachedPreview && cachedPreview->IsOk()) {
				dc.DrawBitmap(*cachedPreview, iconRect.x + FromDIP(1), iconRect.y + FromDIP(1), true);
			}
		}

		if (!item.badge.IsEmpty()) {
			const wxFont previousFont = dc.GetFont();
			wxFont badgeFont = previousFont;
			badgeFont.SetWeight(wxFONTWEIGHT_BOLD);
			if (badgeFont.GetPointSize() > 1) {
				badgeFont.SetPointSize(std::max(6, badgeFont.GetPointSize() - 2));
			}
			dc.SetFont(badgeFont);

			const wxSize badgeSize = dc.GetTextExtent(item.badge);
			const int badgePadX = FromDIP(4);
			const int badgePadY = FromDIP(0);
			wxRect badgeRect(
				tileRect.GetLeft() + FromDIP(6),
				iconRect.GetTop() + FromDIP(2),
				badgeSize.x + badgePadX * 2,
				badgeSize.y + badgePadY * 2
			);
			const int maxBadgeRight = iconRect.GetLeft() - FromDIP(2);
			if (badgeRect.GetRight() >= maxBadgeRight) {
				badgeRect.x = std::max(tileRect.GetLeft() + FromDIP(2), maxBadgeRight - badgeRect.width);
			}

			const wxColour badgeFill(255, 200, 64);
			const wxColour badgeBorder(184, 108, 0);
			const wxColour badgeText(51, 25, 0);
			dc.SetPen(wxPen(badgeBorder));
			dc.SetBrush(wxBrush(badgeFill));
			dc.DrawRoundedRectangle(badgeRect, FromDIP(4));
			dc.SetTextForeground(badgeText);
			dc.DrawLabel(item.badge, badgeRect, wxALIGN_CENTER);
			dc.SetFont(previousFont);
		}

		const wxRect labelRect(
			tileRect.x + FromDIP(4),
			iconRect.GetBottom() + FromDIP(4),
			tileRect.width - FromDIP(8),
			tileRect.height - iconRect.height - FromDIP(14)
		);

		dc.SetTextForeground(isSelected ? wxSystemSettings::GetColour(wxSYS_COLOUR_INFOTEXT) : wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
		dc.DrawLabel(BuildDisplayLabel(dc, item.label, labelRect.width), labelRect, wxALIGN_CENTER_HORIZONTAL | wxALIGN_TOP);
	}

	void OnPaint(wxPaintEvent &) {
		wxAutoBufferedPaintDC dc(this);
		PrepareDC(dc);
		dc.SetBackground(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW)));
		dc.Clear();

		if (items_.empty()) {
			dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
			const wxRect clientRect(wxPoint(0, 0), GetClientSize());
			dc.DrawLabel(emptyMessage_.IsEmpty() ? wxString("Nothing to show here yet.") : emptyMessage_, clientRect, wxALIGN_CENTER_HORIZONTAL | wxALIGN_CENTER_VERTICAL);
			return;
		}

		size_t firstVisibleItem = 0;
		size_t lastVisibleItem = items_.size() - 1;
		GetVisibleItemRange(firstVisibleItem, lastVisibleItem);
		for (size_t i = firstVisibleItem; i <= lastVisibleItem; ++i) {
			DrawTile(dc, i);
		}
	}

	void OnLeftDown(wxMouseEvent &event) {
		const int itemPosition = HitTestItem(event.GetPosition());
		if (itemPosition != wxNOT_FOUND) {
			SelectIndex(items_[itemPosition].index);
		}
		event.Skip();
	}

	void OnLeftDClick(wxMouseEvent &event) {
		OnLeftDown(event);
		if (onSelectionActivated_ && selectedIndex_ >= 0) {
			onSelectionActivated_(selectedIndex_);
		}
	}

	void OnSize(wxSizeEvent &event) {
		InvalidatePreviewCacheIfNeeded();
		UpdateVirtualSize();
		Refresh();
		event.Skip();
	}

	void OnMouseMove(wxMouseEvent &event) {
		const int itemPosition = HitTestItem(event.GetPosition());
		if (itemPosition != wxNOT_FOUND) {
			if (itemPosition != hoveredTooltipItemPosition_) {
				hoveredTooltipItemPosition_ = itemPosition;
				SetToolTip(items_[itemPosition].tooltip.IsEmpty() ? items_[itemPosition].label : items_[itemPosition].tooltip);
			}
		} else {
			if (hoveredTooltipItemPosition_ != wxNOT_FOUND) {
				hoveredTooltipItemPosition_ = wxNOT_FOUND;
				UnsetToolTip();
			}
		}
		event.Skip();
	}

	bool HasItemIndex(int index) const {
		return itemPositionsByIndex_.find(index) != itemPositionsByIndex_.end();
	}

	void RestoreViewStart(const wxPoint &viewStart) {
		int pixelsPerUnitX = 0;
		int pixelsPerUnitY = 0;
		GetScrollPixelsPerUnit(&pixelsPerUnitX, &pixelsPerUnitY);
		if (pixelsPerUnitX <= 0) {
			pixelsPerUnitX = 1;
		}
		if (pixelsPerUnitY <= 0) {
			pixelsPerUnitY = 1;
		}

		const wxSize virtualSize = GetVirtualSize();
		const wxSize clientSize = GetClientSize();
		const int maxX = std::max(0, (virtualSize.GetWidth() - clientSize.GetWidth() + pixelsPerUnitX - 1) / pixelsPerUnitX);
		const int maxY = std::max(0, (virtualSize.GetHeight() - clientSize.GetHeight() + pixelsPerUnitY - 1) / pixelsPerUnitY);
		Scroll(std::clamp(viewStart.x, 0, maxX), std::clamp(viewStart.y, 0, maxY));
	}

	void RebuildItemPositions() {
		itemPositionsByIndex_.clear();
		itemPositionsByIndex_.reserve(items_.size());
		for (size_t i = 0; i < items_.size(); ++i) {
			itemPositionsByIndex_[items_[i].index] = i;
		}
	}

	int GetItemPositionFromLogicalPoint(const wxPoint &logical) const {
		const int spacing = GetSpacing();
		const int cellWidth = GetTileWidth() + spacing;
		const int cellHeight = GetTileHeight() + spacing;
		if (logical.x < spacing || logical.y < spacing) {
			return wxNOT_FOUND;
		}

		const int column = (logical.x - spacing) / cellWidth;
		const int row = (logical.y - spacing) / cellHeight;
		if (column < 0 || row < 0) {
			return wxNOT_FOUND;
		}

		const int columns = GetColumnCount();
		const int itemPosition = row * columns + column;
		if (itemPosition < 0 || itemPosition >= static_cast<int>(items_.size())) {
			return wxNOT_FOUND;
		}

		return GetTileRect(static_cast<size_t>(itemPosition)).Contains(logical) ? itemPosition : wxNOT_FOUND;
	}

	bool FindItemPositionByIndex(int index, size_t &outItemPosition) const {
		const auto it = itemPositionsByIndex_.find(index);
		if (it == itemPositionsByIndex_.end()) {
			return false;
		}
		outItemPosition = it->second;
		return true;
	}

	wxRect GetVisibleLogicalRect() const {
		wxPoint origin(0, 0);
		CalcUnscrolledPosition(0, 0, &origin.x, &origin.y);
		return wxRect(origin, GetClientSize());
	}

	void GetVisibleItemRange(size_t &outFirstItem, size_t &outLastItem) const {
		outFirstItem = 0;
		outLastItem = 0;
		if (items_.empty()) {
			return;
		}

		const wxRect visibleRect = GetVisibleLogicalRect();
		const int spacing = GetSpacing();
		const int cellWidth = GetTileWidth() + spacing;
		const int cellHeight = GetTileHeight() + spacing;
		const int columns = GetColumnCount();
		const int totalRows = static_cast<int>((items_.size() + columns - 1) / columns);
		const int firstRow = std::clamp((visibleRect.y - spacing) / std::max(1, cellHeight), 0, std::max(0, totalRows - 1));
		const int lastRow = std::clamp((visibleRect.GetBottom() - spacing) / std::max(1, cellHeight), firstRow, std::max(0, totalRows - 1));
		outFirstItem = static_cast<size_t>(std::clamp(firstRow * columns, 0, std::max(0, static_cast<int>(items_.size()) - 1)));
		outLastItem = static_cast<size_t>(std::clamp(((lastRow + 1) * columns) - 1, 0, std::max(0, static_cast<int>(items_.size()) - 1)));
	}

	void RefreshTileAtPosition(size_t itemPosition) {
		if (itemPosition >= items_.size()) {
			return;
		}

		const wxRect logicalRect = GetTileRect(itemPosition);
		const wxPoint deviceOrigin = CalcScrolledPosition(logicalRect.GetTopLeft());
		RefreshRect(wxRect(deviceOrigin, logicalRect.GetSize()), false);
	}

	void RefreshSelectionTiles(int previousSelectedIndex, int nextSelectedIndex) {
		size_t itemPosition = 0;
		if (FindItemPositionByIndex(previousSelectedIndex, itemPosition)) {
			RefreshTileAtPosition(itemPosition);
		}
		if (FindItemPositionByIndex(nextSelectedIndex, itemPosition)) {
			RefreshTileAtPosition(itemPosition);
		}
	}

	void InvalidatePreviewCacheIfNeeded() {
		const int previewBitmapSize = GetPreviewBitmapSize();
		if (previewBitmapSize == cachedPreviewBitmapSize_) {
			return;
		}

		previewBitmapCache_.clear();
		cachedPreviewBitmapSize_ = previewBitmapSize;
	}

	const wxBitmap* GetOrCreatePreviewBitmap(int lookId) {
		if (lookId <= 0) {
			return nullptr;
		}

		InvalidatePreviewCacheIfNeeded();
		const auto cachedIt = previewBitmapCache_.find(lookId);
		if (cachedIt != previewBitmapCache_.end()) {
			return cachedIt->second.IsOk() ? &cachedIt->second : nullptr;
		}

		wxBitmap previewBitmap;
		if (Sprite* sprite = g_gui.gfx.getSprite(lookId)) {
			const int previewBitmapSize = std::max(1, cachedPreviewBitmapSize_);
			previewBitmap = wxBitmap(previewBitmapSize, previewBitmapSize, 32);
			wxMemoryDC previewDc(previewBitmap);
			previewDc.SetBackground(wxBrush(wxColour(0, 0, 0, 0)));
			previewDc.Clear();
			sprite->DrawTo(&previewDc, SPRITE_SIZE_32x32, 0, 0, previewBitmapSize, previewBitmapSize);
			previewDc.SelectObject(wxNullBitmap);
		}

		auto inserted = previewBitmapCache_.emplace(lookId, previewBitmap);
		return inserted.first->second.IsOk() ? &inserted.first->second : nullptr;
	}

	std::vector<BrushGridItem> items_;
	std::unordered_map<int, size_t> itemPositionsByIndex_;
	std::unordered_map<int, wxBitmap> previewBitmapCache_;
	int cachedPreviewBitmapSize_ = 0;
	int selectedIndex_ = -1;
	int hoveredTooltipItemPosition_ = wxNOT_FOUND;
	wxString emptyMessage_;
	std::function<void(int)> onSelectionChanged_;
	std::function<void(int)> onSelectionActivated_;
};

namespace {
	const char* const kRuntimeSectionTypes[] = {
		"terrain",
		"terrain_and_raw",
		"doodad",
		"doodad_and_raw",
		"items",
		"items_and_raw",
		"raw",
	};

	wxString TrimmedCopy(wxString value) {
		value.Trim(true);
		value.Trim(false);
		return value;
	}

	wxString BuildPaletteWorkspaceBrushLookupKey(int64_t brushId, const wxString &brushName) {
		if (brushId > 0) {
			return wxString::Format("id:%lld", static_cast<long long>(brushId));
		}

		wxString normalizedName = brushName;
		normalizedName.MakeLower();
		return "name:" + normalizedName;
	}

	bool IsSupportedRuntimeSectionType(const wxString &sectionType) {
		for (const char* candidate : kRuntimeSectionTypes) {
			if (sectionType.IsSameAs(candidate, false)) {
				return true;
			}
		}
		return false;
	}

	wxString DerivePaletteGroupFromSectionType(const wxString &sectionType) {
		if (sectionType.IsSameAs("terrain", false) || sectionType.IsSameAs("terrain_and_raw", false)) {
			return "terrain";
		}
		if (sectionType.IsSameAs("doodad", false) || sectionType.IsSameAs("doodad_and_raw", false)) {
			return "doodad";
		}
		if (sectionType.IsSameAs("item", false) || sectionType.IsSameAs("items", false) || sectionType.IsSameAs("items_and_raw", false)) {
			return "item";
		}
		return "other";
	}

	wxString ResolveBuiltinBrushFamilyKey(const wxString &groupName) {
		if (groupName.IsSameAs("terrain", false)) {
			return "terrain";
		}
		if (groupName.IsSameAs("doodad", false)) {
			return "doodad";
		}
		if (groupName.IsSameAs("item", false)) {
			return "item";
		}
		if (groupName.IsSameAs("other", false)) {
			return "other";
		}
		return wxString();
	}

	wxString NormalizePaletteRuntimeFamilyKey(const wxString &familyKey) {
		if (familyKey.IsSameAs("raw", false) || familyKey.IsSameAs("other", false)) {
			return "other";
		}
		if (familyKey.IsSameAs("terrain", false) || familyKey.IsSameAs("doodad", false) || familyKey.IsSameAs("item", false)) {
			return familyKey.Lower();
		}
		return wxString();
	}

	wxString ResolvePaletteGroupKey(const TilesetStorageRecord &tileset) {
		if (!tileset.paletteGroupName.IsEmpty()) {
			return tileset.paletteGroupName;
		}
		if (!tileset.sections.empty()) {
			return DerivePaletteGroupFromSectionType(tileset.sections.front().sectionType);
		}
		return "other";
	}

	wxString ResolvePaletteGroupRuntimeFamilyKey(const TilesetStorageRecord &tileset) {
		const wxString configuredRuntimeFamily = NormalizePaletteRuntimeFamilyKey(tileset.paletteGroupRuntimeFamily);
		if (!configuredRuntimeFamily.IsEmpty()) {
			return configuredRuntimeFamily;
		}
		for (const TilesetSectionRecord &section : tileset.sections) {
			if (IsSupportedRuntimeSectionType(section.sectionType)) {
				return DerivePaletteGroupFromSectionType(section.sectionType);
			}
		}
		if (!tileset.sections.empty()) {
			return DerivePaletteGroupFromSectionType(tileset.sections.front().sectionType);
		}
		const wxString builtinFamily = ResolveBuiltinBrushFamilyKey(ResolvePaletteGroupKey(tileset));
		return builtinFamily.IsEmpty() ? wxString("terrain") : builtinFamily;
	}

	wxString ResolvePaletteBrushDisplayFamily(const TilesetStorageRecord &tileset) {
		for (const TilesetSectionRecord &section : tileset.sections) {
			if (IsSupportedRuntimeSectionType(section.sectionType)) {
				return DerivePaletteGroupFromSectionType(section.sectionType);
			}
		}

		if (!tileset.sections.empty()) {
			return DerivePaletteGroupFromSectionType(tileset.sections.front().sectionType);
		}

		return ResolvePaletteGroupRuntimeFamilyKey(tileset);
	}

	wxString BuildPaletteFamilyLabel(const wxString &familyKey) {
		if (familyKey.IsSameAs("terrain", false)) {
			return "Terrain";
		}
		if (familyKey.IsSameAs("doodad", false)) {
			return "Doodad";
		}
		if (familyKey.IsSameAs("item", false)) {
			return "Item";
		}
		if (familyKey.IsSameAs("other", false)) {
			return "Other / Raw";
		}
		return familyKey;
	}

	wxString BuildPaletteGroupLabel(const PaletteGroupRecord &group) {
		wxString label = group.name;
		if (group.isBuiltin) {
			label += " [built-in]";
		}
		return label;
	}

	wxString BuildSectionLabel(const TilesetSectionRecord &section) {
		return wxString::Format("%s (%zu)", section.sectionType, section.entries.size());
	}

	wxString ComputeAfterBrushName(const TilesetEntryRecord &entry) {
		if (!entry.brushName.IsEmpty()) {
			return entry.brushName;
		}
		return wxString();
	}

	int ComputeAfterItemId(const TilesetEntryRecord &entry) {
		if (entry.itemId > 0) {
			return entry.itemId;
		}
		if (entry.toItemId > 0) {
			return entry.toItemId;
		}
		return entry.fromItemId;
	}

	wxString BuildPaletteWorkspaceBrushFamilyKeyFromBrushType(const wxString &brushType) {
		if (brushType == "ground") {
			return "terrain";
		}
		if (brushType == "doodad") {
			return "doodad";
		}
		if (brushType == "carpet" || brushType == "table" || brushType == "wall") {
			return "item";
		}
		return "other";
	}

	wxString PreferredSectionTypeForBrushFamilyKey(const wxString &familyKey) {
		if (familyKey == "terrain") {
			return "terrain";
		}
		if (familyKey == "doodad") {
			return "doodad";
		}
		if (familyKey == "item") {
			return "items";
		}
		return "terrain";
	}

	wxString NormalizeSectionTypeForPaletteInsertion(
		const wxString &sourceSectionType,
		const wxString &fallbackFamilyKey,
		const wxString &destinationDisplayFamily,
		const wxString &entryKind
	) {
		if (sourceSectionType.IsEmpty()) {
			return PreferredSectionTypeForBrushFamilyKey(fallbackFamilyKey);
		}
		if (sourceSectionType.IsSameAs("raw", false) && entryKind.IsSameAs("item", false)) {
			if (destinationDisplayFamily.IsSameAs("terrain", false)) {
				return "terrain_and_raw";
			}
			if (destinationDisplayFamily.IsSameAs("doodad", false)) {
				return "doodad_and_raw";
			}
			if (destinationDisplayFamily.IsSameAs("item", false)) {
				return "items_and_raw";
			}
			return "raw";
		}
		if (sourceSectionType.IsSameAs("item", false)) {
			return "items";
		}
		return sourceSectionType;
	}

	int FindVisibleEntryIndex(const std::vector<std::pair<int, int>> &locations, int sectionIndex, int entryIndex) {
		for (size_t i = 0; i < locations.size(); ++i) {
			if (locations[i].first == sectionIndex && locations[i].second == entryIndex) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	int ResolveEntryPreviewItemId(const TilesetEntryRecord &entry) {
		if (entry.itemId > 0) {
			return entry.itemId;
		}
		if (entry.fromItemId > 0) {
			return entry.fromItemId;
		}
		return entry.toItemId;
	}

	wxString BuildItemEntryLabel(const TilesetEntryRecord &entry) {
		const int previewItemId = ResolveEntryPreviewItemId(entry);
		if (previewItemId <= 0) {
			return "Item";
		}

		const int fromItemId = entry.fromItemId > 0 ? entry.fromItemId : previewItemId;
		const int toItemId = entry.toItemId > 0 ? entry.toItemId : fromItemId;
		wxString itemName;
		if (auto type = g_items.getRawItemType(static_cast<uint16_t>(previewItemId)); type && type->id != 0 && !type->name.empty()) {
			itemName = wxString::FromUTF8(type->name.c_str());
		}

		if (toItemId > fromItemId) {
			if (!itemName.IsEmpty()) {
				return wxString::Format("%d-%d - %s", fromItemId, toItemId, itemName);
			}
			return wxString::Format("%d-%d", fromItemId, toItemId);
		}

		if (!itemName.IsEmpty()) {
			return wxString::Format("%d - %s", previewItemId, itemName);
		}
		return wxString::Format("Item %d", previewItemId);
	}

	bool IsMovablePaletteEntry(const TilesetEntryRecord &entry) {
		if (entry.entryKind.IsSameAs("brush", false)) {
			return entry.brushId > 0 || !entry.brushName.IsEmpty();
		}
		if (entry.entryKind.IsSameAs("item", false)) {
			return ResolveEntryPreviewItemId(entry) > 0;
		}
		return false;
	}

	const BrushRecord* FindCatalogBrushRecord(const MaterialsWorkbenchController &controller, int64_t brushId, const wxString &brushName) {
		for (const MaterialsWorkbenchBrushGroup &group : controller.GetBrushGroups()) {
			for (const BrushRecord &record : group.brushes) {
				if (brushId > 0 && record.id == brushId) {
					return &record;
				}
				if (brushId <= 0 && !brushName.IsEmpty() && record.name.IsSameAs(brushName, false)) {
					return &record;
				}
			}
		}
		for (const BrushRecord &record : controller.GetWallBrushes()) {
			if (brushId > 0 && record.id == brushId) {
				return &record;
			}
			if (brushId <= 0 && !brushName.IsEmpty() && record.name.IsSameAs(brushName, false)) {
				return &record;
			}
		}
		return nullptr;
	}

	wxString DescribePaletteEntry(const MaterialsWorkbenchController &controller, const TilesetEntryRecord &entry) {
		if (entry.entryKind.IsSameAs("brush", false)) {
			if (!entry.brushName.IsEmpty()) {
				return entry.brushName;
			}
			if (const BrushRecord* catalogBrush = FindCatalogBrushRecord(controller, entry.brushId, entry.brushName)) {
				return catalogBrush->name;
			}
			return "Brush";
		}
		if (entry.entryKind.IsSameAs("item", false)) {
			return BuildItemEntryLabel(entry);
		}
		if (!entry.entryKind.IsEmpty()) {
			return entry.entryKind;
		}
		return "Entry";
	}

	wxString FormatStorageValue(const wxString &sourceFile) {
		return sourceFile.IsEmpty() ? "materials.db" : sourceFile;
	}

	wxString BuildPaletteEntryTooltip(
		const MaterialsWorkbenchController &controller,
		const TilesetStorageRecord &palette,
		const TilesetSectionRecord &section,
		const TilesetEntryRecord &entry
	) {
		wxString text;
		text << "Palette Entry\n";
		text << "Label: " << DescribePaletteEntry(controller, entry) << "\n";
		text << "Kind: " << (entry.entryKind.IsEmpty() ? wxString("entry") : entry.entryKind) << "\n";
		text << "Palette Category: " << BuildPaletteFamilyLabel(ResolvePaletteGroupKey(palette)) << "\n";
		text << "Palette: " << palette.name << "\n";
		text << "Section: " << section.sectionType << "\n";
		text << "Sort Order: " << entry.sortOrder << "\n";
		text << "Storage: " << FormatStorageValue(palette.sourceFile);

		if (entry.entryKind.IsSameAs("brush", false)) {
			if (const BrushRecord* brushRecord = FindCatalogBrushRecord(controller, entry.brushId, entry.brushName)) {
				text << "\nBrush ID: " << brushRecord->id;
				text << "\nBrush Type: " << brushRecord->type;
				if (brushRecord->lookId > 0) {
					text << "\nLook ID: " << brushRecord->lookId;
				}
				if (brushRecord->serverLookId > 0) {
					text << "\nServer look ID: " << brushRecord->serverLookId;
				}
			} else if (entry.brushId > 0) {
				text << "\nBrush ID: " << entry.brushId;
			}
		} else if (entry.entryKind.IsSameAs("item", false)) {
			const int previewItemId = ResolveEntryPreviewItemId(entry);
			const int fromItemId = entry.fromItemId > 0 ? entry.fromItemId : previewItemId;
			const int toItemId = entry.toItemId > 0 ? entry.toItemId : fromItemId;
			if (previewItemId > 0) {
				text << "\nPreview Item ID: " << previewItemId;
			}
			if (fromItemId > 0) {
				if (toItemId > fromItemId) {
					text << "\nRange: " << fromItemId << "-" << toItemId;
				} else {
					text << "\nItem ID: " << fromItemId;
				}
			}
		}

		if (!entry.afterBrushName.IsEmpty()) {
			text << "\nAfter Brush: " << entry.afterBrushName;
		} else if (entry.afterItemId > 0) {
			text << "\nAfter Item ID: " << entry.afterItemId;
		}

		if (IsMovablePaletteEntry(entry)) {
			text << "\nAction: This entry can move to another palette.";
		}
		return text;
	}

	wxString BuildSourceBrushTooltip(const MaterialsWorkbenchAvailableBrushSource &source, const BrushRecord &record) {
		wxString text;
		text << "Source Brush\n";
		text << "Name: " << record.name << "\n";
		text << "Brush ID: " << record.id << "\n";
		text << "Type: " << record.type << "\n";
		text << "Palette Category: " << BuildPaletteFamilyLabel(source.familyKey) << "\n";
		text << "Source Palette: " << source.paletteLabel << "\n";
		text << "Storage: " << FormatStorageValue(record.sourceFile);
		if (record.lookId > 0) {
			text << "\nLook ID: " << record.lookId;
		}
		if (record.serverLookId > 0) {
			text << "\nServer look ID: " << record.serverLookId;
		}
		text << "\nAction: Add this brush to the current palette.";
		return text;
	}
} // namespace

MaterialsWorkbenchPalettePanel::MaterialsWorkbenchPalettePanel(wxWindow* parent, MaterialsWorkbenchController &controller) :
	wxPanel(parent, wxID_ANY),
	controller_(controller) {
	BuildLayout();
	ClearWorkspace("Select a palette to edit its palette category and entries.");
}

void MaterialsWorkbenchPalettePanel::SetOnPaletteSaved(std::function<void(const wxString &)> callback) {
	onPaletteSaved_ = std::move(callback);
}

void MaterialsWorkbenchPalettePanel::BuildLayout() {
	wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);
	auto makeSectionLabel = [](wxWindow* parent, const wxString &text) {
		wxStaticText* label = new wxStaticText(parent, wxID_ANY, text);
		label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
		return label;
	};

	wxStaticText* title = new wxStaticText(this, wxID_ANY, "Palette Workspace");
	wxFont titleFont = title->GetFont();
	titleFont.SetPointSize(titleFont.GetPointSize() + 4);
	titleFont.SetWeight(wxFONTWEIGHT_BOLD);
	title->SetFont(titleFont);

	titleLabel_ = new wxStaticText(this, wxID_ANY, "Select a palette");
	sourceLabel_ = new wxStaticText(this, wxID_ANY, "");
	statusLabel_ = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
	statusLabel_->SetMinSize(wxSize(-1, FromDIP(20)));

	wxBoxSizer* headerSizer = new wxBoxSizer(wxVERTICAL);
	headerSizer->Add(title, 0, wxBOTTOM, FromDIP(4));
	headerSizer->Add(titleLabel_, 0, wxBOTTOM, FromDIP(2));
	headerSizer->Add(sourceLabel_, 0);

	wxBoxSizer* toolbarSizer = new wxBoxSizer(wxVERTICAL);
	wxWrapSizer* groupRowSizer = new wxWrapSizer(wxHORIZONTAL, 0);
	paletteGroupChoice_ = new wxChoice(this, wxID_ANY);
	currentSectionChoice_ = new wxChoice(this, wxID_ANY);
	paletteGroupChoice_->SetMinSize(wxSize(FromDIP(220), -1));
	currentSectionChoice_->SetMinSize(wxSize(FromDIP(260), -1));
	createPaletteButton_ = new wxButton(this, wxID_ANY, "New Palette");
	renamePaletteButton_ = new wxButton(this, wxID_ANY, "Rename Palette");
	deletePaletteButton_ = new wxButton(this, wxID_ANY, "Delete Palette");
	createPaletteGroupButton_ = new wxButton(this, wxID_ANY, "New Category");
	renamePaletteGroupButton_ = new wxButton(this, wxID_ANY, "Rename Category");
	deletePaletteGroupButton_ = new wxButton(this, wxID_ANY, "Delete Category");
	addSectionButton_ = new wxButton(this, wxID_ANY, "New Section");
	renameSectionButton_ = new wxButton(this, wxID_ANY, "Rename Section");
	deleteSectionButton_ = new wxButton(this, wxID_ANY, "Delete Section");
	currentSectionChoice_->Hide();
	addSectionButton_->Hide();
	renameSectionButton_->Hide();
	deleteSectionButton_->Hide();
	toolbarSizer->Add(makeSectionLabel(this, "Palette Category"), 0, wxBOTTOM, FromDIP(3));
	groupRowSizer->Add(paletteGroupChoice_, 0, wxRIGHT | wxBOTTOM, FromDIP(8));
	toolbarSizer->Add(groupRowSizer, 0, wxEXPAND);

	contentSplitter_ = new wxSplitterWindow(this, wxID_ANY);
	contentSplitter_->SetSashGravity(0.5);
	contentSplitter_->SetMinimumPaneSize(FromDIP(240));

	wxPanel* currentSectionPanel = new wxPanel(contentSplitter_, wxID_ANY);
	wxBoxSizer* currentSectionSizer = new wxBoxSizer(wxVERTICAL);
	wxWrapSizer* paletteBrushActionsSizer = new wxWrapSizer(wxHORIZONTAL, 0);
	wxFlexGridSizer* moveDestinationSizer = new wxFlexGridSizer(2, 2, FromDIP(4), FromDIP(6));
	moveDestinationSizer->AddGrowableCol(1, 1);
	addBrushButton_ = new wxButton(currentSectionPanel, wxID_ANY, "Add Entry");
	moveToPaletteButton_ = new wxButton(currentSectionPanel, wxID_ANY, "Move Entry");
	removeBrushButton_ = new wxButton(currentSectionPanel, wxID_ANY, "Remove Entry");
	moveUpButton_ = new wxButton(currentSectionPanel, wxID_ANY, "Move Up");
	moveDownButton_ = new wxButton(currentSectionPanel, wxID_ANY, "Move Down");
	moveDestinationFamilyChoice_ = new wxChoice(currentSectionPanel, wxID_ANY);
	moveDestinationPaletteChoice_ = new wxChoice(currentSectionPanel, wxID_ANY);
	moveDestinationFamilyChoice_->SetMinSize(wxSize(FromDIP(140), -1));
	moveDestinationPaletteChoice_->SetMinSize(wxSize(FromDIP(220), -1));
	wxStaticText* paletteBrushesTitle = new wxStaticText(currentSectionPanel, wxID_ANY, "Palette Entries");
	currentSectionSizer->Add(paletteBrushesTitle, 0, wxBOTTOM, FromDIP(4));
	sectionSummaryLabel_ = new wxStaticText(currentSectionPanel, wxID_ANY, "");
	currentSectionSizer->Add(sectionSummaryLabel_, 0, wxBOTTOM, FromDIP(4));
	selectionSummaryLabel_ = new wxStaticText(currentSectionPanel, wxID_ANY, "");
	selectionSummaryLabel_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
	currentSectionSizer->Add(selectionSummaryLabel_, 0, wxBOTTOM, FromDIP(4));
	paletteBrushActionsSizer->Add(addBrushButton_, 0, wxRIGHT | wxBOTTOM, FromDIP(4));
	paletteBrushActionsSizer->Add(moveToPaletteButton_, 0, wxRIGHT | wxBOTTOM, FromDIP(4));
	paletteBrushActionsSizer->Add(removeBrushButton_, 0, wxRIGHT | wxBOTTOM, FromDIP(4));
	paletteBrushActionsSizer->Add(moveUpButton_, 0, wxRIGHT | wxBOTTOM, FromDIP(4));
	paletteBrushActionsSizer->Add(moveDownButton_, 0, wxBOTTOM, FromDIP(4));
	currentSectionSizer->Add(paletteBrushActionsSizer, 0, wxBOTTOM, FromDIP(4));
	sectionFilterCtrl_ = new wxTextCtrl(currentSectionPanel, wxID_ANY);
	sectionFilterCtrl_->SetHint("Filter entries");
	currentSectionSizer->Add(sectionFilterCtrl_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
	moveDestinationSizer->Add(new wxStaticText(currentSectionPanel, wxID_ANY, "Destination Palette Category"), 0, wxALIGN_CENTER_VERTICAL);
	moveDestinationSizer->Add(moveDestinationFamilyChoice_, 1, wxEXPAND);
	moveDestinationSizer->Add(new wxStaticText(currentSectionPanel, wxID_ANY, "Destination Palette"), 0, wxALIGN_CENTER_VERTICAL);
	moveDestinationSizer->Add(moveDestinationPaletteChoice_, 1, wxEXPAND);
	currentSectionSizer->Add(moveDestinationSizer, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
	sectionBrushGrid_ = new MaterialsWorkbenchBrushGridPanel(currentSectionPanel);
	currentSectionSizer->Add(sectionBrushGrid_, 1, wxEXPAND);
	currentSectionPanel->SetSizer(currentSectionSizer);

	wxPanel* availablePanel = new wxPanel(contentSplitter_, wxID_ANY);
	wxBoxSizer* availableSizer = new wxBoxSizer(wxVERTICAL);
	wxFlexGridSizer* sourceGridSizer = new wxFlexGridSizer(2, 2, FromDIP(4), FromDIP(6));
	availableBrushFamilyChoice_ = new wxChoice(availablePanel, wxID_ANY);
	availableBrushPaletteChoice_ = new wxChoice(availablePanel, wxID_ANY);
	availableBrushFamilyChoice_->SetMinSize(wxSize(FromDIP(140), -1));
	availableBrushPaletteChoice_->SetMinSize(wxSize(FromDIP(230), -1));
	availableBrushFamilyChoice_->SetMaxSize(wxSize(FromDIP(280), -1));
	availableBrushPaletteChoice_->SetMaxSize(wxSize(FromDIP(280), -1));
	wxStaticText* sourceLibraryTitle = new wxStaticText(availablePanel, wxID_ANY, "Source Catalog");
	availableSizer->Add(sourceLibraryTitle, 0, wxBOTTOM, FromDIP(4));
	sourceGridSizer->Add(new wxStaticText(availablePanel, wxID_ANY, "Palette Category"), 0, wxALIGN_CENTER_VERTICAL);
	sourceGridSizer->Add(availableBrushFamilyChoice_, 0);
	sourceGridSizer->Add(new wxStaticText(availablePanel, wxID_ANY, "Palette"), 0, wxALIGN_CENTER_VERTICAL);
	sourceGridSizer->Add(availableBrushPaletteChoice_, 0);
	availableSizer->Add(sourceGridSizer, 0, wxBOTTOM, FromDIP(6));
	availableBrushSummaryLabel_ = new wxStaticText(availablePanel, wxID_ANY, "");
	availableSizer->Add(availableBrushSummaryLabel_, 0, wxBOTTOM, FromDIP(4));
	wxFlexGridSizer* sourceFilterSizer = new wxFlexGridSizer(1, 2, 0, FromDIP(6));
	sourceFilterSizer->AddGrowableCol(1, 1);
	sourceKindChoice_ = new wxChoice(availablePanel, wxID_ANY);
	sourceKindChoice_->Append("All");
	sourceKindChoice_->Append("Brushes");
	sourceKindChoice_->Append("Items");
	sourceKindChoice_->SetSelection(0);
	sourceFilterCtrl_ = new wxTextCtrl(availablePanel, wxID_ANY);
	sourceFilterCtrl_->SetHint("Filter catalog");
	sourceFilterSizer->Add(sourceKindChoice_, 0, wxALIGN_CENTER_VERTICAL);
	sourceFilterSizer->Add(sourceFilterCtrl_, 1, wxEXPAND);
	availableSizer->Add(sourceFilterSizer, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
	availableBrushGrid_ = new MaterialsWorkbenchBrushGridPanel(availablePanel);
	availableSizer->Add(availableBrushGrid_, 1, wxEXPAND);
	availablePanel->SetSizer(availableSizer);

	contentSplitter_->SplitVertically(currentSectionPanel, availablePanel, FromDIP(680));
	contentSplitter_->CallAfter([this]() { EnsureContentSplitterDefaultSash(); });

	wxBoxSizer* actionSizer = new wxBoxSizer(wxHORIZONTAL);
	actionSizer->Add(createPaletteGroupButton_, 0, wxRIGHT, FromDIP(2));
	actionSizer->Add(renamePaletteGroupButton_, 0, wxRIGHT, FromDIP(2));
	actionSizer->Add(deletePaletteGroupButton_, 0, wxRIGHT, FromDIP(4));
	actionSizer->Add(createPaletteButton_, 0, wxRIGHT, FromDIP(2));
	actionSizer->Add(renamePaletteButton_, 0, wxRIGHT, FromDIP(2));
	actionSizer->Add(deletePaletteButton_, 0);
	wxBoxSizer* footerSizer = new wxBoxSizer(wxHORIZONTAL);
	footerSizer->Add(statusLabel_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
	footerSizer->Add(actionSizer, 0, wxALIGN_CENTER_VERTICAL);

	rootSizer->Add(headerSizer, 0, wxEXPAND | wxALL, FromDIP(8));
	rootSizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));
	rootSizer->Add(toolbarSizer, 0, wxEXPAND | wxALL, FromDIP(8));
	rootSizer->Add(contentSplitter_, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));
	rootSizer->Add(footerSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));
	SetSizer(rootSizer);

	StylePaletteWorkspaceActionButton(createPaletteGroupButton_, "Create a new palette category.");
	StylePaletteWorkspaceActionButton(renamePaletteGroupButton_, "Rename the selected custom palette category.");
	StylePaletteWorkspaceActionButton(deletePaletteGroupButton_, "Delete the selected custom palette category.");
	StylePaletteWorkspaceActionButton(createPaletteButton_, "Create a new palette.");
	StylePaletteWorkspaceActionButton(renamePaletteButton_, "Rename the current palette.");
	StylePaletteWorkspaceActionButton(deletePaletteButton_, "Delete the current palette.");
	StylePaletteWorkspaceActionButton(addBrushButton_, "Add the selected catalog entry as a new entry in this palette.");
	StylePaletteWorkspaceActionButton(moveToPaletteButton_, "Move the selected entry to the destination palette.");
	StylePaletteWorkspaceActionButton(removeBrushButton_, "Remove the selected entry from this palette.");
	StylePaletteWorkspaceActionButton(moveUpButton_, "Move the selected entry earlier in the palette.");
	StylePaletteWorkspaceActionButton(moveDownButton_, "Move the selected entry later in the palette.");
	moveDestinationFamilyChoice_->SetToolTip("Choose the destination palette category for moving the selected entry.");
	moveDestinationPaletteChoice_->SetToolTip("Choose the destination palette for moving the selected entry.");

	createPaletteButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchPalettePanel::OnCreatePalette, this);
	renamePaletteButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchPalettePanel::OnRenamePalette, this);
	deletePaletteButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchPalettePanel::OnDeletePalette, this);
	paletteGroupChoice_->Bind(wxEVT_CHOICE, &MaterialsWorkbenchPalettePanel::OnPaletteGroupChanged, this);
	createPaletteGroupButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchPalettePanel::OnCreatePaletteGroup, this);
	renamePaletteGroupButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchPalettePanel::OnRenamePaletteGroup, this);
	deletePaletteGroupButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchPalettePanel::OnDeletePaletteGroup, this);
	availableBrushFamilyChoice_->Bind(wxEVT_CHOICE, &MaterialsWorkbenchPalettePanel::OnAvailableBrushFamilyChanged, this);
	availableBrushPaletteChoice_->Bind(wxEVT_CHOICE, &MaterialsWorkbenchPalettePanel::OnAvailableBrushPaletteChanged, this);
	moveDestinationFamilyChoice_->Bind(wxEVT_CHOICE, &MaterialsWorkbenchPalettePanel::OnMoveDestinationFamilyChanged, this);
	moveDestinationPaletteChoice_->Bind(wxEVT_CHOICE, &MaterialsWorkbenchPalettePanel::OnMoveDestinationPaletteChanged, this);
	addBrushButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchPalettePanel::OnAddBrush, this);
	moveToPaletteButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchPalettePanel::OnMoveBrushToPalette, this);
	removeBrushButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchPalettePanel::OnRemoveBrush, this);
	moveUpButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchPalettePanel::OnMoveBrushUp, this);
	moveDownButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchPalettePanel::OnMoveBrushDown, this);

	sectionBrushGrid_->SetSelectionChangedHandler([this](int index) {
		if (index >= 0 && index < static_cast<int>(visibleEntryLocations_.size())) {
			currentSectionIndex_ = visibleEntryLocations_[index].first;
			selectedSectionEntryIndex_ = visibleEntryLocations_[index].second;
		} else {
			currentSectionIndex_ = 0;
			selectedSectionEntryIndex_ = -1;
		}
		UpdateButtonState();
	});
	availableBrushGrid_->SetSelectionChangedHandler([this](int index) {
		selectedAvailableBrushListIndex_ = index;
		UpdateButtonState();
	});
	availableBrushGrid_->SetSelectionActivatedHandler([this](int index) {
		selectedAvailableBrushListIndex_ = index;
		UpdateButtonState();
		if (addBrushButton_ && addBrushButton_->IsEnabled()) {
			wxCommandEvent dummy;
			OnAddBrush(dummy);
		}
	});
	sectionFilterCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent &event) {
		sectionFilterQuery_ = TrimmedCopy(event.GetString());
		preserveSectionGridViewStart_ = true;
		RefreshSectionEntries();
		UpdateButtonState();
	});
	sourceKindChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) {
		sourceKindFilter_ = sourceKindChoice_->GetSelection();
		RefreshAvailableBrushes();
		UpdateButtonState();
	});
	sourceFilterCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent &event) {
		sourceFilterQuery_ = TrimmedCopy(event.GetString());
		RefreshAvailableBrushes();
		UpdateButtonState();
	});
}

void MaterialsWorkbenchPalettePanel::ClearWorkspace(const wxString &message) {
	hasPalette_ = false;
	palette_ = TilesetStorageRecord();
	preserveSectionGridViewStart_ = false;
	availableBrushSources_.clear();
	currentAvailableEntries_.clear();
	currentAvailableEntrySectionIndexes_.clear();
	currentAvailableSourceTilesetIndex_ = -1;
	sectionFilterQuery_.clear();
	sourceFilterQuery_.clear();
	sourceKindFilter_ = 0;
	availableBrushFamilyKeys_.clear();
	availableBrushPaletteSourceIndexes_.clear();
	moveDestinationFamilyKeys_.clear();
	moveDestinationPaletteIndexes_.clear();
	paletteGroupKeys_.clear();
	visibleEntryLocations_.clear();
	currentSectionIndex_ = 0;
	selectedSectionEntryIndex_ = -1;
	selectedAvailableBrushListIndex_ = -1;

	titleLabel_->SetLabel("Select a palette");
	sourceLabel_->SetLabel("Palette categories stay visible even when no palette is open.");
	RefreshPaletteGroupChoice();
	availableBrushFamilyChoice_->Clear();
	availableBrushPaletteChoice_->Clear();
	moveDestinationFamilyChoice_->Clear();
	moveDestinationPaletteChoice_->Clear();
	sectionSummaryLabel_->SetLabel(message);
	selectionSummaryLabel_->SetLabel("Select an entry to remove, reorder, or move.");
	availableBrushSummaryLabel_->SetLabel("Select a palette to browse entries.");
	sectionBrushGrid_->SetEmptyMessage(message);
	sectionBrushGrid_->Clear();
	availableBrushGrid_->SetEmptyMessage("Select a palette to browse entries.");
	availableBrushGrid_->Clear();
	if (sectionFilterCtrl_) {
		sectionFilterCtrl_->ChangeValue(wxString());
	}
	if (sourceKindChoice_) {
		sourceKindChoice_->SetSelection(0);
	}
	if (sourceFilterCtrl_) {
		sourceFilterCtrl_->ChangeValue(wxString());
	}
	SetStatusMessage(message);
	UpdateButtonState();
}

bool MaterialsWorkbenchPalettePanel::LoadPalette(const TilesetStorageRecord &tileset) {
	const bool isSamePalette = hasPalette_ && palette_.name.IsSameAs(tileset.name, false);
	palette_ = tileset;
	hasPalette_ = true;
	preserveSectionGridViewStart_ = isSamePalette;
	if (isSamePalette) {
	} else {
		currentSectionIndex_ = 0;
		selectedSectionEntryIndex_ = -1;
	}
	selectedAvailableBrushListIndex_ = -1;
	RefreshWorkspace();
	return true;
}

void MaterialsWorkbenchPalettePanel::RefreshWorkspace() {
	titleLabel_->SetLabel("Palette: " + palette_.name);
	sourceLabel_->SetLabel("Stored in " + (palette_.sourceFile.IsEmpty() ? wxString("materials.db") : palette_.sourceFile));
	RefreshPaletteGroupChoice();
	RefreshSectionChoice();
	RebuildAvailableBrushSources();
	RefreshAvailableBrushFamilies();
	RefreshAvailableBrushPalettes();
	RefreshMoveDestinationFamilies();
	RefreshMoveDestinationPalettes();
	RefreshSectionEntries();
	RefreshAvailableBrushes();
	SetStatusMessage("Ready. Edit palette category and entries.");
	UpdateButtonState();
	Layout();
	if (contentSplitter_ && !contentSplitterCentered_) {
		contentSplitter_->CallAfter([this]() { EnsureContentSplitterDefaultSash(); });
	}
}

void MaterialsWorkbenchPalettePanel::EnsureContentSplitterDefaultSash() {
	if (contentSplitterCentered_ || !contentSplitter_) {
		return;
	}
	if (!addBrushButton_ || !moveToPaletteButton_ || !removeBrushButton_ || !moveUpButton_ || !moveDownButton_) {
		return;
	}

	const int width = contentSplitter_->GetClientSize().GetWidth();
	if (width <= 0) {
		return;
	}

	const int buttonGap = FromDIP(4);
	int desiredLeft = 0;
	const int buttonCount = 5;
	const wxSize buttonSizes[] = {
		addBrushButton_->GetBestSize(),
		moveToPaletteButton_->GetBestSize(),
		removeBrushButton_->GetBestSize(),
		moveUpButton_->GetBestSize(),
		moveDownButton_->GetBestSize(),
	};
	for (const wxSize &size : buttonSizes) {
		desiredLeft += size.x;
	}
	desiredLeft += buttonGap * (buttonCount - 1);
	desiredLeft += FromDIP(24);

	const int minimumPane = contentSplitter_->GetMinimumPaneSize();
	const int maxLeft = std::max(minimumPane, width - minimumPane);
	desiredLeft = std::clamp(desiredLeft, minimumPane, maxLeft);
	contentSplitter_->SetSashPosition(desiredLeft);
	contentSplitterCentered_ = true;
}

void MaterialsWorkbenchPalettePanel::RefreshPaletteGroupChoice() {
	paletteGroupChoice_->Clear();
	paletteGroupKeys_.clear();

	for (const PaletteGroupRecord &group : controller_.GetPaletteGroups()) {
		paletteGroupChoice_->Append(BuildPaletteGroupLabel(group));
		paletteGroupKeys_.push_back(group.name);
	}

	if (paletteGroupKeys_.empty()) {
		return;
	}

	const wxString currentGroupName = palette_.paletteGroupName.IsEmpty() ? wxString("other") : palette_.paletteGroupName;
	int selectedGroupIndex = FindPaletteGroupChoiceIndexByName(currentGroupName);
	if (selectedGroupIndex == wxNOT_FOUND) {
		selectedGroupIndex = FindPaletteGroupChoiceIndexByName("other");
	}
	if (selectedGroupIndex == wxNOT_FOUND) {
		selectedGroupIndex = 0;
	}
	paletteGroupChoice_->SetSelection(selectedGroupIndex);
}

void MaterialsWorkbenchPalettePanel::RefreshSectionChoice() {
	currentSectionChoice_->Clear();

	for (const TilesetSectionRecord &section : palette_.sections) {
		currentSectionChoice_->Append(BuildSectionLabel(section));
	}

	if (!palette_.sections.empty()) {
		currentSectionIndex_ = std::clamp(currentSectionIndex_, 0, static_cast<int>(palette_.sections.size()) - 1);
		currentSectionChoice_->SetSelection(currentSectionIndex_);
	} else {
		currentSectionIndex_ = 0;
	}
}

void MaterialsWorkbenchPalettePanel::RefreshSectionEntries() {
	visibleEntryLocations_.clear();

	if (!hasPalette_ || palette_.sections.empty()) {
		sectionSummaryLabel_->SetLabel("This palette has no entries yet.");
		sectionBrushGrid_->SetEmptyMessage("No entries in this palette yet.\nChoose one on the right and click Add Entry.");
		sectionBrushGrid_->Clear();
		currentSectionIndex_ = 0;
		selectedSectionEntryIndex_ = -1;
		return;
	}

	std::vector<BrushGridItem> items;
	int unsupportedEntries = 0;
	int missingPreviews = 0;
	int hiddenOtherFamilyEntries = 0;
	int filteredEntries = 0;
	const wxString filterQueryLower = sectionFilterQuery_.Lower();
	const wxString displayFamily = ResolvePaletteBrushDisplayFamily(palette_);
	for (size_t sectionIndex = 0; sectionIndex < palette_.sections.size(); ++sectionIndex) {
		const TilesetSectionRecord &section = palette_.sections[sectionIndex];
		if (!DerivePaletteGroupFromSectionType(section.sectionType).IsSameAs(displayFamily, false)) {
			hiddenOtherFamilyEntries += static_cast<int>(section.entries.size());
			continue;
		}
		for (size_t entryIndex = 0; entryIndex < section.entries.size(); ++entryIndex) {
			const TilesetEntryRecord &entry = section.entries[entryIndex];
			const wxString baseLabel = DescribePaletteEntry(controller_, entry);
			const wxString displayLabel = baseLabel;
			if (!filterQueryLower.IsEmpty() && !displayLabel.Lower().Contains(filterQueryLower)) {
				++filteredEntries;
				continue;
			}
			const int visibleIndex = static_cast<int>(visibleEntryLocations_.size());

			if (entry.entryKind.IsSameAs("brush", false)) {
				Brush* brush = g_brushes.getBrush(entry.brushName.ToStdString());
				const BrushRecord* catalogBrush = FindCatalogBrushRecord(controller_, entry.brushId, entry.brushName);
				const int lookId = brush ? brush->getLookID() : (catalogBrush ? catalogBrush->lookId : 0);
				if (!brush && lookId <= 0) {
					++missingPreviews;
					continue;
				}

				visibleEntryLocations_.push_back({ static_cast<int>(sectionIndex), static_cast<int>(entryIndex) });
				items.push_back({ displayLabel, BuildPaletteEntryTooltip(controller_, palette_, section, entry), section.sectionType.IsSameAs("raw", false) ? wxString("RAW") : wxString(), brush, lookId, visibleIndex });
				continue;
			}

			if (entry.entryKind.IsSameAs("item", false)) {
				const int previewItemId = ResolveEntryPreviewItemId(entry);
				if (previewItemId <= 0) {
					++missingPreviews;
					continue;
				}

				Brush* brush = nullptr;
				if (auto type = g_items.getRawItemType(static_cast<uint16_t>(previewItemId)); type && type->id != 0) {
					brush = type->raw_brush;
				}

				visibleEntryLocations_.push_back({ static_cast<int>(sectionIndex), static_cast<int>(entryIndex) });
				items.push_back({ displayLabel, BuildPaletteEntryTooltip(controller_, palette_, section, entry), section.sectionType.IsSameAs("raw", false) ? wxString("RAW") : wxString(), brush, previewItemId, visibleIndex });
				continue;
			}

			++unsupportedEntries;
		}
	}

	wxString summary = wxString::Format("%zu entries in this palette.", items.size());
	if (filteredEntries > 0) {
		summary += wxString::Format(" %d entries are hidden by the filter.", filteredEntries);
	}
	if (hiddenOtherFamilyEntries > 0) {
		summary += wxString::Format(" %d entries from other families are hidden in this view.", hiddenOtherFamilyEntries);
	}
	if (missingPreviews > 0) {
		summary += wxString::Format(" %d entries are hidden until they have preview data.", missingPreviews);
	}
	if (unsupportedEntries > 0) {
		summary += wxString::Format(" %d entries use a storage format that is not shown here yet.", unsupportedEntries);
	}
	sectionSummaryLabel_->SetLabel(summary);
	sectionBrushGrid_->SetEmptyMessage("This palette has entries, but none can be previewed yet.");

	int desiredSelection = FindVisibleEntryIndex(visibleEntryLocations_, currentSectionIndex_, selectedSectionEntryIndex_);
	if (desiredSelection < 0 && !items.empty()) {
		desiredSelection = items.front().index;
	}
	sectionBrushGrid_->SetItems(items, desiredSelection, preserveSectionGridViewStart_);
	preserveSectionGridViewStart_ = false;
	if (items.empty()) {
		currentSectionIndex_ = 0;
		selectedSectionEntryIndex_ = -1;
	}
}

void MaterialsWorkbenchPalettePanel::RebuildAvailableBrushSources() {
	availableBrushSources_.clear();

	const std::vector<TilesetStorageRecord> &tilesets = controller_.GetTilesets();
	const wxString familyOrder[] = { "terrain", "doodad", "item", "other" };
	for (size_t i = 0; i < tilesets.size(); ++i) {
		const TilesetStorageRecord &tileset = tilesets[i];
		std::set<wxString> families;
		for (const TilesetSectionRecord &section : tileset.sections) {
			families.insert(DerivePaletteGroupFromSectionType(section.sectionType));
		}

		for (const wxString &familyKey : familyOrder) {
			if (families.find(familyKey) == families.end()) {
				continue;
			}
			MaterialsWorkbenchAvailableBrushSource source;
			source.familyKey = familyKey;
			source.paletteLabel = tileset.name;
			source.tilesetIndex = static_cast<int>(i);
			availableBrushSources_.push_back(std::move(source));
		}
	}
}

void MaterialsWorkbenchPalettePanel::RefreshAvailableBrushFamilies() {
	const wxString previousFamily = availableBrushFamilyChoice_->GetSelection() != wxNOT_FOUND && availableBrushFamilyChoice_->GetSelection() < static_cast<int>(availableBrushFamilyKeys_.size())
		? availableBrushFamilyKeys_[availableBrushFamilyChoice_->GetSelection()]
		: wxString();

	availableBrushFamilyChoice_->Clear();
	availableBrushFamilyKeys_.clear();

	for (const MaterialsWorkbenchAvailableBrushSource &source : availableBrushSources_) {
		if (std::find(availableBrushFamilyKeys_.begin(), availableBrushFamilyKeys_.end(), source.familyKey) != availableBrushFamilyKeys_.end()) {
			continue;
		}
		availableBrushFamilyChoice_->Append(BuildPaletteFamilyLabel(source.familyKey));
		availableBrushFamilyKeys_.push_back(source.familyKey);
	}

	if (availableBrushFamilyKeys_.empty()) {
		return;
	}

	const wxString recommendedFamily = previousFamily.IsEmpty() ? RecommendBrushGroupForCurrentSection() : previousFamily;
	int selection = 0;
	for (size_t i = 0; i < availableBrushFamilyKeys_.size(); ++i) {
		if (availableBrushFamilyKeys_[i].IsSameAs(recommendedFamily, false)) {
			selection = static_cast<int>(i);
			break;
		}
	}
	availableBrushFamilyChoice_->SetSelection(selection);
}

void MaterialsWorkbenchPalettePanel::RefreshAvailableBrushPalettes() {
	const wxString previousPalette = availableBrushPaletteChoice_->GetStringSelection();
	availableBrushPaletteChoice_->Clear();
	availableBrushPaletteSourceIndexes_.clear();

	const int familySelection = availableBrushFamilyChoice_->GetSelection();
	if (familySelection == wxNOT_FOUND || familySelection >= static_cast<int>(availableBrushFamilyKeys_.size())) {
		return;
	}

	const wxString &familyKey = availableBrushFamilyKeys_[familySelection];
	for (size_t i = 0; i < availableBrushSources_.size(); ++i) {
		const MaterialsWorkbenchAvailableBrushSource &source = availableBrushSources_[i];
		if (!source.familyKey.IsSameAs(familyKey, false)) {
			continue;
		}
		availableBrushPaletteChoice_->Append(source.paletteLabel);
		availableBrushPaletteSourceIndexes_.push_back(static_cast<int>(i));
	}

	if (availableBrushPaletteSourceIndexes_.empty()) {
		return;
	}

	int selection = 0;
	if (!previousPalette.IsEmpty()) {
		for (size_t i = 0; i < availableBrushPaletteSourceIndexes_.size(); ++i) {
			if (availableBrushSources_[availableBrushPaletteSourceIndexes_[i]].paletteLabel.IsSameAs(previousPalette, false)) {
				selection = static_cast<int>(i);
				break;
			}
		}
	} else if (hasPalette_ && familyKey.IsSameAs(palette_.paletteGroupName, false)) {
		for (size_t i = 0; i < availableBrushPaletteSourceIndexes_.size(); ++i) {
			if (availableBrushSources_[availableBrushPaletteSourceIndexes_[i]].paletteLabel.IsSameAs(palette_.name, false)) {
				selection = static_cast<int>(i);
				break;
			}
		}
	}
	availableBrushPaletteChoice_->SetSelection(selection);
}

void MaterialsWorkbenchPalettePanel::RefreshAvailableBrushes() {
	currentAvailableEntries_.clear();
	currentAvailableEntrySectionIndexes_.clear();
	currentAvailableSourceTilesetIndex_ = -1;
	selectedAvailableBrushListIndex_ = -1;

	if (availableBrushPaletteChoice_->GetSelection() == wxNOT_FOUND) {
		availableBrushSummaryLabel_->SetLabel("No source available.");
		availableBrushGrid_->SetEmptyMessage("No source available.");
		availableBrushGrid_->Clear();
		return;
	}

	const int sourceChoiceIndex = availableBrushPaletteChoice_->GetSelection();
	if (sourceChoiceIndex < 0 || sourceChoiceIndex >= static_cast<int>(availableBrushPaletteSourceIndexes_.size())) {
		availableBrushSummaryLabel_->SetLabel("No source available.");
		availableBrushGrid_->SetEmptyMessage("No source available.");
		availableBrushGrid_->Clear();
		return;
	}

	const MaterialsWorkbenchAvailableBrushSource &source = availableBrushSources_[availableBrushPaletteSourceIndexes_[sourceChoiceIndex]];
	const std::vector<TilesetStorageRecord> &tilesets = controller_.GetTilesets();
	if (source.tilesetIndex < 0 || source.tilesetIndex >= static_cast<int>(tilesets.size())) {
		availableBrushSummaryLabel_->SetLabel("No source available.");
		availableBrushGrid_->SetEmptyMessage("No source available.");
		availableBrushGrid_->Clear();
		return;
	}
	currentAvailableSourceTilesetIndex_ = source.tilesetIndex;
	const TilesetStorageRecord &sourceTileset = tilesets[static_cast<size_t>(source.tilesetIndex)];

	std::vector<BrushGridItem> items;
	int brushEntryCount = 0;
	int itemEntryCount = 0;
	int hiddenOtherFamilyEntries = 0;
	int filteredEntries = 0;
	const wxString filterQueryLower = sourceFilterQuery_.Lower();

	for (size_t sectionIndex = 0; sectionIndex < sourceTileset.sections.size(); ++sectionIndex) {
		const TilesetSectionRecord &section = sourceTileset.sections[sectionIndex];
		if (!DerivePaletteGroupFromSectionType(section.sectionType).IsSameAs(source.familyKey, false)) {
			hiddenOtherFamilyEntries += static_cast<int>(section.entries.size());
			continue;
		}

		for (const TilesetEntryRecord &entry : section.entries) {
			if (sourceKindFilter_ == 1 && !entry.entryKind.IsSameAs("brush", false)) {
				++filteredEntries;
				continue;
			}
			if (sourceKindFilter_ == 2 && !entry.entryKind.IsSameAs("item", false)) {
				++filteredEntries;
				continue;
			}
			const wxString label = DescribePaletteEntry(controller_, entry);
			if (!filterQueryLower.IsEmpty() && !label.Lower().Contains(filterQueryLower)) {
				++filteredEntries;
				continue;
			}
			const wxString tooltip = BuildPaletteEntryTooltip(controller_, sourceTileset, section, entry);
			const int visibleIndex = static_cast<int>(currentAvailableEntries_.size());

			Brush* previewBrush = nullptr;
			int previewLookId = 0;
			if (entry.entryKind.IsSameAs("brush", false)) {
				wxString resolvedName = entry.brushName;
				if (resolvedName.IsEmpty()) {
					if (const BrushRecord* catalogBrush = FindCatalogBrushRecord(controller_, entry.brushId, entry.brushName)) {
						resolvedName = catalogBrush->name;
						previewLookId = catalogBrush->lookId;
					}
				}
				if (!resolvedName.IsEmpty()) {
					previewBrush = g_brushes.getBrush(resolvedName.ToStdString());
				}
				if (previewBrush) {
					previewLookId = previewBrush->getLookID();
				}
				++brushEntryCount;
			} else if (entry.entryKind.IsSameAs("item", false)) {
				const int previewItemId = ResolveEntryPreviewItemId(entry);
				previewLookId = previewItemId;
				if (auto type = previewItemId > 0 ? g_items.getRawItemType(static_cast<uint16_t>(previewItemId)) : nullptr; type && type->id != 0) {
					previewBrush = type->raw_brush;
				}
				++itemEntryCount;
			}

			currentAvailableEntries_.push_back(entry);
			currentAvailableEntrySectionIndexes_.push_back(static_cast<int>(sectionIndex));
			items.push_back({ label, tooltip, section.sectionType.IsSameAs("raw", false) ? wxString("RAW") : wxString(), previewBrush, previewLookId, visibleIndex });
		}
	}

	wxString summary = wxString::Format("%zu entries available in %s / %s.", items.size(), BuildPaletteFamilyLabel(source.familyKey), source.paletteLabel);
	if (brushEntryCount > 0 || itemEntryCount > 0) {
		summary += wxString::Format(" %d brush / %d item.", brushEntryCount, itemEntryCount);
	}
	if (filteredEntries > 0) {
		summary += wxString::Format(" %d entries are hidden by filters.", filteredEntries);
	}
	if (hiddenOtherFamilyEntries > 0) {
		summary += wxString::Format(" %d entries from other families are hidden in this view.", hiddenOtherFamilyEntries);
	}
	availableBrushSummaryLabel_->SetLabel(summary);
	availableBrushGrid_->SetEmptyMessage("No entries available in this source palette.");
	availableBrushGrid_->SetItems(items);
}

void MaterialsWorkbenchPalettePanel::RefreshMoveDestinationFamilies() {
	const wxString previousGroup = moveDestinationFamilyChoice_->GetSelection() != wxNOT_FOUND && moveDestinationFamilyChoice_->GetSelection() < static_cast<int>(moveDestinationFamilyKeys_.size())
		? moveDestinationFamilyKeys_[moveDestinationFamilyChoice_->GetSelection()]
		: wxString();

	moveDestinationFamilyChoice_->Clear();
	moveDestinationFamilyKeys_.clear();

	for (const TilesetStorageRecord &tileset : controller_.GetTilesets()) {
		if (hasPalette_ && tileset.name.IsSameAs(palette_.name, false)) {
			continue;
		}

		const wxString groupKey = ResolvePaletteGroupKey(tileset);
		if (std::find(moveDestinationFamilyKeys_.begin(), moveDestinationFamilyKeys_.end(), groupKey) != moveDestinationFamilyKeys_.end()) {
			continue;
		}

		moveDestinationFamilyChoice_->Append(BuildPaletteFamilyLabel(groupKey));
		moveDestinationFamilyKeys_.push_back(groupKey);
	}

	if (moveDestinationFamilyKeys_.empty()) {
		return;
	}

	const wxString preferredGroup = previousGroup.IsEmpty() ? ResolvePaletteGroupKey(palette_) : previousGroup;
	int selection = wxNOT_FOUND;
	for (size_t i = 0; i < moveDestinationFamilyKeys_.size(); ++i) {
		if (moveDestinationFamilyKeys_[i].IsSameAs(preferredGroup, false)) {
			selection = static_cast<int>(i);
			break;
		}
	}
	if (selection == wxNOT_FOUND) {
		const wxString preferredRuntimeFamily = RecommendBrushGroupForCurrentSection();
		for (size_t i = 0; i < moveDestinationFamilyKeys_.size(); ++i) {
			for (const TilesetStorageRecord &tileset : controller_.GetTilesets()) {
				if (!ResolvePaletteGroupKey(tileset).IsSameAs(moveDestinationFamilyKeys_[i], false)) {
					continue;
				}
				if (ResolvePaletteGroupRuntimeFamilyKey(tileset).IsSameAs(preferredRuntimeFamily, false)) {
					selection = static_cast<int>(i);
					break;
				}
			}
			if (selection != wxNOT_FOUND) {
				break;
			}
		}
	}
	if (selection == wxNOT_FOUND) {
		selection = 0;
	}
	moveDestinationFamilyChoice_->SetSelection(selection);
}

void MaterialsWorkbenchPalettePanel::RefreshMoveDestinationPalettes() {
	const wxString previousPalette = moveDestinationPaletteChoice_->GetStringSelection();
	moveDestinationPaletteChoice_->Clear();
	moveDestinationPaletteIndexes_.clear();

	const int familySelection = moveDestinationFamilyChoice_->GetSelection();
	if (familySelection == wxNOT_FOUND || familySelection >= static_cast<int>(moveDestinationFamilyKeys_.size())) {
		return;
	}

	const wxString &groupKey = moveDestinationFamilyKeys_[familySelection];
	const std::vector<TilesetStorageRecord> &tilesets = controller_.GetTilesets();
	for (size_t i = 0; i < tilesets.size(); ++i) {
		const TilesetStorageRecord &tileset = tilesets[i];
		if (hasPalette_ && tileset.name.IsSameAs(palette_.name, false)) {
			continue;
		}

		const wxString tilesetGroup = ResolvePaletteGroupKey(tileset);
		if (!tilesetGroup.IsSameAs(groupKey, false)) {
			continue;
		}

		moveDestinationPaletteChoice_->Append(tileset.name);
		moveDestinationPaletteIndexes_.push_back(static_cast<int>(i));
	}

	if (moveDestinationPaletteIndexes_.empty()) {
		return;
	}

	int selection = 0;
	if (!previousPalette.IsEmpty()) {
		for (size_t i = 0; i < moveDestinationPaletteIndexes_.size(); ++i) {
			if (tilesets[moveDestinationPaletteIndexes_[i]].name.IsSameAs(previousPalette, false)) {
				selection = static_cast<int>(i);
				break;
			}
		}
	}
	moveDestinationPaletteChoice_->SetSelection(selection);
}

void MaterialsWorkbenchPalettePanel::RefreshSelectionFeedback() {
	if (!hasPalette_) {
		selectionSummaryLabel_->SetLabel("Select an entry to remove, reorder, or move.");
		return;
	}

	const int selectedVisibleIndex = GetSelectedVisibleEntryIndex();
	if (selectedVisibleIndex < 0) {
		selectionSummaryLabel_->SetLabel("Select an entry to remove, reorder, or move.");
		return;
	}

	int sectionIndex = 0;
	int entryIndex = 0;
	if (!ResolveVisibleEntryLocation(selectedVisibleIndex, sectionIndex, entryIndex) ||
		sectionIndex < 0 || sectionIndex >= static_cast<int>(palette_.sections.size()) ||
		entryIndex < 0 || entryIndex >= static_cast<int>(palette_.sections[sectionIndex].entries.size())) {
		selectionSummaryLabel_->SetLabel("Select an entry to remove, reorder, or move.");
		return;
	}

	const TilesetEntryRecord &entry = palette_.sections[sectionIndex].entries[entryIndex];
	const wxString entryLabel = DescribePaletteEntry(controller_, entry);
	if (!IsMovablePaletteEntry(entry)) {
		selectionSummaryLabel_->SetLabel("Selected entry: " + entryLabel + ". This format cannot move to another palette yet.");
		return;
	}

	if (moveDestinationPaletteChoice_->GetSelection() == wxNOT_FOUND || moveDestinationPaletteChoice_->GetSelection() >= static_cast<int>(moveDestinationPaletteIndexes_.size())) {
		selectionSummaryLabel_->SetLabel("Selected entry: " + entryLabel + ". Create another palette to enable moving entries.");
		return;
	}

	const int destinationIndex = moveDestinationPaletteIndexes_[moveDestinationPaletteChoice_->GetSelection()];
	const TilesetStorageRecord &destinationTileset = controller_.GetTilesets()[destinationIndex];
	const wxString destinationGroup = moveDestinationFamilyChoice_->GetSelection() != wxNOT_FOUND && moveDestinationFamilyChoice_->GetSelection() < static_cast<int>(moveDestinationFamilyKeys_.size())
		? BuildPaletteFamilyLabel(moveDestinationFamilyKeys_[moveDestinationFamilyChoice_->GetSelection()])
		: BuildPaletteFamilyLabel(ResolvePaletteGroupKey(destinationTileset));
	selectionSummaryLabel_->SetLabel("Selected entry: " + entryLabel + ". Destination: " + destinationGroup + " / " + destinationTileset.name + ".");
}

int MaterialsWorkbenchPalettePanel::GetSelectedVisibleEntryIndex() const {
	if (selectedSectionEntryIndex_ < 0) {
		return -1;
	}
	return FindVisibleEntryIndex(visibleEntryLocations_, currentSectionIndex_, selectedSectionEntryIndex_);
}

bool MaterialsWorkbenchPalettePanel::ResolveVisibleEntryLocation(int visibleIndex, int &sectionIndex, int &entryIndex) const {
	if (visibleIndex < 0 || visibleIndex >= static_cast<int>(visibleEntryLocations_.size())) {
		return false;
	}
	sectionIndex = visibleEntryLocations_[visibleIndex].first;
	entryIndex = visibleEntryLocations_[visibleIndex].second;
	return true;
}

bool MaterialsWorkbenchPalettePanel::MoveSelectedEntryByOffset(int offset, const wxString &directionLabel) {
	if (!hasPalette_ || offset == 0) {
		return false;
	}

	const int selectedVisibleIndex = GetSelectedVisibleEntryIndex();
	const int targetVisibleIndex = selectedVisibleIndex + offset;
	if (selectedVisibleIndex < 0 || targetVisibleIndex < 0 || targetVisibleIndex >= static_cast<int>(visibleEntryLocations_.size())) {
		return false;
	}

	int sourceSectionIndex = 0;
	int sourceEntryIndex = 0;
	int targetSectionIndex = 0;
	int targetEntryIndex = 0;
	if (!ResolveVisibleEntryLocation(selectedVisibleIndex, sourceSectionIndex, sourceEntryIndex) ||
		!ResolveVisibleEntryLocation(targetVisibleIndex, targetSectionIndex, targetEntryIndex)) {
		return false;
	}

	TilesetEntryRecord movingEntry = palette_.sections[sourceSectionIndex].entries[sourceEntryIndex];
	palette_.sections[sourceSectionIndex].entries.erase(palette_.sections[sourceSectionIndex].entries.begin() + sourceEntryIndex);

	int insertIndex = targetEntryIndex;
	if (offset > 0) {
		insertIndex = targetEntryIndex + 1;
	}
	if (sourceSectionIndex == targetSectionIndex && sourceEntryIndex < insertIndex) {
		--insertIndex;
	}
	insertIndex = std::clamp(insertIndex, 0, static_cast<int>(palette_.sections[targetSectionIndex].entries.size()));
	palette_.sections[targetSectionIndex].entries.insert(palette_.sections[targetSectionIndex].entries.begin() + insertIndex, movingEntry);

	currentSectionIndex_ = targetSectionIndex;
	selectedSectionEntryIndex_ = insertIndex;
	return CommitPalette("Moved entry " + directionLabel + " in palette \"" + palette_.name + "\".");
}

bool MaterialsWorkbenchPalettePanel::IsSelectedMovableEntry() const {
	int sectionIndex = 0;
	int entryIndex = 0;
	TilesetEntryRecord entry;
	return ResolveSelectedMovableEntry(sectionIndex, entryIndex, entry);
}

bool MaterialsWorkbenchPalettePanel::ResolveSelectedMovableEntry(int &sectionIndex, int &entryIndex, TilesetEntryRecord &entry) const {
	const int selectedVisibleIndex = GetSelectedVisibleEntryIndex();
	if (!ResolveVisibleEntryLocation(selectedVisibleIndex, sectionIndex, entryIndex)) {
		return false;
	}
	if (sectionIndex < 0 || sectionIndex >= static_cast<int>(palette_.sections.size())) {
		return false;
	}
	const TilesetSectionRecord &section = palette_.sections[sectionIndex];
	if (entryIndex < 0 || entryIndex >= static_cast<int>(section.entries.size())) {
		return false;
	}
	if (!IsMovablePaletteEntry(section.entries[entryIndex])) {
		return false;
	}
	entry = section.entries[entryIndex];
	return true;
}

bool MaterialsWorkbenchPalettePanel::ResolveMoveDestinationPalette(TilesetStorageRecord &outTileset, wxString &outDisplayLabel) const {
	outTileset = TilesetStorageRecord();
	outDisplayLabel.clear();

	const int familySelection = moveDestinationFamilyChoice_->GetSelection();
	const int paletteSelection = moveDestinationPaletteChoice_->GetSelection();
	if (familySelection == wxNOT_FOUND || familySelection >= static_cast<int>(moveDestinationFamilyKeys_.size()) ||
		paletteSelection == wxNOT_FOUND || paletteSelection >= static_cast<int>(moveDestinationPaletteIndexes_.size())) {
		return false;
	}

	const int tilesetIndex = moveDestinationPaletteIndexes_[paletteSelection];
	const std::vector<TilesetStorageRecord> &tilesets = controller_.GetTilesets();
	if (tilesetIndex < 0 || tilesetIndex >= static_cast<int>(tilesets.size())) {
		return false;
	}

	outTileset = tilesets[tilesetIndex];
	outDisplayLabel = BuildPaletteFamilyLabel(moveDestinationFamilyKeys_[familySelection]) + " / " + outTileset.name;
	return true;
}

void MaterialsWorkbenchPalettePanel::UpdateButtonState() {
	createPaletteButton_->Enable(true);
	renamePaletteButton_->Enable(hasPalette_);
	deletePaletteButton_->Enable(hasPalette_);
	createPaletteGroupButton_->Enable(true);

	const PaletteGroupRecord* selectedGroup = GetSelectedPaletteGroup();
	const bool hasSelectedGroup = selectedGroup != nullptr;
	const bool isBuiltinGroup = hasSelectedGroup && selectedGroup->isBuiltin;
	paletteGroupChoice_->Enable(!paletteGroupKeys_.empty());
	renamePaletteGroupButton_->Enable(hasSelectedGroup && !isBuiltinGroup);
	deletePaletteGroupButton_->Enable(hasSelectedGroup && !isBuiltinGroup);
	availableBrushFamilyChoice_->Enable(hasPalette_ && !availableBrushFamilyKeys_.empty());
	availableBrushPaletteChoice_->Enable(hasPalette_ && !availableBrushPaletteSourceIndexes_.empty());
	moveDestinationFamilyChoice_->Enable(hasPalette_ && !moveDestinationFamilyKeys_.empty());
	moveDestinationPaletteChoice_->Enable(hasPalette_ && !moveDestinationPaletteIndexes_.empty());

	const int selectedVisibleIndex = GetSelectedVisibleEntryIndex();
	addBrushButton_->Enable(hasPalette_ && availableBrushPaletteChoice_->GetSelection() != wxNOT_FOUND && selectedAvailableBrushListIndex_ >= 0);
	moveToPaletteButton_->Enable(hasPalette_ && IsSelectedMovableEntry() && moveDestinationPaletteChoice_->GetSelection() != wxNOT_FOUND);
	removeBrushButton_->Enable(hasPalette_ && selectedVisibleIndex >= 0);
	moveUpButton_->Enable(hasPalette_ && selectedVisibleIndex > 0);
	moveDownButton_->Enable(hasPalette_ && selectedVisibleIndex >= 0 && selectedVisibleIndex < static_cast<int>(visibleEntryLocations_.size()) - 1);
	RefreshSelectionFeedback();
}

void MaterialsWorkbenchPalettePanel::SetStatusMessage(const wxString &message) {
	statusLabel_->SetLabel(message);
}

void MaterialsWorkbenchPalettePanel::NormalizePaletteOrdering(TilesetStorageRecord &tileset) const {
	for (size_t sectionIndex = 0; sectionIndex < tileset.sections.size(); ++sectionIndex) {
		TilesetSectionRecord &section = tileset.sections[sectionIndex];
		section.sortOrder = static_cast<int>(sectionIndex);
		for (size_t entryIndex = 0; entryIndex < section.entries.size(); ++entryIndex) {
			TilesetEntryRecord &entry = section.entries[entryIndex];
			entry.sortOrder = static_cast<int>(entryIndex);
			entry.afterBrushName.clear();
			entry.afterItemId = 0;
			if (entryIndex == 0) {
				continue;
			}

			const TilesetEntryRecord &previousEntry = section.entries[entryIndex - 1];
			const wxString afterBrushName = ComputeAfterBrushName(previousEntry);
			if (!afterBrushName.IsEmpty()) {
				entry.afterBrushName = afterBrushName;
				continue;
			}

			entry.afterItemId = ComputeAfterItemId(previousEntry);
		}
	}
}

bool MaterialsWorkbenchPalettePanel::CommitPalette(const wxString &successMessage, const wxString &previousPaletteName, const wxString &selectionPaletteName) {
	NormalizePaletteOrdering(palette_);

	wxString error;
	if (!controller_.SaveTileset(palette_, previousPaletteName, error)) {
		SetStatusMessage("Failed to save palette: " + error);
		return false;
	}

	SetStatusMessage(successMessage);
	if (onPaletteSaved_) {
		onPaletteSaved_(selectionPaletteName.IsEmpty() ? palette_.name : selectionPaletteName);
	}
	return true;
}

wxString MaterialsWorkbenchPalettePanel::RecommendBrushGroupForCurrentSection() const {
	if (!hasPalette_) {
		return "terrain";
	}

	const wxString displayFamily = ResolvePaletteBrushDisplayFamily(palette_);
	if (!displayFamily.IsEmpty()) {
		return displayFamily;
	}

	if (!palette_.sections.empty() && currentSectionIndex_ >= 0 && currentSectionIndex_ < static_cast<int>(palette_.sections.size())) {
		const wxString sectionType = palette_.sections[currentSectionIndex_].sectionType.Lower();
		if (sectionType.Contains("terrain")) {
			return "terrain";
		}
		if (sectionType.Contains("doodad")) {
			return "doodad";
		}
		if (sectionType.Contains("item")) {
			return "item";
		}
	}

	if (palette_.paletteGroupName.IsSameAs("doodad", false)) {
		return "doodad";
	}
	if (palette_.paletteGroupName.IsSameAs("item", false)) {
		return "item";
	}
	if (palette_.paletteGroupName.IsSameAs("other", false)) {
		return "other";
	}
	return "terrain";
}

const TilesetEntryRecord* MaterialsWorkbenchPalettePanel::FindAvailableEntryRecord() const {
	if (selectedAvailableBrushListIndex_ < 0 || selectedAvailableBrushListIndex_ >= static_cast<int>(currentAvailableEntries_.size())) {
		return nullptr;
	}
	return &currentAvailableEntries_[selectedAvailableBrushListIndex_];
}

const TilesetSectionRecord* MaterialsWorkbenchPalettePanel::FindAvailableEntrySection() const {
	if (currentAvailableSourceTilesetIndex_ < 0) {
		return nullptr;
	}
	if (selectedAvailableBrushListIndex_ < 0 || selectedAvailableBrushListIndex_ >= static_cast<int>(currentAvailableEntrySectionIndexes_.size())) {
		return nullptr;
	}
	const int sectionIndex = currentAvailableEntrySectionIndexes_[selectedAvailableBrushListIndex_];
	const std::vector<TilesetStorageRecord> &tilesets = controller_.GetTilesets();
	if (currentAvailableSourceTilesetIndex_ >= static_cast<int>(tilesets.size())) {
		return nullptr;
	}
	const TilesetStorageRecord &tileset = tilesets[static_cast<size_t>(currentAvailableSourceTilesetIndex_)];
	if (sectionIndex < 0 || sectionIndex >= static_cast<int>(tileset.sections.size())) {
		return nullptr;
	}
	return &tileset.sections[static_cast<size_t>(sectionIndex)];
}

int MaterialsWorkbenchPalettePanel::FindSectionIndexByName(const TilesetStorageRecord &tileset, const wxString &sectionName) const {
	for (size_t i = 0; i < tileset.sections.size(); ++i) {
		if (tileset.sections[i].sectionType.IsSameAs(sectionName, false)) {
			return static_cast<int>(i);
		}
	}

	return -1;
}

int MaterialsWorkbenchPalettePanel::FindPaletteGroupChoiceIndexByName(const wxString &groupName) const {
	for (size_t i = 0; i < paletteGroupKeys_.size(); ++i) {
		if (paletteGroupKeys_[i].IsSameAs(groupName, false)) {
			return static_cast<int>(i);
		}
	}

	return wxNOT_FOUND;
}

const PaletteGroupRecord* MaterialsWorkbenchPalettePanel::GetSelectedPaletteGroup() const {
	const int selection = paletteGroupChoice_->GetSelection();
	if (selection == wxNOT_FOUND || selection < 0 || selection >= static_cast<int>(paletteGroupKeys_.size())) {
		return nullptr;
	}

	const wxString &selectedGroupName = paletteGroupKeys_[selection];
	for (const PaletteGroupRecord &group : controller_.GetPaletteGroups()) {
		if (group.name.IsSameAs(selectedGroupName, false)) {
			return &group;
		}
	}

	return nullptr;
}

bool MaterialsWorkbenchPalettePanel::PromptForPaletteName(const wxString &title, const wxString &caption, const wxString &initialValue, const wxString &currentName, wxString &outName) {
	wxTextEntryDialog dialog(this, caption, title, initialValue);
	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}

	const wxString candidateName = TrimmedCopy(dialog.GetValue());
	if (candidateName.IsEmpty()) {
		wxMessageBox("Palette name cannot be empty.", title, wxOK | wxICON_WARNING, this);
		return false;
	}
	if (!currentName.IsEmpty() && candidateName.IsSameAs(currentName, false)) {
		outName = candidateName;
		return true;
	}
	if (controller_.HasTilesetNamed(candidateName)) {
		wxMessageBox("A palette with this name already exists.", title, wxOK | wxICON_WARNING, this);
		return false;
	}

	outName = candidateName;
	return true;
}

bool MaterialsWorkbenchPalettePanel::PromptForPaletteGroupName(const wxString &title, const wxString &caption, const wxString &initialValue, const wxString &currentName, wxString &outName) {
	wxTextEntryDialog dialog(this, caption, title, initialValue);
	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}

	const wxString candidateName = TrimmedCopy(dialog.GetValue());
	if (candidateName.IsEmpty()) {
		wxMessageBox("Category name cannot be empty.", title, wxOK | wxICON_WARNING, this);
		return false;
	}
	if (!currentName.IsEmpty() && candidateName.IsSameAs(currentName, false)) {
		outName = candidateName;
		return true;
	}
	if (controller_.HasPaletteGroupNamed(candidateName)) {
		wxMessageBox("A category with this name already exists.", title, wxOK | wxICON_WARNING, this);
		return false;
	}

	outName = candidateName;
	return true;
}

bool MaterialsWorkbenchPalettePanel::PromptForNewSectionType(const wxString &title, const wxString &caption, wxString &outSectionType) {
	wxArrayString choices;
	for (const char* sectionType : kRuntimeSectionTypes) {
		choices.Add(sectionType);
	}

	wxSingleChoiceDialog dialog(this, caption, title, choices);
	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}

	outSectionType = dialog.GetStringSelection();
	return !outSectionType.IsEmpty();
}

bool MaterialsWorkbenchPalettePanel::PromptForSectionName(const wxString &title, const wxString &caption, const wxString &initialValue, const wxString &currentSectionType, wxString &outSectionType) {
	wxTextEntryDialog dialog(this, caption, title, initialValue);
	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}

	const wxString candidateType = TrimmedCopy(dialog.GetValue());
	if (candidateType.IsEmpty()) {
		wxMessageBox("Section name cannot be empty.", title, wxOK | wxICON_WARNING, this);
		return false;
	}
	if (!IsSupportedRuntimeSectionType(candidateType)) {
		wxMessageBox("Section type must be one of the supported runtime section types.", title, wxOK | wxICON_WARNING, this);
		return false;
	}

	for (const TilesetSectionRecord &section : palette_.sections) {
		if (!currentSectionType.IsEmpty() && section.sectionType.IsSameAs(currentSectionType, false)) {
			continue;
		}
		if (section.sectionType.IsSameAs(candidateType, false)) {
			wxMessageBox("This palette already has a section with that name.", title, wxOK | wxICON_WARNING, this);
			return false;
		}
	}

	outSectionType = candidateType;
	return true;
}

void MaterialsWorkbenchPalettePanel::OnCreatePalette(wxCommandEvent &event) {
	wxString newPaletteName;
	if (!PromptForPaletteName("New Palette", "Enter the new palette name:", "", "", newPaletteName)) {
		return;
	}
	wxString initialGroupName = "other";
	if (const PaletteGroupRecord* selectedGroup = GetSelectedPaletteGroup()) {
		initialGroupName = selectedGroup->name;
	} else if (!palette_.paletteGroupName.IsEmpty()) {
		initialGroupName = palette_.paletteGroupName;
	}

	palette_ = TilesetStorageRecord();
	palette_.name = newPaletteName;
	palette_.sourceFile = "materials.db";
	palette_.paletteGroupName = initialGroupName;
	hasPalette_ = true;
	currentSectionIndex_ = 0;
	selectedSectionEntryIndex_ = -1;
	selectedAvailableBrushListIndex_ = -1;

	if (!CommitPalette("Created empty palette \"" + newPaletteName + "\" in category \"" + initialGroupName + "\".", "", newPaletteName)) {
		return;
	}

	RefreshWorkspace();
}

void MaterialsWorkbenchPalettePanel::OnRenamePalette(wxCommandEvent &event) {
	if (!hasPalette_) {
		return;
	}

	const wxString previousName = palette_.name;
	wxString renamedPaletteName;
	if (!PromptForPaletteName("Rename Palette", "Enter the new palette name:", previousName, previousName, renamedPaletteName)) {
		return;
	}
	if (renamedPaletteName == previousName) {
		SetStatusMessage("Palette name is unchanged.");
		return;
	}

	palette_.name = renamedPaletteName;
	if (!CommitPalette("Renamed palette to \"" + renamedPaletteName + "\".", previousName, renamedPaletteName)) {
		palette_.name = previousName;
		return;
	}

	RefreshWorkspace();
}

void MaterialsWorkbenchPalettePanel::OnDeletePalette(wxCommandEvent &event) {
	if (!hasPalette_) {
		return;
	}

	const wxString paletteName = palette_.name;
	const wxString paletteGroupName = palette_.paletteGroupName.IsEmpty() ? wxString("other") : palette_.paletteGroupName;
	int entryCount = 0;
	for (const TilesetSectionRecord &section : palette_.sections) {
		entryCount += static_cast<int>(section.entries.size());
	}
	wxString entryPreview;
	int previewedEntries = 0;
	const int previewLimit = 8;
	for (const TilesetSectionRecord &section : palette_.sections) {
		for (const TilesetEntryRecord &entry : section.entries) {
			if (previewedEntries >= previewLimit) {
				break;
			}
			entryPreview << "- " << DescribePaletteEntry(controller_, entry) << "\n";
			++previewedEntries;
		}
		if (previewedEntries >= previewLimit) {
			break;
		}
	}
	if (entryCount > previewLimit) {
		entryPreview << wxString::Format("- ...and %d more\n", entryCount - previewLimit);
	}

	wxString warningText = wxString::Format(
		"Delete palette \"%s\"?\n\nCategory: %s\nSections: %zu\nEntries: %d\n\nThis will remove the palette from materials.db.\n\nThis cannot be undone.",
		paletteName,
		paletteGroupName,
		palette_.sections.size(),
		entryCount
	);
	if (!entryPreview.IsEmpty()) {
		warningText << "\n\nEntry preview:\n" << entryPreview;
	}
	if (wxMessageBox(
			warningText,
			"Delete Palette",
			wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
			this
		) != wxYES) {
		return;
	}

	wxString error;
	if (!controller_.DeleteTileset(paletteName, error)) {
		SetStatusMessage("Failed to delete palette: " + error);
		return;
	}

	ClearWorkspace("Palette deleted. Select another palette in the navigation tree.");
	SetStatusMessage("Deleted palette \"" + paletteName + "\".");
	if (onPaletteSaved_) {
		onPaletteSaved_(wxString());
	}
}

void MaterialsWorkbenchPalettePanel::OnPaletteGroupChanged(wxCommandEvent &event) {
	const int selectedChoice = event.GetSelection();
	if (selectedChoice == wxNOT_FOUND || selectedChoice >= static_cast<int>(paletteGroupKeys_.size())) {
		UpdateButtonState();
		return;
	}
	if (!hasPalette_) {
		palette_.paletteGroupName = paletteGroupKeys_[selectedChoice];
		UpdateButtonState();
		return;
	}

	const PaletteGroupRecord* group = GetSelectedPaletteGroup();
	if (!group) {
		SetStatusMessage("Could not read the selected palette category.");
		return;
	}
	if (group->name.IsSameAs(palette_.paletteGroupName, false)) {
		UpdateButtonState();
		return;
	}

	const wxString previousGroupName = palette_.paletteGroupName;
	palette_.paletteGroupName = group->name;
	if (!CommitPalette("Moved palette \"" + palette_.name + "\" to palette category \"" + group->name + "\".")) {
		palette_.paletteGroupName = previousGroupName;
		RefreshPaletteGroupChoice();
		UpdateButtonState();
		return;
	}

	RefreshWorkspace();
}

void MaterialsWorkbenchPalettePanel::OnCreatePaletteGroup(wxCommandEvent &event) {
	wxString groupName;
	if (!PromptForPaletteGroupName("New Palette Category", "Enter the new palette category name:", "", "", groupName)) {
		return;
	}

	PaletteGroupRecord group;
	group.name = groupName;
	wxString error;
	if (!controller_.SavePaletteGroup(group, error)) {
		SetStatusMessage("Failed to create palette category: " + error);
		return;
	}

	palette_.paletteGroupName = group.name;
	RefreshPaletteGroupChoice();
	SetStatusMessage("Created palette category \"" + group.name + "\".");
	if (onPaletteSaved_) {
		onPaletteSaved_(palette_.name);
	}
	UpdateButtonState();
}

void MaterialsWorkbenchPalettePanel::OnRenamePaletteGroup(wxCommandEvent &event) {
	const PaletteGroupRecord* selectedGroup = GetSelectedPaletteGroup();
	if (!selectedGroup || selectedGroup->isBuiltin) {
		return;
	}

	wxString renamedGroupName;
	if (!PromptForPaletteGroupName("Rename Palette Category", "Enter the new palette category name:", selectedGroup->name, selectedGroup->name, renamedGroupName)) {
		return;
	}
	if (renamedGroupName.IsSameAs(selectedGroup->name, false)) {
		SetStatusMessage("Palette category name is unchanged.");
		return;
	}

	PaletteGroupRecord updatedGroup = *selectedGroup;
	updatedGroup.name = renamedGroupName;
	wxString error;
	if (!controller_.SavePaletteGroup(updatedGroup, error)) {
		SetStatusMessage("Failed to rename palette category: " + error);
		return;
	}

	if (palette_.paletteGroupName.IsSameAs(selectedGroup->name, false)) {
		palette_.paletteGroupName = renamedGroupName;
	}
	RefreshPaletteGroupChoice();
	SetStatusMessage("Renamed palette category to \"" + renamedGroupName + "\".");
	if (onPaletteSaved_) {
		onPaletteSaved_(palette_.name);
	}
	UpdateButtonState();
}

void MaterialsWorkbenchPalettePanel::OnDeletePaletteGroup(wxCommandEvent &event) {
	const PaletteGroupRecord* selectedGroup = GetSelectedPaletteGroup();
	if (!selectedGroup || selectedGroup->isBuiltin) {
		return;
	}
	const wxString selectedGroupName = selectedGroup->name;

	std::vector<wxString> affectedPaletteNames;
	for (const TilesetStorageRecord &tileset : controller_.GetTilesets()) {
		if (ResolvePaletteGroupKey(tileset).IsSameAs(selectedGroupName, false)) {
			affectedPaletteNames.push_back(tileset.name);
		}
	}

	wxString successMessage;
	if (!affectedPaletteNames.empty()) {
		wxArrayString destinationLabels;
		std::vector<wxString> destinationKeys;
		int recommendedSelection = wxNOT_FOUND;
		for (const PaletteGroupRecord &group : controller_.GetPaletteGroups()) {
			if (group.name.IsSameAs(selectedGroupName, false)) {
				continue;
			}
			if (group.name.IsSameAs("other", false)) {
				recommendedSelection = static_cast<int>(destinationKeys.size());
			}
			destinationLabels.Add(BuildPaletteGroupLabel(group));
			destinationKeys.push_back(group.name);
		}

		if (destinationKeys.empty()) {
			SetStatusMessage("Cannot delete this palette category because there is no destination palette category available for its palettes.");
			return;
		}

		wxString affectedList;
		const size_t previewCount = std::min<size_t>(affectedPaletteNames.size(), 5);
		for (size_t i = 0; i < previewCount; ++i) {
			affectedList << "- " << affectedPaletteNames[i] << "\n";
		}
		if (affectedPaletteNames.size() > previewCount) {
			affectedList << wxString::Format("- ...and %zu more\n", affectedPaletteNames.size() - previewCount);
		}

		wxSingleChoiceDialog destinationDialog(
			this,
			wxString::Format(
				"Palette category \"%s\" is still used by %zu palette(s).\n\nChoose where those palettes should move before the palette category is deleted.\n\nAffected palettes:\n%s",
				selectedGroupName,
				affectedPaletteNames.size(),
				affectedList
			),
			"Delete Palette Category",
			destinationLabels
		);
		if (recommendedSelection != wxNOT_FOUND) {
			destinationDialog.SetSelection(recommendedSelection);
		}
		if (destinationDialog.ShowModal() != wxID_OK) {
			return;
		}

		const int destinationSelection = destinationDialog.GetSelection();
		if (destinationSelection == wxNOT_FOUND || destinationSelection >= static_cast<int>(destinationKeys.size())) {
			SetStatusMessage("Choose a destination palette category before deleting this one.");
			return;
		}

		const wxString destinationGroup = destinationKeys[static_cast<size_t>(destinationSelection)];
		if (wxMessageBox(
				wxString::Format(
					"Delete palette category \"%s\" and move %zu palette(s) to \"%s\"?\n\nThis updates the affected palettes first and then removes the old palette category.",
					selectedGroupName,
					affectedPaletteNames.size(),
					destinationGroup
				),
				"Delete Palette Category",
				wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
				this
			) != wxYES) {
			return;
		}

		wxString error;
		int movedPaletteCount = 0;
		if (!controller_.DeletePaletteGroupAndReassignPalettes(selectedGroupName, destinationGroup, movedPaletteCount, error)) {
			SetStatusMessage("Failed to delete palette category: " + error);
			return;
		}

		if (palette_.paletteGroupName.IsSameAs(selectedGroupName, false)) {
			palette_.paletteGroupName = destinationGroup;
		}
		successMessage = wxString::Format(
			"Deleted palette category \"%s\" and moved %d palette(s) to \"%s\".",
			selectedGroupName,
			movedPaletteCount,
			destinationGroup
		);
	} else {
		if (wxMessageBox(
				"Delete palette category \"" + selectedGroupName + "\"?",
				"Delete Palette Category",
				wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
				this
			) != wxYES) {
			return;
		}

		wxString error;
		if (!controller_.DeletePaletteGroup(selectedGroupName, error)) {
			SetStatusMessage("Failed to delete palette category: " + error);
			return;
		}

		if (palette_.paletteGroupName.IsSameAs(selectedGroupName, false)) {
			palette_.paletteGroupName = "other";
		}
		successMessage = "Deleted palette category \"" + selectedGroupName + "\".";
	}

	RefreshWorkspace();
	SetStatusMessage(successMessage);
	if (onPaletteSaved_) {
		onPaletteSaved_(palette_.name);
	}
	UpdateButtonState();
}

void MaterialsWorkbenchPalettePanel::OnSectionChanged(wxCommandEvent &event) {
	const int selectedChoice = event.GetSelection();
	if (!hasPalette_ || selectedChoice == wxNOT_FOUND || selectedChoice >= static_cast<int>(palette_.sections.size())) {
		return;
	}
	currentSectionIndex_ = selectedChoice;
	selectedSectionEntryIndex_ = -1;
	preserveSectionGridViewStart_ = false;
	RefreshAvailableBrushFamilies();
	RefreshAvailableBrushPalettes();
	RefreshSectionEntries();
	RefreshAvailableBrushes();
	UpdateButtonState();
}

void MaterialsWorkbenchPalettePanel::OnAddSection(wxCommandEvent &event) {
	if (!hasPalette_) {
		return;
	}

	wxString sectionType;
	if (!PromptForNewSectionType("New Section", "Choose the section to create:", sectionType)) {
		return;
	}
	if (FindSectionIndexByName(palette_, sectionType) >= 0) {
		SetStatusMessage("This palette already has a section for \"" + sectionType + "\".");
		return;
	}

	TilesetSectionRecord section;
	section.sectionType = sectionType;
	section.sortOrder = static_cast<int>(palette_.sections.size());
	palette_.sections.push_back(section);
	currentSectionIndex_ = static_cast<int>(palette_.sections.size()) - 1;
	selectedSectionEntryIndex_ = -1;
	selectedAvailableBrushListIndex_ = -1;

	if (!CommitPalette("Created section \"" + sectionType + "\" in palette \"" + palette_.name + "\".")) {
		return;
	}

	RefreshWorkspace();
}

void MaterialsWorkbenchPalettePanel::OnRenameSection(wxCommandEvent &event) {
	if (!hasPalette_ || palette_.sections.empty() || currentSectionIndex_ >= static_cast<int>(palette_.sections.size())) {
		return;
	}

	TilesetSectionRecord &section = palette_.sections[currentSectionIndex_];
	const wxString previousSectionType = section.sectionType;
	wxString renamedSectionType;
	if (!PromptForSectionName("Rename Section", "Enter the new runtime section type:", previousSectionType, previousSectionType, renamedSectionType)) {
		return;
	}
	if (renamedSectionType == previousSectionType) {
		SetStatusMessage("Section type is unchanged.");
		return;
	}

	section.sectionType = renamedSectionType;
	if (!CommitPalette("Renamed section to \"" + renamedSectionType + "\".")) {
		section.sectionType = previousSectionType;
		return;
	}

	RefreshWorkspace();
}

void MaterialsWorkbenchPalettePanel::OnDeleteSection(wxCommandEvent &event) {
	if (!hasPalette_ || palette_.sections.empty() || currentSectionIndex_ >= static_cast<int>(palette_.sections.size())) {
		return;
	}

	const wxString sectionType = palette_.sections[currentSectionIndex_].sectionType;
	if (wxMessageBox(
			"Delete section \"" + sectionType + "\" from palette \"" + palette_.name + "\"?",
			"Delete Section",
			wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
			this
		) != wxYES) {
		return;
	}

	palette_.sections.erase(palette_.sections.begin() + currentSectionIndex_);
	if (currentSectionIndex_ >= static_cast<int>(palette_.sections.size())) {
		currentSectionIndex_ = std::max(0, static_cast<int>(palette_.sections.size()) - 1);
	}
	selectedSectionEntryIndex_ = -1;
	selectedAvailableBrushListIndex_ = -1;

	if (!CommitPalette("Deleted section \"" + sectionType + "\".")) {
		return;
	}

	RefreshWorkspace();
}

void MaterialsWorkbenchPalettePanel::OnAvailableBrushFamilyChanged(wxCommandEvent &event) {
	selectedAvailableBrushListIndex_ = -1;
	RefreshAvailableBrushPalettes();
	RefreshAvailableBrushes();
	UpdateButtonState();
}

void MaterialsWorkbenchPalettePanel::OnAvailableBrushPaletteChanged(wxCommandEvent &event) {
	selectedAvailableBrushListIndex_ = -1;
	RefreshAvailableBrushes();
	UpdateButtonState();
}

void MaterialsWorkbenchPalettePanel::OnMoveDestinationFamilyChanged(wxCommandEvent &event) {
	RefreshMoveDestinationPalettes();
	UpdateButtonState();
}

void MaterialsWorkbenchPalettePanel::OnMoveDestinationPaletteChanged(wxCommandEvent &event) {
	UpdateButtonState();
}

void MaterialsWorkbenchPalettePanel::OnAddBrush(wxCommandEvent &event) {
	if (!hasPalette_ || availableBrushFamilyChoice_->GetSelection() == wxNOT_FOUND || availableBrushPaletteChoice_->GetSelection() == wxNOT_FOUND) {
		return;
	}

	const TilesetEntryRecord* sourceEntry = FindAvailableEntryRecord();
	const TilesetSectionRecord* sourceSection = FindAvailableEntrySection();
	if (!sourceEntry || !sourceSection) {
		SetStatusMessage("Select an entry from the catalog first.");
		return;
	}

	const wxString familyKey = availableBrushFamilyKeys_[availableBrushFamilyChoice_->GetSelection()];
	const wxString destinationDisplayFamily = ResolvePaletteBrushDisplayFamily(palette_);
	const wxString preferredSectionType = NormalizeSectionTypeForPaletteInsertion(sourceSection->sectionType, familyKey, destinationDisplayFamily, sourceEntry->entryKind);
	int targetSectionIndex = -1;
	for (size_t i = 0; i < palette_.sections.size(); ++i) {
		if (palette_.sections[i].sectionType.IsSameAs(preferredSectionType, false)) {
			targetSectionIndex = static_cast<int>(i);
			break;
		}
	}
	if (targetSectionIndex == -1) {
		const wxString preferredFamily = DerivePaletteGroupFromSectionType(preferredSectionType);
		for (size_t i = 0; i < palette_.sections.size(); ++i) {
			if (DerivePaletteGroupFromSectionType(palette_.sections[i].sectionType).IsSameAs(preferredFamily, false)) {
				targetSectionIndex = static_cast<int>(i);
				break;
			}
		}
	}
	if (targetSectionIndex == -1) {
		TilesetSectionRecord section;
		section.sectionType = preferredSectionType;
		section.sortOrder = static_cast<int>(palette_.sections.size());
		palette_.sections.push_back(section);
		targetSectionIndex = static_cast<int>(palette_.sections.size()) - 1;
	}

	TilesetSectionRecord &section = palette_.sections[targetSectionIndex];
	TilesetEntryRecord entry = *sourceEntry;
	entry.sortOrder = 0;
	entry.afterBrushName.clear();
	entry.afterItemId = 0;

	int insertIndex = static_cast<int>(section.entries.size());
	if (currentSectionIndex_ == targetSectionIndex && selectedSectionEntryIndex_ >= 0) {
		insertIndex = selectedSectionEntryIndex_ + 1;
	}
	insertIndex = std::clamp(insertIndex, 0, static_cast<int>(section.entries.size()));
	section.entries.insert(section.entries.begin() + insertIndex, entry);
	currentSectionIndex_ = targetSectionIndex;
	selectedSectionEntryIndex_ = insertIndex;

	if (!CommitPalette("Added \"" + DescribePaletteEntry(controller_, entry) + "\" to palette \"" + palette_.name + "\".")) {
		return;
	}

	preserveSectionGridViewStart_ = true;
	RefreshSectionChoice();
	RefreshSectionEntries();
	UpdateButtonState();
}

void MaterialsWorkbenchPalettePanel::OnMoveBrushToPalette(wxCommandEvent &event) {
	if (!hasPalette_) {
		return;
	}

	int sourceSectionIndex = 0;
	int sourceEntryIndex = 0;
	TilesetEntryRecord movingEntry;
	if (!ResolveSelectedMovableEntry(sourceSectionIndex, sourceEntryIndex, movingEntry)) {
		SetStatusMessage("Select an entry in this palette first.");
		return;
	}

	wxString destinationDisplayLabel;
	TilesetStorageRecord targetPalette;
	if (!ResolveMoveDestinationPalette(targetPalette, destinationDisplayLabel)) {
		SetStatusMessage("Choose a destination category and palette first.");
		return;
	}

	const wxString sourceSectionType = palette_.sections[sourceSectionIndex].sectionType;
	const wxString sourceFamily = DerivePaletteGroupFromSectionType(sourceSectionType);
	const int destinationFamilySelection = moveDestinationFamilyChoice_->GetSelection();
	const wxString destinationRuntimeFamily =
		destinationFamilySelection != wxNOT_FOUND && destinationFamilySelection < static_cast<int>(moveDestinationFamilyKeys_.size())
			? ResolvePaletteGroupRuntimeFamilyKey(targetPalette)
			: ResolvePaletteBrushDisplayFamily(targetPalette);

	wxString preferredSectionType = sourceSectionType;
	if (!destinationRuntimeFamily.IsEmpty() &&
		!destinationRuntimeFamily.IsSameAs("other", false) &&
		!destinationRuntimeFamily.IsSameAs(sourceFamily, false)) {
		preferredSectionType = PreferredSectionTypeForBrushFamilyKey(destinationRuntimeFamily);
	}

	int targetSectionIndex = FindSectionIndexByName(targetPalette, preferredSectionType);
	if (targetSectionIndex == -1) {
		const wxString preferredFamily = !destinationRuntimeFamily.IsEmpty() ? destinationRuntimeFamily : DerivePaletteGroupFromSectionType(preferredSectionType);
		for (size_t i = 0; i < targetPalette.sections.size(); ++i) {
			if (DerivePaletteGroupFromSectionType(targetPalette.sections[i].sectionType).IsSameAs(preferredFamily, false)) {
				targetSectionIndex = static_cast<int>(i);
				break;
			}
		}
	}
	if (targetSectionIndex == -1) {
		TilesetSectionRecord section;
		section.sectionType = preferredSectionType;
		section.sortOrder = static_cast<int>(targetPalette.sections.size());
		targetPalette.sections.push_back(section);
		targetSectionIndex = static_cast<int>(targetPalette.sections.size()) - 1;
	}

	targetPalette.sections[targetSectionIndex].entries.push_back(movingEntry);
	NormalizePaletteOrdering(targetPalette);

	wxString error;
	if (!controller_.SaveTileset(targetPalette, error)) {
		SetStatusMessage("Failed to move brush to " + destinationDisplayLabel + ": " + error);
		return;
	}

	const wxString movedBrushName = DescribePaletteEntry(controller_, movingEntry);
	palette_.sections[sourceSectionIndex].entries.erase(palette_.sections[sourceSectionIndex].entries.begin() + sourceEntryIndex);
	if (currentSectionIndex_ == sourceSectionIndex) {
		if (sourceEntryIndex >= static_cast<int>(palette_.sections[sourceSectionIndex].entries.size())) {
			selectedSectionEntryIndex_ = static_cast<int>(palette_.sections[sourceSectionIndex].entries.size()) - 1;
		} else {
			selectedSectionEntryIndex_ = sourceEntryIndex;
		}
	} else {
		selectedSectionEntryIndex_ = -1;
	}

	NormalizePaletteOrdering(palette_);
	if (!controller_.SaveTileset(palette_, error)) {
		SetStatusMessage("Moved \"" + movedBrushName + "\" to " + destinationDisplayLabel + ", but could not remove it from \"" + palette_.name + "\": " + error);
		if (onPaletteSaved_) {
			onPaletteSaved_(palette_.name);
		}
		preserveSectionGridViewStart_ = true;
		RefreshWorkspace();
		return;
	}

	SetStatusMessage("Moved \"" + movedBrushName + "\" to " + destinationDisplayLabel + ".");
	if (onPaletteSaved_) {
		onPaletteSaved_(palette_.name);
	}
	preserveSectionGridViewStart_ = true;
	RefreshWorkspace();
}

void MaterialsWorkbenchPalettePanel::OnRemoveBrush(wxCommandEvent &event) {
	if (!hasPalette_ || palette_.sections.empty() || currentSectionIndex_ >= static_cast<int>(palette_.sections.size()) || selectedSectionEntryIndex_ < 0) {
		return;
	}

	TilesetSectionRecord &section = palette_.sections[currentSectionIndex_];
	if (selectedSectionEntryIndex_ >= static_cast<int>(section.entries.size())) {
		return;
	}

	const wxString removedName = DescribePaletteEntry(controller_, section.entries[selectedSectionEntryIndex_]);
	section.entries.erase(section.entries.begin() + selectedSectionEntryIndex_);
	if (selectedSectionEntryIndex_ >= static_cast<int>(section.entries.size())) {
		selectedSectionEntryIndex_ = static_cast<int>(section.entries.size()) - 1;
	}

	if (!CommitPalette("Removed \"" + removedName + "\" from palette \"" + palette_.name + "\".")) {
		return;
	}

	preserveSectionGridViewStart_ = true;
	RefreshSectionChoice();
	RefreshSectionEntries();
	UpdateButtonState();
}

void MaterialsWorkbenchPalettePanel::OnMoveBrushUp(wxCommandEvent &event) {
	if (!MoveSelectedEntryByOffset(-1, "up")) {
		return;
	}

	preserveSectionGridViewStart_ = true;
	RefreshSectionEntries();
	UpdateButtonState();
}

void MaterialsWorkbenchPalettePanel::OnMoveBrushDown(wxCommandEvent &event) {
	if (!MoveSelectedEntryByOffset(1, "down")) {
		return;
	}

	preserveSectionGridViewStart_ = true;
	RefreshSectionEntries();
	UpdateButtonState();
}
