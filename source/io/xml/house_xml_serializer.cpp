#include "app/main.h"

#include "io/xml/house_xml_serializer.h"
#include "game/house.h"
#include "map/map.h"

bool HouseXmlSerializer::load(Map &map, const FileName &dir, wxArrayString &warnings) {
	std::string fn = (const char*)(dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME).mb_str(wxConvUTF8));
	fn += map.getHouseFilename();
	FileName filename(wxstr(fn));
	if (!filename.FileExists()) {
		return false;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(fn.c_str());
	if (!result) {
		return false;
	}
	return load(map, doc, warnings);
}

bool HouseXmlSerializer::load(Map &map, pugi::xml_document &doc, wxArrayString &warnings) {
	pugi::xml_node node = doc.child("houses");
	if (!node) {
		warnings.push_back("IOMapOTBM::loadHouses: Invalid rootheader.");
		return false;
	}

	pugi::xml_attribute attribute;
	for (pugi::xml_node houseNode = node.first_child(); houseNode; houseNode = houseNode.next_sibling()) {
		if (as_lower_str(houseNode.name()) != "house") {
			continue;
		}

		House* house = nullptr;
		const auto houseIdAttribute = houseNode.attribute("houseid");
		if (!houseIdAttribute) {
			warnings.push_back(fmt::format("IOMapOTBM::loadHouses: Could not load house, missing 'houseid' attribute."));
			continue;
		}

		house = map.houses.getHouse(houseIdAttribute.as_uint());

		if (!house) {
			warnings.push_back(fmt::format("IOMapOTBM::loadHouses: Could not load house #{}", houseIdAttribute.as_uint()));
			continue;
		}

		if (house != nullptr) {
			if ((attribute = houseNode.attribute("name"))) {
				house->name = attribute.as_string();
			} else {
				house->name = "House #" + std::to_string(house->id);
			}
		}

		Position exitPosition(
			houseNode.attribute("entryx").as_int(),
			houseNode.attribute("entryy").as_int(),
			houseNode.attribute("entryz").as_int()
		);
		if (exitPosition.x != 0 && exitPosition.y != 0 && exitPosition.z != 0) {
			house->setExit(exitPosition);
		}

		if ((attribute = houseNode.attribute("rent"))) {
			house->rent = attribute.as_int();
		}

		if ((attribute = houseNode.attribute("guildhall"))) {
			house->guildhall = attribute.as_bool();
		}

		if ((attribute = houseNode.attribute("townid"))) {
			house->townid = attribute.as_uint();
		} else {
			warnings.push_back(fmt::format("House {} has no town! House was removed.", house->id));
			map.houses.removeHouse(house);
		}

		if ((attribute = houseNode.attribute("clientid"))) {
			house->clientid = attribute.as_uint();
		}

		if ((attribute = houseNode.attribute("beds"))) {
			house->beds = attribute.as_uint();
		}
	}
	return true;
}

bool HouseXmlSerializer::save(Map &map, const FileName &dir) {
	wxString filepath = dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME);
	filepath += wxString(map.getHouseFilename().c_str(), wxConvUTF8);

	// Create the XML file
	pugi::xml_document doc;
	if (save(map, doc)) {
		return doc.save_file(filepath.wc_str(), "\t", pugi::format_default, pugi::encoding_utf8);
	}
	return false;
}

bool HouseXmlSerializer::save(Map &map, pugi::xml_document &doc) {
	pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
	if (!decl) {
		return false;
	}

	decl.append_attribute("version") = "1.0";

	pugi::xml_node houseNodes = doc.append_child("houses");
	for (const auto &houseEntry : map.houses) {
		const House* house = houseEntry.second;
		pugi::xml_node houseNode = houseNodes.append_child("house");

		houseNode.append_attribute("name") = house->name.c_str();
		houseNode.append_attribute("houseid") = house->id;

		const Position &exitPosition = house->getExit();
		houseNode.append_attribute("entryx") = exitPosition.x;
		houseNode.append_attribute("entryy") = exitPosition.y;
		houseNode.append_attribute("entryz") = exitPosition.z;

		houseNode.append_attribute("rent") = house->rent;
		if (house->guildhall) {
			houseNode.append_attribute("guildhall") = true;
		}

		houseNode.append_attribute("townid") = house->townid;
		houseNode.append_attribute("size") = static_cast<int32_t>(house->size());
		houseNode.append_attribute("clientid") = house->clientid;
		houseNode.append_attribute("beds") = house->beds;
	}
	return true;
}
