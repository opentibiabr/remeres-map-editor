//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef LUA_API_JSON_H
#define LUA_API_JSON_H

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

namespace LuaAPI {

	void registerJson(sol::state &lua);

}

#endif
