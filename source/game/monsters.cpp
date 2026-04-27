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

#include "app/main.h"

#include "game/materials.h"
#include "brushes/brush.h"
#include "game/monsters.h"
#include "brushes/monster_brush.h"

#include <wx/dir.h>

#include "util/lua_parser.h"

MonsterDatabase g_monsters;

MonsterType::MonsterType() :
	missing(false),
	in_other_tileset(false),
	standard(false),
	name(""),
	folder(""),
	brush(nullptr) {
	////
}

MonsterType::MonsterType(const MonsterType &ct) :
	missing(ct.missing),
	in_other_tileset(ct.in_other_tileset),
	standard(ct.standard),
	name(ct.name),
	folder(ct.folder),
	outfit(ct.outfit),
	brush(ct.brush) {
	////
}

MonsterType &MonsterType::operator=(const MonsterType &ct) {
	missing = ct.missing;
	in_other_tileset = ct.in_other_tileset;
	standard = ct.standard;
	name = ct.name;
	folder = ct.folder;
	outfit = ct.outfit;
	brush = ct.brush;
	return *this;
}

MonsterType::~MonsterType() {
	////
}

MonsterType* MonsterType::loadFromOTXML(const FileName &filename, pugi::xml_document &doc, wxArrayString &warnings) {
	ASSERT(doc != nullptr);
	pugi::xml_node node;
	if (!(node = doc.child("monster"))) {
		warnings.push_back("This file is not a monster file");
		return nullptr;
	}

	pugi::xml_attribute attribute;
	if (!(attribute = node.attribute("name"))) {
		warnings.push_back("Couldn't read name tag of monster node.");
		return nullptr;
	}

	MonsterType* ct = newd MonsterType();
	ct->name = attribute.as_string();
	ct->outfit.name = ct->name;

	for (pugi::xml_node optionNode = node.first_child(); optionNode; optionNode = optionNode.next_sibling()) {
		if (as_lower_str(optionNode.name()) != "look") {
			continue;
		}

		if ((attribute = optionNode.attribute("type"))) {
			ct->outfit.lookType = attribute.as_int();
		}

		if ((attribute = optionNode.attribute("item")) || (attribute = optionNode.attribute("lookex")) || (attribute = optionNode.attribute("typeex"))) {
			ct->outfit.lookItem = attribute.as_int();
		}

		if ((attribute = optionNode.attribute("mount"))) {
			ct->outfit.lookMount = attribute.as_int();
		}

		if ((attribute = optionNode.attribute("addon"))) {
			ct->outfit.lookAddon = attribute.as_int();
		}

		if ((attribute = optionNode.attribute("head"))) {
			ct->outfit.lookHead = attribute.as_int();
		}

		if ((attribute = optionNode.attribute("body"))) {
			ct->outfit.lookBody = attribute.as_int();
		}

		if ((attribute = optionNode.attribute("legs"))) {
			ct->outfit.lookLegs = attribute.as_int();
		}

		if ((attribute = optionNode.attribute("feet"))) {
			ct->outfit.lookFeet = attribute.as_int();
		}
	}
	return ct;
}

MonsterDatabase::MonsterDatabase() {
	////
}

MonsterDatabase::~MonsterDatabase() {
	clear();
}

void MonsterDatabase::clear() {
	for (MonsterMap::iterator iter = monster_map.begin(); iter != monster_map.end(); ++iter) {
		delete iter->second;
	}
	monster_map.clear();
}

MonsterType* MonsterDatabase::operator[](const std::string &name) {
	if (name.empty()) {
		return nullptr;
	}

	MonsterMap::iterator iter = monster_map.find(as_lower_str(name));
	if (iter != monster_map.end()) {
		return iter->second;
	}
	return nullptr;
}

MonsterType* MonsterDatabase::addMissingMonsterType(const std::string &name) {
	assert((*this)[name] == nullptr);

	MonsterType* ct = newd MonsterType();
	ct->name = name;
	ct->missing = true;
	ct->outfit.lookType = 130;

	monster_map.insert(std::make_pair(as_lower_str(name), ct));
	return ct;
}

MonsterType* MonsterDatabase::addMonsterType(const std::string &name, const Outfit &outfit) {
	assert((*this)[name] == nullptr);

	MonsterType* ct = newd MonsterType();
	ct->name = name;
	ct->missing = false;
	ct->outfit = outfit;

	monster_map.insert(std::make_pair(as_lower_str(name), ct));
	return ct;
}

bool MonsterDatabase::hasMissing() const {
	for (MonsterMap::const_iterator iter = monster_map.begin(); iter != monster_map.end(); ++iter) {
		if (iter->second->missing) {
			return true;
		}
	}
	return false;
}

bool MonsterDatabase::importXMLFromOT(const FileName &filename, wxString &error, wxArrayString &warnings) {
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(filename.GetFullPath().mb_str());
	if (!result) {
		error = "Couldn't open file \"" + filename.GetFullName() + "\", invalid format?";
		return false;
	}

	pugi::xml_node node;
	if ((node = doc.child("monsters"))) {
		for (pugi::xml_node monsterNode = node.first_child(); monsterNode; monsterNode = monsterNode.next_sibling()) {
			if (as_lower_str(monsterNode.name()) != "monster") {
				continue;
			}

			pugi::xml_attribute attribute;
			if (!(attribute = monsterNode.attribute("file"))) {
				continue;
			}

			FileName monsterFile(filename);
			monsterFile.SetFullName(wxString(attribute.as_string(), wxConvUTF8));

			pugi::xml_document monsterDoc;
			pugi::xml_parse_result monsterResult = monsterDoc.load_file(monsterFile.GetFullPath().mb_str());
			if (!monsterResult) {
				continue;
			}

			MonsterType* monsterType = MonsterType::loadFromOTXML(monsterFile, monsterDoc, warnings);
			if (monsterType) {
				MonsterType* current = (*this)[monsterType->name];
				if (current) {
					*current = *monsterType;
					delete monsterType;
				} else {
					monster_map[as_lower_str(monsterType->name)] = monsterType;

					Tileset* tileSet = nullptr;
					tileSet = g_materials.tilesets["Monsters"];
					ASSERT(tileSet != nullptr);

					Brush* brush = newd MonsterBrush(monsterType);
					g_brushes.addBrush(brush);

					TilesetCategory* tileSetCategory = tileSet->getCategory(TILESET_MONSTER);
					tileSetCategory->brushlist.push_back(brush);
				}
			}
		}
	} else if ((node = doc.child("monster"))) {
		MonsterType* monsterType = MonsterType::loadFromOTXML(filename, doc, warnings);
		if (monsterType) {
			MonsterType* current = (*this)[monsterType->name];

			if (current) {
				*current = *monsterType;
				delete monsterType;
			} else {
				monster_map[as_lower_str(monsterType->name)] = monsterType;

				Tileset* tileSet = nullptr;
				tileSet = g_materials.tilesets["Monsters"];
				ASSERT(tileSet != nullptr);

				Brush* brush = newd MonsterBrush(monsterType);
				g_brushes.addBrush(brush);

				TilesetCategory* tileSetCategory = tileSet->getCategory(TILESET_MONSTER);
				tileSetCategory->brushlist.push_back(brush);
			}
		}
	} else {
		error = "This is not valid OT monster data file.";
		return false;
	}
	return true;
}

bool MonsterDatabase::saveToXML(const FileName &filename) {
	pugi::xml_document doc;

	pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
	decl.append_attribute("version") = "1.0";

	pugi::xml_node monsterNodes = doc.append_child("monsters");
	for (const auto &monsterEntry : monster_map) {
		MonsterType* monsterType = monsterEntry.second;
		if (!monsterType->standard) {
			pugi::xml_node monsterNode = monsterNodes.append_child("monster");

			monsterNode.append_attribute("name") = monsterType->name.c_str();
			monsterNode.append_attribute("type") = "monster";

			const Outfit &outfit = monsterType->outfit;
			monsterNode.append_attribute("looktype") = outfit.lookType;
			monsterNode.append_attribute("lookitem") = outfit.lookItem;
			monsterNode.append_attribute("lookaddon") = outfit.lookAddon;
			monsterNode.append_attribute("lookhead") = outfit.lookHead;
			monsterNode.append_attribute("lookbody") = outfit.lookBody;
			monsterNode.append_attribute("looklegs") = outfit.lookLegs;
			monsterNode.append_attribute("lookfeet") = outfit.lookFeet;
		}
	}
	return doc.save_file(filename.GetFullPath().mb_str(), "\t", pugi::format_default, pugi::encoding_utf8);
}

wxArrayString MonsterDatabase::getMissingMonsterNames() const {
	wxArrayString missingMonsters;
	for (const auto &monsterEntry : monster_map) {
		if (monsterEntry.second->missing) {
			missingMonsters.Add(monsterEntry.second->name);
		}
	}
	return missingMonsters;
}

static std::string extractTopFolder(const wxString &filePath, const wxString &directory) {
	wxFileName fn(filePath);
	wxString relPath = fn.GetPath().Lower();
	wxString dirLower = directory.Lower();
	if (!relPath.StartsWith(dirLower)) {
		return "";
	}
	relPath = relPath.Mid(dirLower.Length());
	if (!relPath.IsEmpty() && (relPath[0] == '/' || relPath[0] == '\\')) {
		relPath = relPath.Mid(1);
	}
	const int slash = relPath.Find('/');
	const int backslash = relPath.Find('\\');
	int sep = wxNOT_FOUND;
	if (slash != wxNOT_FOUND && backslash != wxNOT_FOUND) {
		sep = std::min(slash, backslash);
	} else if (slash != wxNOT_FOUND) {
		sep = slash;
	} else if (backslash != wxNOT_FOUND) {
		sep = backslash;
	}
	wxString fld = (sep == wxNOT_FOUND) ? relPath : relPath.Left(sep);
	return fld.ToStdString();
}

bool MonsterDatabase::loadFromLuaDir(const wxString &directory, wxString &error, wxArrayString &warnings) {
	if (directory.IsEmpty()) {
		return true;
	}
	if (!wxDir::Exists(directory)) {
		error = "Monsters Lua directory does not exist: " + directory;
		return false;
	}

	wxArrayString luaFiles;
	wxDir::GetAllFiles(directory, &luaFiles, "*.lua", wxDIR_FILES | wxDIR_DIRS | wxDIR_HIDDEN);

	int fileCount = 0;
	for (const auto &filePath : luaFiles) {
		if (++fileCount % 50 == 0) {
			wxSafeYield();
		}
		std::string content = LuaParser::readFileContent(filePath.ToStdString());
		if (content.empty()) {
			warnings.push_back("Could not open: " + filePath);
			continue;
		}

		std::string name = LuaParser::parseCreateCall(content, "Game.createMonsterType");
		if (name.empty()) {
			name = LuaParser::parseLocalString(content, "internalMonsterName");
		}
		if (name.empty()) {
			continue;
		}

		MonsterType* existing = (*this)[name];
		if (existing) {
			if (existing->folder.empty()) {
				existing->folder = extractTopFolder(filePath, directory);
			}
			if (!existing->missing) {
				continue;
			}
			Outfit parsed;
			parsed.name = existing->name;
			if (LuaParser::parseOutfit(content, parsed)) {
				existing->outfit = parsed;
				existing->missing = false;
			}
			continue;
		}

		MonsterType* ct = newd MonsterType();
		ct->name = name;
		ct->outfit.name = name;
		ct->standard = false;

		ct->folder = extractTopFolder(filePath, directory);

		if (!LuaParser::parseOutfit(content, ct->outfit)) {
			delete ct;
			continue;
		}

		monster_map[as_lower_str(ct->name)] = ct;
	}
	return true;
}
