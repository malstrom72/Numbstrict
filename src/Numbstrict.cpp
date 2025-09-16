#ifdef __GNUC__
#ifndef __clang__
	#pragma GCC push_options
	#pragma GCC optimize ("no-finite-math-only")
	#pragma GCC optimize ("float-store")
#endif
#endif

#ifdef _MSC_VER
	#pragma float_control(precise, on)
	#pragma float_control(except, off)
	#pragma fenv_access(on)
	#pragma fp_contract(off)
#endif

#ifdef __FAST_MATH__
	//#error This code requires IEEE compliant floating point handling. Avoid -Ofast / -ffast-math etc (at least for this source file).
#endif

#include "assert.h"
#include <sstream>
#include <cmath>
#include <cfenv>
#include <limits>
#include <algorithm>
#include <iostream>
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

static const Char* parseUnsignedInt(const Char* p, const Char* e, unsigned int& i) {
	for (i = 0; p != e && *p >= '0' && *p <= '9'; ++p) {
		i = i * 10 + (*p - '0');
	}
	return p;
}

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>   // _mm_getcsr/_mm_setcsr
#include <float.h>       // _control87 on MSVC
#endif

class StandardFPEnvScope {
public:
	StandardFPEnvScope() {
	#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
		const unsigned int COMMON = _EM_INEXACT|_EM_UNDERFLOW|_EM_OVERFLOW|_EM_ZERODIVIDE|_EM_INVALID|_EM_DENORMAL|_RC_NEAR;
		unsigned int cur;
	#if defined(_M_IX86)
		{ int ok = __control87_2(0,0,&prevX87_,&prevMXCSR_); assert(ok); unsigned int t; ok = __control87_2(COMMON|_PC_53, _MCW_EM|_MCW_RC|_MCW_PC, &t, 0); assert(ok); }
		cur = prevMXCSR_;
	#else
		prevMXCSR_ = _mm_getcsr(); cur = prevMXCSR_; prevX87_ = _control87(0,0); _control87(COMMON, _MCW_EM|_MCW_RC);
	#endif
		cur &= ~(_MM_FLUSH_ZERO_MASK|_MM_DENORMALS_ZERO_MASK);
		cur = (cur & ~_MM_ROUND_MASK) | _MM_ROUND_NEAREST;
		_mm_setcsr(cur);
	#elif defined(__aarch64__)
		int r; r = fegetenv(&prevEnv_); assert(r==0); r = fesetenv(FE_DFL_ENV); assert(r==0); feholdexcept(&dummyEnv_);
	#if defined(__has_builtin) && __has_builtin(__builtin_aarch64_get_fpcr) && __has_builtin(__builtin_aarch64_set_fpcr)
		prevFPCR_ = __builtin_aarch64_get_fpcr(); unsigned long long cur = prevFPCR_; cur &= ~(1ull<<24); cur &= ~(3ull<<22); __builtin_aarch64_set_fpcr(cur);
	#else
		asm volatile("mrs %0, fpcr" : "=r"(prevFPCR_)); unsigned long long cur = prevFPCR_; cur &= ~(1ull<<24); cur &= ~(3ull<<22); asm volatile("msr fpcr, %0" :: "r"(cur));
	#endif
	#elif defined(__arm__)
		int r; r = fegetenv(&prevEnv_); assert(r==0); r = fesetenv(FE_DFL_ENV); assert(r==0); feholdexcept(&dummyEnv_);
		asm volatile("vmrs %0, fpscr" : "=r"(prevFPSCR_)); unsigned int cur = prevFPSCR_; cur &= ~(1u<<24); cur &= ~(3u<<22); asm volatile("vmsr fpscr, %0" :: "r"(cur));
	#else
		int r; r = fegetenv(&prevEnv_); assert(r==0); r = fesetenv(FE_DFL_ENV); assert(r==0); feholdexcept(&dummyEnv_);
	#endif
	}
	~StandardFPEnvScope() {
	#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
	#if defined(_M_IX86)
		{ unsigned int t; __control87_2(prevX87_, _MCW_EM|_MCW_RC|_MCW_PC, &t, 0); }
	#else
		_control87(prevX87_, _MCW_EM|_MCW_RC);
	#endif
		_mm_setcsr(prevMXCSR_);
	#elif defined(__aarch64__)
	#if defined(__has_builtin) && __has_builtin(__builtin_aarch64_set_fpcr)
		__builtin_aarch64_set_fpcr(prevFPCR_);
	#else
		asm volatile("msr fpcr, %0" :: "r"(prevFPCR_));
	#endif
		fesetenv(&prevEnv_);
	#elif defined(__arm__)
		asm volatile("vmsr fpscr, %0" :: "r"(prevFPSCR_)); fesetenv(&prevEnv_);
	#else
		fesetenv(&prevEnv_);
	#endif
	}
private:
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
	unsigned int prevX87_, prevMXCSR_;
#elif defined(__aarch64__)
	fenv_t prevEnv_, dummyEnv_;
	unsigned long long prevFPCR_;
#elif defined(__arm__)
	fenv_t prevEnv_, dummyEnv_;
	unsigned int prevFPSCR_;
#else
	fenv_t prevEnv_, dummyEnv_;
#endif
};

const int NEGATIVE_E_NOTATION_START = -6;
const int POSITIVE_E_NOTATION_START = 10;

/*
	Helper class for high-precision double <=> string conversion routines. 52*2 bits of two doubles allows accurate
	representation of integers between 0 and 81129638414606681695789005144064.
*/
struct DoubleDouble {
	DoubleDouble() { }
	DoubleDouble(double d) : high(floor(d)), low(d - high) { }
	DoubleDouble(double high, double low) : high(high), low(low) {
		assert(high < ldexp(1.0, 53));
		assert(low < 1.0);
	}
	DoubleDouble operator+(const DoubleDouble& other) const {
		const double lowSum = low + other.low;
		const double overflow = floor(lowSum);
		return DoubleDouble((high + other.high) + overflow, lowSum - overflow);
	}
	DoubleDouble operator*(int factor) const {
		const double lowTimesFactor = low * factor;
		const double overflow = floor(lowTimesFactor);
		return DoubleDouble((high * factor) + overflow, lowTimesFactor - overflow);
	}
	DoubleDouble operator/(int divisor) const {
		const double floored = floor(high / divisor);
		const double remainder = high - floored * divisor;
		return DoubleDouble(floored, (low + remainder) / divisor);
	}
	bool operator<(const DoubleDouble& other) const {
		return high < other.high || (high == other.high && low < other.low);
	}
	operator double() const {
		return high + low;
	}
	double high;
	double low;
};

static DoubleDouble multiplyAndAdd(const DoubleDouble& term, const DoubleDouble& factorA, double factorB) {
	const double fmaLow = factorA.low * factorB + term.low;
	const double overflow = floor(fmaLow);
	return DoubleDouble(factorA.high * factorB + term.high + overflow, fmaLow - overflow);
}

static double multiplyAndAdd(double term, double factorA, double factorB) {
	return term + factorA * factorB;
}

static float scaleAndConvert(double factorA, double factorB) {
	return static_cast<float>(factorA * factorB);
}

/**
	If we just do (high + low) first, that sum is rounded to 53 bits once, possibly nudging the result slightly upward.
	Then when we scale down into the subnormal range (right-shift the mantissa) we hit what looks like an exact halfway
	case — and since the current mantissa is odd, IEEE-754 rounds up again. In reality, the exact (high+low) value was
	just below that halfway point, so it should have rounded down to the even mantissa. This is a classic "double
	rounding" problem.

	scaleAndConvert avoids this by combining high and low at full precision under the final exponent window and
	performing a *single* correct round-to-nearest-even step. This matches the Decimal oracle and fixes all denormal
	boundary mismatches.

	Assumptions:
	- 'factor' is an exact power-of-two (normal or subnormal) from the table.
	- 'acc.high' is integral in [0, 2^53) and 'acc.low' ∈ [0,1).
	- Table ensures factorExponent >= -1073 so T = factorExponent + 1073 >= 0.
**/
static double scaleAndConvert(const DoubleDouble& acc, double factor)
{
    if (acc.high == 0.0 && acc.low == 0.0) {
    	return 0.0;
	}
	
	const double fastResult = (acc.high + acc.low) * factor;
	if (fastResult >= 2.2250738585072014e-308) {
		return fastResult;										// Normal result; fast path is exact here.
	}
	
	// Slow path: denormal/transition region — assemble payload then single rounding.
    int factorExponent, highExponent;
    frexp(factor, &factorExponent);
    frexp(acc.high, &highExponent);								// unbiased exponent of acc.high
	
    const int T = factorExponent + 1073;
	assert(T >= 0);												// Guaranteed by table construction (no right-shift branch needed).
	
	// Align (high, low) into the 52-bit subnormal payload scale, then round-to-nearest-even.
	const double Bf = ldexp(acc.low, T);						// fractional contribution
	const double Bi = floor(Bf);
	const double fraction = Bf - Bi;

	// Integer payload (exact in double on this path)
	double Ni = ldexp(acc.high, T) + Bi;

	// Round to nearest, ties-to-even
	if (fraction > 0.5 || (fraction == 0.5 && fmod(Ni, 2.0) != 0.0)) {
		Ni += 1.0;
	}

	// Subnormal construction (and transition to DBL_MIN when Ni == 2^52)
	return ldexp(Ni, -1074);
}

template<typename T> struct Traits { };

template<> struct Traits<double> {
	enum { MIN_EXPONENT = -324, MAX_EXPONENT = 308 };
	typedef DoubleDouble Hires;
};

template<> struct Traits<float> {
	enum { MIN_EXPONENT = -45, MAX_EXPONENT = 38 };
	typedef double Hires;
};

/*
	Generate a table of `DoubleDoubles` for all powers of 10 from -324 to 308. The `DoubleDoubles` are normalized to
	take up as many bits as possible while leaving enough headroom to allow multiplications of up to 10 without
	overflowing. The exp10Factors array will contain the multiplication factors required to revert the normalization.
	I.e. `static_cast<double>(normals[1 - (-324)]) * factors[1 - (-324)] == 10.0`. Notice that for the very lowest
	exponents we refrain from normalizing to correctly convert denormal floating point values.
*/
struct Exp10Table {
	Exp10Table() {
		StandardFPEnvScope standardFPEnv;
		
		const double WIDTH = ldexp(1.0, 53 - 4);

		DoubleDouble normal(WIDTH, 0.0);
		double factor = 1.0 / WIDTH;
		for (int i = 0; i <= Traits<double>::MAX_EXPONENT; ++i) {
			if (normal.high >= WIDTH) {
				factor *= 16.0;
				normal = normal / 16;
			}
			assert(factor < std::numeric_limits<double>::infinity());
			normals[i - Traits<double>::MIN_EXPONENT] = normal;
			factors[i - Traits<double>::MIN_EXPONENT] = factor;
			normal = normal * 10;
		}
		
		normal = DoubleDouble(WIDTH, 0.0);
		factor = 1.0 / WIDTH;
		for (int i = -1; i >= Traits<double>::MIN_EXPONENT; --i) {
			// Check factor / 16.0 > 0.0 to avoid normalizing denormal exponents.
			if (normal.high < WIDTH && factor / 16.0 > 0.0) {
				factor /= 16.0;
				normal = normal * 16;
			}
			normal = normal / 10;
			normals[i - Traits<double>::MIN_EXPONENT] = normal;
			factors[i - Traits<double>::MIN_EXPONENT] = factor;
		}
	}
	DoubleDouble normals[Traits<double>::MAX_EXPONENT + 1 - Traits<double>::MIN_EXPONENT];
	double factors[Traits<double>::MAX_EXPONENT + 1 - Traits<double>::MIN_EXPONENT];
} EXP10_TABLE;

template<typename T> const Char* parseReal(const Char* const b, const Char* const e, T& value) {
	StandardFPEnvScope standardFPEnv;
	
	int exponent = -1;
	T sign = (T)(1.0);
	const Char* significandBegin = b;
	const Char* numberEnd;

	const Char* p = b;
	if (p != e && (*p == '-' || *p == '+')) {
		sign = (*p == '-' ? (T)(-1.0) : (T)(1.0));
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
			value = (T)(0.0);
			return b;
		}

		const Char* significandEnd = p;
		numberEnd = p;
		
		if (e - p >= 2 && (*p == 'e' || *p == 'E')) {
			++p;
			int sign = (*p == '-' ? -1 : 1);
			if (*p == '+' || *p == '-') {
				++p;
			}
			unsigned int ui;
			const Char* q = parseUnsignedInt(p, e, ui);
			if (q != p) {
				exponent += sign * rewrap<int>(ui);
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
		
		if (p == significandEnd || exponent < Traits<T>::MIN_EXPONENT) {
			value = (T)(0.0);
		} else if (exponent > Traits<T>::MAX_EXPONENT) {
			value = std::numeric_limits<T>::infinity();
		} else {
			assert(Traits<double>::MIN_EXPONENT <= exponent && exponent <= Traits<double>::MAX_EXPONENT);
			typename Traits<T>::Hires magnitude = EXP10_TABLE.normals[exponent - Traits<double>::MIN_EXPONENT];
			typename Traits<T>::Hires accumulator(0.0);
			while (p != significandEnd) {
				if (*p != '.') {
					accumulator = multiplyAndAdd(accumulator, magnitude, (*p - '0'));
					magnitude = magnitude / 10;
				}
				++p;
			}
			value = scaleAndConvert(accumulator, EXP10_TABLE.factors[exponent - Traits<double>::MIN_EXPONENT]);
		}
	}
	value *= sign;
	return numberEnd;
}

template<typename T> Char* realToString(Char buffer[32], const T value) {
	StandardFPEnvScope standardFPEnv;
	
	Char* p = buffer;

	T absValue = value;
	if (value < 0) {
		*p++ = '-';
		absValue = -value;
	}
	
	if (isNaN(absValue)) {
		strcpy(p, "nan");
		return p + 3;
	} else if (absValue == 0.0) {
		strcpy(p, "0.0");
		return p + 3;
	} else if (absValue >= std::numeric_limits<T>::infinity()) {
		strcpy(p, "inf");
		return p + 3;
	}

	// frexp is fast and precise and gives log2(x), log10(x) = log2(x) / log2(10)
	int base2Exponent;
	(void) frexp(absValue, &base2Exponent);
	int exponent = std::max(static_cast<int>(ceil(0.30102999566398119521 * (base2Exponent - 1))) - 1
			, static_cast<int>(Traits<T>::MIN_EXPONENT));
	if (exponent < Traits<T>::MAX_EXPONENT) {
		assert(Traits<double>::MIN_EXPONENT <= exponent + 1 && exponent + 1 <= Traits<double>::MAX_EXPONENT);
		const double factor = EXP10_TABLE.factors[exponent + 1 - Traits<double>::MIN_EXPONENT];
		const double magnitude = static_cast<double>(EXP10_TABLE.normals[exponent + 1 - Traits<double>::MIN_EXPONENT]);

		// Notice that in theory we could have a value that is considered equal to next magnitude but should be rounded
		// downwards (to a lower exponential) and not upwards. However in reality, only the first denormal power of 10
		// would be a candidate for this, and for both double and single precision floats, they round upwards.
		if (absValue >= static_cast<T>(magnitude * factor)) {
			++exponent;
		}
	}
	
	const bool eNotation = (exponent < NEGATIVE_E_NOTATION_START || exponent >= POSITIVE_E_NOTATION_START);
	Char* periodPosition = p + (eNotation || exponent < 0 ? 0 : exponent) + 1;
	if (!eNotation && exponent < 0) {
		*p++ = '0';
		*p++ = '.';
		while (p < periodPosition - exponent) {
			*p++ = '0';
		}
	}

	assert(Traits<double>::MIN_EXPONENT <= exponent && exponent <= Traits<double>::MAX_EXPONENT);
	const double factor = EXP10_TABLE.factors[exponent - Traits<double>::MIN_EXPONENT];
	typename Traits<T>::Hires magnitude = EXP10_TABLE.normals[exponent - Traits<double>::MIN_EXPONENT];
	const typename Traits<T>::Hires normalized = absValue / factor;
	typename Traits<T>::Hires accumulator = 0.0;
	T reconstructed;
	do {
		if (p == periodPosition) {
			*p++ = '.';
		}
		
		// Incrementally find the max digit that keeps accumulator < normalized target (instead of using division).
		typename Traits<T>::Hires next = accumulator + magnitude;
		int digit = 0;
		while (next < normalized && digit < 9) {
			accumulator = next;
			next = next + magnitude;
			++digit;
		}

		// Correct behavior is to never reach higher than digit 9.
		assert(next >= normalized);
		
		// Do we hit goal with digit or digit + 1?
		reconstructed = static_cast<T>(scaleAndConvert(accumulator, factor));
		if (reconstructed != absValue) {
			reconstructed = static_cast<T>(scaleAndConvert(accumulator + magnitude, factor));
		}

		// Finally, is next digit >= 5 (magnitude / 2) then increment it (unless we are at max, just to play nicely with
		// poorer parsers).
		if (reconstructed == absValue && accumulator + magnitude / 2 < normalized && absValue != std::numeric_limits<T>::max()) {
			++digit;

			// If this happens we have failed to calculate the correct exponent above.
			assert(digit < 10);
		} else {
			// If this happens we have failed to calculate the correct exponent above.
			assert(accumulator > 0.0);
		}
		
		*p++ = '0' + digit;
		magnitude = magnitude / 10;
		
		// p < buffer + 27 is an extra precaution if the correct value is never reached (e.g. because of too aggressive
		// optimizations). 27 leaves room for longest exponent.
	} while (p < buffer + 27 && reconstructed != absValue);
	
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
			*--q = "0123456789"[x % 10];
		} while ((x /= 10) != 0);
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
	} else {
		if (!keyValueElements(elements)) {
			return false;
		}
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
	} else {
		if (!valueListElements(elements)) {
			return false;
		}
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

	assert(floatToString(0.0f) == "0.0");
	assert(stringToFloat("0.0") == 0.0f);
	assert(floatToString(-1.0f) == "-1.0");
	assert(stringToFloat("-1.0") == -1.0f);
	assert(floatToString(1.12345683575f) == "1.1234568");
	assert(stringToFloat("1.1234568") == 1.12345683575f);
	assert(floatToString(1000.0f) == "1000.0");
	assert(stringToFloat("1000.0") == 1000.0f);
	assert(floatToString(123456792.0f) == "123456790.0");
	assert(stringToFloat("123456790.0") == 123456792.0f);
	assert(floatToString(1.2340000198e+20f) == "1.234e+20");
	assert(stringToFloat("1.234e+20") == 1.2340000198e+20f);
	assert(floatToString(8.7650002873e-20f) == "8.765e-20");
	assert(stringToFloat("8.765e-20") == 8.7650002873e-20f);
	assert(floatToString(-8.76499577749e-40f) == "-8.765e-40");
	assert(stringToFloat("-8.765e-40") == -8.76499577749e-40f);
	assert(floatToString(1.40129846432e-45f) == "1.0e-45");
	assert(stringToFloat("1.0e-45") == 1.40129846432e-45f);
	assert(floatToString(2.80259692865e-45f) == "3.0e-45");
	assert(stringToFloat("3.0e-45") == 2.80259692865e-45f);
	assert(floatToString(3.40282326356e+38f) == "3.4028233e+38");
	assert(stringToFloat("3.4028233e+38") == 3.40282326356e+38f);
	assert(floatToString(3.40282346638e+38f) == "3.4028234e+38");
	assert(stringToFloat("3.4028234e+38") == 3.40282346638e+38f);
	assert(floatToString(std::numeric_limits<float>::infinity()) == "inf");
	assert(stringToFloat("inf") == std::numeric_limits<float>::infinity());
	assert(floatToString(std::numeric_limits<float>::quiet_NaN()) == "nan");
	assert(isNaN(stringToFloat("nan")));

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
