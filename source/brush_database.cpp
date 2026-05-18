#include "main.h"

#include "brush_database.h"

BrushDatabase g_brush_database;

namespace {
constexpr int kBrushDatabaseSchemaVersion = 6;

static wxString ToWxString(const char* value) {
	return value ? wxString::FromUTF8(value) : wxString();
}

static void ReadBrushRecordFromStatement(sqlite3_stmt* stmt, BrushRecord &outBrush) {
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
}
} // namespace

BrushDatabase::BrushDatabase() = default;

BrushDatabase::~BrushDatabase() {
	close();
}

bool BrushDatabase::initialize(const wxString &databasePath) {
	if (!open(databasePath)) {
		return false;
	}
	return initializeSchema();
}

bool BrushDatabase::open(const wxString &databasePath) {
	if (connection_ && databasePath_ == databasePath) {
		return true;
	}

	close();

	wxFileName dbFile(databasePath);
	if (!dbFile.DirExists() && !dbFile.Mkdir(0755, wxPATH_MKDIR_FULL)) {
		return setError("Failed to create SQLite directory: " + dbFile.GetPath());
	}

	const int rc = sqlite3_open(dbFile.GetFullPath().utf8_str(), &connection_);
	if (rc != SQLITE_OK) {
		const wxString dbError = ToWxString(sqlite3_errmsg(connection_));
		close();
		return setError("Failed to open SQLite database: " + dbError);
	}

	databasePath_ = dbFile.GetFullPath();
	lastError_.clear();

	if (!execute("PRAGMA foreign_keys = ON;")) {
		close();
		return false;
	}
	if (!execute("PRAGMA busy_timeout = 3000;")) {
		close();
		return false;
	}

	spdlog::info("SQLite brush database opened: {}", databasePath_.ToStdString());
	return true;
}

void BrushDatabase::close() {
	if (connection_) {
		sqlite3_close(connection_);
		connection_ = nullptr;
	}
	databasePath_.clear();
}

bool BrushDatabase::isOpen() const {
	return connection_ != nullptr;
}

const wxString &BrushDatabase::getDatabasePath() const {
	return databasePath_;
}

const wxString &BrushDatabase::getLastError() const {
	return lastError_;
}

bool BrushDatabase::ensureSchemaVersionTable() {
	if (!execute("CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL);")) {
		return false;
	}
	return execute("INSERT INTO schema_version(version) "
	               "SELECT 0 WHERE NOT EXISTS (SELECT 1 FROM schema_version);");
}

bool BrushDatabase::getSchemaVersion(int &version) {
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

bool BrushDatabase::setSchemaVersion(int version) {
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

bool BrushDatabase::columnExists(const wxString &tableName, const wxString &columnName, bool &exists) {
	exists = false;

	sqlite3_stmt* stmt = nullptr;
	const wxString sql = "PRAGMA table_info(" + tableName + ");";
	if (!prepare(sql.utf8_str(), &stmt)) {
		return false;
	}

	for (;;) {
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
			break;
		}
	}

	sqlite3_finalize(stmt);
	return true;
}

bool BrushDatabase::migrateToVersion1() {
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

bool BrushDatabase::migrateToVersion2() {
	const auto addColumnIfMissing = [this](const wxString &tableName, const wxString &columnName, const wxString &definition) -> bool {
		bool exists = false;
		if (!columnExists(tableName, columnName, exists)) {
			return false;
		}
		if (exists) {
			return true;
		}
		return execute("ALTER TABLE " + tableName + " ADD COLUMN " + definition + ";");
	};

	if (!addColumnIfMissing("brushes", "source_file", "source_file TEXT NOT NULL DEFAULT ''")) {
		return false;
	}
	if (!addColumnIfMissing("brushes", "server_look_id", "server_look_id INTEGER NOT NULL DEFAULT 0")) {
		return false;
	}
	if (!addColumnIfMissing("brushes", "draggable", "draggable INTEGER NOT NULL DEFAULT 0 CHECK(draggable IN (0, 1))")) {
		return false;
	}
	if (!addColumnIfMissing("brushes", "on_blocking", "on_blocking INTEGER NOT NULL DEFAULT 0 CHECK(on_blocking IN (0, 1))")) {
		return false;
	}
	if (!addColumnIfMissing("brushes", "on_duplicate", "on_duplicate INTEGER NOT NULL DEFAULT 0 CHECK(on_duplicate IN (0, 1))")) {
		return false;
	}
	if (!addColumnIfMissing("brushes", "redo_borders", "redo_borders INTEGER NOT NULL DEFAULT 0 CHECK(redo_borders IN (0, 1))")) {
		return false;
	}
	if (!addColumnIfMissing("brushes", "randomize", "randomize INTEGER NOT NULL DEFAULT 0 CHECK(randomize IN (0, 1))")) {
		return false;
	}
	if (!addColumnIfMissing("brushes", "one_size", "one_size INTEGER NOT NULL DEFAULT 0 CHECK(one_size IN (0, 1))")) {
		return false;
	}
	if (!addColumnIfMissing("brushes", "solo_optional", "solo_optional INTEGER NOT NULL DEFAULT 0 CHECK(solo_optional IN (0, 1))")) {
		return false;
	}
	if (!addColumnIfMissing("brushes", "thickness", "thickness INTEGER NOT NULL DEFAULT 0")) {
		return false;
	}
	if (!addColumnIfMissing("brushes", "thickness_ceiling", "thickness_ceiling INTEGER NOT NULL DEFAULT 0")) {
		return false;
	}

	if (!execute("CREATE INDEX IF NOT EXISTS idx_brushes_name ON brushes(name);")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS border_sets ("
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
	             ");")) {
		return false;
	}

	if (!execute("CREATE INDEX IF NOT EXISTS idx_border_sets_owner_scope "
	             "ON border_sets(owner_brush_id, border_scope);")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS border_set_items ("
	             "border_set_id INTEGER NOT NULL,"
	             "edge TEXT NOT NULL,"
	             "item_id INTEGER NOT NULL,"
	             "sort_order INTEGER NOT NULL DEFAULT 0,"
	             "PRIMARY KEY (border_set_id, edge, item_id),"
	             "FOREIGN KEY (border_set_id) REFERENCES border_sets(id) ON DELETE CASCADE"
	             ");")) {
		return false;
	}

	if (!execute("CREATE INDEX IF NOT EXISTS idx_border_set_items_item "
	             "ON border_set_items(item_id);")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS ground_brush_borders ("
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
	             ");")) {
		return false;
	}

	if (!execute("CREATE INDEX IF NOT EXISTS idx_ground_brush_borders_brush "
	             "ON ground_brush_borders(brush_id, border_role, align, sort_order);")) {
		return false;
	}

	if (!execute("CREATE INDEX IF NOT EXISTS idx_ground_brush_borders_target "
	             "ON ground_brush_borders(target_brush_name, target_mode);")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS ground_border_cases ("
	             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
	             "ground_brush_border_id INTEGER NOT NULL,"
	             "sort_order INTEGER NOT NULL DEFAULT 0,"
	             "FOREIGN KEY (ground_brush_border_id) REFERENCES ground_brush_borders(id) ON DELETE CASCADE"
	             ");")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS ground_border_case_conditions ("
	             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
	             "ground_border_case_id INTEGER NOT NULL,"
	             "condition_type TEXT NOT NULL,"
	             "match_value INTEGER NOT NULL DEFAULT 0,"
	             "edge TEXT NOT NULL DEFAULT '',"
	             "sort_order INTEGER NOT NULL DEFAULT 0,"
	             "FOREIGN KEY (ground_border_case_id) REFERENCES ground_border_cases(id) ON DELETE CASCADE"
	             ");")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS ground_border_case_actions ("
	             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
	             "ground_border_case_id INTEGER NOT NULL,"
	             "action_type TEXT NOT NULL,"
	             "target_value INTEGER NOT NULL DEFAULT 0,"
	             "edge TEXT NOT NULL DEFAULT '',"
	             "replacement_value INTEGER NOT NULL DEFAULT 0,"
	             "sort_order INTEGER NOT NULL DEFAULT 0,"
	             "FOREIGN KEY (ground_border_case_id) REFERENCES ground_border_cases(id) ON DELETE CASCADE"
	             ");")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS brush_links ("
	             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
	             "brush_id INTEGER NOT NULL,"
	             "target_brush_id INTEGER,"
	             "target_brush_name TEXT NOT NULL DEFAULT '',"
	             "relation_type TEXT NOT NULL,"
	             "sort_order INTEGER NOT NULL DEFAULT 0,"
	             "FOREIGN KEY (brush_id) REFERENCES brushes(id) ON DELETE CASCADE,"
	             "FOREIGN KEY (target_brush_id) REFERENCES brushes(id) ON DELETE SET NULL"
	             ");")) {
		return false;
	}

	if (!execute("CREATE INDEX IF NOT EXISTS idx_brush_links_type "
	             "ON brush_links(brush_id, relation_type, sort_order);")) {
		return false;
	}

	if (!execute("CREATE INDEX IF NOT EXISTS idx_brush_links_target_name "
	             "ON brush_links(target_brush_name);")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS wall_parts ("
	             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
	             "brush_id INTEGER NOT NULL,"
	             "part_type TEXT NOT NULL,"
	             "sort_order INTEGER NOT NULL DEFAULT 0,"
	             "FOREIGN KEY (brush_id) REFERENCES brushes(id) ON DELETE CASCADE,"
	             "UNIQUE (brush_id, part_type)"
	             ");")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS wall_part_items ("
	             "wall_part_id INTEGER NOT NULL,"
	             "item_id INTEGER NOT NULL,"
	             "chance INTEGER NOT NULL DEFAULT 1 CHECK(chance >= 0),"
	             "sort_order INTEGER NOT NULL DEFAULT 0,"
	             "PRIMARY KEY (wall_part_id, item_id),"
	             "FOREIGN KEY (wall_part_id) REFERENCES wall_parts(id) ON DELETE CASCADE"
	             ");")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS wall_part_doors ("
	             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
	             "wall_part_id INTEGER NOT NULL,"
	             "item_id INTEGER NOT NULL,"
	             "door_type TEXT NOT NULL DEFAULT 'normal',"
	             "is_open INTEGER NOT NULL DEFAULT 0 CHECK(is_open IN (0, 1)),"
	             "wall_hate_me INTEGER NOT NULL DEFAULT 0 CHECK(wall_hate_me IN (0, 1)),"
	             "sort_order INTEGER NOT NULL DEFAULT 0,"
	             "FOREIGN KEY (wall_part_id) REFERENCES wall_parts(id) ON DELETE CASCADE"
	             ");")) {
		return false;
	}

	if (!execute("CREATE INDEX IF NOT EXISTS idx_wall_part_doors_part "
	             "ON wall_part_doors(wall_part_id, sort_order);")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS carpet_nodes ("
	             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
	             "brush_id INTEGER NOT NULL,"
	             "align TEXT NOT NULL,"
	             "sort_order INTEGER NOT NULL DEFAULT 0,"
	             "FOREIGN KEY (brush_id) REFERENCES brushes(id) ON DELETE CASCADE"
	             ");")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS carpet_node_items ("
	             "carpet_node_id INTEGER NOT NULL,"
	             "item_id INTEGER NOT NULL,"
	             "chance INTEGER NOT NULL DEFAULT 1 CHECK(chance >= 0),"
	             "sort_order INTEGER NOT NULL DEFAULT 0,"
	             "PRIMARY KEY (carpet_node_id, item_id),"
	             "FOREIGN KEY (carpet_node_id) REFERENCES carpet_nodes(id) ON DELETE CASCADE"
	             ");")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS table_nodes ("
	             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
	             "brush_id INTEGER NOT NULL,"
	             "align TEXT NOT NULL,"
	             "sort_order INTEGER NOT NULL DEFAULT 0,"
	             "FOREIGN KEY (brush_id) REFERENCES brushes(id) ON DELETE CASCADE"
	             ");")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS table_node_items ("
	             "table_node_id INTEGER NOT NULL,"
	             "item_id INTEGER NOT NULL,"
	             "chance INTEGER NOT NULL DEFAULT 1 CHECK(chance >= 0),"
	             "sort_order INTEGER NOT NULL DEFAULT 0,"
	             "PRIMARY KEY (table_node_id, item_id),"
	             "FOREIGN KEY (table_node_id) REFERENCES table_nodes(id) ON DELETE CASCADE"
	             ");")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS doodad_alternatives ("
	             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
	             "brush_id INTEGER NOT NULL,"
	             "sort_order INTEGER NOT NULL DEFAULT 0,"
	             "FOREIGN KEY (brush_id) REFERENCES brushes(id) ON DELETE CASCADE"
	             ");")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS doodad_single_items ("
	             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
	             "doodad_alternative_id INTEGER NOT NULL,"
	             "item_id INTEGER NOT NULL,"
	             "chance INTEGER NOT NULL DEFAULT 1 CHECK(chance >= 0),"
	             "sort_order INTEGER NOT NULL DEFAULT 0,"
	             "FOREIGN KEY (doodad_alternative_id) REFERENCES doodad_alternatives(id) ON DELETE CASCADE"
	             ");")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS doodad_composites ("
	             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
	             "doodad_alternative_id INTEGER NOT NULL,"
	             "chance INTEGER NOT NULL DEFAULT 1 CHECK(chance >= 0),"
	             "sort_order INTEGER NOT NULL DEFAULT 0,"
	             "FOREIGN KEY (doodad_alternative_id) REFERENCES doodad_alternatives(id) ON DELETE CASCADE"
	             ");")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS doodad_composite_tiles ("
	             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
	             "doodad_composite_id INTEGER NOT NULL,"
	             "offset_x INTEGER NOT NULL DEFAULT 0,"
	             "offset_y INTEGER NOT NULL DEFAULT 0,"
	             "offset_z INTEGER NOT NULL DEFAULT 0,"
	             "sort_order INTEGER NOT NULL DEFAULT 0,"
	             "FOREIGN KEY (doodad_composite_id) REFERENCES doodad_composites(id) ON DELETE CASCADE"
	             ");")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS doodad_composite_tile_items ("
	             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
	             "doodad_composite_tile_id INTEGER NOT NULL,"
	             "item_id INTEGER NOT NULL,"
	             "sort_order INTEGER NOT NULL DEFAULT 0,"
	             "FOREIGN KEY (doodad_composite_tile_id) REFERENCES doodad_composite_tiles(id) ON DELETE CASCADE"
	             ");")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS tilesets ("
	             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
	             "name TEXT NOT NULL UNIQUE,"
	             "source_file TEXT NOT NULL DEFAULT '',"
	             "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
	             "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
	             ");")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS tileset_sections ("
	             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
	             "tileset_id INTEGER NOT NULL,"
	             "section_type TEXT NOT NULL,"
	             "sort_order INTEGER NOT NULL DEFAULT 0,"
	             "FOREIGN KEY (tileset_id) REFERENCES tilesets(id) ON DELETE CASCADE,"
	             "UNIQUE (tileset_id, section_type)"
	             ");")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS tileset_brush_entries ("
	             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
	             "tileset_section_id INTEGER NOT NULL,"
	             "brush_id INTEGER,"
	             "brush_name TEXT NOT NULL DEFAULT '',"
	             "after_brush_name TEXT NOT NULL DEFAULT '',"
	             "sort_order INTEGER NOT NULL DEFAULT 0,"
	             "FOREIGN KEY (tileset_section_id) REFERENCES tileset_sections(id) ON DELETE CASCADE,"
	             "FOREIGN KEY (brush_id) REFERENCES brushes(id) ON DELETE SET NULL"
	             ");")) {
		return false;
	}

	if (!execute("CREATE INDEX IF NOT EXISTS idx_tileset_brush_entries_section "
	             "ON tileset_brush_entries(tileset_section_id, sort_order);")) {
		return false;
	}

	return execute("CREATE INDEX IF NOT EXISTS idx_tileset_brush_entries_name "
	               "ON tileset_brush_entries(brush_name);");
}

bool BrushDatabase::initializeSchema() {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	if (!beginTransaction()) {
		return false;
	}

	if (!ensureSchemaVersionTable()) {
		rollbackTransaction();
		return false;
	}

	int version = 0;
	if (!getSchemaVersion(version)) {
		rollbackTransaction();
		return false;
	}
	if (version > kBrushDatabaseSchemaVersion) {
		rollbackTransaction();
		return setError(wxString::Format("SQLite schema version %d is newer than supported version %d.", version, kBrushDatabaseSchemaVersion));
	}

	if (version < 1) {
		if (!migrateToVersion1() || !setSchemaVersion(1)) {
			rollbackTransaction();
			return false;
		}
		version = 1;
	}

	if (version < 2) {
		if (!migrateToVersion2() || !setSchemaVersion(2)) {
			rollbackTransaction();
			return false;
		}
		version = 2;
	}

	if (version < 3) {
		if (!migrateToVersion3() || !setSchemaVersion(3)) {
			rollbackTransaction();
			return false;
		}
		version = 3;
	}

	if (version < 4) {
		if (!migrateToVersion4() || !setSchemaVersion(4)) {
			rollbackTransaction();
			return false;
		}
		version = 4;
	}

	if (version < 5) {
		if (!migrateToVersion5() || !setSchemaVersion(5)) {
			rollbackTransaction();
			return false;
		}
		version = 5;
	}

	if (version < 6) {
		if (!migrateToVersion6() || !setSchemaVersion(6)) {
			rollbackTransaction();
			return false;
		}
		version = 6;
	}

	if (!commitTransaction()) {
		rollbackTransaction();
		return false;
	}

	spdlog::info("SQLite brush database schema initialized at version {}", version);
	return true;
}

bool BrushDatabase::migrateToVersion3() {
	const wxString recreateSql =
		"DROP TABLE IF EXISTS brush_items;"
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

bool BrushDatabase::migrateToVersion4() {
	const wxString recreateSql =
		"DROP TABLE IF EXISTS brush_items;"
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

bool BrushDatabase::migrateToVersion5() {
	const wxString recreateSql =
		"DROP TABLE IF EXISTS tileset_brush_entries;"
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
	return execute(recreateSql);
}

bool BrushDatabase::migrateToVersion6() {
	const wxString recreateSql =
		"DROP TABLE IF EXISTS tileset_brush_entries;"
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
	return execute(recreateSql);
}

bool BrushDatabase::testDatabaseConnection() {
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

bool BrushDatabase::testBasicCRUD() {
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

bool BrushDatabase::insertBrush(const BrushRecord &brush, int64_t &insertedId) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("INSERT INTO brushes("
	             "name, type, look_id, z_order, source_file, server_look_id, "
	             "draggable, on_blocking, on_duplicate, redo_borders, randomize, "
	             "one_size, solo_optional, thickness, thickness_ceiling, updated_at"
	             ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP);", &stmt)) {
		return false;
	}

	sqlite3_bind_text(stmt, 1, brush.name.utf8_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, brush.type.utf8_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 3, brush.lookId);
	sqlite3_bind_int(stmt, 4, brush.zOrder);
	sqlite3_bind_text(stmt, 5, brush.sourceFile.utf8_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 6, brush.serverLookId);
	sqlite3_bind_int(stmt, 7, brush.draggable ? 1 : 0);
	sqlite3_bind_int(stmt, 8, brush.onBlocking ? 1 : 0);
	sqlite3_bind_int(stmt, 9, brush.onDuplicate ? 1 : 0);
	sqlite3_bind_int(stmt, 10, brush.redoBorders ? 1 : 0);
	sqlite3_bind_int(stmt, 11, brush.randomize ? 1 : 0);
	sqlite3_bind_int(stmt, 12, brush.oneSize ? 1 : 0);
	sqlite3_bind_int(stmt, 13, brush.soloOptional ? 1 : 0);
	sqlite3_bind_int(stmt, 14, brush.thickness);
	sqlite3_bind_int(stmt, 15, brush.thicknessCeiling);

	const int rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		return setErrorFromDatabase("Failed to insert brush");
	}

	insertedId = sqlite3_last_insert_rowid(connection_);
	return true;
}

bool BrushDatabase::upsertBrush(const BrushRecord &brush, int64_t &brushId) {
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

bool BrushDatabase::getBrushById(int64_t brushId, BrushRecord &outBrush) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("SELECT id, name, type, look_id, z_order, source_file, server_look_id, "
	             "draggable, on_blocking, on_duplicate, redo_borders, randomize, "
	             "one_size, solo_optional, thickness, thickness_ceiling "
	             "FROM brushes WHERE id = ?;", &stmt)) {
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

bool BrushDatabase::listBrushesByType(const wxString &type, std::vector<BrushRecord> &outBrushes) {
	outBrushes.clear();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("SELECT id, name, type, look_id, z_order, source_file, server_look_id, "
	             "draggable, on_blocking, on_duplicate, redo_borders, randomize, "
	             "one_size, solo_optional, thickness, thickness_ceiling "
	             "FROM brushes WHERE type = ? ORDER BY name ASC, id ASC;", &stmt)) {
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

bool BrushDatabase::findBrushByNameAndType(const wxString &name, const wxString &type, BrushRecord &outBrush) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("SELECT id, name, type, look_id, z_order, source_file, server_look_id, "
	             "draggable, on_blocking, on_duplicate, redo_borders, randomize, "
	             "one_size, solo_optional, thickness, thickness_ceiling "
	             "FROM brushes WHERE name = ? AND type = ? LIMIT 1;", &stmt)) {
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

bool BrushDatabase::getCompleteBrushById(int64_t brushId, BrushStorageRecord &outBrush) {
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

bool BrushDatabase::updateBrush(const BrushRecord &brush) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("UPDATE brushes "
	             "SET name = ?, type = ?, look_id = ?, z_order = ?, source_file = ?, server_look_id = ?, "
	             "draggable = ?, on_blocking = ?, on_duplicate = ?, redo_borders = ?, randomize = ?, "
	             "one_size = ?, solo_optional = ?, thickness = ?, thickness_ceiling = ?, updated_at = CURRENT_TIMESTAMP "
	             "WHERE id = ?;", &stmt)) {
		return false;
	}

	sqlite3_bind_text(stmt, 1, brush.name.utf8_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, brush.type.utf8_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 3, brush.lookId);
	sqlite3_bind_int(stmt, 4, brush.zOrder);
	sqlite3_bind_text(stmt, 5, brush.sourceFile.utf8_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 6, brush.serverLookId);
	sqlite3_bind_int(stmt, 7, brush.draggable ? 1 : 0);
	sqlite3_bind_int(stmt, 8, brush.onBlocking ? 1 : 0);
	sqlite3_bind_int(stmt, 9, brush.onDuplicate ? 1 : 0);
	sqlite3_bind_int(stmt, 10, brush.redoBorders ? 1 : 0);
	sqlite3_bind_int(stmt, 11, brush.randomize ? 1 : 0);
	sqlite3_bind_int(stmt, 12, brush.oneSize ? 1 : 0);
	sqlite3_bind_int(stmt, 13, brush.soloOptional ? 1 : 0);
	sqlite3_bind_int(stmt, 14, brush.thickness);
	sqlite3_bind_int(stmt, 15, brush.thicknessCeiling);
	sqlite3_bind_int64(stmt, 16, brush.id);

	const int rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		return setErrorFromDatabase("Failed to update brush");
	}

	return sqlite3_changes(connection_) > 0 || setError(wxString::Format("Brush %lld was not updated.", static_cast<long long>(brush.id)));
}

bool BrushDatabase::deleteBrush(int64_t brushId) {
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

bool BrushDatabase::deleteBrushesByType(const wxString &type) {
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

bool BrushDatabase::replaceBrushItems(int64_t brushId, const std::vector<BrushItemRecord> &items) {
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

bool BrushDatabase::getBrushItems(int64_t brushId, std::vector<BrushItemRecord> &outItems) {
	outItems.clear();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("SELECT brush_id, item_id, chance, sort_order "
	             "FROM brush_items WHERE brush_id = ? ORDER BY sort_order ASC, id ASC;", &stmt)) {
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

bool BrushDatabase::upsertBorderSet(const BorderSetRecord &borderSet, int64_t &borderSetId) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	if (borderSet.xmlBorderId > 0) {
		BorderSetRecord existing;
		if (findBorderSetByXmlBorderId(borderSet.xmlBorderId, existing)) {
			sqlite3_stmt* updateStmt = nullptr;
			if (!prepare("UPDATE border_sets SET owner_brush_id = ?, border_scope = ?, border_type = ?, "
			             "border_group = ?, ground_equivalent = ?, source_file = ?, updated_at = CURRENT_TIMESTAMP "
			             "WHERE id = ?;", &updateStmt)) {
				return false;
			}

			if (borderSet.ownerBrushId > 0) {
				sqlite3_bind_int64(updateStmt, 1, borderSet.ownerBrushId);
			} else {
				sqlite3_bind_null(updateStmt, 1);
			}
			sqlite3_bind_text(updateStmt, 2, borderSet.borderScope.utf8_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(updateStmt, 3, borderSet.borderType.utf8_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(updateStmt, 4, borderSet.borderGroup);
			sqlite3_bind_int(updateStmt, 5, borderSet.groundEquivalent);
			sqlite3_bind_text(updateStmt, 6, borderSet.sourceFile.utf8_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64(updateStmt, 7, existing.id);

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
	             "VALUES (?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP);", &stmt)) {
		return false;
	}

	if (borderSet.xmlBorderId > 0) {
		sqlite3_bind_int(stmt, 1, borderSet.xmlBorderId);
	} else {
		sqlite3_bind_null(stmt, 1);
	}
	if (borderSet.ownerBrushId > 0) {
		sqlite3_bind_int64(stmt, 2, borderSet.ownerBrushId);
	} else {
		sqlite3_bind_null(stmt, 2);
	}
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

bool BrushDatabase::findBorderSetByXmlBorderId(int xmlBorderId, BorderSetRecord &outBorderSet) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("SELECT id, xml_border_id, owner_brush_id, border_scope, border_type, border_group, ground_equivalent, source_file "
	             "FROM border_sets WHERE xml_border_id = ? LIMIT 1;", &stmt)) {
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
	outBorderSet.ownerBrushId = sqlite3_column_type(stmt, 2) == SQLITE_NULL ? 0 : sqlite3_column_int64(stmt, 2);
	outBorderSet.borderScope = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
	outBorderSet.borderType = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));
	outBorderSet.borderGroup = sqlite3_column_int(stmt, 5);
	outBorderSet.groundEquivalent = sqlite3_column_int(stmt, 6);
	outBorderSet.sourceFile = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7)));
	sqlite3_finalize(stmt);
	return true;
}

bool BrushDatabase::replaceBorderSetItems(int64_t borderSetId, const std::vector<BorderSetItemRecord> &items) {
	if (!isOpen()) {
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

bool BrushDatabase::deleteBorderSetsByScope(const wxString &borderScope) {
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

bool BrushDatabase::deleteOwnedBorderSetsForBrush(int64_t brushId) {
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

bool BrushDatabase::replaceGroundBrushBorders(int64_t brushId, const std::vector<GroundBrushBorderRecord> &borders) {
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
	             ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);", &insertBorderStmt)) {
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
	             "VALUES (?, ?, ?, ?, ?);", &insertConditionStmt)) {
		sqlite3_finalize(insertBorderStmt);
		sqlite3_finalize(insertCaseStmt);
		rollbackTransaction();
		return false;
	}

	sqlite3_stmt* insertActionStmt = nullptr;
	if (!prepare("INSERT INTO ground_border_case_actions(ground_border_case_id, action_type, target_value, edge, replacement_value, sort_order) "
	             "VALUES (?, ?, ?, ?, ?, ?);", &insertActionStmt)) {
		sqlite3_finalize(insertBorderStmt);
		sqlite3_finalize(insertCaseStmt);
		sqlite3_finalize(insertConditionStmt);
		rollbackTransaction();
		return false;
	}

	for (const GroundBrushBorderRecord &border : borders) {
		sqlite3_reset(insertBorderStmt);
		sqlite3_clear_bindings(insertBorderStmt);
		sqlite3_bind_int64(insertBorderStmt, 1, brushId);
		sqlite3_bind_int64(insertBorderStmt, 2, border.borderSetId);
		sqlite3_bind_text(insertBorderStmt, 3, border.borderRole.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(insertBorderStmt, 4, border.align.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(insertBorderStmt, 5, border.targetMode.utf8_str(), -1, SQLITE_TRANSIENT);
		if (border.targetBrushId > 0) {
			sqlite3_bind_int64(insertBorderStmt, 6, border.targetBrushId);
		} else {
			sqlite3_bind_null(insertBorderStmt, 6);
		}
		sqlite3_bind_text(insertBorderStmt, 7, border.targetBrushName.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int(insertBorderStmt, 8, border.superBorder ? 1 : 0);
		sqlite3_bind_int(insertBorderStmt, 9, border.sortOrder);
		rc = sqlite3_step(insertBorderStmt);
		if (rc != SQLITE_DONE) {
			sqlite3_finalize(insertBorderStmt);
			sqlite3_finalize(insertCaseStmt);
			sqlite3_finalize(insertConditionStmt);
			sqlite3_finalize(insertActionStmt);
			rollbackTransaction();
			return setErrorFromDatabase("Failed to insert ground brush border");
		}

		const int64_t groundBrushBorderId = sqlite3_last_insert_rowid(connection_);
		for (const GroundBorderCaseRecord &caseRecord : border.cases) {
			sqlite3_reset(insertCaseStmt);
			sqlite3_clear_bindings(insertCaseStmt);
			sqlite3_bind_int64(insertCaseStmt, 1, groundBrushBorderId);
			sqlite3_bind_int(insertCaseStmt, 2, caseRecord.sortOrder);
			rc = sqlite3_step(insertCaseStmt);
			if (rc != SQLITE_DONE) {
				sqlite3_finalize(insertBorderStmt);
				sqlite3_finalize(insertCaseStmt);
				sqlite3_finalize(insertConditionStmt);
				sqlite3_finalize(insertActionStmt);
				rollbackTransaction();
				return setErrorFromDatabase("Failed to insert ground border case");
			}

			const int64_t caseId = sqlite3_last_insert_rowid(connection_);
			for (const GroundBorderCaseConditionRecord &condition : caseRecord.conditions) {
				sqlite3_reset(insertConditionStmt);
				sqlite3_clear_bindings(insertConditionStmt);
				sqlite3_bind_int64(insertConditionStmt, 1, caseId);
				sqlite3_bind_text(insertConditionStmt, 2, condition.conditionType.utf8_str(), -1, SQLITE_TRANSIENT);
				sqlite3_bind_int(insertConditionStmt, 3, condition.matchValue);
				sqlite3_bind_text(insertConditionStmt, 4, condition.edge.utf8_str(), -1, SQLITE_TRANSIENT);
				sqlite3_bind_int(insertConditionStmt, 5, condition.sortOrder);
				rc = sqlite3_step(insertConditionStmt);
				if (rc != SQLITE_DONE) {
					sqlite3_finalize(insertBorderStmt);
					sqlite3_finalize(insertCaseStmt);
					sqlite3_finalize(insertConditionStmt);
					sqlite3_finalize(insertActionStmt);
					rollbackTransaction();
					return setErrorFromDatabase("Failed to insert ground border condition");
				}
			}

			for (const GroundBorderCaseActionRecord &action : caseRecord.actions) {
				sqlite3_reset(insertActionStmt);
				sqlite3_clear_bindings(insertActionStmt);
				sqlite3_bind_int64(insertActionStmt, 1, caseId);
				sqlite3_bind_text(insertActionStmt, 2, action.actionType.utf8_str(), -1, SQLITE_TRANSIENT);
				sqlite3_bind_int(insertActionStmt, 3, action.targetValue);
				sqlite3_bind_text(insertActionStmt, 4, action.edge.utf8_str(), -1, SQLITE_TRANSIENT);
				sqlite3_bind_int(insertActionStmt, 5, action.replacementValue);
				sqlite3_bind_int(insertActionStmt, 6, action.sortOrder);
				rc = sqlite3_step(insertActionStmt);
				if (rc != SQLITE_DONE) {
					sqlite3_finalize(insertBorderStmt);
					sqlite3_finalize(insertCaseStmt);
					sqlite3_finalize(insertConditionStmt);
					sqlite3_finalize(insertActionStmt);
					rollbackTransaction();
					return setErrorFromDatabase("Failed to insert ground border action");
				}
			}
		}
	}

	sqlite3_finalize(insertBorderStmt);
	sqlite3_finalize(insertCaseStmt);
	sqlite3_finalize(insertConditionStmt);
	sqlite3_finalize(insertActionStmt);
	if (!commitTransaction()) {
		rollbackTransaction();
		return false;
	}
	return true;
}

bool BrushDatabase::getGroundBrushBorders(int64_t brushId, std::vector<GroundBrushBorderRecord> &outBorders) {
	outBorders.clear();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* borderStmt = nullptr;
	if (!prepare("SELECT id, border_set_id, border_role, align, target_mode, target_brush_id, target_brush_name, super_border, sort_order "
	             "FROM ground_brush_borders WHERE brush_id = ? ORDER BY sort_order ASC, id ASC;", &borderStmt)) {
		return false;
	}

	sqlite3_stmt* caseStmt = nullptr;
	if (!prepare("SELECT id, sort_order FROM ground_border_cases "
	             "WHERE ground_brush_border_id = ? ORDER BY sort_order ASC, id ASC;", &caseStmt)) {
		sqlite3_finalize(borderStmt);
		return false;
	}

	sqlite3_stmt* conditionStmt = nullptr;
	if (!prepare("SELECT condition_type, match_value, edge, sort_order "
	             "FROM ground_border_case_conditions WHERE ground_border_case_id = ? "
	             "ORDER BY sort_order ASC, id ASC;", &conditionStmt)) {
		sqlite3_finalize(borderStmt);
		sqlite3_finalize(caseStmt);
		return false;
	}

	sqlite3_stmt* actionStmt = nullptr;
	if (!prepare("SELECT action_type, target_value, edge, replacement_value, sort_order "
	             "FROM ground_border_case_actions WHERE ground_border_case_id = ? "
	             "ORDER BY sort_order ASC, id ASC;", &actionStmt)) {
		sqlite3_finalize(borderStmt);
		sqlite3_finalize(caseStmt);
		sqlite3_finalize(conditionStmt);
		return false;
	}

	sqlite3_bind_int64(borderStmt, 1, brushId);

	for (;;) {
		const int borderRc = sqlite3_step(borderStmt);
		if (borderRc == SQLITE_DONE) {
			break;
		}
		if (borderRc != SQLITE_ROW) {
			sqlite3_finalize(borderStmt);
			sqlite3_finalize(caseStmt);
			sqlite3_finalize(conditionStmt);
			sqlite3_finalize(actionStmt);
			return setErrorFromDatabase("Failed to read ground brush borders");
		}

		const int64_t groundBrushBorderId = sqlite3_column_int64(borderStmt, 0);

		GroundBrushBorderRecord border;
		border.borderSetId = sqlite3_column_int64(borderStmt, 1);
		border.borderRole = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(borderStmt, 2)));
		border.align = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(borderStmt, 3)));
		border.targetMode = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(borderStmt, 4)));
		border.targetBrushId = sqlite3_column_type(borderStmt, 5) == SQLITE_NULL ? 0 : sqlite3_column_int64(borderStmt, 5);
		border.targetBrushName = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(borderStmt, 6)));
		border.superBorder = sqlite3_column_int(borderStmt, 7) != 0;
		border.sortOrder = sqlite3_column_int(borderStmt, 8);

		sqlite3_reset(caseStmt);
		sqlite3_clear_bindings(caseStmt);
		sqlite3_bind_int64(caseStmt, 1, groundBrushBorderId);

		for (;;) {
			const int caseRc = sqlite3_step(caseStmt);
			if (caseRc == SQLITE_DONE) {
				break;
			}
			if (caseRc != SQLITE_ROW) {
				sqlite3_finalize(borderStmt);
				sqlite3_finalize(caseStmt);
				sqlite3_finalize(conditionStmt);
				sqlite3_finalize(actionStmt);
				return setErrorFromDatabase("Failed to read ground border cases");
			}

			const int64_t caseId = sqlite3_column_int64(caseStmt, 0);

			GroundBorderCaseRecord caseRecord;
			caseRecord.sortOrder = sqlite3_column_int(caseStmt, 1);

			sqlite3_reset(conditionStmt);
			sqlite3_clear_bindings(conditionStmt);
			sqlite3_bind_int64(conditionStmt, 1, caseId);

			for (;;) {
				const int conditionRc = sqlite3_step(conditionStmt);
				if (conditionRc == SQLITE_DONE) {
					break;
				}
				if (conditionRc != SQLITE_ROW) {
					sqlite3_finalize(borderStmt);
					sqlite3_finalize(caseStmt);
					sqlite3_finalize(conditionStmt);
					sqlite3_finalize(actionStmt);
					return setErrorFromDatabase("Failed to read ground border case conditions");
				}

				GroundBorderCaseConditionRecord condition;
				condition.conditionType = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(conditionStmt, 0)));
				condition.matchValue = sqlite3_column_int(conditionStmt, 1);
				condition.edge = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(conditionStmt, 2)));
				condition.sortOrder = sqlite3_column_int(conditionStmt, 3);
				caseRecord.conditions.push_back(condition);
			}

			sqlite3_reset(actionStmt);
			sqlite3_clear_bindings(actionStmt);
			sqlite3_bind_int64(actionStmt, 1, caseId);

			for (;;) {
				const int actionRc = sqlite3_step(actionStmt);
				if (actionRc == SQLITE_DONE) {
					break;
				}
				if (actionRc != SQLITE_ROW) {
					sqlite3_finalize(borderStmt);
					sqlite3_finalize(caseStmt);
					sqlite3_finalize(conditionStmt);
					sqlite3_finalize(actionStmt);
					return setErrorFromDatabase("Failed to read ground border case actions");
				}

				GroundBorderCaseActionRecord action;
				action.actionType = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(actionStmt, 0)));
				action.targetValue = sqlite3_column_int(actionStmt, 1);
				action.edge = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(actionStmt, 2)));
				action.replacementValue = sqlite3_column_int(actionStmt, 3);
				action.sortOrder = sqlite3_column_int(actionStmt, 4);
				caseRecord.actions.push_back(action);
			}

			border.cases.push_back(caseRecord);
		}

		outBorders.push_back(border);
	}

	sqlite3_finalize(borderStmt);
	sqlite3_finalize(caseStmt);
	sqlite3_finalize(conditionStmt);
	sqlite3_finalize(actionStmt);
	return true;
}

bool BrushDatabase::replaceBrushLinks(int64_t brushId, const std::vector<BrushLinkRecord> &links) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
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
		if (link.targetBrushId > 0) {
			sqlite3_bind_int64(insertStmt, 2, link.targetBrushId);
		} else {
			sqlite3_bind_null(insertStmt, 2);
		}
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

bool BrushDatabase::getBrushLinks(int64_t brushId, std::vector<BrushLinkRecord> &outLinks) {
	outLinks.clear();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("SELECT brush_id, target_brush_id, target_brush_name, relation_type, sort_order "
	             "FROM brush_links WHERE brush_id = ? ORDER BY sort_order ASC, id ASC;", &stmt)) {
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
		link.targetBrushId = sqlite3_column_type(stmt, 1) == SQLITE_NULL ? 0 : sqlite3_column_int64(stmt, 1);
		link.targetBrushName = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
		link.relationType = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
		link.sortOrder = sqlite3_column_int(stmt, 4);
		outLinks.push_back(link);
	}

	sqlite3_finalize(stmt);
	return true;
}

bool BrushDatabase::replaceWallParts(int64_t brushId, const std::vector<WallPartRecord> &parts) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
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
	             "VALUES (?, ?, ?, ?, ?, ?);", &insertDoorStmt)) {
		sqlite3_finalize(insertPartStmt);
		sqlite3_finalize(insertItemStmt);
		rollbackTransaction();
		return false;
	}

	for (const WallPartRecord &part : parts) {
		sqlite3_reset(insertPartStmt);
		sqlite3_clear_bindings(insertPartStmt);
		sqlite3_bind_int64(insertPartStmt, 1, brushId);
		sqlite3_bind_text(insertPartStmt, 2, part.partType.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int(insertPartStmt, 3, part.sortOrder);
		rc = sqlite3_step(insertPartStmt);
		if (rc != SQLITE_DONE) {
			sqlite3_finalize(insertPartStmt);
			sqlite3_finalize(insertItemStmt);
			sqlite3_finalize(insertDoorStmt);
			rollbackTransaction();
			return setErrorFromDatabase("Failed to insert wall part");
		}

		const int64_t wallPartId = sqlite3_last_insert_rowid(connection_);

		for (const WallPartItemRecord &item : part.items) {
			sqlite3_reset(insertItemStmt);
			sqlite3_clear_bindings(insertItemStmt);
			sqlite3_bind_int64(insertItemStmt, 1, wallPartId);
			sqlite3_bind_int(insertItemStmt, 2, item.itemId);
			sqlite3_bind_int(insertItemStmt, 3, item.chance);
			sqlite3_bind_int(insertItemStmt, 4, item.sortOrder);
			rc = sqlite3_step(insertItemStmt);
			if (rc != SQLITE_DONE) {
				sqlite3_finalize(insertPartStmt);
				sqlite3_finalize(insertItemStmt);
				sqlite3_finalize(insertDoorStmt);
				rollbackTransaction();
				return setErrorFromDatabase("Failed to insert wall part item");
			}
		}

		for (const WallPartDoorRecord &door : part.doors) {
			sqlite3_reset(insertDoorStmt);
			sqlite3_clear_bindings(insertDoorStmt);
			sqlite3_bind_int64(insertDoorStmt, 1, wallPartId);
			sqlite3_bind_int(insertDoorStmt, 2, door.itemId);
			sqlite3_bind_text(insertDoorStmt, 3, door.doorType.utf8_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(insertDoorStmt, 4, door.isOpen ? 1 : 0);
			sqlite3_bind_int(insertDoorStmt, 5, door.wallHateMe ? 1 : 0);
			sqlite3_bind_int(insertDoorStmt, 6, door.sortOrder);
			rc = sqlite3_step(insertDoorStmt);
			if (rc != SQLITE_DONE) {
				sqlite3_finalize(insertPartStmt);
				sqlite3_finalize(insertItemStmt);
				sqlite3_finalize(insertDoorStmt);
				rollbackTransaction();
				return setErrorFromDatabase("Failed to insert wall part door");
			}
		}
	}

	sqlite3_finalize(insertPartStmt);
	sqlite3_finalize(insertItemStmt);
	sqlite3_finalize(insertDoorStmt);
	if (!commitTransaction()) {
		rollbackTransaction();
		return false;
	}
	return true;
}

bool BrushDatabase::getWallParts(int64_t brushId, std::vector<WallPartRecord> &outParts) {
	outParts.clear();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* partStmt = nullptr;
	if (!prepare("SELECT id, part_type, sort_order FROM wall_parts "
	             "WHERE brush_id = ? ORDER BY sort_order ASC, id ASC;", &partStmt)) {
		return false;
	}

	sqlite3_stmt* itemStmt = nullptr;
	if (!prepare("SELECT item_id, chance, sort_order FROM wall_part_items "
	             "WHERE wall_part_id = ? ORDER BY sort_order ASC, id ASC;", &itemStmt)) {
		sqlite3_finalize(partStmt);
		return false;
	}

	sqlite3_stmt* doorStmt = nullptr;
	if (!prepare("SELECT item_id, door_type, is_open, wall_hate_me, sort_order FROM wall_part_doors "
	             "WHERE wall_part_id = ? ORDER BY sort_order ASC, id ASC;", &doorStmt)) {
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

bool BrushDatabase::replaceCarpetNodes(int64_t brushId, const std::vector<CarpetNodeRecord> &nodes) {
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

	for (const CarpetNodeRecord &node : nodes) {
		sqlite3_reset(insertNodeStmt);
		sqlite3_clear_bindings(insertNodeStmt);
		sqlite3_bind_int64(insertNodeStmt, 1, brushId);
		sqlite3_bind_text(insertNodeStmt, 2, node.align.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int(insertNodeStmt, 3, node.sortOrder);
		rc = sqlite3_step(insertNodeStmt);
		if (rc != SQLITE_DONE) {
			sqlite3_finalize(insertNodeStmt);
			sqlite3_finalize(insertItemStmt);
			rollbackTransaction();
			return setErrorFromDatabase("Failed to insert carpet node");
		}

		const int64_t nodeId = sqlite3_last_insert_rowid(connection_);
		for (const CarpetNodeItemRecord &item : node.items) {
			sqlite3_reset(insertItemStmt);
			sqlite3_clear_bindings(insertItemStmt);
			sqlite3_bind_int64(insertItemStmt, 1, nodeId);
			sqlite3_bind_int(insertItemStmt, 2, item.itemId);
			sqlite3_bind_int(insertItemStmt, 3, item.chance);
			sqlite3_bind_int(insertItemStmt, 4, item.sortOrder);
			rc = sqlite3_step(insertItemStmt);
			if (rc != SQLITE_DONE) {
				sqlite3_finalize(insertNodeStmt);
				sqlite3_finalize(insertItemStmt);
				rollbackTransaction();
				return setErrorFromDatabase("Failed to insert carpet node item");
			}
		}
	}

	sqlite3_finalize(insertNodeStmt);
	sqlite3_finalize(insertItemStmt);
	if (!commitTransaction()) {
		rollbackTransaction();
		return false;
	}
	return true;
}

bool BrushDatabase::getCarpetNodes(int64_t brushId, std::vector<CarpetNodeRecord> &outNodes) {
	outNodes.clear();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* nodeStmt = nullptr;
	if (!prepare("SELECT id, align, sort_order FROM carpet_nodes "
	             "WHERE brush_id = ? ORDER BY sort_order ASC, id ASC;", &nodeStmt)) {
		return false;
	}

	sqlite3_stmt* itemStmt = nullptr;
	if (!prepare("SELECT item_id, chance, sort_order FROM carpet_node_items "
	             "WHERE carpet_node_id = ? ORDER BY sort_order ASC, id ASC;", &itemStmt)) {
		sqlite3_finalize(nodeStmt);
		return false;
	}

	sqlite3_bind_int64(nodeStmt, 1, brushId);

	for (;;) {
		const int nodeRc = sqlite3_step(nodeStmt);
		if (nodeRc == SQLITE_DONE) {
			break;
		}
		if (nodeRc != SQLITE_ROW) {
			sqlite3_finalize(nodeStmt);
			sqlite3_finalize(itemStmt);
			return setErrorFromDatabase("Failed to read carpet nodes");
		}

		const int64_t nodeId = sqlite3_column_int64(nodeStmt, 0);

		CarpetNodeRecord node;
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
				sqlite3_finalize(nodeStmt);
				sqlite3_finalize(itemStmt);
				return setErrorFromDatabase("Failed to read carpet node items");
			}

			CarpetNodeItemRecord item;
			item.itemId = sqlite3_column_int(itemStmt, 0);
			item.chance = sqlite3_column_int(itemStmt, 1);
			item.sortOrder = sqlite3_column_int(itemStmt, 2);
			node.items.push_back(item);
		}

		outNodes.push_back(node);
	}

	sqlite3_finalize(nodeStmt);
	sqlite3_finalize(itemStmt);
	return true;
}

bool BrushDatabase::replaceTableNodes(int64_t brushId, const std::vector<TableNodeRecord> &nodes) {
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

	for (const TableNodeRecord &node : nodes) {
		sqlite3_reset(insertNodeStmt);
		sqlite3_clear_bindings(insertNodeStmt);
		sqlite3_bind_int64(insertNodeStmt, 1, brushId);
		sqlite3_bind_text(insertNodeStmt, 2, node.align.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int(insertNodeStmt, 3, node.sortOrder);
		rc = sqlite3_step(insertNodeStmt);
		if (rc != SQLITE_DONE) {
			sqlite3_finalize(insertNodeStmt);
			sqlite3_finalize(insertItemStmt);
			rollbackTransaction();
			return setErrorFromDatabase("Failed to insert table node");
		}

		const int64_t nodeId = sqlite3_last_insert_rowid(connection_);
		for (const TableNodeItemRecord &item : node.items) {
			sqlite3_reset(insertItemStmt);
			sqlite3_clear_bindings(insertItemStmt);
			sqlite3_bind_int64(insertItemStmt, 1, nodeId);
			sqlite3_bind_int(insertItemStmt, 2, item.itemId);
			sqlite3_bind_int(insertItemStmt, 3, item.chance);
			sqlite3_bind_int(insertItemStmt, 4, item.sortOrder);
			rc = sqlite3_step(insertItemStmt);
			if (rc != SQLITE_DONE) {
				sqlite3_finalize(insertNodeStmt);
				sqlite3_finalize(insertItemStmt);
				rollbackTransaction();
				return setErrorFromDatabase("Failed to insert table node item");
			}
		}
	}

	sqlite3_finalize(insertNodeStmt);
	sqlite3_finalize(insertItemStmt);
	if (!commitTransaction()) {
		rollbackTransaction();
		return false;
	}
	return true;
}

bool BrushDatabase::getTableNodes(int64_t brushId, std::vector<TableNodeRecord> &outNodes) {
	outNodes.clear();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* nodeStmt = nullptr;
	if (!prepare("SELECT id, align, sort_order FROM table_nodes "
	             "WHERE brush_id = ? ORDER BY sort_order ASC, id ASC;", &nodeStmt)) {
		return false;
	}

	sqlite3_stmt* itemStmt = nullptr;
	if (!prepare("SELECT item_id, chance, sort_order FROM table_node_items "
	             "WHERE table_node_id = ? ORDER BY sort_order ASC, id ASC;", &itemStmt)) {
		sqlite3_finalize(nodeStmt);
		return false;
	}

	sqlite3_bind_int64(nodeStmt, 1, brushId);

	for (;;) {
		const int nodeRc = sqlite3_step(nodeStmt);
		if (nodeRc == SQLITE_DONE) {
			break;
		}
		if (nodeRc != SQLITE_ROW) {
			sqlite3_finalize(nodeStmt);
			sqlite3_finalize(itemStmt);
			return setErrorFromDatabase("Failed to read table nodes");
		}

		const int64_t nodeId = sqlite3_column_int64(nodeStmt, 0);

		TableNodeRecord node;
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
				sqlite3_finalize(nodeStmt);
				sqlite3_finalize(itemStmt);
				return setErrorFromDatabase("Failed to read table node items");
			}

			TableNodeItemRecord item;
			item.itemId = sqlite3_column_int(itemStmt, 0);
			item.chance = sqlite3_column_int(itemStmt, 1);
			item.sortOrder = sqlite3_column_int(itemStmt, 2);
			node.items.push_back(item);
		}

		outNodes.push_back(node);
	}

	sqlite3_finalize(nodeStmt);
	sqlite3_finalize(itemStmt);
	return true;
}

bool BrushDatabase::replaceDoodadAlternatives(int64_t brushId, const std::vector<DoodadAlternativeRecord> &alternatives) {
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

	for (const DoodadAlternativeRecord &alternative : alternatives) {
		sqlite3_reset(insertAltStmt);
		sqlite3_clear_bindings(insertAltStmt);
		sqlite3_bind_int64(insertAltStmt, 1, brushId);
		sqlite3_bind_int(insertAltStmt, 2, alternative.sortOrder);
		rc = sqlite3_step(insertAltStmt);
		if (rc != SQLITE_DONE) {
			sqlite3_finalize(insertAltStmt);
			sqlite3_finalize(insertSingleStmt);
			sqlite3_finalize(insertCompositeStmt);
			sqlite3_finalize(insertTileStmt);
			sqlite3_finalize(insertTileItemStmt);
			rollbackTransaction();
			return setErrorFromDatabase("Failed to insert doodad alternative");
		}

		const int64_t alternativeId = sqlite3_last_insert_rowid(connection_);
		for (const DoodadSingleItemRecord &single : alternative.singleItems) {
			sqlite3_reset(insertSingleStmt);
			sqlite3_clear_bindings(insertSingleStmt);
			sqlite3_bind_int64(insertSingleStmt, 1, alternativeId);
			sqlite3_bind_int(insertSingleStmt, 2, single.itemId);
			sqlite3_bind_int(insertSingleStmt, 3, single.chance);
			sqlite3_bind_int(insertSingleStmt, 4, single.sortOrder);
			rc = sqlite3_step(insertSingleStmt);
			if (rc != SQLITE_DONE) {
				sqlite3_finalize(insertAltStmt);
				sqlite3_finalize(insertSingleStmt);
				sqlite3_finalize(insertCompositeStmt);
				sqlite3_finalize(insertTileStmt);
				sqlite3_finalize(insertTileItemStmt);
				rollbackTransaction();
				return setErrorFromDatabase("Failed to insert doodad single item");
			}
		}

		for (const DoodadCompositeRecord &composite : alternative.composites) {
			sqlite3_reset(insertCompositeStmt);
			sqlite3_clear_bindings(insertCompositeStmt);
			sqlite3_bind_int64(insertCompositeStmt, 1, alternativeId);
			sqlite3_bind_int(insertCompositeStmt, 2, composite.chance);
			sqlite3_bind_int(insertCompositeStmt, 3, composite.sortOrder);
			rc = sqlite3_step(insertCompositeStmt);
			if (rc != SQLITE_DONE) {
				sqlite3_finalize(insertAltStmt);
				sqlite3_finalize(insertSingleStmt);
				sqlite3_finalize(insertCompositeStmt);
				sqlite3_finalize(insertTileStmt);
				sqlite3_finalize(insertTileItemStmt);
				rollbackTransaction();
				return setErrorFromDatabase("Failed to insert doodad composite");
			}

			const int64_t compositeId = sqlite3_last_insert_rowid(connection_);
			for (const DoodadCompositeTileRecord &tile : composite.tiles) {
				sqlite3_reset(insertTileStmt);
				sqlite3_clear_bindings(insertTileStmt);
				sqlite3_bind_int64(insertTileStmt, 1, compositeId);
				sqlite3_bind_int(insertTileStmt, 2, tile.offsetX);
				sqlite3_bind_int(insertTileStmt, 3, tile.offsetY);
				sqlite3_bind_int(insertTileStmt, 4, tile.offsetZ);
				sqlite3_bind_int(insertTileStmt, 5, tile.sortOrder);
				rc = sqlite3_step(insertTileStmt);
				if (rc != SQLITE_DONE) {
					sqlite3_finalize(insertAltStmt);
					sqlite3_finalize(insertSingleStmt);
					sqlite3_finalize(insertCompositeStmt);
					sqlite3_finalize(insertTileStmt);
					sqlite3_finalize(insertTileItemStmt);
					rollbackTransaction();
					return setErrorFromDatabase("Failed to insert doodad composite tile");
				}

				const int64_t tileId = sqlite3_last_insert_rowid(connection_);
				for (const DoodadCompositeTileItemRecord &item : tile.items) {
					sqlite3_reset(insertTileItemStmt);
					sqlite3_clear_bindings(insertTileItemStmt);
					sqlite3_bind_int64(insertTileItemStmt, 1, tileId);
					sqlite3_bind_int(insertTileItemStmt, 2, item.itemId);
					sqlite3_bind_int(insertTileItemStmt, 3, item.sortOrder);
					rc = sqlite3_step(insertTileItemStmt);
					if (rc != SQLITE_DONE) {
						sqlite3_finalize(insertAltStmt);
						sqlite3_finalize(insertSingleStmt);
						sqlite3_finalize(insertCompositeStmt);
						sqlite3_finalize(insertTileStmt);
						sqlite3_finalize(insertTileItemStmt);
						rollbackTransaction();
						return setErrorFromDatabase("Failed to insert doodad composite tile item");
					}
				}
			}
		}
	}

	sqlite3_finalize(insertAltStmt);
	sqlite3_finalize(insertSingleStmt);
	sqlite3_finalize(insertCompositeStmt);
	sqlite3_finalize(insertTileStmt);
	sqlite3_finalize(insertTileItemStmt);
	if (!commitTransaction()) {
		rollbackTransaction();
		return false;
	}
	return true;
}

bool BrushDatabase::getDoodadAlternatives(int64_t brushId, std::vector<DoodadAlternativeRecord> &outAlternatives) {
	outAlternatives.clear();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* altStmt = nullptr;
	if (!prepare("SELECT id, sort_order FROM doodad_alternatives "
	             "WHERE brush_id = ? ORDER BY sort_order ASC, id ASC;", &altStmt)) {
		return false;
	}

	sqlite3_stmt* singleStmt = nullptr;
	if (!prepare("SELECT item_id, chance, sort_order FROM doodad_single_items "
	             "WHERE doodad_alternative_id = ? ORDER BY sort_order ASC, id ASC;", &singleStmt)) {
		sqlite3_finalize(altStmt);
		return false;
	}

	sqlite3_stmt* compositeStmt = nullptr;
	if (!prepare("SELECT id, chance, sort_order FROM doodad_composites "
	             "WHERE doodad_alternative_id = ? ORDER BY sort_order ASC, id ASC;", &compositeStmt)) {
		sqlite3_finalize(altStmt);
		sqlite3_finalize(singleStmt);
		return false;
	}

	sqlite3_stmt* tileStmt = nullptr;
	if (!prepare("SELECT id, offset_x, offset_y, offset_z, sort_order FROM doodad_composite_tiles "
	             "WHERE doodad_composite_id = ? ORDER BY sort_order ASC, id ASC;", &tileStmt)) {
		sqlite3_finalize(altStmt);
		sqlite3_finalize(singleStmt);
		sqlite3_finalize(compositeStmt);
		return false;
	}

	sqlite3_stmt* tileItemStmt = nullptr;
	if (!prepare("SELECT item_id, sort_order FROM doodad_composite_tile_items "
	             "WHERE doodad_composite_tile_id = ? ORDER BY sort_order ASC, id ASC;", &tileItemStmt)) {
		sqlite3_finalize(altStmt);
		sqlite3_finalize(singleStmt);
		sqlite3_finalize(compositeStmt);
		sqlite3_finalize(tileStmt);
		return false;
	}

	sqlite3_bind_int64(altStmt, 1, brushId);

	for (;;) {
		const int altRc = sqlite3_step(altStmt);
		if (altRc == SQLITE_DONE) {
			break;
		}
		if (altRc != SQLITE_ROW) {
			sqlite3_finalize(altStmt);
			sqlite3_finalize(singleStmt);
			sqlite3_finalize(compositeStmt);
			sqlite3_finalize(tileStmt);
			sqlite3_finalize(tileItemStmt);
			return setErrorFromDatabase("Failed to read doodad alternatives");
		}

		const int64_t alternativeId = sqlite3_column_int64(altStmt, 0);

		DoodadAlternativeRecord alternative;
		alternative.sortOrder = sqlite3_column_int(altStmt, 1);

		sqlite3_reset(singleStmt);
		sqlite3_clear_bindings(singleStmt);
		sqlite3_bind_int64(singleStmt, 1, alternativeId);

		for (;;) {
			const int singleRc = sqlite3_step(singleStmt);
			if (singleRc == SQLITE_DONE) {
				break;
			}
			if (singleRc != SQLITE_ROW) {
				sqlite3_finalize(altStmt);
				sqlite3_finalize(singleStmt);
				sqlite3_finalize(compositeStmt);
				sqlite3_finalize(tileStmt);
				sqlite3_finalize(tileItemStmt);
				return setErrorFromDatabase("Failed to read doodad single items");
			}

			DoodadSingleItemRecord item;
			item.itemId = sqlite3_column_int(singleStmt, 0);
			item.chance = sqlite3_column_int(singleStmt, 1);
			item.sortOrder = sqlite3_column_int(singleStmt, 2);
			alternative.singleItems.push_back(item);
		}

		sqlite3_reset(compositeStmt);
		sqlite3_clear_bindings(compositeStmt);
		sqlite3_bind_int64(compositeStmt, 1, alternativeId);

		for (;;) {
			const int compositeRc = sqlite3_step(compositeStmt);
			if (compositeRc == SQLITE_DONE) {
				break;
			}
			if (compositeRc != SQLITE_ROW) {
				sqlite3_finalize(altStmt);
				sqlite3_finalize(singleStmt);
				sqlite3_finalize(compositeStmt);
				sqlite3_finalize(tileStmt);
				sqlite3_finalize(tileItemStmt);
				return setErrorFromDatabase("Failed to read doodad composites");
			}

			const int64_t compositeId = sqlite3_column_int64(compositeStmt, 0);

			DoodadCompositeRecord composite;
			composite.chance = sqlite3_column_int(compositeStmt, 1);
			composite.sortOrder = sqlite3_column_int(compositeStmt, 2);

			sqlite3_reset(tileStmt);
			sqlite3_clear_bindings(tileStmt);
			sqlite3_bind_int64(tileStmt, 1, compositeId);

			for (;;) {
				const int tileRc = sqlite3_step(tileStmt);
				if (tileRc == SQLITE_DONE) {
					break;
				}
				if (tileRc != SQLITE_ROW) {
					sqlite3_finalize(altStmt);
					sqlite3_finalize(singleStmt);
					sqlite3_finalize(compositeStmt);
					sqlite3_finalize(tileStmt);
					sqlite3_finalize(tileItemStmt);
					return setErrorFromDatabase("Failed to read doodad composite tiles");
				}

				const int64_t tileId = sqlite3_column_int64(tileStmt, 0);

				DoodadCompositeTileRecord tile;
				tile.offsetX = sqlite3_column_int(tileStmt, 1);
				tile.offsetY = sqlite3_column_int(tileStmt, 2);
				tile.offsetZ = sqlite3_column_int(tileStmt, 3);
				tile.sortOrder = sqlite3_column_int(tileStmt, 4);

				sqlite3_reset(tileItemStmt);
				sqlite3_clear_bindings(tileItemStmt);
				sqlite3_bind_int64(tileItemStmt, 1, tileId);

				for (;;) {
					const int tileItemRc = sqlite3_step(tileItemStmt);
					if (tileItemRc == SQLITE_DONE) {
						break;
					}
					if (tileItemRc != SQLITE_ROW) {
						sqlite3_finalize(altStmt);
						sqlite3_finalize(singleStmt);
						sqlite3_finalize(compositeStmt);
						sqlite3_finalize(tileStmt);
						sqlite3_finalize(tileItemStmt);
						return setErrorFromDatabase("Failed to read doodad composite tile items");
					}

					DoodadCompositeTileItemRecord item;
					item.itemId = sqlite3_column_int(tileItemStmt, 0);
					item.sortOrder = sqlite3_column_int(tileItemStmt, 1);
					tile.items.push_back(item);
				}

				composite.tiles.push_back(tile);
			}

			alternative.composites.push_back(composite);
		}

		outAlternatives.push_back(alternative);
	}

	sqlite3_finalize(altStmt);
	sqlite3_finalize(singleStmt);
	sqlite3_finalize(compositeStmt);
	sqlite3_finalize(tileStmt);
	sqlite3_finalize(tileItemStmt);
	return true;
}

bool BrushDatabase::replaceAllTilesets(const std::vector<TilesetStorageRecord> &tilesets) {
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
	if (!prepare("INSERT INTO tilesets(name, source_file) VALUES (?, ?);", &insertTilesetStmt)) {
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
	             "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);", &insertEntryStmt)) {
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

	for (const TilesetStorageRecord &tileset : tilesets) {
		sqlite3_reset(insertTilesetStmt);
		sqlite3_clear_bindings(insertTilesetStmt);
		sqlite3_bind_text(insertTilesetStmt, 1, tileset.name.utf8_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(insertTilesetStmt, 2, tileset.sourceFile.utf8_str(), -1, SQLITE_TRANSIENT);
		int rc = sqlite3_step(insertTilesetStmt);
		if (rc != SQLITE_DONE) {
			sqlite3_finalize(insertTilesetStmt);
			sqlite3_finalize(insertSectionStmt);
			sqlite3_finalize(insertEntryStmt);
			sqlite3_finalize(findBrushStmt);
			rollbackTransaction();
			return setErrorFromDatabase("Failed to insert tileset");
		}
		const int64_t tilesetId = sqlite3_last_insert_rowid(connection_);

		for (const TilesetSectionRecord &section : tileset.sections) {
			sqlite3_reset(insertSectionStmt);
			sqlite3_clear_bindings(insertSectionStmt);
			sqlite3_bind_int64(insertSectionStmt, 1, tilesetId);
			sqlite3_bind_text(insertSectionStmt, 2, section.sectionType.utf8_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(insertSectionStmt, 3, section.sortOrder);
			rc = sqlite3_step(insertSectionStmt);
			if (rc != SQLITE_DONE) {
				sqlite3_finalize(insertTilesetStmt);
				sqlite3_finalize(insertSectionStmt);
				sqlite3_finalize(insertEntryStmt);
				sqlite3_finalize(findBrushStmt);
				rollbackTransaction();
				return setErrorFromDatabase("Failed to insert tileset section");
			}
			const int64_t sectionId = sqlite3_last_insert_rowid(connection_);

			for (const TilesetEntryRecord &entry : section.entries) {
				int64_t resolvedBrushId = entry.brushId;
				if (resolvedBrushId == 0 && !entry.brushName.IsEmpty()) {
					sqlite3_reset(findBrushStmt);
					sqlite3_clear_bindings(findBrushStmt);
					sqlite3_bind_text(findBrushStmt, 1, entry.brushName.utf8_str(), -1, SQLITE_TRANSIENT);
					const int firstRc = sqlite3_step(findBrushStmt);
					if (firstRc == SQLITE_ROW) {
						resolvedBrushId = sqlite3_column_int64(findBrushStmt, 0);
						const int secondRc = sqlite3_step(findBrushStmt);
						if (secondRc == SQLITE_ROW) {
							resolvedBrushId = 0;
						} else if (secondRc != SQLITE_DONE) {
							sqlite3_finalize(insertTilesetStmt);
							sqlite3_finalize(insertSectionStmt);
							sqlite3_finalize(insertEntryStmt);
							sqlite3_finalize(findBrushStmt);
							rollbackTransaction();
							return setErrorFromDatabase("Failed to resolve tileset brush reference");
						}
					} else if (firstRc != SQLITE_DONE) {
						sqlite3_finalize(insertTilesetStmt);
						sqlite3_finalize(insertSectionStmt);
						sqlite3_finalize(insertEntryStmt);
						sqlite3_finalize(findBrushStmt);
						rollbackTransaction();
						return setErrorFromDatabase("Failed to resolve tileset brush reference");
					}
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
				rc = sqlite3_step(insertEntryStmt);
				if (rc != SQLITE_DONE) {
					sqlite3_finalize(insertTilesetStmt);
					sqlite3_finalize(insertSectionStmt);
					sqlite3_finalize(insertEntryStmt);
					sqlite3_finalize(findBrushStmt);
					rollbackTransaction();
					return setErrorFromDatabase("Failed to insert tileset entry");
				}
			}
		}
	}

	sqlite3_finalize(insertTilesetStmt);
	sqlite3_finalize(insertSectionStmt);
	sqlite3_finalize(insertEntryStmt);
	sqlite3_finalize(findBrushStmt);
	if (!commitTransaction()) {
		rollbackTransaction();
		return false;
	}
	return true;
}

bool BrushDatabase::getTilesetByName(const wxString &name, TilesetStorageRecord &outTileset) {
	outTileset = TilesetStorageRecord();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* tilesetStmt = nullptr;
	if (!prepare("SELECT id, name, source_file FROM tilesets WHERE name = ? LIMIT 1;", &tilesetStmt)) {
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
	sqlite3_finalize(tilesetStmt);

	sqlite3_stmt* sectionStmt = nullptr;
	if (!prepare("SELECT id, section_type, sort_order FROM tileset_sections "
	             "WHERE tileset_id = ? ORDER BY sort_order ASC, id ASC;", &sectionStmt)) {
		return false;
	}

	sqlite3_stmt* entryStmt = nullptr;
	if (!prepare("SELECT entry_kind, brush_id, brush_name, item_id, from_item_id, to_item_id, after_brush_name, after_item_id, sort_order "
	             "FROM tileset_brush_entries WHERE tileset_section_id = ? ORDER BY sort_order ASC, id ASC;", &entryStmt)) {
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
			entry.brushId = sqlite3_column_type(entryStmt, 1) == SQLITE_NULL ? 0 : sqlite3_column_int64(entryStmt, 1);
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

bool BrushDatabase::getAllTilesets(std::vector<TilesetStorageRecord> &outTilesets) {
	outTilesets.clear();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("SELECT name FROM tilesets ORDER BY name ASC, id ASC;", &stmt)) {
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

bool BrushDatabase::generateAuditReport(MaterialsDatabaseAuditReport &outReport) {
	outReport = MaterialsDatabaseAuditReport();

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* countStmt = nullptr;
	if (!prepare("SELECT "
	             "(SELECT COUNT(*) FROM brushes), "
	             "(SELECT COUNT(*) FROM border_sets), "
	             "(SELECT COUNT(*) FROM tilesets), "
	             "(SELECT COUNT(*) FROM tileset_sections), "
	             "(SELECT COUNT(*) FROM tileset_brush_entries), "
	             "(SELECT COUNT(*) FROM ground_brush_borders WHERE target_mode = 'brush' AND target_brush_name <> '' AND target_brush_id IS NULL), "
	             "(SELECT COUNT(*) FROM brush_links WHERE target_brush_name <> '' AND target_brush_name <> 'all' AND target_brush_id IS NULL), "
	             "(SELECT COUNT(*) FROM tileset_brush_entries WHERE brush_name <> '' AND brush_id IS NULL);", &countStmt)) {
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
	outReport.unresolvedGroundTargets = sqlite3_column_int(countStmt, 5);
	outReport.unresolvedBrushLinks = sqlite3_column_int(countStmt, 6);
	outReport.unresolvedTilesetEntries = sqlite3_column_int(countStmt, 7);
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

	sqlite3_finalize(groupedStmt);
	return true;
}

bool BrushDatabase::hasCompleteImportForCurrentSchema(bool &outReady) {
	outReady = false;

	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	int version = 0;
	if (!getSchemaVersion(version)) {
		return false;
	}
	if (version != kBrushDatabaseSchemaVersion) {
		return true;
	}

	MaterialsDatabaseAuditReport report;
	if (!generateAuditReport(report)) {
		return false;
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
	return true;
}

int BrushDatabase::getExpectedSchemaVersion() const {
	return kBrushDatabaseSchemaVersion;
}

bool BrushDatabase::resolveGroundReferenceNames() {
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

	return execute("UPDATE brush_links "
	               "SET target_brush_id = ("
	               "SELECT id FROM brushes b "
	               "WHERE b.name = brush_links.target_brush_name "
	               "LIMIT 1"
	               ") "
	               "WHERE target_brush_name <> '' AND target_brush_name <> 'all';");
}

bool BrushDatabase::execute(const wxString &sql) {
	char* errorMessage = nullptr;
	const int rc = sqlite3_exec(connection_, sql.utf8_str(), nullptr, nullptr, &errorMessage);
	if (rc != SQLITE_OK) {
		const wxString detail = errorMessage ? wxString::FromUTF8(errorMessage) : wxString();
		sqlite3_free(errorMessage);
		return setError("SQLite exec failed: " + detail);
	}
	return true;
}

bool BrushDatabase::prepare(const char* sql, sqlite3_stmt** stmt) {
	const int rc = sqlite3_prepare_v2(connection_, sql, -1, stmt, nullptr);
	if (rc != SQLITE_OK) {
		return setErrorFromDatabase("Failed to prepare SQLite statement");
	}
	return true;
}

bool BrushDatabase::beginTransaction() {
	return execute("BEGIN IMMEDIATE TRANSACTION;");
}

bool BrushDatabase::commitTransaction() {
	return execute("COMMIT;");
}

bool BrushDatabase::rollbackTransaction() {
	return execute("ROLLBACK;");
}

bool BrushDatabase::setError(const wxString &message) {
	lastError_ = message;
	spdlog::error("[BrushDatabase] {}", lastError_.ToStdString());
	return false;
}

bool BrushDatabase::setErrorFromDatabase(const wxString &prefix) {
	const wxString dbMessage = connection_ ? ToWxString(sqlite3_errmsg(connection_)) : "No SQLite connection";
	return setError(prefix + ": " + dbMessage);
}
