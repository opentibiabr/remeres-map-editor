//////////////////////////////////////////////////////////////////////
// This file is part of Canary Map Editor
//////////////////////////////////////////////////////////////////////
// Canary Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Canary Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#ifndef RME_CLIENT_VERSION_H_
#define RME_CLIENT_VERSION_H_

#include "settings.h"

// OTBM versions
enum MapVersionID {
	MAP_OTBM_UNKNOWN = -1,
	MAP_OTBM_1 = 0,
	MAP_OTBM_2 = 1,
	MAP_OTBM_3 = 2,
	MAP_OTBM_4 = 3,
	MAP_OTBM_5 = 4,
	MAP_OTBM_6 = 5,

	MAP_OTBM_LAST_VERSION = MAP_OTBM_6
};

// The composed version of a otbm file (otbm version)
struct MapVersion {
	MapVersion() :
		otbm(MAP_OTBM_5) { }
	MapVersionID otbm;
};

class ClientAssets {
public:
	ClientAssets() = default;
	~ClientAssets() = default;

	// Ensures we don't accidentally copy it.
	ClientAssets(const ClientAssets &) = delete;
	ClientAssets &operator=(const ClientAssets &) = delete;

	static void load();
	// Load protobuf appearance file and catalog-content json with sprites
	static bool loadAppearanceProtobuf(wxString &error, wxArrayString &warnings);
	static void save();

	static std::string getVersionName();

	static FileName getDataPath();
	static FileName getLocalPath();
	static wxString getPath();
	static void setPath(const wxString &newPath) {
		assets_path = newPath;
	}
	static void setLoaded(bool newLoaded) {
		loaded = newLoaded;
	}

	static bool isLoaded() {
		return loaded;
	}

private:
	static std::string version_name;

	static wxString assets_path;
	static wxString data_path;
	static bool loaded;

	static void loadVersion(pugi::xml_node client_node);
};

#endif
