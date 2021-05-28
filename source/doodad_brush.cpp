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

#include "doodad_brush.h"
#include "basemap.h"
#include "pugicast.h"
#include "brush.h"

//=============================================================================
// Doodad brush

DoodadBrush::DoodadBrush() :
	look_id(0),
	thickness(0),
	thickness_ceiling(0),
	draggable(false),
	on_blocking(false),
	one_size(false),
	do_new_borders(false),
	on_duplicate(false),
	clear_mapflags(0),
	clear_statflags(0)
{
	////
}

DoodadBrush::~DoodadBrush()
{
}

bool DoodadBrush::load(pugi::xml_node node, wxArrayString& warnings)
{
	pugi::xml_attribute attribute;
	if((attribute = node.attribute("lookid"))) {
		look_id = pugi::cast<uint16_t>(attribute.value());
	}

	if((attribute = node.attribute("server_lookid"))) {
		look_id = g_items[pugi::cast<uint16_t>(attribute.value())].clientID;
	}

	if((attribute = node.attribute("on_blocking"))) {
		on_blocking = attribute.as_bool();
	}

	if((attribute = node.attribute("on_duplicate"))) {
		on_duplicate = attribute.as_bool();
	}

	if((attribute = node.attribute("redo_borders")) || (attribute = node.attribute("reborder"))) {
		do_new_borders = attribute.as_bool();
	}

	if((attribute = node.attribute("one_size"))) {
		one_size = attribute.as_bool();
	}

	if((attribute = node.attribute("draggable"))) {
		draggable = attribute.as_bool();
	}

	if(node.attribute("remove_optional_border").as_bool()) {
		if(!do_new_borders) {
			warnings.push_back("remove_optional_border will not work without redo_borders\n");
		}
		clear_statflags |= TILESTATE_OP_BORDER;
	}

	const std::string& thicknessString = node.attribute("thickness").as_string();
	if(!thicknessString.empty()) {
		size_t slash = thicknessString.find('/');
		if(slash != std::string::npos) {
			thickness = boost::lexical_cast<int32_t>(thicknessString.substr(0, slash));
			thickness_ceiling = std::max<int32_t>(thickness, boost::lexical_cast<int32_t>(thicknessString.substr(slash + 1)));
		}
	}

	for(pugi::xml_node childNode = node.first_child(); childNode; childNode = childNode.next_sibling()) {
		if(as_lower_str(childNode.name()) != "alternate") {
			continue;
		}
		if(!g_brushes.loadAlternative(childNode, warnings)) {
			return false;
		}
	}
	g_brushes.loadAlternative(node, warnings, g_brushes.alternatives.empty() ? nullptr : g_brushes.alternatives.back());
	return true;
}

bool DoodadBrush::ownsItem(Item* item) const
{
	if(item->getDoodadBrush() == this) {
		return true;
	}

	uint16_t id = item->getID();

	for(std::vector<Brushes::AlternativeBlock*>::const_iterator alt_iter = g_brushes.alternatives.begin(); alt_iter != g_brushes.alternatives.end(); ++alt_iter) {
		if((*alt_iter)->ownsItem(id)) {
			return true;
		}
	}
	return false;
}

void DoodadBrush::undraw(BaseMap* map, Tile* tile)
{
	// Remove all doodad-related
	for(ItemVector::iterator item_iter = tile->items.begin(); item_iter != tile->items.end();) {
		Item* item = *item_iter;
		if(item->getDoodadBrush() != nullptr) {
			if(item->isComplex() && g_settings.getInteger(Config::ERASER_LEAVE_UNIQUE)) {
				++item_iter;
			} else if(g_settings.getInteger(Config::DOODAD_BRUSH_ERASE_LIKE)) {
				// Only delete items of the same doodad brush
				if(ownsItem(item)) {
					delete item;
					item_iter = tile->items.erase(item_iter);
				} else {
					++item_iter;
				}
			} else {
				delete item;
				item_iter = tile->items.erase(item_iter);
			}
		} else {
			++item_iter;
		}
	}

	if(tile->ground && tile->ground->getDoodadBrush() != nullptr) {
		if(g_settings.getInteger(Config::DOODAD_BRUSH_ERASE_LIKE)) {
			// Only delete items of the same doodad brush
			if(ownsItem(tile->ground)) {
				delete tile->ground;
				tile->ground = nullptr;
			}
		} else {
			delete tile->ground;
			tile->ground = nullptr;
		}
	}
}

void DoodadBrush::draw(BaseMap* map, Tile* tile, void* parameter)
{
	int variation = 0;
	if(parameter) {
		variation = *reinterpret_cast<int*>(parameter);
	}

	if(g_brushes.alternatives.empty()) return;

	variation %= g_brushes.alternatives.size();
	const Brushes::AlternativeBlock* ab_ptr = g_brushes.alternatives[variation];
	ASSERT(ab_ptr);

	int roll = random(1, ab_ptr->single_chance);
	for(std::vector<Brushes::SingleBlock>::const_iterator block_iter = ab_ptr->single_items.begin(); block_iter != ab_ptr->single_items.end(); ++block_iter) {
		const Brushes::SingleBlock& sb = *block_iter;
		if(roll <= sb.chance) {
			// Use this!
			tile->addItem(sb.item->deepCopy());
			break;
		}
		roll -= sb.chance;
	}
	if(clear_mapflags || clear_statflags) {
		tile->setMapFlags(tile->getMapFlags() & (~clear_mapflags));
		tile->setMapFlags(tile->getStatFlags() & (~clear_statflags));
	}
}

bool DoodadBrush::isEmpty(int variation) const
{
	if(hasCompositeObjects(variation))
		return false;
	if(hasSingleObjects(variation))
		return false;
	if(thickness <= 0)
		return false;
	return true;
}

int DoodadBrush::getCompositeChance(int ab) const
{
	if(g_brushes.alternatives.empty()) return 0;
	ab %= g_brushes.alternatives.size();
	const Brushes::AlternativeBlock* ab_ptr = g_brushes.alternatives[ab];
	ASSERT(ab_ptr);
	return ab_ptr->composite_chance;
}

int DoodadBrush::getSingleChance(int ab) const
{
	if(g_brushes.alternatives.empty()) return 0;
	ab %= g_brushes.alternatives.size();
	const Brushes::AlternativeBlock* ab_ptr = g_brushes.alternatives[ab];
	ASSERT(ab_ptr);
	return ab_ptr->single_chance;
}

int DoodadBrush::getTotalChance(int ab) const
{
	if(g_brushes.alternatives.empty()) return 0;
	ab %= g_brushes.alternatives.size();
	const Brushes::AlternativeBlock* ab_ptr = g_brushes.alternatives[ab];
	ASSERT(ab_ptr);
	return ab_ptr->composite_chance + ab_ptr->single_chance;
}

bool DoodadBrush::hasSingleObjects(int ab) const
{
	if(g_brushes.alternatives.empty()) return false;
	ab %= g_brushes.alternatives.size();
	Brushes::AlternativeBlock* ab_ptr = g_brushes.alternatives[ab];
	ASSERT(ab_ptr);
	return ab_ptr->single_chance > 0;
}

bool DoodadBrush::hasCompositeObjects(int ab) const
{
	if(g_brushes.alternatives.empty()) return false;
	ab %= g_brushes.alternatives.size();
	Brushes::AlternativeBlock* ab_ptr = g_brushes.alternatives[ab];
	ASSERT(ab_ptr);
	return ab_ptr->composite_chance > 0;
}
