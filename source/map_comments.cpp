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

#include "map_comments.h"

#include "settings.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace {
	// Material 500 tones, shared with the collaboration user colors.
	constexpr uint32_t kPalette[] = {
		0xF44336, 0xE91E63, 0x9C27B0, 0x3F51B5, 0x2196F3, 0x009688,
		0x4CAF50, 0xFF9800, 0xFF5722, 0x795548, 0x607D8B, 0x00BCD4
	};

	int64_t now() {
		return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}

	std::string authorOverride;
}

void MapComments::setAuthorOverride(const std::string &name) {
	authorOverride = name;
}

const char* MapComments::kindName(uint8_t kind) {
	static const char* const names[] = { "Note", "Bug", "Idea", "To do" };
	return names[kind % kKindCount];
}

uint32_t MapComments::kindColor(uint8_t kind) {
	switch (kind) {
		case 1:
			return 0xF44336; // bug
		case 2:
			return 0xFFC107; // idea
		case 3:
			return 0x2196F3; // to do
		default:
			return 0;
	}
}

std::string MapComments::localAuthor() {
	if (!authorOverride.empty()) {
		return authorOverride;
	}
	std::string name = g_settings.getString(Config::COLLAB_USER_NAME);
	if (name.empty()) {
		name = nstr(wxGetUserId());
	}
	if (name.empty()) {
		name = "User";
	}
	return name;
}

uint32_t MapComments::colorForAuthor(const std::string &author) {
	return kPalette[std::hash<std::string> {}(author) % std::size(kPalette)];
}

uint32_t MapComments::paletteColor(size_t index) {
	return kPalette[index % std::size(kPalette)];
}

MapComment* MapComments::find(uint32_t id) {
	auto it = std::find_if(comments.begin(), comments.end(), [id](const MapComment &c) { return c.id == id; });
	return it == comments.end() ? nullptr : &*it;
}

const MapComment* MapComments::get(uint32_t id) const {
	return const_cast<MapComments*>(this)->find(id);
}

const MapComment* MapComments::at(const Position &pos) const {
	for (const MapComment &c : comments) {
		if (c.pos == pos && c.parent == 0) {
			return &c;
		}
	}
	return nullptr;
}

const MapComment* MapComments::rootOf(const MapComment &comment) const {
	const MapComment* root = &comment;
	for (int depth = 0; root->parent != 0 && depth < 8; ++depth) {
		const MapComment* parent = get(root->parent);
		if (!parent) {
			break;
		}
		root = parent;
	}
	return root;
}

const MapComment &MapComments::add(const Position &pos, const std::string &text, uint32_t parent, const std::string &assignee, uint8_t kind) {
	MapComment c;
	do {
		c.id = (idPrefix << 20) | (nextId++ & 0xFFFFF);
	} while (c.id == 0 || find(c.id));
	c.pos = pos;
	c.parent = parent;
	c.assignee = assignee;
	c.kind = kind % kKindCount;
	c.author = localAuthor();
	c.authorColor = colorForAuthor(c.author);
	c.text = text;
	c.created = now();
	comments.push_back(std::move(c));
	return comments.back();
}

bool MapComments::edit(uint32_t id, const std::string &text) {
	MapComment* c = find(id);
	if (!c) {
		return false;
	}
	c->text = text;
	c->edited = now();
	return true;
}

bool MapComments::update(uint32_t id, const std::string &text, const std::string &assignee, uint8_t kind) {
	MapComment* c = find(id);
	if (!c) {
		return false;
	}
	c->text = text;
	c->assignee = assignee;
	c->kind = kind % kKindCount;
	c->edited = now();
	return true;
}

void MapComments::upsert(const MapComment &comment) {
	if (MapComment* existing = find(comment.id)) {
		*existing = comment;
	} else {
		comments.push_back(comment);
	}
	nextId = std::max(nextId, (comment.id & 0xFFFFF) + 1);
}

bool MapComments::setResolved(uint32_t id, bool resolved) {
	MapComment* c = find(id);
	if (!c) {
		return false;
	}
	c->resolved = resolved;
	return true;
}

bool MapComments::remove(uint32_t id) {
	auto it = std::remove_if(comments.begin(), comments.end(), [id](const MapComment &c) { return c.id == id || c.parent == id; });
	if (it == comments.end()) {
		return false;
	}
	comments.erase(it, comments.end());
	return true;
}

void MapComments::clear() {
	comments.clear();
	nextId = 1;
}

bool MapComments::load(const std::string &path) {
	pugi::xml_document doc;
	clear();
	return doc.load_file(path.c_str()) && loadDocument(doc);
}

bool MapComments::loadXml(const std::string &xml) {
	pugi::xml_document doc;
	clear();
	return doc.load_buffer(xml.data(), xml.size()) && loadDocument(doc);
}

bool MapComments::loadDocument(const pugi::xml_document &doc) {
	pugi::xml_node root = doc.child("comments");
	if (!root) {
		return false;
	}

	for (pugi::xml_node node : root.children("comment")) {
		MapComment c;
		c.id = node.attribute("id").as_uint();
		if (c.id == 0 || find(c.id)) {
			c.id = nextId++; // Missing or duplicated id, assign a fresh one
		}
		c.pos = Position(node.attribute("x").as_int(), node.attribute("y").as_int(), node.attribute("z").as_int());
		if (!c.pos.isValid()) {
			continue;
		}
		c.author = node.attribute("author").as_string();
		const char* color = node.attribute("color").as_string();
		c.authorColor = *color == '#' ? static_cast<uint32_t>(std::strtoul(color + 1, nullptr, 16)) : colorForAuthor(c.author);
		c.created = node.attribute("created").as_llong();
		c.edited = node.attribute("edited").as_llong();
		c.resolved = node.attribute("resolved").as_bool();
		c.text = node.text().as_string();
		c.parent = node.attribute("parent").as_uint();
		c.assignee = node.attribute("assignee").as_string();
		c.kind = static_cast<uint8_t>(node.attribute("kind").as_uint() % kKindCount);
		nextId = std::max(nextId, (c.id & 0xFFFFF) + 1);
		comments.push_back(std::move(c));
	}
	return true;
}

bool MapComments::save(const std::string &path) const {
	if (comments.empty()) {
		std::remove(path.c_str());
		return true;
	}

	pugi::xml_document doc;
	fillDocument(doc);
	return doc.save_file(path.c_str(), "\t", pugi::format_default, pugi::encoding_utf8);
}

std::string MapComments::toXml() const {
	pugi::xml_document doc;
	fillDocument(doc);
	std::ostringstream stream;
	doc.save(stream, "", pugi::format_raw, pugi::encoding_utf8);
	return stream.str();
}

void MapComments::fillDocument(pugi::xml_document &doc) const {
	pugi::xml_node root = doc.append_child("comments");
	for (const MapComment &c : comments) {
		pugi::xml_node node = root.append_child("comment");
		node.append_attribute("id") = c.id;
		node.append_attribute("x") = c.pos.x;
		node.append_attribute("y") = c.pos.y;
		node.append_attribute("z") = c.pos.z;
		node.append_attribute("author") = c.author.c_str();
		node.append_attribute("color") = fmt::format("#{:06X}", c.authorColor).c_str();
		node.append_attribute("created") = static_cast<long long>(c.created);
		node.append_attribute("edited") = static_cast<long long>(c.edited);
		node.append_attribute("resolved") = c.resolved ? 1 : 0;
		if (c.parent != 0) {
			node.append_attribute("parent") = c.parent;
		}
		if (!c.assignee.empty()) {
			node.append_attribute("assignee") = c.assignee.c_str();
		}
		if (c.kind != 0) {
			node.append_attribute("kind") = static_cast<unsigned>(c.kind);
		}
		node.text().set(c.text.c_str());
	}
}
