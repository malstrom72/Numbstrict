// Measures parsing and composing a large document, typed reads of its values and the string and number functions,
// printing milliseconds, MB/s and ns per item. Built and run by tools/benchmark.sh and tools/benchmark.cmd in release.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include "../src/Numbstrict.h"

using namespace Numbstrict;

static const int ENTRIES = 200000;
static const int ROUNDS = 5;																							// the best of these counts
static const int CALLS = 1000000;																						// of each small function

static size_t sink = 0;

template<typename F> static double bestSeconds(F body) {
	double best = 1e300;
	for (int round = 0; round < ROUNDS; ++round) {
		const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
		body();
		const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		best = (seconds < best ? seconds : best);
	}
	return best;
}

static String buildDocument() {
	String code = "{\n";
	char line[256];
	for (int i = 0; i < ENTRIES; ++i) {
		switch (i % 5) {
			case 0: snprintf(line, sizeof line, "\tinteger%d: %d\n", i, i * 7919); break;
			case 1: snprintf(line, sizeof line, "\treal%d: %.17g\n", i, i * 0.001 + 1.5); break;
			case 2: snprintf(line, sizeof line, "\ttext%d: \"quoted text %d with an escape \\n and a tab \\t\"\n", i, i);
				break;
			case 3: snprintf(line, sizeof line, "\tarray%d: { %d, %d, %d, %d, %d, %d, %d, %d, %d, %d }\n", i, i, i + 1
					, i + 2, i + 3, i + 4, i + 5, i + 6, i + 7, i + 8, i + 9); break;
			default: snprintf(line, sizeof line, "\tstruct%d: { x: %d, y: %d, name: unquoted text %d }\n", i, i, -i, i);
				break;
		}
		code += line;
	}
	code += "}\n";
	return code;
}

static void report(const char* what, double seconds, double bytes, double items) {
	printf("%-42s %8.1f ms %8.1f MB/s %8.0f ns per item\n", what, seconds * 1e3, bytes / seconds / 1e6
			, seconds * 1e9 / items);
}

template<typename T> static void composeMany(const char* what, const T& text, size_t length) {
	const double seconds = bestSeconds([&]() {
		for (int i = 0; i < CALLS; ++i) {
			sink += compose(text).size();
		}
	});
	report(what, seconds, static_cast<double>(CALLS) * length, CALLS);
}

template<typename F> static void callMany(const char* what, size_t length, F call) {
	const double seconds = bestSeconds([&]() {
		for (int i = 0; i < CALLS; ++i) {
			sink += call();
		}
	});
	report(what, seconds, static_cast<double>(CALLS) * length, CALLS);
}

int main() {
	const String code = buildDocument();
	const double bytes = static_cast<double>(code.size());
	printf("document: %d entries, %.1f MB, best of %d rounds\n", ENTRIES, bytes / 1e6, ROUNDS);

	report("Element(code), one copy of the source", bestSeconds([&]() {
		Element copy(code);
		sink += copy.end() - copy.begin();
	}), bytes, 1);
	Element source(code);
	Struct parsed;
	report("parseStruct, map of the entries", bestSeconds([&]() { parsed = source.to<Struct>(); }), bytes, ENTRIES);
	report("to<Variant>() on every entry", bestSeconds([&]() {
		for (Struct::const_iterator it = parsed.begin(); it != parsed.end(); ++it) {
			sink += it->second.to<Variant>().type;
		}
	}), bytes, ENTRIES);
	report("parseVariant, the whole document as a tree", bestSeconds([&]() {
		sink += parseVariant(source).structure.size();
	}), bytes, ENTRIES);
	String composed;
	report("compose(Struct, multiLine)", bestSeconds([&]() { composed = compose(parsed, true); }), bytes, ENTRIES);
	printf("composed %.1f MB\n", composed.size() / 1e6);

	const char* shortText = "a short name";
	const char* longText = "a longer text that needs quoting because of its comma, and a length of about a hundred";
	composeMany("compose(const Char*), 12 chars", shortText, strlen(shortText));
	composeMany("compose(const String&), 12 chars", String(shortText), strlen(shortText));
	composeMany("compose(const Char*), 100 chars", longText, strlen(longText));
	composeMany("compose(const String&), 100 chars", String(longText), strlen(longText));
	callMany("stringToInt, 10 digits", 10, []() { return static_cast<size_t>(stringToInt("1234567890")); });
	callMany("charsToInt, 10 digits", 10, []() { return static_cast<size_t>(charsToInt("1234567890")); });
	callMany("stringToDouble, 17 digits", 18, []() { return static_cast<size_t>(stringToDouble("1.2345678901234567")); });
	callMany("intToString", 10, []() { return intToString(1234567890).size(); });
	callMany("doubleToString, 17 digits", 18, []() { return doubleToString(1.2345678901234567).size(); });
	printf("(sink %u)\n", static_cast<unsigned>(sink));
	return 0;
}
