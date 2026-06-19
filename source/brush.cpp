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

#include "brush_database.h"
#include "brush.h"
#include "carpet_brush.h"
#include "monster_brush.h"
#include "npc_brush.h"
#include "doodad_brush.h"
#include "ground_brush.h"
#include "house_brush.h"
#include "house_exit_brush.h"
#include "raw_brush.h"
#include "spawn_monster_brush.h"
#include "spawn_npc_brush.h"
#include "table_brush.h"
#include "wall_brush.h"
#include "waypoint_brush.h"
#include "zone_brush.h"

#include "settings.h"

#include "sprites.h"

#include "item.h"
#include "complexitem.h"
#include "monsters.h"
#include "monster.h"
#include "npcs.h"
#include "npc.h"
#include "map.h"

#include "gui.h"

#include <sstream>

Brushes g_brushes;

namespace {
	void AppendStringAttribute(pugi::xml_node node, const char* name, const wxString &value) {
		if (!value.IsEmpty()) {
			node.append_attribute(name).set_value(value.utf8_str());
		}
	}

	void AppendStringAttribute(pugi::xml_node node, const char* name, const std::string &value) {
		if (!value.empty()) {
			node.append_attribute(name).set_value(value.c_str());
		}
	}

	void AppendStringAttribute(pugi::xml_node node, const char* name, const char* value) {
		if (value && value[0] != '\0') {
			node.append_attribute(name).set_value(value);
		}
	}

	void AppendIntAttribute(pugi::xml_node node, const char* name, int value) {
		node.append_attribute(name).set_value(value);
	}

	void AppendBoolAttribute(pugi::xml_node node, const char* name, bool value) {
		node.append_attribute(name).set_value(value ? 1 : 0);
	}

	bool LoadBrushStoragesForType(const wxString &type, std::vector<BrushStorageRecord> &outStorages, wxString &error) {
		std::vector<BrushRecord> records;
		if (!g_brush_database.listBrushesByType(type, records)) {
			error = g_brush_database.getLastError();
			return false;
		}

		for (const BrushRecord &record : records) {
			BrushStorageRecord storage;
			if (!g_brush_database.getCompleteBrushById(record.id, storage)) {
				error = g_brush_database.getLastError();
				return false;
			}
			outStorages.push_back(std::move(storage));
		}

		return true;
	}

	bool FetchBorderSetStorage(int64_t borderSetId, BorderSetStorageRecord &outStorage, wxString &error) {
		outStorage = BorderSetStorageRecord();
		if (!g_brush_database.getBorderSetById(borderSetId, outStorage.borderSet)) {
			error = g_brush_database.getLastError();
			return false;
		}
		if (!g_brush_database.getBorderSetItems(borderSetId, outStorage.items)) {
			error = g_brush_database.getLastError();
			return false;
		}
		return true;
	}

	void AppendBorderSetItems(pugi::xml_node node, const std::vector<BorderSetItemRecord> &items) {
		for (const BorderSetItemRecord &item : items) {
			pugi::xml_node itemNode = node.append_child("borderitem");
			AppendStringAttribute(itemNode, "edge", item.edge);
			AppendIntAttribute(itemNode, "item", item.itemId);
		}
	}

	void AppendGroundCaseConditionNode(pugi::xml_node conditionsNode, const GroundBorderCaseConditionRecord &condition) {
		pugi::xml_node node = conditionsNode.append_child(condition.conditionType.ToStdString().c_str());
		if (condition.conditionType == "match_group") {
			AppendIntAttribute(node, "group", condition.matchValue);
		} else {
			AppendIntAttribute(node, "id", condition.matchValue);
		}
		AppendStringAttribute(node, "edge", condition.edge);
	}

	void AppendGroundCaseActionNode(pugi::xml_node actionsNode, const GroundBorderCaseActionRecord &action) {
		pugi::xml_node node = actionsNode.append_child(action.actionType.ToStdString().c_str());
		if (action.actionType != "delete_borders") {
			AppendIntAttribute(node, "id", action.targetValue);
		}
		if (!action.edge.IsEmpty()) {
			AppendStringAttribute(node, "edge", action.edge);
		}
		if (action.actionType != "delete_borders") {
			AppendIntAttribute(node, "with", action.replacementValue);
		}
	}

	void AppendGroundSpecificCases(pugi::xml_node borderNode, const std::vector<GroundBorderCaseRecord> &cases) {
		for (const GroundBorderCaseRecord &caseRecord : cases) {
			pugi::xml_node specificNode = borderNode.append_child("specific");
			if (!caseRecord.conditions.empty()) {
				pugi::xml_node conditionsNode = specificNode.append_child("conditions");
				for (const GroundBorderCaseConditionRecord &condition : caseRecord.conditions) {
					AppendGroundCaseConditionNode(conditionsNode, condition);
				}
			}
			if (!caseRecord.actions.empty()) {
				pugi::xml_node actionsNode = specificNode.append_child("actions");
				for (const GroundBorderCaseActionRecord &action : caseRecord.actions) {
					AppendGroundCaseActionNode(actionsNode, action);
				}
			}
		}
	}

	bool AppendGroundBorderNode(pugi::xml_node brushNode, const GroundBrushBorderRecord &borderRecord, uint16_t fallbackGroundEquivalent, const wxString &brushName, wxString &error) {
		BorderSetStorageRecord borderSetStorage;
		if (!FetchBorderSetStorage(borderRecord.borderSetId, borderSetStorage, error)) {
			return false;
		}

		const bool optionalBorder = borderRecord.borderRole == "optional";
		pugi::xml_node borderNode = brushNode.append_child(optionalBorder ? "optional" : "border");
		if (!optionalBorder) {
			if (!borderRecord.align.IsEmpty() && borderRecord.align != "optional") {
				AppendStringAttribute(borderNode, "align", borderRecord.align);
			}
			if (borderRecord.targetMode == "none") {
				AppendStringAttribute(borderNode, "to", "none");
			} else if (borderRecord.targetMode == "brush") {
				AppendStringAttribute(borderNode, "to", borderRecord.targetBrushName);
			}
			if (borderRecord.superBorder) {
				AppendBoolAttribute(borderNode, "super", true);
			}
		}

		const BorderSetRecord &borderSet = borderSetStorage.borderSet;
		if (borderSet.xmlBorderId > 0 && borderSet.borderScope == "global") {
			AppendIntAttribute(borderNode, "id", borderSet.xmlBorderId);
		} else {
			int groundEquivalent = borderSet.groundEquivalent;
			if (groundEquivalent <= 0) {
				groundEquivalent = fallbackGroundEquivalent;
			}
			if (groundEquivalent <= 0) {
				error = wxString::Format(
					"Failed to inline border set %lld for ground brush \"%s\": missing ground_equivalent (borderSet.groundEquivalent=%d).",
					static_cast<long long>(borderSet.id),
					brushName,
					borderSet.groundEquivalent
				);
				return false;
			}
			AppendIntAttribute(borderNode, "ground_equivalent", groundEquivalent);
			if (borderSet.borderGroup > 0) {
				AppendIntAttribute(borderNode, "group", borderSet.borderGroup);
			}
			if (!borderSet.borderType.IsEmpty() && borderSet.borderType != "normal") {
				AppendStringAttribute(borderNode, "type", borderSet.borderType);
			}
			AppendBorderSetItems(borderNode, borderSetStorage.items);
		}

		AppendGroundSpecificCases(borderNode, borderRecord.cases);
		return true;
	}

	void AppendWallItemsAndDoors(pugi::xml_node node, const WallPartRecord &part) {
		for (const WallPartItemRecord &item : part.items) {
			pugi::xml_node itemNode = node.append_child("item");
			AppendIntAttribute(itemNode, "id", item.itemId);
			AppendIntAttribute(itemNode, "chance", item.chance);
		}
		for (const WallPartDoorRecord &door : part.doors) {
			pugi::xml_node doorNode = node.append_child("door");
			AppendIntAttribute(doorNode, "id", door.itemId);
			AppendStringAttribute(doorNode, "type", door.doorType);
			AppendBoolAttribute(doorNode, "open", door.isOpen);
			AppendBoolAttribute(doorNode, "hate", door.wallHateMe);
		}
	}

	void AppendDoodadAlternative(pugi::xml_node brushNode, const DoodadAlternativeRecord &alternative) {
		pugi::xml_node alternativeNode = brushNode.append_child("alternate");
		for (const DoodadSingleItemRecord &item : alternative.singleItems) {
			pugi::xml_node itemNode = alternativeNode.append_child("item");
			AppendIntAttribute(itemNode, "id", item.itemId);
			AppendIntAttribute(itemNode, "chance", item.chance);
		}
		for (const DoodadCompositeRecord &composite : alternative.composites) {
			pugi::xml_node compositeNode = alternativeNode.append_child("composite");
			AppendIntAttribute(compositeNode, "chance", composite.chance);
			for (const DoodadCompositeTileRecord &tile : composite.tiles) {
				pugi::xml_node tileNode = compositeNode.append_child("tile");
				AppendIntAttribute(tileNode, "x", tile.offsetX);
				AppendIntAttribute(tileNode, "y", tile.offsetY);
				AppendIntAttribute(tileNode, "z", tile.offsetZ);
				for (const DoodadCompositeTileItemRecord &item : tile.items) {
					pugi::xml_node itemNode = tileNode.append_child("item");
					AppendIntAttribute(itemNode, "id", item.itemId);
				}
			}
		}
	}

	template <typename NodeRecord, typename ItemRecord>
	void AppendAlignedNodes(pugi::xml_node brushNode, const char* containerName, const std::vector<NodeRecord> &nodes) {
		for (const NodeRecord &node : nodes) {
			pugi::xml_node containerNode = brushNode.append_child(containerName);
			AppendStringAttribute(containerNode, "align", node.align);
			for (const ItemRecord &item : node.items) {
				pugi::xml_node itemNode = containerNode.append_child("item");
				AppendIntAttribute(itemNode, "id", item.itemId);
				AppendIntAttribute(itemNode, "chance", item.chance);
			}
		}
	}

	void BuildBrushNode(pugi::xml_document &doc, const BrushStorageRecord &storage) {
		const BrushRecord &brush = storage.brush;
		pugi::xml_node brushNode = doc.append_child("brush");
		AppendStringAttribute(brushNode, "name", brush.name);
		AppendStringAttribute(brushNode, "type", brush.type);
		if (brush.serverLookId > 0) {
			AppendIntAttribute(brushNode, "server_lookid", brush.serverLookId);
		} else if (brush.lookId > 0) {
			AppendIntAttribute(brushNode, "lookid", brush.lookId);
		}

		if (brush.type == "ground") {
			AppendIntAttribute(brushNode, "z-order", brush.zOrder);
			AppendBoolAttribute(brushNode, "randomize", brush.randomize);
			if (brush.soloOptional) {
				AppendBoolAttribute(brushNode, "solo_optional", true);
			}
			for (const BrushItemRecord &item : storage.items) {
				pugi::xml_node itemNode = brushNode.append_child("item");
				AppendIntAttribute(itemNode, "id", item.itemId);
				AppendIntAttribute(itemNode, "chance", item.chance);
			}
			uint16_t fallbackGroundEquivalent = 0;
			if (!storage.items.empty()) {
				fallbackGroundEquivalent = static_cast<uint16_t>(storage.items.front().itemId);
			}
			for (const GroundBrushBorderRecord &border : storage.borders) {
				wxString error;
				if (!AppendGroundBorderNode(brushNode, border, fallbackGroundEquivalent, brush.name, error)) {
					throw std::runtime_error(error.ToStdString());
				}
			}
			for (const BrushLinkRecord &link : storage.links) {
				pugi::xml_node linkNode = brushNode.append_child(link.relationType.ToStdString().c_str());
				AppendStringAttribute(linkNode, "name", link.targetBrushName);
			}
			return;
		}

		if (brush.type == "wall") {
			if (brush.draggable) {
				AppendBoolAttribute(brushNode, "draggable", true);
			}
			if (brush.onBlocking) {
				AppendBoolAttribute(brushNode, "on_blocking", true);
			}
			if (brush.onDuplicate) {
				AppendBoolAttribute(brushNode, "on_duplicate", true);
			}
			if (brush.redoBorders) {
				AppendBoolAttribute(brushNode, "redo_borders", true);
			}
			if (brush.removeOptionalBorder) {
				AppendBoolAttribute(brushNode, "remove_optional_border", true);
			}
			if (brush.oneSize) {
				AppendBoolAttribute(brushNode, "one_size", true);
			}
			if (brush.thickness > 0 || brush.thicknessCeiling > 0) {
				AppendStringAttribute(brushNode, "thickness", wxString::Format("%d/%d", brush.thickness, brush.thicknessCeiling));
			}

			std::map<wxString, pugi::xml_node> wallNodesByType;
			std::set<wxString> redirectTargets;
			for (const BrushLinkRecord &link : storage.links) {
				if (link.relationType == "redirect") {
					redirectTargets.insert(link.targetBrushName);
				}
			}

			for (const WallPartRecord &part : storage.wallParts) {
				if (part.partType.StartsWith("alternate/")) {
					pugi::xml_node alternateNode = brushNode.append_child("alternate");
					AppendWallItemsAndDoors(alternateNode, part);
					continue;
				}

				const wxString alternateMarker = "/alternate/";
				const int alternateIndex = part.partType.Find(alternateMarker);
				if (alternateIndex != wxNOT_FOUND) {
					const wxString basePartType = part.partType.SubString(0, alternateIndex - 1);
					pugi::xml_node wallNode = wallNodesByType[basePartType];
					if (!wallNode) {
						wallNode = brushNode.append_child("wall");
						AppendStringAttribute(wallNode, "type", basePartType);
						wallNodesByType[basePartType] = wallNode;
					}
					pugi::xml_node alternateNode = wallNode.append_child("alternate");
					AppendWallItemsAndDoors(alternateNode, part);
					continue;
				}

				pugi::xml_node wallNode = brushNode.append_child("wall");
				AppendStringAttribute(wallNode, "type", part.partType);
				AppendWallItemsAndDoors(wallNode, part);
				wallNodesByType[part.partType] = wallNode;
			}

			std::set<wxString> emittedFriends;
			for (const BrushLinkRecord &link : storage.links) {
				if (link.relationType != "friend" && link.relationType != "redirect") {
					continue;
				}
				if (!emittedFriends.insert(link.targetBrushName).second) {
					continue;
				}
				pugi::xml_node friendNode = brushNode.append_child("friend");
				AppendStringAttribute(friendNode, "name", link.targetBrushName);
				if (redirectTargets.find(link.targetBrushName) != redirectTargets.end()) {
					AppendBoolAttribute(friendNode, "redirect", true);
				}
			}
			return;
		}

		if (brush.type == "doodad") {
			if (brush.onBlocking) {
				AppendBoolAttribute(brushNode, "on_blocking", true);
			}
			if (brush.onDuplicate) {
				AppendBoolAttribute(brushNode, "on_duplicate", true);
			}
			if (brush.redoBorders) {
				AppendBoolAttribute(brushNode, "redo_borders", true);
			}
			if (brush.removeOptionalBorder) {
				AppendBoolAttribute(brushNode, "remove_optional_border", true);
			}
			if (brush.oneSize) {
				AppendBoolAttribute(brushNode, "one_size", true);
			}
			if (brush.draggable) {
				AppendBoolAttribute(brushNode, "draggable", true);
			}
			if (brush.thickness > 0 || brush.thicknessCeiling > 0) {
				AppendStringAttribute(brushNode, "thickness", wxString::Format("%d/%d", brush.thickness, brush.thicknessCeiling));
			}
			for (const DoodadAlternativeRecord &alternative : storage.doodadAlternatives) {
				AppendDoodadAlternative(brushNode, alternative);
			}
			return;
		}

		if (brush.type == "carpet") {
			AppendAlignedNodes<CarpetNodeRecord, CarpetNodeItemRecord>(brushNode, "carpet", storage.carpetNodes);
			return;
		}

		if (brush.type == "table") {
			AppendAlignedNodes<TableNodeRecord, TableNodeItemRecord>(brushNode, "table", storage.tableNodes);
		}
	}

	bool LoadGlobalBordersFromDatabase(Brushes &brushes, wxArrayString &warnings, wxString &error) {
		std::vector<BorderSetRecord> borderSets;
		if (!g_brush_database.listBorderSetsByScope("global", borderSets)) {
			error = g_brush_database.getLastError();
			return false;
		}

		for (const BorderSetRecord &borderSet : borderSets) {
			if (borderSet.xmlBorderId <= 0) {
				continue;
			}

			std::vector<BorderSetItemRecord> items;
			if (!g_brush_database.getBorderSetItems(borderSet.id, items)) {
				error = g_brush_database.getLastError();
				return false;
			}

			pugi::xml_document doc;
			pugi::xml_node borderNode = doc.append_child("border");
			AppendIntAttribute(borderNode, "id", borderSet.xmlBorderId);
			if (borderSet.borderGroup > 0) {
				AppendIntAttribute(borderNode, "group", borderSet.borderGroup);
			}
			if (!borderSet.borderType.IsEmpty() && borderSet.borderType != "normal") {
				AppendStringAttribute(borderNode, "type", borderSet.borderType);
			}
			AppendBorderSetItems(borderNode, items);
			if (!brushes.unserializeBorder(borderNode, warnings)) {
				error = "Failed to load global border from SQLite.";
				return false;
			}
		}

		return true;
	}

	bool LoadGroundBrushIdsFromDatabase(std::vector<int64_t> &outBrushIds, wxString &error) {
		outBrushIds.clear();
		std::vector<BrushRecord> groundBrushes;
		if (!g_brush_database.listBrushesByType("ground", groundBrushes)) {
			error = g_brush_database.getLastError();
			return false;
		}

		outBrushIds.reserve(groundBrushes.size());
		for (const BrushRecord &brush : groundBrushes) {
			outBrushIds.push_back(brush.id);
		}

		return true;
	}

	void ResetGroundBrushRuntimeState(Brushes &brushes) {
		for (const auto &entry : brushes.getMap()) {
			if (Brush* brush = entry.second; brush && brush->isGround()) {
				brush->asGround()->resetRuntimeState();
			}
		}
	}
}

Brushes::Brushes() {
	////
}

Brushes::~Brushes() {
	////
}

void Brushes::clear() {
	for (auto brushEntry : brushes) {
		delete brushEntry.second;
	}
	brushes.clear();

	for (auto borderEntry : borders) {
		delete borderEntry.second;
	}
	borders.clear();
}

void Brushes::init() {
	addBrush(g_gui.optional_brush = newd OptionalBorderBrush());
	addBrush(g_gui.eraser = newd EraserBrush());
	addBrush(g_gui.spawn_brush = newd SpawnMonsterBrush());
	addBrush(g_gui.spawn_npc_brush = newd SpawnNpcBrush());
	addBrush(g_gui.normal_door_brush = newd DoorBrush(WALL_DOOR_NORMAL));
	addBrush(g_gui.locked_door_brush = newd DoorBrush(WALL_DOOR_LOCKED));
	addBrush(g_gui.magic_door_brush = newd DoorBrush(WALL_DOOR_MAGIC));
	addBrush(g_gui.quest_door_brush = newd DoorBrush(WALL_DOOR_QUEST));
	addBrush(g_gui.hatch_door_brush = newd DoorBrush(WALL_HATCH_WINDOW));
	addBrush(g_gui.window_door_brush = newd DoorBrush(WALL_WINDOW));
	addBrush(g_gui.house_brush = newd HouseBrush());
	addBrush(g_gui.house_exit_brush = newd HouseExitBrush());
	addBrush(g_gui.waypoint_brush = newd WaypointBrush());

	addBrush(g_gui.pz_brush = newd FlagBrush(TILESTATE_PROTECTIONZONE));
	addBrush(g_gui.rook_brush = newd FlagBrush(TILESTATE_NOPVP));
	addBrush(g_gui.nolog_brush = newd FlagBrush(TILESTATE_NOLOGOUT));
	addBrush(g_gui.pvp_brush = newd FlagBrush(TILESTATE_PVPZONE));
	addBrush(g_gui.zone_brush = newd ZoneBrush());

	GroundBrush::init();
	WallBrush::init();
	TableBrush::init();
	CarpetBrush::init();
}

bool Brushes::unserializeBrush(pugi::xml_node node, wxArrayString &warnings) {
	pugi::xml_attribute attribute;
	if (!(attribute = node.attribute("name"))) {
		warnings.push_back("Brush node without name.");
		return false;
	}

	const std::string &brushName = attribute.as_string();
	if (brushName == "all" || brushName == "none") {
		warnings.push_back(wxString("Using reserved brushname \"") << wxstr(brushName) << "\".");
		return false;
	}

	Brush* brush = getBrush(brushName);
	if (!brush) {
		if (!(attribute = node.attribute("type"))) {
			warnings.push_back("Couldn't read brush type");
			return false;
		}

		const std::string brushType = attribute.as_string();
		if (brushType == "border" || brushType == "ground") {
			brush = newd GroundBrush();
		} else if (brushType == "wall") {
			brush = newd WallBrush();
		} else if (brushType == "wall decoration") {
			brush = newd WallDecorationBrush();
		} else if (brushType == "carpet") {
			brush = newd CarpetBrush();
		} else if (brushType == "table") {
			brush = newd TableBrush();
		} else if (brushType == "doodad") {
			brush = newd DoodadBrush();
		} else {
			warnings.push_back(wxString("Unknown brush type ") << wxstr(brushType));
			return false;
		}

		ASSERT(brush);
		brush->setName(brushName);
	}

	if (!node.first_child()) {
		addBrush(brush);
		return true;
	}

	wxArrayString subWarnings;
	brush->load(node, subWarnings);

	if (!subWarnings.empty()) {
		warnings.push_back(wxString("Errors while loading brush \"") << wxstr(brush->getName()) << "\"");
		warnings.insert(warnings.end(), subWarnings.begin(), subWarnings.end());
	}

	if (brush->getName() == "all" || brush->getName() == "none") {
		warnings.push_back(wxString("Using reserved brushname '") << wxstr(brush->getName()) << "'.");
		delete brush;
		return false;
	}

	Brush* otherBrush = getBrush(brush->getName());
	if (otherBrush) {
		if (otherBrush != brush) {
			warnings.push_back(wxString("Duplicate brush name ") << wxstr(brush->getName()) << ". Undefined behaviour may ensue.");
		} else {
			// Don't insert
			return true;
		}
	}

	addBrush(brush);
	return true;
}

bool Brushes::unserializeBorder(pugi::xml_node node, wxArrayString &warnings) {
	pugi::xml_attribute attribute = node.attribute("id");
	if (!attribute) {
		warnings.push_back("Couldn't read border id node");
		return false;
	}

	uint32_t id = attribute.as_uint();
	if (borders[id]) {
		warnings.push_back("Border ID " + std::to_string(id) + " already exists");
		return false;
	}

	AutoBorder* border = newd AutoBorder(id);
	border->load(node, warnings);
	borders[id] = border;
	return true;
}

bool Brushes::loadFromDatabase(wxArrayString &warnings) {
	if (!g_brush_database.isOpen()) {
		warnings.push_back("SQLite brush load skipped because the brush database is not open.");
		return false;
	}

	wxString error;
	if (!LoadGlobalBordersFromDatabase(*this, warnings, error)) {
		if (!error.IsEmpty()) {
			warnings.push_back("Failed to load global borders from SQLite: " + error);
		}
		return false;
	}

	std::vector<BrushStorageRecord> storages;
	static const wxString supportedTypes[] = { "ground", "wall", "doodad", "carpet", "table" };
	for (const wxString &type : supportedTypes) {
		if (!LoadBrushStoragesForType(type, storages, error)) {
			if (!error.IsEmpty()) {
				warnings.push_back("Failed to load SQLite brushes of type \"" + type + "\": " + error);
			}
			return false;
		}
	}

	size_t shellCount = 0;
	for (const BrushStorageRecord &storage : storages) {
		pugi::xml_document shellDoc;
		pugi::xml_node shellNode = shellDoc.append_child("brush");
		AppendStringAttribute(shellNode, "name", storage.brush.name);
		AppendStringAttribute(shellNode, "type", storage.brush.type);
		if (!unserializeBrush(shellNode, warnings)) {
			warnings.push_back("Failed to create SQLite brush shell for \"" + storage.brush.name + "\".");
			continue;
		}
		++shellCount;
	}

	size_t hydratedCount = 0;
	for (const BrushStorageRecord &storage : storages) {
		try {
			pugi::xml_document brushDoc;
			BuildBrushNode(brushDoc, storage);
			if (!unserializeBrush(brushDoc.child("brush"), warnings)) {
				warnings.push_back("Failed to hydrate SQLite brush \"" + storage.brush.name + "\".");
				continue;
			}
			++hydratedCount;
		} catch (const std::exception &ex) {
			warnings.push_back("Failed to build SQLite brush \"" + storage.brush.name + "\": " + wxString::FromUTF8(ex.what()));
			continue;
		}
	}

	if (shellCount == 0) {
		warnings.push_back("SQLite brush load did not create any brush shells.");
		return false;
	}

	if (hydratedCount != storages.size()) {
		warnings.push_back(wxString::Format(
			"SQLite brush load hydrated %zu of %zu brushes; keeping shell brushes for the remaining entries.",
			hydratedCount,
			storages.size()
		));
	}

	spdlog::info(
		"Loaded {} SQLite brush shells and fully hydrated {} brushes from the materials database",
		shellCount,
		hydratedCount
	);
	return true;
}

bool Brushes::reloadBrushFromDatabase(int64_t brushId, wxArrayString &warnings, wxString &error) {
	error.clear();

	if (!g_brush_database.isOpen()) {
		error = "SQLite brush database is not open.";
		return false;
	}

	BrushStorageRecord storage;
	if (!g_brush_database.getCompleteBrushById(brushId, storage)) {
		error = g_brush_database.getLastError();
		return false;
	}

	try {
		pugi::xml_document brushDoc;
		BuildBrushNode(brushDoc, storage);
		if (!unserializeBrush(brushDoc.child("brush"), warnings)) {
			error = "Failed to hydrate runtime brush from SQLite storage.";
			return false;
		}
	} catch (const std::exception &ex) {
		error = wxString::FromUTF8(ex.what());
		return false;
	}

	return true;
}

bool Brushes::reloadBrushFromStorage(const BrushStorageRecord &storage, wxArrayString &warnings, wxString &error) {
	error.clear();

	try {
		pugi::xml_document brushDoc;
		BuildBrushNode(brushDoc, storage);
		if (!unserializeBrush(brushDoc.child("brush"), warnings)) {
			error = "Failed to hydrate runtime brush from in-memory storage.";
			return false;
		}
	} catch (const std::exception &ex) {
		error = wxString::FromUTF8(ex.what());
		return false;
	}

	return true;
}

bool Brushes::buildBrushXmlFromStorage(const BrushStorageRecord &storage, wxString &outXml, wxString &error) const {
	outXml.clear();
	error.clear();
	try {
		pugi::xml_document brushDoc;
		BuildBrushNode(brushDoc, storage);
		std::ostringstream stream;
		brushDoc.save(stream, "\t", pugi::format_default, pugi::encoding_utf8);
		outXml = wxString::FromUTF8(stream.str());
	} catch (const std::exception &ex) {
		error = wxString::FromUTF8(ex.what());
		return false;
	}
	return true;
}

bool Brushes::reloadBorderSetFromDatabase(int64_t borderSetId, wxArrayString &warnings, wxString &error) {
	error.clear();

	if (!g_brush_database.isOpen()) {
		error = "SQLite brush database is not open.";
		return false;
	}

	BorderSetStorageRecord borderSetStorage;
	if (!FetchBorderSetStorage(borderSetId, borderSetStorage, error)) {
		return false;
	}

	const BorderSetRecord &borderSet = borderSetStorage.borderSet;
	if (borderSet.borderScope == "inline" && borderSet.ownerBrushId > 0) {
		return reloadBrushFromDatabase(borderSet.ownerBrushId, warnings, error);
	}

	if (borderSet.borderScope != "global" || borderSet.xmlBorderId <= 0) {
		error = "Unsupported border set runtime refresh configuration.";
		return false;
	}

	std::vector<int64_t> groundBrushIds;
	if (!LoadGroundBrushIdsFromDatabase(groundBrushIds, error)) {
		return false;
	}

	ResetGroundBrushRuntimeState(*this);
	for (auto &entry : borders) {
		delete entry.second;
	}
	borders.clear();

	if (!LoadGlobalBordersFromDatabase(*this, warnings, error)) {
		return false;
	}

	for (int64_t groundBrushId : groundBrushIds) {
		if (!reloadBrushFromDatabase(groundBrushId, warnings, error)) {
			return false;
		}
	}

	return true;
}

void Brushes::addBrush(Brush* brush) {
	if (brush) {
		brushes.insert(std::make_pair(brush->getName(), brush));
	}
}

bool Brushes::renameBrush(Brush* brush, const std::string &oldName, const std::string &newName) {
	if (!brush || oldName == newName) {
		return false;
	}

	const auto range = brushes.equal_range(oldName);
	for (auto it = range.first; it != range.second; ++it) {
		if (it->second == brush) {
			brushes.erase(it);
			brush->setName(newName);
			brushes.insert(std::make_pair(brush->getName(), brush));
			return true;
		}
	}

	brush->setName(newName);
	brushes.insert(std::make_pair(brush->getName(), brush));
	return true;
}

Brush* Brushes::getBrush(const std::string &name) const {
	auto it = brushes.find(name);
	if (it != brushes.end()) {
		return it->second;
	}
	return nullptr;
}

// Brush
uint32_t Brush::id_counter = 0;
Brush::Brush() :
	id(++id_counter), visible(false) {
	////
}

Brush::~Brush() {
	////
}

// TerrainBrush
TerrainBrush::TerrainBrush() :
	look_id(0), hate_friends(false) {
	////
}

TerrainBrush::~TerrainBrush() {
	////
}

bool TerrainBrush::friendOf(TerrainBrush* other) {
	uint32_t borderID = other->getID();
	for (uint32_t friendId : friends) {
		if (friendId == borderID) {
			// printf("%s is friend of %s\n", getName().c_str(), other->getName().c_str());
			return !hate_friends;
		} else if (friendId == 0xFFFFFFFF) {
			// printf("%s is generic friend of %s\n", getName().c_str(), other->getName().c_str());
			return !hate_friends;
		}
	}
	// printf("%s is enemy of %s\n", getName().c_str(), other->getName().c_str());
	return hate_friends;
}

//=============================================================================
// Flag brush
// draws pz etc.

FlagBrush::FlagBrush(uint32_t _flag) :
	flag(_flag) {
	////
}

FlagBrush::~FlagBrush() {
	////
}

std::string FlagBrush::getName() const {
	switch (flag) {
		case TILESTATE_PROTECTIONZONE:
			return "PZ brush (0x01)";
		case TILESTATE_NOPVP:
			return "No combat zone brush (0x04)";
		case TILESTATE_NOLOGOUT:
			return "No logout zone brush (0x08)";
		case TILESTATE_PVPZONE:
			return "PVP Zone brush (0x10)";
	}
	return "Unknown flag brush";
}

int FlagBrush::getLookID() const {
	switch (flag) {
		case TILESTATE_PROTECTIONZONE:
			return EDITOR_SPRITE_PZ_TOOL;
		case TILESTATE_NOPVP:
			return EDITOR_SPRITE_NOPVP_TOOL;
		case TILESTATE_NOLOGOUT:
			return EDITOR_SPRITE_NOLOG_TOOL;
		case TILESTATE_PVPZONE:
			return EDITOR_SPRITE_PVPZ_TOOL;
	}
	return 0;
}

bool FlagBrush::canDraw(BaseMap* map, const Position &position) const {
	Tile* tile = map->getTile(position);
	return tile && tile->hasGround();
}

void FlagBrush::undraw(BaseMap* map, Tile* tile) {
	tile->unsetMapFlags(flag);
}

void FlagBrush::draw(BaseMap* map, Tile* tile, void* parameter) {
	if (tile->hasGround()) {
		tile->setMapFlags(flag);
	}
}

//=============================================================================
// Door brush

DoorBrush::DoorBrush(DoorType _doortype) :
	doortype(_doortype) {
	////
}

DoorBrush::~DoorBrush() {
	////
}

std::string DoorBrush::getName() const {
	switch (doortype) {
		case WALL_DOOR_NORMAL:
			return "Normal door brush";
		case WALL_DOOR_LOCKED:
			return "Locked door brush";
		case WALL_DOOR_MAGIC:
			return "Magic door brush";
		case WALL_DOOR_QUEST:
			return "Quest door brush";
		case WALL_WINDOW:
			return "Window brush";
		case WALL_HATCH_WINDOW:
			return "Hatch window brush";
		default:
			return "Unknown door brush";
	}
}

int DoorBrush::getLookID() const {
	switch (doortype) {
		case WALL_DOOR_NORMAL:
			return EDITOR_SPRITE_DOOR_NORMAL;
		case WALL_DOOR_LOCKED:
			return EDITOR_SPRITE_DOOR_LOCKED;
		case WALL_DOOR_MAGIC:
			return EDITOR_SPRITE_DOOR_MAGIC;
		case WALL_DOOR_QUEST:
			return EDITOR_SPRITE_DOOR_QUEST;
		case WALL_WINDOW:
			return EDITOR_SPRITE_WINDOW_NORMAL;
		case WALL_HATCH_WINDOW:
			return EDITOR_SPRITE_WINDOW_HATCH;
		default:
			return EDITOR_SPRITE_DOOR_NORMAL;
	}
}

void DoorBrush::switchDoor(Item* item) {
	ASSERT(item);
	ASSERT(item->isBrushDoor());

	WallBrush* wb = item->getWallBrush();
	if (!wb) {
		return;
	}

	bool new_open = !item->isOpen();
	BorderType wall_alignment = item->getWallAlignment();
	DoorType doortype = WALL_UNDEFINED;

	for (std::vector<WallBrush::DoorType>::iterator iter = wb->door_items[wall_alignment].begin(); iter != wb->door_items[wall_alignment].end(); ++iter) {
		WallBrush::DoorType &dt = *iter;
		if (dt.id == item->getID()) {
			doortype = dt.type;
			break;
		}
	}
	if (doortype == WALL_UNDEFINED) {
		return;
	}

	for (std::vector<WallBrush::DoorType>::iterator iter = wb->door_items[wall_alignment].begin(); iter != wb->door_items[wall_alignment].end(); ++iter) {
		WallBrush::DoorType &dt = *iter;
		if (dt.type == doortype) {
			ASSERT(dt.id);
			const ItemType &type = g_items.getItemType(dt.id);
			ASSERT(type.id != 0);

			if (type.isOpen == new_open) {
				item->setID(dt.id);
				return;
			}
		}
	}
}

bool DoorBrush::canDraw(BaseMap* map, const Position &position) const {
	Tile* tile = map->getTile(position);
	if (!tile) {
		return false;
	}

	Item* item = tile->getWall();
	if (!item) {
		return false;
	}

	WallBrush* wb = item->getWallBrush();
	if (!wb) {
		return false;
	}

	BorderType wall_alignment = item->getWallAlignment();

	uint16_t discarded_id = 0; // The id of a discarded match
	bool close_match = false;

	bool open = false;
	if (item->isBrushDoor()) {
		open = item->isOpen();
	}

	WallBrush* test_brush = wb;
	do {
		for (std::vector<WallBrush::DoorType>::iterator iter = test_brush->door_items[wall_alignment].begin();
			 iter != test_brush->door_items[wall_alignment].end();
			 ++iter) {
			WallBrush::DoorType &dt = *iter;
			if (dt.type == doortype) {
				ASSERT(dt.id);
				const ItemType &type = g_items.getItemType(dt.id);
				ASSERT(type.id != 0);

				if (type.isOpen == open) {
					return true;
				} else if (!close_match) {
					discarded_id = dt.id;
					close_match = true;
				}
				if (!close_match && discarded_id == 0) {
					discarded_id = dt.id;
				}
			}
		}
		test_brush = test_brush->redirect_to;
	} while (test_brush != wb && test_brush != nullptr);
	// If we've found no perfect match, use a close-to perfect
	if (discarded_id) {
		return true;
	}
	return false;
}

void DoorBrush::undraw(BaseMap* map, Tile* tile) {
	for (ItemVector::iterator it = tile->items.begin(); it != tile->items.end(); ++it) {
		Item* item = *it;
		if (item->isBrushDoor()) {
			item->getWallBrush()->draw(map, tile, nullptr);
			if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
				tile->wallize(map);
			}
			return;
		}
	}
}

void DoorBrush::draw(BaseMap* map, Tile* tile, void* parameter) {
	for (ItemVector::iterator item_iter = tile->items.begin(); item_iter != tile->items.end();) {
		Item* item = *item_iter;
		if (!item->isWall()) {
			++item_iter;
			continue;
		}
		WallBrush* wb = item->getWallBrush();
		if (!wb) {
			++item_iter;
			continue;
		}

		BorderType wall_alignment = item->getWallAlignment();

		uint16_t discarded_id = 0; // The id of a discarded match
		bool close_match = false;
		bool perfect_match = false;

		bool open = false;
		if (parameter) {
			open = *reinterpret_cast<bool*>(parameter);
		}

		if (item->isBrushDoor()) {
			open = item->isOpen();
		}

		WallBrush* test_brush = wb;
		do {
			for (std::vector<WallBrush::DoorType>::iterator iter = test_brush->door_items[wall_alignment].begin();
				 iter != test_brush->door_items[wall_alignment].end();
				 ++iter) {
				WallBrush::DoorType &dt = *iter;
				if (dt.type == doortype) {
					ASSERT(dt.id);
					const ItemType &type = g_items.getItemType(dt.id);
					ASSERT(type.id != 0);

					if (type.isOpen == open) {
						item = transformItem(item, dt.id, tile);
						perfect_match = true;
						break;
					} else if (!close_match) {
						discarded_id = dt.id;
						close_match = true;
					}
					if (!close_match && discarded_id == 0) {
						discarded_id = dt.id;
					}
				}
			}
			test_brush = test_brush->redirect_to;
			if (perfect_match) {
				break;
			}
		} while (test_brush != wb && test_brush != nullptr);

		// If we've found no perfect match, use a close-to perfect
		if (!perfect_match && discarded_id) {
			item = transformItem(item, discarded_id, tile);
		}

		if (g_settings.getInteger(Config::AUTO_ASSIGN_DOORID) && tile->isHouseTile()) {
			Map* mmap = dynamic_cast<Map*>(map);
			Door* door = dynamic_cast<Door*>(item);
			if (mmap && door) {
				House* house = mmap->houses.getHouse(tile->getHouseID());
				ASSERT(house);
				Map* real_map = dynamic_cast<Map*>(map);
				if (real_map) {
					door->setDoorID(house->getEmptyDoorID());
				}
			}
		}

		// We need to consider decorations!
		while (true) {
			// Vector has been modified, before we can use the iterator again we need to find the wall item again
			item_iter = tile->items.begin();
			while (true) {
				if (item_iter == tile->items.end()) {
					return;
				}
				if (*item_iter == item) {
					++item_iter;
					if (item_iter == tile->items.end()) {
						return;
					}
					break;
				}
				++item_iter;
			}
			// Now it points to the correct item!

			item = *item_iter;
			if (item->isWall()) {
				WallBrush* brush = item->getWallBrush();
				if (brush && brush->isWallDecoration()) {
					// We got a decoration!
					for (std::vector<WallBrush::DoorType>::iterator it = brush->door_items[wall_alignment].begin(); it != brush->door_items[wall_alignment].end(); ++it) {
						WallBrush::DoorType &dt = (*it);
						if (dt.type == doortype) {
							ASSERT(dt.id);
							const ItemType &type = g_items.getItemType(dt.id);
							ASSERT(type.id != 0);

							if (type.isOpen == open) {
								item = transformItem(item, dt.id, tile);
								perfect_match = true;
								break;
							} else if (!close_match) {
								discarded_id = dt.id;
								close_match = true;
							}
							if (!close_match && discarded_id == 0) {
								discarded_id = dt.id;
							}
						}
					}
					// If we've found no perfect match, use a close-to perfect
					if (!perfect_match && discarded_id) {
						item = transformItem(item, discarded_id, tile);
					}
					continue;
				}
			}
			break;
		}
		// If we get this far in the loop we should return
		return;
	}
}

//=============================================================================
// Gravel brush

OptionalBorderBrush::OptionalBorderBrush() {
	////
}

OptionalBorderBrush::~OptionalBorderBrush() {
	////
}

std::string OptionalBorderBrush::getName() const {
	return "Optional Border Tool";
}

int OptionalBorderBrush::getLookID() const {
	return EDITOR_SPRITE_OPTIONAL_BORDER_TOOL;
}

bool OptionalBorderBrush::canDraw(BaseMap* map, const Position &position) const {
	Tile* tile = map->getTile(position);

	// You can't do gravel on a mountain tile
	if (tile) {
		if (GroundBrush* bb = tile->getGroundBrush()) {
			if (bb->hasOptionalBorder()) {
				return false;
			}
		}
	}

	uint32_t x = position.x;
	uint32_t y = position.y;
	uint32_t z = position.z;

	tile = map->getTile(x - 1, y - 1, z);
	if (tile) {
		if (GroundBrush* bb = tile->getGroundBrush()) {
			if (bb->hasOptionalBorder()) {
				return true;
			}
		}
	}
	tile = map->getTile(x, y - 1, z);
	if (tile) {
		if (GroundBrush* bb = tile->getGroundBrush()) {
			if (bb->hasOptionalBorder()) {
				return true;
			}
		}
	}
	tile = map->getTile(x + 1, y - 1, z);
	if (tile) {
		if (GroundBrush* bb = tile->getGroundBrush()) {
			if (bb->hasOptionalBorder()) {
				return true;
			}
		}
	}
	tile = map->getTile(x - 1, y, z);
	if (tile) {
		if (GroundBrush* bb = tile->getGroundBrush()) {
			if (bb->hasOptionalBorder()) {
				return true;
			}
		}
	}
	tile = map->getTile(x + 1, y, z);
	if (tile) {
		if (GroundBrush* bb = tile->getGroundBrush()) {
			if (bb->hasOptionalBorder()) {
				return true;
			}
		}
	}
	tile = map->getTile(x - 1, y + 1, z);
	if (tile) {
		if (GroundBrush* bb = tile->getGroundBrush()) {
			if (bb->hasOptionalBorder()) {
				return true;
			}
		}
	}
	tile = map->getTile(x, y + 1, z);
	if (tile) {
		if (GroundBrush* bb = tile->getGroundBrush()) {
			if (bb->hasOptionalBorder()) {
				return true;
			}
		}
	}
	tile = map->getTile(x + 1, y + 1, z);
	if (tile) {
		if (GroundBrush* bb = tile->getGroundBrush()) {
			if (bb->hasOptionalBorder()) {
				return true;
			}
		}
	}

	return false;
}

void OptionalBorderBrush::undraw(BaseMap* map, Tile* tile) {
	tile->setOptionalBorder(false); // The bordering algorithm will handle this automagicaly
}

void OptionalBorderBrush::draw(BaseMap* map, Tile* tile, void* parameter) {
	tile->setOptionalBorder(true); // The bordering algorithm will handle this automagicaly
}
