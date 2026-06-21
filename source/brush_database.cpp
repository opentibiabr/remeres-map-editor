#include "main.h"

#include <sqlite3.h>

#include "brush_database.h"

BrushDatabase g_brush_database;

namespace {
	constexpr int kBrushDatabaseSchemaVersion = 12;
	constexpr const char* kBuiltinPaletteGroupTerrain = "terrain";
	constexpr const char* kBuiltinPaletteGroupDoodad = "doodad";
	constexpr const char* kBuiltinPaletteGroupItem = "item";
	constexpr const char* kBuiltinPaletteGroupOther = "other";
	constexpr const char* kBrushSelectColumns = "id, name, type, look_id, z_order, source_file, server_look_id, "
												"draggable, on_blocking, on_duplicate, redo_borders, randomize, "
												"one_size, solo_optional, thickness, thickness_ceiling, remove_optional_border";
	constexpr const char* kRecreateTilesetTablesSql = "DROP TABLE IF EXISTS tileset_brush_entries;"
													  "DROP TABLE IF EXISTS tileset_sections;"
													  "DROP TABLE IF EXISTS tilesets;"
													  "CREATE TABLE tilesets ("
													  "id INTEGER PRIMARY KEY AUTOINCREMENT,"
													  "name TEXT NOT NULL UNIQUE,"
													  "source_file TEXT NOT NULL DEFAULT '',"
													  "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
													  "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
													  ");"
													  "CREATE TABLE tileset_sections ("
													  "id INTEGER PRIMARY KEY AUTOINCREMENT,"
													  "tileset_id INTEGER NOT NULL,"
													  "section_type TEXT NOT NULL,"
													  "sort_order INTEGER NOT NULL DEFAULT 0,"
													  "FOREIGN KEY (tileset_id) REFERENCES tilesets(id) ON DELETE CASCADE"
													  ");"
													  "CREATE TABLE tileset_brush_entries ("
													  "id INTEGER PRIMARY KEY AUTOINCREMENT,"
													  "tileset_section_id INTEGER NOT NULL,"
													  "entry_kind TEXT NOT NULL DEFAULT 'brush',"
													  "brush_id INTEGER,"
													  "brush_name TEXT NOT NULL DEFAULT '',"
													  "item_id INTEGER NOT NULL DEFAULT 0,"
													  "from_item_id INTEGER NOT NULL DEFAULT 0,"
													  "to_item_id INTEGER NOT NULL DEFAULT 0,"
													  "after_brush_name TEXT NOT NULL DEFAULT '',"
													  "after_item_id INTEGER NOT NULL DEFAULT 0,"
													  "sort_order INTEGER NOT NULL DEFAULT 0,"
													  "FOREIGN KEY (tileset_section_id) REFERENCES tileset_sections(id) ON DELETE CASCADE,"
													  "FOREIGN KEY (brush_id) REFERENCES brushes(id) ON DELETE SET NULL"
													  ");"
													  "CREATE INDEX idx_tileset_brush_entries_section ON tileset_brush_entries(tileset_section_id, sort_order);"
													  "CREATE INDEX idx_tileset_brush_entries_name ON tileset_brush_entries(brush_name);"
													  "CREATE INDEX idx_tileset_brush_entries_item ON tileset_brush_entries(item_id, from_item_id, to_item_id);";

	wxString ToWxString(const char* value) {
		return value ? wxString::FromUTF8(value) : wxString();
	}

	wxString MakeTransactionSavepointName(int savepointId) {
		return wxString::Format("brushdb_sp_%d", savepointId);
	}

	void FinalizeStatements(std::initializer_list<sqlite3_stmt*> statements) {
		for (sqlite3_stmt* stmt : statements) {
			if (stmt) {
				sqlite3_finalize(stmt);
			}
		}
	}

	void BindNullableInt64(sqlite3_stmt* stmt, int index, int64_t value) {
		if (value > 0) {
			sqlite3_bind_int64(stmt, index, value);
		} else {
			sqlite3_bind_null(stmt, index);
		}
	}

	int64_t ReadNullableInt64(sqlite3_stmt* stmt, int index) {
		return sqlite3_column_type(stmt, index) == SQLITE_NULL ? 0 : sqlite3_column_int64(stmt, index);
	}

	wxString DerivePaletteGroupNameFromSectionType(const wxString &sectionType) {
		if (sectionType.IsSameAs("terrain", false) || sectionType.IsSameAs("terrain_and_raw", false)) {
			return kBuiltinPaletteGroupTerrain;
		}
		if (sectionType.IsSameAs("doodad", false) || sectionType.IsSameAs("doodad_and_raw", false)) {
			return kBuiltinPaletteGroupDoodad;
		}
		if (sectionType.IsSameAs("item", false) || sectionType.IsSameAs("items", false) || sectionType.IsSameAs("items_and_raw", false)) {
			return kBuiltinPaletteGroupItem;
		}
		return kBuiltinPaletteGroupOther;
	}

	wxString DerivePaletteRuntimeFamilyFromSectionType(const wxString &sectionType) {
		if (sectionType.IsSameAs("terrain", false) || sectionType.IsSameAs("terrain_and_raw", false)) {
			return "terrain";
		}
		if (sectionType.IsSameAs("doodad", false) || sectionType.IsSameAs("doodad_and_raw", false)) {
			return "doodad";
		}
		if (sectionType.IsSameAs("item", false) || sectionType.IsSameAs("items", false) || sectionType.IsSameAs("items_and_raw", false)) {
			return "item";
		}
		return "raw";
	}

	wxString DerivePaletteGroupNameFromTileset(const TilesetStorageRecord &tileset) {
		if (!tileset.paletteGroupName.IsEmpty()) {
			return tileset.paletteGroupName;
		}
		if (!tileset.sections.empty()) {
			return DerivePaletteGroupNameFromSectionType(tileset.sections.front().sectionType);
		}
		return kBuiltinPaletteGroupOther;
	}

	wxString DerivePaletteRuntimeFamilyFromTileset(const TilesetStorageRecord &tileset) {
		if (!tileset.paletteGroupRuntimeFamily.IsEmpty()) {
			return tileset.paletteGroupRuntimeFamily;
		}
		if (!tileset.sections.empty()) {
			return DerivePaletteRuntimeFamilyFromSectionType(tileset.sections.front().sectionType);
		}
		return "raw";
	}

	int BindBrushRecordFields(sqlite3_stmt* stmt, const BrushRecord &brush, int parameterIndex) {
		sqlite3_bind_text(stmt, parameterIndex++, brush.name.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt, parameterIndex++, brush.type.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int(stmt, parameterIndex++, brush.lookId);
		sqlite3_bind_int(stmt, parameterIndex++, brush.zOrder);
		sqlite3_bind_text(stmt, parameterIndex++, brush.sourceFile.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int(stmt, parameterIndex++, brush.serverLookId);
		sqlite3_bind_int(stmt, parameterIndex++, brush.draggable ? 1 : 0);
		sqlite3_bind_int(stmt, parameterIndex++, brush.onBlocking ? 1 : 0);
		sqlite3_bind_int(stmt, parameterIndex++, brush.onDuplicate ? 1 : 0);
		sqlite3_bind_int(stmt, parameterIndex++, brush.redoBorders ? 1 : 0);
		sqlite3_bind_int(stmt, parameterIndex++, brush.randomize ? 1 : 0);
		sqlite3_bind_int(stmt, parameterIndex++, brush.oneSize ? 1 : 0);
		sqlite3_bind_int(stmt, parameterIndex++, brush.soloOptional ? 1 : 0);
		sqlite3_bind_int(stmt, parameterIndex++, brush.thickness);
		sqlite3_bind_int(stmt, parameterIndex++, brush.thicknessCeiling);
		sqlite3_bind_int(stmt, parameterIndex++, brush.removeOptionalBorder ? 1 : 0);
		return parameterIndex;
	}

	struct AlignedNodeWriteContext {
		sqlite3_stmt* insertNodeStmt = nullptr;
		sqlite3_stmt* insertItemStmt = nullptr;
		wxString insertNodeError;
		wxString insertItemError;
	};

	void ReadBrushRecordFromStatement(sqlite3_stmt* stmt, BrushRecord &outBrush) {
		outBrush.id = sqlite3_column_int64(stmt, 0);
		outBrush.name = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
		outBrush.type = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
		outBrush.lookId = sqlite3_column_int(stmt, 3);
		outBrush.zOrder = sqlite3_column_int(stmt, 4);
		outBrush.sourceFile = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)));
		outBrush.serverLookId = sqlite3_column_int(stmt, 6);
		outBrush.draggable = sqlite3_column_int(stmt, 7) != 0;
		outBrush.onBlocking = sqlite3_column_int(stmt, 8) != 0;
		outBrush.onDuplicate = sqlite3_column_int(stmt, 9) != 0;
		outBrush.redoBorders = sqlite3_column_int(stmt, 10) != 0;
		outBrush.randomize = sqlite3_column_int(stmt, 11) != 0;
		outBrush.oneSize = sqlite3_column_int(stmt, 12) != 0;
		outBrush.soloOptional = sqlite3_column_int(stmt, 13) != 0;
		outBrush.thickness = sqlite3_column_int(stmt, 14);
		outBrush.thicknessCeiling = sqlite3_column_int(stmt, 15);
		outBrush.removeOptionalBorder = sqlite3_column_int(stmt, 16) != 0;
	}

	template <typename NodeRecord, typename ItemRecord, typename SetErrorFn>
	bool WriteAlignedNodesWithItems(
		sqlite3* connection,
		int64_t brushId,
		const std::vector<NodeRecord> &nodes,
		const AlignedNodeWriteContext &context,
		SetErrorFn &&setErrorFromDatabase
	) {
		for (const NodeRecord &node : nodes) {
			sqlite3_reset(context.insertNodeStmt);
			sqlite3_clear_bindings(context.insertNodeStmt);
			sqlite3_bind_int64(context.insertNodeStmt, 1, brushId);
			sqlite3_bind_text(context.insertNodeStmt, 2, node.align.utf8_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(context.insertNodeStmt, 3, node.sortOrder);
			int rc = sqlite3_step(context.insertNodeStmt);
			if (rc != SQLITE_DONE) {
				return setErrorFromDatabase(context.insertNodeError);
			}

			const int64_t nodeId = sqlite3_last_insert_rowid(connection);
			for (const ItemRecord &item : node.items) {
				sqlite3_reset(context.insertItemStmt);
				sqlite3_clear_bindings(context.insertItemStmt);
				sqlite3_bind_int64(context.insertItemStmt, 1, nodeId);
				sqlite3_bind_int(context.insertItemStmt, 2, item.itemId);
				sqlite3_bind_int(context.insertItemStmt, 3, item.chance);
				sqlite3_bind_int(context.insertItemStmt, 4, item.sortOrder);
				rc = sqlite3_step(context.insertItemStmt);
				if (rc != SQLITE_DONE) {
					return setErrorFromDatabase(context.insertItemError);
				}
			}
		}

		return true;
	}

	template <typename NodeRecord, typename ItemRecord, typename SetErrorFn>
	bool ReadAlignedNodesWithItems(
		sqlite3_stmt* nodeStmt,
		sqlite3_stmt* itemStmt,
		std::vector<NodeRecord> &outNodes,
		const wxString &readNodesError,
		const wxString &readItemsError,
		SetErrorFn &&setErrorFromDatabase
	) {
		for (;;) {
			const int nodeRc = sqlite3_step(nodeStmt);
			if (nodeRc == SQLITE_DONE) {
				break;
			}
			if (nodeRc != SQLITE_ROW) {
				return setErrorFromDatabase(readNodesError);
			}

			const int64_t nodeId = sqlite3_column_int64(nodeStmt, 0);

			NodeRecord node;
			node.align = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(nodeStmt, 1)));
			node.sortOrder = sqlite3_column_int(nodeStmt, 2);

			sqlite3_reset(itemStmt);
			sqlite3_clear_bindings(itemStmt);
			sqlite3_bind_int64(itemStmt, 1, nodeId);

			for (;;) {
				const int itemRc = sqlite3_step(itemStmt);
				if (itemRc == SQLITE_DONE) {
					break;
				}
				if (itemRc != SQLITE_ROW) {
					return setErrorFromDatabase(readItemsError);
				}

				ItemRecord item;
				item.itemId = sqlite3_column_int(itemStmt, 0);
				item.chance = sqlite3_column_int(itemStmt, 1);
				item.sortOrder = sqlite3_column_int(itemStmt, 2);
				node.items.push_back(item);
			}

			outNodes.push_back(node);
		}

		return true;
	}

	template <typename SetErrorFn>
	bool WriteGroundBorderConditions(
		int64_t caseId,
		const std::vector<GroundBorderCaseConditionRecord> &conditions,
		sqlite3_stmt* insertConditionStmt,
		SetErrorFn &&setErrorFromDatabase
	) {
		for (const GroundBorderCaseConditionRecord &condition : conditions) {
			sqlite3_reset(insertConditionStmt);
			sqlite3_clear_bindings(insertConditionStmt);
			sqlite3_bind_int64(insertConditionStmt, 1, caseId);
			sqlite3_bind_text(insertConditionStmt, 2, condition.conditionType.utf8_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(insertConditionStmt, 3, condition.matchValue);
			sqlite3_bind_text(insertConditionStmt, 4, condition.edge.utf8_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(insertConditionStmt, 5, condition.sortOrder);
			if (sqlite3_step(insertConditionStmt) != SQLITE_DONE) {
				return setErrorFromDatabase("Failed to insert ground border condition");
			}
		}

		return true;
	}

	template <typename SetErrorFn>
	bool WriteGroundBorderActions(
		int64_t caseId,
		const std::vector<GroundBorderCaseActionRecord> &actions,
		sqlite3_stmt* insertActionStmt,
		SetErrorFn &&setErrorFromDatabase
	) {
		for (const GroundBorderCaseActionRecord &action : actions) {
			sqlite3_reset(insertActionStmt);
			sqlite3_clear_bindings(insertActionStmt);
			sqlite3_bind_int64(insertActionStmt, 1, caseId);
			sqlite3_bind_text(insertActionStmt, 2, action.actionType.utf8_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(insertActionStmt, 3, action.targetValue);
			sqlite3_bind_text(insertActionStmt, 4, action.edge.utf8_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(insertActionStmt, 5, action.replacementValue);
			sqlite3_bind_int(insertActionStmt, 6, action.sortOrder);
			if (sqlite3_step(insertActionStmt) != SQLITE_DONE) {
				return setErrorFromDatabase("Failed to insert ground border action");
			}
		}

		return true;
	}

	template <typename SetErrorFn>
	bool WriteGroundBorderCases(
		sqlite3* connection,
		int64_t groundBrushBorderId,
		const std::vector<GroundBorderCaseRecord> &cases,
		sqlite3_stmt* insertCaseStmt,
		sqlite3_stmt* insertConditionStmt,
		sqlite3_stmt* insertActionStmt,
		SetErrorFn &&setErrorFromDatabase
	) {
		for (const GroundBorderCaseRecord &caseRecord : cases) {
			sqlite3_reset(insertCaseStmt);
			sqlite3_clear_bindings(insertCaseStmt);
			sqlite3_bind_int64(insertCaseStmt, 1, groundBrushBorderId);
			sqlite3_bind_int(insertCaseStmt, 2, caseRecord.sortOrder);
			if (sqlite3_step(insertCaseStmt) != SQLITE_DONE) {
				return setErrorFromDatabase("Failed to insert ground border case");
			}

			const int64_t caseId = sqlite3_last_insert_rowid(connection);
			if (!WriteGroundBorderConditions(caseId, caseRecord.conditions, insertConditionStmt, setErrorFromDatabase)) {
				return false;
			}
			if (!WriteGroundBorderActions(caseId, caseRecord.actions, insertActionStmt, setErrorFromDatabase)) {
				return false;
			}
		}

		return true;
	}

	template <typename SetErrorFn>
	bool ReadGroundBorderConditions(
		int64_t caseId,
		sqlite3_stmt* conditionStmt,
		std::vector<GroundBorderCaseConditionRecord> &outConditions,
		SetErrorFn &&setErrorFromDatabase
	) {
		sqlite3_reset(conditionStmt);
		sqlite3_clear_bindings(conditionStmt);
		sqlite3_bind_int64(conditionStmt, 1, caseId);

		for (;;) {
			const int conditionRc = sqlite3_step(conditionStmt);
			if (conditionRc == SQLITE_DONE) {
				break;
			}
			if (conditionRc != SQLITE_ROW) {
				return setErrorFromDatabase("Failed to read ground border case conditions");
			}

			GroundBorderCaseConditionRecord condition;
			condition.conditionType = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(conditionStmt, 0)));
			condition.matchValue = sqlite3_column_int(conditionStmt, 1);
			condition.edge = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(conditionStmt, 2)));
			condition.sortOrder = sqlite3_column_int(conditionStmt, 3);
			outConditions.push_back(condition);
		}

		return true;
	}

	template <typename SetErrorFn>
	bool ReadGroundBorderActions(
		int64_t caseId,
		sqlite3_stmt* actionStmt,
		std::vector<GroundBorderCaseActionRecord> &outActions,
		SetErrorFn &&setErrorFromDatabase
	) {
		sqlite3_reset(actionStmt);
		sqlite3_clear_bindings(actionStmt);
		sqlite3_bind_int64(actionStmt, 1, caseId);

		for (;;) {
			const int actionRc = sqlite3_step(actionStmt);
			if (actionRc == SQLITE_DONE) {
				break;
			}
			if (actionRc != SQLITE_ROW) {
				return setErrorFromDatabase("Failed to read ground border case actions");
			}

			GroundBorderCaseActionRecord action;
			action.actionType = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(actionStmt, 0)));
			action.targetValue = sqlite3_column_int(actionStmt, 1);
			action.edge = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(actionStmt, 2)));
			action.replacementValue = sqlite3_column_int(actionStmt, 3);
			action.sortOrder = sqlite3_column_int(actionStmt, 4);
			outActions.push_back(action);
		}

		return true;
	}

	template <typename SetErrorFn>
	bool ReadGroundBorderCases(
		sqlite3_stmt* caseStmt,
		sqlite3_stmt* conditionStmt,
		sqlite3_stmt* actionStmt,
		std::vector<GroundBorderCaseRecord> &outCases,
		SetErrorFn &&setErrorFromDatabase
	) {
		for (;;) {
			const int caseRc = sqlite3_step(caseStmt);
			if (caseRc == SQLITE_DONE) {
				break;
			}
			if (caseRc != SQLITE_ROW) {
				return setErrorFromDatabase("Failed to read ground border cases");
			}

			const int64_t caseId = sqlite3_column_int64(caseStmt, 0);

			GroundBorderCaseRecord caseRecord;
			caseRecord.sortOrder = sqlite3_column_int(caseStmt, 1);
			if (!ReadGroundBorderConditions(caseId, conditionStmt, caseRecord.conditions, setErrorFromDatabase)) {
				return false;
			}
			if (!ReadGroundBorderActions(caseId, actionStmt, caseRecord.actions, setErrorFromDatabase)) {
				return false;
			}

			outCases.push_back(caseRecord);
		}

		return true;
	}

	template <typename SetErrorFn>
	bool WriteDoodadSingleItems(
		int64_t alternativeId,
		const std::vector<DoodadSingleItemRecord> &singleItems,
		sqlite3_stmt* insertSingleStmt,
		SetErrorFn &&setErrorFromDatabase
	) {
		for (const DoodadSingleItemRecord &single : singleItems) {
			sqlite3_reset(insertSingleStmt);
			sqlite3_clear_bindings(insertSingleStmt);
			sqlite3_bind_int64(insertSingleStmt, 1, alternativeId);
			sqlite3_bind_int(insertSingleStmt, 2, single.itemId);
			sqlite3_bind_int(insertSingleStmt, 3, single.chance);
			sqlite3_bind_int(insertSingleStmt, 4, single.sortOrder);
			if (sqlite3_step(insertSingleStmt) != SQLITE_DONE) {
				return setErrorFromDatabase("Failed to insert doodad single item");
			}
		}

		return true;
	}

	template <typename SetErrorFn>
	bool WriteDoodadCompositeTileItems(
		int64_t tileId,
		const std::vector<DoodadCompositeTileItemRecord> &items,
		sqlite3_stmt* insertTileItemStmt,
		SetErrorFn &&setErrorFromDatabase
	) {
		for (const DoodadCompositeTileItemRecord &item : items) {
			sqlite3_reset(insertTileItemStmt);
			sqlite3_clear_bindings(insertTileItemStmt);
			sqlite3_bind_int64(insertTileItemStmt, 1, tileId);
			sqlite3_bind_int(insertTileItemStmt, 2, item.itemId);
			sqlite3_bind_int(insertTileItemStmt, 3, item.sortOrder);
			if (sqlite3_step(insertTileItemStmt) != SQLITE_DONE) {
				return setErrorFromDatabase("Failed to insert doodad composite tile item");
			}
		}

		return true;
	}

	template <typename SetErrorFn>
	bool WriteDoodadCompositeTiles(
		sqlite3* connection,
		int64_t compositeId,
		const std::vector<DoodadCompositeTileRecord> &tiles,
		sqlite3_stmt* insertTileStmt,
		sqlite3_stmt* insertTileItemStmt,
		SetErrorFn &&setErrorFromDatabase
	) {
		for (const DoodadCompositeTileRecord &tile : tiles) {
			sqlite3_reset(insertTileStmt);
			sqlite3_clear_bindings(insertTileStmt);
			sqlite3_bind_int64(insertTileStmt, 1, compositeId);
			sqlite3_bind_int(insertTileStmt, 2, tile.offsetX);
			sqlite3_bind_int(insertTileStmt, 3, tile.offsetY);
			sqlite3_bind_int(insertTileStmt, 4, tile.offsetZ);
			sqlite3_bind_int(insertTileStmt, 5, tile.sortOrder);
			if (sqlite3_step(insertTileStmt) != SQLITE_DONE) {
				return setErrorFromDatabase("Failed to insert doodad composite tile");
			}

			const int64_t tileId = sqlite3_last_insert_rowid(connection);
			if (!WriteDoodadCompositeTileItems(tileId, tile.items, insertTileItemStmt, setErrorFromDatabase)) {
				return false;
			}
		}

		return true;
	}

	template <typename SetErrorFn>
	bool WriteDoodadComposites(
		sqlite3* connection,
		int64_t alternativeId,
		const std::vector<DoodadCompositeRecord> &composites,
		sqlite3_stmt* insertCompositeStmt,
		sqlite3_stmt* insertTileStmt,
		sqlite3_stmt* insertTileItemStmt,
		SetErrorFn &&setErrorFromDatabase
	) {
		for (const DoodadCompositeRecord &composite : composites) {
			sqlite3_reset(insertCompositeStmt);
			sqlite3_clear_bindings(insertCompositeStmt);
			sqlite3_bind_int64(insertCompositeStmt, 1, alternativeId);
			sqlite3_bind_int(insertCompositeStmt, 2, composite.chance);
			sqlite3_bind_int(insertCompositeStmt, 3, composite.sortOrder);
			if (sqlite3_step(insertCompositeStmt) != SQLITE_DONE) {
				return setErrorFromDatabase("Failed to insert doodad composite");
			}

			const int64_t compositeId = sqlite3_last_insert_rowid(connection);
			if (!WriteDoodadCompositeTiles(connection, compositeId, composite.tiles, insertTileStmt, insertTileItemStmt, setErrorFromDatabase)) {
				return false;
			}
		}

		return true;
	}

	template <typename SetErrorFn>
	bool ReadDoodadSingleItems(
		int64_t alternativeId,
		sqlite3_stmt* singleStmt,
		std::vector<DoodadSingleItemRecord> &outSingleItems,
		SetErrorFn &&setErrorFromDatabase
	) {
		sqlite3_reset(singleStmt);
		sqlite3_clear_bindings(singleStmt);
		sqlite3_bind_int64(singleStmt, 1, alternativeId);

		for (;;) {
			const int singleRc = sqlite3_step(singleStmt);
			if (singleRc == SQLITE_DONE) {
				break;
			}
			if (singleRc != SQLITE_ROW) {
				return setErrorFromDatabase("Failed to read doodad single items");
			}

			DoodadSingleItemRecord item;
			item.itemId = sqlite3_column_int(singleStmt, 0);
			item.chance = sqlite3_column_int(singleStmt, 1);
			item.sortOrder = sqlite3_column_int(singleStmt, 2);
			outSingleItems.push_back(item);
		}

		return true;
	}

	template <typename SetErrorFn>
	bool ReadDoodadCompositeTileItems(
		int64_t tileId,
		sqlite3_stmt* tileItemStmt,
		std::vector<DoodadCompositeTileItemRecord> &outItems,
		SetErrorFn &&setErrorFromDatabase
	) {
		sqlite3_reset(tileItemStmt);
		sqlite3_clear_bindings(tileItemStmt);
		sqlite3_bind_int64(tileItemStmt, 1, tileId);

		for (;;) {
			const int tileItemRc = sqlite3_step(tileItemStmt);
			if (tileItemRc == SQLITE_DONE) {
				break;
			}
			if (tileItemRc != SQLITE_ROW) {
				return setErrorFromDatabase("Failed to read doodad composite tile items");
			}

			DoodadCompositeTileItemRecord item;
			item.itemId = sqlite3_column_int(tileItemStmt, 0);
			item.sortOrder = sqlite3_column_int(tileItemStmt, 1);
			outItems.push_back(item);
		}

		return true;
	}

	template <typename SetErrorFn>
	bool ReadDoodadCompositeTiles(
		int64_t compositeId,
		sqlite3_stmt* tileStmt,
		sqlite3_stmt* tileItemStmt,
		std::vector<DoodadCompositeTileRecord> &outTiles,
		SetErrorFn &&setErrorFromDatabase
	) {
		sqlite3_reset(tileStmt);
		sqlite3_clear_bindings(tileStmt);
		sqlite3_bind_int64(tileStmt, 1, compositeId);

		for (;;) {
			const int tileRc = sqlite3_step(tileStmt);
			if (tileRc == SQLITE_DONE) {
				break;
			}
			if (tileRc != SQLITE_ROW) {
				return setErrorFromDatabase("Failed to read doodad composite tiles");
			}

			const int64_t tileId = sqlite3_column_int64(tileStmt, 0);

			DoodadCompositeTileRecord tile;
			tile.offsetX = sqlite3_column_int(tileStmt, 1);
			tile.offsetY = sqlite3_column_int(tileStmt, 2);
			tile.offsetZ = sqlite3_column_int(tileStmt, 3);
			tile.sortOrder = sqlite3_column_int(tileStmt, 4);
			if (!ReadDoodadCompositeTileItems(tileId, tileItemStmt, tile.items, setErrorFromDatabase)) {
				return false;
			}

			outTiles.push_back(tile);
		}

		return true;
	}

	template <typename SetErrorFn>
	bool ReadDoodadComposites(
		int64_t alternativeId,
		sqlite3_stmt* compositeStmt,
		sqlite3_stmt* tileStmt,
		sqlite3_stmt* tileItemStmt,
		std::vector<DoodadCompositeRecord> &outComposites,
		SetErrorFn &&setErrorFromDatabase
	) {
		sqlite3_reset(compositeStmt);
		sqlite3_clear_bindings(compositeStmt);
		sqlite3_bind_int64(compositeStmt, 1, alternativeId);

		for (;;) {
			const int compositeRc = sqlite3_step(compositeStmt);
			if (compositeRc == SQLITE_DONE) {
				break;
			}
			if (compositeRc != SQLITE_ROW) {
				return setErrorFromDatabase("Failed to read doodad composites");
			}

			const int64_t compositeId = sqlite3_column_int64(compositeStmt, 0);

			DoodadCompositeRecord composite;
			composite.chance = sqlite3_column_int(compositeStmt, 1);
			composite.sortOrder = sqlite3_column_int(compositeStmt, 2);
			if (!ReadDoodadCompositeTiles(compositeId, tileStmt, tileItemStmt, composite.tiles, setErrorFromDatabase)) {
				return false;
			}

			outComposites.push_back(composite);
		}

		return true;
	}

	template <typename SetErrorFn>
	bool ResolveTilesetBrushReference(
		sqlite3_stmt* findBrushStmt,
		const TilesetEntryRecord &entry,
		int64_t &resolvedBrushId,
		SetErrorFn &&setErrorFromDatabase
	) {
		resolvedBrushId = entry.brushId;
		if (resolvedBrushId != 0 || entry.brushName.IsEmpty()) {
			return true;
		}

		sqlite3_reset(findBrushStmt);
		sqlite3_clear_bindings(findBrushStmt);
		sqlite3_bind_text(findBrushStmt, 1, entry.brushName.utf8_str(), -1, SQLITE_TRANSIENT);

		const int firstRc = sqlite3_step(findBrushStmt);
		if (firstRc == SQLITE_DONE) {
			return true;
		}
		if (firstRc != SQLITE_ROW) {
			return setErrorFromDatabase("Failed to resolve tileset brush reference");
		}

		resolvedBrushId = sqlite3_column_int64(findBrushStmt, 0);
		const int secondRc = sqlite3_step(findBrushStmt);
		if (secondRc == SQLITE_ROW) {
			resolvedBrushId = 0;
			return true;
		}
		if (secondRc != SQLITE_DONE) {
			return setErrorFromDatabase("Failed to resolve tileset brush reference");
		}

		return true;
	}

	template <typename SetErrorFn>
	bool WriteTilesetEntries(
		int64_t sectionId,
		const std::vector<TilesetEntryRecord> &entries,
		sqlite3_stmt* insertEntryStmt,
		sqlite3_stmt* findBrushStmt,
		SetErrorFn &&setErrorFromDatabase
	) {
		for (const TilesetEntryRecord &entry : entries) {
			int64_t resolvedBrushId = 0;
			if (!ResolveTilesetBrushReference(findBrushStmt, entry, resolvedBrushId, setErrorFromDatabase)) {
				return false;
			}

			sqlite3_reset(insertEntryStmt);
			sqlite3_clear_bindings(insertEntryStmt);
			sqlite3_bind_int64(insertEntryStmt, 1, sectionId);
			sqlite3_bind_text(insertEntryStmt, 2, entry.entryKind.utf8_str(), -1, SQLITE_TRANSIENT);
			if (resolvedBrushId != 0) {
				sqlite3_bind_int64(insertEntryStmt, 3, resolvedBrushId);
			} else {
				sqlite3_bind_null(insertEntryStmt, 3);
			}
			sqlite3_bind_text(insertEntryStmt, 4, entry.brushName.utf8_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(insertEntryStmt, 5, entry.itemId);
			sqlite3_bind_int(insertEntryStmt, 6, entry.fromItemId);
			sqlite3_bind_int(insertEntryStmt, 7, entry.toItemId);
			sqlite3_bind_text(insertEntryStmt, 8, entry.afterBrushName.utf8_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(insertEntryStmt, 9, entry.afterItemId);
			sqlite3_bind_int(insertEntryStmt, 10, entry.sortOrder);
			if (sqlite3_step(insertEntryStmt) != SQLITE_DONE) {
				return setErrorFromDatabase("Failed to insert tileset entry");
			}
		}

		return true;
	}

	template <typename SetErrorFn>
	bool WriteWallPartItems(
		int64_t wallPartId,
		const std::vector<WallPartItemRecord> &items,
		sqlite3_stmt* insertItemStmt,
		SetErrorFn &&setErrorFromDatabase
	) {
		for (const WallPartItemRecord &item : items) {
			sqlite3_reset(insertItemStmt);
			sqlite3_clear_bindings(insertItemStmt);
			sqlite3_bind_int64(insertItemStmt, 1, wallPartId);
			sqlite3_bind_int(insertItemStmt, 2, item.itemId);
			sqlite3_bind_int(insertItemStmt, 3, item.chance);
			sqlite3_bind_int(insertItemStmt, 4, item.sortOrder);
			if (sqlite3_step(insertItemStmt) != SQLITE_DONE) {
				return setErrorFromDatabase("Failed to insert wall part item");
			}
		}

		return true;
	}

	template <typename SetErrorFn>
	bool WriteWallPartDoors(
		int64_t wallPartId,
		const std::vector<WallPartDoorRecord> &doors,
		sqlite3_stmt* insertDoorStmt,
		SetErrorFn &&setErrorFromDatabase
	) {
		for (const WallPartDoorRecord &door : doors) {
			sqlite3_reset(insertDoorStmt);
			sqlite3_clear_bindings(insertDoorStmt);
			sqlite3_bind_int64(insertDoorStmt, 1, wallPartId);
			sqlite3_bind_int(insertDoorStmt, 2, door.itemId);
			sqlite3_bind_text(insertDoorStmt, 3, door.doorType.utf8_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(insertDoorStmt, 4, door.isOpen ? 1 : 0);
			sqlite3_bind_int(insertDoorStmt, 5, door.wallHateMe ? 1 : 0);
			sqlite3_bind_int(insertDoorStmt, 6, door.sortOrder);
			if (sqlite3_step(insertDoorStmt) != SQLITE_DONE) {
				return setErrorFromDatabase("Failed to insert wall part door");
			}
		}

		return true;
	}
} // namespace

BrushDatabaseSession::BrushDatabaseSession() = default;

BrushDatabaseSession::~BrushDatabaseSession() {
	close();
}

BrushDatabaseComponent::BrushDatabaseComponent(BrushDatabaseSession &session) :
	session_(session),
	connection_(session.connection_),
	lastError_(session.lastError_),
	readOnly_(session.readOnly_) {
}

BrushDatabaseSchemaManager::BrushDatabaseSchemaManager(BrushDatabaseSession &session) :
	BrushDatabaseComponent(session) {
}

BrushDatabaseBrushRepository::BrushDatabaseBrushRepository(BrushDatabaseSession &session) :
	BrushDatabaseComponent(session) {
}

BrushDatabaseCatalogRepository::BrushDatabaseCatalogRepository(BrushDatabaseSession &session, BrushDatabaseSchemaManager &schemaManager) :
	BrushDatabaseComponent(session),
	schemaManager_(schemaManager) {
}

BrushDatabase::~BrushDatabase() = default;

bool BrushDatabase::initialize(const wxString &databasePath) {
	if (!session_.open(databasePath)) {
		return false;
	}
	return schemaManager_.initializeSchema();
}

bool BrushDatabase::openReadOnly(const wxString &databasePath) {
	return session_.openReadOnly(databasePath);
}

bool BrushDatabase::open(const wxString &databasePath) {
	return session_.open(databasePath);
}

void BrushDatabase::close() {
	session_.close();
}

bool BrushDatabase::isOpen() const {
	return session_.isOpen();
}

bool BrushDatabase::isReadOnly() const {
	return session_.isReadOnly();
}

const wxString &BrushDatabase::getDatabasePath() const {
	return session_.getDatabasePath();
}

const wxString &BrushDatabase::getLastError() const {
	return session_.getLastError();
}

int BrushDatabase::getLastSqliteErrorCode() const {
	return session_.getLastSqliteErrorCode();
}

int BrushDatabase::getLastSqliteExtendedErrorCode() const {
	return session_.getLastSqliteExtendedErrorCode();
}

bool BrushDatabase::testDatabaseConnection() {
	return session_.testDatabaseConnection();
}

bool BrushDatabase::quickCheck(int maxErrors) {
	return session_.quickCheck(maxErrors);
}

bool BrushDatabase::upsertBrush(const BrushRecord &brush, int64_t &brushId) {
	return brushRepository_.upsertBrush(brush, brushId);
}

bool BrushDatabase::getBrushById(int64_t brushId, BrushRecord &outBrush) {
	return brushRepository_.getBrushById(brushId, outBrush);
}

bool BrushDatabase::updateBrush(const BrushRecord &brush) {
	return brushRepository_.updateBrush(brush);
}
bool BrushDatabase::listBrushesByType(const wxString &type, std::vector<BrushRecord> &outBrushes) {
	return brushRepository_.listBrushesByType(type, outBrushes);
}

bool BrushDatabase::findBrushByNameAndType(const wxString &name, const wxString &type, BrushRecord &outBrush) {
	return brushRepository_.findBrushByNameAndType(name, type, outBrush);
}

bool BrushDatabase::getCompleteBrushById(int64_t brushId, BrushStorageRecord &outBrush) {
	return brushRepository_.getCompleteBrushById(brushId, outBrush);
}

bool BrushDatabase::updateBrushReferenceNames(int64_t brushId, const wxString &oldName, const wxString &newName) {
	return brushRepository_.updateBrushReferenceNames(brushId, oldName, newName);
}

bool BrushDatabase::deleteBrushReferences(int64_t brushId, const wxString &brushName) {
	return brushRepository_.deleteBrushReferences(brushId, brushName);
}

bool BrushDatabase::deleteBrush(int64_t brushId) {
	return brushRepository_.deleteBrush(brushId);
}

bool BrushDatabase::deleteBrushesByType(const wxString &type) {
	return brushRepository_.deleteBrushesByType(type);
}

bool BrushDatabase::replaceBrushItems(int64_t brushId, const std::vector<BrushItemRecord> &items) {
	return brushRepository_.replaceBrushItems(brushId, items);
}

bool BrushDatabase::upsertBorderSet(const BorderSetRecord &borderSet, int64_t &borderSetId) {
	return brushRepository_.upsertBorderSet(borderSet, borderSetId);
}

bool BrushDatabase::getBorderSetById(int64_t borderSetId, BorderSetRecord &outBorderSet) {
	return brushRepository_.getBorderSetById(borderSetId, outBorderSet);
}
bool BrushDatabase::findBorderSetByXmlBorderId(int xmlBorderId, BorderSetRecord &outBorderSet) {
	return brushRepository_.findBorderSetByXmlBorderId(xmlBorderId, outBorderSet);
}

bool BrushDatabase::listBorderSetsByScope(const wxString &borderScope, std::vector<BorderSetRecord> &outBorderSets) {
	return brushRepository_.listBorderSetsByScope(borderScope, outBorderSets);
}

bool BrushDatabase::listBorderSetUsages(int64_t borderSetId, std::vector<BorderSetUsageRecord> &outUsages) {
	return brushRepository_.listBorderSetUsages(borderSetId, outUsages);
}

bool BrushDatabase::deleteBorderSet(int64_t borderSetId) {
	return brushRepository_.deleteBorderSet(borderSetId);
}

bool BrushDatabase::replaceBorderSetItems(int64_t borderSetId, const std::vector<BorderSetItemRecord> &items) {
	return brushRepository_.replaceBorderSetItems(borderSetId, items);
}


bool BrushDatabase::getBorderSetItems(int64_t borderSetId, std::vector<BorderSetItemRecord> &outItems) {
	return brushRepository_.getBorderSetItems(borderSetId, outItems);
}
bool BrushDatabase::deleteBorderSetsByScope(const wxString &borderScope) {
	return brushRepository_.deleteBorderSetsByScope(borderScope);
}

bool BrushDatabase::deleteOwnedBorderSetsForBrush(int64_t brushId) {
	return brushRepository_.deleteOwnedBorderSetsForBrush(brushId);
}

bool BrushDatabase::replaceGroundBrushBorders(int64_t brushId, const std::vector<GroundBrushBorderRecord> &borders) {
	return brushRepository_.replaceGroundBrushBorders(brushId, borders);
}

bool BrushDatabase::replaceBrushLinks(int64_t brushId, const std::vector<BrushLinkRecord> &links) {
	return brushRepository_.replaceBrushLinks(brushId, links);
}

bool BrushDatabase::listBrushUsages(int64_t brushId, const wxString &brushName, std::vector<BrushUsageRecord> &outUsages) {
	return brushRepository_.listBrushUsages(brushId, brushName, outUsages);
}

bool BrushDatabase::replaceWallParts(int64_t brushId, const std::vector<WallPartRecord> &parts) {
	return brushRepository_.replaceWallParts(brushId, parts);
}

bool BrushDatabase::replaceCarpetNodes(int64_t brushId, const std::vector<CarpetNodeRecord> &nodes) {
	return brushRepository_.replaceCarpetNodes(brushId, nodes);
}

bool BrushDatabase::replaceTableNodes(int64_t brushId, const std::vector<TableNodeRecord> &nodes) {
	return brushRepository_.replaceTableNodes(brushId, nodes);
}

bool BrushDatabase::replaceDoodadAlternatives(int64_t brushId, const std::vector<DoodadAlternativeRecord> &alternatives) {
	return brushRepository_.replaceDoodadAlternatives(brushId, alternatives);
}

bool BrushDatabase::resolveGroundReferenceNames() {
	return brushRepository_.resolveGroundReferenceNames();
}

bool BrushDatabase::replaceAllTilesets(const std::vector<TilesetStorageRecord> &tilesets) {
	return catalogRepository_.replaceAllTilesets(tilesets);
}

bool BrushDatabase::saveTileset(const TilesetStorageRecord &tileset) {
	return catalogRepository_.saveTileset(tileset);
}

bool BrushDatabase::deleteTileset(const wxString &name) {
	return catalogRepository_.deleteTileset(name);
}

bool BrushDatabase::savePaletteGroup(const PaletteGroupRecord &group) {
	return catalogRepository_.savePaletteGroup(group);
}

bool BrushDatabase::deletePaletteGroup(const wxString &name) {
	return catalogRepository_.deletePaletteGroup(name);
}

bool BrushDatabase::getAllPaletteGroups(std::vector<PaletteGroupRecord> &outGroups) {
	return catalogRepository_.getAllPaletteGroups(outGroups);
}

bool BrushDatabase::getAllTilesets(std::vector<TilesetStorageRecord> &outTilesets) {
	return catalogRepository_.getAllTilesets(outTilesets);
}

bool BrushDatabase::generateAuditReport(MaterialsDatabaseAuditReport &outReport) {
	return catalogRepository_.generateAuditReport(outReport);
}

bool BrushDatabase::hasCompleteImportForCurrentSchema(bool &outReady) {
	return catalogRepository_.hasCompleteImportForCurrentSchema(outReady);
}

bool BrushDatabase::hasCompleteImportForCurrentSchema(bool &outReady, wxString &outReason) {
	return catalogRepository_.hasCompleteImportForCurrentSchema(outReady, outReason);
}

bool BrushDatabase::isMaterialsImportComplete(bool &outComplete, wxString &outReason) {
	return catalogRepository_.isMaterialsImportComplete(outComplete, outReason);
}

bool BrushDatabase::getMaterialsImportStatus(MaterialsImportStatusRecord &outStatus, wxString &outReason) {
	return catalogRepository_.getMaterialsImportStatus(outStatus, outReason);
}

bool BrushDatabase::markMaterialsImportComplete(const wxString &source) {
	return catalogRepository_.markMaterialsImportComplete(source);
}

bool BrushDatabase::getCurrentSchemaVersion(int &outVersion) {
	return schemaManager_.getCurrentSchemaVersion(outVersion);
}

int BrushDatabase::getExpectedSchemaVersion() const {
	return catalogRepository_.getExpectedSchemaVersion();
}

bool BrushDatabaseSession::initialize(const wxString &databasePath) {
	return open(databasePath);
}

bool BrushDatabaseSession::openReadOnly(const wxString &databasePath) {
	return openInternal(databasePath, true);
}

bool BrushDatabaseSession::open(const wxString &databasePath) {
	return openInternal(databasePath, false);
}

bool BrushDatabaseSession::openInternal(const wxString &databasePath, bool readOnly) {
	if (connection_ && databasePath_ == databasePath) {
		if (readOnly_ == readOnly) {
			return true;
		}
		close();
	}
	if (connection_) {
		close();
	}

	if (!readOnly) {
		wxFileName dbFile(databasePath);
		if (!dbFile.DirExists() && !dbFile.Mkdir(0755, wxPATH_MKDIR_FULL)) {
			return setError("Failed to create SQLite directory: " + dbFile.GetPath());
		}
	}

	wxFileName dbFile(databasePath);
	const int flags = readOnly ? SQLITE_OPEN_READONLY : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
	const int rc = sqlite3_open_v2(dbFile.GetFullPath().utf8_str(), &connection_, flags, nullptr);
	if (rc != SQLITE_OK) {
		lastSqliteErrorCode_ = rc;
		lastSqliteExtendedErrorCode_ = connection_ ? sqlite3_extended_errcode(connection_) : rc;
		const wxString dbError = ToWxString(sqlite3_errmsg(connection_));
		lastError_ = "Failed to open SQLite database: " + dbError;
		spdlog::error("[BrushDatabase] {}", lastError_.ToStdString());
		close();
		return false;
	}

	databasePath_ = dbFile.GetFullPath();
	lastError_.clear();
	lastSqliteErrorCode_ = SQLITE_OK;
	lastSqliteExtendedErrorCode_ = SQLITE_OK;
	readOnly_ = readOnly;

	if (!execute("PRAGMA foreign_keys = ON;")) {
		close();
		return false;
	}
	if (!execute("PRAGMA busy_timeout = 3000;")) {
		close();
		return false;
	}
	if (!readOnly) {
		if (!execute("PRAGMA journal_mode = WAL;")) {
			close();
			return false;
		}
	} else if (!execute("PRAGMA query_only = ON;")) {
		close();
		return false;
	}

	spdlog::info("SQLite brush database opened ({}): {}", readOnly ? "read-only" : "read-write", databasePath_.ToStdString());
	return true;
}

void BrushDatabaseSession::close() {
	if (connection_) {
		sqlite3_close(connection_);
		connection_ = nullptr;
	}
	databasePath_.clear();
	readOnly_ = false;
	transactionDepth_ = 0;
	nextSavepointId_ = 0;
	savepointIds_.clear();
}

bool BrushDatabaseSession::isOpen() const {
	return connection_ != nullptr;
}

bool BrushDatabaseSession::isReadOnly() const {
	return readOnly_;
}

const wxString &BrushDatabaseSession::getDatabasePath() const {
	return databasePath_;
}

const wxString &BrushDatabaseSession::getLastError() const {
	return lastError_;
}

int BrushDatabaseSession::getLastSqliteErrorCode() const {
	return lastSqliteErrorCode_;
}

int BrushDatabaseSession::getLastSqliteExtendedErrorCode() const {
	return lastSqliteExtendedErrorCode_;
}

sqlite3* BrushDatabaseSession::connection() const {
	return connection_;
}

sqlite3* BrushDatabaseComponent::connection() const {
	return session_.connection();
}

bool BrushDatabaseComponent::isOpen() const {
	return session_.isOpen();
}

bool BrushDatabaseComponent::isReadOnly() const {
	return session_.isReadOnly();
}

const wxString &BrushDatabaseComponent::lastError() const {
	return session_.getLastError();
}

bool BrushDatabaseComponent::execute(const wxString &sql) {
	return session_.execute(sql);
}

bool BrushDatabaseComponent::prepare(const char* sql, sqlite3_stmt** stmt) {
	return session_.prepare(sql, stmt);
}

bool BrushDatabaseComponent::beginTransaction() {
	return session_.beginTransaction();
}

bool BrushDatabaseComponent::commitTransaction() {
	return session_.commitTransaction();
}

bool BrushDatabaseComponent::rollbackTransaction() {
	return session_.rollbackTransaction();
}

bool BrushDatabaseComponent::setError(const wxString &message) {
	return session_.setError(message);
}

bool BrushDatabaseComponent::setErrorFromDatabase(const wxString &prefix) {
	return session_.setErrorFromDatabase(prefix);
}

bool BrushDatabaseSchemaManager::ensureSchemaVersionTable() {
	if (!execute("CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL);")) {
		return false;
	}
	return execute("INSERT INTO schema_version(version) "
				   "SELECT 0 WHERE NOT EXISTS (SELECT 1 FROM schema_version);");
}

bool BrushDatabaseSchemaManager::getCurrentSchemaVersion(int &version) {
	version = 0;

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("SELECT version FROM schema_version LIMIT 1;", &stmt)) {
		return false;
	}

	const int rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW) {
		version = sqlite3_column_int(stmt, 0);
		sqlite3_finalize(stmt);
		return true;
	}

	sqlite3_finalize(stmt);
	if (rc == SQLITE_DONE) {
		return setError("SQLite schema version row was not found.");
	}
	return setErrorFromDatabase("Failed to query SQLite schema version");
}

bool BrushDatabaseSchemaManager::setSchemaVersion(int version) {
	sqlite3_stmt* stmt = nullptr;
	if (!prepare("UPDATE schema_version SET version = ?;", &stmt)) {
		return false;
	}

	sqlite3_bind_int(stmt, 1, version);
	const int rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		return setErrorFromDatabase("Failed to update SQLite schema version");
	}
	return true;
}

bool BrushDatabaseSchemaManager::columnExists(const wxString &tableName, const wxString &columnName, bool &exists) {
	exists = false;

	sqlite3_stmt* stmt = nullptr;
	const wxString sql = "PRAGMA table_info(" + tableName + ");";
	if (!prepare(sql.utf8_str(), &stmt)) {
		return false;
	}

	while (!exists) {
		const int rc = sqlite3_step(stmt);
		if (rc == SQLITE_DONE) {
			break;
		}
		if (rc != SQLITE_ROW) {
			sqlite3_finalize(stmt);
			return setErrorFromDatabase("Failed to inspect SQLite table info");
		}

		const wxString currentName = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
		if (currentName == columnName) {
			exists = true;
		}
	}

	sqlite3_finalize(stmt);
	return true;
}

bool BrushDatabaseSchemaManager::migrateToVersion1() {
	if (!execute("CREATE TABLE IF NOT EXISTS brushes ("
				 "id INTEGER PRIMARY KEY AUTOINCREMENT,"
				 "name TEXT NOT NULL,"
				 "type TEXT NOT NULL,"
				 "look_id INTEGER NOT NULL DEFAULT 0,"
				 "z_order INTEGER NOT NULL DEFAULT 0,"
				 "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
				 "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
				 ");")) {
		return false;
	}

	if (!execute("CREATE UNIQUE INDEX IF NOT EXISTS idx_brushes_type_name "
				 "ON brushes(type, name);")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS brush_items ("
				 "id INTEGER PRIMARY KEY AUTOINCREMENT,"
				 "brush_id INTEGER NOT NULL,"
				 "item_id INTEGER NOT NULL,"
				 "chance INTEGER NOT NULL DEFAULT 1 CHECK(chance >= 0),"
				 "sort_order INTEGER NOT NULL DEFAULT 0,"
				 "FOREIGN KEY (brush_id) REFERENCES brushes(id) ON DELETE CASCADE"
				 ");")) {
		return false;
	}

	if (!execute("CREATE INDEX IF NOT EXISTS idx_brush_items_item_id "
				 "ON brush_items(item_id);")) {
		return false;
	}

	if (!execute("CREATE INDEX IF NOT EXISTS idx_brush_items_brush_sort "
				 "ON brush_items(brush_id, sort_order);")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS ground_borders ("
				 "brush_id INTEGER NOT NULL,"
				 "border_id INTEGER NOT NULL,"
				 "align TEXT NOT NULL DEFAULT 'outer',"
				 "to_brush_id INTEGER,"
				 "PRIMARY KEY (brush_id, border_id),"
				 "FOREIGN KEY (brush_id) REFERENCES brushes(id) ON DELETE CASCADE,"
				 "FOREIGN KEY (to_brush_id) REFERENCES brushes(id) ON DELETE SET NULL"
				 ");")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS ground_optional_borders ("
				 "brush_id INTEGER NOT NULL,"
				 "border_id INTEGER NOT NULL,"
				 "PRIMARY KEY (brush_id, border_id),"
				 "FOREIGN KEY (brush_id) REFERENCES brushes(id) ON DELETE CASCADE"
				 ");")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS brush_relationships ("
				 "from_brush_id INTEGER NOT NULL,"
				 "to_brush_id INTEGER NOT NULL,"
				 "relationship_type TEXT NOT NULL,"
				 "PRIMARY KEY (from_brush_id, to_brush_id, relationship_type),"
				 "FOREIGN KEY (from_brush_id) REFERENCES brushes(id) ON DELETE CASCADE,"
				 "FOREIGN KEY (to_brush_id) REFERENCES brushes(id) ON DELETE CASCADE"
				 ");")) {
		return false;
	}

	return execute("CREATE INDEX IF NOT EXISTS idx_brush_relationships_to "
				   "ON brush_relationships(to_brush_id, relationship_type);");
}

bool BrushDatabaseSchemaManager::addColumnIfMissing(const wxString &tableName, const wxString &columnName, const wxString &definition) {
	bool exists = false;
	if (!columnExists(tableName, columnName, exists)) {
		return false;
	}
	if (exists) {
		return true;
	}
	return execute("ALTER TABLE " + tableName + " ADD COLUMN " + definition + ";");
}

bool BrushDatabaseSchemaManager::executeStatements(std::initializer_list<const char*> statements) {
	for (const char* statement : statements) {
		if (!execute(statement)) {
			return false;
		}
	}
	return true;
}

bool BrushDatabaseSchemaManager::addVersion2BrushColumns() {
	struct ColumnDefinition {
		const char* name;
		const char* definition;
	};

	static constexpr std::array<ColumnDefinition, 11> kBrushColumns = { {
		{ "source_file", "source_file TEXT NOT NULL DEFAULT ''" },
		{ "server_look_id", "server_look_id INTEGER NOT NULL DEFAULT 0" },
		{ "draggable", "draggable INTEGER NOT NULL DEFAULT 0 CHECK(draggable IN (0, 1))" },
		{ "on_blocking", "on_blocking INTEGER NOT NULL DEFAULT 0 CHECK(on_blocking IN (0, 1))" },
		{ "on_duplicate", "on_duplicate INTEGER NOT NULL DEFAULT 0 CHECK(on_duplicate IN (0, 1))" },
		{ "redo_borders", "redo_borders INTEGER NOT NULL DEFAULT 0 CHECK(redo_borders IN (0, 1))" },
		{ "randomize", "randomize INTEGER NOT NULL DEFAULT 0 CHECK(randomize IN (0, 1))" },
		{ "one_size", "one_size INTEGER NOT NULL DEFAULT 0 CHECK(one_size IN (0, 1))" },
		{ "solo_optional", "solo_optional INTEGER NOT NULL DEFAULT 0 CHECK(solo_optional IN (0, 1))" },
		{ "thickness", "thickness INTEGER NOT NULL DEFAULT 0" },
		{ "thickness_ceiling", "thickness_ceiling INTEGER NOT NULL DEFAULT 0" },
	} };

	for (const ColumnDefinition &column : kBrushColumns) {
		if (!addColumnIfMissing("brushes", column.name, column.definition)) {
			return false;
		}
	}

	return executeStatements({
		"CREATE INDEX IF NOT EXISTS idx_brushes_name ON brushes(name);",
	});
}

bool BrushDatabaseSchemaManager::createVersion2BorderSchema() {
	return executeStatements({
		"CREATE TABLE IF NOT EXISTS border_sets ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"xml_border_id INTEGER UNIQUE,"
		"owner_brush_id INTEGER,"
		"border_scope TEXT NOT NULL DEFAULT 'global',"
		"border_type TEXT NOT NULL DEFAULT 'normal',"
		"border_group INTEGER NOT NULL DEFAULT 0,"
		"ground_equivalent INTEGER NOT NULL DEFAULT 0,"
		"source_file TEXT NOT NULL DEFAULT '',"
		"created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
		"updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
		"FOREIGN KEY (owner_brush_id) REFERENCES brushes(id) ON DELETE CASCADE"
		");",
		"CREATE INDEX IF NOT EXISTS idx_border_sets_owner_scope "
		"ON border_sets(owner_brush_id, border_scope);",
		"CREATE TABLE IF NOT EXISTS border_set_items ("
		"border_set_id INTEGER NOT NULL,"
		"edge TEXT NOT NULL,"
		"item_id INTEGER NOT NULL,"
		"sort_order INTEGER NOT NULL DEFAULT 0,"
		"PRIMARY KEY (border_set_id, edge, item_id),"
		"FOREIGN KEY (border_set_id) REFERENCES border_sets(id) ON DELETE CASCADE"
		");",
		"CREATE INDEX IF NOT EXISTS idx_border_set_items_item "
		"ON border_set_items(item_id);",
		"CREATE TABLE IF NOT EXISTS ground_brush_borders ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"brush_id INTEGER NOT NULL,"
		"border_set_id INTEGER NOT NULL,"
		"border_role TEXT NOT NULL DEFAULT 'normal',"
		"align TEXT NOT NULL DEFAULT 'outer',"
		"target_mode TEXT NOT NULL DEFAULT 'all',"
		"target_brush_id INTEGER,"
		"target_brush_name TEXT NOT NULL DEFAULT '',"
		"super_border INTEGER NOT NULL DEFAULT 0 CHECK(super_border IN (0, 1)),"
		"sort_order INTEGER NOT NULL DEFAULT 0,"
		"FOREIGN KEY (brush_id) REFERENCES brushes(id) ON DELETE CASCADE,"
		"FOREIGN KEY (border_set_id) REFERENCES border_sets(id) ON DELETE CASCADE,"
		"FOREIGN KEY (target_brush_id) REFERENCES brushes(id) ON DELETE SET NULL"
		");",
		"CREATE INDEX IF NOT EXISTS idx_ground_brush_borders_brush "
		"ON ground_brush_borders(brush_id, border_role, align, sort_order);",
		"CREATE INDEX IF NOT EXISTS idx_ground_brush_borders_target "
		"ON ground_brush_borders(target_brush_name, target_mode);",
		"CREATE TABLE IF NOT EXISTS ground_border_cases ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"ground_brush_border_id INTEGER NOT NULL,"
		"sort_order INTEGER NOT NULL DEFAULT 0,"
		"FOREIGN KEY (ground_brush_border_id) REFERENCES ground_brush_borders(id) ON DELETE CASCADE"
		");",
		"CREATE TABLE IF NOT EXISTS ground_border_case_conditions ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"ground_border_case_id INTEGER NOT NULL,"
		"condition_type TEXT NOT NULL,"
		"match_value INTEGER NOT NULL DEFAULT 0,"
		"edge TEXT NOT NULL DEFAULT '',"
		"sort_order INTEGER NOT NULL DEFAULT 0,"
		"FOREIGN KEY (ground_border_case_id) REFERENCES ground_border_cases(id) ON DELETE CASCADE"
		");",
		"CREATE TABLE IF NOT EXISTS ground_border_case_actions ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"ground_border_case_id INTEGER NOT NULL,"
		"action_type TEXT NOT NULL,"
		"target_value INTEGER NOT NULL DEFAULT 0,"
		"edge TEXT NOT NULL DEFAULT '',"
		"replacement_value INTEGER NOT NULL DEFAULT 0,"
		"sort_order INTEGER NOT NULL DEFAULT 0,"
		"FOREIGN KEY (ground_border_case_id) REFERENCES ground_border_cases(id) ON DELETE CASCADE"
		");",
	});
}

bool BrushDatabaseSchemaManager::createVersion2BrushDetailSchema() {
	return executeStatements({
		"CREATE TABLE IF NOT EXISTS brush_links ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"brush_id INTEGER NOT NULL,"
		"target_brush_id INTEGER,"
		"target_brush_name TEXT NOT NULL DEFAULT '',"
		"relation_type TEXT NOT NULL,"
		"sort_order INTEGER NOT NULL DEFAULT 0,"
		"FOREIGN KEY (brush_id) REFERENCES brushes(id) ON DELETE CASCADE,"
		"FOREIGN KEY (target_brush_id) REFERENCES brushes(id) ON DELETE SET NULL"
		");",
		"CREATE INDEX IF NOT EXISTS idx_brush_links_type "
		"ON brush_links(brush_id, relation_type, sort_order);",
		"CREATE INDEX IF NOT EXISTS idx_brush_links_target_name "
		"ON brush_links(target_brush_name);",
		"CREATE TABLE IF NOT EXISTS wall_parts ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"brush_id INTEGER NOT NULL,"
		"part_type TEXT NOT NULL,"
		"sort_order INTEGER NOT NULL DEFAULT 0,"
		"FOREIGN KEY (brush_id) REFERENCES brushes(id) ON DELETE CASCADE,"
		"UNIQUE (brush_id, part_type)"
		");",
		"CREATE TABLE IF NOT EXISTS wall_part_items ("
		"wall_part_id INTEGER NOT NULL,"
		"item_id INTEGER NOT NULL,"
		"chance INTEGER NOT NULL DEFAULT 1 CHECK(chance >= 0),"
		"sort_order INTEGER NOT NULL DEFAULT 0,"
		"PRIMARY KEY (wall_part_id, item_id),"
		"FOREIGN KEY (wall_part_id) REFERENCES wall_parts(id) ON DELETE CASCADE"
		");",
		"CREATE TABLE IF NOT EXISTS wall_part_doors ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"wall_part_id INTEGER NOT NULL,"
		"item_id INTEGER NOT NULL,"
		"door_type TEXT NOT NULL DEFAULT 'normal',"
		"is_open INTEGER NOT NULL DEFAULT 0 CHECK(is_open IN (0, 1)),"
		"wall_hate_me INTEGER NOT NULL DEFAULT 0 CHECK(wall_hate_me IN (0, 1)),"
		"sort_order INTEGER NOT NULL DEFAULT 0,"
		"FOREIGN KEY (wall_part_id) REFERENCES wall_parts(id) ON DELETE CASCADE"
		");",
		"CREATE INDEX IF NOT EXISTS idx_wall_part_doors_part "
		"ON wall_part_doors(wall_part_id, sort_order);",
		"CREATE TABLE IF NOT EXISTS carpet_nodes ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"brush_id INTEGER NOT NULL,"
		"align TEXT NOT NULL,"
		"sort_order INTEGER NOT NULL DEFAULT 0,"
		"FOREIGN KEY (brush_id) REFERENCES brushes(id) ON DELETE CASCADE"
		");",
		"CREATE TABLE IF NOT EXISTS carpet_node_items ("
		"carpet_node_id INTEGER NOT NULL,"
		"item_id INTEGER NOT NULL,"
		"chance INTEGER NOT NULL DEFAULT 1 CHECK(chance >= 0),"
		"sort_order INTEGER NOT NULL DEFAULT 0,"
		"PRIMARY KEY (carpet_node_id, item_id),"
		"FOREIGN KEY (carpet_node_id) REFERENCES carpet_nodes(id) ON DELETE CASCADE"
		");",
		"CREATE TABLE IF NOT EXISTS table_nodes ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"brush_id INTEGER NOT NULL,"
		"align TEXT NOT NULL,"
		"sort_order INTEGER NOT NULL DEFAULT 0,"
		"FOREIGN KEY (brush_id) REFERENCES brushes(id) ON DELETE CASCADE"
		");",
		"CREATE TABLE IF NOT EXISTS table_node_items ("
		"table_node_id INTEGER NOT NULL,"
		"item_id INTEGER NOT NULL,"
		"chance INTEGER NOT NULL DEFAULT 1 CHECK(chance >= 0),"
		"sort_order INTEGER NOT NULL DEFAULT 0,"
		"PRIMARY KEY (table_node_id, item_id),"
		"FOREIGN KEY (table_node_id) REFERENCES table_nodes(id) ON DELETE CASCADE"
		");",
		"CREATE TABLE IF NOT EXISTS doodad_alternatives ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"brush_id INTEGER NOT NULL,"
		"sort_order INTEGER NOT NULL DEFAULT 0,"
		"FOREIGN KEY (brush_id) REFERENCES brushes(id) ON DELETE CASCADE"
		");",
		"CREATE TABLE IF NOT EXISTS doodad_single_items ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"doodad_alternative_id INTEGER NOT NULL,"
		"item_id INTEGER NOT NULL,"
		"chance INTEGER NOT NULL DEFAULT 1 CHECK(chance >= 0),"
		"sort_order INTEGER NOT NULL DEFAULT 0,"
		"FOREIGN KEY (doodad_alternative_id) REFERENCES doodad_alternatives(id) ON DELETE CASCADE"
		");",
		"CREATE TABLE IF NOT EXISTS doodad_composites ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"doodad_alternative_id INTEGER NOT NULL,"
		"chance INTEGER NOT NULL DEFAULT 1 CHECK(chance >= 0),"
		"sort_order INTEGER NOT NULL DEFAULT 0,"
		"FOREIGN KEY (doodad_alternative_id) REFERENCES doodad_alternatives(id) ON DELETE CASCADE"
		");",
		"CREATE TABLE IF NOT EXISTS doodad_composite_tiles ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"doodad_composite_id INTEGER NOT NULL,"
		"offset_x INTEGER NOT NULL DEFAULT 0,"
		"offset_y INTEGER NOT NULL DEFAULT 0,"
		"offset_z INTEGER NOT NULL DEFAULT 0,"
		"sort_order INTEGER NOT NULL DEFAULT 0,"
		"FOREIGN KEY (doodad_composite_id) REFERENCES doodad_composites(id) ON DELETE CASCADE"
		");",
		"CREATE TABLE IF NOT EXISTS doodad_composite_tile_items ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"doodad_composite_tile_id INTEGER NOT NULL,"
		"item_id INTEGER NOT NULL,"
		"sort_order INTEGER NOT NULL DEFAULT 0,"
		"FOREIGN KEY (doodad_composite_tile_id) REFERENCES doodad_composite_tiles(id) ON DELETE CASCADE"
		");",
	});
}

bool BrushDatabaseSchemaManager::createVersion2TilesetSchema() {
	return executeStatements({
		"CREATE TABLE IF NOT EXISTS tilesets ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"name TEXT NOT NULL UNIQUE,"
		"source_file TEXT NOT NULL DEFAULT '',"
		"created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
		"updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
		");",
		"CREATE TABLE IF NOT EXISTS tileset_sections ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"tileset_id INTEGER NOT NULL,"
		"section_type TEXT NOT NULL,"
		"sort_order INTEGER NOT NULL DEFAULT 0,"
		"FOREIGN KEY (tileset_id) REFERENCES tilesets(id) ON DELETE CASCADE,"
		"UNIQUE (tileset_id, section_type)"
		");",
		"CREATE TABLE IF NOT EXISTS tileset_brush_entries ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"tileset_section_id INTEGER NOT NULL,"
		"brush_id INTEGER,"
		"brush_name TEXT NOT NULL DEFAULT '',"
		"after_brush_name TEXT NOT NULL DEFAULT '',"
		"sort_order INTEGER NOT NULL DEFAULT 0,"
		"FOREIGN KEY (tileset_section_id) REFERENCES tileset_sections(id) ON DELETE CASCADE,"
		"FOREIGN KEY (brush_id) REFERENCES brushes(id) ON DELETE SET NULL"
		");",
		"CREATE INDEX IF NOT EXISTS idx_tileset_brush_entries_section "
		"ON tileset_brush_entries(tileset_section_id, sort_order);",
		"CREATE INDEX IF NOT EXISTS idx_tileset_brush_entries_name "
		"ON tileset_brush_entries(brush_name);",
	});
}

bool BrushDatabaseSchemaManager::migrateToVersion2() {
	return addVersion2BrushColumns() && createVersion2BorderSchema() && createVersion2BrushDetailSchema() && createVersion2TilesetSchema();
}

template <auto Migration>
bool BrushDatabaseSchemaManager::applySchemaMigrationStep(int &version, int targetVersion) {
	if (version >= targetVersion) {
		return true;
	}
	if (!(this->*Migration)()) {
		return false;
	}
	if (!setSchemaVersion(targetVersion)) {
		return false;
	}

	version = targetVersion;
	return true;
}

bool BrushDatabaseSchemaManager::initializeSchema() {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}
	if (readOnly_) {
		return setError("SQLite schema initialization requires a read-write connection.");
	}

	if (!beginTransaction()) {
		return false;
	}

	const auto rollback = [this]() {
		rollbackTransaction();
		return false;
	};

	if (!ensureSchemaVersionTable()) {
		return rollback();
	}

	int version = 0;
	if (!getCurrentSchemaVersion(version)) {
		return rollback();
	}
	if (version > kBrushDatabaseSchemaVersion) {
		rollbackTransaction();
		return setError(wxString::Format("SQLite schema version %d is newer than supported version %d.", version, kBrushDatabaseSchemaVersion));
	}
	if (!applySchemaMigrationStep<&BrushDatabaseSchemaManager::migrateToVersion1>(version, 1) || !applySchemaMigrationStep<&BrushDatabaseSchemaManager::migrateToVersion2>(version, 2) || !applySchemaMigrationStep<&BrushDatabaseSchemaManager::migrateToVersion3>(version, 3) || !applySchemaMigrationStep<&BrushDatabaseSchemaManager::migrateToVersion4>(version, 4) || !applySchemaMigrationStep<&BrushDatabaseSchemaManager::migrateToVersion5>(version, 5) || !applySchemaMigrationStep<&BrushDatabaseSchemaManager::migrateToVersion6>(version, 6) || !applySchemaMigrationStep<&BrushDatabaseSchemaManager::migrateToVersion7>(version, 7) || !applySchemaMigrationStep<&BrushDatabaseSchemaManager::migrateToVersion8>(version, 8) || !applySchemaMigrationStep<&BrushDatabaseSchemaManager::migrateToVersion9>(version, 9) || !applySchemaMigrationStep<&BrushDatabaseSchemaManager::migrateToVersion10>(version, 10) || !applySchemaMigrationStep<&BrushDatabaseSchemaManager::migrateToVersion11>(version, 11) || !applySchemaMigrationStep<&BrushDatabaseSchemaManager::migrateToVersion12>(version, 12)) {
		return rollback();
	}

	if (!commitTransaction()) {
		return rollback();
	}

	spdlog::info("SQLite brush database schema initialized at version {}", version);
	return true;
}

bool BrushDatabaseSchemaManager::migrateToVersion3() {
	const wxString recreateSql = "DROP TABLE IF EXISTS brush_items;"
								 "DROP TABLE IF EXISTS wall_part_items;"
								 "DROP TABLE IF EXISTS carpet_node_items;"
								 "DROP TABLE IF EXISTS table_node_items;"
								 "DROP TABLE IF EXISTS doodad_single_items;"
								 "DROP TABLE IF EXISTS doodad_composites;"
								 "CREATE TABLE brush_items ("
								 "brush_id INTEGER NOT NULL,"
								 "item_id INTEGER NOT NULL,"
								 "chance INTEGER NOT NULL DEFAULT 1 CHECK(chance >= 0),"
								 "PRIMARY KEY (brush_id, item_id),"
								 "FOREIGN KEY (brush_id) REFERENCES brushes(id) ON DELETE CASCADE"
								 ");"
								 "CREATE INDEX idx_brush_items_item_id ON brush_items(item_id);"
								 "CREATE TABLE wall_part_items ("
								 "id INTEGER PRIMARY KEY AUTOINCREMENT,"
								 "wall_part_id INTEGER NOT NULL,"
								 "item_id INTEGER NOT NULL,"
								 "chance INTEGER NOT NULL DEFAULT 1 CHECK(chance >= 0),"
								 "sort_order INTEGER NOT NULL DEFAULT 0,"
								 "FOREIGN KEY (wall_part_id) REFERENCES wall_parts(id) ON DELETE CASCADE"
								 ");"
								 "CREATE INDEX idx_wall_part_items_item ON wall_part_items(item_id);"
								 "CREATE TABLE carpet_node_items ("
								 "id INTEGER PRIMARY KEY AUTOINCREMENT,"
								 "carpet_node_id INTEGER NOT NULL,"
								 "item_id INTEGER NOT NULL,"
								 "chance INTEGER NOT NULL DEFAULT 1 CHECK(chance >= 0),"
								 "sort_order INTEGER NOT NULL DEFAULT 0,"
								 "FOREIGN KEY (carpet_node_id) REFERENCES carpet_nodes(id) ON DELETE CASCADE"
								 ");"
								 "CREATE TABLE table_node_items ("
								 "id INTEGER PRIMARY KEY AUTOINCREMENT,"
								 "table_node_id INTEGER NOT NULL,"
								 "item_id INTEGER NOT NULL,"
								 "chance INTEGER NOT NULL DEFAULT 1 CHECK(chance >= 0),"
								 "sort_order INTEGER NOT NULL DEFAULT 0,"
								 "FOREIGN KEY (table_node_id) REFERENCES table_nodes(id) ON DELETE CASCADE"
								 ");"
								 "CREATE TABLE doodad_single_items ("
								 "id INTEGER PRIMARY KEY AUTOINCREMENT,"
								 "doodad_alternative_id INTEGER NOT NULL,"
								 "item_id INTEGER NOT NULL,"
								 "chance INTEGER NOT NULL DEFAULT 1 CHECK(chance >= 0),"
								 "sort_order INTEGER NOT NULL DEFAULT 0,"
								 "FOREIGN KEY (doodad_alternative_id) REFERENCES doodad_alternatives(id) ON DELETE CASCADE"
								 ");"
								 "CREATE TABLE doodad_composites ("
								 "id INTEGER PRIMARY KEY AUTOINCREMENT,"
								 "doodad_alternative_id INTEGER NOT NULL,"
								 "chance INTEGER NOT NULL DEFAULT 1 CHECK(chance >= 0),"
								 "sort_order INTEGER NOT NULL DEFAULT 0,"
								 "FOREIGN KEY (doodad_alternative_id) REFERENCES doodad_alternatives(id) ON DELETE CASCADE"
								 ");";

	if (!execute(recreateSql)) {
		return false;
	}
	return true;
}

bool BrushDatabaseSchemaManager::migrateToVersion4() {
	const wxString recreateSql = "DROP TABLE IF EXISTS brush_items;"
								 "CREATE TABLE brush_items ("
								 "id INTEGER PRIMARY KEY AUTOINCREMENT,"
								 "brush_id INTEGER NOT NULL,"
								 "item_id INTEGER NOT NULL,"
								 "chance INTEGER NOT NULL DEFAULT 1 CHECK(chance >= 0),"
								 "sort_order INTEGER NOT NULL DEFAULT 0,"
								 "FOREIGN KEY (brush_id) REFERENCES brushes(id) ON DELETE CASCADE"
								 ");"
								 "CREATE INDEX idx_brush_items_item_id ON brush_items(item_id);"
								 "CREATE INDEX idx_brush_items_brush_sort ON brush_items(brush_id, sort_order);";
	return execute(recreateSql);
}

bool BrushDatabaseSchemaManager::migrateToVersion5() {
	return execute(kRecreateTilesetTablesSql);
}

bool BrushDatabaseSchemaManager::migrateToVersion6() {
	return execute(kRecreateTilesetTablesSql);
}

bool BrushDatabaseSchemaManager::migrateToVersion7() {
	return execute(kRecreateTilesetTablesSql);
}

bool BrushDatabaseSchemaManager::migrateToVersion8() {
	return executeStatements({
		"CREATE TABLE IF NOT EXISTS palette_groups ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"name TEXT NOT NULL COLLATE NOCASE UNIQUE,"
		"sort_order INTEGER NOT NULL DEFAULT 0,"
		"is_builtin INTEGER NOT NULL DEFAULT 0 CHECK(is_builtin IN (0, 1)),"
		"created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
		"updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
		");",
		"INSERT OR IGNORE INTO palette_groups(name, sort_order, is_builtin) VALUES ('terrain', 0, 1);",
		"INSERT OR IGNORE INTO palette_groups(name, sort_order, is_builtin) VALUES ('doodad', 1, 1);",
		"INSERT OR IGNORE INTO palette_groups(name, sort_order, is_builtin) VALUES ('item', 2, 1);",
		"INSERT OR IGNORE INTO palette_groups(name, sort_order, is_builtin) VALUES ('other', 3, 1);",
		"ALTER TABLE tilesets ADD COLUMN palette_group_id INTEGER REFERENCES palette_groups(id) ON DELETE SET NULL;",
		"CREATE INDEX IF NOT EXISTS idx_tilesets_palette_group ON tilesets(palette_group_id, name);",
		"UPDATE tilesets "
		"SET palette_group_id = ("
		"SELECT pg.id FROM palette_groups AS pg "
		"WHERE pg.name = CASE "
		"WHEN lower(coalesce((SELECT section_type FROM tileset_sections WHERE tileset_id = tilesets.id ORDER BY sort_order ASC, id ASC LIMIT 1), '')) IN ('terrain', 'terrain_and_raw') THEN 'terrain' "
		"WHEN lower(coalesce((SELECT section_type FROM tileset_sections WHERE tileset_id = tilesets.id ORDER BY sort_order ASC, id ASC LIMIT 1), '')) IN ('doodad', 'doodad_and_raw') THEN 'doodad' "
		"WHEN lower(coalesce((SELECT section_type FROM tileset_sections WHERE tileset_id = tilesets.id ORDER BY sort_order ASC, id ASC LIMIT 1), '')) IN ('item', 'items', 'items_and_raw') THEN 'item' "
		"ELSE 'other' END "
		"LIMIT 1"
		");",
	});
}

bool BrushDatabaseSchemaManager::migrateToVersion9() {
	return executeStatements({
		"ALTER TABLE palette_groups ADD COLUMN runtime_family TEXT NOT NULL DEFAULT 'raw';",
		"UPDATE palette_groups SET runtime_family = CASE lower(name) "
		"WHEN 'terrain' THEN 'terrain' "
		"WHEN 'doodad' THEN 'doodad' "
		"WHEN 'item' THEN 'item' "
		"WHEN 'other' THEN 'raw' "
		"ELSE 'raw' END;",
		"UPDATE palette_groups "
		"SET runtime_family = COALESCE(("
		"SELECT CASE "
		"WHEN lower(coalesce(ts_first.section_type, '')) IN ('terrain', 'terrain_and_raw') THEN 'terrain' "
		"WHEN lower(coalesce(ts_first.section_type, '')) IN ('doodad', 'doodad_and_raw') THEN 'doodad' "
		"WHEN lower(coalesce(ts_first.section_type, '')) IN ('item', 'items', 'items_and_raw') THEN 'item' "
		"ELSE 'raw' END "
		"FROM tilesets AS t "
		"LEFT JOIN ("
		"SELECT s.tileset_id, s.section_type "
		"FROM tileset_sections AS s "
		"WHERE s.id IN ("
		"SELECT s2.id FROM tileset_sections AS s2 "
		"WHERE s2.tileset_id = s.tileset_id "
		"ORDER BY s2.sort_order ASC, s2.id ASC LIMIT 1"
		")"
		") AS ts_first ON ts_first.tileset_id = t.id "
		"WHERE t.palette_group_id = palette_groups.id "
		"LIMIT 1"
		"), runtime_family);",
	});
}

bool BrushDatabaseSchemaManager::migrateToVersion10() {
	return true;
}

bool BrushDatabaseSchemaManager::migrateToVersion11() {
	return addColumnIfMissing("brushes", "remove_optional_border", "remove_optional_border INTEGER NOT NULL DEFAULT 0 CHECK(remove_optional_border IN (0, 1))");
}

bool BrushDatabaseSchemaManager::migrateToVersion12() {
	return executeStatements({
		"CREATE TABLE IF NOT EXISTS import_status ("
		"id INTEGER PRIMARY KEY CHECK(id = 1),"
		"completed INTEGER NOT NULL DEFAULT 0 CHECK(completed IN (0, 1)),"
		"completed_at INTEGER,"
		"source TEXT NOT NULL DEFAULT ''"
		");",
		"INSERT OR IGNORE INTO import_status(id, completed) VALUES (1, 0);",
	});
}

bool BrushDatabaseSession::testDatabaseConnection() {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("SELECT 1;", &stmt)) {
		return false;
	}

	const int rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (rc != SQLITE_ROW) {
		return setErrorFromDatabase("SQLite connection test failed");
	}

	return true;
}

bool BrushDatabaseSession::quickCheck(int maxErrors) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}
	if (maxErrors <= 0) {
		maxErrors = 1;
	}

	sqlite3_stmt* stmt = nullptr;
	const wxString sql = wxString::Format("PRAGMA quick_check(%d);", maxErrors);
	if (!prepare(sql.utf8_str(), &stmt)) {
		return false;
	}

	wxString firstIssue;
	for (;;) {
		const int rc = sqlite3_step(stmt);
		if (rc == SQLITE_DONE) {
			break;
		}
		if (rc != SQLITE_ROW) {
			sqlite3_finalize(stmt);
			return setErrorFromDatabase("SQLite quick_check failed");
		}

		const wxString value = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
		if (value == "ok") {
			sqlite3_finalize(stmt);
			return true;
		}
		if (firstIssue.IsEmpty()) {
			firstIssue = value;
		}
	}

	sqlite3_finalize(stmt);
	if (firstIssue.IsEmpty()) {
		return setError("SQLite quick_check returned no result.");
	}
	lastError_ = "SQLite quick_check failed: " + firstIssue;
	lastSqliteErrorCode_ = SQLITE_CORRUPT;
	lastSqliteExtendedErrorCode_ = SQLITE_CORRUPT;
	spdlog::error("[BrushDatabase] {}", lastError_.ToStdString());
	return false;
}

bool BrushDatabaseBrushRepository::testBasicCRUD() {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	BrushRecord brush;
	brush.name = "__sqlite_smoke_test__";
	brush.type = "ground";
	brush.lookId = 100;
	brush.zOrder = 3;

	int64_t insertedId = 0;
	if (!insertBrush(brush, insertedId)) {
		return false;
	}

	std::vector<BrushItemRecord> items = {
		{ insertedId, 100, 100 },
		{ insertedId, 101, 50 },
	};
	if (!replaceBrushItems(insertedId, items)) {
		deleteBrush(insertedId);
		return false;
	}

	BrushRecord loadedBrush;
	if (!getBrushById(insertedId, loadedBrush)) {
		deleteBrush(insertedId);
		return false;
	}

	loadedBrush.name = "__sqlite_smoke_test_updated__";
	if (!updateBrush(loadedBrush)) {
		deleteBrush(insertedId);
		return false;
	}

	std::vector<BrushItemRecord> loadedItems;
	if (!getBrushItems(insertedId, loadedItems)) {
		deleteBrush(insertedId);
		return false;
	}

	if (loadedItems.size() != 2) {
		deleteBrush(insertedId);
		return setError("SQLite smoke test failed: unexpected brush item count.");
	}

	if (!deleteBrush(insertedId)) {
		return false;
	}

	return true;
}

bool BrushDatabaseBrushRepository::insertBrush(const BrushRecord &brush, int64_t &insertedId) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("INSERT INTO brushes("
				 "name, type, look_id, z_order, source_file, server_look_id, "
				 "draggable, on_blocking, on_duplicate, redo_borders, randomize, "
				 "one_size, solo_optional, thickness, thickness_ceiling, remove_optional_border, updated_at"
				 ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP);",
				 &stmt)) {
		return false;
	}

	BindBrushRecordFields(stmt, brush, 1);

	const int rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		return setErrorFromDatabase("Failed to insert brush");
	}

	insertedId = sqlite3_last_insert_rowid(connection_);
	return true;
}

bool BrushDatabaseBrushRepository::upsertBrush(const BrushRecord &brush, int64_t &brushId) {
	BrushRecord existingBrush;
	if (findBrushByNameAndType(brush.name, brush.type, existingBrush)) {
		BrushRecord updatedBrush = brush;
		updatedBrush.id = existingBrush.id;
		if (!updateBrush(updatedBrush)) {
			return false;
		}
		brushId = existingBrush.id;
		return true;
	}

	return insertBrush(brush, brushId);
}

bool BrushDatabaseBrushRepository::getBrushById(int64_t brushId, BrushRecord &outBrush) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare(("SELECT " + wxString::FromUTF8(kBrushSelectColumns) + " FROM brushes WHERE id = ?;").utf8_str(), &stmt)) {
		return false;
	}

	sqlite3_bind_int64(stmt, 1, brushId);

	const int rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW) {
		sqlite3_finalize(stmt);
		return setError(wxString::Format("Brush %lld was not found in SQLite.", static_cast<long long>(brushId)));
	}

	ReadBrushRecordFromStatement(stmt, outBrush);

	sqlite3_finalize(stmt);
	return true;
}

bool BrushDatabaseBrushRepository::listBrushesByType(const wxString &type, std::vector<BrushRecord> &outBrushes) {
	outBrushes.clear();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare(("SELECT " + wxString::FromUTF8(kBrushSelectColumns) + " FROM brushes WHERE type = ? ORDER BY name ASC, id ASC;").utf8_str(), &stmt)) {
		return false;
	}

	sqlite3_bind_text(stmt, 1, type.utf8_str(), -1, SQLITE_TRANSIENT);

	for (;;) {
		const int rc = sqlite3_step(stmt);
		if (rc == SQLITE_DONE) {
			break;
		}
		if (rc != SQLITE_ROW) {
			sqlite3_finalize(stmt);
			return setErrorFromDatabase("Failed to list brushes by type");
		}

		BrushRecord brush;
		ReadBrushRecordFromStatement(stmt, brush);
		outBrushes.push_back(brush);
	}

	sqlite3_finalize(stmt);
	return true;
}

bool BrushDatabaseBrushRepository::findBrushByNameAndType(const wxString &name, const wxString &type, BrushRecord &outBrush) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare(("SELECT " + wxString::FromUTF8(kBrushSelectColumns) + " FROM brushes WHERE name = ? COLLATE NOCASE AND type = ? LIMIT 1;").utf8_str(), &stmt)) {
		return false;
	}

	sqlite3_bind_text(stmt, 1, name.utf8_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, type.utf8_str(), -1, SQLITE_TRANSIENT);

	const int rc = sqlite3_step(stmt);
	if (rc == SQLITE_DONE) {
		sqlite3_finalize(stmt);
		return false;
	}
	if (rc != SQLITE_ROW) {
		sqlite3_finalize(stmt);
		setErrorFromDatabase("Failed to query brush by name and type");
		return false;
	}

	ReadBrushRecordFromStatement(stmt, outBrush);
	sqlite3_finalize(stmt);
	return true;
}

bool BrushDatabaseBrushRepository::getCompleteBrushById(int64_t brushId, BrushStorageRecord &outBrush) {
	outBrush = BrushStorageRecord();

	if (!getBrushById(brushId, outBrush.brush)) {
		return false;
	}
	if (!getBrushItems(brushId, outBrush.items)) {
		return false;
	}
	if (!getGroundBrushBorders(brushId, outBrush.borders)) {
		return false;
	}
	if (!getBrushLinks(brushId, outBrush.links)) {
		return false;
	}
	if (!getWallParts(brushId, outBrush.wallParts)) {
		return false;
	}
	if (!getCarpetNodes(brushId, outBrush.carpetNodes)) {
		return false;
	}
	if (!getTableNodes(brushId, outBrush.tableNodes)) {
		return false;
	}
	return getDoodadAlternatives(brushId, outBrush.doodadAlternatives);
}

bool BrushDatabaseBrushRepository::updateBrush(const BrushRecord &brush) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("UPDATE brushes "
				 "SET name = ?, type = ?, look_id = ?, z_order = ?, source_file = ?, server_look_id = ?, "
				 "draggable = ?, on_blocking = ?, on_duplicate = ?, redo_borders = ?, randomize = ?, "
				 "one_size = ?, solo_optional = ?, thickness = ?, thickness_ceiling = ?, remove_optional_border = ?, updated_at = CURRENT_TIMESTAMP "
				 "WHERE id = ?;",
				 &stmt)) {
		return false;
	}

	sqlite3_bind_int64(stmt, BindBrushRecordFields(stmt, brush, 1), brush.id);

	const int rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		return setErrorFromDatabase("Failed to update brush");
	}

	return sqlite3_changes(connection_) > 0 || setError(wxString::Format("Brush %lld was not updated.", static_cast<long long>(brush.id)));
}

bool BrushDatabaseBrushRepository::updateBrushReferenceNames(int64_t brushId, const wxString &oldName, const wxString &newName) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}
	if (brushId <= 0) {
		return setError("Brush id is invalid.");
	}
	if (oldName == newName) {
		return true;
	}

	const auto executeRenameByIdOrName = [&](const char* sql, const char* failureMessage) -> bool {
		sqlite3_stmt* stmt = nullptr;
		if (!prepare(sql, &stmt)) {
			return false;
		}

		sqlite3_bind_text(stmt, 1, newName.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int64(stmt, 2, brushId);
		sqlite3_bind_text(stmt, 3, oldName.utf8_str(), -1, SQLITE_TRANSIENT);

		const int rc = sqlite3_step(stmt);
		sqlite3_finalize(stmt);
		if (rc != SQLITE_DONE) {
			return setErrorFromDatabase(failureMessage);
		}
		return true;
	};

	const auto executeRenameByNameOnly = [&](const char* sql, const char* failureMessage) -> bool {
		sqlite3_stmt* stmt = nullptr;
		if (!prepare(sql, &stmt)) {
			return false;
		}

		sqlite3_bind_text(stmt, 1, newName.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt, 2, oldName.utf8_str(), -1, SQLITE_TRANSIENT);

		const int rc = sqlite3_step(stmt);
		sqlite3_finalize(stmt);
		if (rc != SQLITE_DONE) {
			return setErrorFromDatabase(failureMessage);
		}
		return true;
	};

	if (!executeRenameByIdOrName(
			"UPDATE tileset_brush_entries "
			"SET brush_name = ? "
			"WHERE brush_id = ? OR brush_name = ?;",
			"Failed to update tileset brush names")) {
		return false;
	}

	if (!executeRenameByNameOnly(
			"UPDATE tileset_brush_entries "
			"SET after_brush_name = ? "
			"WHERE after_brush_name = ?;",
			"Failed to update tileset after-brush names")) {
		return false;
	}

	if (!executeRenameByIdOrName(
			"UPDATE ground_brush_borders "
			"SET target_brush_name = ? "
			"WHERE target_brush_id = ? OR target_brush_name = ?;",
			"Failed to update ground border target brush names")) {
		return false;
	}

	if (!executeRenameByIdOrName(
			"UPDATE brush_links "
			"SET target_brush_name = ? "
			"WHERE target_brush_id = ? OR target_brush_name = ?;",
			"Failed to update brush link target names")) {
		return false;
	}

	return resolveGroundReferenceNames();
}

bool BrushDatabaseBrushRepository::deleteBrushReferences(int64_t brushId, const wxString &brushName) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}
	if (brushId <= 0) {
		return setError("Brush id is invalid.");
	}
	if (brushName.IsEmpty()) {
		return setError("Brush name is invalid.");
	}

	const auto executeDeleteByIdOrName = [&](const char* sql, const char* failureMessage) -> bool {
		sqlite3_stmt* stmt = nullptr;
		if (!prepare(sql, &stmt)) {
			return false;
		}

		sqlite3_bind_int64(stmt, 1, brushId);
		sqlite3_bind_text(stmt, 2, brushName.utf8_str(), -1, SQLITE_TRANSIENT);

		const int rc = sqlite3_step(stmt);
		sqlite3_finalize(stmt);
		if (rc != SQLITE_DONE) {
			return setErrorFromDatabase(failureMessage);
		}
		return true;
	};

	const auto executeDeleteTilesetEntries = [&](const char* sql, const char* failureMessage) -> bool {
		sqlite3_stmt* stmt = nullptr;
		if (!prepare(sql, &stmt)) {
			return false;
		}

		sqlite3_bind_int64(stmt, 1, brushId);
		sqlite3_bind_text(stmt, 2, brushName.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt, 3, brushName.utf8_str(), -1, SQLITE_TRANSIENT);

		const int rc = sqlite3_step(stmt);
		sqlite3_finalize(stmt);
		if (rc != SQLITE_DONE) {
			return setErrorFromDatabase(failureMessage);
		}
		return true;
	};

	if (!executeDeleteTilesetEntries(
			"DELETE FROM tileset_brush_entries "
			"WHERE brush_id = ? OR brush_name = ? OR after_brush_name = ?;",
			"Failed to delete tileset brush entries")) {
		return false;
	}

	if (!executeDeleteByIdOrName(
			"DELETE FROM ground_brush_borders "
			"WHERE target_brush_id = ? OR target_brush_name = ?;",
			"Failed to delete ground border target references")) {
		return false;
	}

	if (!executeDeleteByIdOrName(
			"DELETE FROM brush_links "
			"WHERE target_brush_id = ? OR target_brush_name = ?;",
			"Failed to delete brush link target references")) {
		return false;
	}

	return resolveGroundReferenceNames();
}

bool BrushDatabaseBrushRepository::deleteBrush(int64_t brushId) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("DELETE FROM brushes WHERE id = ?;", &stmt)) {
		return false;
	}

	sqlite3_bind_int64(stmt, 1, brushId);

	const int rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		return setErrorFromDatabase("Failed to delete brush");
	}

	return true;
}

bool BrushDatabaseBrushRepository::deleteBrushesByType(const wxString &type) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("DELETE FROM brushes WHERE type = ?;", &stmt)) {
		return false;
	}

	sqlite3_bind_text(stmt, 1, type.utf8_str(), -1, SQLITE_TRANSIENT);
	const int rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		return setErrorFromDatabase("Failed to delete brushes by type");
	}
	return true;
}

bool BrushDatabaseBrushRepository::replaceBrushItems(int64_t brushId, const std::vector<BrushItemRecord> &items) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	if (!beginTransaction()) {
		return false;
	}

	sqlite3_stmt* deleteStmt = nullptr;
	if (!prepare("DELETE FROM brush_items WHERE brush_id = ?;", &deleteStmt)) {
		rollbackTransaction();
		return false;
	}

	sqlite3_bind_int64(deleteStmt, 1, brushId);
	int rc = sqlite3_step(deleteStmt);
	sqlite3_finalize(deleteStmt);
	if (rc != SQLITE_DONE) {
		rollbackTransaction();
		return setErrorFromDatabase("Failed to clear brush items");
	}

	sqlite3_stmt* insertStmt = nullptr;
	if (!prepare("INSERT INTO brush_items(brush_id, item_id, chance, sort_order) VALUES (?, ?, ?, ?);", &insertStmt)) {
		rollbackTransaction();
		return false;
	}

	int sortOrder = 0;
	for (const BrushItemRecord &item : items) {
		sqlite3_reset(insertStmt);
		sqlite3_clear_bindings(insertStmt);
		sqlite3_bind_int64(insertStmt, 1, brushId);
		sqlite3_bind_int(insertStmt, 2, item.itemId);
		sqlite3_bind_int(insertStmt, 3, item.chance);
		sqlite3_bind_int(insertStmt, 4, item.sortOrder != 0 ? item.sortOrder : sortOrder);

		rc = sqlite3_step(insertStmt);
		if (rc != SQLITE_DONE) {
			sqlite3_finalize(insertStmt);
			rollbackTransaction();
			return setErrorFromDatabase("Failed to insert brush item");
		}
		++sortOrder;
	}

	sqlite3_finalize(insertStmt);

	if (!commitTransaction()) {
		rollbackTransaction();
		return false;
	}

	return true;
}

bool BrushDatabaseBrushRepository::getBrushItems(int64_t brushId, std::vector<BrushItemRecord> &outItems) {
	outItems.clear();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("SELECT brush_id, item_id, chance, sort_order "
				 "FROM brush_items WHERE brush_id = ? ORDER BY sort_order ASC, id ASC;",
				 &stmt)) {
		return false;
	}

	sqlite3_bind_int64(stmt, 1, brushId);

	for (;;) {
		const int rc = sqlite3_step(stmt);
		if (rc == SQLITE_DONE) {
			break;
		}
		if (rc != SQLITE_ROW) {
			sqlite3_finalize(stmt);
			return setErrorFromDatabase("Failed to read brush items");
		}

		BrushItemRecord item;
		item.brushId = sqlite3_column_int64(stmt, 0);
		item.itemId = sqlite3_column_int(stmt, 1);
		item.chance = sqlite3_column_int(stmt, 2);
		item.sortOrder = sqlite3_column_int(stmt, 3);
		outItems.push_back(item);
	}

	sqlite3_finalize(stmt);
	return true;
}

bool BrushDatabaseBrushRepository::upsertBorderSet(const BorderSetRecord &borderSet, int64_t &borderSetId) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	const wxString scope = borderSet.borderScope.Lower();
	if (scope.IsEmpty()) {
		return setError("Border scope is required.");
	}
	if (scope != "global" && scope != "inline") {
		return setError(wxString::Format("Unsupported border scope \"%s\".", borderSet.borderScope));
	}
	if (scope == "global") {
		if (borderSet.xmlBorderId <= 0) {
			return setError("Global border sets require xml_border_id > 0.");
		}
		if (borderSet.ownerBrushId > 0) {
			return setError("Global border sets must not use owner_brush_id.");
		}
	} else {
		if (borderSet.ownerBrushId <= 0) {
			return setError("Inline border sets require owner_brush_id > 0.");
		}
		if (borderSet.xmlBorderId > 0) {
			return setError("Inline border sets must not use xml_border_id.");
		}
		if (borderSet.groundEquivalent <= 0) {
			return setError("Inline border sets require ground_equivalent > 0.");
		}
	}

	if (borderSet.id > 0) {
		if (borderSet.xmlBorderId > 0) {
			BorderSetRecord existing;
			if (findBorderSetByXmlBorderId(borderSet.xmlBorderId, existing) && existing.id != borderSet.id) {
				return setError(wxString::Format(
					"XML border id %d already belongs to border set #%lld.",
					borderSet.xmlBorderId,
					static_cast<long long>(existing.id)
				));
			}
		}

		sqlite3_stmt* updateStmt = nullptr;
		if (!prepare("UPDATE border_sets SET xml_border_id = ?, owner_brush_id = ?, border_scope = ?, border_type = ?, "
					 "border_group = ?, ground_equivalent = ?, source_file = ?, updated_at = CURRENT_TIMESTAMP "
					 "WHERE id = ?;",
					 &updateStmt)) {
			return false;
		}

		if (borderSet.xmlBorderId > 0) {
			sqlite3_bind_int(updateStmt, 1, borderSet.xmlBorderId);
		} else {
			sqlite3_bind_null(updateStmt, 1);
		}
		BindNullableInt64(updateStmt, 2, borderSet.ownerBrushId);
		sqlite3_bind_text(updateStmt, 3, borderSet.borderScope.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(updateStmt, 4, borderSet.borderType.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int(updateStmt, 5, borderSet.borderGroup);
		sqlite3_bind_int(updateStmt, 6, borderSet.groundEquivalent);
		sqlite3_bind_text(updateStmt, 7, borderSet.sourceFile.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int64(updateStmt, 8, borderSet.id);

		const int rc = sqlite3_step(updateStmt);
		sqlite3_finalize(updateStmt);
		if (rc != SQLITE_DONE) {
			return setErrorFromDatabase("Failed to update border set");
		}

		borderSetId = borderSet.id;
		return true;
	}

	if (borderSet.xmlBorderId > 0) {
		BorderSetRecord existing;
		if (findBorderSetByXmlBorderId(borderSet.xmlBorderId, existing)) {
			sqlite3_stmt* updateStmt = nullptr;
			if (!prepare("UPDATE border_sets SET xml_border_id = ?, owner_brush_id = ?, border_scope = ?, border_type = ?, "
						 "border_group = ?, ground_equivalent = ?, source_file = ?, updated_at = CURRENT_TIMESTAMP "
						 "WHERE id = ?;",
						 &updateStmt)) {
				return false;
			}

			sqlite3_bind_int(updateStmt, 1, borderSet.xmlBorderId);
			BindNullableInt64(updateStmt, 2, borderSet.ownerBrushId);
			sqlite3_bind_text(updateStmt, 3, borderSet.borderScope.utf8_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(updateStmt, 4, borderSet.borderType.utf8_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(updateStmt, 5, borderSet.borderGroup);
			sqlite3_bind_int(updateStmt, 6, borderSet.groundEquivalent);
			sqlite3_bind_text(updateStmt, 7, borderSet.sourceFile.utf8_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64(updateStmt, 8, existing.id);

			const int rc = sqlite3_step(updateStmt);
			sqlite3_finalize(updateStmt);
			if (rc != SQLITE_DONE) {
				return setErrorFromDatabase("Failed to update border set");
			}

			borderSetId = existing.id;
			return true;
		}
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("INSERT INTO border_sets(xml_border_id, owner_brush_id, border_scope, border_type, border_group, ground_equivalent, source_file, updated_at) "
				 "VALUES (?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP);",
				 &stmt)) {
		return false;
	}

	if (borderSet.xmlBorderId > 0) {
		sqlite3_bind_int(stmt, 1, borderSet.xmlBorderId);
	} else {
		sqlite3_bind_null(stmt, 1);
	}
	BindNullableInt64(stmt, 2, borderSet.ownerBrushId);
	sqlite3_bind_text(stmt, 3, borderSet.borderScope.utf8_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 4, borderSet.borderType.utf8_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 5, borderSet.borderGroup);
	sqlite3_bind_int(stmt, 6, borderSet.groundEquivalent);
	sqlite3_bind_text(stmt, 7, borderSet.sourceFile.utf8_str(), -1, SQLITE_TRANSIENT);

	const int rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		return setErrorFromDatabase("Failed to insert border set");
	}

	borderSetId = sqlite3_last_insert_rowid(connection_);
	return true;
}

bool BrushDatabaseBrushRepository::getBorderSetById(int64_t borderSetId, BorderSetRecord &outBorderSet) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("SELECT id, xml_border_id, owner_brush_id, border_scope, border_type, border_group, ground_equivalent, source_file "
				 "FROM border_sets WHERE id = ? LIMIT 1;",
				 &stmt)) {
		return false;
	}

	sqlite3_bind_int64(stmt, 1, borderSetId);
	const int rc = sqlite3_step(stmt);
	if (rc == SQLITE_DONE) {
		sqlite3_finalize(stmt);
		return false;
	}
	if (rc != SQLITE_ROW) {
		sqlite3_finalize(stmt);
		return setErrorFromDatabase("Failed to query border set by id");
	}

	outBorderSet.id = sqlite3_column_int64(stmt, 0);
	outBorderSet.xmlBorderId = sqlite3_column_int(stmt, 1);
	outBorderSet.ownerBrushId = ReadNullableInt64(stmt, 2);
	outBorderSet.borderScope = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
	outBorderSet.borderType = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));
	outBorderSet.borderGroup = sqlite3_column_int(stmt, 5);
	outBorderSet.groundEquivalent = sqlite3_column_int(stmt, 6);
	outBorderSet.sourceFile = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7)));
	sqlite3_finalize(stmt);
	return true;
}
bool BrushDatabaseBrushRepository::findBorderSetByXmlBorderId(int xmlBorderId, BorderSetRecord &outBorderSet) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("SELECT id, xml_border_id, owner_brush_id, border_scope, border_type, border_group, ground_equivalent, source_file "
				 "FROM border_sets WHERE xml_border_id = ? LIMIT 1;",
				 &stmt)) {
		return false;
	}

	sqlite3_bind_int(stmt, 1, xmlBorderId);
	const int rc = sqlite3_step(stmt);
	if (rc == SQLITE_DONE) {
		sqlite3_finalize(stmt);
		return false;
	}
	if (rc != SQLITE_ROW) {
		sqlite3_finalize(stmt);
		return setErrorFromDatabase("Failed to query border set by xml id");
	}

	outBorderSet.id = sqlite3_column_int64(stmt, 0);
	outBorderSet.xmlBorderId = sqlite3_column_int(stmt, 1);
	outBorderSet.ownerBrushId = ReadNullableInt64(stmt, 2);
	outBorderSet.borderScope = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
	outBorderSet.borderType = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));
	outBorderSet.borderGroup = sqlite3_column_int(stmt, 5);
	outBorderSet.groundEquivalent = sqlite3_column_int(stmt, 6);
	outBorderSet.sourceFile = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7)));
	sqlite3_finalize(stmt);
	return true;
}

bool BrushDatabaseBrushRepository::listBorderSetsByScope(const wxString &borderScope, std::vector<BorderSetRecord> &outBorderSets) {
	outBorderSets.clear();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("SELECT id, xml_border_id, owner_brush_id, border_scope, border_type, border_group, ground_equivalent, source_file "
				 "FROM border_sets WHERE border_scope = ? ORDER BY id ASC;",
				 &stmt)) {
		return false;
	}

	sqlite3_bind_text(stmt, 1, borderScope.utf8_str(), -1, SQLITE_TRANSIENT);
	for (;;) {
		const int rc = sqlite3_step(stmt);
		if (rc == SQLITE_DONE) {
			break;
		}
		if (rc != SQLITE_ROW) {
			sqlite3_finalize(stmt);
			return setErrorFromDatabase("Failed to list border sets by scope");
		}

		BorderSetRecord borderSet;
		borderSet.id = sqlite3_column_int64(stmt, 0);
		borderSet.xmlBorderId = sqlite3_column_int(stmt, 1);
		borderSet.ownerBrushId = ReadNullableInt64(stmt, 2);
		borderSet.borderScope = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
		borderSet.borderType = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));
		borderSet.borderGroup = sqlite3_column_int(stmt, 5);
		borderSet.groundEquivalent = sqlite3_column_int(stmt, 6);
		borderSet.sourceFile = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7)));
		outBorderSets.push_back(borderSet);
	}

	sqlite3_finalize(stmt);
	return true;
}

bool BrushDatabaseBrushRepository::listBorderSetUsages(int64_t borderSetId, std::vector<BorderSetUsageRecord> &outUsages) {
	outUsages.clear();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare(
			"SELECT b.id, b.name, b.type, b.look_id, b.server_look_id, "
			"COALESCE((SELECT bi.item_id FROM brush_items bi WHERE bi.brush_id = b.id ORDER BY bi.sort_order ASC, bi.id ASC LIMIT 1), 0), "
			"gbb.border_role, gbb.align, gbb.target_mode, gbb.target_brush_id, gbb.target_brush_name, gbb.super_border, gbb.sort_order "
			"FROM ground_brush_borders gbb "
			"JOIN brushes b ON b.id = gbb.brush_id "
			"WHERE gbb.border_set_id = ? "
			"ORDER BY b.name COLLATE NOCASE ASC, gbb.sort_order ASC, gbb.id ASC;",
			&stmt)) {
		return false;
	}

	sqlite3_bind_int64(stmt, 1, borderSetId);

	for (;;) {
		const int rc = sqlite3_step(stmt);
		if (rc == SQLITE_DONE) {
			break;
		}
		if (rc != SQLITE_ROW) {
			sqlite3_finalize(stmt);
			return setErrorFromDatabase("Failed to list border set usages");
		}

		BorderSetUsageRecord usage;
		usage.brushId = sqlite3_column_int64(stmt, 0);
		usage.brushName = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
		usage.brushType = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
		usage.lookId = sqlite3_column_int(stmt, 3);
		usage.serverLookId = sqlite3_column_int(stmt, 4);
		usage.primaryItemId = sqlite3_column_int(stmt, 5);
		usage.borderRole = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)));
		usage.align = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7)));
		usage.targetMode = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)));
		usage.targetBrushId = ReadNullableInt64(stmt, 9);
		usage.targetBrushName = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10)));
		usage.superBorder = sqlite3_column_int(stmt, 11) != 0;
		usage.sortOrder = sqlite3_column_int(stmt, 12);
		outUsages.push_back(std::move(usage));
	}

	sqlite3_finalize(stmt);
	return true;
}

bool BrushDatabaseBrushRepository::deleteBorderSet(int64_t borderSetId) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}
	if (!beginTransaction()) {
		return false;
	}

	sqlite3_stmt* deleteItemsStmt = nullptr;
	if (!prepare("DELETE FROM border_set_items WHERE border_set_id = ?;", &deleteItemsStmt)) {
		rollbackTransaction();
		return false;
	}
	sqlite3_bind_int64(deleteItemsStmt, 1, borderSetId);
	int rc = sqlite3_step(deleteItemsStmt);
	sqlite3_finalize(deleteItemsStmt);
	if (rc != SQLITE_DONE) {
		rollbackTransaction();
		return setErrorFromDatabase("Failed to delete border set items");
	}

	sqlite3_stmt* deleteBorderStmt = nullptr;
	if (!prepare("DELETE FROM border_sets WHERE id = ?;", &deleteBorderStmt)) {
		rollbackTransaction();
		return false;
	}
	sqlite3_bind_int64(deleteBorderStmt, 1, borderSetId);
	rc = sqlite3_step(deleteBorderStmt);
	sqlite3_finalize(deleteBorderStmt);
	if (rc != SQLITE_DONE) {
		rollbackTransaction();
		return setErrorFromDatabase("Failed to delete border set");
	}

	if (!commitTransaction()) {
		rollbackTransaction();
		return false;
	}
	return true;
}
bool BrushDatabaseBrushRepository::replaceBorderSetItems(int64_t borderSetId, const std::vector<BorderSetItemRecord> &items) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
		return setError("SQLite database is not open.");
	}
	if (!beginTransaction()) {
		return false;
	}

	sqlite3_stmt* deleteStmt = nullptr;
	if (!prepare("DELETE FROM border_set_items WHERE border_set_id = ?;", &deleteStmt)) {
		rollbackTransaction();
		return false;
	}
	sqlite3_bind_int64(deleteStmt, 1, borderSetId);
	int rc = sqlite3_step(deleteStmt);
	sqlite3_finalize(deleteStmt);
	if (rc != SQLITE_DONE) {
		rollbackTransaction();
		return setErrorFromDatabase("Failed to clear border set items");
	}

	sqlite3_stmt* insertStmt = nullptr;
	if (!prepare("INSERT INTO border_set_items(border_set_id, edge, item_id, sort_order) VALUES (?, ?, ?, ?);", &insertStmt)) {
		rollbackTransaction();
		return false;
	}

	for (const BorderSetItemRecord &item : items) {
		sqlite3_reset(insertStmt);
		sqlite3_clear_bindings(insertStmt);
		sqlite3_bind_int64(insertStmt, 1, borderSetId);
		sqlite3_bind_text(insertStmt, 2, item.edge.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int(insertStmt, 3, item.itemId);
		sqlite3_bind_int(insertStmt, 4, item.sortOrder);
		rc = sqlite3_step(insertStmt);
		if (rc != SQLITE_DONE) {
			sqlite3_finalize(insertStmt);
			rollbackTransaction();
			return setErrorFromDatabase("Failed to insert border set item");
		}
	}

	sqlite3_finalize(insertStmt);
	if (!commitTransaction()) {
		rollbackTransaction();
		return false;
	}
	return true;
}

bool BrushDatabaseBrushRepository::getBorderSetItems(int64_t borderSetId, std::vector<BorderSetItemRecord> &outItems) {
	outItems.clear();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("SELECT border_set_id, edge, item_id, sort_order "
				 "FROM border_set_items WHERE border_set_id = ? ORDER BY sort_order ASC, edge ASC, item_id ASC;",
				 &stmt)) {
		return false;
	}

	sqlite3_bind_int64(stmt, 1, borderSetId);
	for (;;) {
		const int rc = sqlite3_step(stmt);
		if (rc == SQLITE_DONE) {
			break;
		}
		if (rc != SQLITE_ROW) {
			sqlite3_finalize(stmt);
			return setErrorFromDatabase("Failed to read border set items");
		}

		BorderSetItemRecord item;
		item.borderSetId = sqlite3_column_int64(stmt, 0);
		item.edge = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
		item.itemId = sqlite3_column_int(stmt, 2);
		item.sortOrder = sqlite3_column_int(stmt, 3);
		outItems.push_back(item);
	}

	sqlite3_finalize(stmt);
	return true;
}
bool BrushDatabaseBrushRepository::deleteBorderSetsByScope(const wxString &borderScope) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}
	sqlite3_stmt* stmt = nullptr;
	if (!prepare("DELETE FROM border_sets WHERE border_scope = ?;", &stmt)) {
		return false;
	}
	sqlite3_bind_text(stmt, 1, borderScope.utf8_str(), -1, SQLITE_TRANSIENT);
	const int rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		return setErrorFromDatabase("Failed to delete border sets by scope");
	}
	return true;
}

bool BrushDatabaseBrushRepository::deleteOwnedBorderSetsForBrush(int64_t brushId) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}
	sqlite3_stmt* stmt = nullptr;
	if (!prepare("DELETE FROM border_sets WHERE owner_brush_id = ?;", &stmt)) {
		return false;
	}
	sqlite3_bind_int64(stmt, 1, brushId);
	const int rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		return setErrorFromDatabase("Failed to delete owned border sets for brush");
	}
	return true;
}

bool BrushDatabaseBrushRepository::replaceGroundBrushBorders(int64_t brushId, const std::vector<GroundBrushBorderRecord> &borders) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}
	if (!beginTransaction()) {
		return false;
	}

	sqlite3_stmt* deleteStmt = nullptr;
	if (!prepare("DELETE FROM ground_brush_borders WHERE brush_id = ?;", &deleteStmt)) {
		rollbackTransaction();
		return false;
	}
	sqlite3_bind_int64(deleteStmt, 1, brushId);
	int rc = sqlite3_step(deleteStmt);
	sqlite3_finalize(deleteStmt);
	if (rc != SQLITE_DONE) {
		rollbackTransaction();
		return setErrorFromDatabase("Failed to clear ground brush borders");
	}

	sqlite3_stmt* insertBorderStmt = nullptr;
	if (!prepare("INSERT INTO ground_brush_borders("
				 "brush_id, border_set_id, border_role, align, target_mode, target_brush_id, target_brush_name, super_border, sort_order"
				 ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);",
				 &insertBorderStmt)) {
		rollbackTransaction();
		return false;
	}

	sqlite3_stmt* insertCaseStmt = nullptr;
	if (!prepare("INSERT INTO ground_border_cases(ground_brush_border_id, sort_order) VALUES (?, ?);", &insertCaseStmt)) {
		sqlite3_finalize(insertBorderStmt);
		rollbackTransaction();
		return false;
	}

	sqlite3_stmt* insertConditionStmt = nullptr;
	if (!prepare("INSERT INTO ground_border_case_conditions(ground_border_case_id, condition_type, match_value, edge, sort_order) "
				 "VALUES (?, ?, ?, ?, ?);",
				 &insertConditionStmt)) {
		sqlite3_finalize(insertBorderStmt);
		sqlite3_finalize(insertCaseStmt);
		rollbackTransaction();
		return false;
	}

	sqlite3_stmt* insertActionStmt = nullptr;
	if (!prepare("INSERT INTO ground_border_case_actions(ground_border_case_id, action_type, target_value, edge, replacement_value, sort_order) "
				 "VALUES (?, ?, ?, ?, ?, ?);",
				 &insertActionStmt)) {
		sqlite3_finalize(insertBorderStmt);
		sqlite3_finalize(insertCaseStmt);
		sqlite3_finalize(insertConditionStmt);
		rollbackTransaction();
		return false;
	}

	const auto fail = [this, &insertBorderStmt, &insertCaseStmt, &insertConditionStmt, &insertActionStmt](const wxString &message) {
		FinalizeStatements({ insertBorderStmt, insertCaseStmt, insertConditionStmt, insertActionStmt });
		rollbackTransaction();
		return setErrorFromDatabase(message);
	};

	const auto setDbError = [this](const wxString &message) {
		return setErrorFromDatabase(message);
	};

	for (const GroundBrushBorderRecord &border : borders) {
		sqlite3_reset(insertBorderStmt);
		sqlite3_clear_bindings(insertBorderStmt);
		sqlite3_bind_int64(insertBorderStmt, 1, brushId);
		sqlite3_bind_int64(insertBorderStmt, 2, border.borderSetId);
		sqlite3_bind_text(insertBorderStmt, 3, border.borderRole.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(insertBorderStmt, 4, border.align.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(insertBorderStmt, 5, border.targetMode.utf8_str(), -1, SQLITE_TRANSIENT);
		BindNullableInt64(insertBorderStmt, 6, border.targetBrushId);
		sqlite3_bind_text(insertBorderStmt, 7, border.targetBrushName.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int(insertBorderStmt, 8, border.superBorder ? 1 : 0);
		sqlite3_bind_int(insertBorderStmt, 9, border.sortOrder);
		if (sqlite3_step(insertBorderStmt) != SQLITE_DONE) {
			return fail("Failed to insert ground brush border");
		}

		const int64_t groundBrushBorderId = sqlite3_last_insert_rowid(connection_);
		if (!WriteGroundBorderCases(
				connection_,
				groundBrushBorderId,
				border.cases,
				insertCaseStmt,
				insertConditionStmt,
				insertActionStmt,
				setDbError
			)) {
			return fail(lastError_);
		}
	}

	FinalizeStatements({ insertBorderStmt, insertCaseStmt, insertConditionStmt, insertActionStmt });
	if (!commitTransaction()) {
		rollbackTransaction();
		return false;
	}
	return true;
}

bool BrushDatabaseBrushRepository::getGroundBrushBorders(int64_t brushId, std::vector<GroundBrushBorderRecord> &outBorders) {
	outBorders.clear();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* borderStmt = nullptr;
	if (!prepare("SELECT id, border_set_id, border_role, align, target_mode, target_brush_id, target_brush_name, super_border, sort_order "
				 "FROM ground_brush_borders WHERE brush_id = ? ORDER BY sort_order ASC, id ASC;",
				 &borderStmt)) {
		return false;
	}

	sqlite3_stmt* caseStmt = nullptr;
	if (!prepare("SELECT id, sort_order FROM ground_border_cases "
				 "WHERE ground_brush_border_id = ? ORDER BY sort_order ASC, id ASC;",
				 &caseStmt)) {
		sqlite3_finalize(borderStmt);
		return false;
	}

	sqlite3_stmt* conditionStmt = nullptr;
	if (!prepare("SELECT condition_type, match_value, edge, sort_order "
				 "FROM ground_border_case_conditions WHERE ground_border_case_id = ? "
				 "ORDER BY sort_order ASC, id ASC;",
				 &conditionStmt)) {
		sqlite3_finalize(borderStmt);
		sqlite3_finalize(caseStmt);
		return false;
	}

	sqlite3_stmt* actionStmt = nullptr;
	if (!prepare("SELECT action_type, target_value, edge, replacement_value, sort_order "
				 "FROM ground_border_case_actions WHERE ground_border_case_id = ? "
				 "ORDER BY sort_order ASC, id ASC;",
				 &actionStmt)) {
		sqlite3_finalize(borderStmt);
		sqlite3_finalize(caseStmt);
		sqlite3_finalize(conditionStmt);
		return false;
	}

	sqlite3_bind_int64(borderStmt, 1, brushId);

	const auto fail = [this, &borderStmt, &caseStmt, &conditionStmt, &actionStmt](const wxString &message) {
		FinalizeStatements({ borderStmt, caseStmt, conditionStmt, actionStmt });
		return setErrorFromDatabase(message);
	};

	const auto setDbError = [this](const wxString &message) {
		return setErrorFromDatabase(message);
	};

	for (;;) {
		const int borderRc = sqlite3_step(borderStmt);
		if (borderRc == SQLITE_DONE) {
			break;
		}
		if (borderRc != SQLITE_ROW) {
			return fail("Failed to read ground brush borders");
		}

		const int64_t groundBrushBorderId = sqlite3_column_int64(borderStmt, 0);

		GroundBrushBorderRecord border;
		border.borderSetId = sqlite3_column_int64(borderStmt, 1);
		border.borderRole = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(borderStmt, 2)));
		border.align = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(borderStmt, 3)));
		border.targetMode = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(borderStmt, 4)));
		border.targetBrushId = ReadNullableInt64(borderStmt, 5);
		border.targetBrushName = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(borderStmt, 6)));
		border.superBorder = sqlite3_column_int(borderStmt, 7) != 0;
		border.sortOrder = sqlite3_column_int(borderStmt, 8);

		sqlite3_reset(caseStmt);
		sqlite3_clear_bindings(caseStmt);
		sqlite3_bind_int64(caseStmt, 1, groundBrushBorderId);
		if (!ReadGroundBorderCases(caseStmt, conditionStmt, actionStmt, border.cases, setDbError)) {
			return fail(lastError_);
		}

		outBorders.push_back(border);
	}

	FinalizeStatements({ borderStmt, caseStmt, conditionStmt, actionStmt });
	return true;
}

bool BrushDatabaseBrushRepository::replaceBrushLinks(int64_t brushId, const std::vector<BrushLinkRecord> &links) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}
	for (const BrushLinkRecord &link : links) {
		wxString relation = link.relationType;
		relation.MakeLower();
		if (relation.IsEmpty()) {
			return setError("Brush link relation type cannot be empty.");
		}
		if (relation != "friend" && relation != "enemy" && relation != "redirect") {
			return setError("Unsupported brush link relation type: " + link.relationType);
		}

		wxString targetName = link.targetBrushName;
		targetName.Trim(true);
		targetName.Trim(false);
		if (link.targetBrushId <= 0 && targetName.IsEmpty()) {
			return setError("Brush link must define a target brush id or target brush name.");
		}
		if (relation == "redirect" && targetName.IsSameAs("all", false)) {
			return setError("Redirect brush links must not target \"all\".");
		}
		if (link.sortOrder < 0) {
			return setError("Brush link sort order cannot be negative.");
		}
	}
	if (!beginTransaction()) {
		return false;
	}

	sqlite3_stmt* deleteStmt = nullptr;
	if (!prepare("DELETE FROM brush_links WHERE brush_id = ?;", &deleteStmt)) {
		rollbackTransaction();
		return false;
	}
	sqlite3_bind_int64(deleteStmt, 1, brushId);
	int rc = sqlite3_step(deleteStmt);
	sqlite3_finalize(deleteStmt);
	if (rc != SQLITE_DONE) {
		rollbackTransaction();
		return setErrorFromDatabase("Failed to clear brush links");
	}

	sqlite3_stmt* insertStmt = nullptr;
	if (!prepare("INSERT INTO brush_links(brush_id, target_brush_id, target_brush_name, relation_type, sort_order) VALUES (?, ?, ?, ?, ?);", &insertStmt)) {
		rollbackTransaction();
		return false;
	}

	for (const BrushLinkRecord &link : links) {
		sqlite3_reset(insertStmt);
		sqlite3_clear_bindings(insertStmt);
		sqlite3_bind_int64(insertStmt, 1, brushId);
		BindNullableInt64(insertStmt, 2, link.targetBrushId);
		sqlite3_bind_text(insertStmt, 3, link.targetBrushName.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(insertStmt, 4, link.relationType.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int(insertStmt, 5, link.sortOrder);
		rc = sqlite3_step(insertStmt);
		if (rc != SQLITE_DONE) {
			sqlite3_finalize(insertStmt);
			rollbackTransaction();
			return setErrorFromDatabase("Failed to insert brush link");
		}
	}

	sqlite3_finalize(insertStmt);
	if (!commitTransaction()) {
		rollbackTransaction();
		return false;
	}
	return true;
}

bool BrushDatabaseBrushRepository::getBrushLinks(int64_t brushId, std::vector<BrushLinkRecord> &outLinks) {
	outLinks.clear();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("SELECT brush_id, target_brush_id, target_brush_name, relation_type, sort_order "
				 "FROM brush_links WHERE brush_id = ? ORDER BY sort_order ASC, id ASC;",
				 &stmt)) {
		return false;
	}

	sqlite3_bind_int64(stmt, 1, brushId);

	for (;;) {
		const int rc = sqlite3_step(stmt);
		if (rc == SQLITE_DONE) {
			break;
		}
		if (rc != SQLITE_ROW) {
			sqlite3_finalize(stmt);
			return setErrorFromDatabase("Failed to read brush links");
		}

		BrushLinkRecord link;
		link.brushId = sqlite3_column_int64(stmt, 0);
		link.targetBrushId = ReadNullableInt64(stmt, 1);
		link.targetBrushName = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
		link.relationType = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
		link.sortOrder = sqlite3_column_int(stmt, 4);
		outLinks.push_back(link);
	}

	sqlite3_finalize(stmt);
	return true;
}

bool BrushDatabaseBrushRepository::listBrushUsages(int64_t brushId, const wxString &brushName, std::vector<BrushUsageRecord> &outUsages) {
	outUsages.clear();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* paletteStmt = nullptr;
	if (!prepare(
			"SELECT tileset_id, tileset_name, section_type, relation, ref_id, sort_order "
			"FROM ("
			"SELECT t.id AS tileset_id, t.name AS tileset_name, s.section_type AS section_type, 'palette entry' AS relation, "
			"e.id AS ref_id, e.sort_order AS sort_order, s.sort_order AS section_sort "
			"FROM tileset_brush_entries e "
			"JOIN tileset_sections s ON s.id = e.tileset_section_id "
			"JOIN tilesets t ON t.id = s.tileset_id "
			"WHERE e.entry_kind = 'brush' AND (e.brush_id = ? OR (e.brush_id IS NULL AND e.brush_name = ?)) "
			"UNION ALL "
			"SELECT t.id AS tileset_id, t.name AS tileset_name, s.section_type AS section_type, 'palette order' AS relation, "
			"e.id AS ref_id, e.sort_order AS sort_order, s.sort_order AS section_sort "
			"FROM tileset_brush_entries e "
			"JOIN tileset_sections s ON s.id = e.tileset_section_id "
			"JOIN tilesets t ON t.id = s.tileset_id "
			"WHERE e.after_brush_name <> '' AND e.after_brush_name = ?"
			") "
			"ORDER BY tileset_name COLLATE NOCASE ASC, section_sort ASC, sort_order ASC, ref_id ASC;",
			&paletteStmt)) {
		return false;
	}

	sqlite3_bind_int64(paletteStmt, 1, brushId);
	sqlite3_bind_text(paletteStmt, 2, brushName.utf8_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(paletteStmt, 3, brushName.utf8_str(), -1, SQLITE_TRANSIENT);

	for (;;) {
		const int rc = sqlite3_step(paletteStmt);
		if (rc == SQLITE_DONE) {
			break;
		}
		if (rc != SQLITE_ROW) {
			sqlite3_finalize(paletteStmt);
			return setErrorFromDatabase("Failed to list brush palette usages");
		}

		BrushUsageRecord usage;
		usage.sourceKind = "palette";
		usage.sourceId = sqlite3_column_int64(paletteStmt, 0);
		usage.sourceName = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(paletteStmt, 1)));
		usage.context = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(paletteStmt, 2)));
		usage.relation = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(paletteStmt, 3)));
		usage.refId = sqlite3_column_int64(paletteStmt, 4);
		usage.sortOrder = sqlite3_column_int(paletteStmt, 5);
		outUsages.push_back(std::move(usage));
	}

	sqlite3_finalize(paletteStmt);

	sqlite3_stmt* linkStmt = nullptr;
	if (!prepare(
			"SELECT b.id, b.name, bl.relation_type, bl.sort_order, bl.id "
			"FROM brush_links bl "
			"JOIN brushes b ON b.id = bl.brush_id "
			"WHERE bl.target_brush_id = ? OR (bl.target_brush_id IS NULL AND bl.target_brush_name = ?) "
			"ORDER BY b.name COLLATE NOCASE ASC, bl.sort_order ASC, bl.id ASC;",
			&linkStmt)) {
		return false;
	}

	sqlite3_bind_int64(linkStmt, 1, brushId);
	sqlite3_bind_text(linkStmt, 2, brushName.utf8_str(), -1, SQLITE_TRANSIENT);

	for (;;) {
		const int rc = sqlite3_step(linkStmt);
		if (rc == SQLITE_DONE) {
			break;
		}
		if (rc != SQLITE_ROW) {
			sqlite3_finalize(linkStmt);
			return setErrorFromDatabase("Failed to list brush link usages");
		}

		BrushUsageRecord usage;
		usage.sourceKind = "brush";
		usage.sourceId = sqlite3_column_int64(linkStmt, 0);
		usage.sourceName = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(linkStmt, 1)));
		usage.relation = "brush link";
		usage.context = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(linkStmt, 2)));
		usage.sortOrder = sqlite3_column_int(linkStmt, 3);
		usage.refId = sqlite3_column_int64(linkStmt, 4);
		outUsages.push_back(std::move(usage));
	}

	sqlite3_finalize(linkStmt);

	sqlite3_stmt* borderTargetStmt = nullptr;
	if (!prepare(
			"SELECT b.id, b.name, gbb.border_role, gbb.align, gbb.target_mode, gbb.sort_order, gbb.id "
			"FROM ground_brush_borders gbb "
			"JOIN brushes b ON b.id = gbb.brush_id "
			"WHERE gbb.target_brush_id = ? OR (gbb.target_brush_id IS NULL AND gbb.target_brush_name = ?) "
			"ORDER BY b.name COLLATE NOCASE ASC, gbb.sort_order ASC, gbb.id ASC;",
			&borderTargetStmt)) {
		return false;
	}

	sqlite3_bind_int64(borderTargetStmt, 1, brushId);
	sqlite3_bind_text(borderTargetStmt, 2, brushName.utf8_str(), -1, SQLITE_TRANSIENT);

	for (;;) {
		const int rc = sqlite3_step(borderTargetStmt);
		if (rc == SQLITE_DONE) {
			break;
		}
		if (rc != SQLITE_ROW) {
			sqlite3_finalize(borderTargetStmt);
			return setErrorFromDatabase("Failed to list brush border target usages");
		}

		BrushUsageRecord usage;
		usage.sourceKind = "brush";
		usage.sourceId = sqlite3_column_int64(borderTargetStmt, 0);
		usage.sourceName = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(borderTargetStmt, 1)));
		usage.relation = "border target";
		usage.context = wxString::Format(
			"%s %s %s",
			ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(borderTargetStmt, 2))),
			ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(borderTargetStmt, 3))),
			ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(borderTargetStmt, 4)))
		).Trim(true).Trim(false);
		usage.sortOrder = sqlite3_column_int(borderTargetStmt, 5);
		usage.refId = sqlite3_column_int64(borderTargetStmt, 6);
		outUsages.push_back(std::move(usage));
	}

	sqlite3_finalize(borderTargetStmt);
	return true;
}

bool BrushDatabaseBrushRepository::replaceWallParts(int64_t brushId, const std::vector<WallPartRecord> &parts) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}
	for (const WallPartRecord &part : parts) {
		wxString partType = part.partType;
		partType.Trim(true);
		partType.Trim(false);
		if (partType.IsEmpty()) {
			return setError("Wall part type cannot be empty.");
		}
		if (part.sortOrder < 0) {
			return setError("Wall part sort order cannot be negative.");
		}
		for (const WallPartItemRecord &item : part.items) {
			if (item.itemId <= 0) {
				return setError("Wall part item id must be positive.");
			}
			if (item.chance < 0) {
				return setError("Wall part item chance cannot be negative.");
			}
			if (item.sortOrder < 0) {
				return setError("Wall part item sort order cannot be negative.");
			}
		}
		for (const WallPartDoorRecord &door : part.doors) {
			if (door.itemId <= 0) {
				return setError("Wall part door item id must be positive.");
			}
			wxString doorType = door.doorType;
			doorType.Trim(true);
			doorType.Trim(false);
			if (doorType.IsEmpty()) {
				return setError("Wall part door type cannot be empty.");
			}
			wxString doorTypeLower = doorType;
			doorTypeLower.MakeLower();
			const bool supportedDoorType =
				doorTypeLower == "normal" ||
				doorTypeLower == "locked" ||
				doorTypeLower == "quest" ||
				doorTypeLower == "magic" ||
				doorTypeLower == "archway" ||
				doorTypeLower == "window" ||
				doorTypeLower == "hatch window" ||
				doorTypeLower == "hatch_window" ||
				doorTypeLower == "any door" ||
				doorTypeLower == "any window" ||
				doorTypeLower == "any";
			if (!supportedDoorType) {
				return setError("Unsupported wall part door type: " + doorType);
			}
			if (door.sortOrder < 0) {
				return setError("Wall part door sort order cannot be negative.");
			}
		}
	}
	if (!beginTransaction()) {
		return false;
	}

	sqlite3_stmt* deleteStmt = nullptr;
	if (!prepare("DELETE FROM wall_parts WHERE brush_id = ?;", &deleteStmt)) {
		rollbackTransaction();
		return false;
	}
	sqlite3_bind_int64(deleteStmt, 1, brushId);
	int rc = sqlite3_step(deleteStmt);
	sqlite3_finalize(deleteStmt);
	if (rc != SQLITE_DONE) {
		rollbackTransaction();
		return setErrorFromDatabase("Failed to clear wall parts");
	}

	sqlite3_stmt* insertPartStmt = nullptr;
	if (!prepare("INSERT INTO wall_parts(brush_id, part_type, sort_order) VALUES (?, ?, ?);", &insertPartStmt)) {
		rollbackTransaction();
		return false;
	}

	sqlite3_stmt* insertItemStmt = nullptr;
	if (!prepare("INSERT INTO wall_part_items(wall_part_id, item_id, chance, sort_order) VALUES (?, ?, ?, ?);", &insertItemStmt)) {
		sqlite3_finalize(insertPartStmt);
		rollbackTransaction();
		return false;
	}

	sqlite3_stmt* insertDoorStmt = nullptr;
	if (!prepare("INSERT INTO wall_part_doors(wall_part_id, item_id, door_type, is_open, wall_hate_me, sort_order) "
				 "VALUES (?, ?, ?, ?, ?, ?);",
				 &insertDoorStmt)) {
		sqlite3_finalize(insertPartStmt);
		sqlite3_finalize(insertItemStmt);
		rollbackTransaction();
		return false;
	}

	const auto failWithDatabaseError = [this, &insertPartStmt, &insertItemStmt, &insertDoorStmt](const wxString &message) {
		FinalizeStatements({ insertPartStmt, insertItemStmt, insertDoorStmt });
		rollbackTransaction();
		return setErrorFromDatabase(message);
	};

	const auto fail = [this, &insertPartStmt, &insertItemStmt, &insertDoorStmt]() {
		FinalizeStatements({ insertPartStmt, insertItemStmt, insertDoorStmt });
		rollbackTransaction();
		return false;
	};

	const auto setDbError = [this](const wxString &message) {
		return setErrorFromDatabase(message);
	};

	for (const WallPartRecord &part : parts) {
		sqlite3_reset(insertPartStmt);
		sqlite3_clear_bindings(insertPartStmt);
		sqlite3_bind_int64(insertPartStmt, 1, brushId);
		sqlite3_bind_text(insertPartStmt, 2, part.partType.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int(insertPartStmt, 3, part.sortOrder);
		if (sqlite3_step(insertPartStmt) != SQLITE_DONE) {
			return failWithDatabaseError("Failed to insert wall part");
		}

		const int64_t wallPartId = sqlite3_last_insert_rowid(connection_);
		if (!WriteWallPartItems(wallPartId, part.items, insertItemStmt, setDbError)) {
			return fail();
		}
		if (!WriteWallPartDoors(wallPartId, part.doors, insertDoorStmt, setDbError)) {
			return fail();
		}
	}

	FinalizeStatements({ insertPartStmt, insertItemStmt, insertDoorStmt });
	if (!commitTransaction()) {
		rollbackTransaction();
		return false;
	}
	return true;
}

bool BrushDatabaseBrushRepository::getWallParts(int64_t brushId, std::vector<WallPartRecord> &outParts) {
	outParts.clear();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* partStmt = nullptr;
	if (!prepare("SELECT id, part_type, sort_order FROM wall_parts "
				 "WHERE brush_id = ? ORDER BY sort_order ASC, id ASC;",
				 &partStmt)) {
		return false;
	}

	sqlite3_stmt* itemStmt = nullptr;
	if (!prepare("SELECT item_id, chance, sort_order FROM wall_part_items "
				 "WHERE wall_part_id = ? ORDER BY sort_order ASC, id ASC;",
				 &itemStmt)) {
		sqlite3_finalize(partStmt);
		return false;
	}

	sqlite3_stmt* doorStmt = nullptr;
	if (!prepare("SELECT item_id, door_type, is_open, wall_hate_me, sort_order FROM wall_part_doors "
				 "WHERE wall_part_id = ? ORDER BY sort_order ASC, id ASC;",
				 &doorStmt)) {
		sqlite3_finalize(partStmt);
		sqlite3_finalize(itemStmt);
		return false;
	}

	sqlite3_bind_int64(partStmt, 1, brushId);

	for (;;) {
		const int partRc = sqlite3_step(partStmt);
		if (partRc == SQLITE_DONE) {
			break;
		}
		if (partRc != SQLITE_ROW) {
			sqlite3_finalize(partStmt);
			sqlite3_finalize(itemStmt);
			sqlite3_finalize(doorStmt);
			return setErrorFromDatabase("Failed to read wall parts");
		}

		const int64_t wallPartId = sqlite3_column_int64(partStmt, 0);

		WallPartRecord part;
		part.partType = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(partStmt, 1)));
		part.sortOrder = sqlite3_column_int(partStmt, 2);

		sqlite3_reset(itemStmt);
		sqlite3_clear_bindings(itemStmt);
		sqlite3_bind_int64(itemStmt, 1, wallPartId);

		for (;;) {
			const int itemRc = sqlite3_step(itemStmt);
			if (itemRc == SQLITE_DONE) {
				break;
			}
			if (itemRc != SQLITE_ROW) {
				sqlite3_finalize(partStmt);
				sqlite3_finalize(itemStmt);
				sqlite3_finalize(doorStmt);
				return setErrorFromDatabase("Failed to read wall part items");
			}

			WallPartItemRecord item;
			item.itemId = sqlite3_column_int(itemStmt, 0);
			item.chance = sqlite3_column_int(itemStmt, 1);
			item.sortOrder = sqlite3_column_int(itemStmt, 2);
			part.items.push_back(item);
		}

		sqlite3_reset(doorStmt);
		sqlite3_clear_bindings(doorStmt);
		sqlite3_bind_int64(doorStmt, 1, wallPartId);

		for (;;) {
			const int doorRc = sqlite3_step(doorStmt);
			if (doorRc == SQLITE_DONE) {
				break;
			}
			if (doorRc != SQLITE_ROW) {
				sqlite3_finalize(partStmt);
				sqlite3_finalize(itemStmt);
				sqlite3_finalize(doorStmt);
				return setErrorFromDatabase("Failed to read wall part doors");
			}

			WallPartDoorRecord door;
			door.itemId = sqlite3_column_int(doorStmt, 0);
			door.doorType = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(doorStmt, 1)));
			door.isOpen = sqlite3_column_int(doorStmt, 2) != 0;
			door.wallHateMe = sqlite3_column_int(doorStmt, 3) != 0;
			door.sortOrder = sqlite3_column_int(doorStmt, 4);
			part.doors.push_back(door);
		}

		outParts.push_back(part);
	}

	sqlite3_finalize(partStmt);
	sqlite3_finalize(itemStmt);
	sqlite3_finalize(doorStmt);
	return true;
}

bool BrushDatabaseBrushRepository::replaceCarpetNodes(int64_t brushId, const std::vector<CarpetNodeRecord> &nodes) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}
	if (!beginTransaction()) {
		return false;
	}

	sqlite3_stmt* deleteStmt = nullptr;
	if (!prepare("DELETE FROM carpet_nodes WHERE brush_id = ?;", &deleteStmt)) {
		rollbackTransaction();
		return false;
	}
	sqlite3_bind_int64(deleteStmt, 1, brushId);
	int rc = sqlite3_step(deleteStmt);
	sqlite3_finalize(deleteStmt);
	if (rc != SQLITE_DONE) {
		rollbackTransaction();
		return setErrorFromDatabase("Failed to clear carpet nodes");
	}

	sqlite3_stmt* insertNodeStmt = nullptr;
	if (!prepare("INSERT INTO carpet_nodes(brush_id, align, sort_order) VALUES (?, ?, ?);", &insertNodeStmt)) {
		rollbackTransaction();
		return false;
	}

	sqlite3_stmt* insertItemStmt = nullptr;
	if (!prepare("INSERT INTO carpet_node_items(carpet_node_id, item_id, chance, sort_order) VALUES (?, ?, ?, ?);", &insertItemStmt)) {
		sqlite3_finalize(insertNodeStmt);
		rollbackTransaction();
		return false;
	}

	if (!WriteAlignedNodesWithItems<CarpetNodeRecord, CarpetNodeItemRecord>(
			connection_,
			brushId,
			nodes,
			{ insertNodeStmt, insertItemStmt, "Failed to insert carpet node", "Failed to insert carpet node item" },
			[this](const wxString &message) { return setErrorFromDatabase(message); }
		)) {
		FinalizeStatements({ insertNodeStmt, insertItemStmt });
		rollbackTransaction();
		return false;
	}

	FinalizeStatements({ insertNodeStmt, insertItemStmt });
	if (!commitTransaction()) {
		rollbackTransaction();
		return false;
	}
	return true;
}

bool BrushDatabaseBrushRepository::getCarpetNodes(int64_t brushId, std::vector<CarpetNodeRecord> &outNodes) {
	outNodes.clear();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* nodeStmt = nullptr;
	if (!prepare("SELECT id, align, sort_order FROM carpet_nodes "
				 "WHERE brush_id = ? ORDER BY sort_order ASC, id ASC;",
				 &nodeStmt)) {
		return false;
	}

	sqlite3_stmt* itemStmt = nullptr;
	if (!prepare("SELECT item_id, chance, sort_order FROM carpet_node_items "
				 "WHERE carpet_node_id = ? ORDER BY sort_order ASC, id ASC;",
				 &itemStmt)) {
		sqlite3_finalize(nodeStmt);
		return false;
	}

	sqlite3_bind_int64(nodeStmt, 1, brushId);

	if (!ReadAlignedNodesWithItems<CarpetNodeRecord, CarpetNodeItemRecord>(
			nodeStmt,
			itemStmt,
			outNodes,
			"Failed to read carpet nodes",
			"Failed to read carpet node items",
			[this](const wxString &message) { return setErrorFromDatabase(message); }
		)) {
		FinalizeStatements({ nodeStmt, itemStmt });
		return false;
	}

	FinalizeStatements({ nodeStmt, itemStmt });
	return true;
}

bool BrushDatabaseBrushRepository::replaceTableNodes(int64_t brushId, const std::vector<TableNodeRecord> &nodes) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}
	if (!beginTransaction()) {
		return false;
	}

	sqlite3_stmt* deleteStmt = nullptr;
	if (!prepare("DELETE FROM table_nodes WHERE brush_id = ?;", &deleteStmt)) {
		rollbackTransaction();
		return false;
	}
	sqlite3_bind_int64(deleteStmt, 1, brushId);
	int rc = sqlite3_step(deleteStmt);
	sqlite3_finalize(deleteStmt);
	if (rc != SQLITE_DONE) {
		rollbackTransaction();
		return setErrorFromDatabase("Failed to clear table nodes");
	}

	sqlite3_stmt* insertNodeStmt = nullptr;
	if (!prepare("INSERT INTO table_nodes(brush_id, align, sort_order) VALUES (?, ?, ?);", &insertNodeStmt)) {
		rollbackTransaction();
		return false;
	}

	sqlite3_stmt* insertItemStmt = nullptr;
	if (!prepare("INSERT INTO table_node_items(table_node_id, item_id, chance, sort_order) VALUES (?, ?, ?, ?);", &insertItemStmt)) {
		sqlite3_finalize(insertNodeStmt);
		rollbackTransaction();
		return false;
	}

	if (!WriteAlignedNodesWithItems<TableNodeRecord, TableNodeItemRecord>(
			connection_,
			brushId,
			nodes,
			{ insertNodeStmt, insertItemStmt, "Failed to insert table node", "Failed to insert table node item" },
			[this](const wxString &message) { return setErrorFromDatabase(message); }
		)) {
		FinalizeStatements({ insertNodeStmt, insertItemStmt });
		rollbackTransaction();
		return false;
	}

	FinalizeStatements({ insertNodeStmt, insertItemStmt });
	if (!commitTransaction()) {
		rollbackTransaction();
		return false;
	}
	return true;
}

bool BrushDatabaseBrushRepository::getTableNodes(int64_t brushId, std::vector<TableNodeRecord> &outNodes) {
	outNodes.clear();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* nodeStmt = nullptr;
	if (!prepare("SELECT id, align, sort_order FROM table_nodes "
				 "WHERE brush_id = ? ORDER BY sort_order ASC, id ASC;",
				 &nodeStmt)) {
		return false;
	}

	sqlite3_stmt* itemStmt = nullptr;
	if (!prepare("SELECT item_id, chance, sort_order FROM table_node_items "
				 "WHERE table_node_id = ? ORDER BY sort_order ASC, id ASC;",
				 &itemStmt)) {
		sqlite3_finalize(nodeStmt);
		return false;
	}

	sqlite3_bind_int64(nodeStmt, 1, brushId);

	if (!ReadAlignedNodesWithItems<TableNodeRecord, TableNodeItemRecord>(
			nodeStmt,
			itemStmt,
			outNodes,
			"Failed to read table nodes",
			"Failed to read table node items",
			[this](const wxString &message) { return setErrorFromDatabase(message); }
		)) {
		FinalizeStatements({ nodeStmt, itemStmt });
		return false;
	}

	FinalizeStatements({ nodeStmt, itemStmt });
	return true;
}

bool BrushDatabaseBrushRepository::replaceDoodadAlternatives(int64_t brushId, const std::vector<DoodadAlternativeRecord> &alternatives) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}
	if (!beginTransaction()) {
		return false;
	}

	sqlite3_stmt* deleteStmt = nullptr;
	if (!prepare("DELETE FROM doodad_alternatives WHERE brush_id = ?;", &deleteStmt)) {
		rollbackTransaction();
		return false;
	}
	sqlite3_bind_int64(deleteStmt, 1, brushId);
	int rc = sqlite3_step(deleteStmt);
	sqlite3_finalize(deleteStmt);
	if (rc != SQLITE_DONE) {
		rollbackTransaction();
		return setErrorFromDatabase("Failed to clear doodad alternatives");
	}

	sqlite3_stmt* insertAltStmt = nullptr;
	if (!prepare("INSERT INTO doodad_alternatives(brush_id, sort_order) VALUES (?, ?);", &insertAltStmt)) {
		rollbackTransaction();
		return false;
	}
	sqlite3_stmt* insertSingleStmt = nullptr;
	if (!prepare("INSERT INTO doodad_single_items(doodad_alternative_id, item_id, chance, sort_order) VALUES (?, ?, ?, ?);", &insertSingleStmt)) {
		sqlite3_finalize(insertAltStmt);
		rollbackTransaction();
		return false;
	}
	sqlite3_stmt* insertCompositeStmt = nullptr;
	if (!prepare("INSERT INTO doodad_composites(doodad_alternative_id, chance, sort_order) VALUES (?, ?, ?);", &insertCompositeStmt)) {
		sqlite3_finalize(insertAltStmt);
		sqlite3_finalize(insertSingleStmt);
		rollbackTransaction();
		return false;
	}
	sqlite3_stmt* insertTileStmt = nullptr;
	if (!prepare("INSERT INTO doodad_composite_tiles(doodad_composite_id, offset_x, offset_y, offset_z, sort_order) VALUES (?, ?, ?, ?, ?);", &insertTileStmt)) {
		sqlite3_finalize(insertAltStmt);
		sqlite3_finalize(insertSingleStmt);
		sqlite3_finalize(insertCompositeStmt);
		rollbackTransaction();
		return false;
	}
	sqlite3_stmt* insertTileItemStmt = nullptr;
	if (!prepare("INSERT INTO doodad_composite_tile_items(doodad_composite_tile_id, item_id, sort_order) VALUES (?, ?, ?);", &insertTileItemStmt)) {
		sqlite3_finalize(insertAltStmt);
		sqlite3_finalize(insertSingleStmt);
		sqlite3_finalize(insertCompositeStmt);
		sqlite3_finalize(insertTileStmt);
		rollbackTransaction();
		return false;
	}

	const auto fail = [this, &insertAltStmt, &insertSingleStmt, &insertCompositeStmt, &insertTileStmt, &insertTileItemStmt](const wxString &message) {
		FinalizeStatements({ insertAltStmt, insertSingleStmt, insertCompositeStmt, insertTileStmt, insertTileItemStmt });
		rollbackTransaction();
		return setErrorFromDatabase(message);
	};

	const auto setDbError = [this](const wxString &message) {
		return setErrorFromDatabase(message);
	};

	for (const DoodadAlternativeRecord &alternative : alternatives) {
		sqlite3_reset(insertAltStmt);
		sqlite3_clear_bindings(insertAltStmt);
		sqlite3_bind_int64(insertAltStmt, 1, brushId);
		sqlite3_bind_int(insertAltStmt, 2, alternative.sortOrder);
		if (sqlite3_step(insertAltStmt) != SQLITE_DONE) {
			return fail("Failed to insert doodad alternative");
		}

		const int64_t alternativeId = sqlite3_last_insert_rowid(connection_);
		if (!WriteDoodadSingleItems(alternativeId, alternative.singleItems, insertSingleStmt, setDbError)) {
			return fail(lastError_);
		}
		if (!WriteDoodadComposites(
				connection_,
				alternativeId,
				alternative.composites,
				insertCompositeStmt,
				insertTileStmt,
				insertTileItemStmt,
				setDbError
			)) {
			return fail(lastError_);
		}
	}

	FinalizeStatements({ insertAltStmt, insertSingleStmt, insertCompositeStmt, insertTileStmt, insertTileItemStmt });
	if (!commitTransaction()) {
		rollbackTransaction();
		return false;
	}
	return true;
}

bool BrushDatabaseBrushRepository::getDoodadAlternatives(int64_t brushId, std::vector<DoodadAlternativeRecord> &outAlternatives) {
	outAlternatives.clear();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* altStmt = nullptr;
	if (!prepare("SELECT id, sort_order FROM doodad_alternatives "
				 "WHERE brush_id = ? ORDER BY sort_order ASC, id ASC;",
				 &altStmt)) {
		return false;
	}

	sqlite3_stmt* singleStmt = nullptr;
	if (!prepare("SELECT item_id, chance, sort_order FROM doodad_single_items "
				 "WHERE doodad_alternative_id = ? ORDER BY sort_order ASC, id ASC;",
				 &singleStmt)) {
		sqlite3_finalize(altStmt);
		return false;
	}

	sqlite3_stmt* compositeStmt = nullptr;
	if (!prepare("SELECT id, chance, sort_order FROM doodad_composites "
				 "WHERE doodad_alternative_id = ? ORDER BY sort_order ASC, id ASC;",
				 &compositeStmt)) {
		sqlite3_finalize(altStmt);
		sqlite3_finalize(singleStmt);
		return false;
	}

	sqlite3_stmt* tileStmt = nullptr;
	if (!prepare("SELECT id, offset_x, offset_y, offset_z, sort_order FROM doodad_composite_tiles "
				 "WHERE doodad_composite_id = ? ORDER BY sort_order ASC, id ASC;",
				 &tileStmt)) {
		sqlite3_finalize(altStmt);
		sqlite3_finalize(singleStmt);
		sqlite3_finalize(compositeStmt);
		return false;
	}

	sqlite3_stmt* tileItemStmt = nullptr;
	if (!prepare("SELECT item_id, sort_order FROM doodad_composite_tile_items "
				 "WHERE doodad_composite_tile_id = ? ORDER BY sort_order ASC, id ASC;",
				 &tileItemStmt)) {
		sqlite3_finalize(altStmt);
		sqlite3_finalize(singleStmt);
		sqlite3_finalize(compositeStmt);
		sqlite3_finalize(tileStmt);
		return false;
	}

	sqlite3_bind_int64(altStmt, 1, brushId);

	const auto fail = [this, &altStmt, &singleStmt, &compositeStmt, &tileStmt, &tileItemStmt](const wxString &message) {
		FinalizeStatements({ altStmt, singleStmt, compositeStmt, tileStmt, tileItemStmt });
		return setErrorFromDatabase(message);
	};

	const auto setDbError = [this](const wxString &message) {
		return setErrorFromDatabase(message);
	};

	for (;;) {
		const int altRc = sqlite3_step(altStmt);
		if (altRc == SQLITE_DONE) {
			break;
		}
		if (altRc != SQLITE_ROW) {
			return fail("Failed to read doodad alternatives");
		}

		const int64_t alternativeId = sqlite3_column_int64(altStmt, 0);

		DoodadAlternativeRecord alternative;
		alternative.sortOrder = sqlite3_column_int(altStmt, 1);
		if (!ReadDoodadSingleItems(alternativeId, singleStmt, alternative.singleItems, setDbError)) {
			return fail(lastError_);
		}
		if (!ReadDoodadComposites(
				alternativeId,
				compositeStmt,
				tileStmt,
				tileItemStmt,
				alternative.composites,
				setDbError
			)) {
			return fail(lastError_);
		}

		outAlternatives.push_back(alternative);
	}

	FinalizeStatements({ altStmt, singleStmt, compositeStmt, tileStmt, tileItemStmt });
	return true;
}

bool BrushDatabaseCatalogRepository::replaceAllTilesets(const std::vector<TilesetStorageRecord> &tilesets) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}
	if (!beginTransaction()) {
		return false;
	}

	if (!execute("DELETE FROM tilesets;")) {
		rollbackTransaction();
		return false;
	}

	sqlite3_stmt* insertTilesetStmt = nullptr;
	if (!prepare("INSERT INTO tilesets(name, palette_group_id, source_file) VALUES (?, ?, ?);", &insertTilesetStmt)) {
		rollbackTransaction();
		return false;
	}
	sqlite3_stmt* insertSectionStmt = nullptr;
	if (!prepare("INSERT INTO tileset_sections(tileset_id, section_type, sort_order) VALUES (?, ?, ?);", &insertSectionStmt)) {
		sqlite3_finalize(insertTilesetStmt);
		rollbackTransaction();
		return false;
	}
	sqlite3_stmt* insertEntryStmt = nullptr;
	if (!prepare("INSERT INTO tileset_brush_entries(tileset_section_id, entry_kind, brush_id, brush_name, item_id, from_item_id, to_item_id, after_brush_name, after_item_id, sort_order) "
				 "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
				 &insertEntryStmt)) {
		sqlite3_finalize(insertTilesetStmt);
		sqlite3_finalize(insertSectionStmt);
		rollbackTransaction();
		return false;
	}
	sqlite3_stmt* findBrushStmt = nullptr;
	if (!prepare("SELECT id FROM brushes WHERE name = ? LIMIT 2;", &findBrushStmt)) {
		sqlite3_finalize(insertTilesetStmt);
		sqlite3_finalize(insertSectionStmt);
		sqlite3_finalize(insertEntryStmt);
		rollbackTransaction();
		return false;
	}

	sqlite3_stmt* selectPaletteGroupStmt = nullptr;
	if (!prepare("SELECT id FROM palette_groups WHERE name = ? LIMIT 1;", &selectPaletteGroupStmt)) {
		FinalizeStatements({ insertTilesetStmt, insertSectionStmt, insertEntryStmt, findBrushStmt });
		rollbackTransaction();
		return false;
	}
	sqlite3_stmt* insertPaletteGroupStmt = nullptr;
	if (!prepare("INSERT INTO palette_groups(name, runtime_family, sort_order, is_builtin) "
				 "VALUES (?, ?, COALESCE((SELECT MAX(sort_order) + 1 FROM palette_groups), 4), 0);",
				 &insertPaletteGroupStmt)) {
		FinalizeStatements({ insertTilesetStmt, insertSectionStmt, insertEntryStmt, findBrushStmt, selectPaletteGroupStmt });
		rollbackTransaction();
		return false;
	}

	const auto fail = [this, &insertTilesetStmt, &insertSectionStmt, &insertEntryStmt, &findBrushStmt, &selectPaletteGroupStmt, &insertPaletteGroupStmt](const wxString &message) {
		FinalizeStatements({ insertTilesetStmt, insertSectionStmt, insertEntryStmt, findBrushStmt, selectPaletteGroupStmt, insertPaletteGroupStmt });
		rollbackTransaction();
		return setErrorFromDatabase(message);
	};

	const auto setDbError = [this](const wxString &message) {
		return setErrorFromDatabase(message);
	};
	const auto resolvePaletteGroupId = [&](const TilesetStorageRecord &tileset, int64_t &outGroupId) {
		outGroupId = 0;
		const wxString groupName = DerivePaletteGroupNameFromTileset(tileset);
		const wxString runtimeFamily = DerivePaletteRuntimeFamilyFromTileset(tileset);
		if (groupName.IsEmpty()) {
			return true;
		}

		sqlite3_reset(selectPaletteGroupStmt);
		sqlite3_clear_bindings(selectPaletteGroupStmt);
		sqlite3_bind_text(selectPaletteGroupStmt, 1, groupName.utf8_str(), -1, SQLITE_TRANSIENT);
		const int selectRc = sqlite3_step(selectPaletteGroupStmt);
		if (selectRc == SQLITE_ROW) {
			outGroupId = sqlite3_column_int64(selectPaletteGroupStmt, 0);
			return true;
		}
		if (selectRc != SQLITE_DONE) {
			return setDbError("Failed to resolve palette group");
		}

		sqlite3_reset(insertPaletteGroupStmt);
		sqlite3_clear_bindings(insertPaletteGroupStmt);
		sqlite3_bind_text(insertPaletteGroupStmt, 1, groupName.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(insertPaletteGroupStmt, 2, runtimeFamily.utf8_str(), -1, SQLITE_TRANSIENT);
		if (sqlite3_step(insertPaletteGroupStmt) != SQLITE_DONE) {
			return setDbError("Failed to create palette group");
		}

		outGroupId = sqlite3_last_insert_rowid(connection_);
		return true;
	};

	for (const TilesetStorageRecord &tileset : tilesets) {
		int64_t paletteGroupId = 0;
		if (!resolvePaletteGroupId(tileset, paletteGroupId)) {
			return fail(lastError_);
		}

		sqlite3_reset(insertTilesetStmt);
		sqlite3_clear_bindings(insertTilesetStmt);
		sqlite3_bind_text(insertTilesetStmt, 1, tileset.name.utf8_str(), -1, SQLITE_TRANSIENT);
		BindNullableInt64(insertTilesetStmt, 2, paletteGroupId);
		sqlite3_bind_text(insertTilesetStmt, 3, tileset.sourceFile.utf8_str(), -1, SQLITE_TRANSIENT);
		if (sqlite3_step(insertTilesetStmt) != SQLITE_DONE) {
			return fail("Failed to insert tileset");
		}
		const int64_t tilesetId = sqlite3_last_insert_rowid(connection_);

		for (const TilesetSectionRecord &section : tileset.sections) {
			sqlite3_reset(insertSectionStmt);
			sqlite3_clear_bindings(insertSectionStmt);
			sqlite3_bind_int64(insertSectionStmt, 1, tilesetId);
			sqlite3_bind_text(insertSectionStmt, 2, section.sectionType.utf8_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(insertSectionStmt, 3, section.sortOrder);
			if (sqlite3_step(insertSectionStmt) != SQLITE_DONE) {
				return fail("Failed to insert tileset section");
			}
			const int64_t sectionId = sqlite3_last_insert_rowid(connection_);
			if (!WriteTilesetEntries(sectionId, section.entries, insertEntryStmt, findBrushStmt, setDbError)) {
				return fail(lastError_);
			}
		}
	}

	FinalizeStatements({ insertTilesetStmt, insertSectionStmt, insertEntryStmt, findBrushStmt, selectPaletteGroupStmt, insertPaletteGroupStmt });
	if (!commitTransaction()) {
		rollbackTransaction();
		return false;
	}
	return true;
}

bool BrushDatabaseCatalogRepository::saveTileset(const TilesetStorageRecord &tileset) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}
	if (!beginTransaction()) {
		return false;
	}

	sqlite3_stmt* deleteTilesetStmt = nullptr;
	if (!prepare("DELETE FROM tilesets WHERE name = ?;", &deleteTilesetStmt)) {
		rollbackTransaction();
		return false;
	}
	sqlite3_bind_text(deleteTilesetStmt, 1, tileset.name.utf8_str(), -1, SQLITE_TRANSIENT);
	if (sqlite3_step(deleteTilesetStmt) != SQLITE_DONE) {
		sqlite3_finalize(deleteTilesetStmt);
		rollbackTransaction();
		return setErrorFromDatabase("Failed to delete existing tileset");
	}
	sqlite3_finalize(deleteTilesetStmt);

	sqlite3_stmt* insertTilesetStmt = nullptr;
	if (!prepare("INSERT INTO tilesets(name, palette_group_id, source_file) VALUES (?, ?, ?);", &insertTilesetStmt)) {
		rollbackTransaction();
		return false;
	}
	sqlite3_stmt* insertSectionStmt = nullptr;
	if (!prepare("INSERT INTO tileset_sections(tileset_id, section_type, sort_order) VALUES (?, ?, ?);", &insertSectionStmt)) {
		sqlite3_finalize(insertTilesetStmt);
		rollbackTransaction();
		return false;
	}
	sqlite3_stmt* insertEntryStmt = nullptr;
	if (!prepare("INSERT INTO tileset_brush_entries(tileset_section_id, entry_kind, brush_id, brush_name, item_id, from_item_id, to_item_id, after_brush_name, after_item_id, sort_order) "
				 "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
				 &insertEntryStmt)) {
		sqlite3_finalize(insertTilesetStmt);
		sqlite3_finalize(insertSectionStmt);
		rollbackTransaction();
		return false;
	}
	sqlite3_stmt* findBrushStmt = nullptr;
	if (!prepare("SELECT id FROM brushes WHERE name = ? LIMIT 2;", &findBrushStmt)) {
		FinalizeStatements({ insertTilesetStmt, insertSectionStmt, insertEntryStmt });
		rollbackTransaction();
		return false;
	}
	sqlite3_stmt* selectPaletteGroupStmt = nullptr;
	if (!prepare("SELECT id FROM palette_groups WHERE name = ? LIMIT 1;", &selectPaletteGroupStmt)) {
		FinalizeStatements({ insertTilesetStmt, insertSectionStmt, insertEntryStmt, findBrushStmt });
		rollbackTransaction();
		return false;
	}
	sqlite3_stmt* insertPaletteGroupStmt = nullptr;
	if (!prepare("INSERT INTO palette_groups(name, runtime_family, sort_order, is_builtin) "
				 "VALUES (?, ?, COALESCE((SELECT MAX(sort_order) + 1 FROM palette_groups), 4), 0);",
				 &insertPaletteGroupStmt)) {
		FinalizeStatements({ insertTilesetStmt, insertSectionStmt, insertEntryStmt, findBrushStmt, selectPaletteGroupStmt });
		rollbackTransaction();
		return false;
	}

	const auto fail = [this, &insertTilesetStmt, &insertSectionStmt, &insertEntryStmt, &findBrushStmt, &selectPaletteGroupStmt, &insertPaletteGroupStmt](const wxString &message) {
		FinalizeStatements({ insertTilesetStmt, insertSectionStmt, insertEntryStmt, findBrushStmt, selectPaletteGroupStmt, insertPaletteGroupStmt });
		rollbackTransaction();
		return setErrorFromDatabase(message);
	};

	const auto setDbError = [this](const wxString &message) {
		return setErrorFromDatabase(message);
	};
	const auto resolvePaletteGroupId = [&](int64_t &outGroupId) {
		outGroupId = 0;
		const wxString groupName = DerivePaletteGroupNameFromTileset(tileset);
		const wxString runtimeFamily = DerivePaletteRuntimeFamilyFromTileset(tileset);
		if (groupName.IsEmpty()) {
			return true;
		}

		sqlite3_reset(selectPaletteGroupStmt);
		sqlite3_clear_bindings(selectPaletteGroupStmt);
		sqlite3_bind_text(selectPaletteGroupStmt, 1, groupName.utf8_str(), -1, SQLITE_TRANSIENT);
		const int selectRc = sqlite3_step(selectPaletteGroupStmt);
		if (selectRc == SQLITE_ROW) {
			outGroupId = sqlite3_column_int64(selectPaletteGroupStmt, 0);
			return true;
		}
		if (selectRc != SQLITE_DONE) {
			return setDbError("Failed to resolve palette group");
		}

		sqlite3_reset(insertPaletteGroupStmt);
		sqlite3_clear_bindings(insertPaletteGroupStmt);
		sqlite3_bind_text(insertPaletteGroupStmt, 1, groupName.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(insertPaletteGroupStmt, 2, runtimeFamily.utf8_str(), -1, SQLITE_TRANSIENT);
		if (sqlite3_step(insertPaletteGroupStmt) != SQLITE_DONE) {
			return setDbError("Failed to create palette group");
		}

		outGroupId = sqlite3_last_insert_rowid(connection_);
		return true;
	};

	int64_t paletteGroupId = 0;
	if (!resolvePaletteGroupId(paletteGroupId)) {
		return fail(lastError_);
	}

	sqlite3_bind_text(insertTilesetStmt, 1, tileset.name.utf8_str(), -1, SQLITE_TRANSIENT);
	BindNullableInt64(insertTilesetStmt, 2, paletteGroupId);
	sqlite3_bind_text(insertTilesetStmt, 3, tileset.sourceFile.utf8_str(), -1, SQLITE_TRANSIENT);
	if (sqlite3_step(insertTilesetStmt) != SQLITE_DONE) {
		return fail("Failed to insert tileset");
	}
	const int64_t tilesetId = sqlite3_last_insert_rowid(connection_);

	for (const TilesetSectionRecord &section : tileset.sections) {
		sqlite3_reset(insertSectionStmt);
		sqlite3_clear_bindings(insertSectionStmt);
		sqlite3_bind_int64(insertSectionStmt, 1, tilesetId);
		sqlite3_bind_text(insertSectionStmt, 2, section.sectionType.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int(insertSectionStmt, 3, section.sortOrder);
		if (sqlite3_step(insertSectionStmt) != SQLITE_DONE) {
			return fail("Failed to insert tileset section");
		}
		const int64_t sectionId = sqlite3_last_insert_rowid(connection_);
		if (!WriteTilesetEntries(sectionId, section.entries, insertEntryStmt, findBrushStmt, setDbError)) {
			return fail(lastError_);
		}
	}

	FinalizeStatements({ insertTilesetStmt, insertSectionStmt, insertEntryStmt, findBrushStmt, selectPaletteGroupStmt, insertPaletteGroupStmt });
	if (!commitTransaction()) {
		rollbackTransaction();
		return false;
	}
	return true;
}
bool BrushDatabaseCatalogRepository::deleteTileset(const wxString &name) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}
	if (!beginTransaction()) {
		return false;
	}

	sqlite3_stmt* deleteTilesetStmt = nullptr;
	if (!prepare("DELETE FROM tilesets WHERE name = ?;", &deleteTilesetStmt)) {
		rollbackTransaction();
		return false;
	}

	sqlite3_bind_text(deleteTilesetStmt, 1, name.utf8_str(), -1, SQLITE_TRANSIENT);
	if (sqlite3_step(deleteTilesetStmt) != SQLITE_DONE) {
		sqlite3_finalize(deleteTilesetStmt);
		rollbackTransaction();
		return setErrorFromDatabase("Failed to delete tileset");
	}

	const int deletedRows = sqlite3_changes(connection_);
	sqlite3_finalize(deleteTilesetStmt);
	if (deletedRows <= 0) {
		rollbackTransaction();
		return setError("Tileset '" + name + "' was not found in SQLite.");
	}

	return commitTransaction();
}

bool BrushDatabaseCatalogRepository::savePaletteGroup(const PaletteGroupRecord &group) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	wxString groupName = group.name;
	groupName.Trim(true);
	groupName.Trim(false);
	if (groupName.IsEmpty()) {
		return setError("Palette group name cannot be empty.");
	}

	const wxString runtimeFamily = group.runtimeFamily;

	if (!beginTransaction()) {
		return false;
	}

	sqlite3_stmt* findExistingStmt = nullptr;
	if (!prepare("SELECT id, name, runtime_family, is_builtin FROM palette_groups WHERE id = ? LIMIT 1;", &findExistingStmt)) {
		rollbackTransaction();
		return false;
	}

	sqlite3_stmt* findByNameStmt = nullptr;
	if (!prepare("SELECT id FROM palette_groups WHERE lower(name) = lower(?) LIMIT 1;", &findByNameStmt)) {
		sqlite3_finalize(findExistingStmt);
		rollbackTransaction();
		return false;
	}

	sqlite3_stmt* insertStmt = nullptr;
	if (!prepare("INSERT INTO palette_groups(name, runtime_family, sort_order, is_builtin) "
				 "VALUES (?, ?, COALESCE((SELECT MAX(sort_order) + 1 FROM palette_groups), 4), 0);",
				 &insertStmt)) {
		FinalizeStatements({ findExistingStmt, findByNameStmt });
		rollbackTransaction();
		return false;
	}

	sqlite3_stmt* updateStmt = nullptr;
	if (!prepare("UPDATE palette_groups SET name = ?, runtime_family = ? WHERE id = ?;", &updateStmt)) {
		FinalizeStatements({ findExistingStmt, findByNameStmt, insertStmt });
		rollbackTransaction();
		return false;
	}

	const auto fail = [this, &findExistingStmt, &findByNameStmt, &insertStmt, &updateStmt](const wxString &message) {
		FinalizeStatements({ findExistingStmt, findByNameStmt, insertStmt, updateStmt });
		rollbackTransaction();
		return setErrorFromDatabase(message);
	};

	int64_t existingId = 0;
	bool existingIsBuiltin = false;
	wxString existingName;
	wxString existingRuntimeFamily;
	if (group.id > 0) {
		sqlite3_bind_int64(findExistingStmt, 1, group.id);
		const int existingRc = sqlite3_step(findExistingStmt);
		if (existingRc == SQLITE_DONE) {
			FinalizeStatements({ findExistingStmt, findByNameStmt, insertStmt, updateStmt });
			rollbackTransaction();
			return setError("Palette group was not found in SQLite.");
		}
		if (existingRc != SQLITE_ROW) {
			return fail("Failed to query existing palette group");
		}

		existingId = sqlite3_column_int64(findExistingStmt, 0);
		existingName = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(findExistingStmt, 1)));
		existingRuntimeFamily = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(findExistingStmt, 2)));
		existingIsBuiltin = sqlite3_column_int(findExistingStmt, 3) != 0;
	}

	sqlite3_bind_text(findByNameStmt, 1, groupName.utf8_str(), -1, SQLITE_TRANSIENT);
	const int findByNameRc = sqlite3_step(findByNameStmt);
	if (findByNameRc == SQLITE_ROW) {
		const int64_t conflictingId = sqlite3_column_int64(findByNameStmt, 0);
		if (conflictingId != group.id) {
			FinalizeStatements({ findExistingStmt, findByNameStmt, insertStmt, updateStmt });
			rollbackTransaction();
			return setError("A palette group with this name already exists.");
		}
	} else if (findByNameRc != SQLITE_DONE) {
		return fail("Failed to validate palette group name");
	}

	if (group.id <= 0) {
		sqlite3_bind_text(insertStmt, 1, groupName.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(insertStmt, 2, runtimeFamily.utf8_str(), -1, SQLITE_TRANSIENT);
		if (sqlite3_step(insertStmt) != SQLITE_DONE) {
			return fail("Failed to insert palette group");
		}
	} else {
		if (existingIsBuiltin && !groupName.IsSameAs(existingName, false)) {
			FinalizeStatements({ findExistingStmt, findByNameStmt, insertStmt, updateStmt });
			rollbackTransaction();
			return setError("Built-in palette groups cannot be renamed.");
		}

		sqlite3_bind_text(updateStmt, 1, groupName.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(updateStmt, 2, runtimeFamily.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int64(updateStmt, 3, existingId);
		if (sqlite3_step(updateStmt) != SQLITE_DONE) {
			return fail("Failed to update palette group");
		}
	}

	FinalizeStatements({ findExistingStmt, findByNameStmt, insertStmt, updateStmt });
	if (!commitTransaction()) {
		rollbackTransaction();
		return false;
	}

	return true;
}

bool BrushDatabaseCatalogRepository::deletePaletteGroup(const wxString &name) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	wxString groupName = name;
	groupName.Trim(true);
	groupName.Trim(false);
	if (groupName.IsEmpty()) {
		return setError("Palette group name cannot be empty.");
	}

	if (!beginTransaction()) {
		return false;
	}

	sqlite3_stmt* findGroupStmt = nullptr;
	if (!prepare("SELECT id, is_builtin FROM palette_groups WHERE lower(name) = lower(?) LIMIT 1;", &findGroupStmt)) {
		rollbackTransaction();
		return false;
	}

	sqlite3_stmt* usageStmt = nullptr;
	if (!prepare("SELECT COUNT(*) FROM tilesets WHERE palette_group_id = ?;", &usageStmt)) {
		sqlite3_finalize(findGroupStmt);
		rollbackTransaction();
		return false;
	}

	sqlite3_stmt* deleteStmt = nullptr;
	if (!prepare("DELETE FROM palette_groups WHERE id = ?;", &deleteStmt)) {
		FinalizeStatements({ findGroupStmt, usageStmt });
		rollbackTransaction();
		return false;
	}

	sqlite3_bind_text(findGroupStmt, 1, groupName.utf8_str(), -1, SQLITE_TRANSIENT);
	const int findRc = sqlite3_step(findGroupStmt);
	if (findRc == SQLITE_DONE) {
		FinalizeStatements({ findGroupStmt, usageStmt, deleteStmt });
		rollbackTransaction();
		return setError("Palette group was not found in SQLite.");
	}
	if (findRc != SQLITE_ROW) {
		FinalizeStatements({ findGroupStmt, usageStmt, deleteStmt });
		rollbackTransaction();
		return setErrorFromDatabase("Failed to query palette group");
	}

	const int64_t groupId = sqlite3_column_int64(findGroupStmt, 0);
	const bool isBuiltin = sqlite3_column_int(findGroupStmt, 1) != 0;
	if (isBuiltin) {
		FinalizeStatements({ findGroupStmt, usageStmt, deleteStmt });
		rollbackTransaction();
		return setError("Built-in palette groups cannot be deleted.");
	}

	sqlite3_bind_int64(usageStmt, 1, groupId);
	const int usageRc = sqlite3_step(usageStmt);
	if (usageRc != SQLITE_ROW) {
		FinalizeStatements({ findGroupStmt, usageStmt, deleteStmt });
		rollbackTransaction();
		return setErrorFromDatabase("Failed to validate palette group usage");
	}
	const int paletteCount = sqlite3_column_int(usageStmt, 0);
	if (paletteCount > 0) {
		FinalizeStatements({ findGroupStmt, usageStmt, deleteStmt });
		rollbackTransaction();
		return setError("Move the palettes out of this group before deleting it.");
	}

	sqlite3_bind_int64(deleteStmt, 1, groupId);
	if (sqlite3_step(deleteStmt) != SQLITE_DONE) {
		FinalizeStatements({ findGroupStmt, usageStmt, deleteStmt });
		rollbackTransaction();
		return setErrorFromDatabase("Failed to delete palette group");
	}

	FinalizeStatements({ findGroupStmt, usageStmt, deleteStmt });
	return commitTransaction();
}

bool BrushDatabaseCatalogRepository::getAllPaletteGroups(std::vector<PaletteGroupRecord> &outGroups) {
	outGroups.clear();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("SELECT id, name, runtime_family, sort_order, is_builtin FROM palette_groups ORDER BY sort_order ASC, name COLLATE NOCASE ASC, id ASC;", &stmt)) {
		return false;
	}

	for (;;) {
		const int rc = sqlite3_step(stmt);
		if (rc == SQLITE_DONE) {
			break;
		}
		if (rc != SQLITE_ROW) {
			sqlite3_finalize(stmt);
			return setErrorFromDatabase("Failed to list palette groups");
		}

		PaletteGroupRecord group;
		group.id = sqlite3_column_int64(stmt, 0);
		group.name = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
		group.runtimeFamily = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
		group.sortOrder = sqlite3_column_int(stmt, 3);
		group.isBuiltin = sqlite3_column_int(stmt, 4) != 0;
		outGroups.push_back(group);
	}

	sqlite3_finalize(stmt);
	return true;
}

bool BrushDatabaseCatalogRepository::getTilesetByName(const wxString &name, TilesetStorageRecord &outTileset) {
	outTileset = TilesetStorageRecord();


	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* tilesetStmt = nullptr;
	if (!prepare("SELECT t.id, t.name, t.source_file, t.palette_group_id, pg.name, pg.runtime_family, COALESCE(pg.sort_order, 0) "
				 "FROM tilesets AS t "
				 "LEFT JOIN palette_groups AS pg ON pg.id = t.palette_group_id "
				 "WHERE t.name = ? LIMIT 1;",
				 &tilesetStmt)) {
		return false;
	}

	sqlite3_bind_text(tilesetStmt, 1, name.utf8_str(), -1, SQLITE_TRANSIENT);
	const int rc = sqlite3_step(tilesetStmt);
	if (rc == SQLITE_DONE) {
		sqlite3_finalize(tilesetStmt);
		return setError("Tileset '" + name + "' was not found in SQLite.");
	}
	if (rc != SQLITE_ROW) {
		sqlite3_finalize(tilesetStmt);
		return setErrorFromDatabase("Failed to query tileset by name");
	}

	const int64_t tilesetId = sqlite3_column_int64(tilesetStmt, 0);
	outTileset.name = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(tilesetStmt, 1)));
	outTileset.sourceFile = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(tilesetStmt, 2)));
	outTileset.paletteGroupId = ReadNullableInt64(tilesetStmt, 3);
	outTileset.paletteGroupName = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(tilesetStmt, 4)));
	outTileset.paletteGroupRuntimeFamily = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(tilesetStmt, 5)));
	outTileset.paletteGroupSortOrder = sqlite3_column_int(tilesetStmt, 6);
	sqlite3_finalize(tilesetStmt);

	sqlite3_stmt* sectionStmt = nullptr;
	if (!prepare("SELECT id, section_type, sort_order FROM tileset_sections "
				 "WHERE tileset_id = ? ORDER BY sort_order ASC, id ASC;",
				 &sectionStmt)) {
		return false;
	}

	sqlite3_stmt* entryStmt = nullptr;
	if (!prepare("SELECT entry_kind, brush_id, brush_name, item_id, from_item_id, to_item_id, after_brush_name, after_item_id, sort_order "
				 "FROM tileset_brush_entries WHERE tileset_section_id = ? ORDER BY sort_order ASC, id ASC;",
				 &entryStmt)) {
		sqlite3_finalize(sectionStmt);
		return false;
	}

	sqlite3_bind_int64(sectionStmt, 1, tilesetId);

	for (;;) {
		const int sectionRc = sqlite3_step(sectionStmt);
		if (sectionRc == SQLITE_DONE) {
			break;
		}
		if (sectionRc != SQLITE_ROW) {
			sqlite3_finalize(sectionStmt);
			sqlite3_finalize(entryStmt);
			return setErrorFromDatabase("Failed to read tileset sections");
		}

		const int64_t sectionId = sqlite3_column_int64(sectionStmt, 0);

		TilesetSectionRecord section;
		section.sectionType = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(sectionStmt, 1)));
		section.sortOrder = sqlite3_column_int(sectionStmt, 2);

		sqlite3_reset(entryStmt);
		sqlite3_clear_bindings(entryStmt);
		sqlite3_bind_int64(entryStmt, 1, sectionId);

		for (;;) {
			const int entryRc = sqlite3_step(entryStmt);
			if (entryRc == SQLITE_DONE) {
				break;
			}
			if (entryRc != SQLITE_ROW) {
				sqlite3_finalize(sectionStmt);
				sqlite3_finalize(entryStmt);
				return setErrorFromDatabase("Failed to read tileset entries");
			}

			TilesetEntryRecord entry;
			entry.entryKind = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(entryStmt, 0)));
			entry.brushId = ReadNullableInt64(entryStmt, 1);
			entry.brushName = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(entryStmt, 2)));
			entry.itemId = sqlite3_column_int(entryStmt, 3);
			entry.fromItemId = sqlite3_column_int(entryStmt, 4);
			entry.toItemId = sqlite3_column_int(entryStmt, 5);
			entry.afterBrushName = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(entryStmt, 6)));
			entry.afterItemId = sqlite3_column_int(entryStmt, 7);
			entry.sortOrder = sqlite3_column_int(entryStmt, 8);
			section.entries.push_back(entry);
		}

		outTileset.sections.push_back(section);
	}

	sqlite3_finalize(sectionStmt);
	sqlite3_finalize(entryStmt);
	return true;
}

bool BrushDatabaseCatalogRepository::getAllTilesets(std::vector<TilesetStorageRecord> &outTilesets) {
	outTilesets.clear();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("SELECT t.name "
				 "FROM tilesets AS t "
				 "LEFT JOIN palette_groups AS pg ON pg.id = t.palette_group_id "
				 "ORDER BY COALESCE(pg.sort_order, 999999) ASC, "
				 "pg.name COLLATE NOCASE ASC, "
				 "t.name COLLATE NOCASE ASC, "
				 "t.id ASC;",
				 &stmt)) {
		return false;
	}

	for (;;) {
		const int rc = sqlite3_step(stmt);
		if (rc == SQLITE_DONE) {
			break;
		}
		if (rc != SQLITE_ROW) {
			sqlite3_finalize(stmt);
			return setErrorFromDatabase("Failed to list tilesets");
		}

		TilesetStorageRecord tileset;
		if (!getTilesetByName(ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))), tileset)) {
			sqlite3_finalize(stmt);
			return false;
		}
		outTilesets.push_back(tileset);
	}

	sqlite3_finalize(stmt);
	return true;
}

bool BrushDatabaseCatalogRepository::generateAuditReport(MaterialsDatabaseAuditReport &outReport) {
	outReport = MaterialsDatabaseAuditReport();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	const wxString supportedBrushTypesSql = "('ground', 'wall', 'wall decoration', 'doodad', 'carpet', 'table')";

	sqlite3_stmt* countStmt = nullptr;
	wxString countSql;
	countSql << "SELECT "
			 << "(SELECT COUNT(*) FROM brushes), "
			 << "(SELECT COUNT(*) FROM border_sets), "
			 << "(SELECT COUNT(*) FROM tilesets), "
			 << "(SELECT COUNT(*) FROM tileset_sections), "
			 << "(SELECT COUNT(*) FROM tileset_brush_entries), "
			 << "(SELECT COUNT(*) FROM brushes WHERE type NOT IN " << supportedBrushTypesSql << "), "
			 << "(SELECT COUNT(*) FROM ground_brush_borders WHERE target_mode = 'brush' AND target_brush_name <> '' AND target_brush_id IS NULL), "
			 << "(SELECT COUNT(*) FROM brush_links bl "
			 << "JOIN brushes src ON src.id = bl.brush_id "
			 << "WHERE bl.target_brush_name <> '' AND bl.target_brush_name <> 'all' AND bl.target_brush_id IS NULL "
			 << "AND NOT EXISTS (SELECT 1 FROM brushes t WHERE t.name = bl.target_brush_name COLLATE NOCASE AND t.type = src.type)), "
			 << "(SELECT COUNT(*) FROM tileset_brush_entries WHERE brush_name <> '' AND brush_id IS NULL), "
			 << "(SELECT COUNT(*) FROM ground_border_case_conditions c "
			 << "WHERE c.condition_type = 'match_border' AND c.match_value > 0 "
			 << "AND NOT EXISTS (SELECT 1 FROM border_sets bs WHERE bs.xml_border_id = c.match_value)), "
			 << "(SELECT COUNT(*) FROM ground_border_case_actions a "
			 << "WHERE a.action_type = 'replace_border' AND a.target_value > 0 "
			 << "AND NOT EXISTS (SELECT 1 FROM border_sets bs WHERE bs.xml_border_id = a.target_value)), "
			 << "(SELECT COUNT(*) FROM ground_border_case_conditions c "
			 << "JOIN border_sets bs ON bs.xml_border_id = c.match_value "
			 << "WHERE c.condition_type = 'match_border' AND c.match_value > 0 AND c.edge <> '' "
			 << "AND EXISTS (SELECT 1 FROM border_set_items bi0 WHERE bi0.border_set_id = bs.id) "
			 << "AND NOT EXISTS (SELECT 1 FROM border_set_items bi WHERE bi.border_set_id = bs.id AND bi.edge = c.edge)), "
			 << "(SELECT COUNT(*) FROM ground_border_case_actions a "
			 << "JOIN border_sets bs ON bs.xml_border_id = a.target_value "
			 << "WHERE a.action_type = 'replace_border' AND a.target_value > 0 AND a.edge <> '' "
			 << "AND EXISTS (SELECT 1 FROM border_set_items bi0 WHERE bi0.border_set_id = bs.id) "
			 << "AND NOT EXISTS (SELECT 1 FROM border_set_items bi WHERE bi.border_set_id = bs.id AND bi.edge = a.edge));";
	if (!prepare(countSql.utf8_str(), &countStmt)) {
		return false;
	}

	const int rc = sqlite3_step(countStmt);
	if (rc != SQLITE_ROW) {
		sqlite3_finalize(countStmt);
		return setErrorFromDatabase("Failed to build SQLite audit report");
	}

	outReport.brushCount = sqlite3_column_int(countStmt, 0);
	outReport.borderSetCount = sqlite3_column_int(countStmt, 1);
	outReport.tilesetCount = sqlite3_column_int(countStmt, 2);
	outReport.tilesetSectionCount = sqlite3_column_int(countStmt, 3);
	outReport.tilesetEntryCount = sqlite3_column_int(countStmt, 4);
	outReport.unsupportedBrushTypeCount = sqlite3_column_int(countStmt, 5);
	outReport.unresolvedGroundTargets = sqlite3_column_int(countStmt, 6);
	outReport.unresolvedBrushLinks = sqlite3_column_int(countStmt, 7);
	outReport.unresolvedTilesetEntries = sqlite3_column_int(countStmt, 8);
	outReport.unresolvedCaseMatchBorderIds = sqlite3_column_int(countStmt, 9);
	outReport.unresolvedCaseReplaceBorderTargetIds = sqlite3_column_int(countStmt, 10);
	outReport.caseMatchBorderEdgesWithoutItem = sqlite3_column_int(countStmt, 11);
	outReport.caseReplaceBorderEdgesWithoutItem = sqlite3_column_int(countStmt, 12);
	sqlite3_finalize(countStmt);

	sqlite3_stmt* groupedStmt = nullptr;
	if (!prepare("SELECT type, COUNT(*) FROM brushes GROUP BY type ORDER BY type ASC;", &groupedStmt)) {
		return false;
	}

	for (;;) {
		const int groupedRc = sqlite3_step(groupedStmt);
		if (groupedRc == SQLITE_DONE) {
			break;
		}
		if (groupedRc != SQLITE_ROW) {
			sqlite3_finalize(groupedStmt);
			return setErrorFromDatabase("Failed to group brushes by type");
		}

		BrushTypeCountRecord typeCount;
		typeCount.type = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(groupedStmt, 0)));
		typeCount.count = sqlite3_column_int(groupedStmt, 1);
		outReport.brushTypeCounts.push_back(typeCount);
	}


	sqlite3_stmt* unsupportedTypesStmt = nullptr;
	if (!prepare(("SELECT type, COUNT(*) "
				 "FROM brushes "
				 "WHERE type NOT IN " + supportedBrushTypesSql + " "
				 "GROUP BY type "
				 "ORDER BY COUNT(*) DESC, type COLLATE NOCASE ASC;").utf8_str(),
				 &unsupportedTypesStmt)) {
		return false;
	}

	for (;;) {
		const int rc = sqlite3_step(unsupportedTypesStmt);
		if (rc == SQLITE_DONE) {
			break;
		}
		if (rc != SQLITE_ROW) {
			sqlite3_finalize(unsupportedTypesStmt);
			return setErrorFromDatabase("Failed to group unsupported brush types");
		}

		BrushTypeCountRecord typeCount;
		typeCount.type = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(unsupportedTypesStmt, 0)));
		typeCount.count = sqlite3_column_int(unsupportedTypesStmt, 1);
		outReport.unsupportedBrushTypeCounts.push_back(typeCount);
	}
	sqlite3_finalize(unsupportedTypesStmt);

	sqlite3_stmt* unsupportedSamplesStmt = nullptr;
	if (!prepare(("SELECT id, name, type, source_file "
				 "FROM brushes "
				 "WHERE type NOT IN " + supportedBrushTypesSql + " "
				 "ORDER BY type COLLATE NOCASE ASC, name COLLATE NOCASE ASC, id ASC "
				 "LIMIT 20;").utf8_str(),
				 &unsupportedSamplesStmt)) {
		return false;
	}

	for (;;) {
		const int rc = sqlite3_step(unsupportedSamplesStmt);
		if (rc == SQLITE_DONE) {
			break;
		}
		if (rc != SQLITE_ROW) {
			sqlite3_finalize(unsupportedSamplesStmt);
			return setErrorFromDatabase("Failed to list unsupported brush samples");
		}

		UnsupportedBrushSampleRecord sample;
		sample.id = sqlite3_column_int64(unsupportedSamplesStmt, 0);
		sample.name = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(unsupportedSamplesStmt, 1)));
		sample.type = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(unsupportedSamplesStmt, 2)));
		sample.sourceFile = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(unsupportedSamplesStmt, 3)));
		outReport.unsupportedBrushSamples.push_back(sample);
	}

	sqlite3_finalize(unsupportedSamplesStmt);
	sqlite3_finalize(groupedStmt);
	return true;
}

bool BrushDatabaseCatalogRepository::hasCompleteImportForCurrentSchema(bool &outReady) {
	wxString ignoredReason;
	return hasCompleteImportForCurrentSchema(outReady, ignoredReason);
}

bool BrushDatabaseCatalogRepository::hasCompleteImportForCurrentSchema(bool &outReady, wxString &outReason) {
	outReady = false;
	outReason.clear();

	if (!isOpen()) {
		outReason = "SQLite database is not open.";
		return setError(outReason);
	}

	int version = 0;
	if (!schemaManager_.getCurrentSchemaVersion(version)) {
		outReason = lastError();
		return false;
	}
	if (version != kBrushDatabaseSchemaVersion) {
		outReason = wxString::Format(
			"Schema version mismatch (found %d, expected %d).",
			version,
			kBrushDatabaseSchemaVersion
		);
		return true;
	}

	bool importComplete = false;
	wxString importReason;
	if (!isMaterialsImportComplete(importComplete, importReason)) {
		outReason = importReason.IsEmpty() ? lastError() : importReason;
		return false;
	}

	MaterialsDatabaseAuditReport report;
	if (!generateAuditReport(report)) {
		outReason = lastError();
		return false;
	}

	wxString unsupportedBrushTypesDetail;
	for (size_t i = 0; i < report.unsupportedBrushTypeCounts.size(); ++i) {
		if (i > 0) {
			unsupportedBrushTypesDetail += ", ";
		}
		unsupportedBrushTypesDetail += wxString::Format(
			"%s (%d)",
			report.unsupportedBrushTypeCounts[i].type,
			report.unsupportedBrushTypeCounts[i].count
		);
	}

	if (importComplete) {
		outReady = true;
		if (report.unsupportedBrushTypeCount > 0) {
			outReady = false;
			if (unsupportedBrushTypesDetail.IsEmpty()) {
				outReason = wxString::Format("Database contains unsupported brush types (%d).", report.unsupportedBrushTypeCount);
			} else {
				outReason = wxString::Format(
					"Database contains unsupported brush types (%d): %s.",
					report.unsupportedBrushTypeCount,
					unsupportedBrushTypesDetail
				);
			}
		} else if (report.unresolvedGroundTargets > 0
			|| report.unresolvedBrushLinks > 0
			|| report.unresolvedTilesetEntries > 0
			|| report.unresolvedCaseMatchBorderIds > 0
			|| report.unresolvedCaseReplaceBorderTargetIds > 0) {
			outReady = false;
			outReason = wxString::Format(
				"Database contains unresolved references (ground targets=%d, brush links=%d, tileset entries=%d, match_border ids=%d, replace_border target ids=%d).",
				report.unresolvedGroundTargets,
				report.unresolvedBrushLinks,
				report.unresolvedTilesetEntries,
				report.unresolvedCaseMatchBorderIds,
				report.unresolvedCaseReplaceBorderTargetIds
			);
		}
		return true;
	}

	bool hasGround = false;
	bool hasWall = false;
	bool hasDoodad = false;
	bool hasCarpet = false;
	bool hasTable = false;
	for (const BrushTypeCountRecord &typeCount : report.brushTypeCounts) {
		if (typeCount.type == "ground" && typeCount.count > 0) {
			hasGround = true;
		} else if (typeCount.type == "wall" && typeCount.count > 0) {
			hasWall = true;
		} else if (typeCount.type == "doodad" && typeCount.count > 0) {
			hasDoodad = true;
		} else if (typeCount.type == "carpet" && typeCount.count > 0) {
			hasCarpet = true;
		} else if (typeCount.type == "table" && typeCount.count > 0) {
			hasTable = true;
		}
	}

	outReady = hasGround && hasWall && hasDoodad && hasCarpet && hasTable && report.borderSetCount > 0 && report.tilesetCount > 0;
	if (!outReady) {
		std::vector<wxString> missing;
		if (!hasGround) {
			missing.push_back("ground");
		}
		if (!hasWall) {
			missing.push_back("wall");
		}
		if (!hasDoodad) {
			missing.push_back("doodad");
		}
		if (!hasCarpet) {
			missing.push_back("carpet");
		}
		if (!hasTable) {
			missing.push_back("table");
		}
		if (report.borderSetCount <= 0) {
			missing.push_back("border_sets");
		}
		if (report.tilesetCount <= 0) {
			missing.push_back("tilesets");
		}

		wxString detail;
		for (size_t i = 0; i < missing.size(); ++i) {
			if (i > 0) {
				detail += ", ";
			}
			detail += missing[i];
		}
		outReason = "Database is missing imported materials data (" + detail + ").";
	} else if (report.unsupportedBrushTypeCount > 0) {
		outReady = false;
		if (unsupportedBrushTypesDetail.IsEmpty()) {
			outReason = wxString::Format("Database contains unsupported brush types (%d).", report.unsupportedBrushTypeCount);
		} else {
			outReason = wxString::Format(
				"Database contains unsupported brush types (%d): %s.",
				report.unsupportedBrushTypeCount,
				unsupportedBrushTypesDetail
			);
		}
	} else if (report.unresolvedGroundTargets > 0
			   || report.unresolvedBrushLinks > 0
			   || report.unresolvedTilesetEntries > 0
			   || report.unresolvedCaseMatchBorderIds > 0
			   || report.unresolvedCaseReplaceBorderTargetIds > 0) {
		outReady = false;
		outReason = wxString::Format(
			"Database contains unresolved references (ground targets=%d, brush links=%d, tileset entries=%d, match_border ids=%d, replace_border target ids=%d).",
			report.unresolvedGroundTargets,
			report.unresolvedBrushLinks,
			report.unresolvedTilesetEntries,
			report.unresolvedCaseMatchBorderIds,
			report.unresolvedCaseReplaceBorderTargetIds
		);
	} else if (!isReadOnly()) {
		markMaterialsImportComplete("audit_auto_mark");
	}
	return true;
}

bool BrushDatabaseCatalogRepository::isMaterialsImportComplete(bool &outComplete, wxString &outReason) {
	outComplete = false;
	outReason.clear();

	MaterialsImportStatusRecord status;
	if (!getMaterialsImportStatus(status, outReason)) {
		return false;
	}

	outComplete = status.completed;
	return true;
}

bool BrushDatabaseCatalogRepository::getMaterialsImportStatus(MaterialsImportStatusRecord &outStatus, wxString &outReason) {
	outStatus = MaterialsImportStatusRecord();
	outReason.clear();

	if (!isOpen()) {
		outReason = "SQLite database is not open.";
		return setError(outReason);
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("SELECT completed, coalesce(completed_at, 0), source FROM import_status WHERE id = 1;", &stmt)) {
		outReason = lastError();
		return false;
	}

	const int rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW) {
		outStatus.completed = sqlite3_column_int(stmt, 0) != 0;
		outStatus.completedAt = sqlite3_column_int64(stmt, 1);
		outStatus.source = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
		sqlite3_finalize(stmt);
		return true;
	}
	if (rc == SQLITE_DONE) {
		sqlite3_finalize(stmt);
		outReason = "Import status row is missing.";
		return true;
	}

	sqlite3_finalize(stmt);
	outReason = "Failed to read materials import status.";
	return setErrorFromDatabase(outReason);
}

bool BrushDatabaseCatalogRepository::markMaterialsImportComplete(const wxString &source) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}
	if (isReadOnly()) {
		return setError("SQLite materials import marker requires a read-write connection.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("INSERT OR REPLACE INTO import_status(id, completed, completed_at, source) "
				 "VALUES (1, 1, strftime('%s','now'), ?);", &stmt)) {
		return false;
	}

	sqlite3_bind_text(stmt, 1, source.utf8_str(), -1, SQLITE_TRANSIENT);
	const int rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		return setErrorFromDatabase("Failed to write materials import status");
	}

	return true;
}

int BrushDatabaseCatalogRepository::getExpectedSchemaVersion() const {
	return kBrushDatabaseSchemaVersion;
}

bool BrushDatabaseBrushRepository::resolveGroundReferenceNames() {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	if (!execute("UPDATE ground_brush_borders "
				 "SET target_brush_id = ("
				 "SELECT id FROM brushes b "
				 "WHERE b.name = ground_brush_borders.target_brush_name AND b.type = 'ground' "
				 "LIMIT 1"
				 ") "
				 "WHERE target_brush_name <> '' AND target_mode = 'brush';")) {
		return false;
	}

	if (!execute("UPDATE brush_links "
				   "SET target_brush_id = ("
				   "SELECT id FROM brushes b "
				   "WHERE b.name = brush_links.target_brush_name "
				   "LIMIT 1"
				   ") "
				   "WHERE target_brush_name <> '' AND target_brush_name <> 'all';")) {
		return false;
	}

	return execute("UPDATE tileset_brush_entries "
				   "SET brush_id = ("
				   "SELECT id FROM brushes b "
				   "WHERE b.name = tileset_brush_entries.brush_name "
				   "LIMIT 1"
				   ") "
				   "WHERE brush_name <> '' AND brush_id IS NULL;");
}

bool BrushDatabaseSession::execute(const wxString &sql) {
	char* errorMessage = nullptr;
	const int rc = sqlite3_exec(connection_, sql.utf8_str(), nullptr, nullptr, &errorMessage);
	if (rc != SQLITE_OK) {
		const wxString detail = errorMessage ? wxString::FromUTF8(errorMessage) : wxString();
		sqlite3_free(errorMessage);
		lastError_ = "SQLite exec failed: " + detail;
		lastSqliteErrorCode_ = rc;
		lastSqliteExtendedErrorCode_ = connection_ ? sqlite3_extended_errcode(connection_) : rc;
		spdlog::error("[BrushDatabase] {}", lastError_.ToStdString());
		return false;
	}
	return true;
}

bool BrushDatabaseSession::prepare(const char* sql, sqlite3_stmt** stmt) {
	const int rc = sqlite3_prepare_v2(connection_, sql, -1, stmt, nullptr);
	if (rc != SQLITE_OK) {
		return setErrorFromDatabase("Failed to prepare SQLite statement");
	}
	return true;
}

bool BrushDatabaseSession::beginTransaction() {
	if (readOnly_) {
		return setError("SQLite transaction requires a read-write connection.");
	}

	if (transactionDepth_ == 0) {
		if (!execute("BEGIN IMMEDIATE TRANSACTION;")) {
			return false;
		}
		transactionDepth_ = 1;
		savepointIds_.clear();
		return true;
	}

	const int savepointId = nextSavepointId_++;
	const wxString savepointName = MakeTransactionSavepointName(savepointId);
	if (!execute("SAVEPOINT " + savepointName + ";")) {
		return false;
	}

	savepointIds_.push_back(savepointId);
	++transactionDepth_;
	return true;
}

bool BrushDatabaseSession::commitTransaction() {
	if (readOnly_) {
		return setError("SQLite transaction requires a read-write connection.");
	}
	if (transactionDepth_ <= 0) {
		return setError("SQLite commit requested without an active transaction.");
	}

	if (transactionDepth_ == 1) {
		if (!execute("COMMIT;")) {
			return false;
		}
		transactionDepth_ = 0;
		savepointIds_.clear();
		return true;
	}

	const int savepointId = savepointIds_.back();
	const wxString savepointName = MakeTransactionSavepointName(savepointId);
	if (!execute("RELEASE SAVEPOINT " + savepointName + ";")) {
		return false;
	}

	savepointIds_.pop_back();
	--transactionDepth_;
	return true;
}

bool BrushDatabaseSession::rollbackTransaction() {
	if (readOnly_) {
		return setError("SQLite transaction requires a read-write connection.");
	}
	if (transactionDepth_ <= 0) {
		return setError("SQLite rollback requested without an active transaction.");
	}

	if (transactionDepth_ == 1) {
		if (!execute("ROLLBACK;")) {
			return false;
		}
		transactionDepth_ = 0;
		savepointIds_.clear();
		return true;
	}

	const int savepointId = savepointIds_.back();
	const wxString savepointName = MakeTransactionSavepointName(savepointId);
	if (!execute("ROLLBACK TO SAVEPOINT " + savepointName + ";")) {
		return false;
	}
	if (!execute("RELEASE SAVEPOINT " + savepointName + ";")) {
		return false;
	}

	savepointIds_.pop_back();
	--transactionDepth_;
	return true;
}

bool BrushDatabaseSession::setError(const wxString &message) {
	lastError_ = message;
	lastSqliteErrorCode_ = 0;
	lastSqliteExtendedErrorCode_ = 0;
	spdlog::error("[BrushDatabase] {}", lastError_.ToStdString());
	return false;
}

bool BrushDatabaseSession::setErrorFromDatabase(const wxString &prefix) {
	if (connection_) {
		lastSqliteErrorCode_ = sqlite3_errcode(connection_);
		lastSqliteExtendedErrorCode_ = sqlite3_extended_errcode(connection_);
	} else {
		lastSqliteErrorCode_ = 0;
		lastSqliteExtendedErrorCode_ = 0;
	}
	const wxString dbMessage = connection_ ? ToWxString(sqlite3_errmsg(connection_)) : wxString("No SQLite connection");
	lastError_ = prefix + ": " + dbMessage;
	spdlog::error("[BrushDatabase] {}", lastError_.ToStdString());
	return false;
}
