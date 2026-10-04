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

#ifndef RME_COLLAB_WINDOW_H
#define RME_COLLAB_WINDOW_H

#include <wx/panel.h>

#include "../map_comments.h"

class wxCheckBox;
class wxListCtrl;
class wxListEvent;
class wxNotebook;
class wxSearchCtrl;
class wxTextCtrl;

// Dockable "Collaborate" panel. Session, Chat and History are placeholders until
// the networking phases land; Comments already works against Map::comments.
class CollabWindow : public wxPanel {
public:
	explicit CollabWindow(wxWindow* parent);
	~CollabWindow() override;

	enum Page {
		PAGE_SESSION,
		PAGE_CHAT,
		PAGE_COMMENTS,
		PAGE_HISTORY,
	};
	void SelectPage(Page page);

	// Rebuilds the comments list from the current editor's map.
	void RefreshComments();

	static CollabWindow* Get() {
		return instance;
	}

	// Asks for a comment on the tile under the map cursor. Used by the menu and the panel.
	static void AddCommentAtCursor();

private:
	void BuildSessionPage(wxWindow* page);
	void BuildCommentsPage(wxWindow* page);
	const MapComment* SelectedComment() const;

	wxNotebook* notebook = nullptr;
	wxTextCtrl* name_text = nullptr;
	wxListCtrl* comment_list = nullptr;
	wxCheckBox* show_resolved = nullptr;
	wxSearchCtrl* search = nullptr;

	static CollabWindow* instance;
};

#endif
