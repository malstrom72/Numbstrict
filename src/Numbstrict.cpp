#ifdef __GNUC__
#ifndef __clang__
	#pragma GCC push_options
	#pragma GCC optimize ("no-finite-math-only")
	#pragma GCC optimize ("float-store")
	#pragma GCC optimize ("no-fast-math")
#endif
#endif

#ifdef _MSC_VER
	#pragma float_control(precise, on)
	#pragma float_control(except, off)
	#pragma fenv_access(on)
	#pragma fp_contract(off)
#endif

#ifdef __FAST_MATH__
	#error This code requires IEEE-compliant floating point handling. Avoid -Ofast / -ffast-math etc (at least for this source file), or add -fno-fast-math.	
#endif

#include "assert.h"
#include <sstream>
#include <cmath>
#include <limits>
#include <algorithm>
#include <cstring>
#include <type_traits>
#include "Numbstrict.h"

namespace Numbstrict {

/*
	# diff from loose numbstruck:
	#
	# . ctrl-z eof is not recognized
	# . valid space characters are [ \t\r\n] (ascii codes 32, 7, 13 and 10)
	# . valid characters in comments are space characters and ascii codes 33 to 255
	# . valid characters for unquoted keys are [a-zA-Z0-9_] (first must not be a digit)
	# . valid characters in unquoted text are [ \t] and ascii codes 33 to 126 except [{}"':,=;]
	# . valid characters in quoted strings are [ ] and ascii codes 33 to 126
	# . recognized escape sequences are: '\x' '\u' '\U' '\n' '\r' '\t' '\'' '\"' '\\'
	# . '\l' escape sequence (for 32-bit characters) has been replaced with '\U'
	# . '\x' '\u' and '\U' must have exact number of hex digits (2, 4 and 8)
	# . no escape sequence for decimal character codes
	# . [=] is not accepted as key/value separator, only [:] (and it is required)
	# . [:] after key must be on the same line (also for quoted keys)
	# . [,] or [\n] (or both) is required between each element
	# . [;] is not accepted as element separator, only [,] or [\n]
	# . trailing [,] in key/value lists is illegal (but ok in value only lists)
	# . '{ : }' may be used to declare an empty struct (to differentiate from an empty array)

	root 				<-	_lf (valueList_ / keyValueList_) !.
	valueList_          <-	(value _ next_ / ',' _lf)* (value _lf)?
	keyValueList_       <-  (':' / keyValue_ (next_ keyValue_)*) _lf
	next_ 				<-	([\r\n] (_lf ',')? / _lf ',') _lf
	keyValue_ 			<-	key (_ ':' _ value?) _
	key                 <-  quotedString / identifier
	value       		<-  array / struct / quotedString / real / integer / boolean / text
	array				<-	'{' _lf valueList_ '}'
	struct				<-	'{' _lf keyValueList_ '}'
	text 				<-	(_ (![{}"':,=;] !('/' '*') !('/' '/') [\41-\176])+)+
	identifier			<-	[a-zA-Z_] [a-zA-Z0-9_]*
	quotedString        <-  doubleQuotedString / singleQuotedString
	doubleQuotedString  <-  '"' (escapeCode / !["\\\r\n] [\40-\176])* '"'
	singleQuotedString	<-	"'" (escapeCode / !['\\\r\n] [\40-\176])* "'"
	real				<-	([-+]? decimal ('.' [0-9]+)? ([eE] [-+]? decimal)?) / [-+]? 'inf' / 'nan'
	integer				<-	[-+]? ('0x' hex+ / decimal)
	decimal 			<-	'0' / [1-9] [0-9]*
	boolean 			<-	'false' / 'true'
	escapeCode          <-  '\\x' hex2 / '\\u' hex4 / '\\U' hex8 / '\\' [nrt'"\\]
	hex8                <-  hex4 hex4
	hex4                <-  hex2 hex2
	hex2                <-  hex hex
	hex                 <-  [0-9A-Fa-f]
	_lf                 <-  (comment / [ \t\r\n])*
	_              		<-  (comment / [ \t])*
	comment             <-  singleLineComment / multiLineComment
	singleLineComment   <-  '/' '/' [\40-\377\t]* (!. / &[\r\n])
	multiLineComment    <-  '/' '*' (multiLineComment / !'*' '/' [\40-\377\t\r\n])* '*' '/'
*/

template<typename T> bool isNaN(const T v) { return v != v; }

/**
	Rewraps values to/from signed and unsigned with well-defined behavior. Zero-extends signed to unsigned of larger
	types.
**/
template<typename T, typename F> T rewrap(F i) {
	typedef typename std::make_unsigned<F>::type UF;
	typedef typename std::make_unsigned<T>::type UT;
	const UT ui = static_cast<UT>(static_cast<UF>(i));
	if (std::is_signed<T>()) {
		// Cast from unsigned to signed is undefined in C. Optimizers might not wrap values as expected.
		const UT HALF_MAX = static_cast<UT>(1) << (sizeof (UT) * 8 - 1);
		const T QUARTER_MAX = static_cast<T>(HALF_MAX >> 1);
		return (ui < HALF_MAX ? static_cast<T>(ui) : static_cast<T>(ui - HALF_MAX) - QUARTER_MAX - QUARTER_MAX);
	} else {
		// T is unsigned type. No problem.
		return ui;
	}
}

template<typename I> I skipWhite(I p, const I e) {
	while (p != e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
		++p;
	}
	return p;
}

static int fromHex(Char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	} else if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	} else if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	} else {
		return -1;
	}
}

template<typename C> bool genericUnquoteString(StringIt& p, const StringIt e, std::basic_string<C>& string) {
	const Char quoteChar = *p;
	assert(quoteChar == '\"' || quoteChar == '\'');
	++p;
	StringIt b = p;
	while (p != e && *p != quoteChar) {
		if (*p < 0) {	// assume iso8859-1
			string.insert(string.end(), b, p);
			string.push_back(static_cast<unsigned char>(*p));
			++p;
			b = p;
		} else if (*p == '\\') {
			string.insert(string.end(), b, p);
			++p;
			if (p == e) {
				return false;
			}
			switch (*p) {
				case '\\': case '\'': case '\"': {
					string += *p;
					++p;
					break;
				}

				case 'n': case 'r': case 't': {
					C c;
					switch (*p) {
						case 'n': c = '\n'; break;
						case 'r': c = '\r'; break;
						case 't': c = '\t'; break;
						default: assert(0);
					}
					string += c;
					++p;
					break;
				}

				case 'x': case 'u': case 'U': {
					int n;
					switch (*p) {
						case 'x': n = 2; break;
						case 'u': n = 4; break;
						case 'U': n = 8; break;
						default: assert(0);
					}
					++p;
					const StringIt b = p;
					uint32_t ui = 0;
					for (int i = 0; i < n; ++i) {
						if (p == e) {
							return false;
						}
						const int v = fromHex(*p);
						if (v < 0) {
							return false;
						}
						++p;
						ui = (ui << 4) | v;
					}
					if (sizeof (C) == 1) {		// single byte output = iso-8859-1 (not utf!)
						if (ui >= 0x100) {
							p = b;
							return false;
						}
						string += rewrap<C>(ui);
					} else {
						if (ui >= 0x110000 || (ui >= 0xD800 && ui < 0xE000) || ui == 0xFFFE || ui == 0xFFFF) {
							p = b;
							return false;
						}
						if (sizeof (C) == 2) {	// double byte output = UTF16
							if (ui >= 0x10000) {
								const uint32_t x = ui - 0x10000;
								string += rewrap<C>(0xD800 | (x >> 10));
								string += rewrap<C>(0xDC00 | (x & ((1 << 10) - 1)));
							} else {
								string += rewrap<C>(ui);
							}
						} else {				// four byte output (or anything else) = UTF32
							string += rewrap<C>(ui);
						}
					}
					break;
				}

				default: return false;
			}
			b = p;
		} else if (static_cast<UChar>(*p) < 32 || static_cast<UChar>(*p) >= 127) {
			return false;
		} else {
			++p;
		}
	}
	string.insert(string.end(), b, p);
	if (p == e) {
		return false;
	}
	++p;
	return true;
}

template<class T> Char* intToString(Char* buffer, T i, int radix = 10, int minLength = 1) {
	assert(2 <= radix && radix <= 16);
	assert(0 <= minLength && minLength <= static_cast<int>(sizeof (T) * 8));
	Char* p = buffer + sizeof (T) * 8 + 1;
	Char* e = p - minLength;
	for (T x = i; p > e || x != 0; x /= radix) {
		assert(p >= buffer + 2);
		--p;
		*p = ("fedcba9876543210123456789abcdef")[15 + x % radix];	// Mirrored hex string to handle negative x.
	}
	if (std::numeric_limits<T>::is_signed && i < 0) {
		--p;
		*p = '-';
	}
	return p;
}

template<class T> String intToString(T i, int radix = 10, int minLength = 1) {
	assert(2 <= radix && radix <= 16);
	assert(0 <= minLength && minLength <= static_cast<int>(sizeof (T) * 8));
	Char buffer[sizeof (T) * 8 + 1];
	Char* p = intToString<T>(buffer, i, radix, minLength);
	return String(p, buffer + sizeof (T) * 8 + 1);
}

template<typename C> bool isTextChar(C c) {
	switch (c) {
		case ' ': case '\t': case ',': case '{': case '}': case '\"':
		case '\'': case '=': case ';': case ':': case '\r': case '\n': return false;
		default: return (c >= 32 && c < 127);
	}
}

template<typename C> bool areAllTextChars(const std::basic_string<C>& s) {
	for (typename std::basic_string<C>::const_iterator it = s.begin(); it != s.end(); ++it) {
		if (!isTextChar(*it)) {
			return false;
		}
		if (s.end() - it >= 2 && it[0] == '/' && (it[1] == '/' || it[1] == '*')) {
			return false;
		}
	}
	return true;
}

// FIX : make it take a range instead, e.g. for String compose(const Char* fromString, bool preferUnquoted = false);
template<typename C> String quoteString(const std::basic_string<C>& fromString, bool preferUnquoted, Char quoteChar) {
	assert(quoteChar == '\"' || quoteChar == '\'');
	if (preferUnquoted && areAllTextChars(fromString)) {
		return String(fromString.begin(), fromString.end());
	} else {
		String quoted(1, quoteChar);
		typename std::basic_string<C>::const_iterator p = fromString.begin();
		typename std::basic_string<C>::const_iterator e = fromString.end();
		typename std::basic_string<C>::const_iterator b = p;
		while (p != e) {
			uint32_t ui = rewrap<uint32_t>(*p);
			if (ui >= 32 && ui < 127 && *p != quoteChar && *p != '\\') {
				++p;
			} else {
				quoted.insert(quoted.end(), b, p);
				switch (*p) {
					case '\\': quoted += "\\\\"; break;
					case '\n': quoted += "\\n"; break;
					case '\r': quoted += "\\r"; break;
					case '\t': quoted += "\\t"; break;
					default: {
						if (*p == quoteChar) {
							quoted += '\\';
							quoted += quoteChar;
							break;
						}
						if (sizeof (C) == 2 && ui >= 0xD800 && ui < 0xDC00) {		// double byte input = UTF16
							if (p + 1 != e) {
								const uint32_t ui2 = rewrap<uint32_t>(p[1]);
								if (ui2 >= 0xDC00 && ui2 < 0xE000) {
									ui = 0x10000 + (((ui & ((1 << 10) - 1)) << 10) | (ui2 & ((1 << 10) - 1)));
									++p;
								}
							}
						}
						const Char* e = "\\U";
						int n = 8;
						if (ui < 0x100) {
							e = "\\x";
							n = 2;
						} else if (ui < 0x10000) {
							e = "\\u";
							n = 4;
						}
						quoted += e;
						quoted += intToString(ui, 16, n);
						break;
					}
				}
				++p;
				b = p;
			}
		}
		quoted.insert(quoted.end(), b, p);
		quoted += quoteChar;
		return quoted;
	}
}

// Parses the unsigned digit run of a decimal exponent. Clamps accumulation well above the real
// exponent range (~400) but far below any wrap, so pathological exponents (e.g. "1e3000000000")
// saturate instead of wrapping mod 2^32. The caller applies the sign, so a saturated magnitude still
// underflows to 0 / overflows to Inf correctly.
static const Char* parseExponentDigits(const Char* p, const Char* e, unsigned int& i) {
	for (i = 0; p != e && *p >= '0' && *p <= '9'; ++p) {
		if (i < 1000000u) {
			i = i * 10 + (*p - '0');
		}
	}
	return p;
}

const int NEGATIVE_E_NOTATION_START = -6;
const int POSITIVE_E_NOTATION_START = 10;

/*
	Real number conversions. Both directions are integer arithmetic on `Words<N>`; the only approximation is a table
	of powers of five truncated to 128 bits, whose error is one-sided. A rounding decision is made on an interval of
	relative width below 2^-127 that contains the exact value, and number theory says what such an interval can hold:
	a decimal of at most 20 significant digits is never within 2^-125 of a binary64 rounding midpoint unless it lies
	exactly on it (binary32: 2^-94), and a decimal of at most 18 digits is never within 2^-124 of a double unless it
	equals it. Nothing depends on the floating-point environment: values are taken apart, compared and assembled on
	their bits.
*/

/*
	Unsigned integer of N 32-bit words, least significant first, with only the operations the conversions need. The
	conversions use 2, 3, 4 and 8 words; table construction 32; the exact comparison of long inputs 128.
*/
template<int N> class Words {
	public:
		Words() { memset(words, 0, sizeof words); }
		explicit Words(uint64_t value) {																				// requires N >= 2
			memset(words, 0, sizeof words);
			words[0] = static_cast<uint32_t>(value);
			words[1] = static_cast<uint32_t>(value >> 32);
		}
		template<int M> explicit Words(const Words<M>& other) {															// requires the value to fit
			memset(words, 0, sizeof words);
			for (int i = 0; i < M; ++i) {
				assert((i < N || other.word(i) == 0) && "value must fit in N words");
				if (i < N) {
					words[i] = other.word(i);
				}
			}
		}
		static Words powerOfTwo(int bit) {
			Words result;
			result.words[bit / 32] = static_cast<uint32_t>(1) << (bit % 32);
			return result;
		}
		uint32_t word(int i) const { return words[i]; }
		uint64_t low64() const { return words[0] | (static_cast<uint64_t>(words[1]) << 32); }
		int bitLength() const {
			for (int i = N; i > 0; --i) {
				if (words[i - 1] != 0) {
					int bits = (i - 1) * 32;
					for (uint32_t top = words[i - 1]; top != 0; top >>= 1) {
						++bits;
					}
					return bits;
				}
			}
			return 0;
		}
		int compare(const Words& other) const {
			for (int i = N; i > 0; --i) {
				if (words[i - 1] != other.words[i - 1]) {
					return (words[i - 1] < other.words[i - 1] ? -1 : 1);
				}
			}
			return 0;
		}
		void multiplyAdd(uint32_t factor, uint32_t addend) {
			uint64_t carry = addend;
			for (int i = 0; i < N; ++i) {
				carry += static_cast<uint64_t>(words[i]) * factor;
				words[i] = static_cast<uint32_t>(carry);
				carry >>= 32;
			}
			assert(carry == 0 && "product must fit in N words");
		}
		void multiplyByPowerOfTen(int power) {
			for (; power >= 9; power -= 9) {
				multiplyAdd(1000000000u, 0);
			}
			for (; power > 0; --power) {
				multiplyAdd(10, 0);
			}
		}
		void add(const Words& other) {
			uint64_t carry = 0;
			for (int i = 0; i < N; ++i) {
				carry += static_cast<uint64_t>(words[i]) + other.words[i];
				words[i] = static_cast<uint32_t>(carry);
				carry >>= 32;
			}
			assert(carry == 0 && "sum must fit in N words");
		}
		void subtract(const Words& other) {																				// requires *this >= other
			int64_t borrow = 0;
			for (int i = 0; i < N; ++i) {
				const int64_t difference = static_cast<int64_t>(words[i]) - other.words[i] - borrow;
				borrow = (difference < 0 ? 1 : 0);
				words[i] = static_cast<uint32_t>(difference + (borrow << 32));
			}
			assert(borrow == 0 && "subtraction must not go below zero");
		}
		void shiftLeft(int bits) {																						// requires the result to fit
			assert(bitLength() + bits <= N * 32 && "shifted value must fit in N words");
			const int wordShift = bits / 32;
			const int bitShift = bits % 32;
			for (int i = N; i > 0; --i) {
				const int source = i - 1 - wordShift;
				uint32_t value = (source >= 0 ? words[source] << bitShift : 0);
				if (bitShift != 0 && source > 0) {
					value |= words[source - 1] >> (32 - bitShift);
				}
				words[i - 1] = value;
			}
		}
		void shiftRight(int bits) {
			const int wordShift = bits / 32;
			const int bitShift = bits % 32;
			for (int i = 0; i < N; ++i) {
				const int source = i + wordShift;
				uint32_t value = (source < N ? words[source] >> bitShift : 0);
				if (bitShift != 0 && source + 1 < N) {
					value |= words[source + 1] << (32 - bitShift);
				}
				words[i] = value;
			}
		}
		void keepLowBits(int bits) {
			for (int i = 0; i < N; ++i) {
				if (i * 32 >= bits) {
					words[i] = 0;
				} else if (i * 32 + 32 > bits) {
					words[i] &= (static_cast<uint32_t>(1) << (bits - i * 32)) - 1;
				}
			}
		}
		void setBit(int bit) { words[bit / 32] |= static_cast<uint32_t>(1) << (bit % 32); }
		template<int A, int B> void setProduct(const Words<A>& a, const Words<B>& b) {									// requires N >= A + B
			memset(words, 0, sizeof words);
			for (int i = 0; i < A; ++i) {
				uint64_t carry = 0;
				for (int j = 0; j < B; ++j) {
					carry += static_cast<uint64_t>(a.word(i)) * b.word(j) + words[i + j];
					words[i + j] = static_cast<uint32_t>(carry);
					carry >>= 32;
				}
				words[i + B] = static_cast<uint32_t>(carry);
			}
		}

	private:
		uint32_t words[N];
};

/*
	Powers of five truncated to 128 bits: entry q holds P with 5^q = (P + f) * 2^e, 0 <= f < 1 and P in [2^127, 2^128);
	negative q hold 1 / 5^-q in the same form. P is truncated, never rounded, so the true value is never below P; the
	rounding decisions rely on this one-sided error. Entries up to 5^55 are exact. Parsing needs q in -343..308, the
	formatter up to 343.
*/
class PowerOfFiveTable {
	public:
		enum { MIN_POWER = -343, MAX_POWER = 343, COUNT = MAX_POWER + 1 - MIN_POWER };
		PowerOfFiveTable() {
			Words<32> power(1);
			for (int q = 0; q <= MAX_POWER; ++q) {
				const int length = power.bitLength();
				Words<32> top = power;
				if (length > 128) {
					top.shiftRight(length - 128);
				} else {
					top.shiftLeft(128 - length);
				}
				significands[q - MIN_POWER] = Words<4>(top);
				exponents[q - MIN_POWER] = length - 128;
				power.multiplyAdd(5, 0);
			}
			power = Words<32>(1);
			for (int k = 1; k <= -MIN_POWER; ++k) {
				power.multiplyAdd(5, 0);
				const int length = power.bitLength();
				Words<32> remainder = Words<32>::powerOfTwo(length + 127);												// 2^(length + 127) / 5^k lies in [2^127, 2^128)
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
				assert(quotient.bitLength() == 128 && "the reciprocal is normalized by construction");
				significands[-k - MIN_POWER] = quotient;
				exponents[-k - MIN_POWER] = -(length + 127);
			}
		}
		const Words<4>& significand(int power) const { return significands[power - MIN_POWER]; }
		int exponent(int power) const { return exponents[power - MIN_POWER]; }

	private:
		Words<4> significands[COUNT];
		int exponents[COUNT];
};

static const PowerOfFiveTable POWERS_OF_FIVE;

template<typename T> struct Traits { };

template<> struct Traits<double> {
	typedef uint64_t Bits;
	enum { MANTISSA_BITS = 53, MIN_EXPONENT = -1074, MAX_EXPONENT = 1023 };												// of the smallest subnormal bit; of the leading bit of the largest finite value
	enum { MIN_DECIMAL_EXPONENT = -324, MAX_DECIMAL_EXPONENT = 308 };													// of the leading digit: below everything rounds to zero, above to infinity
	enum { MAX_DIGITS = 17 };																							// significant digits that always round-trip
	static const uint64_t SIGN_BIT = static_cast<uint64_t>(1) << 63;
	static const uint64_t EXPONENT_MASK = static_cast<uint64_t>(0x7FF) << 52;
	static double fromBits(uint64_t bits) { double value; memcpy(&value, &bits, sizeof value); return value; }
	static uint64_t toBits(double value) { uint64_t bits; memcpy(&bits, &value, sizeof bits); return bits; }
};

template<> struct Traits<float> {
	typedef uint32_t Bits;
	enum { MANTISSA_BITS = 24, MIN_EXPONENT = -149, MAX_EXPONENT = 127 };
	enum { MIN_DECIMAL_EXPONENT = -46, MAX_DECIMAL_EXPONENT = 38 };
	enum { MAX_DIGITS = 9 };
	static const uint32_t SIGN_BIT = static_cast<uint32_t>(1) << 31;
	static const uint32_t EXPONENT_MASK = static_cast<uint32_t>(0xFF) << 23;
	static float fromBits(uint32_t bits) { float value; memcpy(&value, &bits, sizeof value); return value; }
	static uint32_t toBits(float value) { uint32_t bits; memcpy(&bits, &value, sizeof bits); return bits; }
};

static const int MAX_SIGNIFICANT_DIGITS = 20;																			// decided by the 128-bit product; longer inputs need the exact comparison only when their two candidates disagree
static const int MAX_EXACT_DIGITS = 800;																				// no rounding midpoint has more than 767 significant digits, so later digits only matter as "nonzero"

/*
	Rounds significand * 10^power to the nearest T, ties to even, for 0 < significand < 10^20 and power within the
	table. With x = significand * P the exact scaled value lies in [x, x + significand), so the result follows from
	where that interval sits relative to the rounding midpoint: entirely below, entirely above, or containing it,
	which by the gap theorem happens only for an exact tie.
*/
template<typename T> static T convertDecimal(const Words<3>& significand, int power) {
	typedef Traits<T> Limits;
	Words<8> x;
	x.setProduct(significand, POWERS_OF_FIVE.significand(power));
	const int length = x.bitLength();
	const int scale = power + POWERS_OF_FIVE.exponent(power);															// value = (x + delta) * 2^scale
	int position = std::max(length - static_cast<int>(Limits::MANTISSA_BITS)
			, static_cast<int>(Limits::MIN_EXPONENT) - scale);															// bit of x that becomes the result's lsb
	Words<8> mantissa = x;
	mantissa.shiftRight(position);
	Words<8> rest = x;
	rest.keepLowBits(position);
	Words<8> restPlusDelta = rest;
	restPlusDelta.add(Words<8>(significand));
	const Words<8> half = Words<8>::powerOfTwo(position - 1);
	bool roundUp;
	if (restPlusDelta.compare(half) <= 0) {
		roundUp = false;
	} else if (rest.compare(half) > 0) {
		roundUp = true;
	} else {
		roundUp = ((mantissa.low64() & 1) != 0);																		// the interval holds the midpoint: an exact tie
	}
	uint64_t bits = mantissa.low64() + (roundUp ? 1 : 0);
	if (bits == (static_cast<uint64_t>(1) << Limits::MANTISSA_BITS)) {
		bits >>= 1;
		++position;
	}
	const int lsbExponent = position + scale;
	if (bits < (static_cast<uint64_t>(1) << (Limits::MANTISSA_BITS - 1))) {
		assert(lsbExponent == Limits::MIN_EXPONENT && "a short mantissa is a subnormal or zero");
		return Limits::fromBits(static_cast<typename Limits::Bits>(bits));
	}
	const int exponent = lsbExponent + Limits::MANTISSA_BITS - 1;
	if (exponent > Limits::MAX_EXPONENT) {
		return std::numeric_limits<T>::infinity();
	}
	const typename Limits::Bits field
			= static_cast<typename Limits::Bits>(exponent - Limits::MIN_EXPONENT - Limits::MANTISSA_BITS + 2);
	return Limits::fromBits((field << (Limits::MANTISSA_BITS - 1))
			| static_cast<typename Limits::Bits>(bits - (static_cast<uint64_t>(1) << (Limits::MANTISSA_BITS - 1))));
}

/*
	Exact comparison of the decimal 0.d0d1d2... * 10^(leadingExponent + 1), given by all its significant digits
	(decimal points skipped), against the rounding midpoint just above `lower`: -1 below, 0 on it, 1 above. Only for
	inputs of more than MAX_SIGNIFICANT_DIGITS whose two candidates round differently, so speed is irrelevant.
*/
template<typename T> static int compareWithUpperMidpoint(const Char* digits, const Char* end, int leadingExponent
		, T lower) {
	typedef Traits<T> Limits;
	Words<128> left;
	int count = 0;
	bool tailNonZero = false;
	for (const Char* p = digits; p != end; ++p) {
		if (*p != '.') {
			if (count < MAX_EXACT_DIGITS) {
				left.multiplyAdd(10, *p - '0');
				++count;
			} else if (*p != '0') {
				tailNonZero = true;
			}
		}
	}
	const int power = leadingExponent + 1 - count;																		// value = left * 10^power (+ nonzero tail)
	const typename Limits::Bits bits = Limits::toBits(lower);
	const int field = static_cast<int>(bits >> (Limits::MANTISSA_BITS - 1));
	uint64_t mantissa = bits & ((static_cast<typename Limits::Bits>(1) << (Limits::MANTISSA_BITS - 1)) - 1);
	int exponent2 = Limits::MIN_EXPONENT - 1;																			// midpoint = (2 * mantissa + 1) * 2^exponent2
	if (field != 0) {
		mantissa |= static_cast<uint64_t>(1) << (Limits::MANTISSA_BITS - 1);
		exponent2 += field - 1;
	}
	Words<128> right(2 * mantissa + 1);
	if (power >= 0) {
		left.multiplyByPowerOfTen(power);
	} else {
		right.multiplyByPowerOfTen(-power);
	}
	if (exponent2 >= 0) {
		right.shiftLeft(exponent2);
	} else {
		left.shiftLeft(-exponent2);
	}
	const int comparison = left.compare(right);
	return (comparison == 0 && tailNonZero ? 1 : comparison);
}

template<typename T> const Char* parseReal(const Char* const b, const Char* const e, T& value) {
	typedef Traits<T> Limits;
	int exponent = -1;
	bool negative = false;
	const Char* significandBegin = b;
	const Char* numberEnd;
	const Char* p = b;
	if (p != e && (*p == '-' || *p == '+')) {
		negative = (*p == '-');
		++p;
		significandBegin = p;
	}
	if (e - p >= 3 && strncmp(p, "inf", 3) == 0) {
		value = std::numeric_limits<T>::infinity();
		numberEnd = p + 3;
	} else if (e - p >= 3 && strncmp(p, "nan", 3) == 0) {
		value = std::numeric_limits<T>::quiet_NaN();
		numberEnd = p + 3;
	} else {
		while (p != e && *p >= '0' && *p <= '9') {
			++exponent;
			++p;
		}
		if (p != e && *p == '.') {
			if (p == significandBegin) {
				++significandBegin;
			}
			++p;
			while (p != e && *p >= '0' && *p <= '9') {
				++p;
			}
		}
		if (p == significandBegin) {
			value = static_cast<T>(0.0);
			return b;
		}
		const Char* significandEnd = p;
		numberEnd = p;
		if (e - p >= 2 && (*p == 'e' || *p == 'E')) {
			++p;
			const int exponentSign = (*p == '-' ? -1 : 1);
			if (*p == '+' || *p == '-') {
				++p;
			}
			unsigned int ui;
			const Char* q = parseExponentDigits(p, e, ui);
			if (q != p) {
				exponent += exponentSign * static_cast<int>(ui);
				numberEnd = q;
			}
		}
		p = significandBegin;
		while (p != significandEnd && (*p == '0' || *p == '.')) {
			if (*p == '0') {
				--exponent;
			}
			++p;
		}
		if (p == significandEnd || exponent < Limits::MIN_DECIMAL_EXPONENT) {
			value = static_cast<T>(0.0);
		} else if (exponent > Limits::MAX_DECIMAL_EXPONENT) {
			value = std::numeric_limits<T>::infinity();
		} else {
			const Char* const firstDigit = p;
			Words<3> significand;
			int digitCount = 0;
			bool tailNonZero = false;
			while (p != significandEnd) {
				if (*p != '.') {
					if (digitCount < MAX_SIGNIFICANT_DIGITS) {
						significand.multiplyAdd(10, *p - '0');
						++digitCount;
					} else if (*p != '0') {
						tailNonZero = true;
					}
				}
				++p;
			}
			const int power = exponent - (digitCount - 1);
			value = convertDecimal<T>(significand, power);
			if (tailNonZero) {																							// the value lies strictly between the two 20-digit candidates; if they round apart, decide exactly
				Words<3> upperSignificand = significand;
				upperSignificand.multiplyAdd(1, 1);
				const T upper = convertDecimal<T>(upperSignificand, power);
				if (Limits::toBits(upper) != Limits::toBits(value)) {
					const int comparison = compareWithUpperMidpoint<T>(firstDigit, significandEnd, exponent, value);
					if (comparison > 0 || (comparison == 0 && (Limits::toBits(value) & 1) != 0)) {
						value = upper;
					}
				}
			}
		}
	}
	if (negative) {																										// on the bits: a multiplication would flush a subnormal to zero under FTZ
		value = Limits::fromBits(Limits::toBits(value) | Limits::SIGN_BIT);
	}
	return numberEnd;
}

/*
	floor(mantissa * 2^exponent2 * 10^power), which the caller keeps below 10^18, and in `halfComparison` where the
	remainder lies relative to one half: -1 below, 0 exactly on it, 1 above. The value gap theorem makes the interval
	[x, x + mantissa) decide this exactly or mark an exact boundary.
*/
static uint64_t scaledFloor(uint64_t mantissa, int exponent2, int power, int& halfComparison) {
	Words<8> x;
	x.setProduct(Words<2>(mantissa), POWERS_OF_FIVE.significand(power));
	const int shift = -(exponent2 + POWERS_OF_FIVE.exponent(power) + power);											// the product is (x + delta) * 2^-shift
	assert(shift > 0 && shift < 256 && "the scaled value is below 10^18 and the product above 2^127");
	Words<8> integerPart = x;
	integerPart.shiftRight(shift);
	Words<8> rest = x;
	rest.keepLowBits(shift);
	Words<8> restPlusDelta = rest;
	restPlusDelta.add(Words<8>(mantissa));
	uint64_t result = integerPart.low64();
	if (restPlusDelta.compare(Words<8>::powerOfTwo(shift)) > 0) {														// the interval holds the next integer: the value is exactly it
		++result;
		halfComparison = -1;
	} else {
		const Words<8> half = Words<8>::powerOfTwo(shift - 1);
		halfComparison = (restPlusDelta.compare(half) <= 0 ? -1 : rest.compare(half) > 0 ? 1 : 0);
	}
	return result;
}

template<typename T> static void decompose(T value, uint64_t& mantissa, int& exponent2) {								// value = mantissa * 2^exponent2
	typedef Traits<T> Limits;
	const typename Limits::Bits bits = Limits::toBits(value);
	const int field = static_cast<int>(bits >> (Limits::MANTISSA_BITS - 1));
	mantissa = bits & ((static_cast<typename Limits::Bits>(1) << (Limits::MANTISSA_BITS - 1)) - 1);
	exponent2 = Limits::MIN_EXPONENT;
	if (field != 0) {
		mantissa |= static_cast<typename Limits::Bits>(1) << (Limits::MANTISSA_BITS - 1);
		exponent2 += field - 1;
	}
}

/*
	The shortest decimal (digits as an integer, their count and the leading digit's exponent) that parses back to the
	positive finite `value`. For n digits the candidates are the truncation F = floor(value * 10^(n-1-k)) and F + 1;
	the smallest n at which one of them converts back to `value` wins, and when both do, the closer one (the lower on
	an exact half). The largest finite value never takes the upper candidate, so that its text stays below the
	overflow threshold.
*/
template<typename T> static uint64_t shortestDigits(T value, int& digitCount, int& exponent10) {
	typedef Traits<T> Limits;
	uint64_t mantissa;
	int exponent2;
	decompose(value, mantissa, exponent2);
	const int binaryExponent = exponent2 + Words<2>(mantissa).bitLength() - 1;											// floor(log2(value))
	const int scaled = binaryExponent * 1233;																			// 1233 / 4096 approximates log10(2); off by at most one
	int k = (scaled >= 0 ? scaled : scaled - 4095) / 4096;
	int half;
	uint64_t first = scaledFloor(mantissa, exponent2, -k, half);
	while (first >= 10) {
		++k;
		first = scaledFloor(mantissa, exponent2, -k, half);
	}
	while (first == 0) {
		--k;
		first = scaledFloor(mantissa, exponent2, -k, half);
	}
	const typename Limits::Bits valueBits = Limits::toBits(value);
	int low = 1;																										// binary search: if n digits suffice, so do n + 1
	int high = Limits::MAX_DIGITS;
	uint64_t chosen = 0;
	int chosenCount = 0;
	int chosenExponent = 0;
	while (low <= high) {
		const int n = (low + high) / 2;
		const uint64_t truncated = scaledFloor(mantissa, exponent2, n - 1 - k, half);
		uint64_t pow10 = 1;
		for (int i = 0; i < n; ++i) {
			pow10 *= 10;
		}
		const bool carry = (truncated + 1 == pow10);																	// F + 1 is 10^n: one digit at the next exponent
		const Words<3> upperDigits(carry ? 1 : truncated + 1);
		const int upperPower = (carry ? k + 1 : k - n + 1);
		const bool lowerFits = (Limits::toBits(convertDecimal<T>(Words<3>(truncated), k - n + 1)) == valueBits);
		const bool upperFits = (Limits::toBits(convertDecimal<T>(upperDigits, upperPower)) == valueBits);
		if (lowerFits || upperFits) {
			const bool useUpper = (!lowerFits || (half > 0 && upperFits
					&& valueBits != Limits::toBits(std::numeric_limits<T>::max())));
			chosen = (useUpper ? (carry ? 1 : truncated + 1) : truncated);
			chosenCount = (useUpper && carry ? 1 : n);
			chosenExponent = (useUpper && carry ? k + 1 : k);
			high = n - 1;
		} else {
			low = n + 1;
		}
	}
	assert(chosenCount != 0 && "MAX_DIGITS digits always round-trip");
	digitCount = chosenCount;
	exponent10 = chosenExponent;
	return chosen;
}

template<typename T> Char* realToString(Char buffer[32], T value) {
	typedef Traits<T> Limits;
	Char* p = buffer;
	const typename Limits::Bits bits = Limits::toBits(value);
	const typename Limits::Bits magnitudeBits = bits & ~Limits::SIGN_BIT;
	if ((bits & Limits::SIGN_BIT) != 0) {
		*p++ = '-';
	}
	if ((magnitudeBits & Limits::EXPONENT_MASK) == Limits::EXPONENT_MASK) {
		strcpy(p, magnitudeBits == Limits::EXPONENT_MASK ? "inf" : "nan");
		return p + 3;
	} else if (magnitudeBits == 0) {
		strcpy(p, "0.0");
		return p + 3;
	}
	int digitCount;
	int exponent;
	uint64_t digits = shortestDigits(Limits::fromBits(magnitudeBits), digitCount, exponent);
	Char digitChars[24];
	for (int i = digitCount; i > 0; --i) {
		digitChars[i - 1] = static_cast<Char>('0' + digits % 10);
		digits /= 10;
	}
	const bool eNotation = (exponent < NEGATIVE_E_NOTATION_START || exponent >= POSITIVE_E_NOTATION_START);
	Char* const periodPosition = p + (eNotation || exponent < 0 ? 0 : exponent) + 1;
	if (!eNotation && exponent < 0) {
		*p++ = '0';
		*p++ = '.';
		while (p < periodPosition - exponent) {
			*p++ = '0';
		}
	}
	for (int i = 0; i < digitCount; ++i) {
		if (p == periodPosition) {
			*p++ = '.';
		}
		*p++ = digitChars[i];
	}
	while (p < periodPosition) {
		*p++ = '0';
	}
	if (p == periodPosition) {
		*p++ = '.';
		*p++ = '0';
	}
	if (eNotation) {
		*p++ = 'e';
		*p++ = (exponent < 0 ? '-' : '+');
		int x = (exponent < 0 ? -exponent : exponent);
		Char* q = buffer + 32;
		do {
			*--q = static_cast<Char>('0' + x % 10);
			x /= 10;
		} while (x != 0);
		assert(q >= p);
		p = std::copy(q, buffer + 32, p);
	}
	assert(p <= buffer + 32);
	return p;
}

const char* ParsingError::what() const throw() {
	try {
		if (errorString.empty()) {
			std::ostringstream ss;
			ss << "Invalid Numbstrict";
			if (!filename.empty()) {
				ss << " in " << filename;
			}
			ss << " at line " << line << " column " << column << " (offset " << offset << ')';
			errorString = ss.str();
		}
		return errorString.c_str();
	}
	catch (...) {
		assert(0);
		return "exception in Numbstrict::ParsingError::what()";
	}
}

LineAndColumn Element::lineAndColumn(const StringIt p) const {
	assert(exists());
	LineAndColumn lineAndColumn(1, 1);
	for (StringIt q = s->first.begin(); q != p; ++q) {
		assert(q != s->first.end());
		if (*q == '\n') {
			++lineAndColumn.first;
			lineAndColumn.second = 0;
		}
		++lineAndColumn.second;
	}
	return lineAndColumn;
}

bool Parser::eof() const { return p == source.end(); }
StringIt Parser::getFailPoint() const { return p; }
Parser::Parser(const Element& source) : source(source), p(source.begin()) { }
String::difference_type Parser::left() const { return source.end() - p; }

template<typename T> bool Parser::tryToParseSignedInt(T& i) {
	whiteAndComments();
	i = 0;
	int sign = 1;
	if (!eof() && (*p == '+' || *p == '-')) {
		sign = ((*p == '-') ? -1 : 1);
		++p;
	}
	const StringIt b = p;
	typedef typename std::make_unsigned<T>::type UT;
	const UT HALF_MAX = static_cast<UT>(1) << (sizeof (UT) * 8 - 1);
	const T QUARTER_MAX = static_cast<T>(HALF_MAX >> 1);
	const UT limit = (sign >= 0 ? HALF_MAX : HALF_MAX + 1);
	UT ui = 0;
	if (left() >= 2 && p[0] == '0' && p[1] == 'x') {
		p += 2;
		int v;
		while (!eof() && (v = fromHex(*p)) >= 0) {
			ui = ui * 16 + v;
			if (ui >= limit) {
				return false;
			}
			++p;
		}
	} else if (!eof() && *p == '0') {
		++p;
	} else {
		while (!eof() && *p >= '0' && *p <= '9') {
			ui = ui * 10 + (*p - '0');
			if (ui >= limit) {
				return false;
			}
			++p;
		}
	}
	if (ui == HALF_MAX) {
		assert(sign < 0);
		i = static_cast<T>(0 - QUARTER_MAX - QUARTER_MAX);
	} else {
		i = static_cast<T>(ui) * sign;
	}
	if (p == b) {
		return false;
	}
	whiteAndComments();
	return eof();
}

template<typename T> bool Parser::tryToParseUnsignedInt(T& ui) {
	whiteAndComments();
	ui = 0;
	if (!eof() && *p == '+') {
		++p;
	}
	const StringIt b = p;
	if (left() >= 2 && p[0] == '0' && p[1] == 'x') {
		p += 2;
		int v;
		while (!eof() && (v = fromHex(*p)) >= 0) {
			const T lui = ui;
			ui = ui * 16 + v;
			if (ui < lui) {
				return false;
			}
			++p;
		}
	} else if (!eof() && *p == '0') {
		++p;
	} else {
		while (!eof() && *p >= '0' && *p <= '9') {
			const T lui = ui;
			ui = ui * 10 + (*p - '0');
			if (ui < lui) {
				return false;
			}
			++p;
		}
	}
	if (p == b) {
		return false;
	}
	whiteAndComments();
	return eof();
}

bool Parser::tryToParse(int8_t& i) { return tryToParseSignedInt(i); }
bool Parser::tryToParse(uint8_t& i) { return tryToParseUnsignedInt(i); }
bool Parser::tryToParse(int16_t& i) { return tryToParseSignedInt(i); }
bool Parser::tryToParse(uint16_t& i) { return tryToParseUnsignedInt(i); }
bool Parser::tryToParse(int32_t& i) { return tryToParseSignedInt(i); }
bool Parser::tryToParse(uint32_t& i) { return tryToParseUnsignedInt(i); }
bool Parser::tryToParse(int64_t& i) { return tryToParseSignedInt(i); }
bool Parser::tryToParse(uint64_t& i) { return tryToParseUnsignedInt(i); }

bool Parser::tryToParse(bool& b) {
	b = false;
	whiteAndComments();
	if (left() >= 5 && std::equal(p, p + 5, "false")) {
		p += 5;
	} else if (left() >= 4 && std::equal(p, p + 4, "true")) {
		b = true;
		p += 4;
	} else {
		return false;
	}
	whiteAndComments();
	return eof();
}

void Parser::throwError() {
	size_t offset = source.offset(p);
	const LineAndColumn lineAndColumn = source.lineAndColumn(p);
	throw ParsingError(source.filename(), offset, lineAndColumn.first, lineAndColumn.second);
}

bool Parser::comment() {
	if (left() < 2 || p[0] != '/') {
		return false;
	}
	if (p[1] == '/') {
		p += 2;
		while (!eof() && *p != '\r' && *p != '\n' && (static_cast<UChar>(*p) >= 32 || *p == '\t')) {
			++p;
		}
		return true;
	} else if (p[1] == '*') {
		p += 2;

		// Refrain from using recursion when it is easy, to prevent stack overflow.
		int nestCounter = 1;
		while (!eof() && nestCounter > 0) {
			if (static_cast<UChar>(*p) < 32 && *p != '\r' && *p != '\n' && *p != '\t') {
				break;
			}
			if (left() >= 2 && p[0] == '/' && p[1] == '*') {
				++nestCounter;
				p += 2;
			} else if (left() >= 2 && p[0] == '*' && p[1] == '/') {
				--nestCounter;
				p += 2;
			} else {
				++p;
			}
		}
		return true;
	} else {
		return false;
	}
}

bool Parser::whiteAndComments() {
	const StringIt b = p;
	while (!eof()) {
		if (!comment()) {
			if (!(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
				break;
			}
			++p;
		}
	}
	return p != b;
}

bool Parser::horizontalWhiteAndComments() {
	const StringIt b = p;
	while (!eof()) {
		if (!comment()) {
			if (!(*p == ' ' || *p == '\t')) {
				break;
			}
			++p;
		}
	}
	return p != b;
}

template<typename C> bool isLeadingIdentifierChar(const C c) {
	return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_');
}

template<typename C> bool isIdentifierChar(const C c) {
	return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_');
}

template<typename S> bool keyNeedsQuoting(const S& key) {
	typename S::const_iterator p = key.begin();
	const typename S::const_iterator e = key.end();
	if (p == e || !isLeadingIdentifierChar(*p)) {
		return true;
	}
	++p;
	while (p != e) {
		if (!isIdentifierChar(*p)) {
			return true;
		}
		++p;
	}
	return false;
}

bool Parser::parseIdentifier(String& identifier) {
	const StringIt b = p;
	if (eof() || !isLeadingIdentifierChar(*p)) {
		return false;
	}
	++p;
	while (!eof() && isIdentifierChar(*p)) {
		++p;
	}
	identifier = String(b, p);
	return true;
}

bool Parser::quotedStringElement(Element& element) {
	assert(!eof() && (*p == '\"' || *p == '\''));
	const StringIt b = p;
	const Char quoteChar = *p;
	++p;
	while (!eof() && *p != quoteChar) {
		if (*p == '\\') {
			++p;
		}
		if (!eof()) {
			++p;
		}
	}
	const bool ok = !eof();
	if (ok) {
		++p;
	}
	element = Element(source, b, p);
	return ok;
}

template<typename C> bool Parser::quotedString(std::basic_string<C>& string) {
	if (eof() || !(*p == '\"' || *p == '\'')) {
		return false;
	}
	return genericUnquoteString(p, source.end(), string);
}

template<typename C> void Parser::unquotedText(std::basic_string<C>& string) {
	whiteAndComments();
	do {
		StringIt b = p;
		while (!eof() && isTextChar(*p) && !(left() >= 2 && p[0] == '/' && (p[1] == '/' || p[1] == '*'))) {
			++p;
		}
		if (b != p) {
			if (!string.empty()) {
				string += ' ';
			}
			string.insert(string.end(), b, p);
		}
	} while (whiteAndComments());
}

template<typename C> bool Parser::stringOrText(std::basic_string<C>& string) {
	string.clear();
	whiteAndComments();
	if (eof() || !(*p == '\"' || *p == '\'')) {
		unquotedText(string);
	} else if (quotedString(string)) {
		whiteAndComments();
	} else {
		return false;
	}
	return eof();
}

bool Parser::tryToParse(String& string) { return stringOrText(string); }
bool Parser::tryToParse(WideString& string) { return stringOrText(string); }

bool Parser::blockElement(Element& element) {
	element = Element(source, p, p);
	if (eof() || *p != '{') {
		return false;
	}
	++p;
	int nestCounter = 1;
	while (!eof() && nestCounter > 0) {
		Element dummy;
		switch (*p) {
			case '\"': case '\'': if (!quotedStringElement(dummy)) return false; break;
			case '/': if (!comment()) ++p; break;
			case '{': ++nestCounter; ++p; break;
			case '}': --nestCounter; ++p; break;
			default: ++p;
		}
	}
	element = Element(element, element.begin(), p);
	return (nestCounter == 0);
}

bool Parser::unquotedTextElement(Element& element) {
	element = Element(source, p, p);
	do {
		StringIt b = p;
		while (!eof() && isTextChar(*p) && !(left() >= 2 && p[0] == '/' && (p[1] == '/' || p[1] == '*'))) {
			++p;
		}
		if (b != p) {
			element = Element(element, element.begin(), p);
		}
	} while (horizontalWhiteAndComments());
	return true;
}

bool Parser::valueElement(Element& element) {
	if (!eof() && *p == '{') {
		return blockElement(element);
	} else if (!eof() && (*p == '\"' || *p == '\'')) {
		return quotedStringElement(element);
	} else if (!eof() && isTextChar(*p)) {
		return unquotedTextElement(element);
	} else {
		element = Element(source, p, p);
		return true;
	}
}

static void toKeyString(String& d, const String& s) { d = s; }
static void toKeyString(WideString& d, const String& s) { d = WideString(s.begin(), s.end()); }

template<typename C> bool Parser::keyValuePair(std::map<std::basic_string<C>, Element>& elements) {
	std::pair<std::basic_string<C>, Element> kv;
	const StringIt b1 = p;
	String identifier;
	if (parseIdentifier(identifier)) {
		toKeyString(kv.first, identifier);
	} else if (!quotedString(kv.first)) {
		return false;
	}
	horizontalWhiteAndComments();
	kv.second = Element(source, p, p);
	if (eof() || *p != ':') {
		return false;
	}
	++p;
	horizontalWhiteAndComments();
	if (!valueElement(kv.second)) {
		return false;
	}
	horizontalWhiteAndComments();
	if (!elements.insert(kv).second) {
		p = b1;
		return false;
	}
	return true;
}

bool Parser::nextElement() {
	bool optionalComma = (!eof() && (*p == '\r' || *p == '\n'));
	whiteAndComments();
	if (eof() || *p == '}') {
		return true;
	}
	if (*p == ',') {
		++p;
		whiteAndComments();
		return true;
	}
	return optionalComma;
}

template<typename C> bool Parser::keyValueElements(std::map<std::basic_string<C>, Element>& elements) {
	if (!eof() && *p == ':') {	// special empty struct syntax { : }
		++p;
		whiteAndComments();
	} else {
		while (!eof() && *p != '}') {
			if (!keyValuePair(elements)) {
				return false;
			}
			if (!nextElement()) {
				return false;
			}
		}
	}
	return true;
}

bool Parser::valueListElements(Array& elements) {
	while (!eof() && *p != '}') {
		Element v(source, p, p);
		if (!valueElement(v)) {
			return false;
		}
		horizontalWhiteAndComments();
		elements.push_back(v);
		if (!nextElement()) {
			return false;
		}
	}
	return true;
}

template<typename S> bool Parser::tryToParseStruct(S& elements) {
	elements.clear();
	whiteAndComments();
	if (!eof() && *p == '{') {
		++p;
		whiteAndComments();
		if (!keyValueElements(elements)) {
			return false;
		}
		if (eof() || *p != '}') {
			return false;
		}
		++p;
		whiteAndComments();
	} else if (!keyValueElements(elements)) {
		return false;
	}
	return eof();
}

bool Parser::tryToParse(Struct& elements) {
	return tryToParseStruct(elements);
}

bool Parser::tryToParse(WideStruct& elements) {
	return tryToParseStruct(elements);
}

bool Parser::tryToParse(Array& elements) {
	elements.clear();
	whiteAndComments();
	if (!eof() && *p == '{') {
		++p;
		whiteAndComments();
		if (!valueListElements(elements)) {
			return false;
		}
		if (eof() || *p != '}') {
			return false;
		}
		++p;
		whiteAndComments();
	} else if (!valueListElements(elements)) {
		return false;
	}
	return eof();
}

bool Parser::tryToParse(Variant& toVariant) {
	toVariant = Variant();
	whiteAndComments();
	if (!eof()) {
		const StringIt b = p;
		switch (*p) {
			case 't': case 'f': {
				if (tryToParse(toVariant.boolean)) {
					toVariant.type = Variant::BOOLEAN;
					return true;
				}
				break;
			}
			case 'i': // inf
			case 'n': // nan
			case '+': case '-':
			case '0': case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9': {
				if (tryToParse(toVariant.integer)) {
					toVariant.type = Variant::INTEGER;
					return true;
				}
				p = b;
				if (tryToParse(toVariant.unsignedInteger)) {
					toVariant.type = Variant::UNSIGNED_INTEGER;
					return true;
				}
				p = b;
				if (tryToParse(toVariant.real)) {
					toVariant.type = Variant::REAL;
					return true;
				}
				break;
			}
			case '{': {
				if (tryToParse(toVariant.array)) {
					toVariant.type = Variant::ARRAY;
					return true;
				}
				p = b;
				if (tryToParse(toVariant.structure)) {
					toVariant.type = Variant::STRUCT;
					return true;
				}
				return false;
			}
		}
		p = b;
	}
	if (tryToParse(toVariant.text)) {
		toVariant.type = Variant::TEXT;
		return true;
	}
	return false;
}

template<typename T> bool Parser::tryToParseReal(T& r) {
	whiteAndComments();
	if (eof()) {
		return false;
	}
	const Char* const b = &*p;
	const Char* e = parseReal<T>(b, &*(source.end() - 1) + 1, r);
	if (e == b) {
		return false;
	}
	p = source.begin() + (e - &*source.begin());
	whiteAndComments();
	return eof();
}

bool Parser::tryToParse(double& d) { return tryToParseReal(d); }
bool Parser::tryToParse(float& f) { return tryToParseReal(f); }

bool Parser::isEmpty() {
	whiteAndComments();
	return eof();
}

bool Parser::isBracketed() {
	whiteAndComments();
	return !eof() && *p == '{';
}

bool Parser::isQuoted() {
	whiteAndComments();
	return !eof() && (*p == '"' || *p == '\'');
}

static String reindent(const String& s, int tabCount) {
	const StringIt b = s.begin();
	const StringIt e = s.end();
	StringIt p = e;
	while (p != b && p[-1] != '\n') {
		--p;
	}
	int dropCount = 0;
	while (p != e && *p == '\t') {
		++dropCount;
		++p;
	}
	String indented;
	p = b;
	while (p != e) {
		int i = 0;
		while (p != e && *p == '\t' && i < dropCount) {
			++p;
			++i;
		}
		const StringIt q = p;
		while (p != e && *p != '\n') {
			++p;
		}
		if (p != e) {
			++p;
		}
		indented.insert(indented.end(), q, p);
		if (p != e) {
			for (int i = 0; i < tabCount; ++i) {
				indented += '\t';
			}
		}
	}
	return indented;
}

String compose(const Char* fromString, bool preferUnquoted) {
	return quoteString(String(fromString), preferUnquoted, '\"');
}

String compose(const String& fromString, bool preferUnquoted) {
	return quoteString(fromString, preferUnquoted, '\"');
}

String compose(const WideChar* fromString, bool preferUnquoted) {
	return quoteString(WideString(fromString), preferUnquoted, '\"');
}

String compose(const WideString& fromString, bool preferUnquoted) {
	return quoteString(fromString, preferUnquoted, '\"');
}

String compose(const Array& array, bool multiLine, bool bracket) {
	String string = (bracket ? (multiLine ? "{\n" : "{ ") : "");
	for (Array::const_iterator it = array.begin(); it != array.end(); ++it) {
		if (multiLine && bracket) {
			string += '\t';
		}
		const String code = it->code();
		string += reindent(code, multiLine && bracket ? 1 : 0);
		const bool lastElement = (it + 1 == array.end());
		if (!lastElement || Parser(code).isEmpty()) {
			string += ',';
		}
		if (!lastElement || multiLine || bracket) {
			string += (multiLine ? '\n' : ' ');
		}
	}
	return string + (bracket ? "}" : "");
}

static const String& toCharString(const String& s) { return s; }
static String toCharString(const WideString& s) { return String(s.begin(), s.end()); }

template<typename S> String composeStruct(const S& structure, bool multiLine, bool bracket) {
	String string = (bracket ? (multiLine ? "{\n\t" : "{ ") : "");
	for (typename S::const_iterator it = structure.begin(); it != structure.end(); ++it) {
		if (it->second.exists()) {
			if (it != structure.begin()) {
				string += (multiLine ? (bracket ? "\n\t" : "\n") : ", ");
			}
			string += (keyNeedsQuoting(it->first)
					? quoteString(it->first, false, '\"') : toCharString(it->first));
			string += ": ";
			string += reindent(it->second.code(), multiLine && bracket ? 1 : 0);
		}
	}
	if (structure.begin() == structure.end()) {
		string += ':';
	}
	return string + (multiLine ? (bracket ? "\n}" : "\n") : (bracket ? " }" : ""));
}

String compose(const Struct& structure, bool multiLine, bool bracket) {
	return composeStruct(structure, multiLine, bracket);
}

String compose(const WideStruct& structure, bool multiLine, bool bracket) {
	return composeStruct(structure, multiLine, bracket);
}

String compose(float fromFloat) { return floatToString(fromFloat); }
String compose(double fromDouble) { return doubleToString(fromDouble); }

template<typename T> String composeSignedInt(const T i, const bool hexFormat, int minHexLength) {
	if (hexFormat) {
		typedef typename std::make_unsigned<T>::type UT;
		const T MINI = std::numeric_limits<T>::min();
		return (i < 0
				? String("-0x") + intToString<UT>(static_cast<UT>((i == MINI ? i : -i)), 16, minHexLength)
				: String("0x") + intToString<UT>(static_cast<UT>(i), 16, minHexLength));
	} else {
		return intToString<T>(i, 10, 1);
	}
}

template<typename T> String composeUnsignedInt(const T ui, const bool hexFormat, int minHexLength) {
	return (hexFormat ? String("0x") + intToString<T>(ui, 16, minHexLength) : intToString<T>(ui, 10, 1));
}

String compose(int8_t fromInt, bool hexFormat, int minHexLength) {
	return composeSignedInt(fromInt, hexFormat, minHexLength);
}

String compose(uint8_t fromInt, bool hexFormat, int minHexLength) {
	return composeUnsignedInt(fromInt, hexFormat, minHexLength);
}

String compose(int16_t fromInt, bool hexFormat, int minHexLength) {
	return composeSignedInt(fromInt, hexFormat, minHexLength);
}

String compose(uint16_t fromInt, bool hexFormat, int minHexLength) {
	return composeUnsignedInt(fromInt, hexFormat, minHexLength);
}

String compose(int32_t fromInt, bool hexFormat, int minHexLength) {
	return composeSignedInt(fromInt, hexFormat, minHexLength);
}

String compose(uint32_t fromInt, bool hexFormat, int minHexLength) {
	return composeUnsignedInt(fromInt, hexFormat, minHexLength);
}

String compose(int64_t fromInt, bool hexFormat, int minHexLength) {
	return composeSignedInt(fromInt, hexFormat, minHexLength);
}

String compose(uint64_t fromInt, bool hexFormat, int minHexLength) {
	return composeUnsignedInt(fromInt, hexFormat, minHexLength);
}

String compose(bool fromBool) {
	return (fromBool ? "true" : "false");
}

String compose(const Variant& variant) {
	switch (variant.type) {
		default: assert(0);
		case Variant::STRUCT: return compose(variant.structure);
		case Variant::ARRAY: return compose(variant.array);
		case Variant::TEXT: return compose(variant.text);
		case Variant::REAL: return compose(variant.real);
		case Variant::INTEGER: return compose(variant.integer);
		case Variant::UNSIGNED_INTEGER: return compose(variant.unsignedInteger, true);
		case Variant::BOOLEAN: return compose(variant.boolean);
	}
}

template<typename T> T stringToReal(const Char* b, const Char* e, const Char** next) {
	if (e == 0) {
		e = b + strlen(b);
	}
	const Char* p = skipWhite(b, e);
	T v;
	p = skipWhite(parseReal<T>(p, e, v), e);
	if (next != 0) {
		*next = p;
	}
	return v;
}

template<typename T> T stringToReal(const String& s, size_t* nextOffset) {
	const Char* next;
	const T v = stringToReal<T>(s.data(), s.data() + s.size(), &next);
	if (nextOffset != 0) {
		*nextOffset = next - s.data();
	}
	return v;
}

template<typename T> std::basic_string<T> unescapeGeneric(const String& s, size_t* nextOffset) {
	std::basic_string<T> unescaped;
	StringIt p = s.begin();
	const StringIt e = s.end();
	p = skipWhite(p, e);
	if (p != s.end() && (*p == '\"' || *p == '\'')) {
		genericUnquoteString(p, e, unescaped);
		p = skipWhite(p, e);
	} else if (!s.empty()) {
		// recast because we assume iso8859-1 in source string
		const unsigned char* recasted = reinterpret_cast<const unsigned char*>(s.data());
		unescaped = std::basic_string<T>(recasted, recasted + s.size());
		p = e;
	}
	if (nextOffset != 0) {
		*nextOffset = p - s.begin();
	}
	return unescaped;
}

Char* floatToChars(float value, Char* destination) {
	Char* p = realToString<float>(destination, value);
	assert(p < destination + 32);
	*p = 0;
	return p;
}

String floatToString(float value) {
	Char buffer[32];
	return String(buffer, realToString<float>(buffer, value));
}

float charsToFloat(const Char* begin, const Char* end, const Char** next) {
	return stringToReal<float>(begin, end, next);
}

float stringToFloat(const String& s, size_t* nextOffset) {
	return stringToReal<float>(s, nextOffset);
}

Char* doubleToChars(double value, Char* destination) {
	Char* p = realToString<double>(destination, value);
	assert(p < destination + 32);
	*p = 0;
	return p;
}

String doubleToString(double value) {
	Char buffer[32];
	return String(buffer, realToString<double>(buffer, value));
}

double charsToDouble(const Char* begin, const Char* end, const Char** next) {
	return stringToReal<double>(begin, end, next);
}

double stringToDouble(const String& s, size_t* nextOffset) {
	return stringToReal<double>(s, nextOffset);
}

Char* intToChars(int value, Char* destination) {
	Char buffer[sizeof (int) * 8 + 1];
	Char* p = intToString(buffer, value, 10, 1);
	p = std::copy(p, buffer + sizeof (int) * 8 + 1, destination);
	*p = 0;
	return p;
}

Char* intToHexChars(unsigned int value, Char* destination, int minLength) {
	Char buffer[sizeof (unsigned int) * 8 + 1];
	Char* p = intToString(buffer, value, 16, minLength);
	p = std::copy(p, buffer + sizeof (unsigned int) * 8 + 1, destination);
	*p = 0;
	return p;
}

String intToString(int value) {
	return intToString(value, 10);
}

String intToHexString(unsigned int value, int minLength) {
	return intToString(value, 16, minLength);
}

int stringToInt(const String& s, size_t* nextOffset) {
	int v = 0;
	Element source(s);
	Parser parser(source);
	bool success = parser.tryToParse(v);
	if (nextOffset != 0) {
		*nextOffset = (success ? s.size() : parser.getFailPoint() - source.begin());
	}
	return v;
}

String quoteString(const String& s, Char quoteChar) {
	return quoteString(s, false, quoteChar);
}

String unquoteString(const String& s, size_t* nextOffset) {
	return unescapeGeneric<Char>(s, nextOffset);
}

String quoteWideString(const WideString& s, Char quoteChar) {
	return quoteString(s, false, quoteChar);
}

WideString unquoteWideString(const String& s, size_t* nextOffset) {
	return unescapeGeneric<WideChar>(s, nextOffset);
}

const char* UndefinedNamedElementError::what() const throw() {
	try {
	if (errorString.empty()) {
		errorString = "Undefined Numbstrict element: " + (keyNeedsQuoting(name) ? quoteString(name) : name);
	}
	return errorString.c_str();
	}
	catch (...) {
		assert(0);
		return "exception in Numbstrict::UndefinedNamedElementError::what()";
	}
}

bool unitTest() {
#if !defined(NDEBUG)
	std::u16string emoji16;
	emoji16 += 0xd83d;
	emoji16 += 0xdc19;
	String qs = quoteString(emoji16, false, '\"');
	assert(quoteString(emoji16, false, '\"') == "\"\\U0001f419\"");

	std::u32string emoji32;
	emoji32 += 0x1F419;
	assert(quoteString(emoji32, false, '\"') == "\"\\U0001f419\"");

	Parser parser16(Element("\"\\U0001f419\""));
	emoji16.clear();
	assert(parser16.quotedString(emoji16));
	assert(emoji16.size() == 2 && emoji16[0] == 0xd83d && emoji16[1] == 0xdc19);

	Parser parser32(Element("\"\\U0001f419\""));
	emoji32.clear();
	assert(parser32.quotedString(emoji32));
	assert(emoji32.size() == 1 && emoji32[0] == 0x1F419);

	assert(reindent("asdf", 1) == "asdf");
	assert(reindent("\t\t\tasdf", 1) == "asdf");
	assert(reindent("{\n\t\t\t}", 0) == "{\n}");
	assert(reindent("{\n\t\t\tasdf", 1) == "{\n\tasdf");
	assert(reindent("{\n\t\t\t\t1\n\t\t\t\t2\n\t\t\t}", 0) == "{\n\t1\n\t2\n}");
	assert(reindent("{\n\t\t\t\t1\n\t\t\t\t2\n\t\t\t}", 1) == "{\n\t\t1\n\t\t2\n\t}");

    assert((rewrap<int8_t, uint32_t>(13) == 13));
    assert((rewrap<int8_t, uint32_t>(4294967285) == -11));
    assert((rewrap<uint8_t, uint32_t>(13) == 13));
    assert((rewrap<uint8_t, uint32_t>(4294967285) == 245));
    assert((rewrap<int16_t, uint32_t>(13) == 13));
    assert((rewrap<int16_t, uint32_t>(4294967285) == -11));
    assert((rewrap<uint16_t, uint32_t>(13) == 13));
    assert((rewrap<uint16_t, uint32_t>(4294967285) == 65525));
    assert((rewrap<int32_t, uint32_t>(13) == 13));
    assert((rewrap<int32_t, uint32_t>(4294967285) == -11));
    assert((rewrap<uint32_t, uint32_t>(13) == 13));
    assert((rewrap<uint32_t, uint32_t>(4294967285) == 4294967285));
    assert((rewrap<uint32_t, uint16_t>(65525) == 65525));
    assert((rewrap<int32_t, uint16_t>(65525) == 65525));
    assert((rewrap<uint16_t, uint16_t>(65525) == 65525));
    assert((rewrap<int16_t, uint16_t>(65525) == -11));

    assert((rewrap<uint32_t, int8_t>(-11) == 245));
    assert((rewrap<uint32_t, uint8_t>(245) == 245));
    assert((rewrap<uint32_t, int16_t>(-11) == 65525));
    assert((rewrap<uint32_t, uint16_t>(65525) == 65525));
    assert((rewrap<uint32_t, int32_t>(-11) == 4294967285));
    assert((rewrap<uint32_t, uint32_t>(4294967285) == 4294967285));

	assert(Numbstrict::quoteString("") == "\"\"");
	assert(Numbstrict::unquoteString("\"\"") == "");
	assert(Numbstrict::unquoteString("\'\'") == "");
	assert(Numbstrict::unquoteString("") == "");
	assert(Numbstrict::unquoteString("\"abcd\"") == "abcd");
	assert(Numbstrict::unquoteString("abcd") == "abcd");
	assert(Numbstrict::quoteString("abcd") == "\"abcd\"");
	assert(Numbstrict::unquoteString("  \t \r \n   \"abcd\" \t \r \n  ") == "abcd");
	assert(Numbstrict::quoteString("\"") == "\"\\\"\"");
	const char SINGLE_QUOTE = '\'';	// bug in MSVC2022 gives errors on the following lines unless I do like this
	assert(Numbstrict::quoteString("\"", SINGLE_QUOTE) == "'\"'");
	assert(Numbstrict::quoteString("\'", SINGLE_QUOTE) == "'\\''");
	assert(Numbstrict::unquoteString("\"\\\"\"") == "\"");
	assert(Numbstrict::quoteString("'") == "\"'\"");
	assert(Numbstrict::unquoteString("\"'\"") == "'");
	assert(Numbstrict::unquoteString("\"a\\n\\tbcdef\\rkoko\\\"\\\\'\\x23\\u0049\\U000000F2_end\"") == "a\n\tbcdef\rkoko\"\\'\x23\x49\xF2_end");
	assert(Numbstrict::unquoteWideString("\"a\\n\\tbcdef\\rkoko\\\"\\\\'\\x23\\u4949\\U0000F232_end\"") == L"a\n\tbcdef\rkoko\"\\'\x23\u4949\uF232_end");

	assert(intToString(0) == "0");
	assert(stringToInt("0") == 0);
	assert(intToString(101010) == "101010");
	assert(stringToInt("101010") == 101010);
	assert(intToString(-101010) == "-101010");
	assert(stringToInt("-101010") == -101010);
	assert(intToString(2147483647) == "2147483647");
	assert(stringToInt("2147483647") == 2147483647);
	assert(intToString(-2147483647 - 1) == "-2147483648");
	assert(stringToInt("-2147483648") == -2147483647 - 1);
	assert(intToHexString(0, 1) == "0");
	assert(stringToInt("0x0") == 0);
	assert(intToHexString(0xff, 1) == "ff");
	assert(stringToInt("0xff") == 0xff);
	assert(intToHexString(0xff, 4) == "00ff");
	assert(stringToInt("0x00ff") == 0xff);
	assert(intToHexString(0, 8) == "00000000");
	assert(stringToInt("0x00000000") == 0);
	assert(intToHexString(0x01234567U, 8) == "01234567");
	assert(stringToInt("0x01234567") == 0x01234567U);
	assert(intToHexString(0x89abcdefU, 8) == "89abcdef");
// TODO: assert(stringToInt("0x89abcdef") == 0x89abcdefU);
	assert(intToHexString(0x7fffffffU) == "7fffffff");
// TODO: assert(stringToInt("0x7fffffff") == 0x7fffffffU);
	assert(intToHexString(0xffffffffU) == "ffffffff");
// TODO: assert(stringToInt("0xffffffff") == 0xffffffffU);

	assert(doubleToString(0.0) == "0.0");
	assert(stringToDouble("0.0") == 0.0);
	assert(doubleToString(-0.0) == "-0.0");
	assert(std::signbit(stringToDouble("-0.0")));
	assert(doubleToString(-1.0) == "-1.0");
	assert(stringToDouble("-1.0") == -1.0);
	assert(doubleToString(1.12345689101112133911897) == "1.1234568910111213");
	assert(stringToDouble("1.1234568910111213") == 1.12345689101112133911897);
	assert(doubleToString(999.999999999999886313162) == "999.9999999999999");
	assert(stringToDouble("999.9999999999999") == 999.999999999999886313162);
	assert(doubleToString(123456789.12345677614212) == "123456789.12345678");
	assert(stringToDouble("123456789.12345678") == 123456789.12345677614212);
	assert(doubleToString(123400000000000000000.0) == "1.234e+20");
	assert(stringToDouble("1.234e+20") == 123400000000000000000.0);
	assert(doubleToString(1.23400000000000005948965e+40) == "1.234e+40");
	assert(stringToDouble("1.234e+40") == 1.23400000000000005948965e+40);
	assert(doubleToString(1.23400000000000008905397e+80) == "1.234e+80");
	assert(stringToDouble("1.234e+80") == 1.23400000000000008905397e+80);
	assert(doubleToString(8.76499999999999967951181e-20) == "8.765e-20");
	assert(stringToDouble("8.765e-20") == 8.76499999999999967951181e-20);
	assert(doubleToString(-8.76499999999999944958345e-40) == "-8.765e-40");
	assert(stringToDouble("-8.765e-40") == -8.76499999999999944958345e-40);
	assert(doubleToString(8.76500000000000034073598e-80) == "8.765e-80");
	assert(stringToDouble("8.765e-80") == 8.76500000000000034073598e-80);
	assert(doubleToString(4.94065645841246544176569e-324) == "5.0e-324");
	assert(stringToDouble("5.0e-324") == 4.94065645841246544176569e-324);
	assert(doubleToString(9.88131291682493088353138e-324) == "1.0e-323");
	assert(stringToDouble("1.0e-323") == 9.88131291682493088353138e-324);
	assert(doubleToString(1.79769313486231570814527e+308) == "1.7976931348623157e+308");
	assert(stringToDouble("1.7976931348623157e+308") == 1.79769313486231570814527e+308);
	assert(doubleToString(1.79769313486231550856124e+308) == "1.7976931348623155e+308");
	assert(stringToDouble("1.7976931348623155e+308") == 1.79769313486231550856124e+308);
	assert(doubleToString(std::numeric_limits<double>::infinity()) == "inf");
	assert(stringToDouble("inf") == std::numeric_limits<double>::infinity());
	assert(doubleToString(std::numeric_limits<double>::quiet_NaN()) == "nan");
	assert(isNaN(stringToDouble("nan")));

	{
		struct FragileDoubleCase {
			uint64_t bits;
			const char* expected;
		};
		static const FragileDoubleCase kFragileDoubleCases[] = {
			{ 0x0000000000000000ull, "0.0" },
			{ 0x0000000000000001ull, "5.0e-324" },
			{ 0x0000000000000002ull, "1.0e-323" },
			{ 0x000ffffffffffff8ull, "2.2250738585071974e-308" },
			{ 0x000ffffffffffff9ull, "2.225073858507198e-308" },
			{ 0x000ffffffffffffaull, "2.2250738585071984e-308" },
			{ 0x000ffffffffffffbull, "2.225073858507199e-308" },
			{ 0x000ffffffffffffcull, "2.2250738585071994e-308" },
			{ 0x000ffffffffffffdull, "2.2250738585072e-308" },
			{ 0x000ffffffffffffeull, "2.2250738585072004e-308" },
			{ 0x000fffffffffffffull, "2.225073858507201e-308" },
			{ 0x0010000000000000ull, "2.2250738585072014e-308" },
			{ 0x0010000000000001ull, "2.225073858507202e-308" },
			{ 0x0010000000000002ull, "2.2250738585072024e-308" },
			{ 0x0010000000000003ull, "2.225073858507203e-308" },
			{ 0x0010000000000004ull, "2.2250738585072034e-308" },
			{ 0x0010000000000005ull, "2.225073858507204e-308" },
			{ 0x0010000000000006ull, "2.2250738585072043e-308" },
			{ 0x0010000000000007ull, "2.225073858507205e-308" },
			{ 0x0010000000000008ull, "2.2250738585072053e-308" },
			{ 0x3e7ad7f29abcaf47ull, "9.999999999999998e-8" },
			{ 0x3e7ad7f29abcaf49ull, "1.0000000000000001e-7" },
			{ 0x3eb0c6f7a0b5ed8cull, "9.999999999999997e-7" },
			{ 0x3eb0c6f7a0b5ed8eull, "0.0000010000000000000002" },
			{ 0x4202a05f1fffffffull, "9999999999.999998" },
			{ 0x4202a05f20000001ull, "1.0000000000000002e+10" },
			// Just below powers of two (rounding carry into exponent)
			{ 0x3fffffffffffffffull, "1.9999999999999998" },
			{ 0x400fffffffffffffull, "3.9999999999999996" },
			{ 0x401fffffffffffffull, "7.999999999999999" },
			{ 0x402fffffffffffffull, "15.999999999999998" },
			{ 0x403fffffffffffffull, "31.999999999999996" },
			{ 0x7feffffffffffffeull, "1.7976931348623155e+308" },
			{ 0x7fefffffffffffffull, "1.7976931348623157e+308" },
			{ 0x7ff0000000000000ull, "inf" },
			{ 0x8000000000000001ull, "-5.0e-324" },
			{ 0x8000000000000002ull, "-1.0e-323" },
			{ 0x8009d1f053c113dcull, "-1.3656492814424367e-308" },
			{ 0x800ffffffffffffeull, "-2.2250738585072004e-308" },
			{ 0x800fffffffffffffull, "-2.225073858507201e-308" },
			{ 0x8010000000000000ull, "-2.2250738585072014e-308" },
			{ 0x8010000000000001ull, "-2.225073858507202e-308" },
			{ 0xbe7ad7f29abcaf47ull, "-9.999999999999998e-8" },
			{ 0xbe7ad7f29abcaf49ull, "-1.0000000000000001e-7" },
			{ 0xbeb0c6f7a0b5ed8cull, "-9.999999999999997e-7" },
			{ 0xbeb0c6f7a0b5ed8eull, "-0.0000010000000000000002" },
			{ 0xc202a05f1fffffffull, "-9999999999.999998" },
			{ 0xc202a05f20000001ull, "-1.0000000000000002e+10" },
			{ 0xffeffffffffffffeull, "-1.7976931348623155e+308" },
			{ 0xffefffffffffffffull, "-1.7976931348623157e+308" },
			{ 0xfff0000000000000ull, "-inf" },
		};
		auto toDoubleBits = [](double number) -> uint64_t {
			uint64_t bits = 0;
			std::memcpy(&bits, &number, sizeof bits);
			return bits;
		};
		for (const FragileDoubleCase& entry : kFragileDoubleCases) {
			double value;
			std::memcpy(&value, &entry.bits, sizeof value);
			const String text = doubleToString(value);
			assert(text == entry.expected);
			const double parsed = stringToDouble(entry.expected);
			uint64_t parsedBits = toDoubleBits(parsed);
			assert(parsedBits == entry.bits);
			const double roundTrip = stringToDouble(text);
			parsedBits = toDoubleBits(roundTrip);
			assert(parsedBits == entry.bits);
		}
	}


	{
		struct FragileFloatCase {
			uint32_t bits;
			const char* expected;
			const char* source;
		};
		static const FragileFloatCase kFragileFloatCases[] = {
			{ 0x00000000u, "0.0", "0.0" },
			{ 0x00000001u, "1.0e-45", "1.0e-45" },
			{ 0x00000002u, "3.0e-45", "3.0e-45" },
			{ 0x007ffffeu, "1.1754941e-38", "1.1754941e-38" },
			{ 0x007fffffu, "1.1754942e-38", "1.1754942e-38" },
			{ 0x00800000u, "1.1754944e-38", "1.1754944e-38" },
			{ 0x00800001u, "1.1754945e-38", "1.1754945e-38" },
			// Additional simple cases migrated from inline asserts
			{ 0x80000000u, "-0.0", "-0.0" },
			{ 0xbf800000u, "-1.0", "-1.0" },
			{ 0x3f8fcd6fu, "1.1234568", "1.1234568" },
			{ 0x447a0000u, "1000.0", "1000.0" },
			{ 0x4ceb79a3u, "123456790.0", "123456792.0" },
			{ 0x60d6109cu, "1.234e+20", "1.2340000198e+20" },
			{ 0x1fcef52du, "8.765e-20", "8.7650002873e-20" },
			{ 0x80098b53u, "-8.765e-40", "-8.76499577749e-40" },
			{ 0x7f7ffffeu, "3.4028233e+38", "3.40282326356e+38" },
			{ 0x7f7fffffu, "3.4028234e+38", "3.40282346638e+38" },
			{ 0x33d6bf94u, "9.9999994e-8", "9.9999994e-8" },
			{ 0x33d6bf96u, "1.0000001e-7", "1.0000001e-7" },
			{ 0x358637bcu, "9.999999e-7", "9.999999e-7" },
			{ 0x358637beu, "0.0000010000001", "0.0000010000001" },
			{ 0x501502f8u, "9999999000.0", "9999999000.0" },
			{ 0x501502fau, "1.0000001e+10", "1.0000001e+10" },
			// Just below powers of two (rounding carry into exponent)
			{ 0x3f7fffffu, "0.99999994", "0.99999994" },
			{ 0x3fffffffu, "1.9999999", "1.9999999" },
			{ 0x407fffffu, "3.9999998", "3.9999998" },
			{ 0x40ffffffu, "7.9999995", "7.9999995" },
			{ 0x417fffffu, "15.999999", "15.999999" },
			{ 0x427fffffu, "63.999996", "63.999996" },
			// Force-overflow parse probes: long decimals that round up at power-of-two boundaries
			{ 0x3f800000u, "1.0", "0.9999999701976776123046875" },
			{ 0x40000000u, "2.0", "1.999999940395355224609375" },
			{ 0x40800000u, "4.0", "3.99999988079071044921875" },
			{ 0x41000000u, "8.0", "7.9999997615814208984375" },
			{ 0x7f800000u, "inf", "inf" },
			{ 0x80000001u, "-1.0e-45", "-1.0e-45" },
			{ 0x80000002u, "-3.0e-45", "-3.0e-45" },
			{ 0x807ffffeu, "-1.1754941e-38", "-1.1754941e-38" },
			{ 0x807fffffu, "-1.1754942e-38", "-1.1754942e-38" },
			{ 0x80800000u, "-1.1754944e-38", "-1.1754944e-38" },
			{ 0x80800001u, "-1.1754945e-38", "-1.1754945e-38" },
			{ 0x8d2eaca7u, "-5.382571e-31", "-5.382571e-31" },
			{ 0x95ae43feu, "-7.0385313e-26", "-7.0385313e-26" },
			{ 0xb3d6bf94u, "-9.9999994e-8", "-9.9999994e-8" },
			{ 0xb3d6bf96u, "-1.0000001e-7", "-1.0000001e-7" },
			{ 0xb58637bcu, "-9.999999e-7", "-9.999999e-7" },
			{ 0xb58637beu, "-0.0000010000001", "-0.0000010000001" },
			{ 0xd01502f8u, "-9999999000.0", "-9999999000.0" },
			{ 0xd01502fau, "-1.0000001e+10", "-1.0000001e+10" },
			{ 0xeb000000u, "-1.5474251e+26", "-1.5474251e+26" },
			{ 0xd3329163u, "-7.6694336e+11", "-7.669433611830981e+11" },
			{ 0xff800000u, "-inf", "-inf" },
		};
		auto toFloatBits = [](float number) -> uint32_t {
			uint32_t bits = 0;
			std::memcpy(&bits, &number, sizeof bits);
			return bits;
		};
		for (const FragileFloatCase& entry : kFragileFloatCases) {
			float value;
			std::memcpy(&value, &entry.bits, sizeof value);
			const String text = floatToString(value);
			assert(text == entry.expected);
			const float parsed = stringToFloat(entry.expected);
			uint32_t parsedBits = toFloatBits(parsed);
			assert(parsedBits == entry.bits);
			if (entry.source && entry.source[0]) {
				const float parsedSource = stringToFloat(entry.source);
				parsedBits = toFloatBits(parsedSource);
				assert(parsedBits == entry.bits);
			}
			const float roundTrip = stringToFloat(text);
			parsedBits = toFloatBits(roundTrip);
			assert(parsedBits == entry.bits);
		}
	}


		// floatToString checks migrated into fragile table above; corresponding parse
		// checks are covered by the fragile table loop (expected/source/round-trip).
	assert(floatToString(std::numeric_limits<float>::quiet_NaN()) == "nan");
	assert(isNaN(stringToFloat("nan")));

	{
		auto toFloatBits = [](float number) -> uint32_t {
			uint32_t bits = 0; std::memcpy(&bits, &number, sizeof bits); return bits;
		};

		// Regression: decimals in the round-up band toward the smallest subnormal float
		// (2^-149 = 1.40129846e-45, bits 0x1) must round up, not flush to 0. The round-up
		// threshold is 2^-150 (~7.006e-46); anything above it rounds to 0x1, below to 0.
		// (Previously float MIN_EXPONENT of -45 flushed the whole 7e-46..1e-45 band to 0.)
		assert(toFloatBits(stringToFloat("8e-46")) == 0x00000001u);
		assert(toFloatBits(stringToFloat("7.1e-46")) == 0x00000001u);
		assert(toFloatBits(stringToFloat("9.9e-46")) == 0x00000001u);
		assert(toFloatBits(stringToFloat("1.4e-45")) == 0x00000001u);
		assert(toFloatBits(stringToFloat("6e-46")) == 0x00000000u);   // below threshold -> 0

		// Regression: pathological exponents must saturate to 0 / Inf, not wrap mod 2^32.
		// A sign-flipping wrap previously turned "1e-3000000000" into +Inf and "1e3000000000"
		// into 0. Verify for both float and double, underflow and overflow directions.
		assert(stringToFloat("1e-3000000000") == 0.0f);
		assert(stringToFloat("1e3000000000") == std::numeric_limits<float>::infinity());
		assert(stringToFloat("1e-400") == 0.0f);
		assert(stringToFloat("1e309") == std::numeric_limits<float>::infinity());
		assert(stringToFloat("1e2147483648") == std::numeric_limits<float>::infinity());
		assert(stringToFloat("1e4294967297") == std::numeric_limits<float>::infinity());
		assert(stringToDouble("1e-3000000000") == 0.0);
		assert(stringToDouble("1e3000000000") == std::numeric_limits<double>::infinity());
		assert(stringToDouble("1e-400") == 0.0);
		assert(stringToDouble("1e309") == std::numeric_limits<double>::infinity());
		assert(stringToDouble("1e2147483648") == std::numeric_limits<double>::infinity());
		assert(stringToDouble("1e4294967297") == std::numeric_limits<double>::infinity());
	}

	assert(compose(false) == "false");
	assert(compose(true) == "true");
	assert(compose(0) == "0");
	assert(compose(0xffff) == "65535");
	assert(compose(-12345) == "-12345");
	assert(compose(0x7fffffff) == "2147483647");
	assert(compose(static_cast<int32_t>(0x80000000)) == "-2147483648");
	assert(compose(0x0, true, 1) == "0x0");
	assert(compose(0xA, true) == "0x0000000a");
	assert(compose(0x7fffffff, true) == "0x7fffffff");

	{
		const std::string text("\"a\\n\\tbcdef\\rkoko\\\"\\\\'\\x23\\u0049\\U000000F2_end\"");
		Parser charParser(text);
		std::string parsed;
		assert(charParser.tryToParse(parsed));
		assert(parsed == "a\n\tbcdef\rkoko\"\\'\x23\x49\xF2_end");
	}
	{
		const std::string text("\"a\\n\\tbcdef\\rkoko\\\"\\\\'\\x23\\u4949\\U0000F232_end\"");
		Parser charParser(text);
		std::wstring parsed;
		assert(charParser.tryToParse(parsed));
		assert(parsed == L"a\n\tbcdef\rkoko\"\\'\x23\u4949\uF232_end");
	}
	{
		const std::string text("");
		Parser charParser(text);
		std::string parsed;
		assert(charParser.tryToParse(parsed));
		assert(parsed == "");
	}
	{
		Parser charParser(Element("    /*   */asdf    /*****/qwpeoi"));
		std::string parsed;
		assert(charParser.tryToParse(parsed));
		assert(parsed == "asdf qwpeoi");
	}
	{
		const std::string text("   // \n asdf  //  /** \nqwpeoi // trail");
		Parser charParser(text);
		std::string parsed;
		assert(charParser.tryToParse(parsed));
		assert(parsed == "asdf qwpeoi");
	}
	{
		const Element text("\"a\\n\\tbcdef\\rkoko\\\"\\\\'\\x23\\u0049\\U00000032_end");
		Parser charParser(text);
		std::string parsed;
		assert(!charParser.tryToParse(parsed));
		assert(charParser.getFailPoint() == text.end());
		assert(parsed == "a\n\tbcdef\rkoko\"\\'\x23\x49\x32_end");
	}
	{
		const Element text("\"a\\n\\tbcdef\\rkoko\\\"\\\\'\\x23\\u004\\U00000032_end\"");
		Parser charParser(text);
		std::string parsed;
		assert(!charParser.tryToParse(parsed));
		assert(charParser.getFailPoint() == text.begin() + 31);
		assert(parsed == "a\n\tbcdef\rkoko\"\\'\x23");
	}
	{
		const Element text("\"a\\n\\tbcdef\\rkoko\\\"\\\\'\\");
		Parser charParser(text);
		std::string parsed;
		assert(!charParser.tryToParse(parsed));
		assert(charParser.getFailPoint() == text.begin() + 23);
		assert(parsed == "a\n\tbcdef\rkoko\"\\'");
	}
	
	{
		int8_t i8;
		uint8_t ui8;
		int16_t i16;
		uint16_t ui16;
		int32_t i32;
		uint32_t ui32;
		int64_t i64;
		uint64_t ui64;

		assert(Parser(Element("0")).tryToParse(i8) && i8 == 0);
		assert(Parser(Element("0")).tryToParse(ui8) && ui8 == 0);
		assert(Parser(Element("0")).tryToParse(i16) && i16 == 0);
		assert(Parser(Element("0")).tryToParse(ui16) && ui16 == 0);
		assert(Parser(Element("0")).tryToParse(i32) && i32 == 0);
		assert(Parser(Element("0")).tryToParse(ui32) && ui32 == 0);
		assert(Parser(Element("0")).tryToParse(i64) && i64 == 0);
		assert(Parser(Element("0")).tryToParse(ui64) && ui64 == 0);

		assert(Parser(Element(" \t\n\r 1 \t\n\r ")).tryToParse(i8) && i8 == 1);
		assert(Parser(Element(" \t\n\r 1 \t\n\r ")).tryToParse(ui8) && ui8 == 1);
		assert(Parser(Element(" \t\n\r 1 \t\n\r ")).tryToParse(i16) && i16 == 1);
		assert(Parser(Element(" \t\n\r 1 \t\n\r ")).tryToParse(ui16) && ui16 == 1);
		assert(Parser(Element(" \t\n\r 1 \t\n\r ")).tryToParse(i32) && i32 == 1);
		assert(Parser(Element(" \t\n\r 1 \t\n\r ")).tryToParse(ui32) && ui32 == 1);
		assert(Parser(Element(" \t\n\r 1 \t\n\r ")).tryToParse(i64) && i64 == 1);
		assert(Parser(Element(" \t\n\r 1 \t\n\r ")).tryToParse(ui64) && ui64 == 1);

		assert(Parser(Element("127")).tryToParse(i8) && i8 == 127);
		assert(Parser(Element("-128")).tryToParse(i8) && i8 == -128);
		assert(Parser(Element("255")).tryToParse(ui8) && ui8 == 255);
		assert(Parser(Element("32767")).tryToParse(i16) && i16 == 32767);
		assert(Parser(Element("-32768")).tryToParse(i16) && i16 == -32768);
		assert(Parser(Element("65535")).tryToParse(ui16) && ui16 == 65535);
		assert(Parser(Element("2147483647")).tryToParse(i32) && i32 == 2147483647);
		assert(Parser(Element("-2147483648")).tryToParse(i32) && i32 == -2147483647 - 1);
		assert(Parser(Element("4294967295")).tryToParse(ui32) && ui32 == 4294967295U);
		assert(Parser(Element("9223372036854775807")).tryToParse(i64) && i64 == 9223372036854775807LL);
		assert(Parser(Element("-9223372036854775808")).tryToParse(i64) && i64 == -9223372036854775807LL - 1);
		assert(Parser(Element("18446744073709551615")).tryToParse(ui64) && ui64 == 18446744073709551615ULL);

		assert(Parser(Element("0x7f")).tryToParse(i8) && i8 == 127);
		assert(Parser(Element("-0x80")).tryToParse(i8) && i8 == -128);
		assert(Parser(Element("0xff")).tryToParse(ui8) && ui8 == 255);
		assert(Parser(Element("0x7fff")).tryToParse(i16) && i16 == 32767);
		assert(Parser(Element("-0x8000")).tryToParse(i16) && i16 == -32768);
		assert(Parser(Element("0xffff")).tryToParse(ui16) && ui16 == 65535);
		assert(Parser(Element("0x7fffffff")).tryToParse(i32) && i32 == 2147483647);
		assert(Parser(Element("-0x80000000")).tryToParse(i32) && i32 == -2147483647 - 1);
		assert(Parser(Element("0xffffffff")).tryToParse(ui32) && ui32 == 4294967295U);
		assert(Parser(Element("0x7fffffffffffffff")).tryToParse(i64) && i64 == 9223372036854775807LL);
		assert(Parser(Element("-0x8000000000000000")).tryToParse(i64) && i64 == -9223372036854775807LL - 1);
		assert(Parser(Element("0xffffffffffffffff")).tryToParse(ui64) && ui64 == 18446744073709551615ULL);

		assert(Parser(Element("0x0000000000000001")).tryToParse(i64) && i64 == 1LL);
		assert(Parser(Element("-0x0000000000000001")).tryToParse(i64) && i64 == -1LL);
		assert(Parser(Element("0x0000000000000001")).tryToParse(ui64) && ui64 == 1ULL);

		size_t failOffset = 0xffffffff;
		
		assert(!Parser(Element("-1")).tryToParse(ui8, failOffset) && failOffset == 0);
		assert(!Parser(Element("-1")).tryToParse(ui16, failOffset) && failOffset == 0);
		assert(!Parser(Element("-1")).tryToParse(ui32, failOffset) && failOffset == 0);
		assert(!Parser(Element("-1")).tryToParse(ui64, failOffset) && failOffset == 0);
		assert(!Parser(Element("-0x1")).tryToParse(ui8, failOffset) && failOffset == 0);
		assert(!Parser(Element("-0x1")).tryToParse(ui16, failOffset) && failOffset == 0);
		assert(!Parser(Element("-0x1")).tryToParse(ui32, failOffset) && failOffset == 0);
		assert(!Parser(Element("-0x1")).tryToParse(ui64, failOffset) && failOffset == 0);

		assert(!Parser(Element("128")).tryToParse(i8, failOffset) && failOffset == 2);
		assert(!Parser(Element("-129")).tryToParse(i8, failOffset) && failOffset == 3);
		assert(!Parser(Element("32768")).tryToParse(i16, failOffset) && failOffset == 4);
		assert(!Parser(Element("-32769")).tryToParse(i16, failOffset) && failOffset == 5);
		assert(!Parser(Element("2147483648")).tryToParse(i32, failOffset) && failOffset == 9);
		assert(!Parser(Element("-2147483649")).tryToParse(i32, failOffset) && failOffset == 10);
		assert(!Parser(Element("9223372036854775808")).tryToParse(i64, failOffset) && failOffset == 18);
		assert(!Parser(Element("-9223372036854775809")).tryToParse(i64, failOffset) && failOffset == 19);

		assert(!Parser(Element("0x80")).tryToParse(i8, failOffset) && failOffset == 3);
		assert(!Parser(Element("-0x81")).tryToParse(i8, failOffset) && failOffset == 4);
		assert(!Parser(Element("0x8000")).tryToParse(i16, failOffset) && failOffset == 5);
		assert(!Parser(Element("-0x8001")).tryToParse(i16, failOffset) && failOffset == 6);
		assert(!Parser(Element("0x80000000")).tryToParse(i32, failOffset) && failOffset == 9);
		assert(!Parser(Element("-0x80000001")).tryToParse(i32, failOffset) && failOffset == 10);
		assert(!Parser(Element("0x8000000000000000")).tryToParse(i64, failOffset) && failOffset == 17);
		assert(!Parser(Element("-0x8000000000000001")).tryToParse(i64, failOffset) && failOffset == 18);

		assert(!Parser(Element("256")).tryToParse(ui8, failOffset) && failOffset == 2);
		assert(!Parser(Element("65536")).tryToParse(ui16, failOffset) && failOffset == 4);
		assert(!Parser(Element("4294967296")).tryToParse(ui32, failOffset) && failOffset == 9);
		assert(!Parser(Element("18446744073709551616")).tryToParse(ui64, failOffset) && failOffset == 19);

		assert(!Parser(Element("0x100")).tryToParse(ui8, failOffset) && failOffset == 4);
		assert(!Parser(Element("0x10000")).tryToParse(ui16, failOffset) && failOffset == 6);
		assert(!Parser(Element("0x100000000")).tryToParse(ui32, failOffset) && failOffset == 10);
		assert(!Parser(Element("0x10000000000000000")).tryToParse(ui64, failOffset) && failOffset == 18);

		assert(!Parser(Element("-")).tryToParse(i32, failOffset) && failOffset == 1);
		assert(!Parser(Element("- 128")).tryToParse(i32, failOffset) && failOffset == 1);
		assert(!Parser(Element("-00")).tryToParse(i32, failOffset) && failOffset == 2);
		assert(!Parser(Element("+-0")).tryToParse(i32, failOffset) && failOffset == 1);
		assert(!Parser(Element("+1234x")).tryToParse(i32, failOffset) && failOffset == 5);
	}
	
	{
		const std::string structString("   \n \t  /* /*   */ */ \n   // /*  \n { /* /* \n */ */ }    /* // */ //");
		Parser structParser(structString);
		Struct structure;
		assert(structParser.tryToParse(structure));
		assert(structure.empty());
	}
	{
		const std::string structString("   \n \t  /* /*   */ */ \n   // /*  \n { /* /* \n */ */ }    /* // */ //");
		Parser structParser(structString);
		WideStruct structure;
		assert(structParser.tryToParse(structure));
		assert(structure.empty());
	}

	{
		const std::string structString("   \n \t  /* /*   */ */ \n   // /*  \n { /* /* \n */ */  x /**/ :  23/*  */ 666 }    /* // */ //");
		Parser structParser(structString);
		Struct structure;
		assert(structParser.tryToParse(structure));
		assert(structure.size() == 1);
		assert(structure["x"].to<std::string>() == "23 666");
	}
	{
		const std::string structString("   \n \t  /* /*   */ */ \n   // /*  \n { /* /* \n */ */  x /**/ :  23/*  */ 666 }    /* // */ //");
		Parser structParser(structString);
		WideStruct structure;
		assert(structParser.tryToParse(structure));
		assert(structure.size() == 1);
		assert(structure[L"x"].to<std::string>() == "23 666");
	}

	{
		const std::string structString("   \n \t  /* /*   */ */ \n   // /*  \n { /* /* \n */ */  x /**/ :  23/*  */ 666   ,  '  y  ' : 'asfd' \n\n,\nz:'qwer'\naaa:bbb\nq:\nw: }    /* // */ //");
		Parser structParser(structString);
		Struct structure;
		assert(structParser.tryToParse(structure));
		assert(structure.size() == 6);
		assert(structure["x"].to<std::string>() == "23 666");
		assert(structure["  y  "].to<std::string>() == "asfd");
		assert(structure["z"].to<std::string>() == "qwer");
		assert(structure["aaa"].to<std::string>() == "bbb");
		assert(structure["q"].to<std::string>() == "");
		assert(structure["w"].to<std::string>() == "");
	}
	{
		const std::string structString("   \n \t  /* /*   */ */ \n   // /*  \n { /* /* \n */ */  x /**/ :  23/*  */ 666   ,  '  y  ' : 'asfd' \n\n,\nz:'qwer'\n\"\\u0074\\u20AC\\u00E4\\u0073\\u0074\":bbb\nq:\nw: }    /* // */ //");
		Parser structParser(structString);
		WideStruct structure;
		assert(structParser.tryToParse(structure));
		assert(structure.size() == 6);
		assert(structure[L"x"].to<std::string>() == "23 666");
		assert(structure[L"  y  "].to<std::string>() == "asfd");
		assert(structure[L"z"].to<std::string>() == "qwer");
			assert(structure[L"t\u20AC\u00E4st"].to<std::string>() == "bbb");
		assert(structure[L"q"].to<std::string>() == "");
		assert(structure[L"w"].to<std::string>() == "");
	}

	{
		const Element structString("   \n \t  /* /*   */ */ \n   // /*  \n { /* /* \n */ */  x  : /* 23/*  */ 666   ,  '  y  ' : 'asfd' \n\n,\nz:'qwer'\naaa:bbb }    /* // */ //");
		Parser structParser(structString);
		Struct structure;
		assert(!structParser.tryToParse(structure));
		assert(structure.size() == 1);
		assert(structure["x"].to<std::string>() == "");
		assert(structParser.getFailPoint() == structString.end());
	}

	{
		const Element structString("a:3,a:4");
		Parser structParser(structString);
		Struct structure;
		assert(!structParser.tryToParse(structure));
		assert(structure.size() == 1);
		assert(structure["a"].to<std::string>() == "3");
		assert(structParser.getFailPoint() == structString.begin() + 4);
	}

	{
		const Element structString("a:3 a:4");
		Parser structParser(structString);
		Struct structure;
		assert(!structParser.tryToParse(structure));
		assert(structure.size() == 1);
		assert(structure["a"].to<std::string>() == "3 a");
		assert(structParser.getFailPoint() == structString.begin() + 5);
	}

	{
		const Element structString("a:3,:4");
		Parser structParser(structString);
		Struct structure;
		assert(!structParser.tryToParse(structure));
		assert(structure.size() == 1);
		assert(structure["a"].to<std::string>() == "3");
		assert(structParser.getFailPoint() == structString.begin() + 4);
	}

	{
		const std::string structString("x: { /* }}    */ // asdf } \n 'asdf}\\'}': , qwe:{   } }");
		Parser structParser(structString);
		Struct structure;
		assert(structParser.tryToParse(structure));
		assert(structure.size() == 1);
		assert(structure["x"].code() == "{ /* }}    */ // asdf } \n 'asdf}\\'}': , qwe:{   } }");

		Parser structParser2(structure["x"]);
		Struct structure2;
		assert(structParser2.tryToParse(structure2));
		assert(structure2.size() == 2);
		assert(structure2["asdf}'}"].to<std::string>() == "");
		assert(structure2["qwe"].code() == "{   }");
	}
	
	{
		const std::string arrayString("  {  one  ,  two,three  , , five\nsix,\nseven\n\n,\n\n\teight\n\n,\n\n}   ");
		Parser arrayParser(arrayString);
		Array array;
		assert(arrayParser.tryToParse(array));
		static const char* correct[8] = { "one", "two", "three", "", "five", "six", "seven", "eight" };
		assert(array.size() == 8);
		for (Array::const_iterator it = array.begin(); it != array.end(); ++it) {
			assert(it->code() == correct[it - array.begin()]);
		}
	}

	{
		assert(compose(static_cast<int8_t>(0)) == "0");
		assert(compose(static_cast<uint8_t>(0)) == "0");
		assert(compose(static_cast<int16_t>(0)) == "0");
		assert(compose(static_cast<uint16_t>(0)) == "0");
		assert(compose(static_cast<int32_t>(0)) == "0");
		assert(compose(static_cast<uint32_t>(0)) == "0");
		assert(compose(static_cast<int64_t>(0)) == "0");
		assert(compose(static_cast<uint64_t>(0)) == "0");
		assert(compose(static_cast<int8_t>(-0x80)) == "-128");
		assert(compose(static_cast<int8_t>(-0x7f)) == "-127");
		assert(compose(static_cast<int8_t>(0x7f)) == "127");
		assert(compose(static_cast<uint8_t>(0xff)) == "255");
		assert(compose(static_cast<int16_t>(-0x8000)) == "-32768");
		assert(compose(static_cast<int16_t>(-0x7fff)) == "-32767");
		assert(compose(static_cast<int16_t>(0x7fff)) == "32767");
		assert(compose(static_cast<uint16_t>(0xffff)) == "65535");
		assert(compose(static_cast<int32_t>(-0x7fffffff - 1)) == "-2147483648");
		assert(compose(static_cast<int32_t>(-0x7fffffff)) == "-2147483647");
		assert(compose(static_cast<int32_t>(0x7fffffff)) == "2147483647");
		assert(compose(static_cast<uint32_t>(0xffffffffu)) == "4294967295");
		assert(compose(static_cast<int64_t>(-0x8000000000000000LL)) == "-9223372036854775808");
		assert(compose(static_cast<int64_t>(-0x7fffffffffffffffLL)) == "-9223372036854775807");
		assert(compose(static_cast<int64_t>(0x7fffffffffffffffLL)) == "9223372036854775807");
		assert(compose(static_cast<uint64_t>(0xffffffffffffffffULL)) == "18446744073709551615");

		assert(compose(static_cast<int8_t>(0), true) == "0x00");
		assert(compose(static_cast<uint8_t>(0), true) == "0x00");
		assert(compose(static_cast<int16_t>(0), true) == "0x0000");
		assert(compose(static_cast<uint16_t>(0), true) == "0x0000");
		assert(compose(static_cast<int32_t>(0), true) == "0x00000000");
		assert(compose(static_cast<uint32_t>(0), true) == "0x00000000");
		assert(compose(static_cast<int64_t>(0), true) == "0x0000000000000000");
		assert(compose(static_cast<uint64_t>(0), true) == "0x0000000000000000");
		assert(compose(static_cast<int8_t>(-0x80), true) == "-0x80");
		assert(compose(static_cast<int8_t>(-0x7f), true) == "-0x7f");
		assert(compose(static_cast<int8_t>(0x7f), true) == "0x7f");
		assert(compose(static_cast<uint8_t>(0xff), true) == "0xff");
		assert(compose(static_cast<int16_t>(-0x8000), true) == "-0x8000");
		assert(compose(static_cast<int16_t>(-0x7fff), true) == "-0x7fff");
		assert(compose(static_cast<int16_t>(0x7fff), true) == "0x7fff");
		assert(compose(static_cast<uint16_t>(0xffff), true) == "0xffff");
		assert(compose(static_cast<int32_t>(-0x7fffffff - 1), true) == "-0x80000000");
		assert(compose(static_cast<int32_t>(-0x7fffffff), true) == "-0x7fffffff");
		assert(compose(static_cast<int32_t>(0x7fffffff), true) == "0x7fffffff");
		assert(compose(static_cast<uint32_t>(0xffffffffu), true) == "0xffffffff");
		assert(compose(static_cast<int64_t>(-0x8000000000000000LL), true) == "-0x8000000000000000");
		assert(compose(static_cast<int64_t>(-0x7fffffffffffffffLL), true) == "-0x7fffffffffffffff");
		assert(compose(static_cast<int64_t>(0x7fffffffffffffffLL), true) == "0x7fffffffffffffff");
		assert(compose(static_cast<uint64_t>(0xffffffffffffffffULL), true) == "0xffffffffffffffff");
	}
	
	{
		Array vec;
		assert(compose(vec) == "{ }");
		assert(compose(vec, true) == "{\n}");
		vec.push_back(compose("first string"));
		assert(compose(vec) == "{ \"first string\" }");
		assert(compose(vec, true) == "{\n\t\"first string\"\n}");
		vec.push_back(compose("second string", true));
		assert(compose(vec) == "{ \"first string\", \"second string\" }");
		assert(compose(vec, true) == "{\n\t\"first string\",\n\t\"second string\"\n}");
		vec.push_back(compose("", true));
		assert(compose(vec) == "{ \"first string\", \"second string\", , }");
		assert(compose(vec, true) == "{\n\t\"first string\",\n\t\"second string\",\n\t,\n}");
		vec.push_back(compose("last string"));
		assert(compose(vec) == "{ \"first string\", \"second string\", , \"last string\" }");
		assert(compose(vec, true) == "{\n\t\"first string\",\n\t\"second string\",\n\t,\n\t\"last string\"\n}");
	}

	{
		Struct map;
		assert(compose(map, false) == "{ : }");
		assert(compose(map, true) == "{\n\t:\n}");
		map.insert(std::make_pair("abc", compose("tiktok")));
		assert(compose(map, false) == "{ abc: \"tiktok\" }");
		assert(compose(map, true) == "{\n\tabc: \"tiktok\"\n}");
		map.insert(std::make_pair("def", compose("plipplop")));
		assert(compose(map, false) == "{ abc: \"tiktok\", def: \"plipplop\" }");
		assert(compose(map, true) == "{\n\tabc: \"tiktok\"\n\tdef: \"plipplop\"\n}");
	}

	{
		Variant v;
		Variant w;

		v.type = Variant::STRUCT;
		assert(compose(v) == "{ : }");
		v.structure.insert(std::make_pair(L"abc", compose("tiktok")));
		assert(compose(v) == "{ abc: \"tiktok\" }");
		w = Element("{ abc: \"tiktok\" }").to<Variant>();
		assert(w.type == Variant::STRUCT && w.structure.size() == 1);

		v.type = Variant::ARRAY;
		assert(compose(v) == "{ }");
		v.array.push_back(compose(1234.5678));
		v.array.push_back(compose(-5984));
		assert(compose(v) == "{ 1234.5678, -5984 }");
		w = Element("{ 1234.5678, -5984 }").to<Variant>();
		assert(w.type == Variant::ARRAY && w.array.size() == 2);

		v.type = Variant::TEXT;
		v.text = L"arbitrary thing";
		assert(compose(v) == "\"arbitrary thing\"");
		w = Element("\"arbitrary thing\"").to<Variant>();
		assert(w.type == Variant::TEXT && w.text == L"arbitrary thing");

		v.type = Variant::REAL;
		v.real = 12345.678;
		assert(compose(v) == "12345.678");
		w = Element("12345.678").to<Variant>();
		assert(w.type == Variant::REAL && w.real == 12345.678);

		v.type = Variant::INTEGER;
		v.integer = 12345678;
		assert(compose(v) == "12345678");
		w = Element("12345678").to<Variant>();
		assert(w.type == Variant::INTEGER && w.integer == 12345678);

		v.type = Variant::UNSIGNED_INTEGER;
		v.unsignedInteger = 0xeac0bff359aefc59ULL;
		assert(compose(v) == "0xeac0bff359aefc59");
		w = Element("0xeac0bff359aefc59").to<Variant>();
		assert(w.type == Variant::UNSIGNED_INTEGER && w.unsignedInteger == 0xeac0bff359aefc59ULL);
	}
#endif

	return true;
}

} // namespace Numbstrict

#ifdef REGISTER_UNIT_TEST
REGISTER_UNIT_TEST(Numbstrict::unitTest)
#endif

#ifdef __GNUC__
#ifndef __clang__
	#pragma GCC pop_options
#endif
#endif
