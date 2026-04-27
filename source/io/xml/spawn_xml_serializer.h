#ifndef RME_SPAWN_XML_SERIALIZER_H_
#define RME_SPAWN_XML_SERIALIZER_H_

#include <wx/arrstr.h>

class Map;
namespace pugi {
	class xml_document;
}

class SpawnXmlSerializer {
public:
	static bool loadMonsters(Map &map, const FileName &dir, wxArrayString &warnings);
	static bool loadMonsters(Map &map, pugi::xml_document &doc, wxArrayString &warnings);
	static bool loadNpcs(Map &map, const FileName &dir, wxArrayString &warnings);
	static bool loadNpcs(Map &map, pugi::xml_document &doc, wxArrayString &warnings);
	static bool saveMonsters(Map &map, const FileName &dir);
	static bool saveMonsters(Map &map, pugi::xml_document &doc);
	static bool saveNpcs(Map &map, const FileName &dir);
	static bool saveNpcs(Map &map, pugi::xml_document &doc);
};

#endif
