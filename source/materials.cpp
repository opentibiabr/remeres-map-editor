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

#include <format>

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
				item.chance = childNode.attribute("chance").as_int(1);
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

	bool LoadMaterialsDocumentRoot(const FileName &filename, const wxString &context, pugi::xml_document &doc, pugi::xml_node &materialsNode, wxArrayString &warnings) {
		pugi::xml_parse_result result = doc.load_file(filename.GetFullPath().mb_str());
		if (!result) {
			warnings.push_back(context + " could not read " + filename.GetFullName());
			return false;
		}

		materialsNode = doc.child("materials");
		if (!materialsNode) {
			warnings.push_back(context + " found invalid root in " + filename.GetFullName());
			return false;
		}

		return true;
	}

	FileName ResolveMaterialInclude(const FileName &baseFile, const wxString &includePath) {
		return FileName(baseFile.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR) + includePath);
	}

	bool ImportGroundBrushesRecursive(const FileName &groundsFile, wxArrayString &warnings, std::set<wxString> &visited);
	bool ImportWallBrushesRecursive(const FileName &wallsFile, wxArrayString &warnings, std::set<wxString> &visited);

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
		return align.IsEmpty() ? wxString("outer") : align;
	}

	void ParseThicknessString(const wxString &thicknessString, int &thickness, int &thicknessCeiling) {
		thickness = 0;
		thicknessCeiling = 0;
		if (thicknessString.IsEmpty()) {
			return;
		}

		wxString beforeSlash = thicknessString.BeforeFirst('/');
		wxString afterSlash = thicknessString.AfterFirst('/');
		long parsedValue = 0;
		if (!beforeSlash.IsEmpty() && beforeSlash.ToLong(&parsedValue)) {
			thickness = static_cast<int>(parsedValue);
		}
		if (!afterSlash.IsEmpty() && afterSlash != thicknessString && afterSlash.ToLong(&parsedValue)) {
			thicknessCeiling = static_cast<int>(parsedValue);
		}
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

	bool TryParseGroundCaseCondition(pugi::xml_node conditionNode, int sortOrder, GroundBorderCaseConditionRecord &outCondition) {
		const std::string conditionName = as_lower_str(conditionNode.name());
		if (conditionName != "match_border" && conditionName != "match_group" && conditionName != "match_item") {
			return false;
		}

		outCondition = GroundBorderCaseConditionRecord();
		outCondition.conditionType = wxString::FromUTF8(conditionName.c_str());
		outCondition.sortOrder = sortOrder;
		outCondition.edge = wxString(conditionNode.attribute("edge").as_string(), wxConvUTF8);

		if (conditionName == "match_group") {
			outCondition.matchValue = conditionNode.attribute("group").as_int();
		} else {
			outCondition.matchValue = conditionNode.attribute("id").as_int();
		}

		return true;
	}

	void CollectGroundCaseConditions(pugi::xml_node conditionsNode, std::vector<GroundBorderCaseConditionRecord> &outConditions) {
		auto sortOrder = static_cast<int>(outConditions.size());
		for (pugi::xml_node conditionNode = conditionsNode.first_child(); conditionNode; conditionNode = conditionNode.next_sibling()) {
			GroundBorderCaseConditionRecord condition;
			if (!TryParseGroundCaseCondition(conditionNode, sortOrder, condition)) {
				continue;
			}

			outConditions.push_back(condition);
			++sortOrder;
		}
	}

	bool TryParseGroundCaseAction(pugi::xml_node actionNode, int sortOrder, GroundBorderCaseActionRecord &outAction) {
		const std::string actionName = as_lower_str(actionNode.name());
		if (actionName != "replace_border" && actionName != "replace_item" && actionName != "delete_borders") {
			return false;
		}

		outAction = GroundBorderCaseActionRecord();
		outAction.actionType = wxString::FromUTF8(actionName.c_str());
		outAction.sortOrder = sortOrder;
		outAction.targetValue = actionNode.attribute("id").as_int();
		outAction.edge = wxString(actionNode.attribute("edge").as_string(), wxConvUTF8);
		outAction.replacementValue = actionNode.attribute("with").as_int();
		return true;
	}

	void CollectGroundCaseActions(pugi::xml_node actionsNode, std::vector<GroundBorderCaseActionRecord> &outActions) {
		auto sortOrder = static_cast<int>(outActions.size());
		for (pugi::xml_node actionNode = actionsNode.first_child(); actionNode; actionNode = actionNode.next_sibling()) {
			GroundBorderCaseActionRecord action;
			if (!TryParseGroundCaseAction(actionNode, sortOrder, action)) {
				continue;
			}

			outActions.push_back(action);
			++sortOrder;
		}
	}

	void CollectGroundSpecificCaseBranches(pugi::xml_node specificNode, GroundBorderCaseRecord &caseRecord) {
		for (pugi::xml_node branchNode = specificNode.first_child(); branchNode; branchNode = branchNode.next_sibling()) {
			const std::string branchName = as_lower_str(branchNode.name());
			if (branchName == "conditions") {
				CollectGroundCaseConditions(branchNode, caseRecord.conditions);
				continue;
			}

			if (branchName == "actions") {
				CollectGroundCaseActions(branchNode, caseRecord.actions);
			}
		}
	}

	bool ImportGlobalBordersRecursive(const FileName &filename, wxArrayString &warnings, std::set<wxString> &visited) {
		const wxString normalizedPath = filename.GetFullPath();
		if (visited.find(normalizedPath) != visited.end()) {
			return true;
		}
		visited.insert(normalizedPath);

		pugi::xml_document doc;
		pugi::xml_node materialsNode;
		if (!LoadMaterialsDocumentRoot(filename, "SQLite border import", doc, materialsNode, warnings)) {
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
				borderSet.groundEquivalent = childNode.attribute("ground_equivalent").as_int();
				borderSet.sourceFile = MaterialSourcePath(filename);

				int64_t borderSetId = 0;
				if (!g_brush_database.upsertBorderSet(borderSet, borderSetId)) {
					warnings.push_back(wxString::FromUTF8(std::format("SQLite border import failed for border id {}: {}", xmlBorderId, g_brush_database.getLastError().ToStdString())));
					return false;
				}

				std::vector<BorderSetItemRecord> items;
				CollectBorderSetItems(childNode, items);
				if (!g_brush_database.replaceBorderSetItems(borderSetId, items)) {
					warnings.push_back(wxString::FromUTF8(std::format("SQLite border item import failed for border id {}: {}", xmlBorderId, g_brush_database.getLastError().ToStdString())));
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
			CollectGroundSpecificCaseBranches(specificNode, caseRecord);

			outCases.push_back(caseRecord);
		}

		return true;
	}

	PendingBorderSetImport BuildInlineBorderSet(pugi::xml_node node, const wxString &borderType, const FileName &sourceFile) {
		PendingBorderSetImport pending;
		pending.borderSet.borderScope = "inline";
		const wxString declaredType = wxString(node.attribute("type").as_string(), wxConvUTF8).Lower();
		if (!declaredType.IsEmpty() && (declaredType == "normal" || declaredType == "optional")) {
			pending.borderSet.borderType = declaredType;
		} else {
			pending.borderSet.borderType = borderType;
		}
		pending.borderSet.borderGroup = node.attribute("group").as_int();
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
				item.chance = childNode.attribute("chance").as_int(1);
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

	void AssignBrushIdToItems(std::vector<BrushItemRecord> &items, int64_t brushId) {
		for (BrushItemRecord &item : items) {
			item.brushId = brushId;
		}
	}

	void AssignBrushIdToLinks(std::vector<BrushLinkRecord> &links, int64_t brushId) {
		for (BrushLinkRecord &link : links) {
			link.brushId = brushId;
		}
	}

	bool ImportPendingGroundInlineBorders(
		std::vector<PendingBorderSetImport> &pendingInlineBorders,
		int64_t brushId,
		const wxString &brushName,
		const wxString &importFailureContext,
		const wxString &itemImportFailureContext,
		std::vector<int64_t> &outBorderSetIds,
		wxArrayString &warnings
	) {
		outBorderSetIds.assign(pendingInlineBorders.size(), 0);
		for (size_t i = 0; i < pendingInlineBorders.size(); ++i) {
			pendingInlineBorders[i].borderSet.ownerBrushId = brushId;
			int64_t borderSetId = 0;
			if (!g_brush_database.upsertBorderSet(pendingInlineBorders[i].borderSet, borderSetId)) {
				warnings.push_back(importFailureContext + " for \"" + brushName + "\": " + g_brush_database.getLastError());
				return false;
			}

			outBorderSetIds[i] = borderSetId;
			if (!g_brush_database.replaceBorderSetItems(borderSetId, pendingInlineBorders[i].items)) {
				warnings.push_back(itemImportFailureContext + " for \"" + brushName + "\": " + g_brush_database.getLastError());
				return false;
			}
		}

		return true;
	}

	bool ResolveGroundBorderSetId(
		const PendingGroundBrushBorderImport &pendingBorder,
		const std::vector<int64_t> &inlineBorderIds,
		const wxString &brushName,
		const wxString &missingBorderContext,
		int64_t &outBorderSetId,
		wxArrayString &warnings
	) {
		outBorderSetId = 0;
		if (pendingBorder.xmlBorderId > 0) {
			BorderSetRecord borderSet;
			if (!g_brush_database.findBorderSetByXmlBorderId(pendingBorder.xmlBorderId, borderSet)) {
				warnings.push_back(missingBorderContext + " " + std::to_string(pendingBorder.xmlBorderId) + " not found for \"" + brushName + "\"");
				return false;
			}

			outBorderSetId = borderSet.id;
			return true;
		}

		if (pendingBorder.inlineBorderIndex >= 0 && pendingBorder.inlineBorderIndex < static_cast<int>(inlineBorderIds.size())) {
			outBorderSetId = inlineBorderIds[pendingBorder.inlineBorderIndex];
		}

		return true;
	}

	bool AppendResolvedGroundBorders(
		const std::vector<PendingGroundBrushBorderImport> &pendingBorders,
		const std::vector<int64_t> &inlineBorderIds,
		const wxString &brushName,
		const wxString &missingBorderContext,
		std::vector<GroundBrushBorderRecord> &outBorders,
		wxArrayString &warnings
	) {
		for (const PendingGroundBrushBorderImport &pendingBorder : pendingBorders) {
			int64_t borderSetId = 0;
			if (!ResolveGroundBorderSetId(pendingBorder, inlineBorderIds, brushName, missingBorderContext, borderSetId, warnings)) {
				return false;
			}
			if (borderSetId <= 0) {
				continue;
			}

			GroundBrushBorderRecord border = pendingBorder.border;
			border.borderSetId = borderSetId;
			outBorders.push_back(border);
		}

		return true;
	}

	bool ImportPendingGroundBrush(PendingGroundBrushImport pending, wxArrayString &warnings) {
		int64_t brushId = 0;
		if (!g_brush_database.upsertBrush(pending.brush, brushId)) {
			warnings.push_back("SQLite ground import failed for brush \"" + pending.brush.name + "\": " + g_brush_database.getLastError());
			return false;
		}

		if (!g_brush_database.deleteOwnedBorderSetsForBrush(brushId)) {
			warnings.push_back("SQLite ground import failed cleaning inline borders for \"" + pending.brush.name + "\": " + g_brush_database.getLastError());
			return false;
		}

		AssignBrushIdToItems(pending.items, brushId);
		if (!g_brush_database.replaceBrushItems(brushId, pending.items)) {
			warnings.push_back("SQLite ground import failed writing items for \"" + pending.brush.name + "\": " + g_brush_database.getLastError());
			return false;
		}

		std::vector<int64_t> optionalInlineIds;
		if (!ImportPendingGroundInlineBorders(
				pending.optionalInlineBorders,
				brushId,
				pending.brush.name,
				"SQLite optional border import failed",
				"SQLite optional border item import failed",
				optionalInlineIds,
				warnings
			)) {
			return false;
		}

		std::vector<int64_t> normalInlineIds;
		if (!ImportPendingGroundInlineBorders(
				pending.normalInlineBorders,
				brushId,
				pending.brush.name,
				"SQLite inline border import failed",
				"SQLite inline border item import failed",
				normalInlineIds,
				warnings
			)) {
			return false;
		}

		std::vector<GroundBrushBorderRecord> borders;
		if (!AppendResolvedGroundBorders(
				pending.optionalBorders,
				optionalInlineIds,
				pending.brush.name,
				"SQLite optional border id",
				borders,
				warnings
			)) {
			return false;
		}
		if (!AppendResolvedGroundBorders(
				pending.normalBorders,
				normalInlineIds,
				pending.brush.name,
				"SQLite border id",
				borders,
				warnings
			)) {
			return false;
		}

		if (!g_brush_database.replaceGroundBrushBorders(brushId, borders)) {
			warnings.push_back("SQLite ground border import failed for \"" + pending.brush.name + "\": " + g_brush_database.getLastError());
			return false;
		}

		AssignBrushIdToLinks(pending.links, brushId);
		if (!g_brush_database.replaceBrushLinks(brushId, pending.links)) {
			warnings.push_back("SQLite brush link import failed for \"" + pending.brush.name + "\": " + g_brush_database.getLastError());
			return false;
		}

		return true;
	}

	bool ImportGroundBrushesFile(const FileName &groundsFile, wxArrayString &warnings) {
		std::set<wxString> visited;
		return ImportGroundBrushesRecursive(groundsFile, warnings, visited);
	}

	bool ImportGroundBrushesRecursive(const FileName &groundsFile, wxArrayString &warnings, std::set<wxString> &visited) {
		const wxString normalizedPath = groundsFile.GetFullPath();
		if (visited.find(normalizedPath) != visited.end()) {
			return true;
		}
		visited.insert(normalizedPath);

		pugi::xml_document doc;
		pugi::xml_node materialsNode;
		if (!LoadMaterialsDocumentRoot(groundsFile, "SQLite ground import", doc, materialsNode, warnings)) {
			return false;
		}

		for (pugi::xml_node brushNode = materialsNode.first_child(); brushNode; brushNode = brushNode.next_sibling()) {
			const std::string childName = as_lower_str(brushNode.name());
			if (childName == "include") {
				const wxString includePath = wxString(brushNode.attribute("file").as_string(), wxConvUTF8);
				if (!includePath.empty() && !ImportGroundBrushesRecursive(ResolveMaterialInclude(groundsFile, includePath), warnings, visited)) {
					return false;
				}
				continue;
			}

			if (childName != "brush") {
				continue;
			}

			PendingGroundBrushImport pending;
			if (!ParseGroundBrushNode(groundsFile, brushNode, pending, warnings)) {
				continue;
			}
			if (!ImportPendingGroundBrush(pending, warnings)) {
				return false;
			}
		}

		return true;
	}

	void CollectWallItemNodes(pugi::xml_node parentNode, std::vector<WallPartItemRecord> &outItems) {
		auto sortOrder = static_cast<int>(outItems.size());
		for (pugi::xml_node childNode = parentNode.first_child(); childNode; childNode = childNode.next_sibling()) {
			if (as_lower_str(childNode.name()) != "item") {
				continue;
			}

			const int itemId = childNode.attribute("id").as_int();
			if (itemId <= 0) {
				continue;
			}

			WallPartItemRecord item;
			item.itemId = itemId;
			item.chance = childNode.attribute("chance").as_int();
			item.sortOrder = sortOrder++;
			outItems.push_back(item);
		}
	}

	void CollectWallDoorNodes(pugi::xml_node parentNode, std::vector<WallPartDoorRecord> &outDoors) {
		auto sortOrder = static_cast<int>(outDoors.size());
		for (pugi::xml_node childNode = parentNode.first_child(); childNode; childNode = childNode.next_sibling()) {
			if (as_lower_str(childNode.name()) != "door") {
				continue;
			}

			const int itemId = childNode.attribute("id").as_int();
			if (itemId <= 0) {
				continue;
			}

			WallPartDoorRecord door;
			door.itemId = itemId;
			door.doorType = wxString(childNode.attribute("type").as_string(), wxConvUTF8);
			door.isOpen = childNode.attribute("open").as_bool();
			door.wallHateMe = childNode.attribute("hate").as_bool();
			door.sortOrder = sortOrder++;
			outDoors.push_back(door);
		}
	}

	bool WallPartHasContent(const WallPartRecord &part) {
		return !part.items.empty() || !part.doors.empty();
	}

	WallPartRecord &GetOrCreateWallPart(std::vector<WallPartRecord> &outParts, const wxString &partType, int sortOrder) {
		for (auto &existingPart : outParts) {
			if (existingPart.partType == partType) {
				return existingPart;
			}
		}

		WallPartRecord part;
		part.partType = partType;
		part.sortOrder = sortOrder;
		outParts.push_back(part);
		return outParts.back();
	}

	bool PopulateWallPartContent(pugi::xml_node sourceNode, WallPartRecord &part) {
		const bool wasEmpty = !WallPartHasContent(part);
		CollectWallItemNodes(sourceNode, part.items);
		CollectWallDoorNodes(sourceNode, part.doors);
		return wasEmpty && WallPartHasContent(part);
	}

	void CollectNestedWallAlternates(pugi::xml_node wallNode, const wxString &basePartType, std::vector<WallPartRecord> &outParts, int &partSortOrder) {
		int localAlternateIndex = 0;
		for (pugi::xml_node subChild = wallNode.first_child(); subChild; subChild = subChild.next_sibling()) {
			if (as_lower_str(subChild.name()) != "alternate") {
				continue;
			}

			const wxString alternatePartType = basePartType + wxString::Format("/alternate/%d", localAlternateIndex++);
			auto &alternatePart = GetOrCreateWallPart(outParts, alternatePartType, partSortOrder);
			if (PopulateWallPartContent(subChild, alternatePart)) {
				++partSortOrder;
			}
		}
	}

	void CollectWallPartNode(pugi::xml_node childNode, std::vector<WallPartRecord> &outParts, int &partSortOrder) {
		const wxString partType = wxString(childNode.attribute("type").as_string(), wxConvUTF8);
		if (partType.IsEmpty()) {
			return;
		}

		auto &part = GetOrCreateWallPart(outParts, partType, partSortOrder);
		if (PopulateWallPartContent(childNode, part)) {
			++partSortOrder;
		}

		CollectNestedWallAlternates(childNode, part.partType, outParts, partSortOrder);
	}

	void CollectStandaloneWallAlternateNode(pugi::xml_node childNode, std::vector<WallPartRecord> &outParts, int &partSortOrder, int &alternateIndex) {
		WallPartRecord alternatePart;
		alternatePart.partType = wxString::Format("alternate/%d", alternateIndex++);
		alternatePart.sortOrder = partSortOrder;
		if (!PopulateWallPartContent(childNode, alternatePart)) {
			return;
		}

		++partSortOrder;
		outParts.push_back(alternatePart);
	}

	void AppendWallFriendLinks(pugi::xml_node childNode, std::vector<BrushLinkRecord> &outLinks, int &linkSortOrder) {
		const wxString targetName = wxString(childNode.attribute("name").as_string(), wxConvUTF8);
		if (targetName.IsEmpty()) {
			return;
		}

		BrushLinkRecord friendLink;
		friendLink.targetBrushName = targetName;
		friendLink.relationType = "friend";
		friendLink.sortOrder = linkSortOrder++;
		outLinks.push_back(friendLink);

		if (!childNode.attribute("redirect").as_bool()) {
			return;
		}

		BrushLinkRecord redirectLink;
		redirectLink.targetBrushName = targetName;
		redirectLink.relationType = "redirect";
		redirectLink.sortOrder = linkSortOrder++;
		outLinks.push_back(redirectLink);
	}

	bool TryAppendDoodadSingleItem(pugi::xml_node childNode, int &singleSortOrder, DoodadAlternativeRecord &alternative) {
		if (as_lower_str(childNode.name()) != "item") {
			return false;
		}

		const int itemId = childNode.attribute("id").as_int();
		if (itemId <= 0) {
			return true;
		}

		DoodadSingleItemRecord item;
		item.itemId = itemId;
		item.chance = childNode.attribute("chance").as_int();
		item.sortOrder = singleSortOrder++;
		alternative.singleItems.push_back(item);
		return true;
	}

	bool TryAppendDoodadCompositeTileItem(pugi::xml_node itemNode, int &tileItemSortOrder, DoodadCompositeTileRecord &tile) {
		if (as_lower_str(itemNode.name()) != "item") {
			return false;
		}

		const int itemId = itemNode.attribute("id").as_int();
		if (itemId <= 0) {
			return true;
		}

		DoodadCompositeTileItemRecord item;
		item.itemId = itemId;
		item.sortOrder = tileItemSortOrder++;
		tile.items.push_back(item);
		return true;
	}

	void CollectDoodadCompositeTileItems(pugi::xml_node tileNode, DoodadCompositeTileRecord &tile) {
		int tileItemSortOrder = 0;
		for (pugi::xml_node itemNode = tileNode.first_child(); itemNode; itemNode = itemNode.next_sibling()) {
			TryAppendDoodadCompositeTileItem(itemNode, tileItemSortOrder, tile);
		}
	}

	bool TryAppendDoodadCompositeTile(pugi::xml_node tileNode, int &tileSortOrder, DoodadCompositeRecord &composite) {
		if (as_lower_str(tileNode.name()) != "tile") {
			return false;
		}

		DoodadCompositeTileRecord tile;
		tile.offsetX = tileNode.attribute("x").as_int();
		tile.offsetY = tileNode.attribute("y").as_int();
		tile.offsetZ = tileNode.attribute("z").as_int();
		tile.sortOrder = tileSortOrder++;
		CollectDoodadCompositeTileItems(tileNode, tile);
		if (!tile.items.empty()) {
			composite.tiles.push_back(tile);
		}
		return true;
	}

	void CollectDoodadCompositeTiles(pugi::xml_node compositeNode, DoodadCompositeRecord &composite) {
		int tileSortOrder = 0;
		for (pugi::xml_node tileNode = compositeNode.first_child(); tileNode; tileNode = tileNode.next_sibling()) {
			TryAppendDoodadCompositeTile(tileNode, tileSortOrder, composite);
		}
	}

	bool TryAppendDoodadComposite(pugi::xml_node childNode, int &compositeSortOrder, DoodadAlternativeRecord &alternative) {
		if (as_lower_str(childNode.name()) != "composite") {
			return false;
		}

		DoodadCompositeRecord composite;
		composite.chance = childNode.attribute("chance").as_int();
		composite.sortOrder = compositeSortOrder++;
		CollectDoodadCompositeTiles(childNode, composite);
		if (!composite.tiles.empty()) {
			alternative.composites.push_back(composite);
		}
		return true;
	}

	bool ParseWallBrushNode(const FileName &sourceFile, pugi::xml_node brushNode, BrushRecord &outBrush, std::vector<WallPartRecord> &outParts, std::vector<BrushLinkRecord> &outLinks, wxArrayString &warnings) {
		if (wxString(brushNode.attribute("type").as_string(), wxConvUTF8) != "wall") {
			return false;
		}

		outBrush = BrushRecord();
		outBrush.name = wxString(brushNode.attribute("name").as_string(), wxConvUTF8);
		outBrush.type = "wall";
		outBrush.lookId = brushNode.attribute("lookid").as_int();
		outBrush.serverLookId = brushNode.attribute("server_lookid").as_int();
		outBrush.draggable = brushNode.attribute("draggable").as_bool();
		outBrush.onBlocking = brushNode.attribute("on_blocking").as_bool();
		outBrush.onDuplicate = brushNode.attribute("on_duplicate").as_bool();
		outBrush.redoBorders = brushNode.attribute("redo_borders").as_bool() || brushNode.attribute("reborder").as_bool();
		outBrush.removeOptionalBorder = brushNode.attribute("remove_optional_border").as_bool();
		outBrush.oneSize = brushNode.attribute("one_size").as_bool();
		outBrush.sourceFile = MaterialSourcePath(sourceFile);
		ParseThicknessString(wxString(brushNode.attribute("thickness").as_string(), wxConvUTF8), outBrush.thickness, outBrush.thicknessCeiling);

		if (outBrush.name.IsEmpty()) {
			warnings.push_back("SQLite wall import found wall brush without name in " + sourceFile.GetFullName());
			return false;
		}

		outParts.clear();
		outLinks.clear();

		int partSortOrder = 0;
		int alternateIndex = 0;
		int linkSortOrder = 0;
		for (pugi::xml_node childNode = brushNode.first_child(); childNode; childNode = childNode.next_sibling()) {
			const std::string childName = as_lower_str(childNode.name());
			if (childName == "wall") {
				CollectWallPartNode(childNode, outParts, partSortOrder);
				continue;
			}
			if (childName == "alternate") {
				CollectStandaloneWallAlternateNode(childNode, outParts, partSortOrder, alternateIndex);
				continue;
			}
			if (childName == "friend") {
				AppendWallFriendLinks(childNode, outLinks, linkSortOrder);
			}
		}

		return true;
	}

	bool ImportWallBrushesFile(const FileName &wallsFile, wxArrayString &warnings) {
		std::set<wxString> visited;
		return ImportWallBrushesRecursive(wallsFile, warnings, visited);
	}

	bool ImportWallBrushesRecursive(const FileName &wallsFile, wxArrayString &warnings, std::set<wxString> &visited) {
		const wxString normalizedPath = wallsFile.GetFullPath();
		if (visited.find(normalizedPath) != visited.end()) {
			return true;
		}
		visited.insert(normalizedPath);

		pugi::xml_document doc;
		pugi::xml_node materialsNode;
		if (!LoadMaterialsDocumentRoot(wallsFile, "SQLite wall import", doc, materialsNode, warnings)) {
			return false;
		}

		for (pugi::xml_node brushNode = materialsNode.first_child(); brushNode; brushNode = brushNode.next_sibling()) {
			const std::string childName = as_lower_str(brushNode.name());
			if (childName == "include") {
				const wxString includePath = wxString(brushNode.attribute("file").as_string(), wxConvUTF8);
				if (!includePath.empty() && !ImportWallBrushesRecursive(ResolveMaterialInclude(wallsFile, includePath), warnings, visited)) {
					return false;
				}
				continue;
			}

			if (childName != "brush") {
				continue;
			}

			BrushRecord brush;
			std::vector<WallPartRecord> parts;
			std::vector<BrushLinkRecord> links;
			if (!ParseWallBrushNode(wallsFile, brushNode, brush, parts, links, warnings)) {
				continue;
			}

			int64_t brushId = 0;
			if (!g_brush_database.upsertBrush(brush, brushId)) {
				warnings.push_back("SQLite wall import failed for brush \"" + brush.name + "\": " + g_brush_database.getLastError());
				return false;
			}

			if (!g_brush_database.replaceWallParts(brushId, parts)) {
				warnings.push_back("SQLite wall parts import failed for brush \"" + brush.name + "\": " + g_brush_database.getLastError());
				return false;
			}

			for (BrushLinkRecord &link : links) {
				link.brushId = brushId;
			}
			if (!g_brush_database.replaceBrushLinks(brushId, links)) {
				warnings.push_back("SQLite wall links import failed for brush \"" + brush.name + "\": " + g_brush_database.getLastError());
				return false;
			}
		}

		return true;
	}

	void CollectDoodadAlternativeContent(pugi::xml_node sourceNode, DoodadAlternativeRecord &alternative) {
		auto singleSortOrder = static_cast<int>(alternative.singleItems.size());
		auto compositeSortOrder = static_cast<int>(alternative.composites.size());
		for (pugi::xml_node childNode = sourceNode.first_child(); childNode; childNode = childNode.next_sibling()) {
			if (TryAppendDoodadSingleItem(childNode, singleSortOrder, alternative)) {
				continue;
			}
			TryAppendDoodadComposite(childNode, compositeSortOrder, alternative);
		}
	}

	bool NodeHasDoodadContent(pugi::xml_node node) {
		for (pugi::xml_node childNode = node.first_child(); childNode; childNode = childNode.next_sibling()) {
			const std::string childName = as_lower_str(childNode.name());
			if (childName == "item" || childName == "composite") {
				return true;
			}
		}
		return false;
	}

	bool ParseDoodadBrushNode(const FileName &sourceFile, pugi::xml_node brushNode, BrushRecord &outBrush, std::vector<DoodadAlternativeRecord> &outAlternatives, wxArrayString &warnings) {
		if (wxString(brushNode.attribute("type").as_string(), wxConvUTF8) != "doodad") {
			return false;
		}

		outBrush = BrushRecord();
		outBrush.name = wxString(brushNode.attribute("name").as_string(), wxConvUTF8);
		outBrush.type = "doodad";
		outBrush.lookId = brushNode.attribute("lookid").as_int();
		outBrush.serverLookId = brushNode.attribute("server_lookid").as_int();
		outBrush.draggable = brushNode.attribute("draggable").as_bool();
		outBrush.onBlocking = brushNode.attribute("on_blocking").as_bool();
		outBrush.onDuplicate = brushNode.attribute("on_duplicate").as_bool();
		outBrush.redoBorders = brushNode.attribute("redo_borders").as_bool() || brushNode.attribute("reborder").as_bool();
		outBrush.removeOptionalBorder = brushNode.attribute("remove_optional_border").as_bool();
		outBrush.oneSize = brushNode.attribute("one_size").as_bool();
		outBrush.sourceFile = MaterialSourcePath(sourceFile);
		ParseThicknessString(wxString(brushNode.attribute("thickness").as_string(), wxConvUTF8), outBrush.thickness, outBrush.thicknessCeiling);

		if (outBrush.name.IsEmpty()) {
			warnings.push_back("SQLite doodad import found brush without name in " + sourceFile.GetFullName());
			return false;
		}
		if (outBrush.removeOptionalBorder && !outBrush.redoBorders) {
			warnings.push_back("SQLite doodad import warning: remove_optional_border requires redo_borders (reborder) to take effect in the runtime for brush \"" + outBrush.name + "\".");
		}

		outAlternatives.clear();
		int sortOrder = 0;
		for (pugi::xml_node childNode = brushNode.first_child(); childNode; childNode = childNode.next_sibling()) {
			if (as_lower_str(childNode.name()) != "alternate") {
				continue;
			}

			DoodadAlternativeRecord alternative;
			alternative.sortOrder = sortOrder++;
			CollectDoodadAlternativeContent(childNode, alternative);
			outAlternatives.push_back(alternative);
		}

		if (NodeHasDoodadContent(brushNode)) {
			if (outAlternatives.empty()) {
				DoodadAlternativeRecord alternative;
				alternative.sortOrder = sortOrder++;
				CollectDoodadAlternativeContent(brushNode, alternative);
				outAlternatives.push_back(alternative);
			} else {
				CollectDoodadAlternativeContent(brushNode, outAlternatives.back());
			}
		}

		return true;
	}

	struct AlignedBrushParseOptions {
		wxString expectedBrushType;
		const char* containerNodeName = nullptr;
		bool allowDirectIdFallback = false;
	};

	template <typename NodeRecord, typename ItemRecord>
	bool ParseAlignedBrushNode(
		const FileName &sourceFile,
		pugi::xml_node brushNode,
		const AlignedBrushParseOptions &options,
		BrushRecord &outBrush,
		std::vector<NodeRecord> &outNodes,
		wxArrayString &warnings
	) {
		if (wxString(brushNode.attribute("type").as_string(), wxConvUTF8) != options.expectedBrushType) {
			return false;
		}

		outBrush = BrushRecord();
		outBrush.name = wxString(brushNode.attribute("name").as_string(), wxConvUTF8);
		outBrush.type = options.expectedBrushType;
		outBrush.lookId = brushNode.attribute("lookid").as_int();
		outBrush.serverLookId = brushNode.attribute("server_lookid").as_int();
		outBrush.sourceFile = MaterialSourcePath(sourceFile);

		if (outBrush.name.IsEmpty()) {
			warnings.push_back("SQLite " + options.expectedBrushType + " import found brush without name in " + sourceFile.GetFullName());
			return false;
		}

		outNodes.clear();
		int nodeSortOrder = 0;
		for (pugi::xml_node childNode = brushNode.first_child(); childNode; childNode = childNode.next_sibling()) {
			if (as_lower_str(childNode.name()) != options.containerNodeName) {
				continue;
			}

			NodeRecord node;
			node.align = wxString(childNode.attribute("align").as_string(), wxConvUTF8);
			node.sortOrder = nodeSortOrder++;

			int itemSortOrder = 0;
			bool hasNestedItems = false;
			for (pugi::xml_node itemNode = childNode.first_child(); itemNode; itemNode = itemNode.next_sibling()) {
				if (as_lower_str(itemNode.name()) != "item") {
					continue;
				}

				hasNestedItems = true;
				const int itemId = itemNode.attribute("id").as_int();
				if (itemId <= 0) {
					continue;
				}

				ItemRecord item;
				item.itemId = itemId;
				item.chance = itemNode.attribute("chance").as_int();
				item.sortOrder = itemSortOrder++;
				node.items.push_back(item);
			}

			if (options.allowDirectIdFallback && !hasNestedItems) {
				const int itemId = childNode.attribute("id").as_int();
				if (itemId > 0) {
					ItemRecord item;
					item.itemId = itemId;
					item.chance = 1;
					item.sortOrder = 0;
					node.items.push_back(item);
				}
			}

			if (!node.items.empty()) {
				outNodes.push_back(node);
			}
		}

		return true;
	}

	bool ParseCarpetBrushNode(const FileName &sourceFile, pugi::xml_node brushNode, BrushRecord &outBrush, std::vector<CarpetNodeRecord> &outNodes, wxArrayString &warnings) {
		static const AlignedBrushParseOptions options = { "carpet", "carpet", true };
		return ParseAlignedBrushNode<CarpetNodeRecord, CarpetNodeItemRecord>(
			sourceFile,
			brushNode,
			options,
			outBrush,
			outNodes,
			warnings
		);
	}

	bool ParseTableBrushNode(const FileName &sourceFile, pugi::xml_node brushNode, BrushRecord &outBrush, std::vector<TableNodeRecord> &outNodes, wxArrayString &warnings) {
		static const AlignedBrushParseOptions options = { "table", "table", false };
		return ParseAlignedBrushNode<TableNodeRecord, TableNodeItemRecord>(
			sourceFile,
			brushNode,
			options,
			outBrush,
			outNodes,
			warnings
		);
	}

	bool IsSupportedTilesetSectionType(const std::string &nodeName) {
		return nodeName == "terrain" || nodeName == "terrain_and_raw" || nodeName == "doodad" || nodeName == "doodad_and_raw" || nodeName == "items" || nodeName == "items_and_raw" || nodeName == "raw";
	}

	wxString DerivePaletteGroupNameFromSectionType(const wxString &sectionType) {
		if (sectionType.IsSameAs("terrain", false) || sectionType.IsSameAs("terrain_and_raw", false)) {
			return "terrain";
		}
		if (sectionType.IsSameAs("doodad", false) || sectionType.IsSameAs("doodad_and_raw", false)) {
			return "doodad";
		}
		if (sectionType.IsSameAs("item", false) || sectionType.IsSameAs("items", false) || sectionType.IsSameAs("items_and_raw", false)) {
			return "item";
		}
		return "other";
	}

	void AppendNormalizedTilesetItemEntries(const TilesetEntryRecord &sourceEntry, TilesetSectionRecord &section, int &sortOrder) {
		const int fromItemId = sourceEntry.itemId > 0 ? sourceEntry.itemId : sourceEntry.fromItemId;
		int toItemId = sourceEntry.toItemId > 0 ? sourceEntry.toItemId : fromItemId;
		if (fromItemId <= 0) {
			return;
		}

		toItemId = std::max(fromItemId, toItemId);
		int previousItemId = 0;
		for (int itemId = fromItemId; itemId <= toItemId; ++itemId) {
			TilesetEntryRecord normalizedEntry = sourceEntry;
			normalizedEntry.itemId = itemId;
			normalizedEntry.fromItemId = 0;
			normalizedEntry.toItemId = 0;
			normalizedEntry.sortOrder = sortOrder++;

			if (previousItemId > 0) {
				normalizedEntry.afterBrushName.clear();
				normalizedEntry.afterItemId = previousItemId;
			}

			section.entries.push_back(std::move(normalizedEntry));
			previousItemId = itemId;
		}
	}

	void CollectTilesetSectionEntries(
		const FileName &sourceFile,
		const wxString &tilesetName,
		pugi::xml_node sectionNode,
		TilesetSectionRecord &section,
		wxArrayString &warnings
	) {
		int sortOrder = 0;
		for (pugi::xml_node entryNode = sectionNode.first_child(); entryNode; entryNode = entryNode.next_sibling()) {
			const std::string entryNodeName = as_lower_str(entryNode.name());
			if (entryNodeName != "brush" && entryNodeName != "item") {
				warnings.push_back(
					"SQLite tileset import skipped unsupported entry <" + wxString::FromUTF8(entryNodeName.c_str()) + "> "
					"in tileset \"" + tilesetName + "\" (" + sourceFile.GetFullName() + ")."
				);
				continue;
			}

			TilesetEntryRecord entry;
			entry.entryKind = wxString::FromUTF8(entryNodeName.c_str());
			entry.brushName = wxString(entryNode.attribute("name").as_string(), wxConvUTF8);
			entry.itemId = entryNode.attribute("id").as_int();
			entry.fromItemId = entryNode.attribute("fromid").as_int();
			entry.toItemId = entryNode.attribute("toid").as_int();
			entry.afterBrushName = wxString(entryNode.attribute("after").as_string(), wxConvUTF8);
			entry.afterItemId = entryNode.attribute("afteritem").as_int();
			if (entry.toItemId == 0) {
				entry.toItemId = entry.fromItemId != 0 ? entry.fromItemId : entry.itemId;
			}
			entry.sortOrder = sortOrder;

			if (entry.entryKind == "brush" && !entry.brushName.IsEmpty()) {
				entry.sortOrder = sortOrder++;
				section.entries.push_back(entry);
			} else if (entry.entryKind == "item" && (entry.itemId > 0 || entry.fromItemId > 0)) {
				AppendNormalizedTilesetItemEntries(entry, section, sortOrder);
			}
		}
	}

	bool ParseTilesetNode(const FileName &sourceFile, pugi::xml_node tilesetNode, const wxString &inheritedPaletteGroup, TilesetStorageRecord &outTileset, wxArrayString &warnings) {
		outTileset = TilesetStorageRecord();
		outTileset.name = wxString(tilesetNode.attribute("name").as_string(), wxConvUTF8);
		outTileset.sourceFile = MaterialSourcePath(sourceFile);
		outTileset.paletteGroupName = wxString(tilesetNode.attribute("palette_group").as_string(), wxConvUTF8);
		if (outTileset.paletteGroupName.IsEmpty()) {
			outTileset.paletteGroupName = inheritedPaletteGroup;
		}
		if (outTileset.name.IsEmpty()) {
			warnings.push_back("SQLite tileset import found tileset without name in " + sourceFile.GetFullName());
			return false;
		}

		int sectionSortOrder = 0;
		for (pugi::xml_node childNode = tilesetNode.first_child(); childNode; childNode = childNode.next_sibling()) {
			const std::string sectionNodeName = as_lower_str(childNode.name());
			if (!IsSupportedTilesetSectionType(sectionNodeName)) {
				warnings.push_back(
					"SQLite tileset import skipped unsupported section <" + wxString::FromUTF8(sectionNodeName.c_str()) + "> "
					"in tileset \"" + outTileset.name + "\" (" + sourceFile.GetFullName() + ")."
				);
				continue;
			}

			TilesetSectionRecord section;
			section.sectionType = wxString::FromUTF8(sectionNodeName.c_str());
			section.sortOrder = sectionSortOrder++;
			CollectTilesetSectionEntries(sourceFile, outTileset.name, childNode, section, warnings);
			outTileset.sections.push_back(section);
		}

		if (outTileset.paletteGroupName.IsEmpty()) {
			if (!outTileset.sections.empty()) {
				outTileset.paletteGroupName = DerivePaletteGroupNameFromSectionType(outTileset.sections.front().sectionType);
			} else {
				outTileset.paletteGroupName = "other";
			}
		}

		return true;
	}

	bool ImportTilesetsRecursive(const FileName &filename, const wxString &inheritedPaletteGroup, wxArrayString &warnings, std::set<wxString> &visited, std::vector<TilesetStorageRecord> &outTilesets) {
		const wxString normalizedPath = filename.GetFullPath();
		if (visited.find(normalizedPath) != visited.end()) {
			return true;
		}
		visited.insert(normalizedPath);

		pugi::xml_document doc;
		pugi::xml_node materialsNode;
		if (!LoadMaterialsDocumentRoot(filename, "SQLite tileset import", doc, materialsNode, warnings)) {
			return false;
		}

		for (pugi::xml_node childNode = materialsNode.first_child(); childNode; childNode = childNode.next_sibling()) {
			const std::string childName = as_lower_str(childNode.name());
			if (childName == "include") {
				const wxString includePath = wxString(childNode.attribute("file").as_string(), wxConvUTF8);
				wxString includePaletteGroup = wxString(childNode.attribute("palette_group").as_string(), wxConvUTF8);
				if (includePaletteGroup.IsEmpty()) {
					includePaletteGroup = inheritedPaletteGroup;
				}
				if (!includePath.empty() && !ImportTilesetsRecursive(ResolveMaterialInclude(filename, includePath), includePaletteGroup, warnings, visited, outTilesets)) {
					return false;
				}
				continue;
			}

			if (childName != "tileset") {
				continue;
			}

			TilesetStorageRecord tileset;
			if (ParseTilesetNode(filename, childNode, inheritedPaletteGroup, tileset, warnings)) {
				outTilesets.push_back(tileset);
			}
		}

		return true;
	}

	enum class DecorativeBrushKind {
		None,
		Doodad,
		Carpet,
		Table,
	};

	struct ParsedDecorativeBrush {
		DecorativeBrushKind kind = DecorativeBrushKind::None;
		BrushRecord brush;
		std::vector<DoodadAlternativeRecord> doodadAlternatives;
		std::vector<CarpetNodeRecord> carpetNodes;
		std::vector<TableNodeRecord> tableNodes;
	};

	bool ParseDecorativeBrushNode(const FileName &filename, pugi::xml_node brushNode, ParsedDecorativeBrush &outParsed, wxArrayString &warnings) {
		using enum DecorativeBrushKind;

		outParsed = ParsedDecorativeBrush();
		if (ParseDoodadBrushNode(filename, brushNode, outParsed.brush, outParsed.doodadAlternatives, warnings)) {
			outParsed.kind = Doodad;
			return true;
		}
		if (ParseCarpetBrushNode(filename, brushNode, outParsed.brush, outParsed.carpetNodes, warnings)) {
			outParsed.kind = Carpet;
			return true;
		}
		if (ParseTableBrushNode(filename, brushNode, outParsed.brush, outParsed.tableNodes, warnings)) {
			outParsed.kind = Table;
			return true;
		}
		return false;
	}

	bool StoreDecorativeBrushContent(const ParsedDecorativeBrush &parsed, int64_t brushId, wxArrayString &warnings) {
		using enum DecorativeBrushKind;

		const wxString &brushName = parsed.brush.name;
		switch (parsed.kind) {
			case Doodad:
				if (g_brush_database.replaceDoodadAlternatives(brushId, parsed.doodadAlternatives)) {
					return true;
				}
				warnings.push_back("SQLite doodad import failed for brush \"" + brushName + "\": " + g_brush_database.getLastError());
				return false;
			case Carpet:
				if (g_brush_database.replaceCarpetNodes(brushId, parsed.carpetNodes)) {
					return true;
				}
				warnings.push_back("SQLite carpet import failed for brush \"" + brushName + "\": " + g_brush_database.getLastError());
				return false;
			case Table:
				if (g_brush_database.replaceTableNodes(brushId, parsed.tableNodes)) {
					return true;
				}
				warnings.push_back("SQLite table import failed for brush \"" + brushName + "\": " + g_brush_database.getLastError());
				return false;
			case None:
				return false;
		}

		return false;
	}

	bool ImportDecorativeBrushesRecursive(const FileName &filename, wxArrayString &warnings, std::set<wxString> &visited) {
		const wxString normalizedPath = filename.GetFullPath();
		if (visited.find(normalizedPath) != visited.end()) {
			return true;
		}
		visited.insert(normalizedPath);

		pugi::xml_document doc;
		pugi::xml_node materialsNode;
		if (!LoadMaterialsDocumentRoot(filename, "SQLite decorative import", doc, materialsNode, warnings)) {
			return false;
		}

		for (pugi::xml_node childNode = materialsNode.first_child(); childNode; childNode = childNode.next_sibling()) {
			const std::string childName = as_lower_str(childNode.name());
			if (childName == "include") {
				const wxString includePath = wxString(childNode.attribute("file").as_string(), wxConvUTF8);
				if (!includePath.empty() && !ImportDecorativeBrushesRecursive(ResolveMaterialInclude(filename, includePath), warnings, visited)) {
					return false;
				}
				continue;
			}
			if (childName != "brush") {
				continue;
			}

			ParsedDecorativeBrush parsed;
			if (!ParseDecorativeBrushNode(filename, childNode, parsed, warnings)) {
				continue;
			}

			int64_t brushId = 0;
			if (!g_brush_database.upsertBrush(parsed.brush, brushId)) {
				warnings.push_back("SQLite decorative import failed for brush \"" + parsed.brush.name + "\": " + g_brush_database.getLastError());
				return false;
			}

			if (!StoreDecorativeBrushContent(parsed, brushId, warnings)) {
				return false;
			}

			if (!g_brush_database.replaceBrushLinks(brushId, {})) {
				warnings.push_back("SQLite decorative link cleanup failed for brush \"" + parsed.brush.name + "\": " + g_brush_database.getLastError());
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
	clearTilesets();
}

void Materials::clearTilesets() {
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

bool Materials::shouldSkipSqliteBootstrapImports(bool &outSkip, wxString &reason) {
	outSkip = false;
	reason.clear();

	if (!g_settings.getBoolean(Config::USE_SQLITE_MATERIALS)) {
		return true;
	}
	if (!g_brush_database.isOpen()) {
		reason = "SQLite brush database is not open.";
		return false;
	}

	wxString readinessReason;
	if (!g_brush_database.hasCompleteImportForCurrentSchema(outSkip, readinessReason)) {
		reason = g_brush_database.getLastError();
		return false;
	}

	if (outSkip) {
		MaterialsImportStatusRecord status;
		wxString statusReason;
		if (g_brush_database.getMaterialsImportStatus(status, statusReason) && status.completed) {
			reason = wxString::Format(
				"SQLite materials import skipped: import marker complete (source=%s, completed_at=%lld).",
				status.source,
				static_cast<long long>(status.completedAt)
			);
		} else {
			reason = wxString::Format("SQLite materials import skipped: materials.db already matches schema version %d and contains imported data.", g_brush_database.getExpectedSchemaVersion());
		}
	} else {
		if (readinessReason.IsEmpty()) {
			readinessReason = wxString::Format("Missing imported data for schema version %d.", g_brush_database.getExpectedSchemaVersion());
		}
		reason = "SQLite materials import required: " + readinessReason;
	}
	return true;
}

bool Materials::bootstrapSqliteDatabase(wxString &error, wxArrayString &warnings) {
	if (!g_settings.getBoolean(Config::USE_SQLITE_MATERIALS)) {
		return true;
	}
	if (!g_brush_database.isOpen()) {
		error = "SQLite brush database is not open.";
		return false;
	}

	const bool imported = g_brush_database.runInTransaction([&]() {
		if (!migrateGroundsToSQLite(error, warnings)) {
			return false;
		}
		if (!migrateWallsToSQLite(error, warnings)) {
			return false;
		}
		if (!migrateDecorativeBrushesToSQLite(error, warnings)) {
			return false;
		}
		if (!migrateTilesetsToSQLite(error, warnings)) {
			return false;
		}
		if (!g_brush_database.resolveGroundReferenceNames()) {
			error = g_brush_database.getLastError();
			return false;
		}
		MaterialsDatabaseAuditReport report;
		if (g_brush_database.generateAuditReport(report)) {
			if (report.unresolvedGroundTargets > 0
				|| report.unresolvedBrushLinks > 0
				|| report.unresolvedTilesetEntries > 0
				|| report.unresolvedCaseMatchBorderIds > 0
				|| report.unresolvedCaseReplaceBorderTargetIds > 0
				) {
				warnings.push_back(wxString::Format(
					"SQLite materials import completed with unresolved references (ground targets=%d, brush links=%d, tileset entries=%d, match_border ids=%d, replace_border target ids=%d).",
					report.unresolvedGroundTargets,
					report.unresolvedBrushLinks,
					report.unresolvedTilesetEntries,
					report.unresolvedCaseMatchBorderIds,
					report.unresolvedCaseReplaceBorderTargetIds
				));
			}
		} else {
			warnings.push_back("SQLite materials import completed, but audit report generation failed: " + g_brush_database.getLastError());
		}
		if (!g_brush_database.markMaterialsImportComplete("xml_bootstrap")) {
			error = g_brush_database.getLastError();
			return false;
		}
		return true;
	});

	if (!imported && error.IsEmpty()) {
		error = g_brush_database.getLastError();
	}

	return imported;
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
	// Ground brushes are not confined to grounds.xml in the current dataset, so import
	// every ground brush reachable from the shared brushs.xml include tree.
	const FileName brushsRoot(GUI::GetDataDirectory() + "materials/brushs.xml");

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
	if (!ImportGroundBrushesFile(brushsRoot, warnings)) {
		error = "Failed to import ground brushes into SQLite.";
		return false;
	}
	if (!g_brush_database.resolveGroundReferenceNames()) {
		error = g_brush_database.getLastError();
		return false;
	}

	spdlog::info("SQLite ground import completed from {}", brushsRoot.GetFullPath().ToStdString());
	return true;
}

bool Materials::migrateWallsToSQLite(wxString &error, wxArrayString &warnings) {
	if (!g_settings.getBoolean(Config::USE_SQLITE_MATERIALS)) {
		return true;
	}
	if (!g_brush_database.isOpen()) {
		error = "SQLite brush database is not open.";
		return false;
	}

	// Wall brushes are not confined to walls.xml in the current dataset, so import
	// every wall brush reachable from the shared brushs.xml include tree.
	const FileName brushsRoot(GUI::GetDataDirectory() + "materials/brushs.xml");
	if (!g_brush_database.deleteBrushesByType("wall")) {
		error = g_brush_database.getLastError();
		return false;
	}
	if (!ImportWallBrushesFile(brushsRoot, warnings)) {
		error = "Failed to import wall brushes into SQLite.";
		return false;
	}
	if (!g_brush_database.resolveGroundReferenceNames()) {
		error = g_brush_database.getLastError();
		return false;
	}

	spdlog::info("SQLite wall import completed from {}", brushsRoot.GetFullPath().ToStdString());
	return true;
}

bool Materials::migrateDecorativeBrushesToSQLite(wxString &error, wxArrayString &warnings) {
	if (!g_settings.getBoolean(Config::USE_SQLITE_MATERIALS)) {
		return true;
	}
	if (!g_brush_database.isOpen()) {
		error = "SQLite brush database is not open.";
		return false;
	}

	const FileName brushsRoot(GUI::GetDataDirectory() + "materials/brushs.xml");
	if (!g_brush_database.deleteBrushesByType("doodad")) {
		error = g_brush_database.getLastError();
		return false;
	}
	if (!g_brush_database.deleteBrushesByType("carpet")) {
		error = g_brush_database.getLastError();
		return false;
	}
	if (!g_brush_database.deleteBrushesByType("table")) {
		error = g_brush_database.getLastError();
		return false;
	}

	std::set<wxString> visited;
	if (!ImportDecorativeBrushesRecursive(brushsRoot, warnings, visited)) {
		error = "Failed to import doodad/carpet/table brushes into SQLite.";
		return false;
	}

	spdlog::info("SQLite decorative brush import completed from {}", brushsRoot.GetFullPath().ToStdString());
	return true;
}

bool Materials::migrateTilesetsToSQLite(wxString &error, wxArrayString &warnings) {
	if (!g_settings.getBoolean(Config::USE_SQLITE_MATERIALS)) {
		return true;
	}
	if (!g_brush_database.isOpen()) {
		error = "SQLite brush database is not open.";
		return false;
	}

	const FileName tilesetsRoot(GUI::GetDataDirectory() + "materials/tilesets.xml");
	std::vector<TilesetStorageRecord> tilesetsToStore;
	std::set<wxString> visited;
	if (!ImportTilesetsRecursive(tilesetsRoot, wxString(), warnings, visited, tilesetsToStore)) {
		error = "Failed to import tilesets into SQLite.";
		return false;
	}

	const wxString tilesetsDirectory = GUI::GetDataDirectory() + "materials/tilesets";
	if (wxDir::Exists(tilesetsDirectory)) {
		wxArrayString tilesetFiles;
		wxDir::GetAllFiles(tilesetsDirectory, &tilesetFiles, "*.xml", wxDIR_FILES | wxDIR_DIRS | wxDIR_HIDDEN);
		for (const auto &tilesetPath : tilesetFiles) {
			const FileName tilesetFile(tilesetPath);
			const wxString normalizedTilesetPath = tilesetFile.GetFullPath();
			if (visited.find(normalizedTilesetPath) == visited.end()) {
				warnings.push_back("SQLite tileset import found tileset file not referenced by tilesets.xml: " + MaterialSourcePath(tilesetFile));
			}
			if (!ImportTilesetsRecursive(FileName(tilesetPath), wxString(), warnings, visited, tilesetsToStore)) {
				error = "Failed to import tilesets into SQLite.";
				return false;
			}
		}
	}

	std::set<wxString> seenTilesetNames;
	std::vector<TilesetStorageRecord> uniqueTilesets;
	uniqueTilesets.reserve(tilesetsToStore.size());
	for (const auto &tileset : tilesetsToStore) {
		wxString key = tileset.name;
		key.MakeLower();
		if (seenTilesetNames.insert(key).second) {
			uniqueTilesets.push_back(tileset);
			continue;
		}
		warnings.push_back("SQLite tileset import found duplicate tileset name \"" + tileset.name + "\" in " + tileset.sourceFile + ". Keeping the first instance.");
	}

	if (!g_brush_database.replaceAllTilesets(uniqueTilesets)) {
		error = g_brush_database.getLastError();
		return false;
	}

	spdlog::info("SQLite tileset import completed from {}", tilesetsRoot.GetFullPath().ToStdString());
	return true;
}

bool Materials::loadTilesetsFromDatabase(wxArrayString &warnings) {
	if (!g_settings.getBoolean(Config::USE_SQLITE_MATERIALS)) {
		return true;
	}
	if (!g_brush_database.isOpen()) {
		warnings.push_back("SQLite tileset load skipped because the brush database is not open.");
		return false;
	}

	std::vector<TilesetStorageRecord> tilesetsFromDatabase;
	if (!g_brush_database.getAllTilesets(tilesetsFromDatabase)) {
		warnings.push_back("Failed to load tilesets from SQLite: " + g_brush_database.getLastError());
		return false;
	}

	clearTilesets();
	for (const TilesetStorageRecord &storage : tilesetsFromDatabase) {
		const std::string tilesetName = storage.name.ToStdString();
		Tileset* tileset = newd Tileset(g_brushes, tilesetName);
		tileset->loadFromStorage(storage, warnings);
		tilesets.insert(std::make_pair(tilesetName, tileset));
	}

	spdlog::info("Loaded {} tilesets from SQLite materials database", tilesetsFromDatabase.size());
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
