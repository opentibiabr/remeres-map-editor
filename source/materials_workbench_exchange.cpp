#include "main.h"

#include "materials_workbench_exchange.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "brush_database.h"
#include "materials_workbench_controller.h"

namespace {
	nlohmann::json ExportBorderSetEntity(int xmlBorderId, wxString &error) {
		BorderSetRecord borderSet;
		if (!g_brush_database.findBorderSetByXmlBorderId(xmlBorderId, borderSet) || borderSet.id <= 0) {
			error = wxString::Format("Border %d not found in materials.db.", xmlBorderId);
			return nlohmann::json();
		}

		std::vector<BorderSetItemRecord> items;
		if (!g_brush_database.getBorderSetItems(borderSet.id, items)) {
			error = g_brush_database.getLastError();
			return nlohmann::json();
		}

		nlohmann::json entity;
		entity["kind"] = "border_set";
		entity["key"] = { { "xmlBorderId", borderSet.xmlBorderId } };
		entity["borderSet"] = {
			{ "xmlBorderId", borderSet.xmlBorderId },
			{ "borderScope", "global" },
			{ "borderType", borderSet.borderType.ToStdString() },
			{ "borderGroup", borderSet.borderGroup },
			{ "groundEquivalent", borderSet.groundEquivalent },
		};

		nlohmann::json outItems = nlohmann::json::array();
		for (const BorderSetItemRecord &item : items) {
			outItems.push_back({
				{ "edge", item.edge.ToStdString() },
				{ "itemId", item.itemId },
				{ "sortOrder", item.sortOrder },
			});
		}
		entity["items"] = std::move(outItems);
		return entity;
	}

	nlohmann::json ExportPaletteGroupEntity(const PaletteGroupRecord &group) {
		nlohmann::json entity;
		entity["kind"] = "palette_group";
		entity["key"] = { { "name", group.name.ToStdString() } };
		entity["group"] = {
			{ "name", group.name.ToStdString() },
			{ "runtimeFamily", group.runtimeFamily.ToStdString() },
			{ "sortOrder", group.sortOrder },
			{ "isBuiltin", group.isBuiltin },
		};
		return entity;
	}

	nlohmann::json ExportPaletteEntity(const TilesetStorageRecord &tileset) {
		nlohmann::json entity;
		entity["kind"] = "palette";
		entity["key"] = { { "name", tileset.name.ToStdString() } };
		entity["palette"] = {
			{ "name", tileset.name.ToStdString() },
			{ "paletteGroupName", tileset.paletteGroupName.ToStdString() },
			{ "paletteGroupRuntimeFamily", tileset.paletteGroupRuntimeFamily.ToStdString() },
		};

		nlohmann::json sections = nlohmann::json::array();
		for (const TilesetSectionRecord &section : tileset.sections) {
			nlohmann::json entries = nlohmann::json::array();
			for (const TilesetEntryRecord &entry : section.entries) {
				nlohmann::json e;
				e["entryKind"] = entry.entryKind.ToStdString();
				e["brushName"] = entry.brushName.ToStdString();
				e["brushId"] = entry.brushId;
				e["itemId"] = entry.itemId;
				e["fromItemId"] = entry.fromItemId;
				e["toItemId"] = entry.toItemId;
				e["afterBrushName"] = entry.afterBrushName.ToStdString();
				e["afterItemId"] = entry.afterItemId;
				e["sortOrder"] = entry.sortOrder;
				entries.push_back(std::move(e));
			}
			sections.push_back({
				{ "sectionType", section.sectionType.ToStdString() },
				{ "sortOrder", section.sortOrder },
				{ "entries", std::move(entries) },
			});
		}
		entity["sections"] = std::move(sections);
		return entity;
	}

	BrushRecord ResolveBrushByIdOrName(int64_t id, const wxString &typeHint, const wxString &nameHint) {
		BrushRecord brush;
		if (id > 0 && g_brush_database.getBrushById(id, brush) && brush.id > 0) {
			return brush;
		}
		if (!typeHint.IsEmpty() && !nameHint.IsEmpty() && g_brush_database.findBrushByNameAndType(nameHint, typeHint, brush) && brush.id > 0) {
			return brush;
		}
		if (!nameHint.IsEmpty() && !typeHint.IsEmpty()) {
			return BrushRecord();
		}
		return BrushRecord();
	}

	nlohmann::json ExportBrushKey(const BrushRecord &brush) {
		return {
			{ "type", brush.type.ToStdString() },
			{ "name", brush.name.ToStdString() },
		};
	}

	bool TryLoadCompleteBrushStorage(int64_t brushId, BrushStorageRecord &outStorage, wxString &error) {
		if (!g_brush_database.getCompleteBrushById(brushId, outStorage) || outStorage.brush.id <= 0) {
			error = wxString::Format("Brush #%lld not found in materials.db.", static_cast<long long>(brushId));
			return false;
		}
		error.clear();
		return true;
	}

	nlohmann::json ExportBrushRecordJson(const BrushRecord &brush) {
		return {
			{ "type", brush.type.ToStdString() },
			{ "name", brush.name.ToStdString() },
			{ "lookId", brush.lookId },
			{ "serverLookId", brush.serverLookId },
			{ "zOrder", brush.zOrder },
			{ "draggable", brush.draggable },
			{ "onBlocking", brush.onBlocking },
			{ "onDuplicate", brush.onDuplicate },
			{ "redoBorders", brush.redoBorders },
			{ "randomize", brush.randomize },
			{ "oneSize", brush.oneSize },
			{ "thickness", brush.thickness },
			{ "thicknessCeiling", brush.thicknessCeiling },
			{ "soloOptional", brush.soloOptional },
			{ "removeOptionalBorder", brush.removeOptionalBorder },
		};
	}

	nlohmann::json ExportBrushItemsJson(const std::vector<BrushItemRecord> &items) {
		nlohmann::json out = nlohmann::json::array();
		for (const BrushItemRecord &item : items) {
			out.push_back({
				{ "itemId", item.itemId },
				{ "chance", item.chance },
				{ "sortOrder", item.sortOrder },
			});
		}
		return out;
	}

	nlohmann::json ExportBrushLinksJson(const std::vector<BrushLinkRecord> &links) {
		nlohmann::json out = nlohmann::json::array();
		for (const BrushLinkRecord &link : links) {
			nlohmann::json row;
			row["relationType"] = link.relationType.ToStdString();
			row["sortOrder"] = link.sortOrder;
			if (!link.targetBrushName.IsEmpty()) {
				row["targetName"] = link.targetBrushName.ToStdString();
			}
			if (link.targetBrushId > 0) {
				BrushRecord target;
				if (g_brush_database.getBrushById(link.targetBrushId, target) && target.id > 0) {
					row["target"] = ExportBrushKey(target);
				}
			}
			out.push_back(std::move(row));
		}
		return out;
	}

	struct InlineBorderSetsExport {
		InlineBorderSetsExport() = default;
		InlineBorderSetsExport(InlineBorderSetsExport&&) noexcept = default;
		InlineBorderSetsExport& operator=(InlineBorderSetsExport&&) noexcept = default;

		nlohmann::json sets = nlohmann::json::array();
		std::unordered_map<int64_t, int> indexById;
	};

	InlineBorderSetsExport ExportInlineBorderSets(const std::vector<GroundBrushBorderRecord> &borders) {
		InlineBorderSetsExport out;
		for (const GroundBrushBorderRecord &border : borders) {
			if (border.borderSetId <= 0) {
				continue;
			}

			BorderSetRecord borderSet;
			if (!g_brush_database.getBorderSetById(border.borderSetId, borderSet) || borderSet.id <= 0) {
				continue;
			}
			if (!borderSet.borderScope.IsSameAs("inline", false)) {
				continue;
			}
			if (out.indexById.contains(borderSet.id)) {
				continue;
			}

			std::vector<BorderSetItemRecord> borderItems;
			if (!g_brush_database.getBorderSetItems(borderSet.id, borderItems)) {
				continue;
			}

			const auto inlineIndex = static_cast<int>(out.sets.size());
			out.indexById.insert({ borderSet.id, inlineIndex });

			nlohmann::json exportedItems = nlohmann::json::array();
			for (const BorderSetItemRecord &item : borderItems) {
				exportedItems.push_back({
					{ "edge", item.edge.ToStdString() },
					{ "itemId", item.itemId },
					{ "sortOrder", item.sortOrder },
				});
			}

			out.sets.push_back({
				{ "inlineIndex", inlineIndex },
				{ "borderType", borderSet.borderType.ToStdString() },
				{ "borderGroup", borderSet.borderGroup },
				{ "groundEquivalent", borderSet.groundEquivalent },
				{ "items", std::move(exportedItems) },
			});
		}
		return out;
	}

	bool TryBuildGroundBorderRef(int64_t borderSetId, const std::unordered_map<int64_t, int> &inlineBorderIndexById, nlohmann::json &outRef) {
		if (borderSetId <= 0) {
			return false;
		}

		BorderSetRecord borderSet;
		if (!g_brush_database.getBorderSetById(borderSetId, borderSet) || borderSet.id <= 0) {
			return false;
		}

		if (borderSet.borderScope.IsSameAs("global", false) && borderSet.xmlBorderId > 0) {
			outRef = { { "scope", "global" }, { "xmlBorderId", borderSet.xmlBorderId } };
			return true;
		}

		if (borderSet.borderScope.IsSameAs("inline", false)) {
			const auto it = inlineBorderIndexById.find(borderSet.id);
			if (it != inlineBorderIndexById.end()) {
				outRef = { { "scope", "inline" }, { "inlineIndex", it->second } };
				return true;
			}
		}

		return false;
	}

	nlohmann::json ExportGroundBordersJson(const std::vector<GroundBrushBorderRecord> &borders, const std::unordered_map<int64_t, int> &inlineBorderIndexById) {
		nlohmann::json out = nlohmann::json::array();
		for (const GroundBrushBorderRecord &border : borders) {
			nlohmann::json row;
			row["borderRole"] = border.borderRole.ToStdString();
			row["align"] = border.align.ToStdString();
			row["targetMode"] = border.targetMode.ToStdString();
			row["targetBrushName"] = border.targetBrushName.ToStdString();
			row["superBorder"] = border.superBorder;
			row["sortOrder"] = border.sortOrder;

			nlohmann::json borderRef;
			if (TryBuildGroundBorderRef(border.borderSetId, inlineBorderIndexById, borderRef)) {
				row["borderRef"] = std::move(borderRef);
			}

			if (border.targetBrushId > 0) {
				BrushRecord target;
				if (g_brush_database.getBrushById(border.targetBrushId, target) && target.id > 0) {
					row["targetBrush"] = ExportBrushKey(target);
				}
			}

			nlohmann::json cases = nlohmann::json::array();
			for (const GroundBorderCaseRecord &caseRecord : border.cases) {
				nlohmann::json conditions = nlohmann::json::array();
				for (const GroundBorderCaseConditionRecord &condition : caseRecord.conditions) {
					conditions.push_back({
						{ "type", condition.conditionType.ToStdString() },
						{ "matchValue", condition.matchValue },
						{ "edge", condition.edge.ToStdString() },
						{ "sortOrder", condition.sortOrder },
					});
				}
				nlohmann::json actions = nlohmann::json::array();
				for (const GroundBorderCaseActionRecord &action : caseRecord.actions) {
					actions.push_back({
						{ "type", action.actionType.ToStdString() },
						{ "targetValue", action.targetValue },
						{ "edge", action.edge.ToStdString() },
						{ "replacementValue", action.replacementValue },
						{ "sortOrder", action.sortOrder },
					});
				}
				cases.push_back({
					{ "sortOrder", caseRecord.sortOrder },
					{ "conditions", std::move(conditions) },
					{ "actions", std::move(actions) },
				});
			}
			row["cases"] = std::move(cases);
			out.push_back(std::move(row));
		}
		return out;
	}

	nlohmann::json ExportWallPartsJson(const std::vector<WallPartRecord> &parts) {
		nlohmann::json out = nlohmann::json::array();
		for (const WallPartRecord &part : parts) {
			nlohmann::json partItems = nlohmann::json::array();
			for (const WallPartItemRecord &item : part.items) {
				partItems.push_back({ { "itemId", item.itemId }, { "chance", item.chance }, { "sortOrder", item.sortOrder } });
			}
			nlohmann::json doors = nlohmann::json::array();
			for (const WallPartDoorRecord &door : part.doors) {
				doors.push_back({
					{ "itemId", door.itemId },
					{ "doorType", door.doorType.ToStdString() },
					{ "isOpen", door.isOpen },
					{ "wallHateMe", door.wallHateMe },
					{ "sortOrder", door.sortOrder },
				});
			}
			out.push_back({
				{ "partType", part.partType.ToStdString() },
				{ "sortOrder", part.sortOrder },
				{ "items", std::move(partItems) },
				{ "doors", std::move(doors) },
			});
		}
		return out;
	}

	nlohmann::json ExportCarpetNodesJson(const std::vector<CarpetNodeRecord> &nodes) {
		nlohmann::json out = nlohmann::json::array();
		for (const CarpetNodeRecord &node : nodes) {
			nlohmann::json nodeItems = nlohmann::json::array();
			for (const CarpetNodeItemRecord &item : node.items) {
				nodeItems.push_back({ { "itemId", item.itemId }, { "chance", item.chance }, { "sortOrder", item.sortOrder } });
			}
			out.push_back({ { "align", node.align.ToStdString() }, { "sortOrder", node.sortOrder }, { "items", std::move(nodeItems) } });
		}
		return out;
	}

	nlohmann::json ExportTableNodesJson(const std::vector<TableNodeRecord> &nodes) {
		nlohmann::json out = nlohmann::json::array();
		for (const TableNodeRecord &node : nodes) {
			nlohmann::json nodeItems = nlohmann::json::array();
			for (const TableNodeItemRecord &item : node.items) {
				nodeItems.push_back({ { "itemId", item.itemId }, { "chance", item.chance }, { "sortOrder", item.sortOrder } });
			}
			out.push_back({ { "align", node.align.ToStdString() }, { "sortOrder", node.sortOrder }, { "items", std::move(nodeItems) } });
		}
		return out;
	}

	nlohmann::json ExportDoodadTileItemsJson(const std::vector<DoodadCompositeTileItemRecord> &items) {
		nlohmann::json out = nlohmann::json::array();
		for (const DoodadCompositeTileItemRecord &item : items) {
			out.push_back({ { "itemId", item.itemId }, { "sortOrder", item.sortOrder } });
		}
		return out;
	}

	nlohmann::json ExportDoodadTilesJson(const std::vector<DoodadCompositeTileRecord> &tiles) {
		nlohmann::json out = nlohmann::json::array();
		for (const DoodadCompositeTileRecord &tile : tiles) {
			out.push_back({
				{ "offsetX", tile.offsetX },
				{ "offsetY", tile.offsetY },
				{ "offsetZ", tile.offsetZ },
				{ "sortOrder", tile.sortOrder },
				{ "items", ExportDoodadTileItemsJson(tile.items) },
			});
		}
		return out;
	}

	nlohmann::json ExportDoodadCompositesJson(const std::vector<DoodadCompositeRecord> &composites) {
		nlohmann::json out = nlohmann::json::array();
		for (const DoodadCompositeRecord &comp : composites) {
			out.push_back({
				{ "chance", comp.chance },
				{ "sortOrder", comp.sortOrder },
				{ "tiles", ExportDoodadTilesJson(comp.tiles) },
			});
		}
		return out;
	}

	nlohmann::json ExportDoodadAlternativesJson(const std::vector<DoodadAlternativeRecord> &alternatives) {
		nlohmann::json out = nlohmann::json::array();
		for (const DoodadAlternativeRecord &alt : alternatives) {
			nlohmann::json single = nlohmann::json::array();
			for (const DoodadSingleItemRecord &item : alt.singleItems) {
				single.push_back({ { "itemId", item.itemId }, { "chance", item.chance }, { "sortOrder", item.sortOrder } });
			}
			out.push_back({
				{ "sortOrder", alt.sortOrder },
				{ "singleItems", std::move(single) },
				{ "composites", ExportDoodadCompositesJson(alt.composites) },
			});
		}
		return out;
	}

	nlohmann::json ExportBrushStorageEntity(int64_t brushId, wxString &error) {
		BrushStorageRecord storage;
		if (!TryLoadCompleteBrushStorage(brushId, storage, error)) {
			return nlohmann::json();
		}

		const BrushRecord &brush = storage.brush;
		nlohmann::json entity;
		entity["kind"] = "brush";
		entity["key"] = ExportBrushKey(brush);
		entity["brush"] = ExportBrushRecordJson(brush);
		entity["items"] = ExportBrushItemsJson(storage.items);
		entity["links"] = ExportBrushLinksJson(storage.links);

		InlineBorderSetsExport inlineBorders = ExportInlineBorderSets(storage.borders);
		entity["inlineBorderSets"] = std::move(inlineBorders.sets);
		entity["groundBorders"] = ExportGroundBordersJson(storage.borders, inlineBorders.indexById);

		entity["wallParts"] = ExportWallPartsJson(storage.wallParts);
		entity["carpetNodes"] = ExportCarpetNodesJson(storage.carpetNodes);
		entity["tableNodes"] = ExportTableNodesJson(storage.tableNodes);
		entity["doodadAlternatives"] = ExportDoodadAlternativesJson(storage.doodadAlternatives);
		return entity;
	}

	bool IsJsonString(const nlohmann::json &v) {
		return v.is_string();
	}

	wxString JsonToWxString(const nlohmann::json &v) {
		const std::string value = v.get<std::string>();
		return wxString::FromUTF8(value.c_str());
	}

	bool ParseBrushKey(const nlohmann::json &key, wxString &outType, wxString &outName) {
		if (!key.is_object()) {
			return false;
		}
		if (!key.contains("type") || !IsJsonString(key["type"])) {
			return false;
		}
		if (!key.contains("name") || !IsJsonString(key["name"])) {
			return false;
		}
		outType = JsonToWxString(key["type"]);
		outName = JsonToWxString(key["name"]);
		return !outType.IsEmpty() && !outName.IsEmpty();
	}

	bool TryParseGlobalBorderSetRecord(const nlohmann::json &set, BorderSetRecord &outRecord, wxString &error) {
		if (!set.contains("xmlBorderId") || !set["xmlBorderId"].is_number_integer()) {
			error = "Invalid border_set entity: missing xmlBorderId.";
			return false;
		}

		outRecord = BorderSetRecord();
		outRecord.borderScope = "global";
		outRecord.xmlBorderId = set["xmlBorderId"].get<int>();
		if (outRecord.xmlBorderId <= 0) {
			error = "Invalid border_set entity: xmlBorderId must be greater than zero.";
			return false;
		}
		if (set.contains("borderType") && IsJsonString(set["borderType"])) {
			outRecord.borderType = JsonToWxString(set["borderType"]);
		}
		if (set.contains("borderGroup") && set["borderGroup"].is_number_integer()) {
			outRecord.borderGroup = set["borderGroup"].get<int>();
		}
		if (set.contains("groundEquivalent") && set["groundEquivalent"].is_number_integer()) {
			outRecord.groundEquivalent = set["groundEquivalent"].get<int>();
		}
		error.clear();
		return true;
	}

	std::vector<BorderSetItemRecord> ParseBorderSetItemsFromArray(const nlohmann::json &itemsJson, int64_t borderSetId) {
		std::vector<BorderSetItemRecord> items;
		if (!itemsJson.is_array()) {
			return items;
		}

		for (const nlohmann::json &row : itemsJson) {
			if (!row.is_object()) {
				continue;
			}
			if (!row.contains("edge") || !IsJsonString(row["edge"])) {
				continue;
			}
			if (!row.contains("itemId") || !row["itemId"].is_number_integer()) {
				continue;
			}

			BorderSetItemRecord item;
			item.borderSetId = borderSetId;
			item.edge = JsonToWxString(row["edge"]);
			item.itemId = row["itemId"].get<int>();
			item.sortOrder = static_cast<int>(items.size());
			if (row.contains("sortOrder") && row["sortOrder"].is_number_integer()) {
				item.sortOrder = row["sortOrder"].get<int>();
			}
			items.push_back(item);
		}
		return items;
	}

	std::vector<BorderSetItemRecord> ParseBorderSetItemsFromEntity(const nlohmann::json &entity, int64_t borderSetId) {
		if (!entity.contains("items")) {
			return {};
		}
		return ParseBorderSetItemsFromArray(entity["items"], borderSetId);
	}

	template <typename T, typename Less>
	void SortAndReindexBySortOrder(std::vector<T> &values, Less less);

	bool ApplyBorderSetEntity(const nlohmann::json &entity, MaterialsWorkbenchImportReport &report, wxString &error) {
		if (!entity.contains("borderSet") || !entity["borderSet"].is_object()) {
			error = "Invalid border_set entity.";
			return false;
		}
		const nlohmann::json &set = entity["borderSet"];
		BorderSetRecord record;
		if (!TryParseGlobalBorderSetRecord(set, record, error)) {
			return false;
		}

		BorderSetRecord existing;
		const bool hadExisting = g_brush_database.findBorderSetByXmlBorderId(record.xmlBorderId, existing) && existing.id > 0;

		int64_t borderSetId = 0;
		if (!g_brush_database.upsertBorderSet(record, borderSetId) || borderSetId <= 0) {
			error = g_brush_database.getLastError();
			return false;
		}

		std::vector<BorderSetItemRecord> items = ParseBorderSetItemsFromEntity(entity, borderSetId);
		SortAndReindexBySortOrder(items, [](const BorderSetItemRecord &a, const BorderSetItemRecord &b) {
			if (a.sortOrder != b.sortOrder) {
				return a.sortOrder < b.sortOrder;
			}
			return a.edge < b.edge;
		});

		if (!g_brush_database.replaceBorderSetItems(borderSetId, items)) {
			error = g_brush_database.getLastError();
			return false;
		}

		report.importedBorderSetIds.push_back(borderSetId);
		if (hadExisting) {
			++report.updated;
		} else {
			++report.created;
		}
		return true;
	}

	bool ApplyPaletteGroupEntity(MaterialsWorkbenchController &controller, const nlohmann::json &entity, MaterialsWorkbenchImportReport &report, wxString &error) {
		if (!entity.contains("group") || !entity["group"].is_object()) {
			error = "Invalid palette_group entity.";
			return false;
		}
		const nlohmann::json &g = entity["group"];
		if (!g.contains("name") || !IsJsonString(g["name"])) {
			error = "Invalid palette_group entity: missing name.";
			return false;
		}

		PaletteGroupRecord group;
		group.name = JsonToWxString(g["name"]);
		if (g.contains("runtimeFamily") && IsJsonString(g["runtimeFamily"])) {
			group.runtimeFamily = JsonToWxString(g["runtimeFamily"]);
		}
		if (g.contains("sortOrder") && g["sortOrder"].is_number_integer()) {
			group.sortOrder = g["sortOrder"].get<int>();
		}
		group.isBuiltin = false;

		const bool hadExisting = controller.HasPaletteGroupNamed(group.name);
		if (hadExisting) {
			for (const PaletteGroupRecord &existing : controller.GetPaletteGroups()) {
				if (existing.name.IsSameAs(group.name, false) && existing.id > 0) {
					group.id = existing.id;
					break;
				}
			}
		}
		if (!controller.SavePaletteGroupWithoutReload(group, error)) {
			return false;
		}
		if (hadExisting) {
			++report.updated;
		} else {
			++report.created;
		}
		return true;
	}

	bool ValidatePaletteEntitySchema(const nlohmann::json &entity, const nlohmann::json* &outPalette, const nlohmann::json* &outSections, wxString &error) {
		outPalette = nullptr;
		outSections = nullptr;
		if (!entity.contains("palette") || !entity["palette"].is_object()) {
			error = "Invalid palette entity.";
			return false;
		}
		const nlohmann::json &p = entity["palette"];
		if (!p.contains("name") || !IsJsonString(p["name"])) {
			error = "Invalid palette entity: missing name.";
			return false;
		}
		if (!entity.contains("sections") || !entity["sections"].is_array()) {
			error = "Invalid palette entity: missing sections.";
			return false;
		}
		outPalette = &p;
		outSections = &entity["sections"];
		return true;
	}

	TilesetEntryRecord ParseTilesetEntry(const nlohmann::json &e, int defaultSortOrder) {
		TilesetEntryRecord entry;
		entry.entryKind = JsonToWxString(e["entryKind"]);
		if (e.contains("brushName") && IsJsonString(e["brushName"])) {
			entry.brushName = JsonToWxString(e["brushName"]);
		}
		if (e.contains("itemId") && e["itemId"].is_number_integer()) {
			entry.itemId = e["itemId"].get<int>();
		}
		if (e.contains("fromItemId") && e["fromItemId"].is_number_integer()) {
			entry.fromItemId = e["fromItemId"].get<int>();
		}
		if (e.contains("toItemId") && e["toItemId"].is_number_integer()) {
			entry.toItemId = e["toItemId"].get<int>();
		}
		if (e.contains("afterBrushName") && IsJsonString(e["afterBrushName"])) {
			entry.afterBrushName = JsonToWxString(e["afterBrushName"]);
		}
		if (e.contains("afterItemId") && e["afterItemId"].is_number_integer()) {
			entry.afterItemId = e["afterItemId"].get<int>();
		}
		entry.sortOrder = defaultSortOrder;
		if (e.contains("sortOrder") && e["sortOrder"].is_number_integer()) {
			entry.sortOrder = e["sortOrder"].get<int>();
		}
		return entry;
	}

	std::vector<TilesetEntryRecord> ParseTilesetEntries(const nlohmann::json &entriesJson) {
		std::vector<TilesetEntryRecord> entries;
		if (!entriesJson.is_array()) {
			return entries;
		}
		for (const nlohmann::json &e : entriesJson) {
			if (!e.is_object() || !e.contains("entryKind") || !IsJsonString(e["entryKind"])) {
				continue;
			}
			entries.push_back(ParseTilesetEntry(e, static_cast<int>(entries.size())));
		}
		return entries;
	}

	TilesetSectionRecord ParseTilesetSection(const nlohmann::json &s, int defaultSortOrder) {
		TilesetSectionRecord section;
		section.sectionType = JsonToWxString(s["sectionType"]);
		section.sortOrder = defaultSortOrder;
		if (s.contains("sortOrder") && s["sortOrder"].is_number_integer()) {
			section.sortOrder = s["sortOrder"].get<int>();
		}
		if (s.contains("entries") && s["entries"].is_array()) {
			section.entries = ParseTilesetEntries(s["entries"]);
		}
		return section;
	}

	std::vector<TilesetSectionRecord> ParseTilesetSections(const nlohmann::json &sectionsJson) {
		std::vector<TilesetSectionRecord> sections;
		for (const nlohmann::json &s : sectionsJson) {
			if (!s.is_object()) {
				continue;
			}
			if (!s.contains("sectionType") || !IsJsonString(s["sectionType"])) {
				continue;
			}
			sections.push_back(ParseTilesetSection(s, static_cast<int>(sections.size())));
		}
		return sections;
	}

	bool ApplyPaletteEntity(MaterialsWorkbenchController &controller, const nlohmann::json &entity, MaterialsWorkbenchImportReport &report, wxString &error) {
		const nlohmann::json* paletteJson = nullptr;
		const nlohmann::json* sectionsJson = nullptr;
		if (!ValidatePaletteEntitySchema(entity, paletteJson, sectionsJson, error)) {
			return false;
		}

		TilesetStorageRecord tileset;
		tileset.name = JsonToWxString((*paletteJson)["name"]);
		if (paletteJson->contains("paletteGroupName") && IsJsonString((*paletteJson)["paletteGroupName"])) {
			tileset.paletteGroupName = JsonToWxString((*paletteJson)["paletteGroupName"]);
		}
		if (paletteJson->contains("paletteGroupRuntimeFamily") && IsJsonString((*paletteJson)["paletteGroupRuntimeFamily"])) {
			tileset.paletteGroupRuntimeFamily = JsonToWxString((*paletteJson)["paletteGroupRuntimeFamily"]);
		}

		tileset.sections = ParseTilesetSections(*sectionsJson);

		const bool hadExisting = controller.HasTilesetNamed(tileset.name);
		if (!controller.SaveTilesetWithoutReload(tileset, error)) {
			return false;
		}
		report.importedPaletteNames.push_back(tileset.name);
		if (hadExisting) {
			++report.updated;
		} else {
			++report.created;
		}
		return true;
	}

	template <typename T, typename Less>
	void SortAndReindexBySortOrder(std::vector<T> &values, Less less) {
		std::ranges::sort(values, less);
		int i = 0;
		for (T &value : values) {
			value.sortOrder = i;
			++i;
		}
	}

	bool ValidateBrushEntitySchema(const nlohmann::json &entity, const nlohmann::json* &outBrush, wxString &error) {
		outBrush = nullptr;
		if (!entity.contains("brush") || !entity["brush"].is_object()) {
			error = "Invalid brush entity.";
			return false;
		}
		const nlohmann::json &b = entity["brush"];
		if (!b.contains("type") || !IsJsonString(b["type"]) || !b.contains("name") || !IsJsonString(b["name"])) {
			error = "Invalid brush entity: missing type/name.";
			return false;
		}
		outBrush = &b;
		return true;
	}

	void AssignOptionalInt(const nlohmann::json &obj, const char* key, int &outValue) {
		const auto it = obj.find(key);
		if (it != obj.end() && it->is_number_integer()) {
			outValue = it->get<int>();
		}
	}

	void AssignOptionalBool(const nlohmann::json &obj, const char* key, bool &outValue) {
		const auto it = obj.find(key);
		if (it != obj.end() && it->is_boolean()) {
			outValue = it->get<bool>();
		}
	}

	void AssignOptionalWxString(const nlohmann::json &obj, const char* key, wxString &outValue) {
		const auto it = obj.find(key);
		if (it != obj.end() && IsJsonString(*it)) {
			outValue = JsonToWxString(*it);
		}
	}

	void ResolveBrushLinkTargetFromRow(const nlohmann::json &row, BrushLinkRecord &link) {
		if (const auto it = row.find("target"); it != row.end() && it->is_object()) {
			wxString targetType;
			wxString targetName;
			if (!ParseBrushKey(*it, targetType, targetName)) {
				return;
			}

			BrushRecord target;
			if (g_brush_database.findBrushByNameAndType(targetName, targetType, target) && target.id > 0) {
				link.targetBrushId = target.id;
				link.targetBrushName = target.name;
			} else {
				link.targetBrushId = 0;
				link.targetBrushName = targetName;
			}
			return;
		}

		if (const auto it = row.find("targetName"); it != row.end() && IsJsonString(*it)) {
			link.targetBrushName = JsonToWxString(*it);
		}
	}

	void ApplyOptionalBrushFields(const nlohmann::json &b, BrushRecord &brush) {
		AssignOptionalInt(b, "lookId", brush.lookId);
		AssignOptionalInt(b, "serverLookId", brush.serverLookId);
		AssignOptionalInt(b, "zOrder", brush.zOrder);
		AssignOptionalBool(b, "draggable", brush.draggable);
		AssignOptionalBool(b, "onBlocking", brush.onBlocking);
		AssignOptionalBool(b, "onDuplicate", brush.onDuplicate);
		AssignOptionalBool(b, "redoBorders", brush.redoBorders);
		AssignOptionalBool(b, "randomize", brush.randomize);
		AssignOptionalBool(b, "oneSize", brush.oneSize);
		AssignOptionalInt(b, "thickness", brush.thickness);
		AssignOptionalInt(b, "thicknessCeiling", brush.thicknessCeiling);
		AssignOptionalBool(b, "soloOptional", brush.soloOptional);
		AssignOptionalBool(b, "removeOptionalBorder", brush.removeOptionalBorder);
	}

	bool UpsertBrushFromJson(const nlohmann::json &brushJson, int64_t &outBrushId, bool &outHadExisting, wxString &error) {
		BrushRecord brush;
		brush.type = JsonToWxString(brushJson["type"]);
		brush.name = JsonToWxString(brushJson["name"]);
		ApplyOptionalBrushFields(brushJson, brush);

		BrushRecord existing;
		outHadExisting = g_brush_database.findBrushByNameAndType(brush.name, brush.type, existing) && existing.id > 0;

		outBrushId = 0;
		if (!g_brush_database.upsertBrush(brush, outBrushId) || outBrushId <= 0) {
			error = g_brush_database.getLastError();
			return false;
		}
		return true;
	}

	std::vector<BrushItemRecord> ParseBrushItems(const nlohmann::json &entity, int64_t brushId) {
		std::vector<BrushItemRecord> items;
		if (!entity.contains("items") || !entity["items"].is_array()) {
			return items;
		}
		for (const nlohmann::json &row : entity["items"]) {
			if (!row.is_object() || !row.contains("itemId") || !row["itemId"].is_number_integer()) {
				continue;
			}
			BrushItemRecord item;
			item.brushId = brushId;
			item.itemId = row["itemId"].get<int>();
			if (row.contains("chance") && row["chance"].is_number_integer()) {
				item.chance = row["chance"].get<int>();
			}
			if (row.contains("sortOrder") && row["sortOrder"].is_number_integer()) {
				item.sortOrder = row["sortOrder"].get<int>();
			}
			items.push_back(item);
		}
		SortAndReindexBySortOrder(items, [](const BrushItemRecord &a, const BrushItemRecord &b) { return a.sortOrder < b.sortOrder; });
		return items;
	}

	bool ReplaceBrushItemsFromEntity(const nlohmann::json &entity, int64_t brushId, wxString &error) {
		const std::vector<BrushItemRecord> items = ParseBrushItems(entity, brushId);
		if (!g_brush_database.replaceBrushItems(brushId, items)) {
			error = g_brush_database.getLastError();
			return false;
		}
		return true;
	}

	std::vector<BrushLinkRecord> ParseBrushLinks(const nlohmann::json &entity, int64_t brushId) {
		std::vector<BrushLinkRecord> links;
		if (!entity.contains("links") || !entity["links"].is_array()) {
			return links;
		}
		for (const nlohmann::json &row : entity["links"]) {
			if (!row.is_object() || !row.contains("relationType") || !IsJsonString(row["relationType"])) {
				continue;
			}
			BrushLinkRecord link;
			link.brushId = brushId;
			link.relationType = JsonToWxString(row["relationType"]);
			link.sortOrder = static_cast<int>(links.size());
			if (row.contains("sortOrder") && row["sortOrder"].is_number_integer()) {
				link.sortOrder = row["sortOrder"].get<int>();
			}
			ResolveBrushLinkTargetFromRow(row, link);
			links.push_back(link);
		}
		SortAndReindexBySortOrder(links, [](const BrushLinkRecord &a, const BrushLinkRecord &b) { return a.sortOrder < b.sortOrder; });
		return links;
	}

	bool ReplaceBrushLinksFromEntity(const nlohmann::json &entity, int64_t brushId, wxString &error) {
		const std::vector<BrushLinkRecord> links = ParseBrushLinks(entity, brushId);
		if (!g_brush_database.replaceBrushLinks(brushId, links)) {
			error = g_brush_database.getLastError();
			return false;
		}
		return true;
	}

	bool TryImportInlineBorderSet(
		const nlohmann::json &set,
		int64_t brushId,
		std::unordered_map<int, int64_t> &outInlineBorderIdByIndex,
		wxString &error
	) {
		if (!set.is_object() || !set.contains("inlineIndex") || !set["inlineIndex"].is_number_integer()) {
			return true;
		}

		const int inlineIndex = set["inlineIndex"].get<int>();
		BorderSetRecord borderSet;
		borderSet.borderScope = "inline";
		borderSet.ownerBrushId = brushId;
		AssignOptionalWxString(set, "borderType", borderSet.borderType);
		AssignOptionalInt(set, "borderGroup", borderSet.borderGroup);
		AssignOptionalInt(set, "groundEquivalent", borderSet.groundEquivalent);

		int64_t borderSetId = 0;
		if (!g_brush_database.upsertBorderSet(borderSet, borderSetId) || borderSetId <= 0) {
			error = g_brush_database.getLastError();
			return false;
		}

		std::vector<BorderSetItemRecord> items;
		if (set.contains("items")) {
			items = ParseBorderSetItemsFromArray(set["items"], borderSetId);
		}
		SortAndReindexBySortOrder(items, [](const BorderSetItemRecord &a, const BorderSetItemRecord &b) {
			if (a.sortOrder != b.sortOrder) {
				return a.sortOrder < b.sortOrder;
			}
			return a.edge < b.edge;
		});
		if (!g_brush_database.replaceBorderSetItems(borderSetId, items)) {
			error = g_brush_database.getLastError();
			return false;
		}

		outInlineBorderIdByIndex.try_emplace(inlineIndex, borderSetId);
		return true;
	}

	bool ImportInlineBorderSets(
		const nlohmann::json &entity,
		int64_t brushId,
		std::unordered_map<int, int64_t> &outInlineBorderIdByIndex,
		wxString &error
	) {
		outInlineBorderIdByIndex.clear();
		if (!entity.contains("inlineBorderSets") || !entity["inlineBorderSets"].is_array()) {
			return true;
		}
		for (const nlohmann::json &set : entity["inlineBorderSets"]) {
			if (!TryImportInlineBorderSet(set, brushId, outInlineBorderIdByIndex, error)) {
				return false;
			}
		}
		return true;
	}

	int64_t ResolveBorderSetIdFromRef(const nlohmann::json &ref, const std::unordered_map<int, int64_t> &inlineBorderIdByIndex) {
		if (!ref.contains("scope") || !IsJsonString(ref["scope"])) {
			return 0;
		}
		const wxString scope = JsonToWxString(ref["scope"]);
		if (scope.IsSameAs("global", false) && ref.contains("xmlBorderId") && ref["xmlBorderId"].is_number_integer()) {
			const int xmlBorderId = ref["xmlBorderId"].get<int>();
			BorderSetRecord set;
			if (g_brush_database.findBorderSetByXmlBorderId(xmlBorderId, set) && set.id > 0) {
				return set.id;
			}
			return 0;
		}
		if (scope.IsSameAs("inline", false) && ref.contains("inlineIndex") && ref["inlineIndex"].is_number_integer()) {
			const int inlineIndex = ref["inlineIndex"].get<int>();
			const auto it = inlineBorderIdByIndex.find(inlineIndex);
			return it != inlineBorderIdByIndex.end() ? it->second : 0;
		}
		return 0;
	}

	GroundBorderCaseConditionRecord ParseGroundBorderCaseCondition(const nlohmann::json &cond, int &ioSortOrder) {
		GroundBorderCaseConditionRecord condition;
		condition.conditionType = JsonToWxString(cond["type"]);
		if (cond.contains("matchValue") && cond["matchValue"].is_number_integer()) {
			condition.matchValue = cond["matchValue"].get<int>();
		}
		if (cond.contains("edge") && IsJsonString(cond["edge"])) {
			condition.edge = JsonToWxString(cond["edge"]);
		}
		condition.sortOrder = ioSortOrder++;
		if (cond.contains("sortOrder") && cond["sortOrder"].is_number_integer()) {
			condition.sortOrder = cond["sortOrder"].get<int>();
		}
		return condition;
	}

	GroundBorderCaseActionRecord ParseGroundBorderCaseAction(const nlohmann::json &act, int &ioSortOrder) {
		GroundBorderCaseActionRecord action;
		action.actionType = JsonToWxString(act["type"]);
		if (act.contains("targetValue") && act["targetValue"].is_number_integer()) {
			action.targetValue = act["targetValue"].get<int>();
		}
		if (act.contains("edge") && IsJsonString(act["edge"])) {
			action.edge = JsonToWxString(act["edge"]);
		}
		if (act.contains("replacementValue") && act["replacementValue"].is_number_integer()) {
			action.replacementValue = act["replacementValue"].get<int>();
		}
		action.sortOrder = ioSortOrder++;
		if (act.contains("sortOrder") && act["sortOrder"].is_number_integer()) {
			action.sortOrder = act["sortOrder"].get<int>();
		}
		return action;
	}

	GroundBorderCaseRecord ParseGroundBorderCase(const nlohmann::json &c) {
		GroundBorderCaseRecord caseRecord;
		if (c.contains("sortOrder") && c["sortOrder"].is_number_integer()) {
			caseRecord.sortOrder = c["sortOrder"].get<int>();
		}
		if (c.contains("conditions") && c["conditions"].is_array()) {
			int nextSort = 0;
			for (const nlohmann::json &cond : c["conditions"]) {
				if (!cond.is_object() || !cond.contains("type") || !IsJsonString(cond["type"])) {
					continue;
				}
				caseRecord.conditions.push_back(ParseGroundBorderCaseCondition(cond, nextSort));
			}
		}
		if (c.contains("actions") && c["actions"].is_array()) {
			int nextSort = 0;
			for (const nlohmann::json &act : c["actions"]) {
				if (!act.is_object() || !act.contains("type") || !IsJsonString(act["type"])) {
					continue;
				}
				caseRecord.actions.push_back(ParseGroundBorderCaseAction(act, nextSort));
			}
		}
		return caseRecord;
	}

	void ResolveGroundBorderTargetBrushFromRow(const nlohmann::json &row, GroundBrushBorderRecord &border) {
		if (!row.contains("targetBrush") || !row["targetBrush"].is_object()) {
			return;
		}

		wxString targetType;
		wxString targetName;
		if (!ParseBrushKey(row["targetBrush"], targetType, targetName)) {
			return;
		}

		BrushRecord target;
		if (g_brush_database.findBrushByNameAndType(targetName, targetType, target) && target.id > 0) {
			border.targetBrushId = target.id;
			border.targetBrushName = target.name;
		} else {
			border.targetBrushId = 0;
			border.targetBrushName = targetName;
		}
	}

	void ParseGroundBorderCasesFromRow(const nlohmann::json &row, GroundBrushBorderRecord &border) {
		if (!row.contains("cases") || !row["cases"].is_array()) {
			return;
		}
		for (const nlohmann::json &c : row["cases"]) {
			if (!c.is_object()) {
				continue;
			}
			border.cases.push_back(ParseGroundBorderCase(c));
		}
	}

	bool TryParseGroundBorderFromRow(
		const nlohmann::json &row,
		const std::unordered_map<int, int64_t> &inlineBorderIdByIndex,
		int defaultSortOrder,
		GroundBrushBorderRecord &outBorder
	) {
		if (!row.is_object()) {
			return false;
		}

		outBorder = GroundBrushBorderRecord();
		AssignOptionalWxString(row, "borderRole", outBorder.borderRole);
		AssignOptionalWxString(row, "align", outBorder.align);
		AssignOptionalWxString(row, "targetMode", outBorder.targetMode);
		AssignOptionalWxString(row, "targetBrushName", outBorder.targetBrushName);
		AssignOptionalBool(row, "superBorder", outBorder.superBorder);

		outBorder.sortOrder = defaultSortOrder;
		AssignOptionalInt(row, "sortOrder", outBorder.sortOrder);

		if (row.contains("borderRef") && row["borderRef"].is_object()) {
			outBorder.borderSetId = ResolveBorderSetIdFromRef(row["borderRef"], inlineBorderIdByIndex);
		}

		ResolveGroundBorderTargetBrushFromRow(row, outBorder);
		ParseGroundBorderCasesFromRow(row, outBorder);
		return true;
	}

	std::vector<GroundBrushBorderRecord> ParseGroundBorders(
		const nlohmann::json &entity,
		const std::unordered_map<int, int64_t> &inlineBorderIdByIndex
	) {
		std::vector<GroundBrushBorderRecord> borders;
		if (!entity.contains("groundBorders") || !entity["groundBorders"].is_array()) {
			return borders;
		}
		for (const nlohmann::json &row : entity["groundBorders"]) {
			GroundBrushBorderRecord border;
			if (!TryParseGroundBorderFromRow(row, inlineBorderIdByIndex, static_cast<int>(borders.size()), border)) {
				continue;
			}
			borders.push_back(std::move(border));
		}
		SortAndReindexBySortOrder(borders, [](const GroundBrushBorderRecord &a, const GroundBrushBorderRecord &b) { return a.sortOrder < b.sortOrder; });
		return borders;
	}

	bool ReplaceGroundBordersFromEntity(
		const nlohmann::json &entity,
		int64_t brushId,
		const std::unordered_map<int, int64_t> &inlineBorderIdByIndex,
		wxString &error
	) {
		const std::vector<GroundBrushBorderRecord> borders = ParseGroundBorders(entity, inlineBorderIdByIndex);
		if (!g_brush_database.replaceGroundBrushBorders(brushId, borders)) {
			error = g_brush_database.getLastError();
			return false;
		}
		return true;
	}

	bool TryParseWallPartItemFromJson(const nlohmann::json &it, int defaultSortOrder, WallPartItemRecord &outItem) {
		if (!it.is_object() || !it.contains("itemId") || !it["itemId"].is_number_integer()) {
			return false;
		}
		outItem = WallPartItemRecord();
		outItem.itemId = it["itemId"].get<int>();
		outItem.sortOrder = defaultSortOrder;
		AssignOptionalInt(it, "chance", outItem.chance);
		AssignOptionalInt(it, "sortOrder", outItem.sortOrder);
		return true;
	}

	bool TryParseWallPartDoorFromJson(const nlohmann::json &it, int defaultSortOrder, WallPartDoorRecord &outDoor) {
		if (!it.is_object() || !it.contains("itemId") || !it["itemId"].is_number_integer()) {
			return false;
		}
		outDoor = WallPartDoorRecord();
		outDoor.itemId = it["itemId"].get<int>();
		outDoor.sortOrder = defaultSortOrder;
		AssignOptionalWxString(it, "doorType", outDoor.doorType);
		AssignOptionalBool(it, "isOpen", outDoor.isOpen);
		AssignOptionalBool(it, "wallHateMe", outDoor.wallHateMe);
		AssignOptionalInt(it, "sortOrder", outDoor.sortOrder);
		return true;
	}

	bool TryParseWallPartFromJson(const nlohmann::json &p, int defaultSortOrder, WallPartRecord &outPart) {
		if (!p.is_object() || !p.contains("partType") || !IsJsonString(p["partType"])) {
			return false;
		}
		outPart = WallPartRecord();
		outPart.partType = JsonToWxString(p["partType"]);
		outPart.sortOrder = defaultSortOrder;
		AssignOptionalInt(p, "sortOrder", outPart.sortOrder);

		if (p.contains("items") && p["items"].is_array()) {
			for (const nlohmann::json &it : p["items"]) {
				WallPartItemRecord item;
				if (!TryParseWallPartItemFromJson(it, static_cast<int>(outPart.items.size()), item)) {
					continue;
				}
				outPart.items.push_back(item);
			}
		}
		if (p.contains("doors") && p["doors"].is_array()) {
			for (const nlohmann::json &it : p["doors"]) {
				WallPartDoorRecord door;
				if (!TryParseWallPartDoorFromJson(it, static_cast<int>(outPart.doors.size()), door)) {
					continue;
				}
				outPart.doors.push_back(door);
			}
		}
		return true;
	}

	std::vector<WallPartRecord> ParseWallParts(const nlohmann::json &entity) {
		std::vector<WallPartRecord> wallParts;
		if (!entity.contains("wallParts") || !entity["wallParts"].is_array()) {
			return wallParts;
		}
		for (const nlohmann::json &p : entity["wallParts"]) {
			WallPartRecord part;
			if (!TryParseWallPartFromJson(p, static_cast<int>(wallParts.size()), part)) {
				continue;
			}
			wallParts.push_back(std::move(part));
		}
		return wallParts;
	}

	bool ReplaceWallPartsFromEntity(const nlohmann::json &entity, int64_t brushId, wxString &error) {
		const std::vector<WallPartRecord> wallParts = ParseWallParts(entity);
		if (!g_brush_database.replaceWallParts(brushId, wallParts)) {
			error = g_brush_database.getLastError();
			return false;
		}
		return true;
	}

	bool TryParseCarpetNodeItemFromJson(const nlohmann::json &it, int defaultSortOrder, CarpetNodeItemRecord &outItem) {
		if (!it.is_object() || !it.contains("itemId") || !it["itemId"].is_number_integer()) {
			return false;
		}
		outItem = CarpetNodeItemRecord();
		outItem.itemId = it["itemId"].get<int>();
		outItem.sortOrder = defaultSortOrder;
		AssignOptionalInt(it, "chance", outItem.chance);
		AssignOptionalInt(it, "sortOrder", outItem.sortOrder);
		return true;
	}

	bool TryParseCarpetNodeFromJson(const nlohmann::json &n, int defaultSortOrder, CarpetNodeRecord &outNode) {
		if (!n.is_object() || !n.contains("align") || !IsJsonString(n["align"])) {
			return false;
		}
		outNode = CarpetNodeRecord();
		outNode.align = JsonToWxString(n["align"]);
		outNode.sortOrder = defaultSortOrder;
		AssignOptionalInt(n, "sortOrder", outNode.sortOrder);
		if (n.contains("items") && n["items"].is_array()) {
			for (const nlohmann::json &it : n["items"]) {
				CarpetNodeItemRecord item;
				if (!TryParseCarpetNodeItemFromJson(it, static_cast<int>(outNode.items.size()), item)) {
					continue;
				}
				outNode.items.push_back(item);
			}
		}
		return true;
	}

	std::vector<CarpetNodeRecord> ParseCarpetNodes(const nlohmann::json &entity) {
		std::vector<CarpetNodeRecord> carpet;
		if (!entity.contains("carpetNodes") || !entity["carpetNodes"].is_array()) {
			return carpet;
		}
		for (const nlohmann::json &n : entity["carpetNodes"]) {
			CarpetNodeRecord node;
			if (!TryParseCarpetNodeFromJson(n, static_cast<int>(carpet.size()), node)) {
				continue;
			}
			carpet.push_back(std::move(node));
		}
		return carpet;
	}

	bool ReplaceCarpetNodesFromEntity(const nlohmann::json &entity, int64_t brushId, wxString &error) {
		const std::vector<CarpetNodeRecord> carpet = ParseCarpetNodes(entity);
		if (!g_brush_database.replaceCarpetNodes(brushId, carpet)) {
			error = g_brush_database.getLastError();
			return false;
		}
		return true;
	}

	bool TryParseTableNodeItemFromJson(const nlohmann::json &it, int defaultSortOrder, TableNodeItemRecord &outItem) {
		if (!it.is_object() || !it.contains("itemId") || !it["itemId"].is_number_integer()) {
			return false;
		}
		outItem = TableNodeItemRecord();
		outItem.itemId = it["itemId"].get<int>();
		outItem.sortOrder = defaultSortOrder;
		AssignOptionalInt(it, "chance", outItem.chance);
		AssignOptionalInt(it, "sortOrder", outItem.sortOrder);
		return true;
	}

	bool TryParseTableNodeFromJson(const nlohmann::json &n, int defaultSortOrder, TableNodeRecord &outNode) {
		if (!n.is_object() || !n.contains("align") || !IsJsonString(n["align"])) {
			return false;
		}
		outNode = TableNodeRecord();
		outNode.align = JsonToWxString(n["align"]);
		outNode.sortOrder = defaultSortOrder;
		AssignOptionalInt(n, "sortOrder", outNode.sortOrder);
		if (n.contains("items") && n["items"].is_array()) {
			for (const nlohmann::json &it : n["items"]) {
				TableNodeItemRecord item;
				if (!TryParseTableNodeItemFromJson(it, static_cast<int>(outNode.items.size()), item)) {
					continue;
				}
				outNode.items.push_back(item);
			}
		}
		return true;
	}

	std::vector<TableNodeRecord> ParseTableNodes(const nlohmann::json &entity) {
		std::vector<TableNodeRecord> table;
		if (!entity.contains("tableNodes") || !entity["tableNodes"].is_array()) {
			return table;
		}
		for (const nlohmann::json &n : entity["tableNodes"]) {
			TableNodeRecord node;
			if (!TryParseTableNodeFromJson(n, static_cast<int>(table.size()), node)) {
				continue;
			}
			table.push_back(std::move(node));
		}
		return table;
	}

	bool ReplaceTableNodesFromEntity(const nlohmann::json &entity, int64_t brushId, wxString &error) {
		const std::vector<TableNodeRecord> table = ParseTableNodes(entity);
		if (!g_brush_database.replaceTableNodes(brushId, table)) {
			error = g_brush_database.getLastError();
			return false;
		}
		return true;
	}

	bool TryParseDoodadSingleItemFromJson(const nlohmann::json &it, int defaultSortOrder, DoodadSingleItemRecord &outItem) {
		if (!it.is_object() || !it.contains("itemId") || !it["itemId"].is_number_integer()) {
			return false;
		}
		outItem = DoodadSingleItemRecord();
		outItem.itemId = it["itemId"].get<int>();
		outItem.sortOrder = defaultSortOrder;
		AssignOptionalInt(it, "chance", outItem.chance);
		AssignOptionalInt(it, "sortOrder", outItem.sortOrder);
		return true;
	}

	bool TryParseDoodadCompositeTileItemFromJson(const nlohmann::json &it, int defaultSortOrder, DoodadCompositeTileItemRecord &outItem) {
		if (!it.is_object() || !it.contains("itemId") || !it["itemId"].is_number_integer()) {
			return false;
		}
		outItem = DoodadCompositeTileItemRecord();
		outItem.itemId = it["itemId"].get<int>();
		outItem.sortOrder = defaultSortOrder;
		AssignOptionalInt(it, "sortOrder", outItem.sortOrder);
		return true;
	}

	void ParseDoodadCompositeTileItemsFromJson(const nlohmann::json &tileJson, DoodadCompositeTileRecord &ioTile) {
		if (!tileJson.contains("items") || !tileJson["items"].is_array()) {
			return;
		}
		for (const nlohmann::json &it : tileJson["items"]) {
			DoodadCompositeTileItemRecord item;
			if (!TryParseDoodadCompositeTileItemFromJson(it, static_cast<int>(ioTile.items.size()), item)) {
				continue;
			}
			ioTile.items.push_back(item);
		}
	}

	bool TryParseDoodadCompositeTileFromJson(const nlohmann::json &t, int defaultSortOrder, DoodadCompositeTileRecord &outTile) {
		if (!t.is_object()) {
			return false;
		}
		outTile = DoodadCompositeTileRecord();
		AssignOptionalInt(t, "offsetX", outTile.offsetX);
		AssignOptionalInt(t, "offsetY", outTile.offsetY);
		AssignOptionalInt(t, "offsetZ", outTile.offsetZ);
		outTile.sortOrder = defaultSortOrder;
		AssignOptionalInt(t, "sortOrder", outTile.sortOrder);
		ParseDoodadCompositeTileItemsFromJson(t, outTile);
		return true;
	}

	void ParseDoodadCompositeTilesFromJson(const nlohmann::json &compJson, DoodadCompositeRecord &ioComposite) {
		if (!compJson.contains("tiles") || !compJson["tiles"].is_array()) {
			return;
		}
		for (const nlohmann::json &t : compJson["tiles"]) {
			DoodadCompositeTileRecord tile;
			if (!TryParseDoodadCompositeTileFromJson(t, static_cast<int>(ioComposite.tiles.size()), tile)) {
				continue;
			}
			ioComposite.tiles.push_back(std::move(tile));
		}
	}

	bool TryParseDoodadCompositeFromJson(const nlohmann::json &c, int defaultSortOrder, DoodadCompositeRecord &outComposite) {
		if (!c.is_object()) {
			return false;
		}
		outComposite = DoodadCompositeRecord();
		outComposite.sortOrder = defaultSortOrder;
		AssignOptionalInt(c, "chance", outComposite.chance);
		AssignOptionalInt(c, "sortOrder", outComposite.sortOrder);
		ParseDoodadCompositeTilesFromJson(c, outComposite);
		return true;
	}

	void ParseDoodadCompositesFromJson(const nlohmann::json &altJson, DoodadAlternativeRecord &ioAlt) {
		if (!altJson.contains("composites") || !altJson["composites"].is_array()) {
			return;
		}
		for (const nlohmann::json &c : altJson["composites"]) {
			DoodadCompositeRecord comp;
			if (!TryParseDoodadCompositeFromJson(c, static_cast<int>(ioAlt.composites.size()), comp)) {
				continue;
			}
			ioAlt.composites.push_back(std::move(comp));
		}
	}

	void ParseDoodadSingleItemsFromJson(const nlohmann::json &altJson, DoodadAlternativeRecord &ioAlt) {
		if (!altJson.contains("singleItems") || !altJson["singleItems"].is_array()) {
			return;
		}
		for (const nlohmann::json &it : altJson["singleItems"]) {
			DoodadSingleItemRecord item;
			if (!TryParseDoodadSingleItemFromJson(it, static_cast<int>(ioAlt.singleItems.size()), item)) {
				continue;
			}
			ioAlt.singleItems.push_back(item);
		}
	}

	bool TryParseDoodadAlternativeFromJson(const nlohmann::json &a, int defaultSortOrder, DoodadAlternativeRecord &outAlt) {
		if (!a.is_object()) {
			return false;
		}
		outAlt = DoodadAlternativeRecord();
		outAlt.sortOrder = defaultSortOrder;
		AssignOptionalInt(a, "sortOrder", outAlt.sortOrder);
		ParseDoodadSingleItemsFromJson(a, outAlt);
		ParseDoodadCompositesFromJson(a, outAlt);
		return true;
	}

	std::vector<DoodadAlternativeRecord> ParseDoodadAlternatives(const nlohmann::json &entity) {
		std::vector<DoodadAlternativeRecord> doodad;
		if (!entity.contains("doodadAlternatives") || !entity["doodadAlternatives"].is_array()) {
			return doodad;
		}
		for (const nlohmann::json &a : entity["doodadAlternatives"]) {
			DoodadAlternativeRecord alt;
			if (!TryParseDoodadAlternativeFromJson(a, static_cast<int>(doodad.size()), alt)) {
				continue;
			}
			doodad.push_back(std::move(alt));
		}
		return doodad;
	}

	bool ReplaceDoodadAlternativesFromEntity(const nlohmann::json &entity, int64_t brushId, wxString &error) {
		const std::vector<DoodadAlternativeRecord> doodad = ParseDoodadAlternatives(entity);
		if (!g_brush_database.replaceDoodadAlternatives(brushId, doodad)) {
			error = g_brush_database.getLastError();
			return false;
		}
		return true;
	}

	bool ApplyBrushEntity(const nlohmann::json &entity, MaterialsWorkbenchImportReport &report, wxString &error) {
		int64_t brushId = 0;
		bool hadExisting = false;
		const nlohmann::json* brushJson = nullptr;
		if (!ValidateBrushEntitySchema(entity, brushJson, error)) {
			return false;
		}
		if (!UpsertBrushFromJson(*brushJson, brushId, hadExisting, error)) {
			return false;
		}
		report.importedBrushIds.push_back(brushId);

		if (!ReplaceBrushItemsFromEntity(entity, brushId, error)) {
			return false;
		}

		if (!ReplaceBrushLinksFromEntity(entity, brushId, error)) {
			return false;
		}

		std::unordered_map<int, int64_t> inlineBorderIdByIndex;
		if (!ImportInlineBorderSets(entity, brushId, inlineBorderIdByIndex, error)) {
			return false;
		}

		if (!ReplaceGroundBordersFromEntity(entity, brushId, inlineBorderIdByIndex, error)) {
			return false;
		}

		if (!ReplaceWallPartsFromEntity(entity, brushId, error)) {
			return false;
		}

		if (!ReplaceCarpetNodesFromEntity(entity, brushId, error)) {
			return false;
		}

		if (!ReplaceTableNodesFromEntity(entity, brushId, error)) {
			return false;
		}

		if (!ReplaceDoodadAlternativesFromEntity(entity, brushId, error)) {
			return false;
		}

		g_brush_database.resolveGroundReferenceNames();

		if (hadExisting) {
			++report.updated;
		} else {
			++report.created;
		}
		return true;
	}
} // namespace

namespace {
	void SeedBrushQueue(const std::set<int64_t> &brushIds, std::deque<int64_t> &outQueue) {
		for (int64_t brushId : brushIds) {
			if (brushId > 0) {
				outQueue.push_back(brushId);
			}
		}
	}

	bool EnqueueBrush(std::set<int64_t> &brushIds, std::deque<int64_t> &queue, int64_t brushId) {
		if (brushId <= 0) {
			return false;
		}
		if (brushIds.insert(brushId).second) {
			queue.push_back(brushId);
			return true;
		}
		return false;
	}

	bool InsertPaletteName(std::set<wxString> &paletteNames, const wxString &name) {
		if (name.IsEmpty()) {
			return false;
		}
		return paletteNames.insert(name).second;
	}

	bool InsertPaletteGroupName(std::set<wxString> &paletteGroupNames, const wxString &name) {
		if (name.IsEmpty()) {
			return false;
		}
		return paletteGroupNames.insert(name).second;
	}

	bool TryInsertGlobalBorderXmlId(int64_t borderSetId, std::set<int> &borderXmlIds) {
		if (borderSetId <= 0) {
			return false;
		}
		BorderSetRecord borderSet;
		if (!g_brush_database.getBorderSetById(borderSetId, borderSet)) {
			return false;
		}
		if (!borderSet.borderScope.IsSameAs("global", false)) {
			return false;
		}
		if (borderSet.xmlBorderId <= 0) {
			return false;
		}
		return borderXmlIds.insert(borderSet.xmlBorderId).second;
	}

	bool ProcessBrushDependencyQueue(
		std::set<int64_t> &brushIds,
		std::set<int> &borderXmlIds,
		std::deque<int64_t> &brushQueue,
		std::unordered_set<int64_t> &processedBrushes
	) {
		bool changed = false;
		while (!brushQueue.empty()) {
			const int64_t brushId = brushQueue.front();
			brushQueue.pop_front();
			if (!processedBrushes.insert(brushId).second) {
				continue;
			}

			BrushStorageRecord storage;
			if (!g_brush_database.getCompleteBrushById(brushId, storage) || storage.brush.id <= 0) {
				continue;
			}

			for (const GroundBrushBorderRecord &border : storage.borders) {
				changed = TryInsertGlobalBorderXmlId(border.borderSetId, borderXmlIds) || changed;
				changed = EnqueueBrush(brushIds, brushQueue, border.targetBrushId) || changed;
			}

			for (const BrushLinkRecord &link : storage.links) {
				changed = EnqueueBrush(brushIds, brushQueue, link.targetBrushId) || changed;
			}
		}
		return changed;
	}

	bool CollectPaletteDependenciesFromSelectedGroups(
		const MaterialsWorkbenchController &controller,
		const std::set<wxString> &paletteGroupNames,
		std::set<wxString> &paletteNames
	) {
		bool changed = false;
		for (const TilesetStorageRecord &tileset : controller.GetTilesets()) {
			if (tileset.paletteGroupName.IsEmpty()) {
				continue;
			}
			if (paletteGroupNames.find(tileset.paletteGroupName) == paletteGroupNames.end()) {
				continue;
			}
			changed = InsertPaletteName(paletteNames, tileset.name) || changed;
		}
		return changed;
	}

	bool CollectBrushDependenciesFromSelectedPalettes(
		const MaterialsWorkbenchController &controller,
		const std::set<wxString> &paletteNames,
		std::set<wxString> &paletteGroupNames,
		std::set<int64_t> &brushIds,
		std::deque<int64_t> &brushQueue
	) {
		bool changed = false;
		for (const TilesetStorageRecord &tileset : controller.GetTilesets()) {
			if (paletteNames.find(tileset.name) == paletteNames.end()) {
				continue;
			}
			changed = InsertPaletteGroupName(paletteGroupNames, tileset.paletteGroupName) || changed;
			for (const TilesetSectionRecord &section : tileset.sections) {
				for (const TilesetEntryRecord &entry : section.entries) {
					if (!entry.entryKind.IsSameAs("brush", false)) {
						continue;
					}
					changed = EnqueueBrush(brushIds, brushQueue, entry.brushId) || changed;
				}
			}
		}
		return changed;
	}

	bool CollectPalettesReferencingSelectedBrushes(
		const MaterialsWorkbenchController &controller,
		const std::set<int64_t> &brushIds,
		std::set<wxString> &paletteNames
	) {
		bool changed = false;
		for (const TilesetStorageRecord &tileset : controller.GetTilesets()) {
			if (paletteNames.find(tileset.name) != paletteNames.end()) {
				continue;
			}
			bool referencesSelectedBrush = false;
			for (const TilesetSectionRecord &section : tileset.sections) {
				for (const TilesetEntryRecord &entry : section.entries) {
					if (!entry.entryKind.IsSameAs("brush", false) || entry.brushId <= 0) {
						continue;
					}
					if (brushIds.find(entry.brushId) != brushIds.end()) {
						referencesSelectedBrush = true;
						break;
					}
				}
				if (referencesSelectedBrush) {
					break;
				}
			}
			if (referencesSelectedBrush) {
				changed = InsertPaletteName(paletteNames, tileset.name) || changed;
			}
		}
		return changed;
	}
} // namespace

bool ResolveMaterialsWorkbenchExportSelection(
	MaterialsWorkbenchController &controller,
	const MaterialsWorkbenchExportSelection &selection,
	MaterialsWorkbenchResolvedExportSelection &outResolved,
	wxString &error
) {
	error.clear();
	outResolved = MaterialsWorkbenchResolvedExportSelection();

	std::set<int64_t> brushIds(selection.brushIds.begin(), selection.brushIds.end());
	std::set<int> borderXmlIds(selection.globalBorderXmlIds.begin(), selection.globalBorderXmlIds.end());
	std::set<wxString> paletteNames(selection.paletteNames.begin(), selection.paletteNames.end());
	std::set<wxString> paletteGroupNames(selection.paletteGroupNames.begin(), selection.paletteGroupNames.end());

	if (selection.includeDependencies) {
		std::deque<int64_t> brushQueue;
		std::unordered_set<int64_t> processedBrushes;
		SeedBrushQueue(brushIds, brushQueue);

		bool changed = true;
		while (changed) {
			changed = false;
			changed = ProcessBrushDependencyQueue(brushIds, borderXmlIds, brushQueue, processedBrushes) || changed;
			changed = CollectPaletteDependenciesFromSelectedGroups(controller, paletteGroupNames, paletteNames) || changed;
			changed = CollectBrushDependenciesFromSelectedPalettes(controller, paletteNames, paletteGroupNames, brushIds, brushQueue) || changed;
			changed = CollectPalettesReferencingSelectedBrushes(controller, brushIds, paletteNames) || changed;
		}
	}

	outResolved.brushIds.assign(brushIds.begin(), brushIds.end());
	outResolved.globalBorderXmlIds.assign(borderXmlIds.begin(), borderXmlIds.end());
	outResolved.paletteNames.assign(paletteNames.begin(), paletteNames.end());
	outResolved.paletteGroupNames.assign(paletteGroupNames.begin(), paletteGroupNames.end());
	return true;
}

nlohmann::json BuildMaterialsWorkbenchExportJson(
	MaterialsWorkbenchController &controller,
	const MaterialsWorkbenchExportSelection &selection,
	wxString &error
) {
	error.clear();

	MaterialsWorkbenchResolvedExportSelection resolved;
	if (!ResolveMaterialsWorkbenchExportSelection(controller, selection, resolved, error)) {
		return nlohmann::json();
	}
	const std::set<wxString> paletteGroupNames(resolved.paletteGroupNames.begin(), resolved.paletteGroupNames.end());
	const std::set<wxString> paletteNames(resolved.paletteNames.begin(), resolved.paletteNames.end());

	nlohmann::json entities = nlohmann::json::array();

	for (int xmlBorderId : resolved.globalBorderXmlIds) {
		nlohmann::json entity = ExportBorderSetEntity(xmlBorderId, error);
		if (!error.IsEmpty()) {
			return nlohmann::json();
		}
		entities.push_back(std::move(entity));
	}

	for (int64_t brushId : resolved.brushIds) {
		nlohmann::json entity = ExportBrushStorageEntity(brushId, error);
		if (!error.IsEmpty()) {
			return nlohmann::json();
		}
		entities.push_back(std::move(entity));
	}

	for (const PaletteGroupRecord &group : controller.GetPaletteGroups()) {
		if (paletteGroupNames.find(group.name) == paletteGroupNames.end()) {
			continue;
		}
		entities.push_back(ExportPaletteGroupEntity(group));
	}

	for (const TilesetStorageRecord &tileset : controller.GetTilesets()) {
		if (paletteNames.find(tileset.name) == paletteNames.end()) {
			continue;
		}
		entities.push_back(ExportPaletteEntity(tileset));
	}

	nlohmann::json root;
	root["format"] = "rme-materials";
	root["formatVersion"] = 1;
	root["entities"] = std::move(entities);
	return root;
}

namespace {
	struct ImportEntities {
		std::vector<nlohmann::json> borderSets;
		std::vector<nlohmann::json> paletteGroups;
		std::vector<nlohmann::json> palettes;
		std::vector<nlohmann::json> brushes;
	};

	bool ValidateMaterialsImportRoot(const nlohmann::json &root, const nlohmann::json* &outEntities, wxString &error) {
		outEntities = nullptr;
		if (!root.is_object() || !root.contains("format") || !root["format"].is_string() || root["format"].get<std::string>() != "rme-materials") {
			error = "Invalid JSON: unknown format.";
			return false;
		}
		if (!root.contains("formatVersion") || !root["formatVersion"].is_number_integer() || root["formatVersion"].get<int>() != 1) {
			error = "Invalid JSON: unsupported format version.";
			return false;
		}
		if (!root.contains("entities") || !root["entities"].is_array()) {
			error = "Invalid JSON: missing entities.";
			return false;
		}
		outEntities = &root["entities"];
		return true;
	}

	ImportEntities PartitionImportEntities(const nlohmann::json &entities) {
		ImportEntities out;
		for (const nlohmann::json &entity : entities) {
			if (!entity.is_object() || !entity.contains("kind") || !entity["kind"].is_string()) {
				continue;
			}
			const std::string kind = entity["kind"].get<std::string>();
			if (kind == "border_set") {
				out.borderSets.push_back(entity);
			} else if (kind == "palette_group") {
				out.paletteGroups.push_back(entity);
			} else if (kind == "palette") {
				out.palettes.push_back(entity);
			} else if (kind == "brush" || kind == "wall") {
				out.brushes.push_back(entity);
			}
		}
		return out;
	}

	struct ProgressTicker {
		const MaterialsWorkbenchImportProgressCallback* progress = nullptr;
		int totalEntities = 0;
		int currentEntity = 0;

		bool Tick(const wxString &stage) {
			if (!progress || !(*progress)) {
				return true;
			}
			++currentEntity;
			return (*progress)(currentEntity, totalEntities, stage);
		}
	};

	bool TickOrCancel(ProgressTicker &ticker, const wxString &stage, wxString &error) {
		if (ticker.Tick(stage)) {
			return true;
		}
		error = "Import canceled.";
		return false;
	}

	wxString NormalizeNameForReservation(const wxString &name) {
		wxString value = name;
		value.MakeLower();
		return value;
	}

	void ReserveName(std::unordered_set<wxString> &reserved, const wxString &name) {
		reserved.insert(NormalizeNameForReservation(name));
	}

	bool IsReservedName(const std::unordered_set<wxString> &reserved, const wxString &name) {
		return reserved.find(NormalizeNameForReservation(name)) != reserved.end();
	}

	wxString MakeUniqueImportedName(
		const wxString &base,
		const std::function<bool(const wxString &)> &exists,
		std::unordered_set<wxString> &reserved
	) {
		if (!exists(base) && !IsReservedName(reserved, base)) {
			ReserveName(reserved, base);
			return base;
		}
		for (int attempt = 1; attempt < 1000; ++attempt) {
			wxString candidate = base;
			if (attempt == 1) {
				candidate += " (imported)";
			} else {
				candidate += wxString::Format(" (imported %d)", attempt);
			}
			if (!exists(candidate) && !IsReservedName(reserved, candidate)) {
				ReserveName(reserved, candidate);
				return candidate;
			}
		}
		ReserveName(reserved, base);
		return base;
	}

	void BuildRenameMapsIfNeeded(
		MaterialsWorkbenchController &controller,
		const MaterialsWorkbenchImportOptions &options,
		const ImportEntities &entities,
		std::unordered_map<wxString, wxString> &outRenamedPaletteGroups,
		std::unordered_map<wxString, wxString> &outRenamedPalettes
	) {
		outRenamedPaletteGroups.clear();
		outRenamedPalettes.clear();
		if (options.onConflict != MaterialsWorkbenchImportConflictStrategy::RenameWithSuffix) {
			return;
		}

		std::unordered_set<wxString> reservedGroupNames;
		std::unordered_set<wxString> reservedPaletteNames;

		for (const nlohmann::json &entity : entities.paletteGroups) {
			if (!entity.contains("group") || !entity["group"].is_object() || !entity["group"].contains("name") || !IsJsonString(entity["group"]["name"])) {
				continue;
			}
			const wxString name = JsonToWxString(entity["group"]["name"]);
			if (controller.HasPaletteGroupNamed(name)) {
				const wxString newName = MakeUniqueImportedName(
					name,
					[&](const wxString &candidate) { return controller.HasPaletteGroupNamed(candidate); },
					reservedGroupNames
				);
				if (!newName.IsSameAs(name, false)) {
					outRenamedPaletteGroups.try_emplace(name, newName);
				}
			} else {
				ReserveName(reservedGroupNames, name);
			}
		}

		for (const nlohmann::json &entity : entities.palettes) {
			if (!entity.contains("palette") || !entity["palette"].is_object() || !entity["palette"].contains("name") || !IsJsonString(entity["palette"]["name"])) {
				continue;
			}
			const wxString name = JsonToWxString(entity["palette"]["name"]);
			if (controller.HasTilesetNamed(name)) {
				const wxString newName = MakeUniqueImportedName(
					name,
					[&](const wxString &candidate) { return controller.HasTilesetNamed(candidate); },
					reservedPaletteNames
				);
				if (!newName.IsSameAs(name, false)) {
					outRenamedPalettes.try_emplace(name, newName);
				}
			} else {
				ReserveName(reservedPaletteNames, name);
			}
		}
	}

	bool ShouldSkipPaletteGroupEntity(const MaterialsWorkbenchController &controller, const nlohmann::json &entity) {
		if (!entity.contains("group") || !entity["group"].is_object() || !entity["group"].contains("name") || !entity["group"]["name"].is_string()) {
			return false;
		}
		const wxString name = JsonToWxString(entity["group"]["name"]);
		return controller.HasPaletteGroupNamed(name);
	}

	bool ApplyPaletteGroupEntityWithConflicts(
		MaterialsWorkbenchController &controller,
		const MaterialsWorkbenchImportOptions &options,
		const std::unordered_map<wxString, wxString> &renamedPaletteGroups,
		const nlohmann::json &entity,
		MaterialsWorkbenchImportReport &outReport,
		wxString &error
	) {
		if (options.onConflict != MaterialsWorkbenchImportConflictStrategy::RenameWithSuffix) {
			return ApplyPaletteGroupEntity(controller, entity, outReport, error);
		}
		if (entity.contains("group") && entity["group"].is_object() && entity["group"].contains("name") && entity["group"]["name"].is_string()) {
			const wxString name = JsonToWxString(entity["group"]["name"]);
			const auto it = renamedPaletteGroups.find(name);
			if (it != renamedPaletteGroups.end()) {
				nlohmann::json patched = entity;
				patched["group"]["name"] = it->second.ToStdString();
				return ApplyPaletteGroupEntity(controller, patched, outReport, error);
			}
		}
		return ApplyPaletteGroupEntity(controller, entity, outReport, error);
	}

	bool ApplyPaletteGroupsPhase(
		MaterialsWorkbenchController &controller,
		const MaterialsWorkbenchImportOptions &options,
		const std::unordered_map<wxString, wxString> &renamedPaletteGroups,
		const std::vector<nlohmann::json> &entities,
		ProgressTicker &ticker,
		MaterialsWorkbenchImportReport &outReport,
		wxString &error
	) {
		for (const nlohmann::json &entity : entities) {
			if (options.onConflict == MaterialsWorkbenchImportConflictStrategy::SkipExisting && ShouldSkipPaletteGroupEntity(controller, entity)) {
				++outReport.skipped;
				if (!TickOrCancel(ticker, "Skipping palette groups", error)) {
					return false;
				}
				continue;
			}
			if (!ApplyPaletteGroupEntityWithConflicts(controller, options, renamedPaletteGroups, entity, outReport, error)) {
				return false;
			}
			if (!TickOrCancel(ticker, "Importing palette groups", error)) {
				return false;
			}
		}
		return true;
	}

	bool ShouldSkipBorderSetEntity(const nlohmann::json &entity) {
		if (!entity.contains("borderSet") || !entity["borderSet"].is_object()
			|| !entity["borderSet"].contains("xmlBorderId") || !entity["borderSet"]["xmlBorderId"].is_number_integer()) {
			return false;
		}
		const int xmlBorderId = entity["borderSet"]["xmlBorderId"].get<int>();
		BorderSetRecord existing;
		return g_brush_database.findBorderSetByXmlBorderId(xmlBorderId, existing) && existing.id > 0;
	}

	bool ApplyBorderSetsPhase(
		const MaterialsWorkbenchImportOptions &options,
		const std::vector<nlohmann::json> &entities,
		ProgressTicker &ticker,
		MaterialsWorkbenchImportReport &outReport,
		wxString &error
	) {
		for (const nlohmann::json &entity : entities) {
			if (options.onConflict == MaterialsWorkbenchImportConflictStrategy::SkipExisting && ShouldSkipBorderSetEntity(entity)) {
				++outReport.skipped;
				if (!TickOrCancel(ticker, "Skipping borders", error)) {
					return false;
				}
				continue;
			}
			if (!ApplyBorderSetEntity(entity, outReport, error)) {
				return false;
			}
			if (!TickOrCancel(ticker, "Importing borders", error)) {
				return false;
			}
		}
		return true;
	}

	bool ShouldSkipBrushEntity(const nlohmann::json &entity) {
		if (!entity.contains("brush") || !entity["brush"].is_object()
			|| !entity["brush"].contains("type") || !entity["brush"].contains("name")
			|| !entity["brush"]["type"].is_string() || !entity["brush"]["name"].is_string()) {
			return false;
		}
		const wxString type = JsonToWxString(entity["brush"]["type"]);
		const wxString name = JsonToWxString(entity["brush"]["name"]);
		BrushRecord existing;
		return g_brush_database.findBrushByNameAndType(name, type, existing) && existing.id > 0;
	}

	bool ApplyBrushesPhase(
		const MaterialsWorkbenchImportOptions &options,
		const std::vector<nlohmann::json> &entities,
		ProgressTicker &ticker,
		MaterialsWorkbenchImportReport &outReport,
		wxString &error
	) {
		for (const nlohmann::json &entity : entities) {
			if (options.onConflict == MaterialsWorkbenchImportConflictStrategy::SkipExisting && ShouldSkipBrushEntity(entity)) {
				++outReport.skipped;
				if (!TickOrCancel(ticker, "Skipping brushes", error)) {
					return false;
				}
				continue;
			}
			if (!ApplyBrushEntity(entity, outReport, error)) {
				return false;
			}
			if (!TickOrCancel(ticker, "Importing brushes", error)) {
				return false;
			}
		}
		return true;
	}

	bool ShouldSkipPaletteEntity(const MaterialsWorkbenchController &controller, const nlohmann::json &entity) {
		if (!entity.contains("palette") || !entity["palette"].is_object() || !entity["palette"].contains("name") || !entity["palette"]["name"].is_string()) {
			return false;
		}
		const wxString name = JsonToWxString(entity["palette"]["name"]);
		return controller.HasTilesetNamed(name);
	}

	nlohmann::json PatchPaletteEntityIfNeeded(
		const nlohmann::json &entity,
		const std::unordered_map<wxString, wxString> &renamedPaletteGroups,
		const std::unordered_map<wxString, wxString> &renamedPalettes,
		bool &outChanged
	) {
		outChanged = false;
		nlohmann::json patched = entity;
		if (!patched.contains("palette") || !patched["palette"].is_object()) {
			return patched;
		}
		if (patched["palette"].contains("name") && patched["palette"]["name"].is_string()) {
			const wxString name = JsonToWxString(patched["palette"]["name"]);
			const auto it = renamedPalettes.find(name);
			if (it != renamedPalettes.end()) {
				patched["palette"]["name"] = it->second.ToStdString();
				outChanged = true;
			}
		}
		if (patched["palette"].contains("paletteGroupName") && patched["palette"]["paletteGroupName"].is_string()) {
			const wxString groupName = JsonToWxString(patched["palette"]["paletteGroupName"]);
			const auto it = renamedPaletteGroups.find(groupName);
			if (it != renamedPaletteGroups.end()) {
				patched["palette"]["paletteGroupName"] = it->second.ToStdString();
				outChanged = true;
			}
		}
		return patched;
	}

	bool ApplyPalettesPhase(
		MaterialsWorkbenchController &controller,
		const MaterialsWorkbenchImportOptions &options,
		const std::unordered_map<wxString, wxString> &renamedPaletteGroups,
		const std::unordered_map<wxString, wxString> &renamedPalettes,
		const std::vector<nlohmann::json> &entities,
		ProgressTicker &ticker,
		MaterialsWorkbenchImportReport &outReport,
		wxString &error
	) {
		for (const nlohmann::json &entity : entities) {
			if (options.onConflict == MaterialsWorkbenchImportConflictStrategy::SkipExisting && ShouldSkipPaletteEntity(controller, entity)) {
				++outReport.skipped;
				if (!TickOrCancel(ticker, "Skipping palettes", error)) {
					return false;
				}
				continue;
			}

			const nlohmann::json* entityToApply = &entity;
			nlohmann::json patched;
			if (options.onConflict == MaterialsWorkbenchImportConflictStrategy::RenameWithSuffix) {
				bool wasPatched = false;
				patched = PatchPaletteEntityIfNeeded(entity, renamedPaletteGroups, renamedPalettes, wasPatched);
				if (wasPatched) {
					entityToApply = &patched;
				}
			}

			if (!ApplyPaletteEntity(controller, *entityToApply, outReport, error)) {
				return false;
			}
			if (!TickOrCancel(ticker, "Importing palettes", error)) {
				return false;
			}
		}
		return true;
	}

	bool ApplyImportTransaction(
		MaterialsWorkbenchController &controller,
		const MaterialsWorkbenchImportOptions &options,
		const std::unordered_map<wxString, wxString> &renamedPaletteGroups,
		const std::unordered_map<wxString, wxString> &renamedPalettes,
		const ImportEntities &entities,
		ProgressTicker &ticker,
		MaterialsWorkbenchImportReport &outReport,
		wxString &error
	) {
		if (!ApplyPaletteGroupsPhase(controller, options, renamedPaletteGroups, entities.paletteGroups, ticker, outReport, error)) {
			return false;
		}
		if (!ApplyBorderSetsPhase(options, entities.borderSets, ticker, outReport, error)) {
			return false;
		}
		if (!ApplyBrushesPhase(options, entities.brushes, ticker, outReport, error)) {
			return false;
		}
		return ApplyPalettesPhase(controller, options, renamedPaletteGroups, renamedPalettes, entities.palettes, ticker, outReport, error);
	}
} // namespace

bool ApplyMaterialsWorkbenchImportJson(
	MaterialsWorkbenchController &controller,
	const nlohmann::json &root,
	MaterialsWorkbenchImportReport &outReport,
	wxString &error
) {
	return ApplyMaterialsWorkbenchImportJson(controller, root, MaterialsWorkbenchImportOptions(), outReport, error);
}

bool ApplyMaterialsWorkbenchImportJsonWithProgress(
	MaterialsWorkbenchController &controller,
	const nlohmann::json &root,
	const MaterialsWorkbenchImportProgressCallback &progress,
	MaterialsWorkbenchImportReport &outReport,
	wxString &error
) {
	return ApplyMaterialsWorkbenchImportJsonWithProgress(controller, root, MaterialsWorkbenchImportOptions(), progress, outReport, error);
}

bool ApplyMaterialsWorkbenchImportJson(
	MaterialsWorkbenchController &controller,
	const nlohmann::json &root,
	const MaterialsWorkbenchImportOptions &options,
	MaterialsWorkbenchImportReport &outReport,
	wxString &error
) {
	return ApplyMaterialsWorkbenchImportJsonWithProgress(controller, root, options, MaterialsWorkbenchImportProgressCallback(), outReport, error);
}

bool ApplyMaterialsWorkbenchImportJsonWithProgress(
	MaterialsWorkbenchController &controller,
	const nlohmann::json &root,
	const MaterialsWorkbenchImportOptions &options,
	const MaterialsWorkbenchImportProgressCallback &progress,
	MaterialsWorkbenchImportReport &outReport,
	wxString &error
) {
	error.clear();
	outReport = MaterialsWorkbenchImportReport();

	const nlohmann::json* rootEntities = nullptr;
	if (!ValidateMaterialsImportRoot(root, rootEntities, error)) {
		return false;
	}
	const ImportEntities entities = PartitionImportEntities(*rootEntities);

	ProgressTicker ticker;
	ticker.progress = &progress;
	ticker.totalEntities = static_cast<int>(entities.paletteGroups.size() + entities.borderSets.size() + entities.brushes.size() + entities.palettes.size());

	std::unordered_map<wxString, wxString> renamedPaletteGroups;
	std::unordered_map<wxString, wxString> renamedPalettes;
	BuildRenameMapsIfNeeded(controller, options, entities, renamedPaletteGroups, renamedPalettes);

	if (!g_brush_database.runInTransaction([&]() {
			return ApplyImportTransaction(controller, options, renamedPaletteGroups, renamedPalettes, entities, ticker, outReport, error);
		})) {
		if (error.IsEmpty()) {
			error = g_brush_database.getLastError();
		}
		return false;
	}

	return controller.ReloadCatalog();
}
