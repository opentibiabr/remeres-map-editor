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

void KmapWriter::build(Map & map)
{
	builder.Finish(Kmap::CreateMap(builder, buildHeader(map), buildMapData(map)));
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

FBOffset<Kmap::MapHeader> KmapWriter::buildHeader(Map & map)
{
	return Kmap::CreateMapHeader(
		builder,
		map.getWidth(),
		map.getHeight(),
		map.getVersion().otbm,
		buildString(getFullFileName(map.getSpawnFilename())),
		buildString(getFullFileName(map.getSpawnNpcFilename())),
		buildString(getFullFileName(map.getHouseFilename())),
		buildString("Saved with Remere's Map Editor " + __RME_VERSION__ + "\n" + map.getMapDescription())
	);
}

FBOffset<Kmap::MapData> KmapWriter::buildMapData(Map & map)
{
	return Kmap::CreateMapData(builder, buildAreas(map), buildTowns(map), buildWaypoints(map));
}

FBVectorOffset<FBOffset<Kmap::Area>> KmapWriter::buildAreas(Map &map)
{
	StdOffsetVector<Kmap::Area> areasOffset;
	StdOffsetVector<Kmap::Tile> tilesOffset;

	Position* areaPos = nullptr;
	MapIterator mapIterator = map.begin();
	while (mapIterator != map.end())
	{
		Tile *tile = (*mapIterator)->get();
		if (tile == nullptr || tile->size() == 0)
		{
			++mapIterator;
			continue;
		}

		Position pos = tile->getPosition();

		if (isNewArea(pos, areaPos))
		{
			if (auto areaOffset = buildArea(pos, areaPos, tilesOffset);
			!areaOffset.IsNull())
			{
				areasOffset.push_back(areaOffset);
			}

			areaPos->x = pos.x;
			areaPos->y = pos.y;
			areaPos->z = pos.z;
			tilesOffset.clear();
		}

		if (areaPos == nullptr) {
			areaPos = &pos;
		}

		auto offset = buildTile(*tile);
		if (!offset.IsNull())
		{
			tilesOffset.push_back(offset);
		}

		++mapIterator;
	}

	return builder.CreateVector(areasOffset);
}

FBOffset<Kmap::Area> KmapWriter::buildArea(const Position pos, Position *areaPos, StdOffsetVector<Kmap::Tile> tilesOffset)
{
	if (tilesOffset.empty())
	{
		return 0;
	}

	StdOffsetVector<Kmap::Tile> finalizedOffset;
	tilesOffset.swap(finalizedOffset);

	auto areaOffset = Kmap::CreateArea(
		builder,
		builder.CreateVector(finalizedOffset),
		Kmap::CreatePosition(builder, areaPos->x &0xFF00, areaPos->y &0xFF00, areaPos->z)
	);

	return areaOffset;
}

bool KmapWriter::isNewArea(const Position &pos, Position *areaPos)
{
	if (areaPos == nullptr)
	{
		return false;
	}

	return pos.x < areaPos->x ||
	pos.x >= areaPos->x + 256 ||
	pos.y < areaPos->y ||
	pos.y >= areaPos->y + 256 ||
	pos.z != areaPos->z;
}

FBOffset<Kmap::Tile> KmapWriter::buildTile(Tile &tile)
{
	Item *ground = tile.ground;
	bool hasGround = false;
	if (ground && !ground->isMetaItem())
	{
		hasGround = true;

		for(Item* item : tile.items)
		{
			if (!ground->hasBorderEquivalent())
			{
				break;
			}
			if (item->getGroundEquivalent() == ground->getID())
			{
				hasGround = false;
				break;
			}
		}
	}

	return Kmap::CreateTile(
		builder,
		buildItems(tile.items),
		hasGround ? buildItem(*ground) : 0,
		tile.getX() &0xFF,
		tile.getY() &0xFF,
		tile.getMapFlags(),
		tile.getHouseID()
	);
}

FBVectorOffset<FBOffset<Kmap::Item>> KmapWriter::buildItems(std::vector<Item*> &items)
{
	if (items.empty())
	{
		return 0;
	}

	StdOffsetVector<Kmap::Item> itemsOffset;

	for (Item *item: items)
	{
		if (item == nullptr)
		{
			continue;
		}

		if (auto itemOffset = buildItem(*item);
		!itemOffset.IsNull())
		{
			itemsOffset.push_back(itemOffset);
		}
	}

	if (itemsOffset.empty())
	{
		return 0;
	}

	return builder.CreateVector(itemsOffset);
}

FBOffset<Kmap::Item> KmapWriter::buildItem(Item &item)
{
	if (item.isMetaItem())
	{
		return 0;
	}

	FBOffset<Kmap::ItemAttributes> itemAttributesOffset = 0;

	if (!item.getText().empty() || !item.getDescription().empty() || item.getSubtype() > 0)
	{
		itemAttributesOffset = CreateItemAttributes(
			builder,
			item.getSubtype(),
			buildString(item.getText()),
			buildString(item.getDescription()),
			buildActionAttributes(item)
		);
	}

	return Kmap::CreateItem(
		builder,
		item.getID(),
		buildItemsDetails(item),
		itemAttributesOffset
	);
}

FBOffset<Kmap::Action> KmapWriter::buildActionAttributes(Item &item)
{
	if (item.getActionID() == 0 && item.getUniqueID() == 0)
	{
		return 0;
	}

	return Kmap::CreateAction(builder, item.getActionID(), item.getUniqueID());
}

FBOffset<Kmap::ItemDetails> KmapWriter::buildItemsDetails(Item &item)
{
	Container *container = dynamic_cast<Container*>(&item);
	Depot *depot = dynamic_cast<Depot*>(&item);
	Door *door = dynamic_cast<Door*>(&item);
	Teleport *teleport = dynamic_cast<Teleport*>(&item);

	if (container == nullptr && depot == nullptr && door == nullptr && teleport == nullptr)
	{
		return 0;
	}

	return Kmap::CreateItemDetails(
		builder,
		container ? buildItems(container->getVector()) : 0,
		depot ? depot->getDepotID() : 0,
		door ? door->getDoorID() : 0,
		teleport ? buildPosition(teleport->getDestination()) : 0
	);
}

FBVectorOffset<FBOffset<Kmap::Town>> KmapWriter::buildTowns(Map & map)
{
	StdOffsetVector<Kmap::Town> townsOffset;

	for (const auto &[townId, town]: map.towns)
	{
		townsOffset.push_back(
			Kmap::CreateTown(
				builder,
				townId,
				buildString(town->getName()),
				buildPosition(town->getTemplePosition())
			)
		);
	}

	return builder.CreateVector(townsOffset);
}

FBVectorOffset<FBOffset<Kmap::Waypoint>> KmapWriter::buildWaypoints(Map & map)
{
	StdOffsetVector<Kmap::Waypoint> waypointsOffSet;

	for (const auto &[waypointId, waypoint]: map.waypoints)
	{
		waypointsOffSet.push_back(
			Kmap::CreateWaypoint(
				builder,
				buildString(waypoint->name),
				buildPosition(waypoint->pos)
			)
		);
	}

	return builder.CreateVector(waypointsOffSet);
}

FBOffset<flatbuffers::String> KmapWriter::buildString(std::string string)
{
	return string.empty() ? builder.CreateString(string) : 0;
}

FBOffset<Kmap::Position> KmapWriter::buildPosition(const Position &pos)
{
	return Kmap::CreatePosition(builder, pos.x, pos.y, pos.z);
}

std::string KmapWriter::getFullFileName(std::string fileName)
{
	FileName tmpName;
	tmpName.Assign(wxstr(fileName));
	return nstr(tmpName.GetFullName());
}
