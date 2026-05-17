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
	wxString sourceFile;
	int serverLookId = 0;
	bool draggable = false;
	bool onBlocking = false;
	bool onDuplicate = false;
	bool redoBorders = false;
	bool randomize = false;
	bool oneSize = false;
	bool soloOptional = false;
	int thickness = 0;
	int thicknessCeiling = 0;
};

struct BrushItemRecord {
	int64_t brushId = 0;
	int itemId = 0;
	int chance = 0;
	int sortOrder = 0;
};

struct BorderSetRecord {
	int64_t id = 0;
	int xmlBorderId = 0;
	int64_t ownerBrushId = 0;
	wxString borderScope;
	wxString borderType;
	int borderGroup = 0;
	int groundEquivalent = 0;
	wxString sourceFile;
};

struct BorderSetItemRecord {
	int64_t borderSetId = 0;
	wxString edge;
	int itemId = 0;
	int sortOrder = 0;
};

struct GroundBorderCaseConditionRecord {
	wxString conditionType;
	int matchValue = 0;
	wxString edge;
	int sortOrder = 0;
};

struct GroundBorderCaseActionRecord {
	wxString actionType;
	int targetValue = 0;
	wxString edge;
	int replacementValue = 0;
	int sortOrder = 0;
};

struct GroundBorderCaseRecord {
	int sortOrder = 0;
	std::vector<GroundBorderCaseConditionRecord> conditions;
	std::vector<GroundBorderCaseActionRecord> actions;
};

struct GroundBrushBorderRecord {
	int64_t borderSetId = 0;
	wxString borderRole;
	wxString align;
	wxString targetMode;
	int64_t targetBrushId = 0;
	wxString targetBrushName;
	bool superBorder = false;
	int sortOrder = 0;
	std::vector<GroundBorderCaseRecord> cases;
};

struct BrushLinkRecord {
	int64_t brushId = 0;
	int64_t targetBrushId = 0;
	wxString targetBrushName;
	wxString relationType;
	int sortOrder = 0;
};

struct WallPartItemRecord {
	int itemId = 0;
	int chance = 0;
	int sortOrder = 0;
};

struct WallPartDoorRecord {
	int itemId = 0;
	wxString doorType;
	bool isOpen = false;
	bool wallHateMe = false;
	int sortOrder = 0;
};

struct WallPartRecord {
	wxString partType;
	int sortOrder = 0;
	std::vector<WallPartItemRecord> items;
	std::vector<WallPartDoorRecord> doors;
};

struct CarpetNodeItemRecord {
	int itemId = 0;
	int chance = 0;
	int sortOrder = 0;
};

struct CarpetNodeRecord {
	wxString align;
	int sortOrder = 0;
	std::vector<CarpetNodeItemRecord> items;
};

struct TableNodeItemRecord {
	int itemId = 0;
	int chance = 0;
	int sortOrder = 0;
};

struct TableNodeRecord {
	wxString align;
	int sortOrder = 0;
	std::vector<TableNodeItemRecord> items;
};

struct DoodadSingleItemRecord {
	int itemId = 0;
	int chance = 0;
	int sortOrder = 0;
};

struct DoodadCompositeTileItemRecord {
	int itemId = 0;
	int sortOrder = 0;
};

struct DoodadCompositeTileRecord {
	int offsetX = 0;
	int offsetY = 0;
	int offsetZ = 0;
	int sortOrder = 0;
	std::vector<DoodadCompositeTileItemRecord> items;
};

struct DoodadCompositeRecord {
	int chance = 0;
	int sortOrder = 0;
	std::vector<DoodadCompositeTileRecord> tiles;
};

struct DoodadAlternativeRecord {
	int sortOrder = 0;
	std::vector<DoodadSingleItemRecord> singleItems;
	std::vector<DoodadCompositeRecord> composites;
};

struct TilesetEntryRecord {
	wxString entryKind;
	int64_t brushId = 0;
	wxString brushName;
	int itemId = 0;
	int fromItemId = 0;
	int toItemId = 0;
	wxString afterBrushName;
	int afterItemId = 0;
	int sortOrder = 0;
};

struct TilesetSectionRecord {
	wxString sectionType;
	int sortOrder = 0;
	std::vector<TilesetEntryRecord> entries;
};

struct TilesetStorageRecord {
	wxString name;
	wxString sourceFile;
	std::vector<TilesetSectionRecord> sections;
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
	bool upsertBrush(const BrushRecord &brush, int64_t &brushId);
	bool getBrushById(int64_t brushId, BrushRecord &outBrush);
	bool findBrushByNameAndType(const wxString &name, const wxString &type, BrushRecord &outBrush);
	bool updateBrush(const BrushRecord &brush);
	bool deleteBrush(int64_t brushId);
	bool deleteBrushesByType(const wxString &type);

	bool replaceBrushItems(int64_t brushId, const std::vector<BrushItemRecord> &items);
	bool getBrushItems(int64_t brushId, std::vector<BrushItemRecord> &outItems);
	bool upsertBorderSet(const BorderSetRecord &borderSet, int64_t &borderSetId);
	bool findBorderSetByXmlBorderId(int xmlBorderId, BorderSetRecord &outBorderSet);
	bool replaceBorderSetItems(int64_t borderSetId, const std::vector<BorderSetItemRecord> &items);
	bool deleteBorderSetsByScope(const wxString &borderScope);
	bool deleteOwnedBorderSetsForBrush(int64_t brushId);
	bool replaceGroundBrushBorders(int64_t brushId, const std::vector<GroundBrushBorderRecord> &borders);
	bool replaceBrushLinks(int64_t brushId, const std::vector<BrushLinkRecord> &links);
	bool replaceWallParts(int64_t brushId, const std::vector<WallPartRecord> &parts);
	bool replaceCarpetNodes(int64_t brushId, const std::vector<CarpetNodeRecord> &nodes);
	bool replaceTableNodes(int64_t brushId, const std::vector<TableNodeRecord> &nodes);
	bool replaceDoodadAlternatives(int64_t brushId, const std::vector<DoodadAlternativeRecord> &alternatives);
	bool replaceAllTilesets(const std::vector<TilesetStorageRecord> &tilesets);
	bool resolveGroundReferenceNames();

private:
	bool ensureSchemaVersionTable();
	bool getSchemaVersion(int &version);
	bool setSchemaVersion(int version);
	bool migrateToVersion1();
	bool migrateToVersion2();
	bool migrateToVersion3();
	bool migrateToVersion4();
	bool migrateToVersion5();
	bool migrateToVersion6();
	bool columnExists(const wxString &tableName, const wxString &columnName, bool &exists);

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
