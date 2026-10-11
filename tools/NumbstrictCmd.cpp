// Checks a Numbstrict file by parsing every nested value and, with -n, writes it back normalized: comments dropped,
// a struct or array on one line when that fits in 40 columns and otherwise one entry per line indented with tabs,
// keys in the order of the input and unquoted when they are identifiers, text unquoted when the format allows it
// unless the input quoted it, integers in decimal unless the input wrote them in hexadecimal, reals in their shortest
// form. Lines end with a line feed whatever the platform, so the output is the same everywhere.
//	NumbstrictCmd [-n] <input file>|- [<output file>|-]
// Exit code 0 when the input is valid Numbstrict, 1 with the error on stderr when it is not, 2 for bad arguments.
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#ifdef _WIN32
	#include <fcntl.h>
	#include <io.h>
#endif
#include "../src/Numbstrict.h"

using namespace Numbstrict;

static const size_t SINGLE_LINE_LIMIT = 40;																				// as tools/NumbstrictNorm.pika

static bool isIdentifier(const WideString& key) {
	if (key.empty() || (key[0] >= '0' && key[0] <= '9')) {
		return false;
	}
	for (WideString::const_iterator p = key.begin(); p != key.end(); ++p) {
		const WideChar c = *p;
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
			return false;
		}
	}
	return true;
}

static String composeKey(const WideString& key) {
	return (isIdentifier(key) ? String(key.begin(), key.end()) : compose(key));
}

static bool startsWith(const Element& element, const char* prefix) {
	const size_t length = strlen(prefix);
	return static_cast<size_t>(element.end() - element.begin()) >= length
			&& std::equal(prefix, prefix + length, element.begin());
}

static bool isHexadecimal(const Element& element) {
	const bool signed_ = startsWith(element, "-") || startsWith(element, "+");
	return (signed_ ? startsWith(element, "-0x") || startsWith(element, "+0x") : startsWith(element, "0x"));
}

typedef std::pair<WideString, Element> Entry;

static bool bySourceOrder(const Entry& a, const Entry& b) {
	return a.second.begin() < b.second.begin();
}

static String normalize(const Element& element, int indent, bool bracket);
static std::vector<Entry> entriesOf(const WideStruct& structure);
static std::vector<Entry> entriesOf(const Array& array);

/*
	The entries of a struct (with keys) or an array (without), on one line when that fits the limit, or one per line
	at `indent` tabs plus one. `indent` -1 asks for the one-line form regardless. Without `bracket` the entries stand
	alone, as the top level of a file does.
*/
static String composeEntry(const Entry& entry, bool key, int indent) {
	const bool empty = (entry.second.begin() == entry.second.end());
	const String value = (empty ? String() : normalize(entry.second, indent, true));
	return (key ? composeKey(entry.first) + ":" + (value.empty() ? "" : " ") : "") + value;
}

static String composeEntries(const std::vector<Entry>& entries, bool keys, int indent, bool bracket) {
	String single = (bracket ? "{ " : "");
	for (size_t i = 0; i < entries.size(); ++i) {
		single += (i == 0 ? "" : ", ") + composeEntry(entries[i], keys, -1);
	}
	single += (bracket ? " }" : "");
	if (indent < 0 || single.size() <= SINGLE_LINE_LIMIT) {
		return single;
	}
	const int inner = (bracket ? indent + 1 : indent);
	String lines = (bracket ? "{" : "");
	for (size_t i = 0; i < entries.size(); ++i) {
		lines += (keys || i == 0 ? String() : String(",")) + (bracket || i != 0 ? "\n" : "") + String(inner, '\t')
				+ composeEntry(entries[i], keys, inner);
	}
	return lines + (bracket ? "\n" + String(indent, '\t') + "}" : "");
}

static String normalize(const Element& element, int indent, bool bracket) {
	const Variant v = element.to<Variant>();
	switch (v.type) {
		case Variant::STRUCT: {
			return (v.structure.empty() ? "{ : }" : composeEntries(entriesOf(v.structure), true, indent, bracket));
		}
		case Variant::ARRAY: {
			return (v.array.empty() ? "{ }" : composeEntries(entriesOf(v.array), false, indent, bracket));
		}
		case Variant::TEXT: {																							// unquoted text stays as it was
			const bool quoted = (startsWith(element, "\"") || startsWith(element, "'"));
			return (quoted ? compose(v.text) : String(v.text.begin(), v.text.end()));
		}
		case Variant::REAL: return compose(v.real);
		case Variant::INTEGER: {
			return (isHexadecimal(element) && v.integer >= 0 ? compose(static_cast<uint64_t>(v.integer), true, 1)
					: compose(v.integer));
		}
		case Variant::UNSIGNED_INTEGER: return compose(v.unsignedInteger, isHexadecimal(element), 1);
		case Variant::BOOLEAN: return compose(v.boolean);
		default: assert(0); return String();
	}
}

static std::vector<Entry> entriesOf(const WideStruct& structure) {
	std::vector<Entry> entries(structure.begin(), structure.end());
	std::sort(entries.begin(), entries.end(), bySourceOrder);
	return entries;
}

static std::vector<Entry> entriesOf(const Array& array) {
	std::vector<Entry> entries;
	for (Array::const_iterator it = array.begin(); it != array.end(); ++it) {
		entries.push_back(Entry(WideString(), *it));
	}
	return entries;
}

/*
	The top level of a file is a list of key and value pairs or of values without braces around it; the first form is
	tried first, and when neither parses the struct's error is the one reported.
*/
static String normalizeRoot(const Element& root) {
	WideStruct structure;
	if (Parser(root).tryToParse(structure)) {
		return composeEntries(entriesOf(structure), true, 0, false);
	}
	Array array;
	if (Parser(root).tryToParse(array)) {
		return composeEntries(entriesOf(array), false, 0, false);
	}
	root.to<WideStruct>();
	return String();
}

static String loadEntireStream(std::istream& stream) {
	return String(std::istreambuf_iterator<Char>(stream), std::istreambuf_iterator<Char>());
}

int main(int argc, const char* argv[]) {
	int argi = 1;
	const bool writeNormalized = (argi < argc && strcmp(argv[argi], "-n") == 0);
	argi += (writeNormalized ? 1 : 0);
	if (argi >= argc || argi + 2 < argc) {
		std::cerr << "NumbstrictCmd [-n] <input file>|- [<output file>|-]" << std::endl
				<< "Checks the input; with -n also writes it normalized. Exit code 1 on invalid input." << std::endl;
		return 2;
	}
	const std::string inputPath = argv[argi];
	const std::string outputPath = (argi + 1 < argc ? argv[argi + 1] : "-");
#ifdef _WIN32
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);
#endif
	String source;
	if (inputPath == "-") {
		source = loadEntireStream(std::cin);
	} else {
		std::ifstream stream(inputPath.c_str(), std::ios::binary);
		if (!stream.good()) {
			std::cerr << "Could not open input file " << inputPath << std::endl;
			return 1;
		}
		source = loadEntireStream(stream);
	}
	try {
		const String root = normalizeRoot(Element(source, (inputPath == "-" ? "stdin" : inputPath)));
		const String normalized = (root.empty() ? root : root + "\n");
		if (writeNormalized) {
			if (outputPath == "-") {
				std::cout << normalized;
			} else {
				std::ofstream stream(outputPath.c_str(), std::ios::binary);
				if (!stream.good()) {
					std::cerr << "Could not open output file " << outputPath << std::endl;
					return 1;
				}
				stream << normalized;
			}
		}
	}
	catch (const Exception& x) {
		std::cerr << x.what() << std::endl;
		return 1;
	}
	return 0;
}
