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

#include "tileset.h"
#include "brush_database.h"
#include "monsters.h"
#include "monster_brush.h"
#include "npcs.h"
#include "npc_brush.h"
#include "items.h"
#include "raw_brush.h"

#include <algorithm>
#include <iterator>
#include <string_view>

namespace {
	bool ResolveTilesetCategoryTypes(std::string_view sectionType, TilesetCategoryType &primaryType, bool &hasSecondaryType, TilesetCategoryType &secondaryType) {
		hasSecondaryType = false;
		secondaryType = TILESET_UNKNOWN;

		if (sectionType == "terrain") {
			primaryType = TILESET_TERRAIN;
			return true;
		}
		if (sectionType == "doodad") {
			primaryType = TILESET_DOODAD;
			return true;
		}
		if (sectionType == "items") {
			primaryType = TILESET_ITEM;
			return true;
		}
		if (sectionType == "raw") {
			primaryType = TILESET_RAW;
			return true;
		}
		if (sectionType == "terrain_and_raw") {
			primaryType = TILESET_TERRAIN;
			hasSecondaryType = true;
			secondaryType = TILESET_RAW;
			return true;
		}
		if (sectionType == "doodad_and_raw") {
			primaryType = TILESET_DOODAD;
			hasSecondaryType = true;
			secondaryType = TILESET_RAW;
			return true;
		}
		if (sectionType == "items_and_raw") {
			primaryType = TILESET_ITEM;
			hasSecondaryType = true;
			secondaryType = TILESET_RAW;
			return true;
		}
		return false;
	}

	std::string ResolveAfterBrushName(const TilesetEntryRecord &entry, bool preserveStoredOrder) {
		std::string afterBrushName = entry.afterBrushName.ToStdString();
		if (preserveStoredOrder || entry.afterItemId <= 0) {
			return afterBrushName;
		}

		const ItemType &type = g_items.getItemType(entry.afterItemId);
		if (type.id == 0) {
			return afterBrushName;
		}

		return type.raw_brush ? type.raw_brush->getName() : std::string();
	}

	std::vector<Brush*>::iterator FindInsertPosition(std::vector<Brush*> &brushlist, std::string_view afterBrushName) {
		if (afterBrushName.empty()) {
			return brushlist.end();
		}
		const auto it = std::find_if(brushlist.begin(), brushlist.end(), [&](const Brush* brush) {
			return brush != nullptr && brush->getName() == afterBrushName;
		});
		return it == brushlist.end() ? it : std::next(it);
	}

	bool LoadBrushEntry(
		Brushes &brushes,
		TilesetCategory &category,
		const TilesetEntryRecord &entry,
		wxArrayString &warnings,
		bool preserveStoredOrder,
		std::string_view afterBrushName
	) {
		if (entry.brushName.IsEmpty()) {
			return true;
		}

		Brush* brush = brushes.getBrush(entry.brushName.ToStdString());
		if (!brush) {
			warnings.push_back("Brush \"" + entry.brushName + "\" doesn't exist.");
			return true;
		}

		brush->flagAsVisible();
		if (preserveStoredOrder || afterBrushName.empty()) {
			category.brushlist.push_back(brush);
			return true;
		}

		auto insertPosition = FindInsertPosition(category.brushlist, afterBrushName);
		if (insertPosition == category.brushlist.end()) {
			category.brushlist.push_back(brush);
			return true;
		}

		category.brushlist.insert(insertPosition, brush);
		return true;
	}

	bool LoadItemEntry(
		Brushes &brushes,
		TilesetCategory &category,
		const TilesetEntryRecord &entry,
		wxArrayString &warnings,
		bool preserveStoredOrder,
		std::string_view afterBrushName
	) {
		const auto fromId = static_cast<uint16_t>(entry.fromItemId > 0 ? entry.fromItemId : entry.itemId);
		auto toId = static_cast<uint16_t>(entry.toItemId > 0 ? entry.toItemId : fromId);
		if (fromId == 0) {
			warnings.push_back("Couldn't read raw ids from SQLite tileset entry.");
			return true;
		}
		toId = std::max<uint16_t>(fromId, toId);

		const size_t rangeSize = static_cast<size_t>(toId - fromId) + 1;
		if (preserveStoredOrder) {
			category.brushlist.reserve(category.brushlist.size() + rangeSize);
			for (uint16_t id = fromId; id <= toId; ++id) {
				const auto &type = g_items.getRawItemType(id);
				if (!type || type->id == 0) {
					continue;
				}

				RAWBrush* brush;
				if (type->raw_brush) {
					brush = type->raw_brush;
				} else {
					brush = type->raw_brush = newd RAWBrush(type->id);
					type->has_raw = true;
					brushes.addBrush(brush);
				}

				if (type->doodad_brush == nullptr && !category.isTrivial()) {
					type->doodad_brush = brush;
				}

				brush->flagAsVisible();
				category.brushlist.push_back(brush);
				category.tileset.previousId = id;
			}
			return true;
		}

		std::vector<Brush*> tempBrushVector;
		tempBrushVector.reserve(rangeSize);
		for (uint16_t id = fromId; id <= toId; ++id) {
			const auto &type = g_items.getRawItemType(id);
			if (!type || type->id == 0) {
				continue;
			}

			RAWBrush* brush;
			if (type->raw_brush) {
				brush = type->raw_brush;
			} else {
				brush = type->raw_brush = newd RAWBrush(type->id);
				type->has_raw = true;
				brushes.addBrush(brush);
			}

			if (type->doodad_brush == nullptr && !category.isTrivial()) {
				type->doodad_brush = brush;
			}

			brush->flagAsVisible();
			tempBrushVector.push_back(brush);
			category.tileset.previousId = id;
		}

		auto insertPosition = category.brushlist.end();
		if (!preserveStoredOrder) {
			insertPosition = FindInsertPosition(category.brushlist, afterBrushName);
		}
		category.brushlist.insert(insertPosition, tempBrushVector.begin(), tempBrushVector.end());
		return true;
	}
}

Tileset::Tileset(Brushes &brushes, const std::string &name) :
	name(name),
	brushes(brushes) {
	////
}

Tileset::~Tileset() {
	for (TilesetCategoryArray::iterator iter = categories.begin(); iter != categories.end(); ++iter) {
		delete *iter;
	}
}

void Tileset::clear() {
	for (TilesetCategoryArray::iterator iter = categories.begin(); iter != categories.end(); ++iter) {
		(*iter)->brushlist.clear();
	}
}

bool Tileset::containsBrush(Brush* brush) const {
	for (TilesetCategoryArray::const_iterator iter = categories.begin(); iter != categories.end(); ++iter) {
		if ((*iter)->containsBrush(brush)) {
			return true;
		}
	}

	return false;
}

TilesetCategory* Tileset::getCategory(TilesetCategoryType type) {
	ASSERT(type >= TILESET_UNKNOWN && type <= TILESET_HOUSE);
	for (TilesetCategoryArray::iterator iter = categories.begin(); iter != categories.end(); ++iter) {
		if ((*iter)->getType() == type) {
			return *iter;
		}
	}
	TilesetCategory* tsc = newd TilesetCategory(*this, type);
	categories.push_back(tsc);
	return tsc;
}

bool TilesetCategory::containsBrush(Brush* brush) const {
	for (std::vector<Brush*>::const_iterator iter = brushlist.begin(); iter != brushlist.end(); ++iter) {
		if (*iter == brush) {
			return true;
		}
	}

	return false;
}

const TilesetCategory* Tileset::getCategory(TilesetCategoryType type) const {
	ASSERT(type >= TILESET_UNKNOWN && type <= TILESET_HOUSE);
	for (TilesetCategoryArray::const_iterator iter = categories.begin(); iter != categories.end(); ++iter) {
		if ((*iter)->getType() == type) {
			return *iter;
		}
	}
	return nullptr;
}

void Tileset::loadCategory(pugi::xml_node node, wxArrayString &warnings) {
	TilesetCategory* category = nullptr;
	TilesetCategory* subCategory = nullptr;

	const std::string nodeName = as_lower_str(node.name());
	TilesetCategoryType primaryType = TILESET_UNKNOWN;
	TilesetCategoryType secondaryType = TILESET_UNKNOWN;
	bool hasSecondaryType = false;
	if (ResolveTilesetCategoryTypes(nodeName, primaryType, hasSecondaryType, secondaryType)) {
		category = getCategory(primaryType);
		if (hasSecondaryType) {
			subCategory = getCategory(secondaryType);
		}
	} else if (nodeName == "raw") {
		category = getCategory(TILESET_RAW);
	} else if (nodeName == "monsters") {
		category = getCategory(TILESET_MONSTER);
		for (pugi::xml_node brushNode = node.first_child(); brushNode; brushNode = brushNode.next_sibling()) {
			const std::string &brushName = as_lower_str(brushNode.name());
			if (brushName != "monster") {
				continue;
			}

			pugi::xml_attribute attribute;
			if (!(attribute = brushNode.attribute("name"))) {
				warnings.push_back("Couldn't read monster name tag of monster tileset");
				continue;
			}

			const std::string &monsterName = attribute.as_string();
			MonsterType* ctype = g_monsters[monsterName];
			if (ctype) {
				MonsterBrush* brush;
				if (ctype->brush) {
					brush = ctype->brush;
				} else {
					brush = ctype->brush = newd MonsterBrush(ctype);
					brushes.addBrush(brush);
				}
				brush->flagAsVisible();
				category->brushlist.push_back(brush);
			} else {
				warnings.push_back(wxString("Unknown monster type \"") << wxstr(monsterName) << "\"");
			}
		}
	} else if (nodeName == "npcs") {
		category = getCategory(TILESET_NPC);
		for (pugi::xml_node brushNode = node.first_child(); brushNode; brushNode = brushNode.next_sibling()) {
			const std::string &brushName = as_lower_str(brushNode.name());
			if (brushName != "npc") {
				continue;
			}

			pugi::xml_attribute attribute;
			if (!(attribute = brushNode.attribute("name"))) {
				warnings.push_back("Couldn't read npc name tag of npc tileset");
				continue;
			}

			const std::string &npcName = attribute.as_string();
			NpcType* npcType = g_npcs[npcName];
			if (npcType) {
				NpcBrush* brush;
				if (npcType->brush) {
					brush = npcType->brush;
				} else {
					brush = npcType->brush = newd NpcBrush(npcType);
					brushes.addBrush(brush);
				}
				brush->flagAsVisible();
				category->brushlist.push_back(brush);
			} else {
				warnings.push_back(wxString("Unknown npc type \"") << wxstr(npcName) << "\"");
			}
		}
	}

	if (!category) {
		return;
	}

	for (pugi::xml_node brushNode = node.first_child(); brushNode; brushNode = brushNode.next_sibling()) {
		category->loadBrush(brushNode, warnings);
		if (subCategory) {
			subCategory->loadBrush(brushNode, warnings);
		}
	}
}

void Tileset::loadFromStorage(const TilesetStorageRecord &storage, wxArrayString &warnings) {
	paletteGroupName = storage.paletteGroupName.ToStdString();
	paletteGroupRuntimeFamily = storage.paletteGroupRuntimeFamily.ToStdString();
	paletteGroupSortOrder = storage.paletteGroupSortOrder;

	for (const TilesetSectionRecord &section : storage.sections) {
		const std::string sectionType = as_lower_str(section.sectionType.ToStdString());
		TilesetCategoryType primaryType = TILESET_UNKNOWN;
		TilesetCategoryType secondaryType = TILESET_UNKNOWN;
		bool hasSecondaryType = false;
		if (!ResolveTilesetCategoryTypes(sectionType, primaryType, hasSecondaryType, secondaryType)) {
			warnings.push_back("Unsupported SQLite tileset section \"" + section.sectionType + "\" in tileset \"" + wxString::FromUTF8(name.c_str()) + "\".");
			continue;
		}

		TilesetCategory* category = getCategory(primaryType);
		TilesetCategory* subCategory = hasSecondaryType ? getCategory(secondaryType) : nullptr;
		for (const TilesetEntryRecord &entry : section.entries) {
			category->loadEntry(entry, warnings, true);
			if (subCategory) {
				subCategory->loadEntry(entry, warnings, true);
			}
		}
	}
}

//

TilesetCategory::TilesetCategory(Tileset &parent, TilesetCategoryType type) :
	type(type), tileset(parent) {
	ASSERT(type >= TILESET_UNKNOWN && type <= TILESET_HOUSE);
}

TilesetCategory::~TilesetCategory() {
	ASSERT(type >= TILESET_UNKNOWN && type <= TILESET_HOUSE);
}

bool TilesetCategory::isTrivial() const {
	return (type == TILESET_ITEM) || (type == TILESET_RAW);
}

void TilesetCategory::loadBrush(pugi::xml_node node, wxArrayString &warnings) {
	pugi::xml_attribute attribute;

	std::string brushName = node.attribute("after").as_string();
	if ((attribute = node.attribute("afteritem"))) {
		const ItemType &type = g_items.getItemType(attribute.as_uint());
		if (type.id != 0) {
			brushName = type.raw_brush ? type.raw_brush->getName() : std::string();
		}
	}

	const std::string &nodeName = as_lower_str(node.name());
	if (nodeName == "brush") {
		if (!(attribute = node.attribute("name"))) {
			return;
		}

		Brush* brush = tileset.brushes.getBrush(attribute.as_string());
		if (brush) {
			auto insertPosition = brushlist.end();
			if (!brushName.empty()) {
				for (auto itt = brushlist.begin(); itt != brushlist.end(); ++itt) {
					if ((*itt)->getName() == brushName) {
						insertPosition = ++itt;
						break;
					}
				}
			}
			brush->flagAsVisible();
			brushlist.insert(insertPosition, brush);
		} else {
			warnings.push_back("Brush \"" + wxString(attribute.as_string(), wxConvUTF8) + "\" doesn't exist.");
		}
	} else if (nodeName == "item") {
		uint16_t fromId = 0, toId = 0;
		if (!(attribute = node.attribute("id"))) {
			if (!(attribute = node.attribute("fromid"))) {
				warnings.push_back("Couldn't read raw ids.");
			}
			toId = node.attribute("toid").as_uint();
		}

		fromId = attribute.as_uint();
		toId = std::max<uint16_t>(fromId, toId);

		std::vector<Brush*> tempBrushVector;
		for (uint16_t id = fromId; id <= toId; ++id) {
			const auto &type = g_items.getRawItemType(id);
			// Ignore item if not exist
			if (!type || type->id == 0) {
				continue;
			}

			RAWBrush* brush;
			if (type->raw_brush) {
				brush = type->raw_brush;
			} else {
				brush = type->raw_brush = newd RAWBrush(type->id);
				type->has_raw = true;
				tileset.brushes.addBrush(brush); // This will take care of cleaning up afterwards
			}

			if (type->doodad_brush == nullptr && !isTrivial()) {
				type->doodad_brush = brush;
			}

			brush->flagAsVisible();
			tempBrushVector.push_back(brush);

			tileset.previousId = id;
		}

		auto insertPosition = brushlist.end();
		if (!brushName.empty()) {
			for (auto itt = brushlist.begin(); itt != brushlist.end(); ++itt) {
				if ((*itt)->getName() == brushName) {
					insertPosition = ++itt;
					break;
				}
			}
		}
		brushlist.insert(insertPosition, tempBrushVector.begin(), tempBrushVector.end());
	}
}

void TilesetCategory::loadEntry(const TilesetEntryRecord &entry, wxArrayString &warnings, bool preserveStoredOrder) {
	if (entry.entryKind.IsSameAs("brush", false)) {
		const std::string afterBrushName = preserveStoredOrder ? std::string() : ResolveAfterBrushName(entry, preserveStoredOrder);
		LoadBrushEntry(tileset.brushes, *this, entry, warnings, preserveStoredOrder, afterBrushName);
		return;
	}

	if (entry.entryKind.IsSameAs("item", false)) {
		const std::string afterBrushName = preserveStoredOrder ? std::string() : ResolveAfterBrushName(entry, preserveStoredOrder);
		LoadItemEntry(tileset.brushes, *this, entry, warnings, preserveStoredOrder, afterBrushName);
		return;
	}
}
