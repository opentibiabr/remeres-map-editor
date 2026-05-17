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

#include "editor.h"
#include "items.h"
#include "monsters.h"
#include <algorithm>

#include "gui.h"
#include "materials.h"
#include "brush_database.h"
#include "brush.h"
#include "monster_brush.h"
#include "npc_brush.h"
#include "raw_brush.h"

Materials g_materials;

namespace {
bool ExtractSimpleBrushNode(pugi::xml_node brushNode, BrushRecord &outBrush, std::vector<BrushItemRecord> &outItems, wxString &error, wxArrayString &warnings) {
	pugi::xml_attribute nameAttr = brushNode.attribute("name");
	pugi::xml_attribute typeAttr = brushNode.attribute("type");
	if (!nameAttr || !typeAttr) {
		error = "Brush node is missing required name/type attributes.";
		return false;
	}

	outBrush.name = wxString(nameAttr.as_string(), wxConvUTF8);
	outBrush.type = wxString(typeAttr.as_string(), wxConvUTF8);
	outBrush.lookId = brushNode.attribute("server_lookid") ? brushNode.attribute("server_lookid").as_int() : brushNode.attribute("lookid").as_int();
	outBrush.zOrder = brushNode.attribute("z-order").as_int();

	outItems.clear();
	for (pugi::xml_node childNode = brushNode.first_child(); childNode; childNode = childNode.next_sibling()) {
		const std::string childName = as_lower_str(childNode.name());
		if (childName == "item") {
			pugi::xml_attribute itemIdAttr = childNode.attribute("id");
			if (!itemIdAttr) {
				continue;
			}

			BrushItemRecord item;
			item.itemId = itemIdAttr.as_int();
			item.chance = std::max(1, childNode.attribute("chance").as_int(1));
			outItems.push_back(item);
		} else if (childName == "composite") {
			error = "Only simple brushes with direct <item> nodes are supported by the SQLite validation import.";
			return false;
		}
	}

	if (outItems.empty()) {
		warnings.push_back("SQLite validation import found brush \"" + outBrush.name + "\" without direct item nodes.");
	}

	return true;
}

bool FindAndExtractSimpleBrushRecursive(const FileName &filename, const wxString &brushName, BrushRecord &outBrush, std::vector<BrushItemRecord> &outItems, wxString &error, wxArrayString &warnings, std::set<wxString> &visited) {
	const wxString normalizedPath = filename.GetFullPath();
	if (visited.find(normalizedPath) != visited.end()) {
		return false;
	}
	visited.insert(normalizedPath);

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(filename.GetFullPath().mb_str());
	if (!result) {
		warnings.push_back("SQLite validation import could not read " + filename.GetFullName());
		return false;
	}

	pugi::xml_node materialsNode = doc.child("materials");
	if (!materialsNode) {
		warnings.push_back("SQLite validation import found invalid root in " + filename.GetFullName());
		return false;
	}

	for (pugi::xml_node childNode = materialsNode.first_child(); childNode; childNode = childNode.next_sibling()) {
		const std::string childName = as_lower_str(childNode.name());
		if (childName == "brush") {
			pugi::xml_attribute nameAttr = childNode.attribute("name");
			if (nameAttr && wxString(nameAttr.as_string(), wxConvUTF8) == brushName) {
				return ExtractSimpleBrushNode(childNode, outBrush, outItems, error, warnings);
			}
		} else if (childName == "include") {
			pugi::xml_attribute includeAttr = childNode.attribute("file");
			if (!includeAttr) {
				continue;
			}

			FileName includeName;
			includeName.SetPath(filename.GetPath());
			includeName.SetName(wxString(includeAttr.as_string(), wxConvUTF8));
			if (FindAndExtractSimpleBrushRecursive(includeName, brushName, outBrush, outItems, error, warnings, visited)) {
				return true;
			}
		}
	}

	return false;
}

struct PendingBorderSetImport {
	BorderSetRecord borderSet;
	std::vector<BorderSetItemRecord> items;
};

struct PendingGroundBrushBorderImport {
	int xmlBorderId = 0;
	int inlineBorderIndex = -1;
	GroundBrushBorderRecord border;
};

struct PendingGroundBrushImport {
	BrushRecord brush;
	std::vector<BrushItemRecord> items;
	std::vector<PendingBorderSetImport> optionalInlineBorders;
	std::vector<PendingGroundBrushBorderImport> optionalBorders;
	std::vector<PendingBorderSetImport> normalInlineBorders;
	std::vector<PendingGroundBrushBorderImport> normalBorders;
	std::vector<BrushLinkRecord> links;
};

wxString MaterialSourcePath(const FileName &filename) {
	return filename.GetFullPath();
}

FileName ResolveMaterialInclude(const FileName &baseFile, const wxString &includePath) {
	return FileName(baseFile.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR) + includePath);
}

wxString ParseBorderTargetMode(pugi::xml_node borderNode, wxString &targetBrushName) {
	const wxString toValue = wxString(borderNode.attribute("to").as_string(), wxConvUTF8);
	if (toValue.IsEmpty() || toValue == "all") {
		targetBrushName.clear();
		return "all";
	}
	if (toValue == "none") {
		targetBrushName.clear();
		return "none";
	}
	targetBrushName = toValue;
	return "brush";
}

wxString ParseBorderAlign(pugi::xml_node borderNode) {
	const wxString align = wxString(borderNode.attribute("align").as_string(), wxConvUTF8);
	return align.IsEmpty() ? "outer" : align;
}

void CollectBorderSetItems(pugi::xml_node borderNode, std::vector<BorderSetItemRecord> &outItems) {
	outItems.clear();
	int sortOrder = 0;
	for (pugi::xml_node childNode = borderNode.first_child(); childNode; childNode = childNode.next_sibling()) {
		if (as_lower_str(childNode.name()) != "borderitem") {
			continue;
		}

		const wxString edge = wxString(childNode.attribute("edge").as_string(), wxConvUTF8);
		const int itemId = childNode.attribute("item").as_int();
		if (edge.IsEmpty() || itemId <= 0) {
			continue;
		}

		BorderSetItemRecord item;
		item.edge = edge;
		item.itemId = itemId;
		item.sortOrder = sortOrder++;
		outItems.push_back(item);
	}
}

bool ImportGlobalBordersRecursive(const FileName &filename, wxArrayString &warnings, std::set<wxString> &visited) {
	const wxString normalizedPath = filename.GetFullPath();
	if (visited.find(normalizedPath) != visited.end()) {
		return true;
	}
	visited.insert(normalizedPath);

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(filename.GetFullPath().mb_str());
	if (!result) {
		warnings.push_back("SQLite border import could not read " + filename.GetFullName());
		return false;
	}

	pugi::xml_node materialsNode = doc.child("materials");
	if (!materialsNode) {
		warnings.push_back("SQLite border import found invalid root in " + filename.GetFullName());
		return false;
	}

	for (pugi::xml_node childNode = materialsNode.first_child(); childNode; childNode = childNode.next_sibling()) {
		const std::string childName = as_lower_str(childNode.name());
		if (childName == "include") {
			const wxString includePath = wxString(childNode.attribute("file").as_string(), wxConvUTF8);
			if (!includePath.empty() && !ImportGlobalBordersRecursive(ResolveMaterialInclude(filename, includePath), warnings, visited)) {
				return false;
			}
		} else if (childName == "border") {
			const int xmlBorderId = childNode.attribute("id").as_int();
			if (xmlBorderId <= 0) {
				continue;
			}

			BorderSetRecord borderSet;
			borderSet.xmlBorderId = xmlBorderId;
			borderSet.borderScope = "global";
			borderSet.borderType = wxString(childNode.attribute("type").as_string(), wxConvUTF8);
			if (borderSet.borderType.IsEmpty()) {
				borderSet.borderType = "normal";
			}
			borderSet.borderGroup = childNode.attribute("group").as_int();
			borderSet.sourceFile = MaterialSourcePath(filename);

			int64_t borderSetId = 0;
			if (!g_brush_database.upsertBorderSet(borderSet, borderSetId)) {
				warnings.push_back("SQLite border import failed for border id " + std::to_string(xmlBorderId) + ": " + g_brush_database.getLastError());
				return false;
			}

			std::vector<BorderSetItemRecord> items;
			CollectBorderSetItems(childNode, items);
			if (!g_brush_database.replaceBorderSetItems(borderSetId, items)) {
				warnings.push_back("SQLite border item import failed for border id " + std::to_string(xmlBorderId) + ": " + g_brush_database.getLastError());
				return false;
			}
		}
	}

	return true;
}

bool ParseGroundSpecificCases(pugi::xml_node borderNode, std::vector<GroundBorderCaseRecord> &outCases) {
	outCases.clear();
	int caseSortOrder = 0;
	for (pugi::xml_node specificNode = borderNode.first_child(); specificNode; specificNode = specificNode.next_sibling()) {
		if (as_lower_str(specificNode.name()) != "specific") {
			continue;
		}

		GroundBorderCaseRecord caseRecord;
		caseRecord.sortOrder = caseSortOrder++;

		int conditionSortOrder = 0;
		int actionSortOrder = 0;
		for (pugi::xml_node branchNode = specificNode.first_child(); branchNode; branchNode = branchNode.next_sibling()) {
			const std::string branchName = as_lower_str(branchNode.name());
			if (branchName == "conditions") {
				for (pugi::xml_node conditionNode = branchNode.first_child(); conditionNode; conditionNode = conditionNode.next_sibling()) {
					const std::string conditionName = as_lower_str(conditionNode.name());
					GroundBorderCaseConditionRecord condition;
					condition.conditionType = wxString::FromUTF8(conditionName.c_str());
					condition.sortOrder = conditionSortOrder++;
					if (conditionName == "match_border") {
						condition.matchValue = conditionNode.attribute("id").as_int();
						condition.edge = wxString(conditionNode.attribute("edge").as_string(), wxConvUTF8);
					} else if (conditionName == "match_group") {
						condition.matchValue = conditionNode.attribute("group").as_int();
						condition.edge = wxString(conditionNode.attribute("edge").as_string(), wxConvUTF8);
					} else if (conditionName == "match_item") {
						condition.matchValue = conditionNode.attribute("id").as_int();
					} else {
						continue;
					}
					caseRecord.conditions.push_back(condition);
				}
			} else if (branchName == "actions") {
				for (pugi::xml_node actionNode = branchNode.first_child(); actionNode; actionNode = actionNode.next_sibling()) {
					const std::string actionName = as_lower_str(actionNode.name());
					GroundBorderCaseActionRecord action;
					action.actionType = wxString::FromUTF8(actionName.c_str());
					action.sortOrder = actionSortOrder++;
					if (actionName == "replace_border") {
						action.targetValue = actionNode.attribute("id").as_int();
						action.edge = wxString(actionNode.attribute("edge").as_string(), wxConvUTF8);
						action.replacementValue = actionNode.attribute("with").as_int();
					} else if (actionName == "replace_item") {
						action.targetValue = actionNode.attribute("id").as_int();
						action.replacementValue = actionNode.attribute("with").as_int();
					} else if (actionName == "delete_borders") {
						// No extra payload needed.
					} else {
						continue;
					}
					caseRecord.actions.push_back(action);
				}
			}
		}

		outCases.push_back(caseRecord);
	}

	return true;
}

PendingBorderSetImport BuildInlineBorderSet(pugi::xml_node node, const wxString &borderType, const FileName &sourceFile) {
	PendingBorderSetImport pending;
	pending.borderSet.borderScope = "inline";
	pending.borderSet.borderType = borderType;
	pending.borderSet.groundEquivalent = node.attribute("ground_equivalent").as_int();
	pending.borderSet.sourceFile = MaterialSourcePath(sourceFile);
	CollectBorderSetItems(node, pending.items);
	return pending;
}

bool ParseGroundBrushNode(const FileName &sourceFile, pugi::xml_node brushNode, PendingGroundBrushImport &outBrush, wxArrayString &warnings) {
	if (wxString(brushNode.attribute("type").as_string(), wxConvUTF8) != "ground") {
		return false;
	}

	outBrush = PendingGroundBrushImport();
	outBrush.brush.name = wxString(brushNode.attribute("name").as_string(), wxConvUTF8);
	outBrush.brush.type = "ground";
	outBrush.brush.lookId = brushNode.attribute("lookid").as_int();
	outBrush.brush.serverLookId = brushNode.attribute("server_lookid").as_int();
	outBrush.brush.zOrder = brushNode.attribute("z-order").as_int();
	outBrush.brush.randomize = brushNode.attribute("randomize").as_bool();
	outBrush.brush.soloOptional = brushNode.attribute("solo_optional").as_bool();
	outBrush.brush.sourceFile = MaterialSourcePath(sourceFile);

	if (outBrush.brush.name.IsEmpty()) {
		warnings.push_back("SQLite ground import found ground brush without name in " + sourceFile.GetFullName());
		return false;
	}

	int linkSortOrder = 0;
	int optionalSortOrder = 0;
	int borderSortOrder = 0;
	for (pugi::xml_node childNode = brushNode.first_child(); childNode; childNode = childNode.next_sibling()) {
		const std::string childName = as_lower_str(childNode.name());
		if (childName == "item") {
			const int itemId = childNode.attribute("id").as_int();
			if (itemId <= 0) {
				continue;
			}

			BrushItemRecord item;
			item.itemId = itemId;
			item.chance = std::max(1, childNode.attribute("chance").as_int(1));
			outBrush.items.push_back(item);
		} else if (childName == "optional") {
			PendingGroundBrushBorderImport borderImport;
			borderImport.border.borderRole = "optional";
			borderImport.border.align = "optional";
			borderImport.border.targetMode = "none";
			borderImport.border.sortOrder = optionalSortOrder++;

			if (childNode.attribute("id")) {
				borderImport.xmlBorderId = childNode.attribute("id").as_int();
			} else if (childNode.attribute("ground_equivalent")) {
				borderImport.inlineBorderIndex = static_cast<int>(outBrush.optionalInlineBorders.size());
				outBrush.optionalInlineBorders.push_back(BuildInlineBorderSet(childNode, "optional", sourceFile));
			}

			outBrush.optionalBorders.push_back(borderImport);
		} else if (childName == "border") {
			PendingGroundBrushBorderImport borderImport;
			borderImport.border.borderRole = "normal";
			borderImport.border.align = ParseBorderAlign(childNode);
			borderImport.border.targetMode = ParseBorderTargetMode(childNode, borderImport.border.targetBrushName);
			borderImport.border.superBorder = childNode.attribute("super").as_bool();
			borderImport.border.sortOrder = borderSortOrder++;

			if (childNode.attribute("id")) {
				borderImport.xmlBorderId = childNode.attribute("id").as_int();
			} else if (childNode.attribute("ground_equivalent")) {
				borderImport.inlineBorderIndex = static_cast<int>(outBrush.normalInlineBorders.size());
				outBrush.normalInlineBorders.push_back(BuildInlineBorderSet(childNode, "normal", sourceFile));
			}

			ParseGroundSpecificCases(childNode, borderImport.border.cases);
			outBrush.normalBorders.push_back(borderImport);
		} else if (childName == "friend" || childName == "enemy") {
			BrushLinkRecord link;
			link.relationType = wxString::FromUTF8(childName.c_str());
			link.targetBrushName = wxString(childNode.attribute("name").as_string(), wxConvUTF8);
			link.sortOrder = linkSortOrder++;
			outBrush.links.push_back(link);
		} else if (childName == "clear_borders") {
			outBrush.normalInlineBorders.clear();
			outBrush.normalBorders.clear();
			borderSortOrder = 0;
		} else if (childName == "clear_friends") {
			outBrush.links.clear();
			linkSortOrder = 0;
		}
	}

	return true;
}

bool ImportGroundBrushesFile(const FileName &groundsFile, wxArrayString &warnings) {
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(groundsFile.GetFullPath().mb_str());
	if (!result) {
		warnings.push_back("SQLite ground import could not read " + groundsFile.GetFullName());
		return false;
	}

	pugi::xml_node materialsNode = doc.child("materials");
	if (!materialsNode) {
		warnings.push_back("SQLite ground import found invalid root in " + groundsFile.GetFullName());
		return false;
	}

	for (pugi::xml_node brushNode = materialsNode.first_child(); brushNode; brushNode = brushNode.next_sibling()) {
		if (as_lower_str(brushNode.name()) != "brush") {
			continue;
		}

		PendingGroundBrushImport pending;
		if (!ParseGroundBrushNode(groundsFile, brushNode, pending, warnings)) {
			continue;
		}

		int64_t brushId = 0;
		if (!g_brush_database.upsertBrush(pending.brush, brushId)) {
			warnings.push_back("SQLite ground import failed for brush \"" + pending.brush.name + "\": " + g_brush_database.getLastError());
			return false;
		}

		if (!g_brush_database.deleteOwnedBorderSetsForBrush(brushId)) {
			warnings.push_back("SQLite ground import failed cleaning inline borders for \"" + pending.brush.name + "\": " + g_brush_database.getLastError());
			return false;
		}

		for (BrushItemRecord &item : pending.items) {
			item.brushId = brushId;
		}
		if (!g_brush_database.replaceBrushItems(brushId, pending.items)) {
			warnings.push_back("SQLite ground import failed writing items for \"" + pending.brush.name + "\": " + g_brush_database.getLastError());
			return false;
		}

		std::vector<int64_t> optionalInlineIds(pending.optionalInlineBorders.size(), 0);
		for (size_t i = 0; i < pending.optionalInlineBorders.size(); ++i) {
			pending.optionalInlineBorders[i].borderSet.ownerBrushId = brushId;
			int64_t borderSetId = 0;
			if (!g_brush_database.upsertBorderSet(pending.optionalInlineBorders[i].borderSet, borderSetId)) {
				warnings.push_back("SQLite optional border import failed for \"" + pending.brush.name + "\": " + g_brush_database.getLastError());
				return false;
			}
			optionalInlineIds[i] = borderSetId;
			if (!g_brush_database.replaceBorderSetItems(borderSetId, pending.optionalInlineBorders[i].items)) {
				warnings.push_back("SQLite optional border item import failed for \"" + pending.brush.name + "\": " + g_brush_database.getLastError());
				return false;
			}
		}

		std::vector<int64_t> normalInlineIds(pending.normalInlineBorders.size(), 0);
		for (size_t i = 0; i < pending.normalInlineBorders.size(); ++i) {
			pending.normalInlineBorders[i].borderSet.ownerBrushId = brushId;
			int64_t borderSetId = 0;
			if (!g_brush_database.upsertBorderSet(pending.normalInlineBorders[i].borderSet, borderSetId)) {
				warnings.push_back("SQLite inline border import failed for \"" + pending.brush.name + "\": " + g_brush_database.getLastError());
				return false;
			}
			normalInlineIds[i] = borderSetId;
			if (!g_brush_database.replaceBorderSetItems(borderSetId, pending.normalInlineBorders[i].items)) {
				warnings.push_back("SQLite inline border item import failed for \"" + pending.brush.name + "\": " + g_brush_database.getLastError());
				return false;
			}
		}

		std::vector<GroundBrushBorderRecord> borders;
		for (PendingGroundBrushBorderImport &pendingBorder : pending.optionalBorders) {
			GroundBrushBorderRecord border = pendingBorder.border;
			if (pendingBorder.xmlBorderId > 0) {
				BorderSetRecord borderSet;
				if (!g_brush_database.findBorderSetByXmlBorderId(pendingBorder.xmlBorderId, borderSet)) {
					warnings.push_back("SQLite optional border id " + std::to_string(pendingBorder.xmlBorderId) + " not found for \"" + pending.brush.name + "\"");
					return false;
				}
				border.borderSetId = borderSet.id;
			} else if (pendingBorder.inlineBorderIndex >= 0 && pendingBorder.inlineBorderIndex < static_cast<int>(optionalInlineIds.size())) {
				border.borderSetId = optionalInlineIds[pendingBorder.inlineBorderIndex];
			}
			if (border.borderSetId > 0) {
				borders.push_back(border);
			}
		}

		for (PendingGroundBrushBorderImport &pendingBorder : pending.normalBorders) {
			GroundBrushBorderRecord border = pendingBorder.border;
			if (pendingBorder.xmlBorderId > 0) {
				BorderSetRecord borderSet;
				if (!g_brush_database.findBorderSetByXmlBorderId(pendingBorder.xmlBorderId, borderSet)) {
					warnings.push_back("SQLite border id " + std::to_string(pendingBorder.xmlBorderId) + " not found for \"" + pending.brush.name + "\"");
					return false;
				}
				border.borderSetId = borderSet.id;
			} else if (pendingBorder.inlineBorderIndex >= 0 && pendingBorder.inlineBorderIndex < static_cast<int>(normalInlineIds.size())) {
				border.borderSetId = normalInlineIds[pendingBorder.inlineBorderIndex];
			}
			if (border.borderSetId > 0) {
				borders.push_back(border);
			}
		}

		if (!g_brush_database.replaceGroundBrushBorders(brushId, borders)) {
			warnings.push_back("SQLite ground border import failed for \"" + pending.brush.name + "\": " + g_brush_database.getLastError());
			return false;
		}

		for (BrushLinkRecord &link : pending.links) {
			link.brushId = brushId;
		}
		if (!g_brush_database.replaceBrushLinks(brushId, pending.links)) {
			warnings.push_back("SQLite brush link import failed for \"" + pending.brush.name + "\": " + g_brush_database.getLastError());
			return false;
		}
	}

	return true;
}
} // namespace

Materials::Materials() {
	////
}

Materials::~Materials() {
	clear();
}

void Materials::clear() {
	for (TilesetContainer::iterator iter = tilesets.begin(); iter != tilesets.end(); ++iter) {
		delete iter->second;
	}

	tilesets.clear();
}

bool Materials::initializeBrushDatabase(wxArrayString &warnings) {
	if (!g_settings.getBoolean(Config::USE_SQLITE_MATERIALS)) {
		return true;
	}

	const wxString databasePath = GUI::GetDataDirectory() + "materials/materials.db";
	if (!g_brush_database.initialize(databasePath)) {
		warnings.push_back("SQLite brush database initialization failed: " + g_brush_database.getLastError());
		return false;
	}

	if (!g_brush_database.testDatabaseConnection()) {
		warnings.push_back("SQLite brush database connection test failed: " + g_brush_database.getLastError());
		return false;
	}

	spdlog::info("SQLite brush backend is enabled at {}", databasePath.ToStdString());
	return true;
}

bool Materials::migrateSingleBrushToSQLite(const FileName &identifier, const wxString &brushName, wxString &error, wxArrayString &warnings) {
	if (!g_settings.getBoolean(Config::USE_SQLITE_MATERIALS)) {
		return true;
	}
	if (!g_brush_database.isOpen()) {
		error = "SQLite brush database is not open.";
		return false;
	}

	BrushRecord brush;
	std::vector<BrushItemRecord> items;
	std::set<wxString> visited;
	if (!FindAndExtractSimpleBrushRecursive(identifier, brushName, brush, items, error, warnings, visited)) {
		if (error.IsEmpty()) {
			error = "Brush \"" + brushName + "\" was not found in materials XML includes.";
		}
		return false;
	}

	int64_t brushId = 0;
	if (!g_brush_database.upsertBrush(brush, brushId)) {
		error = g_brush_database.getLastError();
		return false;
	}

	for (BrushItemRecord &item : items) {
		item.brushId = brushId;
	}
	if (!g_brush_database.replaceBrushItems(brushId, items)) {
		error = g_brush_database.getLastError();
		return false;
	}

	spdlog::info("SQLite validation import migrated brush '{}' with {} items", brush.name.ToStdString(), items.size());
	return true;
}

bool Materials::migrateGroundsToSQLite(wxString &error, wxArrayString &warnings) {
	if (!g_settings.getBoolean(Config::USE_SQLITE_MATERIALS)) {
		return true;
	}
	if (!g_brush_database.isOpen()) {
		error = "SQLite brush database is not open.";
		return false;
	}

	const FileName bordersFile(GUI::GetDataDirectory() + "materials/borders.xml");
	const FileName groundsFile(GUI::GetDataDirectory() + "materials/brushs/grounds.xml");

	if (!g_brush_database.deleteBrushesByType("ground")) {
		error = g_brush_database.getLastError();
		return false;
	}
	if (!g_brush_database.deleteBorderSetsByScope("global")) {
		error = g_brush_database.getLastError();
		return false;
	}

	std::set<wxString> visited;
	if (!ImportGlobalBordersRecursive(bordersFile, warnings, visited)) {
		error = "Failed to import border sets into SQLite.";
		return false;
	}
	if (!ImportGroundBrushesFile(groundsFile, warnings)) {
		error = "Failed to import ground brushes into SQLite.";
		return false;
	}
	if (!g_brush_database.resolveGroundReferenceNames()) {
		error = g_brush_database.getLastError();
		return false;
	}

	spdlog::info("SQLite ground import completed from {}", groundsFile.GetFullPath().ToStdString());
	return true;
}

bool Materials::loadMaterials(const FileName &identifier, wxString &error, wxArrayString &warnings) {
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(identifier.GetFullPath().mb_str());
	if (!result) {
		warnings.push_back("Could not open " + identifier.GetFullName() + " (file not found or syntax error)");
		return false;
	}

	pugi::xml_node node = doc.child("materials");
	if (!node) {
		warnings.push_back(identifier.GetFullName() + ": Invalid rootheader.");
		return false;
	}

	unserializeMaterials(identifier, node, error, warnings);
	return true;
}

bool Materials::unserializeMaterials(const FileName &filename, pugi::xml_node node, wxString &error, wxArrayString &warnings) {
	wxString warning;
	pugi::xml_attribute attribute;
	for (pugi::xml_node childNode = node.first_child(); childNode; childNode = childNode.next_sibling()) {
		const std::string &childName = as_lower_str(childNode.name());
		if (childName == "include") {
			if (!(attribute = childNode.attribute("file"))) {
				continue;
			}

			FileName includeName;
			includeName.SetPath(filename.GetPath());
			includeName.SetName(wxString(attribute.as_string(), wxConvUTF8));

			wxString subError;
			if (!loadMaterials(includeName, subError, warnings)) {
				warnings.push_back("Error while loading file \"" + includeName.GetFullName() + "\": " + subError);
			}
		} else if (childName == "metaitem") {
			g_items.loadMetaItem(childNode);
		} else if (childName == "border") {
			g_brushes.unserializeBorder(childNode, warnings);
			if (warning.size()) {
				warnings.push_back("materials.xml: " + warning);
			}
		} else if (childName == "brush") {
			g_brushes.unserializeBrush(childNode, warnings);
			if (warning.size()) {
				warnings.push_back("materials.xml: " + warning);
			}
		} else if (childName == "tileset") {
			unserializeTileset(childNode, warnings);
		}
	}
	return true;
}

void Materials::populateRawBrushes(Tileset* others) {
	for (int32_t id = 0; id <= g_items.getMaxID(); ++id) {
		auto type = g_items.getRawItemType(id);
		if (!type) {
			continue;
		}

		if (!type->isMetaItem()) {
			Brush* brush;
			if (type->in_other_tileset) {
				others->getCategory(TILESET_RAW)->brushlist.push_back(type->raw_brush);
				continue;
			} else if (!type->raw_brush) {
				brush = type->raw_brush = newd RAWBrush(type->id);
				type->has_raw = true;
				g_brushes.addBrush(type->raw_brush);
			} else if (!type->has_raw) {
				brush = type->raw_brush;
			} else {
				continue;
			}

			brush->flagAsVisible();
			others->getCategory(TILESET_RAW)->brushlist.push_back(type->raw_brush);
			type->in_other_tileset = true;
		}
	}
}

void Materials::createOtherTileset() {
	Tileset* others;
	if (tilesets["Others"] != nullptr) {
		others = tilesets["Others"];
		others->clear();
	} else {
		others = newd Tileset(g_brushes, "Others");
		tilesets["Others"] = others;
	}

	populateRawBrushes(others);

	// Clear TILESET_MONSTER categories from previous folder tilesets
	for (auto &[name, ts] : tilesets) {
		if (name != "Others" && name != "Monsters") {
			TilesetCategory* cat = ts->getCategory(TILESET_MONSTER);
			if (cat) {
				cat->brushlist.clear();
			}
		}
	}

	for (MonsterMap::iterator iter = g_monsters.begin(); iter != g_monsters.end(); ++iter) {
		MonsterType* type = iter->second;

		if (type->brush == nullptr) {
			type->brush = newd MonsterBrush(type);
			g_brushes.addBrush(type->brush);
			type->brush->flagAsVisible();
			type->in_other_tileset = true;
		}

		// Always adds in Others
		others->getCategory(TILESET_MONSTER)->brushlist.push_back(type->brush);

		// Adds to the folder tileset if it exists
		if (!type->folder.empty()) {
			std::string tsName = type->folder;
			std::replace(tsName.begin(), tsName.end(), '_', ' ');
			tsName[0] = std::toupper(static_cast<unsigned char>(tsName[0]));

			if (tsName == "Others") {
				continue;
			}

			Tileset* folderTs;
			auto it = tilesets.find(tsName);
			if (it != tilesets.end()) {
				folderTs = it->second;
			} else {
				folderTs = newd Tileset(g_brushes, tsName);
				tilesets[tsName] = folderTs;
			}
			folderTs->getCategory(TILESET_MONSTER)->brushlist.push_back(type->brush);
		}
	}
}

void Materials::createNpcTileset() {
	Tileset* npcTileset;
	if (tilesets["NPCs"] != nullptr) {
		npcTileset = tilesets["NPCs"];
		npcTileset->clear();
	} else {
		npcTileset = newd Tileset(g_brushes, "NPCs");
		tilesets["NPCs"] = npcTileset;
	}

	for (NpcMap::iterator iter = g_npcs.begin(); iter != g_npcs.end(); ++iter) {
		NpcType* type = iter->second;
		if (type->in_other_tileset) {
			npcTileset->getCategory(TILESET_NPC)->brushlist.push_back(type->brush);
		} else if (type->brush == nullptr) {
			type->brush = newd NpcBrush(type);
			g_brushes.addBrush(type->brush);
			type->brush->flagAsVisible();
			type->in_other_tileset = true;
			npcTileset->getCategory(TILESET_NPC)->brushlist.push_back(type->brush);
		}
	}
}

bool Materials::unserializeTileset(pugi::xml_node node, wxArrayString &warnings) {
	pugi::xml_attribute attribute;
	if (!(attribute = node.attribute("name"))) {
		warnings.push_back("Couldn't read tileset name");
		return false;
	}

	const std::string &name = attribute.as_string();

	Tileset* tileset;
	auto it = tilesets.find(name);
	if (it != tilesets.end()) {
		tileset = it->second;
	} else {
		tileset = newd Tileset(g_brushes, name);
		tilesets.insert(std::make_pair(name, tileset));
	}

	for (pugi::xml_node childNode = node.first_child(); childNode; childNode = childNode.next_sibling()) {
		tileset->loadCategory(childNode, warnings);
	}
	return true;
}

void Materials::addToTileset(std::string tilesetName, int itemId, TilesetCategoryType categoryType) {
	ItemType &it = g_items[itemId];

	if (it.id == 0) {
		return;
	}

	Tileset* tileset;
	auto _it = tilesets.find(tilesetName);
	if (_it != tilesets.end()) {
		tileset = _it->second;
	} else {
		tileset = newd Tileset(g_brushes, tilesetName);
		tilesets.insert(std::make_pair(tilesetName, tileset));
	}

	TilesetCategory* category = tileset->getCategory(categoryType);

	if (!it.isMetaItem()) {
		Brush* brush;
		if (it.in_other_tileset) {
			category->brushlist.push_back(it.raw_brush);
			return;
		} else if (it.raw_brush == nullptr) {
			brush = it.raw_brush = newd RAWBrush(it.id);
			it.has_raw = true;
			g_brushes.addBrush(it.raw_brush);
		} else {
			brush = it.raw_brush;
		}

		brush->flagAsVisible();
		category->brushlist.push_back(it.raw_brush);
		it.in_other_tileset = true;
	}
}

bool Materials::isInTileset(Item* item, std::string tilesetName) const {
	const ItemType &type = g_items.getItemType(item->getID());
	return type.id != 0 && (isInTileset(type.brush, tilesetName) || isInTileset(type.doodad_brush, tilesetName) || isInTileset(type.raw_brush, tilesetName));
}

bool Materials::isInTileset(Brush* brush, std::string tilesetName) const {
	if (!brush) {
		return false;
	}

	TilesetContainer::const_iterator tilesetiter = tilesets.find(tilesetName);
	if (tilesetiter == tilesets.end()) {
		return false;
	}
	Tileset* tileset = tilesetiter->second;

	return tileset->containsBrush(brush);
}
