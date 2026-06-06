#include "main.h"

#include "materials_workbench_brush_panel.h"

#include <cmath>
#include <limits>
#include <set>
#include <utility>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dcbuffer.h>
#include <wx/dialog.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/settings.h>
#include <wx/spinctrl.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "brush.h"
#include "graphics.h"
#include "gui.h"
#include "items.h"
#include "materials_workbench_controller.h"
#include "sprite_appearances.h"

namespace {
	struct DoodadPreviewSpriteMetrics;
	DoodadPreviewSpriteMetrics ResolveDoodadPreviewSpriteMetrics(int itemId);
	bool HasValidDoodadPreviewSprite(int itemId);
	wxRect GetDoodadPreviewSpriteRect(int itemId, const wxPoint &drawPoint);
	void DrawDoodadPreviewItemSprite(wxDC &dc, int itemId, const wxPoint &tileAnchor);

	bool IsValidBrushEditorType(const wxString &type) {
		return type == "ground" || type == "carpet" || type == "table" || type == "doodad";
	}

	bool IsKnownItemId(int itemId) {
		if (itemId <= 0 || itemId > std::numeric_limits<uint16_t>::max()) {
			return false;
		}
		return g_items.isValidID(static_cast<uint16_t>(itemId));
	}

	wxStaticText* CreateSectionLabel(wxWindow* parent, const wxString &label) {
		wxStaticText* text = new wxStaticText(parent, wxID_ANY, label);
		wxFont font = text->GetFont();
		font.SetWeight(wxFONTWEIGHT_BOLD);
		text->SetFont(font);
		return text;
	}

	void StyleBrushWorkspaceSubtitle(wxStaticText* label) {
		label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
	}

	void StyleBrushWorkspaceStatusLabel(wxStaticText* label) {
		label->SetMinSize(wxSize(-1, label->GetParent()->FromDIP(20)));
		label->Wrap(label->GetParent()->FromDIP(760));
	}

	void StyleBrushWorkspaceActionButton(wxButton* button, const wxString &tooltip) {
		button->SetMinSize(wxSize(button->GetParent()->FromDIP(108), button->GetParent()->FromDIP(20)));
		button->SetToolTip(tooltip);
	}

	wxTextCtrl* CreateTextField(wxWindow* parent, long style = 0) {
		return new wxTextCtrl(parent, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, style);
	}

	wxSpinCtrl* CreateSpinField(wxWindow* parent, int minValue, int maxValue) {
		wxSpinCtrl* ctrl = new wxSpinCtrl(parent, wxID_ANY);
		ctrl->SetRange(minValue, maxValue);
		return ctrl;
	}

	int GetMaxEditableItemId() {
		return static_cast<int>(std::min<uint32_t>(g_items.getMaxID(), std::numeric_limits<uint16_t>::max()));
	}

	wxSpinCtrl* CreateItemIdSpinField(wxWindow* parent) {
		return CreateSpinField(parent, 0, GetMaxEditableItemId());
	}

	wxSpinCtrl* CreateLookIdSpinField(wxWindow* parent) {
		return CreateSpinField(parent, 0, GetMaxEditableItemId());
	}

	wxString FormatImportedFromValue(const wxString &sourceFile) {
		return sourceFile.IsEmpty() ? "Not imported from legacy XML" : sourceFile;
	}

	wxString ParseImportedFromEditorValue(const wxString &sourceFile) {
		return sourceFile == "Not imported from legacy XML" ? "" : sourceFile;
	}

	int CaptureListTopItem(wxListBox* listBox) {
		if (!listBox || listBox->GetCount() == 0) {
			return wxNOT_FOUND;
		}
		return listBox->GetTopItem();
	}

	void RestoreListTopItem(wxListBox* listBox, int topItem) {
		if (!listBox || listBox->GetCount() == 0 || topItem == wxNOT_FOUND) {
			return;
		}
		const int clampedTopItem = std::min<int>(topItem, static_cast<int>(listBox->GetCount()) - 1);
		if (clampedTopItem >= 0) {
			listBox->SetFirstItem(clampedTopItem);
		}
	}

	int ClampIndexForCount(int index, size_t count) {
		if (count == 0) {
			return -1;
		}
		if (index < 0) {
			return 0;
		}
		return std::min<int>(index, static_cast<int>(count) - 1);
	}

	wxString TrimmedValue(const wxTextCtrl* ctrl) {
		wxString value = ctrl->GetValue();
		value.Trim(true);
		value.Trim(false);
		return value;
	}

	wxString FormatAlignedNodeLabel(const wxString &align, size_t itemCount, size_t index) {
		return wxString::Format("%zu. %s (%zu item%s)", index + 1, align, itemCount, itemCount == 1 ? "" : "s");
	}

	wxString FormatAlignedItemLabel(int itemId, int chance, size_t index) {
		return wxString::Format("%zu. item %d (chance %d)", index + 1, itemId, chance);
	}

	wxString FormatGroundItemLabel(int itemId, int chance, size_t index) {
		return wxString::Format("%zu. item %d (chance %d)", index + 1, itemId, chance);
	}

	wxString FormatDoodadAlternativeLabel(const DoodadAlternativeRecord &alternative, size_t index) {
		return wxString::Format(
			"%zu. %zu single | %zu composite",
			index + 1,
			alternative.singleItems.size(),
			alternative.composites.size()
		);
	}

	wxString FormatDoodadSingleItemLabel(int itemId, int chance, size_t index) {
		return wxString::Format("%zu. item %d (chance %d)", index + 1, itemId, chance);
	}

	wxString FormatDoodadCompositeLabel(const DoodadCompositeRecord &composite, size_t index) {
		return wxString::Format("%zu. chance %d | %zu tile%s", index + 1, composite.chance, composite.tiles.size(), composite.tiles.size() == 1 ? "" : "s");
	}

	wxString FormatDoodadTileLabel(const DoodadCompositeTileRecord &tile, size_t index) {
		return wxString::Format(
			"%zu. (%d, %d, %d) | %zu item%s",
			index + 1,
			tile.offsetX,
			tile.offsetY,
			tile.offsetZ,
			tile.items.size(),
			tile.items.size() == 1 ? "" : "s"
		);
	}

	wxString FormatDoodadTileItemLabel(int itemId, size_t index) {
		return wxString::Format("%zu. item %d", index + 1, itemId);
	}

	constexpr int kDefaultNewTableNodeChance = 10;

	enum class SliderArrowDirection {
		Left,
		Right,
		Up,
		Down
	};

	void DrawSliderArrowGlyph(wxDC &dc, const wxRect &rect, const wxColour &colour, SliderArrowDirection direction) {
		wxRect innerRect = rect;
		innerRect.Deflate(std::max(3, std::min(rect.width, rect.height) / 4));
		if (innerRect.width <= 0 || innerRect.height <= 0) {
			return;
		}

		const int cx = innerRect.x + innerRect.width / 2;
		const int cy = innerRect.y + innerRect.height / 2;
		wxPoint points[3];
		switch (direction) {
		case SliderArrowDirection::Left:
			points[0] = wxPoint(innerRect.x, cy);
			points[1] = wxPoint(innerRect.GetRight() + 1, innerRect.y);
			points[2] = wxPoint(innerRect.GetRight() + 1, innerRect.GetBottom() + 1);
			break;
		case SliderArrowDirection::Right:
			points[0] = wxPoint(innerRect.GetRight() + 1, cy);
			points[1] = wxPoint(innerRect.x, innerRect.y);
			points[2] = wxPoint(innerRect.x, innerRect.GetBottom() + 1);
			break;
		case SliderArrowDirection::Up:
			points[0] = wxPoint(cx, innerRect.y);
			points[1] = wxPoint(innerRect.x, innerRect.GetBottom() + 1);
			points[2] = wxPoint(innerRect.GetRight() + 1, innerRect.GetBottom() + 1);
			break;
		case SliderArrowDirection::Down:
			points[0] = wxPoint(cx, innerRect.GetBottom() + 1);
			points[1] = wxPoint(innerRect.x, innerRect.y);
			points[2] = wxPoint(innerRect.GetRight() + 1, innerRect.y);
			break;
		}

		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(colour));
		dc.DrawPolygon(3, points);
	}

	bool ShowDoodadSingleItemDialog(wxWindow* parent, const wxString &title, int &itemId, int &chance) {
		wxDialog dialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
		wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);
		wxFlexGridSizer* formSizer = new wxFlexGridSizer(2, parent->FromDIP(8), parent->FromDIP(8));
		formSizer->AddGrowableCol(1, 1);

		wxSpinCtrl* itemIdCtrl = CreateItemIdSpinField(&dialog);
		itemIdCtrl->SetValue(itemId);
		wxSpinCtrl* chanceCtrl = CreateSpinField(&dialog, 0, 1000000);
		chanceCtrl->SetValue(chance);

		formSizer->Add(new wxStaticText(&dialog, wxID_ANY, "Item ID"), 0, wxALIGN_CENTER_VERTICAL);
		formSizer->Add(itemIdCtrl, 1, wxEXPAND);
		formSizer->Add(new wxStaticText(&dialog, wxID_ANY, "Chance"), 0, wxALIGN_CENTER_VERTICAL);
		formSizer->Add(chanceCtrl, 1, wxEXPAND);
		rootSizer->Add(formSizer, 1, wxEXPAND | wxALL, parent->FromDIP(12));
		rootSizer->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, parent->FromDIP(12));

		dialog.SetSizerAndFit(rootSizer);
		dialog.SetMinSize(dialog.GetSize());
		if (dialog.ShowModal() != wxID_OK) {
			return false;
		}

		itemId = itemIdCtrl->GetValue();
		chance = chanceCtrl->GetValue();
		return true;
	}

	bool ShowDoodadChanceDialog(wxWindow* parent, const wxString &title, int &chance) {
		wxDialog dialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
		wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);
		wxFlexGridSizer* formSizer = new wxFlexGridSizer(2, parent->FromDIP(8), parent->FromDIP(8));
		formSizer->AddGrowableCol(1, 1);

		wxSpinCtrl* chanceCtrl = CreateSpinField(&dialog, 0, 1000000);
		chanceCtrl->SetValue(chance);

		formSizer->Add(new wxStaticText(&dialog, wxID_ANY, "Chance"), 0, wxALIGN_CENTER_VERTICAL);
		formSizer->Add(chanceCtrl, 1, wxEXPAND);
		rootSizer->Add(formSizer, 1, wxEXPAND | wxALL, parent->FromDIP(12));
		rootSizer->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, parent->FromDIP(12));

		dialog.SetSizerAndFit(rootSizer);
		dialog.SetMinSize(dialog.GetSize());
		if (dialog.ShowModal() != wxID_OK) {
			return false;
		}

		chance = chanceCtrl->GetValue();
		return true;
	}

	bool ShowDoodadTileItemDialog(wxWindow* parent, const wxString &title, int &itemId) {
		wxDialog dialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
		wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);
		wxFlexGridSizer* formSizer = new wxFlexGridSizer(2, parent->FromDIP(8), parent->FromDIP(8));
		formSizer->AddGrowableCol(1, 1);

		wxSpinCtrl* itemIdCtrl = CreateItemIdSpinField(&dialog);
		itemIdCtrl->SetValue(itemId);

		formSizer->Add(new wxStaticText(&dialog, wxID_ANY, "Item ID"), 0, wxALIGN_CENTER_VERTICAL);
		formSizer->Add(itemIdCtrl, 1, wxEXPAND);
		rootSizer->Add(formSizer, 1, wxEXPAND | wxALL, parent->FromDIP(12));
		rootSizer->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, parent->FromDIP(12));

		dialog.SetSizerAndFit(rootSizer);
		dialog.SetMinSize(dialog.GetSize());
		if (dialog.ShowModal() != wxID_OK) {
			return false;
		}

		itemId = itemIdCtrl->GetValue();
		return true;
	}

	bool ShowWeightedBrushItemDialog(wxWindow* parent, const wxString &title, int &itemId, int &chance) {
		wxDialog dialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
		wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);
		wxFlexGridSizer* formSizer = new wxFlexGridSizer(2, parent->FromDIP(8), parent->FromDIP(8));
		formSizer->AddGrowableCol(1, 1);

		wxSpinCtrl* itemIdCtrl = CreateItemIdSpinField(&dialog);
		itemIdCtrl->SetValue(itemId);
		wxSpinCtrl* chanceCtrl = CreateSpinField(&dialog, 0, 1000000);
		chanceCtrl->SetValue(chance);

		formSizer->Add(new wxStaticText(&dialog, wxID_ANY, "Item ID"), 0, wxALIGN_CENTER_VERTICAL);
		formSizer->Add(itemIdCtrl, 1, wxEXPAND);
		formSizer->Add(new wxStaticText(&dialog, wxID_ANY, "Chance"), 0, wxALIGN_CENTER_VERTICAL);
		formSizer->Add(chanceCtrl, 1, wxEXPAND);
		rootSizer->Add(formSizer, 1, wxEXPAND | wxALL, parent->FromDIP(12));
		rootSizer->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, parent->FromDIP(12));

		dialog.SetSizerAndFit(rootSizer);
		dialog.SetMinSize(dialog.GetSize());
		if (dialog.ShowModal() != wxID_OK) {
			return false;
		}

		itemId = itemIdCtrl->GetValue();
		chance = chanceCtrl->GetValue();
		return true;
	}

	bool ShowWeightedBrushItemDialogWithPreview(wxWindow* parent, const wxString &title, int &itemId, int &chance) {
		wxDialog dialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
		wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);
		wxFlexGridSizer* formSizer = new wxFlexGridSizer(2, parent->FromDIP(8), parent->FromDIP(8));
		formSizer->AddGrowableCol(1, 1);

		wxSpinCtrl* itemIdCtrl = CreateItemIdSpinField(&dialog);
		itemIdCtrl->SetValue(itemId);
		wxSpinCtrl* chanceCtrl = CreateSpinField(&dialog, 0, 1000000);
		chanceCtrl->SetValue(chance);
		wxStaticText* helperLabel = new wxStaticText(&dialog, wxID_ANY, "Type an Item ID to preview the sprite before confirming.");
		wxPanel* previewPanel = new wxPanel(&dialog, wxID_ANY, wxDefaultPosition, wxSize(parent->FromDIP(208), parent->FromDIP(132)), wxBORDER_SIMPLE);
		previewPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);

		formSizer->Add(new wxStaticText(&dialog, wxID_ANY, "Item ID"), 0, wxALIGN_CENTER_VERTICAL);
		formSizer->Add(itemIdCtrl, 1, wxEXPAND);
		formSizer->Add(new wxStaticText(&dialog, wxID_ANY, "Chance"), 0, wxALIGN_CENTER_VERTICAL);
		formSizer->Add(chanceCtrl, 1, wxEXPAND);
		rootSizer->Add(formSizer, 0, wxEXPAND | wxALL, parent->FromDIP(12));
		rootSizer->Add(helperLabel, 0, wxLEFT | wxRIGHT | wxBOTTOM, parent->FromDIP(12));
		rootSizer->Add(previewPanel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, parent->FromDIP(12));
		rootSizer->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, parent->FromDIP(12));

		auto refreshPreviewState = [&]() {
			const int previewItemId = itemIdCtrl->GetValue();
			if (previewItemId > 0 && HasValidDoodadPreviewSprite(previewItemId)) {
				helperLabel->SetLabel(wxString::Format("Item %d ready to add.", previewItemId));
			} else {
				helperLabel->SetLabel("Type a valid Item ID to preview the sprite before confirming.");
			}
			previewPanel->Refresh();
		};

		previewPanel->Bind(wxEVT_PAINT, [itemIdCtrl, previewPanel](wxPaintEvent &WXUNUSED(evt)) {
			wxAutoBufferedPaintDC dc(previewPanel);
			const wxRect clientRect = previewPanel->GetClientRect();
			dc.SetBackground(wxBrush(wxColour(20, 24, 32)));
			dc.Clear();
			dc.SetPen(wxPen(wxColour(72, 80, 94), 1));
			dc.SetBrush(wxBrush(wxColour(24, 28, 36)));
			wxRect frameRect = clientRect;
			frameRect.Deflate(previewPanel->FromDIP(10), previewPanel->FromDIP(10));
			dc.DrawRoundedRectangle(frameRect, previewPanel->FromDIP(6));

			const int previewItemId = itemIdCtrl->GetValue();
			if (previewItemId <= 0 || !HasValidDoodadPreviewSprite(previewItemId)) {
				dc.SetTextForeground(wxColour(150, 156, 170));
				dc.DrawLabel("No preview", frameRect, wxALIGN_CENTER);
				return;
			}

			const wxRect spriteBounds = GetDoodadPreviewSpriteRect(previewItemId, wxPoint(0, 0));
			const wxPoint drawPoint(
				frameRect.x + std::max(0, (frameRect.width - spriteBounds.width) / 2) - spriteBounds.x,
				frameRect.y + std::max(0, (frameRect.height - spriteBounds.height) / 2) - spriteBounds.y
			);
			DrawDoodadPreviewItemSprite(dc, previewItemId, drawPoint);
		});
		itemIdCtrl->Bind(wxEVT_SPINCTRL, [refreshPreviewState](wxCommandEvent &WXUNUSED(evt)) { refreshPreviewState(); });
		itemIdCtrl->Bind(wxEVT_TEXT, [refreshPreviewState](wxCommandEvent &WXUNUSED(evt)) { refreshPreviewState(); });
		refreshPreviewState();

		dialog.SetSizerAndFit(rootSizer);
		dialog.SetMinSize(dialog.GetSize());
		if (dialog.ShowModal() != wxID_OK) {
			return false;
		}

		itemId = itemIdCtrl->GetValue();
		chance = chanceCtrl->GetValue();
		return true;
	}

	struct DoodadPreviewSpriteMetrics {
		int spriteId = 0;
		int widthPx = 32;
		int heightPx = 32;
		wxPoint drawOffset;
		int drawHeight = 0;

		bool isValid() const {
			return spriteId > 0;
		}
	};

	int ResolveDoodadPreviewLookId(int itemId) {
		if (!IsKnownItemId(itemId)) {
			return 0;
		}
		const ItemType &itemType = g_items.getItemType(static_cast<uint16_t>(itemId));
		if (itemType.sprite_id > 0) {
			return static_cast<int>(itemType.sprite_id);
		}
		if (!itemType.m_sprites.empty() && itemType.m_sprites.front() > 0) {
			return itemType.m_sprites.front();
		}
		return itemType.clientID;
	}

	DoodadPreviewSpriteMetrics ResolveDoodadPreviewSpriteMetrics(int itemId) {
		DoodadPreviewSpriteMetrics metrics;
		if (!IsKnownItemId(itemId)) {
			return metrics;
		}

		ItemType &itemType = g_items.getItemType(static_cast<uint16_t>(itemId));
		GameSprite* sprite = itemType.sprite;
		if (!sprite && itemType.clientID > 0) {
			sprite = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(itemType.clientID));
		}
		if (!sprite) {
			return metrics;
		}

		metrics.spriteId = ResolveDoodadPreviewLookId(itemId);
		if (const auto spriteData = g_spriteAppearances.getSprite(metrics.spriteId)) {
			metrics.widthPx = std::max<int>(spriteData->size.width, 32);
			metrics.heightPx = std::max<int>(spriteData->size.height, 32);
		} else {
			metrics.widthPx = std::max<int>(sprite->getWidth(), 32);
			metrics.heightPx = std::max<int>(sprite->getHeight(), 32);
		}
		metrics.drawOffset = sprite->getDrawOffset();
		metrics.drawHeight = sprite->getDrawHeight();
		return metrics;
	}

	bool HasValidDoodadPreviewSprite(int itemId) {
		return ResolveDoodadPreviewSpriteMetrics(itemId).isValid();
	}

	wxBitmap BuildDoodadPreviewBitmap(int spriteId) {
		const auto spriteData = g_spriteAppearances.getSprite(spriteId);
		if (!spriteData || spriteData->size.width <= 0 || spriteData->size.height <= 0) {
			return wxBitmap();
		}

		wxImage image(spriteData->size.width, spriteData->size.height);
		image.InitAlpha();
		const auto *pixels = spriteData->pixels.data();
		for (int y = 0; y < spriteData->size.height; ++y) {
			for (int x = 0; x < spriteData->size.width; ++x) {
				const int index = (y * spriteData->size.width + x) * 4;
				image.SetRGB(x, y, pixels[index + 2], pixels[index + 1], pixels[index]);
				image.SetAlpha(x, y, pixels[index + 3]);
			}
		}

		return wxBitmap(image);
	}

	wxRect GetBitmapVisibleBounds(const wxBitmap &bitmap) {
		if (!bitmap.IsOk()) {
			return wxRect();
		}

		const wxImage image = bitmap.ConvertToImage();
		if (!image.IsOk()) {
			return wxRect(0, 0, bitmap.GetWidth(), bitmap.GetHeight());
		}

		const int width = image.GetWidth();
		const int height = image.GetHeight();
		int minX = width;
		int minY = height;
		int maxX = -1;
		int maxY = -1;

		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				const bool visible = image.HasAlpha() ? image.GetAlpha(x, y) > 0 : true;
				if (!visible) {
					continue;
				}
				minX = std::min(minX, x);
				minY = std::min(minY, y);
				maxX = std::max(maxX, x);
				maxY = std::max(maxY, y);
			}
		}

		if (maxX < minX || maxY < minY) {
			return wxRect(0, 0, width, height);
		}

		return wxRect(minX, minY, maxX - minX + 1, maxY - minY + 1);
	}

	wxRect GetDoodadPreviewSpriteRect(int itemId, const wxPoint &drawPoint) {
		const DoodadPreviewSpriteMetrics metrics = ResolveDoodadPreviewSpriteMetrics(itemId);
		if (!metrics.isValid()) {
			return wxRect(drawPoint.x, drawPoint.y, 32, 32);
		}

		return wxRect(
			drawPoint.x - metrics.drawOffset.x,
			drawPoint.y - metrics.drawOffset.y,
			metrics.widthPx,
			metrics.heightPx
		);
	}

	wxRect GetDoodadPreviewTileStackBounds(const std::vector<DoodadCompositeTileItemRecord> &items, const wxPoint &tileAnchor) {
		wxRect bounds(tileAnchor.x, tileAnchor.y, 32, 32);
		int drawX = tileAnchor.x;
		int drawY = tileAnchor.y;
		for (const auto &item : items) {
			const DoodadPreviewSpriteMetrics metrics = ResolveDoodadPreviewSpriteMetrics(item.itemId);
			bounds.Union(GetDoodadPreviewSpriteRect(item.itemId, wxPoint(drawX, drawY)));
			if (metrics.isValid()) {
				drawX -= metrics.drawHeight;
				drawY -= metrics.drawHeight;
			}
		}
		return bounds;
	}

	void DrawDoodadPreviewTileStack(wxDC &dc, const std::vector<DoodadCompositeTileItemRecord> &items, const wxPoint &tileAnchor) {
		int drawX = tileAnchor.x;
		int drawY = tileAnchor.y;
		for (const auto &item : items) {
			const DoodadPreviewSpriteMetrics metrics = ResolveDoodadPreviewSpriteMetrics(item.itemId);
			if (metrics.isValid()) {
				const wxRect spriteRect = GetDoodadPreviewSpriteRect(item.itemId, wxPoint(drawX, drawY));
				const wxBitmap bitmap = BuildDoodadPreviewBitmap(metrics.spriteId);
				if (bitmap.IsOk()) {
					dc.DrawBitmap(bitmap, spriteRect.x, spriteRect.y, true);
				}
				drawX -= metrics.drawHeight;
				drawY -= metrics.drawHeight;
			}
		}
	}

	void DrawDoodadPreviewItemSprite(wxDC &dc, int itemId, const wxPoint &tileAnchor) {
		const DoodadPreviewSpriteMetrics metrics = ResolveDoodadPreviewSpriteMetrics(itemId);
		if (!metrics.isValid()) {
			return;
		}

		const wxRect spriteRect = GetDoodadPreviewSpriteRect(itemId, tileAnchor);
		const wxBitmap bitmap = BuildDoodadPreviewBitmap(metrics.spriteId);
		if (!bitmap.IsOk()) {
			return;
		}
		dc.DrawBitmap(bitmap, spriteRect.x, spriteRect.y, true);
	}

	void DrawCenteredPreviewItemSprite(wxDC &dc, const wxRect &bounds, int itemId) {
		const DoodadPreviewSpriteMetrics metrics = ResolveDoodadPreviewSpriteMetrics(itemId);
		if (!metrics.isValid()) {
			return;
		}

		const wxBitmap bitmap = BuildDoodadPreviewBitmap(metrics.spriteId);
		if (!bitmap.IsOk()) {
			return;
		}

		const wxRect visibleBounds = GetBitmapVisibleBounds(bitmap);
		const int targetX = bounds.x + std::max(0, (bounds.width - visibleBounds.width) / 2);
		const int targetY = bounds.y + std::max(0, (bounds.height - visibleBounds.height) / 2);
		const int drawX = targetX - visibleBounds.x;
		const int drawY = targetY - visibleBounds.y;
		dc.DrawBitmap(bitmap, drawX, drawY, true);
	}

	int SumWeightedBrushChances(const std::vector<BrushItemRecord> &items) {
		int total = 0;
		for (const BrushItemRecord &item : items) {
			total += std::max(0, item.chance);
		}
		return total;
	}

	double ComputeWeightedBrushRatio(const std::vector<BrushItemRecord> &items, size_t index) {
		if (index >= items.size()) {
			return 0.0;
		}
		const int total = SumWeightedBrushChances(items);
		if (total <= 0) {
			return items.empty() ? 0.0 : 1.0 / static_cast<double>(items.size());
		}
		return static_cast<double>(std::max(0, items[index].chance)) / static_cast<double>(total);
	}

	wxString FormatWeightedBrushPercent(const std::vector<BrushItemRecord> &items, size_t index) {
		return wxString::Format("%.1f%%", ComputeWeightedBrushRatio(items, index) * 100.0);
	}

	wxString GetWeightedBrushBadge(const std::vector<BrushItemRecord> &items, size_t index) {
		const double ratio = ComputeWeightedBrushRatio(items, index);
		if (ratio >= 0.60) {
			return "dominant";
		}
		if (ratio >= 0.30) {
			return "common";
		}
		if (ratio >= 0.12) {
			return "steady";
		}
		if (ratio >= 0.05) {
			return "rare";
		}
		return "ultra rare";
	}

	wxColour GetWeightedBrushBadgeColour(const std::vector<BrushItemRecord> &items, size_t index) {
		const double ratio = ComputeWeightedBrushRatio(items, index);
		if (ratio >= 0.60) {
			return wxColour(80, 166, 255);
		}
		if (ratio >= 0.30) {
			return wxColour(91, 194, 139);
		}
		if (ratio >= 0.12) {
			return wxColour(232, 190, 96);
		}
		if (ratio >= 0.05) {
			return wxColour(230, 140, 72);
		}
		return wxColour(194, 96, 126);
	}

	std::vector<wxRect> BuildWeightedBrushCardRects(wxWindow* window, const wxRect &clientRect, size_t count) {
		std::vector<wxRect> rects;
		if (!window || count == 0) {
			return rects;
		}
		const int padding = window->FromDIP(8);
		const int gap = window->FromDIP(8);
		const int cardHeight = window->FromDIP(78);
		const int cardWidth = std::max(window->FromDIP(220), clientRect.width - padding * 2);
		rects.reserve(count);
		int y = padding;
		for (size_t i = 0; i < count; ++i) {
			rects.emplace_back(padding, y, cardWidth, cardHeight);
			y += cardHeight + gap;
		}
		return rects;
	}

	template <typename TItemRecord>
	int SumAlignedItemChances(const std::vector<TItemRecord> &items) {
		int total = 0;
		for (const auto &item : items) {
			total += std::max(0, item.chance);
		}
		return total;
	}

	template <typename TItemRecord>
	double ComputeAlignedItemRatio(const std::vector<TItemRecord> &items, size_t index) {
		if (index >= items.size()) {
			return 0.0;
		}
		const int total = SumAlignedItemChances(items);
		if (total <= 0) {
			return items.empty() ? 0.0 : 1.0 / static_cast<double>(items.size());
		}
		return static_cast<double>(std::max(0, items[index].chance)) / static_cast<double>(total);
	}

	template <typename TItemRecord>
	wxString FormatAlignedItemPercent(const std::vector<TItemRecord> &items, size_t index) {
		return wxString::Format("%.1f%%", ComputeAlignedItemRatio(items, index) * 100.0);
	}

	template <typename TItemRecord>
	wxString GetAlignedItemBadge(const std::vector<TItemRecord> &items, size_t index) {
		const double ratio = ComputeAlignedItemRatio(items, index);
		if (ratio >= 0.60) {
			return "dominant";
		}
		if (ratio >= 0.30) {
			return "common";
		}
		if (ratio >= 0.12) {
			return "steady";
		}
		if (ratio >= 0.05) {
			return "rare";
		}
		return "ultra rare";
	}

	template <typename TItemRecord>
	wxColour GetAlignedItemBadgeColour(const std::vector<TItemRecord> &items, size_t index) {
		const double ratio = ComputeAlignedItemRatio(items, index);
		if (ratio >= 0.60) {
			return wxColour(80, 166, 255);
		}
		if (ratio >= 0.30) {
			return wxColour(91, 194, 139);
		}
		if (ratio >= 0.12) {
			return wxColour(232, 190, 96);
		}
		if (ratio >= 0.05) {
			return wxColour(230, 140, 72);
		}
		return wxColour(194, 96, 126);
	}

	struct AlignedContextSlot {
		const char* align = "";
		const char* label = "";
		const char* description = "";
		int column = 0;
		int row = 0;
	};

	const std::vector<AlignedContextSlot>& GetCarpetContextSlots() {
		static const std::vector<AlignedContextSlot> kSlots = {
			{"cse", "NW", "Top-left carpet slot.", 0, 0},
			{"s", "N", "Top edge carpet slot.", 1, 0},
			{"csw", "NE", "Top-right carpet slot.", 2, 0},
			{"e", "W", "Left edge carpet slot.", 0, 1},
			{"center", "C", "Center carpet slot.", 1, 1},
			{"w", "E", "Right edge carpet slot.", 2, 1},
			{"cne", "SW", "Bottom-left carpet slot.", 0, 2},
			{"n", "S", "Bottom edge carpet slot.", 1, 2},
			{"cnw", "SE", "Bottom-right carpet slot.", 2, 2},
		};
		return kSlots;
	}

	const std::vector<AlignedContextSlot>& GetTableContextSlots() {
		static const std::vector<AlignedContextSlot> kSlots = {
			{"north", "Top\nEnd", "Closes a vertical run at the top.", 0, 0},
			{"vertical", "Vertical\nRun", "Connects with neighbors above and below.", 0, 1},
			{"south", "Bottom\nEnd", "Closes a vertical run at the bottom.", 0, 2},
			{"west", "Left\nEnd", "Closes a horizontal run on the left.", 0, 3},
			{"horizontal", "Horizontal\nRun", "Connects with neighbors left and right.", 1, 3},
			{"east", "Right\nEnd", "Closes a horizontal run on the right.", 2, 3},
			{"alone", "Paint\nSeed", "Used on the first click when painting before neighbors are formed.", 2, 0},
		};
		return kSlots;
	}

	const std::vector<AlignedContextSlot>& GetAlignedContextSlots(const wxString &type) {
		return type == "table" ? GetTableContextSlots() : GetCarpetContextSlots();
	}

	std::vector<wxString> GetCarpetAlignChoices() {
		static const char* kPreferredOrder[] = {"center", "s", "n", "e", "w", "cse", "csw", "cne", "cnw"};
		std::vector<wxString> aligns;
		aligns.reserve(std::size(kPreferredOrder));
		for (const char* align : kPreferredOrder) {
			aligns.emplace_back(wxString::FromUTF8(align));
		}
		return aligns;
	}

	std::vector<wxString> GetTableAlignChoices() {
		std::vector<wxString> aligns;
		for (const auto &slot : GetTableContextSlots()) {
			aligns.emplace_back(wxString::FromUTF8(slot.align));
		}
		return aligns;
	}

	bool IsKnownTableAlign(const wxString &align) {
		wxString normalized = align;
		normalized.MakeLower();
		for (const auto &candidate : GetTableAlignChoices()) {
			wxString current = candidate;
			current.MakeLower();
			if (current == normalized) {
				return true;
			}
		}
		return false;
	}

	bool IsKnownCarpetAlign(const wxString &align) {
		wxString normalized = align;
		normalized.MakeLower();
		for (const auto &candidate : GetCarpetAlignChoices()) {
			wxString current = candidate;
			current.MakeLower();
			if (current == normalized) {
				return true;
			}
		}
		return false;
	}

	template <typename TNodeRecord>
	int FindAlignedNodeIndexByAlign(const std::vector<TNodeRecord> &nodes, const wxString &align) {
		wxString target = align;
		target.MakeLower();
		for (size_t i = 0; i < nodes.size(); ++i) {
			wxString current = nodes[i].align;
			current.MakeLower();
			if (current == target) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	int FindTableNodeIndexByAlignExcluding(const std::vector<TableNodeRecord> &nodes, const wxString &align, int ignoredIndex) {
		wxString target = align;
		target.MakeLower();
		for (size_t i = 0; i < nodes.size(); ++i) {
			if (static_cast<int>(i) == ignoredIndex) {
				continue;
			}
			wxString current = nodes[i].align;
			current.MakeLower();
			if (current == target) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	wxString FindNextMissingTableAlign(const std::vector<TableNodeRecord> &nodes, const wxString &preferredAlign = "") {
		if (!preferredAlign.IsEmpty() && FindAlignedNodeIndexByAlign(nodes, preferredAlign) == -1 && IsKnownTableAlign(preferredAlign)) {
			return preferredAlign;
		}
		for (const wxString &candidate : GetTableAlignChoices()) {
			if (FindAlignedNodeIndexByAlign(nodes, candidate) == -1) {
				return candidate;
			}
		}
		return "";
	}

	wxString FindNextMissingCarpetAlign(const std::vector<CarpetNodeRecord> &nodes, const wxString &preferredAlign = "") {
		if (!preferredAlign.IsEmpty() && FindAlignedNodeIndexByAlign(nodes, preferredAlign) == -1 && IsKnownCarpetAlign(preferredAlign)) {
			return preferredAlign;
		}
		for (const wxString &candidate : GetCarpetAlignChoices()) {
			if (FindAlignedNodeIndexByAlign(nodes, candidate) == -1) {
				return candidate;
			}
		}
		return "";
	}

	wxPoint ResolveCarpetPreviewAnchor(wxWindow* window, const wxRect &cellRect, const wxString &align, const wxRect &spriteBounds) {
		(void)window;
		(void)align;
		const int centerX = cellRect.x + (cellRect.width - spriteBounds.width) / 2 - spriteBounds.x;
		const int centerY = cellRect.y + (cellRect.height - spriteBounds.height) / 2 - spriteBounds.y;
		return wxPoint(centerX, centerY);
	}

	void DrawAlignedCarpetContextScene(
		wxDC &dc,
		wxWindow* window,
		const wxRect &cellRect,
		const wxString &align,
		int itemId,
		bool selected
	) {
		const wxRect centerReference(
			cellRect.x + (cellRect.width - window->FromDIP(20)) / 2,
			cellRect.y + (cellRect.height - window->FromDIP(20)) / 2,
			window->FromDIP(20),
			window->FromDIP(20)
		);
		dc.SetPen(wxPen(selected ? wxColour(80, 166, 255) : wxColour(88, 96, 114), 1));
		dc.SetBrush(wxBrush(wxColour(42, 48, 58)));
		dc.DrawRoundedRectangle(centerReference, window->FromDIP(4));

		wxString normalized = align;
		normalized.MakeLower();
		if (normalized != "center") {
			dc.SetPen(wxPen(wxColour(74, 82, 96), 1, wxPENSTYLE_DOT));
			const wxPoint start(
				centerReference.x + centerReference.width / 2,
				centerReference.y + centerReference.height / 2
			);
			wxPoint end = start;
			if (normalized == "n") {
				end.y = cellRect.y + window->FromDIP(10);
			} else if (normalized == "s") {
				end.y = cellRect.GetBottom() - window->FromDIP(10);
			} else if (normalized == "w") {
				end.x = cellRect.x + window->FromDIP(10);
			} else if (normalized == "e") {
				end.x = cellRect.GetRight() - window->FromDIP(10);
			} else if (normalized == "cnw") {
				end = wxPoint(cellRect.x + window->FromDIP(10), cellRect.y + window->FromDIP(10));
			} else if (normalized == "cne") {
				end = wxPoint(cellRect.GetRight() - window->FromDIP(10), cellRect.y + window->FromDIP(10));
			} else if (normalized == "csw") {
				end = wxPoint(cellRect.x + window->FromDIP(10), cellRect.GetBottom() - window->FromDIP(10));
			} else if (normalized == "cse") {
				end = wxPoint(cellRect.GetRight() - window->FromDIP(10), cellRect.GetBottom() - window->FromDIP(10));
			}
			dc.DrawLine(start, end);
		}

		if (itemId > 0) {
			const wxRect spriteBounds = GetDoodadPreviewSpriteRect(itemId, wxPoint(0, 0));
			const wxPoint drawPoint = ResolveCarpetPreviewAnchor(window, cellRect, align, spriteBounds);
			DrawDoodadPreviewItemSprite(dc, itemId, drawPoint);
		}
	}

	void DrawTableNeighbourStub(wxDC &dc, wxWindow* window, const wxRect &cellRect, const wxString &align) {
		const int railGap = window->FromDIP(8);
		const int railReach = window->FromDIP(12);
		const int capSize = window->FromDIP(6);
		const wxColour lineColour(88, 96, 110);
		const wxColour capColour(74, 82, 96);
		dc.SetPen(wxPen(lineColour, 2));

		auto drawCap = [&](const wxPoint &center) {
			const wxRect capRect(
				center.x - capSize / 2,
				center.y - capSize / 2,
				capSize,
				capSize
			);
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.SetBrush(wxBrush(capColour));
			dc.DrawRoundedRectangle(capRect, window->FromDIP(2));
			dc.SetPen(wxPen(lineColour, 2));
		};

		auto drawVerticalRail = [&](bool topCap, bool bottomCap) {
			const int railX = cellRect.GetRight() + railGap;
			const int centerY = cellRect.y + cellRect.height / 2;
			const wxPoint top(railX, centerY - railReach);
			const wxPoint bottom(railX, centerY + railReach);
			dc.DrawLine(top, bottom);
			if (topCap) {
				drawCap(top);
			}
			if (bottomCap) {
				drawCap(bottom);
			}
		};

		auto drawHorizontalRail = [&](bool leftCap, bool rightCap) {
			const int railY = cellRect.GetBottom() + railGap;
			const int centerX = cellRect.x + cellRect.width / 2;
			const wxPoint left(centerX - railReach, railY);
			const wxPoint right(centerX + railReach, railY);
			dc.DrawLine(left, right);
			if (leftCap) {
				drawCap(left);
			}
			if (rightCap) {
				drawCap(right);
			}
		};

		wxString normalized = align;
		normalized.MakeLower();
		if (normalized == "vertical") {
			drawVerticalRail(true, true);
		} else if (normalized == "north") {
			drawVerticalRail(true, false);
		} else if (normalized == "south") {
			drawVerticalRail(false, true);
		} else if (normalized == "horizontal") {
			drawHorizontalRail(true, true);
		} else if (normalized == "west") {
			drawHorizontalRail(true, false);
		} else if (normalized == "east") {
			drawHorizontalRail(false, true);
		}
	}

	void DrawAlignedTableContextScene(
		wxDC &dc,
		wxWindow* window,
		const wxRect &cellRect,
		const wxString &align,
		int itemId
	) {
		DrawTableNeighbourStub(dc, window, cellRect, align);
		if (itemId <= 0) {
			return;
		}

		wxRect spriteRect = cellRect;
		spriteRect.Deflate(window->FromDIP(8), window->FromDIP(8));
		DrawCenteredPreviewItemSprite(dc, spriteRect, itemId);
	}

	std::vector<int> CollectDoodadCompositeFloors(const DoodadCompositeRecord &composite) {
		std::set<int> uniqueFloors;
		for (const auto &tile : composite.tiles) {
			uniqueFloors.insert(tile.offsetZ);
		}
		return std::vector<int>(uniqueFloors.begin(), uniqueFloors.end());
	}

	struct DoodadPreviewFloorLayout {
		int floor = 0;
		bool showTitle = true;
		int minCellX = 0;
		int maxCellX = 0;
		int minCellY = 0;
		int maxCellY = 0;
		int originX = 0;
		int originY = 0;
		int titleY = 0;
		std::vector<int> tileIndices;
	};

	struct DoodadPreviewHit {
		bool valid = false;
		int floor = 0;
		int cellX = 0;
		int cellY = 0;
		int tileIndex = -1;
		wxRect cellRect;
	};

	const DoodadCompositeRecord* GetSelectedDoodadComposite(
		const BrushStorageRecord &storage,
		int alternativeIndex,
		int compositeIndex
	) {
		if (alternativeIndex < 0 || alternativeIndex >= static_cast<int>(storage.doodadAlternatives.size())) {
			return nullptr;
		}
		const auto &alternative = storage.doodadAlternatives[alternativeIndex];
		if (compositeIndex < 0 || compositeIndex >= static_cast<int>(alternative.composites.size())) {
			return nullptr;
		}
		return &alternative.composites[compositeIndex];
	}

	DoodadCompositeRecord* GetSelectedDoodadComposite(
		BrushStorageRecord &storage,
		int alternativeIndex,
		int compositeIndex
	) {
		if (alternativeIndex < 0 || alternativeIndex >= static_cast<int>(storage.doodadAlternatives.size())) {
			return nullptr;
		}
		auto &alternative = storage.doodadAlternatives[alternativeIndex];
		if (compositeIndex < 0 || compositeIndex >= static_cast<int>(alternative.composites.size())) {
			return nullptr;
		}
		return &alternative.composites[compositeIndex];
	}

	std::vector<int> ResolveDoodadPreviewFloorsToDraw(const DoodadCompositeRecord &composite, int selectedFloor) {
		if (selectedFloor != MaterialsWorkbenchBrushPanel::kDoodadPreviewAllFloors) {
			return {selectedFloor};
		}
		return CollectDoodadCompositeFloors(composite);
	}

	wxPoint GetDoodadPreviewProjectedCell(const DoodadCompositeTileRecord &tile, bool combinedAllFloors) {
		if (!combinedAllFloors) {
			return wxPoint(tile.offsetX, tile.offsetY);
		}
		// Local doodad floors use negative Z for upper levels; project them up/left in the combined scene.
		return wxPoint(tile.offsetX + tile.offsetZ, tile.offsetY + tile.offsetZ);
	}

	DoodadPreviewFloorLayout BuildDoodadPreviewEmptyLayout(const wxRect &contentRect, int floor, int cellSize) {
		DoodadPreviewFloorLayout layout;
		layout.floor = floor;
		layout.showTitle = floor != MaterialsWorkbenchBrushPanel::kDoodadPreviewAllFloors;
		layout.minCellX = -1;
		layout.maxCellX = 1;
		layout.minCellY = -1;
		layout.maxCellY = 1;

		const int logicalMinPixelX = layout.minCellX * cellSize;
		const int logicalMaxPixelX = (layout.maxCellX + 1) * cellSize;
		const int logicalMinPixelY = layout.minCellY * cellSize;
		const int logicalMaxPixelY = (layout.maxCellY + 1) * cellSize;
		const int logicalBoundsWidth = logicalMaxPixelX - logicalMinPixelX;
		const int logicalBoundsHeight = logicalMaxPixelY - logicalMinPixelY;
		layout.originX = contentRect.x + std::max(0, (contentRect.GetWidth() - logicalBoundsWidth) / 2) - logicalMinPixelX;
		layout.originY = contentRect.y + std::max(0, (contentRect.GetHeight() - logicalBoundsHeight) / 2) - logicalMinPixelY;
		layout.titleY = contentRect.y;
		return layout;
	}

	DoodadPreviewFloorLayout BuildDoodadPreviewCombinedLayout(
		const wxRect &contentRect,
		const DoodadCompositeRecord &composite,
		int cellSize
	) {
		DoodadPreviewFloorLayout layout;
		layout.floor = MaterialsWorkbenchBrushPanel::kDoodadPreviewAllFloors;
		layout.showTitle = false;

		bool initialized = false;

		for (size_t tileIndex = 0; tileIndex < composite.tiles.size(); ++tileIndex) {
			const auto &tile = composite.tiles[tileIndex];
			const wxPoint projectedCell = GetDoodadPreviewProjectedCell(tile, true);
			layout.tileIndices.push_back(static_cast<int>(tileIndex));
			if (!initialized) {
				layout.minCellX = layout.maxCellX = projectedCell.x;
				layout.minCellY = layout.maxCellY = projectedCell.y;
				initialized = true;
			} else {
				layout.minCellX = std::min(layout.minCellX, projectedCell.x);
				layout.maxCellX = std::max(layout.maxCellX, projectedCell.x);
				layout.minCellY = std::min(layout.minCellY, projectedCell.y);
				layout.maxCellY = std::max(layout.maxCellY, projectedCell.y);
			}
		}

		std::stable_sort(
			layout.tileIndices.begin(),
			layout.tileIndices.end(),
			[&composite](int leftIndex, int rightIndex) {
				const auto &left = composite.tiles[leftIndex];
				const auto &right = composite.tiles[rightIndex];
				if (left.offsetZ != right.offsetZ) {
					return left.offsetZ > right.offsetZ;
				}
				if (left.offsetX != right.offsetX) {
					return left.offsetX < right.offsetX;
				}
				return left.offsetY < right.offsetY;
			}
		);

		layout.minCellX -= 1;
		layout.maxCellX += 1;
		layout.minCellY -= 1;
		layout.maxCellY += 1;

		const int logicalMinPixelX = layout.minCellX * cellSize;
		const int logicalMaxPixelX = (layout.maxCellX + 1) * cellSize;
		const int logicalMinPixelY = layout.minCellY * cellSize;
		const int logicalMaxPixelY = (layout.maxCellY + 1) * cellSize;
		const int logicalBoundsWidth = logicalMaxPixelX - logicalMinPixelX;
		const int logicalBoundsHeight = logicalMaxPixelY - logicalMinPixelY;
		layout.originX = contentRect.x + std::max(0, (contentRect.GetWidth() - logicalBoundsWidth) / 2) - logicalMinPixelX;
		layout.originY = contentRect.y + std::max(0, (contentRect.GetHeight() - logicalBoundsHeight) / 2) - logicalMinPixelY;
		layout.titleY = contentRect.y;
		return layout;
	}

	std::vector<DoodadPreviewFloorLayout> BuildDoodadPreviewFloorLayouts(
		wxDC &dc,
		const wxRect &contentRect,
		const DoodadCompositeRecord &composite,
		const std::vector<int> &floorsToDraw,
		int cellSize,
		int floorGap,
		int titleGap
	) {
		struct FloorMeasure {
			int floor = 0;
			int minCellX = 0;
			int maxCellX = 0;
			int minCellY = 0;
			int maxCellY = 0;
			int minPixelX = 0;
			int maxPixelX = 0;
			int minPixelY = 0;
			int maxPixelY = 0;
			std::vector<int> tileIndices;
		};

		std::vector<FloorMeasure> measures;
		measures.reserve(floorsToDraw.size());
		int globalMinCellX = 0;
		int globalMaxCellX = 0;
		int globalMinCellY = 0;
		int globalMaxCellY = 0;
		int globalMinPixelX = 0;
		int globalMaxPixelX = 0;
		int globalMinPixelY = 0;
		int globalMaxPixelY = 0;
		bool hasGlobalBounds = false;
		for (int floor : floorsToDraw) {
			FloorMeasure measure;
			measure.floor = floor;
			bool initialized = false;
			for (size_t tileIndex = 0; tileIndex < composite.tiles.size(); ++tileIndex) {
				const auto &tile = composite.tiles[tileIndex];
				if (tile.offsetZ != floor) {
					continue;
				}

				measure.tileIndices.push_back(static_cast<int>(tileIndex));
				if (!initialized) {
					measure.minCellX = measure.maxCellX = tile.offsetX;
					measure.minCellY = measure.maxCellY = tile.offsetY;
					measure.minPixelX = tile.offsetX * cellSize;
					measure.maxPixelX = tile.offsetX * cellSize + cellSize;
					measure.minPixelY = tile.offsetY * cellSize;
					measure.maxPixelY = tile.offsetY * cellSize + cellSize;
					initialized = true;
				} else {
					measure.minCellX = std::min(measure.minCellX, tile.offsetX);
					measure.maxCellX = std::max(measure.maxCellX, tile.offsetX);
					measure.minCellY = std::min(measure.minCellY, tile.offsetY);
					measure.maxCellY = std::max(measure.maxCellY, tile.offsetY);
				}

				const wxPoint tileAnchor(tile.offsetX * cellSize, tile.offsetY * cellSize);
				const wxRect cellRect(tileAnchor.x, tileAnchor.y, cellSize, cellSize);
				measure.minPixelX = std::min(measure.minPixelX, cellRect.GetLeft());
				measure.maxPixelX = std::max(measure.maxPixelX, cellRect.GetRight() + 1);
				measure.minPixelY = std::min(measure.minPixelY, cellRect.GetTop());
				measure.maxPixelY = std::max(measure.maxPixelY, cellRect.GetBottom() + 1);
				const wxRect stackRect = GetDoodadPreviewTileStackBounds(tile.items, tileAnchor);
				measure.minPixelX = std::min(measure.minPixelX, stackRect.GetLeft());
				measure.maxPixelX = std::max(measure.maxPixelX, stackRect.GetRight() + 1);
				measure.minPixelY = std::min(measure.minPixelY, stackRect.GetTop());
				measure.maxPixelY = std::max(measure.maxPixelY, stackRect.GetBottom() + 1);
			}

			if (!measure.tileIndices.empty()) {
				std::stable_sort(
					measure.tileIndices.begin(),
					measure.tileIndices.end(),
					[&composite](int leftIndex, int rightIndex) {
						const auto &left = composite.tiles[leftIndex];
						const auto &right = composite.tiles[rightIndex];
						if (left.offsetX != right.offsetX) {
							return left.offsetX < right.offsetX;
						}
						return left.offsetY < right.offsetY;
					}
				);

				measure.minCellX -= 1;
				measure.maxCellX += 1;
				measure.minCellY -= 1;
				measure.maxCellY += 1;
				measure.minPixelX = std::min(measure.minPixelX, measure.minCellX * cellSize);
				measure.maxPixelX = std::max(measure.maxPixelX, (measure.maxCellX + 1) * cellSize);
				measure.minPixelY = std::min(measure.minPixelY, measure.minCellY * cellSize);
				measure.maxPixelY = std::max(measure.maxPixelY, (measure.maxCellY + 1) * cellSize);
				measures.push_back(measure);

				if (!hasGlobalBounds) {
					globalMinCellX = measure.minCellX;
					globalMaxCellX = measure.maxCellX;
					globalMinCellY = measure.minCellY;
					globalMaxCellY = measure.maxCellY;
					globalMinPixelX = measure.minPixelX;
					globalMaxPixelX = measure.maxPixelX;
					globalMinPixelY = measure.minPixelY;
					globalMaxPixelY = measure.maxPixelY;
					hasGlobalBounds = true;
				} else {
					globalMinCellX = std::min(globalMinCellX, measure.minCellX);
					globalMaxCellX = std::max(globalMaxCellX, measure.maxCellX);
					globalMinCellY = std::min(globalMinCellY, measure.minCellY);
					globalMaxCellY = std::max(globalMaxCellY, measure.maxCellY);
					globalMinPixelX = std::min(globalMinPixelX, measure.minPixelX);
					globalMaxPixelX = std::max(globalMaxPixelX, measure.maxPixelX);
					globalMinPixelY = std::min(globalMinPixelY, measure.minPixelY);
					globalMaxPixelY = std::max(globalMaxPixelY, measure.maxPixelY);
				}
			}
		}

		const int titleHeight = dc.GetCharHeight();
		const bool useSharedViewport = measures.size() > 1;
		const int globalBoundsHeight = hasGlobalBounds ? (globalMaxPixelY - globalMinPixelY) : 0;
		int totalHeight = 0;
		for (size_t i = 0; i < measures.size(); ++i) {
			const int boundsHeight = useSharedViewport ? globalBoundsHeight : (measures[i].maxPixelY - measures[i].minPixelY);
			totalHeight += titleHeight + titleGap + boundsHeight;
			if (i + 1 < measures.size()) {
				totalHeight += floorGap;
			}
		}

		int currentY = contentRect.y + std::max(0, (contentRect.GetHeight() - totalHeight) / 2);
		std::vector<DoodadPreviewFloorLayout> layouts;
		layouts.reserve(measures.size());
		const int globalBoundsWidth = hasGlobalBounds ? (globalMaxPixelX - globalMinPixelX) : 0;
		const int sharedOriginX = contentRect.x + std::max(0, (contentRect.GetWidth() - globalBoundsWidth) / 2) - globalMinPixelX;
		for (const auto &measure : measures) {
			const int boundsWidth = measure.maxPixelX - measure.minPixelX;
			const int boundsHeight = useSharedViewport ? globalBoundsHeight : (measure.maxPixelY - measure.minPixelY);

			DoodadPreviewFloorLayout layout;
			layout.floor = measure.floor;
			layout.minCellX = useSharedViewport ? globalMinCellX : measure.minCellX;
			layout.maxCellX = useSharedViewport ? globalMaxCellX : measure.maxCellX;
			layout.minCellY = useSharedViewport ? globalMinCellY : measure.minCellY;
			layout.maxCellY = useSharedViewport ? globalMaxCellY : measure.maxCellY;
			layout.originX = useSharedViewport
				? sharedOriginX
				: contentRect.x + std::max(0, (contentRect.GetWidth() - boundsWidth) / 2) - measure.minPixelX;
			layout.originY = useSharedViewport
				? currentY + titleHeight + titleGap - globalMinPixelY
				: currentY + titleHeight + titleGap - measure.minPixelY;
			layout.titleY = currentY;
			layout.tileIndices = measure.tileIndices;
			layouts.push_back(layout);
			currentY += titleHeight + titleGap + boundsHeight + floorGap;
		}

		return layouts;
	}

	std::vector<DoodadPreviewFloorLayout> BuildDoodadPreviewLayouts(
		wxDC &dc,
		const wxRect &contentRect,
		const DoodadCompositeRecord &composite,
		int selectedFloor,
		int cellSize,
		int floorGap,
		int titleGap
	) {
		if (composite.tiles.empty()) {
			const int emptyFloor = selectedFloor == MaterialsWorkbenchBrushPanel::kDoodadPreviewAllFloors ? selectedFloor : selectedFloor;
			return {BuildDoodadPreviewEmptyLayout(contentRect, emptyFloor, cellSize)};
		}
		if (selectedFloor == MaterialsWorkbenchBrushPanel::kDoodadPreviewAllFloors) {
			return {BuildDoodadPreviewCombinedLayout(contentRect, composite, cellSize)};
		}
		return BuildDoodadPreviewFloorLayouts(
			dc,
			contentRect,
			composite,
			ResolveDoodadPreviewFloorsToDraw(composite, selectedFloor),
			cellSize,
			floorGap,
			titleGap
		);
	}

	bool HitTestDoodadPreview(
		const DoodadCompositeRecord &composite,
		const std::vector<DoodadPreviewFloorLayout> &layouts,
		const wxPoint &point,
		int cellSize,
		DoodadPreviewHit &hit
	) {
		for (const auto &layout : layouts) {
			for (int cellY = layout.minCellY; cellY <= layout.maxCellY; ++cellY) {
				for (int cellX = layout.minCellX; cellX <= layout.maxCellX; ++cellX) {
					const wxRect cellRect(
						layout.originX + cellX * cellSize,
						layout.originY + cellY * cellSize,
						cellSize,
						cellSize
					);
					if (!cellRect.Contains(point)) {
						continue;
					}

					hit.valid = true;
					hit.floor = layout.floor;
					hit.cellX = cellX;
					hit.cellY = cellY;
					hit.cellRect = cellRect;
					for (auto it = layout.tileIndices.rbegin(); it != layout.tileIndices.rend(); ++it) {
						const int tileIndex = *it;
						const auto &tile = composite.tiles[tileIndex];
						const bool combinedAllFloors = layout.floor == MaterialsWorkbenchBrushPanel::kDoodadPreviewAllFloors;
						const bool matchingFloor = combinedAllFloors || tile.offsetZ == layout.floor;
						const wxPoint projectedCell = GetDoodadPreviewProjectedCell(tile, combinedAllFloors);
						if (projectedCell.x == cellX && projectedCell.y == cellY && matchingFloor) {
							hit.tileIndex = tileIndex;
							hit.floor = tile.offsetZ;
							break;
						}
					}
					return true;
				}
			}
		}
		return false;
	}

	wxString FormatBrushOverviewText(const BrushStorageRecord &storage) {
		const BrushRecord &brush = storage.brush;
		wxString text;
		text << "Brush: " << brush.name << "\n";
		text << "Type: " << brush.type << "\n";
		text << "ID: " << brush.id << "\n";
		text << "Storage: materials.db\n";
		text << "Imported from: " << FormatImportedFromValue(brush.sourceFile) << "\n\n";
		text << "lookId: " << brush.lookId << "\n";
		text << "serverLookId: " << brush.serverLookId << "\n";
		text << "zOrder: " << brush.zOrder << "\n";
		return text;
	}

	wxString FormatBrushInspectorText(const BrushStorageRecord &storage) {
		const BrushRecord &brush = storage.brush;
		wxString text = FormatBrushOverviewText(storage);
		text << "\nFlags\n";
		text << "  draggable: " << (brush.draggable ? "yes" : "no") << "\n";
		text << "  onBlocking: " << (brush.onBlocking ? "yes" : "no") << "\n";
		text << "  onDuplicate: " << (brush.onDuplicate ? "yes" : "no") << "\n";
		text << "  redoBorders: " << (brush.redoBorders ? "yes" : "no") << "\n";
		text << "  randomize: " << (brush.randomize ? "yes" : "no") << "\n";
		text << "  oneSize: " << (brush.oneSize ? "yes" : "no") << "\n";
		text << "  soloOptional: " << (brush.soloOptional ? "yes" : "no") << "\n";
		text << "\nBrush items: " << storage.items.size() << "\n";
		text << "Ground borders: " << storage.borders.size() << "\n";
		text << "Links: " << storage.links.size() << "\n";
		text << "Wall parts: " << storage.wallParts.size() << "\n";
		text << "Carpet nodes: " << storage.carpetNodes.size() << "\n";
		text << "Table nodes: " << storage.tableNodes.size() << "\n";
		text << "Doodad alternatives: " << storage.doodadAlternatives.size() << "\n";
		return text;
	}

	void NormalizeVariationSortOrdersForStorage(BrushStorageRecord &storage) {
		for (size_t i = 0; i < storage.items.size(); ++i) {
			storage.items[i].sortOrder = static_cast<int>(i);
		}
		for (size_t i = 0; i < storage.carpetNodes.size(); ++i) {
			storage.carpetNodes[i].sortOrder = static_cast<int>(i);
			for (size_t j = 0; j < storage.carpetNodes[i].items.size(); ++j) {
				storage.carpetNodes[i].items[j].sortOrder = static_cast<int>(j);
			}
		}
		for (size_t i = 0; i < storage.tableNodes.size(); ++i) {
			storage.tableNodes[i].sortOrder = static_cast<int>(i);
			for (size_t j = 0; j < storage.tableNodes[i].items.size(); ++j) {
				storage.tableNodes[i].items[j].sortOrder = static_cast<int>(j);
			}
		}
		for (size_t i = 0; i < storage.doodadAlternatives.size(); ++i) {
			DoodadAlternativeRecord &alternative = storage.doodadAlternatives[i];
			alternative.sortOrder = static_cast<int>(i);
			for (size_t j = 0; j < alternative.singleItems.size(); ++j) {
				alternative.singleItems[j].sortOrder = static_cast<int>(j);
			}
			for (size_t j = 0; j < alternative.composites.size(); ++j) {
				DoodadCompositeRecord &composite = alternative.composites[j];
				composite.sortOrder = static_cast<int>(j);
				for (size_t k = 0; k < composite.tiles.size(); ++k) {
					DoodadCompositeTileRecord &tile = composite.tiles[k];
					tile.sortOrder = static_cast<int>(k);
					for (size_t l = 0; l < tile.items.size(); ++l) {
						tile.items[l].sortOrder = static_cast<int>(l);
					}
				}
			}
		}
	}

	void NormalizeBrushStorageForEditing(BrushStorageRecord &storage) {
		storage.brush.type.MakeLower();
		if (storage.brush.type == "ground") {
			storage.carpetNodes.clear();
			storage.tableNodes.clear();
			storage.doodadAlternatives.clear();
		} else if (storage.brush.type == "carpet") {
			storage.items.clear();
			storage.tableNodes.clear();
			storage.doodadAlternatives.clear();
		} else if (storage.brush.type == "table") {
			storage.items.clear();
			storage.carpetNodes.clear();
			storage.doodadAlternatives.clear();
		} else if (storage.brush.type == "doodad") {
			storage.items.clear();
			storage.carpetNodes.clear();
			storage.tableNodes.clear();
		} else {
			storage.items.clear();
			storage.carpetNodes.clear();
			storage.tableNodes.clear();
			storage.doodadAlternatives.clear();
		}
		NormalizeVariationSortOrdersForStorage(storage);
	}

	template <typename T, typename Compare>
	bool VectorsEqual(const std::vector<T> &left, const std::vector<T> &right, Compare compare) {
		if (left.size() != right.size()) {
			return false;
		}
		for (size_t i = 0; i < left.size(); ++i) {
			if (!compare(left[i], right[i])) {
				return false;
			}
		}
		return true;
	}

	bool AreBrushRecordsEqual(const BrushRecord &left, const BrushRecord &right) {
		return left.id == right.id &&
			   left.name == right.name &&
			   left.type == right.type &&
			   left.lookId == right.lookId &&
			   left.zOrder == right.zOrder &&
			   left.sourceFile == right.sourceFile &&
			   left.serverLookId == right.serverLookId &&
			   left.draggable == right.draggable &&
			   left.onBlocking == right.onBlocking &&
			   left.onDuplicate == right.onDuplicate &&
			   left.redoBorders == right.redoBorders &&
			   left.randomize == right.randomize &&
			   left.oneSize == right.oneSize &&
			   left.soloOptional == right.soloOptional &&
			   left.thickness == right.thickness &&
			   left.thicknessCeiling == right.thicknessCeiling;
	}

	bool AreBrushItemRecordsEqual(const BrushItemRecord &left, const BrushItemRecord &right) {
		return left.brushId == right.brushId &&
			   left.itemId == right.itemId &&
			   left.chance == right.chance &&
			   left.sortOrder == right.sortOrder;
	}

	bool AreGroundBorderCaseConditionRecordsEqual(const GroundBorderCaseConditionRecord &left, const GroundBorderCaseConditionRecord &right) {
		return left.conditionType == right.conditionType &&
			   left.matchValue == right.matchValue &&
			   left.edge == right.edge &&
			   left.sortOrder == right.sortOrder;
	}

	bool AreGroundBorderCaseActionRecordsEqual(const GroundBorderCaseActionRecord &left, const GroundBorderCaseActionRecord &right) {
		return left.actionType == right.actionType &&
			   left.targetValue == right.targetValue &&
			   left.edge == right.edge &&
			   left.replacementValue == right.replacementValue &&
			   left.sortOrder == right.sortOrder;
	}

	bool AreGroundBorderCaseRecordsEqual(const GroundBorderCaseRecord &left, const GroundBorderCaseRecord &right) {
		return left.sortOrder == right.sortOrder &&
			   VectorsEqual(left.conditions, right.conditions, AreGroundBorderCaseConditionRecordsEqual) &&
			   VectorsEqual(left.actions, right.actions, AreGroundBorderCaseActionRecordsEqual);
	}

	bool AreGroundBrushBorderRecordsEqual(const GroundBrushBorderRecord &left, const GroundBrushBorderRecord &right) {
		return left.borderSetId == right.borderSetId &&
			   left.borderRole == right.borderRole &&
			   left.align == right.align &&
			   left.targetMode == right.targetMode &&
			   left.targetBrushId == right.targetBrushId &&
			   left.targetBrushName == right.targetBrushName &&
			   left.superBorder == right.superBorder &&
			   left.sortOrder == right.sortOrder &&
			   VectorsEqual(left.cases, right.cases, AreGroundBorderCaseRecordsEqual);
	}

	bool AreBrushLinkRecordsEqual(const BrushLinkRecord &left, const BrushLinkRecord &right) {
		return left.brushId == right.brushId &&
			   left.targetBrushId == right.targetBrushId &&
			   left.targetBrushName == right.targetBrushName &&
			   left.relationType == right.relationType &&
			   left.sortOrder == right.sortOrder;
	}

	bool AreWallPartItemRecordsEqual(const WallPartItemRecord &left, const WallPartItemRecord &right) {
		return left.itemId == right.itemId &&
			   left.chance == right.chance &&
			   left.sortOrder == right.sortOrder;
	}

	bool AreWallPartDoorRecordsEqual(const WallPartDoorRecord &left, const WallPartDoorRecord &right) {
		return left.itemId == right.itemId &&
			   left.doorType == right.doorType &&
			   left.isOpen == right.isOpen &&
			   left.wallHateMe == right.wallHateMe &&
			   left.sortOrder == right.sortOrder;
	}

	bool AreWallPartRecordsEqual(const WallPartRecord &left, const WallPartRecord &right) {
		return left.partType == right.partType &&
			   left.sortOrder == right.sortOrder &&
			   VectorsEqual(left.items, right.items, AreWallPartItemRecordsEqual) &&
			   VectorsEqual(left.doors, right.doors, AreWallPartDoorRecordsEqual);
	}

	bool AreCarpetNodeItemRecordsEqual(const CarpetNodeItemRecord &left, const CarpetNodeItemRecord &right) {
		return left.itemId == right.itemId &&
			   left.chance == right.chance &&
			   left.sortOrder == right.sortOrder;
	}

	bool AreCarpetNodeRecordsEqual(const CarpetNodeRecord &left, const CarpetNodeRecord &right) {
		return left.align == right.align &&
			   left.sortOrder == right.sortOrder &&
			   VectorsEqual(left.items, right.items, AreCarpetNodeItemRecordsEqual);
	}

	bool AreTableNodeItemRecordsEqual(const TableNodeItemRecord &left, const TableNodeItemRecord &right) {
		return left.itemId == right.itemId &&
			   left.chance == right.chance &&
			   left.sortOrder == right.sortOrder;
	}

	bool AreTableNodeRecordsEqual(const TableNodeRecord &left, const TableNodeRecord &right) {
		return left.align == right.align &&
			   left.sortOrder == right.sortOrder &&
			   VectorsEqual(left.items, right.items, AreTableNodeItemRecordsEqual);
	}

	bool AreDoodadSingleItemRecordsEqual(const DoodadSingleItemRecord &left, const DoodadSingleItemRecord &right) {
		return left.itemId == right.itemId &&
			   left.chance == right.chance &&
			   left.sortOrder == right.sortOrder;
	}

	bool AreDoodadCompositeTileItemRecordsEqual(const DoodadCompositeTileItemRecord &left, const DoodadCompositeTileItemRecord &right) {
		return left.itemId == right.itemId &&
			   left.sortOrder == right.sortOrder;
	}

	bool AreDoodadCompositeTileRecordsEqual(const DoodadCompositeTileRecord &left, const DoodadCompositeTileRecord &right) {
		return left.offsetX == right.offsetX &&
			   left.offsetY == right.offsetY &&
			   left.offsetZ == right.offsetZ &&
			   left.sortOrder == right.sortOrder &&
			   VectorsEqual(left.items, right.items, AreDoodadCompositeTileItemRecordsEqual);
	}

	bool AreDoodadCompositeRecordsEqual(const DoodadCompositeRecord &left, const DoodadCompositeRecord &right) {
		return left.chance == right.chance &&
			   left.sortOrder == right.sortOrder &&
			   VectorsEqual(left.tiles, right.tiles, AreDoodadCompositeTileRecordsEqual);
	}

	bool AreDoodadAlternativeRecordsEqual(const DoodadAlternativeRecord &left, const DoodadAlternativeRecord &right) {
		return left.sortOrder == right.sortOrder &&
			   VectorsEqual(left.singleItems, right.singleItems, AreDoodadSingleItemRecordsEqual) &&
			   VectorsEqual(left.composites, right.composites, AreDoodadCompositeRecordsEqual);
	}

	bool AreBrushStorageRecordsEqual(const BrushStorageRecord &left, const BrushStorageRecord &right) {
		return AreBrushRecordsEqual(left.brush, right.brush) &&
			   VectorsEqual(left.items, right.items, AreBrushItemRecordsEqual) &&
			   VectorsEqual(left.borders, right.borders, AreGroundBrushBorderRecordsEqual) &&
			   VectorsEqual(left.links, right.links, AreBrushLinkRecordsEqual) &&
			   VectorsEqual(left.wallParts, right.wallParts, AreWallPartRecordsEqual) &&
			   VectorsEqual(left.carpetNodes, right.carpetNodes, AreCarpetNodeRecordsEqual) &&
			   VectorsEqual(left.tableNodes, right.tableNodes, AreTableNodeRecordsEqual) &&
			   VectorsEqual(left.doodadAlternatives, right.doodadAlternatives, AreDoodadAlternativeRecordsEqual);
	}

	wxColour GetModifiedFieldColour() {
		return wxColour(255, 248, 214);
	}

	void ApplyModifiedEditorStyle(wxWindow* window, bool modified) {
		if (!window) {
			return;
		}

		wxFont font = window->GetFont();
		font.SetWeight(modified ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
		window->SetFont(font);
		window->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
		window->SetForegroundColour(modified ? wxColour(176, 102, 0) : wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
		window->Refresh();
	}

	void ApplyModifiedToggleStyle(wxCheckBox* checkBox, bool modified) {
		if (!checkBox) {
			return;
		}

		wxFont font = checkBox->GetFont();
		font.SetWeight(modified ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
		checkBox->SetFont(font);
		checkBox->Refresh();
	}

	void ApplyModifiedLabelStyle(wxStaticText* label, const wxString &baseLabel, bool modified) {
		if (!label) {
			return;
		}

		wxFont font = label->GetFont();
		font.SetWeight(modified ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
		label->SetFont(font);
		label->SetLabel(modified ? baseLabel + " [modified]" : baseLabel);
		label->SetForegroundColour(modified ? wxColour(176, 102, 0) : wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
		label->Refresh();
	}
} // namespace

MaterialsWorkbenchBrushPanel::MaterialsWorkbenchBrushPanel(wxWindow* parent, MaterialsWorkbenchController &controller) :
	wxPanel(parent, wxID_ANY),
	controller_(controller) {
	BuildLayout();
	ClearWorkspace("Select a brush in the navigation tree to edit its properties.");
}

void MaterialsWorkbenchBrushPanel::SetOnBrushSaved(std::function<void(int64_t, const wxString&, const wxString&)> callback) {
	onBrushSaved_ = std::move(callback);
}

void MaterialsWorkbenchBrushPanel::SetOnBrushStateChanged(std::function<void()> callback) {
	onBrushStateChanged_ = std::move(callback);
}

bool MaterialsWorkbenchBrushPanel::HasPendingChanges() const {
	return hasBrush_ && dirty_;
}

bool MaterialsWorkbenchBrushPanel::IsCurrentBrushSelection(const wxString &contextKey, int itemIndex) const {
	return hasBrush_ && currentContextKey_ == contextKey && currentItemIndex_ == itemIndex;
}

wxString MaterialsWorkbenchBrushPanel::GetCurrentBrushDisplayName() const {
	if (!hasBrush_) {
		return "";
	}

	const wxString displayName = nameCtrl_ ? TrimmedValue(nameCtrl_) : "";
	return displayName.IsEmpty() ? brushStorage_.brush.name : displayName;
}

wxString MaterialsWorkbenchBrushPanel::GetCurrentBrushInspectorText() const {
	if (!hasBrush_) {
		return "Select a palette, brush, border set or wall entry in the navigation tree to inspect its SQLite-backed metadata.";
	}

	return FormatBrushInspectorText(BuildEditableStorageFromCurrentState());
}

bool MaterialsWorkbenchBrushPanel::ResolvePendingChangesBeforeSwitch(wxWindow* parent, const wxString &targetLabel) {
	if (!HasPendingChanges()) {
		return true;
	}

	const wxString destination = targetLabel.IsEmpty() ? "the selected entry" : "\"" + targetLabel + "\"";
	wxMessageDialog dialog(
		parent,
		"Brush \"" + brushStorage_.brush.name + "\" has unsaved changes.\n\n"
		"You are switching to " + destination + ".\n\n"
		"Yes: save and continue\n"
		"No: discard local changes and continue\n"
		"Cancel: stay on the current brush",
		"Unsaved Brush Changes",
		wxYES_NO | wxCANCEL | wxICON_WARNING
	);
	dialog.SetYesNoCancelLabels("Save", "Discard", "Cancel");

	switch (dialog.ShowModal()) {
	case wxID_YES:
		return SaveCurrentBrush();
	case wxID_NO:
		return LoadBrush(currentContextKey_, currentItemIndex_);
	default:
		SetStatusMessage("Selection change canceled. Pending brush edits were kept.");
		return false;
	}
}

void MaterialsWorkbenchBrushPanel::BuildLayout() {
	wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);

	wxStaticText* title = new wxStaticText(this, wxID_ANY, "Brush Workspace");
	wxFont titleFont = title->GetFont();
	titleFont.SetPointSize(titleFont.GetPointSize() + 4);
	titleFont.SetWeight(wxFONTWEIGHT_BOLD);
	title->SetFont(titleFont);

	titleLabel_ = new wxStaticText(this, wxID_ANY, "No brush selected");
	subtitleLabel_ = new wxStaticText(this, wxID_ANY, "Edit the SQLite-backed brush metadata and variations used by the runtime catalog.");
	StyleBrushWorkspaceSubtitle(subtitleLabel_);

	workspaceTabs_ = new wxNotebook(this, wxID_ANY);
	workspaceTabs_->AddPage(BuildMetadataPage(workspaceTabs_), "Metadata");
	workspaceTabs_->AddPage(BuildVariationsPage(workspaceTabs_), "Editor");

	wxBoxSizer* headerSizer = new wxBoxSizer(wxVERTICAL);
	headerSizer->Add(title, 0, wxBOTTOM, FromDIP(4));
	headerSizer->Add(titleLabel_, 0, wxBOTTOM, FromDIP(2));
	headerSizer->Add(subtitleLabel_, 0);

	wxBoxSizer* actionSizer = new wxBoxSizer(wxHORIZONTAL);
	saveButton_ = new wxButton(this, wxID_SAVE, "Save Brush");
	revertButton_ = new wxButton(this, wxID_ANY, "Revert");
	StyleBrushWorkspaceActionButton(saveButton_, "Write the current brush metadata and variations to materials.db.");
	StyleBrushWorkspaceActionButton(revertButton_, "Discard local brush edits and reload the current brush from materials.db.");
	actionSizer->Add(saveButton_, 0, wxRIGHT, FromDIP(6));
	actionSizer->Add(revertButton_, 0);

	statusLabel_ = new wxStaticText(this, wxID_ANY, "");
	StyleBrushWorkspaceStatusLabel(statusLabel_);
	wxBoxSizer* footerSizer = new wxBoxSizer(wxHORIZONTAL);
	footerSizer->Add(statusLabel_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
	footerSizer->Add(actionSizer, 0, wxALIGN_CENTER_VERTICAL);

	rootSizer->Add(headerSizer, 0, wxEXPAND | wxALL, FromDIP(8));
	rootSizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));
	rootSizer->Add(workspaceTabs_, 1, wxEXPAND | wxALL, FromDIP(8));
	rootSizer->Add(footerSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(2));
	SetSizer(rootSizer);

	saveButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnSave, this);
	revertButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnRevert, this);
}

wxPanel* MaterialsWorkbenchBrushPanel::BuildMetadataPage(wxNotebook* notebook) {
	wxScrolledWindow* scrolled = new wxScrolledWindow(notebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	metadataPage_ = scrolled;
	scrolled->SetScrollRate(FromDIP(10), FromDIP(10));

	wxBoxSizer* contentSizer = new wxBoxSizer(wxVERTICAL);
	summaryLabel_ = new wxStaticText(scrolled, wxID_ANY, "");

	contentSizer->Add(CreateSectionLabel(scrolled, "Identity"), 0, wxBOTTOM, FromDIP(6));

	wxFlexGridSizer* identityGrid = new wxFlexGridSizer(2, FromDIP(8), FromDIP(10));
	identityGrid->AddGrowableCol(1, 1);

	idCtrl_ = CreateTextField(scrolled, wxTE_READONLY);
	storageCtrl_ = CreateTextField(scrolled, wxTE_READONLY);
	nameCtrl_ = CreateTextField(scrolled);
	typeCtrl_ = CreateTextField(scrolled);
	sourceCtrl_ = CreateTextField(scrolled);

	identityGrid->Add(new wxStaticText(scrolled, wxID_ANY, "SQLite ID"), 0, wxALIGN_CENTER_VERTICAL);
	identityGrid->Add(idCtrl_, 1, wxEXPAND);
	identityGrid->Add(new wxStaticText(scrolled, wxID_ANY, "Storage"), 0, wxALIGN_CENTER_VERTICAL);
	identityGrid->Add(storageCtrl_, 1, wxEXPAND);
	identityGrid->Add(new wxStaticText(scrolled, wxID_ANY, "Name"), 0, wxALIGN_CENTER_VERTICAL);
	identityGrid->Add(nameCtrl_, 1, wxEXPAND);
	identityGrid->Add(new wxStaticText(scrolled, wxID_ANY, "Type"), 0, wxALIGN_CENTER_VERTICAL);
	identityGrid->Add(typeCtrl_, 1, wxEXPAND);
	identityGrid->Add(new wxStaticText(scrolled, wxID_ANY, "Imported From"), 0, wxALIGN_CENTER_VERTICAL);
	identityGrid->Add(sourceCtrl_, 1, wxEXPAND);

	contentSizer->Add(identityGrid, 0, wxEXPAND | wxBOTTOM, FromDIP(12));
	contentSizer->Add(new wxStaticLine(scrolled), 0, wxEXPAND | wxBOTTOM, FromDIP(10));

	contentSizer->Add(CreateSectionLabel(scrolled, "Rendering And Placement"), 0, wxBOTTOM, FromDIP(6));

	wxFlexGridSizer* numericGrid = new wxFlexGridSizer(2, FromDIP(8), FromDIP(10));
	numericGrid->AddGrowableCol(1, 1);

	lookIdCtrl_ = CreateLookIdSpinField(scrolled);
	serverLookIdCtrl_ = CreateLookIdSpinField(scrolled);
	lookIdOwnershipLabel_ = new wxStaticText(scrolled, wxID_ANY, "Runtime owner: select a brush.");
	serverLookIdOwnershipLabel_ = new wxStaticText(scrolled, wxID_ANY, "Runtime owner: select a brush.");
	zOrderCtrl_ = CreateSpinField(scrolled, -1000000, 1000000);
	thicknessCtrl_ = CreateSpinField(scrolled, 0, 1000000);
	thicknessCeilingCtrl_ = CreateSpinField(scrolled, 0, 1000000);

	numericGrid->Add(new wxStaticText(scrolled, wxID_ANY, "lookId"), 0, wxALIGN_CENTER_VERTICAL);
	numericGrid->Add(lookIdCtrl_, 1, wxEXPAND);
	numericGrid->AddSpacer(0);
	numericGrid->Add(lookIdOwnershipLabel_, 1, wxEXPAND);
	numericGrid->Add(new wxStaticText(scrolled, wxID_ANY, "serverLookId"), 0, wxALIGN_CENTER_VERTICAL);
	numericGrid->Add(serverLookIdCtrl_, 1, wxEXPAND);
	numericGrid->AddSpacer(0);
	numericGrid->Add(serverLookIdOwnershipLabel_, 1, wxEXPAND);
	numericGrid->Add(new wxStaticText(scrolled, wxID_ANY, "zOrder"), 0, wxALIGN_CENTER_VERTICAL);
	numericGrid->Add(zOrderCtrl_, 1, wxEXPAND);
	numericGrid->Add(new wxStaticText(scrolled, wxID_ANY, "Thickness"), 0, wxALIGN_CENTER_VERTICAL);
	numericGrid->Add(thicknessCtrl_, 1, wxEXPAND);
	numericGrid->Add(new wxStaticText(scrolled, wxID_ANY, "Thickness Ceiling"), 0, wxALIGN_CENTER_VERTICAL);
	numericGrid->Add(thicknessCeilingCtrl_, 1, wxEXPAND);

	contentSizer->Add(numericGrid, 0, wxEXPAND | wxBOTTOM, FromDIP(12));
	contentSizer->Add(new wxStaticLine(scrolled), 0, wxEXPAND | wxBOTTOM, FromDIP(10));

	contentSizer->Add(CreateSectionLabel(scrolled, "Flags"), 0, wxBOTTOM, FromDIP(6));

	wxGridSizer* flagsGrid = new wxGridSizer(2, FromDIP(8), FromDIP(8));
	draggableCtrl_ = new wxCheckBox(scrolled, wxID_ANY, "Draggable");
	onBlockingCtrl_ = new wxCheckBox(scrolled, wxID_ANY, "On Blocking");
	onDuplicateCtrl_ = new wxCheckBox(scrolled, wxID_ANY, "On Duplicate");
	redoBordersCtrl_ = new wxCheckBox(scrolled, wxID_ANY, "Redo Borders");
	randomizeCtrl_ = new wxCheckBox(scrolled, wxID_ANY, "Randomize");
	oneSizeCtrl_ = new wxCheckBox(scrolled, wxID_ANY, "One Size");
	soloOptionalCtrl_ = new wxCheckBox(scrolled, wxID_ANY, "Solo Optional");

	flagsGrid->Add(draggableCtrl_, 0, wxEXPAND);
	flagsGrid->Add(onBlockingCtrl_, 0, wxEXPAND);
	flagsGrid->Add(onDuplicateCtrl_, 0, wxEXPAND);
	flagsGrid->Add(redoBordersCtrl_, 0, wxEXPAND);
	flagsGrid->Add(randomizeCtrl_, 0, wxEXPAND);
	flagsGrid->Add(oneSizeCtrl_, 0, wxEXPAND);
	flagsGrid->Add(soloOptionalCtrl_, 0, wxEXPAND);
	flagsGrid->AddSpacer(0);

	contentSizer->Add(flagsGrid, 0, wxEXPAND | wxBOTTOM, FromDIP(12));
	contentSizer->Add(new wxStaticLine(scrolled), 0, wxEXPAND | wxBOTTOM, FromDIP(10));

	contentSizer->Add(CreateSectionLabel(scrolled, "Stored Brush Data"), 0, wxBOTTOM, FromDIP(6));
	contentSizer->Add(summaryLabel_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

	scrolled->SetSizer(contentSizer);

	nameCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	typeCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	sourceCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	lookIdCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	lookIdCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	serverLookIdCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	serverLookIdCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	zOrderCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	zOrderCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	thicknessCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	thicknessCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	thicknessCeilingCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	thicknessCeilingCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	draggableCtrl_->Bind(wxEVT_CHECKBOX, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	onBlockingCtrl_->Bind(wxEVT_CHECKBOX, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	onDuplicateCtrl_->Bind(wxEVT_CHECKBOX, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	redoBordersCtrl_->Bind(wxEVT_CHECKBOX, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	randomizeCtrl_->Bind(wxEVT_CHECKBOX, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	oneSizeCtrl_->Bind(wxEVT_CHECKBOX, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	soloOptionalCtrl_->Bind(wxEVT_CHECKBOX, &MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged, this);
	return scrolled;
}

wxPanel* MaterialsWorkbenchBrushPanel::BuildVariationsPage(wxNotebook* notebook) {
	wxPanel* panel = new wxPanel(notebook, wxID_ANY);
	wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);

	rootSizer->Add(
		new wxStaticText(
			panel,
			wxID_ANY,
			"Brush variations are stored in materials.db and edited per domain: weighted items for grounds, aligned nodes for carpet/table, alternatives for doodads."
		),
		0,
		wxEXPAND | wxBOTTOM,
		FromDIP(8)
	);

	variationsStatusLabel_ = new wxStaticText(panel, wxID_ANY, "Variation Data");
	rootSizer->Add(variationsStatusLabel_, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

	variationsBook_ = new wxSimplebook(panel, wxID_ANY);
	variationsBook_->AddPage(BuildUnsupportedVariationsPage(variationsBook_), "Unsupported");
	variationsBook_->AddPage(BuildGroundVariationsPage(variationsBook_), "Ground");
	variationsBook_->AddPage(BuildCarpetVariationsPage(variationsBook_), "Carpet");
	variationsBook_->AddPage(BuildTableVariationsPage(variationsBook_), "Table");
	variationsBook_->AddPage(BuildDoodadVariationsPage(variationsBook_), "Doodad");

	rootSizer->Add(variationsBook_, 1, wxEXPAND);
	panel->SetSizer(rootSizer);
	SetActiveAlignedEditorWidgets(nullptr);
	return panel;
}

void MaterialsWorkbenchBrushPanel::SetActiveAlignedEditorWidgets(AlignedEditorWidgets* widgets) {
	alignedSectionLabel_ = widgets ? widgets->sectionLabel : nullptr;
	alignedVisualInfoLabel_ = widgets ? widgets->visualInfoLabel : nullptr;
	alignedContextPanel_ = widgets ? widgets->contextPanel : nullptr;
	alignedAddNodeButton_ = widgets ? widgets->addNodeButton : nullptr;
	alignedAddItemButton_ = widgets ? widgets->addItemButton : nullptr;
	alignedRemoveItemButton_ = widgets ? widgets->removeItemButton : nullptr;
	alignedNodesList_ = widgets ? widgets->nodesList : nullptr;
	alignedSeamlessPreviewInfoLabel_ = widgets ? widgets->seamlessPreviewInfoLabel : nullptr;
	alignedSeamlessPreviewPanel_ = widgets ? widgets->seamlessPreviewPanel : nullptr;
	alignedItemsSummaryLabel_ = widgets ? widgets->itemsSummaryLabel : nullptr;
	alignedItemsScroll_ = widgets ? widgets->itemsScroll : nullptr;
	alignedItemsCardsPanel_ = widgets ? widgets->itemsCardsPanel : nullptr;
	alignedAdvancedPanel_ = widgets ? widgets->advancedPanel : nullptr;
	alignedNodeAlignCtrl_ = widgets ? widgets->nodeAlignCtrl : nullptr;
	alignedNodeAlignChoice_ = widgets ? widgets->nodeAlignChoice : nullptr;
	alignedAdvancedInfoLabel_ = widgets ? widgets->advancedInfoLabel : nullptr;
	alignedItemsList_ = widgets ? widgets->itemsList : nullptr;
	alignedItemIdCtrl_ = widgets ? widgets->itemIdCtrl : nullptr;
	alignedItemChanceCtrl_ = widgets ? widgets->itemChanceCtrl : nullptr;
	alignedItemOwnershipLabel_ = widgets ? widgets->itemOwnershipLabel : nullptr;
}

wxPanel* MaterialsWorkbenchBrushPanel::BuildUnsupportedVariationsPage(wxSimplebook* book) {
	wxPanel* panel = new wxPanel(book, wxID_ANY);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	variationsEmptyLabel_ = new wxStaticText(panel, wxID_ANY, "Select a brush to inspect variation data.");
	sizer->AddStretchSpacer();
	sizer->Add(variationsEmptyLabel_, 0, wxALIGN_CENTER | wxALL, FromDIP(12));
	sizer->AddStretchSpacer();
	panel->SetSizer(sizer);
	return panel;
}

wxPanel* MaterialsWorkbenchBrushPanel::BuildGroundVariationsPage(wxSimplebook* book) {
	wxPanel* panel = new wxPanel(book, wxID_ANY);
	wxBoxSizer* rootSizer = new wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* listSizer = new wxBoxSizer(wxVERTICAL);
	listSizer->Add(CreateSectionLabel(panel, "Ground Variants"), 0, wxBOTTOM, FromDIP(6));
	groundItemsScroll_ = new wxScrolledWindow(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_SIMPLE);
	groundItemsScroll_->SetScrollRate(0, panel->FromDIP(16));
	groundItemsCardsPanel_ = new wxPanel(groundItemsScroll_, wxID_ANY);
	groundItemsCardsPanel_->SetBackgroundStyle(wxBG_STYLE_PAINT);
	wxBoxSizer* cardsSizer = new wxBoxSizer(wxVERTICAL);
	cardsSizer->Add(groundItemsCardsPanel_, 1, wxEXPAND);
	groundItemsScroll_->SetSizer(cardsSizer);
	groundItemsCardsPanel_->SetMinSize(wxSize(panel->FromDIP(300), panel->FromDIP(220)));
	listSizer->Add(groundItemsScroll_, 1, wxEXPAND | wxBOTTOM, FromDIP(6));
	wxBoxSizer* buttonsSizer = new wxBoxSizer(wxHORIZONTAL);
	wxButton* addButton = new wxButton(panel, wxID_ANY, "Add Item");
	wxButton* removeButton = new wxButton(panel, wxID_ANY, "Remove");
	buttonsSizer->Add(addButton, 1, wxRIGHT, FromDIP(4));
	buttonsSizer->Add(removeButton, 1);
	listSizer->Add(buttonsSizer, 0, wxEXPAND);
	groundItemsList_ = new wxListBox(panel, wxID_ANY);
	groundItemsList_->Hide();
	listSizer->Add(groundItemsList_, 0, wxEXPAND);
	groundItemIdCtrl_ = CreateItemIdSpinField(panel);
	groundItemChanceCtrl_ = CreateSpinField(panel, 0, 1000000);
	groundItemOwnershipLabel_ = new wxStaticText(panel, wxID_ANY, "Runtime owner: select an item entry.");
	groundItemIdCtrl_->Hide();
	groundItemChanceCtrl_->Hide();
	groundItemOwnershipLabel_->Hide();
	listSizer->Add(groundItemIdCtrl_, 0, wxEXPAND);
	listSizer->Add(groundItemChanceCtrl_, 0, wxEXPAND);
	listSizer->Add(groundItemOwnershipLabel_, 0, wxEXPAND);

	wxBoxSizer* editorSizer = new wxBoxSizer(wxVERTICAL);
	editorSizer->Add(CreateSectionLabel(panel, "Variation Preview"), 0, wxBOTTOM, FromDIP(6));
	groundPreviewInfoLabel_ = new wxStaticText(
		panel,
		wxID_ANY,
		"Ground variants now use visual cards with weighted badges. The preview shows the current variation set together in a compact visual grid."
	);
	StyleBrushWorkspaceSubtitle(groundPreviewInfoLabel_);
	groundPreviewInfoLabel_->Wrap(panel->FromDIP(520));
	editorSizer->Add(groundPreviewInfoLabel_, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
	groundPreviewScroll_ = new wxScrolledWindow(panel, wxID_ANY, wxDefaultPosition, wxSize(panel->FromDIP(420), panel->FromDIP(250)), wxVSCROLL | wxBORDER_SIMPLE);
	groundPreviewScroll_->SetScrollRate(0, panel->FromDIP(16));
	groundPreviewPanel_ = new wxPanel(groundPreviewScroll_, wxID_ANY);
	groundPreviewPanel_->SetBackgroundStyle(wxBG_STYLE_PAINT);
	groundPreviewPanel_->SetMinSize(wxSize(panel->FromDIP(420), panel->FromDIP(250)));
	wxBoxSizer* previewCanvasSizer = new wxBoxSizer(wxVERTICAL);
	previewCanvasSizer->Add(groundPreviewPanel_, 1, wxEXPAND);
	groundPreviewScroll_->SetSizer(previewCanvasSizer);
	editorSizer->Add(groundPreviewScroll_, 1, wxEXPAND | wxBOTTOM, FromDIP(8));
	groundPreviewHighlightCtrl_ = new wxCheckBox(panel, wxID_ANY, "Highlight rarity");
	groundPreviewDistributionLabel_ = new wxStaticText(panel, wxID_ANY, "Distribution: no weighted variants yet.");
	StyleBrushWorkspaceSubtitle(groundPreviewDistributionLabel_);
	wxBoxSizer* previewFooterSizer = new wxBoxSizer(wxHORIZONTAL);
	previewFooterSizer->Add(groundPreviewHighlightCtrl_, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(10));
	previewFooterSizer->Add(groundPreviewDistributionLabel_, 1, wxALIGN_CENTER_VERTICAL);
	editorSizer->Add(previewFooterSizer, 0, wxEXPAND);

	rootSizer->Add(listSizer, 0, wxEXPAND | wxRIGHT, FromDIP(10));
	rootSizer->Add(new wxStaticLine(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL), 0, wxEXPAND | wxRIGHT, FromDIP(10));
	rootSizer->Add(editorSizer, 1, wxEXPAND);
	panel->SetSizer(rootSizer);

	addButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnAddGroundItem, this);
	removeButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnRemoveGroundItem, this);
	groundItemsList_->Bind(wxEVT_LISTBOX, &MaterialsWorkbenchBrushPanel::OnGroundItemSelected, this);
	groundItemsCardsPanel_->Bind(wxEVT_PAINT, &MaterialsWorkbenchBrushPanel::OnGroundCardsPaint, this);
	groundItemsCardsPanel_->Bind(wxEVT_LEFT_DOWN, &MaterialsWorkbenchBrushPanel::OnGroundCardsLeftDown, this);
	groundItemsCardsPanel_->Bind(wxEVT_RIGHT_DOWN, &MaterialsWorkbenchBrushPanel::OnGroundCardsRightDown, this);
	groundPreviewPanel_->Bind(wxEVT_PAINT, &MaterialsWorkbenchBrushPanel::OnGroundPreviewPaint, this);
	groundPreviewPanel_->Bind(wxEVT_LEFT_DOWN, &MaterialsWorkbenchBrushPanel::OnGroundPreviewLeftDown, this);
	groundPreviewPanel_->Bind(wxEVT_MOTION, &MaterialsWorkbenchBrushPanel::OnGroundPreviewMotion, this);
	groundPreviewPanel_->Bind(wxEVT_LEAVE_WINDOW, &MaterialsWorkbenchBrushPanel::OnGroundPreviewMouseLeave, this);
	groundPreviewScroll_->Bind(wxEVT_SIZE, &MaterialsWorkbenchBrushPanel::OnGroundPreviewSize, this);
	groundPreviewHighlightCtrl_->Bind(wxEVT_CHECKBOX, &MaterialsWorkbenchBrushPanel::OnGroundPreviewHighlightToggled, this);
	groundItemIdCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnGroundItemValueChanged, this);
	groundItemChanceCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnGroundItemValueChanged, this);
	groundItemIdCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnGroundItemValueChanged, this);
	groundItemChanceCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnGroundItemValueChanged, this);
	return panel;
}

wxPanel* MaterialsWorkbenchBrushPanel::BuildCarpetVariationsPage(wxSimplebook* book) {
	wxPanel* panel = new wxPanel(book, wxID_ANY);
	wxBoxSizer* rootSizer = new wxBoxSizer(wxHORIZONTAL);
	const int alignedEditorWidth = panel->FromDIP(560);
	AlignedEditorWidgets &widgets = carpetAlignedWidgets_;
	widgets.page = panel;

	wxBoxSizer* nodesSizer = new wxBoxSizer(wxVERTICAL);
	widgets.sectionLabel = CreateSectionLabel(panel, "Carpet Layout Map");
	widgets.visualInfoLabel = new wxStaticText(
		panel,
		wxID_ANY,
		"Click the layout map to inspect the center, edge, or corner context. Empty slots stay visible so missing carpet coverage is obvious."
	);
	StyleBrushWorkspaceSubtitle(widgets.visualInfoLabel);
	widgets.visualInfoLabel->Wrap(panel->FromDIP(250));
	widgets.contextPanel = new wxPanel(panel, wxID_ANY, wxDefaultPosition, wxSize(panel->FromDIP(250), panel->FromDIP(250)), wxBORDER_SIMPLE);
	widgets.contextPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
	widgets.contextPanel->SetMinSize(wxSize(panel->FromDIP(250), panel->FromDIP(250)));
	widgets.nodesList = new wxListBox(panel, wxID_ANY);
	widgets.nodesList->Hide();
	wxBoxSizer* nodeButtons = new wxBoxSizer(wxHORIZONTAL);
	widgets.addNodeButton = new wxButton(panel, wxID_ANY, "Add Context");
	wxButton* removeNodeButton = new wxButton(panel, wxID_ANY, "Remove Context");
	nodeButtons->Add(widgets.addNodeButton, 1, wxRIGHT, FromDIP(4));
	nodeButtons->Add(removeNodeButton, 1);
	nodesSizer->Add(widgets.sectionLabel, 0, wxBOTTOM, FromDIP(6));
	nodesSizer->Add(widgets.visualInfoLabel, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
	nodesSizer->Add(widgets.contextPanel, 1, wxEXPAND | wxBOTTOM, FromDIP(8));
	nodesSizer->Add(nodeButtons, 0, wxEXPAND);
	nodesSizer->Add(widgets.nodesList, 0, wxEXPAND);

	wxPanel* editorPanel = new wxPanel(panel, wxID_ANY);
	wxBoxSizer* editorFrameSizer = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* editorSizer = new wxBoxSizer(wxVERTICAL);
	widgets.seamlessPreviewInfoLabel = new wxStaticText(editorPanel, wxID_ANY, "");
	StyleBrushWorkspaceSubtitle(widgets.seamlessPreviewInfoLabel);
	editorSizer->Add(widgets.seamlessPreviewInfoLabel, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
	editorSizer->Add(CreateSectionLabel(editorPanel, "Seamless Preview"), 0, wxBOTTOM, FromDIP(6));
	widgets.seamlessPreviewPanel = new wxPanel(editorPanel, wxID_ANY, wxDefaultPosition, wxSize(alignedEditorWidth, editorPanel->FromDIP(188)), wxBORDER_SIMPLE);
	widgets.seamlessPreviewPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
	widgets.seamlessPreviewPanel->SetMinSize(wxSize(alignedEditorWidth, editorPanel->FromDIP(188)));
	editorSizer->Add(widgets.seamlessPreviewPanel, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
	editorSizer->Add(CreateSectionLabel(editorPanel, "Context Variants"), 0, wxBOTTOM, FromDIP(6));
	widgets.itemsSummaryLabel = new wxStaticText(
		editorPanel,
		wxID_ANY,
		"Select a context in the layout map to inspect its weighted carpet variants and tune the selected card below."
	);
	StyleBrushWorkspaceSubtitle(widgets.itemsSummaryLabel);
	widgets.itemsSummaryLabel->Wrap(alignedEditorWidth);
	editorSizer->Add(widgets.itemsSummaryLabel, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
	widgets.itemsScroll = new wxScrolledWindow(editorPanel, wxID_ANY, wxDefaultPosition, wxSize(alignedEditorWidth, editorPanel->FromDIP(220)), wxVSCROLL | wxBORDER_SIMPLE);
	widgets.itemsScroll->SetScrollRate(0, editorPanel->FromDIP(16));
	widgets.itemsCardsPanel = new wxPanel(widgets.itemsScroll, wxID_ANY);
	widgets.itemsCardsPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
	wxBoxSizer* alignedCardsSizer = new wxBoxSizer(wxVERTICAL);
	alignedCardsSizer->Add(widgets.itemsCardsPanel, 1, wxEXPAND);
	widgets.itemsScroll->SetSizer(alignedCardsSizer);
	widgets.itemsCardsPanel->SetMinSize(wxSize(alignedEditorWidth, editorPanel->FromDIP(180)));
	editorSizer->Add(widgets.itemsScroll, 1, wxEXPAND | wxBOTTOM, FromDIP(8));

	widgets.itemsList = new wxListBox(editorPanel, wxID_ANY);
	widgets.itemsList->Hide();

	wxBoxSizer* itemButtons = new wxBoxSizer(wxHORIZONTAL);
	wxButton* addItemButton = new wxButton(editorPanel, wxID_ANY, "Add Variant");
	wxButton* removeItemButton = new wxButton(editorPanel, wxID_ANY, "Remove Variant");
	itemButtons->Add(addItemButton, 1, wxRIGHT, FromDIP(4));
	itemButtons->Add(removeItemButton, 1);
	editorSizer->Add(itemButtons, 0, wxEXPAND | wxBOTTOM, FromDIP(10));

	editorSizer->Add(CreateSectionLabel(editorPanel, "Context Details"), 0, wxBOTTOM, FromDIP(6));
	widgets.advancedInfoLabel = new wxStaticText(
		editorPanel,
		wxID_ANY,
		"The layout map chooses the slot. Use the fields below to confirm the selected context and tune the active variant."
	);
	StyleBrushWorkspaceSubtitle(widgets.advancedInfoLabel);
	widgets.advancedInfoLabel->Wrap(alignedEditorWidth);
	editorSizer->Add(widgets.advancedInfoLabel, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
	wxFlexGridSizer* nodeForm = new wxFlexGridSizer(2, FromDIP(8), FromDIP(8));
	nodeForm->AddGrowableCol(1, 1);
	widgets.nodeAlignCtrl = CreateTextField(editorPanel);
	widgets.nodeAlignCtrl->SetEditable(false);
	widgets.nodeAlignCtrl->SetToolTip("The layout map controls the selected carpet slot.");
	widgets.nodeAlignChoice = new wxChoice(editorPanel, wxID_ANY);
	widgets.nodeAlignChoice->Hide();
	nodeForm->Add(new wxStaticText(editorPanel, wxID_ANY, "Map Slot"), 0, wxALIGN_CENTER_VERTICAL);
	nodeForm->Add(widgets.nodeAlignCtrl, 1, wxEXPAND);
	nodeForm->AddSpacer(0);
	nodeForm->Add(widgets.nodeAlignChoice, 1, wxEXPAND);
	editorSizer->Add(nodeForm, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
	editorSizer->Add(widgets.itemsList, 0, wxEXPAND);

	wxFlexGridSizer* itemForm = new wxFlexGridSizer(2, FromDIP(8), FromDIP(8));
	itemForm->AddGrowableCol(1, 1);
	widgets.itemIdCtrl = CreateItemIdSpinField(editorPanel);
	widgets.itemChanceCtrl = CreateSpinField(editorPanel, 0, 1000000);
	widgets.itemOwnershipLabel = new wxStaticText(editorPanel, wxID_ANY, "Runtime owner: select an item entry.");
	itemForm->Add(new wxStaticText(editorPanel, wxID_ANY, "Item ID"), 0, wxALIGN_CENTER_VERTICAL);
	itemForm->Add(widgets.itemIdCtrl, 1, wxEXPAND);
	itemForm->AddSpacer(0);
	itemForm->Add(widgets.itemOwnershipLabel, 1, wxEXPAND);
	itemForm->Add(new wxStaticText(editorPanel, wxID_ANY, "Chance"), 0, wxALIGN_CENTER_VERTICAL);
	itemForm->Add(widgets.itemChanceCtrl, 1, wxEXPAND);
	editorSizer->Add(itemForm, 0, wxEXPAND);
	editorFrameSizer->AddStretchSpacer();
	editorFrameSizer->Add(editorSizer, 0, wxEXPAND);
	editorFrameSizer->AddStretchSpacer();
	editorPanel->SetSizer(editorFrameSizer);

	rootSizer->Add(nodesSizer, 0, wxEXPAND | wxRIGHT, FromDIP(10));
	rootSizer->Add(new wxStaticLine(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL), 0, wxEXPAND | wxRIGHT, FromDIP(10));
	rootSizer->Add(editorPanel, 1, wxEXPAND);
	panel->SetSizer(rootSizer);

	widgets.addNodeButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnAddAlignedNode, this);
	removeNodeButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnRemoveAlignedNode, this);
	widgets.nodesList->Bind(wxEVT_LISTBOX, &MaterialsWorkbenchBrushPanel::OnAlignedNodeSelected, this);
	widgets.nodeAlignCtrl->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnAlignedNodeAlignChanged, this);
	widgets.nodeAlignChoice->Bind(wxEVT_CHOICE, &MaterialsWorkbenchBrushPanel::OnAlignedNodeAlignChanged, this);
	widgets.contextPanel->Bind(wxEVT_PAINT, &MaterialsWorkbenchBrushPanel::OnAlignedContextPaint, this);
	widgets.contextPanel->Bind(wxEVT_LEFT_DOWN, &MaterialsWorkbenchBrushPanel::OnAlignedContextLeftDown, this);
	widgets.contextPanel->Bind(wxEVT_MOTION, &MaterialsWorkbenchBrushPanel::OnAlignedContextMotion, this);
	widgets.contextPanel->Bind(wxEVT_LEAVE_WINDOW, &MaterialsWorkbenchBrushPanel::OnAlignedContextMouseLeave, this);
	widgets.seamlessPreviewPanel->Bind(wxEVT_PAINT, &MaterialsWorkbenchBrushPanel::OnAlignedSeamlessPreviewPaint, this);
	addItemButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnAddAlignedItem, this);
	removeItemButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnRemoveAlignedItem, this);
	widgets.itemsList->Bind(wxEVT_LISTBOX, &MaterialsWorkbenchBrushPanel::OnAlignedItemSelected, this);
	widgets.itemsCardsPanel->Bind(wxEVT_PAINT, &MaterialsWorkbenchBrushPanel::OnAlignedItemsCardsPaint, this);
	widgets.itemsCardsPanel->Bind(wxEVT_LEFT_DOWN, &MaterialsWorkbenchBrushPanel::OnAlignedItemsCardsLeftDown, this);
	widgets.itemsCardsPanel->Bind(wxEVT_RIGHT_DOWN, &MaterialsWorkbenchBrushPanel::OnAlignedItemsCardsRightDown, this);
	widgets.itemIdCtrl->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnAlignedItemValueChanged, this);
	widgets.itemChanceCtrl->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnAlignedItemValueChanged, this);
	widgets.itemIdCtrl->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnAlignedItemValueChanged, this);
	widgets.itemChanceCtrl->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnAlignedItemValueChanged, this);
	return panel;
}

wxPanel* MaterialsWorkbenchBrushPanel::BuildTableVariationsPage(wxSimplebook* book) {
	wxPanel* panel = new wxPanel(book, wxID_ANY);
	wxBoxSizer* rootSizer = new wxBoxSizer(wxHORIZONTAL);
	const int alignedEditorWidth = panel->FromDIP(560);
	AlignedEditorWidgets &widgets = tableAlignedWidgets_;
	widgets.page = panel;

	wxBoxSizer* nodesSizer = new wxBoxSizer(wxVERTICAL);
	widgets.sectionLabel = CreateSectionLabel(panel, "Table States");
	widgets.visualInfoLabel = new wxStaticText(
		panel,
		wxID_ANY,
		"Select a state to inspect it. Use Add Node for missing states and + inside a state to add items."
	);
	StyleBrushWorkspaceSubtitle(widgets.visualInfoLabel);
	widgets.visualInfoLabel->Wrap(panel->FromDIP(250));
	widgets.contextPanel = new wxPanel(panel, wxID_ANY, wxDefaultPosition, wxSize(panel->FromDIP(250), panel->FromDIP(250)), wxBORDER_SIMPLE);
	widgets.contextPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
	widgets.contextPanel->SetMinSize(wxSize(panel->FromDIP(250), panel->FromDIP(250)));
	widgets.nodesList = new wxListBox(panel, wxID_ANY);
	widgets.nodesList->Hide();
	wxBoxSizer* nodeButtons = new wxBoxSizer(wxHORIZONTAL);
	widgets.addNodeButton = new wxButton(panel, wxID_ANY, "Add Node");
	wxButton* removeNodeButton = new wxButton(panel, wxID_ANY, "Remove");
	nodeButtons->Add(widgets.addNodeButton, 1, wxRIGHT, FromDIP(4));
	nodeButtons->Add(removeNodeButton, 1);
	nodesSizer->Add(widgets.sectionLabel, 0, wxBOTTOM, FromDIP(6));
	nodesSizer->Add(widgets.visualInfoLabel, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
	nodesSizer->Add(widgets.contextPanel, 1, wxEXPAND | wxBOTTOM, FromDIP(8));
	nodesSizer->Add(nodeButtons, 0, wxEXPAND);
	nodesSizer->Add(widgets.nodesList, 0, wxEXPAND);

	wxPanel* editorPanel = new wxPanel(panel, wxID_ANY);
	wxBoxSizer* editorFrameSizer = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* editorSizer = new wxBoxSizer(wxVERTICAL);
	widgets.seamlessPreviewInfoLabel = new wxStaticText(editorPanel, wxID_ANY, "");
	StyleBrushWorkspaceSubtitle(widgets.seamlessPreviewInfoLabel);
	widgets.seamlessPreviewInfoLabel->Hide();
	editorSizer->Add(widgets.seamlessPreviewInfoLabel, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
	editorSizer->Add(CreateSectionLabel(editorPanel, "Seamless Preview"), 0, wxBOTTOM, FromDIP(6));
	widgets.seamlessPreviewPanel = new wxPanel(editorPanel, wxID_ANY, wxDefaultPosition, wxSize(alignedEditorWidth, editorPanel->FromDIP(156)), wxBORDER_SIMPLE);
	widgets.seamlessPreviewPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
	widgets.seamlessPreviewPanel->SetMinSize(wxSize(alignedEditorWidth, editorPanel->FromDIP(156)));
	editorSizer->Add(widgets.seamlessPreviewPanel, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
	editorSizer->Add(CreateSectionLabel(editorPanel, "State Items"), 0, wxBOTTOM, FromDIP(6));
	widgets.itemsSummaryLabel = new wxStaticText(
		editorPanel,
		wxID_ANY,
		"Select a state to inspect and manage its weighted items."
	);
	StyleBrushWorkspaceSubtitle(widgets.itemsSummaryLabel);
	widgets.itemsSummaryLabel->Wrap(alignedEditorWidth);
	editorSizer->Add(widgets.itemsSummaryLabel, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
	widgets.itemsScroll = new wxScrolledWindow(editorPanel, wxID_ANY, wxDefaultPosition, wxSize(alignedEditorWidth, editorPanel->FromDIP(220)), wxVSCROLL | wxBORDER_SIMPLE);
	widgets.itemsScroll->SetScrollRate(0, editorPanel->FromDIP(16));
	widgets.itemsCardsPanel = new wxPanel(widgets.itemsScroll, wxID_ANY);
	widgets.itemsCardsPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
	wxBoxSizer* alignedCardsSizer = new wxBoxSizer(wxVERTICAL);
	alignedCardsSizer->Add(widgets.itemsCardsPanel, 1, wxEXPAND);
	widgets.itemsScroll->SetSizer(alignedCardsSizer);
	widgets.itemsCardsPanel->SetMinSize(wxSize(alignedEditorWidth, editorPanel->FromDIP(180)));
	editorSizer->Add(widgets.itemsScroll, 1, wxEXPAND | wxBOTTOM, FromDIP(8));

	wxBoxSizer* itemButtons = new wxBoxSizer(wxHORIZONTAL);
	widgets.addItemButton = new wxButton(editorPanel, wxID_ANY, "Add Item");
	widgets.addItemButton->Hide();
	widgets.removeItemButton = new wxButton(editorPanel, wxID_ANY, "Remove Item");
	itemButtons->Add(widgets.removeItemButton, 1);
	editorSizer->Add(itemButtons, 0, wxEXPAND | wxBOTTOM, FromDIP(10));

	widgets.advancedPanel = new wxPanel(editorPanel, wxID_ANY);
	wxBoxSizer* advancedSizer = new wxBoxSizer(wxVERTICAL);
	advancedSizer->Add(CreateSectionLabel(widgets.advancedPanel, "Advanced"), 0, wxBOTTOM, FromDIP(6));
	widgets.advancedInfoLabel = new wxStaticText(
		widgets.advancedPanel,
		wxID_ANY,
		"Choose a table state to edit it, or select an empty slot to place the next node precisely."
	);
	StyleBrushWorkspaceSubtitle(widgets.advancedInfoLabel);
	widgets.advancedInfoLabel->Wrap(alignedEditorWidth);
	advancedSizer->Add(widgets.advancedInfoLabel, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
	wxFlexGridSizer* nodeForm = new wxFlexGridSizer(2, FromDIP(8), FromDIP(8));
	nodeForm->AddGrowableCol(1, 1);
	widgets.nodeAlignCtrl = CreateTextField(widgets.advancedPanel);
	widgets.nodeAlignCtrl->Hide();
	widgets.nodeAlignChoice = new wxChoice(widgets.advancedPanel, wxID_ANY);
	for (const wxString &align : GetTableAlignChoices()) {
		widgets.nodeAlignChoice->Append(align);
	}
	nodeForm->Add(new wxStaticText(widgets.advancedPanel, wxID_ANY, "Align"), 0, wxALIGN_CENTER_VERTICAL);
	nodeForm->Add(widgets.nodeAlignCtrl, 1, wxEXPAND);
	nodeForm->AddSpacer(0);
	nodeForm->Add(widgets.nodeAlignChoice, 1, wxEXPAND);
	advancedSizer->Add(nodeForm, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
	widgets.itemsList = new wxListBox(widgets.advancedPanel, wxID_ANY);
	widgets.itemsList->Hide();
	advancedSizer->Add(widgets.itemsList, 0, wxEXPAND);

	wxFlexGridSizer* itemForm = new wxFlexGridSizer(2, FromDIP(8), FromDIP(8));
	itemForm->AddGrowableCol(1, 1);
	widgets.itemIdCtrl = CreateItemIdSpinField(widgets.advancedPanel);
	widgets.itemChanceCtrl = CreateSpinField(widgets.advancedPanel, 0, 1000000);
	widgets.itemOwnershipLabel = new wxStaticText(widgets.advancedPanel, wxID_ANY, "Runtime owner: select an item entry.");
	itemForm->Add(new wxStaticText(widgets.advancedPanel, wxID_ANY, "Item ID"), 0, wxALIGN_CENTER_VERTICAL);
	itemForm->Add(widgets.itemIdCtrl, 1, wxEXPAND);
	itemForm->AddSpacer(0);
	itemForm->Add(widgets.itemOwnershipLabel, 1, wxEXPAND);
	itemForm->Add(new wxStaticText(widgets.advancedPanel, wxID_ANY, "Chance"), 0, wxALIGN_CENTER_VERTICAL);
	itemForm->Add(widgets.itemChanceCtrl, 1, wxEXPAND);
	advancedSizer->Add(itemForm, 0, wxEXPAND);
	widgets.advancedPanel->SetSizer(advancedSizer);
	widgets.advancedPanel->Hide();
	editorSizer->Add(widgets.advancedPanel, 0, wxEXPAND);
	editorFrameSizer->AddStretchSpacer();
	editorFrameSizer->Add(editorSizer, 0, wxEXPAND);
	editorFrameSizer->AddStretchSpacer();
	editorPanel->SetSizer(editorFrameSizer);

	rootSizer->Add(nodesSizer, 0, wxEXPAND | wxRIGHT, FromDIP(10));
	rootSizer->Add(new wxStaticLine(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL), 0, wxEXPAND | wxRIGHT, FromDIP(10));
	rootSizer->Add(editorPanel, 1, wxEXPAND);
	panel->SetSizer(rootSizer);

	widgets.addNodeButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnAddAlignedNode, this);
	removeNodeButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnRemoveAlignedNode, this);
	widgets.nodesList->Bind(wxEVT_LISTBOX, &MaterialsWorkbenchBrushPanel::OnAlignedNodeSelected, this);
	widgets.nodeAlignCtrl->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnAlignedNodeAlignChanged, this);
	widgets.nodeAlignChoice->Bind(wxEVT_CHOICE, &MaterialsWorkbenchBrushPanel::OnAlignedNodeAlignChanged, this);
	widgets.contextPanel->Bind(wxEVT_PAINT, &MaterialsWorkbenchBrushPanel::OnAlignedContextPaint, this);
	widgets.contextPanel->Bind(wxEVT_LEFT_DOWN, &MaterialsWorkbenchBrushPanel::OnAlignedContextLeftDown, this);
	widgets.contextPanel->Bind(wxEVT_MOTION, &MaterialsWorkbenchBrushPanel::OnAlignedContextMotion, this);
	widgets.contextPanel->Bind(wxEVT_LEAVE_WINDOW, &MaterialsWorkbenchBrushPanel::OnAlignedContextMouseLeave, this);
	widgets.seamlessPreviewPanel->Bind(wxEVT_PAINT, &MaterialsWorkbenchBrushPanel::OnAlignedSeamlessPreviewPaint, this);
	widgets.addItemButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnAddAlignedItem, this);
	widgets.removeItemButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnRemoveAlignedItem, this);
	widgets.itemsList->Bind(wxEVT_LISTBOX, &MaterialsWorkbenchBrushPanel::OnAlignedItemSelected, this);
	widgets.itemsCardsPanel->Bind(wxEVT_PAINT, &MaterialsWorkbenchBrushPanel::OnAlignedItemsCardsPaint, this);
	widgets.itemsCardsPanel->Bind(wxEVT_LEFT_DOWN, &MaterialsWorkbenchBrushPanel::OnAlignedItemsCardsLeftDown, this);
	widgets.itemsCardsPanel->Bind(wxEVT_RIGHT_DOWN, &MaterialsWorkbenchBrushPanel::OnAlignedItemsCardsRightDown, this);
	widgets.itemIdCtrl->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnAlignedItemValueChanged, this);
	widgets.itemChanceCtrl->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnAlignedItemValueChanged, this);
	widgets.itemIdCtrl->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnAlignedItemValueChanged, this);
	widgets.itemChanceCtrl->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnAlignedItemValueChanged, this);
	return panel;
}

wxPanel* MaterialsWorkbenchBrushPanel::BuildDoodadVariationsPage(wxSimplebook* book) {
	wxPanel* panel = new wxPanel(book, wxID_ANY);
	wxBoxSizer* pageSizer = new wxBoxSizer(wxVERTICAL);
	wxScrolledWindow* scrolled = new wxScrolledWindow(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	scrolled->SetScrollRate(0, panel->FromDIP(16));
	pageSizer->Add(scrolled, 1, wxEXPAND);
	panel->SetSizer(pageSizer);

	wxBoxSizer* rootSizer = new wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* structureSizer = new wxBoxSizer(wxVERTICAL);
	structureSizer->Add(CreateSectionLabel(scrolled, "Single Items"), 0, wxBOTTOM, FromDIP(6));
	doodadSingleItemsList_ = new wxListBox(scrolled, wxID_ANY);
	doodadSingleItemsList_->SetMinSize(wxSize(scrolled->FromDIP(180), scrolled->FromDIP(96)));
	structureSizer->Add(doodadSingleItemsList_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
	wxBoxSizer* singleButtons = new wxBoxSizer(wxHORIZONTAL);
	wxButton* addSingleButton = new wxButton(scrolled, wxID_ANY, "Add Single");
	wxButton* removeSingleButton = new wxButton(scrolled, wxID_ANY, "Remove");
	singleButtons->Add(addSingleButton, 1, wxRIGHT, FromDIP(4));
	singleButtons->Add(removeSingleButton, 1);
	structureSizer->Add(singleButtons, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
	doodadSingleItemIdCtrl_ = CreateItemIdSpinField(scrolled);
	doodadSingleItemChanceCtrl_ = CreateSpinField(scrolled, 0, 1000000);
	doodadSingleItemOwnershipLabel_ = new wxStaticText(scrolled, wxID_ANY, "Runtime owner: select an item entry.");
	doodadSingleItemIdCtrl_->Hide();
	doodadSingleItemChanceCtrl_->Hide();
	doodadSingleItemOwnershipLabel_->Hide();
	structureSizer->Add(new wxStaticLine(scrolled), 0, wxEXPAND | wxBOTTOM, FromDIP(10));
	structureSizer->Add(CreateSectionLabel(scrolled, "Composites"), 0, wxBOTTOM, FromDIP(6));
	doodadCompositesList_ = new wxListBox(scrolled, wxID_ANY);
	doodadCompositesList_->SetMinSize(wxSize(scrolled->FromDIP(220), scrolled->FromDIP(132)));
	structureSizer->Add(doodadCompositesList_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
	wxBoxSizer* compositeButtons = new wxBoxSizer(wxHORIZONTAL);
	wxButton* addCompositeButton = new wxButton(scrolled, wxID_ANY, "Add Composite");
	wxButton* removeCompositeButton = new wxButton(scrolled, wxID_ANY, "Remove");
	compositeButtons->Add(addCompositeButton, 1, wxRIGHT, FromDIP(4));
	compositeButtons->Add(removeCompositeButton, 1);
	structureSizer->Add(compositeButtons, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
	doodadCompositeChanceCtrl_ = CreateSpinField(scrolled, 0, 1000000);
	doodadCompositeChanceCtrl_->Hide();
	structureSizer->Add(new wxStaticLine(scrolled), 0, wxEXPAND | wxBOTTOM, FromDIP(10));
	structureSizer->Add(CreateSectionLabel(scrolled, "Tile Layers"), 0, wxBOTTOM, FromDIP(6));
	doodadTileItemsList_ = new wxListBox(scrolled, wxID_ANY);
	doodadTileItemsList_->SetMinSize(wxSize(scrolled->FromDIP(220), scrolled->FromDIP(116)));
	structureSizer->Add(doodadTileItemsList_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
	wxBoxSizer* tileItemButtons = new wxBoxSizer(wxHORIZONTAL);
	wxButton* addTileItemButton = new wxButton(scrolled, wxID_ANY, "Add Item");
	wxButton* removeTileItemButton = new wxButton(scrolled, wxID_ANY, "Remove");
	tileItemButtons->Add(addTileItemButton, 1, wxRIGHT, FromDIP(4));
	tileItemButtons->Add(removeTileItemButton, 1);
	structureSizer->Add(tileItemButtons, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
	doodadTileItemIdCtrl_ = CreateItemIdSpinField(scrolled);
	doodadTileItemOwnershipLabel_ = new wxStaticText(scrolled, wxID_ANY, "Runtime owner: select a tile in the grid or an item entry.");
	doodadTileItemIdCtrl_->Hide();
	doodadTileItemOwnershipLabel_->Hide();

	wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);
	rightSizer->Add(CreateSectionLabel(scrolled, "Scene Editor"), 0, wxBOTTOM, FromDIP(6));
	doodadPreviewSummaryLabel_ = new wxStaticText(scrolled, wxID_ANY, "Use the slider to move between alternatives, then author the current scene directly in the grid.");
	StyleBrushWorkspaceSubtitle(doodadPreviewSummaryLabel_);
	doodadPreviewSummaryLabel_->Wrap(scrolled->FromDIP(520));
	rightSizer->Add(doodadPreviewSummaryLabel_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
	doodadPreviewHintLabel_ = new wxStaticText(scrolled, wxID_ANY, "Click a tile to select it. Use Add Tile and Remove for structure changes. Ctrl+click appends a layer to the selected tile.");
	StyleBrushWorkspaceSubtitle(doodadPreviewHintLabel_);
	doodadPreviewHintLabel_->Wrap(scrolled->FromDIP(520));
	doodadPreviewHintLabel_->Hide();
	doodadAlternativeSliderPanel_ = new wxPanel(scrolled, wxID_ANY);
	doodadAlternativeSliderPanel_->SetMinSize(wxSize(-1, scrolled->FromDIP(32)));
	doodadAlternativeSliderPanel_->SetBackgroundStyle(wxBG_STYLE_PAINT);
	doodadAlternativeSliderPanel_->SetToolTip("Navigate alternatives, click indicators to jump, or use + and - to manage them.");
	rightSizer->Add(doodadAlternativeSliderPanel_, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
	doodadPreviewPanel_ = new wxPanel(scrolled, wxID_ANY, wxDefaultPosition, wxSize(scrolled->FromDIP(520), scrolled->FromDIP(420)), wxBORDER_SIMPLE);
	doodadPreviewPanel_->SetBackgroundStyle(wxBG_STYLE_PAINT);
	doodadPreviewPanel_->SetMinSize(wxSize(scrolled->FromDIP(520), scrolled->FromDIP(420)));
	doodadPreviewFloorSliderPanel_ = new wxPanel(scrolled, wxID_ANY);
	doodadPreviewFloorSliderPanel_->SetMinSize(wxSize(scrolled->FromDIP(44), scrolled->FromDIP(420)));
	doodadPreviewFloorSliderPanel_->SetBackgroundStyle(wxBG_STYLE_PAINT);
	doodadPreviewFloorSliderPanel_->SetToolTip("Select the floor to preview or author new tiles.");
	wxBoxSizer* previewContentSizer = new wxBoxSizer(wxHORIZONTAL);
	previewContentSizer->Add(doodadPreviewPanel_, 1, wxEXPAND | wxRIGHT, FromDIP(8));
	previewContentSizer->Add(doodadPreviewFloorSliderPanel_, 0, wxEXPAND);
	rightSizer->Add(previewContentSizer, 1, wxEXPAND | wxBOTTOM, FromDIP(12));

	doodadTilesList_ = new wxListBox(scrolled, wxID_ANY);
	doodadTilesList_->SetMinSize(wxSize(scrolled->FromDIP(180), scrolled->FromDIP(84)));
	doodadTilesList_->Hide();
	wxButton* addTileButton = new wxButton(scrolled, wxID_ANY, "Add Tile");
	wxButton* removeTileButton = new wxButton(scrolled, wxID_ANY, "Remove");
	addTileButton->Hide();
	removeTileButton->Hide();
	doodadTileOffsetXCtrl_ = CreateSpinField(scrolled, -1000, 1000);
	doodadTileOffsetYCtrl_ = CreateSpinField(scrolled, -1000, 1000);
	doodadTileOffsetZCtrl_ = CreateSpinField(scrolled, -1000, 1000);
	doodadTileOffsetXCtrl_->Hide();
	doodadTileOffsetYCtrl_->Hide();
	doodadTileOffsetZCtrl_->Hide();

	rootSizer->Add(structureSizer, 0, wxEXPAND | wxRIGHT, FromDIP(10));
	rootSizer->Add(new wxStaticLine(scrolled, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL), 0, wxEXPAND | wxRIGHT, FromDIP(10));
	rootSizer->Add(rightSizer, 1, wxEXPAND);
	scrolled->SetSizer(rootSizer);
	scrolled->FitInside();

	doodadAlternativeSliderPanel_->Bind(wxEVT_PAINT, &MaterialsWorkbenchBrushPanel::OnDoodadAlternativeSliderPaint, this);
	doodadAlternativeSliderPanel_->Bind(wxEVT_LEFT_DOWN, &MaterialsWorkbenchBrushPanel::OnDoodadAlternativeSliderLeftDown, this);
	doodadPreviewFloorSliderPanel_->Bind(wxEVT_PAINT, &MaterialsWorkbenchBrushPanel::OnDoodadFloorSliderPaint, this);
	doodadPreviewFloorSliderPanel_->Bind(wxEVT_LEFT_DOWN, &MaterialsWorkbenchBrushPanel::OnDoodadFloorSliderLeftDown, this);
	addSingleButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnAddDoodadSingleItem, this);
	removeSingleButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnRemoveDoodadSingleItem, this);
	doodadSingleItemsList_->Bind(wxEVT_LISTBOX, &MaterialsWorkbenchBrushPanel::OnDoodadSingleItemSelected, this);
	doodadSingleItemsList_->Bind(wxEVT_RIGHT_DOWN, &MaterialsWorkbenchBrushPanel::OnDoodadSingleItemRightDown, this);
	doodadSingleItemIdCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnDoodadSingleItemValueChanged, this);
	doodadSingleItemChanceCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnDoodadSingleItemValueChanged, this);
	doodadSingleItemIdCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnDoodadSingleItemValueChanged, this);
	doodadSingleItemChanceCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnDoodadSingleItemValueChanged, this);
	addCompositeButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnAddDoodadComposite, this);
	removeCompositeButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnRemoveDoodadComposite, this);
	doodadCompositesList_->Bind(wxEVT_LISTBOX, &MaterialsWorkbenchBrushPanel::OnDoodadCompositeSelected, this);
	doodadCompositesList_->Bind(wxEVT_RIGHT_DOWN, &MaterialsWorkbenchBrushPanel::OnDoodadCompositeRightDown, this);
	doodadCompositeChanceCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnDoodadCompositeChanceChanged, this);
	doodadCompositeChanceCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnDoodadCompositeChanceChanged, this);
	doodadPreviewPanel_->Bind(wxEVT_PAINT, &MaterialsWorkbenchBrushPanel::OnDoodadPreviewPaint, this);
	doodadPreviewPanel_->Bind(wxEVT_LEFT_DOWN, &MaterialsWorkbenchBrushPanel::OnDoodadPreviewLeftDown, this);
	doodadPreviewPanel_->Bind(wxEVT_RIGHT_DOWN, &MaterialsWorkbenchBrushPanel::OnDoodadPreviewRightDown, this);
	addTileButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnAddDoodadTile, this);
	removeTileButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnRemoveDoodadTile, this);
	doodadTilesList_->Bind(wxEVT_LISTBOX, &MaterialsWorkbenchBrushPanel::OnDoodadTileSelected, this);
	doodadTileOffsetXCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnDoodadTileOffsetChanged, this);
	doodadTileOffsetYCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnDoodadTileOffsetChanged, this);
	doodadTileOffsetZCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnDoodadTileOffsetChanged, this);
	doodadTileOffsetXCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnDoodadTileOffsetChanged, this);
	doodadTileOffsetYCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnDoodadTileOffsetChanged, this);
	doodadTileOffsetZCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnDoodadTileOffsetChanged, this);
	addTileItemButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnAddDoodadTileItem, this);
	removeTileItemButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchBrushPanel::OnRemoveDoodadTileItem, this);
	doodadTileItemsList_->Bind(wxEVT_LISTBOX, &MaterialsWorkbenchBrushPanel::OnDoodadTileItemSelected, this);
	doodadTileItemsList_->Bind(wxEVT_RIGHT_DOWN, &MaterialsWorkbenchBrushPanel::OnDoodadTileItemRightDown, this);
	doodadTileItemIdCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchBrushPanel::OnDoodadTileItemValueChanged, this);
	doodadTileItemIdCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchBrushPanel::OnDoodadTileItemValueChanged, this);
	return panel;
}

void MaterialsWorkbenchBrushPanel::ClearWorkspace(const wxString &message) {
	brushStorage_ = BrushStorageRecord();
	loadedBrushStorage_ = BrushStorageRecord();
	runtimeSyncedBrushStorage_ = BrushStorageRecord();
	currentContextKey_.clear();
	currentItemIndex_ = -1;
	hasBrush_ = false;
	dirty_ = false;
	hasRuntimeSyncedBrushStorage_ = false;
	ResetVariationSelection();

	UpdateWorkspaceHeader();
	summaryLabel_->SetLabel(message);

	internalUpdate_ = true;
	idCtrl_->SetValue("");
	storageCtrl_->SetValue("");
	nameCtrl_->SetValue("");
	typeCtrl_->SetValue("");
	sourceCtrl_->SetValue("");
	lookIdCtrl_->SetValue(0);
	serverLookIdCtrl_->SetValue(0);
	zOrderCtrl_->SetValue(0);
	thicknessCtrl_->SetValue(0);
	thicknessCeilingCtrl_->SetValue(0);
	draggableCtrl_->SetValue(false);
	onBlockingCtrl_->SetValue(false);
	onDuplicateCtrl_->SetValue(false);
	redoBordersCtrl_->SetValue(false);
	randomizeCtrl_->SetValue(false);
	oneSizeCtrl_->SetValue(false);
	soloOptionalCtrl_->SetValue(false);
	internalUpdate_ = false;
	RefreshLookIdOwnershipHints();

	SetFieldsEnabled(false);
	UpdateActionButtons();
	RefreshVariationEditor();
	UpdateModifiedHighlights();
	NotifyBrushStateChanged();
	if (workspaceTabs_) {
		workspaceTabs_->SetSelection(0);
	}
	SetStatusMessage(message);
	Layout();
}

bool MaterialsWorkbenchBrushPanel::LoadBrush(const wxString &contextKey, int itemIndex) {
	const int64_t previousBrushId = hasBrush_ ? brushStorage_.brush.id : 0;
	const VariationEditorState previousVariationState = hasBrush_ ? CaptureVariationEditorState() : VariationEditorState();

	wxString error;
	BrushStorageRecord storage;
	if (!controller_.GetBrushDetails(contextKey, itemIndex, storage, error)) {
		spdlog::warn(
			"Materials Workbench failed to load brush details: context='{}' index={} error='{}'",
			contextKey.ToStdString(),
			itemIndex,
			error.ToStdString()
		);
		ClearWorkspace("Failed to load brush details: " + error);
		return false;
	}

	const bool preserveVariationState = previousVariationState.valid && previousBrushId == storage.brush.id;

	brushStorage_ = storage;
	loadedBrushStorage_ = storage;
	runtimeSyncedBrushStorage_ = storage;
	currentContextKey_ = contextKey;
	currentItemIndex_ = itemIndex;
	hasBrush_ = true;
	dirty_ = false;
	hasRuntimeSyncedBrushStorage_ = true;
	ResetVariationSelection();

	PopulateFields();
	if (preserveVariationState) {
		RestoreVariationEditorState(previousVariationState);
	}
	SetFieldsEnabled(true);
	UpdateActionButtons();
	UpdateModifiedHighlights();
	NotifyBrushStateChanged();
	SetStatusMessage("Ready. Editing brush data from materials.db. Update metadata or variations, then Save or Revert.");
	spdlog::info(
		"Materials Workbench loaded brush from materials.db: id={} name='{}' type='{}' preserved_context={}",
		static_cast<long long>(brushStorage_.brush.id),
		brushStorage_.brush.name.ToStdString(),
		brushStorage_.brush.type.ToStdString(),
		preserveVariationState
	);
	Layout();
	return true;
}

void MaterialsWorkbenchBrushPanel::PopulateFields() {
	PopulateMetadataFields();
	UpdateSummary();
	RefreshVariationEditor();
}

void MaterialsWorkbenchBrushPanel::PopulateMetadataFields() {
	const BrushRecord &brush = brushStorage_.brush;

	internalUpdate_ = true;
	idCtrl_->SetValue(wxString::Format("%lld", static_cast<long long>(brush.id)));
	storageCtrl_->SetValue("materials.db");
	nameCtrl_->SetValue(brush.name);
	typeCtrl_->SetValue(brush.type);
	sourceCtrl_->SetValue(FormatImportedFromValue(brush.sourceFile));
	lookIdCtrl_->SetValue(brush.lookId);
	serverLookIdCtrl_->SetValue(brush.serverLookId);
	zOrderCtrl_->SetValue(brush.zOrder);
	thicknessCtrl_->SetValue(brush.thickness);
	thicknessCeilingCtrl_->SetValue(brush.thicknessCeiling);
	draggableCtrl_->SetValue(brush.draggable);
	onBlockingCtrl_->SetValue(brush.onBlocking);
	onDuplicateCtrl_->SetValue(brush.onDuplicate);
	redoBordersCtrl_->SetValue(brush.redoBorders);
	randomizeCtrl_->SetValue(brush.randomize);
	oneSizeCtrl_->SetValue(brush.oneSize);
	soloOptionalCtrl_->SetValue(brush.soloOptional);
	internalUpdate_ = false;
	RefreshLookIdOwnershipHints();
	UpdateWorkspaceHeader();
}

void MaterialsWorkbenchBrushPanel::UpdateSummary() {
	summaryLabel_->SetLabel(
		wxString::Format(
			"Items: %zu | Borders: %zu | Links: %zu | Wall parts: %zu | Carpet nodes: %zu | Table nodes: %zu | Doodad alternatives: %zu",
			brushStorage_.items.size(),
			brushStorage_.borders.size(),
			brushStorage_.links.size(),
			brushStorage_.wallParts.size(),
			brushStorage_.carpetNodes.size(),
			brushStorage_.tableNodes.size(),
			brushStorage_.doodadAlternatives.size()
		)
	);
}

void MaterialsWorkbenchBrushPanel::SetStatusMessage(const wxString &message) {
	statusLabel_->SetLabel(message);
	statusLabel_->Wrap(FromDIP(760));
}

BrushStorageRecord MaterialsWorkbenchBrushPanel::BuildEditableStorageFromCurrentState() const {
	BrushStorageRecord storage = brushStorage_;
	BrushRecord &brush = storage.brush;
	brush.name = TrimmedValue(nameCtrl_);
	brush.type = TrimmedValue(typeCtrl_);
	brush.sourceFile = ParseImportedFromEditorValue(TrimmedValue(sourceCtrl_));
	brush.lookId = lookIdCtrl_->GetValue();
	brush.serverLookId = serverLookIdCtrl_->GetValue();
	brush.zOrder = zOrderCtrl_->GetValue();
	brush.thickness = thicknessCtrl_->GetValue();
	brush.thicknessCeiling = thicknessCeilingCtrl_->GetValue();
	brush.draggable = draggableCtrl_->GetValue();
	brush.onBlocking = onBlockingCtrl_->GetValue();
	brush.onDuplicate = onDuplicateCtrl_->GetValue();
	brush.redoBorders = redoBordersCtrl_->GetValue();
	brush.randomize = randomizeCtrl_->GetValue();
	brush.oneSize = oneSizeCtrl_->GetValue();
	brush.soloOptional = soloOptionalCtrl_->GetValue();
	NormalizeBrushStorageForEditing(storage);
	return storage;
}

void MaterialsWorkbenchBrushPanel::RefreshDirtyState() {
	if (!hasBrush_) {
		dirty_ = false;
		UpdateWorkspaceHeader();
		UpdateActionButtons();
		UpdateModifiedHighlights();
		NotifyBrushStateChanged();
		return;
	}

	const BrushStorageRecord editableStorage = BuildEditableStorageFromCurrentState();
	dirty_ = !AreBrushStorageRecordsEqual(editableStorage, loadedBrushStorage_);
	UpdateWorkspaceHeader();
	UpdateActionButtons();
	UpdateModifiedHighlights();
	RefreshLiveDoodadRuntime(editableStorage);
	NotifyBrushStateChanged();
}

void MaterialsWorkbenchBrushPanel::RefreshLiveDoodadRuntime(const BrushStorageRecord &editableStorage) {
	if (!hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (!hasRuntimeSyncedBrushStorage_) {
		runtimeSyncedBrushStorage_ = loadedBrushStorage_;
		hasRuntimeSyncedBrushStorage_ = true;
	}
	if (AreBrushStorageRecordsEqual(editableStorage, runtimeSyncedBrushStorage_)) {
		return;
	}

	const BrushStorageRecord previousStorage = brushStorage_;
	brushStorage_ = editableStorage;
	wxString validationError;
	const bool valid = ValidateBrushStorage(validationError);
	brushStorage_ = previousStorage;
	if (!valid) {
		return;
	}

	const wxString previousRuntimeName = runtimeSyncedBrushStorage_.brush.name;
	const wxString newRuntimeName = editableStorage.brush.name;
	if (previousRuntimeName != newRuntimeName) {
		if (Brush* runtimeBrush = g_brushes.getBrush(previousRuntimeName.ToStdString())) {
			g_brushes.renameBrush(runtimeBrush, previousRuntimeName.ToStdString(), newRuntimeName.ToStdString());
		}
	}

	wxArrayString warnings;
	wxString runtimeError;
	if (!g_brushes.reloadBrushFromStorage(editableStorage, warnings, runtimeError)) {
		spdlog::warn(
			"Materials Workbench live doodad runtime sync failed: id={} name='{}' error='{}'",
			static_cast<long long>(editableStorage.brush.id),
			editableStorage.brush.name.ToStdString(),
			runtimeError.ToStdString()
		);
		return;
	}
	for (const wxString &warning : warnings) {
		spdlog::warn(
			"Materials Workbench live doodad runtime sync warning: id={} warning='{}'",
			static_cast<long long>(editableStorage.brush.id),
			warning.ToStdString()
		);
	}

	const uint16_t effectiveLookId = static_cast<uint16_t>(
		editableStorage.brush.serverLookId > 0 ? editableStorage.brush.serverLookId : editableStorage.brush.lookId
	);
	if (!g_gui.SyncBrushInPalettes(previousRuntimeName, newRuntimeName, effectiveLookId)) {
		spdlog::debug(
			"Materials Workbench live doodad palette sync skipped: old='{}' new='{}' lookId={}",
			previousRuntimeName.ToStdString(),
			newRuntimeName.ToStdString(),
			effectiveLookId
		);
	}

	runtimeSyncedBrushStorage_ = editableStorage;
	hasRuntimeSyncedBrushStorage_ = true;
}

void MaterialsWorkbenchBrushPanel::NotifyBrushStateChanged() {
	if (onBrushStateChanged_) {
		onBrushStateChanged_();
	}
}

void MaterialsWorkbenchBrushPanel::UpdateModifiedHighlights() {
	if (!hasBrush_) {
		ApplyModifiedEditorStyle(nameCtrl_, false);
		ApplyModifiedEditorStyle(typeCtrl_, false);
		ApplyModifiedEditorStyle(sourceCtrl_, false);
		ApplyModifiedEditorStyle(lookIdCtrl_, false);
		ApplyModifiedEditorStyle(serverLookIdCtrl_, false);
		ApplyModifiedEditorStyle(zOrderCtrl_, false);
		ApplyModifiedEditorStyle(thicknessCtrl_, false);
		ApplyModifiedEditorStyle(thicknessCeilingCtrl_, false);
		ApplyModifiedToggleStyle(draggableCtrl_, false);
		ApplyModifiedToggleStyle(onBlockingCtrl_, false);
		ApplyModifiedToggleStyle(onDuplicateCtrl_, false);
		ApplyModifiedToggleStyle(redoBordersCtrl_, false);
		ApplyModifiedToggleStyle(randomizeCtrl_, false);
		ApplyModifiedToggleStyle(oneSizeCtrl_, false);
		ApplyModifiedToggleStyle(soloOptionalCtrl_, false);
		ApplyModifiedLabelStyle(variationsStatusLabel_, "Variation Data", false);
		ApplyModifiedLabelStyle(alignedSectionLabel_, UsesAlignedVariationEditor() && GetEffectiveBrushType() == "table" ? "Table States" : "Carpet Layout Map", false);
		ApplyModifiedEditorStyle(groundItemsList_, false);
		ApplyModifiedEditorStyle(groundItemsCardsPanel_, false);
		ApplyModifiedEditorStyle(groundPreviewPanel_, false);
		ApplyModifiedEditorStyle(groundItemIdCtrl_, false);
		ApplyModifiedEditorStyle(groundItemChanceCtrl_, false);
		ApplyModifiedEditorStyle(alignedContextPanel_, false);
		ApplyModifiedEditorStyle(alignedNodesList_, false);
		ApplyModifiedEditorStyle(alignedSeamlessPreviewPanel_, false);
		ApplyModifiedEditorStyle(alignedItemsCardsPanel_, false);
		ApplyModifiedEditorStyle(alignedNodeAlignCtrl_, false);
		ApplyModifiedEditorStyle(alignedNodeAlignChoice_, false);
		ApplyModifiedEditorStyle(alignedItemsList_, false);
		ApplyModifiedEditorStyle(alignedItemIdCtrl_, false);
		ApplyModifiedEditorStyle(alignedItemChanceCtrl_, false);
		ApplyModifiedEditorStyle(doodadAlternativeSliderPanel_, false);
		ApplyModifiedEditorStyle(doodadSingleItemsList_, false);
		ApplyModifiedEditorStyle(doodadSingleItemIdCtrl_, false);
		ApplyModifiedEditorStyle(doodadSingleItemChanceCtrl_, false);
		ApplyModifiedEditorStyle(doodadCompositesList_, false);
		ApplyModifiedEditorStyle(doodadCompositeChanceCtrl_, false);
		ApplyModifiedEditorStyle(doodadTilesList_, false);
		ApplyModifiedEditorStyle(doodadTileOffsetXCtrl_, false);
		ApplyModifiedEditorStyle(doodadTileOffsetYCtrl_, false);
		ApplyModifiedEditorStyle(doodadTileOffsetZCtrl_, false);
		ApplyModifiedEditorStyle(doodadTileItemsList_, false);
		ApplyModifiedEditorStyle(doodadTileItemIdCtrl_, false);
		return;
	}

	const BrushStorageRecord editableStorage = BuildEditableStorageFromCurrentState();
	UpdateMetadataModifiedHighlights(editableStorage.brush);
	UpdateVariationModifiedHighlights(editableStorage);
}

void MaterialsWorkbenchBrushPanel::CommitVariationEditorState() {
	const wxString type = GetEffectiveBrushType();
	if (type == "ground") {
		const int selection = groundItemsList_ ? groundItemsList_->GetSelection() : wxNOT_FOUND;
		if (selection != wxNOT_FOUND && static_cast<size_t>(selection) < brushStorage_.items.size()) {
			brushStorage_.items[static_cast<size_t>(selection)].itemId = groundItemIdCtrl_->GetValue();
			brushStorage_.items[static_cast<size_t>(selection)].chance = groundItemChanceCtrl_->GetValue();
		}
		return;
	}

	if (type == "carpet") {
		const int nodeSelection = alignedNodesList_ ? alignedNodesList_->GetSelection() : wxNOT_FOUND;
		if (nodeSelection != wxNOT_FOUND && static_cast<size_t>(nodeSelection) < brushStorage_.carpetNodes.size()) {
			auto &node = brushStorage_.carpetNodes[static_cast<size_t>(nodeSelection)];
			node.align = alignedPendingCarpetAlign_.IsEmpty() ? alignedNodeAlignCtrl_->GetValue() : alignedPendingCarpetAlign_;
			const int itemSelection = alignedItemsList_ ? alignedItemsList_->GetSelection() : wxNOT_FOUND;
			if (itemSelection != wxNOT_FOUND && static_cast<size_t>(itemSelection) < node.items.size()) {
				node.items[static_cast<size_t>(itemSelection)].itemId = alignedItemIdCtrl_->GetValue();
				node.items[static_cast<size_t>(itemSelection)].chance = alignedItemChanceCtrl_->GetValue();
			}
		}
		return;
	}

	if (type == "table") {
		const int nodeSelection = alignedNodesList_ ? alignedNodesList_->GetSelection() : wxNOT_FOUND;
		if (nodeSelection != wxNOT_FOUND && static_cast<size_t>(nodeSelection) < brushStorage_.tableNodes.size()) {
			auto &node = brushStorage_.tableNodes[static_cast<size_t>(nodeSelection)];
			node.align = alignedNodeAlignChoice_ ? alignedNodeAlignChoice_->GetStringSelection() : node.align;
			const int itemSelection = alignedItemsList_ ? alignedItemsList_->GetSelection() : wxNOT_FOUND;
			if (itemSelection != wxNOT_FOUND && static_cast<size_t>(itemSelection) < node.items.size()) {
				node.items[static_cast<size_t>(itemSelection)].itemId = alignedItemIdCtrl_->GetValue();
				node.items[static_cast<size_t>(itemSelection)].chance = alignedItemChanceCtrl_->GetValue();
			}
		}
		return;
	}

	if (type == "doodad") {
		const int alternativeSelection = doodadAlternativeIndex_;
		if (alternativeSelection == wxNOT_FOUND || static_cast<size_t>(alternativeSelection) >= brushStorage_.doodadAlternatives.size()) {
			return;
		}

		auto &alternative = brushStorage_.doodadAlternatives[static_cast<size_t>(alternativeSelection)];
		const int singleSelection = doodadSingleItemIndex_;
		if (singleSelection != wxNOT_FOUND && static_cast<size_t>(singleSelection) < alternative.singleItems.size()) {
			alternative.singleItems[static_cast<size_t>(singleSelection)].itemId = doodadSingleItemIdCtrl_->GetValue();
			alternative.singleItems[static_cast<size_t>(singleSelection)].chance = doodadSingleItemChanceCtrl_->GetValue();
		}

		const int compositeSelection = doodadCompositeIndex_;
		if (compositeSelection != wxNOT_FOUND && static_cast<size_t>(compositeSelection) < alternative.composites.size()) {
			auto &composite = alternative.composites[static_cast<size_t>(compositeSelection)];
			composite.chance = doodadCompositeChanceCtrl_->GetValue();

			const int tileSelection = doodadTileIndex_;
			if (tileSelection != wxNOT_FOUND && static_cast<size_t>(tileSelection) < composite.tiles.size()) {
				auto &tile = composite.tiles[static_cast<size_t>(tileSelection)];
				tile.offsetX = doodadTileOffsetXCtrl_->GetValue();
				tile.offsetY = doodadTileOffsetYCtrl_->GetValue();
				tile.offsetZ = doodadTileOffsetZCtrl_->GetValue();

				const int tileItemSelection = doodadTileItemIndex_;
				if (tileItemSelection != wxNOT_FOUND && static_cast<size_t>(tileItemSelection) < tile.items.size()) {
					tile.items[static_cast<size_t>(tileItemSelection)].itemId = doodadTileItemIdCtrl_->GetValue();
				}
			}
		}
	}
}

void MaterialsWorkbenchBrushPanel::UpdateMetadataModifiedHighlights(const BrushRecord &editableBrush) {
	ApplyModifiedEditorStyle(nameCtrl_, editableBrush.name != loadedBrushStorage_.brush.name);
	ApplyModifiedEditorStyle(typeCtrl_, editableBrush.type != loadedBrushStorage_.brush.type);
	ApplyModifiedEditorStyle(sourceCtrl_, editableBrush.sourceFile != loadedBrushStorage_.brush.sourceFile);
	ApplyModifiedEditorStyle(lookIdCtrl_, editableBrush.lookId != loadedBrushStorage_.brush.lookId);
	ApplyModifiedEditorStyle(serverLookIdCtrl_, editableBrush.serverLookId != loadedBrushStorage_.brush.serverLookId);
	ApplyModifiedEditorStyle(zOrderCtrl_, editableBrush.zOrder != loadedBrushStorage_.brush.zOrder);
	ApplyModifiedEditorStyle(thicknessCtrl_, editableBrush.thickness != loadedBrushStorage_.brush.thickness);
	ApplyModifiedEditorStyle(thicknessCeilingCtrl_, editableBrush.thicknessCeiling != loadedBrushStorage_.brush.thicknessCeiling);
	ApplyModifiedToggleStyle(draggableCtrl_, editableBrush.draggable != loadedBrushStorage_.brush.draggable);
	ApplyModifiedToggleStyle(onBlockingCtrl_, editableBrush.onBlocking != loadedBrushStorage_.brush.onBlocking);
	ApplyModifiedToggleStyle(onDuplicateCtrl_, editableBrush.onDuplicate != loadedBrushStorage_.brush.onDuplicate);
	ApplyModifiedToggleStyle(redoBordersCtrl_, editableBrush.redoBorders != loadedBrushStorage_.brush.redoBorders);
	ApplyModifiedToggleStyle(randomizeCtrl_, editableBrush.randomize != loadedBrushStorage_.brush.randomize);
	ApplyModifiedToggleStyle(oneSizeCtrl_, editableBrush.oneSize != loadedBrushStorage_.brush.oneSize);
	ApplyModifiedToggleStyle(soloOptionalCtrl_, editableBrush.soloOptional != loadedBrushStorage_.brush.soloOptional);
}

void MaterialsWorkbenchBrushPanel::UpdateVariationModifiedHighlights(const BrushStorageRecord &editableStorage) {
	const bool groundModified = !VectorsEqual(editableStorage.items, loadedBrushStorage_.items, AreBrushItemRecordsEqual);
	const bool carpetModified = !VectorsEqual(editableStorage.carpetNodes, loadedBrushStorage_.carpetNodes, AreCarpetNodeRecordsEqual);
	const bool tableModified = !VectorsEqual(editableStorage.tableNodes, loadedBrushStorage_.tableNodes, AreTableNodeRecordsEqual);
	const bool doodadModified = !VectorsEqual(editableStorage.doodadAlternatives, loadedBrushStorage_.doodadAlternatives, AreDoodadAlternativeRecordsEqual);
	const bool variationsModified = groundModified || carpetModified || tableModified || doodadModified;

	ApplyModifiedLabelStyle(variationsStatusLabel_, "Variation Data", variationsModified);
	ApplyModifiedEditorStyle(groundItemsList_, groundModified);
	ApplyModifiedEditorStyle(groundItemsCardsPanel_, groundModified);
	ApplyModifiedEditorStyle(groundPreviewPanel_, groundModified);
	ApplyModifiedEditorStyle(groundItemIdCtrl_, groundModified);
	ApplyModifiedEditorStyle(groundItemChanceCtrl_, groundModified);

	const bool alignedModified = GetEffectiveBrushType() == "table" ? tableModified : carpetModified;
	ApplyModifiedLabelStyle(alignedSectionLabel_, GetEffectiveBrushType() == "table" ? "Table States" : "Carpet Layout Map", alignedModified);
	ApplyModifiedEditorStyle(alignedContextPanel_, alignedModified);
	ApplyModifiedEditorStyle(alignedNodesList_, alignedModified);
	ApplyModifiedEditorStyle(alignedSeamlessPreviewPanel_, alignedModified);
	ApplyModifiedEditorStyle(alignedItemsCardsPanel_, alignedModified);
	ApplyModifiedEditorStyle(alignedNodeAlignCtrl_, alignedModified);
	ApplyModifiedEditorStyle(alignedNodeAlignChoice_, alignedModified);
	ApplyModifiedEditorStyle(alignedItemsList_, alignedModified);
	ApplyModifiedEditorStyle(alignedItemIdCtrl_, alignedModified);
	ApplyModifiedEditorStyle(alignedItemChanceCtrl_, alignedModified);

	ApplyModifiedEditorStyle(doodadAlternativeSliderPanel_, doodadModified);
	ApplyModifiedEditorStyle(doodadSingleItemsList_, doodadModified);
	ApplyModifiedEditorStyle(doodadSingleItemIdCtrl_, doodadModified);
	ApplyModifiedEditorStyle(doodadSingleItemChanceCtrl_, doodadModified);
	ApplyModifiedEditorStyle(doodadCompositesList_, doodadModified);
	ApplyModifiedEditorStyle(doodadCompositeChanceCtrl_, doodadModified);
	ApplyModifiedEditorStyle(doodadTilesList_, doodadModified);
	ApplyModifiedEditorStyle(doodadTileOffsetXCtrl_, doodadModified);
	ApplyModifiedEditorStyle(doodadTileOffsetYCtrl_, doodadModified);
	ApplyModifiedEditorStyle(doodadTileOffsetZCtrl_, doodadModified);
	ApplyModifiedEditorStyle(doodadTileItemsList_, doodadModified);
	ApplyModifiedEditorStyle(doodadTileItemIdCtrl_, doodadModified);
}

bool MaterialsWorkbenchBrushPanel::TryGetRuntimeBrushOwnerName(int itemId, wxString &ownerName) const {
	ownerName.clear();
	if (!IsKnownItemId(itemId)) {
		return false;
	}

	const ItemType &itemType = g_items.getItemType(static_cast<uint16_t>(itemId));
	if (!itemType.brush) {
		return false;
	}

	ownerName = wxString::FromUTF8(itemType.brush->getName());
	return !ownerName.IsEmpty();
}

bool MaterialsWorkbenchBrushPanel::IsCurrentBrushOwnerName(const wxString &ownerName) const {
	if (ownerName.IsEmpty()) {
		return false;
	}

	const wxString editedName = hasBrush_ ? TrimmedValue(nameCtrl_) : "";
	return ownerName == editedName || ownerName == brushStorage_.brush.name || ownerName == loadedBrushStorage_.brush.name;
}

void MaterialsWorkbenchBrushPanel::UpdateItemOwnershipHint(wxStaticText* label, int itemId, bool hasSelection) const {
	if (!label) {
		return;
	}

	if (!hasSelection) {
		label->SetLabel("Runtime owner: select an item entry.");
		label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
		label->Refresh();
		return;
	}

	if (itemId <= 0) {
		label->SetLabel("Runtime owner: item id must be greater than 0.");
		label->SetForegroundColour(wxColour(176, 102, 0));
		label->Refresh();
		return;
	}

	if (!IsKnownItemId(itemId)) {
		label->SetLabel(wxString::Format("Runtime owner: item id %d does not exist in the catalog.", itemId));
		label->SetForegroundColour(wxColour(176, 102, 0));
		label->Refresh();
		return;
	}

	wxString ownerName;
	if (!TryGetRuntimeBrushOwnerName(itemId, ownerName)) {
		label->SetLabel("Runtime owner: free item id.");
		label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
		label->Refresh();
		return;
	}

	if (IsCurrentBrushOwnerName(ownerName)) {
		label->SetLabel(wxString::Format("Runtime owner: this brush (%s).", ownerName));
		label->SetForegroundColour(wxColour(46, 125, 50));
		label->Refresh();
		return;
	}

	label->SetLabel(wxString::Format("Runtime owner: already used by brush \"%s\".", ownerName));
	label->SetForegroundColour(wxColour(176, 102, 0));
	label->Refresh();
}

void MaterialsWorkbenchBrushPanel::RefreshLookIdOwnershipHints() const {
	if (!hasBrush_) {
		if (lookIdOwnershipLabel_) {
			lookIdOwnershipLabel_->SetLabel("Runtime owner: select a brush.");
			lookIdOwnershipLabel_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
			lookIdOwnershipLabel_->Refresh();
		}
		if (serverLookIdOwnershipLabel_) {
			serverLookIdOwnershipLabel_->SetLabel("Runtime owner: select a brush.");
			serverLookIdOwnershipLabel_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
			serverLookIdOwnershipLabel_->Refresh();
		}
		return;
	}

	UpdateItemOwnershipHint(lookIdOwnershipLabel_, lookIdCtrl_ ? lookIdCtrl_->GetValue() : 0, true);
	UpdateItemOwnershipHint(serverLookIdOwnershipLabel_, serverLookIdCtrl_ ? serverLookIdCtrl_->GetValue() : 0, true);
}

MaterialsWorkbenchBrushPanel::VariationEditorState MaterialsWorkbenchBrushPanel::CaptureVariationEditorState() const {
	VariationEditorState state;
	state.valid = hasBrush_;
	state.workspaceTabSelection = workspaceTabs_ ? workspaceTabs_->GetSelection() : 0;
	if (metadataPage_) {
		metadataPage_->GetViewStart(&state.metadataViewX, &state.metadataViewY);
	}
	state.groundItemIndex = groundItemIndex_;
	state.alignedNodeIndex = alignedNodeIndex_;
	state.alignedItemIndex = alignedItemIndex_;
	state.doodadAlternativeIndex = doodadAlternativeIndex_;
	state.doodadSingleItemIndex = doodadSingleItemIndex_;
	state.doodadCompositeIndex = doodadCompositeIndex_;
	state.doodadTileIndex = doodadTileIndex_;
	state.doodadTileItemIndex = doodadTileItemIndex_;
	state.doodadPreviewFloor = doodadPreviewFloor_;
	state.doodadPreviewPreferComposite = doodadPreviewPreferComposite_;
	state.groundTopItem = CaptureListTopItem(groundItemsList_);
	state.alignedNodesTopItem = CaptureListTopItem(alignedNodesList_);
	state.alignedItemsTopItem = CaptureListTopItem(alignedItemsList_);
	state.doodadAlternativesTopItem = wxNOT_FOUND;
	state.doodadSingleItemsTopItem = CaptureListTopItem(doodadSingleItemsList_);
	state.doodadCompositesTopItem = CaptureListTopItem(doodadCompositesList_);
	state.doodadTilesTopItem = CaptureListTopItem(doodadTilesList_);
	state.doodadTileItemsTopItem = CaptureListTopItem(doodadTileItemsList_);
	return state;
}

void MaterialsWorkbenchBrushPanel::RestoreVariationEditorState(const VariationEditorState &state) {
	if (!state.valid || !hasBrush_) {
		return;
	}

	if (workspaceTabs_ && workspaceTabs_->GetPageCount() > 0) {
		const int tabSelection = std::min<int>(std::max(0, state.workspaceTabSelection), static_cast<int>(workspaceTabs_->GetPageCount()) - 1);
		workspaceTabs_->SetSelection(tabSelection);
	}

	if (metadataPage_ && state.metadataViewX != -1 && state.metadataViewY != -1) {
		metadataPage_->Scroll(state.metadataViewX, state.metadataViewY);
	}

	groundItemIndex_ = ClampIndexForCount(state.groundItemIndex, brushStorage_.items.size());

	if (GetEffectiveBrushType() == "carpet") {
		alignedNodeIndex_ = ClampIndexForCount(state.alignedNodeIndex, brushStorage_.carpetNodes.size());
		if (alignedNodeIndex_ >= 0) {
			alignedItemIndex_ = ClampIndexForCount(state.alignedItemIndex, brushStorage_.carpetNodes[alignedNodeIndex_].items.size());
		} else {
			alignedItemIndex_ = -1;
		}
	} else if (GetEffectiveBrushType() == "table") {
		alignedNodeIndex_ = ClampIndexForCount(state.alignedNodeIndex, brushStorage_.tableNodes.size());
		if (alignedNodeIndex_ >= 0) {
			alignedItemIndex_ = ClampIndexForCount(state.alignedItemIndex, brushStorage_.tableNodes[alignedNodeIndex_].items.size());
		} else {
			alignedItemIndex_ = -1;
		}
	} else {
		alignedNodeIndex_ = -1;
		alignedItemIndex_ = -1;
	}

	doodadAlternativeIndex_ = ClampIndexForCount(state.doodadAlternativeIndex, brushStorage_.doodadAlternatives.size());
	if (doodadAlternativeIndex_ >= 0) {
		const auto &alternative = brushStorage_.doodadAlternatives[doodadAlternativeIndex_];
		doodadSingleItemIndex_ = ClampIndexForCount(state.doodadSingleItemIndex, alternative.singleItems.size());
		doodadCompositeIndex_ = ClampIndexForCount(state.doodadCompositeIndex, alternative.composites.size());
		if (doodadCompositeIndex_ >= 0) {
			const auto &composite = alternative.composites[doodadCompositeIndex_];
			doodadTileIndex_ = ClampIndexForCount(state.doodadTileIndex, composite.tiles.size());
			if (doodadTileIndex_ >= 0) {
				doodadTileItemIndex_ = ClampIndexForCount(state.doodadTileItemIndex, composite.tiles[doodadTileIndex_].items.size());
			} else {
				doodadTileItemIndex_ = -1;
			}
		} else {
			doodadTileIndex_ = -1;
			doodadTileItemIndex_ = -1;
		}
	} else {
		doodadSingleItemIndex_ = -1;
		doodadCompositeIndex_ = -1;
		doodadTileIndex_ = -1;
		doodadTileItemIndex_ = -1;
	}
	doodadPreviewFloor_ = state.doodadPreviewFloor;
	doodadPreviewPreferComposite_ = state.doodadPreviewPreferComposite;

	RefreshVariationEditor();

	RestoreListTopItem(groundItemsList_, state.groundTopItem);
	RestoreListTopItem(alignedNodesList_, state.alignedNodesTopItem);
	RestoreListTopItem(alignedItemsList_, state.alignedItemsTopItem);
	RestoreListTopItem(doodadSingleItemsList_, state.doodadSingleItemsTopItem);
	RestoreListTopItem(doodadCompositesList_, state.doodadCompositesTopItem);
	RestoreListTopItem(doodadTilesList_, state.doodadTilesTopItem);
	RestoreListTopItem(doodadTileItemsList_, state.doodadTileItemsTopItem);
}

void MaterialsWorkbenchBrushPanel::UpdateWorkspaceHeader() {
	if (workspaceTabs_ && workspaceTabs_->GetPageCount() > 1) {
		workspaceTabs_->SetPageText(1, GetVariationTabTitle());
	}

	if (!hasBrush_) {
		titleLabel_->SetLabel("No brush selected");
		subtitleLabel_->SetLabel("Select a brush in the navigation tree to edit metadata, variations, and runtime-facing identifiers.");
		return;
	}

	const wxString modifiedSuffix = dirty_ ? " [modified]" : "";
	const wxString displayName = hasBrush_ ? TrimmedValue(nameCtrl_) : "";
	const wxString type = GetEffectiveBrushType();
	titleLabel_->SetLabel("Editing brush: " + (displayName.IsEmpty() ? brushStorage_.brush.name : displayName) + modifiedSuffix);
	if (dirty_) {
		subtitleLabel_->SetLabel("Unsaved local edits differ from materials.db. Save to persist them or Revert to discard them before switching brushes.");
	} else if (type == "table") {
		subtitleLabel_->SetLabel("Ready to edit table states, seamless joins, and weighted state items for this SQLite-backed brush.");
	} else if (type == "carpet") {
		subtitleLabel_->SetLabel("Ready to edit carpet layout contexts and weighted variants for this SQLite-backed brush.");
	} else if (type == "doodad") {
		subtitleLabel_->SetLabel("Ready to edit doodad metadata, alternatives, and composites for this SQLite-backed brush.");
	} else if (type == "ground") {
		subtitleLabel_->SetLabel("Ready to edit weighted ground items for this SQLite-backed brush.");
	} else {
		subtitleLabel_->SetLabel("Ready to edit metadata and runtime-facing variation data for this SQLite-backed brush.");
	}
}

void MaterialsWorkbenchBrushPanel::UpdateActionButtons() {
	if (saveButton_) {
		saveButton_->Enable(hasBrush_ && dirty_);
	}
	if (revertButton_) {
		revertButton_->Enable(hasBrush_ && dirty_);
	}
}

void MaterialsWorkbenchBrushPanel::SetFieldsEnabled(bool enabled) {
	nameCtrl_->Enable(enabled);
	typeCtrl_->Enable(enabled);
	sourceCtrl_->Enable(enabled);
	lookIdCtrl_->Enable(enabled);
	serverLookIdCtrl_->Enable(enabled);
	zOrderCtrl_->Enable(enabled);
	thicknessCtrl_->Enable(enabled);
	thicknessCeilingCtrl_->Enable(enabled);
	draggableCtrl_->Enable(enabled);
	onBlockingCtrl_->Enable(enabled);
	onDuplicateCtrl_->Enable(enabled);
	redoBordersCtrl_->Enable(enabled);
	randomizeCtrl_->Enable(enabled);
	oneSizeCtrl_->Enable(enabled);
	soloOptionalCtrl_->Enable(enabled);
	if (workspaceTabs_) {
		workspaceTabs_->Enable(enabled);
	}
}

void MaterialsWorkbenchBrushPanel::ResetVariationSelection() {
	groundItemIndex_ = -1;
	alignedNodeIndex_ = -1;
	alignedItemIndex_ = -1;
	alignedPendingCarpetAlign_.clear();
	alignedPendingTableAlign_.clear();
	doodadAlternativeIndex_ = -1;
	doodadSingleItemIndex_ = -1;
	doodadCompositeIndex_ = -1;
	doodadTileIndex_ = -1;
	doodadTileItemIndex_ = -1;
	doodadPreviewFloor_ = MaterialsWorkbenchBrushPanel::kDoodadPreviewAllFloors;
	doodadPreviewPreferComposite_ = true;
	doodadPreviewAvailableFloors_.clear();
}

wxString MaterialsWorkbenchBrushPanel::GetVariationTabTitle() const {
	if (!hasBrush_) {
		return "Editor";
	}

	const wxString type = GetEffectiveBrushType();
	if (type == "table") {
		return "Table Editor";
	}
	if (type == "carpet") {
		return "Carpet Editor";
	}
	if (type == "doodad") {
		return "Doodad Editor";
	}
	if (type == "ground") {
		return "Ground Editor";
	}

	return "Editor";
}

wxString MaterialsWorkbenchBrushPanel::GetEffectiveBrushType() const {
	wxString type = hasBrush_ ? TrimmedValue(typeCtrl_) : "";
	if (type.IsEmpty()) {
		type = brushStorage_.brush.type;
	}
	type.MakeLower();
	return type;
}

bool MaterialsWorkbenchBrushPanel::UsesGroundVariationEditor() const {
	return GetEffectiveBrushType() == "ground";
}

bool MaterialsWorkbenchBrushPanel::UsesAlignedVariationEditor() const {
	const wxString type = GetEffectiveBrushType();
	return type == "carpet" || type == "table";
}

bool MaterialsWorkbenchBrushPanel::UsesDoodadVariationEditor() const {
	return GetEffectiveBrushType() == "doodad";
}

void MaterialsWorkbenchBrushPanel::RefreshVariationEditor() {
	if (!variationsBook_) {
		return;
	}

	if (!hasBrush_) {
		SetActiveAlignedEditorWidgets(nullptr);
		variationsEmptyLabel_->SetLabel("Select a brush to inspect variation data.");
		variationsBook_->SetSelection(0);
		return;
	}

	if (UsesGroundVariationEditor()) {
		SetActiveAlignedEditorWidgets(nullptr);
		variationsBook_->SetSelection(1);
		RefreshGroundItemList();
		RefreshGroundSelection();
		return;
	}

	if (UsesAlignedVariationEditor()) {
		const wxString type = GetEffectiveBrushType();
		SetActiveAlignedEditorWidgets(type == "table" ? &tableAlignedWidgets_ : &carpetAlignedWidgets_);
		if (type == "table" && alignedPendingTableAlign_.IsEmpty()) {
			alignedPendingTableAlign_ = FindNextMissingTableAlign(brushStorage_.tableNodes, "alone");
		}
		variationsBook_->SetSelection(type == "table" ? 3 : 2);
		RefreshAlignedNodeList();
		RefreshAlignedSelection();
		return;
	}

	if (UsesDoodadVariationEditor()) {
		SetActiveAlignedEditorWidgets(nullptr);
		variationsBook_->SetSelection(4);
		RefreshDoodadAlternativeList();
		RefreshDoodadSelection();
		return;
	}

	SetActiveAlignedEditorWidgets(nullptr);
	variationsEmptyLabel_->SetLabel(
		"Variations are not yet exposed for '" + GetEffectiveBrushType() + "' brushes in this first workbench pass."
	);
	variationsBook_->SetSelection(0);
}

void MaterialsWorkbenchBrushPanel::RefreshGroundItemList() {
	if (!groundItemsList_) {
		return;
	}

	const int topItem = CaptureListTopItem(groundItemsList_);
	groundItemsList_->Clear();
	for (size_t i = 0; i < brushStorage_.items.size(); ++i) {
		groundItemsList_->Append(FormatGroundItemLabel(brushStorage_.items[i].itemId, brushStorage_.items[i].chance, i));
	}

	if (brushStorage_.items.empty()) {
		groundItemIndex_ = -1;
	} else if (groundItemIndex_ < 0 || groundItemIndex_ >= static_cast<int>(brushStorage_.items.size())) {
		groundItemIndex_ = 0;
	}

	if (groundItemIndex_ >= 0) {
		groundItemsList_->SetSelection(groundItemIndex_);
	}
	RestoreListTopItem(groundItemsList_, topItem);

	if (groundItemsCardsPanel_) {
		const int padding = groundItemsCardsPanel_->FromDIP(8);
		const int gap = groundItemsCardsPanel_->FromDIP(8);
		const int cardHeight = groundItemsCardsPanel_->FromDIP(78);
		const int minHeight = brushStorage_.items.empty()
			? groundItemsCardsPanel_->FromDIP(160)
			: padding * 2 + static_cast<int>(brushStorage_.items.size()) * cardHeight + std::max(0, static_cast<int>(brushStorage_.items.size()) - 1) * gap;
		groundItemsCardsPanel_->SetMinSize(wxSize(groundItemsCardsPanel_->GetMinSize().x, minHeight));
		groundItemsCardsPanel_->GetContainingSizer()->Layout();
		if (groundItemsScroll_) {
			groundItemsScroll_->FitInside();
		}
		groundItemsCardsPanel_->Refresh();
	}
}

void MaterialsWorkbenchBrushPanel::RefreshGroundSelection() {
	internalUpdate_ = true;

	const bool hasItem = groundItemIndex_ >= 0 && groundItemIndex_ < static_cast<int>(brushStorage_.items.size());
	groundItemIdCtrl_->Enable(hasItem);
	groundItemChanceCtrl_->Enable(hasItem);

	if (hasItem) {
		groundItemIdCtrl_->SetValue(brushStorage_.items[groundItemIndex_].itemId);
		groundItemChanceCtrl_->SetValue(brushStorage_.items[groundItemIndex_].chance);
	} else {
		groundItemIdCtrl_->SetValue(0);
		groundItemChanceCtrl_->SetValue(0);
	}

	internalUpdate_ = false;
	UpdateItemOwnershipHint(groundItemOwnershipLabel_, groundItemIdCtrl_->GetValue(), hasItem);
	RefreshGroundPreviewState();
}

void MaterialsWorkbenchBrushPanel::RefreshGroundPreviewState() {
	const bool hasItems = !brushStorage_.items.empty();
	if (groundPreviewInfoLabel_) {
		if (hasItems && groundItemIndex_ >= 0 && groundItemIndex_ < static_cast<int>(brushStorage_.items.size())) {
			const BrushItemRecord &item = brushStorage_.items[static_cast<size_t>(groundItemIndex_)];
			groundPreviewInfoLabel_->SetLabel(
				wxString::Format(
					"Selected variant %d | item %d | chance %d | %s | badge %s",
					groundItemIndex_ + 1,
					item.itemId,
					item.chance,
					FormatWeightedBrushPercent(brushStorage_.items, static_cast<size_t>(groundItemIndex_)),
					GetWeightedBrushBadge(brushStorage_.items, static_cast<size_t>(groundItemIndex_))
				)
			);
		} else {
			groundPreviewInfoLabel_->SetLabel("Ground variants now use visual cards with weighted badges. The preview grid shows all current variations together.");
		}
		groundPreviewInfoLabel_->Wrap(groundPreviewInfoLabel_->GetParent()->FromDIP(520));
	}

	if (groundPreviewHighlightCtrl_) {
		groundPreviewHighlightCtrl_->Enable(hasItems);
	}

	if (groundPreviewDistributionLabel_) {
		if (hasItems) {
			groundPreviewDistributionLabel_->SetLabel(
				wxString::Format(
					"Distribution: %zu variants | total chance %d | hover for id and percentage",
					brushStorage_.items.size(),
					SumWeightedBrushChances(brushStorage_.items)
				)
			);
		} else {
			groundPreviewDistributionLabel_->SetLabel("Distribution: no weighted variants yet.");
		}
	}

	RefreshGroundPreviewLayout();
	if (groundPreviewPanel_) {
		groundPreviewPanel_->Refresh();
	}
	if (groundItemsCardsPanel_) {
		groundItemsCardsPanel_->Refresh();
	}
}

void MaterialsWorkbenchBrushPanel::RefreshGroundPreviewLayout() {
	if (!groundPreviewScroll_ || !groundPreviewPanel_) {
		return;
	}

	const wxSize clientSize = groundPreviewScroll_->GetClientSize();
	const int tileCell = groundPreviewPanel_->FromDIP(32);
	const int columns = std::max(1, clientSize.x / std::max(1, tileCell));
	const int rows = brushStorage_.items.empty()
		? 1
		: (static_cast<int>(brushStorage_.items.size()) + columns - 1) / columns;
	const int contentWidth = std::max(clientSize.x, columns * tileCell);
	const int contentHeight = std::max(clientSize.y, rows * tileCell);

	groundPreviewPanel_->SetMinSize(wxSize(contentWidth, contentHeight));
	if (groundPreviewScroll_->GetSizer()) {
		groundPreviewScroll_->GetSizer()->Layout();
	}
	groundPreviewScroll_->FitInside();
}

void MaterialsWorkbenchBrushPanel::RefreshAlignedNodeList() {
	if (!alignedNodesList_) {
		return;
	}

	const int topItem = CaptureListTopItem(alignedNodesList_);
	alignedNodesList_->Clear();
	if (GetEffectiveBrushType() == "carpet") {
		const auto &nodes = brushStorage_.carpetNodes;
		for (size_t i = 0; i < nodes.size(); ++i) {
			alignedNodesList_->Append(FormatAlignedNodeLabel(nodes[i].align, nodes[i].items.size(), i));
		}

		if (nodes.empty()) {
			alignedNodeIndex_ = -1;
		} else if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			alignedNodeIndex_ = 0;
		}
	} else {
		const auto &nodes = brushStorage_.tableNodes;
		for (size_t i = 0; i < nodes.size(); ++i) {
			alignedNodesList_->Append(FormatAlignedNodeLabel(nodes[i].align, nodes[i].items.size(), i));
		}

		if (nodes.empty()) {
			alignedNodeIndex_ = -1;
		} else if (alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			alignedNodeIndex_ = static_cast<int>(nodes.size()) - 1;
		} else if (alignedNodeIndex_ < 0 && alignedPendingTableAlign_.IsEmpty()) {
			alignedNodeIndex_ = 0;
		}
	}

	if (alignedNodeIndex_ >= 0) {
		alignedNodesList_->SetSelection(alignedNodeIndex_);
	}
	RestoreListTopItem(alignedNodesList_, topItem);
}

void MaterialsWorkbenchBrushPanel::RefreshAlignedItemList() {
	if (!alignedItemsList_) {
		return;
	}

	const int topItem = CaptureListTopItem(alignedItemsList_);
	alignedItemsList_->Clear();
	if (GetEffectiveBrushType() == "carpet") {
		const auto &nodes = brushStorage_.carpetNodes;
		if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			alignedItemIndex_ = -1;
		} else {
			const auto &items = nodes[alignedNodeIndex_].items;
			for (size_t i = 0; i < items.size(); ++i) {
				alignedItemsList_->Append(FormatAlignedItemLabel(items[i].itemId, items[i].chance, i));
			}

			if (items.empty()) {
				alignedItemIndex_ = -1;
			} else if (alignedItemIndex_ < 0 || alignedItemIndex_ >= static_cast<int>(items.size())) {
				alignedItemIndex_ = 0;
			}
		}
	} else {
		const auto &nodes = brushStorage_.tableNodes;
		if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			alignedItemIndex_ = -1;
		} else {
			const auto &items = nodes[alignedNodeIndex_].items;
			for (size_t i = 0; i < items.size(); ++i) {
				alignedItemsList_->Append(FormatAlignedItemLabel(items[i].itemId, items[i].chance, i));
			}

			if (items.empty()) {
				alignedItemIndex_ = -1;
			} else if (alignedItemIndex_ < 0 || alignedItemIndex_ >= static_cast<int>(items.size())) {
				alignedItemIndex_ = 0;
			}
		}
	}

	if (alignedItemIndex_ >= 0) {
		alignedItemsList_->SetSelection(alignedItemIndex_);
	}
	RestoreListTopItem(alignedItemsList_, topItem);

	if (alignedItemsCardsPanel_) {
		const size_t itemCount = alignedItemsList_->GetCount();
		const int padding = alignedItemsCardsPanel_->FromDIP(8);
		const int gap = alignedItemsCardsPanel_->FromDIP(8);
		const int cardHeight = alignedItemsCardsPanel_->FromDIP(78);
		const int minHeight = itemCount == 0
			? alignedItemsCardsPanel_->FromDIP(160)
			: padding * 2 + static_cast<int>(itemCount) * cardHeight + std::max(0, static_cast<int>(itemCount) - 1) * gap;
		alignedItemsCardsPanel_->SetMinSize(wxSize(alignedItemsCardsPanel_->GetMinSize().x, minHeight));
		if (alignedItemsCardsPanel_->GetContainingSizer()) {
			alignedItemsCardsPanel_->GetContainingSizer()->Layout();
		}
		if (alignedItemsScroll_) {
			alignedItemsScroll_->FitInside();
		}
		alignedItemsCardsPanel_->Refresh();
	}
}

void MaterialsWorkbenchBrushPanel::RefreshAlignedSelection() {
	internalUpdate_ = true;
	const wxString type = GetEffectiveBrushType();
	bool hasNode = false;
	if (type == "carpet") {
		const auto &nodes = brushStorage_.carpetNodes;
		hasNode = alignedNodeIndex_ >= 0 && alignedNodeIndex_ < static_cast<int>(nodes.size());
		const wxString selectedAlign = hasNode
			? nodes[alignedNodeIndex_].align
			: (!alignedPendingCarpetAlign_.IsEmpty() ? alignedPendingCarpetAlign_ : FindNextMissingCarpetAlign(nodes, "center"));
		alignedPendingCarpetAlign_ = selectedAlign;
		alignedNodeAlignCtrl_->Show();
		if (alignedNodeAlignChoice_) {
			alignedNodeAlignChoice_->Hide();
		}
		alignedNodeAlignCtrl_->Enable(!selectedAlign.IsEmpty());
		if (!selectedAlign.IsEmpty()) {
			alignedNodeAlignCtrl_->SetValue(selectedAlign);
		} else {
			alignedNodeAlignCtrl_->SetValue("");
		}
		if (!hasNode) {
			alignedItemIndex_ = -1;
		}
	} else {
		const auto &nodes = brushStorage_.tableNodes;
		hasNode = alignedNodeIndex_ >= 0 && alignedNodeIndex_ < static_cast<int>(nodes.size());
		if (alignedNodeAlignCtrl_) {
			alignedNodeAlignCtrl_->Hide();
		}
		if (alignedNodeAlignChoice_) {
			alignedNodeAlignChoice_->Show();
		}
		const wxString selectedAlign = hasNode
			? nodes[alignedNodeIndex_].align
			: (!alignedPendingTableAlign_.IsEmpty() ? alignedPendingTableAlign_ : FindNextMissingTableAlign(nodes, "alone"));
		alignedPendingTableAlign_ = selectedAlign;
		alignedNodeAlignChoice_->Enable(!selectedAlign.IsEmpty());
		const int selectedChoice = alignedNodeAlignChoice_->FindString(selectedAlign);
		if (selectedChoice != wxNOT_FOUND) {
			alignedNodeAlignChoice_->SetSelection(selectedChoice);
		} else {
			alignedNodeAlignChoice_->SetSelection(wxNOT_FOUND);
		}
		if (hasNode) {
		} else {
			alignedItemIndex_ = -1;
		}
	}
	Layout();

	RefreshAlignedItemList();

	bool hasItem = false;
	if (hasNode) {
		if (GetEffectiveBrushType() == "carpet") {
			const auto &items = brushStorage_.carpetNodes[alignedNodeIndex_].items;
			hasItem = alignedItemIndex_ >= 0 && alignedItemIndex_ < static_cast<int>(items.size());
			if (hasItem) {
				alignedItemIdCtrl_->SetValue(items[alignedItemIndex_].itemId);
				alignedItemChanceCtrl_->SetValue(items[alignedItemIndex_].chance);
			}
		} else {
			const auto &items = brushStorage_.tableNodes[alignedNodeIndex_].items;
			hasItem = alignedItemIndex_ >= 0 && alignedItemIndex_ < static_cast<int>(items.size());
			if (hasItem) {
				alignedItemIdCtrl_->SetValue(items[alignedItemIndex_].itemId);
				alignedItemChanceCtrl_->SetValue(items[alignedItemIndex_].chance);
			}
		}
	}
	if (!hasItem) {
		alignedItemIdCtrl_->SetValue(0);
		alignedItemChanceCtrl_->SetValue(0);
	}
	alignedItemIdCtrl_->Enable(hasItem);
	alignedItemChanceCtrl_->Enable(hasItem);
	internalUpdate_ = false;
	UpdateItemOwnershipHint(alignedItemOwnershipLabel_, alignedItemIdCtrl_->GetValue(), hasItem);
	RefreshAlignedVisualState();
}

void MaterialsWorkbenchBrushPanel::RefreshAlignedVisualState() {
	if (!UsesAlignedVariationEditor()) {
		return;
	}

	const wxString type = GetEffectiveBrushType();
	const bool hasNode = alignedNodeIndex_ >= 0;
	const bool hasPendingCarpetSlot = type == "carpet" && !alignedPendingCarpetAlign_.IsEmpty() && !hasNode;
	const bool hasPendingTableSlot = type == "table" && !alignedPendingTableAlign_.IsEmpty() && !hasNode;
	if (alignedVisualInfoLabel_) {
		if (type == "table") {
			alignedVisualInfoLabel_->SetLabel(
				hasNode
					? "Selected state stays locked on the left. Manage its weighted items on the right, and use + inside the state to add more."
					: (hasPendingTableSlot
						? "Empty state selected. Use Add Node to create this missing state."
						: "Select a state to inspect it. Missing states stay selectable so Add Node knows exactly where to create the next one.")
			);
		} else {
			alignedVisualInfoLabel_->SetLabel(
				hasNode
					? wxString::Format("Selected carpet context %s stays highlighted in the layout map while you edit its variants on the right.", alignedPendingCarpetAlign_)
					: (hasPendingCarpetSlot
						? wxString::Format("Empty carpet slot %s selected. Use Add Context to create coverage exactly there.", alignedPendingCarpetAlign_)
						: "Click the carpet layout map to select a context. Empty slots stay visible so missing coverage stands out immediately.")
			);
		}
		alignedVisualInfoLabel_->Wrap(FromDIP(250));
	}

	if (alignedAdvancedInfoLabel_) {
		if (type == "table") {
			if (hasNode && alignedNodeIndex_ < static_cast<int>(brushStorage_.tableNodes.size())) {
				const auto &node = brushStorage_.tableNodes[alignedNodeIndex_];
				alignedAdvancedInfoLabel_->SetLabel(
					wxString::Format(
						"Advanced: state %s selected. Align uses guided options, and weighted items stay editable below.",
						node.align
					)
				);
			} else if (hasPendingTableSlot) {
				alignedAdvancedInfoLabel_->SetLabel(
					wxString::Format(
						"Advanced: empty state %s selected. Add Node creates one new state here with default chance %d.",
						alignedPendingTableAlign_,
						kDefaultNewTableNodeChance
					)
				);
			} else {
				alignedAdvancedInfoLabel_->SetLabel("Advanced: choose a table state to edit it, or select an empty slot to place the next node precisely.");
			}
		} else {
			if (hasNode) {
				alignedAdvancedInfoLabel_->SetLabel(
					wxString::Format(
						"Context %s selected. The map owns slot selection; use the fields below to edit the active variant.",
						alignedPendingCarpetAlign_
					)
				);
			} else if (hasPendingCarpetSlot) {
				alignedAdvancedInfoLabel_->SetLabel(
					wxString::Format(
						"Empty carpet slot %s selected. Add Context creates a new context here, then Add Variant fills it with weighted items.",
						alignedPendingCarpetAlign_
					)
				);
			} else {
				alignedAdvancedInfoLabel_->SetLabel("Select a carpet slot in the layout map to inspect it, or choose an empty slot before creating a new context.");
			}
		}
		alignedAdvancedInfoLabel_->Wrap(FromDIP(520));
	}

	if (alignedAddNodeButton_) {
		if (type == "table") {
			const wxString nextMissingAlign = FindNextMissingTableAlign(brushStorage_.tableNodes, alignedPendingTableAlign_);
			alignedAddNodeButton_->Enable(!nextMissingAlign.IsEmpty());
			alignedAddNodeButton_->SetToolTip(
				nextMissingAlign.IsEmpty()
					? "All table states are already configured."
					: "Create the missing state selected in Table States."
			);
		} else {
			const wxString nextMissingAlign = FindNextMissingCarpetAlign(brushStorage_.carpetNodes, alignedPendingCarpetAlign_);
			alignedAddNodeButton_->Enable(!nextMissingAlign.IsEmpty());
			alignedAddNodeButton_->SetToolTip(
				nextMissingAlign.IsEmpty()
					? "All carpet contexts are already configured."
					: wxString::Format("Create the selected carpet context in slot %s.", nextMissingAlign)
			);
		}
	}

	if (alignedAdvancedPanel_) {
		alignedAdvancedPanel_->Show(type != "table");
	}
	if (alignedAddItemButton_) {
		alignedAddItemButton_->Show(type != "table");
	}
	if (alignedRemoveItemButton_) {
		alignedRemoveItemButton_->Show(type != "table");
		alignedRemoveItemButton_->SetLabel(type == "table" ? "Remove" : "Remove Variant");
	}

	if (alignedItemsSummaryLabel_) {
		if (type == "carpet") {
			if (alignedNodeIndex_ >= 0 && alignedNodeIndex_ < static_cast<int>(brushStorage_.carpetNodes.size())) {
				const auto &node = brushStorage_.carpetNodes[alignedNodeIndex_];
				alignedItemsSummaryLabel_->SetLabel(
					wxString::Format(
						"Context %s | %zu variant%s | selected from the carpet layout map",
						node.align,
						node.items.size(),
						node.items.size() == 1 ? "" : "s"
					)
				);
			} else if (hasPendingCarpetSlot) {
				alignedItemsSummaryLabel_->SetLabel(
					wxString::Format(
						"Empty context %s selected | use Add Context to create it in the layout map",
						alignedPendingCarpetAlign_
					)
				);
			} else {
				alignedItemsSummaryLabel_->SetLabel("Select a carpet context in the layout map to inspect its weighted variants.");
			}
		} else {
			if (alignedNodeIndex_ >= 0 && alignedNodeIndex_ < static_cast<int>(brushStorage_.tableNodes.size())) {
				const auto &node = brushStorage_.tableNodes[alignedNodeIndex_];
				alignedItemsSummaryLabel_->SetLabel(
					wxString::Format(
						"State %s | %zu item%s | manage this state",
						node.align,
						node.items.size(),
						node.items.size() == 1 ? "" : "s"
					)
				);
			} else if (hasPendingTableSlot) {
				alignedItemsSummaryLabel_->SetLabel(
					wxString::Format(
						"Empty state %s selected | use Add Node to create it",
						alignedPendingTableAlign_
					)
				);
			} else {
				alignedItemsSummaryLabel_->SetLabel("Select a state to inspect and manage its weighted items.");
			}
		}
		alignedItemsSummaryLabel_->Wrap(FromDIP(520));
	}

	if (alignedSeamlessPreviewInfoLabel_) {
		if (type == "table") {
			alignedSeamlessPreviewInfoLabel_->Hide();
		} else if (type == "carpet") {
			if (alignedNodeIndex_ >= 0 && alignedNodeIndex_ < static_cast<int>(brushStorage_.carpetNodes.size())) {
				const auto &node = brushStorage_.carpetNodes[alignedNodeIndex_];
				const bool hasSelectedVariant = alignedItemIndex_ >= 0 && alignedItemIndex_ < static_cast<int>(node.items.size());
				alignedSeamlessPreviewInfoLabel_->SetLabel(
					hasSelectedVariant
						? wxString::Format(
							"Seamless preview follows slot %s and the active variant item %d inside one continuous carpet composition.",
							node.align,
							node.items[alignedItemIndex_].itemId
						)
						: wxString::Format(
							"Seamless preview shows the full carpet composition around slot %s using the first available variant in each configured context.",
							node.align
						)
				);
			} else if (hasPendingCarpetSlot) {
				alignedSeamlessPreviewInfoLabel_->SetLabel(
					wxString::Format(
						"Seamless preview reserves slot %s in the carpet composition so you can see where the next context will land before creating it.",
						alignedPendingCarpetAlign_
					)
				);
			} else {
				alignedSeamlessPreviewInfoLabel_->SetLabel("Seamless preview shows the carpet as one continuous composition using the contexts currently configured in the map.");
			}
			alignedSeamlessPreviewInfoLabel_->Wrap(FromDIP(520));
			alignedSeamlessPreviewInfoLabel_->Show();
		} else {
			alignedSeamlessPreviewInfoLabel_->Hide();
		}
	}

	RefreshAlignedSeamlessPreview();

	if (alignedContextPanel_) {
		alignedContextPanel_->Refresh();
	}
	if (alignedItemsCardsPanel_) {
		alignedItemsCardsPanel_->Refresh();
	}
}

bool MaterialsWorkbenchBrushPanel::ShowTableItemDialog(const wxString &title, int &itemId, int &chance) {
	return ShowWeightedBrushItemDialogWithPreview(this, title, itemId, chance);
}

void MaterialsWorkbenchBrushPanel::AddTableItemToNodeWithDialog(int nodeIndex) {
	if (!hasBrush_ || nodeIndex < 0 || nodeIndex >= static_cast<int>(brushStorage_.tableNodes.size())) {
		return;
	}

	int itemId = 0;
	int chance = kDefaultNewTableNodeChance;
	if (!ShowTableItemDialog("Add Table Item", itemId, chance)) {
		return;
	}

	auto &items = brushStorage_.tableNodes[nodeIndex].items;
	items.push_back({itemId, chance});
	alignedNodeIndex_ = nodeIndex;
	alignedItemIndex_ = static_cast<int>(items.size()) - 1;
	alignedPendingTableAlign_ = brushStorage_.tableNodes[nodeIndex].align;
	RefreshAlignedSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage(wxString::Format("Added item %d to table state %s.", itemId, brushStorage_.tableNodes[nodeIndex].align));
}

void MaterialsWorkbenchBrushPanel::EditTableItemWithDialog(int nodeIndex, int itemIndex) {
	if (!hasBrush_ || nodeIndex < 0 || nodeIndex >= static_cast<int>(brushStorage_.tableNodes.size())) {
		return;
	}
	auto &items = brushStorage_.tableNodes[nodeIndex].items;
	if (itemIndex < 0 || itemIndex >= static_cast<int>(items.size())) {
		return;
	}

	int itemId = items[itemIndex].itemId;
	int chance = items[itemIndex].chance;
	if (!ShowTableItemDialog("Edit Table Item", itemId, chance)) {
		return;
	}

	items[itemIndex].itemId = itemId;
	items[itemIndex].chance = chance;
	alignedNodeIndex_ = nodeIndex;
	alignedItemIndex_ = itemIndex;
	alignedPendingTableAlign_ = brushStorage_.tableNodes[nodeIndex].align;
	RefreshAlignedSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage(wxString::Format("Updated table item %d.", itemId));
}

void MaterialsWorkbenchBrushPanel::RefreshAlignedSeamlessPreview() {
	const wxString type = GetEffectiveBrushType();
	const bool showAlignedPreview = UsesAlignedVariationEditor() && (type == "table" || type == "carpet");
	if (alignedSeamlessPreviewPanel_) {
		alignedSeamlessPreviewPanel_->Show(showAlignedPreview);
		if (showAlignedPreview) {
			alignedSeamlessPreviewPanel_->Refresh();
		}
		if (wxWindow* parent = alignedSeamlessPreviewPanel_->GetParent()) {
			parent->Layout();
		}
	}
	Layout();
}

void MaterialsWorkbenchBrushPanel::OnAlignedSeamlessPreviewPaint(wxPaintEvent &WXUNUSED(event)) {
	if (!alignedSeamlessPreviewPanel_) {
		return;
	}

	wxAutoBufferedPaintDC dc(alignedSeamlessPreviewPanel_);
	const wxRect clientRect = alignedSeamlessPreviewPanel_->GetClientRect();
	dc.SetBackground(wxBrush(wxColour(20, 24, 32)));
	dc.Clear();

	if (!hasBrush_ || (GetEffectiveBrushType() != "table" && GetEffectiveBrushType() != "carpet")) {
		return;
	}

	if (GetEffectiveBrushType() == "carpet") {
		enum class CarpetSeamlessCollapseMode {
			None,
			MiddleColumn,
			MiddleRow,
		};

		struct CarpetPreviewTile {
			wxString align;
			wxString label;
			bool exists = false;
			bool selected = false;
			int itemId = 0;
			wxPoint tileAnchor;
			wxRect spriteRect;
			wxBitmap bitmap;
			wxRect visibleBounds;

			bool hasSprite() const {
				return bitmap.IsOk() && visibleBounds.width > 0 && visibleBounds.height > 0;
			}
		};

		struct CarpetPreviewMeasure {
			wxRect tileUnion;
			wxRect spriteUnion;
			wxRect visibleUnion;
			bool hasTileUnion = false;
			bool hasSpriteUnion = false;
			bool hasVisibleUnion = false;
		};

		const wxColour laneFill(24, 28, 36);
		const int padding = alignedSeamlessPreviewPanel_->FromDIP(12);
		const int tileCell = alignedSeamlessPreviewPanel_->FromDIP(32);
		wxRect innerRect = clientRect;
		innerRect.Deflate(padding, padding);
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(laneFill));
		dc.DrawRoundedRectangle(innerRect, alignedSeamlessPreviewPanel_->FromDIP(8));

		const wxString selectedAlign = !alignedPendingCarpetAlign_.IsEmpty()
			? alignedPendingCarpetAlign_
			: (alignedNodeIndex_ >= 0 && alignedNodeIndex_ < static_cast<int>(brushStorage_.carpetNodes.size())
				? brushStorage_.carpetNodes[alignedNodeIndex_].align
				: wxString("center"));
		const auto hasCarpetNode = [&](const wxString &align) {
			return FindAlignedNodeIndexByAlign(brushStorage_.carpetNodes, align) >= 0;
		};
		const auto matchesMissingPair = [&](const std::vector<wxString> &missingAligns) {
			for (const auto &slot : GetCarpetContextSlots()) {
				const wxString align = wxString::FromUTF8(slot.align);
				const bool shouldBeMissing = std::find_if(
					missingAligns.begin(),
					missingAligns.end(),
					[&](const wxString &candidate) { return candidate.CmpNoCase(align) == 0; }
				) != missingAligns.end();
				if (shouldBeMissing) {
					if (hasCarpetNode(align)) {
						return false;
					}
				} else if (!hasCarpetNode(align)) {
					return false;
				}
			}
			return true;
		};
		const CarpetSeamlessCollapseMode collapseMode = matchesMissingPair({"n", "s"})
			? CarpetSeamlessCollapseMode::MiddleColumn
			: (matchesMissingPair({"e", "w"})
				? CarpetSeamlessCollapseMode::MiddleRow
				: CarpetSeamlessCollapseMode::None);
		const auto resolvePreviewItemId = [&](const wxString &align) -> int {
			const int nodeIndex = FindAlignedNodeIndexByAlign(brushStorage_.carpetNodes, align);
			if (nodeIndex < 0 || nodeIndex >= static_cast<int>(brushStorage_.carpetNodes.size())) {
				return 0;
			}
			const auto &node = brushStorage_.carpetNodes[static_cast<size_t>(nodeIndex)];
			if (selectedAlign.CmpNoCase(align) == 0 &&
				alignedItemIndex_ >= 0 &&
				alignedItemIndex_ < static_cast<int>(node.items.size()) &&
				node.items[alignedItemIndex_].itemId > 0) {
				return node.items[alignedItemIndex_].itemId;
			}
			for (const auto &item : node.items) {
				if (item.itemId > 0) {
					return item.itemId;
				}
			}
			return node.items.empty() ? 0 : node.items.front().itemId;
		};

		std::vector<CarpetPreviewTile> tiles;
		tiles.reserve(GetCarpetContextSlots().size());
		for (const auto &slot : GetCarpetContextSlots()) {
			if ((collapseMode == CarpetSeamlessCollapseMode::MiddleColumn && slot.column == 1) ||
				(collapseMode == CarpetSeamlessCollapseMode::MiddleRow && slot.row == 1)) {
				continue;
			}
			const wxString align = wxString::FromUTF8(slot.align);
			const int nodeIndex = FindAlignedNodeIndexByAlign(brushStorage_.carpetNodes, align);
			CarpetPreviewTile tile;
			tile.align = align;
			tile.label = wxString::FromUTF8(slot.label);
			tile.exists = nodeIndex >= 0;
			tile.selected = selectedAlign.CmpNoCase(align) == 0;
			tile.itemId = tile.exists ? resolvePreviewItemId(align) : 0;
			const int collapsedColumn = collapseMode == CarpetSeamlessCollapseMode::MiddleColumn && slot.column > 1
				? slot.column - 1
				: slot.column;
			const int collapsedRow = collapseMode == CarpetSeamlessCollapseMode::MiddleRow && slot.row > 1
				? slot.row - 1
				: slot.row;
			tile.tileAnchor = wxPoint(collapsedColumn * tileCell, collapsedRow * tileCell);
			tile.spriteRect = GetDoodadPreviewSpriteRect(tile.itemId, tile.tileAnchor);
			if (tile.itemId > 0) {
				const DoodadPreviewSpriteMetrics metrics = ResolveDoodadPreviewSpriteMetrics(tile.itemId);
				if (metrics.isValid()) {
					tile.bitmap = BuildDoodadPreviewBitmap(metrics.spriteId);
					if (tile.bitmap.IsOk()) {
						tile.visibleBounds = GetBitmapVisibleBounds(tile.bitmap);
					}
				}
			}
			tiles.push_back(tile);
		}

		CarpetPreviewMeasure measure;
		for (const CarpetPreviewTile &tile : tiles) {
			const wxRect tileRect(tile.tileAnchor.x, tile.tileAnchor.y, tileCell, tileCell);
			if (!measure.hasTileUnion) {
				measure.tileUnion = tileRect;
				measure.hasTileUnion = true;
			} else {
				measure.tileUnion.Union(tileRect);
			}
			if (tile.hasSprite()) {
				const wxRect visibleRect(
					tile.spriteRect.x + tile.visibleBounds.x,
					tile.spriteRect.y + tile.visibleBounds.y,
					tile.visibleBounds.width,
					tile.visibleBounds.height
				);
				if (!measure.hasSpriteUnion) {
					measure.spriteUnion = tile.spriteRect;
					measure.hasSpriteUnion = true;
				} else {
					measure.spriteUnion.Union(tile.spriteRect);
				}
				if (!measure.hasVisibleUnion) {
					measure.visibleUnion = visibleRect;
					measure.hasVisibleUnion = true;
				} else {
					measure.visibleUnion.Union(visibleRect);
				}
			}
		}
		if (!measure.hasTileUnion) {
			return;
		}
		if (!measure.hasSpriteUnion) {
			measure.spriteUnion = measure.tileUnion;
		}
		if (!measure.hasVisibleUnion) {
			measure.visibleUnion = measure.tileUnion;
		}

		const wxRect focusUnion = measure.hasVisibleUnion ? measure.visibleUnion : measure.tileUnion;
		const wxPoint originOffset(
			innerRect.x + std::max(0, (innerRect.width - focusUnion.width) / 2) - focusUnion.x,
			innerRect.y + std::max(0, (innerRect.height - focusUnion.height) / 2) - focusUnion.y
		);

		for (const CarpetPreviewTile &tile : tiles) {
			if (!tile.hasSprite()) {
				continue;
			}
			dc.DrawBitmap(tile.bitmap, tile.spriteRect.x + originOffset.x, tile.spriteRect.y + originOffset.y, true);
		}
		return;
	}

	const auto resolvePreviewItemId = [&](const wxString &align) -> int {
		const int nodeIndex = FindAlignedNodeIndexByAlign(brushStorage_.tableNodes, align);
		if (nodeIndex < 0 || nodeIndex >= static_cast<int>(brushStorage_.tableNodes.size())) {
			return 0;
		}
		const auto &items = brushStorage_.tableNodes[static_cast<size_t>(nodeIndex)].items;
		for (const auto &item : items) {
			if (item.itemId > 0) {
				return item.itemId;
			}
		}
		return items.empty() ? 0 : items.front().itemId;
	};

	const auto hasPreviewNode = [&](const wxString &align) -> bool {
		return FindAlignedNodeIndexByAlign(brushStorage_.tableNodes, align) >= 0;
	};

	const wxColour frameColour(74, 82, 96);
	const int outerPadding = alignedSeamlessPreviewPanel_->FromDIP(12);
	const int boxGap = alignedSeamlessPreviewPanel_->FromDIP(12);
	wxRect contentRect = clientRect;
	contentRect.Deflate(outerPadding, outerPadding);
	const int boxWidth = std::max(alignedSeamlessPreviewPanel_->FromDIP(160), (contentRect.width - boxGap) / 2);
	const wxRect verticalRect(contentRect.x, contentRect.y, boxWidth, contentRect.height);
	const wxRect horizontalRect(contentRect.x + boxWidth + boxGap, contentRect.y, std::max(0, contentRect.width - boxWidth - boxGap), contentRect.height);

	const auto drawPreviewBox = [&](const wxRect &boxRect, const std::vector<wxString> &aligns, bool vertical) {
		struct PreviewSegment {
			wxString align;
			int itemId = 0;
			wxPoint tileAnchor;
			wxRect spriteRect;
			wxBitmap bitmap;
			wxRect visibleBounds;

			bool hasSprite() const {
				return bitmap.IsOk() && visibleBounds.width > 0 && visibleBounds.height > 0;
			}
		};

		struct PreviewMeasure {
			wxRect spriteUnion;
			wxRect visibleUnion;
			wxRect anchorUnion;
			bool hasSpriteUnion = false;
			bool hasVisibleUnion = false;
		};

		dc.SetPen(wxPen(frameColour, 1));
		dc.SetBrush(wxBrush(wxColour(24, 28, 36)));
		dc.DrawRoundedRectangle(boxRect, alignedSeamlessPreviewPanel_->FromDIP(8));

		wxRect innerRect = boxRect;
		innerRect.Deflate(alignedSeamlessPreviewPanel_->FromDIP(10), alignedSeamlessPreviewPanel_->FromDIP(10));

		const int tileCell = alignedSeamlessPreviewPanel_->FromDIP(32);
		const int indicatorSpace = alignedSeamlessPreviewPanel_->FromDIP(20);
		const int capSize = alignedSeamlessPreviewPanel_->FromDIP(6);
		const wxColour lineColour(88, 96, 110);
		const wxColour capColour(74, 82, 96);
		wxRect laneRect = innerRect;
		if (vertical) {
			laneRect.SetWidth(std::max(tileCell * 2, innerRect.width - indicatorSpace));
		} else {
			laneRect.SetHeight(std::max(tileCell * 2, innerRect.height - indicatorSpace));
		}

		std::vector<PreviewSegment> segments;
		segments.reserve(aligns.size());
		for (const wxString &align : aligns) {
			if (!hasPreviewNode(align)) {
				continue;
			}
			PreviewSegment segment;
			segment.align = align;
			segment.itemId = resolvePreviewItemId(align);
			if (segment.itemId > 0) {
				const DoodadPreviewSpriteMetrics metrics = ResolveDoodadPreviewSpriteMetrics(segment.itemId);
				if (metrics.isValid()) {
					segment.bitmap = BuildDoodadPreviewBitmap(metrics.spriteId);
					if (segment.bitmap.IsOk()) {
						segment.visibleBounds = GetBitmapVisibleBounds(segment.bitmap);
					}
				}
			}
			segments.push_back(segment);
		}

		if (segments.empty()) {
			return;
		}

		for (size_t i = 0; i < segments.size(); ++i) {
			segments[i].tileAnchor = vertical
				? wxPoint(0, static_cast<int>(i) * tileCell)
				: wxPoint(static_cast<int>(i) * tileCell, 0);
			segments[i].spriteRect = GetDoodadPreviewSpriteRect(segments[i].itemId, segments[i].tileAnchor);
		}

		PreviewMeasure measure;
		for (const PreviewSegment &segment : segments) {
			const wxRect anchorRect(segment.tileAnchor.x, segment.tileAnchor.y, tileCell, tileCell);
			if (measure.anchorUnion.IsEmpty()) {
				measure.anchorUnion = anchorRect;
			} else {
				measure.anchorUnion.Union(anchorRect);
			}

			if (segment.hasSprite()) {
				const wxRect visibleRect(
					segment.spriteRect.x + segment.visibleBounds.x,
					segment.spriteRect.y + segment.visibleBounds.y,
					segment.visibleBounds.width,
					segment.visibleBounds.height
				);
				if (!measure.hasSpriteUnion) {
					measure.spriteUnion = segment.spriteRect;
					measure.hasSpriteUnion = true;
				} else {
					measure.spriteUnion.Union(segment.spriteRect);
				}
				if (!measure.hasVisibleUnion) {
					measure.visibleUnion = visibleRect;
					measure.hasVisibleUnion = true;
				} else {
					measure.visibleUnion.Union(visibleRect);
				}
			}
		}
		if (!measure.hasSpriteUnion) {
			measure.spriteUnion = measure.anchorUnion;
		}
		if (!measure.hasVisibleUnion) {
			measure.visibleUnion = measure.anchorUnion;
		}

		int originOffsetX = (vertical ? innerRect.x : laneRect.x) +
			std::max(0, ((vertical ? innerRect.width : laneRect.width) - measure.visibleUnion.width) / 2) -
			measure.visibleUnion.x;
		const int originOffsetY = laneRect.y + std::max(0, (laneRect.height - measure.visibleUnion.height) / 2) - measure.visibleUnion.y;
		if (vertical) {
			const int railX = measure.anchorUnion.GetRight() + originOffsetX + indicatorSpace / 2;
			const int railMinX = railX - capSize / 2;
			const int railMaxX = railX + capSize / 2;
			if (railMaxX > innerRect.GetRight()) {
				originOffsetX -= railMaxX - innerRect.GetRight();
			}
			if (railMinX < innerRect.x) {
				originOffsetX += innerRect.x - railMinX;
			}
		}
		const wxPoint originOffset(originOffsetX, originOffsetY);

		for (const PreviewSegment &segment : segments) {
			if (!segment.hasSprite()) {
				continue;
			}
			dc.DrawBitmap(segment.bitmap, segment.spriteRect.x + originOffset.x, segment.spriteRect.y + originOffset.y, true);
		}

		auto drawCap = [&](const wxPoint &center) {
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.SetBrush(wxBrush(capColour));
			dc.DrawRoundedRectangle(
				wxRect(center.x - capSize / 2, center.y - capSize / 2, capSize, capSize),
				alignedSeamlessPreviewPanel_->FromDIP(2)
			);
			dc.SetPen(wxPen(lineColour, 2));
		};

		auto hasStartCap = [&]() {
			wxString align = segments.front().align;
			align.MakeLower();
			return vertical ? (align == "north" || align == "vertical") : (align == "west" || align == "horizontal");
		};

		auto hasEndCap = [&]() {
			wxString align = segments.back().align;
			align.MakeLower();
			return vertical ? (align == "south" || align == "vertical") : (align == "east" || align == "horizontal");
		};

		dc.SetPen(wxPen(lineColour, 2));
		if (vertical) {
			const int railX = measure.anchorUnion.GetRight() + originOffset.x + indicatorSpace / 2;
			const wxPoint start(railX, segments.front().tileAnchor.y + originOffset.y + tileCell / 2);
			const wxPoint end(railX, segments.back().tileAnchor.y + originOffset.y + tileCell / 2);
			dc.DrawLine(start, end);
			if (hasStartCap()) {
				drawCap(start);
			}
			if (hasEndCap()) {
				drawCap(end);
			}
		} else {
			const int railY = measure.anchorUnion.GetBottom() + originOffset.y + indicatorSpace / 2;
			const wxPoint start(segments.front().tileAnchor.x + originOffset.x + tileCell / 2, railY);
			const wxPoint end(segments.back().tileAnchor.x + originOffset.x + tileCell / 2, railY);
			dc.DrawLine(start, end);
			if (hasStartCap()) {
				drawCap(start);
			}
			if (hasEndCap()) {
				drawCap(end);
			}
		}
	};

	drawPreviewBox(verticalRect, {"north", "vertical", "south"}, true);
	drawPreviewBox(horizontalRect, {"west", "horizontal", "east"}, false);
}

void MaterialsWorkbenchBrushPanel::RefreshDoodadAlternativeList() {
	if (!doodadAlternativeSliderPanel_) {
		return;
	}

	if (brushStorage_.doodadAlternatives.empty()) {
		doodadAlternativeIndex_ = -1;
	} else if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		doodadAlternativeIndex_ = 0;
	}

	doodadAlternativeIndicatorRects_.clear();
	RefreshDoodadAlternativeSlider();
}

void MaterialsWorkbenchBrushPanel::RefreshDoodadAlternativeSlider() {
	if (!doodadAlternativeSliderPanel_) {
		return;
	}

	doodadAlternativeSliderPanel_->Refresh();
}

void MaterialsWorkbenchBrushPanel::SelectDoodadAlternative(int index) {
	if (!hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (index < 0 || index >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		return;
	}
	if (doodadAlternativeIndex_ == index) {
		return;
	}

	doodadAlternativeIndex_ = index;
	doodadPreviewAuthoringFloors_.clear();
	doodadSingleItemIndex_ = ClampIndexForCount(doodadSingleItemIndex_, brushStorage_.doodadAlternatives[static_cast<size_t>(index)].singleItems.size());
	doodadCompositeIndex_ = ClampIndexForCount(doodadCompositeIndex_, brushStorage_.doodadAlternatives[static_cast<size_t>(index)].composites.size());
	RefreshDoodadAlternativeList();
	RefreshDoodadSelection();
}

void MaterialsWorkbenchBrushPanel::StepDoodadAlternative(int delta) {
	if (!hasBrush_ || !UsesDoodadVariationEditor() || brushStorage_.doodadAlternatives.empty()) {
		return;
	}

	const int baseIndex = doodadAlternativeIndex_ >= 0 ? doodadAlternativeIndex_ : 0;
	SelectDoodadAlternative(std::clamp(baseIndex + delta, 0, static_cast<int>(brushStorage_.doodadAlternatives.size()) - 1));
}

void MaterialsWorkbenchBrushPanel::StepDoodadPreviewFloor(int delta) {
	if (!hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}

	std::vector<int> entries;
	entries.push_back(MaterialsWorkbenchBrushPanel::kDoodadPreviewAllFloors);
	entries.insert(entries.end(), doodadPreviewAvailableFloors_.begin(), doodadPreviewAvailableFloors_.end());
	if (entries.size() <= 1) {
		return;
	}

	int currentIndex = 0;
	const auto it = std::find(entries.begin(), entries.end(), doodadPreviewFloor_);
	if (it != entries.end()) {
		currentIndex = static_cast<int>(std::distance(entries.begin(), it));
	}
	const int nextIndex = std::clamp(currentIndex + delta, 0, static_cast<int>(entries.size()) - 1);
	doodadPreviewFloor_ = entries[static_cast<size_t>(nextIndex)];
	RefreshDoodadPreview();
}

void MaterialsWorkbenchBrushPanel::AddDoodadPreviewFloor() {
	if (!hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}

	int newFloor = 0;
	if (!doodadPreviewAvailableFloors_.empty()) {
		newFloor = *std::min_element(doodadPreviewAvailableFloors_.begin(), doodadPreviewAvailableFloors_.end()) - 1;
	} else if (doodadPreviewFloor_ != MaterialsWorkbenchBrushPanel::kDoodadPreviewAllFloors) {
		newFloor = doodadPreviewFloor_ - 1;
	}

	if (std::find(doodadPreviewAuthoringFloors_.begin(), doodadPreviewAuthoringFloors_.end(), newFloor) == doodadPreviewAuthoringFloors_.end()) {
		doodadPreviewAuthoringFloors_.push_back(newFloor);
	}
	doodadPreviewFloor_ = newFloor;
	RefreshDoodadPreview();
	SetStatusMessage(wxString::Format("Added floor %d for doodad authoring.", newFloor));
}

void MaterialsWorkbenchBrushPanel::RemoveDoodadPreviewFloor() {
	if (!hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadPreviewFloor_ == MaterialsWorkbenchBrushPanel::kDoodadPreviewAllFloors) {
		SetStatusMessage("Select a specific floor before removing it.");
		return;
	}

	DoodadCompositeRecord* composite = GetSelectedDoodadComposite(brushStorage_, doodadAlternativeIndex_, doodadCompositeIndex_);
	if (!composite) {
		SetStatusMessage("Select a composite before removing floors.");
		return;
	}

	const int floorToRemove = doodadPreviewFloor_;
	auto &tiles = composite->tiles;
	tiles.erase(
		std::remove_if(
			tiles.begin(),
			tiles.end(),
			[floorToRemove](const DoodadCompositeTileRecord &tile) {
				return tile.offsetZ == floorToRemove;
			}
		),
		tiles.end()
	);
	doodadPreviewAuthoringFloors_.erase(
		std::remove(doodadPreviewAuthoringFloors_.begin(), doodadPreviewAuthoringFloors_.end(), floorToRemove),
		doodadPreviewAuthoringFloors_.end()
	);
	if (doodadTileIndex_ >= static_cast<int>(tiles.size())) {
		doodadTileIndex_ = static_cast<int>(tiles.size()) - 1;
	}
	doodadTileItemIndex_ = -1;

	const std::vector<int> remainingFloors = CollectDoodadCompositeFloors(*composite);
	if (!remainingFloors.empty()) {
		doodadPreviewFloor_ = remainingFloors.back();
	} else if (!doodadPreviewAuthoringFloors_.empty()) {
		doodadPreviewFloor_ = *std::max_element(doodadPreviewAuthoringFloors_.begin(), doodadPreviewAuthoringFloors_.end());
	} else {
		doodadPreviewFloor_ = MaterialsWorkbenchBrushPanel::kDoodadPreviewAllFloors;
	}

	RefreshDoodadSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage(wxString::Format("Removed floor %d from the doodad scene.", floorToRemove));
}

void MaterialsWorkbenchBrushPanel::OnDoodadAlternativeSliderPaint(wxPaintEvent &WXUNUSED(event)) {
	if (!doodadAlternativeSliderPanel_) {
		return;
	}

	wxAutoBufferedPaintDC dc(doodadAlternativeSliderPanel_);
	dc.SetBackground(wxBrush(doodadAlternativeSliderPanel_->GetBackgroundColour()));
	dc.Clear();

	const wxRect rect = doodadAlternativeSliderPanel_->GetClientRect();
	const int padding = doodadAlternativeSliderPanel_->FromDIP(4);
	const int controlWidth = doodadAlternativeSliderPanel_->FromDIP(28);
	const int controlHeight = doodadAlternativeSliderPanel_->FromDIP(22);
	const int gap = doodadAlternativeSliderPanel_->FromDIP(6);
	const int indicatorSize = doodadAlternativeSliderPanel_->FromDIP(16);
	const int indicatorGap = doodadAlternativeSliderPanel_->FromDIP(8);
	const int rowY = rect.y + std::max(0, (rect.height - controlHeight) / 2);
	doodadAlternativeIndicatorRects_.clear();

	const wxColour borderColour = wxColour(74, 79, 92);
	const wxColour textColour = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
	const wxColour mutedColour = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
	const wxColour filledColour = wxColour(124, 186, 255);
	const wxColour emptyFillColour = wxColour(48, 52, 60);
	const wxColour emptyBorderColour = wxColour(92, 97, 110);
	const wxColour accentColour = wxColour(255, 196, 92);
	const wxColour accentInnerColour = wxColour(255, 222, 140);
	const wxColour panelColour = wxColour(34, 37, 44);

	auto drawControl = [&](const wxRect &controlRect, const wxString &label, bool enabled) {
		dc.SetPen(wxPen(enabled ? borderColour : wxColour(58, 60, 70)));
		dc.SetBrush(wxBrush(panelColour));
		dc.DrawRoundedRectangle(controlRect, doodadAlternativeSliderPanel_->FromDIP(4));
		dc.SetTextForeground(enabled ? textColour : mutedColour);
		dc.DrawLabel(label, controlRect, wxALIGN_CENTER);
	};

	auto drawArrowControl = [&](const wxRect &controlRect, SliderArrowDirection direction, bool enabled) {
		dc.SetPen(wxPen(enabled ? borderColour : wxColour(58, 60, 70)));
		dc.SetBrush(wxBrush(panelColour));
		dc.DrawRoundedRectangle(controlRect, doodadAlternativeSliderPanel_->FromDIP(4));
		DrawSliderArrowGlyph(dc, controlRect, enabled ? textColour : mutedColour, direction);
	};

	const int alternativeCount = static_cast<int>(brushStorage_.doodadAlternatives.size());
	const bool hasAlternative = doodadAlternativeIndex_ >= 0 && doodadAlternativeIndex_ < alternativeCount;
	const int totalIndicatorsWidth = alternativeCount > 0 ? alternativeCount * indicatorSize + (alternativeCount - 1) * indicatorGap : 0;
	const int rowContentWidth = controlWidth + gap + controlWidth + gap + totalIndicatorsWidth + gap + controlWidth + gap + controlWidth;
	const int rowStartX = rect.x + std::max(0, (rect.width - rowContentWidth) / 2);
	doodadAlternativeRemoveRect_ = wxRect(rowStartX, rowY, controlWidth, controlHeight);
	doodadAlternativePrevRect_ = wxRect(doodadAlternativeRemoveRect_.GetRight() + gap, rowY, controlWidth, controlHeight);
	const int indicatorStartX = doodadAlternativePrevRect_.GetRight() + gap;
	doodadAlternativeNextRect_ = wxRect(indicatorStartX + totalIndicatorsWidth + gap, rowY, controlWidth, controlHeight);
	doodadAlternativeAddRect_ = wxRect(doodadAlternativeNextRect_.GetRight() + gap, rowY, controlWidth, controlHeight);

	drawArrowControl(doodadAlternativePrevRect_, SliderArrowDirection::Left, hasAlternative && doodadAlternativeIndex_ > 0);
	drawArrowControl(doodadAlternativeNextRect_, SliderArrowDirection::Right, hasAlternative && doodadAlternativeIndex_ + 1 < alternativeCount);
	drawControl(doodadAlternativeAddRect_, "+", hasBrush_ && UsesDoodadVariationEditor());
	drawControl(doodadAlternativeRemoveRect_, "-", hasAlternative);

	const int indicatorY = rowY + (controlHeight - indicatorSize) / 2;

	for (int i = 0; i < alternativeCount; ++i) {
		wxRect indicatorRect(indicatorStartX + i * (indicatorSize + indicatorGap), indicatorY, indicatorSize, indicatorSize);
		doodadAlternativeIndicatorRects_.push_back(indicatorRect);
		const DoodadAlternativeRecord &alternative = brushStorage_.doodadAlternatives[static_cast<size_t>(i)];
		bool hasVisualContent = !alternative.singleItems.empty();
		for (const DoodadCompositeRecord &composite : alternative.composites) {
			if (!composite.tiles.empty()) {
				hasVisualContent = true;
				break;
			}
		}
		const bool active = i == doodadAlternativeIndex_;
		if (active) {
			wxRect outerRect = indicatorRect;
			outerRect.Inflate(doodadAlternativeSliderPanel_->FromDIP(2));
			dc.SetPen(wxPen(accentColour, 2));
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.DrawRoundedRectangle(outerRect, doodadAlternativeSliderPanel_->FromDIP(4));
		}
		dc.SetPen(wxPen(hasVisualContent ? borderColour : emptyBorderColour, 1));
		dc.SetBrush(wxBrush(hasVisualContent ? filledColour : emptyFillColour));
		dc.DrawRoundedRectangle(indicatorRect, doodadAlternativeSliderPanel_->FromDIP(3));
		if (active) {
			wxRect innerRect = indicatorRect;
			innerRect.Deflate(doodadAlternativeSliderPanel_->FromDIP(3));
			if (innerRect.width > 0 && innerRect.height > 0) {
				dc.SetPen(*wxTRANSPARENT_PEN);
				dc.SetBrush(wxBrush(accentInnerColour));
				dc.DrawRoundedRectangle(innerRect, doodadAlternativeSliderPanel_->FromDIP(2));
			}
		}
	}

	if (alternativeCount == 0) {
		dc.SetTextForeground(mutedColour);
		dc.DrawText("No alternatives", rect.x + (rect.width - dc.GetTextExtent("No alternatives").GetWidth()) / 2, rowY + (controlHeight - dc.GetCharHeight()) / 2);
	}
}

void MaterialsWorkbenchBrushPanel::OnDoodadAlternativeSliderLeftDown(wxMouseEvent &event) {
	const wxPoint position = event.GetPosition();
	if (doodadAlternativePrevRect_.Contains(position)) {
		StepDoodadAlternative(-1);
		return;
	}
	if (doodadAlternativeNextRect_.Contains(position)) {
		StepDoodadAlternative(1);
		return;
	}
	if (doodadAlternativeAddRect_.Contains(position)) {
		wxCommandEvent dummy;
		OnAddDoodadAlternative(dummy);
		return;
	}
	if (doodadAlternativeRemoveRect_.Contains(position)) {
		wxCommandEvent dummy;
		OnRemoveDoodadAlternative(dummy);
		return;
	}
	for (size_t i = 0; i < doodadAlternativeIndicatorRects_.size(); ++i) {
		if (doodadAlternativeIndicatorRects_[i].Contains(position)) {
			SelectDoodadAlternative(static_cast<int>(i));
			return;
		}
	}
	event.Skip();
}

void MaterialsWorkbenchBrushPanel::OnDoodadFloorSliderPaint(wxPaintEvent &WXUNUSED(event)) {
	if (!doodadPreviewFloorSliderPanel_) {
		return;
	}

	wxAutoBufferedPaintDC dc(doodadPreviewFloorSliderPanel_);
	dc.SetBackground(wxBrush(doodadPreviewFloorSliderPanel_->GetBackgroundColour()));
	dc.Clear();

	const wxRect rect = doodadPreviewFloorSliderPanel_->GetClientRect();
	const int padding = doodadPreviewFloorSliderPanel_->FromDIP(4);
	const int controlWidth = rect.width - padding * 2;
	const int controlHeight = doodadPreviewFloorSliderPanel_->FromDIP(22);
	const int indicatorSize = std::max(doodadPreviewFloorSliderPanel_->FromDIP(18), std::min(controlWidth, doodadPreviewFloorSliderPanel_->FromDIP(26)));
	const int gap = doodadPreviewFloorSliderPanel_->FromDIP(6);
	doodadPreviewFloorIndicatorRects_.clear();

	const wxColour borderColour = wxColour(74, 79, 92);
	const wxColour textColour = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
	const wxColour mutedColour = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
	const wxColour panelColour = wxColour(34, 37, 44);
	const wxColour accentColour = wxColour(255, 196, 92);
	const wxColour accentInnerColour = wxColour(255, 222, 140);
	const wxColour filledColour = wxColour(124, 186, 255);
	const wxColour emptyFillColour = wxColour(48, 52, 60);

	std::vector<int> entries;
	entries.push_back(MaterialsWorkbenchBrushPanel::kDoodadPreviewAllFloors);
	entries.insert(entries.end(), doodadPreviewAvailableFloors_.begin(), doodadPreviewAvailableFloors_.end());
	const bool enabled = doodadPreviewFloorSliderPanel_->IsEnabled() && entries.size() > 1;

	auto drawControl = [&](const wxRect &controlRect, const wxString &label, bool controlEnabled) {
		dc.SetPen(wxPen(controlEnabled ? borderColour : wxColour(58, 60, 70)));
		dc.SetBrush(wxBrush(panelColour));
		dc.DrawRoundedRectangle(controlRect, doodadPreviewFloorSliderPanel_->FromDIP(4));
		dc.SetTextForeground(controlEnabled ? textColour : mutedColour);
		dc.DrawLabel(label, controlRect, wxALIGN_CENTER);
	};

	auto drawArrowControl = [&](const wxRect &controlRect, SliderArrowDirection direction, bool controlEnabled) {
		dc.SetPen(wxPen(controlEnabled ? borderColour : wxColour(58, 60, 70)));
		dc.SetBrush(wxBrush(panelColour));
		dc.DrawRoundedRectangle(controlRect, doodadPreviewFloorSliderPanel_->FromDIP(4));
		DrawSliderArrowGlyph(dc, controlRect, controlEnabled ? textColour : mutedColour, direction);
	};

	const int contentHeight = controlHeight + gap + static_cast<int>(entries.size()) * indicatorSize + std::max(0, static_cast<int>(entries.size()) - 1) * gap + gap + controlHeight;
	const int totalHeight = controlHeight + gap + contentHeight + gap + controlHeight;
	const int startY = rect.y + std::max(0, (rect.height - totalHeight) / 2);
	doodadPreviewFloorAddRect_ = wxRect(rect.x + padding, startY, controlWidth, controlHeight);
	doodadPreviewFloorUpRect_ = wxRect(rect.x + padding, doodadPreviewFloorAddRect_.GetBottom() + 1 + gap, controlWidth, controlHeight);
	const int indicatorX = rect.x + padding + std::max(0, (controlWidth - indicatorSize) / 2);
	int indicatorY = doodadPreviewFloorUpRect_.GetBottom() + 1 + gap;

	int currentIndex = 0;
	const auto activeIt = std::find(entries.begin(), entries.end(), doodadPreviewFloor_);
	if (activeIt != entries.end()) {
		currentIndex = static_cast<int>(std::distance(entries.begin(), activeIt));
	}

	drawControl(doodadPreviewFloorAddRect_, "+", doodadPreviewFloorSliderPanel_->IsEnabled());
	drawArrowControl(doodadPreviewFloorUpRect_, SliderArrowDirection::Up, enabled && currentIndex > 0);
	for (size_t i = 0; i < entries.size(); ++i) {
		wxRect indicatorRect(indicatorX, indicatorY, indicatorSize, indicatorSize);
		doodadPreviewFloorIndicatorRects_.push_back(indicatorRect);
		const bool active = static_cast<int>(i) == currentIndex;
		const bool allFloors = entries[i] == MaterialsWorkbenchBrushPanel::kDoodadPreviewAllFloors;
		if (active) {
			wxRect outerRect = indicatorRect;
			outerRect.Inflate(doodadPreviewFloorSliderPanel_->FromDIP(2));
			dc.SetPen(wxPen(accentColour, 2));
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.DrawRoundedRectangle(outerRect, doodadPreviewFloorSliderPanel_->FromDIP(4));
		}
		dc.SetPen(wxPen(enabled ? borderColour : wxColour(92, 97, 110), 1));
		dc.SetBrush(wxBrush(active ? filledColour : emptyFillColour));
		dc.DrawRoundedRectangle(indicatorRect, doodadPreviewFloorSliderPanel_->FromDIP(3));
		dc.SetTextForeground(active ? wxColour(24, 28, 34) : (enabled ? textColour : mutedColour));
		dc.DrawLabel(allFloors ? "A" : wxString::Format("%d", entries[i]), indicatorRect, wxALIGN_CENTER);
		if (active) {
			wxRect innerRect = indicatorRect;
			innerRect.Deflate(doodadPreviewFloorSliderPanel_->FromDIP(3));
			if (innerRect.width > 0 && innerRect.height > 0) {
				dc.SetPen(*wxTRANSPARENT_PEN);
				dc.SetBrush(wxBrush(accentInnerColour));
				dc.DrawRoundedRectangle(innerRect, doodadPreviewFloorSliderPanel_->FromDIP(2));
				dc.SetTextForeground(wxColour(24, 28, 34));
				dc.DrawLabel(allFloors ? "A" : wxString::Format("%d", entries[i]), innerRect, wxALIGN_CENTER);
			}
		}
		indicatorY += indicatorSize + gap;
	}
	doodadPreviewFloorDownRect_ = wxRect(rect.x + padding, indicatorY, controlWidth, controlHeight);
	doodadPreviewFloorRemoveRect_ = wxRect(rect.x + padding, doodadPreviewFloorDownRect_.GetBottom() + 1 + gap, controlWidth, controlHeight);
	drawArrowControl(doodadPreviewFloorDownRect_, SliderArrowDirection::Down, enabled && currentIndex + 1 < static_cast<int>(entries.size()));
	drawControl(
		doodadPreviewFloorRemoveRect_,
		"-",
		doodadPreviewFloorSliderPanel_->IsEnabled() && doodadPreviewFloor_ != MaterialsWorkbenchBrushPanel::kDoodadPreviewAllFloors
	);
}

void MaterialsWorkbenchBrushPanel::OnDoodadFloorSliderLeftDown(wxMouseEvent &event) {
	const wxPoint position = event.GetPosition();
	if (doodadPreviewFloorAddRect_.Contains(position)) {
		AddDoodadPreviewFloor();
		return;
	}
	if (doodadPreviewFloorUpRect_.Contains(position)) {
		StepDoodadPreviewFloor(-1);
		return;
	}
	if (doodadPreviewFloorDownRect_.Contains(position)) {
		StepDoodadPreviewFloor(1);
		return;
	}
	if (doodadPreviewFloorRemoveRect_.Contains(position)) {
		RemoveDoodadPreviewFloor();
		return;
	}
	std::vector<int> entries;
	entries.push_back(MaterialsWorkbenchBrushPanel::kDoodadPreviewAllFloors);
	entries.insert(entries.end(), doodadPreviewAvailableFloors_.begin(), doodadPreviewAvailableFloors_.end());
	for (size_t i = 0; i < doodadPreviewFloorIndicatorRects_.size() && i < entries.size(); ++i) {
		if (doodadPreviewFloorIndicatorRects_[i].Contains(position)) {
			doodadPreviewFloor_ = entries[i];
			RefreshDoodadPreview();
			return;
		}
	}
	event.Skip();
}

void MaterialsWorkbenchBrushPanel::RefreshDoodadSingleItemList() {
	const int topItem = CaptureListTopItem(doodadSingleItemsList_);
	doodadSingleItemsList_->Clear();
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		doodadSingleItemIndex_ = -1;
		return;
	}

	const auto &items = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].singleItems;
	for (size_t i = 0; i < items.size(); ++i) {
		doodadSingleItemsList_->Append(FormatDoodadSingleItemLabel(items[i].itemId, items[i].chance, i));
	}

	if (items.empty()) {
		doodadSingleItemIndex_ = -1;
	} else if (doodadSingleItemIndex_ < 0 || doodadSingleItemIndex_ >= static_cast<int>(items.size())) {
		doodadSingleItemIndex_ = 0;
	}

	if (doodadSingleItemIndex_ >= 0) {
		doodadSingleItemsList_->SetSelection(doodadSingleItemIndex_);
	}
	RestoreListTopItem(doodadSingleItemsList_, topItem);
}

void MaterialsWorkbenchBrushPanel::RefreshDoodadCompositeList() {
	const int topItem = CaptureListTopItem(doodadCompositesList_);
	doodadCompositesList_->Clear();
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		doodadCompositeIndex_ = -1;
		return;
	}

	const auto &composites = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].composites;
	for (size_t i = 0; i < composites.size(); ++i) {
		doodadCompositesList_->Append(FormatDoodadCompositeLabel(composites[i], i));
	}

	if (composites.empty()) {
		doodadCompositeIndex_ = -1;
	} else if (doodadCompositeIndex_ < 0 || doodadCompositeIndex_ >= static_cast<int>(composites.size())) {
		doodadCompositeIndex_ = 0;
	}

	if (doodadCompositeIndex_ >= 0) {
		doodadCompositesList_->SetSelection(doodadCompositeIndex_);
	}
	RestoreListTopItem(doodadCompositesList_, topItem);
}

void MaterialsWorkbenchBrushPanel::RefreshDoodadTileList() {
	const int topItem = CaptureListTopItem(doodadTilesList_);
	doodadTilesList_->Clear();
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		doodadTileIndex_ = -1;
		return;
	}
	const auto &alternative = brushStorage_.doodadAlternatives[doodadAlternativeIndex_];
	if (doodadCompositeIndex_ < 0 || doodadCompositeIndex_ >= static_cast<int>(alternative.composites.size())) {
		doodadTileIndex_ = -1;
		return;
	}

	const auto &tiles = alternative.composites[doodadCompositeIndex_].tiles;
	for (size_t i = 0; i < tiles.size(); ++i) {
		doodadTilesList_->Append(FormatDoodadTileLabel(tiles[i], i));
	}

	if (tiles.empty()) {
		doodadTileIndex_ = -1;
	} else if (doodadTileIndex_ < 0 || doodadTileIndex_ >= static_cast<int>(tiles.size())) {
		doodadTileIndex_ = 0;
	}

	if (doodadTileIndex_ >= 0) {
		doodadTilesList_->SetSelection(doodadTileIndex_);
	}
	RestoreListTopItem(doodadTilesList_, topItem);
}

void MaterialsWorkbenchBrushPanel::RefreshDoodadTileItemList() {
	const int topItem = CaptureListTopItem(doodadTileItemsList_);
	doodadTileItemsList_->Clear();
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		doodadTileItemIndex_ = -1;
		return;
	}
	const auto &alternative = brushStorage_.doodadAlternatives[doodadAlternativeIndex_];
	if (doodadCompositeIndex_ < 0 || doodadCompositeIndex_ >= static_cast<int>(alternative.composites.size())) {
		doodadTileItemIndex_ = -1;
		return;
	}
	const auto &composite = alternative.composites[doodadCompositeIndex_];
	if (doodadTileIndex_ < 0 || doodadTileIndex_ >= static_cast<int>(composite.tiles.size())) {
		doodadTileItemIndex_ = -1;
		return;
	}

	const auto &items = composite.tiles[doodadTileIndex_].items;
	for (size_t i = 0; i < items.size(); ++i) {
		doodadTileItemsList_->Append(FormatDoodadTileItemLabel(items[i].itemId, i));
	}

	if (items.empty()) {
		doodadTileItemIndex_ = -1;
	} else if (doodadTileItemIndex_ < 0 || doodadTileItemIndex_ >= static_cast<int>(items.size())) {
		doodadTileItemIndex_ = 0;
	}

	if (doodadTileItemIndex_ >= 0) {
		doodadTileItemsList_->SetSelection(doodadTileItemIndex_);
	}
	RestoreListTopItem(doodadTileItemsList_, topItem);
}

void MaterialsWorkbenchBrushPanel::RefreshDoodadSelection() {
	internalUpdate_ = true;

	RefreshDoodadSingleItemList();
	RefreshDoodadCompositeList();
	RefreshDoodadTileList();
	RefreshDoodadTileItemList();

	const bool hasAlternative = doodadAlternativeIndex_ >= 0 && doodadAlternativeIndex_ < static_cast<int>(brushStorage_.doodadAlternatives.size());
	bool hasSingleItem = false;
	bool hasComposite = false;
	bool hasTile = false;
	bool hasTileItem = false;

	if (hasAlternative) {
		const auto &alternative = brushStorage_.doodadAlternatives[doodadAlternativeIndex_];
		hasSingleItem = doodadSingleItemIndex_ >= 0 && doodadSingleItemIndex_ < static_cast<int>(alternative.singleItems.size());
		hasComposite = doodadCompositeIndex_ >= 0 && doodadCompositeIndex_ < static_cast<int>(alternative.composites.size());

		if (hasSingleItem) {
			const auto &singleItem = alternative.singleItems[doodadSingleItemIndex_];
			doodadSingleItemIdCtrl_->SetValue(singleItem.itemId);
			doodadSingleItemChanceCtrl_->SetValue(singleItem.chance);
		} else {
			doodadSingleItemIdCtrl_->SetValue(0);
			doodadSingleItemChanceCtrl_->SetValue(0);
		}

		if (hasComposite) {
			const auto &composite = alternative.composites[doodadCompositeIndex_];
			doodadCompositeChanceCtrl_->SetValue(composite.chance);
			hasTile = doodadTileIndex_ >= 0 && doodadTileIndex_ < static_cast<int>(composite.tiles.size());
			if (hasTile) {
				const auto &tile = composite.tiles[doodadTileIndex_];
				doodadTileOffsetXCtrl_->SetValue(tile.offsetX);
				doodadTileOffsetYCtrl_->SetValue(tile.offsetY);
				doodadTileOffsetZCtrl_->SetValue(tile.offsetZ);
				hasTileItem = doodadTileItemIndex_ >= 0 && doodadTileItemIndex_ < static_cast<int>(tile.items.size());
				if (hasTileItem) {
					doodadTileItemIdCtrl_->SetValue(tile.items[doodadTileItemIndex_].itemId);
				} else {
					doodadTileItemIdCtrl_->SetValue(0);
				}
			} else {
				doodadTileOffsetXCtrl_->SetValue(0);
				doodadTileOffsetYCtrl_->SetValue(0);
				doodadTileOffsetZCtrl_->SetValue(0);
				doodadTileItemIdCtrl_->SetValue(0);
			}
		} else {
			doodadCompositeChanceCtrl_->SetValue(0);
			doodadTileOffsetXCtrl_->SetValue(0);
			doodadTileOffsetYCtrl_->SetValue(0);
			doodadTileOffsetZCtrl_->SetValue(0);
			doodadTileItemIdCtrl_->SetValue(0);
		}
	} else {
		doodadSingleItemIdCtrl_->SetValue(0);
		doodadSingleItemChanceCtrl_->SetValue(0);
		doodadCompositeChanceCtrl_->SetValue(0);
		doodadTileOffsetXCtrl_->SetValue(0);
		doodadTileOffsetYCtrl_->SetValue(0);
		doodadTileOffsetZCtrl_->SetValue(0);
		doodadTileItemIdCtrl_->SetValue(0);
	}

	doodadSingleItemIdCtrl_->Enable(hasSingleItem);
	doodadSingleItemChanceCtrl_->Enable(hasSingleItem);
	doodadCompositeChanceCtrl_->Enable(hasComposite);
	doodadTileOffsetXCtrl_->Enable(hasTile);
	doodadTileOffsetYCtrl_->Enable(hasTile);
	doodadTileOffsetZCtrl_->Enable(hasTile);
	doodadTileItemIdCtrl_->Enable(hasComposite);

	internalUpdate_ = false;
	UpdateItemOwnershipHint(doodadSingleItemOwnershipLabel_, doodadSingleItemIdCtrl_->GetValue(), hasSingleItem);
	UpdateItemOwnershipHint(doodadTileItemOwnershipLabel_, doodadTileItemIdCtrl_->GetValue(), hasComposite);
	RefreshDoodadPreview();
}

void MaterialsWorkbenchBrushPanel::RefreshDoodadPreviewFloorChoice() {
	if (!doodadPreviewFloorSliderPanel_) {
		return;
	}

	std::vector<int> compositeFloors;

	const bool hasAlternative = doodadAlternativeIndex_ >= 0 && doodadAlternativeIndex_ < static_cast<int>(brushStorage_.doodadAlternatives.size());
	bool hasSingleItem = false;
	bool hasComposite = false;
	if (hasAlternative) {
		const auto &alternative = brushStorage_.doodadAlternatives[doodadAlternativeIndex_];
		hasSingleItem = doodadSingleItemIndex_ >= 0 && doodadSingleItemIndex_ < static_cast<int>(alternative.singleItems.size());
		hasComposite = doodadCompositeIndex_ >= 0 && doodadCompositeIndex_ < static_cast<int>(alternative.composites.size());
	}

	const bool showComposite = hasComposite && (doodadPreviewPreferComposite_ || !hasSingleItem);
	if (!showComposite) {
		doodadPreviewAvailableFloors_.clear();
		doodadPreviewAuthoringFloors_.clear();
		doodadPreviewFloor_ = MaterialsWorkbenchBrushPanel::kDoodadPreviewAllFloors;
		doodadPreviewFloorSliderPanel_->Enable(false);
		doodadPreviewFloorSliderPanel_->Refresh();
		return;
	}

	const auto &composite = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].composites[doodadCompositeIndex_];
	compositeFloors = CollectDoodadCompositeFloors(composite);
	doodadPreviewAvailableFloors_ = compositeFloors;
	doodadPreviewAvailableFloors_.insert(
		doodadPreviewAvailableFloors_.end(),
		doodadPreviewAuthoringFloors_.begin(),
		doodadPreviewAuthoringFloors_.end()
	);
	if (doodadPreviewFloor_ != MaterialsWorkbenchBrushPanel::kDoodadPreviewAllFloors) {
		doodadPreviewAvailableFloors_.push_back(doodadPreviewFloor_);
	}
	std::sort(doodadPreviewAvailableFloors_.begin(), doodadPreviewAvailableFloors_.end());
	doodadPreviewAvailableFloors_.erase(
		std::unique(doodadPreviewAvailableFloors_.begin(), doodadPreviewAvailableFloors_.end()),
		doodadPreviewAvailableFloors_.end()
	);

	if (doodadPreviewFloor_ != MaterialsWorkbenchBrushPanel::kDoodadPreviewAllFloors) {
		const auto it = std::find(doodadPreviewAvailableFloors_.begin(), doodadPreviewAvailableFloors_.end(), doodadPreviewFloor_);
		if (it == doodadPreviewAvailableFloors_.end()) {
			doodadPreviewFloor_ = MaterialsWorkbenchBrushPanel::kDoodadPreviewAllFloors;
		}
	}
	doodadPreviewFloorSliderPanel_->Enable(true);
	doodadPreviewFloorSliderPanel_->Refresh();
}

void MaterialsWorkbenchBrushPanel::RefreshDoodadPreview() {
	RefreshDoodadPreviewFloorChoice();

	if (!doodadPreviewSummaryLabel_) {
		return;
	}

	const bool hasAlternative = doodadAlternativeIndex_ >= 0 && doodadAlternativeIndex_ < static_cast<int>(brushStorage_.doodadAlternatives.size());
	bool hasSingleItem = false;
	bool hasComposite = false;
	if (hasAlternative) {
		const auto &alternative = brushStorage_.doodadAlternatives[doodadAlternativeIndex_];
		hasSingleItem = doodadSingleItemIndex_ >= 0 && doodadSingleItemIndex_ < static_cast<int>(alternative.singleItems.size());
		hasComposite = doodadCompositeIndex_ >= 0 && doodadCompositeIndex_ < static_cast<int>(alternative.composites.size());
	}

	const bool showComposite = hasComposite && (doodadPreviewPreferComposite_ || !hasSingleItem);
	if (!hasAlternative) {
		doodadPreviewSummaryLabel_->SetLabel("Select an alternative to start authoring a doodad scene on the 32x32 grid.");
	} else if (showComposite) {
		const auto &composite = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].composites[doodadCompositeIndex_];
		doodadPreviewSummaryLabel_->SetLabel(
			wxString::Format(
				"Alternative %d | composite %d | %zu tile%s | %zu floor%s",
				doodadAlternativeIndex_ + 1,
				doodadCompositeIndex_ + 1,
				composite.tiles.size(),
				composite.tiles.size() == 1 ? "" : "s",
				doodadPreviewAvailableFloors_.size(),
				doodadPreviewAvailableFloors_.size() == 1 ? "" : "s"
			)
		);
	} else if (hasSingleItem) {
		const auto &singleItem = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].singleItems[doodadSingleItemIndex_];
		doodadPreviewSummaryLabel_->SetLabel(
			wxString::Format(
				"Alternative %d | single %d | item %d",
				doodadAlternativeIndex_ + 1,
				doodadSingleItemIndex_ + 1,
				singleItem.itemId
			)
		);
	} else {
		doodadPreviewSummaryLabel_->SetLabel(
			wxString::Format("Alternative %d is empty. Add a single item or a composite to build this doodad.", doodadAlternativeIndex_ + 1)
		);
	}

	doodadPreviewSummaryLabel_->Wrap(doodadPreviewSummaryLabel_->GetParent()->FromDIP(520));
	if (doodadPreviewPanel_) {
		doodadPreviewPanel_->Refresh();
	}
	if (doodadPreviewFloorSliderPanel_) {
		doodadPreviewFloorSliderPanel_->Refresh();
	}
}

void MaterialsWorkbenchBrushPanel::OnDoodadPreviewPaint(wxPaintEvent &WXUNUSED(event)) {
	if (!doodadPreviewPanel_) {
		return;
	}

	wxAutoBufferedPaintDC dc(doodadPreviewPanel_);
	const wxRect panelRect = doodadPreviewPanel_->GetClientRect();
	dc.SetBackground(wxBrush(wxColour(22, 24, 28)));
	dc.Clear();

	if (panelRect.GetWidth() <= 0 || panelRect.GetHeight() <= 0) {
		return;
	}

	const int margin = doodadPreviewPanel_->FromDIP(12);
	const int cellSize = doodadPreviewPanel_->FromDIP(32);
	const int floorGap = doodadPreviewPanel_->FromDIP(18);
	const int titleGap = doodadPreviewPanel_->FromDIP(4);
	wxRect contentRect(panelRect);
	contentRect.Deflate(margin);
	const wxColour cardColour(29, 31, 36);
	const wxColour cellColour(43, 47, 54);
	const wxColour cellBorderColour(76, 84, 96);
	const wxColour selectionColour(100, 174, 255);
	const wxColour textColour(219, 223, 230);

	dc.SetPen(wxPen(cardColour));
	dc.SetBrush(wxBrush(cardColour));
	dc.DrawRoundedRectangle(contentRect, doodadPreviewPanel_->FromDIP(8));

	const bool hasAlternative = doodadAlternativeIndex_ >= 0 && doodadAlternativeIndex_ < static_cast<int>(brushStorage_.doodadAlternatives.size());
	if (!hasAlternative) {
		dc.SetTextForeground(textColour);
		dc.DrawLabel("No doodad alternative selected.", contentRect, wxALIGN_CENTER);
		return;
	}

	const auto &alternative = brushStorage_.doodadAlternatives[doodadAlternativeIndex_];
	const bool hasSingleItem = doodadSingleItemIndex_ >= 0 && doodadSingleItemIndex_ < static_cast<int>(alternative.singleItems.size());
	const bool hasComposite = doodadCompositeIndex_ >= 0 && doodadCompositeIndex_ < static_cast<int>(alternative.composites.size());
	const bool showComposite = hasComposite && (doodadPreviewPreferComposite_ || !hasSingleItem);

	auto drawEmptyState = [&](const wxString &message) {
		dc.SetTextForeground(textColour);
		dc.DrawLabel(message, contentRect, wxALIGN_CENTER);
	};

	if (!showComposite && !hasSingleItem) {
		drawEmptyState("This alternative has no scene content yet.");
		return;
	}

	if (!showComposite) {
		const int singleItemId = alternative.singleItems[doodadSingleItemIndex_].itemId;
		const wxRect baseCellRect(0, 0, cellSize, cellSize);
		wxRect previewBounds = baseCellRect;
		previewBounds.Union(GetDoodadPreviewSpriteRect(singleItemId, wxPoint(0, 0)));
		wxRect cellRect = baseCellRect;
		cellRect.Offset(
			contentRect.x + (contentRect.GetWidth() - previewBounds.GetWidth()) / 2 - previewBounds.x,
			contentRect.y + (contentRect.GetHeight() - previewBounds.GetHeight()) / 2 - previewBounds.y
		);
		dc.SetPen(wxPen(selectionColour, 2));
		dc.SetBrush(wxBrush(cellColour));
		dc.DrawRectangle(cellRect);
		DrawDoodadPreviewItemSprite(dc, singleItemId, cellRect.GetTopLeft());
		return;
	}

	const auto &composite = alternative.composites[doodadCompositeIndex_];
	if (composite.tiles.empty()) {
		drawEmptyState("The selected composite has no tiles yet. Double-click the scene to add the first tile.");
		return;
	}

	const std::vector<int> floorsToDraw = ResolveDoodadPreviewFloorsToDraw(composite, doodadPreviewFloor_);
	if (floorsToDraw.empty()) {
		drawEmptyState("No floors are available for the selected composite.");
		return;
	}

	const std::vector<DoodadPreviewFloorLayout> layouts = BuildDoodadPreviewLayouts(
		dc,
		contentRect,
		composite,
		doodadPreviewFloor_,
		cellSize,
		floorGap,
		titleGap
	);
	if (layouts.empty()) {
		drawEmptyState("The selected floor filter hides all tiles.");
		return;
	}

	const wxColour emptyCellColour(35, 38, 44);
	const wxColour hoverCellColour(54, 60, 70);
	dc.SetTextForeground(textColour);
	for (const auto &layout : layouts) {
		if (layout.showTitle) {
			dc.DrawText(wxString::Format("Floor %d", layout.floor), layout.originX + layout.minCellX * cellSize, layout.titleY);
		}

		for (int cellY = layout.minCellY; cellY <= layout.maxCellY; ++cellY) {
			for (int cellX = layout.minCellX; cellX <= layout.maxCellX; ++cellX) {
				const wxRect cellRect(
					layout.originX + cellX * cellSize,
					layout.originY + cellY * cellSize,
					cellSize,
					cellSize
				);

				bool hasTileAtCell = false;
				for (int tileIndex : layout.tileIndices) {
					const auto &tile = composite.tiles[tileIndex];
					const bool combinedAllFloors = layout.floor == MaterialsWorkbenchBrushPanel::kDoodadPreviewAllFloors;
					const bool matchingFloor = combinedAllFloors || tile.offsetZ == layout.floor;
					const wxPoint projectedCell = GetDoodadPreviewProjectedCell(tile, combinedAllFloors);
					if (projectedCell.x == cellX && projectedCell.y == cellY && matchingFloor) {
						hasTileAtCell = true;
						break;
					}
				}

				const wxPoint selectedProjectedCell =
					(doodadTileIndex_ >= 0 && doodadTileIndex_ < static_cast<int>(composite.tiles.size()))
						? GetDoodadPreviewProjectedCell(
							composite.tiles[doodadTileIndex_],
							layout.floor == MaterialsWorkbenchBrushPanel::kDoodadPreviewAllFloors
						)
						: wxPoint(std::numeric_limits<int>::min(), std::numeric_limits<int>::min());
				const bool isSelectedTile =
					doodadTileIndex_ >= 0 &&
					doodadTileIndex_ < static_cast<int>(composite.tiles.size()) &&
					selectedProjectedCell.x == cellX &&
					selectedProjectedCell.y == cellY &&
					(layout.floor == MaterialsWorkbenchBrushPanel::kDoodadPreviewAllFloors || composite.tiles[doodadTileIndex_].offsetZ == layout.floor);

				dc.SetPen(wxPen(isSelectedTile ? selectionColour : cellBorderColour, isSelectedTile ? 2 : 1));
				dc.SetBrush(wxBrush(hasTileAtCell ? cellColour : emptyCellColour));
				dc.DrawRectangle(cellRect);
				if (!hasTileAtCell) {
					dc.SetPen(wxPen(hoverCellColour, 1, wxPENSTYLE_DOT));
					dc.DrawLine(cellRect.GetLeft() + cellSize / 2, cellRect.GetTop() + 4, cellRect.GetLeft() + cellSize / 2, cellRect.GetBottom() - 3);
					dc.DrawLine(cellRect.GetLeft() + 4, cellRect.GetTop() + cellSize / 2, cellRect.GetRight() - 3, cellRect.GetTop() + cellSize / 2);
				}
			}
		}

		for (int tileIndex : layout.tileIndices) {
			const auto &tile = composite.tiles[tileIndex];
			const wxPoint projectedCell = GetDoodadPreviewProjectedCell(tile, layout.floor == MaterialsWorkbenchBrushPanel::kDoodadPreviewAllFloors);
			const wxPoint tileAnchor(layout.originX + projectedCell.x * cellSize, layout.originY + projectedCell.y * cellSize);
			DrawDoodadPreviewTileStack(dc, tile.items, tileAnchor);
		}
	}
}

void MaterialsWorkbenchBrushPanel::NormalizeVariationSortOrders() {
	NormalizeVariationSortOrdersForStorage(brushStorage_);
}

bool MaterialsWorkbenchBrushPanel::ValidateBrushStorage(wxString &error) const {
	const BrushRecord &brush = brushStorage_.brush;
	const auto isOwnedByDifferentRuntimeBrush = [&](int itemId, wxString &ownerName) -> bool {
		if (!TryGetRuntimeBrushOwnerName(itemId, ownerName)) {
			return false;
		}

		return ownerName != brush.name && ownerName != loadedBrushStorage_.brush.name;
	};

	if (brush.name.IsEmpty()) {
		error = "Brush name cannot be empty.";
		return false;
	}
	if (brush.type.IsEmpty()) {
		error = "Brush type cannot be empty.";
		return false;
	}

	const wxString type = brush.type.Lower();
	if (!IsValidBrushEditorType(type)) {
		error = "Brush type must be one of: ground, carpet, table or doodad.";
		return false;
	}

	if (brush.lookId < 0) {
		error = "lookId cannot be negative.";
		return false;
	}
	if (brush.lookId > 0 && !IsKnownItemId(brush.lookId)) {
		error = wxString::Format("lookId uses unknown item id %d.", brush.lookId);
		return false;
	}
	if (brush.serverLookId < 0) {
		error = "serverLookId cannot be negative.";
		return false;
	}
	if (brush.serverLookId > 0 && !IsKnownItemId(brush.serverLookId)) {
		error = wxString::Format("serverLookId uses unknown item id %d.", brush.serverLookId);
		return false;
	}

	if (type == "ground") {
		if (brushStorage_.items.empty()) {
			error = "Ground brush must contain at least one weighted item.";
			return false;
		}
		for (size_t itemIndex = 0; itemIndex < brushStorage_.items.size(); ++itemIndex) {
			const BrushItemRecord &item = brushStorage_.items[itemIndex];
			if (item.itemId <= 0) {
				error = wxString::Format("Ground item %zu must use a positive item id.", itemIndex + 1);
				return false;
			}
			if (!IsKnownItemId(item.itemId)) {
				error = wxString::Format("Ground item %zu uses unknown item id %d.", itemIndex + 1, item.itemId);
				return false;
			}
			const ItemType &itemType = g_items.getItemType(static_cast<uint16_t>(item.itemId));
			if (!itemType.isGroundTile()) {
				error = wxString::Format("Ground item %zu uses item id %d, which is not a ground item.", itemIndex + 1, item.itemId);
				return false;
			}
			wxString ownerName;
			if (isOwnedByDifferentRuntimeBrush(item.itemId, ownerName)) {
				error = wxString::Format(
					"Ground item %zu uses item id %d, which already belongs to brush \"%s\" in the runtime catalog.",
					itemIndex + 1,
					item.itemId,
					ownerName
				);
				return false;
			}
		}
	}

	if (type == "carpet" || type == "table") {
		if (type == "carpet") {
			for (size_t nodeIndex = 0; nodeIndex < brushStorage_.carpetNodes.size(); ++nodeIndex) {
				const auto &node = brushStorage_.carpetNodes[nodeIndex];
				if (node.align.IsEmpty()) {
					error = wxString::Format("Carpet context %zu requires a map slot.", nodeIndex + 1);
					return false;
				}
				if (node.items.empty()) {
					error = wxString::Format("Carpet context %zu must contain at least one variant.", nodeIndex + 1);
					return false;
				}
				for (size_t itemIndex = 0; itemIndex < node.items.size(); ++itemIndex) {
					const auto &item = node.items[itemIndex];
					if (item.itemId <= 0) {
						error = wxString::Format("Carpet context %zu variant %zu must use a positive item id.", nodeIndex + 1, itemIndex + 1);
						return false;
					}
					if (!IsKnownItemId(item.itemId)) {
						error = wxString::Format("Carpet context %zu variant %zu uses unknown item id %d.", nodeIndex + 1, itemIndex + 1, item.itemId);
						return false;
					}
					wxString ownerName;
					if (isOwnedByDifferentRuntimeBrush(item.itemId, ownerName)) {
						error = wxString::Format(
							"Carpet context %zu variant %zu uses item id %d, which already belongs to brush \"%s\" in the runtime catalog.",
							nodeIndex + 1,
							itemIndex + 1,
							item.itemId,
							ownerName
						);
						return false;
					}
				}
			}
		} else {
			for (size_t nodeIndex = 0; nodeIndex < brushStorage_.tableNodes.size(); ++nodeIndex) {
				const auto &node = brushStorage_.tableNodes[nodeIndex];
				if (node.align.IsEmpty()) {
					error = wxString::Format("Node %zu requires an align value.", nodeIndex + 1);
					return false;
				}
				if (node.items.empty()) {
					error = wxString::Format("Node %zu must contain at least one item.", nodeIndex + 1);
					return false;
				}
				for (size_t itemIndex = 0; itemIndex < node.items.size(); ++itemIndex) {
					const auto &item = node.items[itemIndex];
					if (item.itemId <= 0) {
						error = wxString::Format("Node %zu item %zu must use a positive item id.", nodeIndex + 1, itemIndex + 1);
						return false;
					}
					if (!IsKnownItemId(item.itemId)) {
						error = wxString::Format("Node %zu item %zu uses unknown item id %d.", nodeIndex + 1, itemIndex + 1, item.itemId);
						return false;
					}
					wxString ownerName;
					if (isOwnedByDifferentRuntimeBrush(item.itemId, ownerName)) {
						error = wxString::Format(
							"Node %zu item %zu uses item id %d, which already belongs to brush \"%s\" in the runtime catalog.",
							nodeIndex + 1,
							itemIndex + 1,
							item.itemId,
							ownerName
						);
						return false;
					}
				}
			}
		}
	}

	if (type == "doodad") {
		for (size_t altIndex = 0; altIndex < brushStorage_.doodadAlternatives.size(); ++altIndex) {
			const DoodadAlternativeRecord &alternative = brushStorage_.doodadAlternatives[altIndex];
			if (alternative.singleItems.empty() && alternative.composites.empty()) {
				error = wxString::Format("Alternative %zu must contain at least one single item or composite.", altIndex + 1);
				return false;
			}
			for (size_t itemIndex = 0; itemIndex < alternative.singleItems.size(); ++itemIndex) {
				const DoodadSingleItemRecord &item = alternative.singleItems[itemIndex];
				if (item.itemId <= 0) {
					error = wxString::Format("Alternative %zu single item %zu must use a positive item id.", altIndex + 1, itemIndex + 1);
					return false;
				}
				if (!IsKnownItemId(item.itemId)) {
					error = wxString::Format("Alternative %zu single item %zu uses unknown item id %d.", altIndex + 1, itemIndex + 1, item.itemId);
					return false;
				}
				wxString ownerName;
				if (isOwnedByDifferentRuntimeBrush(item.itemId, ownerName)) {
					error = wxString::Format(
						"Alternative %zu single item %zu uses item id %d, which already belongs to brush \"%s\" in the runtime catalog.",
						altIndex + 1,
						itemIndex + 1,
						item.itemId,
						ownerName
					);
					return false;
				}
			}
			for (size_t compositeIndex = 0; compositeIndex < alternative.composites.size(); ++compositeIndex) {
				const DoodadCompositeRecord &composite = alternative.composites[compositeIndex];
				if (composite.tiles.empty()) {
					error = wxString::Format("Alternative %zu composite %zu must contain at least one tile.", altIndex + 1, compositeIndex + 1);
					return false;
				}
				for (size_t tileIndex = 0; tileIndex < composite.tiles.size(); ++tileIndex) {
					const DoodadCompositeTileRecord &tile = composite.tiles[tileIndex];
					if (tile.items.empty()) {
						error = wxString::Format(
							"Alternative %zu composite %zu tile %zu must contain at least one item.",
							altIndex + 1,
							compositeIndex + 1,
							tileIndex + 1
						);
						return false;
					}
					for (size_t tileItemIndex = 0; tileItemIndex < tile.items.size(); ++tileItemIndex) {
						if (tile.items[tileItemIndex].itemId <= 0) {
							error = wxString::Format(
								"Alternative %zu composite %zu tile %zu item %zu must use a positive item id.",
								altIndex + 1,
								compositeIndex + 1,
								tileIndex + 1,
								tileItemIndex + 1
							);
							return false;
						}
						if (!IsKnownItemId(tile.items[tileItemIndex].itemId)) {
							error = wxString::Format(
								"Alternative %zu composite %zu tile %zu item %zu uses unknown item id %d.",
								altIndex + 1,
								compositeIndex + 1,
								tileIndex + 1,
								tileItemIndex + 1,
								tile.items[tileItemIndex].itemId
							);
							return false;
						}
						wxString ownerName;
						if (isOwnedByDifferentRuntimeBrush(tile.items[tileItemIndex].itemId, ownerName)) {
							error = wxString::Format(
								"Alternative %zu composite %zu tile %zu item %zu uses item id %d, which already belongs to brush \"%s\" in the runtime catalog.",
								altIndex + 1,
								compositeIndex + 1,
								tileIndex + 1,
								tileItemIndex + 1,
								tile.items[tileItemIndex].itemId,
								ownerName
							);
							return false;
						}
					}
				}
			}
		}
	}

	error.clear();
	return true;
}

bool MaterialsWorkbenchBrushPanel::SaveCurrentBrush() {
	if (!hasBrush_) {
		SetStatusMessage("Select a brush before saving.");
		return false;
	}

	const VariationEditorState previousVariationState = CaptureVariationEditorState();
	CommitVariationEditorState();
	brushStorage_ = BuildEditableStorageFromCurrentState();

	wxString validationError;
	if (!ValidateBrushStorage(validationError)) {
		spdlog::warn(
			"Materials Workbench blocked brush save for id={} name='{}': {}",
			static_cast<long long>(brushStorage_.brush.id),
			brushStorage_.brush.name.ToStdString(),
			validationError.ToStdString()
		);
		SetStatusMessage("Cannot save brush: " + validationError);
		return false;
	}

	wxString error;
	const wxString previousName = loadedBrushStorage_.brush.name;
	if (!controller_.SaveBrushDetails(brushStorage_, error)) {
		spdlog::warn(
			"Materials Workbench failed to save brush after validation: id={} name='{}' type='{}' error='{}'",
			static_cast<long long>(brushStorage_.brush.id),
			brushStorage_.brush.name.ToStdString(),
			brushStorage_.brush.type.ToStdString(),
			error.ToStdString()
		);
		SetStatusMessage("Failed to save brush: " + error);
		return false;
	}

	loadedBrushStorage_ = brushStorage_;
	runtimeSyncedBrushStorage_ = brushStorage_;
	hasRuntimeSyncedBrushStorage_ = true;
	PopulateFields();
	if (previousVariationState.valid) {
		RestoreVariationEditorState(previousVariationState);
	}
	RefreshDirtyState();
	SetStatusMessage("Saved brush metadata and variations to materials.db. Targeted runtime sync remained in place.");
	spdlog::info(
		"Materials Workbench saved brush and variations: id={} old_name='{}' new_name='{}' type='{}' preserved_context={}",
		static_cast<long long>(brushStorage_.brush.id),
		previousName.ToStdString(),
		brushStorage_.brush.name.ToStdString(),
		brushStorage_.brush.type.ToStdString(),
		previousVariationState.valid
	);

	if (onBrushSaved_) {
		onBrushSaved_(brushStorage_.brush.id, previousName, brushStorage_.brush.name);
	}
	return true;
}

void MaterialsWorkbenchBrushPanel::OnSave(wxCommandEvent &WXUNUSED(event)) {
	SaveCurrentBrush();
}

void MaterialsWorkbenchBrushPanel::OnRevert(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_) {
		ClearWorkspace("Select a brush in the navigation tree to edit its properties.");
		return;
	}

	const int64_t brushId = brushStorage_.brush.id;
	const wxString brushName = brushStorage_.brush.name;

	if (!LoadBrush(currentContextKey_, currentItemIndex_)) {
		spdlog::warn(
			"Materials Workbench failed to revert brush from materials.db: id={} name='{}'",
			static_cast<long long>(brushId),
			brushName.ToStdString()
		);
		return;
	}

	SetStatusMessage("Reverted local brush edits and reloaded metadata and variations from materials.db.");
	spdlog::info(
		"Materials Workbench reverted brush from materials.db: id={} name='{}'",
		static_cast<long long>(brushId),
		brushName.ToStdString()
	);
}

void MaterialsWorkbenchBrushPanel::OnMetadataFieldChanged(wxCommandEvent &event) {
	if (internalUpdate_ || !hasBrush_) {
		event.Skip();
		return;
	}

	UpdateWorkspaceHeader();
	RefreshLookIdOwnershipHints();
	RefreshVariationEditor();
	RefreshDirtyState();
	event.Skip();
}

void MaterialsWorkbenchBrushPanel::OnAddGroundItem(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesGroundVariationEditor()) {
		return;
	}

	int itemId = groundItemIdCtrl_ ? groundItemIdCtrl_->GetValue() : 0;
	int chance = 1;
	if (!ShowWeightedBrushItemDialog(this, "Add Ground Variant", itemId, chance)) {
		return;
	}

	BrushItemRecord item;
	item.itemId = itemId;
	item.chance = chance;
	brushStorage_.items.push_back(item);
	groundItemIndex_ = static_cast<int>(brushStorage_.items.size()) - 1;
	RefreshGroundItemList();
	RefreshGroundSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage(wxString::Format("Added ground variant %d.", itemId));
}

void MaterialsWorkbenchBrushPanel::OnRemoveGroundItem(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesGroundVariationEditor()) {
		return;
	}
	if (groundItemIndex_ < 0 || groundItemIndex_ >= static_cast<int>(brushStorage_.items.size())) {
		SetStatusMessage("Select a ground item before removing it.");
		return;
	}

	brushStorage_.items.erase(brushStorage_.items.begin() + groundItemIndex_);
	if (groundItemIndex_ >= static_cast<int>(brushStorage_.items.size())) {
		groundItemIndex_ = static_cast<int>(brushStorage_.items.size()) - 1;
	}
	RefreshGroundItemList();
	RefreshGroundSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Removed ground variation item.");
}

void MaterialsWorkbenchBrushPanel::OnGroundItemSelected(wxCommandEvent &event) {
	groundItemIndex_ = event.GetSelection();
	RefreshGroundSelection();
}

void MaterialsWorkbenchBrushPanel::OnGroundCardsPaint(wxPaintEvent &WXUNUSED(event)) {
	if (!groundItemsCardsPanel_) {
		return;
	}

	wxAutoBufferedPaintDC dc(groundItemsCardsPanel_);
	const wxRect clientRect = groundItemsCardsPanel_->GetClientRect();
	const wxColour panelColour = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
	const wxColour textColour = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
	const wxColour mutedText(150, 156, 170);
	dc.SetBackground(wxBrush(panelColour));
	dc.Clear();

	groundItemCardRects_ = BuildWeightedBrushCardRects(groundItemsCardsPanel_, clientRect, brushStorage_.items.size());
	if (brushStorage_.items.empty()) {
		dc.SetTextForeground(mutedText);
		dc.DrawLabel("Add weighted variants to see visual cards and a compact preview grid.", clientRect, wxALIGN_CENTER);
		return;
	}

	const int totalChance = std::max(1, SumWeightedBrushChances(brushStorage_.items));
	for (size_t i = 0; i < brushStorage_.items.size() && i < groundItemCardRects_.size(); ++i) {
		const BrushItemRecord &item = brushStorage_.items[i];
		const wxRect &cardRect = groundItemCardRects_[i];
		const bool selected = static_cast<int>(i) == groundItemIndex_;
		const wxColour accent = GetWeightedBrushBadgeColour(brushStorage_.items, i);
		const wxColour cardFill = selected ? wxColour(38, 46, 60) : wxColour(28, 31, 38);
		const wxColour border = selected ? accent : wxColour(72, 76, 88);
		dc.SetPen(wxPen(border, selected ? 2 : 1));
		dc.SetBrush(wxBrush(cardFill));
		dc.DrawRoundedRectangle(cardRect, groundItemsCardsPanel_->FromDIP(6));

		wxRect spriteCell = cardRect;
		spriteCell.Deflate(groundItemsCardsPanel_->FromDIP(10), groundItemsCardsPanel_->FromDIP(10));
		spriteCell.SetWidth(groundItemsCardsPanel_->FromDIP(56));
		dc.SetPen(wxPen(wxColour(84, 92, 110)));
		dc.SetBrush(wxBrush(wxColour(22, 25, 32)));
		dc.DrawRoundedRectangle(spriteCell, groundItemsCardsPanel_->FromDIP(4));
		if (item.itemId > 0) {
			const wxRect spriteBounds = GetDoodadPreviewSpriteRect(item.itemId, wxPoint(0, 0));
			wxPoint drawPoint(
				spriteCell.x + std::max(0, (spriteCell.width - spriteBounds.width) / 2) - spriteBounds.x,
				spriteCell.y + std::max(0, (spriteCell.height - spriteBounds.height) / 2) - spriteBounds.y
			);
			DrawDoodadPreviewItemSprite(dc, item.itemId, drawPoint);
		}

		const int textX = spriteCell.GetRight() + 1 + groundItemsCardsPanel_->FromDIP(10);
		const int titleY = cardRect.y + groundItemsCardsPanel_->FromDIP(10);
		const int detailY = titleY + groundItemsCardsPanel_->FromDIP(22);
		dc.SetTextForeground(textColour);
		dc.DrawText(wxString::Format("%zu. item %d", i + 1, item.itemId), textX, titleY);
		dc.SetTextForeground(mutedText);
		dc.DrawText(
			wxString::Format("chance %d | %s", item.chance, FormatWeightedBrushPercent(brushStorage_.items, i)),
			textX,
			detailY
		);

		wxString badge = GetWeightedBrushBadge(brushStorage_.items, i);
		wxSize badgeSize = dc.GetTextExtent(badge);
		wxRect badgeRect(
			cardRect.GetRight() - groundItemsCardsPanel_->FromDIP(12) - badgeSize.x - groundItemsCardsPanel_->FromDIP(12),
			cardRect.y + groundItemsCardsPanel_->FromDIP(10),
			badgeSize.x + groundItemsCardsPanel_->FromDIP(12),
			groundItemsCardsPanel_->FromDIP(22)
		);
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(accent));
		dc.DrawRoundedRectangle(badgeRect, groundItemsCardsPanel_->FromDIP(10));
		dc.SetTextForeground(wxColour(24, 28, 34));
		dc.DrawLabel(badge, badgeRect, wxALIGN_CENTER);

		wxRect barRect(
			textX,
			cardRect.GetBottom() - groundItemsCardsPanel_->FromDIP(16),
			cardRect.GetRight() - textX - groundItemsCardsPanel_->FromDIP(12),
			groundItemsCardsPanel_->FromDIP(6)
		);
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(wxColour(54, 58, 70)));
		dc.DrawRoundedRectangle(barRect, groundItemsCardsPanel_->FromDIP(3));
		wxRect fillRect = barRect;
		fillRect.width = std::max(groundItemsCardsPanel_->FromDIP(6), static_cast<int>(std::lround(static_cast<double>(barRect.width) * item.chance / totalChance)));
		fillRect.width = std::min(fillRect.width, barRect.width);
		dc.SetBrush(wxBrush(accent));
		dc.DrawRoundedRectangle(fillRect, groundItemsCardsPanel_->FromDIP(3));
	}
}

void MaterialsWorkbenchBrushPanel::OnGroundCardsLeftDown(wxMouseEvent &event) {
	if (!hasBrush_ || !UsesGroundVariationEditor() || !groundItemsCardsPanel_) {
		event.Skip();
		return;
	}
	groundItemCardRects_ = BuildWeightedBrushCardRects(groundItemsCardsPanel_, groundItemsCardsPanel_->GetClientRect(), brushStorage_.items.size());
	for (size_t i = 0; i < groundItemCardRects_.size(); ++i) {
		if (groundItemCardRects_[i].Contains(event.GetPosition())) {
			groundItemIndex_ = static_cast<int>(i);
			if (groundItemsList_) {
				groundItemsList_->SetSelection(groundItemIndex_);
			}
			RefreshGroundSelection();
			return;
		}
	}
	event.Skip();
}

void MaterialsWorkbenchBrushPanel::OnGroundCardsRightDown(wxMouseEvent &event) {
	if (!hasBrush_ || !UsesGroundVariationEditor() || !groundItemsCardsPanel_) {
		event.Skip();
		return;
	}
	groundItemCardRects_ = BuildWeightedBrushCardRects(groundItemsCardsPanel_, groundItemsCardsPanel_->GetClientRect(), brushStorage_.items.size());
	for (size_t i = 0; i < groundItemCardRects_.size(); ++i) {
		if (!groundItemCardRects_[i].Contains(event.GetPosition())) {
			continue;
		}

		int itemId = brushStorage_.items[i].itemId;
		int chance = brushStorage_.items[i].chance;
		if (ShowWeightedBrushItemDialog(this, "Edit Ground Variant", itemId, chance)) {
			brushStorage_.items[i].itemId = itemId;
			brushStorage_.items[i].chance = chance;
			RefreshGroundItemList();
			RefreshGroundSelection();
			UpdateSummary();
			RefreshDirtyState();
			SetStatusMessage(wxString::Format("Updated ground variant %d.", itemId));
		}
		return;
	}
	event.Skip();
}

void MaterialsWorkbenchBrushPanel::OnGroundPreviewPaint(wxPaintEvent &WXUNUSED(event)) {
	if (!groundPreviewPanel_) {
		return;
	}

	wxAutoBufferedPaintDC dc(groundPreviewPanel_);
	const wxRect clientRect = groundPreviewPanel_->GetClientRect();
	dc.SetBackground(wxBrush(wxColour(20, 24, 32)));
	dc.Clear();
	groundPreviewTileRects_.clear();

	if (brushStorage_.items.empty()) {
		dc.SetTextForeground(wxColour(150, 156, 170));
		dc.DrawLabel("The seamless variation grid will appear here once weighted variants exist.", clientRect, wxALIGN_CENTER);
		return;
	}

	const int tileCell = groundPreviewPanel_->FromDIP(32);
	const int columns = std::max(1, clientRect.width / std::max(1, tileCell));
	groundPreviewTileRects_.reserve(brushStorage_.items.size());

	for (size_t i = 0; i < brushStorage_.items.size(); ++i) {
		const int row = static_cast<int>(i) / columns;
		const int column = static_cast<int>(i) % columns;
		wxRect cellRect(
			column * tileCell,
			row * tileCell,
			tileCell,
			tileCell
		);
		groundPreviewTileRects_.push_back(cellRect);

		const bool selected = static_cast<int>(i) == groundItemIndex_;
		const wxColour accent = GetWeightedBrushBadgeColour(brushStorage_.items, i);
		if (brushStorage_.items[i].itemId > 0) {
			const wxRect spriteBounds = GetDoodadPreviewSpriteRect(brushStorage_.items[i].itemId, wxPoint(0, 0));
			wxPoint drawPoint(
				cellRect.x + std::max(0, (cellRect.width - spriteBounds.width) / 2) - spriteBounds.x,
				cellRect.y + std::max(0, (cellRect.height - spriteBounds.height) / 2) - spriteBounds.y
			);
			DrawDoodadPreviewItemSprite(dc, brushStorage_.items[i].itemId, drawPoint);
		}

		if (groundPreviewHighlightEnabled_) {
			const wxColour overlayColour(accent.Red(), accent.Green(), accent.Blue(), selected ? 88 : 56);
			dc.SetPen(wxPen(accent, selected ? 2 : 1));
			dc.SetBrush(wxBrush(overlayColour));
			dc.DrawRectangle(cellRect);
		} else if (selected) {
			dc.SetPen(wxPen(accent, 2));
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.DrawRectangle(cellRect);
		}
	}
}

void MaterialsWorkbenchBrushPanel::OnGroundPreviewLeftDown(wxMouseEvent &event) {
	if (!hasBrush_ || !UsesGroundVariationEditor() || !groundPreviewPanel_) {
		event.Skip();
		return;
	}

	const wxPoint position = event.GetPosition();
	for (size_t i = 0; i < groundPreviewTileRects_.size() && i < brushStorage_.items.size(); ++i) {
		if (!groundPreviewTileRects_[i].Contains(position)) {
			continue;
		}

		groundItemIndex_ = static_cast<int>(i);
		if (groundItemsList_) {
			groundItemsList_->SetSelection(groundItemIndex_);
		}
		RefreshGroundSelection();
		return;
	}

	event.Skip();
}

void MaterialsWorkbenchBrushPanel::OnGroundPreviewMotion(wxMouseEvent &event) {
	if (!groundPreviewPanel_) {
		event.Skip();
		return;
	}

	const wxPoint position = event.GetPosition();
	for (size_t i = 0; i < groundPreviewTileRects_.size() && i < brushStorage_.items.size(); ++i) {
		if (!groundPreviewTileRects_[i].Contains(position)) {
			continue;
		}

		groundPreviewPanel_->SetToolTip(
			wxString::Format(
				"Item ID %d | %s | chance %d | badge %s",
				brushStorage_.items[i].itemId,
				FormatWeightedBrushPercent(brushStorage_.items, i),
				brushStorage_.items[i].chance,
				GetWeightedBrushBadge(brushStorage_.items, i)
			)
		);
		return;
	}

	groundPreviewPanel_->UnsetToolTip();
	event.Skip();
}

void MaterialsWorkbenchBrushPanel::OnGroundPreviewMouseLeave(wxMouseEvent &event) {
	if (groundPreviewPanel_) {
		groundPreviewPanel_->UnsetToolTip();
	}
	event.Skip();
}

void MaterialsWorkbenchBrushPanel::OnGroundPreviewSize(wxSizeEvent &event) {
	RefreshGroundPreviewLayout();
	if (groundPreviewPanel_) {
		groundPreviewPanel_->Refresh();
	}
	event.Skip();
}

void MaterialsWorkbenchBrushPanel::OnGroundPreviewHighlightToggled(wxCommandEvent &WXUNUSED(event)) {
	groundPreviewHighlightEnabled_ = groundPreviewHighlightCtrl_ && groundPreviewHighlightCtrl_->GetValue();
	if (groundPreviewPanel_) {
		groundPreviewPanel_->Refresh();
	}
}

void MaterialsWorkbenchBrushPanel::OnGroundItemValueChanged(wxCommandEvent &WXUNUSED(event)) {
	if (internalUpdate_ || !hasBrush_ || !UsesGroundVariationEditor()) {
		return;
	}
	if (groundItemIndex_ < 0 || groundItemIndex_ >= static_cast<int>(brushStorage_.items.size())) {
		return;
	}

	brushStorage_.items[groundItemIndex_].itemId = groundItemIdCtrl_->GetValue();
	brushStorage_.items[groundItemIndex_].chance = groundItemChanceCtrl_->GetValue();
	RefreshGroundItemList();
	if (groundItemIndex_ >= 0) {
		groundItemsList_->SetSelection(groundItemIndex_);
	}
	UpdateItemOwnershipHint(groundItemOwnershipLabel_, groundItemIdCtrl_->GetValue(), true);
	RefreshGroundPreviewState();
	UpdateSummary();
	RefreshDirtyState();
}

void MaterialsWorkbenchBrushPanel::OnAddAlignedNode(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesAlignedVariationEditor()) {
		return;
	}
	if (GetEffectiveBrushType() == "carpet") {
		wxString targetAlign = FindNextMissingCarpetAlign(brushStorage_.carpetNodes, alignedPendingCarpetAlign_);
		if (targetAlign.IsEmpty()) {
			SetStatusMessage("All carpet contexts are already configured. Remove one before adding another.");
			return;
		}
		CarpetNodeRecord node;
		node.align = targetAlign;
		brushStorage_.carpetNodes.push_back(node);
		alignedNodeIndex_ = static_cast<int>(brushStorage_.carpetNodes.size()) - 1;
		alignedPendingCarpetAlign_ = node.align;
	} else {
		wxString targetAlign = FindNextMissingTableAlign(brushStorage_.tableNodes, alignedPendingTableAlign_);
		if (targetAlign.IsEmpty()) {
			SetStatusMessage("All table states are already configured. Remove one before adding another.");
			return;
		}
		TableNodeRecord node;
		node.align = targetAlign;
		TableNodeItemRecord item;
		item.chance = kDefaultNewTableNodeChance;
		node.items.push_back(item);
		brushStorage_.tableNodes.push_back(node);
		alignedNodeIndex_ = static_cast<int>(brushStorage_.tableNodes.size()) - 1;
		alignedPendingTableAlign_ = node.align;
	}
	RefreshAlignedNodeList();
	RefreshAlignedSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage(GetEffectiveBrushType() == "table" ? "Added table state in the selected empty slot." : "Added carpet context in the selected map slot.");
}

void MaterialsWorkbenchBrushPanel::OnRemoveAlignedNode(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesAlignedVariationEditor()) {
		return;
	}
	if (GetEffectiveBrushType() == "carpet") {
		auto &nodes = brushStorage_.carpetNodes;
		if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			SetStatusMessage("Select a carpet context before removing it.");
			return;
		}
		const wxString removedAlign = nodes[alignedNodeIndex_].align;
		nodes.erase(nodes.begin() + alignedNodeIndex_);
		alignedNodeIndex_ = -1;
		alignedItemIndex_ = -1;
		alignedPendingCarpetAlign_ = removedAlign;
	} else {
		auto &nodes = brushStorage_.tableNodes;
		if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			SetStatusMessage("Select a node before removing it.");
			return;
		}
		const wxString removedAlign = nodes[alignedNodeIndex_].align;
		nodes.erase(nodes.begin() + alignedNodeIndex_);
		alignedNodeIndex_ = -1;
		alignedItemIndex_ = -1;
		alignedPendingTableAlign_ = removedAlign;
	}
	RefreshAlignedNodeList();
	RefreshAlignedSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage(GetEffectiveBrushType() == "table" ? "Removed table state." : "Removed carpet context.");
}

void MaterialsWorkbenchBrushPanel::OnAlignedNodeSelected(wxCommandEvent &event) {
	alignedNodeIndex_ = event.GetSelection();
	if (GetEffectiveBrushType() == "carpet" && alignedNodeIndex_ >= 0 &&
		alignedNodeIndex_ < static_cast<int>(brushStorage_.carpetNodes.size())) {
		alignedPendingCarpetAlign_ = brushStorage_.carpetNodes[alignedNodeIndex_].align;
	} else if (GetEffectiveBrushType() == "table" && alignedNodeIndex_ >= 0 &&
		alignedNodeIndex_ < static_cast<int>(brushStorage_.tableNodes.size())) {
		alignedPendingTableAlign_ = brushStorage_.tableNodes[alignedNodeIndex_].align;
	}
	RefreshAlignedSelection();
}

void MaterialsWorkbenchBrushPanel::OnAlignedNodeAlignChanged(wxCommandEvent &WXUNUSED(event)) {
	if (internalUpdate_ || !hasBrush_ || !UsesAlignedVariationEditor()) {
		return;
	}
	if (GetEffectiveBrushType() == "carpet") {
		return;
	} else {
		auto &nodes = brushStorage_.tableNodes;
		if (!alignedNodeAlignChoice_) {
			return;
		}
		const wxString selectedAlign = alignedNodeAlignChoice_->GetStringSelection();
		if (selectedAlign.IsEmpty()) {
			return;
		}
		if (alignedNodeIndex_ >= 0 && alignedNodeIndex_ < static_cast<int>(nodes.size())) {
			if (FindTableNodeIndexByAlignExcluding(nodes, selectedAlign, alignedNodeIndex_) >= 0) {
				internalUpdate_ = true;
				const int previousChoice = alignedNodeAlignChoice_->FindString(nodes[alignedNodeIndex_].align);
				alignedNodeAlignChoice_->SetSelection(previousChoice);
				internalUpdate_ = false;
				SetStatusMessage("That table state already exists. Pick an empty slot instead.");
				return;
			}
			nodes[alignedNodeIndex_].align = selectedAlign;
			alignedPendingTableAlign_ = selectedAlign;
		} else {
			const int existingNodeIndex = FindAlignedNodeIndexByAlign(nodes, selectedAlign);
			if (existingNodeIndex >= 0) {
				alignedNodeIndex_ = existingNodeIndex;
				alignedPendingTableAlign_ = nodes[existingNodeIndex].align;
			} else {
				alignedPendingTableAlign_ = selectedAlign;
			}
			RefreshAlignedSelection();
			return;
		}
	}
	RefreshAlignedNodeList();
	if (alignedNodeIndex_ >= 0) {
		alignedNodesList_->SetSelection(alignedNodeIndex_);
	}
	RefreshDirtyState();
}

void MaterialsWorkbenchBrushPanel::OnAddAlignedItem(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesAlignedVariationEditor()) {
		return;
	}
	if (GetEffectiveBrushType() == "carpet") {
		auto &nodes = brushStorage_.carpetNodes;
		if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			SetStatusMessage("Select a carpet context before adding variants.");
			return;
		}
		int itemId = 0;
		int chance = 1;
		if (!ShowWeightedBrushItemDialogWithPreview(this, "Add Carpet Variant", itemId, chance)) {
			return;
		}
		CarpetNodeItemRecord item;
		item.itemId = itemId;
		item.chance = chance;
		nodes[alignedNodeIndex_].items.push_back(item);
		alignedItemIndex_ = static_cast<int>(nodes[alignedNodeIndex_].items.size()) - 1;
	} else {
		if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(brushStorage_.tableNodes.size())) {
			SetStatusMessage("Add or select a node before adding items.");
			return;
		}
		AddTableItemToNodeWithDialog(alignedNodeIndex_);
		return;
	}
	RefreshAlignedSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage(GetEffectiveBrushType() == "table" ? "Added node item." : "Added carpet variant.");
}

void MaterialsWorkbenchBrushPanel::OnRemoveAlignedItem(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesAlignedVariationEditor()) {
		return;
	}
	if (GetEffectiveBrushType() == "carpet") {
		auto &nodes = brushStorage_.carpetNodes;
		if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			SetStatusMessage("Select a carpet context first.");
			return;
		}
		auto &items = nodes[alignedNodeIndex_].items;
		if (alignedItemIndex_ < 0 || alignedItemIndex_ >= static_cast<int>(items.size())) {
			SetStatusMessage("Select a carpet variant before removing it.");
			return;
		}
		items.erase(items.begin() + alignedItemIndex_);
		if (alignedItemIndex_ >= static_cast<int>(items.size())) {
			alignedItemIndex_ = static_cast<int>(items.size()) - 1;
		}
	} else {
		auto &nodes = brushStorage_.tableNodes;
		if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			SetStatusMessage("Select a node first.");
			return;
		}
		auto &items = nodes[alignedNodeIndex_].items;
		if (alignedItemIndex_ < 0 || alignedItemIndex_ >= static_cast<int>(items.size())) {
			SetStatusMessage("Select an item before removing it.");
			return;
		}
		items.erase(items.begin() + alignedItemIndex_);
		if (alignedItemIndex_ >= static_cast<int>(items.size())) {
			alignedItemIndex_ = static_cast<int>(items.size()) - 1;
		}
	}
	RefreshAlignedSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage(GetEffectiveBrushType() == "table" ? "Removed node item." : "Removed carpet variant.");
}

void MaterialsWorkbenchBrushPanel::OnAlignedItemSelected(wxCommandEvent &event) {
	alignedItemIndex_ = event.GetSelection();
	RefreshAlignedSelection();
}

void MaterialsWorkbenchBrushPanel::OnAlignedItemValueChanged(wxCommandEvent &WXUNUSED(event)) {
	if (internalUpdate_ || !hasBrush_ || !UsesAlignedVariationEditor()) {
		return;
	}
	if (GetEffectiveBrushType() == "carpet") {
		auto &nodes = brushStorage_.carpetNodes;
		if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			return;
		}
		auto &items = nodes[alignedNodeIndex_].items;
		if (alignedItemIndex_ < 0 || alignedItemIndex_ >= static_cast<int>(items.size())) {
			return;
		}
		items[alignedItemIndex_].itemId = alignedItemIdCtrl_->GetValue();
		items[alignedItemIndex_].chance = alignedItemChanceCtrl_->GetValue();
	} else {
		auto &nodes = brushStorage_.tableNodes;
		if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(nodes.size())) {
			return;
		}
		auto &items = nodes[alignedNodeIndex_].items;
		if (alignedItemIndex_ < 0 || alignedItemIndex_ >= static_cast<int>(items.size())) {
			return;
		}
		items[alignedItemIndex_].itemId = alignedItemIdCtrl_->GetValue();
		items[alignedItemIndex_].chance = alignedItemChanceCtrl_->GetValue();
	}
	RefreshAlignedItemList();
	if (alignedItemIndex_ >= 0) {
		alignedItemsList_->SetSelection(alignedItemIndex_);
	}
	UpdateItemOwnershipHint(alignedItemOwnershipLabel_, alignedItemIdCtrl_->GetValue(), true);
	RefreshDirtyState();
}

void MaterialsWorkbenchBrushPanel::OnAlignedContextPaint(wxPaintEvent &WXUNUSED(event)) {
	if (!alignedContextPanel_) {
		return;
	}

	wxAutoBufferedPaintDC dc(alignedContextPanel_);
	const wxRect clientRect = alignedContextPanel_->GetClientRect();
	dc.SetBackground(wxBrush(wxColour(20, 24, 32)));
	dc.Clear();
	alignedContextRects_.clear();
	alignedContextAddRects_.clear();
	alignedContextRectAligns_.clear();
	alignedContextRectTooltips_.clear();

	const wxString type = GetEffectiveBrushType();
	const auto &slots = GetAlignedContextSlots(type);
	if (slots.empty()) {
		return;
	}

	int maxColumn = 0;
	int maxRow = 0;
	for (const auto &slot : slots) {
		maxColumn = std::max(maxColumn, slot.column);
		maxRow = std::max(maxRow, slot.row);
	}

	const int columns = maxColumn + 1;
	const int rows = maxRow + 1;
	const bool compactTableCards = type == "table";
	const int padding = alignedContextPanel_->FromDIP(compactTableCards ? 8 : 12);
	const int gap = alignedContextPanel_->FromDIP(compactTableCards ? 6 : 8);
	int cellWidth = 0;
	int cellHeight = 0;
	int originX = padding;
	int originY = padding;
	if (compactTableCards) {
		cellWidth = alignedContextPanel_->FromDIP(72);
		cellHeight = alignedContextPanel_->FromDIP(64);
		const int clusterWidth = columns * cellWidth + (columns - 1) * gap;
		const int clusterHeight = rows * cellHeight + (rows - 1) * gap;
		originX = clientRect.x + std::max(0, (clientRect.width - clusterWidth) / 2);
		originY = clientRect.y + std::max(0, (clientRect.height - clusterHeight) / 2);
	} else {
		cellWidth = std::max(alignedContextPanel_->FromDIP(56), (clientRect.width - padding * 2 - gap * (columns - 1)) / std::max(1, columns));
		cellHeight = std::max(alignedContextPanel_->FromDIP(56), (clientRect.height - padding * 2 - gap * (rows - 1)) / std::max(1, rows));
	}
	const wxColour textColour = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
	const wxColour mutedText(150, 156, 170);
	const auto drawTableCardCopy = [&](const wxRect &cellRect, const wxString &label, const wxString &stateText) {
		wxRect titleRect = cellRect;
		titleRect.Deflate(alignedContextPanel_->FromDIP(6), alignedContextPanel_->FromDIP(6));
		titleRect.SetHeight(alignedContextPanel_->FromDIP(28));
		dc.SetTextForeground(textColour);
		dc.DrawLabel(label, titleRect, wxALIGN_CENTER_HORIZONTAL | wxALIGN_TOP);

		wxRect stateRect = cellRect;
		stateRect.Deflate(alignedContextPanel_->FromDIP(6), alignedContextPanel_->FromDIP(6));
		stateRect.SetTop(cellRect.y + alignedContextPanel_->FromDIP(34));
		stateRect.SetHeight(alignedContextPanel_->FromDIP(16));
		dc.SetTextForeground(mutedText);
		dc.DrawLabel(stateText, stateRect, wxALIGN_CENTER_HORIZONTAL | wxALIGN_TOP);
	};

	for (const auto &slot : slots) {
		wxRect cellRect(
			originX + slot.column * (cellWidth + gap),
			originY + slot.row * (cellHeight + gap),
			cellWidth,
			cellHeight
		);
		alignedContextRects_.push_back(cellRect);
		alignedContextAddRects_.push_back(wxRect());
		alignedContextRectAligns_.push_back(wxString::FromUTF8(slot.align));
		if (type == "table") {
			wxString label = wxString::FromUTF8(slot.label);
			label.Replace("\n", " ");
			alignedContextRectTooltips_.push_back(
				wxString::Format("%s\n%s", label, wxString::FromUTF8(slot.description))
			);
		} else {
			alignedContextRectTooltips_.push_back(
				wxString::Format("%s\n%s", wxString::FromUTF8(slot.label), wxString::FromUTF8(slot.description))
			);
		}

		int nodeIndex = -1;
		if (type == "table") {
			nodeIndex = FindAlignedNodeIndexByAlign(brushStorage_.tableNodes, wxString::FromUTF8(slot.align));
		} else {
			nodeIndex = FindAlignedNodeIndexByAlign(brushStorage_.carpetNodes, wxString::FromUTF8(slot.align));
		}

		const bool exists = nodeIndex >= 0;
		const bool selected = exists
			? nodeIndex == alignedNodeIndex_
			: ((type == "table" && alignedPendingTableAlign_.CmpNoCase(wxString::FromUTF8(slot.align)) == 0) ||
			   (type == "carpet" && alignedPendingCarpetAlign_.CmpNoCase(wxString::FromUTF8(slot.align)) == 0));
		const wxColour accent = selected ? wxColour(80, 166, 255) : (exists ? wxColour(91, 194, 139) : wxColour(176, 102, 0));
		const wxColour fill = selected ? wxColour(38, 46, 60) : (exists ? wxColour(28, 31, 38) : wxColour(34, 29, 26));
		dc.SetPen(wxPen(accent, selected ? 2 : 1));
		dc.SetBrush(wxBrush(fill));
		dc.DrawRoundedRectangle(cellRect, alignedContextPanel_->FromDIP(6));

		if (!compactTableCards) {
			wxRect titleRect = cellRect;
			titleRect.Deflate(alignedContextPanel_->FromDIP(8), alignedContextPanel_->FromDIP(8));
			dc.SetTextForeground(textColour);
			dc.DrawText(wxString::FromUTF8(slot.label), titleRect.x, titleRect.y);
		}

		if (exists) {
			int itemId = 0;
			size_t itemCount = 0;
			if (type == "table") {
				const auto &node = brushStorage_.tableNodes[static_cast<size_t>(nodeIndex)];
				itemCount = node.items.size();
				if (!node.items.empty()) {
					itemId = node.items.front().itemId;
				}
			} else {
				const auto &node = brushStorage_.carpetNodes[static_cast<size_t>(nodeIndex)];
				itemCount = node.items.size();
				if (!node.items.empty()) {
					itemId = node.items.front().itemId;
				}
			}

			wxRect previewRect = cellRect;
			previewRect.Deflate(alignedContextPanel_->FromDIP(compactTableCards ? 8 : 10), alignedContextPanel_->FromDIP(compactTableCards ? 8 : 12));
			previewRect.SetTop(previewRect.y + (type == "table" ? alignedContextPanel_->FromDIP(14) : alignedContextPanel_->FromDIP(10)));
			previewRect.SetHeight(std::max(alignedContextPanel_->FromDIP(32), previewRect.height - (type == "table" ? alignedContextPanel_->FromDIP(18) : alignedContextPanel_->FromDIP(24))));
			if (type == "table" && itemCount == 0) {
				drawTableCardCopy(cellRect, wxString::FromUTF8(slot.label), "No items");
			} else if (type == "table") {
				DrawAlignedTableContextScene(dc, alignedContextPanel_, cellRect, wxString::FromUTF8(slot.align), itemId);
			} else {
				DrawAlignedCarpetContextScene(dc, alignedContextPanel_, previewRect, wxString::FromUTF8(slot.align), itemId, selected);
			}

			if (type == "table") {
				wxRect addRect(
					cellRect.GetRight() - alignedContextPanel_->FromDIP(24),
					cellRect.GetBottom() - alignedContextPanel_->FromDIP(24),
					alignedContextPanel_->FromDIP(18),
					alignedContextPanel_->FromDIP(18)
				);
				alignedContextAddRects_.back() = addRect;
				dc.SetPen(*wxTRANSPARENT_PEN);
				dc.SetBrush(wxBrush(selected ? wxColour(80, 166, 255) : wxColour(91, 194, 139)));
				dc.DrawRoundedRectangle(addRect, alignedContextPanel_->FromDIP(6));
				dc.SetTextForeground(wxColour(20, 24, 32));
				dc.DrawLabel("+", addRect, wxALIGN_CENTER);
			} else {
				wxString countBadge = wxString::Format("%zu v", itemCount);
				wxSize badgeSize = dc.GetTextExtent(countBadge);
				wxRect badgeRect(
					cellRect.GetRight() - alignedContextPanel_->FromDIP(12) - badgeSize.x - alignedContextPanel_->FromDIP(10),
					cellRect.y + alignedContextPanel_->FromDIP(10),
					badgeSize.x + alignedContextPanel_->FromDIP(10),
					alignedContextPanel_->FromDIP(18)
				);
				dc.SetPen(*wxTRANSPARENT_PEN);
				dc.SetBrush(wxBrush(selected ? wxColour(80, 166, 255) : wxColour(91, 194, 139)));
				dc.DrawRoundedRectangle(badgeRect, alignedContextPanel_->FromDIP(8));
				dc.SetTextForeground(wxColour(20, 24, 32));
				dc.DrawLabel(countBadge, badgeRect, wxALIGN_CENTER);
			}

			if (!compactTableCards) {
				dc.SetTextForeground(mutedText);
				dc.DrawLabel(
					wxString::Format("%zu item%s", itemCount, itemCount == 1 ? "" : "s"),
					wxRect(cellRect.x, cellRect.GetBottom() - alignedContextPanel_->FromDIP(28), cellRect.width, alignedContextPanel_->FromDIP(16)),
					wxALIGN_CENTER
				);
			}
		} else if (compactTableCards) {
			drawTableCardCopy(cellRect, wxString::FromUTF8(slot.label), "Empty");
		} else if (!compactTableCards) {
			wxRect emptyRect = cellRect;
			emptyRect.Deflate(alignedContextPanel_->FromDIP(10), alignedContextPanel_->FromDIP(10));
			dc.SetPen(wxPen(wxColour(112, 120, 136), 1, wxPENSTYLE_SHORT_DASH));
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.DrawRoundedRectangle(emptyRect, alignedContextPanel_->FromDIP(5));
			dc.SetTextForeground(mutedText);
			dc.DrawLabel(
				selected ? "empty slot" : "missing",
				wxRect(cellRect.x, cellRect.y + (cellRect.height / 2) - alignedContextPanel_->FromDIP(16), cellRect.width, alignedContextPanel_->FromDIP(16)),
				wxALIGN_CENTER
			);
			wxRect addRect(
				cellRect.GetRight() - alignedContextPanel_->FromDIP(28),
				cellRect.GetBottom() - alignedContextPanel_->FromDIP(24),
				alignedContextPanel_->FromDIP(20),
				alignedContextPanel_->FromDIP(18)
			);
			alignedContextAddRects_.back() = addRect;
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.SetBrush(wxBrush(selected ? wxColour(80, 166, 255) : wxColour(176, 102, 0)));
			dc.DrawRoundedRectangle(addRect, alignedContextPanel_->FromDIP(6));
			dc.SetTextForeground(wxColour(20, 24, 32));
			dc.DrawLabel("+", addRect, wxALIGN_CENTER);
		}
	}
}

void MaterialsWorkbenchBrushPanel::OnAlignedContextLeftDown(wxMouseEvent &event) {
	if (!hasBrush_ || !UsesAlignedVariationEditor() || !alignedContextPanel_) {
		event.Skip();
		return;
	}

	const wxPoint position = event.GetPosition();
	for (size_t i = 0; i < alignedContextRects_.size() && i < alignedContextRectAligns_.size(); ++i) {
		if (!alignedContextRects_[i].Contains(position)) {
			continue;
		}

		int nodeIndex = -1;
		if (GetEffectiveBrushType() == "table") {
			nodeIndex = FindAlignedNodeIndexByAlign(brushStorage_.tableNodes, alignedContextRectAligns_[i]);
		} else {
			nodeIndex = FindAlignedNodeIndexByAlign(brushStorage_.carpetNodes, alignedContextRectAligns_[i]);
		}
		if (GetEffectiveBrushType() == "table" &&
			i < alignedContextAddRects_.size() &&
			!alignedContextAddRects_[i].IsEmpty() &&
			alignedContextAddRects_[i].Contains(position) &&
			nodeIndex >= 0) {
			AddTableItemToNodeWithDialog(nodeIndex);
			return;
		}
		if (GetEffectiveBrushType() == "carpet" &&
			i < alignedContextAddRects_.size() &&
			!alignedContextAddRects_[i].IsEmpty() &&
			alignedContextAddRects_[i].Contains(position) &&
			nodeIndex < 0) {
			alignedNodeIndex_ = -1;
			alignedItemIndex_ = -1;
			alignedPendingCarpetAlign_ = alignedContextRectAligns_[i];
			wxCommandEvent dummy;
			OnAddAlignedNode(dummy);
			return;
		}
		if (nodeIndex < 0) {
			if (GetEffectiveBrushType() == "table") {
				alignedNodeIndex_ = -1;
				alignedItemIndex_ = -1;
				alignedPendingTableAlign_ = alignedContextRectAligns_[i];
				RefreshAlignedSelection();
				SetStatusMessage("Selected an empty table state. Add Node will create it here.");
				return;
			}
			alignedNodeIndex_ = -1;
			alignedItemIndex_ = -1;
			alignedPendingCarpetAlign_ = alignedContextRectAligns_[i];
			RefreshAlignedSelection();
			SetStatusMessage("Selected an empty carpet slot. Add Context will create it here.");
			return;
		}

		alignedNodeIndex_ = nodeIndex;
		if (GetEffectiveBrushType() == "table") {
			alignedPendingTableAlign_ = alignedContextRectAligns_[i];
		} else {
			alignedPendingCarpetAlign_ = alignedContextRectAligns_[i];
		}
		if (alignedNodesList_) {
			alignedNodesList_->SetSelection(alignedNodeIndex_);
		}
		RefreshAlignedSelection();
		return;
	}

	event.Skip();
}

void MaterialsWorkbenchBrushPanel::OnAlignedContextMotion(wxMouseEvent &event) {
	if (!alignedContextPanel_) {
		event.Skip();
		return;
	}

	const wxPoint position = event.GetPosition();
	for (size_t i = 0; i < alignedContextRects_.size() && i < alignedContextRectTooltips_.size(); ++i) {
		if (!alignedContextRects_[i].Contains(position)) {
			continue;
		}
		alignedContextPanel_->SetToolTip(alignedContextRectTooltips_[i]);
		return;
	}

	alignedContextPanel_->UnsetToolTip();
	event.Skip();
}

void MaterialsWorkbenchBrushPanel::OnAlignedContextMouseLeave(wxMouseEvent &event) {
	if (alignedContextPanel_) {
		alignedContextPanel_->UnsetToolTip();
	}
	event.Skip();
}

void MaterialsWorkbenchBrushPanel::OnAlignedItemsCardsPaint(wxPaintEvent &WXUNUSED(event)) {
	if (!alignedItemsCardsPanel_) {
		return;
	}

	wxAutoBufferedPaintDC dc(alignedItemsCardsPanel_);
	const wxRect clientRect = alignedItemsCardsPanel_->GetClientRect();
	const wxColour panelColour = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
	const wxColour textColour = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
	const wxColour mutedText(150, 156, 170);
	dc.SetBackground(wxBrush(panelColour));
	dc.Clear();
	alignedItemCardRects_.clear();
	alignedItemEditRects_.clear();
	alignedItemRemoveRects_.clear();

	if (!hasBrush_ || !UsesAlignedVariationEditor()) {
		return;
	}

	const wxString type = GetEffectiveBrushType();
	if (type == "carpet") {
		if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(brushStorage_.carpetNodes.size())) {
			dc.SetTextForeground(mutedText);
			dc.DrawLabel("Select a carpet context in the layout map to reveal visual variant cards.", clientRect, wxALIGN_CENTER);
			return;
		}

		const auto &items = brushStorage_.carpetNodes[alignedNodeIndex_].items;
		alignedItemCardRects_ = BuildWeightedBrushCardRects(alignedItemsCardsPanel_, clientRect, items.size());
		if (items.empty()) {
			dc.SetTextForeground(mutedText);
			dc.DrawLabel("Add Variant opens a preview dialog so you can pick the first carpet item visually.", clientRect, wxALIGN_CENTER);
			return;
		}

		const int totalChance = std::max(1, SumAlignedItemChances(items));
		for (size_t i = 0; i < items.size() && i < alignedItemCardRects_.size(); ++i) {
			const wxRect &cardRect = alignedItemCardRects_[i];
			alignedItemEditRects_.push_back(wxRect());
			alignedItemRemoveRects_.push_back(wxRect());
			const bool selected = static_cast<int>(i) == alignedItemIndex_;
			const wxColour accent = GetAlignedItemBadgeColour(items, i);
			const wxColour cardFill = selected ? wxColour(38, 46, 60) : wxColour(28, 31, 38);
			const wxColour border = selected ? accent : wxColour(72, 76, 88);
			dc.SetPen(wxPen(border, selected ? 2 : 1));
			dc.SetBrush(wxBrush(cardFill));
			dc.DrawRoundedRectangle(cardRect, alignedItemsCardsPanel_->FromDIP(6));

			wxRect spriteCell = cardRect;
			spriteCell.Deflate(alignedItemsCardsPanel_->FromDIP(10), alignedItemsCardsPanel_->FromDIP(10));
			spriteCell.SetWidth(alignedItemsCardsPanel_->FromDIP(56));
			dc.SetPen(wxPen(wxColour(84, 92, 110)));
			dc.SetBrush(wxBrush(wxColour(22, 25, 32)));
			dc.DrawRoundedRectangle(spriteCell, alignedItemsCardsPanel_->FromDIP(4));
			if (items[i].itemId > 0) {
				DrawCenteredPreviewItemSprite(dc, spriteCell, items[i].itemId);
			}

			const int textX = spriteCell.GetRight() + 1 + alignedItemsCardsPanel_->FromDIP(10);
			const int titleY = cardRect.y + alignedItemsCardsPanel_->FromDIP(10);
			const int detailY = titleY + alignedItemsCardsPanel_->FromDIP(22);
			dc.SetTextForeground(textColour);
			dc.DrawText(wxString::Format("%zu. item %d", i + 1, items[i].itemId), textX, titleY);
			dc.SetTextForeground(mutedText);
			dc.DrawText(
				wxString::Format("chance %d | %s", items[i].chance, FormatAlignedItemPercent(items, i)),
				textX,
				detailY
			);

			wxString badge = GetAlignedItemBadge(items, i);
			wxSize badgeSize = dc.GetTextExtent(badge);
			wxRect badgeRect(
				cardRect.GetRight() - alignedItemsCardsPanel_->FromDIP(12) - badgeSize.x - alignedItemsCardsPanel_->FromDIP(12),
				cardRect.y + alignedItemsCardsPanel_->FromDIP(10),
				badgeSize.x + alignedItemsCardsPanel_->FromDIP(12),
				alignedItemsCardsPanel_->FromDIP(22)
			);
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.SetBrush(wxBrush(accent));
			dc.DrawRoundedRectangle(badgeRect, alignedItemsCardsPanel_->FromDIP(10));
			dc.SetTextForeground(wxColour(24, 28, 34));
			dc.DrawLabel(badge, badgeRect, wxALIGN_CENTER);

			wxRect removeRect(
				badgeRect.GetRight() - alignedItemsCardsPanel_->FromDIP(22),
				badgeRect.GetBottom() + alignedItemsCardsPanel_->FromDIP(6),
				alignedItemsCardsPanel_->FromDIP(22),
				alignedItemsCardsPanel_->FromDIP(18)
			);
			alignedItemRemoveRects_.back() = removeRect;
			dc.SetBrush(wxBrush(wxColour(204, 74, 74)));
			dc.DrawRoundedRectangle(removeRect, alignedItemsCardsPanel_->FromDIP(6));
			dc.SetTextForeground(wxColour(20, 24, 32));
			dc.DrawLabel("-", removeRect, wxALIGN_CENTER);

			wxRect editRect(
				removeRect.x - alignedItemsCardsPanel_->FromDIP(26),
				removeRect.y,
				alignedItemsCardsPanel_->FromDIP(22),
				removeRect.height
			);
			alignedItemEditRects_.back() = editRect;
			dc.SetBrush(wxBrush(wxColour(80, 166, 255)));
			dc.DrawRoundedRectangle(editRect, alignedItemsCardsPanel_->FromDIP(6));
			dc.SetTextForeground(wxColour(20, 24, 32));
			dc.DrawLabel("E", editRect, wxALIGN_CENTER);

			wxRect barRect(
				textX,
				cardRect.GetBottom() - alignedItemsCardsPanel_->FromDIP(16),
				cardRect.GetRight() - textX - alignedItemsCardsPanel_->FromDIP(12),
				alignedItemsCardsPanel_->FromDIP(6)
			);
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.SetBrush(wxBrush(wxColour(54, 58, 70)));
			dc.DrawRoundedRectangle(barRect, alignedItemsCardsPanel_->FromDIP(3));
			wxRect fillRect = barRect;
			fillRect.width = std::max(alignedItemsCardsPanel_->FromDIP(6), static_cast<int>(std::lround(static_cast<double>(barRect.width) * items[i].chance / totalChance)));
			fillRect.width = std::min(fillRect.width, barRect.width);
			dc.SetBrush(wxBrush(accent));
			dc.DrawRoundedRectangle(fillRect, alignedItemsCardsPanel_->FromDIP(3));
		}
		return;
	}

	if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(brushStorage_.tableNodes.size())) {
		dc.SetTextForeground(mutedText);
		dc.DrawLabel("Select a state to reveal its weighted items.", clientRect, wxALIGN_CENTER);
		return;
	}

	const auto &items = brushStorage_.tableNodes[alignedNodeIndex_].items;
	alignedItemCardRects_ = BuildWeightedBrushCardRects(alignedItemsCardsPanel_, clientRect, items.size());
	if (items.empty()) {
		dc.SetTextForeground(mutedText);
		dc.DrawLabel("This state has no items yet. Use + in the selected state to add the first one.", clientRect, wxALIGN_CENTER);
		return;
	}

	const int totalChance = std::max(1, SumAlignedItemChances(items));
	for (size_t i = 0; i < items.size() && i < alignedItemCardRects_.size(); ++i) {
		const wxRect &cardRect = alignedItemCardRects_[i];
		alignedItemEditRects_.push_back(wxRect());
		alignedItemRemoveRects_.push_back(wxRect());
		const bool selected = static_cast<int>(i) == alignedItemIndex_;
		const wxColour accent = GetAlignedItemBadgeColour(items, i);
		const wxColour cardFill = selected ? wxColour(38, 46, 60) : wxColour(28, 31, 38);
		const wxColour border = selected ? accent : wxColour(72, 76, 88);
		dc.SetPen(wxPen(border, selected ? 2 : 1));
		dc.SetBrush(wxBrush(cardFill));
		dc.DrawRoundedRectangle(cardRect, alignedItemsCardsPanel_->FromDIP(6));

		wxRect spriteCell = cardRect;
		spriteCell.Deflate(alignedItemsCardsPanel_->FromDIP(10), alignedItemsCardsPanel_->FromDIP(10));
		spriteCell.SetWidth(alignedItemsCardsPanel_->FromDIP(56));
		dc.SetPen(wxPen(wxColour(84, 92, 110)));
		dc.SetBrush(wxBrush(wxColour(22, 25, 32)));
		dc.DrawRoundedRectangle(spriteCell, alignedItemsCardsPanel_->FromDIP(4));
		if (items[i].itemId > 0) {
			DrawCenteredPreviewItemSprite(dc, spriteCell, items[i].itemId);
		}

		const int textX = spriteCell.GetRight() + 1 + alignedItemsCardsPanel_->FromDIP(10);
		const int titleY = cardRect.y + alignedItemsCardsPanel_->FromDIP(10);
		const int detailY = titleY + alignedItemsCardsPanel_->FromDIP(22);
		dc.SetTextForeground(textColour);
		dc.DrawText(wxString::Format("%zu. item %d", i + 1, items[i].itemId), textX, titleY);
		dc.SetTextForeground(mutedText);
		dc.DrawText(
			wxString::Format("chance %d | %s", items[i].chance, FormatAlignedItemPercent(items, i)),
			textX,
			detailY
		);

		wxString badge = GetAlignedItemBadge(items, i);
		wxSize badgeSize = dc.GetTextExtent(badge);
		wxRect badgeRect(
			cardRect.GetRight() - alignedItemsCardsPanel_->FromDIP(12) - badgeSize.x - alignedItemsCardsPanel_->FromDIP(12),
			cardRect.y + alignedItemsCardsPanel_->FromDIP(10),
			badgeSize.x + alignedItemsCardsPanel_->FromDIP(12),
			alignedItemsCardsPanel_->FromDIP(22)
		);
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(accent));
		dc.DrawRoundedRectangle(badgeRect, alignedItemsCardsPanel_->FromDIP(10));
		dc.SetTextForeground(wxColour(24, 28, 34));
		dc.DrawLabel(badge, badgeRect, wxALIGN_CENTER);

		wxRect removeRect(
			badgeRect.GetRight() - alignedItemsCardsPanel_->FromDIP(22),
			badgeRect.GetBottom() + alignedItemsCardsPanel_->FromDIP(6),
			alignedItemsCardsPanel_->FromDIP(22),
			alignedItemsCardsPanel_->FromDIP(18)
		);
		alignedItemRemoveRects_.back() = removeRect;
		dc.SetBrush(wxBrush(wxColour(204, 74, 74)));
		dc.DrawRoundedRectangle(removeRect, alignedItemsCardsPanel_->FromDIP(6));
		dc.SetTextForeground(wxColour(20, 24, 32));
		dc.DrawLabel("-", removeRect, wxALIGN_CENTER);

		wxRect editRect(
			removeRect.x - alignedItemsCardsPanel_->FromDIP(26),
			removeRect.y,
			alignedItemsCardsPanel_->FromDIP(22),
			removeRect.height
		);
		alignedItemEditRects_.back() = editRect;
		dc.SetBrush(wxBrush(wxColour(80, 166, 255)));
		dc.DrawRoundedRectangle(editRect, alignedItemsCardsPanel_->FromDIP(6));
		dc.SetTextForeground(wxColour(20, 24, 32));
		dc.DrawLabel("E", editRect, wxALIGN_CENTER);

		wxRect barRect(
			textX,
			cardRect.GetBottom() - alignedItemsCardsPanel_->FromDIP(16),
			cardRect.GetRight() - textX - alignedItemsCardsPanel_->FromDIP(12),
			alignedItemsCardsPanel_->FromDIP(6)
		);
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(wxColour(54, 58, 70)));
		dc.DrawRoundedRectangle(barRect, alignedItemsCardsPanel_->FromDIP(3));
		wxRect fillRect = barRect;
		fillRect.width = std::max(alignedItemsCardsPanel_->FromDIP(6), static_cast<int>(std::lround(static_cast<double>(barRect.width) * items[i].chance / totalChance)));
		fillRect.width = std::min(fillRect.width, barRect.width);
		dc.SetBrush(wxBrush(accent));
		dc.DrawRoundedRectangle(fillRect, alignedItemsCardsPanel_->FromDIP(3));
	}
}

void MaterialsWorkbenchBrushPanel::OnAlignedItemsCardsLeftDown(wxMouseEvent &event) {
	if (!hasBrush_ || !UsesAlignedVariationEditor() || !alignedItemsCardsPanel_) {
		event.Skip();
		return;
	}

	const wxPoint position = event.GetPosition();
	if (GetEffectiveBrushType() == "table" || GetEffectiveBrushType() == "carpet") {
		const bool isTable = GetEffectiveBrushType() == "table";
		for (size_t i = 0; i < alignedItemEditRects_.size(); ++i) {
			if (!alignedItemEditRects_[i].IsEmpty() && alignedItemEditRects_[i].Contains(position)) {
				if (isTable) {
					if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(brushStorage_.tableNodes.size()) ||
						i >= brushStorage_.tableNodes[alignedNodeIndex_].items.size()) {
						return;
					}
					alignedItemIndex_ = static_cast<int>(i);
					EditTableItemWithDialog(alignedNodeIndex_, static_cast<int>(i));
					return;
				}
				if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(brushStorage_.carpetNodes.size()) ||
					i >= brushStorage_.carpetNodes[alignedNodeIndex_].items.size()) {
					return;
				}
				int itemId = brushStorage_.carpetNodes[alignedNodeIndex_].items[i].itemId;
				int chance = brushStorage_.carpetNodes[alignedNodeIndex_].items[i].chance;
				if (ShowWeightedBrushItemDialogWithPreview(this, "Edit Carpet Variant", itemId, chance)) {
					auto &item = brushStorage_.carpetNodes[alignedNodeIndex_].items[i];
					item.itemId = itemId;
					item.chance = chance;
					alignedItemIndex_ = static_cast<int>(i);
					RefreshAlignedSelection();
					UpdateSummary();
					RefreshDirtyState();
					SetStatusMessage(wxString::Format("Updated carpet variant item %d.", itemId));
				}
				return;
			}
		}
		for (size_t i = 0; i < alignedItemRemoveRects_.size(); ++i) {
			if (!alignedItemRemoveRects_[i].IsEmpty() && alignedItemRemoveRects_[i].Contains(position)) {
				if (isTable) {
					if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(brushStorage_.tableNodes.size()) ||
						i >= brushStorage_.tableNodes[alignedNodeIndex_].items.size()) {
						return;
					}
					auto &items = brushStorage_.tableNodes[alignedNodeIndex_].items;
					alignedItemIndex_ = static_cast<int>(i);
					items.erase(items.begin() + static_cast<std::ptrdiff_t>(i));
					if (alignedItemIndex_ >= static_cast<int>(items.size())) {
						alignedItemIndex_ = static_cast<int>(items.size()) - 1;
					}
					RefreshAlignedSelection();
					UpdateSummary();
					RefreshDirtyState();
					SetStatusMessage("Removed node item.");
					return;
				}
				if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(brushStorage_.carpetNodes.size()) ||
					i >= brushStorage_.carpetNodes[alignedNodeIndex_].items.size()) {
					return;
				}
				auto &items = brushStorage_.carpetNodes[alignedNodeIndex_].items;
				alignedItemIndex_ = static_cast<int>(i);
				items.erase(items.begin() + static_cast<std::ptrdiff_t>(i));
				if (alignedItemIndex_ >= static_cast<int>(items.size())) {
					alignedItemIndex_ = static_cast<int>(items.size()) - 1;
				}
				RefreshAlignedSelection();
				UpdateSummary();
				RefreshDirtyState();
				SetStatusMessage("Removed carpet variant.");
				return;
			}
		}
	}
	for (size_t i = 0; i < alignedItemCardRects_.size(); ++i) {
		if (!alignedItemCardRects_[i].Contains(position)) {
			continue;
		}

		alignedItemIndex_ = static_cast<int>(i);
		if (alignedItemsList_) {
			alignedItemsList_->SetSelection(alignedItemIndex_);
		}
		RefreshAlignedSelection();
		return;
	}

	event.Skip();
}

void MaterialsWorkbenchBrushPanel::OnAlignedItemsCardsRightDown(wxMouseEvent &event) {
	if (!hasBrush_ || !UsesAlignedVariationEditor() || !alignedItemsCardsPanel_) {
		event.Skip();
		return;
	}

	const wxPoint position = event.GetPosition();
	for (size_t i = 0; i < alignedItemCardRects_.size(); ++i) {
		if (!alignedItemCardRects_[i].Contains(position)) {
			continue;
		}

		int itemId = 0;
		int chance = 1;
		if (GetEffectiveBrushType() == "table") {
			if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(brushStorage_.tableNodes.size()) ||
				i >= brushStorage_.tableNodes[alignedNodeIndex_].items.size()) {
				return;
			}
			EditTableItemWithDialog(alignedNodeIndex_, static_cast<int>(i));
			return;
		}
		if (alignedNodeIndex_ < 0 || alignedNodeIndex_ >= static_cast<int>(brushStorage_.carpetNodes.size()) ||
			i >= brushStorage_.carpetNodes[alignedNodeIndex_].items.size()) {
			return;
		}
		itemId = brushStorage_.carpetNodes[alignedNodeIndex_].items[i].itemId;
		chance = brushStorage_.carpetNodes[alignedNodeIndex_].items[i].chance;

		const wxString dialogTitle = "Edit Carpet Variant";
		if (ShowWeightedBrushItemDialogWithPreview(this, dialogTitle, itemId, chance)) {
			auto &item = brushStorage_.carpetNodes[alignedNodeIndex_].items[i];
			item.itemId = itemId;
			item.chance = chance;
			alignedItemIndex_ = static_cast<int>(i);
			RefreshAlignedSelection();
			UpdateSummary();
			RefreshDirtyState();
			SetStatusMessage(wxString::Format("Updated carpet variant item %d.", itemId));
		}
		return;
	}

	event.Skip();
}

void MaterialsWorkbenchBrushPanel::OnAddDoodadAlternative(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	brushStorage_.doodadAlternatives.emplace_back();
	doodadAlternativeIndex_ = static_cast<int>(brushStorage_.doodadAlternatives.size()) - 1;
	doodadPreviewAuthoringFloors_.clear();
	RefreshDoodadAlternativeList();
	RefreshDoodadSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Added doodad alternative.");
}

void MaterialsWorkbenchBrushPanel::OnRemoveDoodadAlternative(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		SetStatusMessage("Select an alternative before removing it.");
		return;
	}
	brushStorage_.doodadAlternatives.erase(brushStorage_.doodadAlternatives.begin() + doodadAlternativeIndex_);
	if (doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		doodadAlternativeIndex_ = static_cast<int>(brushStorage_.doodadAlternatives.size()) - 1;
	}
	doodadPreviewAuthoringFloors_.clear();
	RefreshDoodadAlternativeList();
	RefreshDoodadSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Removed doodad alternative.");
}

void MaterialsWorkbenchBrushPanel::OnAddDoodadSingleItem(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		SetStatusMessage("Add or select an alternative before adding single items.");
		return;
	}

	int itemId = doodadSingleItemIdCtrl_ ? doodadSingleItemIdCtrl_->GetValue() : 0;
	int chance = 1;
	if (!ShowDoodadSingleItemDialog(this, "Add Single Item", itemId, chance)) {
		return;
	}

	DoodadSingleItemRecord item;
	item.itemId = itemId;
	item.chance = chance;
	auto &singleItems = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].singleItems;
	singleItems.push_back(item);
	doodadSingleItemIndex_ = static_cast<int>(singleItems.size()) - 1;
	doodadPreviewPreferComposite_ = false;
	RefreshDoodadSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage(wxString::Format("Added doodad single item %d.", itemId));
}

void MaterialsWorkbenchBrushPanel::OnRemoveDoodadSingleItem(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		SetStatusMessage("Select an alternative first.");
		return;
	}
	auto &singleItems = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].singleItems;
	if (doodadSingleItemIndex_ < 0 || doodadSingleItemIndex_ >= static_cast<int>(singleItems.size())) {
		SetStatusMessage("Select a single item before removing it.");
		return;
	}
	singleItems.erase(singleItems.begin() + doodadSingleItemIndex_);
	if (doodadSingleItemIndex_ >= static_cast<int>(singleItems.size())) {
		doodadSingleItemIndex_ = static_cast<int>(singleItems.size()) - 1;
	}
	RefreshDoodadSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Removed doodad single item.");
}

void MaterialsWorkbenchBrushPanel::OnDoodadSingleItemSelected(wxCommandEvent &event) {
	doodadSingleItemIndex_ = event.GetSelection();
	doodadPreviewPreferComposite_ = false;
	RefreshDoodadSelection();
}

void MaterialsWorkbenchBrushPanel::OnDoodadSingleItemRightDown(wxMouseEvent &event) {
	if (!hasBrush_ || !UsesDoodadVariationEditor() || !doodadSingleItemsList_) {
		event.Skip();
		return;
	}

	const int index = doodadSingleItemsList_->HitTest(event.GetPosition());
	if (index == wxNOT_FOUND || doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		event.Skip();
		return;
	}

	auto &singleItems = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].singleItems;
	if (index < 0 || index >= static_cast<int>(singleItems.size())) {
		event.Skip();
		return;
	}

	int itemId = singleItems[static_cast<size_t>(index)].itemId;
	int chance = singleItems[static_cast<size_t>(index)].chance;
	if (ShowDoodadSingleItemDialog(this, "Edit Single Item", itemId, chance)) {
		singleItems[static_cast<size_t>(index)].itemId = itemId;
		singleItems[static_cast<size_t>(index)].chance = chance;
		doodadSingleItemIndex_ = index;
		doodadPreviewPreferComposite_ = false;
		RefreshDoodadSelection();
		RefreshDoodadAlternativeList();
		UpdateSummary();
		RefreshDirtyState();
		SetStatusMessage(wxString::Format("Updated doodad single item %d.", itemId));
	}
}

void MaterialsWorkbenchBrushPanel::OnDoodadSingleItemValueChanged(wxCommandEvent &WXUNUSED(event)) {
	if (internalUpdate_ || !hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		return;
	}
	auto &singleItems = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].singleItems;
	if (doodadSingleItemIndex_ < 0 || doodadSingleItemIndex_ >= static_cast<int>(singleItems.size())) {
		return;
	}
	singleItems[doodadSingleItemIndex_].itemId = doodadSingleItemIdCtrl_->GetValue();
	singleItems[doodadSingleItemIndex_].chance = doodadSingleItemChanceCtrl_->GetValue();
	doodadPreviewPreferComposite_ = false;
	RefreshDoodadSingleItemList();
	if (doodadSingleItemIndex_ >= 0) {
		doodadSingleItemsList_->SetSelection(doodadSingleItemIndex_);
	}
	RefreshDoodadAlternativeList();
	UpdateItemOwnershipHint(doodadSingleItemOwnershipLabel_, doodadSingleItemIdCtrl_->GetValue(), true);
	RefreshDoodadPreview();
	RefreshDirtyState();
}

void MaterialsWorkbenchBrushPanel::OnAddDoodadComposite(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		SetStatusMessage("Add or select an alternative before adding composites.");
		return;
	}

	int chance = 1;
	if (!ShowDoodadChanceDialog(this, "Add Composite", chance)) {
		return;
	}

	DoodadCompositeRecord composite;
	composite.chance = chance;
	auto &composites = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].composites;
	composites.push_back(composite);
	doodadCompositeIndex_ = static_cast<int>(composites.size()) - 1;
	doodadPreviewAuthoringFloors_.clear();
	doodadPreviewPreferComposite_ = true;
	RefreshDoodadSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage(wxString::Format("Added doodad composite with chance %d.", chance));
}

void MaterialsWorkbenchBrushPanel::OnRemoveDoodadComposite(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		SetStatusMessage("Select an alternative first.");
		return;
	}
	auto &composites = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].composites;
	if (doodadCompositeIndex_ < 0 || doodadCompositeIndex_ >= static_cast<int>(composites.size())) {
		SetStatusMessage("Select a composite before removing it.");
		return;
	}
	composites.erase(composites.begin() + doodadCompositeIndex_);
	if (doodadCompositeIndex_ >= static_cast<int>(composites.size())) {
		doodadCompositeIndex_ = static_cast<int>(composites.size()) - 1;
	}
	doodadPreviewAuthoringFloors_.clear();
	RefreshDoodadSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Removed doodad composite.");
}

void MaterialsWorkbenchBrushPanel::OnDoodadCompositeSelected(wxCommandEvent &event) {
	doodadCompositeIndex_ = event.GetSelection();
	doodadPreviewAuthoringFloors_.clear();
	doodadPreviewPreferComposite_ = true;
	RefreshDoodadSelection();
}

void MaterialsWorkbenchBrushPanel::OnDoodadCompositeRightDown(wxMouseEvent &event) {
	if (!hasBrush_ || !UsesDoodadVariationEditor() || !doodadCompositesList_) {
		event.Skip();
		return;
	}

	const int index = doodadCompositesList_->HitTest(event.GetPosition());
	if (index == wxNOT_FOUND || doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		event.Skip();
		return;
	}

	auto &composites = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].composites;
	if (index < 0 || index >= static_cast<int>(composites.size())) {
		event.Skip();
		return;
	}

	int chance = composites[static_cast<size_t>(index)].chance;
	if (ShowDoodadChanceDialog(this, "Edit Composite Chance", chance)) {
		composites[static_cast<size_t>(index)].chance = chance;
		doodadCompositeIndex_ = index;
		doodadPreviewAuthoringFloors_.clear();
		doodadPreviewPreferComposite_ = true;
		RefreshDoodadSelection();
		UpdateSummary();
		RefreshDirtyState();
		SetStatusMessage(wxString::Format("Updated composite chance to %d.", chance));
	}
}

void MaterialsWorkbenchBrushPanel::OnDoodadCompositeChanceChanged(wxCommandEvent &WXUNUSED(event)) {
	if (internalUpdate_ || !hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		return;
	}
	auto &composites = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].composites;
	if (doodadCompositeIndex_ < 0 || doodadCompositeIndex_ >= static_cast<int>(composites.size())) {
		return;
	}
	composites[doodadCompositeIndex_].chance = doodadCompositeChanceCtrl_->GetValue();
	doodadPreviewPreferComposite_ = true;
	RefreshDoodadCompositeList();
	if (doodadCompositeIndex_ >= 0) {
		doodadCompositesList_->SetSelection(doodadCompositeIndex_);
	}
	RefreshDoodadAlternativeList();
	UpdateItemOwnershipHint(doodadTileItemOwnershipLabel_, doodadTileItemIdCtrl_->GetValue(), true);
	RefreshDoodadPreview();
	RefreshDirtyState();
}

void MaterialsWorkbenchBrushPanel::OnAddDoodadTile(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		SetStatusMessage("Select an alternative first.");
		return;
	}
	auto &composites = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].composites;
	if (doodadCompositeIndex_ < 0 || doodadCompositeIndex_ >= static_cast<int>(composites.size())) {
		SetStatusMessage("Select a composite before adding tiles.");
		return;
	}
	auto &tiles = composites[doodadCompositeIndex_].tiles;
	DoodadCompositeTileRecord tile;
	if (doodadTileIndex_ >= 0 && doodadTileIndex_ < static_cast<int>(tiles.size())) {
		const DoodadCompositeTileRecord &selectedTile = tiles[doodadTileIndex_];
		tile.offsetX = selectedTile.offsetX + 1;
		tile.offsetY = selectedTile.offsetY;
		tile.offsetZ = selectedTile.offsetZ;
	}
	tiles.push_back(tile);
	doodadTileIndex_ = static_cast<int>(tiles.size()) - 1;
	doodadTileItemIndex_ = tile.items.empty() ? -1 : 0;
	doodadPreviewPreferComposite_ = true;
	RefreshDoodadSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Added doodad composite tile and selected it.");
}

void MaterialsWorkbenchBrushPanel::OnRemoveDoodadTile(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		SetStatusMessage("Select an alternative first.");
		return;
	}
	auto &composites = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].composites;
	if (doodadCompositeIndex_ < 0 || doodadCompositeIndex_ >= static_cast<int>(composites.size())) {
		SetStatusMessage("Select a composite first.");
		return;
	}
	auto &tiles = composites[doodadCompositeIndex_].tiles;
	if (doodadTileIndex_ < 0 || doodadTileIndex_ >= static_cast<int>(tiles.size())) {
		SetStatusMessage("Select a tile before removing it.");
		return;
	}
	tiles.erase(tiles.begin() + doodadTileIndex_);
	if (doodadTileIndex_ >= static_cast<int>(tiles.size())) {
		doodadTileIndex_ = static_cast<int>(tiles.size()) - 1;
	}
	RefreshDoodadSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Removed doodad composite tile.");
}

void MaterialsWorkbenchBrushPanel::OnDoodadTileSelected(wxCommandEvent &event) {
	doodadTileIndex_ = event.GetSelection();
	doodadPreviewPreferComposite_ = true;
	RefreshDoodadSelection();
}

void MaterialsWorkbenchBrushPanel::OnDoodadTileOffsetChanged(wxCommandEvent &WXUNUSED(event)) {
	if (internalUpdate_ || !hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		return;
	}
	auto &composites = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].composites;
	if (doodadCompositeIndex_ < 0 || doodadCompositeIndex_ >= static_cast<int>(composites.size())) {
		return;
	}
	auto &tiles = composites[doodadCompositeIndex_].tiles;
	if (doodadTileIndex_ < 0 || doodadTileIndex_ >= static_cast<int>(tiles.size())) {
		return;
	}
	tiles[doodadTileIndex_].offsetX = doodadTileOffsetXCtrl_->GetValue();
	tiles[doodadTileIndex_].offsetY = doodadTileOffsetYCtrl_->GetValue();
	tiles[doodadTileIndex_].offsetZ = doodadTileOffsetZCtrl_->GetValue();
	doodadPreviewPreferComposite_ = true;
	RefreshDoodadTileList();
	if (doodadTileIndex_ >= 0) {
		doodadTilesList_->SetSelection(doodadTileIndex_);
	}
	RefreshDoodadPreview();
	RefreshDirtyState();
}

void MaterialsWorkbenchBrushPanel::OnAddDoodadTileItem(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		SetStatusMessage("Select an alternative first.");
		return;
	}
	auto &composites = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].composites;
	if (doodadCompositeIndex_ < 0 || doodadCompositeIndex_ >= static_cast<int>(composites.size())) {
		SetStatusMessage("Select a composite first.");
		return;
	}
	auto &tiles = composites[doodadCompositeIndex_].tiles;
	if (doodadTileIndex_ < 0 || doodadTileIndex_ >= static_cast<int>(tiles.size())) {
		SetStatusMessage("Select a tile before adding items.");
		return;
	}

	int itemId = doodadTileItemIdCtrl_ ? doodadTileItemIdCtrl_->GetValue() : 0;
	if (!ShowDoodadTileItemDialog(this, "Add Tile Layer", itemId)) {
		return;
	}

	DoodadCompositeTileItemRecord item;
	item.itemId = itemId;
	tiles[doodadTileIndex_].items.push_back(item);
	doodadTileItemIndex_ = static_cast<int>(tiles[doodadTileIndex_].items.size()) - 1;
	doodadPreviewPreferComposite_ = true;
	RefreshDoodadSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage(wxString::Format("Added tile layer %d.", itemId));
}

void MaterialsWorkbenchBrushPanel::OnRemoveDoodadTileItem(wxCommandEvent &WXUNUSED(event)) {
	if (!hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		SetStatusMessage("Select an alternative first.");
		return;
	}
	auto &composites = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].composites;
	if (doodadCompositeIndex_ < 0 || doodadCompositeIndex_ >= static_cast<int>(composites.size())) {
		SetStatusMessage("Select a composite first.");
		return;
	}
	auto &tiles = composites[doodadCompositeIndex_].tiles;
	if (doodadTileIndex_ < 0 || doodadTileIndex_ >= static_cast<int>(tiles.size())) {
		SetStatusMessage("Select a tile first.");
		return;
	}
	auto &items = tiles[doodadTileIndex_].items;
	if (doodadTileItemIndex_ < 0 || doodadTileItemIndex_ >= static_cast<int>(items.size())) {
		SetStatusMessage("Select a tile item before removing it.");
		return;
	}
	items.erase(items.begin() + doodadTileItemIndex_);
	if (doodadTileItemIndex_ >= static_cast<int>(items.size())) {
		doodadTileItemIndex_ = static_cast<int>(items.size()) - 1;
	}
	RefreshDoodadSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Removed doodad tile item.");
}

void MaterialsWorkbenchBrushPanel::OnDoodadTileItemSelected(wxCommandEvent &event) {
	doodadTileItemIndex_ = event.GetSelection();
	doodadPreviewPreferComposite_ = true;
	RefreshDoodadSelection();
}

void MaterialsWorkbenchBrushPanel::OnDoodadTileItemRightDown(wxMouseEvent &event) {
	if (!hasBrush_ || !UsesDoodadVariationEditor() || !doodadTileItemsList_) {
		event.Skip();
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		event.Skip();
		return;
	}

	auto &composites = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].composites;
	if (doodadCompositeIndex_ < 0 || doodadCompositeIndex_ >= static_cast<int>(composites.size())) {
		event.Skip();
		return;
	}

	auto &tiles = composites[doodadCompositeIndex_].tiles;
	if (doodadTileIndex_ < 0 || doodadTileIndex_ >= static_cast<int>(tiles.size())) {
		event.Skip();
		return;
	}

	const int index = doodadTileItemsList_->HitTest(event.GetPosition());
	auto &items = tiles[doodadTileIndex_].items;
	if (index == wxNOT_FOUND || index < 0 || index >= static_cast<int>(items.size())) {
		event.Skip();
		return;
	}

	int itemId = items[static_cast<size_t>(index)].itemId;
	if (ShowDoodadTileItemDialog(this, "Edit Tile Layer", itemId)) {
		items[static_cast<size_t>(index)].itemId = itemId;
		doodadTileItemIndex_ = index;
		doodadPreviewPreferComposite_ = true;
		RefreshDoodadSelection();
		UpdateSummary();
		RefreshDirtyState();
		SetStatusMessage(wxString::Format("Updated tile layer to %d.", itemId));
	}
}

void MaterialsWorkbenchBrushPanel::OnDoodadTileItemValueChanged(wxCommandEvent &WXUNUSED(event)) {
	if (internalUpdate_ || !hasBrush_ || !UsesDoodadVariationEditor()) {
		return;
	}
	if (doodadAlternativeIndex_ < 0 || doodadAlternativeIndex_ >= static_cast<int>(brushStorage_.doodadAlternatives.size())) {
		return;
	}
	auto &composites = brushStorage_.doodadAlternatives[doodadAlternativeIndex_].composites;
	if (doodadCompositeIndex_ < 0 || doodadCompositeIndex_ >= static_cast<int>(composites.size())) {
		return;
	}
	auto &tiles = composites[doodadCompositeIndex_].tiles;
	if (doodadTileIndex_ < 0 || doodadTileIndex_ >= static_cast<int>(tiles.size())) {
		UpdateItemOwnershipHint(doodadTileItemOwnershipLabel_, doodadTileItemIdCtrl_->GetValue(), false);
		return;
	}

	auto &items = tiles[doodadTileIndex_].items;
	bool createdFirstTileItem = false;
	if (doodadTileItemIndex_ < 0 || doodadTileItemIndex_ >= static_cast<int>(items.size())) {
		if (items.empty() && doodadTileItemIdCtrl_->GetValue() > 0) {
			DoodadCompositeTileItemRecord item;
			item.itemId = doodadTileItemIdCtrl_->GetValue();
			items.push_back(item);
			doodadTileItemIndex_ = 0;
			createdFirstTileItem = true;
		} else if (!items.empty()) {
			doodadTileItemIndex_ = 0;
		} else {
			UpdateItemOwnershipHint(doodadTileItemOwnershipLabel_, doodadTileItemIdCtrl_->GetValue(), true);
			return;
		}
	}

	items[doodadTileItemIndex_].itemId = doodadTileItemIdCtrl_->GetValue();
	doodadPreviewPreferComposite_ = true;
	RefreshDoodadTileItemList();
	if (doodadTileItemIndex_ >= 0) {
		doodadTileItemsList_->SetSelection(doodadTileItemIndex_);
	}
	RefreshDoodadTileList();
	if (doodadTileIndex_ >= 0) {
		doodadTilesList_->SetSelection(doodadTileIndex_);
	}
	RefreshDoodadCompositeList();
	if (doodadCompositeIndex_ >= 0) {
		doodadCompositesList_->SetSelection(doodadCompositeIndex_);
	}
	RefreshDoodadAlternativeList();
	UpdateItemOwnershipHint(doodadTileItemOwnershipLabel_, doodadTileItemIdCtrl_->GetValue(), true);
	if (createdFirstTileItem) {
		SetStatusMessage("Applied Item ID to the selected doodad tile.");
	}
	RefreshDoodadPreview();
	RefreshDirtyState();
}

void MaterialsWorkbenchBrushPanel::OnDoodadPreviewLeftDown(wxMouseEvent &event) {
	if (!hasBrush_ || !UsesDoodadVariationEditor() || !doodadPreviewPanel_) {
		event.Skip();
		return;
	}

	DoodadCompositeRecord* composite = GetSelectedDoodadComposite(brushStorage_, doodadAlternativeIndex_, doodadCompositeIndex_);
	if (!composite) {
		SetStatusMessage("Select a composite before editing the scene.");
		return;
	}

	wxClientDC dc(doodadPreviewPanel_);
	wxRect contentRect = doodadPreviewPanel_->GetClientRect();
	contentRect.Deflate(doodadPreviewPanel_->FromDIP(12));
	const int cellSize = doodadPreviewPanel_->FromDIP(32);
	const std::vector<DoodadPreviewFloorLayout> layouts = BuildDoodadPreviewLayouts(
		dc,
		contentRect,
		*composite,
		doodadPreviewFloor_,
		cellSize,
		doodadPreviewPanel_->FromDIP(18),
		doodadPreviewPanel_->FromDIP(4)
	);

	DoodadPreviewHit hit;
	if (!HitTestDoodadPreview(*composite, layouts, event.GetPosition(), cellSize, hit) || !hit.valid) {
		event.Skip();
		return;
	}

	if (hit.tileIndex >= 0) {
		doodadTileIndex_ = hit.tileIndex;
		doodadPreviewPreferComposite_ = true;
		if (!composite->tiles[hit.tileIndex].items.empty()) {
			doodadTileItemIndex_ = ClampIndexForCount(doodadTileItemIndex_, composite->tiles[hit.tileIndex].items.size());
		}
		if (event.ControlDown()) {
			const int itemId = doodadTileItemIdCtrl_ ? doodadTileItemIdCtrl_->GetValue() : 0;
			if (itemId <= 0) {
				SetStatusMessage("Set an Item ID before appending it to a tile from the scene editor.");
				RefreshDoodadSelection();
				return;
			}

			DoodadCompositeTileItemRecord item;
			item.itemId = itemId;
			composite->tiles[hit.tileIndex].items.push_back(item);
			doodadTileItemIndex_ = static_cast<int>(composite->tiles[hit.tileIndex].items.size()) - 1;
			RefreshDoodadSelection();
			UpdateSummary();
			RefreshDirtyState();
			SetStatusMessage(wxString::Format("Added item %d to the selected doodad tile.", itemId));
			return;
		}
		RefreshDoodadSelection();
		return;
	}

	DoodadCompositeTileRecord tile;
	tile.offsetX = hit.cellX;
	tile.offsetY = hit.cellY;
	if (doodadPreviewFloor_ != MaterialsWorkbenchBrushPanel::kDoodadPreviewAllFloors) {
		tile.offsetZ = doodadPreviewFloor_;
	} else if (doodadTileIndex_ >= 0 && doodadTileIndex_ < static_cast<int>(composite->tiles.size())) {
		tile.offsetZ = composite->tiles[static_cast<size_t>(doodadTileIndex_)].offsetZ;
	} else {
		tile.offsetZ = 0;
	}
	const int itemId = doodadTileItemIdCtrl_ ? doodadTileItemIdCtrl_->GetValue() : 0;
	if (itemId > 0 && event.ControlDown()) {
		DoodadCompositeTileItemRecord item;
		item.itemId = itemId;
		tile.items.push_back(item);
	}
	composite->tiles.push_back(tile);
	doodadTileIndex_ = static_cast<int>(composite->tiles.size()) - 1;
	doodadTileItemIndex_ = tile.items.empty() ? -1 : 0;
	doodadPreviewPreferComposite_ = true;
	RefreshDoodadSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Added doodad tile from the scene editor.");
}

void MaterialsWorkbenchBrushPanel::OnDoodadPreviewRightDown(wxMouseEvent &event) {
	if (!hasBrush_ || !UsesDoodadVariationEditor() || !doodadPreviewPanel_) {
		event.Skip();
		return;
	}

	DoodadCompositeRecord* composite = GetSelectedDoodadComposite(brushStorage_, doodadAlternativeIndex_, doodadCompositeIndex_);
	if (!composite || composite->tiles.empty()) {
		event.Skip();
		return;
	}

	wxClientDC dc(doodadPreviewPanel_);
	wxRect contentRect = doodadPreviewPanel_->GetClientRect();
	contentRect.Deflate(doodadPreviewPanel_->FromDIP(12));
	const int cellSize = doodadPreviewPanel_->FromDIP(32);
	const std::vector<DoodadPreviewFloorLayout> layouts = BuildDoodadPreviewLayouts(
		dc,
		contentRect,
		*composite,
		doodadPreviewFloor_,
		cellSize,
		doodadPreviewPanel_->FromDIP(18),
		doodadPreviewPanel_->FromDIP(4)
	);

	DoodadPreviewHit hit;
	if (!HitTestDoodadPreview(*composite, layouts, event.GetPosition(), cellSize, hit) || hit.tileIndex < 0) {
		event.Skip();
		return;
	}

	const int previousTileIndex = doodadTileIndex_;
	auto &tile = composite->tiles[static_cast<size_t>(hit.tileIndex)];
	doodadTileIndex_ = hit.tileIndex;
	doodadPreviewPreferComposite_ = true;
	if (!tile.items.empty()) {
		int itemIndex = tile.items.size() - 1;
		if (previousTileIndex == hit.tileIndex && doodadTileItemIndex_ >= 0 && doodadTileItemIndex_ < static_cast<int>(tile.items.size())) {
			itemIndex = doodadTileItemIndex_;
		}
		tile.items.erase(tile.items.begin() + itemIndex);
		doodadTileItemIndex_ = std::min(itemIndex, static_cast<int>(tile.items.size()) - 1);
		RefreshDoodadSelection();
		UpdateSummary();
		RefreshDirtyState();
		SetStatusMessage("Removed doodad tile item from the scene editor.");
		return;
	}

	composite->tiles.erase(composite->tiles.begin() + hit.tileIndex);
	if (doodadTileIndex_ >= static_cast<int>(composite->tiles.size())) {
		doodadTileIndex_ = static_cast<int>(composite->tiles.size()) - 1;
	}
	doodadTileItemIndex_ = -1;
	RefreshDoodadSelection();
	UpdateSummary();
	RefreshDirtyState();
	SetStatusMessage("Removed doodad tile from the scene editor.");
}
