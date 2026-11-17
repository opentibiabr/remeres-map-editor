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

#include "creature_directories.h"

#include "lua_parser.h"
#include "settings.h"

namespace {
	// A map can be deep inside its datapack, like world/quest/<quest>/<map>.otbm
	constexpr int maxParentFolders = 8;

	wxString findCreatureFolder(const wxString &parent, const wxString &relativePath, std::initializer_list<const char*> filePatterns) {
		wxFileName folder = wxFileName::DirName(parent);
		for (const wxString &part : wxSplit(relativePath, '/')) {
			folder.AppendDir(part);
		}

		const wxString path = folder.GetPath();
		if (!wxDir::Exists(path)) {
			return wxEmptyString;
		}

		// Any folder can have this name, the creature files are what makes it the right one
		for (const char* pattern : filePatterns) {
			if (!wxDir::FindFirst(path, pattern).empty()) {
				return path;
			}
		}
		return wxEmptyString;
	}

	// Canary keeps Lua files in monster and npc, the forks of The Forgotten Server keep XML files
	// there, and the servers for 7.x keep npcs in .npc files. Some servers changed the folder names.
	void searchDatapack(const wxString &datapack, CreatureDirectories &found) {
		for (const char* name : { "monster", "monsters", "scripts/monsters" }) {
			if (found.monsters.empty()) {
				found.monsters = findCreatureFolder(datapack, name, { "*.lua", "*.xml" });
			}
		}
		for (const char* name : { "npc", "npcs" }) {
			if (found.npcs.empty()) {
				found.npcs = findCreatureFolder(datapack, name, { "*.lua", "*.xml", "*.npc" });
			}
		}
	}

	wxString readActiveDatapack(const wxString &serverFolder) {
		const wxFileName config(serverFolder, "config.lua");
		if (!config.FileExists()) {
			return wxEmptyString;
		}

		static constexpr std::string_view key = "dataPackDirectory";

		std::istringstream lines(LuaParser::readFileContent(config.GetFullPath().ToStdString()));
		std::string line;
		while (std::getline(lines, line)) {
			const size_t start = line.find_first_not_of(" \t");
			if (start == std::string::npos || line.compare(start, key.size(), key) != 0) {
				continue;
			}

			const size_t open = line.find('"', start);
			const size_t close = open == std::string::npos ? open : line.find('"', open + 1);
			if (close != std::string::npos) {
				return wxstr(line.substr(open + 1, close - open - 1));
			}
		}
		return wxEmptyString;
	}

	void searchServerFolder(const wxString &serverFolder, CreatureDirectories &found) {
		const auto searchChild = [&serverFolder, &found](const wxString &name) {
			wxFileName folder = wxFileName::DirName(serverFolder);
			folder.AppendDir(name);
			searchDatapack(folder.GetPath(), found);
		};

		// The active datapack comes first, a server folder can hold several of them
		const wxString activeDatapack = readActiveDatapack(serverFolder);
		if (!activeDatapack.empty()) {
			searchChild(activeDatapack);
		}
		searchChild("data");

		wxDir server(serverFolder);
		if (!server.IsOpened()) {
			return;
		}

		wxString name;
		for (bool next = server.GetFirst(&name, "data-*", wxDIR_DIRS); next; next = server.GetNext(&name)) {
			searchChild(name);
		}
	}

	void adoptDirectory(Config::Key setting, const wxString &directory) {
		const wxString current = wxstr(g_settings.getString(setting));
		if (!directory.empty() && (current.empty() || !wxDir::Exists(current))) {
			g_settings.setString(setting, nstr(directory));
		}
	}
}

CreatureDirectories findCreatureDirectories(const FileName &mapFile) {
	// A folder that can not be read is only a place without creatures
	wxLogNull noLog;

	CreatureDirectories found;
	wxFileName folder = wxFileName::DirName(mapFile.GetPath());
	folder.MakeAbsolute();

	for (int depth = 0; depth <= maxParentFolders; ++depth) {
		const wxString path = folder.GetPath();
		searchDatapack(path, found);
		searchServerFolder(path, found);

		if ((!found.monsters.empty() && !found.npcs.empty()) || folder.GetDirCount() == 0) {
			break;
		}
		folder.RemoveLastDir();
	}
	return found;
}

void adoptCreatureDirectories(const CreatureDirectories &directories) {
	adoptDirectory(Config::MONSTERS_LUA_DIRECTORY, directories.monsters);
	adoptDirectory(Config::NPCS_LUA_DIRECTORY, directories.npcs);
}
