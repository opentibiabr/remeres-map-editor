#ifndef RME_KMAP_WRITER_HPP_
#define RME_KMAP_WRITER_HPP_

#include "map.h"
#include "filehandle.h"
#include "flatbuffers/flatbuffers.h"
#include "flatbuffer/map_generated.h"

class KmapWriter {

	public:
		KmapWriter() = default;

		void build(Map& map) {
			auto headerOffset = buildHeader(map);
			MapIterator mapIterator = map.begin();
			auto dataOffset = buildMapData(map);
			builder.Finish(canary::kmap::CreateMap(builder, headerOffset, dataOffset));
		}

		uint8_t* getBuffer() {
			return builder.GetBufferPointer();
		}


	private:
		flatbuffers::FlatBufferBuilder builder;

		flatbuffers::Offset<canary::kmap::MapHeader> buildHeader(Map& map) {
			auto monsterSpawnFile = builder.CreateString(getFullFileName(map.getSpawnFilename()));
			auto npcSpawnFile = builder.CreateString(getFullFileName(map.getSpawnNpcFilename()));
			auto houseFile = builder.CreateString(getFullFileName(map.getHouseFilename()));
			auto description = builder.CreateString("Saved with Remere's Map Editor " + __RME_VERSION__ + "\n" + map.getMapDescription());

			return canary::kmap::CreateMapHeader(
				builder,
				map.getWidth(),
				map.getHeight(),
				map.getVersion().otbm,
				monsterSpawnFile,
				npcSpawnFile,
				houseFile,
				description
			);
		}

		flatbuffers::Offset<canary::kmap::MapData> buildMapData(Map &map) {
			auto areas = buildArea(map);
			auto towns = buildTowns(map);
			auto waypoints = buildWaypoints(map);

			return canary::kmap::CreateMapData(builder, areas, towns, waypoints);
		}

		flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<canary::kmap::Area>>> buildArea(Map& map) {
			std::vector<flatbuffers::Offset<canary::kmap::Area>> areasOffset;

			MapIterator mapIterator = map.begin();
			while(mapIterator != map.end()) {
				int houseInfoOffset = 0;
				// Get tile
				Tile* tile = (*mapIterator)->get();

				const Position& pos = tile->getPosition();

				auto position = canary::kmap::CreatePosition(
					builder,
					pos.x,
					pos.y,
					pos.z
				);

				auto tilesVec = buildTiles(map, *tile, pos);
				auto createAreasOffSet = canary::kmap::CreateArea(builder, tilesVec, position);
				areasOffset.push_back(createAreasOffSet);
			}

			return builder.CreateVector(areasOffset);
		}

		flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<canary::kmap::Tile>>> buildTiles(Map &map, Tile &tile, const Position& pos) {
			std::vector<flatbuffers::Offset<canary::kmap::Tile>> tilesOffset;
			House* house = map.houses.getHouse(tile.getHouseID());
			flatbuffers::Offset<canary::kmap::HouseInfo> houseInfoOffset = canary::kmap::CreateHouseInfo(
				builder,
				tile.getHouseID(),
				house->getEmptyDoorID()
			);

			Item* ground = tile.ground;

			auto createTilesOffset = canary::kmap::CreateTile(
				builder,
				buildItems(tile, pos),
				tile.getX(),
				tile.getY(),
				tile.getMapFlags(),
				ground->getID(),
				houseInfoOffset,
				buildActionAttributes(*ground, pos)
			);

			tilesOffset.push_back(createTilesOffset);

			return builder.CreateVector(tilesOffset);
		}

		flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<canary::kmap::Item>>> buildItems(Tile &tile, const Position& pos) {
			std::vector<flatbuffers::Offset<canary::kmap::Item>> itemsOffset;
			for (Item* item : tile.items) {
				std::string text = item->getText();
				std::string description = item->getDescription();
				auto textOffset = (!text.empty()) ? builder.CreateString(text) : 0;

				uint8_t depotId = 0;
				Depot* depot = dynamic_cast<Depot*>(item);
				if (depot && depot->getDepotID()) {
					depotId = depot->getDepotID();
				}

				auto items = builder.CreateVector(itemsOffset);

				auto createItemsOffset = canary::kmap::CreateItem(
					builder,
					items,
					item->getID(),
					item->getCount(),
					depotId,
					item->getSubtype(),
					textOffset,
					buildActionAttributes(*item, pos)
				);

				//if (is subitem) return canary::kmap::Item.createItemsVector(builder, itemsOffset);
				itemsOffset.push_back(createItemsOffset);
			}

			return builder.CreateVector(itemsOffset);
		}

		flatbuffers::Offset<canary::kmap::ActionAttributes> buildActionAttributes(Item &item, const Position& pos) {
			uint16_t actionId = item.getActionID();
			uint16_t uniqueId = item.getActionID();
			if (actionId == 0 && uniqueId == 0) {
				return {};
			}

			auto positionOffSet = canary::kmap::CreatePosition(
				builder,
				pos.x,
				pos.y,
				pos.z
			);

			return canary::kmap::CreateActionAttributes(
				builder,
				actionId,
				uniqueId,
				positionOffSet
			);
		}

		flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<canary::kmap::Town>>> buildTowns(Map &map) {
			std::vector<flatbuffers::Offset<canary::kmap::Town>> townsOffset;
			for(const auto& [townId, town] : map.towns) {
				const Position& townPosition = town->getTemplePosition();

				auto positionOffSet = canary::kmap::CreatePosition(
					builder,
					townPosition.x,
					townPosition.y,
					townPosition.z
				);

				auto name = builder.CreateString(town->getName());

				auto createTownOffset = canary::kmap::CreateTown(
					builder,
					townId,
					name,
					positionOffSet
				);

				townsOffset.push_back(createTownOffset);

			}

			return builder.CreateVector(townsOffset);
		}

		flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<canary::kmap::Waypoint>>> buildWaypoints(Map &map) {
			std::vector<flatbuffers::Offset<canary::kmap::Waypoint>> waypointsOffSet;
			for(const auto& [waypointId, waypoint] : map.waypoints) {
				auto position = canary::kmap::CreatePosition(
					builder,
					waypoint->pos.x,
					waypoint->pos.y,
					waypoint->pos.z
				);

				auto name = builder.CreateString(waypoint->name);

				auto createWaypointOffSet = canary::kmap::CreateWaypoint(
					builder,
					name,
					position
				);

				waypointsOffSet.push_back(createWaypointOffSet);
			}

			return builder.CreateVector(waypointsOffSet);
		}

		
		std::string getFullFileName(std::string fileName) {
			FileName tmpName;
			tmpName.Assign(wxstr(fileName));
			return nstr(tmpName.GetFullName());
		};
};

#endif
