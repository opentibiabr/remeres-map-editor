#ifndef RME_KMAP_WRITER_HPP_
#define RME_KMAP_WRITER_HPP_

#include "flatbuffers/flatbuffers.h"
#include "flatbuffer/kmap_generated.h"

template<class T>
using FBOffset = flatbuffers::Offset<T>;

template<class T>
using FBVector = flatbuffers::Vector<T>;

template<class T>
using FBVectorOffset = FBOffset<FBVector<T>>;

template<class T>
using StdOffsetVector = std::vector<FBOffset<T>>;

class KmapWriter {

	public:
		KmapWriter() = default;

		void build(Map& map);
		void save(std::string mapPath);
		uint8_t* getBuffer();
		flatbuffers::uoffset_t getSize();

	private:
		flatbuffers::FlatBufferBuilder builder;

		FBOffset<Kmap::MapHeader> buildHeader(Map& map);
		FBOffset<Kmap::MapData> buildMapData(Map &map);
		FBVectorOffset<FBOffset<Kmap::Area>> buildAreas(Map& map);
		FBOffset<Kmap::Area> buildArea(const Position pos, Position *areaPos, StdOffsetVector<Kmap::Tile> tilesOffset);
		bool isNewArea(const Position &pos, Position *areaPos);
		FBOffset<Kmap::Tile> buildTile(Tile &tile);
		FBVectorOffset<FBOffset<Kmap::Item>> buildItems(std::vector<Item*> &items);
		FBOffset<Kmap::Item> buildItem(Item &item);
		FBOffset<Kmap::Action> buildActionAttributes(Item &item);
		FBOffset<Kmap::ItemDetails> buildItemsDetails(Item &item);
		FBVectorOffset<FBOffset<Kmap::Town>> buildTowns(Map &map);
		FBVectorOffset<FBOffset<Kmap::Waypoint>> buildWaypoints(Map &map);
		FBOffset<flatbuffers::String> buildString(std::string string);
		FBOffset<Kmap::Position> buildPosition(const Position &pos);
		std::string getFullFileName(std::string fileName);
};

#endif
