//
// Created by lucas on 8/26/2022.
//

#ifndef REMERES_MAP_EDITOR_RME_MAP_HEADER_H
#define REMERES_MAP_EDITOR_RME_MAP_HEADER_H

class RMEMapHeader : public Kmap::IHeader
{
public:
	RMEMapHeader(Map& map) : map(map) {};

	[[nodiscard]] uint16_t Width() const override
	{
		return map.width;
	}

	[[nodiscard]] uint16_t Height() const override
	{
		return map.height;
	}

	[[nodiscard]] uint16_t Version() const override
	{
		return map.getVersion().otbm;
	}

	[[nodiscard]] const std::string& MonsterSpawnFilename() const override
	{
		return getFullFileName(map.spawnsMonster);
	}

	[[nodiscard]] const std::string& NpcSpawnFilename() const override
	{
		return getFullFileName(map.spawnsNpc);
	}

	[[nodiscard]] const std::string& HouseSpawnFilename() const override
	{
		return getFullFileName(map.housefile);
	}

	[[nodiscard]] const std::string& Description() const override
	{
		return map.description;
	}

private:
	Map& map;

	std::string getFullFileName(std::string fileName)
	{
		FileName tmpName;
		tmpName.Assign(wxstr(fileName));
		return nstr(tmpName.GetFullName());
	}
};

#endif
