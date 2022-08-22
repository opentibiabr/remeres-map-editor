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

#include "otml.h"
#include <ranges>

OTMLException::OTMLException(const OTMLNodePtr& node, const std::string& error) {
	std::stringstream ss;
	ss << "OTML error";
	if(!node->source().empty())
		ss << " in '" << node->source() << "'";
	ss << ": " << error;
	m_what = ss.str();
}

OTMLException::OTMLException(const OTMLDocumentPtr& doc, const std::string& error, int line) {
	std::stringstream ss;
	ss << "OTML error";
	if(doc && !doc->source().empty()) {
		ss << " in '" << doc->source() << "'";
		if(line >= 0)
			ss << " at line " << line;
	}
	ss << ": " << error;
	m_what = ss.str();
}

OTMLNodePtr OTMLNode::create(std::string tag, bool unique) {
	OTMLNodePtr node(new OTMLNode);
	node->setTag(tag);
	node->setUnique(unique);
	return node;
}

OTMLNodePtr OTMLNode::create(std::string tag, std::string value) {
	OTMLNodePtr node(new OTMLNode);
	node->setTag(tag);
	node->setValue(value);
	node->setUnique(true);
	return node;
}

bool OTMLNode::hasChildren() const {
	int count = 0;
	for(OTMLNodeList::const_iterator it = m_children.begin(), end = m_children.end(); it != end; ++it) {
		const OTMLNodePtr& child = *it;
		if(!child->isNull())
			count++;
	}
	return count > 0;
}

OTMLNodePtr OTMLNode::get(const std::string& childTag) const {
	for(OTMLNodeList::const_iterator it = m_children.begin(), end = m_children.end(); it != end; ++it) {
		const OTMLNodePtr& child = *it;
		if(child->tag() == childTag && !child->isNull())
			return child;
	}
	return OTMLNodePtr();
}

OTMLNodePtr OTMLNode::getIndex(int childIndex) const {
	if(childIndex < size() && childIndex >= 0)
		return m_children[childIndex];
	return OTMLNodePtr();
}

OTMLNodePtr OTMLNode::at(const std::string& childTag) {
	OTMLNodePtr res;
	for(OTMLNodeList::iterator it = m_children.begin(), end = m_children.end(); it != end; ++it) {
		const OTMLNodePtr& child = *it;
		if(child->tag() == childTag && !child->isNull()) {
			res = child;
			break;
		}
	}
	if(!res) {
		std::stringstream ss;
		ss << "child node with tag '" << childTag << "' not found";
		throw OTMLException(shared_from_this(), ss.str());
	}
	return res;
}

OTMLNodePtr OTMLNode::atIndex(int childIndex) {
	if(childIndex >= size() || childIndex < 0) {
		std::stringstream ss;
		ss << "child node with index '" << childIndex << "' not found";
		throw OTMLException(shared_from_this(), ss.str());
	}
	return m_children[childIndex];
}

void OTMLNode::addChild(const OTMLNodePtr& newChild) {
	if(newChild->hasTag()) {
		for(OTMLNodeList::iterator it = m_children.begin(), end = m_children.end(); it != end; ++it) {
			const OTMLNodePtr& node = *it;
			if(node->tag() == newChild->tag() && (node->isUnique() || newChild->isUnique())) {
				newChild->setUnique(true);

				if(node->hasChildren() && newChild->hasChildren()) {
					OTMLNodePtr tmpNode = node->clone();
					tmpNode->merge(newChild);
					newChild->copy(tmpNode);
				}

				replaceChild(node, newChild);
				OTMLNodeList::iterator it = m_children.begin();
				while(it != m_children.end()) {
					OTMLNodePtr node = (*it);
					if(node != newChild && node->tag() == newChild->tag()) {
						node->setParent(OTMLNodePtr());
						it = m_children.erase(it);
					}
					else
						++it;
				}
				return;
			}
		}
	}
	m_children.push_back(newChild);
	newChild->setParent(shared_from_this());
}

bool OTMLNode::removeChild(const OTMLNodePtr& oldChild) {
	OTMLNodeList::iterator it = std::find(m_children.begin(), m_children.end(), oldChild);
	if(it != m_children.end()) {
		m_children.erase(it);
		oldChild->setParent(OTMLNodePtr());
		return true;
	}
	return false;
}

bool OTMLNode::replaceChild(const OTMLNodePtr& oldChild, const OTMLNodePtr& newChild) {
	OTMLNodeList::iterator it = std::find(m_children.begin(), m_children.end(), oldChild);
	if(it != m_children.end()) {
		oldChild->setParent(OTMLNodePtr());
		newChild->setParent(shared_from_this());
		it = m_children.erase(it);
		m_children.insert(it, newChild);
		return true;
	}
	return false;
}

void OTMLNode::copy(const OTMLNodePtr& node)
{
	setTag(node->tag());
	setValue(node->rawValue());
	setUnique(node->isUnique());
	setNull(node->isNull());
	setSource(node->source());
	clear();
	for(OTMLNodeList::iterator it = m_children.begin(), end = m_children.end(); it != end; ++it) {
		const OTMLNodePtr& child = *it;
		addChild(child->clone());
	}
}

void OTMLNode::merge(const OTMLNodePtr& node) {
	for(OTMLNodeList::iterator it = m_children.begin(), end = m_children.end(); it != end; ++it) {
		const OTMLNodePtr& child = *it;
		addChild(child->clone());
	}
	setTag(node->tag());
	setSource(node->source());
}

void OTMLNode::clear() {
	for(OTMLNodeList::iterator it = m_children.begin(), end = m_children.end(); it != end; ++it) {
		const OTMLNodePtr& child = *it;
		child->setParent(OTMLNodePtr());
	}
	m_children.clear();
}

OTMLNodeList OTMLNode::children() const {
	OTMLNodeList children;
	for(OTMLNodeList::const_iterator it = m_children.begin(), end = m_children.end(); it != end; ++it) {
		const OTMLNodePtr& child = *it;
		if(!child->isNull())
			children.push_back(child);
	}
	return children;
}

OTMLNodePtr OTMLNode::clone() const {
	OTMLNodePtr myClone(new OTMLNode);
	myClone->setTag(m_tag);
	myClone->setValue(m_value);
	myClone->setUnique(m_unique);
	myClone->setNull(m_null);
	myClone->setSource(m_source);
	for(OTMLNodeList::const_iterator it = m_children.begin(), end = m_children.end(); it != end; ++it) {
		const OTMLNodePtr& child = *it;
		myClone->addChild(child->clone());
	}
	return myClone;
}

std::string OTMLNode::emit() {
	return OTMLEmitter::emitNode(shared_from_this(), 0);
}

template<typename T>
T OTMLNode::value() {
	T ret;
	if(!otml_util::cast(m_value, ret))
		throw OTMLException(shared_from_this(), "failed to cast node value");
	return ret;
}

template<typename T>
T OTMLNode::valueAtIndex(int childIndex) {
	OTMLNodePtr node = atIndex(childIndex);
	return node->value<T>();
}

bool OTMLNode::valueAt(const std::string& childTag) {
	OTMLNodePtr node = at(childTag);
	return node->value<bool>();
}

std::string OTMLNode::valueAt(const std::string& childTag, const std::string& def) {
	if(OTMLNodePtr node = get(childTag))
		if(!node->isNull())
			return node->value<std::string>();
	return def;
}

template<typename T>
T OTMLNode::valueAtIndex(int childIndex, const T& def) {
	if(OTMLNodePtr node = getIndex(childIndex))
		return node->value<T>();
	return def;
}

template<typename T>
void OTMLNode::write(const T& v) {
	m_value = otml_util::safeCast<std::string>(v);
}

template<typename T>
void OTMLNode::writeAt(const std::string& childTag, const T& v) {
	OTMLNodePtr child = OTMLNode::create(childTag);
	child->setUnique(true);
	child->write<T>(v);
	addChild(child);
}

template<typename T>
void OTMLNode::writeIn(const T& v) {
	OTMLNodePtr child = OTMLNode::create();
	child->write<T>(v);
	addChild(child);
}

OTMLDocumentPtr OTMLDocument::create() {
	OTMLDocumentPtr doc(new OTMLDocument);
	doc->setTag("doc");
	return doc;
}

OTMLDocumentPtr OTMLDocument::parse(const std::string& fileName) {
	std::ifstream fin(fileName.c_str());
	if(!fin.good()) {
		std::stringstream ss;
		ss << "failed to open file " << fileName;
		throw OTMLException(ss.str());
	}
	return parse(fin, fileName);
}

OTMLDocumentPtr OTMLDocument::parse(std::istream& in, const std::string& source) {
	OTMLDocumentPtr doc(new OTMLDocument);
	doc->setSource(source);
	OTMLParser parser(doc, in);
	parser.parse();
	return doc;
}

std::string OTMLDocument::emit() {
	return OTMLEmitter::emitNode(shared_from_this()) + "\n";
}

bool OTMLDocument::save(const std::string& fileName) {
	m_source = fileName;
	std::ofstream fout(fileName.c_str());
	if(fout.good()) {
		fout << emit();
		fout.close();
		return true;
	}
	return false;
}

std::string OTMLEmitter::emitNode(const OTMLNodePtr& node, int currentDepth) {
	std::stringstream ss;
	if(currentDepth >= 0) {
		for(int i = 0; i<currentDepth; ++i)
			ss << "  ";
		if(node->hasTag()) {
			ss << node->tag();
			if(node->hasValue() || node->isUnique() || node->isNull())
				ss << ":";
		}
		else
			ss << "-";
		if(node->isNull())
			ss << " ~";
		else if(node->hasValue()) {
			ss << " ";
			std::string value = node->rawValue();
			if(value.find("\n") != std::string::npos) {
				if(value[value.length() - 1] == '\n' && value[value.length() - 2] == '\n')
					ss << "|+";
				else if(value[value.length() - 1] == '\n')
					ss << "|";
				else
					ss << "|-";
				for(std::size_t pos = 0; pos < value.length(); ++pos) {
					ss << "\n";
					for(int i = 0; i<currentDepth + 1; ++i)
						ss << "  ";
					while(pos < value.length()) {
						if(value[pos] == '\n')
							break;
						ss << value[pos++];
					}
				}
			}
			else
				ss << value;
		}
	}
	for(int i = 0; i<node->size(); ++i) {
		if(currentDepth >= 0 || i != 0)
			ss << "\n";
		ss << emitNode(node->atIndex(i), currentDepth + 1);
	}
	return ss.str();
}

void OTMLParser::parse() {
	if(!in.good())
		throw OTMLException(doc, "cannot read from input stream");
	while(!in.eof())
		parseLine(getNextLine());
}

std::string OTMLParser::getNextLine() {
	currentLine++;
	std::string line;
	std::getline(in, line);
	return line;
}

int OTMLParser::getLineDepth(const std::string& line, bool multilining) {
	std::size_t spaces = 0;
	while(line[spaces] == ' ')
		spaces++;

	int depth = spaces / 2;
	if(!multilining || depth <= currentDepth) {
		if(line[spaces] == '\t')
			throw OTMLException(doc, "indentation with tabs are not allowed", currentLine);
		if(spaces % 2 != 0)
			throw OTMLException(doc, "must indent every 2 spaces", currentLine);
	}
	return depth;
}

void OTMLParser::trim(std::string& string)
{
	auto view = std::ranges::views::all(string)
		| std::ranges::views::drop_while(isspace)
		| std::ranges::views::reverse
		| std::ranges::views::drop_while(isspace)
		| std::ranges::views::reverse;
		std::string result{view.begin(), view.end()};
		string = std::move(result);
}

std::vector<std::string> OTMLParser::split(const char *str, char ch /*= ' '*/)
{
	std::vector<std::string> result;
	do
	{
		const char *begin = str;

		while(*str != ch && *str)
		{
			str++;
		}
		result.push_back(std::string(begin, str));
	} while (0 != *str++);

	return result;
}

void OTMLParser::parseLine(std::string line) {
	int depth = getLineDepth(line);
	if(depth == -1)
		return;
	trim(line);
	if(line.empty())
		return;
	if(line.substr(0, 2) == "//")
		return;
	if(depth == currentDepth + 1) {
		currentParent = previousNode;
	}
	else if(depth < currentDepth) {
		for(int i = 0; i<currentDepth - depth; ++i)
			currentParent = currentParent->parent();
	}
	else if(depth != currentDepth)
		throw OTMLException(doc, "invalid indentation depth, are you indenting correctly?", currentLine);
	currentDepth = depth;
	parseNode(line);
}

void OTMLParser::parseNode(const std::string& data) {
	std::string tag;
	std::string value;
	std::size_t dotsPos = data.find_first_of(':');
	int nodeLine = currentLine;
	if(!data.empty() && data[0] == '-') {
		value = data.substr(1);
		trim(value);
	}
	else if(dotsPos != std::string::npos) {
		tag = data.substr(0, dotsPos);
		if(data.size() > dotsPos + 1)
			value = data.substr(dotsPos + 1);
	}
	else {
		tag = data;
	}
	trim(tag);
	trim(value);
	if(value == "|" || value == "|-" || value == "|+") {
		std::string multiLineData;
		do {
			size_t lastPos = in.tellg();
			std::string line = getNextLine();
			int depth = getLineDepth(line, true);
			if(depth > currentDepth) {
				multiLineData += line.substr((currentDepth + 1) * 2);
			}
			else {
				trim(line);
				if(!line.empty()) {
					in.seekg(lastPos, std::ios::beg);
					currentLine--;
					break;
				}
			}
			multiLineData += "\n";
		} while(!in.eof());
		if(value == "|" || value == "|-") {
			int lastPos = multiLineData.length();
			while(multiLineData[--lastPos] == '\n')
				multiLineData.erase(lastPos, 1);

			if(value == "|")
				multiLineData.append("\n");
		}
		value = multiLineData;
	}
	OTMLNodePtr node = OTMLNode::create(tag);
	node->setUnique(dotsPos != std::string::npos);
	node->setTag(tag);
	node->setSource(doc->source() + ":" + otml_util::safeCast<std::string>(nodeLine));
	if(value == "~")
		node->setNull(true);
	else {
		if(std::ranges::starts_with(value, "[") && std::ranges::ends_with(value, "]")) {
			std::string tmp = value.substr(1, value.length() - 2);
			for (const auto word : split(tmp.c_str())) {
				std::string v = word;
				trim(v);
				node->writeIn(v);
			}
		}
		else
			node->setValue(value);
	}

	currentParent->addChild(node);
	previousNode = node;
}
