#pragma once

#include <wx/scrolwin.h>
#include <wx/dcbuffer.h>
#include <wx/event.h>
#include <wx/string.h>

#include <vector>

#include "gui.h"
#include "items.h"
#include "graphics.h"

class BrushBrowserGridPanel : public wxScrolledWindow {
public:
    struct Entry {
        wxString label;
        wxString key;
        uint16_t previewItemId = 0;
    };

    BrushBrowserGridPanel(wxWindow* parent, wxWindowID id = wxID_ANY) :
        wxScrolledWindow(parent, id, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_NONE)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetScrollRate(0, m_cellSize + m_padding);

        Bind(wxEVT_PAINT, &BrushBrowserGridPanel::OnPaint, this);
        Bind(wxEVT_LEFT_UP, &BrushBrowserGridPanel::OnLeftUp, this);
        Bind(wxEVT_SIZE, &BrushBrowserGridPanel::OnSize, this);
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
        if (!key.IsEmpty()) {
            for (size_t i = 0; i < m_entries.size(); ++i) {
                if (m_entries[i].key == key) {
                    m_selected = (int)i;
                    break;
                }
            }
        }
        Refresh();
    }

private:
    std::vector<Entry> m_entries;
    int m_cellSize = 32;
    int m_padding = 2;
    int m_cols = 1;
    int m_rows = 0;
    int m_selected = -1;

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

    void OnPaint(wxPaintEvent&) {
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
                    sprite->DrawTo(&dc, SPRITE_SIZE_32x32, x, y, m_cellSize, m_cellSize);
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
};