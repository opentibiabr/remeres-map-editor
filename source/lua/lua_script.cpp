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
#include "lua_script.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <array>

// Constructor for single .lua file
LuaScript::LuaScript(const std::string &filepath) :
	filepath(filepath),
	isPackageScript(false) {

	// Extract filename from path
	if (size_t lastSlash = filepath.find_last_of("/\\"); lastSlash != std::string::npos) {
		filename = filepath.substr(lastSlash + 1);
		directory = filepath.substr(0, lastSlash);
	} else {
		filename = filepath;
		directory = ".";
	}

	// Default display name is filename without extension
	if (size_t lastDot = filename.find_last_of('.'); lastDot != std::string::npos) {
		displayName = filename.substr(0, lastDot);
	} else {
		displayName = filename;
	}

	// Replace underscores with spaces for display
	std::replace(displayName.begin(), displayName.end(), '_', ' ');

	// Parse metadata from script comments
	parseMetadata();
}

// Constructor for directory with manifest.lua
LuaScript::LuaScript(const std::string &dir, bool /*isDirectory*/) :
	directory(dir),
	isPackageScript(true) {

	// Extract folder name for default display name
	if (size_t lastSlash = dir.find_last_of("/\\"); lastSlash != std::string::npos) {
		displayName = dir.substr(lastSlash + 1);
	} else {
		displayName = dir;
	}

	// Replace underscores with spaces for display
	std::replace(displayName.begin(), displayName.end(), '_', ' ');

	// Parse metadata from manifest.lua
	parseMetadata();
}

void LuaScript::parseMetadata() {
	if (isPackageScript) {
		parseMetadataFromManifest();
	} else {
		parseMetadataFromComments();
	}
}

static std::string stripLuaComments(std::string line, bool &inBlockComment) {
	size_t bcEnd = line.find("]]");
	if (inBlockComment) {
		if (bcEnd != std::string::npos) {
			inBlockComment = false;
			line = line.substr(bcEnd + 2);
		} else {
			return {};
		}
	}
	if (size_t bcStart = line.find("--[["); bcStart != std::string::npos && bcEnd == std::string::npos) {
		inBlockComment = true;
		line = line.substr(0, bcStart);
	}
	if (auto commentPos = line.find("--"); commentPos != std::string::npos) {
		line = line.substr(0, commentPos);
	}
	return line;
}

static bool isAutorunTrue(const std::string &line) {
	auto pos = line.find("autorun");
	if (pos == std::string::npos) {
		return false;
	}
	bool validBefore = (pos == 0) || (!std::isalnum(static_cast<unsigned char>(line[pos - 1])) && line[pos - 1] != '_');
	size_t afterPos = pos + 7; // length of "autorun"
	if (bool validAfter = (afterPos >= line.size()) || (!std::isalnum(line[afterPos]) && line[afterPos] != '_'); validBefore && validAfter) {
		return false;
	}
	std::string rest = line.substr(afterPos);
	rest.erase(0, rest.find_first_not_of(" \t"));
	if (rest.empty() || rest[0] != '=') {
		return false;
	}
	rest = rest.substr(1);
	rest.erase(0, rest.find_first_not_of(" \t"));
	return rest.starts_with("true");
}

static std::string extractTagValue(const std::string &comment, std::string_view tag) {
	if (!comment.starts_with(tag)) {
		return {};
	}
	std::string value = comment.substr(tag.size());
	auto s = value.find_first_not_of(" \t");
	if (s == std::string::npos) {
		return {};
	}
	auto e = value.find_last_not_of(" \t");
	return value.substr(s, e - s + 1);
}

bool LuaScript::scanAutorunValue(const std::string &content) const {
	bool inBlockComment = false;
	std::istringstream stream(content);
	std::string line;
	while (std::getline(stream, line)) {
		line = stripLuaComments(std::move(line), inBlockComment);
		if (!line.empty() && isAutorunTrue(line)) {
			return true;
		}
	}
	return false;
}

static std::string getManifestValue(const std::string &content, const std::string &key) {
	const std::array<std::string, 4> patterns = {
		key + " = \"", key + " = '", key + "=\"", key + "='"
	};
	const std::array<char, 4> quotes = { '"', '\'', '"', '\'' };

	size_t pos = std::string::npos;
	char quote = '"';
	for (int i = 0; i < 4; ++i) {
		size_t found = content.find(patterns[i]);
		while (found != std::string::npos) {
			if (bool validBefore = (found == 0) || (!std::isalnum(static_cast<unsigned char>(content[found - 1])) && content[found - 1] != '_'); validBefore) {
				pos = found;
				quote = quotes[i];
				break;
			}
			found = content.find(patterns[i], found + 1);
		}
		if (pos != std::string::npos) {
			break;
		}
	}

	if (pos == std::string::npos) {
		return "";
	}

	size_t valueStart = content.find(quote, pos) + 1;
	size_t valueEnd = content.find(quote, valueStart);
	if (valueEnd == std::string::npos) {
		return "";
	}

	return content.substr(valueStart, valueEnd - valueStart);
}

void LuaScript::parseMetadataFromManifest() {
	// Parse manifest.lua which returns a table with metadata
	// Format:
	// return {
	//     name = "Script Name",
	//     description = "Description",
	//     author = "Author",
	//     version = "1.0.0",
	//     main = "main_script",
	//     shortcut = "Ctrl+Shift+H"
	// }

	std::string manifestPath = directory + "/manifest.lua";
	std::ifstream file(manifestPath);
	if (!file.is_open()) {
		// Try default main.lua
		filepath = directory + "/main.lua";
		filename = "main.lua";
		return;
	}

	std::stringstream buffer;
	buffer << file.rdbuf();
	std::string content = buffer.str();

	std::string val;

	val = getManifestValue(content, "name");
	if (!val.empty()) {
		displayName = val;
	}

	val = getManifestValue(content, "description");
	if (!val.empty()) {
		description = val;
	}

	val = getManifestValue(content, "author");
	if (!val.empty()) {
		author = val;
	}

	val = getManifestValue(content, "version");
	if (!val.empty()) {
		version = val;
	}

	val = getManifestValue(content, "shortcut");
	if (!val.empty()) {
		shortcut = val;
	}

	val = getManifestValue(content, "autorun");
	if (!val.empty()) {
		autorun = (val == "true");
	} else {
		autorun = scanAutorunValue(content);
	}

	val = getManifestValue(content, "main");
	if (!val.empty()) {
		// Add .lua extension if not present
		if (!val.ends_with(".lua")) {
			val += ".lua";
		}
		filepath = directory + "/" + val;
		filename = val;
	} else {
		// Default to main.lua
		filepath = directory + "/main.lua";
		filename = "main.lua";
	}
}

// Static helper — trim leading whitespace
static std::string trimLeading(const std::string &str) {
	if (auto s = str.find_first_not_of(" \t"); s != std::string::npos) {
		return str.substr(s);
	}
	return {};
}

// Static helper — trim leading + trailing whitespace
static std::string trimBoth(const std::string &str) {
	auto start = str.find_first_not_of(" \t");
	if (start == std::string::npos) {
		return {};
	}
	auto end = str.find_last_not_of(" \t");
	return str.substr(start, end - start + 1);
}

// Helper — process a metadata tag, returns true if a tag was found
bool LuaScript::processMetadataTag(const std::string &comment, bool &foundName, std::ostringstream &descBuilder) {
	if (comment.starts_with("@Title:")) {
		displayName = trimLeading(comment.substr(7));
		foundName = true;
		return true;
	}
	if (comment.starts_with("@Description:")) {
		std::string descPart = trimLeading(comment.substr(13));
		if (descBuilder.tellp() > 0) {
			descBuilder << " ";
		}
		descBuilder << descPart;
		return true;
	}
	if (comment.starts_with("@Author:")) {
		author = trimLeading(comment.substr(8));
		return true;
	}
	if (comment.starts_with("@Version:")) {
		version = trimLeading(comment.substr(9));
		return true;
	}
	if (comment.starts_with("@Shortcut:")) {
		shortcut = trimLeading(comment.substr(10));
		return true;
	}
	if (comment.starts_with("@AutoRun:") || comment.starts_with("@Autorun:")) {
		if (trimBoth(comment.substr(9)) == "true") {
			autorun = true;
		}
		return true;
	}
	return false;
}

void LuaScript::parseMetadataFromComments() {
	std::ifstream file(filepath);
	if (!file.is_open()) {
		return;
	}

	std::string line;
	bool foundName = false;
	std::ostringstream descBuilder;
	int lineNum = 0;
	const int maxHeaderLines = 20;

	while (std::getline(file, line) && lineNum < maxHeaderLines) {
		lineNum++;

		if (auto start = line.find_first_not_of(" \t"); start != std::string::npos) {
			line = line.substr(start);
		} else {
			continue;
		}

		if (!line.starts_with("--")) {
			break;
		}

		std::string comment = line.substr(2);
		if (auto start = comment.find_first_not_of(" \t"); start != std::string::npos) {
			comment = comment.substr(start);
		} else {
			continue;
		}

		if (comment.empty()) {
			continue;
		}

		if (processMetadataTag(comment, foundName, descBuilder)) {
			continue;
		}

		if (!foundName) {
			displayName = comment;
			foundName = true;
		} else {
			if (descBuilder.tellp() > 0) {
				descBuilder << " ";
			}
			descBuilder << comment;
		}
	}

	description = descBuilder.str();
}
