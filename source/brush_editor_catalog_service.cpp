//////////////////////////////////////////////////////////////////////
// brush_editor_catalog_service.cpp - Read-only catalog builder
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "brush_editor_catalog_service.h"
#include "brush.h"
#include "items.h"
#include "materials.h"

namespace {
wxString CategoryTypeToLabel(TilesetCategoryType type) {
	switch (type) {
		case TILESET_TERRAIN:
			return "Terrain";
		case TILESET_MONSTER:
			return "Monster";
		case TILESET_NPC:
			return "NPC";
		case TILESET_DOODAD:
			return "Doodad";
		case TILESET_ITEM:
			return "Item";
		case TILESET_RAW:
			return "Raw";
		case TILESET_HOUSE:
			return "House";
		case TILESET_WAYPOINT:
			return "Waypoint";
		case TILESET_ZONES:
			return "Zone";
		case TILESET_UNKNOWN:
		default:
			return "Unknown";
	}
}

wxString MakePaletteName(const wxString &tilesetName, TilesetCategoryType categoryType) {
	return tilesetName + " [" + CategoryTypeToLabel(categoryType) + "]";
}

bool HasMembership(const BrushEditorBrushDefinition &definition, const wxString &tilesetName, TilesetCategoryType categoryType) {
	for (const BrushEditorPaletteMembership &membership : definition.memberships) {
		if (membership.tilesetName == tilesetName && membership.categoryType == categoryType) {
			return true;
		}
	}
	return false;
}

void AddMembership(BrushEditorBrushDefinition &definition, const wxString &tilesetName, TilesetCategoryType categoryType, int orderHint) {
	if (tilesetName.IsEmpty() || categoryType == TILESET_UNKNOWN) {
		return;
	}

	if (HasMembership(definition, tilesetName, categoryType)) {
		return;
	}

	BrushEditorPaletteMembership membership;
	membership.tilesetName = tilesetName;
	membership.categoryType = categoryType;
	membership.orderHint = orderHint;
	definition.memberships.push_back(membership);
}

void AddStorageLocation(BrushEditorBrushDefinition &definition, const wxString &filePath, const wxString &elementName, const wxString &logicalId) {
	for (const BrushEditorStorageLocation &location : definition.storageLocations) {
		if (location.filePath == filePath && location.xmlElementName == elementName && location.logicalId == logicalId) {
			return;
		}
	}

	BrushEditorStorageLocation location;
	location.filePath = filePath;
	location.xmlElementName = elementName;
	location.logicalId = logicalId;
	definition.storageLocations.push_back(location);
}

BrushEditorBrushKind InferBrushKind(Brush* brush, TilesetCategoryType categoryType) {
	if (!brush) {
		return BrushEditorBrushKind::Unknown;
	}

	if (brush->isGround()) {
		return BrushEditorBrushKind::Ground;
	}
	if (brush->isWall()) {
		return BrushEditorBrushKind::Wall;
	}
	if (brush->isRaw()) {
		return BrushEditorBrushKind::Raw;
	}
	if (brush->isDoodad()) {
		return BrushEditorBrushKind::Doodad;
	}
	if (brush->isMonster() || brush->isNpc()) {
		return BrushEditorBrushKind::Creature;
	}
	if (brush->isSpawnMonster() || brush->isSpawnNpc()) {
		return BrushEditorBrushKind::Spawn;
	}
	if (brush->isHouse() || brush->isHouseExit()) {
		return BrushEditorBrushKind::House;
	}
	if (brush->isWaypoint()) {
		return BrushEditorBrushKind::Waypoint;
	}
	if (brush->isZone() || brush->isFlag()) {
		return BrushEditorBrushKind::Zone;
	}

	switch (categoryType) {
		case TILESET_TERRAIN:
			return BrushEditorBrushKind::Ground;
		case TILESET_DOODAD:
			return BrushEditorBrushKind::Doodad;
		case TILESET_ITEM:
			return BrushEditorBrushKind::Item;
		case TILESET_RAW:
			return BrushEditorBrushKind::Raw;
		case TILESET_MONSTER:
		case TILESET_NPC:
			return BrushEditorBrushKind::Creature;
		case TILESET_HOUSE:
			return BrushEditorBrushKind::House;
		case TILESET_WAYPOINT:
			return BrushEditorBrushKind::Waypoint;
		case TILESET_ZONES:
			return BrushEditorBrushKind::Zone;
		case TILESET_UNKNOWN:
		default:
			return BrushEditorBrushKind::Unknown;
	}
}

uint16_t GetPreviewItemId(Brush* brush) {
	if (!brush) {
		return 0;
	}

	const int lookId = brush->getLookID();
	if (lookId > 0 && lookId <= std::numeric_limits<uint16_t>::max()) {
		return static_cast<uint16_t>(lookId);
	}

	const uint32_t brushId = brush->getID();
	if (brushId > 0 && brushId <= std::numeric_limits<uint16_t>::max()) {
		return static_cast<uint16_t>(brushId);
	}

	return 0;
}

void ApplyCapabilitiesForKind(BrushEditorBrushDefinition &definition) {
	definition.AddCapability(BRUSH_CAP_GENERAL);

	if (!definition.memberships.empty()) {
		definition.AddCapability(BRUSH_CAP_PALETTES);
	}

	switch (definition.kind) {
		case BrushEditorBrushKind::Ground:
			definition.AddCapability(BRUSH_CAP_VARIATIONS);
			definition.AddCapability(BRUSH_CAP_BORDERS);
			definition.AddCapability(BRUSH_CAP_FLAGS);
			break;
		case BrushEditorBrushKind::Wall:
			definition.AddCapability(BRUSH_CAP_STRUCTURE);
			definition.AddCapability(BRUSH_CAP_FLAGS);
			break;
		case BrushEditorBrushKind::Border:
			definition.AddCapability(BRUSH_CAP_FLAGS);
			break;
		default:
			break;
	}
}

wxString BuildBrushXmlPath(const wxString &dataDirectory, const wxString &fileName) {
	return dataDirectory + wxFileName::GetPathSeparator() + "materials" + wxFileName::GetPathSeparator() + "brushs" + wxFileName::GetPathSeparator() + fileName;
}

wxString BuildBorderXmlPath(const wxString &dataDirectory) {
	return dataDirectory + wxFileName::GetPathSeparator() + "materials" + wxFileName::GetPathSeparator() + "borders" + wxFileName::GetPathSeparator() + "borders.xml";
}

bool LoadDocumentIfExists(const wxString &filePath, pugi::xml_document &doc, wxArrayString &warnings) {
	if (!wxFileExists(filePath)) {
		return false;
	}

	const pugi::xml_parse_result result = doc.load_file(nstr(filePath).c_str());
	if (!result) {
		warnings.push_back("Failed to parse " + filePath);
		return false;
	}

	return true;
}

void LoadPaletteMembershipsFromMaterials(BrushEditorCatalog &catalog) {
	for (const auto &tilesetEntry : g_materials.tilesets) {
		const wxString tilesetName = wxString(tilesetEntry.first);
		Tileset* const tileset = tilesetEntry.second;
		if (!tileset) {
			continue;
		}

		for (TilesetCategory* category : tileset->categories) {
			if (!category || category->brushlist.empty()) {
				continue;
			}

			const wxString paletteName = MakePaletteName(tilesetName, category->getType());
			BrushEditorPaletteDefinition &palette = catalog.GetOrCreatePalette(paletteName);
			palette.categoryType = category->getType();
			palette.builtin = true;

			for (size_t index = 0; index < category->brushlist.size(); ++index) {
				Brush* brush = category->brushlist[index];
				if (!brush) {
					continue;
				}

				const wxString brushName = wxstr(brush->getName()).Trim(false).Trim(true);
				if (brushName.IsEmpty()) {
					continue;
				}

				BrushEditorBrushDefinition &definition = catalog.GetOrCreateBrush(brushName);
				if (definition.kind == BrushEditorBrushKind::Unknown) {
					definition.kind = InferBrushKind(brush, category->getType());
				}

				if (definition.previewItemId == 0) {
					definition.previewItemId = GetPreviewItemId(brush);
				}
				if (definition.serverLookId == 0) {
					definition.serverLookId = definition.previewItemId;
				}

				AddMembership(definition, tilesetName, category->getType(), static_cast<int>(index));
				palette.AddBrush(brushName);
				ApplyCapabilitiesForKind(definition);
			}
		}
	}
}

void LoadGroundBrushes(BrushEditorCatalog &catalog, const wxString &dataDirectory, wxArrayString &warnings) {
	const wxString groundsFile = BuildBrushXmlPath(dataDirectory, "grounds.xml");

	pugi::xml_document doc;
	if (!LoadDocumentIfExists(groundsFile, doc, warnings)) {
		return;
	}

	const pugi::xml_node materials = doc.child("materials");
	if (!materials) {
		warnings.push_back("grounds.xml: missing <materials> root");
		return;
	}

	for (pugi::xml_node brushNode = materials.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
		const wxString brushName = wxString(brushNode.attribute("name").as_string());
		if (brushName.IsEmpty()) {
			continue;
		}

		const wxString typeValue = wxString(brushNode.attribute("type").as_string());
		BrushEditorBrushDefinition &definition = catalog.GetOrCreateBrush(brushName);

		if (!typeValue.IsEmpty()) {
			const BrushEditorBrushKind parsedKind = BrushEditorBrushKindFromString(typeValue);
			if (parsedKind != BrushEditorBrushKind::Unknown) {
				definition.kind = parsedKind;
			}
		}
		if (definition.kind == BrushEditorBrushKind::Unknown) {
			definition.kind = BrushEditorBrushKind::Ground;
		}

		definition.zOrder = brushNode.attribute("z-order").as_int(definition.zOrder);
		definition.optional = brushNode.child("optional");
		definition.ground = (definition.kind == BrushEditorBrushKind::Ground);

		uint16_t mainLookId = 0;
		if (pugi::xml_attribute attr = brushNode.attribute("server_lookid")) {
			mainLookId = attr.as_uint();
		} else if (pugi::xml_attribute attr = brushNode.attribute("lookid")) {
			mainLookId = attr.as_uint();
		}
		if (mainLookId != 0) {
			definition.serverLookId = mainLookId;
			if (definition.previewItemId == 0) {
				definition.previewItemId = mainLookId;
			}
		}

		AddStorageLocation(definition, groundsFile, "brush", brushName);

		definition.variations.clear();
		definition.borders.clear();

		for (pugi::xml_node itemNode = brushNode.child("item"); itemNode; itemNode = itemNode.next_sibling("item")) {
			const uint16_t itemId = itemNode.attribute("id").as_uint();
			if (itemId == 0) {
				continue;
			}

			BrushEditorVariation variation;
			variation.label = wxString::Format("Item %u", static_cast<unsigned>(itemId));
			variation.chance = std::max(1, itemNode.attribute("chance").as_int(1));

			BrushEditorCompositeTile tile;
			tile.x = 0;
			tile.y = 0;
			tile.itemId = itemId;
			tile.chance = variation.chance;

			variation.tiles.push_back(tile);
			definition.variations.push_back(variation);

			if (definition.previewItemId == 0) {
				definition.previewItemId = itemId;
			}
		}

		for (pugi::xml_node compositeNode = brushNode.child("composite"); compositeNode; compositeNode = compositeNode.next_sibling("composite")) {
			BrushEditorVariation variation;
			variation.label = "Composite";
			variation.chance = std::max(1, compositeNode.attribute("chance").as_int(1));

			for (pugi::xml_node tileNode = compositeNode.child("tile"); tileNode; tileNode = tileNode.next_sibling("tile")) {
				BrushEditorCompositeTile tile;
				tile.x = tileNode.attribute("x").as_int();
				tile.y = tileNode.attribute("y").as_int();
				tile.chance = variation.chance;

				pugi::xml_node itemNode = tileNode.child("item");
				if (itemNode) {
					tile.itemId = itemNode.attribute("id").as_uint();
				}

				if (tile.IsValid()) {
					variation.tiles.push_back(tile);
					if (definition.previewItemId == 0) {
						definition.previewItemId = tile.itemId;
					}
				}
			}

			if (!variation.tiles.empty()) {
				definition.variations.push_back(variation);
			}
		}

		for (pugi::xml_node borderNode = brushNode.child("border"); borderNode; borderNode = borderNode.next_sibling("border")) {
			const int borderId = borderNode.attribute("id").as_int();
			if (borderId <= 0) {
				continue;
			}

			BrushEditorBorderConfig border;
			border.borderId = borderId;
			border.alignment = wxString(borderNode.attribute("align").as_string());
			if (border.alignment.IsEmpty()) {
				border.alignment = "outer";
			}
			border.includeToNone = wxString(borderNode.attribute("to").as_string()) == "none";
			border.includeInner = border.alignment == "inner";
			border.optional = definition.optional;
			border.ground = definition.ground;
			border.group = definition.group;

			definition.borders.push_back(border);
		}

		ApplyCapabilitiesForKind(definition);
		if (definition.variations.empty()) {
			definition.RemoveCapability(BRUSH_CAP_VARIATIONS);
		}
		if (definition.borders.empty()) {
			definition.RemoveCapability(BRUSH_CAP_BORDERS);
		}
	}
}

void AppendItemsToWallPart(BrushEditorWallPart &part, pugi::xml_node parentNode) {
	for (pugi::xml_node itemNode = parentNode.child("item"); itemNode; itemNode = itemNode.next_sibling("item")) {
		const uint16_t itemId = itemNode.attribute("id").as_uint();
		if (itemId == 0) {
			continue;
		}

		const int chance = itemNode.attribute("chance").as_int(1);
		part.items.push_back({ itemId, chance });

		if (part.typeName.IsEmpty()) {
			part.typeName = "wall";
		}
	}
}

void LoadWallBrushes(BrushEditorCatalog &catalog, const wxString &dataDirectory, wxArrayString &warnings) {
	const wxString wallFiles[] = {
		BuildBrushXmlPath(dataDirectory, "walls.xml"),
		BuildBrushXmlPath(dataDirectory, "doodads.xml")
	};
	for (const wxString &sourceFile : wallFiles) {
		pugi::xml_document doc;
		if (!LoadDocumentIfExists(sourceFile, doc, warnings)) {
			continue;
		}
		const pugi::xml_node materials = doc.child("materials");
		if (!materials) {
			warnings.push_back(wxFileName(sourceFile).GetFullName() + ": missing <materials> root");
			continue;
		}
		for (pugi::xml_node brushNode = materials.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
			const wxString brushName = wxString(brushNode.attribute("name").as_string());
			const wxString typeValue = wxString(brushNode.attribute("type").as_string()).Lower();
			if (brushName.IsEmpty() || (typeValue != "wall" && !brushNode.child("wall"))) {
				continue;
			}
			BrushEditorBrushDefinition &definition = catalog.GetOrCreateBrush(brushName);
			if (definition.kind != BrushEditorBrushKind::Wall) {
				definition.variations.clear();
				definition.borders.clear();
				definition.wallParts.clear();
			}
			definition.kind = BrushEditorBrushKind::Wall;
			definition.optional = false;
			definition.ground = false;
			const uint16_t serverLookId = brushNode.attribute("server_lookid").as_uint();
			if (serverLookId != 0) {
				definition.serverLookId = serverLookId;
				if (definition.previewItemId == 0) definition.previewItemId = serverLookId;
			}
			AddStorageLocation(definition, sourceFile, "brush", brushName);
			for (pugi::xml_node wallNode = brushNode.child("wall"); wallNode; wallNode = wallNode.next_sibling("wall")) {
				const wxString typeName = wxString(wallNode.attribute("type").as_string());
				if (typeName.IsEmpty()) continue;
				BrushEditorWallPart part;
				part.typeName = typeName;
				AppendItemsToWallPart(part, wallNode);
				for (pugi::xml_node alternateNode = wallNode.child("alternate"); alternateNode; alternateNode = alternateNode.next_sibling("alternate")) AppendItemsToWallPart(part, alternateNode);
				if (!part.items.empty()) {
					definition.wallParts[typeName] = part;
					if (definition.previewItemId == 0) definition.previewItemId = part.items.front().first;
				}
			}
			for (pugi::xml_node alternateNode = brushNode.child("alternate"); alternateNode; alternateNode = alternateNode.next_sibling("alternate")) {
				BrushEditorWallPart part;
				part.typeName = "alternate";
				AppendItemsToWallPart(part, alternateNode);
				if (!part.items.empty()) {
					definition.wallParts["alternate"] = part;
					if (definition.previewItemId == 0) definition.previewItemId = part.items.front().first;
				}
			}
			ApplyCapabilitiesForKind(definition);
			if (definition.wallParts.empty()) definition.RemoveCapability(BRUSH_CAP_STRUCTURE);
		}
	}
}

void LoadBorderBrushes(BrushEditorCatalog &catalog, const wxString &dataDirectory, wxArrayString &warnings) {
	const wxString bordersFile = BuildBorderXmlPath(dataDirectory);

	pugi::xml_document doc;
	if (!LoadDocumentIfExists(bordersFile, doc, warnings)) {
		return;
	}

	const pugi::xml_node materials = doc.child("materials");
	if (!materials) {
		warnings.push_back("borders.xml: missing <materials> root");
		return;
	}

	for (pugi::xml_node borderNode = materials.child("border"); borderNode; borderNode = borderNode.next_sibling("border")) {
		const int borderId = borderNode.attribute("id").as_int();
		if (borderId <= 0) {
			continue;
		}

		const wxString explicitName = wxString(borderNode.attribute("name").as_string());
		const wxString brushName = explicitName.IsEmpty()
			? wxString::Format("Border %d", borderId)
			: explicitName;

		BrushEditorBrushDefinition &definition = catalog.GetOrCreateBrush(brushName);
		definition.kind = BrushEditorBrushKind::Border;
		definition.group = borderNode.attribute("group").as_int(0);
		definition.optional = wxString(borderNode.attribute("type").as_string()) == "optional";
		definition.ground = borderNode.attribute("ground").as_bool(false);

		AddStorageLocation(definition, bordersFile, "border", wxString::Format("%d", borderId));

		for (pugi::xml_node borderItemNode = borderNode.child("borderitem"); borderItemNode; borderItemNode = borderItemNode.next_sibling("borderitem")) {
			const uint16_t itemId = borderItemNode.attribute("item").as_uint();
			if (itemId != 0) {
				definition.previewItemId = itemId;
				definition.serverLookId = itemId;
				break;
			}
		}

		ApplyCapabilitiesForKind(definition);
	}
}
} // namespace

bool BrushEditorCatalogService::LoadCatalog(const wxString &dataDirectory, BrushEditorCatalog &catalog, wxArrayString &warnings) const {
	catalog.Clear();

	LoadPaletteMembershipsFromMaterials(catalog);
	LoadGroundBrushes(catalog, dataDirectory, warnings);
	LoadWallBrushes(catalog, dataDirectory, warnings);
	LoadBorderBrushes(catalog, dataDirectory, warnings);

	for (BrushEditorBrushDefinition &brush : catalog.brushes) {
		if (!brush.memberships.empty()) {
			brush.AddCapability(BRUSH_CAP_PALETTES);
		}
		if (brush.serverLookId == 0) {
			brush.serverLookId = brush.previewItemId;
		}
	}

	catalog.Sort();
	return true;
}