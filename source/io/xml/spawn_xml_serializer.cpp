#include "app/main.h"

#include "io/xml/spawn_xml_serializer.h"
#include "app/settings.h"
#include "game/monsters.h"
#include "game/monster.h"
#include "game/npcs.h"
#include "game/npc.h"
#include "game/spawn_monster.h"
#include "game/spawn_npc.h"
#include "map/map.h"
#include "map/tile.h"

bool SpawnXmlSerializer::loadMonsters(Map &map, const FileName &dir, wxArrayString &warnings) {
	std::string fn = (const char*)(dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME).mb_str(wxConvUTF8));
	fn += map.spawnmonsterfile;

	FileName filename(wxstr(fn));
	if (!filename.FileExists()) {
		return false;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(fn.c_str());
	if (!result) {
		return false;
	}
	return loadMonsters(map, doc, warnings);
}

bool SpawnXmlSerializer::loadMonsters(Map &map, pugi::xml_document &doc, wxArrayString &warnings) {
	pugi::xml_node node = doc.child("monsters");
	if (!node) {
		warnings.push_back("IOMapOTBM::loadSpawnsMonster: Invalid rootheader.");
		return false;
	}

	for (pugi::xml_node spawnNode = node.first_child(); spawnNode; spawnNode = spawnNode.next_sibling()) {
		if (as_lower_str(spawnNode.name()) != "monster") {
			continue;
		}

		Position spawnPosition;
		spawnPosition.x = spawnNode.attribute("centerx").as_int();
		spawnPosition.y = spawnNode.attribute("centery").as_int();
		spawnPosition.z = spawnNode.attribute("centerz").as_int();

		if (spawnPosition.x == 0 || spawnPosition.y == 0) {
			warnings.push_back("Bad position data on one monster spawn, discarding...");
			continue;
		}

		int32_t radius = spawnNode.attribute("radius").as_int();
		if (radius < 1) {
			warnings.push_back("Couldn't read radius of monster spawn.. discarding spawn...");
			continue;
		}

		Tile* tile = map.getTile(spawnPosition);
		if (tile && tile->spawnMonster) {
			warnings.push_back(fmt::format("Duplicate monster spawn on position {}:{}:{}", tile->getX(), tile->getY(), tile->getZ()));
			continue;
		}

		SpawnMonster* spawnMonster = newd SpawnMonster(radius);
		if (!tile) {
			tile = map.allocator(map.createTileL(spawnPosition));
			map.setTile(spawnPosition, tile);
		}

		tile->spawnMonster = spawnMonster;
		map.addSpawnMonster(tile);

		for (pugi::xml_node monsterNode = spawnNode.first_child(); monsterNode; monsterNode = monsterNode.next_sibling()) {
			const std::string &monsterNodeName = as_lower_str(monsterNode.name());
			if (monsterNodeName != "monster") {
				continue;
			}

			const std::string &name = monsterNode.attribute("name").as_string();
			if (name.empty()) {
				wxString err;
				err << "Bad monster position data, discarding monster at spawn " << spawnPosition.x << ":" << spawnPosition.y << ":" << spawnPosition.z << " due to missing name.";
				warnings.Add(err);
				break;
			}

			uint16_t spawntime = monsterNode.attribute("spawntime").as_uint(0);
			if (spawntime == 0) {
				spawntime = g_settings.getInteger(Config::DEFAULT_SPAWN_MONSTER_TIME);
			}

			auto weight = static_cast<uint8_t>(monsterNode.attribute("weight").as_uint());
			if (weight == 0) {
				weight = static_cast<uint8_t>(g_settings.getInteger(Config::MONSTER_DEFAULT_WEIGHT));
			}

			Direction direction = NORTH;
			int dir = monsterNode.attribute("direction").as_int(-1);
			if (dir >= DIRECTION_FIRST && dir <= DIRECTION_LAST) {
				direction = (Direction)dir;
			}

			Position monsterPosition(spawnPosition);

			pugi::xml_attribute xAttribute = monsterNode.attribute("x");
			pugi::xml_attribute yAttribute = monsterNode.attribute("y");
			if (!xAttribute || !yAttribute) {
				wxString err;
				err << "Bad monster position data, discarding monster \"" << name << "\" at spawn " << monsterPosition.x << ":" << monsterPosition.y << ":" << monsterPosition.z << " due to invalid position.";
				warnings.Add(err);
				break;
			}

			monsterPosition.x += xAttribute.as_int();
			monsterPosition.y += yAttribute.as_int();

			radius = std::max<int32_t>(radius, std::abs(monsterPosition.x - spawnPosition.x));
			radius = std::max<int32_t>(radius, std::abs(monsterPosition.y - spawnPosition.y));
			radius = std::min<int32_t>(radius, g_settings.getInteger(Config::MAX_SPAWN_MONSTER_RADIUS));

			Tile* monsterTile;
			if (monsterPosition == spawnPosition) {
				monsterTile = tile;
			} else {
				monsterTile = map.getTile(monsterPosition);
			}

			if (!monsterTile) {
				const auto error = fmt::format("Discarding monster \"{}\" at {}:{}:{} due to invalid position", name, monsterPosition.x, monsterPosition.y, monsterPosition.z);
				warnings.Add(error);
				break;
			}

			if (monsterTile->isMonsterRepeated(name)) {
				const auto error = fmt::format("Duplicate monster \"{}\" at {}:{}:{} was discarded.", name, monsterPosition.x, monsterPosition.y, monsterPosition.z);
				warnings.Add(error);
				break;
			}

			MonsterType* type = g_monsters[name];
			if (!type) {
				type = g_monsters.addMissingMonsterType(name);
			}

			Monster* monster = newd Monster(type);
			monster->setDirection(direction);
			monster->setSpawnMonsterTime(spawntime);
			monster->setWeight(weight);
			monsterTile->monsters.emplace_back(monster);

			if (monsterTile->getLocation()->getSpawnMonsterCount() == 0) {
				// No monster spawn, create a newd one
				ASSERT(monsterTile->spawnMonster == nullptr);
				SpawnMonster* spawnMonster = newd SpawnMonster(1);
				monsterTile->spawnMonster = spawnMonster;
				map.addSpawnMonster(monsterTile);
			}
		}
	}
	return true;
}

bool SpawnXmlSerializer::loadNpcs(Map &map, const FileName &dir, wxArrayString &warnings) {
	std::string fn = (const char*)(dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME).mb_str(wxConvUTF8));
	fn += map.spawnnpcfile;

	FileName filename(wxstr(fn));
	if (!filename.FileExists()) {
		return false;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(fn.c_str());
	if (!result) {
		return false;
	}
	return loadNpcs(map, doc, warnings);
}

bool SpawnXmlSerializer::loadNpcs(Map &map, pugi::xml_document &doc, wxArrayString &warnings) {
	pugi::xml_node node = doc.child("npcs");
	if (!node) {
		warnings.push_back("IOMapOTBM::loadSpawnsNpc: Invalid rootheader.");
		return false;
	}

	for (pugi::xml_node spawnNpcNode = node.first_child(); spawnNpcNode; spawnNpcNode = spawnNpcNode.next_sibling()) {
		if (as_lower_str(spawnNpcNode.name()) != "npc") {
			continue;
		}

		Position spawnPosition;
		spawnPosition.x = spawnNpcNode.attribute("centerx").as_int();
		spawnPosition.y = spawnNpcNode.attribute("centery").as_int();
		spawnPosition.z = spawnNpcNode.attribute("centerz").as_int();

		if (spawnPosition.x == 0 || spawnPosition.y == 0) {
			warnings.push_back("Bad position data on one npc spawn, discarding...");
			continue;
		}

		int32_t radius = spawnNpcNode.attribute("radius").as_int();
		if (radius < 1) {
			warnings.push_back("Couldn't read radius of npc spawn.. discarding spawn...");
			continue;
		}

		Tile* spawnTile = map.getTile(spawnPosition);
		if (spawnTile && spawnTile->spawnNpc) {
			warnings.push_back(fmt::format("Duplicate npc spawn on position {}:{}:{}", spawnTile->getX(), spawnTile->getY(), spawnTile->getZ()));
			continue;
		}

		SpawnNpc* spawnNpc = newd SpawnNpc(radius);
		if (!spawnTile) {
			spawnTile = map.allocator(map.createTileL(spawnPosition));
			map.setTile(spawnPosition, spawnTile);
		}

		spawnTile->spawnNpc = spawnNpc;
		map.addSpawnNpc(spawnTile);

		for (pugi::xml_node npcNode = spawnNpcNode.first_child(); npcNode; npcNode = npcNode.next_sibling()) {
			const std::string &npcNodeName = as_lower_str(npcNode.name());
			if (npcNodeName != "npc") {
				continue;
			}

			const std::string &name = npcNode.attribute("name").as_string();
			if (name.empty()) {
				wxString err;
				err << "Bad npc position data, discarding npc at spawn " << spawnPosition.x << ":" << spawnPosition.y << ":" << spawnPosition.z << " due to missing name.";
				warnings.Add(err);
				break;
			}

			int32_t spawntime = npcNode.attribute("spawntime").as_int();
			if (spawntime == 0) {
				spawntime = g_settings.getInteger(Config::DEFAULT_SPAWN_NPC_TIME);
			}

			Direction direction = NORTH;
			int dir = npcNode.attribute("direction").as_int(-1);
			if (dir >= DIRECTION_FIRST && dir <= DIRECTION_LAST) {
				direction = (Direction)dir;
			}

			Position npcPosition(spawnPosition);

			pugi::xml_attribute xAttribute = npcNode.attribute("x");
			pugi::xml_attribute yAttribute = npcNode.attribute("y");
			if (!xAttribute || !yAttribute) {
				wxString err;
				err << "Bad npc position data, discarding npc \"" << name << "\" at spawn " << npcPosition.x << ":" << npcPosition.y << ":" << npcPosition.z << " due to invalid position.";
				warnings.Add(err);
				break;
			}

			npcPosition.x += xAttribute.as_int();
			npcPosition.y += yAttribute.as_int();

			radius = std::max<int32_t>(radius, std::abs(npcPosition.x - spawnPosition.x));
			radius = std::max<int32_t>(radius, std::abs(npcPosition.y - spawnPosition.y));
			radius = std::min<int32_t>(radius, g_settings.getInteger(Config::MAX_SPAWN_NPC_RADIUS));

			Tile* npcTile;
			if (npcPosition == spawnPosition) {
				npcTile = spawnTile;
			} else {
				npcTile = map.getTile(npcPosition);
			}

			if (!npcTile) {
				wxString err;
				err << "Discarding npc \"" << name << "\" at " << npcPosition.x << ":" << npcPosition.y << ":" << npcPosition.z << " due to invalid position.";
				warnings.Add(err);
				break;
			}

			if (npcTile->npc) {
				wxString err;
				err << "Duplicate npc \"" << name << "\" at " << npcPosition.x << ":" << npcPosition.y << ":" << npcPosition.z << " was discarded.";
				warnings.Add(err);
				break;
			}

			NpcType* type = g_npcs[name];
			if (!type) {
				type = g_npcs.addMissingNpcType(name);
			}

			Npc* npc = newd Npc(type);
			npc->setDirection(direction);
			npc->setSpawnNpcTime(spawntime);
			npcTile->npc = npc;

			if (npcTile->getLocation()->getSpawnNpcCount() == 0) {
				// No npc spawn, create a newd one
				ASSERT(npcTile->spawnNpc == nullptr);
				SpawnNpc* spawnNpc = newd SpawnNpc(1);
				npcTile->spawnNpc = spawnNpc;
				map.addSpawnNpc(npcTile);
			}
		}
	}
	return true;
}

bool SpawnXmlSerializer::saveMonsters(Map &map, const FileName &dir) {
	wxString filepath = dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME);
	filepath += wxString(map.spawnmonsterfile.c_str(), wxConvUTF8);

	// Create the XML file
	pugi::xml_document doc;
	if (saveSpawns(map, doc)) {
		return doc.save_file(filepath.wc_str(), "\t", pugi::format_default, pugi::encoding_utf8);
	}
	return false;
}

bool SpawnXmlSerializer::saveMonsters(Map &map, pugi::xml_document &doc) {
	pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
	if (!decl) {
		return false;
	}

	decl.append_attribute("version") = "1.0";

	MonsterList monsterList;

	pugi::xml_node spawnNodes = doc.append_child("monsters");
	for (const auto &spawnPosition : map.spawnsMonster) {
		Tile* tile = map.getTile(spawnPosition);
		if (tile == nullptr) {
			continue;
		}

		SpawnMonster* spawnMonster = tile->spawnMonster;
		ASSERT(spawnMonster);

		pugi::xml_node spawnNode = spawnNodes.append_child("monster");
		spawnNode.append_attribute("centerx") = spawnPosition.x;
		spawnNode.append_attribute("centery") = spawnPosition.y;
		spawnNode.append_attribute("centerz") = spawnPosition.z;

		int32_t radius = spawnMonster->getSize();
		spawnNode.append_attribute("radius") = radius;

		for (auto y = -radius; y <= radius; ++y) {
			for (auto x = -radius; x <= radius; ++x) {
				const auto monsterTile = map.getTile(spawnPosition + Position(x, y, 0));
				if (monsterTile) {
					for (const auto monster : monsterTile->monsters) {
						if (monster && !monster->isSaved()) {
							pugi::xml_node monsterNode = spawnNode.append_child("monster");
							monsterNode.append_attribute("name") = monster->getName().c_str();
							monsterNode.append_attribute("x") = x;
							monsterNode.append_attribute("y") = y;
							monsterNode.append_attribute("z") = spawnPosition.z;
							monsterNode.append_attribute("spawntime") = monster->getSpawnMonsterTime();
							if (monster->getDirection() != NORTH) {
								monsterNode.append_attribute("direction") = monster->getDirection();
							}

							if (monsterTile->monsters.size() > 1) {
								const auto weight = monster->getWeight();
								monsterNode.append_attribute("weight") = weight > 0 ? weight : g_settings.getInteger(Config::MONSTER_DEFAULT_WEIGHT);
							}

							// Mark as saved
							monster->save();
							monsterList.push_back(monster);
						}
					}
				}
			}
		}
	}

	for (Monster* monster : monsterList) {
		monster->reset();
	}
	return true;
}

bool SpawnXmlSerializer::saveNpcs(Map &map, const FileName &dir) {
	wxString filepath = dir.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME);
	filepath += wxString(map.spawnnpcfile.c_str(), wxConvUTF8);

	// Create the XML file
	pugi::xml_document doc;
	if (saveSpawnsNpc(map, doc)) {
		return doc.save_file(filepath.wc_str(), "\t", pugi::format_default, pugi::encoding_utf8);
	}
	return false;
}

bool SpawnXmlSerializer::saveNpcs(Map &map, pugi::xml_document &doc) {
	pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
	if (!decl) {
		return false;
	}

	decl.append_attribute("version") = "1.0";

	NpcList npcList;

	pugi::xml_node spawnNodes = doc.append_child("npcs");
	for (const auto &spawnPosition : map.spawnsNpc) {
		Tile* tile = map.getTile(spawnPosition);
		if (tile == nullptr) {
			continue;
		}

		SpawnNpc* spawnNpc = tile->spawnNpc;
		ASSERT(spawnNpc);

		pugi::xml_node spawnNpcNode = spawnNodes.append_child("npc");
		spawnNpcNode.append_attribute("centerx") = spawnPosition.x;
		spawnNpcNode.append_attribute("centery") = spawnPosition.y;
		spawnNpcNode.append_attribute("centerz") = spawnPosition.z;

		int32_t radius = spawnNpc->getSize();
		spawnNpcNode.append_attribute("radius") = radius;

		for (int32_t y = -radius; y <= radius; ++y) {
			for (int32_t x = -radius; x <= radius; ++x) {
				Tile* npcTile = map.getTile(spawnPosition + Position(x, y, 0));
				if (npcTile) {
					Npc* npc = npcTile->npc;
					if (npc && !npc->isSaved()) {
						pugi::xml_node npcNode = spawnNpcNode.append_child("npc");
						npcNode.append_attribute("name") = npc->getName().c_str();
						npcNode.append_attribute("x") = x;
						npcNode.append_attribute("y") = y;
						npcNode.append_attribute("z") = spawnPosition.z;
						npcNode.append_attribute("spawntime") = npc->getSpawnNpcTime();
						if (npc->getDirection() != NORTH) {
							npcNode.append_attribute("direction") = npc->getDirection();
						}

						// Mark as saved
						npc->save();
						npcList.push_back(npc);
					}
				}
			}
		}
	}

	for (Npc* npc : npcList) {
		npc->reset();
	}
	return true;
}
