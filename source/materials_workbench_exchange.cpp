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

	nlohmann::json ExportBrushStorageEntity(int64_t brushId, wxString &error) {
		BrushStorageRecord storage;
		if (!g_brush_database.getCompleteBrushById(brushId, storage) || storage.brush.id <= 0) {
			error = wxString::Format("Brush #%lld not found in materials.db.", static_cast<long long>(brushId));
			return nlohmann::json();
		}

		const BrushRecord &brush = storage.brush;
		nlohmann::json entity;
		entity["kind"] = "brush";
		entity["key"] = ExportBrushKey(brush);
		entity["brush"] = {
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

		nlohmann::json items = nlohmann::json::array();
		for (const BrushItemRecord &item : storage.items) {
			items.push_back({
				{ "itemId", item.itemId },
				{ "chance", item.chance },
				{ "sortOrder", item.sortOrder },
			});
		}
		entity["items"] = std::move(items);

		nlohmann::json links = nlohmann::json::array();
		for (const BrushLinkRecord &link : storage.links) {
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
			links.push_back(std::move(row));
		}
		entity["links"] = std::move(links);

		nlohmann::json inlineBorderSets = nlohmann::json::array();
		std::unordered_map<int64_t, int> inlineBorderIndexById;
		for (const GroundBrushBorderRecord &border : storage.borders) {
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
			if (inlineBorderIndexById.find(borderSet.id) != inlineBorderIndexById.end()) {
				continue;
			}
			std::vector<BorderSetItemRecord> borderItems;
			if (!g_brush_database.getBorderSetItems(borderSet.id, borderItems)) {
				continue;
			}
			const int inlineIndex = static_cast<int>(inlineBorderSets.size());
			inlineBorderIndexById.insert({ borderSet.id, inlineIndex });
			nlohmann::json exportedItems = nlohmann::json::array();
			for (const BorderSetItemRecord &item : borderItems) {
				exportedItems.push_back({
					{ "edge", item.edge.ToStdString() },
					{ "itemId", item.itemId },
					{ "sortOrder", item.sortOrder },
				});
			}
			inlineBorderSets.push_back({
				{ "inlineIndex", inlineIndex },
				{ "borderType", borderSet.borderType.ToStdString() },
				{ "borderGroup", borderSet.borderGroup },
				{ "groundEquivalent", borderSet.groundEquivalent },
				{ "items", std::move(exportedItems) },
			});
		}
		entity["inlineBorderSets"] = std::move(inlineBorderSets);

		nlohmann::json borders = nlohmann::json::array();
		for (const GroundBrushBorderRecord &border : storage.borders) {
			nlohmann::json row;
			row["borderRole"] = border.borderRole.ToStdString();
			row["align"] = border.align.ToStdString();
			row["targetMode"] = border.targetMode.ToStdString();
			row["targetBrushName"] = border.targetBrushName.ToStdString();
			row["superBorder"] = border.superBorder;
			row["sortOrder"] = border.sortOrder;

			if (border.borderSetId > 0) {
				BorderSetRecord borderSet;
				if (g_brush_database.getBorderSetById(border.borderSetId, borderSet) && borderSet.id > 0) {
					if (borderSet.borderScope.IsSameAs("global", false) && borderSet.xmlBorderId > 0) {
						row["borderRef"] = { { "scope", "global" }, { "xmlBorderId", borderSet.xmlBorderId } };
					} else if (borderSet.borderScope.IsSameAs("inline", false)) {
						const auto it = inlineBorderIndexById.find(borderSet.id);
						if (it != inlineBorderIndexById.end()) {
							row["borderRef"] = { { "scope", "inline" }, { "inlineIndex", it->second } };
						}
					}
				}
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
			borders.push_back(std::move(row));
		}
		entity["groundBorders"] = std::move(borders);

		nlohmann::json wallParts = nlohmann::json::array();
		for (const WallPartRecord &part : storage.wallParts) {
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
			wallParts.push_back({
				{ "partType", part.partType.ToStdString() },
				{ "sortOrder", part.sortOrder },
				{ "items", std::move(partItems) },
				{ "doors", std::move(doors) },
			});
		}
		entity["wallParts"] = std::move(wallParts);

		nlohmann::json carpet = nlohmann::json::array();
		for (const CarpetNodeRecord &node : storage.carpetNodes) {
			nlohmann::json nodeItems = nlohmann::json::array();
			for (const CarpetNodeItemRecord &item : node.items) {
				nodeItems.push_back({ { "itemId", item.itemId }, { "chance", item.chance }, { "sortOrder", item.sortOrder } });
			}
			carpet.push_back({ { "align", node.align.ToStdString() }, { "sortOrder", node.sortOrder }, { "items", std::move(nodeItems) } });
		}
		entity["carpetNodes"] = std::move(carpet);

		nlohmann::json table = nlohmann::json::array();
		for (const TableNodeRecord &node : storage.tableNodes) {
			nlohmann::json nodeItems = nlohmann::json::array();
			for (const TableNodeItemRecord &item : node.items) {
				nodeItems.push_back({ { "itemId", item.itemId }, { "chance", item.chance }, { "sortOrder", item.sortOrder } });
			}
			table.push_back({ { "align", node.align.ToStdString() }, { "sortOrder", node.sortOrder }, { "items", std::move(nodeItems) } });
		}
		entity["tableNodes"] = std::move(table);

		nlohmann::json doodad = nlohmann::json::array();
		for (const DoodadAlternativeRecord &alt : storage.doodadAlternatives) {
			nlohmann::json single = nlohmann::json::array();
			for (const DoodadSingleItemRecord &item : alt.singleItems) {
				single.push_back({ { "itemId", item.itemId }, { "chance", item.chance }, { "sortOrder", item.sortOrder } });
			}
			nlohmann::json composites = nlohmann::json::array();
			for (const DoodadCompositeRecord &comp : alt.composites) {
				nlohmann::json tiles = nlohmann::json::array();
				for (const DoodadCompositeTileRecord &tile : comp.tiles) {
					nlohmann::json tileItems = nlohmann::json::array();
					for (const DoodadCompositeTileItemRecord &item : tile.items) {
						tileItems.push_back({ { "itemId", item.itemId }, { "sortOrder", item.sortOrder } });
					}
					tiles.push_back({
						{ "offsetX", tile.offsetX },
						{ "offsetY", tile.offsetY },
						{ "offsetZ", tile.offsetZ },
						{ "sortOrder", tile.sortOrder },
						{ "items", std::move(tileItems) },
					});
				}
				composites.push_back({ { "chance", comp.chance }, { "sortOrder", comp.sortOrder }, { "tiles", std::move(tiles) } });
			}
			doodad.push_back({ { "sortOrder", alt.sortOrder }, { "singleItems", std::move(single) }, { "composites", std::move(composites) } });
		}
		entity["doodadAlternatives"] = std::move(doodad);

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

	bool ApplyBorderSetEntity(const nlohmann::json &entity, MaterialsWorkbenchImportReport &report, wxString &error) {
		if (!entity.contains("borderSet") || !entity["borderSet"].is_object()) {
			error = "Invalid border_set entity.";
			return false;
		}
		const nlohmann::json &set = entity["borderSet"];
		if (!set.contains("xmlBorderId") || !set["xmlBorderId"].is_number_integer()) {
			error = "Invalid border_set entity: missing xmlBorderId.";
			return false;
		}

		BorderSetRecord record;
		record.borderScope = "global";
		record.xmlBorderId = set["xmlBorderId"].get<int>();
		if (record.xmlBorderId <= 0) {
			error = "Invalid border_set entity: xmlBorderId must be greater than zero.";
			return false;
		}
		if (set.contains("borderType") && IsJsonString(set["borderType"])) {
			record.borderType = JsonToWxString(set["borderType"]);
		}
		if (set.contains("borderGroup") && set["borderGroup"].is_number_integer()) {
			record.borderGroup = set["borderGroup"].get<int>();
		}
		if (set.contains("groundEquivalent") && set["groundEquivalent"].is_number_integer()) {
			record.groundEquivalent = set["groundEquivalent"].get<int>();
		}

		BorderSetRecord existing;
		const bool hadExisting = g_brush_database.findBorderSetByXmlBorderId(record.xmlBorderId, existing) && existing.id > 0;

		int64_t borderSetId = 0;
		if (!g_brush_database.upsertBorderSet(record, borderSetId) || borderSetId <= 0) {
			error = g_brush_database.getLastError();
			return false;
		}

		std::vector<BorderSetItemRecord> items;
		if (entity.contains("items") && entity["items"].is_array()) {
			for (const nlohmann::json &row : entity["items"]) {
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
		}

		std::sort(items.begin(), items.end(), [](const BorderSetItemRecord &a, const BorderSetItemRecord &b) {
			if (a.sortOrder != b.sortOrder) {
				return a.sortOrder < b.sortOrder;
			}
			return a.edge < b.edge;
		});
		for (size_t i = 0; i < items.size(); ++i) {
			items[i].sortOrder = static_cast<int>(i);
		}

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
		std::sort(values.begin(), values.end(), less);
		for (size_t i = 0; i < values.size(); ++i) {
			values[i].sortOrder = static_cast<int>(i);
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

	void ApplyOptionalBrushFields(const nlohmann::json &b, BrushRecord &brush) {
		if (b.contains("lookId") && b["lookId"].is_number_integer()) {
			brush.lookId = b["lookId"].get<int>();
		}
		if (b.contains("serverLookId") && b["serverLookId"].is_number_integer()) {
			brush.serverLookId = b["serverLookId"].get<int>();
		}
		if (b.contains("zOrder") && b["zOrder"].is_number_integer()) {
			brush.zOrder = b["zOrder"].get<int>();
		}
		if (b.contains("draggable") && b["draggable"].is_boolean()) {
			brush.draggable = b["draggable"].get<bool>();
		}
		if (b.contains("onBlocking") && b["onBlocking"].is_boolean()) {
			brush.onBlocking = b["onBlocking"].get<bool>();
		}
		if (b.contains("onDuplicate") && b["onDuplicate"].is_boolean()) {
			brush.onDuplicate = b["onDuplicate"].get<bool>();
		}
		if (b.contains("redoBorders") && b["redoBorders"].is_boolean()) {
			brush.redoBorders = b["redoBorders"].get<bool>();
		}
		if (b.contains("randomize") && b["randomize"].is_boolean()) {
			brush.randomize = b["randomize"].get<bool>();
		}
		if (b.contains("oneSize") && b["oneSize"].is_boolean()) {
			brush.oneSize = b["oneSize"].get<bool>();
		}
		if (b.contains("thickness") && b["thickness"].is_number_integer()) {
			brush.thickness = b["thickness"].get<int>();
		}
		if (b.contains("thicknessCeiling") && b["thicknessCeiling"].is_number_integer()) {
			brush.thicknessCeiling = b["thicknessCeiling"].get<int>();
		}
		if (b.contains("soloOptional") && b["soloOptional"].is_boolean()) {
			brush.soloOptional = b["soloOptional"].get<bool>();
		}
		if (b.contains("removeOptionalBorder") && b["removeOptionalBorder"].is_boolean()) {
			brush.removeOptionalBorder = b["removeOptionalBorder"].get<bool>();
		}
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
			if (row.contains("target") && row["target"].is_object()) {
				wxString targetType;
				wxString targetName;
				if (ParseBrushKey(row["target"], targetType, targetName)) {
					BrushRecord target;
					if (g_brush_database.findBrushByNameAndType(targetName, targetType, target) && target.id > 0) {
						link.targetBrushId = target.id;
						link.targetBrushName = target.name;
					} else {
						link.targetBrushId = 0;
						link.targetBrushName = targetName;
					}
				}
			} else if (row.contains("targetName") && IsJsonString(row["targetName"])) {
				link.targetBrushName = JsonToWxString(row["targetName"]);
			}
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
			if (!set.is_object() || !set.contains("inlineIndex") || !set["inlineIndex"].is_number_integer()) {
				continue;
			}
			const int inlineIndex = set["inlineIndex"].get<int>();
			BorderSetRecord borderSet;
			borderSet.borderScope = "inline";
			borderSet.ownerBrushId = brushId;
			if (set.contains("borderType") && IsJsonString(set["borderType"])) {
				borderSet.borderType = JsonToWxString(set["borderType"]);
			}
			if (set.contains("borderGroup") && set["borderGroup"].is_number_integer()) {
				borderSet.borderGroup = set["borderGroup"].get<int>();
			}
			if (set.contains("groundEquivalent") && set["groundEquivalent"].is_number_integer()) {
				borderSet.groundEquivalent = set["groundEquivalent"].get<int>();
			}
			int64_t borderSetId = 0;
			if (!g_brush_database.upsertBorderSet(borderSet, borderSetId) || borderSetId <= 0) {
				error = g_brush_database.getLastError();
				return false;
			}
			std::vector<BorderSetItemRecord> items;
			if (set.contains("items") && set["items"].is_array()) {
				for (const nlohmann::json &row : set["items"]) {
					if (!row.is_object() || !row.contains("edge") || !IsJsonString(row["edge"]) || !row.contains("itemId") || !row["itemId"].is_number_integer()) {
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
			}
			std::sort(items.begin(), items.end(), [](const BorderSetItemRecord &a, const BorderSetItemRecord &b) {
				if (a.sortOrder != b.sortOrder) {
					return a.sortOrder < b.sortOrder;
				}
				return a.edge < b.edge;
			});
			for (size_t i = 0; i < items.size(); ++i) {
				items[i].sortOrder = static_cast<int>(i);
			}
			if (!g_brush_database.replaceBorderSetItems(borderSetId, items)) {
				error = g_brush_database.getLastError();
				return false;
			}
			outInlineBorderIdByIndex.try_emplace(inlineIndex, borderSetId);
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

	std::vector<GroundBrushBorderRecord> ParseGroundBorders(
		const nlohmann::json &entity,
		const std::unordered_map<int, int64_t> &inlineBorderIdByIndex
	) {
		std::vector<GroundBrushBorderRecord> borders;
		if (!entity.contains("groundBorders") || !entity["groundBorders"].is_array()) {
			return borders;
		}
		for (const nlohmann::json &row : entity["groundBorders"]) {
			if (!row.is_object()) {
				continue;
			}
			GroundBrushBorderRecord border;
			if (row.contains("borderRole") && IsJsonString(row["borderRole"])) {
				border.borderRole = JsonToWxString(row["borderRole"]);
			}
			if (row.contains("align") && IsJsonString(row["align"])) {
				border.align = JsonToWxString(row["align"]);
			}
			if (row.contains("targetMode") && IsJsonString(row["targetMode"])) {
				border.targetMode = JsonToWxString(row["targetMode"]);
			}
			if (row.contains("targetBrushName") && IsJsonString(row["targetBrushName"])) {
				border.targetBrushName = JsonToWxString(row["targetBrushName"]);
			}
			if (row.contains("superBorder") && row["superBorder"].is_boolean()) {
				border.superBorder = row["superBorder"].get<bool>();
			}
			border.sortOrder = static_cast<int>(borders.size());
			if (row.contains("sortOrder") && row["sortOrder"].is_number_integer()) {
				border.sortOrder = row["sortOrder"].get<int>();
			}

			if (row.contains("borderRef") && row["borderRef"].is_object()) {
				border.borderSetId = ResolveBorderSetIdFromRef(row["borderRef"], inlineBorderIdByIndex);
			}

			if (row.contains("targetBrush") && row["targetBrush"].is_object()) {
				wxString targetType;
				wxString targetName;
				if (ParseBrushKey(row["targetBrush"], targetType, targetName)) {
					BrushRecord target;
					if (g_brush_database.findBrushByNameAndType(targetName, targetType, target) && target.id > 0) {
						border.targetBrushId = target.id;
						border.targetBrushName = target.name;
					} else {
						border.targetBrushId = 0;
						border.targetBrushName = targetName;
					}
				}
			}

			if (row.contains("cases") && row["cases"].is_array()) {
				for (const nlohmann::json &c : row["cases"]) {
					if (!c.is_object()) {
						continue;
					}
					border.cases.push_back(ParseGroundBorderCase(c));
				}
			}
			borders.push_back(border);
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

	std::vector<WallPartRecord> ParseWallParts(const nlohmann::json &entity) {
		std::vector<WallPartRecord> wallParts;
		if (!entity.contains("wallParts") || !entity["wallParts"].is_array()) {
			return wallParts;
		}
		for (const nlohmann::json &p : entity["wallParts"]) {
			if (!p.is_object() || !p.contains("partType") || !IsJsonString(p["partType"])) {
				continue;
			}
			WallPartRecord part;
			part.partType = JsonToWxString(p["partType"]);
			part.sortOrder = static_cast<int>(wallParts.size());
			if (p.contains("sortOrder") && p["sortOrder"].is_number_integer()) {
				part.sortOrder = p["sortOrder"].get<int>();
			}
			if (p.contains("items") && p["items"].is_array()) {
				for (const nlohmann::json &it : p["items"]) {
					if (!it.is_object() || !it.contains("itemId") || !it["itemId"].is_number_integer()) {
						continue;
					}
					WallPartItemRecord item;
					item.itemId = it["itemId"].get<int>();
					if (it.contains("chance") && it["chance"].is_number_integer()) {
						item.chance = it["chance"].get<int>();
					}
					item.sortOrder = static_cast<int>(part.items.size());
					if (it.contains("sortOrder") && it["sortOrder"].is_number_integer()) {
						item.sortOrder = it["sortOrder"].get<int>();
					}
					part.items.push_back(item);
				}
			}
			if (p.contains("doors") && p["doors"].is_array()) {
				for (const nlohmann::json &it : p["doors"]) {
					if (!it.is_object() || !it.contains("itemId") || !it["itemId"].is_number_integer()) {
						continue;
					}
					WallPartDoorRecord door;
					door.itemId = it["itemId"].get<int>();
					if (it.contains("doorType") && IsJsonString(it["doorType"])) {
						door.doorType = JsonToWxString(it["doorType"]);
					}
					if (it.contains("isOpen") && it["isOpen"].is_boolean()) {
						door.isOpen = it["isOpen"].get<bool>();
					}
					if (it.contains("wallHateMe") && it["wallHateMe"].is_boolean()) {
						door.wallHateMe = it["wallHateMe"].get<bool>();
					}
					door.sortOrder = static_cast<int>(part.doors.size());
					if (it.contains("sortOrder") && it["sortOrder"].is_number_integer()) {
						door.sortOrder = it["sortOrder"].get<int>();
					}
					part.doors.push_back(door);
				}
			}
			wallParts.push_back(part);
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

	std::vector<CarpetNodeRecord> ParseCarpetNodes(const nlohmann::json &entity) {
		std::vector<CarpetNodeRecord> carpet;
		if (!entity.contains("carpetNodes") || !entity["carpetNodes"].is_array()) {
			return carpet;
		}
		for (const nlohmann::json &n : entity["carpetNodes"]) {
			if (!n.is_object() || !n.contains("align") || !IsJsonString(n["align"])) {
				continue;
			}
			CarpetNodeRecord node;
			node.align = JsonToWxString(n["align"]);
			node.sortOrder = static_cast<int>(carpet.size());
			if (n.contains("sortOrder") && n["sortOrder"].is_number_integer()) {
				node.sortOrder = n["sortOrder"].get<int>();
			}
			if (n.contains("items") && n["items"].is_array()) {
				for (const nlohmann::json &it : n["items"]) {
					if (!it.is_object() || !it.contains("itemId") || !it["itemId"].is_number_integer()) {
						continue;
					}
					CarpetNodeItemRecord item;
					item.itemId = it["itemId"].get<int>();
					if (it.contains("chance") && it["chance"].is_number_integer()) {
						item.chance = it["chance"].get<int>();
					}
					item.sortOrder = static_cast<int>(node.items.size());
					if (it.contains("sortOrder") && it["sortOrder"].is_number_integer()) {
						item.sortOrder = it["sortOrder"].get<int>();
					}
					node.items.push_back(item);
				}
			}
			carpet.push_back(node);
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

	std::vector<TableNodeRecord> ParseTableNodes(const nlohmann::json &entity) {
		std::vector<TableNodeRecord> table;
		if (!entity.contains("tableNodes") || !entity["tableNodes"].is_array()) {
			return table;
		}
		for (const nlohmann::json &n : entity["tableNodes"]) {
			if (!n.is_object() || !n.contains("align") || !IsJsonString(n["align"])) {
				continue;
			}
			TableNodeRecord node;
			node.align = JsonToWxString(n["align"]);
			node.sortOrder = static_cast<int>(table.size());
			if (n.contains("sortOrder") && n["sortOrder"].is_number_integer()) {
				node.sortOrder = n["sortOrder"].get<int>();
			}
			if (n.contains("items") && n["items"].is_array()) {
				for (const nlohmann::json &it : n["items"]) {
					if (!it.is_object() || !it.contains("itemId") || !it["itemId"].is_number_integer()) {
						continue;
					}
					TableNodeItemRecord item;
					item.itemId = it["itemId"].get<int>();
					if (it.contains("chance") && it["chance"].is_number_integer()) {
						item.chance = it["chance"].get<int>();
					}
					item.sortOrder = static_cast<int>(node.items.size());
					if (it.contains("sortOrder") && it["sortOrder"].is_number_integer()) {
						item.sortOrder = it["sortOrder"].get<int>();
					}
					node.items.push_back(item);
				}
			}
			table.push_back(node);
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

	std::vector<DoodadAlternativeRecord> ParseDoodadAlternatives(const nlohmann::json &entity) {
		std::vector<DoodadAlternativeRecord> doodad;
		if (!entity.contains("doodadAlternatives") || !entity["doodadAlternatives"].is_array()) {
			return doodad;
		}
		for (const nlohmann::json &a : entity["doodadAlternatives"]) {
			if (!a.is_object()) {
				continue;
			}
			DoodadAlternativeRecord alt;
			alt.sortOrder = static_cast<int>(doodad.size());
			if (a.contains("sortOrder") && a["sortOrder"].is_number_integer()) {
				alt.sortOrder = a["sortOrder"].get<int>();
			}
			if (a.contains("singleItems") && a["singleItems"].is_array()) {
				for (const nlohmann::json &it : a["singleItems"]) {
					if (!it.is_object() || !it.contains("itemId") || !it["itemId"].is_number_integer()) {
						continue;
					}
					DoodadSingleItemRecord item;
					item.itemId = it["itemId"].get<int>();
					if (it.contains("chance") && it["chance"].is_number_integer()) {
						item.chance = it["chance"].get<int>();
					}
					item.sortOrder = static_cast<int>(alt.singleItems.size());
					if (it.contains("sortOrder") && it["sortOrder"].is_number_integer()) {
						item.sortOrder = it["sortOrder"].get<int>();
					}
					alt.singleItems.push_back(item);
				}
			}
			if (a.contains("composites") && a["composites"].is_array()) {
				for (const nlohmann::json &c : a["composites"]) {
					if (!c.is_object()) {
						continue;
					}
					DoodadCompositeRecord comp;
					comp.sortOrder = static_cast<int>(alt.composites.size());
					if (c.contains("chance") && c["chance"].is_number_integer()) {
						comp.chance = c["chance"].get<int>();
					}
					if (c.contains("sortOrder") && c["sortOrder"].is_number_integer()) {
						comp.sortOrder = c["sortOrder"].get<int>();
					}
					if (c.contains("tiles") && c["tiles"].is_array()) {
						for (const nlohmann::json &t : c["tiles"]) {
							if (!t.is_object()) {
								continue;
							}
							DoodadCompositeTileRecord tile;
							if (t.contains("offsetX") && t["offsetX"].is_number_integer()) {
								tile.offsetX = t["offsetX"].get<int>();
							}
							if (t.contains("offsetY") && t["offsetY"].is_number_integer()) {
								tile.offsetY = t["offsetY"].get<int>();
							}
							if (t.contains("offsetZ") && t["offsetZ"].is_number_integer()) {
								tile.offsetZ = t["offsetZ"].get<int>();
							}
							tile.sortOrder = static_cast<int>(comp.tiles.size());
							if (t.contains("sortOrder") && t["sortOrder"].is_number_integer()) {
								tile.sortOrder = t["sortOrder"].get<int>();
							}
							if (t.contains("items") && t["items"].is_array()) {
								for (const nlohmann::json &it : t["items"]) {
									if (!it.is_object() || !it.contains("itemId") || !it["itemId"].is_number_integer()) {
										continue;
									}
									DoodadCompositeTileItemRecord item;
									item.itemId = it["itemId"].get<int>();
									item.sortOrder = static_cast<int>(tile.items.size());
									if (it.contains("sortOrder") && it["sortOrder"].is_number_integer()) {
										item.sortOrder = it["sortOrder"].get<int>();
									}
									tile.items.push_back(item);
								}
							}
							comp.tiles.push_back(tile);
						}
					}
					alt.composites.push_back(comp);
				}
			}
			doodad.push_back(alt);
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
				if (border.borderSetId > 0) {
					BorderSetRecord borderSet;
					if (g_brush_database.getBorderSetById(border.borderSetId, borderSet)
						&& borderSet.borderScope.IsSameAs("global", false) && borderSet.xmlBorderId > 0) {
						changed = borderXmlIds.insert(borderSet.xmlBorderId).second || changed;
					}
				}
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

			if (options.onConflict == MaterialsWorkbenchImportConflictStrategy::RenameWithSuffix) {
				bool changed = false;
				nlohmann::json patched = PatchPaletteEntityIfNeeded(entity, renamedPaletteGroups, renamedPalettes, changed);
				if (changed) {
					if (!ApplyPaletteEntity(controller, patched, outReport, error)) {
						return false;
					}
					if (!TickOrCancel(ticker, "Importing palettes", error)) {
						return false;
					}
					continue;
				}
			}

			if (!ApplyPaletteEntity(controller, entity, outReport, error)) {
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
