#include "main.h"

#include "brush_database.h"

BrushDatabase g_brush_database;

namespace {
constexpr int kBrushDatabaseSchemaVersion = 2;

static wxString ToWxString(const char* value) {
	return value ? wxString::FromUTF8(value) : wxString();
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
	             "brush_id INTEGER NOT NULL,"
	             "item_id INTEGER NOT NULL,"
	             "chance INTEGER NOT NULL DEFAULT 1 CHECK(chance > 0),"
	             "PRIMARY KEY (brush_id, item_id),"
	             "FOREIGN KEY (brush_id) REFERENCES brushes(id) ON DELETE CASCADE"
	             ");")) {
		return false;
	}

	if (!execute("CREATE INDEX IF NOT EXISTS idx_brush_items_item_id "
	             "ON brush_items(item_id);")) {
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
	             "chance INTEGER NOT NULL DEFAULT 1 CHECK(chance > 0),"
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
	             "chance INTEGER NOT NULL DEFAULT 1 CHECK(chance > 0),"
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
	             "chance INTEGER NOT NULL DEFAULT 1 CHECK(chance > 0),"
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
	             "chance INTEGER NOT NULL DEFAULT 1 CHECK(chance > 0),"
	             "sort_order INTEGER NOT NULL DEFAULT 0,"
	             "FOREIGN KEY (doodad_alternative_id) REFERENCES doodad_alternatives(id) ON DELETE CASCADE"
	             ");")) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS doodad_composites ("
	             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
	             "doodad_alternative_id INTEGER NOT NULL,"
	             "chance INTEGER NOT NULL DEFAULT 1 CHECK(chance > 0),"
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

	if (!commitTransaction()) {
		rollbackTransaction();
		return false;
	}

	spdlog::info("SQLite brush database schema initialized at version {}", version);
	return true;
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
	if (!prepare("INSERT INTO brushes(name, type, look_id, z_order, updated_at) "
	             "VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP);", &stmt)) {
		return false;
	}

	sqlite3_bind_text(stmt, 1, brush.name.utf8_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, brush.type.utf8_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 3, brush.lookId);
	sqlite3_bind_int(stmt, 4, brush.zOrder);

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
	if (!prepare("SELECT id, name, type, look_id, z_order FROM brushes WHERE id = ?;", &stmt)) {
		return false;
	}

	sqlite3_bind_int64(stmt, 1, brushId);

	const int rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW) {
		sqlite3_finalize(stmt);
		return setError(wxString::Format("Brush %lld was not found in SQLite.", static_cast<long long>(brushId)));
	}

	outBrush.id = sqlite3_column_int64(stmt, 0);
	outBrush.name = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
	outBrush.type = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
	outBrush.lookId = sqlite3_column_int(stmt, 3);
	outBrush.zOrder = sqlite3_column_int(stmt, 4);

	sqlite3_finalize(stmt);
	return true;
}

bool BrushDatabase::findBrushByNameAndType(const wxString &name, const wxString &type, BrushRecord &outBrush) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("SELECT id, name, type, look_id, z_order "
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

	outBrush.id = sqlite3_column_int64(stmt, 0);
	outBrush.name = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
	outBrush.type = ToWxString(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
	outBrush.lookId = sqlite3_column_int(stmt, 3);
	outBrush.zOrder = sqlite3_column_int(stmt, 4);
	sqlite3_finalize(stmt);
	return true;
}

bool BrushDatabase::updateBrush(const BrushRecord &brush) {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	sqlite3_stmt* stmt = nullptr;
	if (!prepare("UPDATE brushes "
	             "SET name = ?, type = ?, look_id = ?, z_order = ?, updated_at = CURRENT_TIMESTAMP "
	             "WHERE id = ?;", &stmt)) {
		return false;
	}

	sqlite3_bind_text(stmt, 1, brush.name.utf8_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, brush.type.utf8_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 3, brush.lookId);
	sqlite3_bind_int(stmt, 4, brush.zOrder);
	sqlite3_bind_int64(stmt, 5, brush.id);

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
	if (!prepare("INSERT INTO brush_items(brush_id, item_id, chance) VALUES (?, ?, ?);", &insertStmt)) {
		rollbackTransaction();
		return false;
	}

	for (const BrushItemRecord &item : items) {
		sqlite3_reset(insertStmt);
		sqlite3_clear_bindings(insertStmt);
		sqlite3_bind_int64(insertStmt, 1, brushId);
		sqlite3_bind_int(insertStmt, 2, item.itemId);
		sqlite3_bind_int(insertStmt, 3, item.chance);

		rc = sqlite3_step(insertStmt);
		if (rc != SQLITE_DONE) {
			sqlite3_finalize(insertStmt);
			rollbackTransaction();
			return setErrorFromDatabase("Failed to insert brush item");
		}
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
	if (!prepare("SELECT brush_id, item_id, chance "
	             "FROM brush_items WHERE brush_id = ? ORDER BY item_id ASC;", &stmt)) {
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
		outItems.push_back(item);
	}

	sqlite3_finalize(stmt);
	return true;
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
