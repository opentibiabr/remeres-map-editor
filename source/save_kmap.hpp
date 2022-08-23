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

#ifndef RME_SAVE_KMAP_HPP_
#define RME_SAVE_KMAP_HPP_

#include <fstream>
#include <utility>
#include "kmap/kmap_writer.hpp"
#include "map.h"

using Detail = Kmap::DTO::ItemDetail;
using Attribute = Kmap::DTO::ItemAttribute;

class SaveKMap {
public:
	void build(Map & map);

private:
	Kmap::Writer writer;

	void save(std::string mapPath);

	void setHeader(const Map& map);

	void buildAreas(Map &map);

private:
	FBBuffer generated;

	void buildItem(std::vector<Item*> &items);

	Detail buildItemDetail(Item *item);

	Attribute buildItemAttribute(Item *item);

	void buildContainerItems(Container* container);

	void buildTowns(Map& map);

	void buildWaypoints(Map& map);

	bool isFinishedArea(const Position &pos, int areaX, int areaY, int areaZ);

	bool hasGround(Tile &tile);

	std::string getFullFileName(std::string fileName);
	Kmap::DTO::Position buildPosition(Position pos);
};

#endif