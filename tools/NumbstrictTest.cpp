#include <iostream>
#include <fstream>
#include <algorithm>
#include <iterator>
#include <set>
#include "../src/Numbstrict.h"

//	#include <rapidcheck.h>

	#include <limits>

int numbstrictTest(const std::string& source) {
#if 0
	std::cout << Numbstrict::compose(source, true) << std::endl;
	bool i = Numbstrict::Element(source).to<bool>();
	std::cout << i << std::endl;
#endif
//	std::cout << Numbstrict::compose(0.0002432) << std::endl;

//	Numbstrict::Struct m = Numbstrict::Element(source).to<Numbstrict::Struct>();

	Numbstrict::Variant v = Numbstrict::Element(source).to<Numbstrict::Variant>();
	switch (v.type) {
		case Numbstrict::Variant::INVALID: std::cout << "INVALID" << std::endl; break;
		case Numbstrict::Variant::STRUCT: std::cout << "STRUCT(" << v.array.size() << ") " << Numbstrict::compose(v.structure, true) << std::endl; break;
		case Numbstrict::Variant::ARRAY: std::cout << "ARRAY (" << v.array.size() << ") " << Numbstrict::compose(v.array, true) << std::endl; break;
		case Numbstrict::Variant::TEXT: std::cout << "TEXT: " << Numbstrict::compose(v.text, true) << std::endl; break;
		case Numbstrict::Variant::REAL: std::cout << "REAL: " << Numbstrict::compose(v.real) << std::endl; break;
		case Numbstrict::Variant::INTEGER: std::cout << "INTEGER: " << Numbstrict::compose(v.integer, false) << std::endl; break;
		case Numbstrict::Variant::BOOLEAN: std::cout << "BOOLEAN: " << Numbstrict::compose(v.boolean) << std::endl; break;
	}
#if 0
	Numbstrict::Array array;
	Numbstrict::Struct structure = Numbstrict::Element(source);
/*	if (!parser.tryToParse(array)) {
		ptrdiff_t offset = parser.getFailPoint() - source.begin();
		std::cout << "!!!! Fail at offset " << offset << std::endl;
		std::cout << "----------" << std::endl;
		const size_t start = std::max<ptrdiff_t>(offset - 50, 0);
		std::cout << source.substr(start, offset - start) << "<!!!!>" << source.substr(offset, 50) << std::endl; 
	}*/
	std::cout << '{' << std::endl;
	for (const auto& elem : array) {
		std::cout << elem << std::endl;
	}
	std::cout << '}' << std::endl;
	for (const auto& elem : structure) {
		std::cout << elem.first << ": " << elem.second << std::endl;
	}
#endif
/*
	Numbstrict::Span root(source);
	std::vector< Numbstrict::Span > subkeys = root.parseToArray();
	for (const auto& elem in subkeys) {
		Numbstrict::Parser parser(elem);
		if (!parser.parseText()) {
		}
		parser.sourceText();
		
		StringIt failpoint;
		std::string s;
		if (!elem.parseToString(s, failpoint))
		
		std::string s = elem;
*/
	return 0;
}

template<typename C> std::basic_string<C> loadEntireStream(std::basic_istream<C>& stream) {
	std::istreambuf_iterator<C> it(stream);
	std::istreambuf_iterator<C> end;
	return std::basic_string<C>(it, end);
}

class XorshiftRandom2x32 {
	public:		XorshiftRandom2x32(unsigned int seed0 = 123456789, unsigned int seed1 = 362436069);
	public:		void randomSeed();
	public:		unsigned int nextUnsignedInt() throw();
	public:		unsigned int nextUnsignedInt(unsigned int maxx) throw(); // Range [0,maxx]
	public:		double nextDouble() throw();
	public:		double operator()() throw();
	public:		float nextFloat() throw();
	public:		void setState(unsigned int x, unsigned int y) throw();
	public:		void getState(unsigned int& x, unsigned int& y) throw();
	protected:	unsigned int px;
	protected:	unsigned int py;
};
inline XorshiftRandom2x32::XorshiftRandom2x32(unsigned int seed0, unsigned int seed1) : px(seed0), py(seed1) { }
inline unsigned int XorshiftRandom2x32::nextUnsignedInt() throw() {
	unsigned int t = px ^ (px << 10);
	px = py;
	py = py ^ (py >> 13) ^ t ^ (t >> 10);
	return py;
}

// From MersenneTwister by by Makoto Matsumoto, Takuji Nishimura, and Shawn Cokus, Richard J. Wagner, Magnus Jonsson
inline unsigned int XorshiftRandom2x32::nextUnsignedInt(unsigned int maxx) throw() {
	unsigned int used = maxx;
	used |= used >> 1;
	used |= used >> 2;
	used |= used >> 4;
	used |= used >> 8;
	used |= used >> 16;
	
	unsigned int i;
	do {
		i = nextUnsignedInt() & used;
	} while (i > maxx);
	return i;
}

inline double XorshiftRandom2x32::nextDouble() throw() {
	nextUnsignedInt();
	return py * 2.3283064365386962890625e-10 + px * 5.42101086242752217003726400434970855712890625e-20;
}

inline double XorshiftRandom2x32::operator()() throw() {
	return nextDouble();
}

inline float XorshiftRandom2x32::nextFloat() throw() {
	return static_cast<float>(nextUnsignedInt() * 2.3283064365386962890625e-10);
}

inline void XorshiftRandom2x32::setState(unsigned int x, unsigned int y) throw() { px = x; py = y; }
inline void XorshiftRandom2x32::getState(unsigned int& x, unsigned int& y) throw() { x = px; y = py; }

static bool createRandomBool(XorshiftRandom2x32& prng) {
	return (prng.nextUnsignedInt(1) != 0);
}

static int createRandomInt(XorshiftRandom2x32& prng) {
	if (prng.nextUnsignedInt(1) == 0) {
		return static_cast<int>(prng.nextUnsignedInt(200)) - 100;
	} else {
		return static_cast<int>(prng.nextUnsignedInt());
	}
}

static double createRandomReal(XorshiftRandom2x32& prng) {
	switch (prng.nextUnsignedInt(3)) {
		default: assert(0);
		case 0: return prng.nextDouble();
		case 1: return prng.nextDouble() * 200.0 - 100.0;
		case 2: return prng.nextDouble() * 2000000.0 - 1000000.0;
		case 3: {
			union {
				unsigned int v[2];
				double d;
			} u;
			u.v[0] = prng.nextUnsignedInt();
			u.v[1] = prng.nextUnsignedInt();
			return u.d;
		}
	}
}

Numbstrict::Variant::Type randomType(XorshiftRandom2x32& prng, int maxDepth) {
	return static_cast<Numbstrict::Variant::Type>(maxDepth <= 0 ? prng.nextUnsignedInt(3) + 3 : prng.nextUnsignedInt(5) + 1);
}

std::wstring cleanupWString(const std::wstring& s) {
	std::wstring clean;
	std::copy_if(s.begin(), s.end(), std::back_inserter(clean), [](wchar_t wc) {
		return (!(wc < 0 || wc >= 0x110000 || (wc >= 0xD800 && wc < 0xE000) || wc == 0xFFFE || wc == 0xFFFF));
	});
	return clean;
}

static std::wstring createRandomWideString(XorshiftRandom2x32& prng) {
	switch (prng.nextUnsignedInt(3)) {
		default: assert(0);
		case 0:
		case 1:
		case 2: {
			std::wstring s;
			size_t l = std::min(prng.nextUnsignedInt(32), prng.nextUnsignedInt(32));
			for (size_t i = 0; i < l; ++i) {
				s += static_cast<wchar_t>(32 + prng.nextUnsignedInt(126 - 32));
			}
			return s;
		}
		case 3: {
			std::wstring s;
			size_t l = 1 + std::min(std::min(prng.nextUnsignedInt(256), prng.nextUnsignedInt(256)), prng.nextUnsignedInt(256));
			for (size_t i = 0; i < l; ++i) {
				s += static_cast<wchar_t>(prng.nextUnsignedInt());
			}
			return cleanupWString(s);
		}
	}
}

static std::string createRandomKey(XorshiftRandom2x32& prng, std::set<std::string>& usedup) {
	std::string key;
	do {
		key.clear();
		size_t l = 1 + prng.nextUnsignedInt(31);
		for (size_t i = 0; i < l; ++i) {
			key += static_cast<char>(32 + prng.nextUnsignedInt(126 - 32));
		}
	} while (!usedup.insert(key).second);
	return key;
}

Numbstrict::Element composeRandomElement(XorshiftRandom2x32& dataPRNG, XorshiftRandom2x32& formatPRNG, int maxDepth);
bool verifyRandomElement(XorshiftRandom2x32& prng, int maxDepth, const Numbstrict::Element& element);

Numbstrict::Struct createRandomStruct(XorshiftRandom2x32& dataPRNG, XorshiftRandom2x32& formatPRNG, int maxDepth) {
	Numbstrict::Struct map;
	size_t size = std::min(dataPRNG.nextUnsignedInt(32), dataPRNG.nextUnsignedInt(32));
	std::set<std::string> usedup;
	for (size_t i = 0; i < size; ++i) {
		const std::string key = createRandomKey(dataPRNG, usedup);
		map.insert(std::make_pair(key, composeRandomElement(dataPRNG, formatPRNG, maxDepth)));
	}
	return map;
}

bool verifyRandomStruct(XorshiftRandom2x32& prng, int maxDepth, const Numbstrict::Struct& map) {
	size_t size = std::min(prng.nextUnsignedInt(32), prng.nextUnsignedInt(32));
	if (map.size() != size) {
		return false;
	}
	std::set<std::string> usedup;
	for (size_t i = 0; i < size; ++i) {
		const std::string key = createRandomKey(prng, usedup);
		auto found = map.find(key);
		if (found == map.end()) {
			return false;
		}
		const Numbstrict::Element& e = found->second;
		if (!verifyRandomElement(prng, maxDepth, e)) {
			return false;
		}
	}
	return true;
}

Numbstrict::Array createRandomArray(XorshiftRandom2x32& dataPRNG, XorshiftRandom2x32& formatPRNG, int maxDepth) {
	Numbstrict::Array vec;
	vec.resize(std::min(dataPRNG.nextUnsignedInt(128), dataPRNG.nextUnsignedInt(128)));
	for (Numbstrict::Element& e : vec) {
		e = composeRandomElement(dataPRNG, formatPRNG, maxDepth);
	}
	return vec;
}

bool verifyRandomArray(XorshiftRandom2x32& prng, int maxDepth, const Numbstrict::Array& vec) {
	size_t expectedSize = std::min(prng.nextUnsignedInt(128), prng.nextUnsignedInt(128));
	size_t gotSize = vec.size();
	if (gotSize != expectedSize) {
		assert(0);
		return false;
	}
	for (const Numbstrict::Element& e : vec) {
		if (!verifyRandomElement(prng, maxDepth, e)) {
			assert(0);
			return false;
		}
	}
	return true;
}

static Numbstrict::String createRandomMultilineComment(XorshiftRandom2x32& prng, int maxDepth) {
	Numbstrict::String s = "/*";
	size_t l = std::min(prng.nextUnsignedInt(64), prng.nextUnsignedInt(64));
	for (size_t i = 0; i < l; ++i) {
		int max = 126;
		switch (prng.nextUnsignedInt(7)) {
			case 0: s += " \t\n\r"[prng.nextUnsignedInt(3)]; break;
			case 1: {
				if (maxDepth > 0 && (s.empty() || s.back() != '*')) {
					s += createRandomMultilineComment(prng, maxDepth - 1);
				}
				break;
			}
			case 2: max = 255;
			default: {
				char c = static_cast<char>(32 + prng.nextUnsignedInt(max - 32));
				if (c != '*' && c != '/') {
					s += static_cast<char>(c);
				}
			#if 0
				if (s.size() >= 2 && (std::equal(s.end() - 2, s.end(), "/*")
						|| std::equal(s.end() - 2, s.end(), "*/"))) {
					s.resize(s.size() - 1);
				}
			#endif
				break;
			}
		}
	}
	return s + "*/";
}

static Numbstrict::String createRandomWhiteSpace(XorshiftRandom2x32& prng, bool lfOk) {
	Numbstrict::String s;
	do {
		switch (prng.nextUnsignedInt(lfOk ? 2 : 1)) {
			case 0: {
				do {
					s += " \t\n\r"[prng.nextUnsignedInt(lfOk ? 3 : 1)];
				} while (prng.nextUnsignedInt(3) != 0);
				break;
			}
			case 1: s += createRandomMultilineComment(prng, 2); break;
			case 2: {
				s += "//";
				size_t l = std::min(prng.nextUnsignedInt(64), prng.nextUnsignedInt(64));
				for (size_t i = 0; i < l; ++i) {
					s += static_cast<char>(32 + prng.nextUnsignedInt(255 - 32));
				}
				if (prng.nextUnsignedInt(1) != 0) {
					s += '\r';
				}
				s += '\n';
				break;
			}
		}
	} while (prng.nextUnsignedInt(3) != 0);
	return s;
}

Numbstrict::Element composeRandomElement(XorshiftRandom2x32& dataPRNG, XorshiftRandom2x32& formatPRNG, int maxDepth) {
	Numbstrict::Variant::Type type = randomType(dataPRNG, maxDepth);
	Numbstrict::String s;
	const bool option = (formatPRNG.nextUnsignedInt(1) != 0);
	switch (type) {
		case Numbstrict::Variant::STRUCT: s = Numbstrict::compose(createRandomStruct(dataPRNG, formatPRNG, maxDepth - 1), option); break;
		case Numbstrict::Variant::ARRAY: s = Numbstrict::compose(createRandomArray(dataPRNG, formatPRNG, maxDepth - 1), option); break;
		case Numbstrict::Variant::TEXT: s = Numbstrict::compose(createRandomWideString(dataPRNG), option); break;
		case Numbstrict::Variant::REAL: s = Numbstrict::compose(createRandomReal(dataPRNG)); break;
		case Numbstrict::Variant::INTEGER: s = Numbstrict::compose(createRandomInt(dataPRNG), option); break;
		case Numbstrict::Variant::BOOLEAN: s = Numbstrict::compose(createRandomBool(dataPRNG)); break;
		default: assert(0); break;
	}
	if (formatPRNG.nextUnsignedInt(3) == 0) {
		s = createRandomWhiteSpace(formatPRNG, false) + s;
	}
	if (formatPRNG.nextUnsignedInt(3) == 0) {
		Numbstrict::String ws = createRandomWhiteSpace(formatPRNG, true);
		if (!ws.empty() && ws[0] == '/') {
			s += ' ';
		}
		s += ws;
	}
	return Numbstrict::Element(s);
}

bool doublesAreEqual(double a, double b) {
	return (((a != a) && (b != b)) || a == b);
}

bool verifyRandomElement(XorshiftRandom2x32& prng, int maxDepth, const Numbstrict::Element& element) {
	Numbstrict::Variant::Type type = randomType(prng, maxDepth);
	Numbstrict::String s;
	switch (type) {
		case Numbstrict::Variant::STRUCT: return verifyRandomStruct(prng, maxDepth - 1, element.to<Numbstrict::Struct>());
		case Numbstrict::Variant::ARRAY: return verifyRandomArray(prng, maxDepth - 1, element.to<Numbstrict::Array>());
		case Numbstrict::Variant::TEXT: {
			if (element.to<std::wstring>() != createRandomWideString(prng)) {
				const std::string s = element.code();
				std::cout << s << std::endl;
				assert(0);
				return false;
			}
			return true;
		}
		case Numbstrict::Variant::REAL: {
			if (!doublesAreEqual(element.to<double>(), createRandomReal(prng))) {
				assert(0);
				return false;
			}
			return true;
		}
		case Numbstrict::Variant::INTEGER: {
			if (element.to<int>() != createRandomInt(prng)) {
				assert(0);
				return false;
			}
			return true;
		}
		case Numbstrict::Variant::BOOLEAN: {
			if (element.to<bool>() != createRandomBool(prng)) {
				assert(0);
				return false;
			}
			return true;
		}
		default: assert(0); break;
	}
	return false;
}

struct RndState {
	unsigned int v[4];
};

static void deepParse(const Numbstrict::Element& element) {
	Numbstrict::Variant var = Numbstrict::parseVariant(element);

	std::string backAgain = Numbstrict::compose(var);
	Numbstrict::Variant var2 = Numbstrict::parseVariant(backAgain, "backAgain");
	std::string andAgain = Numbstrict::compose(var2);
	if (backAgain != andAgain) {
		var = Numbstrict::parseVariant(element);
		backAgain = Numbstrict::compose(var);
		var2 = Numbstrict::parseVariant(backAgain, "backAgain");
		andAgain = Numbstrict::compose(var2);
	}
	assert(backAgain == andAgain);

	switch (var.type) {
		case Numbstrict::Variant::STRUCT: {
			for (Numbstrict::Struct::const_iterator it = var.structure.begin(); it != var.structure.end(); ++it) {
				deepParse(it->second);
			}
			break;
		}
		case Numbstrict::Variant::ARRAY: {
			for (Numbstrict::Array::const_iterator it = var.array.begin(); it != var.array.end(); ++it) {
				deepParse(*it);
			}
			break;
		}
		case Numbstrict::Variant::TEXT:
		case Numbstrict::Variant::REAL:
		case Numbstrict::Variant::UNSIGNED_INTEGER:
		case Numbstrict::Variant::INTEGER:
		case Numbstrict::Variant::BOOLEAN: {
			break;
		}

		default: assert(0);
	}
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
	const std::string contents(reinterpret_cast<const char*>(Data), reinterpret_cast<const char*>(Data) + Size);
	Numbstrict::Struct structure;
	try {
		structure = Numbstrict::parseStruct(contents, "test");
		for (Numbstrict::Struct::const_iterator it = structure.begin(); it != structure.end(); ++it) {
			deepParse(it->second);
		}
	}
	catch (Numbstrict::Exception& x) {
		return 0;
	}
	std::string backAgain = Numbstrict::compose(structure);
	Numbstrict::Struct structure2 = Numbstrict::parseStruct(backAgain, "backAgain");
	std::string andAgain = Numbstrict::compose(structure2);
	assert(backAgain == andAgain);


	
/*   Numbstrict::Variant variant;
	try {
		variant = Numbstrict::parseVariant(contents, "somefile");
	}
	catch (Numbstrict::Exception& x) {
		;
	}
	if (variant.type != Numbstrict::Variant::INVALID) {
		Numbstrict::compose(variant);
	}	*/
	// std::cout << composed << std::endl;
	return 0;
}

#ifdef LIBFUZZ_STANDALONE

#include <dirent.h>

void doOne(const char* fn) {
	printf ("%s\n", fn);
	fprintf(stderr, "Running: %s\n", fn);
	FILE *f = fopen(fn, "r");
	assert(f);
	fseek(f, 0, SEEK_END);
	size_t len = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *buf = (unsigned char*)malloc(len);
	size_t n_read = fread(buf, 1, len, f);
	fclose(f);
	assert(n_read == len);
	LLVMFuzzerTestOneInput(buf, len);
	free(buf);
	fprintf(stderr, "Done:    %s: (%zd bytes)\n", fn, n_read);
}

int main(int argc, const char* argv[]) {
	for (int i = 1; i < argc; ++i) {
		DIR *dir;
		struct dirent *ent;
		if ((dir = opendir (argv[i])) != NULL) {
			while ((ent = readdir (dir)) != NULL) {
				if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
					char fn[1024];
					strcpy(fn, argv[i]);
					strcat(fn, ent->d_name);
					doOne(fn);
				}
			}
				closedir (dir);
		} else {
			if (errno == ENOTDIR) {
				doOne(argv[i]);
			} else {
				perror("");
				return EXIT_FAILURE;
			}
		}
	}
	return 0;
}
#endif

#ifndef LIBFUZZ
#ifndef LIBFUZZ_STANDALONE
int main(int argc, const char* argv[]) {
	try {
	#if !defined(NDEBUG)
		Numbstrict::unitTest();
	#endif

	#if 0
		XorshiftRandom2x32 dataPRNG(32234823, 29834293);
		XorshiftRandom2x32 formatPRNG(1239192, 12831212);
		
		dataPRNG.setState(0xba3a677, 0xc3bfe443);
		formatPRNG.setState(0x9d36e362, 0xfbfc3552);
		
		size_t minSize = 0x7FFFFFFF;
		std::string minCode;
		RndState minState;
		
		for (int i = 0; i < 10000; ++i) {
			const int MAX_DEPTH = 2;
			std::cout << i << std::endl;
			RndState rndState;
			dataPRNG.getState(rndState.v[0], rndState.v[1]);
			formatPRNG.getState(rndState.v[2], rndState.v[3]);
			XorshiftRandom2x32 prngCopy(dataPRNG);
			XorshiftRandom2x32 prngCopy2(dataPRNG);
			Numbstrict::Struct map = createRandomStruct(dataPRNG, formatPRNG, MAX_DEPTH);
			Numbstrict::String composed = compose(map, (formatPRNG.nextUnsignedInt() & 1) != 0, (formatPRNG.nextUnsignedInt() & 1) != 0);
		//	std::cout << std::endl << std::endl << "-------------------------------------" << std::endl << std::endl;
		//	std::cout << composed << std::endl;
			Numbstrict::Parser parser(composed);
			Numbstrict::Struct back;
			bool success = parser.tryToParse(back);
			if (success) {
				success = verifyRandomStruct(prngCopy, MAX_DEPTH, map);
				assert(success);
			}
			std::cout << (success ? "success" : "failure") << std::endl;
			if (!success) {
				if (composed.size() < minSize) {
					minSize = composed.size();
					minState = rndState;
					minCode = composed;
				}
				return 1;
			}
			
			{
				do {
					size_t deleteLength = std::min(formatPRNG.nextUnsignedInt(8), formatPRNG.nextUnsignedInt(8));
					size_t insertLength = std::min(formatPRNG.nextUnsignedInt(4), formatPRNG.nextUnsignedInt(4));
					size_t offset = (deleteLength >= composed.size() ? 0 : formatPRNG.nextUnsignedInt(composed.size() - deleteLength));
					composed.erase(offset, deleteLength);
					for (int i = 0; i < insertLength; ++i) {
						composed.insert(offset + i, 1, static_cast<char>(formatPRNG.nextUnsignedInt(255)));
					}
				} while (formatPRNG.nextUnsignedInt(1) == 0);
				Numbstrict::Parser parser(composed);
				Numbstrict::Struct back;
				bool success = parser.tryToParse(back);
				if (success) {
					success = verifyRandomStruct(prngCopy2, MAX_DEPTH, map);
				}
				std::cout << (success ? "(success)" : "(failure)") << std::endl;
			}
		}
		std::cout << "MINININININI" << std::endl;
		std::cout << std::endl << std::endl << "-------------------------------------" << std::endl << std::endl;
		std::cout << minCode << std::endl;
		std::cout << std::endl << std::endl << "-------------------------------------" << std::endl << std::endl;
		std::cout << std::hex << "0x" << minState.v[0] << ", " << "0x" << minState.v[1] << ", " << "0x" << minState.v[2] << ", " << "0x" << minState.v[3] << std::endl;
		return 0;
	#if 0
		{
			rc::check("string -> escaped -> unescaped",
					[](const std::string& s) {
						std::string escaped = Numbstrict::escape(s);
						size_t offset;
						const std::string back = Numbstrict::unescape(escaped, &offset);
						// std::cout << s << "=" << escaped << "=" << back << std::endl;
			  			RC_ASSERT(back == s);
			  			RC_ASSERT(offset == escaped.size());
					});

			rc::check("random string -> unescaped",
					[](const std::string& s) {
						size_t offset;
						const std::string back = Numbstrict::unescape(s, &offset);
						RC_ASSERT(back.size() <= s.size());
			  			RC_ASSERT(offset <= s.size());
					});

			rc::check("random wstring -> unescaped",
					[](const std::string& s) {
						size_t offset;
						const std::wstring back = Numbstrict::unescapeWide(s, &offset);
						RC_ASSERT(back.size() <= s.size());
			  			RC_ASSERT(offset <= s.size());
					});

			rc::check("double -> string -> double",
					[](const double value) {
						std::string s = Numbstrict::doubleToString(value);
						size_t offset;
						//std::cout << s << std::endl;
						const double back = Numbstrict::stringToDouble(s, &offset);
			  			RC_ASSERT(back == value);
			  			RC_ASSERT(offset == s.size());
					});

			rc::check("float -> string -> float",
					[](const float value) {
						std::string s = Numbstrict::floatToString(value);
						size_t offset;
						//std::cout << s << std::endl;
						const float back = Numbstrict::stringToFloat(s, &offset);
			  			RC_ASSERT(back == value);
			  			RC_ASSERT(offset == s.size());
					});

			rc::check("double of int64 -> string -> double",
					[](const int64_t int64) {
						union {
							int64_t i;
							double f;
						} u;
						u.i = int64;
						double value = u.f;
						std::string s = Numbstrict::doubleToString(value);
						size_t offset;
						const double back = Numbstrict::stringToDouble(s, &offset);
						// std::cout << int64 << ", " << value << " = " << s << " = " << back << std::endl;
			  			RC_ASSERT((back != back && value != value) || (back == value));
			  			RC_ASSERT(offset == s.size());
					});

			rc::check("float of int32 -> string -> float",
					[](const int int32) {
						union {
							int32_t i;
							float f;
						} u;
						u.i = int32;
						float value = u.f;
						std::string s = Numbstrict::floatToString(value);
						size_t offset;
						// std::cout << int32 << ", " << value << " = " << s << std::endl;
						const float back = Numbstrict::stringToFloat(s, &offset);
			  			RC_ASSERT((back != back && value != value) || (back == value));
			  			RC_ASSERT(offset == s.size());
					});

			rc::check("random string -> float",
					[](const std::string& s) {
						size_t offset;
						const double back = Numbstrict::stringToFloat(s, &offset);
			  			RC_ASSERT(offset <= s.size());
					});

			rc::check("random string -> double",
					[](const std::string& s) {
						size_t offset;
						const double back = Numbstrict::stringToDouble(s, &offset);
			  			RC_ASSERT(offset <= s.size());
					});

			rc::check("wstring -> escaped -> unescaped",
					[](const std::wstring& s) {
						std::wstring clean = cleanupWString(s);
						std::string escaped = Numbstrict::escapeWide(clean);
						size_t offset;
						const std::wstring back = Numbstrict::unescapeWide(escaped, &offset);
						// std::cout << escaped << std::endl;
			  			RC_ASSERT(back == clean);
			  			RC_ASSERT(offset == escaped.size());
					});
		}
	#endif
	#endif

		if (argc < 2) {
			std::cerr << "Arguments: <input file>|-" << std::endl;
			return 1;
		}

		std::string contents;
		if (strcmp(argv[1], "-") == 0) {
			contents = loadEntireStream<char>(std::cin);
		} else {
			std::ifstream fileStream(argv[1]);
			if (!fileStream.good()) {
				std::cerr << "Could not open input file" << std::endl;
				return 1;
			}
			fileStream.exceptions(std::ios_base::badbit | std::ios_base::failbit);
			contents = loadEntireStream<char>(fileStream);
		}
		
/*		if (!numbstrictTest(contents)) {
			return 1;
		}*/
		Numbstrict::Struct structure = Numbstrict::parseStruct(contents, "somefile");
		for (Numbstrict::Struct::const_iterator it = structure.begin(); it != structure.end(); ++it) {
			deepParse(it->second);
		}
		std::cout << Numbstrict::compose(structure) << std::endl;
	}
	catch (const std::exception& x) {
		std::cout << "Exception: " << x.what() << std::endl;
		return 1;
	}
	catch (...) {
		std::cout << "General exception" << std::endl;
		return 1;
	}
	return 0;
}
#endif
#endif
