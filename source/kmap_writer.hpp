#ifndef RME_KMAP_WRITER_HPP_
#define RME_KMAP_WRITER_HPP_

#include "map.h"
#include "filehandle.h"
#include "flatbuffers/flatbuffers.h"
#include "flatbuffer/map_generated.h"
#include "gui.h"

class KmapWriter {

	public:
		KmapWriter() = default;

		void build(Map& map) {
			auto headerOffset = buildHeader(map);
			MapIterator mapIterator = map.begin();
			auto dataOffset = buildMapData(map);
			builder.Finish(canary::kmap::CreateMap(builder, headerOffset, dataOffset));
		}

		void saveKmap() {
			FileName dataDirectory = GUI::GetLocalDataDirectory() + "map.kmap";
			auto mapPath = nstr(dataDirectory.GetFullName());
			std::ofstream ofs(mapPath, std::ofstream::binary);
			ofs.write((char*)builder.GetBufferPointer(), builder.GetSize());
			ofs.close();
			wxMessageBox(mapPath);
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
			std::vector<flatbuffers::Offset<canary::kmap::Tile>> tilesOffset;

			int local_x = -1, local_y = -1, local_z = -1;
			MapIterator mapIterator = map.begin();
			while(mapIterator != map.end()) {
				Tile* tile = (*mapIterator)->get();
				if(tile == nullptr || tile->size() == 0) {
					++mapIterator;
					continue;
				}

				const Position& pos = tile->getPosition();

				if(pos.x < local_x || pos.x >= local_x + 256 || pos.y < local_y || pos.y >= local_y + 256 || pos.z != local_z) {
					auto position = canary::kmap::CreatePosition(
						builder,
						local_x = pos.x & 0xFF00,
						local_y = pos.y & 0xFF00,
						local_z = pos.z
					);

					auto tileVec = builder.CreateVector(tilesOffset);
					areasOffset.push_back(canary::kmap::CreateArea(builder, tileVec, position));
					tilesOffset.clear();
				}

				House* house = map.houses.getHouse(tile->getHouseID());
				flatbuffers::Offset<canary::kmap::HouseInfo> houseInfoOffset = house ? canary::kmap::CreateHouseInfo(
					builder,
					tile->getHouseID(),
					house->getEmptyDoorID()
				) : 0;

				Item * ground = tile->ground;
				if (ground) {
					auto createActionOffSet = buildActionAttributes(*tile->ground, pos);
					auto createTilesOffset = canary::kmap::CreateTile(
						builder,
						buildItems(*tile, pos),
						tile->getX() & 0xFF,
						tile->getY() & 0xFF,
						tile->getMapFlags(),
						ground->getID(),
						houseInfoOffset,
						createActionOffSet
					);

					tilesOffset.push_back(createTilesOffset);
				}
				++mapIterator;
			}

			return builder.CreateVector(areasOffset);
		}

		flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<canary::kmap::Tile>>> buildTiles(Map &map, Tile &tile, const Position& pos) {
			std::vector<flatbuffers::Offset<canary::kmap::Tile>> tilesOffset;
			House* house = map.houses.getHouse(tile.getHouseID());
			flatbuffers::Offset<canary::kmap::HouseInfo> houseInfoOffset = house ? canary::kmap::CreateHouseInfo(
				builder,
				tile.getHouseID(),
				house->getEmptyDoorID()
			) : 0;

			Item* ground = tile.ground;

			auto createActionOffSet = buildActionAttributes(*ground, pos);

			auto createTilesOffset = ground ? canary::kmap::CreateTile(
				builder,
				buildItems(tile, pos),
				tile.getX(),
				tile.getY(),
				tile.getMapFlags(),
				ground->getID(),
				houseInfoOffset,
				createActionOffSet
			) : 0;

			tilesOffset.push_back(createTilesOffset);

			return builder.CreateVector(tilesOffset);
		}

		flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<canary::kmap::Item>>> buildItems(Tile &tile, const Position& pos) {
			std::vector<flatbuffers::Offset<canary::kmap::Item>> itemsOffset;
			for (Item* item : tile.items) {
				if (item == nullptr) {
					continue;
				}

				std::string text = item->getText();
				std::string description = item->getDescription();
				auto textOffset = (!text.empty()) ? builder.CreateString(text) : 0;

				uint8_t depotId = 0;
				Depot* depot = dynamic_cast<Depot*>(item);
				if (depot && depot->getDepotID()) {
					depotId = depot->getDepotID();
				}

				auto createActionOffSet = buildActionAttributes(*item, pos);

				auto createItemsOffset = canary::kmap::CreateItem(
					builder,
					0,
					item->getID(),
					item->getCount(),
					depotId,
					item->getSubtype(),
					textOffset,
					createActionOffSet
				);

				//if (is subitem) return canary::kmap::Item.createItemsVector(builder, itemsOffset);
				itemsOffset.push_back(createItemsOffset);
			}

			return builder.CreateVector(itemsOffset);
		}

		flatbuffers::Offset<canary::kmap::ActionAttributes> buildActionAttributes(Item &item, const Position& pos) {
			uint16_t actionId = item.getActionID();
			uint16_t uniqueId = item.getActionID();
			if (actionId == 0 || uniqueId == 0) {
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
