#include "main.h"
#include "lua_api_json.h"
#include <nlohmann/json.hpp>

namespace LuaAPI {

	// Forward declarations
	sol::object valueToLua(const nlohmann::json &val, sol::state_view &lua);
	nlohmann::json luaToValue(const sol::object &obj);

	// Convert nlohmann::json to Lua Object
	sol::object valueToLua(const nlohmann::json &val, sol::state_view &lua) {
		if (val.is_null()) {
			return sol::nil;
		} else if (val.is_boolean()) {
			return sol::make_object(lua, val.get<bool>());
		} else if (val.is_number_integer()) {
			return sol::make_object(lua, val.get<int64_t>());
		} else if (val.is_number_float()) {
			return sol::make_object(lua, val.get<double>());
		} else if (val.is_string()) {
			return sol::make_object(lua, val.get<std::string>());
		} else if (val.is_array()) {
			sol::table t = lua.create_table();
			for (size_t i = 0; i < val.size(); ++i) {
				t[i + 1] = valueToLua(val[i], lua); // Lua 1-based indexing
			}
			return t;
		} else if (val.is_object()) {
			sol::table t = lua.create_table();
			for (auto &[key, value] : val.items()) {
				t[key] = valueToLua(value, lua);
			}
			return t;
		}
		return sol::nil;
	}

	// Convert Lua Object to nlohmann::json
	nlohmann::json luaToValue(const sol::object &obj) {
		switch (obj.get_type()) {
			case sol::type::nil:
				return nullptr;
			case sol::type::boolean:
				return obj.as<bool>();
			case sol::type::number: {
				double d = obj.as<double>();
				if (d == static_cast<int64_t>(d)) {
					return static_cast<int64_t>(d);
				}
				return d;
			}
			case sol::type::string:
				return obj.as<std::string>();
			case sol::type::table: {
				sol::table t = obj.as<sol::table>();

				bool isArray = true;
				size_t maxKey = 0;
				size_t count = 0;

				for (auto &pair : t) {
					count++;
					if (pair.first.get_type() == sol::type::number) {
						double k = pair.first.as<double>();
						if (k >= 1 && k == static_cast<size_t>(k)) {
							size_t idx = static_cast<size_t>(k);
							if (idx > maxKey) {
								maxKey = idx;
							}
						} else {
							isArray = false;
							break;
						}
					} else {
						isArray = false;
						break;
					}
				}

				if (isArray && count > 0 && maxKey != count) {
					isArray = false;
				}
				if (count == 0) {
					isArray = false;
				}

				if (isArray) {
					nlohmann::json arr = nlohmann::json::array();
					for (size_t i = 1; i <= count; ++i) {
						arr.push_back(t[i].valid() ? luaToValue(t[i]) : nlohmann::json(nullptr));
					}
					return arr;
				} else {
					nlohmann::json objVal = nlohmann::json::object();
					for (auto &pair : t) {
						std::string key;
						if (pair.first.get_type() == sol::type::string) {
							key = pair.first.as<std::string>();
						} else if (pair.first.get_type() == sol::type::number) {
							key = std::to_string(pair.first.as<int64_t>());
						} else {
							continue;
						}
						objVal[key] = luaToValue(pair.second);
					}
					return objVal;
				}
			}
			default:
				return nullptr;
		}
	}

	void registerJson(sol::state &lua) {
		sol::table jsonTable = lua.create_table();

		jsonTable.set_function("encode", [](sol::object obj, sol::this_state s) -> std::string {
			nlohmann::json val = luaToValue(obj);
			return val.dump(); // minified output
		});

		jsonTable.set_function("encode_pretty", [](sol::object obj, sol::this_state s) -> std::string {
			nlohmann::json val = luaToValue(obj);
			return val.dump(4); // pretty-printed with 4-space indent
		});

		jsonTable.set_function("decode", [](std::string jsonStr, sol::this_state s) -> sol::object {
			try {
				nlohmann::json val = nlohmann::json::parse(jsonStr);
				sol::state_view lua(s);
				return valueToLua(val, lua);
			} catch (const nlohmann::json::parse_error &) {
				return sol::nil;
			}
		});

		lua["json"] = jsonTable;
	}

} // namespace LuaAPI
