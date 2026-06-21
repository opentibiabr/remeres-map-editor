#ifndef RME_BRUSH_DATABASE_H_
#define RME_BRUSH_DATABASE_H_

#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

#include <wx/string.h>

struct sqlite3;
struct sqlite3_stmt;

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
	bool removeOptionalBorder = false;
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

struct BorderSetStorageRecord {
	BorderSetRecord borderSet;
	std::vector<BorderSetItemRecord> items;
};

struct BorderSetUsageRecord {
	int64_t brushId = 0;
	wxString brushName;
	wxString brushType;
	int lookId = 0;
	int serverLookId = 0;
	int primaryItemId = 0;
	wxString borderRole;
	wxString align;
	wxString targetMode;
	int64_t targetBrushId = 0;
	wxString targetBrushName;
	bool superBorder = false;
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

struct BrushUsageRecord {
	wxString sourceKind;
	int64_t sourceId = 0;
	wxString sourceName;
	wxString relation;
	wxString context;
	int sortOrder = 0;
	int64_t refId = 0;
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

struct PaletteGroupRecord {
	int64_t id = 0;
	wxString name;
	wxString runtimeFamily;
	int sortOrder = 0;
	bool isBuiltin = false;
};

struct TilesetStorageRecord {
	wxString name;
	wxString sourceFile;
	int64_t paletteGroupId = 0;
	wxString paletteGroupName;
	wxString paletteGroupRuntimeFamily;
	int paletteGroupSortOrder = 0;
	std::vector<TilesetSectionRecord> sections;
};

struct BrushStorageRecord {
	BrushRecord brush;
	std::vector<BrushItemRecord> items;
	std::vector<GroundBrushBorderRecord> borders;
	std::vector<BrushLinkRecord> links;
	std::vector<WallPartRecord> wallParts;
	std::vector<CarpetNodeRecord> carpetNodes;
	std::vector<TableNodeRecord> tableNodes;
	std::vector<DoodadAlternativeRecord> doodadAlternatives;
};

struct BrushTypeCountRecord {
	wxString type;
	int count = 0;
};

struct UnsupportedBrushSampleRecord {
	int64_t id = 0;
	wxString name;
	wxString type;
	wxString sourceFile;
};

struct UnresolvedTilesetEntrySampleRecord {
	wxString paletteGroupName;
	wxString tilesetName;
	wxString tilesetSourceFile;
	wxString sectionType;
	wxString entryKind;
	wxString brushName;
	int itemId = 0;
	int fromItemId = 0;
	int toItemId = 0;
	wxString afterBrushName;
	int afterItemId = 0;
	int sortOrder = 0;
};

struct MaterialsDatabaseAuditReport {
	int brushCount = 0;
	int borderSetCount = 0;
	int tilesetCount = 0;
	int tilesetSectionCount = 0;
	int tilesetEntryCount = 0;
	int unsupportedBrushTypeCount = 0;
	int unresolvedGroundTargets = 0;
	int unresolvedBrushLinks = 0;
	int unresolvedTilesetEntries = 0;
	int unresolvedCaseMatchBorderIds = 0;
	int unresolvedCaseReplaceBorderTargetIds = 0;
	int caseMatchBorderEdgesWithoutItem = 0;
	int caseReplaceBorderEdgesWithoutItem = 0;
	std::vector<BrushTypeCountRecord> brushTypeCounts;
	std::vector<BrushTypeCountRecord> unsupportedBrushTypeCounts;
	std::vector<UnsupportedBrushSampleRecord> unsupportedBrushSamples;
	std::vector<UnresolvedTilesetEntrySampleRecord> unresolvedTilesetEntrySamples;
};

struct MaterialsImportStatusRecord {
	bool completed = false;
	int64_t completedAt = 0;
	wxString source;
};

class BrushDatabaseSession {
public:
	BrushDatabaseSession();
	~BrushDatabaseSession();

	bool initialize(const wxString &databasePath);
	bool openReadOnly(const wxString &databasePath);
	bool open(const wxString &databasePath);
	void close();

	bool isOpen() const;
	bool isReadOnly() const;
	const wxString &getDatabasePath() const;
	const wxString &getLastError() const;
	int getLastSqliteErrorCode() const;
	int getLastSqliteExtendedErrorCode() const;
	sqlite3* connection() const;

	bool testDatabaseConnection();
	bool quickCheck(int maxErrors = 1);
	bool execute(const wxString &sql);
	bool prepare(const char* sql, sqlite3_stmt** stmt);
	bool beginTransaction();
	bool commitTransaction();
	bool rollbackTransaction();
	bool setError(const wxString &message);
	bool setErrorFromDatabase(const wxString &prefix);
	template <typename Operation>
	bool runInTransaction(Operation &&operation) {
		if (!beginTransaction()) {
			return false;
		}

		if (!std::forward<Operation>(operation)()) {
			rollbackTransaction();
			return false;
		}

		if (!commitTransaction()) {
			rollbackTransaction();
			return false;
		}

		return true;
	}

private:
	friend class BrushDatabaseComponent;

	bool openInternal(const wxString &databasePath, bool readOnly);

	sqlite3* connection_ = nullptr;
	wxString databasePath_;
	wxString lastError_;
	int lastSqliteErrorCode_ = 0;
	int lastSqliteExtendedErrorCode_ = 0;
	bool readOnly_ = false;
	int transactionDepth_ = 0;
	int nextSavepointId_ = 0;
	std::vector<int> savepointIds_;
};

class BrushDatabaseComponent {
public:
	explicit BrushDatabaseComponent(BrushDatabaseSession &session);

protected:
	sqlite3* connection() const;
	bool isOpen() const;
	bool isReadOnly() const;
	const wxString &lastError() const;
	bool execute(const wxString &sql);
	bool prepare(const char* sql, sqlite3_stmt** stmt);
	bool beginTransaction();
	bool commitTransaction();
	bool rollbackTransaction();
	bool setError(const wxString &message);
	bool setErrorFromDatabase(const wxString &prefix);

	BrushDatabaseSession &session_;
	sqlite3*&connection_;
	wxString &lastError_;
	bool &readOnly_;
};

class BrushDatabaseSchemaManager : public BrushDatabaseComponent {
public:
	explicit BrushDatabaseSchemaManager(BrushDatabaseSession &session);

	bool initializeSchema();
	bool getCurrentSchemaVersion(int &version);

private:
	bool ensureSchemaVersionTable();
	bool setSchemaVersion(int version);
	template <auto Migration>
	bool applySchemaMigrationStep(int &version, int targetVersion);
	bool migrateToVersion1();
	bool migrateToVersion2();
	bool migrateToVersion3();
	bool migrateToVersion4();
	bool migrateToVersion5();
	bool migrateToVersion6();
	bool migrateToVersion7();
	bool migrateToVersion8();
	bool migrateToVersion9();
	bool migrateToVersion10();
	bool migrateToVersion11();
	bool migrateToVersion12();
	bool addColumnIfMissing(const wxString &tableName, const wxString &columnName, const wxString &definition);
	bool executeStatements(std::initializer_list<const char*> statements);
	bool addVersion2BrushColumns();
	bool createVersion2BorderSchema();
	bool createVersion2BrushDetailSchema();
	bool createVersion2TilesetSchema();
	bool columnExists(const wxString &tableName, const wxString &columnName, bool &exists);
};

class BrushDatabaseBrushRepository : public BrushDatabaseComponent {
public:
	explicit BrushDatabaseBrushRepository(BrushDatabaseSession &session);

	bool testBasicCRUD();
	bool insertBrush(const BrushRecord &brush, int64_t &insertedId);
	bool upsertBrush(const BrushRecord &brush, int64_t &brushId);
	bool getBrushById(int64_t brushId, BrushRecord &outBrush);
	bool listBrushesByType(const wxString &type, std::vector<BrushRecord> &outBrushes);
	bool findBrushByNameAndType(const wxString &name, const wxString &type, BrushRecord &outBrush);
	bool getCompleteBrushById(int64_t brushId, BrushStorageRecord &outBrush);
	bool updateBrush(const BrushRecord &brush);
	bool updateBrushReferenceNames(int64_t brushId, const wxString &oldName, const wxString &newName);
	bool deleteBrushReferences(int64_t brushId, const wxString &brushName);
	bool deleteBrush(int64_t brushId);
	bool deleteBrushesByType(const wxString &type);
	bool replaceBrushItems(int64_t brushId, const std::vector<BrushItemRecord> &items);
	bool getBrushItems(int64_t brushId, std::vector<BrushItemRecord> &outItems);
	bool upsertBorderSet(const BorderSetRecord &borderSet, int64_t &borderSetId);
	bool getBorderSetById(int64_t borderSetId, BorderSetRecord &outBorderSet);
	bool findBorderSetByXmlBorderId(int xmlBorderId, BorderSetRecord &outBorderSet);
	bool listBorderSetsByScope(const wxString &borderScope, std::vector<BorderSetRecord> &outBorderSets);
	bool listBorderSetUsages(int64_t borderSetId, std::vector<BorderSetUsageRecord> &outUsages);
	bool deleteBorderSet(int64_t borderSetId);
	bool replaceBorderSetItems(int64_t borderSetId, const std::vector<BorderSetItemRecord> &items);
	bool getBorderSetItems(int64_t borderSetId, std::vector<BorderSetItemRecord> &outItems);
	bool deleteBorderSetsByScope(const wxString &borderScope);
	bool deleteOwnedBorderSetsForBrush(int64_t brushId);
	bool replaceGroundBrushBorders(int64_t brushId, const std::vector<GroundBrushBorderRecord> &borders);
	bool getGroundBrushBorders(int64_t brushId, std::vector<GroundBrushBorderRecord> &outBorders);
	bool replaceBrushLinks(int64_t brushId, const std::vector<BrushLinkRecord> &links);
	bool getBrushLinks(int64_t brushId, std::vector<BrushLinkRecord> &outLinks);
	bool listBrushUsages(int64_t brushId, const wxString &brushName, std::vector<BrushUsageRecord> &outUsages);
	bool replaceWallParts(int64_t brushId, const std::vector<WallPartRecord> &parts);
	bool getWallParts(int64_t brushId, std::vector<WallPartRecord> &outParts);
	bool replaceCarpetNodes(int64_t brushId, const std::vector<CarpetNodeRecord> &nodes);
	bool getCarpetNodes(int64_t brushId, std::vector<CarpetNodeRecord> &outNodes);
	bool replaceTableNodes(int64_t brushId, const std::vector<TableNodeRecord> &nodes);
	bool getTableNodes(int64_t brushId, std::vector<TableNodeRecord> &outNodes);
	bool replaceDoodadAlternatives(int64_t brushId, const std::vector<DoodadAlternativeRecord> &alternatives);
	bool getDoodadAlternatives(int64_t brushId, std::vector<DoodadAlternativeRecord> &outAlternatives);
	bool resolveGroundReferenceNames();
};

class BrushDatabaseCatalogRepository : public BrushDatabaseComponent {
public:
	BrushDatabaseCatalogRepository(BrushDatabaseSession &session, BrushDatabaseSchemaManager &schemaManager);

	bool replaceAllTilesets(const std::vector<TilesetStorageRecord> &tilesets);
	bool saveTileset(const TilesetStorageRecord &tileset);
	bool deleteTileset(const wxString &name);
	bool savePaletteGroup(const PaletteGroupRecord &group);
	bool deletePaletteGroup(const wxString &name);
	bool getAllPaletteGroups(std::vector<PaletteGroupRecord> &outGroups);
	bool getTilesetByName(const wxString &name, TilesetStorageRecord &outTileset);
	bool getAllTilesets(std::vector<TilesetStorageRecord> &outTilesets);
	bool generateAuditReport(MaterialsDatabaseAuditReport &outReport);
	bool hasCompleteImportForCurrentSchema(bool &outReady);
	bool hasCompleteImportForCurrentSchema(bool &outReady, wxString &outReason);
	bool isMaterialsImportComplete(bool &outComplete, wxString &outReason);
	bool getMaterialsImportStatus(MaterialsImportStatusRecord &outStatus, wxString &outReason);
	bool markMaterialsImportComplete(const wxString &source);
	int getExpectedSchemaVersion() const;

private:
	BrushDatabaseSchemaManager &schemaManager_;
};

class BrushDatabase {
public:
	BrushDatabase() = default;
	~BrushDatabase();

	bool initialize(const wxString &databasePath);
	bool openReadOnly(const wxString &databasePath);
	bool open(const wxString &databasePath);
	void close();

	bool isOpen() const;
	bool isReadOnly() const;
	const wxString &getDatabasePath() const;
	const wxString &getLastError() const;
	int getLastSqliteErrorCode() const;
	int getLastSqliteExtendedErrorCode() const;
	bool testDatabaseConnection();
	bool quickCheck(int maxErrors = 1);

	bool upsertBrush(const BrushRecord &brush, int64_t &brushId);
	bool getBrushById(int64_t brushId, BrushRecord &outBrush);
	bool updateBrush(const BrushRecord &brush);
	bool listBrushesByType(const wxString &type, std::vector<BrushRecord> &outBrushes);
	bool findBrushByNameAndType(const wxString &name, const wxString &type, BrushRecord &outBrush);
	bool getCompleteBrushById(int64_t brushId, BrushStorageRecord &outBrush);
	bool updateBrushReferenceNames(int64_t brushId, const wxString &oldName, const wxString &newName);
	bool deleteBrushReferences(int64_t brushId, const wxString &brushName);
	bool deleteBrush(int64_t brushId);
	bool deleteBrushesByType(const wxString &type);
	bool replaceBrushItems(int64_t brushId, const std::vector<BrushItemRecord> &items);
	bool upsertBorderSet(const BorderSetRecord &borderSet, int64_t &borderSetId);
	bool getBorderSetById(int64_t borderSetId, BorderSetRecord &outBorderSet);
	bool findBorderSetByXmlBorderId(int xmlBorderId, BorderSetRecord &outBorderSet);
	bool listBorderSetsByScope(const wxString &borderScope, std::vector<BorderSetRecord> &outBorderSets);
	bool listBorderSetUsages(int64_t borderSetId, std::vector<BorderSetUsageRecord> &outUsages);
	bool deleteBorderSet(int64_t borderSetId);
	bool replaceBorderSetItems(int64_t borderSetId, const std::vector<BorderSetItemRecord> &items);
	bool getBorderSetItems(int64_t borderSetId, std::vector<BorderSetItemRecord> &outItems);
	bool deleteBorderSetsByScope(const wxString &borderScope);
	bool deleteOwnedBorderSetsForBrush(int64_t brushId);
	bool replaceGroundBrushBorders(int64_t brushId, const std::vector<GroundBrushBorderRecord> &borders);
	bool replaceBrushLinks(int64_t brushId, const std::vector<BrushLinkRecord> &links);
	bool replaceWallParts(int64_t brushId, const std::vector<WallPartRecord> &parts);
	bool replaceCarpetNodes(int64_t brushId, const std::vector<CarpetNodeRecord> &nodes);
	bool replaceTableNodes(int64_t brushId, const std::vector<TableNodeRecord> &nodes);
	bool replaceDoodadAlternatives(int64_t brushId, const std::vector<DoodadAlternativeRecord> &alternatives);
	bool resolveGroundReferenceNames();
	bool listBrushUsages(int64_t brushId, const wxString &brushName, std::vector<BrushUsageRecord> &outUsages);

	bool replaceAllTilesets(const std::vector<TilesetStorageRecord> &tilesets);
	bool saveTileset(const TilesetStorageRecord &tileset);
	bool deleteTileset(const wxString &name);
	bool savePaletteGroup(const PaletteGroupRecord &group);
	bool deletePaletteGroup(const wxString &name);
	bool getAllPaletteGroups(std::vector<PaletteGroupRecord> &outGroups);
	bool getAllTilesets(std::vector<TilesetStorageRecord> &outTilesets);
	bool generateAuditReport(MaterialsDatabaseAuditReport &outReport);
	bool hasCompleteImportForCurrentSchema(bool &outReady);
	bool hasCompleteImportForCurrentSchema(bool &outReady, wxString &outReason);
	bool isMaterialsImportComplete(bool &outComplete, wxString &outReason);
	bool getMaterialsImportStatus(MaterialsImportStatusRecord &outStatus, wxString &outReason);
	bool markMaterialsImportComplete(const wxString &source);
	bool getCurrentSchemaVersion(int &outVersion);
	int getExpectedSchemaVersion() const;
	template <typename Operation>
	bool runInTransaction(Operation &&operation) {
		return session_.runInTransaction(std::forward<Operation>(operation));
	}

private:
	BrushDatabaseSession session_;
	BrushDatabaseSchemaManager schemaManager_ { session_ };
	BrushDatabaseBrushRepository brushRepository_ { session_ };
	BrushDatabaseCatalogRepository catalogRepository_ { session_, schemaManager_ };
};

extern BrushDatabase g_brush_database;

#endif
