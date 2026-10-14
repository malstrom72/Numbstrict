/*
	Generates the power-of-five table in src/Numbstrict.cpp and writes it back in place:

		powersOfFive src/Numbstrict.cpp            rewrites the generated block when it differs
		powersOfFive src/Numbstrict.cpp --check    reports drift, changes nothing

	build.sh and build.cmd run --check, so a hand edited table fails the build.
*/

#include <assert.h>
#include <stdint.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

typedef uint32_t UInt32;
typedef uint64_t UInt64;

const int MIN_POWER = -343;
const int MAX_POWER = 343;
const int COUNT = MAX_POWER + 1 - MIN_POWER;
const int WRAP_COLUMN = 120;
const char* const BEGIN_MARKER = "generated: powers of five";
const char* const END_MARKER = "end generated: powers of five";

// The subset of Words in src/Numbstrict.cpp that building the table needs.
template<int N> class Words {
	public:
		explicit Words(UInt64 value = 0) {
			for (int i = 0; i < N; ++i) {
				words[i] = (i == 0 ? static_cast<UInt32>(value) : i == 1 ? static_cast<UInt32>(value >> 32) : 0);
			}
		}
		template<int M> explicit Words(const Words<M>& other) {
			assert(other.bitLength() <= N * 32);
			for (int i = 0; i < N; ++i) {
				words[i] = (i < M ? other.word(i) : 0);
			}
		}
		static Words powerOfTwo(int bit) {
			Words result;
			result.setBit(bit);
			return result;
		}
		UInt32 word(int i) const { return words[i]; }
		int bitLength() const {
			int i = N;
			while (i > 0 && words[i - 1] == 0) {
				--i;
			}
			int bits = i * 32;
			for (UInt32 top = (i > 0 ? words[i - 1] : 0x80000000u); (top & 0x80000000u) == 0; top <<= 1) {
				--bits;
			}
			return bits;
		}
		int compare(const Words& other) const {
			for (int i = N; i > 0; --i) {
				if (words[i - 1] != other.words[i - 1]) {
					return (words[i - 1] < other.words[i - 1] ? -1 : 1);
				}
			}
			return 0;
		}
		void setBit(int bit) { words[bit / 32] |= static_cast<UInt32>(1) << (bit % 32); }
		void multiplyAdd(UInt32 factor, UInt32 addend) {
			UInt64 carry = addend;
			for (int i = 0; i < N; ++i) {
				carry += static_cast<UInt64>(words[i]) * factor;
				words[i] = static_cast<UInt32>(carry);
				carry >>= 32;
			}
			assert(carry == 0);
		}
		void subtract(const Words& other) {
			UInt64 borrow = 0;
			for (int i = 0; i < N; ++i) {
				const UInt64 difference = static_cast<UInt64>(words[i]) - other.words[i] - borrow;
				words[i] = static_cast<UInt32>(difference);
				borrow = ((difference >> 32) != 0 ? 1 : 0);
			}
			assert(borrow == 0);
		}
		void shiftLeft(int bits) {
			assert(bitLength() + bits <= N * 32);
			const int wordShift = bits / 32;
			const int bitShift = bits % 32;
			for (int i = N; i > 0; --i) {
				const int source = i - 1 - wordShift;
				words[i - 1] = (source >= 0 ? words[source] << bitShift : 0)
						| (bitShift != 0 && source > 0 ? words[source - 1] >> (32 - bitShift) : 0);
			}
		}
		void shiftRight(int bits) {
			const int wordShift = bits / 32;
			const int bitShift = bits % 32;
			for (int i = 0; i < N; ++i) {
				const int source = i + wordShift;
				words[i] = (source < N ? words[source] >> bitShift : 0)
						| (bitShift != 0 && source + 1 < N ? words[source + 1] << (32 - bitShift) : 0);
			}
		}

	protected:
		UInt32 words[N];
};

struct Entry {
	Words<4> significand;
	int exponent;
};

static void buildTable(Entry* entries) {
	Words<32> power(1);
	for (int q = 0; q <= MAX_POWER; ++q) {
		const int length = power.bitLength();
		Words<32> top = power;
		top.shiftLeft(128);								// then down to exactly 128 bits, from either side
		top.shiftRight(length);
		entries[q - MIN_POWER].significand = Words<4>(top);
		entries[q - MIN_POWER].exponent = length - 128;
		power.multiplyAdd(5, 0);
	}
	power = Words<32>(1);
	for (int k = 1; k <= -MIN_POWER; ++k) {				// 1 / 5^k is floor(2^(length + 127) / 5^k)
		power.multiplyAdd(5, 0);
		const int length = power.bitLength();
		Words<32> remainder = Words<32>::powerOfTwo(length + 127);
		Words<32> divisor = power;
		divisor.shiftLeft(127);
		Words<4> quotient;
		for (int bit = 127; bit >= 0; --bit) {
			if (remainder.compare(divisor) >= 0) {
				remainder.subtract(divisor);
				quotient.setBit(bit);
			}
			divisor.shiftRight(1);
		}
		assert(quotient.bitLength() == 128);			// normalized by construction
		entries[-k - MIN_POWER].significand = quotient;
		entries[-k - MIN_POWER].exponent = -(length + 127);
	}
}

// Packs `pieces` as many to a line as fit the wrap column, with the tab counted as four columns.
static void emitArray(std::ostream& out, const char* declaration, const std::string* pieces, int count
		, const std::string& eol) {
	out << declaration << " = {" << eol;
	std::string line;
	for (int i = 0; i < count; ++i) {
		const std::string piece = pieces[i] + (i + 1 < count ? "," : "");
		if (!line.empty() && 4 + line.size() + piece.size() > static_cast<size_t>(WRAP_COLUMN)) {
			out << '\t' << line << eol;
			line.clear();
		}
		line += piece;
	}
	out << '\t' << line << eol << "};" << eol;
}

// The library computes the exponents rather than storing them, with the formula in powerOfFive(q) in Numbstrict.cpp.
static bool exponentsMatchFormula(const Entry* entries) {
	for (int q = MIN_POWER; q <= MAX_POWER; ++q) {
		const int k = (q < 0 ? -q : q);
		const int length = ((k * 1217359) >> 19) + 1;
		if (entries[q - MIN_POWER].exponent != (q < 0 ? -(length + 127) : length - 128)) {
			return false;
		}
	}
	return true;
}

static std::string generateBlock(const Entry* entries, const std::string& eol) {
	std::string words[COUNT * 4];
	for (int i = 0; i < COUNT; ++i) {
		for (int j = 0; j < 4; ++j) {
			std::ostringstream hex;
			hex << "0x" << std::hex << std::uppercase;
			hex.width(8);
			hex.fill('0');
			hex << entries[i].significand.word(j);
			words[i * 4 + j] = hex.str();
		}
	}
	std::ostringstream out;
	std::ostringstream declaration;
	declaration << "static const uint32_t POWER_OF_FIVE_WORDS[" << COUNT * 4 << "]";
	emitArray(out, declaration.str().c_str(), words, COUNT * 4, eol);
	return out.str();
}

int main(int argc, const char* argv[]) {
	if (argc < 2) {
		std::cerr << "Usage: powersOfFive <Numbstrict.cpp> [--check]" << std::endl;
		return 1;
	}
	const char* const path = argv[1];
	const bool checking = (argc > 2 && std::string(argv[2]) == "--check");
	std::string source;
	{
		std::ifstream in(path, std::ios::binary);
		std::ostringstream contents;
		contents << in.rdbuf();
		if (!in) {
			std::cerr << "Cannot read " << path << std::endl;
			return 1;
		}
		source = contents.str();
	}
	const size_t marker = source.find(BEGIN_MARKER);
	const size_t endMarker = source.find(END_MARKER);
	if (marker == std::string::npos || endMarker == std::string::npos || endMarker < marker) {
		std::cerr << "No \"" << BEGIN_MARKER << "\" block in " << path << std::endl;
		return 1;
	}
	const size_t begin = source.find('\n', marker) + 1;
	const size_t end = source.rfind('\n', endMarker) + 1;
	const std::string eol = (begin >= 2 && source[begin - 2] == '\r' ? "\r\n" : "\n");
	static Entry entries[COUNT];
	buildTable(entries);
	if (!exponentsMatchFormula(entries)) {
		std::cerr << "The exponent formula in powerOfFive(q) no longer matches the table." << std::endl;
		return 1;
	}
	const std::string block = generateBlock(entries, eol);
	if (source.compare(begin, end - begin, block) == 0) {
		std::cout << "Power-of-five table is up to date." << std::endl;
		return 0;
	}
	if (checking) {
		std::cerr << "Power-of-five table is stale in " << path << ". Run tools/powersOfFive.cpp without --check to "
				"regenerate it." << std::endl;
		return 1;
	}
	std::ofstream out(path, std::ios::binary);
	out << source.substr(0, begin) << block << source.substr(end);
	if (!out) {
		std::cerr << "Cannot write " << path << std::endl;
		return 1;
	}
	std::cout << "Wrote the power-of-five table to " << path << "." << std::endl;
	return 0;
}
