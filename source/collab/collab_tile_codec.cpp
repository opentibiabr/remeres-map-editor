//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "collab_tile_codec.h"

#include "collab_protocol.h"

#include "../filehandle.h"
#include "../iomap.h"
#include "../iomap_otbm.h"
#include "../item.h"
#include "../map.h"
#include "../monster.h"
#include "../npc.h"
#include "../spawn_monster.h"
#include "../spawn_npc.h"
#include "../tile.h"

namespace collab {

	namespace {
		constexpr size_t kMaxItemStream = 512 * 1024;
		constexpr size_t kMaxCreatures = 256;
		constexpr size_t kMaxZones = 1024;
		constexpr size_t kMaxCreatureName = 255;

		bool isBlank(const Tile* tile) {
			return !tile || (!tile->ground && tile->items.empty() && tile->monsters.empty() && !tile->spawnMonster && !tile->npc && !tile->spawnNpc && tile->house_id == 0 && tile->getMapFlags() == 0 && tile->zones.empty());
		}

		// The same OTBM node layout the map file uses for a tile, minus position and house.
		std::string encodeItems(const Tile* tile, const IOMap &io) {
			MemoryNodeFileWriteHandle writer;
			writer.addNode(OTBM_TILE);
			if (tile->ground) {
				if (tile->ground->isComplex()) {
					tile->ground->serializeItemNode_OTBM(io, writer);
				} else {
					writer.addByte(OTBM_ATTR_ITEM);
					tile->ground->serializeItemCompact_OTBM(io, writer);
				}
			}
			for (const Item* item : tile->items) {
				if (item) {
					item->serializeItemNode_OTBM(io, writer);
				}
			}
			writer.endNode();
			return std::string(reinterpret_cast<const char*>(writer.getMemory()), writer.getSize());
		}

		void decodeItems(Tile* tile, const std::string &stream, const IOMap &io) {
			MemoryNodeFileReadHandle reader(reinterpret_cast<const uint8_t*>(stream.data()), stream.size());
			BinaryNode* node = reader.getRootNode();
			if (!node) {
				throw ProtocolError("missing item stream");
			}

			uint8_t type = 0;
			if (!node->getByte(type) || type != OTBM_TILE) {
				throw ProtocolError("invalid item stream");
			}

			uint8_t attribute;
			while (node->getU8(attribute)) {
				if (attribute != OTBM_ATTR_ITEM) {
					throw ProtocolError("unknown tile attribute");
				}
				if (Item* item = Item::Create_OTBM(io, node)) {
					tile->addItem(item);
				}
			}

			BinaryNode* itemNode = node->getChild();
			if (itemNode) {
				do {
					uint8_t itemType = 0;
					if (!itemNode->getByte(itemType) || itemType != OTBM_ITEM) {
						throw ProtocolError("invalid item node");
					}
					if (Item* item = Item::Create_OTBM(io, itemNode)) {
						if (!item->unserializeItemNode_OTBM(io, itemNode)) {
							delete item;
							throw ProtocolError("invalid item attributes");
						}
						tile->addItem(item);
					}
				} while (itemNode->advance());
			}
		}

		Direction readDirection(ByteReader &r) {
			const uint8_t direction = r.u8();
			if (direction > DIRECTION_LAST) {
				throw ProtocolError("invalid direction");
			}
			return static_cast<Direction>(direction);
		}

		void fill(Tile* tile, const std::string &bytes, const IOMap &io) {
			ByteReader r(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());

			tile->house_id = r.u32();
			tile->setMapFlags(static_cast<uint16_t>(r.u32() & 0xFFFF));
			decodeItems(tile, r.blob(kMaxItemStream), io);

			const uint16_t zoneCount = r.u16();
			if (zoneCount > kMaxZones) {
				throw ProtocolError("too many zones");
			}
			for (uint16_t i = 0; i < zoneCount; ++i) {
				tile->addZone(r.u32());
			}

			if (r.u8()) {
				const uint16_t size = r.u16();
				if (size >= 100) {
					throw ProtocolError("invalid spawn radius");
				}
				tile->spawnMonster = newd SpawnMonster(size);
			}

			const uint16_t monsterCount = r.u16();
			if (monsterCount > kMaxCreatures) {
				throw ProtocolError("too many monsters");
			}
			for (uint16_t i = 0; i < monsterCount; ++i) {
				auto* monster = newd Monster(r.str(kMaxCreatureName));
				monster->setSpawnMonsterTime(r.u16());
				monster->setDirection(readDirection(r));
				monster->setWeight(r.u8());
				tile->addMonster(monster);
			}

			if (r.u8()) {
				const uint16_t size = r.u16();
				if (size >= 100) {
					throw ProtocolError("invalid spawn radius");
				}
				tile->spawnNpc = newd SpawnNpc(size);
			}

			if (r.u8()) {
				auto* npc = newd Npc(r.str(kMaxCreatureName));
				npc->setSpawnNpcTime(static_cast<int>(r.u32()));
				npc->setDirection(readDirection(r));
				tile->npc = npc;
			}

			if (r.remaining() != 0) {
				throw ProtocolError("trailing bytes in tile");
			}
		}
	}

	std::string encodeTile(const Tile* tile, const IOMap &io) {
		if (isBlank(tile)) {
			return std::string();
		}

		ByteWriter w;
		w.u32(tile->house_id);
		w.u32(tile->getMapFlags());
		w.blob(encodeItems(tile, io));

		// std::set: already sorted, so the bytes are deterministic
		w.u16(static_cast<uint16_t>(tile->zones.size()));
		for (unsigned int zone : tile->zones) {
			w.u32(zone);
		}

		w.u8(tile->spawnMonster ? 1 : 0);
		if (tile->spawnMonster) {
			w.u16(static_cast<uint16_t>(tile->spawnMonster->getSize()));
		}

		w.u16(static_cast<uint16_t>(tile->monsters.size()));
		for (const Monster* monster : tile->monsters) {
			w.str(monster->getTypeName());
			w.u16(monster->getSpawnMonsterTime());
			w.u8(static_cast<uint8_t>(monster->getDirection()));
			w.u8(static_cast<uint8_t>(monster->getWeight()));
		}

		w.u8(tile->spawnNpc ? 1 : 0);
		if (tile->spawnNpc) {
			w.u16(static_cast<uint16_t>(tile->spawnNpc->getSize()));
		}

		w.u8(tile->npc ? 1 : 0);
		if (tile->npc) {
			w.str(tile->npc->getTypeName());
			w.u32(static_cast<uint32_t>(tile->npc->getSpawnNpcTime()));
			w.u8(static_cast<uint8_t>(tile->npc->getDirection()));
		}
		return std::string(w.buffer.begin(), w.buffer.end());
	}

	bool sameCreatures(const Tile* a, const Tile* b) {
		const bool hasA = a != nullptr;
		const bool hasB = b != nullptr;
		const SpawnMonster* spawnA = hasA ? a->spawnMonster : nullptr;
		const SpawnMonster* spawnB = hasB ? b->spawnMonster : nullptr;
		if ((spawnA != nullptr) != (spawnB != nullptr) || (spawnA && spawnA->getSize() != spawnB->getSize())) {
			return false;
		}
		const SpawnNpc* npcSpawnA = hasA ? a->spawnNpc : nullptr;
		const SpawnNpc* npcSpawnB = hasB ? b->spawnNpc : nullptr;
		if ((npcSpawnA != nullptr) != (npcSpawnB != nullptr) || (npcSpawnA && npcSpawnA->getSize() != npcSpawnB->getSize())) {
			return false;
		}
		const Npc* npcA = hasA ? a->npc : nullptr;
		const Npc* npcB = hasB ? b->npc : nullptr;
		if ((npcA != nullptr) != (npcB != nullptr)) {
			return false;
		}
		if (npcA && (npcA->getTypeName() != npcB->getTypeName() || npcA->getSpawnNpcTime() != npcB->getSpawnNpcTime() || npcA->getDirection() != npcB->getDirection())) {
			return false;
		}
		const size_t countA = hasA ? a->monsters.size() : 0;
		const size_t countB = hasB ? b->monsters.size() : 0;
		if (countA != countB) {
			return false;
		}
		for (size_t i = 0; i < countA; ++i) {
			const Monster* x = a->monsters[i];
			const Monster* y = b->monsters[i];
			if (x->getTypeName() != y->getTypeName() || x->getSpawnMonsterTime() != y->getSpawnMonsterTime() || x->getDirection() != y->getDirection() || x->getWeight() != y->getWeight()) {
				return false;
			}
		}
		return true;
	}

	Tile* decodeTile(Map &map, const Position &pos, const std::string &bytes, const IOMap &io) {
		Tile* tile = map.allocator(map.createTileL(pos));
		if (bytes.empty()) {
			return tile;
		}
		try {
			fill(tile, bytes, io);
		} catch (...) {
			delete tile;
			throw;
		}
		return tile;
	}

} // namespace collab
