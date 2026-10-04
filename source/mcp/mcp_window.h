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

#ifndef RME_MCP_WINDOW_H
#define RME_MCP_WINDOW_H

#include <wx/panel.h>
#include <wx/textctrl.h>

class wxButton;
class wxCheckBox;
class wxChoice;
class wxSpinCtrl;
class wxSpinEvent;
class wxStaticText;

// Dockable panel that owns the MCP server's lifecycle: start/stop, port,
// the read-only gate, and a log of what the connected LLM is doing.
class McpWindow : public wxPanel {
public:
	explicit McpWindow(wxWindow* parent);
	~McpWindow() override;

	void LogMessage(const wxString &message, bool isError = false);

	static McpWindow* Get() {
		return instance;
	}

	// Starts the server without a panel open, for the auto-start on boot.
	static void StartFromSettings();

protected:
	void BuildUI();
	void UpdateStatus();

	void OnToggleServer(wxCommandEvent &event);
	void OnPortChanged(wxSpinEvent &event);
	void OnToggleWrite(wxCommandEvent &event);
	void OnCopyConfig(wxCommandEvent &event);
	void OnClearLog(wxCommandEvent &event);

private:
	wxCheckBox* enable_checkbox = nullptr;
	wxCheckBox* write_checkbox = nullptr;
	wxSpinCtrl* port_spin = nullptr;
	wxStaticText* status_label = nullptr;
	wxTextCtrl* endpoint_text = nullptr;
	wxChoice* client_choice = nullptr;
	wxButton* copy_button = nullptr;
	wxTextCtrl* log_output = nullptr;

	static McpWindow* instance;

	DECLARE_EVENT_TABLE()
};

#endif // RME_MCP_WINDOW_H
