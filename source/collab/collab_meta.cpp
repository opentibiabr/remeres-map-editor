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

#include "collab_meta.h"

#include "../house.h"
#include "../map.h"
#include "../town.h"
#include "../waypoints.h"
#include "../zones.h"

namespace collab {

	namespace {
		constexpr size_t kMaxKey = 255;
		constexpr size_t kMaxData = 64 * 1024;

		std::pair<uint8_t, std::string> keyOf(MetaKind kind, const std::string &key) {
			return { static_cast<uint8_t>(kind), key };
		}

		void writePosition(ByteWriter &w, const Position &pos) {
			w.u16(static_cast<uint16_t>(pos.x));
			w.u16(static_cast<uint16_t>(pos.y));
			w.u8(static_cast<uint8_t>(pos.z));
		}

		Position readPosition(ByteReader &r) {
			const int x = r.u16();
			const int y = r.u16();
			const int z = r.u8();
			return Position(x, y, z);
		}

		uint32_t parseId(const std::string &key) {
			char* end = nullptr;
			const unsigned long id = std::strtoul(key.c_str(), &end, 10);
			if (key.empty() || *end != '\0' || id == 0 || id > 0xFFFFFFFFul) {
				throw ProtocolError("invalid id");
			}
			return static_cast<uint32_t>(id);
		}

		std::string bytes(const ByteWriter &w) {
			return std::string(w.buffer.begin(), w.buffer.end());
		}

		std::string encodeHouse(const House &house) {
			ByteWriter w;
			w.str(house.name);
			w.u32(house.townid);
			w.u32(static_cast<uint32_t>(house.rent));
			w.u8(house.guildhall ? 1 : 0);
			w.u32(static_cast<uint32_t>(house.clientid));
			w.u32(static_cast<uint32_t>(house.beds));
			writePosition(w, house.getExit());
			return bytes(w);
		}

		std::string encodeTown(const Town &town) {
			ByteWriter w;
			w.str(town.getName());
			writePosition(w, town.getTemplePosition());
			return bytes(w);
		}

		std::string encodeWaypoint(const Waypoint &waypoint) {
			ByteWriter w;
			w.str(waypoint.name);
			writePosition(w, waypoint.pos);
			return bytes(w);
		}

		std::string encodeZone(unsigned int id) {
			ByteWriter w;
			w.u32(id);
			return bytes(w);
		}

		std::string encodeProps(const Map &map) {
			ByteWriter w;
			w.blob(map.getMapDescription());
			w.u16(static_cast<uint16_t>(map.getWidth()));
			w.u16(static_cast<uint16_t>(map.getHeight()));
			return bytes(w);
		}

		const std::string kPropsKey = "map";
	}

	MetaState captureMeta(Map &map) {
		MetaState state;
		for (const auto &entry : map.houses) {
			state[keyOf(MetaKind::House, std::to_string(entry.first))] = encodeHouse(*entry.second);
		}
		for (const auto &entry : map.towns) {
			state[keyOf(MetaKind::Town, std::to_string(entry.first))] = encodeTown(*entry.second);
		}
		for (const auto &entry : map.waypoints.waypoints) {
			state[keyOf(MetaKind::Waypoint, entry.first)] = encodeWaypoint(*entry.second);
		}
		for (const auto &entry : map.zones.zones) {
			state[keyOf(MetaKind::Zone, entry.first)] = encodeZone(entry.second);
		}
		state[keyOf(MetaKind::MapProps, kPropsKey)] = encodeProps(map);
		return state;
	}

	std::optional<std::string> captureMetaEntry(Map &map, MetaKind kind, const std::string &key) {
		switch (kind) {
			case MetaKind::House: {
				const House* house = map.houses.getHouse(parseId(key));
				return house ? std::optional<std::string>(encodeHouse(*house)) : std::nullopt;
			}
			case MetaKind::Town: {
				const Town* town = map.towns.getTown(parseId(key));
				return town ? std::optional<std::string>(encodeTown(*town)) : std::nullopt;
			}
			case MetaKind::Waypoint: {
				auto it = map.waypoints.waypoints.find(key);
				return it != map.waypoints.waypoints.end() ? std::optional<std::string>(encodeWaypoint(*it->second)) : std::nullopt;
			}
			case MetaKind::Zone: {
				auto it = map.zones.zones.find(key);
				return it != map.zones.zones.end() ? std::optional<std::string>(encodeZone(it->second)) : std::nullopt;
			}
			case MetaKind::MapProps:
				return encodeProps(map);
			default:
				return std::nullopt;
		}
	}

	std::vector<MetaOp> diffMeta(const MetaState &before, const MetaState &after) {
		std::vector<MetaOp> ops;
		for (const auto &entry : after) {
			auto it = before.find(entry.first);
			if (it == before.end() || it->second != entry.second) {
				ops.push_back({ static_cast<MetaKind>(entry.first.first), false, entry.first.second, entry.second });
			}
		}
		for (const auto &entry : before) {
			if (after.find(entry.first) == after.end()) {
				ops.push_back({ static_cast<MetaKind>(entry.first.first), true, entry.first.second, std::string() });
			}
		}
		return ops;
	}

	void applyMetaOp(Map &map, const MetaOp &op) {
		switch (op.kind) {
			case MetaKind::House: {
				const uint32_t id = parseId(op.key);
				House* house = map.houses.getHouse(id);
				if (op.remove) {
					if (house) {
						map.houses.removeHouse(house);
					}
					return;
				}

				ByteReader r(reinterpret_cast<const uint8_t*>(op.data.data()), op.data.size());
				const std::string name = r.str(kMaxKey);
				const uint32_t townid = r.u32();
				const int rent = static_cast<int>(r.u32());
				const bool guildhall = r.u8() != 0;
				const int clientid = static_cast<int>(r.u32());
				const int beds = static_cast<int>(r.u32());
				const Position exit = readPosition(r);

				if (!house) {
					house = newd House(map);
					house->id = id;
					map.houses.addHouse(house);
				}
				house->name = name;
				house->townid = townid;
				house->rent = rent;
				house->guildhall = guildhall;
				house->clientid = clientid;
				house->beds = beds;
				if (exit.isValid()) {
					house->setExit(exit);
				}
				return;
			}
			case MetaKind::Town: {
				const uint32_t id = parseId(op.key);
				if (op.remove) {
					auto it = map.towns.find(id);
					if (it != map.towns.end()) {
						delete it->second;
						map.towns.erase(it);
					}
					return;
				}

				ByteReader r(reinterpret_cast<const uint8_t*>(op.data.data()), op.data.size());
				const std::string name = r.str(kMaxKey);
				const Position temple = readPosition(r);
				Town* town = map.towns.getTown(id);
				if (!town) {
					town = newd Town(id);
					map.towns.addTown(town);
				}
				town->setName(name);
				town->setTemplePosition(temple);
				return;
			}
			case MetaKind::Waypoint: {
				if (op.remove) {
					map.waypoints.removeWaypoint(op.key);
					return;
				}
				ByteReader r(reinterpret_cast<const uint8_t*>(op.data.data()), op.data.size());
				auto* waypoint = newd Waypoint();
				waypoint->name = r.str(kMaxKey);
				waypoint->pos = readPosition(r);
				map.waypoints.addWaypoint(waypoint); // replaces a waypoint with the same name
				return;
			}
			case MetaKind::Zone: {
				if (op.remove) {
					map.zones.removeZone(op.key);
					return;
				}
				ByteReader r(reinterpret_cast<const uint8_t*>(op.data.data()), op.data.size());
				const uint32_t id = r.u32();
				if (map.zones.getZoneID(op.key) != id) {
					map.zones.removeZone(op.key);
					map.zones.addZone(op.key, id); // false when another zone owns the id: leave it out
				}
				return;
			}
			case MetaKind::MapProps: {
				if (op.remove) {
					return;
				}
				ByteReader r(reinterpret_cast<const uint8_t*>(op.data.data()), op.data.size());
				const std::string description = r.blob(kMaxData);
				const uint16_t width = r.u16();
				const uint16_t height = r.u16();
				if (width == 0 || height == 0) {
					throw ProtocolError("invalid map size");
				}
				map.setMapDescription(description);
				map.setWidth(width);
				map.setHeight(height);
				return;
			}
			default:
				throw ProtocolError("unknown entry kind");
		}
	}

	void attachPendingHouseTiles(Map &map, PendingHouseTiles &pending) {
		for (auto it = pending.begin(); it != pending.end();) {
			House* house = map.houses.getHouse(it->first);
			if (!house) {
				++it;
				continue;
			}
			for (const Position &pos : it->second) {
				Tile* tile = map.getTile(pos);
				if (tile && tile->getHouseID() == it->first) {
					house->addTile(tile);
				}
			}
			it = pending.erase(it);
		}
	}

	void writeMetaOps(ByteWriter &w, const std::vector<MetaOp> &ops) {
		w.u16(static_cast<uint16_t>(ops.size()));
		for (const MetaOp &op : ops) {
			w.u8(static_cast<uint8_t>(op.kind));
			w.u8(op.remove ? 1 : 0);
			w.str(op.key);
			w.blob(op.data);
		}
	}

	std::vector<MetaOp> readMetaOps(ByteReader &r) {
		const uint16_t count = r.u16();
		if (count > kMaxMetaOps) {
			throw ProtocolError("too many entries");
		}
		std::vector<MetaOp> ops;
		ops.reserve(count);
		for (uint16_t i = 0; i < count; ++i) {
			MetaOp op;
			const uint8_t kind = r.u8();
			if (kind >= static_cast<uint8_t>(MetaKind::Count)) {
				throw ProtocolError("unknown entry kind");
			}
			op.kind = static_cast<MetaKind>(kind);
			op.remove = r.u8() != 0;
			op.key = r.str(kMaxKey);
			op.data = r.blob(kMaxData);
			ops.push_back(std::move(op));
		}
		return ops;
	}

} // namespace collab
