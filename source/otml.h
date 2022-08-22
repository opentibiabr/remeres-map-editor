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

#ifndef RME_OTML_H
#define RME_OTML_H

#include <sstream>
#include <fstream>
#include <string>
#include <vector>
#include <exception>
#include <memory>
#include <algorithm>
#include <cmath>

class OTMLNode;
class OTMLDocument;
class OTMLParser;
class OTMLEmitter;

typedef std::shared_ptr<OTMLNode> OTMLNodePtr;
typedef std::enable_shared_from_this<OTMLNode> OTMLNodeEnableSharedFromThis;
typedef std::shared_ptr<OTMLDocument> OTMLDocumentPtr;
typedef std::weak_ptr<OTMLNode> OTMLNodeWeakPtr;

typedef std::vector<OTMLNodePtr> OTMLNodeList;

namespace otml_util {
	template<typename T, typename R>
	bool cast(const T& in, R& out) {
		std::stringstream ss;
		ss << in;
		ss >> out;
		return !!ss && ss.eof();
	}

	template<typename T>
	bool cast(const T& in, std::string& out) {
		std::stringstream ss;
		ss << in;
		out = ss.str();
		return true;
	}

	template<>
	inline bool cast(const std::string& in, std::string& out) {
		out = in;
		return true;
	}

	template<>
	inline bool cast(const std::string& in, bool& b) {
		if(in == "true")
			b = true;
		else if(in == "false")
			b = false;
		else
			return false;
		return true;
	}

	template<>
	inline bool cast(const std::string& in, char& c) {
		if(in.length() != 1)
			return false;
		c = in[0];
		return true;
	}

	template<>
	inline bool cast(const std::string& in, long& l) {
		if(in.find_first_not_of("-0123456789") != std::string::npos)
			return false;
		std::size_t t = in.find_last_of('-');
		if(t != std::string::npos && t != 0)
			return false;
		l = atol(in.c_str());
		return true;
	}

	template<>
	inline bool cast(const std::string& in, int& i) {
		long l;
		if(cast(in, l)) {
			i = l;
			return true;
		}
		return false;
	}

	template<>
	inline bool cast(const std::string& in, double& d) {
		if(in.find_first_not_of("-0123456789.") != std::string::npos)
			return false;
		std::size_t t = in.find_last_of('-');
		if(t != std::string::npos &&  t != 0)
			return false;
		t = in.find_first_of('.');
		if(t != std::string::npos && (t == 0 || t == in.length() - 1 || in.find_first_of('.', t + 1) != std::string::npos))
			return false;
		d = atof(in.c_str());
		return true;
	}

	template<>
	inline bool cast(const bool& in, std::string& out) {
		out = (in ? "true" : "false");
		return true;
	}

	class BadCast : public std::bad_cast {
	public:
		virtual ~BadCast() throw() { }
		virtual const char* what() { return "failed to cast value"; }
	};

	template<typename R, typename T>
	R safeCast(const T& t) {
		R r;
		if(!cast(t, r))
			throw BadCast();
		return r;
	}
};

class OTMLException : public std::exception {
public:
	OTMLException(const std::string& error) : m_what(error) { }
	OTMLException(const OTMLNodePtr& node, const std::string& error);
	OTMLException(const OTMLDocumentPtr& doc, const std::string& error, int line = -1);
	virtual ~OTMLException() throw() { };

	virtual const char* what() const throw() { return m_what.c_str(); }

protected:
	std::string m_what;
};

class OTMLNode : public OTMLNodeEnableSharedFromThis {
public:
	virtual ~OTMLNode() { }

	static OTMLNodePtr create(std::string tag = "", bool unique = false);
	static OTMLNodePtr create(std::string tag, std::string value);

	std::string tag() const { return m_tag; }
	int size() const { return m_children.size(); }
	OTMLNodePtr parent() const { return m_parent.lock(); }
	std::string source() const { return m_source; }
	std::string rawValue() const { return m_value; }

	bool isUnique() const { return m_unique; }
	bool isNull() const { return m_null; }

	bool hasTag() const { return !m_tag.empty(); }
	bool hasValue() const { return !m_value.empty(); }
	bool hasChildren() const;
	bool hasChildAt(const std::string& childTag) { return !!get(childTag); }
	bool hasChildAtIndex(int childIndex) { return !!getIndex(childIndex); }

	void setTag(std::string tag) { m_tag = tag; }
	void setValue(const std::string& value) { m_value = value; }
	void setNull(bool null) { m_null = null; }
	void setUnique(bool unique) { m_unique = unique; }
	void setParent(const OTMLNodePtr& parent) { m_parent = parent; }
	void setSource(const std::string& source) { m_source = source; }

	OTMLNodePtr get(const std::string& childTag) const;
	OTMLNodePtr getIndex(int childIndex) const;

	OTMLNodePtr at(const std::string& childTag);
	OTMLNodePtr atIndex(int childIndex);

	void addChild(const OTMLNodePtr& newChild);
	bool removeChild(const OTMLNodePtr& oldChild);
	bool replaceChild(const OTMLNodePtr& oldChild, const OTMLNodePtr& newChild);
	void merge(const OTMLNodePtr& node);
	void copy(const OTMLNodePtr& node);
	void clear();

	OTMLNodeList children() const;
	OTMLNodePtr clone() const;

	template<typename T>
	T value();

	template<typename T>
	T valueAtIndex(int childIndex);
	template<typename T>
	T valueAtIndex(int childIndex, const T& def);

	bool valueAt(const std::string& childTag);
	std::string valueAt(const std::string& childTag, const std::string& def);

	template<typename T>
	void write(const T& v);
	template<typename T>
	void writeAt(const std::string& childTag, const T& v);
	template<typename T>
	void writeIn(const T& v);

	virtual std::string emit();

protected:
	OTMLNode() : m_unique(false), m_null(false) { }

	OTMLNodeList m_children;
	OTMLNodeWeakPtr m_parent;
	std::string m_tag;
	std::string m_value;
	std::string m_source;
	bool m_unique;
	bool m_null;
};

class OTMLDocument : public OTMLNode {
public:
	virtual ~OTMLDocument() { }
	static OTMLDocumentPtr create();
	static OTMLDocumentPtr parse(const std::string& fileName);
	static OTMLDocumentPtr parse(std::istream& in, const std::string& source);
	std::string emit();
	bool save(const std::string& fileName);

private:
	OTMLDocument() { }
};

class OTMLParser {
public:
	OTMLParser(OTMLDocumentPtr doc, std::istream& in) :
		currentDepth(0), currentLine(0),
		doc(doc), currentParent(doc),
		in(in) { }
	void parse();

private:
	std::string getNextLine();
	int getLineDepth(const std::string& line, bool multilining = false);
	void trim(std::string& string);
	std::vector<std::string> split(const char *str, char ch = ' ');
	void parseLine(std::string line);
	void parseNode(const std::string& data);

	int currentDepth;
	int currentLine;
	OTMLDocumentPtr doc;
	OTMLNodePtr currentParent;
	OTMLNodePtr previousNode;
	std::istream& in;
};

class OTMLEmitter {
public:
	static std::string emitNode(const OTMLNodePtr& node, int currentDepth = -1);
};

#endif
