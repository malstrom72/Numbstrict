#ifndef Numbstrict_h
#define Numbstrict_h

#include "assert.h"
#include <map>
#include <vector>
#include <string>
#include <exception>
#include <memory>
#include <cstdint>
#include <ostream>	// for std::basic_ostream and iostream state used by operator<<

namespace Numbstrict {

class Parser;
typedef char Char;
typedef unsigned char UChar;
typedef wchar_t WideChar;
typedef std::basic_string<Char> String;	// notice: not utf8, assumed ISO-8859-1!
typedef std::basic_string<WideChar> WideString;
typedef std::basic_string<Char>::const_iterator StringIt;
typedef std::basic_string<WideChar>::const_iterator WideStringIt;
typedef std::pair<String, String> SourceAndFile;
typedef std::pair<int, int> LineAndColumn;

struct Exception : public std::exception { virtual ~Exception() throw() { } };

/**
	An UndefinedElementError is thrown when trying to parse an uninitialized Element. A typical situation would be
	trying to read the contents of an undefined key in an Struct.
**/
struct UndefinedElementError : public Exception {
	UndefinedElementError() { }
	virtual const char* what() const throw() { return "Undefined Numbstrict element"; }
	virtual ~UndefinedElementError() throw() { }
};

class UndefinedNamedElementError : public UndefinedElementError {
	public:
		UndefinedNamedElementError(const std::string& name) : name(name) { }
		virtual const char* what() const throw();
		virtual ~UndefinedNamedElementError() throw() { }
	
	protected:
		std::string name;
		mutable std::string errorString;
};

class ParsingError : public Exception {
	public:
		ParsingError(const String& filename, size_t offset, int line, int column)
						: filename(filename), offset(offset), line(line), column(column) { }
		virtual const char* what() const throw();
		String getFilename() const { return filename; }
		size_t getOffset() const { return offset; }
		int getLineNumber() const { return line; }
		int getColumnNumber() const { return column; }
		virtual ~ParsingError() throw() { }

	protected:
		const String filename;
		const size_t offset;
		const int line;
		const int column;
	mutable std::string errorString;
};

/**
	An Element represents the entire source code text or a partially parsed or composed piece of it. It maintains a
	shared pointer to the original source String (and optional filename) and iterators that designates a range within
	that source.

	Use to<type>() to attempt parsing the Element source code into one of the supported types: Array, Struct, String,
	WideString, double, float, int, bool and Variant.

	Use one of the overloaded global compose() functions to create an Element from one of the supported types.
	
	Use code() to extract the Numbstrict source code string or substring for this element.

	An Element can be used to construct a Parser object for more parsing options, e.g. for parsing without throwing an
	exception on error.
**/
class Element {
	public:
		Element() { }
		Element(const String& code, const String& filename = String())
				: s(std::make_shared<SourceAndFile>(code, filename)), b(s->first.begin()), e(s->first.end()) { }
		Element(const Element& parent, const StringIt begin, const StringIt end) : s(parent.s), b(begin), e(end) { }
		bool exists() const { return static_cast<bool>(s); }
		StringIt begin() const { assert(exists()); return b; }
		StringIt end() const { assert(exists()); return e; }
		template<typename T> T to() const;
		template<typename T> T toOptional(const T& defaultValue = T()) const;
		template<typename T> bool tryToParse(T& target) const;       // expects convertible to `T`; false on failure
		String code() const { if (!exists()) { throw UndefinedElementError(); }; return String(b, e); }
		String optionalCode(const String& defaultCode = String()) const { return (!exists() ? defaultCode : code()); }
		String filename() const { assert(exists()); return s->second; }
		size_t offset(const StringIt p) const { assert(exists()); return p - s->first.begin(); }	// `p` = source iterator
		LineAndColumn lineAndColumn(StringIt p) const;				// `p` = source iterator
	
	protected:
		std::shared_ptr<SourceAndFile> s;
		StringIt b;
		StringIt e;
};

// Now that Element is a complete type, containers of Element are well-formed.
typedef std::vector<Element> Array;
typedef std::map<String, Element> Struct;	// standard struct handles only iso-8859-1 keys
typedef std::map<WideString, Element> WideStruct;	// a wide struct can handle any unicode keys

/**
	Notice that type is deduced from text contents and there may be ambiguities, e.g. an empty struct might be
	identified as an empty array. Implementation does not depend on C++11 non-trival class unions and stores structures
	and arrays separately for simplicity. If we based this on C++17 we could have used std::variant instead.
**/
struct Variant {
	enum Type {
		INVALID
		, STRUCT 			// { : }
		, ARRAY 			// { }
		, TEXT 				// "" '' and generic text (including unparsable { } elements)
		, REAL 				// #.#
		, UNSIGNED_INTEGER	// [+]# (only overflowing 64-bit integers)
		, INTEGER 			// [+-]#
		, BOOLEAN 			// true | false
	} type;
	Variant() : type(INVALID) { }
	WideStruct structure;
	Array array;
	WideString text;
	union {
		double real;
		int64_t integer;
		uint64_t unsignedInteger;
		bool boolean;
	};
};


class Parser {
	friend bool unitTest();
	
	public:
		Parser(const Element& source);
		StringIt getFailPoint() const;

		// Parses whitespaces and comments and returns true if entire string was parsed.
		bool isEmpty();

		bool tryToParse(Array& toArray);	// expects '{ }' array; false on failure
		bool tryToParse(Struct& toStruct);	// expects '{ : }' struct; false on failure
		bool tryToParse(WideStruct& toWideStruct);	// expects '{ : }' wide struct; false on failure
		bool tryToParse(String& toString);	// expects quoted or unquoted string; false on failure
		bool tryToParse(WideString& toString);	// expects quoted string; false on failure
		bool tryToParse(float& toFloat);	// expects real number; false on failure
		bool tryToParse(double& toDouble);	// expects real number; false on failure
		bool tryToParse(int8_t& toInt);	// expects signed integer; false on failure
		bool tryToParse(uint8_t& toInt);	// expects unsigned integer; false on failure
		bool tryToParse(int16_t& toInt);	// expects signed integer; false on failure
		bool tryToParse(uint16_t& toInt);	// expects unsigned integer; false on failure
		bool tryToParse(int32_t& toInt);	// expects signed integer; false on failure
		bool tryToParse(uint32_t& toInt);	// expects unsigned integer; false on failure
		bool tryToParse(int64_t& toInt);	// expects signed integer; false on failure
		bool tryToParse(uint64_t& toInt);	// expects unsigned integer; false on failure
		bool tryToParse(bool& toBool);	// expects `true` or `false`; false on failure
		bool tryToParse(Variant& toVariant);	// expects any element; false on failure
		template<typename T> bool tryToParse(std::vector<T>& toVector);	// expects '{ }' array; false on failure
		template<typename T> bool tryToParse(std::map<String, T>& toMap);	// expects '{ : }' struct; false on failure
		template<typename T> bool tryToParse(std::map<WideString, T>& toMap);	// expects '{ : }' wide struct; false on failure
		template<typename T> bool tryToParse(T& to, size_t& failOffset);	// sets `failOffset` on error; false on failure
		template<typename T> T& parse(T& to);

	protected:
		const Element source;
		StringIt p;
		template<typename T> bool tryToParseSignedInt(T& i);
		template<typename T> bool tryToParseUnsignedInt(T& ui);
		template<typename T> bool tryToParseReal(T& r);
		template<typename C> bool quotedString(std::basic_string<C>& string);
		template<typename C> void unquotedText(std::basic_string<C>& string);
		template<typename C> bool stringOrText(std::basic_string<C>& string);
		bool valueListElements(Array& elements);
		template<typename C> bool keyValuePair(std::map<std::basic_string<C>, Element>& elements);
		template<typename C> bool keyValueElements(std::map<std::basic_string<C>, Element>& elements);
		template<typename S> bool tryToParseStruct(S& elements);
		bool parseIdentifier(String& identifier);
		bool blockElement(Element& block);
		bool valueElement(Element& Element);
		bool quotedStringElement(Element& Element);
		bool unquotedTextElement(Element& Element);
		bool nextElement();
		bool horizontalWhiteAndComments();
		bool whiteAndComments();
		bool comment();
		bool eof() const;
		void throwError();
		String::difference_type left() const;
};

template<typename T> bool Parser::tryToParse(T& to, size_t& failOffset) { // sets `failOffset` on error; false if fail
	if (!tryToParse(to)) {
		failOffset = getFailPoint() - source.begin();
		return false;
	}
	return true;
}

template<typename T> T& Parser::parse(T& to) {
	if (!tryToParse(to)) {
		throwError();
	}
	return to;
}

template<typename T> T Element::to() const {
	if (!exists()) {
		throw UndefinedElementError();
	}
	T v;
	Parser(*this).parse(v);
	return v;
}

template<typename T> T Element::toOptional(const T& defaultValue) const {
	T v(defaultValue);
	if (exists()) {
		Parser(*this).parse(v);
	}
	return v;
}

template<typename T> bool Element::tryToParse(T& target) const { // expects convertible to `T`; false on failure
	if (!exists()) {
		throw UndefinedElementError();
	}
	return Parser(*this).tryToParse(target);
}

template<typename T> bool Parser::tryToParse(std::vector<T>& toVector) { // expects '{ }' array; false on failure
	Array elems;
	if (!tryToParse(elems)) {
		return false;
	}
	toVector.clear();
	toVector.reserve(elems.size());
	for (Array::const_iterator it = elems.begin(); it != elems.end(); ++it) {
		toVector.push_back(it->template to<T>());
	}
	return true;
}

template<typename T> bool Parser::tryToParse(std::map<String, T>& toMap) { // expects '{ : }' struct; false on failure
	Struct elems;
	if (!tryToParse(elems)) {
		return false;
	}
	toMap.clear();
	for (Struct::const_iterator it = elems.begin(); it != elems.end(); ++it) {
		toMap.insert(std::make_pair(it->first, it->second.template to<T>()));
	}
	return true;
}

template<typename T> bool Parser::tryToParse(std::map<WideString, T>& toMap) { // expects '{:}' struct; false if fail
	WideStruct elems;
	if (!tryToParse(elems)) {
		return false;
	}
	toMap.clear();
	for (WideStruct::const_iterator it = elems.begin(); it != elems.end(); ++it) {
		toMap.insert(std::make_pair(it->first, it->second.template to<T>()));
	}
	return true;
}

inline std::basic_ostream<Char>& operator<<(std::basic_ostream<Char>& o, const Element& s) {
	o << s.to<String>();
	return o;
}

inline std::basic_ostream<WideChar>& operator<<(std::basic_ostream<WideChar>& o, const Element& s) {
	o << s.to<WideString>();
	return o;
}

String compose(bool fromBool);
String compose(int8_t fromInt, bool hexFormat = false, int minHexLength = 2);
String compose(uint8_t fromInt, bool hexFormat = false, int minHexLength = 2);
String compose(int16_t fromInt, bool hexFormat = false, int minHexLength = 4);
String compose(uint16_t fromInt, bool hexFormat = false, int minHexLength = 4);
String compose(int32_t fromInt, bool hexFormat = false, int minHexLength = 8);
String compose(uint32_t fromInt, bool hexFormat = false, int minHexLength = 8);
String compose(int64_t fromInt, bool hexFormat = false, int minHexLength = 16);
String compose(uint64_t fromInt, bool hexFormat = false, int minHexLength = 16);
String compose(float fromFloat);	// same as floatToString()
String compose(double fromDouble);	// same as doubleToString()
String compose(const Char* fromString, bool preferUnquoted = false);
String compose(const String& fromString, bool preferUnquoted = false);
String compose(const WideChar* fromString, bool preferUnquoted = false);
String compose(const WideString& fromString, bool preferUnquoted = false);
String compose(const Array& array, bool multiLine = false, bool bracket = true);
String compose(const Struct& structure, bool multiLine = false, bool bracket = true);
String compose(const WideStruct& structure, bool multiLine = false, bool bracket = true);
String compose(const Variant& variant);

template<typename T> String compose(const std::vector<T>& vector, bool multiLine = false, bool bracket = true) {
	Array elems;
	elems.reserve(vector.size());
	for (typename std::vector<T>::const_iterator it = vector.begin(); it != vector.end(); ++it) {
		elems.push_back(compose(*it));
	}
	return compose(elems, multiLine, bracket);
}

template<typename T> String compose(const std::map<String, T>& map, bool multiLine = false, bool bracket = true) {
	Struct elems;
	for (typename std::map<String, T>::const_iterator it = map.begin(); it != map.end(); ++it) {
		elems.insert(std::make_pair(it->first, compose(it->second)));
	}
	return compose(elems, multiLine, bracket);
}

// Functional interface. Same as element.to<type>() etc, so just a matter of taste.

template<typename T> T parseRequired(const Element& source) { return source.to<T>(); }
template<typename T> T parseOptional(const Element& source, const T& defaultValue = T()) { return source.toOptional<T>(defaultValue); }
template<typename T> std::vector<T> parseVector(const String& code, const String& filename = String()) { return Element(code, filename).to< std::vector<T> >(); }
template<typename T> std::vector<T> parseVector(const Element& source) { return source.to< std::vector<T> >(); }
template<typename T> std::map<String, T> parseMap(const String& code, const String& filename = String()) { return Element(code, filename).to< std::map<String, T> >(); }
template<typename T> std::map<String, T> parseMap(const Element& source) { return source.to< std::map<String, T> >(); }
inline Array parseArray(const String& code, const String& filename = String()) { return Element(code, filename).to<Array>(); }
inline Array parseArray(const Element& source) { return source.to<Array>(); }
inline Struct parseStruct(const String& code, const String& filename = String()) { return Element(code, filename).to<Struct>(); }
inline Struct parseStruct(const Element& source) { return source.to<Struct>(); }
inline WideStruct parseWideStruct(const String& code, const String& filename = String()) { return Element(code, filename).to<WideStruct>(); }
inline WideStruct parseWideStruct(const Element& source) { return source.to<WideStruct>(); }
inline Variant parseVariant(const String& code, const String& filename = String()) { return Element(code, filename).to<Variant>(); }
inline Variant parseVariant(const Element& source) { return source.to<Variant>(); }

String intToString(int value);
String intToHexString(unsigned int value, int minLength = 8);
int stringToInt(const String& s, size_t* nextOffset = 0);

class FloatStringBatchGuard {
	public:
		FloatStringBatchGuard();
		~FloatStringBatchGuard();
	private:
		bool ownsNormalization;
};

String floatToString(float value);
float stringToFloat(const String& s, size_t* nextOffset = 0);
String doubleToString(double value);
double stringToDouble(const String& s, size_t* nextOffset = 0);


String quoteString(const String& s, Char quoteChar = '\"');
String unquoteString(const String& s, size_t* nextOffset = 0);
String quoteWideString(const WideString& s, Char quoteChar = '\"');
WideString unquoteWideString(const String& s, size_t* nextOffset = 0);	// Handy low-level utilities. All accepts leading and trailing white spaces and will convert as much as possible.
// Similar to strtod etc. They work directly on Char pointers and they do not allocate.
Char* intToChars(int value, Char* destination);	// destination should fit at least 11 chars (for 32-bit int)
// TODO : int charsToInt(const Char* begin, const Char* end = 0, const Char** next = 0);	// if end is 0, strlen will be used
Char* intToHexChars(unsigned int value, Char* destination, int minLength = 8);	// destination should fit at least 8 chars (for 32-bit int)
// TODO : unsigned int hexCharsToInt(const Char* begin, const Char* end = 0, const Char** next = 0);	// if end is 0, strlen will be used
Char* floatToChars(float value, Char* destination);	// destination should fit at least 32 chars
float charsToFloat(const Char* begin, const Char* end = 0, const Char** next = 0);	// if end is 0, strlen will be used
Char* doubleToChars(const double value, Char* destination);	// destination should fit at least 32 chars
double charsToDouble(const Char* begin, const Char* end = 0, const Char** next = 0);	// if end is 0, strlen will be used

bool unitTest();

} // namespace Numbstrict

#endif /* Numbstrict_h */
