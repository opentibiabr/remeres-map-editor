#ifndef RME_KMAP_WRITER_HPP_
#define RME_KMAP_WRITER_HPP_

#include "flatbuffers/flatbuffers.h"
#include "flatbuffer/kmap_generated.h"

class KmapWriter {

	public:
		KmapWriter() = default;

		void build(Map& map);
		void save(std::string mapPath);
		uint8_t* getBuffer();
		flatbuffers::uoffset_t getSize();

	private:
		// Makes reading easier and reduces the complexity of returns
		using FlatOffSetHeader = flatbuffers::Offset<Kmap::MapHeader>;
		using FlatOffSetMapData = flatbuffers::Offset<Kmap::MapData>;
		using FlatOffSetTile = flatbuffers::Offset<Kmap::Tile>;
		using FlatOffSetItem = flatbuffers::Offset<Kmap::Item>;
		using FlatOffSetArea = flatbuffers::Offset<Kmap::Area>;
		using FlatOffSetTown = flatbuffers::Offset<Kmap::Town>;
		using FlatOffSetWayPoint = flatbuffers::Offset<Kmap::Waypoint>;
		using FlatOffSetAttributes = flatbuffers::Offset<Kmap::ActionAttributes>;
		using FlatTownVector = flatbuffers::Offset<flatbuffers::Vector<FlatOffSetTown>>;
		using FlatWayPointVector = flatbuffers::Offset<flatbuffers::Vector<FlatOffSetWayPoint>>;
		using FlatAreaVector = flatbuffers::Offset<flatbuffers::Vector<FlatOffSetArea>>;
		using FlatItemVector = flatbuffers::Offset<flatbuffers::Vector<FlatOffSetItem>>;

		flatbuffers::FlatBufferBuilder builder;

		FlatOffSetHeader buildHeader(Map& map);
		FlatOffSetMapData buildMapData(Map &map);
		FlatAreaVector buildArea(Map& map);
		FlatOffSetTile buildTile(Map &map, Tile &tile, const Position& pos);
		bool isNewArea(const Position &pos, Position *areaPos);
		FlatOffSetItem buildGround(Item &item, const Position &pos);
		FlatItemVector buildItems(Tile &tile, const Position& pos);
		FlatOffSetAttributes buildActionAttributes(Item &item, const Position& pos);
		FlatTownVector buildTowns(Map &map);
		FlatWayPointVector buildWaypoints(Map &map);
		std::string getFullFileName(std::string fileName);
};

#endif
