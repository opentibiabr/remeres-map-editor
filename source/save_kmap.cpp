// /*
//  * KnaryLib - a helper CPP library for open-source MMORPG development, developed and maintained by OpenTibiaBr.
//  * Copyright (C) 2022 - Lucas Grossi <lucas.ggrossi@gmail.com>
//  *
//  * This program is free software: you can redistribute it and/or modify
//  * it under the terms of the GNU Affero General Public License as published by
//  * the Free Software Foundation, either version 3 of the License, or
//  * (at your option) any later version.
//  *
//  * This program is distributed in the hope that it will be useful,
//  * but WITHOUT ANY WARRANTY; without even the implied warranty of
//  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  * GNU Affero General Public License for more details.
//  *
//  * You should have received a copy of the GNU Affero General Public License along
//  * with this program.  If not, see <https://www.gnu.org/licenses/>.
//  *

#include "main.h"

#include "save_kmap.hpp"

void SaveKMap::build(Map & map)
{
	try {
		setHeader(map);
		buildAreas(map);
		buildTowns(map);
		buildWaypoints(map);

		generated = writer.finish();
		save(map.getPath() + ".kmap");
	} catch (const Kmap::CannotWriteKmap& e) {
		std::cout << e.what();
	}
}

void SaveKMap::save(std::string mapPath)
{
	std::ofstream ofs(mapPath, std::ofstream::binary);
	ofs.write((char *) generated.first, generated.second);
	ofs.close();
	wxMessageBox(mapPath);
}

void SaveKMap::setHeader(const Map& map)
{
	writer.addHeader(
		Kmap::DTO::Header(
			map.getWidth(),
			map.getHeight(),
			map.getVersion().otbm,
			getFullFileName(map.getSpawnFilename()),
			getFullFileName(map.getSpawnNpcFilename()),
			getFullFileName(map.getHouseFilename()),
			"Saved with Remere's Map Editor " + __RME_VERSION__ + "\n" + map.getMapDescription()
		)
	);
}

void SaveKMap::buildAreas(Map& map)
{
	int x = -1, y = -1, z = -1;
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

		bool isFinished = isFinishedArea(pos, x, y, z);

		if (isFinished) writer.addArea(Kmap::DTO::Position(x &0xFF00, y &0xFF00, z));

		if (isFinished || x < 0)
		{
			x = pos.x;
			y = pos.y;
			z = pos.z;
		}

		for (Item *item : tile->items) {
			buildItem(*item);
		}

		writer.addTile(
			!hasGround(*tile)
				? Kmap::DTO::Tile(
					tile->getX() & 0xFF,
					tile->getY() & 0xFF,
					tile->getMapFlags(),
					tile->getHouseID()
				) : Kmap::DTO::Tile(
					tile->getX() & 0xFF,
					tile->getY() & 0xFF,
					tile->getMapFlags(),
					tile->getHouseID(),
					tile->ground->getID(),
					buildItemDetail(tile->ground),
					buildItemAttribute(tile->ground)
				)
		);

		++mapIterator;
	}

	if (x != -1) writer.addArea(Kmap::DTO::Position(x & 0xFF00, y & 0xFF00, z));
}

void SaveKMap::buildItem(Item &item)
{
	if (item.isMetaItem()) return;

	buildContainerItems(dynamic_cast<Container*>(&item));

	writer.addItem(
		item.getID(),
		buildItemDetail(&item),
		buildItemAttribute(&item)
	);
}

Detail SaveKMap::buildItemDetail(Item *item) {
	if (item == nullptr) return Detail();

	Depot *depot = dynamic_cast<Depot*>(item);
	Door *door = dynamic_cast<Door*>(item);
	Teleport *teleport = dynamic_cast<Teleport*>(item);

	return Detail(
		depot ? depot->getDepotID() : 0,
		door ? door->getDoorID() : 0,
		buildPosition(teleport ? teleport->getDestination() : Position(0, 0, 0))
	);
}

Attribute SaveKMap::buildItemAttribute(Item *item) {
	if (item == nullptr) return Attribute();

	return Attribute(
		item->getSubtype(),
		item->getActionID(),
		item->getUniqueID(),
		item->getText(),
		item->getDescription()
	);
}

void SaveKMap::buildContainerItems(Container* container)
{
	if (container == nullptr) return;

	for (auto item : container->getVector()) {
		if (item->isMetaItem()) return;
		writer.addContainerItem(item->getID(), buildItemAttribute(item));
	}
}

void SaveKMap::buildTowns(Map& map)
{
	for (const auto &[townId, town]: map.towns)
	{
		writer.addTown(Kmap::DTO::Town(townId, town->getName(), buildPosition(town->getTemplePosition())));
	}
}

void SaveKMap::buildWaypoints(Map& map)
{
	for (const auto &[waypointId, waypoint]: map.waypoints)
	{
		writer.addWaypoint(Kmap::DTO::Waypoint(waypoint->name, buildPosition(waypoint->pos)));
	}
}

bool SaveKMap::isFinishedArea(const Position &pos, int areaX, int areaY, int areaZ)
{
	if (areaX < 0 || areaY < 0 || areaZ < 0) return false;

	return pos.x < areaX ||
				 pos.x >= areaX + 256 ||
				 pos.y < areaY ||
				 pos.y >= areaY + 256 ||
				 pos.z != areaZ;
}

bool SaveKMap::hasGround(Tile &tile) {
	auto ground = tile.ground;
	if (ground == nullptr || ground->isMetaItem()) {
		return false;
	}

	for(Item* item : tile.items)
	{
		if (!ground->hasBorderEquivalent()) return true;
		if (item->getGroundEquivalent() == ground->getID())
		{
			return false;
		}
	}

	return true;
}

std::string SaveKMap::getFullFileName(std::string fileName)
{
	FileName tmpName;
	tmpName.Assign(wxstr(fileName));
	return nstr(tmpName.GetFullName());
}

Kmap::DTO::Position SaveKMap::buildPosition(Position pos)  {
	return Kmap::DTO::Position(pos.x, pos.y, pos.z);
}
