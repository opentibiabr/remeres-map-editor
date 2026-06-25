#include "main.h"

#include "materials_workbench_inspector_dialog.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <ranges>
#include <set>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/listctrl.h>
#include <wx/srchctrl.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "brush_database.h"
#include "items.h"
#include "sqlite_materials_inspector.h"

namespace {
	constexpr int kWarningListSeverityColumn = 0;
	constexpr int kWarningListDomainColumn = 1;
	constexpr int kWarningListEntityColumn = 2;
	constexpr int kWarningListIssueColumn = 3;
	constexpr int kWarningListCountColumn = 4;
	constexpr int kWarningListStatusColumn = 5;

	wxString NormalizeQuery(const wxString &value) {
		wxString normalized = value;
		normalized.Trim(true);
		normalized.Trim(false);
		return normalized.Lower();
	}

	bool TextMatchesQuery(const wxString &value, const wxString &normalizedQuery) {
		if (normalizedQuery.IsEmpty()) {
			return true;
		}
		return value.Lower().Find(normalizedQuery) != wxNOT_FOUND;
	}

	int GetSelectedListIndex(wxListCtrl* list) {
		if (!list) {
			return -1;
		}
		return list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	}

	bool IsKnownWorkbenchInspectorItemId(int itemId) {
		if (itemId <= 0 || itemId > std::numeric_limits<uint16_t>::max()) {
			return false;
		}
		return g_items.isValidID(static_cast<uint16_t>(itemId));
	}

	wxString NormalizeAlign(wxString value) {
		value.Trim(true);
		value.Trim(false);
		value.MakeLower();
		return value;
	}

	const std::vector<wxString> &GetRequiredCarpetAligns() {
		static const std::vector<wxString> aligns = {
			"center", "n", "s", "e", "w", "cnw", "cne", "csw", "cse"
		};
		return aligns;
	}

	const std::vector<wxString> &GetKnownWorkbenchInspectorCarpetAligns() {
		static const std::vector<wxString> aligns = {
			"center", "n", "s", "e", "w", "cnw", "cne", "csw", "cse", "dnw", "dne", "dsw", "dse"
		};
		return aligns;
	}

	const std::vector<wxString> &GetRequiredTableAligns() {
		static const std::vector<wxString> aligns = {
			"north", "vertical", "south", "west", "horizontal", "east", "alone"
		};
		return aligns;
	}

	wxString JoinStrings(const std::vector<wxString> &values, const wxString &separator) {
		wxString result;
		for (size_t i = 0; i < values.size(); ++i) {
			if (i > 0) {
				result << separator;
			}
			result << values[i];
		}
		return result;
	}

	bool CanGoToEntity(const wxString &entityKind, int64_t entityId, const wxString &entityName) {
		if (entityKind.IsEmpty()) {
			return false;
		}
		if (entityKind == "palette") {
			return !entityName.IsEmpty();
		}
		return entityId > 0;
	}

	using WarningRow = MaterialsWorkbenchInspectorDialog::WarningRow;

	struct BorderSetIdCache {
		BorderSetIdCache() = default;
		BorderSetIdCache(BorderSetIdCache&&) noexcept = default;
		BorderSetIdCache& operator=(BorderSetIdCache&&) noexcept = default;

		std::set<int64_t> validBorderSetIds;
		std::set<int64_t> globalBorderSetIds;
	};

	struct WarningCollector {
		std::vector<WarningRow> &warnings;

		void Add(
			const wxString &severity,
			const wxString &domain,
			const wxString &entityKind,
			const wxString &issue,
			int count,
			const wxString &status,
			const wxString &details,
			int64_t entityId = 0,
			const wxString &entityName = wxString()
		) {
			WarningRow row;
			row.severity = severity;
			row.domain = domain;
			row.entityKind = entityKind;
			row.entityId = entityId;
			row.entityName = entityName;
			row.issue = issue;
			row.count = count;
			row.status = status;
			row.details = details;
			warnings.push_back(std::move(row));
		}
	};

	bool EnsureBrushDatabaseOpen(WarningCollector &out) {
		if (g_brush_database.isOpen()) {
			return true;
		}
		out.Add(
			"Error",
			"Workbench",
			"database",
			"materials.db is not open",
			1,
			"Active",
			"SQLite brush database is not open."
		);
		return false;
	}

	bool BuildAuditReport(WarningCollector &out, MaterialsDatabaseAuditReport &outReport) {
		if (g_brush_database.generateAuditReport(outReport)) {
			return true;
		}
		out.Add(
			"Error",
			"Workbench",
			"database",
			"Failed to scan materials.db",
			1,
			"Active",
			g_brush_database.getLastError()
		);
		return false;
	}

	struct BrushAlignAudit {
		std::set<wxString> presentAligns;
		std::vector<wxString> unknownAligns;
		std::vector<wxString> emptyAligns;
		std::set<int> invalidItems;
		std::vector<wxString> missingAligns;
	};

	void FinalizeBrushAlignAudit(const std::vector<wxString> &requiredAligns, BrushAlignAudit &audit) {
		audit.missingAligns.clear();
		for (const wxString &required : requiredAligns) {
			if (audit.presentAligns.find(required) == audit.presentAligns.end()) {
				audit.missingAligns.push_back(required);
			}
		}
	}

	BrushAlignAudit AuditCarpetBrushStorage(const BrushStorageRecord &storage) {
		BrushAlignAudit audit;
		for (const CarpetNodeRecord &node : storage.carpetNodes) {
			const wxString align = NormalizeAlign(node.align);
			if (align.IsEmpty()) {
				continue;
			}
			bool known = false;
			for (const wxString &candidate : GetKnownWorkbenchInspectorCarpetAligns()) {
				if (candidate == align) {
					known = true;
					break;
				}
			}
			if (!known) {
				audit.unknownAligns.push_back(node.align);
			}
			bool requiredAlign = false;
			for (const wxString &required : GetRequiredCarpetAligns()) {
				if (required == align) {
					requiredAlign = true;
					break;
				}
			}
			if (node.items.empty()) {
				audit.emptyAligns.push_back(node.align);
				continue;
			}
			if (requiredAlign) {
				audit.presentAligns.insert(align);
			}
			for (const CarpetNodeItemRecord &item : node.items) {
				if (!IsKnownWorkbenchInspectorItemId(item.itemId)) {
					audit.invalidItems.insert(item.itemId);
				}
			}
		}
		FinalizeBrushAlignAudit(GetRequiredCarpetAligns(), audit);
		return audit;
	}

	BrushAlignAudit AuditTableBrushStorage(const BrushStorageRecord &storage) {
		BrushAlignAudit audit;
		for (const TableNodeRecord &node : storage.tableNodes) {
			const wxString align = NormalizeAlign(node.align);
			if (align.IsEmpty()) {
				continue;
			}
			bool known = false;
			for (const wxString &required : GetRequiredTableAligns()) {
				if (required == align) {
					known = true;
					break;
				}
			}
			if (!known) {
				audit.unknownAligns.push_back(node.align);
			}
			if (node.items.empty()) {
				audit.emptyAligns.push_back(node.align);
				continue;
			}
			audit.presentAligns.insert(align);
			for (const TableNodeItemRecord &item : node.items) {
				if (!IsKnownWorkbenchInspectorItemId(item.itemId)) {
					audit.invalidItems.insert(item.itemId);
				}
			}
		}
		FinalizeBrushAlignAudit(GetRequiredTableAligns(), audit);
		return audit;
	}

	void EmitAlignWarnings(
		WarningCollector &out,
		const BrushRecord &brush,
		const wxString &emptyIssue,
		const wxString &emptyLabelPrefix,
		const wxString &unknownIssue,
		const wxString &unknownLabelPrefix,
		const wxString &invalidIssue,
		const wxString &missingIssue,
		const wxString &missingLabelPrefix,
		const BrushAlignAudit &audit
	) {
		if (!audit.emptyAligns.empty()) {
			out.Add(
				"Error",
				"Brush",
				"brush",
				emptyIssue,
				static_cast<int>(audit.emptyAligns.size()),
				"Active",
				emptyLabelPrefix + JoinStrings(audit.emptyAligns, ", "),
				brush.id,
				brush.name
			);
		}
		if (!audit.unknownAligns.empty()) {
			out.Add(
				"Warning",
				"Brush",
				"brush",
				unknownIssue,
				static_cast<int>(audit.unknownAligns.size()),
				"Active",
				unknownLabelPrefix + JoinStrings(audit.unknownAligns, ", "),
				brush.id,
				brush.name
			);
		}
		if (!audit.invalidItems.empty()) {
			wxArrayString ids;
			for (int id : audit.invalidItems) {
				ids.Add(wxString::Format("%d", id));
			}
			out.Add(
				"Error",
				"Brush",
				"brush",
				invalidIssue,
				static_cast<int>(audit.invalidItems.size()),
				"Active",
				"Invalid item ids: " + wxJoin(ids, ','),
				brush.id,
				brush.name
			);
		}
		if (!audit.missingAligns.empty()) {
			out.Add(
				"Warning",
				"Brush",
				"brush",
				missingIssue,
				static_cast<int>(audit.missingAligns.size()),
				"Active",
				missingLabelPrefix + JoinStrings(audit.missingAligns, ", "),
				brush.id,
				brush.name
			);
		}
	}

	void CollectCarpetBrushWarnings(WarningCollector &out) {
		std::vector<BrushRecord> carpetBrushes;
		if (!g_brush_database.listBrushesByType("carpet", carpetBrushes)) {
			out.Add(
				"Error",
				"Brush",
				"database",
				"Failed to list carpet brushes",
				1,
				"Active",
				g_brush_database.getLastError()
			);
			return;
		}
		for (const BrushRecord &brush : carpetBrushes) {
			BrushStorageRecord storage;
			if (!g_brush_database.getCompleteBrushById(brush.id, storage)) {
				continue;
			}
			const BrushAlignAudit audit = AuditCarpetBrushStorage(storage);
			EmitAlignWarnings(
				out,
				brush,
				"Carpet has empty contexts",
				"Empty: ",
				"Carpet has unknown align slots",
				"Unknown: ",
				"Carpet has invalid item ids",
				"Carpet missing contexts",
				"Missing: ",
				audit
			);
		}
	}

	void CollectTableBrushWarnings(WarningCollector &out) {
		std::vector<BrushRecord> tableBrushes;
		if (!g_brush_database.listBrushesByType("table", tableBrushes)) {
			out.Add(
				"Error",
				"Brush",
				"database",
				"Failed to list table brushes",
				1,
				"Active",
				g_brush_database.getLastError()
			);
			return;
		}
		for (const BrushRecord &brush : tableBrushes) {
			BrushStorageRecord storage;
			if (!g_brush_database.getCompleteBrushById(brush.id, storage)) {
				continue;
			}
			const BrushAlignAudit audit = AuditTableBrushStorage(storage);
			EmitAlignWarnings(
				out,
				brush,
				"Table has empty states",
				"Empty: ",
				"Table has unknown state slots",
				"Unknown: ",
				"Table has invalid item ids",
				"Table missing states",
				"Missing: ",
				audit
			);
		}
	}

	BorderSetIdCache CollectBorderSetIds(WarningCollector &out) {
		BorderSetIdCache cache;
		std::vector<BorderSetRecord> globalBorderSets;
		if (g_brush_database.listBorderSetsByScope("global", globalBorderSets)) {
			for (const BorderSetRecord &borderSet : globalBorderSets) {
				cache.validBorderSetIds.insert(borderSet.id);
				cache.globalBorderSetIds.insert(borderSet.id);
			}
		} else {
			out.Add(
				"Error",
				"Border",
				"database",
				"Failed to list global border sets",
				1,
				"Active",
				g_brush_database.getLastError()
			);
		}

		std::vector<BorderSetRecord> inlineBorderSets;
		if (g_brush_database.listBorderSetsByScope("inline", inlineBorderSets)) {
			for (const BorderSetRecord &borderSet : inlineBorderSets) {
				cache.validBorderSetIds.insert(borderSet.id);
			}
		} else {
			out.Add(
				"Error",
				"Border",
				"database",
				"Failed to list inline border sets",
				1,
				"Active",
				g_brush_database.getLastError()
			);
		}
		return cache;
	}

	void CollectBrushItemIdWarnings(WarningCollector &out, const BrushRecord &brush, const BrushStorageRecord &storage) {
		std::set<int> invalidItemIds;
		for (const BrushItemRecord &item : storage.items) {
			if (!IsKnownWorkbenchInspectorItemId(item.itemId)) {
				invalidItemIds.insert(item.itemId);
			}
		}
		if (invalidItemIds.empty()) {
			return;
		}
		wxArrayString ids;
		for (int id : invalidItemIds) {
			ids.Add(wxString::Format("%d", id));
		}
		out.Add(
			"Error",
			"Brush",
			"brush",
			"Brush has invalid item ids",
			static_cast<int>(invalidItemIds.size()),
			"Active",
			"Invalid item ids: " + wxJoin(ids, ','),
			brush.id,
			brush.name
		);
	}

	void CollectBrushLookWarnings(WarningCollector &out, const BrushRecord &brush, const BrushStorageRecord &storage) {
		if (storage.brush.lookId > 0 && !IsKnownWorkbenchInspectorItemId(storage.brush.lookId)) {
			out.Add(
				"Error",
				"Brush",
				"brush",
				"Brush has invalid lookId",
				1,
				"Active",
				wxString::Format("lookId: %d", storage.brush.lookId),
				brush.id,
				brush.name
			);
		}
		if (storage.brush.serverLookId > 0 && !IsKnownWorkbenchInspectorItemId(storage.brush.serverLookId)) {
			out.Add(
				"Error",
				"Brush",
				"brush",
				"Brush has invalid serverLookId",
				1,
				"Active",
				wxString::Format("serverLookId: %d", storage.brush.serverLookId),
				brush.id,
				brush.name
			);
		}
		if (storage.brush.lookId > 0 && storage.brush.serverLookId > 0) {
			out.Add(
				"Warning",
				"Brush",
				"brush",
				"Brush sets both lookId and serverLookId",
				2,
				"Active",
				wxString::Format("lookId: %d\nserverLookId: %d", storage.brush.lookId, storage.brush.serverLookId),
				brush.id,
				brush.name
			);
		}
	}

	void CollectGroundBrushWarnings(WarningCollector &out, const BrushRecord &brush, const BrushStorageRecord &storage, const BorderSetIdCache &borderSets) {
		if (storage.items.empty() && storage.borders.empty()) {
			out.Add(
				"Error",
				"Brush",
				"brush",
				"Ground brush has no items or borders",
				1,
				"Active",
				"A ground brush with no items and no borders will not render meaningful content.",
				brush.id,
				brush.name
			);
		}

		std::set<int64_t> missingBorderSetIds;
		std::vector<wxString> unresolvedTargets;
		for (const GroundBrushBorderRecord &border : storage.borders) {
			if (border.borderSetId > 0 && !borderSets.validBorderSetIds.contains(border.borderSetId)) {
				missingBorderSetIds.insert(border.borderSetId);
			}
			if (border.targetMode == "brush" && !border.targetBrushName.IsEmpty() && border.targetBrushId <= 0) {
				unresolvedTargets.push_back(border.targetBrushName);
			}
		}

		if (!missingBorderSetIds.empty()) {
			wxArrayString ids;
			for (int64_t id : missingBorderSetIds) {
				ids.Add(wxString::Format("%lld", static_cast<long long>(id)));
			}
			out.Add(
				"Error",
				"Brush",
				"brush",
				"Ground brush references missing border sets",
				static_cast<int>(missingBorderSetIds.size()),
				"Active",
				"Missing border set ids: " + wxJoin(ids, ','),
				brush.id,
				brush.name
			);
		}

		if (!unresolvedTargets.empty()) {
			wxArrayString targets;
			for (const wxString &name : unresolvedTargets) {
				targets.Add(name);
			}
			out.Add(
				"Warning",
				"Brush",
				"brush",
				"Ground brush has unresolved border targets",
				static_cast<int>(unresolvedTargets.size()),
				"Active",
				"Targets: " + wxJoin(targets, ','),
				brush.id,
				brush.name
			);
		}
	}

	void CollectBrushLinkWarnings(WarningCollector &out, const BrushRecord &brush, const BrushStorageRecord &storage) {
		std::vector<wxString> unresolvedLinks;
		for (const BrushLinkRecord &link : storage.links) {
			if (!link.targetBrushName.IsEmpty() && link.targetBrushName.CmpNoCase("all") != 0 && link.targetBrushId <= 0) {
				unresolvedLinks.push_back(link.targetBrushName);
			}
		}
		if (unresolvedLinks.empty()) {
			return;
		}
		wxArrayString targets;
		for (const wxString &name : unresolvedLinks) {
			targets.Add(name);
		}
		out.Add(
			"Warning",
			"Brush",
			"brush",
			"Brush has unresolved links",
			static_cast<int>(unresolvedLinks.size()),
			"Active",
			"Targets: " + wxJoin(targets, ','),
			brush.id,
			brush.name
		);
	}

	struct DoodadWarningAudit {
		std::vector<int> emptyAlternatives;
		std::vector<int> emptyComposites;
		std::vector<int> emptyTiles;
		std::set<int> invalidItemIds;
	};

	void CollectDoodadInvalidItemId(int itemId, std::set<int> &outInvalidItemIds) {
		if (!IsKnownWorkbenchInspectorItemId(itemId)) {
			outInvalidItemIds.insert(itemId);
		}
	}

	void CollectDoodadSingleItemWarnings(const DoodadAlternativeRecord &alt, DoodadWarningAudit &ioAudit) {
		for (const DoodadSingleItemRecord &single : alt.singleItems) {
			CollectDoodadInvalidItemId(single.itemId, ioAudit.invalidItemIds);
		}
	}

	void CollectDoodadCompositeWarnings(const DoodadCompositeRecord &composite, DoodadWarningAudit &ioAudit, int compositeIndex) {
		if (composite.tiles.empty()) {
			ioAudit.emptyComposites.push_back(compositeIndex);
			return;
		}
		for (size_t tileIndex = 0; tileIndex < composite.tiles.size(); ++tileIndex) {
			const DoodadCompositeTileRecord &tile = composite.tiles[tileIndex];
			if (tile.items.empty()) {
				ioAudit.emptyTiles.push_back(static_cast<int>(tileIndex));
				continue;
			}
			for (const DoodadCompositeTileItemRecord &tileItem : tile.items) {
				CollectDoodadInvalidItemId(tileItem.itemId, ioAudit.invalidItemIds);
			}
		}
	}

	void CollectDoodadAlternativeWarnings(const DoodadAlternativeRecord &alt, DoodadWarningAudit &ioAudit, int altIndex) {
		if (alt.singleItems.empty() && alt.composites.empty()) {
			ioAudit.emptyAlternatives.push_back(altIndex);
		}
		CollectDoodadSingleItemWarnings(alt, ioAudit);
		for (size_t compositeIndex = 0; compositeIndex < alt.composites.size(); ++compositeIndex) {
			CollectDoodadCompositeWarnings(alt.composites[compositeIndex], ioAudit, static_cast<int>(compositeIndex));
		}
	}

	void EmitDoodadWarnings(WarningCollector &out, const BrushRecord &brush, const DoodadWarningAudit &audit) {
		if (!audit.emptyAlternatives.empty()) {
			wxArrayString indices;
			for (int idx : audit.emptyAlternatives) {
				indices.Add(wxString::Format("%d", idx + 1));
			}
			out.Add(
				"Warning",
				"Brush",
				"brush",
				"Doodad has empty alternatives",
				static_cast<int>(audit.emptyAlternatives.size()),
				"Active",
				"Alternatives: " + wxJoin(indices, ','),
				brush.id,
				brush.name
			);
		}

		if (!audit.emptyComposites.empty()) {
			wxArrayString indices;
			for (int idx : audit.emptyComposites) {
				indices.Add(wxString::Format("%d", idx + 1));
			}
			out.Add(
				"Warning",
				"Brush",
				"brush",
				"Doodad has empty composites",
				static_cast<int>(audit.emptyComposites.size()),
				"Active",
				"Composites: " + wxJoin(indices, ','),
				brush.id,
				brush.name
			);
		}

		if (!audit.emptyTiles.empty()) {
			out.Add(
				"Warning",
				"Brush",
				"brush",
				"Doodad has empty tiles",
				static_cast<int>(audit.emptyTiles.size()),
				"Active",
				"Some doodad composite tiles contain no items.",
				brush.id,
				brush.name
			);
		}

		if (!audit.invalidItemIds.empty()) {
			wxArrayString ids;
			for (int id : audit.invalidItemIds) {
				ids.Add(wxString::Format("%d", id));
			}
			out.Add(
				"Error",
				"Brush",
				"brush",
				"Doodad has invalid item ids",
				static_cast<int>(audit.invalidItemIds.size()),
				"Active",
				"Invalid item ids: " + wxJoin(ids, ','),
				brush.id,
				brush.name
			);
		}
	}

	void CollectDoodadWarnings(WarningCollector &out, const BrushRecord &brush, const BrushStorageRecord &storage) {
		if (storage.doodadAlternatives.empty()) {
			out.Add(
				"Error",
				"Brush",
				"brush",
				"Doodad has no alternatives",
				1,
				"Active",
				"Doodad brushes should define at least one alternative.",
				brush.id,
				brush.name
			);
			return;
		}

		DoodadWarningAudit audit;
		for (size_t altIndex = 0; altIndex < storage.doodadAlternatives.size(); ++altIndex) {
			CollectDoodadAlternativeWarnings(storage.doodadAlternatives[altIndex], audit, static_cast<int>(altIndex));
		}
		EmitDoodadWarnings(out, brush, audit);
	}

	void CollectWallWarnings(WarningCollector &out, const BrushRecord &brush, const BrushStorageRecord &storage) {
		if (storage.wallParts.empty()) {
			out.Add(
				"Error",
				"Wall",
				"brush",
				"Wall has no parts",
				1,
				"Active",
				"Wall brushes should define at least one part type.",
				brush.id,
				brush.name
			);
			return;
		}

		struct WallWarningAudit {
			int emptyPartTypeCount = 0;
			int invalidDoorTypeCount = 0;
			std::vector<wxString> emptyParts;
			std::set<int> invalidItemIds;
		};

		const auto auditWallParts = [&]() {
			WallWarningAudit audit;
			for (const WallPartRecord &part : storage.wallParts) {
				if (part.partType.IsEmpty()) {
					++audit.emptyPartTypeCount;
				}
				if (part.items.empty() && part.doors.empty()) {
					audit.emptyParts.push_back(part.partType.IsEmpty() ? wxString::FromUTF8("<empty>") : part.partType);
				}
				for (const WallPartItemRecord &item : part.items) {
					if (!IsKnownWorkbenchInspectorItemId(item.itemId)) {
						audit.invalidItemIds.insert(item.itemId);
					}
				}
				for (const WallPartDoorRecord &door : part.doors) {
					if (door.doorType.IsEmpty()) {
						++audit.invalidDoorTypeCount;
					}
					if (!IsKnownWorkbenchInspectorItemId(door.itemId)) {
						audit.invalidItemIds.insert(door.itemId);
					}
				}
			}
			return audit;
		};

		const WallWarningAudit audit = auditWallParts();

		if (audit.emptyPartTypeCount > 0) {
			out.Add(
				"Warning",
				"Wall",
				"brush",
				"Wall has empty part types",
				audit.emptyPartTypeCount,
				"Active",
				"Some wall parts have an empty partType.",
				brush.id,
				brush.name
			);
		}

		if (!audit.emptyParts.empty()) {
			wxArrayString parts;
			for (const wxString &part : audit.emptyParts) {
				parts.Add(part);
			}
			out.Add(
				"Error",
				"Wall",
				"brush",
				"Wall has empty parts",
				static_cast<int>(audit.emptyParts.size()),
				"Active",
				"Empty parts: " + wxJoin(parts, ','),
				brush.id,
				brush.name
			);
		}

		if (audit.invalidDoorTypeCount > 0) {
			out.Add(
				"Warning",
				"Wall",
				"brush",
				"Wall has doors with empty type",
				audit.invalidDoorTypeCount,
				"Active",
				"Some wall doors have an empty doorType.",
				brush.id,
				brush.name
			);
		}

		if (!audit.invalidItemIds.empty()) {
			wxArrayString ids;
			for (int id : audit.invalidItemIds) {
				ids.Add(wxString::Format("%d", id));
			}
			out.Add(
				"Error",
				"Wall",
				"brush",
				"Wall has invalid item ids",
				static_cast<int>(audit.invalidItemIds.size()),
				"Active",
				"Invalid item ids: " + wxJoin(ids, ','),
				brush.id,
				brush.name
			);
		}
	}

	bool IsWallBrushType(const wxString &type) {
		return type == "wall_brush" || type == "wall" || type == "wall decoration";
	}

	void CollectBrushWarnings(WarningCollector &out, const MaterialsDatabaseAuditReport &report, const BorderSetIdCache &borderSets) {
		for (const BrushTypeCountRecord &typeCount : report.brushTypeCounts) {
			std::vector<BrushRecord> brushes;
			if (!g_brush_database.listBrushesByType(typeCount.type, brushes)) {
				out.Add(
					"Error",
					"Brush",
					"database",
					"Failed to list brushes",
					1,
					"Active",
					wxString::Format("Type: %s\n%s", typeCount.type, g_brush_database.getLastError())
				);
				continue;
			}

			for (const BrushRecord &brush : brushes) {
				BrushStorageRecord storage;
				if (!g_brush_database.getCompleteBrushById(brush.id, storage)) {
					continue;
				}
				CollectBrushLookWarnings(out, brush, storage);
				CollectBrushItemIdWarnings(out, brush, storage);
				if (storage.brush.type == "ground") {
					CollectGroundBrushWarnings(out, brush, storage, borderSets);
				}
				CollectBrushLinkWarnings(out, brush, storage);
				if (storage.brush.type == "doodad") {
					CollectDoodadWarnings(out, brush, storage);
				}
				if (IsWallBrushType(storage.brush.type)) {
					CollectWallWarnings(out, brush, storage);
				}
			}
		}
	}

	void CollectBorderSetWarnings(WarningCollector &out, const BorderSetIdCache &borderSets) {
		const auto loadBorderSetsByScope = [](const char* scope, std::vector<BorderSetRecord> &outSets) {
			outSets.clear();
			return g_brush_database.listBorderSetsByScope(scope, outSets);
		};

		const auto appendBorderSetItemWarnings = [&](const BorderSetRecord &borderSet, const std::vector<BorderSetItemRecord> &items) {
			if (items.empty()) {
				out.Add(
					"Error",
					"Border",
					"border_set",
					"Border set has no items",
					1,
					"Active",
					"This border set defines no edge items.",
					borderSet.id,
					wxString::Format("Border set %lld", static_cast<long long>(borderSet.id))
				);
			}

			if (borderSet.groundEquivalent > 0 && !IsKnownWorkbenchInspectorItemId(borderSet.groundEquivalent)) {
				out.Add(
					"Warning",
					"Border",
					"border_set",
					"Border set has invalid groundEquivalent",
					1,
					"Active",
					wxString::Format("groundEquivalent: %d", borderSet.groundEquivalent),
					borderSet.id,
					wxString::Format("Border set %lld", static_cast<long long>(borderSet.id))
				);
			}

			std::set<int> invalidItemIds;
			std::set<wxString> emptyEdges;
			for (const BorderSetItemRecord &item : items) {
				if (item.edge.IsEmpty()) {
					emptyEdges.insert("<empty>");
				}
				if (!IsKnownWorkbenchInspectorItemId(item.itemId)) {
					invalidItemIds.insert(item.itemId);
				}
			}

			if (!emptyEdges.empty()) {
				wxArrayString edges;
				for (const wxString &edge : emptyEdges) {
					edges.Add(edge);
				}
				out.Add(
					"Warning",
					"Border",
					"border_set",
					"Border set has empty edges",
					static_cast<int>(emptyEdges.size()),
					"Active",
					"Edges: " + wxJoin(edges, ','),
					borderSet.id,
					wxString::Format("Border set %lld", static_cast<long long>(borderSet.id))
				);
			}

			if (!invalidItemIds.empty()) {
				wxArrayString ids;
				for (int id : invalidItemIds) {
					ids.Add(wxString::Format("%d", id));
				}
				out.Add(
					"Error",
					"Border",
					"border_set",
					"Border set has invalid item ids",
					static_cast<int>(invalidItemIds.size()),
					"Active",
					"Invalid item ids: " + wxJoin(ids, ','),
					borderSet.id,
					wxString::Format("Border set %lld", static_cast<long long>(borderSet.id))
				);
			}
		};

		const auto maybeAddUnusedGlobalBorderWarning = [&](const BorderSetRecord &borderSet) {
			if (!borderSets.globalBorderSetIds.contains(borderSet.id)) {
				return;
			}
			std::vector<BorderSetUsageRecord> usages;
			if (g_brush_database.listBorderSetUsages(borderSet.id, usages) && usages.empty()) {
				out.Add(
					"Warning",
					"Border",
					"border_set",
					"Global border set is unused",
					0,
					"Active",
					"This global border set is not referenced by any brush.",
					borderSet.id,
					wxString::Format("Border set %lld", static_cast<long long>(borderSet.id))
				);
			}
		};

		std::vector<BorderSetRecord> borderSetsToCheck;
		std::vector<BorderSetRecord> globalBorderSets;
		std::vector<BorderSetRecord> inlineBorderSets;
		if (loadBorderSetsByScope("global", globalBorderSets)) {
			borderSetsToCheck.insert(borderSetsToCheck.end(), globalBorderSets.begin(), globalBorderSets.end());
		}
		if (loadBorderSetsByScope("inline", inlineBorderSets)) {
			borderSetsToCheck.insert(borderSetsToCheck.end(), inlineBorderSets.begin(), inlineBorderSets.end());
		}

		for (const BorderSetRecord &borderSet : borderSetsToCheck) {
			std::vector<BorderSetItemRecord> items;
			if (!g_brush_database.getBorderSetItems(borderSet.id, items)) {
				out.Add(
					"Error",
					"Border",
					"border_set",
					"Failed to load border set items",
					1,
					"Active",
					g_brush_database.getLastError(),
					borderSet.id,
					wxString::Format("Border set %lld", static_cast<long long>(borderSet.id))
				);
				continue;
			}

			appendBorderSetItemWarnings(borderSet, items);
			maybeAddUnusedGlobalBorderWarning(borderSet);
		}
	}

	void CollectPaletteWarnings(WarningCollector &out) {
		const auto maybeAddInvalidPaletteItemField = [&](
			const TilesetStorageRecord &tileset,
			const TilesetSectionRecord &section,
			const TilesetEntryRecord &entry,
			const char* fieldName,
			int value
		) {
			if (value <= 0 || IsKnownWorkbenchInspectorItemId(value)) {
				return;
			}
			out.Add(
				"Error",
				"Palette",
				"palette",
				wxString::Format("Palette item entry has invalid %s", fieldName),
				1,
				"Active",
				wxString::Format("Section: %s\n%s: %d\nSort order: %d", section.sectionType.c_str(), fieldName, value, entry.sortOrder),
				0,
				tileset.name
			);
		};

		std::vector<TilesetStorageRecord> tilesets;
		if (!g_brush_database.getAllTilesets(tilesets)) {
			out.Add(
				"Error",
				"Palette",
				"database",
				"Failed to load palettes",
				1,
				"Active",
				g_brush_database.getLastError()
			);
			return;
		}

		for (const TilesetStorageRecord &tileset : tilesets) {
			for (const TilesetSectionRecord &section : tileset.sections) {
				for (const TilesetEntryRecord &entry : section.entries) {
					if (entry.entryKind.CmpNoCase("brush") == 0) {
						if (!entry.brushName.IsEmpty() && entry.brushId <= 0) {
							out.Add(
								"Warning",
								"Palette",
								"palette",
								"Palette entry references missing brush",
								1,
								"Active",
								wxString::Format("Section: %s\nBrush: %s\nSort order: %d", section.sectionType.c_str(), entry.brushName.c_str(), entry.sortOrder),
								0,
								tileset.name
							);
						}
						continue;
					}
					if (entry.entryKind.CmpNoCase("item") == 0) {
						maybeAddInvalidPaletteItemField(tileset, section, entry, "itemId", entry.itemId);
						maybeAddInvalidPaletteItemField(tileset, section, entry, "fromItemId", entry.fromItemId);
						maybeAddInvalidPaletteItemField(tileset, section, entry, "toItemId", entry.toItemId);
						maybeAddInvalidPaletteItemField(tileset, section, entry, "afterItemId", entry.afterItemId);
					}
				}
			}
		}
	}

	void CollectAuditCounterWarnings(WarningCollector &out, const MaterialsDatabaseAuditReport &report) {
		if (report.unresolvedGroundTargets > 0) {
			out.Add(
				"Warning",
				"Brush",
				"brush",
				"Unresolved ground targets",
				report.unresolvedGroundTargets,
				"Active",
				"Some ground border targets reference missing brushes."
			);
		}
		if (report.unresolvedBrushLinks > 0) {
			out.Add(
				"Warning",
				"Brush",
				"brush",
				"Unresolved brush links",
				report.unresolvedBrushLinks,
				"Active",
				"Some brush relationships reference missing brushes."
			);
		}
	}

	std::vector<wxString> CollectUniquePaletteNames(const std::vector<UnresolvedTilesetEntrySampleRecord> &samples) {
		std::vector<wxString> paletteNames;
		for (const UnresolvedTilesetEntrySampleRecord &sample : samples) {
			if (sample.tilesetName.IsEmpty()) {
				continue;
			}
			bool exists = false;
			for (const wxString &name : paletteNames) {
				if (name.IsSameAs(sample.tilesetName, false)) {
					exists = true;
					break;
				}
			}
			if (!exists) {
				paletteNames.push_back(sample.tilesetName);
			}
		}
		std::ranges::sort(paletteNames, [](const wxString &a, const wxString &b) {
			return a.CmpNoCase(b) < 0;
		});
		return paletteNames;
	}

	wxString NormalizeSampleField(wxString value) {
		value.Trim(true);
		value.Trim(false);
		return value.IsEmpty() ? wxString::FromUTF8("<unknown>") : value;
	}

	void CollectUnresolvedTilesetEntryWarnings(WarningCollector &out, const MaterialsDatabaseAuditReport &report) {
		if (report.unresolvedTilesetEntries <= 0 || report.unresolvedTilesetEntrySamples.empty()) {
			return;
		}

		const std::vector<wxString> paletteNames = CollectUniquePaletteNames(report.unresolvedTilesetEntrySamples);
		for (const wxString &paletteName : paletteNames) {
			wxString group = "<unknown>";
			int sampleCount = 0;
			wxString sampleDetails;
			for (const UnresolvedTilesetEntrySampleRecord &sample : report.unresolvedTilesetEntrySamples) {
				if (!sample.tilesetName.IsSameAs(paletteName, false)) {
					continue;
				}
				if (sampleCount == 0) {
					group = NormalizeSampleField(sample.paletteGroupName);
				}
				++sampleCount;
				const wxString section = NormalizeSampleField(sample.sectionType);
				const wxString entryKind = NormalizeSampleField(sample.entryKind);
				const wxString brush = NormalizeSampleField(sample.brushName);
				sampleDetails << wxString::Format("- section=\"%s\" kind=\"%s\" brush=\"%s\"\n", section, entryKind, brush);
			}
			out.Add(
				"Warning",
				"Palette",
				"palette",
				"Unresolved tileset entries",
				sampleCount,
				"Active",
				wxString::Format(
					"Some palette entries reference missing brushes.\n\nGroup: %s\nTotal unresolved tileset entries: %d\nSamples shown: %d\n\nSamples:\n%s",
					group,
					report.unresolvedTilesetEntries,
					sampleCount,
					sampleDetails
				),
				0,
				paletteName
			);
		}
	}
} // namespace

MaterialsWorkbenchInspectorDialog::MaterialsWorkbenchInspectorDialog(wxWindow* parent, GoToHandler goToHandler) :
	wxDialog(parent, wxID_ANY, "Inspector", wxDefaultPosition, wxSize(980, 620), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	goToHandler_(std::move(goToHandler)) {
	BuildLayout();
	ReloadWarnings();
}

void MaterialsWorkbenchInspectorDialog::SelectSqliteTab() {
	if (!notebook_) {
		return;
	}
	const int index = notebook_->FindPage(sqlitePanel_);
	if (index != wxNOT_FOUND) {
		notebook_->SetSelection(index);
	}
}

void MaterialsWorkbenchInspectorDialog::BuildLayout() {
	wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);

	notebook_ = new wxNotebook(this, wxID_ANY);
	BuildHealthTab(notebook_);
	BuildSqliteTab(notebook_);
	rootSizer->Add(notebook_, 1, wxEXPAND | wxALL, FromDIP(10));

	SetSizerAndFit(rootSizer);
	Layout();
}

void MaterialsWorkbenchInspectorDialog::BuildHealthTab(wxNotebook* notebook) {
	wxPanel* panel = new wxPanel(notebook, wxID_ANY);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	wxStaticText* title = new wxStaticText(panel, wxID_ANY, "Health / Warnings");
	wxFont titleFont = title->GetFont();
	titleFont.SetWeight(wxFONTWEIGHT_BOLD);
	title->SetFont(titleFont);

	wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);
	headerSizer->Add(title, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));
	headerSizer->AddStretchSpacer(1);

	warningRescanButton_ = new wxButton(panel, wxID_ANY, "Rescan");
	warningGoToButton_ = new wxButton(panel, wxID_ANY, "Go to");
	warningGoToButton_->Enable(false);
	headerSizer->Add(warningRescanButton_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
	headerSizer->Add(warningGoToButton_, 0, wxALIGN_CENTER_VERTICAL);

	wxBoxSizer* filterSizer = new wxBoxSizer(wxHORIZONTAL);
	warningSearchCtrl_ = new wxSearchCtrl(panel, wxID_ANY);
	warningSearchCtrl_->ShowSearchButton(false);
	warningSearchCtrl_->ShowCancelButton(true);
	warningSearchCtrl_->SetDescriptiveText("Search warnings");

	wxArrayString severities;
	severities.Add("All severities");
	severities.Add("Warning");
	severities.Add("Error");
	warningSeverityChoice_ = new wxChoice(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, severities);
	warningSeverityChoice_->SetSelection(0);

	wxArrayString domains;
	domains.Add("All domains");
	domains.Add("Brush");
	domains.Add("Palette");
	domains.Add("Border");
	domains.Add("Wall");
	warningDomainChoice_ = new wxChoice(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, domains);
	warningDomainChoice_->SetSelection(0);

	wxArrayString issues;
	issues.Add("All issues");
	warningIssueChoice_ = new wxChoice(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, issues);
	warningIssueChoice_->SetSelection(0);

	filterSizer->Add(warningSearchCtrl_, 1, wxEXPAND | wxRIGHT, FromDIP(8));
	filterSizer->Add(warningSeverityChoice_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
	filterSizer->Add(warningDomainChoice_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
	filterSizer->Add(warningIssueChoice_, 0, wxALIGN_CENTER_VERTICAL);

	wxSplitterWindow* splitter = new wxSplitterWindow(panel, wxID_ANY);
	splitter->SetSashGravity(0.62);
	splitter->SetMinimumPaneSize(FromDIP(120));

	wxPanel* listPanel = new wxPanel(splitter, wxID_ANY);
	wxBoxSizer* listSizer = new wxBoxSizer(wxVERTICAL);
	warningList_ = new wxListCtrl(listPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	warningList_->AppendColumn("Severity", wxLIST_FORMAT_LEFT, FromDIP(90));
	warningList_->AppendColumn("Domain", wxLIST_FORMAT_LEFT, FromDIP(90));
	warningList_->AppendColumn("Entity", wxLIST_FORMAT_LEFT, FromDIP(220));
	warningList_->AppendColumn("Issue", wxLIST_FORMAT_LEFT, FromDIP(360));
	warningList_->AppendColumn("Count", wxLIST_FORMAT_RIGHT, FromDIP(70));
	warningList_->AppendColumn("Status", wxLIST_FORMAT_LEFT, FromDIP(90));
	listSizer->Add(warningList_, 1, wxEXPAND);
	listPanel->SetSizer(listSizer);

	wxPanel* detailsPanel = new wxPanel(splitter, wxID_ANY);
	wxBoxSizer* detailsSizer = new wxBoxSizer(wxVERTICAL);
	wxStaticText* detailsTitle = new wxStaticText(detailsPanel, wxID_ANY, "Details");
	wxFont detailsTitleFont = detailsTitle->GetFont();
	detailsTitleFont.SetWeight(wxFONTWEIGHT_BOLD);
	detailsTitle->SetFont(detailsTitleFont);
	warningDetails_ = new wxTextCtrl(detailsPanel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
	detailsSizer->Add(detailsTitle, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
	detailsSizer->Add(warningDetails_, 1, wxEXPAND);
	detailsPanel->SetSizer(detailsSizer);

	splitter->SplitVertically(listPanel, detailsPanel, FromDIP(600));

	sizer->Add(headerSizer, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
	sizer->Add(new wxStaticLine(panel), 0, wxEXPAND | wxBOTTOM, FromDIP(10));
	sizer->Add(filterSizer, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
	sizer->Add(splitter, 1, wxEXPAND);

	warningRescanButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
		ReloadWarnings();
	});
	warningGoToButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
		GoToSelectedWarning();
	});
	warningSearchCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent &) {
		ApplyWarningFilter();
	});
	warningSearchCtrl_->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN, [this](wxCommandEvent &) {
		warningSearchCtrl_->ChangeValue("");
		ApplyWarningFilter();
	});
	warningSeverityChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) {
		ApplyWarningFilter();
	});
	warningDomainChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) {
		ApplyWarningFilter();
	});
	warningIssueChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) {
		ApplyWarningFilter();
	});
	warningList_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent &) {
		UpdateWarningDetails();
	});
	warningList_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent &) {
		GoToSelectedWarning();
	});

	panel->SetSizer(sizer);
	notebook->AddPage(panel, "Health");
}

void MaterialsWorkbenchInspectorDialog::BuildSqliteTab(wxNotebook* notebook) {
	wxPanel* panel = new wxPanel(notebook, wxID_ANY);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	wxStaticText* title = new wxStaticText(panel, wxID_ANY, "SQLite Materials Inspector");
	wxFont titleFont = title->GetFont();
	titleFont.SetWeight(wxFONTWEIGHT_BOLD);
	title->SetFont(titleFont);

	sizer->Add(title, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
	sizer->Add(new wxStaticLine(panel), 0, wxEXPAND | wxBOTTOM, FromDIP(10));

	auto sqlitePanel = std::make_unique<SQLiteMaterialsInspectorPanel>(panel);
	sqlitePanel_ = sqlitePanel.get();
	sizer->Add(sqlitePanel_, 1, wxEXPAND);
	sqlitePanel.release();

	panel->SetSizer(sizer);
	notebook->AddPage(panel, "SQLite");
}

void MaterialsWorkbenchInspectorDialog::ReloadWarnings() {
	warnings_.clear();
	WarningCollector collector{ warnings_ };
	if (!EnsureBrushDatabaseOpen(collector)) {
		ApplyWarningFilter();
		return;
	}

	MaterialsDatabaseAuditReport report;
	if (!BuildAuditReport(collector, report)) {
		ApplyWarningFilter();
		return;
	}

	CollectCarpetBrushWarnings(collector);
	CollectTableBrushWarnings(collector);
	const BorderSetIdCache borderSets = CollectBorderSetIds(collector);
	CollectBrushWarnings(collector, report, borderSets);
	CollectBorderSetWarnings(collector, borderSets);
	CollectPaletteWarnings(collector);
	CollectAuditCounterWarnings(collector, report);
	CollectUnresolvedTilesetEntryWarnings(collector, report);

	if (warnings_.empty()) {
		collector.Add(
			"Warning",
			"Workbench",
			wxString(),
			"No warnings",
			0,
			"OK",
			"No issues detected by the current scanner."
		);
	}

	RebuildIssueFilterChoices();
	ApplyWarningFilter();
}

void MaterialsWorkbenchInspectorDialog::RebuildIssueFilterChoices() {
	if (!warningIssueChoice_) {
		return;
	}

	const wxString previousSelection = warningIssueChoice_->GetStringSelection();

	std::set<wxString> uniqueIssues;
	for (const WarningRow &row : warnings_) {
		if (!row.issue.IsEmpty() && row.issue != "No warnings") {
			uniqueIssues.insert(row.issue);
		}
	}

	warningIssueChoice_->Freeze();
	warningIssueChoice_->Clear();
	warningIssueChoice_->Append("All issues");
	for (const wxString &issue : uniqueIssues) {
		warningIssueChoice_->Append(issue);
	}

	int restoreIndex = wxNOT_FOUND;
	if (!previousSelection.IsEmpty() && previousSelection != "All issues") {
		restoreIndex = warningIssueChoice_->FindString(previousSelection);
	}
	warningIssueChoice_->SetSelection(restoreIndex != wxNOT_FOUND ? restoreIndex : 0);
	warningIssueChoice_->Thaw();
}

void MaterialsWorkbenchInspectorDialog::ApplyWarningFilter() {
	filteredWarningIndices_.clear();
	const wxString query = NormalizeQuery(warningSearchCtrl_ ? warningSearchCtrl_->GetValue() : wxString());
	const wxString severityFilter = warningSeverityChoice_ ? warningSeverityChoice_->GetStringSelection() : wxString::FromUTF8("All severities");
	const wxString domainFilter = warningDomainChoice_ ? warningDomainChoice_->GetStringSelection() : wxString::FromUTF8("All domains");
	const wxString issueFilter = warningIssueChoice_ ? warningIssueChoice_->GetStringSelection() : wxString::FromUTF8("All issues");

	for (size_t i = 0; i < warnings_.size(); ++i) {
		const WarningRow &row = warnings_[i];
		if (severityFilter != "All severities" && row.severity != severityFilter) {
			continue;
		}
		if (domainFilter != "All domains" && row.domain != domainFilter) {
			continue;
		}
		if (issueFilter != "All issues" && row.issue != issueFilter) {
			continue;
		}
		if (!query.IsEmpty()) {
			wxString haystack;
			haystack << row.severity << " " << row.domain << " " << row.entityKind << " " << row.entityName << " " << row.issue << " " << row.status;
			if (!TextMatchesQuery(haystack, query)) {
				continue;
			}
		}
		filteredWarningIndices_.push_back(i);
	}

	if (!warningList_) {
		return;
	}

	warningList_->Freeze();
	warningList_->DeleteAllItems();
	for (size_t viewIndex = 0; viewIndex < filteredWarningIndices_.size(); ++viewIndex) {
		const WarningRow &row = warnings_[filteredWarningIndices_[viewIndex]];
		const wxString entityLabel = row.entityName.IsEmpty() ? row.entityKind : (row.entityKind + ": " + row.entityName);
		const long listIndex = warningList_->InsertItem(static_cast<long>(viewIndex), row.severity);
		warningList_->SetItem(listIndex, kWarningListDomainColumn, row.domain);
		warningList_->SetItem(listIndex, kWarningListEntityColumn, entityLabel);
		warningList_->SetItem(listIndex, kWarningListIssueColumn, row.issue);
		warningList_->SetItem(listIndex, kWarningListCountColumn, wxString::Format("%d", row.count));
		warningList_->SetItem(listIndex, kWarningListStatusColumn, row.status);
	}
	warningList_->Thaw();

	if (warningList_->GetItemCount() > 0) {
		warningList_->SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
	} else if (warningDetails_) {
		warningDetails_->Clear();
	}
	UpdateWarningDetails();
}

void MaterialsWorkbenchInspectorDialog::UpdateWarningDetails() {
	if (!warningDetails_ || !warningList_) {
		return;
	}
	const int selected = GetSelectedListIndex(warningList_);
	if (selected < 0 || static_cast<size_t>(selected) >= filteredWarningIndices_.size()) {
		warningDetails_->Clear();
		if (warningGoToButton_) {
			warningGoToButton_->Enable(false);
		}
		return;
	}

	const WarningRow &row = warnings_[filteredWarningIndices_[static_cast<size_t>(selected)]];
	wxString text;
	text << "Severity: " << row.severity << "\n";
	text << "Domain: " << row.domain << "\n";
	text << "Issue: " << row.issue << "\n";
	text << "Count: " << row.count << "\n";
	text << "Status: " << row.status << "\n";
	if (!row.entityKind.IsEmpty()) {
		text << "Entity: " << row.entityKind;
		if (row.entityId > 0) {
			text << wxString::Format(" #%lld", static_cast<long long>(row.entityId));
		}
		if (!row.entityName.IsEmpty()) {
			text << " (" << row.entityName << ")";
		}
		text << "\n";
	}
	if (!row.details.IsEmpty()) {
		text << "\n"
			 << row.details;
	}
	warningDetails_->SetValue(text);

	bool canGoTo = false;
	if (goToHandler_) {
		canGoTo = CanGoToEntity(row.entityKind, row.entityId, row.entityName);
	}
	if (warningGoToButton_) {
		warningGoToButton_->Enable(canGoTo);
	}
}

void MaterialsWorkbenchInspectorDialog::GoToSelectedWarning() {
	if (!goToHandler_ || !warningList_) {
		return;
	}
	const int selected = GetSelectedListIndex(warningList_);
	if (selected < 0 || static_cast<size_t>(selected) >= filteredWarningIndices_.size()) {
		return;
	}
	const WarningRow &row = warnings_[filteredWarningIndices_[static_cast<size_t>(selected)]];
	if (!CanGoToEntity(row.entityKind, row.entityId, row.entityName)) {
		return;
	}
	goToHandler_(row.entityKind, row.entityId, row.entityName);
}
