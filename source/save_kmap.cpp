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
#include "kmap/rme_item_attribute.hpp"
#include "kmap/rme_item_detail.hpp"
#include "kmap/rme_map_header.hpp"
#include "kmap/rme_tile.hpp"

void SaveKMap::build(Map& map)
{
	try {
		writer.addHeader(RMEMapHeader(map));
		buildAreas(map);
		buildTowns(map);

		generated = writer.finish();
	} catch (const Kmap::CannotWriteKmap& e) {
		std::cout << e.what();
	}
}

void SaveKMap::save(std::string mapPath)
{
	mapPath .= ".kmap";
	std::ofstream ofs(mapPath, std::ofstream::binary);
	ofs.write((char *) generated.first, generated.second);
	ofs.close();
	wxMessageBox(mapPath);
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

		if (bool isFirst = x == -1; isFinishedArea(pos, x, y, z)) {
			x = pos.x & 0xFF00;
			y = pos.y & 0xFF00;
			z = pos.z;

			if (!isFirst) writer.addArea(Position(x, y ,z));
		}

		for (Item *item : tile->items) {
			buildItem(*item);
		}

		writer.addTile(RMETile(tile));

		++mapIterator;
	}

	if (x != -1) writer.addArea(Position(x & 0xFF00, y & 0xFF00, z));
}

void SaveKMap::buildItem(Item& item)
{
	if (item.isMetaItem()) return;

	buildContainerItems(dynamic_cast<Container*>(&item));

	writer.addItem(
		item.getID(),
		RMEItemDetail(&item),
		RMEItemAttribute(&item)
	);
}

void SaveKMap::buildContainerItems(Container* container)
{
	if (container == nullptr) return;

	for (auto item : container->getVector()) {
		if (item->isMetaItem()) return;
		writer.addContainerItem(item->getID(), RMEItemAttribute(item));
	}
}

void SaveKMap::buildTowns(Map& map)
{
	for (const auto &[townId, town]: map.towns) writer.addTown(town);
}

bool SaveKMap::isFinishedArea(const Position &pos, int areaX, int areaY, int areaZ)
{
	return pos.x < areaX || pos.x >= areaX + 256 || pos.y < areaY || pos.y >= areaY + 256 || pos.z != areaZ;
}
