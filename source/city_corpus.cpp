//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "city_corpus.h"

#include "brush.h"
#include "complexitem.h"
#include "editor.h"
#include "ground_brush.h"
#include "gui.h"
#include "house.h"
#include "map.h"
#include "selection.h"
#include "tile.h"
#include "town.h"
#include "wall_brush.h"

namespace {
using json = nlohmann::json;

struct Bounds {
	int minX;
	int maxX;
	int minY;
	int maxY;

	int width() const noexcept {
		return maxX - minX + 1;
	}

	int height() const noexcept {
		return maxY - minY + 1;
	}

	bool includes(const Position &position) const noexcept {
		return position.x >= minX && position.x <= maxX &&
			position.y >= minY && position.y <= maxY;
	}
};

struct SampleStatistics {
	uint64_t tileCount = 0;
	uint64_t itemCount = 0;
	uint64_t houseTileCount = 0;
	std::map<int, uint64_t> tilesByFloor;
	std::map<std::string, uint64_t> itemTags;
	std::map<uint16_t, uint64_t> itemIds;
	std::map<uint32_t, uint64_t> includedHouseTiles;
};

struct HouseExtent {
	const House* house = nullptr;
	Bounds bounds = { 0, 0, 0, 0 };
};

struct District {
	Bounds bounds = { 0, 0, 0, 0 };
	std::vector<const House*> anchorHouses;
	std::string role;
	std::string inferenceMethod;
	std::string confidence;
	int templeDistance = 0;
};

struct InferredCity {
	const Town* town = nullptr;
	std::vector<District> districts;
};

struct CorpusStatistics {
	uint64_t tileCount = 0;
	uint64_t itemCount = 0;
	uint64_t houseTileCount = 0;
	uint64_t districtCount = 0;
};

constexpr uint64_t LargeProjectionThreshold = 5000000;
constexpr int DistrictClusterGap = 24;
constexpr int DistrictContextMargin = 16;
constexpr int TempleFallbackRadius = 96;

json boundsToJson(const Bounds &bounds) {
	return {
		{ "minX", bounds.minX },
		{ "maxX", bounds.maxX },
		{ "minY", bounds.minY },
		{ "maxY", bounds.maxY },
	};
}

void includePosition(Bounds &bounds, const Position &position) {
	bounds.minX = std::min(bounds.minX, position.x);
	bounds.maxX = std::max(bounds.maxX, position.x);
	bounds.minY = std::min(bounds.minY, position.y);
	bounds.maxY = std::max(bounds.maxY, position.y);
}

Bounds expandBounds(const Bounds &bounds, int amount) {
	return {
		std::max(0, bounds.minX - amount),
		std::min(rme::MapMaxWidth, bounds.maxX + amount),
		std::max(0, bounds.minY - amount),
		std::min(rme::MapMaxHeight, bounds.maxY + amount),
	};
}

int intervalDistance(int firstMinimum, int firstMaximum, int secondMinimum, int secondMaximum) {
	if (firstMaximum < secondMinimum) {
		return secondMinimum - firstMaximum;
	}
	if (secondMaximum < firstMinimum) {
		return firstMinimum - secondMaximum;
	}
	return 0;
}

int boundsDistance(const Bounds &first, const Bounds &second) {
	return std::max(
		intervalDistance(first.minX, first.maxX, second.minX, second.maxX),
		intervalDistance(first.minY, first.maxY, second.minY, second.maxY)
	);
}

int positionDistance(const Position &position, const Bounds &bounds) {
	return std::max(
		intervalDistance(position.x, position.x, bounds.minX, bounds.maxX),
		intervalDistance(position.y, position.y, bounds.minY, bounds.maxY)
	);
}

void addTag(std::vector<std::string> &tags, const std::string &tag) {
	if (std::find(tags.begin(), tags.end(), tag) == tags.end()) {
		tags.push_back(tag);
	}
}

bool containsAny(const std::string &value, std::initializer_list<const char*> terms) {
	for (const char* term : terms) {
		if (value.find(term) != std::string::npos) {
			return true;
		}
	}
	return false;
}

std::string brushNameFor(const Item &item) {
	if (GroundBrush* groundBrush = item.getGroundBrush()) {
		return groundBrush->getName();
	}
	if (WallBrush* wallBrush = item.getWallBrush()) {
		return wallBrush->getName();
	}
	if (Brush* brush = item.getBrush()) {
		return brush->getName();
	}
	if (Brush* brush = item.getDoodadBrush()) {
		return brush->getName();
	}
	return "";
}

std::vector<std::string> classifyItem(const Item &item, bool ground) {
	const ItemType &type = item.getItemType();
	const std::string name = as_lower_str(item.getFullName());
	const std::string brushName = as_lower_str(brushNameFor(item));
	const std::string searchable = name + " " + brushName;
	std::vector<std::string> tags;
	addTag(tags, "item");

	if (ground || type.isGroundTile()) {
		addTag(tags, "ground");
		if (containsAny(searchable, { "grass", "lawn" })) {
			addTag(tags, "grass");
		} else if (containsAny(searchable, { "road", "street", "pavement", "cobble", "sidewalk", "path" })) {
			addTag(tags, "road");
		} else if (containsAny(searchable, { "water", "sea", "ocean", "river", "swamp" })) {
			addTag(tags, "water");
		} else if (containsAny(searchable, { "floor", "tile", "wood", "marble" })) {
			addTag(tags, "floor");
		} else {
			addTag(tags, "terrain");
		}
	}

	if (item.isBorder() || item.isOptionalBorder()) {
		addTag(tags, "border");
	}
	if (item.isWall()) {
		addTag(tags, "wall");
	}
	if (item.isDoor() || type.isDoor()) {
		addTag(tags, "door");
	}
	if (type.isDepot()) {
		addTag(tags, "depot");
	}
	if (type.isTeleport()) {
		addTag(tags, "teleport");
	}
	if (type.isFloorChange() || containsAny(searchable, { "stairs", "stairway", "ladder", "ramp", "rope spot" })) {
		addTag(tags, "vertical_connector");
	}
	if (containsAny(searchable, { "window" })) {
		addTag(tags, "window");
	}
	if (containsAny(searchable, { "fence" })) {
		addTag(tags, "fence");
	}
	if (containsAny(searchable, { "railing" })) {
		addTag(tags, "railing");
	}
	if (containsAny(searchable, { "roof" })) {
		addTag(tags, "roof");
	}
	if (item.getDoodadBrush()) {
		addTag(tags, "doodad");
	}

	if (tags.size() == 1) {
		addTag(tags, "unclassified");
	}
	return tags;
}

json positionToJson(const Position &position) {
	return {
		{ "x", position.x },
		{ "y", position.y },
		{ "z", position.z },
	};
}

json itemToJson(const Item &item, bool ground, SampleStatistics &statistics) {
	const std::vector<std::string> tags = classifyItem(item, ground);
	json output = {
		{ "id", item.getID() },
		{ "name", item.getFullName() },
		{ "tags", tags },
	};

	const std::string brushName = brushNameFor(item);
	if (!brushName.empty()) {
		output["brush"] = brushName;
	}
	if (const Door* door = dynamic_cast<const Door*>(&item)) {
		output["doorId"] = door->getDoorID();
	}
	if (const Depot* depot = dynamic_cast<const Depot*>(&item)) {
		output["depotId"] = depot->getDepotID();
	}
	if (const Teleport* teleport = dynamic_cast<const Teleport*>(&item); teleport && teleport->hasDestination()) {
		output["destination"] = positionToJson(teleport->getDestination());
	}

	++statistics.itemCount;
	++statistics.itemIds[item.getID()];
	for (const std::string &tag : tags) {
		++statistics.itemTags[tag];
	}
	return output;
}

json counterToJson(const std::map<int, uint64_t> &counter) {
	json output = json::object();
	for (const auto &[key, count] : counter) {
		output[std::to_string(key)] = count;
	}
	return output;
}

json counterToJson(const std::map<uint16_t, uint64_t> &counter) {
	json output = json::object();
	for (const auto &[key, count] : counter) {
		output[std::to_string(key)] = count;
	}
	return output;
}

json counterToJson(const std::map<std::string, uint64_t> &counter) {
	json output = json::object();
	for (const auto &[key, count] : counter) {
		output[key] = count;
	}
	return output;
}

bool includesContent(const Tile* tile) {
	return tile && (!tile->empty() || tile->isHouseTile() || tile->getMapFlags() != TILESTATE_NONE || !tile->zones.empty());
}

json tileToJson(const Tile &tile, SampleStatistics &statistics) {
	json output = {
		{ "position", positionToJson(tile.getPosition()) },
		{ "mapFlags", tile.getMapFlags() },
		{ "items", json::array() },
	};
	std::vector<std::string> tileTags;

	if (tile.ground) {
		output["ground"] = itemToJson(*tile.ground, true, statistics);
		for (const auto &tag : output["ground"]["tags"]) {
			addTag(tileTags, tag.get<std::string>());
		}
	}
	for (const Item* item : tile.items) {
		if (!item) {
			continue;
		}
		json serializedItem = itemToJson(*item, false, statistics);
		for (const auto &tag : serializedItem["tags"]) {
			addTag(tileTags, tag.get<std::string>());
		}
		output["items"].push_back(std::move(serializedItem));
	}
	if (tile.isHouseTile()) {
		output["houseId"] = tile.getHouseID();
		addTag(tileTags, "house_tile");
		++statistics.houseTileCount;
		++statistics.includedHouseTiles[tile.getHouseID()];
	}
	if (tile.isPZ()) {
		addTag(tileTags, "protection_zone");
	}
	if (!tile.zones.empty()) {
		output["zones"] = tile.zones;
		addTag(tileTags, "zone");
	}
	output["tags"] = tileTags;
	return output;
}

json serializeHouse(const House &house, uint64_t selectedTileCount) {
	return {
		{ "id", house.id },
		{ "name", house.name },
		{ "townId", house.townid },
		{ "guildhall", house.guildhall },
		{ "rent", house.rent },
		{ "beds", house.beds },
		{ "exit", positionToJson(house.getExit()) },
		{ "fullTileCount", house.size() },
		{ "sampleTileCount", selectedTileCount },
	};
}

json serializeTown(const Town &town) {
	return {
		{ "id", town.getID() },
		{ "name", town.getName() },
		{ "templePosition", positionToJson(town.getTemplePosition()) },
	};
}

json serializeTiles(const Map &map, const Bounds &bounds, SampleStatistics &statistics) {
	json tiles = json::array();
	for (int z = rme::MapMinLayer; z <= rme::MapMaxLayer; ++z) {
		for (int y = bounds.minY; y <= bounds.maxY; ++y) {
			for (int x = bounds.minX; x <= bounds.maxX; ++x) {
				const Tile* tile = map.getTile(x, y, z);
				if (!includesContent(tile)) {
					continue;
				}
				tiles.push_back(tileToJson(*tile, statistics));
				++statistics.tileCount;
				++statistics.tilesByFloor[z];
			}
		}
	}
	return tiles;
}

json summaryToJson(const SampleStatistics &statistics) {
	return {
		{ "tileCount", statistics.tileCount },
		{ "itemCount", statistics.itemCount },
		{ "houseTileCount", statistics.houseTileCount },
		{ "houseCount", statistics.includedHouseTiles.size() },
		{ "tilesByFloor", counterToJson(statistics.tilesByFloor) },
		{ "itemTags", counterToJson(statistics.itemTags) },
		{ "itemIds", counterToJson(statistics.itemIds) },
	};
}

bool findHouseExtent(const House &house, HouseExtent &extent) {
	bool hasPosition = false;
	for (const Position &position : house.getTiles()) {
		if (!hasPosition) {
			extent = { &house, { position.x, position.x, position.y, position.y } };
			hasPosition = true;
		} else {
			includePosition(extent.bounds, position);
		}
	}
	if (house.getExit().isValid()) {
		if (!hasPosition) {
			extent = { &house, { house.getExit().x, house.getExit().x, house.getExit().y, house.getExit().y } };
		} else {
			includePosition(extent.bounds, house.getExit());
		}
		hasPosition = true;
	}
	return hasPosition;
}

std::vector<HouseExtent> collectHouseExtents(const Map &map, uint32_t townId) {
	std::vector<HouseExtent> extents;
	for (const auto &[houseId, house] : map.houses) {
		if (!house || house->townid != townId) {
			continue;
		}
		HouseExtent extent;
		if (findHouseExtent(*house, extent)) {
			extents.push_back(extent);
		}
	}
	return extents;
}

std::vector<District> inferDistricts(const Map &map, const Town &town) {
	const Position &temple = town.getTemplePosition();
	std::vector<HouseExtent> extents = collectHouseExtents(map, town.getID());
	if (extents.empty()) {
		if (!temple.isValid()) {
			return {};
		}
		return { {
			expandBounds({ temple.x, temple.x, temple.y, temple.y }, TempleFallbackRadius),
			{},
			"main",
			"temple_fallback",
			"low",
			0,
		} };
	}

	std::vector<District> districts;
	std::vector<bool> included(extents.size(), false);
	for (size_t start = 0; start < extents.size(); ++start) {
		if (included[start]) {
			continue;
		}
		District district;
		district.bounds = extents[start].bounds;
		std::vector<size_t> pending = { start };
		included[start] = true;
		for (size_t next = 0; next < pending.size(); ++next) {
			const HouseExtent &current = extents[pending[next]];
			district.anchorHouses.push_back(current.house);
			includePosition(district.bounds, Position(current.bounds.minX, current.bounds.minY, rme::MapGroundLayer));
			includePosition(district.bounds, Position(current.bounds.maxX, current.bounds.maxY, rme::MapGroundLayer));
			for (size_t candidate = 0; candidate < extents.size(); ++candidate) {
				if (!included[candidate] && boundsDistance(current.bounds, extents[candidate].bounds) <= DistrictClusterGap) {
					included[candidate] = true;
					pending.push_back(candidate);
				}
			}
		}
		district.templeDistance = temple.isValid() ? positionDistance(temple, district.bounds) : 0;
		districts.push_back(std::move(district));
	}

	size_t mainDistrict = 0;
	for (size_t index = 1; index < districts.size(); ++index) {
		if ((temple.isValid() && districts[index].templeDistance < districts[mainDistrict].templeDistance) ||
			(!temple.isValid() && districts[index].anchorHouses.size() > districts[mainDistrict].anchorHouses.size())) {
			mainDistrict = index;
		}
	}
	for (size_t index = 0; index < districts.size(); ++index) {
		District &district = districts[index];
		district.role = index == mainDistrict ? "main" : "satellite";
		district.inferenceMethod = temple.isValid() ?
			"house_clusters_with_temple_anchor" :
			"house_clusters_without_temple";
		district.confidence = district.anchorHouses.size() > 1 ? "high" : "medium";
		district.bounds = expandBounds(district.bounds, DistrictContextMargin);
	}
	return districts;
}

std::vector<InferredCity> inferCities(const Map &map) {
	std::vector<InferredCity> cities;
	for (const auto &[townId, town] : map.towns) {
		if (town) {
			cities.push_back({ town, inferDistricts(map, *town) });
		}
	}
	return cities;
}

json buildDistrict(const Map &map, const District &district, size_t districtIndex, SampleStatistics &statistics) {
	json houses = json::array();
	std::set<uint32_t> anchorHouseIds;
	const json tiles = serializeTiles(map, district.bounds, statistics);
	for (const House* house : district.anchorHouses) {
		if (!house) {
			continue;
		}
		json serialized = serializeHouse(*house, statistics.includedHouseTiles[house->id]);
		serialized["anchor"] = true;
		houses.push_back(std::move(serialized));
		anchorHouseIds.insert(house->id);
	}
	for (const auto &[houseId, count] : statistics.includedHouseTiles) {
		if (anchorHouseIds.contains(houseId)) {
			continue;
		}
		const House* house = map.houses.getHouse(houseId);
		if (house) {
			json serialized = serializeHouse(*house, count);
			serialized["anchor"] = false;
			houses.push_back(std::move(serialized));
		} else {
			houses.push_back({
				{ "id", houseId },
				{ "sampleTileCount", count },
				{ "anchor", false },
				{ "missingMetadata", true },
			});
		}
	}

	return {
		{ "index", districtIndex },
		{ "role", district.role },
		{ "inferenceMethod", district.inferenceMethod },
		{ "confidence", district.confidence },
		{ "templeDistance", district.templeDistance },
		{ "bounds", boundsToJson(district.bounds) },
		{ "houses", houses },
		{ "tiles", tiles },
		{ "summary", summaryToJson(statistics) },
	};
}

json buildSample(const Map &map, const Bounds &bounds, const std::string &sampleName) {
	SampleStatistics statistics;
	const json tiles = serializeTiles(map, bounds, statistics);

	json houses = json::array();
	std::set<uint32_t> includedTownIds;
	for (const auto &[houseId, count] : statistics.includedHouseTiles) {
		const House* house = map.houses.getHouse(houseId);
		if (house) {
			houses.push_back(serializeHouse(*house, count));
			includedTownIds.insert(house->townid);
		} else {
			houses.push_back({
				{ "id", houseId },
				{ "sampleTileCount", count },
				{ "missingMetadata", true },
			});
		}
	}

	json towns = json::array();
	for (const auto &[townId, town] : map.towns) {
		if (town && (includedTownIds.contains(townId) || bounds.includes(town->getTemplePosition()))) {
			towns.push_back(serializeTown(*town));
		}
	}

	json floors = json::array();
	for (const auto &[floor, count] : statistics.tilesByFloor) {
		if (count > 0) {
			floors.push_back(floor);
		}
	}

	return {
		{ "format", "rme-city-corpus" },
		{ "version", 1 },
		{ "sample", {
			{ "name", sampleName },
			{ "sourceMap", map.getFilename() },
			{ "mapVersion", static_cast<int>(map.getVersion().otbm) },
			{ "bounds", boundsToJson(bounds) },
			{ "floors", floors },
		} },
		{ "towns", towns },
		{ "houses", houses },
		{ "tiles", tiles },
		{ "structures", json::array() },
		{ "summary", summaryToJson(statistics) },
	};
}

Bounds selectionBounds(const Selection &selection) {
	const Position minimum = selection.minPosition();
	const Position maximum = selection.maxPosition();
	return { minimum.x, maximum.x, minimum.y, maximum.y };
}

uint64_t projectedPositions(const Bounds &bounds) {
	return static_cast<uint64_t>(bounds.width()) *
		static_cast<uint64_t>(bounds.height()) * rme::MapLayers;
}

bool confirmLargeProjection(wxWindow* parent, uint64_t positions, const wxString &title) {
	if (positions <= LargeProjectionThreshold) {
		return true;
	}
	return g_gui.PopupDialog(
		parent,
		title,
		wxString::Format("This projection inspects %llu tile positions across all floors. Continue exporting?", static_cast<unsigned long long>(positions)),
		wxYES | wxNO | wxICON_WARNING
	) == wxID_YES;
}

bool writeSample(wxWindow* parent, const Map &map, const Bounds &bounds, const std::string &sampleName) {
	if (!confirmLargeProjection(parent, projectedPositions(bounds), "Export City Learning Sample")) {
		return false;
	}

	wxFileDialog dialog(
		parent,
		"Save City Learning Sample",
		wxEmptyString,
		wxstr(sampleName + ".json"),
		"JSON files (*.json)|*.json",
		wxFD_SAVE | wxFD_OVERWRITE_PROMPT
	);
	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}

	const json sample = buildSample(map, bounds, sampleName);
	std::ofstream output(nstr(dialog.GetPath()), std::ios::out | std::ios::trunc);
	if (!output) {
		g_gui.PopupDialog(parent, "Export City Learning Sample", "Could not open the selected JSON file for writing.", wxOK | wxICON_ERROR);
		return false;
	}
	output << sample.dump(2) << '\n';
	output.flush();
	if (!output.good()) {
		g_gui.PopupDialog(parent, "Export City Learning Sample", "Could not finish writing the JSON sample.", wxOK | wxICON_ERROR);
		return false;
	}

	g_gui.PopupDialog(
		parent,
		"Export City Learning Sample",
		wxString::Format(
			"Exported %llu tiles to '%s'.",
			static_cast<unsigned long long>(sample["summary"]["tileCount"].get<uint64_t>()),
			dialog.GetPath().c_str()
		),
		wxOK | wxICON_INFORMATION
	);
	return true;
}

bool writeAllTownCorpus(wxWindow* parent, const Map &map) {
	const std::vector<InferredCity> cities = inferCities(map);
	if (cities.empty()) {
		g_gui.PopupDialog(parent, "Export All Town Learning Corpus", "The open map has no towns to export.", wxOK | wxICON_INFORMATION);
		return false;
	}

	uint64_t positions = 0;
	for (const InferredCity &city : cities) {
		for (const District &district : city.districts) {
			positions += projectedPositions(district.bounds);
		}
	}
	if (!confirmLargeProjection(parent, positions, "Export All Town Learning Corpus")) {
		return false;
	}

	wxFileDialog dialog(
		parent,
		"Save All Town Learning Corpus",
		wxEmptyString,
		"all-towns-city-corpus.json",
		"JSON files (*.json)|*.json",
		wxFD_SAVE | wxFD_OVERWRITE_PROMPT
	);
	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}

	std::ofstream output(nstr(dialog.GetPath()), std::ios::out | std::ios::trunc);
	if (!output) {
		g_gui.PopupDialog(parent, "Export All Town Learning Corpus", "Could not open the selected JSON file for writing.", wxOK | wxICON_ERROR);
		return false;
	}

	CorpusStatistics totals;
	output << "{\n"
		<< "  \"format\": \"rme-city-corpus\",\n"
		<< "  \"version\": 2,\n"
		<< "  \"sourceMap\": " << json(map.getFilename()).dump() << ",\n"
		<< "  \"mapVersion\": " << static_cast<int>(map.getVersion().otbm) << ",\n"
		<< "  \"inference\": {\n"
		<< "    \"districtClusterGap\": " << DistrictClusterGap << ",\n"
		<< "    \"districtContextMargin\": " << DistrictContextMargin << ",\n"
		<< "    \"templeFallbackRadius\": " << TempleFallbackRadius << "\n"
		<< "  },\n"
		<< "  \"cities\": [\n";
	for (size_t cityIndex = 0; cityIndex < cities.size(); ++cityIndex) {
		const InferredCity &city = cities[cityIndex];
		if (cityIndex > 0) {
			output << ",\n";
		}
		output << "    {\n"
			<< "      \"town\": " << serializeTown(*city.town).dump() << ",\n"
			<< "      \"districts\": [\n";
		for (size_t districtIndex = 0; districtIndex < city.districts.size(); ++districtIndex) {
			SampleStatistics districtStatistics;
			const json district = buildDistrict(map, city.districts[districtIndex], districtIndex + 1, districtStatistics);
			if (districtIndex > 0) {
				output << ",\n";
			}
			output << "        " << district.dump(8);
			totals.tileCount += districtStatistics.tileCount;
			totals.itemCount += districtStatistics.itemCount;
			totals.houseTileCount += districtStatistics.houseTileCount;
			++totals.districtCount;
		}
		output << "\n      ]";
		if (city.districts.empty()) {
			output << ",\n      \"warning\": \"Town has neither house evidence nor a valid temple position.\"";
		}
		output << "\n    }";
	}
	output << "\n  ],\n"
		<< "  \"summary\": {\n"
		<< "    \"cityCount\": " << cities.size() << ",\n"
		<< "    \"districtCount\": " << totals.districtCount << ",\n"
		<< "    \"tileCount\": " << totals.tileCount << ",\n"
		<< "    \"itemCount\": " << totals.itemCount << ",\n"
		<< "    \"houseTileCount\": " << totals.houseTileCount << ",\n"
		<< "    \"projectedPositionCount\": " << positions << "\n"
		<< "  }\n"
		<< "}\n";
	output.flush();
	if (!output.good()) {
		g_gui.PopupDialog(parent, "Export All Town Learning Corpus", "Could not finish writing the JSON corpus.", wxOK | wxICON_ERROR);
		return false;
	}

	g_gui.PopupDialog(
		parent,
		"Export All Town Learning Corpus",
		wxString::Format(
			"Exported %llu towns, %llu districts and %llu tiles to '%s'.",
			static_cast<unsigned long long>(cities.size()),
			static_cast<unsigned long long>(totals.districtCount),
			static_cast<unsigned long long>(totals.tileCount),
			dialog.GetPath().c_str()
		),
		wxOK | wxICON_INFORMATION
	);
	return true;
}
} // namespace

namespace CityCorpus {
bool ExportSelection(Editor &editor, wxWindow* parent) {
	if (!editor.hasSelection()) {
		g_gui.PopupDialog(parent, "Export City Learning Sample", "Select a city area before exporting a learning sample.", wxOK | wxICON_INFORMATION);
		return false;
	}

	wxTextEntryDialog nameDialog(parent, "Name this learning sample:", "Export City Learning Sample", "city-sample");
	if (nameDialog.ShowModal() != wxID_OK) {
		return false;
	}
	std::string sampleName = nstr(nameDialog.GetValue());
	trim(sampleName);
	if (sampleName.empty()) {
		g_gui.PopupDialog(parent, "Export City Learning Sample", "The learning sample name cannot be empty.", wxOK | wxICON_INFORMATION);
		return false;
	}
	return writeSample(parent, editor.getMap(), selectionBounds(editor.getSelection()), sampleName);
}

bool ExportAllTowns(Editor &editor, wxWindow* parent) {
	return writeAllTownCorpus(parent, editor.getMap());
}
} // namespace CityCorpus
