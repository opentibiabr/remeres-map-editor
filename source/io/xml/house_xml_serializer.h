#ifndef RME_HOUSE_XML_SERIALIZER_H_
#define RME_HOUSE_XML_SERIALIZER_H_

#include <wx/arrstr.h>

class Map;
namespace pugi {
	class xml_document;
}

class HouseXmlSerializer {
public:
	static bool load(Map &map, const FileName &dir, wxArrayString &warnings);
	static bool load(Map &map, pugi::xml_document &doc, wxArrayString &warnings);
	static bool save(Map &map, const FileName &dir);
	static bool save(Map &map, pugi::xml_document &doc);
};

#endif
