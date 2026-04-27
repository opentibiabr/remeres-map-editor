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

#ifndef RME_LUA_PARSER_H_
#define RME_LUA_PARSER_H_

#include <string>
#include <string_view>
#include <fstream>
#include "util/outfit.h"

namespace LuaParser {

	inline std::string parseCreateCall(std::string_view content, std::string_view funcName) {
		size_t pos = content.find(funcName);
		if (pos == std::string::npos) {
			return "";
		}
		pos += funcName.size();
		while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t')) {
			++pos;
		}
		if (pos >= content.size() || content[pos] != '(') {
			return "";
		}
		++pos;
		while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t' || content[pos] == '\n' || content[pos] == '\r')) {
			++pos;
		}
		if (pos >= content.size() || content[pos] != '"') {
			return "";
		}
		++pos;
		size_t nameStart = pos;
		while (pos < content.size() && content[pos] != '"') {
			++pos;
		}
		if (pos >= content.size()) {
			return "";
		}
		return std::string(content.substr(nameStart, pos - nameStart));
	}

	inline std::string parseLocalString(std::string_view content, std::string_view varName) {
		std::string pattern = "local ";
		pattern += varName;
		size_t pos = content.find(pattern);
		if (pos == std::string::npos) {
			return "";
		}
		pos += pattern.size();
		while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t')) {
			++pos;
		}
		if (pos >= content.size() || content[pos] != '=') {
			return "";
		}
		++pos;
		while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t')) {
			++pos;
		}
		if (pos >= content.size() || content[pos] != '"') {
			return "";
		}
		++pos;
		size_t nameStart = pos;
		while (pos < content.size() && content[pos] != '"') {
			++pos;
		}
		if (pos >= content.size()) {
			return "";
		}
		return std::string(content.substr(nameStart, pos - nameStart));
	}

	inline int parseField(std::string_view block, std::string_view key) {
		size_t searchFrom = 0;
		while (searchFrom < block.size()) {
			size_t pos = block.find(key, searchFrom);
			if (pos == std::string::npos) {
				return -1;
			}
			size_t afterKey = pos + key.size();
			if (afterKey < block.size() && (std::isalnum(block[afterKey]) || block[afterKey] == '_')) {
				searchFrom = afterKey;
				continue;
			}
			pos = afterKey;
			while (pos < block.size() && (block[pos] == ' ' || block[pos] == '\t')) {
				++pos;
			}
			if (pos >= block.size() || block[pos] != '=') {
				searchFrom = pos;
				continue;
			}
			++pos;
			while (pos < block.size() && (block[pos] == ' ' || block[pos] == '\t')) {
				++pos;
			}
			int val = 0;
			bool found = false;
			while (pos < block.size() && block[pos] >= '0' && block[pos] <= '9') {
				val = val * 10 + (block[pos] - '0');
				found = true;
				++pos;
			}
			if (found) {
				return val;
			}
			searchFrom = pos;
		}
		return -1;
	}

	inline std::string readFileContent(const std::string &filePath) {
		std::ifstream ifs(filePath, std::ios::binary | std::ios::ate);
		if (!ifs.is_open()) {
			return "";
		}
		auto fileSize = ifs.tellg();
		if (fileSize < 0) {
			return "";
		}
		ifs.seekg(0, std::ios::beg);
		std::string content(static_cast<size_t>(fileSize), '\0');
		if (!ifs.read(&content[0], fileSize)) {
			return "";
		}
		return content;
	}

	inline bool parseOutfit(std::string_view content, Outfit &outfit) {
		size_t outfitPos = content.find(".outfit");
		if (outfitPos == std::string::npos) {
			outfitPos = content.find(":outfit(");
		}
		if (outfitPos == std::string::npos) {
			return false;
		}
		size_t braceStart = content.find('{', outfitPos);
		if (braceStart == std::string::npos) {
			return false;
		}
		int depth = 1;
		size_t braceEnd = braceStart + 1;
		while (braceEnd < content.size() && depth > 0) {
			if (content[braceEnd] == '{') {
				++depth;
			} else if (content[braceEnd] == '}') {
				--depth;
			}
			++braceEnd;
		}
		if (depth != 0) {
			return false;
		}
		--braceEnd;
		std::string block(content.substr(braceStart, braceEnd - braceStart + 1));

		int val;
		bool hasBaseLook = false;
		if ((val = parseField(block, "lookType")) >= 0) {
			outfit.lookType = val;
			hasBaseLook = true;
		}
		if ((val = parseField(block, "lookTypeEx")) >= 0) {
			outfit.lookItem = val;
			hasBaseLook = true;
		}
		if ((val = parseField(block, "lookHead")) >= 0) {
			outfit.lookHead = val;
		}
		if ((val = parseField(block, "lookBody")) >= 0) {
			outfit.lookBody = val;
		}
		if ((val = parseField(block, "lookLegs")) >= 0) {
			outfit.lookLegs = val;
		}
		if ((val = parseField(block, "lookFeet")) >= 0) {
			outfit.lookFeet = val;
		}
		if ((val = parseField(block, "lookAddons")) >= 0) {
			outfit.lookAddon = val;
		}
		if ((val = parseField(block, "lookMount")) >= 0) {
			outfit.lookMount = val;
		}
		return hasBaseLook;
	}

} // namespace LuaParser

#endif
