#ifndef RME_BRUSH_DATABASE_H_
#define RME_BRUSH_DATABASE_H_

#include "main.h"

#include <sqlite3.h>

struct BrushRecord {
	int64_t id = 0;
	wxString name;
	wxString type;
	int lookId = 0;
	int zOrder = 0;
};

struct BrushItemRecord {
	int64_t brushId = 0;
	int itemId = 0;
	int chance = 0;
};

class BrushDatabase {
public:
	BrushDatabase();
	~BrushDatabase();

	bool initialize(const wxString &databasePath);
	bool open(const wxString &databasePath);
	void close();

	bool isOpen() const;
	const wxString &getDatabasePath() const;
	const wxString &getLastError() const;

	bool initializeSchema();
	bool testDatabaseConnection();
	bool testBasicCRUD();

	bool insertBrush(const BrushRecord &brush, int64_t &insertedId);
	bool getBrushById(int64_t brushId, BrushRecord &outBrush);
	bool updateBrush(const BrushRecord &brush);
	bool deleteBrush(int64_t brushId);

	bool replaceBrushItems(int64_t brushId, const std::vector<BrushItemRecord> &items);
	bool getBrushItems(int64_t brushId, std::vector<BrushItemRecord> &outItems);

private:
	bool execute(const wxString &sql);
	bool prepare(const char* sql, sqlite3_stmt** stmt);
	bool beginTransaction();
	bool commitTransaction();
	bool rollbackTransaction();
	bool setError(const wxString &message);
	bool setErrorFromDatabase(const wxString &prefix);

	sqlite3* connection_ = nullptr;
	wxString databasePath_;
	wxString lastError_;
};

extern BrushDatabase g_brush_database;

#endif
