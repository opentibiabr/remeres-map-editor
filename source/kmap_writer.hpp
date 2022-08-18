#ifndef RME_KMAP_WRITER_HPP_
#define RME_KMAP_WRITER_HPP_

#include "map.h"
#include "filehandle.h"
#include "flatbuffers/flatbuffers.h"
#include "flatbuffer/kmap_generated.h"
#include "gui.h"

class KmapWriter {

	public:
		KmapWriter() = default;

		void build(Map& map) {
			auto headerOffset = buildHeader(map);
			MapIterator mapIterator = map.begin();
			auto dataOffset = buildMapData(map);
			builder.Finish(Kmap::CreateMap(builder, headerOffset, dataOffset));
			save(map);
		}

		void save(Map& map) {
			auto mapFullPath = map.getPath() + ".kmap";
			std::ofstream ofs(mapFullPath, std::ofstream::binary);
			ofs.write((char*)getBuffer(), getSize());
			ofs.close();
			wxMessageBox(map.getPath()+ ".kmap");
		}

		uint8_t* getBuffer() {
			return builder.GetBufferPointer();
		}

		flatbuffers::uoffset_t getSize() {
			return builder.GetSize();
		}

	private:
		flatbuffers::FlatBufferBuilder builder;

		flatbuffers::Offset<Kmap::MapHeader> buildHeader(Map& map) {
			auto monsterSpawnFile = builder.CreateString(getFullFileName(map.getSpawnFilename()));
			auto npcSpawnFile = builder.CreateString(getFullFileName(map.getSpawnNpcFilename()));
			auto houseFile = builder.CreateString(getFullFileName(map.getHouseFilename()));
			auto description = builder.CreateString("Saved with Remere's Map Editor " + __RME_VERSION__ + "\n" + map.getMapDescription());

			return Kmap::CreateMapHeader(
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

		flatbuffers::Offset<Kmap::MapData> buildMapData(Map &map) {
			auto areas = buildArea(map);
			auto towns = buildTowns(map);
			auto waypoints = buildWaypoints(map);

			return Kmap::CreateMapData(builder, areas, towns, waypoints);
		}

		flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<Kmap::Area>>> buildArea(Map& map) {
			std::vector<flatbuffers::Offset<Kmap::Area>> areasOffset;
			std::vector<flatbuffers::Offset<Kmap::Tile>> tilesOffset;

			Position *areaPos;
			MapIterator mapIterator = map.begin();

			while(mapIterator != map.end()) {
				Tile* tile = (*mapIterator)->get();
				if(tile == nullptr || tile->size() == 0) {
					++mapIterator;
					continue;
				}

				const Position& pos = tile->getPosition();

				if(isNewArea(pos, areaPos)) {
					auto position = Kmap::CreatePosition(
						builder,
						areaPos->x & 0xFF00,
						areaPos->y & 0xFF00,
						areaPos->z
					);

					areaPos->x = pos.x;
					areaPos->y = pos.y;
					areaPos->z = pos.z;

					auto tiles = builder.CreateVector(tilesOffset);

					areasOffset.push_back(Kmap::CreateArea(builder, tiles));

					tilesOffset.clear();
				}

				if (!areaPos) {
					areaPos = &Position(pos.x, pos.y, pos.z);
				}

				tilesOffset.push_back(buildTile(map, *tile, pos));
				++mapIterator;
			}

			return builder.CreateVector(areasOffset);
		}

		flatbuffers::Offset<Kmap::Tile> buildTile(Map &map, Tile &tile, const Position& pos) {
			Item* ground = tile.ground;
			if (!ground) return 0;

			auto items = buildItems(tile, pos);

			House* house = map.houses.getHouse(tile.getHouseID());
			auto houseInfoOffset = house ? Kmap::CreateHouseInfo(
				builder,
				tile.getHouseID(),
				house->getEmptyDoorID()
			) : 0;

			auto createActionOffSet = buildActionAttributes(*ground, pos);
			return Kmap::CreateTile(
				builder,
				items,
				tile.getX() & 0xFF,
				tile.getY() & 0xFF,
				tile.getMapFlags(),
				ground->getID(),
				houseInfoOffset,
				createActionOffSet
			);
		}

		bool isNewArea(const Position &pos, Position *areaPos) {
			if (!areaPos) return false;

			return pos.x < areaPos->x
				|| pos.x >= areaPos->x + 256
				|| pos.y < areaPos->y
				|| pos.y >= areaPos->y + 256
				|| pos.z != areaPos->z;
		}

		flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<Kmap::Item>>> buildItems(Tile &tile, const Position& pos) {
			std::vector<flatbuffers::Offset<Kmap::Item>> itemsOffset;
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

				auto createItemsOffset = Kmap::CreateItem(
					builder,
					0,
					item->getID(),
					item->getCount(),
					depotId,
					item->getSubtype(),
					textOffset,
					createActionOffSet
				);

				//if (is subitem) return Kmap::Item.createItemsVector(builder, itemsOffset);
				itemsOffset.push_back(createItemsOffset);
			}

			return builder.CreateVector(itemsOffset);
		}

		flatbuffers::Offset<Kmap::ActionAttributes> buildActionAttributes(Item &item, const Position& pos) {
			uint16_t actionId = item.getActionID();
			uint16_t uniqueId = item.getActionID();
			if (actionId == 0 || uniqueId == 0) {
				return {};
			}

			auto positionOffSet = Kmap::CreatePosition(
				builder,
				pos.x,
				pos.y,
				pos.z
			);

			return Kmap::CreateActionAttributes(
				builder,
				actionId,
				uniqueId,
				positionOffSet
			);
		}

		flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<Kmap::Town>>> buildTowns(Map &map) {
			std::vector<flatbuffers::Offset<Kmap::Town>> townsOffset;
			for(const auto& [townId, town] : map.towns) {
				const Position& townPosition = town->getTemplePosition();

				auto positionOffSet = Kmap::CreatePosition(
					builder,
					townPosition.x,
					townPosition.y,
					townPosition.z
				);

				auto name = builder.CreateString(town->getName());

				auto createTownOffset = Kmap::CreateTown(
					builder,
					townId,
					name,
					positionOffSet
				);

				townsOffset.push_back(createTownOffset);

			}

			return builder.CreateVector(townsOffset);
		}

		flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<Kmap::Waypoint>>> buildWaypoints(Map &map) {
			std::vector<flatbuffers::Offset<Kmap::Waypoint>> waypointsOffSet;
			for(const auto& [waypointId, waypoint] : map.waypoints) {
				auto position = Kmap::CreatePosition(
					builder,
					waypoint->pos.x,
					waypoint->pos.y,
					waypoint->pos.z
				);

				auto name = builder.CreateString(waypoint->name);

				auto createWaypointOffSet = Kmap::CreateWaypoint(
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
