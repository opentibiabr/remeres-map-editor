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
// along with this program. If not, see<http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "kmap_writer.hpp"

#include "map.h"

// Makes reading easier and reduces the complexity of returns
using FlatOffSetHeader = flatbuffers::Offset<Kmap::MapHeader>;
using FlatOffSetMapData = flatbuffers::Offset<Kmap::MapData>;
using FlatOffSetTile = flatbuffers::Offset<Kmap::Tile>;
using FlatOffSetItem = flatbuffers::Offset<Kmap::Item>;
using FlatOffSetArea = flatbuffers::Offset<Kmap::Area>;
using FlatOffSetTown = flatbuffers::Offset<Kmap::Town>;
using FlatOffSetWayPoint = flatbuffers::Offset<Kmap::Waypoint>;
using FlatOffSetAttributes = flatbuffers::Offset<Kmap::ActionAttributes>;
using FlatTownVector = flatbuffers::Offset<flatbuffers::Vector<FlatOffSetTown>>;
using FlatWayPointVector = flatbuffers::Offset<flatbuffers::Vector<FlatOffSetWayPoint>>;
using FlatAreaVector = flatbuffers::Offset<flatbuffers::Vector<FlatOffSetArea>>;
using FlatItemVector = flatbuffers::Offset<flatbuffers::Vector<FlatOffSetItem>>;

void KmapWriter::build(Map & map)
{
	auto headerOffset = buildHeader(map);
	auto dataOffset = buildMapData(map);
	builder.Finish(Kmap::CreateMap(builder, headerOffset, dataOffset));
	save(map.getPath() + ".kmap");
}

void KmapWriter::save(std::string mapPath)
{
	std::ofstream ofs(mapPath, std::ofstream::binary);
	ofs.write((char*) getBuffer(), getSize());
	ofs.close();
	wxMessageBox(mapPath);
}

uint8_t *KmapWriter::getBuffer()
{
	return builder.GetBufferPointer();
}

flatbuffers::uoffset_t KmapWriter::getSize()
{
	return builder.GetSize();
}

FlatOffSetHeader KmapWriter::buildHeader(Map & map)
{
	auto monsterSpawnFile = builder.CreateString(getFullFileName(map.getSpawnFilename()));
	auto npcSpawnFile = builder.CreateString(getFullFileName(map.getSpawnNpcFilename()));
	auto houseFile = builder.CreateString(getFullFileName(map.getHouseFilename()));
	auto description = builder.CreateString("Saved with Remere's Map Editor " + __RME_VERSION__ + "\n" + map.getMapDescription());

	return Kmap::CreateMapHeader(
		builder,
		map.getWidth(),
		map.getHeight(),
		map.getVersion().otbm,
		monsterSpawnFile,
		npcSpawnFile,
		houseFile,
		description
	);
}

FlatOffSetMapData KmapWriter::buildMapData(Map & map)
{
	auto areas = buildArea(map);
	auto towns = buildTowns(map);
	auto waypoints = buildWaypoints(map);

	return Kmap::CreateMapData(builder, areas, towns, waypoints);
}

FlatAreaVector KmapWriter::buildArea(Map & map)
{
	std::vector<FlatOffSetArea> areasOffset;
	std::vector<FlatOffSetTile> tilesOffset;

	Position * areaPos = nullptr;
	MapIterator mapIterator = map.begin();
	while (mapIterator != map.end())
	{
		Tile *tile = (*mapIterator)->get();
		if (tile == nullptr || tile->size() == 0)
		{
			++mapIterator;
			continue;
		}

		const Position &pos = tile->getPosition();

		if (isNewArea(pos, areaPos))
		{
			std::vector<FlatOffSetTile> finalizedOffset;
			tilesOffset.swap(finalizedOffset);
			auto tiles = builder.CreateVector(finalizedOffset);

			auto position = Kmap::CreatePosition(
				builder,
				areaPos->x &0xFF00,
				areaPos->y &0xFF00,
				areaPos->z
			);

			areasOffset.push_back(Kmap::CreateArea(builder, tiles, position));

			areaPos->x = pos.x;
			areaPos->y = pos.y;
			areaPos->z = pos.z;
			tilesOffset.clear();

		}

		auto offset = buildTile(map, *tile, pos);
		if (!offset.IsNull())
		{
			tilesOffset.push_back(offset);
		}

		++mapIterator;
	}

	return builder.CreateVector(areasOffset);
}

FlatOffSetTile KmapWriter::buildTile(Map &map, Tile &tile, const Position &pos)
{
	Item *ground = tile.ground;
	if (ground == nullptr || ground->isMetaItem())
	{
		return 0;
	}

	House *house = map.houses.getHouse(tile.getHouseID());
	auto houseInfoOffset = house ? Kmap::CreateHouseInfo(
		builder,
		tile.getHouseID(),
		house->getEmptyDoorID()
	) : 0;

	auto createActionOffSet = buildActionAttributes(*ground, pos);

	return Kmap::CreateTile(
		builder,
		buildGround(*ground, pos),
		tile.getX() &0xFF,
		tile.getY() &0xFF,
		tile.getMapFlags(),
		ground->getID(),
		houseInfoOffset
	);
}

bool KmapWriter::isNewArea(const Position &pos, Position *areaPos)
{
	if (!areaPos) return false;

	return pos.x < areaPos->x ||
		pos.x >= areaPos->x + 256 ||
		pos.y < areaPos->y ||
		pos.y >= areaPos->y + 256 ||
		pos.z != areaPos->z;
}

FlatOffSetItem KmapWriter::buildGround(Item &item, const Position &pos)
{
	auto createActionOffSet = buildActionAttributes(item, pos);
	auto itemAttributesOffset = CreateItemAttributes(
		builder,
		0,
		0,
		0,
		0,
		0,
		createActionOffSet
	);

	return Kmap::CreateItem(
		builder,
		0,
		item.getID(),
		itemAttributesOffset
	);
}

FlatItemVector KmapWriter::buildItems(Tile &tile, const Position &pos)
{
	std::vector<FlatOffSetItem> itemsOffset;
	for (Item *item: tile.items)
	{
		if (item == nullptr || item->isMetaItem())
		{
			continue;
		}

		std::string text = item->getText();
		std::string description = item->getDescription();
		auto textOffset = (!text.empty()) ? builder.CreateString(text) : 0;
		auto descriptionOffset = (!description.empty()) ? builder.CreateString(description) : 0;
		auto createActionOffSet = buildActionAttributes(*item, pos);

		Depot *depot = dynamic_cast<Depot*> (item);
		auto itemAttributesOffset = CreateItemAttributes(
			builder,
			item->getCount(),
			depot ? depot->getDepotID() : 0,
			item->getSubtype(),
			textOffset,
			descriptionOffset,
			createActionOffSet
		);

		auto createItemsOffset = Kmap::CreateItem(
			builder,
			buildItems(tile, pos),
			item->getID(),
			itemAttributesOffset
		);

		//if (is subitem) return Kmap::Item.createItemsVector(builder, itemsOffset);
		itemsOffset.push_back(createItemsOffset);
	}

	return builder.CreateVector(itemsOffset);
}

FlatOffSetAttributes KmapWriter::buildActionAttributes(Item &item, const Position &pos)
{
	uint16_t actionId = item.getActionID();
	uint16_t uniqueId = item.getActionID();
	if (actionId == 0 || uniqueId == 0)
	{
		return 0;
	}

	auto positionOffSet = Kmap::CreatePosition(
		builder,
		pos.x,
		pos.y,
		pos.z
	);

	return Kmap::CreateActionAttributes(
		builder,
		actionId,
		uniqueId,
		positionOffSet
	);
}

FlatTownVector KmapWriter::buildTowns(Map & map)
{
	std::vector<FlatOffSetTown> townsOffset;
	for (const auto &[townId, town]: map.towns)
	{
		const Position &townPosition = town->getTemplePosition();

		auto positionOffSet = Kmap::CreatePosition(
			builder,
			townPosition.x,
			townPosition.y,
			townPosition.z
	);

		auto name = builder.CreateString(town->getName());

		auto createTownOffset = Kmap::CreateTown(
			builder,
			townId,
			name,
			positionOffSet
	);

		townsOffset.push_back(createTownOffset);

	}

	return builder.CreateVector(townsOffset);
}

FlatWayPointVector KmapWriter::buildWaypoints(Map & map)
{
	std::vector<FlatOffSetWayPoint> waypointsOffSet;
	for (const auto &[waypointId, waypoint]: map.waypoints)
	{
		auto position = Kmap::CreatePosition(
			builder,
			waypoint->pos.x,
			waypoint->pos.y,
			waypoint->pos.z
	);

		auto name = builder.CreateString(waypoint->name);

		auto createWaypointOffSet = Kmap::CreateWaypoint(
			builder,
			name,
			position
	);

		waypointsOffSet.push_back(createWaypointOffSet);
	}

	return builder.CreateVector(waypointsOffSet);
}

std::string KmapWriter::getFullFileName(std::string fileName)
{
	FileName tmpName;
	tmpName.Assign(wxstr(fileName));
	return nstr(tmpName.GetFullName());
}
