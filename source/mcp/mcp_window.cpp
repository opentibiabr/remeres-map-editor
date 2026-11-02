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

#include "mcp_window.h"

#include "mcp_server.h"
#include "mcp_tools.h"

#include "../gui_ids.h"
#include "../settings.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/clipbrd.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

McpWindow* McpWindow::instance = nullptr;

BEGIN_EVENT_TABLE(McpWindow, wxPanel)
EVT_CHECKBOX(MCP_ENABLE_CHECKBOX, McpWindow::OnToggleServer)
EVT_CHECKBOX(MCP_WRITE_CHECKBOX, McpWindow::OnToggleWrite)
EVT_SPINCTRL(MCP_PORT_SPIN, McpWindow::OnPortChanged)
EVT_BUTTON(MCP_COPY_CONFIG, McpWindow::OnCopyConfig)
EVT_BUTTON(MCP_CLEAR_LOG, McpWindow::OnClearLog)
END_EVENT_TABLE()

namespace {

	// The server logs from the asio thread; wx widgets may only be touched on
	// the GUI thread, so every line is bounced through CallAfter.
	void InstallLogBridge() {
		mcp::Server::get().setLogCallback([](mcp::Server::LogLevel level, const std::string &message) {
			const bool isError = level == mcp::Server::LogLevel::Error;
			auto deliver = [message, isError]() {
				if (McpWindow* window = McpWindow::Get()) {
					window->LogMessage(wxString::FromUTF8(message), isError);
				}
			};

			if (wxThread::IsMain()) {
				deliver();
			} else {
				wxTheApp->CallAfter(deliver);
			}
		});
	}

	wxString BuildConfigSnippet(int clientIndex, const wxString &url) {
		switch (clientIndex) {
			case 0: // Claude Code
				return wxString::Format("claude mcp add --transport http remeres %s", url);
			case 1: // Claude Desktop
				return wxString::Format(
					"\"mcpServers\": {\n"
					"  \"remeres\": {\n"
					"    \"type\": \"http\",\n"
					"    \"url\": \"%s\"\n"
					"  }\n"
					"}",
					url
				);
			case 2: // Codex
				return wxString::Format(
					"[mcp_servers.remeres]\n"
					"url = \"%s\"",
					url
				);
			default: // stdio bridge, for clients without HTTP transport
				return wxString::Format(
					"\"remeres\": {\n"
					"  \"command\": \"python\",\n"
					"  \"args\": [\"tools/mcp_stdio_bridge.py\", \"%s\"]\n"
					"}",
					url
				);
		}
	}

} // namespace

McpWindow::McpWindow(wxWindow* parent) :
	wxPanel(parent, wxID_ANY) {
	instance = this;
	BuildUI();
	InstallLogBridge();
	UpdateStatus();
}

McpWindow::~McpWindow() {
	mcp::Server::get().setLogCallback(nullptr);
	if (instance == this) {
		instance = nullptr;
	}
}

void McpWindow::StartFromSettings() {
	if (g_settings.getInteger(Config::MCP_ENABLED) == 0) {
		return;
	}

	mcp::Server &server = mcp::Server::get();
	server.setWriteAllowed(g_settings.getInteger(Config::MCP_ALLOW_WRITE) != 0);
	server.start(static_cast<uint16_t>(g_settings.getInteger(Config::MCP_PORT)));
}

void McpWindow::BuildUI() {
	wxBoxSizer* mainSizer = newd wxBoxSizer(wxVERTICAL);

	// --- server controls -------------------------------------------------
	wxBoxSizer* controlSizer = newd wxBoxSizer(wxHORIZONTAL);

	enable_checkbox = newd wxCheckBox(this, MCP_ENABLE_CHECKBOX, "Enable server");
	enable_checkbox->SetToolTip("Start the MCP server so an AI client can read and edit this map");
	controlSizer->Add(enable_checkbox, 0, wxALIGN_CENTER_VERTICAL | wxALL, 4);

	controlSizer->Add(newd wxStaticText(this, wxID_ANY, "Port:"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);

	port_spin = newd wxSpinCtrl(this, MCP_PORT_SPIN, wxEmptyString, wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 1024, 65535, g_settings.getInteger(Config::MCP_PORT));
	controlSizer->Add(port_spin, 0, wxALIGN_CENTER_VERTICAL | wxALL, 4);

	mainSizer->Add(controlSizer, 0, wxEXPAND);

	// Writes are opt-in: an LLM should not be able to edit the map just
	// because the user started the server to let it look around.
	write_checkbox = newd wxCheckBox(this, MCP_WRITE_CHECKBOX, "Allow write operations (map edits, undoable)");
	write_checkbox->SetToolTip("While unchecked the AI can only read the map. Edits still go through the undo history.");
	write_checkbox->SetValue(g_settings.getInteger(Config::MCP_ALLOW_WRITE) != 0);
	mainSizer->Add(write_checkbox, 0, wxLEFT | wxRIGHT | wxBOTTOM, 4);

	status_label = newd wxStaticText(this, wxID_ANY, "Stopped");
	mainSizer->Add(status_label, 0, wxLEFT | wxRIGHT | wxBOTTOM, 4);

	// --- connection details ----------------------------------------------
	wxStaticBoxSizer* connectionSizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Connect a client");

	endpoint_text = newd wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
	connectionSizer->Add(endpoint_text, 0, wxEXPAND | wxALL, 2);

	wxBoxSizer* copySizer = newd wxBoxSizer(wxHORIZONTAL);
	const wxString clients[] = { "Claude Code", "Claude Desktop", "Codex", "stdio bridge" };
	client_choice = newd wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 4, clients);
	client_choice->SetSelection(0);
	copySizer->Add(client_choice, 1, wxALIGN_CENTER_VERTICAL | wxALL, 2);

	copy_button = newd wxButton(this, MCP_COPY_CONFIG, "Copy config");
	copy_button->SetToolTip("Copy the configuration snippet for the selected client to the clipboard");
	copySizer->Add(copy_button, 0, wxALL, 2);

	connectionSizer->Add(copySizer, 0, wxEXPAND);
	mainSizer->Add(connectionSizer, 0, wxEXPAND | wxALL, 4);

	// --- log --------------------------------------------------------------
	wxBoxSizer* logHeaderSizer = newd wxBoxSizer(wxHORIZONTAL);
	logHeaderSizer->Add(newd wxStaticText(this, wxID_ANY, "Log:"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
	logHeaderSizer->AddStretchSpacer();
	logHeaderSizer->Add(newd wxButton(this, MCP_CLEAR_LOG, "Clear"), 0, wxALL, 2);
	mainSizer->Add(logHeaderSizer, 0, wxEXPAND);

	log_output = newd wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 140), wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxHSCROLL);
	log_output->SetFont(wxFont(9, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
	log_output->SetBackgroundColour(wxColour(30, 30, 30));
	log_output->SetForegroundColour(wxColour(200, 200, 200));
	mainSizer->Add(log_output, 1, wxEXPAND | wxALL, 2);

	SetSizer(mainSizer);
}

void McpWindow::UpdateStatus() {
	mcp::Server &server = mcp::Server::get();
	const bool running = server.isRunning();

	enable_checkbox->SetValue(running);
	port_spin->Enable(!running);

	if (running) {
		mcp::ToolRegistry::get().ensureRegistered();
		status_label->SetLabel(wxString::Format(
			"Listening - %zu tools available - %s",
			mcp::ToolRegistry::get().all().size(),
			wxString(server.isWriteAllowed() ? "writes allowed" : "read-only")
		));
		status_label->SetForegroundColour(wxColour(60, 150, 60));
		endpoint_text->SetValue(wxString::FromUTF8(server.getEndpointUrl()));
	} else if (!server.getLastError().empty()) {
		status_label->SetLabel("Error: " + wxString::FromUTF8(server.getLastError()));
		status_label->SetForegroundColour(wxColour(200, 70, 70));
		endpoint_text->SetValue(wxEmptyString);
	} else {
		status_label->SetLabel("Stopped");
		status_label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
		endpoint_text->SetValue(wxEmptyString);
	}

	copy_button->Enable(running);
	Layout();
}

void McpWindow::LogMessage(const wxString &message, bool isError) {
	if (!log_output) {
		return;
	}

	wxTextAttr attr;
	attr.SetTextColour(isError ? wxColour(255, 100, 100) : wxColour(200, 200, 200));
	log_output->SetDefaultStyle(attr);

	log_output->AppendText(wxDateTime::Now().Format("[%H:%M:%S] ") + message);
	if (!message.EndsWith("\n")) {
		log_output->AppendText("\n");
	}
	log_output->ShowPosition(log_output->GetLastPosition());
}

void McpWindow::OnToggleServer(wxCommandEvent &event) {
	mcp::Server &server = mcp::Server::get();

	if (event.IsChecked()) {
		const auto port = static_cast<uint16_t>(port_spin->GetValue());
		if (server.start(port)) {
			g_settings.setInteger(Config::MCP_ENABLED, 1);
			g_settings.setInteger(Config::MCP_PORT, port);
		} else {
			LogMessage(wxString::FromUTF8(server.getLastError()), true);
		}
	} else {
		server.stop();
		g_settings.setInteger(Config::MCP_ENABLED, 0);
	}

	UpdateStatus();
}

void McpWindow::OnPortChanged(wxSpinEvent &event) {
	g_settings.setInteger(Config::MCP_PORT, port_spin->GetValue());
}

void McpWindow::OnToggleWrite(wxCommandEvent &event) {
	const bool allowed = event.IsChecked();
	mcp::Server::get().setWriteAllowed(allowed);
	g_settings.setInteger(Config::MCP_ALLOW_WRITE, allowed ? 1 : 0);
	LogMessage(allowed ? "write operations enabled" : "write operations disabled");
	UpdateStatus();
}

void McpWindow::OnCopyConfig(wxCommandEvent &event) {
	const wxString url = endpoint_text->GetValue();
	if (url.IsEmpty()) {
		return;
	}

	const wxString snippet = BuildConfigSnippet(client_choice->GetSelection(), url);
	if (wxTheClipboard->Open()) {
		wxTheClipboard->SetData(newd wxTextDataObject(snippet));
		wxTheClipboard->Close();
		LogMessage("configuration copied to the clipboard");
	}
}

void McpWindow::OnClearLog(wxCommandEvent &event) {
	if (log_output) {
		log_output->Clear();
	}
}
