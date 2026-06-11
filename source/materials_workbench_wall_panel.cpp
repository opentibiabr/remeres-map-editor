#include "main.h"

#include "materials_workbench_wall_panel.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <unordered_map>
#include <utility>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/radiobut.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/wrapsizer.h>

#include "common_windows.h"
#include "find_item_window.h"
#include "items.h"
#include "materials_workbench_controller.h"
#include "sprite_appearances.h"
#include "wall_brush.h"

namespace {
	enum class WallPanelDoorFamily {
		Unknown,
		Door,
		Window,
	};

	bool IsKnownWallPanelItemId(int itemId);

	struct WallPanelDoorTypeSpec {
		bool valid = false;
		bool allowAny = false;
		bool expectsExact = false;
		::DoorType exactType = WALL_UNDEFINED;
		WallPanelDoorFamily family = WallPanelDoorFamily::Unknown;
		wxString normalizedLabel;
	};

	class WallWorkspaceToggleButton : public ItemToggleButton {
	public:
		WallWorkspaceToggleButton(wxWindow* parent, int itemId) :
			ItemToggleButton(parent, RENDER_SIZE_64x64, itemId) {
		}
	};

	struct WallPreviewSpriteMetrics {
		int spriteId = 0;
		int widthPx = 32;
		int heightPx = 32;
		wxPoint drawOffset;
		int drawHeight = 0;

		bool isValid() const {
			return spriteId > 0;
		}
	};

	int ResolveWallPreviewLookId(int itemId) {
		if (!IsKnownWallPanelItemId(itemId)) {
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

	WallPreviewSpriteMetrics ResolveWallPreviewSpriteMetrics(int itemId) {
		WallPreviewSpriteMetrics metrics;
		if (!IsKnownWallPanelItemId(itemId)) {
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

		metrics.spriteId = ResolveWallPreviewLookId(itemId);
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

	wxBitmap BuildWallPreviewBitmap(int spriteId) {
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

	wxRect GetWallPreviewSpriteRect(int itemId, const wxPoint &tileAnchor) {
		const WallPreviewSpriteMetrics metrics = ResolveWallPreviewSpriteMetrics(itemId);
		if (!metrics.isValid()) {
			return wxRect(tileAnchor.x, tileAnchor.y, 32, 32);
		}

		return wxRect(
			tileAnchor.x - metrics.drawOffset.x,
			tileAnchor.y - metrics.drawOffset.y,
			metrics.widthPx,
			metrics.heightPx
		);
	}

	int ChooseWallPreviewItemId(const std::vector<WallPartItemRecord> &items) {
		if (items.empty()) {
			return 0;
		}
		int bestItemId = items.front().itemId;
		int bestChance = items.front().chance;
		for (const auto &item : items) {
			if (item.chance > bestChance) {
				bestChance = item.chance;
				bestItemId = item.itemId;
			}
		}
		return bestItemId;
	}

	wxString NormalizeWallPreviewPartType(const wxString &partType) {
		wxString trimmed(partType);
		trimmed.Trim(true).Trim(false);
		const wxString marker = "/alternate/";
		const wxString lower = trimmed.Lower();
		const int alternateIndex = lower.Find(marker);
		if (alternateIndex == wxNOT_FOUND) {
			return trimmed.Lower();
		}
		return trimmed.SubString(0, alternateIndex - 1).Lower();
	}

	wxString PartTypeForWallAlignment(uint32_t alignment) {
		switch (alignment) {
		case WALL_VERTICAL:
			return "vertical";
		case WALL_HORIZONTAL:
			return "horizontal";
		case WALL_POLE:
			return "pole";
		case WALL_SOUTH_END:
			return "south end";
		case WALL_EAST_END:
			return "east end";
		case WALL_NORTH_END:
			return "north end";
		case WALL_WEST_END:
			return "west end";
		case WALL_SOUTH_T:
			return "south t";
		case WALL_EAST_T:
			return "east t";
		case WALL_WEST_T:
			return "west t";
		case WALL_NORTH_T:
			return "north t";
		case WALL_NORTHWEST_DIAGONAL:
			return "northwest diagonal";
		case WALL_NORTHEAST_DIAGONAL:
			return "northeast diagonal";
		case WALL_SOUTHWEST_DIAGONAL:
			return "southwest diagonal";
		case WALL_SOUTHEAST_DIAGONAL:
			return "southeast diagonal";
		case WALL_INTERSECTION:
			return "intersection";
		case WALL_UNTOUCHABLE:
			return "untouchable";
		default:
			return "";
		}
	}

	wxString WallPreviewAlignmentTag(uint32_t alignment) {
		switch (alignment) {
		case WALL_VERTICAL:
			return "V";
		case WALL_HORIZONTAL:
			return "H";
		case WALL_POLE:
			return "P";
		case WALL_SOUTH_END:
			return "S-end";
		case WALL_EAST_END:
			return "E-end";
		case WALL_NORTH_END:
			return "N-end";
		case WALL_WEST_END:
			return "W-end";
		case WALL_SOUTH_T:
			return "S-T";
		case WALL_EAST_T:
			return "E-T";
		case WALL_WEST_T:
			return "W-T";
		case WALL_NORTH_T:
			return "N-T";
		case WALL_NORTHWEST_DIAGONAL:
			return "NW";
		case WALL_NORTHEAST_DIAGONAL:
			return "NE";
		case WALL_SOUTHWEST_DIAGONAL:
			return "SW";
		case WALL_SOUTHEAST_DIAGONAL:
			return "SE";
		case WALL_INTERSECTION:
			return "X";
		case WALL_UNTOUCHABLE:
			return "U";
		default:
			return "";
		}
	}

	std::vector<wxString> PartTypeCandidatesForWallAlignment(uint32_t alignment, bool north, bool west, bool east, bool south) {
		const wxString primary = PartTypeForWallAlignment(alignment);
		if (primary.IsEmpty()) {
			return {};
		}
		switch (alignment) {
		case WALL_NORTHWEST_DIAGONAL:
		case WALL_NORTHEAST_DIAGONAL:
		case WALL_SOUTHWEST_DIAGONAL:
		case WALL_SOUTHEAST_DIAGONAL:
			if (east && south && !north && !west) {
				return { primary, "pole" };
			}
			if (west && south && !north && !east) {
				return { primary, "horizontal" };
			}
			if (east && north && !south && !west) {
				return { primary, "vertical" };
			}
			if (west && north && !south && !east) {
				return { primary, "corner" };
			}
			return { primary, "horizontal", "vertical" };
		default:
			return { primary };
		}
	}

	class WallWorkspaceComposedPreviewPanel : public wxPanel {
	public:
		explicit WallWorkspaceComposedPreviewPanel(wxWindow* parent) :
			wxPanel(parent, wxID_ANY) {
			SetBackgroundStyle(wxBG_STYLE_PAINT);
			SetMinSize(wxSize(FromDIP(520), FromDIP(220)));
			Bind(wxEVT_PAINT, &WallWorkspaceComposedPreviewPanel::OnPaint, this);
		}

		void SetPreviewState(const BrushStorageRecord* storage, const wxString &selectedPartType, int selectedItemId, int selectedDoorItemId, bool strict, bool showOverlays, int doorSide) {
			storage_ = storage;
			selectedPartType_ = NormalizeWallPreviewPartType(selectedPartType);
			selectedItemId_ = selectedItemId;
			selectedDoorItemId_ = selectedDoorItemId;
			strict_ = strict;
			showOverlays_ = showOverlays;
			doorSide_ = doorSide;
			Refresh();
		}

	private:
		struct DrawOp {
			int itemId = 0;
			wxPoint tileAnchor;
			wxRect spriteRect;
			wxRect tileRect;
			uint32_t alignment = 0;
			wxString expectedPartType;
			wxString resolvedPartType;
			bool isDoor = false;
			bool usedFallback = false;
		};

		const WallPartRecord* FindPartByType(const wxString &type) const {
			if (!storage_) {
				return nullptr;
			}
			for (const WallPartRecord &part : storage_->wallParts) {
				if (NormalizeWallPreviewPartType(part.partType) == type) {
					return &part;
				}
			}
			return nullptr;
		}

		wxBitmap GetCachedBitmap(int itemId) {
			const WallPreviewSpriteMetrics metrics = ResolveWallPreviewSpriteMetrics(itemId);
			if (!metrics.isValid()) {
				return wxBitmap();
			}
			const auto it = bitmapCache_.find(metrics.spriteId);
			if (it != bitmapCache_.end()) {
				return it->second;
			}
			wxBitmap bitmap = BuildWallPreviewBitmap(metrics.spriteId);
			bitmapCache_.emplace(metrics.spriteId, bitmap);
			return bitmap;
		}

		std::vector<DrawOp> BuildScene(const std::vector<wxPoint> &wallCells, const wxPoint* doorCell, int doorItemId, wxRect &outBounds) {
			std::vector<DrawOp> ops;
			outBounds = wxRect();
			if (!storage_) {
				return ops;
			}

			const auto hasWallCell = [&wallCells](int x, int y) {
				for (const wxPoint &cell : wallCells) {
					if (cell.x == x && cell.y == y) {
						return true;
					}
				}
				return false;
			};

			for (const wxPoint &cell : wallCells) {
				const bool north = hasWallCell(cell.x, cell.y - 1);
				const bool west = hasWallCell(cell.x - 1, cell.y);
				const bool east = hasWallCell(cell.x + 1, cell.y);
				const bool south = hasWallCell(cell.x, cell.y + 1);
				uint32_t tiledata = 0;
				if (north) tiledata |= 1u << 0;
				if (west) tiledata |= 1u << 1;
				if (east) tiledata |= 1u << 2;
				if (south) tiledata |= 1u << 3;
				const uint32_t alignment = WallBrush::full_border_types[tiledata & 0x0F];
				const wxString expectedPartType = PartTypeForWallAlignment(alignment);
				const std::vector<wxString> partCandidates = strict_
					? (expectedPartType.IsEmpty() ? std::vector<wxString>() : std::vector<wxString>{ expectedPartType })
					: PartTypeCandidatesForWallAlignment(alignment, north, west, east, south);
				const WallPartRecord* part = nullptr;
				wxString resolvedPartType = expectedPartType;
				bool usedFallback = false;
				for (const wxString &candidate : partCandidates) {
					part = FindPartByType(candidate);
					if (part) {
						resolvedPartType = candidate;
						usedFallback = !expectedPartType.IsEmpty() &&
							NormalizeWallPreviewPartType(expectedPartType) != NormalizeWallPreviewPartType(candidate);
						break;
					}
				}
				int itemId = part ? ChooseWallPreviewItemId(part->items) : 0;
				bool isDoor = false;
				if (doorCell && doorItemId > 0 && cell.x == doorCell->x && cell.y == doorCell->y) {
					itemId = doorItemId;
					isDoor = true;
					if (!selectedPartType_.IsEmpty()) {
						resolvedPartType = selectedPartType_;
					}
				}

				const wxPoint tileAnchor(cell.x * 32, cell.y * 32);
				DrawOp op;
				op.itemId = itemId;
				op.tileAnchor = tileAnchor;
				op.tileRect = wxRect(tileAnchor.x, tileAnchor.y, 32, 32);
				op.spriteRect = itemId > 0 ? GetWallPreviewSpriteRect(itemId, tileAnchor) : op.tileRect;
				op.alignment = alignment;
				op.expectedPartType = expectedPartType;
				op.resolvedPartType = resolvedPartType;
				op.isDoor = isDoor;
				op.usedFallback = usedFallback;
				ops.push_back(op);
				outBounds.Union(op.tileRect);
				if (itemId > 0) {
					outBounds.Union(op.spriteRect);
				}
			}

			if (outBounds.IsEmpty()) {
				outBounds = wxRect(0, 0, 32, 32);
			}
			return ops;
		}

		void DrawScene(wxGraphicsContext &gc, const wxRect &viewport, const wxString &title, const std::vector<DrawOp> &ops, const wxRect &bounds) {
			gc.SetFont(GetFont(), wxColour(170, 176, 190));
			wxDouble tw;
			wxDouble th;
			gc.GetTextExtent(title, &tw, &th, nullptr, nullptr);
			gc.DrawText(title, viewport.x + FromDIP(6), viewport.y + FromDIP(6));

			wxRect sceneViewport = viewport;
			sceneViewport.y += FromDIP(20);
			sceneViewport.height -= FromDIP(20);
			sceneViewport.Deflate(FromDIP(6), FromDIP(6));

			const double scaleX = bounds.width > 0 ? static_cast<double>(sceneViewport.width) / static_cast<double>(bounds.width) : 1.0;
			const double scaleY = bounds.height > 0 ? static_cast<double>(sceneViewport.height) / static_cast<double>(bounds.height) : 1.0;
			const double scale = std::max(0.05, std::min(scaleX, scaleY));

			const double originX = sceneViewport.x + (sceneViewport.width - bounds.width * scale) / 2.0;
			const double originY = sceneViewport.y + (sceneViewport.height - bounds.height * scale) / 2.0;

			gc.PushState();
			gc.Translate(originX, originY);
			gc.Scale(scale, scale);
			gc.Translate(-bounds.x, -bounds.y);

			const wxColour cellA(28, 28, 28);
			const wxColour cellB(24, 24, 24);
			const wxColour gridLine(0, 0, 0, 72);
			const wxColour missingFill(90, 20, 20, 80);
			const wxColour missingOutline(255, 120, 120, 150);
			const wxColour overlayText(220, 224, 232);
			const wxColour fallbackOutline(255, 215, 90, 220);

			gc.SetPen(wxPen(gridLine, 1));
			for (const auto &op : ops) {
				const wxRect &cell = op.tileRect;
				gc.SetBrush(wxBrush((((cell.x / 32) + (cell.y / 32)) % 2 == 0) ? cellA : cellB));
				gc.DrawRectangle(cell.x, cell.y, cell.width, cell.height);
			}

			const wxString selectedPart = selectedPartType_;
			for (const auto &op : ops) {
				if (!selectedPart.IsEmpty() && NormalizeWallPreviewPartType(op.resolvedPartType) == selectedPart) {
					gc.SetPen(wxPen(wxColour(255, 215, 90, 180), 2));
					gc.SetBrush(*wxTRANSPARENT_BRUSH);
					gc.DrawRectangle(op.tileRect.x, op.tileRect.y, op.tileRect.width, op.tileRect.height);
				}
			}

			if (strict_) {
				for (const auto &op : ops) {
					if (op.isDoor || op.itemId > 0) {
						continue;
					}
					gc.SetPen(wxPen(missingOutline, 2));
					gc.SetBrush(wxBrush(missingFill));
					gc.DrawRectangle(op.tileRect.x, op.tileRect.y, op.tileRect.width, op.tileRect.height);
				}
			}

			for (const auto &op : ops) {
				if (op.itemId <= 0) {
					continue;
				}
				wxBitmap bitmap = GetCachedBitmap(op.itemId);
				if (!bitmap.IsOk()) {
					continue;
				}
				gc.DrawBitmap(bitmap, op.spriteRect.x, op.spriteRect.y, bitmap.GetWidth(), bitmap.GetHeight());
				if (selectedItemId_ > 0 && op.itemId == selectedItemId_) {
					gc.SetPen(wxPen(wxColour(120, 200, 255, 200), 2));
					gc.SetBrush(*wxTRANSPARENT_BRUSH);
					gc.DrawRectangle(op.tileRect.x, op.tileRect.y, op.tileRect.width, op.tileRect.height);
				}
				if (selectedDoorItemId_ > 0 && op.isDoor && op.itemId == selectedDoorItemId_) {
					gc.SetPen(wxPen(wxColour(160, 255, 160, 220), 2));
					gc.SetBrush(wxBrush(wxColour(160, 255, 160, 48)));
					gc.DrawRectangle(op.tileRect.x, op.tileRect.y, op.tileRect.width, op.tileRect.height);
				}
			}

			if (showOverlays_) {
				wxFont overlayFont = GetFont();
				overlayFont.SetPointSize(std::max(6, overlayFont.GetPointSize() - 2));
				gc.SetFont(overlayFont, overlayText);
				for (const auto &op : ops) {
					const wxString tag = WallPreviewAlignmentTag(op.alignment);
					if (!tag.IsEmpty()) {
						gc.DrawText(tag, op.tileRect.x + 2, op.tileRect.y + 1);
					}
					wxString partLabel = op.resolvedPartType;
					if (partLabel.IsEmpty()) {
						partLabel = op.expectedPartType;
					}
					if (!partLabel.IsEmpty()) {
						gc.DrawText(partLabel, op.tileRect.x + 2, op.tileRect.y + 14);
					}
					if (op.usedFallback && !strict_) {
						gc.SetPen(wxPen(fallbackOutline, 2));
						gc.SetBrush(*wxTRANSPARENT_BRUSH);
						gc.DrawRectangle(op.tileRect.x + 1, op.tileRect.y + 1, op.tileRect.width - 2, op.tileRect.height - 2);
					}
				}
			}

			gc.PopState();
		}

		void OnPaint(wxPaintEvent &) {
			wxAutoBufferedPaintDC dc(this);
			dc.SetBackground(wxBrush(wxColour(16, 16, 16)));
			dc.Clear();

			std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
			if (!gc) {
				return;
			}

			const wxRect client = GetClientRect();
			wxRect left = client;
			wxRect right = client;
			left.width = client.width / 2;
			right.x = left.GetRight() + 1;
			right.width = client.width - left.width - 1;

			wxRect boundsA;
			wxRect boundsB;
			const int roomSize = 5;
			std::vector<wxPoint> roomCells;
			roomCells.reserve(roomSize * 4);
			for (int y = 0; y < roomSize; ++y) {
				for (int x = 0; x < roomSize; ++x) {
					if (x == 0 || y == 0 || x == roomSize - 1 || y == roomSize - 1) {
						roomCells.push_back(wxPoint(x, y));
					}
				}
			}
			wxPoint doorCell(roomSize / 2, 0);
			switch (doorSide_) {
			case 1:
				doorCell = wxPoint(roomSize / 2, 0);
				break;
			case 2:
				doorCell = wxPoint(roomSize - 1, roomSize / 2);
				break;
			case 3:
				doorCell = wxPoint(roomSize / 2, roomSize - 1);
				break;
			case 4:
				doorCell = wxPoint(0, roomSize / 2);
				break;
			default: {
				const wxString selectedPart = selectedPartType_.Lower();
				const bool wantsVertical = selectedPart.Contains("vertical") || selectedPart.Contains("east") || selectedPart.Contains("west");
				const bool wantsHorizontal = selectedPart.Contains("horizontal") || selectedPart.Contains("north") || selectedPart.Contains("south");
				if (wantsVertical && !wantsHorizontal) {
					doorCell = wxPoint(roomSize - 1, roomSize / 2);
				} else if (wantsHorizontal && !wantsVertical) {
					doorCell = wxPoint(roomSize / 2, 0);
				}
				break;
			}
			}

			const std::vector<DrawOp> roomOps = BuildScene(roomCells, nullptr, 0, boundsA);
			const std::vector<DrawOp> roomWithDoorOps = BuildScene(roomCells, selectedDoorItemId_ > 0 ? &doorCell : nullptr, selectedDoorItemId_, boundsB);

			DrawScene(*gc, left, "Composed room", roomOps, boundsA);
			DrawScene(*gc, right, selectedDoorItemId_ > 0 ? "Room with door" : "Room with door (select a door)", roomWithDoorOps, boundsB);
		}

		const BrushStorageRecord* storage_ = nullptr;
		wxString selectedPartType_;
		int selectedItemId_ = 0;
		int selectedDoorItemId_ = 0;
		bool strict_ = false;
		bool showOverlays_ = false;
		int doorSide_ = 0;
		std::unordered_map<int, wxBitmap> bitmapCache_;
	};

	wxString DescribeDoor(const WallPartDoorRecord &door) {
		return wxString::Format("%s | id %d | %s%s",
			door.doorType,
			door.itemId,
			door.isOpen ? "open" : "closed",
			door.wallHateMe ? " | hate" : "");
	}

	void StyleWallWorkspaceSubtitle(wxStaticText* label) {
		label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
	}

	void StyleWallWorkspaceStatusLabel(wxStaticText* label) {
		label->SetMinSize(wxSize(-1, label->GetParent()->FromDIP(20)));
		label->Wrap(label->GetParent()->FromDIP(760));
	}

	void StyleWallWorkspaceActionButton(wxButton* button, const wxString &tooltip) {
		button->SetMinSize(wxSize(-1, button->GetParent()->FromDIP(20)));
		button->SetToolTip(tooltip);
	}

	void NormalizeWallPartRecord(WallPartRecord &part) {
		for (size_t i = 0; i < part.items.size(); ++i) {
			part.items[i].sortOrder = static_cast<int>(i);
		}
		for (size_t i = 0; i < part.doors.size(); ++i) {
			part.doors[i].sortOrder = static_cast<int>(i);
		}
	}

	template <typename T, typename Compare>
	bool WallPanelVectorsEqual(const std::vector<T> &left, const std::vector<T> &right, Compare compare) {
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

	bool AreWallPanelPartItemRecordsEqual(const WallPartItemRecord &left, const WallPartItemRecord &right) {
		return left.itemId == right.itemId &&
			   left.chance == right.chance &&
			   left.sortOrder == right.sortOrder;
	}

	bool AreWallPanelPartDoorRecordsEqual(const WallPartDoorRecord &left, const WallPartDoorRecord &right) {
		return left.itemId == right.itemId &&
			   left.doorType == right.doorType &&
			   left.isOpen == right.isOpen &&
			   left.wallHateMe == right.wallHateMe &&
			   left.sortOrder == right.sortOrder;
	}

	bool AreWallPanelPartRecordsEqual(const WallPartRecord &left, const WallPartRecord &right) {
		return left.partType == right.partType &&
			   left.sortOrder == right.sortOrder &&
			   WallPanelVectorsEqual(left.items, right.items, AreWallPanelPartItemRecordsEqual) &&
			   WallPanelVectorsEqual(left.doors, right.doors, AreWallPanelPartDoorRecordsEqual);
	}

	bool IsKnownWallPanelItemId(int itemId) {
		if (itemId <= 0 || itemId > std::numeric_limits<uint16_t>::max()) {
			return false;
		}
		return g_items.isValidID(static_cast<uint16_t>(itemId));
	}

	int GetWallPanelMaxEditableItemId() {
		return static_cast<int>(std::min<uint32_t>(g_items.getMaxID(), std::numeric_limits<uint16_t>::max()));
	}

	WallPanelDoorFamily GetWallPanelDoorFamily(::DoorType doorType) {
		switch (doorType) {
		case WALL_ARCHWAY:
		case WALL_DOOR_NORMAL:
		case WALL_DOOR_LOCKED:
		case WALL_DOOR_QUEST:
		case WALL_DOOR_MAGIC:
			return WallPanelDoorFamily::Door;
		case WALL_WINDOW:
		case WALL_HATCH_WINDOW:
			return WallPanelDoorFamily::Window;
		default:
			return WallPanelDoorFamily::Unknown;
		}
	}

	wxString DescribeWallPanelDoorType(::DoorType doorType) {
		switch (doorType) {
		case WALL_ARCHWAY:
			return "archway";
		case WALL_DOOR_NORMAL:
			return "normal";
		case WALL_DOOR_LOCKED:
			return "locked";
		case WALL_DOOR_QUEST:
			return "quest";
		case WALL_DOOR_MAGIC:
			return "magic";
		case WALL_WINDOW:
			return "window";
		case WALL_HATCH_WINDOW:
			return "hatch_window";
		default:
			return "unknown";
		}
	}

	WallPanelDoorTypeSpec ParseWallPanelDoorTypeSpec(const wxString &doorType) {
		WallPanelDoorTypeSpec spec;
		const wxString normalized = doorType.Lower();
		spec.normalizedLabel = normalized;

		if (normalized == "archway") {
			spec.valid = true;
			spec.expectsExact = true;
			spec.exactType = WALL_ARCHWAY;
			spec.family = WallPanelDoorFamily::Door;
		} else if (normalized == "normal") {
			spec.valid = true;
			spec.expectsExact = true;
			spec.exactType = WALL_DOOR_NORMAL;
			spec.family = WallPanelDoorFamily::Door;
		} else if (normalized == "locked") {
			spec.valid = true;
			spec.expectsExact = true;
			spec.exactType = WALL_DOOR_LOCKED;
			spec.family = WallPanelDoorFamily::Door;
		} else if (normalized == "quest") {
			spec.valid = true;
			spec.expectsExact = true;
			spec.exactType = WALL_DOOR_QUEST;
			spec.family = WallPanelDoorFamily::Door;
		} else if (normalized == "magic") {
			spec.valid = true;
			spec.expectsExact = true;
			spec.exactType = WALL_DOOR_MAGIC;
			spec.family = WallPanelDoorFamily::Door;
		} else if (normalized == "window") {
			spec.valid = true;
			spec.expectsExact = true;
			spec.exactType = WALL_WINDOW;
			spec.family = WallPanelDoorFamily::Window;
		} else if (normalized == "hatch_window" || normalized == "hatch window") {
			spec.valid = true;
			spec.expectsExact = true;
			spec.exactType = WALL_HATCH_WINDOW;
			spec.family = WallPanelDoorFamily::Window;
			spec.normalizedLabel = "hatch_window";
		} else if (normalized == "any door") {
			spec.valid = true;
			spec.family = WallPanelDoorFamily::Door;
		} else if (normalized == "any window") {
			spec.valid = true;
			spec.family = WallPanelDoorFamily::Window;
		} else if (normalized == "any") {
			spec.valid = true;
			spec.allowAny = true;
		}

		return spec;
	}
} // namespace

MaterialsWorkbenchWallPanel::MaterialsWorkbenchWallPanel(wxWindow* parent, MaterialsWorkbenchController &controller) :
	wxPanel(parent, wxID_ANY),
	controller_(controller) {
	BuildLayout();
	ClearWorkspace("Select a wall brush in the navigation tree to edit its wall parts.");
}

void MaterialsWorkbenchWallPanel::SetOnWallBrushSaved(std::function<void(int64_t)> callback) {
	onWallBrushSaved_ = std::move(callback);
}

void MaterialsWorkbenchWallPanel::SetOnWallBrushStateChanged(std::function<void()> callback) {
	onWallBrushStateChanged_ = std::move(callback);
}

bool MaterialsWorkbenchWallPanel::HasPendingChanges() const {
	return hasWallBrush_ && dirty_;
}

bool MaterialsWorkbenchWallPanel::IsCurrentWallSelection(const wxString &contextKey, int itemIndex) const {
	return hasWallBrush_ && currentContextKey_ == contextKey && currentItemIndex_ == itemIndex;
}

wxString MaterialsWorkbenchWallPanel::GetCurrentWallDisplayName() const {
	return hasWallBrush_ ? wallBrushStorage_.brush.name : "";
}

bool MaterialsWorkbenchWallPanel::ResolvePendingChangesBeforeSwitch(wxWindow* parent, const wxString &targetLabel) {
	if (!HasPendingChanges()) {
		return true;
	}

	const wxString destination = targetLabel.IsEmpty() ? "the selected entry" : "\"" + targetLabel + "\"";
	wxMessageDialog dialog(
		parent,
		"Wall brush \"" + wallBrushStorage_.brush.name + "\" has unsaved changes.\n\n"
		"You are switching to " + destination + ".\n\n"
		"Yes: save and continue\n"
		"No: discard local changes and continue\n"
		"Cancel: stay on the current wall brush",
		"Unsaved Wall Changes",
		wxYES_NO | wxCANCEL | wxICON_WARNING
	);
	dialog.SetYesNoCancelLabels("Save", "Discard", "Cancel");

	switch (dialog.ShowModal()) {
	case wxID_YES:
		return SaveCurrentWallBrush();
	case wxID_NO:
		return LoadWallBrush(currentContextKey_, currentItemIndex_);
	default:
		SetStatusMessage("Selection change canceled. Pending wall edits were kept.");
		return false;
	}
}

const WallPartRecord* MaterialsWorkbenchWallPanel::GetSelectedPart() const {
	if (selectedPartIndex_ < 0 || selectedPartIndex_ >= static_cast<int>(wallBrushStorage_.wallParts.size())) {
		return nullptr;
	}
	return &wallBrushStorage_.wallParts[selectedPartIndex_];
}

WallPartRecord* MaterialsWorkbenchWallPanel::GetSelectedPart() {
	if (selectedPartIndex_ < 0 || selectedPartIndex_ >= static_cast<int>(wallBrushStorage_.wallParts.size())) {
		return nullptr;
	}
	return &wallBrushStorage_.wallParts[selectedPartIndex_];
}

void MaterialsWorkbenchWallPanel::BuildLayout() {
	wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);

	wxStaticText* title = new wxStaticText(this, wxID_ANY, "Wall Workspace");
	wxFont titleFont = title->GetFont();
	titleFont.SetPointSize(titleFont.GetPointSize() + 4);
	titleFont.SetWeight(wxFONTWEIGHT_BOLD);
	title->SetFont(titleFont);

	titleLabel_ = new wxStaticText(this, wxID_ANY, "No wall brush selected");
	subtitleLabel_ = new wxStaticText(this, wxID_ANY, "Edit persisted wall parts, door definitions and alternates from materials.db.");
	StyleWallWorkspaceSubtitle(subtitleLabel_);

	wxScrolledWindow* scrolled = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	scrolled->SetScrollRate(FromDIP(10), FromDIP(10));

	wxBoxSizer* contentSizer = new wxBoxSizer(wxVERTICAL);
	summaryLabel_ = new wxStaticText(scrolled, wxID_ANY, "");

	wxStaticBoxSizer* identityBox = new wxStaticBoxSizer(wxVERTICAL, scrolled, "Wall Brush");
	wxWindow* identityParent = identityBox->GetStaticBox();
	wxFlexGridSizer* identityGrid = new wxFlexGridSizer(2, FromDIP(8), FromDIP(10));
	identityGrid->AddGrowableCol(1, 1);

	brushIdCtrl_ = new wxTextCtrl(identityParent, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
	brushNameCtrl_ = new wxTextCtrl(identityParent, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
	partChoice_ = new wxChoice(identityParent, wxID_ANY);
	addPartButton_ = new wxButton(identityParent, wxID_ANY, "+");
	partSummaryLabel_ = new wxStaticText(identityParent, wxID_ANY, "");

	identityGrid->Add(new wxStaticText(identityParent, wxID_ANY, "SQLite ID"), 0, wxALIGN_CENTER_VERTICAL);
	identityGrid->Add(brushIdCtrl_, 1, wxEXPAND);
	identityGrid->Add(new wxStaticText(identityParent, wxID_ANY, "Name"), 0, wxALIGN_CENTER_VERTICAL);
	identityGrid->Add(brushNameCtrl_, 1, wxEXPAND);
	identityGrid->Add(new wxStaticText(identityParent, wxID_ANY, "Part Type"), 0, wxALIGN_CENTER_VERTICAL);
	wxBoxSizer* partRowSizer = new wxBoxSizer(wxHORIZONTAL);
	partRowSizer->Add(partChoice_, 1, wxEXPAND);
	partRowSizer->Add(addPartButton_, 0, wxLEFT, FromDIP(6));
	identityGrid->Add(partRowSizer, 1, wxEXPAND);

	identityBox->Add(identityGrid, 0, wxEXPAND | wxALL, FromDIP(8));
	identityBox->Add(partSummaryLabel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

	wxStaticBoxSizer* previewBox = new wxStaticBoxSizer(wxVERTICAL, scrolled, "Preview");
	wxWindow* previewParent = previewBox->GetStaticBox();
	composedPreview_ = new WallWorkspaceComposedPreviewPanel(previewParent);
	previewBox->Add(composedPreview_, 0, wxEXPAND | wxALL, FromDIP(6));

	previewFillRadio_ = new wxRadioButton(previewParent, wxID_ANY, "Fill", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	previewStrictRadio_ = new wxRadioButton(previewParent, wxID_ANY, "Strict");
	previewFillRadio_->SetValue(true);
	previewOverlayCtrl_ = new wxCheckBox(previewParent, wxID_ANY, "Show overlays");
	previewOverlayCtrl_->SetValue(false);
	previewDoorAutoRadio_ = new wxRadioButton(previewParent, wxID_ANY, "Auto", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	previewDoorNorthRadio_ = new wxRadioButton(previewParent, wxID_ANY, "N");
	previewDoorEastRadio_ = new wxRadioButton(previewParent, wxID_ANY, "E");
	previewDoorSouthRadio_ = new wxRadioButton(previewParent, wxID_ANY, "S");
	previewDoorWestRadio_ = new wxRadioButton(previewParent, wxID_ANY, "W");
	previewDoorAutoRadio_->SetValue(true);

	wxBoxSizer* previewControls = new wxBoxSizer(wxHORIZONTAL);
	previewControls->Add(new wxStaticText(previewParent, wxID_ANY, "Mode"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
	previewControls->Add(previewFillRadio_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
	previewControls->Add(previewStrictRadio_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
	previewControls->Add(new wxStaticText(previewParent, wxID_ANY, "Door"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
	previewControls->Add(previewDoorAutoRadio_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
	previewControls->Add(previewDoorNorthRadio_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
	previewControls->Add(previewDoorEastRadio_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
	previewControls->Add(previewDoorSouthRadio_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
	previewControls->Add(previewDoorWestRadio_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
	previewControls->Add(previewOverlayCtrl_, 0, wxALIGN_CENTER_VERTICAL);
	previewBox->Add(previewControls, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

	wxBoxSizer* gridsRow = new wxBoxSizer(wxHORIZONTAL);

	wxStaticBoxSizer* itemBox = new wxStaticBoxSizer(wxVERTICAL, scrolled, "Wall Items");
	wxWindow* itemBoxParent = itemBox->GetStaticBox();
	itemGridScroll_ = new wxScrolledWindow(itemBoxParent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	itemGridScroll_->SetScrollRate(FromDIP(10), FromDIP(10));
	itemGridScroll_->SetMinSize(wxSize(-1, FromDIP(200)));
	itemGridSizer_ = new wxWrapSizer(wxHORIZONTAL);
	itemGridScroll_->SetSizer(itemGridSizer_);
	itemBox->Add(itemGridScroll_, 1, wxEXPAND | wxALL, FromDIP(6));

	wxStaticBoxSizer* doorBox = new wxStaticBoxSizer(wxVERTICAL, scrolled, "Doors");
	wxWindow* doorBoxParent = doorBox->GetStaticBox();
	doorGridScroll_ = new wxScrolledWindow(doorBoxParent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	doorGridScroll_->SetScrollRate(FromDIP(10), FromDIP(10));
	doorGridScroll_->SetMinSize(wxSize(-1, FromDIP(200)));
	doorGridSizer_ = new wxWrapSizer(wxHORIZONTAL);
	doorGridScroll_->SetSizer(doorGridSizer_);
	doorBox->Add(doorGridScroll_, 1, wxEXPAND | wxALL, FromDIP(6));

	gridsRow->Add(itemBox, 1, wxRIGHT | wxEXPAND, FromDIP(10));
	gridsRow->Add(doorBox, 1, wxEXPAND);

	wxBoxSizer* editorRow = new wxBoxSizer(wxHORIZONTAL);

	wxStaticBoxSizer* itemEditorBox = new wxStaticBoxSizer(wxVERTICAL, scrolled, "Selected Item");
	wxWindow* itemEditorParent = itemEditorBox->GetStaticBox();
	selectedItemLabel_ = new wxStaticText(itemEditorParent, wxID_ANY, "No wall item selected");
	itemPreviewButton_ = new ItemButton(itemEditorParent, RENDER_SIZE_32x32, 0);
	itemIdCtrl_ = new wxSpinCtrl(itemEditorParent, wxID_ANY);
	itemIdCtrl_->SetRange(0, GetWallPanelMaxEditableItemId());
	itemChanceCtrl_ = new wxSpinCtrl(itemEditorParent, wxID_ANY);
	itemChanceCtrl_->SetRange(0, 100000);

	wxFlexGridSizer* itemEditorGrid = new wxFlexGridSizer(2, FromDIP(8), FromDIP(10));
	itemEditorGrid->AddGrowableCol(1, 1);
	itemEditorGrid->Add(new wxStaticText(itemEditorParent, wxID_ANY, "Preview"), 0, wxALIGN_CENTER_VERTICAL);
	itemEditorGrid->Add(itemPreviewButton_, 0, wxALIGN_LEFT);
	itemEditorGrid->Add(new wxStaticText(itemEditorParent, wxID_ANY, "Item ID"), 0, wxALIGN_CENTER_VERTICAL);
	itemEditorGrid->Add(itemIdCtrl_, 1, wxEXPAND);
	itemEditorGrid->Add(new wxStaticText(itemEditorParent, wxID_ANY, "Chance"), 0, wxALIGN_CENTER_VERTICAL);
	itemEditorGrid->Add(itemChanceCtrl_, 1, wxEXPAND);

	wxBoxSizer* itemActionSizer = new wxBoxSizer(wxHORIZONTAL);
	wxButton* pickItemButton = new wxButton(itemEditorParent, wxID_ANY, "Pick Item");
	wxButton* applyItemButton = new wxButton(itemEditorParent, wxID_ANY, "Apply Item");
	wxButton* removeItemButton = new wxButton(itemEditorParent, wxID_ANY, "Remove Item");
	itemActionSizer->Add(pickItemButton, 0, wxRIGHT, FromDIP(6));
	itemActionSizer->Add(applyItemButton, 0, wxRIGHT, FromDIP(6));
	itemActionSizer->Add(removeItemButton, 0);

	itemEditorBox->Add(selectedItemLabel_, 0, wxEXPAND | wxALL, FromDIP(6));
	itemEditorBox->Add(itemEditorGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
	itemEditorBox->Add(itemActionSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

	wxStaticBoxSizer* doorEditorBox = new wxStaticBoxSizer(wxVERTICAL, scrolled, "Selected Door");
	wxWindow* doorEditorParent = doorEditorBox->GetStaticBox();
	selectedDoorLabel_ = new wxStaticText(doorEditorParent, wxID_ANY, "No door selected");
	doorPreviewButton_ = new ItemButton(doorEditorParent, RENDER_SIZE_32x32, 0);
	doorItemIdCtrl_ = new wxSpinCtrl(doorEditorParent, wxID_ANY);
	doorItemIdCtrl_->SetRange(0, GetWallPanelMaxEditableItemId());
	doorTypeChoice_ = new wxChoice(doorEditorParent, wxID_ANY);
	const wxString doorTypes[] = { "archway", "normal", "locked", "quest", "magic", "window", "hatch_window", "hatch window", "any door", "any window", "any" };
	for (const wxString &doorType : doorTypes) {
		doorTypeChoice_->Append(doorType);
	}
	doorOpenCtrl_ = new wxCheckBox(doorEditorParent, wxID_ANY, "Open");
	doorHateCtrl_ = new wxCheckBox(doorEditorParent, wxID_ANY, "Wall Hate");

	wxFlexGridSizer* doorEditorGrid = new wxFlexGridSizer(2, FromDIP(8), FromDIP(10));
	doorEditorGrid->AddGrowableCol(1, 1);
	doorEditorGrid->Add(new wxStaticText(doorEditorParent, wxID_ANY, "Preview"), 0, wxALIGN_CENTER_VERTICAL);
	doorEditorGrid->Add(doorPreviewButton_, 0, wxALIGN_LEFT);
	doorEditorGrid->Add(new wxStaticText(doorEditorParent, wxID_ANY, "Item ID"), 0, wxALIGN_CENTER_VERTICAL);
	doorEditorGrid->Add(doorItemIdCtrl_, 1, wxEXPAND);
	doorEditorGrid->Add(new wxStaticText(doorEditorParent, wxID_ANY, "Door Type"), 0, wxALIGN_CENTER_VERTICAL);
	doorEditorGrid->Add(doorTypeChoice_, 1, wxEXPAND);
	doorEditorGrid->Add(new wxStaticText(doorEditorParent, wxID_ANY, "Flags"), 0, wxALIGN_CENTER_VERTICAL);
	wxBoxSizer* doorFlagSizer = new wxBoxSizer(wxHORIZONTAL);
	doorFlagSizer->Add(doorOpenCtrl_, 0, wxRIGHT, FromDIP(8));
	doorFlagSizer->Add(doorHateCtrl_, 0);
	doorEditorGrid->Add(doorFlagSizer, 1, wxEXPAND);

	wxBoxSizer* doorActionSizer = new wxBoxSizer(wxHORIZONTAL);
	wxButton* pickDoorItemButton = new wxButton(doorEditorParent, wxID_ANY, "Pick Door Item");
	wxButton* applyDoorButton = new wxButton(doorEditorParent, wxID_ANY, "Apply Door");
	wxButton* removeDoorButton = new wxButton(doorEditorParent, wxID_ANY, "Remove Door");
	doorActionSizer->Add(pickDoorItemButton, 0, wxRIGHT, FromDIP(6));
	doorActionSizer->Add(applyDoorButton, 0, wxRIGHT, FromDIP(6));
	doorActionSizer->Add(removeDoorButton, 0);

	doorEditorBox->Add(selectedDoorLabel_, 0, wxEXPAND | wxALL, FromDIP(6));
	doorEditorBox->Add(doorEditorGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
	doorEditorBox->Add(doorActionSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

	editorRow->Add(itemEditorBox, 1, wxRIGHT | wxEXPAND, FromDIP(10));
	editorRow->Add(doorEditorBox, 1, wxEXPAND);

	contentSizer->Add(summaryLabel_, 0, wxEXPAND | wxALL, FromDIP(6));
	contentSizer->Add(identityBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
	contentSizer->Add(previewBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
	contentSizer->Add(gridsRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
	contentSizer->Add(editorRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

	scrolled->SetSizer(contentSizer);

	wxBoxSizer* headerSizer = new wxBoxSizer(wxVERTICAL);
	headerSizer->Add(title, 0, wxBOTTOM, FromDIP(4));
	headerSizer->Add(titleLabel_, 0, wxBOTTOM, FromDIP(2));
	headerSizer->Add(subtitleLabel_, 0);

	wxBoxSizer* actionSizer = new wxBoxSizer(wxHORIZONTAL);
	saveButton_ = new wxButton(this, wxID_SAVE, "Save Wall Brush");
	revertButton_ = new wxButton(this, wxID_ANY, "Revert");
	StyleWallWorkspaceActionButton(saveButton_, "Write the current wall part and door edits to materials.db.");
	StyleWallWorkspaceActionButton(revertButton_, "Discard local wall edits and reload the current wall brush from materials.db.");
	actionSizer->Add(saveButton_, 0, wxRIGHT, FromDIP(2));
	actionSizer->Add(revertButton_, 0);

	statusLabel_ = new wxStaticText(this, wxID_ANY, "");
	StyleWallWorkspaceStatusLabel(statusLabel_);
	wxBoxSizer* footerSizer = new wxBoxSizer(wxHORIZONTAL);
	footerSizer->Add(statusLabel_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
	footerSizer->Add(actionSizer, 0, wxALIGN_CENTER_VERTICAL);

	rootSizer->Add(headerSizer, 0, wxEXPAND | wxALL, FromDIP(8));
	rootSizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));
	rootSizer->Add(scrolled, 1, wxEXPAND | wxALL, FromDIP(8));
	rootSizer->Add(footerSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(2));
	SetSizer(rootSizer);

	partChoice_->Bind(wxEVT_CHOICE, &MaterialsWorkbenchWallPanel::OnPartChanged, this);
	addPartButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchWallPanel::OnAddPartType, this);
	previewFillRadio_->Bind(wxEVT_RADIOBUTTON, &MaterialsWorkbenchWallPanel::OnPreviewOptionsChanged, this);
	previewStrictRadio_->Bind(wxEVT_RADIOBUTTON, &MaterialsWorkbenchWallPanel::OnPreviewOptionsChanged, this);
	previewDoorAutoRadio_->Bind(wxEVT_RADIOBUTTON, &MaterialsWorkbenchWallPanel::OnPreviewOptionsChanged, this);
	previewDoorNorthRadio_->Bind(wxEVT_RADIOBUTTON, &MaterialsWorkbenchWallPanel::OnPreviewOptionsChanged, this);
	previewDoorEastRadio_->Bind(wxEVT_RADIOBUTTON, &MaterialsWorkbenchWallPanel::OnPreviewOptionsChanged, this);
	previewDoorSouthRadio_->Bind(wxEVT_RADIOBUTTON, &MaterialsWorkbenchWallPanel::OnPreviewOptionsChanged, this);
	previewDoorWestRadio_->Bind(wxEVT_RADIOBUTTON, &MaterialsWorkbenchWallPanel::OnPreviewOptionsChanged, this);
	previewOverlayCtrl_->Bind(wxEVT_CHECKBOX, &MaterialsWorkbenchWallPanel::OnPreviewOptionsChanged, this);
	pickItemButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchWallPanel::OnPickItem, this);
	applyItemButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchWallPanel::OnApplyItem, this);
	removeItemButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchWallPanel::OnRemoveItem, this);
	pickDoorItemButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchWallPanel::OnPickDoorItem, this);
	applyDoorButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchWallPanel::OnApplyDoor, this);
	removeDoorButton->Bind(wxEVT_BUTTON, &MaterialsWorkbenchWallPanel::OnRemoveDoor, this);
	saveButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchWallPanel::OnSave, this);
	revertButton_->Bind(wxEVT_BUTTON, &MaterialsWorkbenchWallPanel::OnRevert, this);
	itemIdCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchWallPanel::OnItemIdChanged, this);
	itemIdCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchWallPanel::OnItemIdSpin, this);
	doorItemIdCtrl_->Bind(wxEVT_TEXT, &MaterialsWorkbenchWallPanel::OnDoorItemIdChanged, this);
	doorItemIdCtrl_->Bind(wxEVT_SPINCTRL, &MaterialsWorkbenchWallPanel::OnDoorItemIdSpin, this);
}

void MaterialsWorkbenchWallPanel::ClearWorkspace(const wxString &message) {
	wallBrushStorage_ = BrushStorageRecord();
	loadedWallBrushStorage_ = BrushStorageRecord();
	currentContextKey_.clear();
	currentItemIndex_ = -1;
	selectedPartIndex_ = -1;
	selectedItemIndex_ = -1;
	selectedDoorIndex_ = -1;
	hasWallBrush_ = false;
	dirty_ = false;
	partEditorStates_.clear();

	UpdateWorkspaceHeader();
	summaryLabel_->SetLabel(message);
	brushIdCtrl_->SetValue("");
	brushNameCtrl_->SetValue("");
	partChoice_->Clear();
	partSummaryLabel_->SetLabel("");
	previewFillRadio_->SetValue(true);
	previewDoorAutoRadio_->SetValue(true);
	previewOverlayCtrl_->SetValue(false);
	selectedItemLabel_->SetLabel("No wall item selected");
	itemIdCtrl_->SetValue(0);
	itemChanceCtrl_->SetValue(0);
	itemPreviewButton_->SetSprite(0);
	selectedDoorLabel_->SetLabel("No door selected");
	doorItemIdCtrl_->SetValue(0);
	doorTypeChoice_->SetSelection(wxNOT_FOUND);
	doorOpenCtrl_->SetValue(false);
	doorHateCtrl_->SetValue(false);
	doorPreviewButton_->SetSprite(0);

	itemGridSizer_->Clear(true);
	doorGridSizer_->Clear(true);
	itemButtons_.clear();
	doorButtons_.clear();

	SetFieldsEnabled(false);
	UpdateActionButtons();
	NotifyWallBrushStateChanged();
	SetStatusMessage(message);
	RefreshComposedPreview();
	Layout();
}

bool MaterialsWorkbenchWallPanel::LoadWallBrush(const wxString &contextKey, int itemIndex) {
	const int64_t previousBrushId = hasWallBrush_ ? wallBrushStorage_.brush.id : 0;
	const WallEditorState previousEditorState = hasWallBrush_ ? CaptureEditorState() : WallEditorState();

	wxString error;
	BrushStorageRecord storage;
	if (!controller_.GetBrushDetails(contextKey, itemIndex, storage, error)) {
		spdlog::warn(
			"Materials Workbench failed to load wall brush details: context='{}' index={} error='{}'",
			contextKey.ToStdString(),
			itemIndex,
			error.ToStdString()
		);
		ClearWorkspace("Failed to load wall brush details: " + error);
		return false;
	}

	BrushStorageRecord comparableStorage = storage;
	for (WallPartRecord &part : comparableStorage.wallParts) {
		NormalizeWallPartRecord(part);
	}
	for (size_t i = 0; i < comparableStorage.wallParts.size(); ++i) {
		comparableStorage.wallParts[i].sortOrder = static_cast<int>(i);
	}

	const bool preserveEditorState = previousEditorState.valid && previousBrushId == comparableStorage.brush.id;
	wallBrushStorage_ = storage;
	loadedWallBrushStorage_ = comparableStorage;
	currentContextKey_ = contextKey;
	currentItemIndex_ = itemIndex;
	hasWallBrush_ = true;
	dirty_ = false;
	partEditorStates_.clear();
	selectedPartIndex_ = storage.wallParts.empty() ? -1 : 0;
	selectedItemIndex_ = -1;
	selectedDoorIndex_ = -1;

	PopulateFields();
	if (preserveEditorState) {
		RestoreEditorState(previousEditorState);
	}
	SetFieldsEnabled(true);
	UpdateActionButtons();
	NotifyWallBrushStateChanged();
	SetStatusMessage("Ready. Editing wall data from materials.db. Update parts or doors, then Save or Revert.");
	spdlog::info(
		"Materials Workbench loaded wall brush from materials.db: id={} name='{}' preserved_context={}",
		static_cast<long long>(wallBrushStorage_.brush.id),
		wallBrushStorage_.brush.name.ToStdString(),
		preserveEditorState
	);
	Layout();
	return true;
}

void MaterialsWorkbenchWallPanel::PopulateFields() {
	const BrushRecord &brush = wallBrushStorage_.brush;
	UpdateWorkspaceHeader();
	summaryLabel_->SetLabel(wxString::Format(
		"Wall parts: %zu | Links: %zu | Source: %s",
		wallBrushStorage_.wallParts.size(),
		wallBrushStorage_.links.size(),
		brush.sourceFile
	));
	brushIdCtrl_->SetValue(wxString::Format("%lld", static_cast<long long>(brush.id)));
	brushNameCtrl_->SetValue(brush.name);

	RefreshPartChoice();
	RefreshSelectedPart();
}

void MaterialsWorkbenchWallPanel::RefreshPartChoice() {
	partChoice_->Clear();
	for (const WallPartRecord &part : wallBrushStorage_.wallParts) {
		partChoice_->Append(part.partType);
	}

	if (wallBrushStorage_.wallParts.empty()) {
		selectedPartIndex_ = -1;
		partChoice_->SetSelection(wxNOT_FOUND);
		return;
	}

	if (selectedPartIndex_ < 0 || selectedPartIndex_ >= static_cast<int>(wallBrushStorage_.wallParts.size())) {
		selectedPartIndex_ = 0;
	}
	partChoice_->SetSelection(selectedPartIndex_);
}

void MaterialsWorkbenchWallPanel::RefreshSelectedPart() {
	const WallPartRecord* part = GetSelectedPart();
	if (!part) {
		partSummaryLabel_->SetLabel("This wall brush has no editable wall parts yet.");
		itemGridSizer_->Clear(true);
		doorGridSizer_->Clear(true);
		itemButtons_.clear();
		doorButtons_.clear();
		SyncSelectedItemEditor();
		SyncSelectedDoorEditor();
		RefreshComposedPreview();
		Layout();
		return;
	}

	partSummaryLabel_->SetLabel(wxString::Format(
		"Part `%s` | Items: %zu | Doors: %zu",
		part->partType,
		part->items.size(),
		part->doors.size()
	));

	if (selectedItemIndex_ >= static_cast<int>(part->items.size())) {
		selectedItemIndex_ = -1;
	}
	if (selectedDoorIndex_ >= static_cast<int>(part->doors.size())) {
		selectedDoorIndex_ = -1;
	}

	RefreshItemGrid();
	RefreshDoorGrid();
	SyncSelectedItemEditor();
	SyncSelectedDoorEditor();
	RefreshComposedPreview();
	Layout();
}

void MaterialsWorkbenchWallPanel::RefreshItemGrid() {
	int viewX = 0;
	int viewY = 0;
	if (itemGridScroll_) {
		itemGridScroll_->GetViewStart(&viewX, &viewY);
	}

	itemGridSizer_->Clear(true);
	itemButtons_.clear();

	const WallPartRecord* part = GetSelectedPart();
	if (!part) {
		itemGridScroll_->Layout();
		return;
	}

	for (size_t i = 0; i < part->items.size(); ++i) {
		const WallPartItemRecord &item = part->items[i];
		wxPanel* cell = new wxPanel(itemGridScroll_, wxID_ANY);
		wxBoxSizer* cellSizer = new wxBoxSizer(wxVERTICAL);
		auto* button = new WallWorkspaceToggleButton(cell, item.itemId);
		button->SetValue(static_cast<int>(i) == selectedItemIndex_);
		button->Bind(wxEVT_LEFT_DOWN, [this, index = static_cast<int>(i)](wxMouseEvent &event) {
			selectedItemIndex_ = index;
			CallAfter([this]() {
				RefreshItemGrid();
				SyncSelectedItemEditor();
			});
		});
		cellSizer->Add(button, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(4));
		cellSizer->Add(new wxStaticText(cell, wxID_ANY, wxString::Format(wxT("id %d \u2022 chance %d"), item.itemId, item.chance)), 0, wxALIGN_CENTER);
		cell->SetSizer(cellSizer);
		itemGridSizer_->Add(cell, 0, wxALL, FromDIP(4));
		itemButtons_.push_back(button);
	}

	itemGridScroll_->FitInside();
	itemGridScroll_->Layout();
	itemGridScroll_->Scroll(viewX, viewY);
}

void MaterialsWorkbenchWallPanel::RefreshDoorGrid() {
	int viewX = 0;
	int viewY = 0;
	if (doorGridScroll_) {
		doorGridScroll_->GetViewStart(&viewX, &viewY);
	}

	doorGridSizer_->Clear(true);
	doorButtons_.clear();

	const WallPartRecord* part = GetSelectedPart();
	if (!part) {
		doorGridScroll_->Layout();
		return;
	}

	for (size_t i = 0; i < part->doors.size(); ++i) {
		const WallPartDoorRecord &door = part->doors[i];
		wxPanel* cell = new wxPanel(doorGridScroll_, wxID_ANY);
		wxBoxSizer* cellSizer = new wxBoxSizer(wxVERTICAL);
		auto* button = new WallWorkspaceToggleButton(cell, door.itemId);
		button->SetValue(static_cast<int>(i) == selectedDoorIndex_);
		button->Bind(wxEVT_LEFT_DOWN, [this, index = static_cast<int>(i)](wxMouseEvent &event) {
			selectedDoorIndex_ = index;
			CallAfter([this]() {
				RefreshDoorGrid();
				SyncSelectedDoorEditor();
			});
		});
		cellSizer->Add(button, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(4));
		cellSizer->Add(new wxStaticText(cell, wxID_ANY, wxString::Format(wxT("id %d \u2022 %s"), door.itemId, door.doorType)), 0, wxALIGN_CENTER);
		cellSizer->Add(new wxStaticText(cell, wxID_ANY, door.isOpen ? "open" : "closed"), 0, wxALIGN_CENTER);
		cell->SetSizer(cellSizer);
		doorGridSizer_->Add(cell, 0, wxALL, FromDIP(4));
		doorButtons_.push_back(button);
	}

	doorGridScroll_->FitInside();
	doorGridScroll_->Layout();
	doorGridScroll_->Scroll(viewX, viewY);
}

void MaterialsWorkbenchWallPanel::SyncSelectedItemEditor() {
	const WallPartRecord* part = GetSelectedPart();
	if (!part || selectedItemIndex_ < 0 || selectedItemIndex_ >= static_cast<int>(part->items.size())) {
		selectedItemLabel_->SetLabel("No wall item selected");
		itemIdCtrl_->SetValue(0);
		itemChanceCtrl_->SetValue(0);
		itemPreviewButton_->SetSprite(0);
		RefreshComposedPreview();
		return;
	}

	const WallPartItemRecord &item = part->items[selectedItemIndex_];
	selectedItemLabel_->SetLabel(wxString::Format("Editing item #%d", selectedItemIndex_ + 1));
	itemIdCtrl_->SetValue(item.itemId);
	itemChanceCtrl_->SetValue(item.chance);
	itemPreviewButton_->SetSprite(item.itemId);
	RefreshComposedPreview();
}

void MaterialsWorkbenchWallPanel::SyncSelectedDoorEditor() {
	const WallPartRecord* part = GetSelectedPart();
	if (!part || selectedDoorIndex_ < 0 || selectedDoorIndex_ >= static_cast<int>(part->doors.size())) {
		selectedDoorLabel_->SetLabel("No door selected");
		doorItemIdCtrl_->SetValue(0);
		doorTypeChoice_->SetSelection(wxNOT_FOUND);
		doorOpenCtrl_->SetValue(false);
		doorHateCtrl_->SetValue(false);
		doorPreviewButton_->SetSprite(0);
		RefreshComposedPreview();
		return;
	}

	const WallPartDoorRecord &door = part->doors[selectedDoorIndex_];
	selectedDoorLabel_->SetLabel("Editing door: " + DescribeDoor(door));
	doorItemIdCtrl_->SetValue(door.itemId);
	doorTypeChoice_->SetStringSelection(door.doorType);
	doorOpenCtrl_->SetValue(door.isOpen);
	doorHateCtrl_->SetValue(door.wallHateMe);
	doorPreviewButton_->SetSprite(door.itemId);
	RefreshComposedPreview();
}

void MaterialsWorkbenchWallPanel::RefreshComposedPreview() {
	if (!composedPreview_) {
		return;
	}
	auto* composedPreview = static_cast<WallWorkspaceComposedPreviewPanel*>(composedPreview_);
	const bool strict = previewStrictRadio_ && previewStrictRadio_->GetValue();
	const bool showOverlays = previewOverlayCtrl_ && previewOverlayCtrl_->GetValue();
	int doorSide = 0;
	if (previewDoorNorthRadio_ && previewDoorNorthRadio_->GetValue()) {
		doorSide = 1;
	} else if (previewDoorEastRadio_ && previewDoorEastRadio_->GetValue()) {
		doorSide = 2;
	} else if (previewDoorSouthRadio_ && previewDoorSouthRadio_->GetValue()) {
		doorSide = 3;
	} else if (previewDoorWestRadio_ && previewDoorWestRadio_->GetValue()) {
		doorSide = 4;
	}
	if (!hasWallBrush_) {
		composedPreview->SetPreviewState(nullptr, "", 0, 0, strict, showOverlays, doorSide);
		return;
	}

	const WallPartRecord* part = GetSelectedPart();
	const wxString selectedPartType = part ? part->partType : "";
	int selectedItemId = 0;
	int selectedDoorItemId = 0;
	if (part && selectedItemIndex_ >= 0 && selectedItemIndex_ < static_cast<int>(part->items.size())) {
		selectedItemId = part->items[selectedItemIndex_].itemId;
	}
	if (part && selectedDoorIndex_ >= 0 && selectedDoorIndex_ < static_cast<int>(part->doors.size())) {
		selectedDoorItemId = part->doors[selectedDoorIndex_].itemId;
	}
	composedPreview->SetPreviewState(&wallBrushStorage_, selectedPartType, selectedItemId, selectedDoorItemId, strict, showOverlays, doorSide);
}

void MaterialsWorkbenchWallPanel::NormalizeWallParts() {
	for (size_t i = 0; i < wallBrushStorage_.wallParts.size(); ++i) {
		wallBrushStorage_.wallParts[i].sortOrder = static_cast<int>(i);
		NormalizeWallPartRecord(wallBrushStorage_.wallParts[i]);
	}
}

void MaterialsWorkbenchWallPanel::SetStatusMessage(const wxString &message) {
	statusLabel_->SetLabel(message);
	statusLabel_->Wrap(FromDIP(760));
}

void MaterialsWorkbenchWallPanel::SetFieldsEnabled(bool enabled) {
	partChoice_->Enable(enabled);
	addPartButton_->Enable(enabled);
	previewFillRadio_->Enable(enabled);
	previewStrictRadio_->Enable(enabled);
	previewOverlayCtrl_->Enable(enabled);
	previewDoorAutoRadio_->Enable(enabled);
	previewDoorNorthRadio_->Enable(enabled);
	previewDoorEastRadio_->Enable(enabled);
	previewDoorSouthRadio_->Enable(enabled);
	previewDoorWestRadio_->Enable(enabled);
	itemIdCtrl_->Enable(enabled);
	itemChanceCtrl_->Enable(enabled);
	doorItemIdCtrl_->Enable(enabled);
	doorTypeChoice_->Enable(enabled);
	doorOpenCtrl_->Enable(enabled);
	doorHateCtrl_->Enable(enabled);
	itemPreviewButton_->Enable(enabled);
	doorPreviewButton_->Enable(enabled);
}

BrushStorageRecord MaterialsWorkbenchWallPanel::BuildComparableStorageFromCurrentState() const {
	BrushStorageRecord storage = wallBrushStorage_;
	for (size_t i = 0; i < storage.wallParts.size(); ++i) {
		storage.wallParts[i].sortOrder = static_cast<int>(i);
		NormalizeWallPartRecord(storage.wallParts[i]);
	}
	return storage;
}

MaterialsWorkbenchWallPanel::WallEditorState MaterialsWorkbenchWallPanel::CaptureEditorState() const {
	WallEditorState state;
	state.valid = hasWallBrush_;
	if (itemGridScroll_) {
		itemGridScroll_->GetViewStart(&state.itemGridViewX, &state.itemGridViewY);
	}
	if (doorGridScroll_) {
		doorGridScroll_->GetViewStart(&state.doorGridViewX, &state.doorGridViewY);
	}

	const WallPartRecord* part = GetSelectedPart();
	if (!part) {
		return state;
	}

	state.partType = part->partType;
	if (selectedItemIndex_ >= 0 && selectedItemIndex_ < static_cast<int>(part->items.size())) {
		state.itemSortOrder = part->items[selectedItemIndex_].sortOrder;
		state.itemId = part->items[selectedItemIndex_].itemId;
	}
	if (selectedDoorIndex_ >= 0 && selectedDoorIndex_ < static_cast<int>(part->doors.size())) {
		const WallPartDoorRecord &door = part->doors[selectedDoorIndex_];
		state.doorSortOrder = door.sortOrder;
		state.doorItemId = door.itemId;
		state.doorType = door.doorType;
		state.doorIsOpen = door.isOpen;
		state.doorWallHateMe = door.wallHateMe;
	}
	return state;
}

void MaterialsWorkbenchWallPanel::RestoreEditorState(const WallEditorState &state) {
	if (!state.valid || !hasWallBrush_) {
		return;
	}

	selectedPartIndex_ = 0;
	for (size_t i = 0; i < wallBrushStorage_.wallParts.size(); ++i) {
		if (wallBrushStorage_.wallParts[i].partType == state.partType) {
			selectedPartIndex_ = static_cast<int>(i);
			break;
		}
	}

	const WallPartRecord* part = GetSelectedPart();
	selectedItemIndex_ = -1;
	selectedDoorIndex_ = -1;
	if (!part) {
		RefreshPartChoice();
		RefreshSelectedPart();
		return;
	}

	for (size_t i = 0; i < part->items.size(); ++i) {
		const WallPartItemRecord &item = part->items[i];
		if (item.sortOrder == state.itemSortOrder && item.itemId == state.itemId) {
			selectedItemIndex_ = static_cast<int>(i);
			break;
		}
	}
	if (selectedItemIndex_ == -1 && state.itemId > 0) {
		for (size_t i = 0; i < part->items.size(); ++i) {
			if (part->items[i].itemId == state.itemId) {
				selectedItemIndex_ = static_cast<int>(i);
				break;
			}
		}
	}

	for (size_t i = 0; i < part->doors.size(); ++i) {
		const WallPartDoorRecord &door = part->doors[i];
		if (door.sortOrder == state.doorSortOrder &&
			door.itemId == state.doorItemId &&
			door.doorType == state.doorType &&
			door.isOpen == state.doorIsOpen &&
			door.wallHateMe == state.doorWallHateMe) {
			selectedDoorIndex_ = static_cast<int>(i);
			break;
		}
	}
	if (selectedDoorIndex_ == -1 && state.doorItemId > 0) {
		for (size_t i = 0; i < part->doors.size(); ++i) {
			const WallPartDoorRecord &door = part->doors[i];
			if (door.itemId == state.doorItemId && door.doorType == state.doorType) {
				selectedDoorIndex_ = static_cast<int>(i);
				break;
			}
		}
	}

	RefreshPartChoice();
	RefreshSelectedPart();
	if (itemGridScroll_ && state.itemGridViewX >= 0 && state.itemGridViewY >= 0) {
		itemGridScroll_->Scroll(state.itemGridViewX, state.itemGridViewY);
	}
	if (doorGridScroll_ && state.doorGridViewX >= 0 && state.doorGridViewY >= 0) {
		doorGridScroll_->Scroll(state.doorGridViewX, state.doorGridViewY);
	}
	SaveCurrentPartEditorState();
}

void MaterialsWorkbenchWallPanel::SaveCurrentPartEditorState() {
	if (!hasWallBrush_) {
		return;
	}

	const WallEditorState state = CaptureEditorState();
	if (!state.valid || state.partType.IsEmpty()) {
		return;
	}

	partEditorStates_[state.partType] = state;
}

void MaterialsWorkbenchWallPanel::RestoreCurrentPartEditorState() {
	const WallPartRecord* part = GetSelectedPart();
	selectedItemIndex_ = -1;
	selectedDoorIndex_ = -1;
	if (!part) {
		RefreshSelectedPart();
		return;
	}

	auto it = partEditorStates_.find(part->partType);
	if (it == partEditorStates_.end()) {
		RefreshSelectedPart();
		return;
	}

	const WallEditorState &state = it->second;
	for (size_t i = 0; i < part->items.size(); ++i) {
		const WallPartItemRecord &item = part->items[i];
		if (item.sortOrder == state.itemSortOrder && item.itemId == state.itemId) {
			selectedItemIndex_ = static_cast<int>(i);
			break;
		}
	}
	if (selectedItemIndex_ == -1 && state.itemId > 0) {
		for (size_t i = 0; i < part->items.size(); ++i) {
			if (part->items[i].itemId == state.itemId) {
				selectedItemIndex_ = static_cast<int>(i);
				break;
			}
		}
	}

	for (size_t i = 0; i < part->doors.size(); ++i) {
		const WallPartDoorRecord &door = part->doors[i];
		if (door.sortOrder == state.doorSortOrder &&
			door.itemId == state.doorItemId &&
			door.doorType == state.doorType &&
			door.isOpen == state.doorIsOpen &&
			door.wallHateMe == state.doorWallHateMe) {
			selectedDoorIndex_ = static_cast<int>(i);
			break;
		}
	}
	if (selectedDoorIndex_ == -1 && state.doorItemId > 0) {
		for (size_t i = 0; i < part->doors.size(); ++i) {
			const WallPartDoorRecord &door = part->doors[i];
			if (door.itemId == state.doorItemId && door.doorType == state.doorType) {
				selectedDoorIndex_ = static_cast<int>(i);
				break;
			}
		}
	}

	RefreshSelectedPart();
	if (itemGridScroll_ && state.itemGridViewX >= 0 && state.itemGridViewY >= 0) {
		itemGridScroll_->Scroll(state.itemGridViewX, state.itemGridViewY);
	}
	if (doorGridScroll_ && state.doorGridViewX >= 0 && state.doorGridViewY >= 0) {
		doorGridScroll_->Scroll(state.doorGridViewX, state.doorGridViewY);
	}
}

void MaterialsWorkbenchWallPanel::RefreshDirtyState() {
	if (!hasWallBrush_) {
		dirty_ = false;
		UpdateWorkspaceHeader();
		UpdateActionButtons();
		NotifyWallBrushStateChanged();
		return;
	}

	dirty_ = !WallPanelVectorsEqual(
		BuildComparableStorageFromCurrentState().wallParts,
		loadedWallBrushStorage_.wallParts,
		AreWallPanelPartRecordsEqual
	);
	UpdateWorkspaceHeader();
	UpdateActionButtons();
	NotifyWallBrushStateChanged();
}

void MaterialsWorkbenchWallPanel::NotifyWallBrushStateChanged() {
	if (onWallBrushStateChanged_) {
		onWallBrushStateChanged_();
	}
}

void MaterialsWorkbenchWallPanel::UpdateWorkspaceHeader() {
	if (!hasWallBrush_) {
		titleLabel_->SetLabel("No wall brush selected");
		subtitleLabel_->SetLabel("Select a wall brush in the navigation tree to edit wall parts, alternates, and door definitions.");
		return;
	}

	titleLabel_->SetLabel("Editing wall brush: " + wallBrushStorage_.brush.name + (dirty_ ? " [modified]" : ""));
	subtitleLabel_->SetLabel(
		dirty_
			? "Unsaved local wall edits differ from materials.db. Save to persist them or Revert to discard them before switching entries."
			: "Ready to edit wall parts, alternates, and door definitions for this SQLite-backed brush."
	);
}

void MaterialsWorkbenchWallPanel::UpdateActionButtons() {
	if (saveButton_) {
		saveButton_->Enable(hasWallBrush_ && dirty_);
	}
	if (revertButton_) {
		revertButton_->Enable(hasWallBrush_ && dirty_);
	}
}

bool MaterialsWorkbenchWallPanel::ValidateWallBrushStorage(wxString &error) const {
	for (size_t partIndex = 0; partIndex < wallBrushStorage_.wallParts.size(); ++partIndex) {
		const WallPartRecord &part = wallBrushStorage_.wallParts[partIndex];

		for (size_t itemIndex = 0; itemIndex < part.items.size(); ++itemIndex) {
			const WallPartItemRecord &item = part.items[itemIndex];
			if (item.itemId <= 0) {
				error = wxString::Format("Wall part %zu item %zu must use an item id greater than zero.", partIndex + 1, itemIndex + 1);
				return false;
			}
			if (!IsKnownWallPanelItemId(item.itemId)) {
				error = wxString::Format("Wall part %zu item %zu uses unknown item id %d.", partIndex + 1, itemIndex + 1, item.itemId);
				return false;
			}
		}

		for (size_t doorIndex = 0; doorIndex < part.doors.size(); ++doorIndex) {
			const WallPartDoorRecord &door = part.doors[doorIndex];
			if (door.itemId <= 0) {
				error = wxString::Format("Wall part %zu door %zu must use an item id greater than zero.", partIndex + 1, doorIndex + 1);
				return false;
			}
			if (!IsKnownWallPanelItemId(door.itemId)) {
				error = wxString::Format("Wall part %zu door %zu uses unknown item id %d.", partIndex + 1, doorIndex + 1, door.itemId);
				return false;
			}

			const WallPanelDoorTypeSpec doorTypeSpec = ParseWallPanelDoorTypeSpec(door.doorType);
			if (!doorTypeSpec.valid) {
				error = wxString::Format(
					"Wall part %zu door %zu uses unsupported door type \"%s\".",
					partIndex + 1,
					doorIndex + 1,
					door.doorType
				);
				return false;
			}

			const ItemType &itemType = g_items.getItemType(static_cast<uint16_t>(door.itemId));
			const bool hasRuntimeDoorRegistration =
				itemType.isWall &&
				itemType.isBrushDoor &&
				itemType.brush &&
				itemType.brush->isWall();
			if (hasRuntimeDoorRegistration) {
				if (itemType.isOpen != door.isOpen) {
					error = wxString::Format(
						"Wall part %zu door %zu uses item id %d with open=%s, but the selected record is %s.",
						partIndex + 1,
						doorIndex + 1,
						door.itemId,
						itemType.isOpen ? "true" : "false",
						door.isOpen ? "open" : "closed"
					);
					return false;
				}
				if (itemType.wall_hate_me != door.wallHateMe) {
					error = wxString::Format(
						"Wall part %zu door %zu uses item id %d with hate=%s, but the selected record is %s.",
						partIndex + 1,
						doorIndex + 1,
						door.itemId,
						itemType.wall_hate_me ? "true" : "false",
						door.wallHateMe ? "hate" : "not hate"
					);
					return false;
				}

				const ::DoorType runtimeDoorType = itemType.brush->asWall()->getDoorTypeFromID(static_cast<uint16_t>(door.itemId));
				if (runtimeDoorType == WALL_UNDEFINED) {
					error = wxString::Format(
						"Wall part %zu door %zu item id %d is not mapped to a wall door type in the runtime catalog.",
						partIndex + 1,
						doorIndex + 1,
						door.itemId
					);
					return false;
				}
				const WallPanelDoorFamily runtimeDoorFamily = GetWallPanelDoorFamily(runtimeDoorType);
				if (doorTypeSpec.expectsExact && runtimeDoorType != doorTypeSpec.exactType) {
					error = wxString::Format(
						"Wall part %zu door %zu uses item id %d for \"%s\", but the runtime door type is \"%s\".",
						partIndex + 1,
						doorIndex + 1,
						door.itemId,
						doorTypeSpec.normalizedLabel,
						DescribeWallPanelDoorType(runtimeDoorType)
					);
					return false;
				}
				if (!doorTypeSpec.allowAny &&
					doorTypeSpec.family != WallPanelDoorFamily::Unknown &&
					runtimeDoorFamily != WallPanelDoorFamily::Unknown &&
					runtimeDoorFamily != doorTypeSpec.family) {
					error = wxString::Format(
						"Wall part %zu door %zu uses item id %d for \"%s\", but the item belongs to \"%s\".",
						partIndex + 1,
						doorIndex + 1,
						door.itemId,
						doorTypeSpec.normalizedLabel,
						DescribeWallPanelDoorType(runtimeDoorType)
					);
					return false;
				}
			}
		}
	}

	return true;
}

bool MaterialsWorkbenchWallPanel::SaveCurrentWallBrush() {
	if (!hasWallBrush_) {
		SetStatusMessage("Select a wall brush before saving.");
		return false;
	}

	if (!dirty_) {
		SetStatusMessage("No pending wall changes to save.");
		return true;
	}

	const WallEditorState previousEditorState = CaptureEditorState();
	wallBrushStorage_ = BuildComparableStorageFromCurrentState();

	wxString validationError;
	if (!ValidateWallBrushStorage(validationError)) {
		spdlog::warn(
			"Materials Workbench blocked wall brush save for id={} name='{}': {}",
			static_cast<long long>(wallBrushStorage_.brush.id),
			wallBrushStorage_.brush.name.ToStdString(),
			validationError.ToStdString()
		);
		SetStatusMessage("Cannot save wall brush: " + validationError);
		return false;
	}

	wxString error;
	if (!controller_.SaveWallBrushParts(wallBrushStorage_, error)) {
		spdlog::warn(
			"Materials Workbench failed to save wall brush parts after validation: id={} name='{}' error='{}'",
			static_cast<long long>(wallBrushStorage_.brush.id),
			wallBrushStorage_.brush.name.ToStdString(),
			error.ToStdString()
		);
		SetStatusMessage("Failed to save wall brush parts: " + error);
		return false;
	}

	loadedWallBrushStorage_ = wallBrushStorage_;
	PopulateFields();
	if (previousEditorState.valid) {
		RestoreEditorState(previousEditorState);
	}
	RefreshDirtyState();
	SetStatusMessage("Saved wall parts and doors to materials.db. Targeted runtime sync remained in place.");
	spdlog::info(
		"Materials Workbench saved wall brush parts: id={} name='{}' preserved_context={}",
		static_cast<long long>(wallBrushStorage_.brush.id),
		wallBrushStorage_.brush.name.ToStdString(),
		previousEditorState.valid
	);
	if (onWallBrushSaved_) {
		onWallBrushSaved_(wallBrushStorage_.brush.id);
	}
	return true;
}

void MaterialsWorkbenchWallPanel::OnPartChanged(wxCommandEvent &event) {
	SaveCurrentPartEditorState();
	selectedPartIndex_ = partChoice_->GetSelection();
	RestoreCurrentPartEditorState();
	event.Skip();
}

void MaterialsWorkbenchWallPanel::OnPickItem(wxCommandEvent &event) {
	if (!GetSelectedPart()) {
		SetStatusMessage("Select a wall part before choosing an item.");
		return;
	}

	FindItemDialog dialog(this, "Select Wall Item");
	dialog.setSearchMode(FindItemDialog::ItemIDs);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	itemIdCtrl_->SetValue(dialog.getResultID());
	itemPreviewButton_->SetSprite(dialog.getResultID());
}

void MaterialsWorkbenchWallPanel::OnApplyItem(wxCommandEvent &event) {
	WallPartRecord* part = GetSelectedPart();
	if (!part) {
		SetStatusMessage("Select a wall part before applying an item.");
		return;
	}

	WallPartItemRecord record;
	record.itemId = itemIdCtrl_->GetValue();
	record.chance = itemChanceCtrl_->GetValue();
	if (record.itemId <= 0) {
		SetStatusMessage("Wall item id must be greater than zero.");
		return;
	}
	if (!IsKnownWallPanelItemId(record.itemId)) {
		SetStatusMessage(wxString::Format("Wall item id %d is not present in the current item catalog.", record.itemId));
		return;
	}

	if (selectedItemIndex_ >= 0 && selectedItemIndex_ < static_cast<int>(part->items.size())) {
		record.sortOrder = part->items[selectedItemIndex_].sortOrder;
		part->items[selectedItemIndex_] = record;
	} else {
		record.sortOrder = static_cast<int>(part->items.size());
		part->items.push_back(record);
		selectedItemIndex_ = static_cast<int>(part->items.size()) - 1;
	}

	NormalizeWallPartRecord(*part);
	RefreshSelectedPart();
	RefreshDirtyState();
	SetStatusMessage("Wall item updated locally. Save the wall brush to persist.");
}

void MaterialsWorkbenchWallPanel::OnRemoveItem(wxCommandEvent &event) {
	WallPartRecord* part = GetSelectedPart();
	if (!part || selectedItemIndex_ < 0 || selectedItemIndex_ >= static_cast<int>(part->items.size())) {
		SetStatusMessage("Select a wall item before removing it.");
		return;
	}

	part->items.erase(part->items.begin() + selectedItemIndex_);
	selectedItemIndex_ = -1;
	NormalizeWallPartRecord(*part);
	RefreshSelectedPart();
	RefreshDirtyState();
	SetStatusMessage("Wall item removed locally. Save the wall brush to persist.");
}

void MaterialsWorkbenchWallPanel::OnPickDoorItem(wxCommandEvent &event) {
	if (!GetSelectedPart()) {
		SetStatusMessage("Select a wall part before choosing a door item.");
		return;
	}

	FindItemDialog dialog(this, "Select Door Item");
	dialog.setSearchMode(FindItemDialog::ItemIDs);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	doorItemIdCtrl_->SetValue(dialog.getResultID());
	doorPreviewButton_->SetSprite(dialog.getResultID());
}

void MaterialsWorkbenchWallPanel::OnApplyDoor(wxCommandEvent &event) {
	WallPartRecord* part = GetSelectedPart();
	if (!part) {
		SetStatusMessage("Select a wall part before applying a door.");
		return;
	}
	if (doorTypeChoice_->GetSelection() == wxNOT_FOUND) {
		SetStatusMessage("Select a door type before applying the door.");
		return;
	}

	WallPartDoorRecord record;
	record.itemId = doorItemIdCtrl_->GetValue();
	record.doorType = doorTypeChoice_->GetStringSelection();
	record.isOpen = doorOpenCtrl_->GetValue();
	record.wallHateMe = doorHateCtrl_->GetValue();
	if (record.itemId <= 0) {
		SetStatusMessage("Door item id must be greater than zero.");
		return;
	}
	if (!IsKnownWallPanelItemId(record.itemId)) {
		SetStatusMessage(wxString::Format("Door item id %d is not present in the current item catalog.", record.itemId));
		return;
	}

	const bool replacingExistingDoor = selectedDoorIndex_ >= 0 && selectedDoorIndex_ < static_cast<int>(part->doors.size());
	WallPartDoorRecord previousDoorRecord;
	if (replacingExistingDoor) {
		previousDoorRecord = part->doors[selectedDoorIndex_];
	}

	if (selectedDoorIndex_ >= 0 && selectedDoorIndex_ < static_cast<int>(part->doors.size())) {
		record.sortOrder = part->doors[selectedDoorIndex_].sortOrder;
		part->doors[selectedDoorIndex_] = record;
	} else {
		record.sortOrder = static_cast<int>(part->doors.size());
		part->doors.push_back(record);
		selectedDoorIndex_ = static_cast<int>(part->doors.size()) - 1;
	}

	NormalizeWallPartRecord(*part);
	wxString validationError;
	if (!ValidateWallBrushStorage(validationError)) {
		spdlog::warn(
			"Materials Workbench blocked wall door apply for brush id={} name='{}' part='{}' item_id={} type='{}': {}",
			static_cast<long long>(wallBrushStorage_.brush.id),
			wallBrushStorage_.brush.name.ToStdString(),
			part->partType.ToStdString(),
			record.itemId,
			record.doorType.ToStdString(),
			validationError.ToStdString()
		);
		if (replacingExistingDoor && selectedDoorIndex_ >= 0 && selectedDoorIndex_ < static_cast<int>(part->doors.size())) {
			part->doors[selectedDoorIndex_] = previousDoorRecord;
		} else if (selectedDoorIndex_ >= 0 && selectedDoorIndex_ < static_cast<int>(part->doors.size())) {
			part->doors.erase(part->doors.begin() + selectedDoorIndex_);
			selectedDoorIndex_ = -1;
		}
		NormalizeWallPartRecord(*part);
		RefreshSelectedPart();
		SetStatusMessage("Cannot apply door: " + validationError);
		return;
	}
	RefreshSelectedPart();
	RefreshDirtyState();
	SetStatusMessage("Door updated locally. Save the wall brush to persist.");
}

void MaterialsWorkbenchWallPanel::OnRemoveDoor(wxCommandEvent &event) {
	WallPartRecord* part = GetSelectedPart();
	if (!part || selectedDoorIndex_ < 0 || selectedDoorIndex_ >= static_cast<int>(part->doors.size())) {
		SetStatusMessage("Select a door before removing it.");
		return;
	}

	part->doors.erase(part->doors.begin() + selectedDoorIndex_);
	selectedDoorIndex_ = -1;
	NormalizeWallPartRecord(*part);
	RefreshSelectedPart();
	RefreshDirtyState();
	SetStatusMessage("Door removed locally. Save the wall brush to persist.");
}

void MaterialsWorkbenchWallPanel::OnAddPartType(wxCommandEvent &event) {
	if (!hasWallBrush_) {
		return;
	}

	const auto partTypeExists = [this](const wxString &baseType) {
		for (const WallPartRecord &part : wallBrushStorage_.wallParts) {
			if (NormalizeWallPreviewPartType(part.partType) == baseType) {
				return true;
			}
		}
		return false;
	};

	wxMenu menu;
	struct Candidate {
		wxString label;
		wxString baseType;
	};
	const Candidate candidates[] = {
		{ "vertical", "vertical" },
		{ "horizontal", "horizontal" },
		{ "pole", "pole" },
		{ "corner", "corner" },
		{ "north end", "north end" },
		{ "south end", "south end" },
		{ "east end", "east end" },
		{ "west end", "west end" },
		{ "intersection", "intersection" },
		{ "north T", "north t" },
		{ "south T", "south t" },
		{ "east T", "east t" },
		{ "west T", "west t" },
		{ "northwest diagonal", "northwest diagonal" },
		{ "northeast diagonal", "northeast diagonal" },
		{ "southwest diagonal", "southwest diagonal" },
		{ "southeast diagonal", "southeast diagonal" },
	};

	int selectedId = wxID_NONE;
	for (const Candidate &candidate : candidates) {
		const bool enabled = !partTypeExists(candidate.baseType);
		wxMenuItem* item = menu.Append(wxID_ANY, candidate.label);
		item->Enable(enabled);
		if (enabled && selectedId == wxID_NONE) {
			selectedId = item->GetId();
		}
	}

	selectedId = GetPopupMenuSelectionFromUser(menu);
	if (selectedId == wxID_NONE) {
		return;
	}

	wxString selectedLabel;
	for (wxMenuItem* item : menu.GetMenuItems()) {
		if (item->GetId() == selectedId) {
			selectedLabel = item->GetItemLabelText();
			break;
		}
	}
	if (selectedLabel.IsEmpty()) {
		return;
	}

	SaveCurrentPartEditorState();

	WallPartRecord part;
	part.partType = selectedLabel;
	part.sortOrder = static_cast<int>(wallBrushStorage_.wallParts.size());
	NormalizeWallPartRecord(part);
	wallBrushStorage_.wallParts.push_back(part);
	selectedPartIndex_ = static_cast<int>(wallBrushStorage_.wallParts.size()) - 1;

	RefreshPartChoice();
	RefreshSelectedPart();
	RefreshDirtyState();
	SetStatusMessage("Added new wall part locally. Save the wall brush to persist.");
}

void MaterialsWorkbenchWallPanel::OnPreviewOptionsChanged(wxCommandEvent &event) {
	RefreshComposedPreview();
	event.Skip();
}

void MaterialsWorkbenchWallPanel::OnSave(wxCommandEvent &event) {
	SaveCurrentWallBrush();
}

void MaterialsWorkbenchWallPanel::OnRevert(wxCommandEvent &event) {
	if (!hasWallBrush_) {
		ClearWorkspace("Select a wall brush in the navigation tree to edit its wall parts.");
		return;
	}
	const int64_t brushId = wallBrushStorage_.brush.id;
	const wxString brushName = wallBrushStorage_.brush.name;
	if (!dirty_) {
		SetStatusMessage("Wall brush already matches materials.db.");
		return;
	}

	if (!LoadWallBrush(currentContextKey_, currentItemIndex_)) {
		spdlog::warn(
			"Materials Workbench failed to revert wall brush from materials.db: id={} name='{}'",
			static_cast<long long>(brushId),
			brushName.ToStdString()
		);
		return;
	}

	SetStatusMessage("Reverted local wall edits and reloaded the wall brush from materials.db.");
	spdlog::info(
		"Materials Workbench reverted wall brush from materials.db: id={} name='{}'",
		static_cast<long long>(brushId),
		brushName.ToStdString()
	);
}

void MaterialsWorkbenchWallPanel::OnItemIdChanged(wxCommandEvent &event) {
	itemPreviewButton_->SetSprite(itemIdCtrl_->GetValue());
	event.Skip();
}

void MaterialsWorkbenchWallPanel::OnItemIdSpin(wxSpinEvent &event) {
	itemPreviewButton_->SetSprite(itemIdCtrl_->GetValue());
	event.Skip();
}

void MaterialsWorkbenchWallPanel::OnDoorItemIdChanged(wxCommandEvent &event) {
	doorPreviewButton_->SetSprite(doorItemIdCtrl_->GetValue());
	event.Skip();
}

void MaterialsWorkbenchWallPanel::OnDoorItemIdSpin(wxSpinEvent &event) {
	doorPreviewButton_->SetSprite(doorItemIdCtrl_->GetValue());
	event.Skip();
}
