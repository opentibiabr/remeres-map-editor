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

#include "main.h"

#include "zone_brush.h"
#include "zones.h"
#include "basemap.h"

//=============================================================================
// Zone Brush

ZoneBrush::ZoneBrush() :
	FlagBrush(0) {
	////
}

ZoneBrush::~ZoneBrush() {
	////
}

void ZoneBrush::setZone(unsigned int id) {
	zoneId = id;
}

unsigned int ZoneBrush::getZone() const {
	return zoneId;
}

bool ZoneBrush::canDraw(BaseMap* map, const Position &position) const {
	return map->getTile(position) != nullptr && zoneId != 0;
}

void ZoneBrush::undraw(BaseMap* map, Tile* tile) {
	tile->removeZone(zoneId);
}

void ZoneBrush::draw(BaseMap* map, Tile* tile, void* parameter) {
	if (tile->hasGround()) {
		tile->addZone(zoneId);
	}
}
