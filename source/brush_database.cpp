#include "main.h"

#include "brush_database.h"

BrushDatabase g_brush_database;

namespace {
constexpr int kBrushDatabaseSchemaVersion = 1;

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

bool BrushDatabase::initializeSchema() {
	if (!isOpen()) {
		return setError("SQLite database is not open.");
	}

	if (!beginTransaction()) {
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL);")) {
		rollbackTransaction();
		return false;
	}

	if (!execute("INSERT INTO schema_version(version) "
	             "SELECT 0 WHERE NOT EXISTS (SELECT 1 FROM schema_version);")) {
		rollbackTransaction();
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS brushes ("
	             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
	             "name TEXT NOT NULL,"
	             "type TEXT NOT NULL,"
	             "look_id INTEGER NOT NULL DEFAULT 0,"
	             "z_order INTEGER NOT NULL DEFAULT 0,"
	             "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
	             "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
	             ");")) {
		rollbackTransaction();
		return false;
	}

	if (!execute("CREATE UNIQUE INDEX IF NOT EXISTS idx_brushes_type_name "
	             "ON brushes(type, name);")) {
		rollbackTransaction();
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS brush_items ("
	             "brush_id INTEGER NOT NULL,"
	             "item_id INTEGER NOT NULL,"
	             "chance INTEGER NOT NULL DEFAULT 1 CHECK(chance > 0),"
	             "PRIMARY KEY (brush_id, item_id),"
	             "FOREIGN KEY (brush_id) REFERENCES brushes(id) ON DELETE CASCADE"
	             ");")) {
		rollbackTransaction();
		return false;
	}

	if (!execute("CREATE INDEX IF NOT EXISTS idx_brush_items_item_id "
	             "ON brush_items(item_id);")) {
		rollbackTransaction();
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
		rollbackTransaction();
		return false;
	}

	if (!execute("CREATE TABLE IF NOT EXISTS ground_optional_borders ("
	             "brush_id INTEGER NOT NULL,"
	             "border_id INTEGER NOT NULL,"
	             "PRIMARY KEY (brush_id, border_id),"
	             "FOREIGN KEY (brush_id) REFERENCES brushes(id) ON DELETE CASCADE"
	             ");")) {
		rollbackTransaction();
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
		rollbackTransaction();
		return false;
	}

	if (!execute("CREATE INDEX IF NOT EXISTS idx_brush_relationships_to "
	             "ON brush_relationships(to_brush_id, relationship_type);")) {
		rollbackTransaction();
		return false;
	}

	if (!execute(wxString::Format("UPDATE schema_version SET version = %d;", kBrushDatabaseSchemaVersion))) {
		rollbackTransaction();
		return false;
	}

	if (!commitTransaction()) {
		rollbackTransaction();
		return false;
	}

	spdlog::info("SQLite brush database schema initialized at version {}", kBrushDatabaseSchemaVersion);
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
