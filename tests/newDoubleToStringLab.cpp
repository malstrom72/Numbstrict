#ifdef __GNUC__
	#pragma GCC push_options
	#pragma GCC optimize ("no-finite-math-only")
	#pragma GCC optimize ("float-store")
#endif

#ifdef _MSC_VER
	#pragma float_control(precise, on, push)
#endif

#ifdef __FAST_MATH__
	//#error This code requires IEEE compliant floating point handling. Avoid -Ofast / -ffast-math etc (at least for this source file).
#endif

#include "assert.h"
#include <algorithm>
#include <iostream>
#include <string>
#include <cmath>
#include <cstring>

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

typedef char Char;
typedef std::string String;


const int NEGATIVE_E_NOTATION_START = -6;
const int POSITIVE_E_NOTATION_START = 10;

/*
	Support class for high-precision double <=> string conversion routines. 52*2 bits of two doubles allows accurate
	representation of integers between 0 and 81129638414606681695789005144064.
*/
struct DoubleDouble {
	DoubleDouble() { }
	DoubleDouble(double d) : high(floor(d)), low(d - high) { }
	DoubleDouble(double high, double low) : high(high), low(low) {
		assert(high < ldexp(1, 53));
		assert(low < 1.0);
	}
	DoubleDouble operator+(const DoubleDouble& other) {
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

template<typename T> struct Traits { };

template<> struct Traits<double> {
	enum { MIN_EXPONENT = -324, MAX_EXPONENT = 308 };
	typedef DoubleDouble Hires;
};

template<> struct Traits<float> {
	enum { MIN_EXPONENT = -45, MAX_EXPONENT = 38 };
	typedef double Hires;
};

struct Exp10Table {
	Exp10Table() {
		/*
			Generate a table of `DoubleDoubles` for all powers of 10 from -324 to 308. The `DoubleDoubles` are
			normalized to take up as many bits as possible while leaving enough headroom to allow multiplications of up
			to 10 without overflowing. The exp10Factors array will contain the multiplication factors required to
			revert the normalization. I.e. `static_cast<double>(normals[1 - (-324)]) * factors[1 - (-324)] == 10.0`.
			Notice that for the very lowest exponents we refrain from normalizing to correctly convert denormal floating
			point values.
		*/
		const double WIDTH = ldexp(1, 53 - 4);

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

template<typename C> C wrap(uint32_t ui) {
    const uint32_t HALF_MAX = 1U << (sizeof (C) * 8 - 1);
    const C QUARTER_MAX = static_cast<C>(HALF_MAX >> 1);
    ui &= ((HALF_MAX - 1) << 1) | 1;
	if (static_cast<C>(-1) >= 0) {
		// C is unsigned type. No problem.
		return static_cast<C>(ui);
	} else {
		// Cast from unsigned to signed is undefined in C. Optimizers might not wrap values as expected.
		return (ui < HALF_MAX ? static_cast<C>(ui) : static_cast<C>(ui - HALF_MAX) - QUARTER_MAX - QUARTER_MAX);
	}
}

// Returns pointer to beginning of string. End of string is always buffer + 32 (*not* zero-terminated by this routine).
static Char* intToString(Char buffer[32], int i) {
	Char* p = buffer + 32;
	int x = i;
	do {
		*--p = "9876543210123456789"[9 + x % 10];		// Mirrored string to handle negative x.
	} while ((x /= 10) != 0);
	if (i < 0) {
		*--p = '-';
	}
	assert(p >= buffer);
	return p;
}

static const Char* parseUnsignedInt(const Char* p, const Char* e, unsigned int& i) {
	for (i = 0; p != e && *p >= '0' && *p <= '9'; ++p) {
		i = i * 10 + (*p - '0');
	}
	return p;
}

template<typename T> const Char* parseReal(const Char* const b, const Char* const e, T& value) {
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
				exponent += sign * wrap<int>(ui);
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
			const double factor = EXP10_TABLE.factors[exponent - Traits<double>::MIN_EXPONENT];
			value = static_cast<T>(static_cast<double>(accumulator) * factor);
		}
	}
	value *= sign;
	return numberEnd;
}

template<typename T> Char* realToString(Char buffer[32], const T value) {
	Char* p = buffer;

	T absValue = value;
	if (value < 0) {
		*p++ = '-';
		absValue = -value;
	}
	
	if (absValue != absValue) {
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
		assert(next >= normalized); // Correct behavior is to never reach higher than digit 9.
		
		// Do we hit goal with digit or digit + 1?
		reconstructed = static_cast<T>(static_cast<double>(accumulator) * factor);
		if (reconstructed != absValue) {
			reconstructed = static_cast<T>(static_cast<double>(accumulator + magnitude) * factor);
		}
		// Finally, is next digit >= 5 (magnitude / 2) then increment the current digit.
		// (Unless we are at max, just to play nicely with poorer parsers.)
		if (reconstructed == absValue && accumulator + magnitude / 2 < normalized && absValue != std::numeric_limits<T>::max()) {
			++digit;
			assert(digit < 10); // If this happens we have failed to calculate the correct exponent above.
		} else {
			assert(accumulator > 0.0); // If this happens we have failed to calculate the correct exponent above.
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

		// intToString fills the buffer from right (buffer + 32) to left and there should always be enough space
		p = std::copy(intToString(buffer, exponent < 0 ? -exponent : exponent), buffer + 32, p);
	}
	assert(p <= buffer + 32);
	return p;
}

template<typename T> void test(T d) {
	Char buffer[32];
//	String s2(buffer, realToStringB<T>(buffer, d));
	String s(buffer, realToString<T>(buffer, d));
	const Char* const b = s.data();
	const Char* const e = b + s.size();
	T f = 0.0;
	parseReal<T>(b, e, f);
	std::cout << d << " = " << s << " = " << f << std::endl;
	assert(d == f);
}
#if 1
template<typename T> void stressTest2(int iterations) {
	{
		XorshiftRandom2x32 prng;
		for (int i = 0; i < iterations; ++i) {
			char buffer[32];
			union {
				unsigned int v[2];
				double d;
			} u;
			u.v[0] = prng.nextUnsignedInt();
			u.v[1] = prng.nextUnsignedInt();
			Char* end = realToString<T>(buffer, u.d);
			//std::cout << std::string(buffer, end) << std::endl;
			T t;
			if (parseReal<T>(buffer, end, t) != end) {
				std::cout << "BAD!" << std::endl;
			//	return 1;
			}
			if (t != u.d && (t == t || u.d == u.d)) {
				std::cout << std::string(buffer, end) << std::endl;
				std::cout << u.d << " != " << t << std::endl;
				std::cout << "BAD!" << std::endl;
			//	return 1;
			}
		}
	}
}
#endif

template<typename T> struct IntType { };
template<> struct IntType<double> { typedef uint64_t type; };
template<> struct IntType<float> { typedef uint32_t type; };

template<typename T> int testSmallValues(int max) {
	XorshiftRandom2x32 prng;
	for (int i = 0; i < max; ++i) {
		char buffer[32];
		union {
			typename IntType<T>::type i;
			T v;
		} u;
		u.i = i;
		Char* end = realToString(buffer, u.v);
		std::string s0(buffer, end);
		/*std::cout << " " << u.v << std::endl;
		std::cout << "=";*/
		std::cout << s0 << std::endl;
		const Char* const b = s0.data();
		const Char* const e = b + s0.size();
		T t = 0.0;
		parseReal<T>(b, e, t);
		if (t != u.v) {
			std::cout << u.v << " != " << t << std::endl;
			std::cout << "BAD!" << std::endl;
			return 1;
		}
	}
	return 0;
}

template<typename T> int testLargeValues(int max) {
	XorshiftRandom2x32 prng;
	T v = std::numeric_limits<T>::max();
	for (int i = 0; i < max; ++i) {
		char buffer[32];
		Char* end = realToString(buffer, v);
		std::string s0(buffer, end);
	/*	std::cout << " " << v << std::endl;
		std::cout << "=";*/
		std::cout << s0 << std::endl;
		const Char* const b = s0.data();
		const Char* const e = b + s0.size();
		T t = 0.0;
		parseReal<T>(b, e, t);
		if (t != v) {
			std::cout << v << " != " << t << std::endl;
			std::cout << "BAD!" << std::endl;
			return 1;
		}
		v = nextafter(v, static_cast<T>(0));
	}
	return 0;
}

template<typename T> int stressTest3(int iterations) {
	XorshiftRandom2x32 prng;
	for (int i = 0; i < 1000000; ++i) {
		char buffer[32];
		union {
			unsigned int v[sizeof (T) / sizeof (unsigned int)];
			T d;
		} u;
		for (int i = 0; i < sizeof (u.v) / sizeof (*u.v); ++i) {
			u.v[i] = prng.nextUnsignedInt();
		}
		Char* end = realToString(buffer, u.d);
		std::string s0(buffer, end);
		std::cout << s0 << std::endl;
		const Char* const b = s0.data();
		const Char* const e = b + s0.size();
		T t = 0.0;
		parseReal<T>(b, e, t);
		if (t != u.d && (t == t || u.d == u.d)) {
			std::cout << u.d << " != " << t << std::endl;
			std::cout << "BAD!" << std::endl;
			return 1;
		}
	}
	return 0;
}

int main(int argc, const char* argv[]) {
#if 0
{
std::copy(NORMALS + 0, NORMALS + sizeof (NORMALS) / sizeof (*NORMALS), NORMALSb);
double v = 0.5;
double c = 0.0;
int xp = 1;
for (int i = 1; i < EXPONENT_RANGE<double>::MAX; ++i) {
	double x = v / 4.0 - c;
	double t = v + x;
	c = (t - v) - x;
	v = t;
	xp += 3;
	if (v >= 1.0) {
		v /= 2.0;
		++xp;
	}

	double w = v;
	assert(xp == EXPS[i - Traits<double>::MIN_EXPONENT]);
	NORMALSb[i - Traits<double>::MIN_EXPONENT] = w;
}
}

{
	std::cout.precision(20);
	{
		int count = 0;
		const double width = ldexp(1, 53 - 3);
		double high = 0.0;
		double low = 1.0 / width;
		double power = 1.0;
		for (int i = 0; i <= EXPONENT_RANGE<double>::MAX; ++i) {
			assert(low < 1.0 && high < width * 8.0);
			const double v = (high + low) * power * width;

			/*double g = myPow10(i);
			if (v != g) {
				std::cout << v << " != " << g << std::endl;
				++count;
			}*/
			std::cout << std::dec << i << " " << std::hex << *(uint64_t*)(&v) << " " << v << std::endl;
			
			power *= 2.0;
			const double overflow = floor(low * 5.0);
			low = (low * 5.0) - overflow;
			high = (high * 5.0) + overflow;
			if (high >= width) {
				power *= 8.0;
				const double floored = floor(high / 8.0);
				low = (low + (high - floored * 8.0)) / 8.0;
				high = floored;
			}
			assert(low < 1.0 && high < width * 8.0);
		}
	//	std::cout << count << std::endl;
	}
	{
		int count = 0;
		const double width = ldexp(1, 53 - 3);
		double high = width;
		double low = 0.0;
		double power = 1.0;
		for (int i = -1; i >= EXPONENT_RANGE<double>::MIN; --i) {
			if (high < width) {
				power /= 8.0;
				const double overflow = floor(low * 8.0);
				low = (low * 8.0) - overflow;
				high = (high * 8.0) + overflow;
			}
			power /= 2.0;
			const double floored = floor(high / 5.0);
			low = (low + (high - floored * 5.0)) / 5.0;
			high = floored;
			
			assert(low < 1.0 && high < width * 8.0);
			const double v = (high + low) * power / width;
			/*
			double g = myPow10(i);
			if (v != g) {
				std::cout << v << " != " << g << std::endl;
				++count;
			}*/

			std::cout << std::dec << i << std::hex << *(uint64_t*)(&v) << " " << v << std::endl;
		}
		//std::cout << count << std::endl;
		return 0;
	}


double ddd = ldexp(0.5984799861907958984375, 191);
//double ddd = ldexp(0.5984799861907958984375, 65);
char buffer[32];
Char* end = realToStringNuXJS(buffer, ddd);
std::string s(buffer, end);
std::cout << ddd << " = " << s << std::endl;

ddd = nextafter(ddd, 1e100);
end = realToStringNuXJS(buffer, ddd);
s = std::string(buffer, end);
std::cout << ddd << " = " << s << std::endl;

ddd = nextafter(ddd, 1e100);
end = realToStringNuXJS(buffer, ddd);
s = std::string(buffer, end);
std::cout << ddd << " = " << s << std::endl;

return 0;

}

#if 0
{
	std::cout.precision(20);
	{
		double v = 1.0;
		double c = 0.0;
		double s = 1.0;
		double h = 1.0;
		double p = 1.0;
		double err = 0.0;
		int xp = 1;
		int count = 0;
		int n = 0;
		// fesetround(FE_UPWARD);
		for (int i = 1; i < EXPONENT_RANGE<double>::MAX; ++i) {
		/*	double x = v * 0.25 - c;
			double t = v + x;
			c = (t - v) - x;
			v = t;
			xp += 3;
			if (v * p >= 1.0) {
				p *= 0.5;
				++xp;
			}*/
			/*
			double l = v;
			double ddd = v * 0.25;
			v += ddd;
			double dd = ddd - (v - l);
			v *= 8;
			dd *= 8;
			xp += 3;
			err += dd;
		//	double pv = v;
		//	v += err;
		//	err -= (v - pv);

			if (v * p >= 1.0) {
				p *= 0.5;
				++xp;
			}*/
/*
			double x = s / 4.0 - c;
			double t = s + x;
			c = (t - s) - x;
			s = t;
			p *= 8;
			c *= 1.25;
 
			if (s != TEZZZT[i] || c != 0) {
				std::cout << s << " != " << TEZZZT[i] << std::endl;
				std::cout << "  " << c << std::endl;
			}
			*/
/*
			double w = (v + err) * p;
			double f = ldexp(w, xp);
			double g = myPow10(i);
			double h = s * p;
			assert(xp == EXPS[i - Traits<double>::MIN_EXPONENT]);*/
		/*	if (f != g || w != NORMALS[i - Traits<double>::MIN_EXPONENT]) {
				std::cout << f << " != " << g << " (" << s << "*" << p << ")" << std::endl;
				std::cout << w << " != " << NORMALS[i - Traits<double>::MIN_EXPONENT] << std::endl;
				std::cout << "  " << (f - g) << " or " << (NORMALS[i - Traits<double>::MIN_EXPONENT] - w) << std::endl;
				++count;
				w += (NORMALS[i - Traits<double>::MIN_EXPONENT] - w);
			}*//*
				std::cout << "  " << err << std::endl;
			if (w != NORMALS[i - Traits<double>::MIN_EXPONENT]) {
				std::cout << w << " != " << NORMALS[i - Traits<double>::MIN_EXPONENT] << std::endl;
				std::cout << n << " " << (NORMALS[i - Traits<double>::MIN_EXPONENT] - w) << std::endl;
				++count;
			//	v += (NORMALS[i - Traits<double>::MIN_EXPONENT] - v);
				n = 0;
			}*/

			double l = v;
			v *= 8;
			double d = l * 2;
			l = v;
			v += d;
			err += d - (v - l);

			double g = myPow10(i);
			if (v != g) {
				std::cout << v << " != " << g << std::endl;
				++count;
			}
			++n;
		}
		std::cout << count << std::endl;
		std::cout << err << std::endl;
		return 0;
	}


double ddd = ldexp(0.5984799861907958984375, 191);
//double ddd = ldexp(0.5984799861907958984375, 65);
char buffer[32];
Char* end = realToStringNuXJS(buffer, ddd);
std::string s(buffer, end);
std::cout << ddd << " = " << s << std::endl;

ddd = nextafter(ddd, 1e100);
end = realToStringNuXJS(buffer, ddd);
s = std::string(buffer, end);
std::cout << ddd << " = " << s << std::endl;

ddd = nextafter(ddd, 1e100);
end = realToStringNuXJS(buffer, ddd);
s = std::string(buffer, end);
std::cout << ddd << " = " << s << std::endl;

return 0;

}

	std::cout.precision(13);
	{
		for (int i = Traits<double>::MIN_EXPONENT; i <= Traits<double>::MAX_EXPONENT; ++i) {
			double a = pow(10.0, i);
			double b = myPow10(i);
			std::cout << std::dec << "(" << i << ") "  << std::hex << *(int64_t*)(&NORMALS[i - Traits<double>::MIN_EXPONENT]) <<" "<< std::hex << *(int64_t*)(&a) << (a == b ? " == " : " != ")  << std::hex << *(int64_t*)(&b) << std::endl;
		}
		return 0;
	}
	if (false) {
		double d = pow(10.0, -40);
		double d2 = pow(10.0, 40);
		for (int i = -40; i >= -60; --i) {
			double f = pow(10.0, i);
			int base2Exponent;
			double g = frexp(f, &base2Exponent);
			double h = myPow10(i);
			std::cout << d << " = " << (1 / d2) << " = " << f << " = " << h << " = " << g << "*2^" << std::dec << base2Exponent << " = " << std::hex << *(int64_t*)(&g) << std::endl;
			d /= 10;
			d2 *= 10;
		}
	}
	
#endif
#endif
	std::cout.precision(24);
#if 0
test(9.80909e-45f);

test(5e-324);
test(1.7976931348623157e+308);
test(123456789.12345678);

{
	double f = 0.0;
	//std::string s("8529438593.23485e40");
	//std::string s("8.52943859323485e+49");
	//std::string s("0.6");
	//std::string s("0.3333");
	//std::string s("1.23456789e-309");
	//std::string s("5.23456789e-324");
	//std::string s("1.7976931348623157e+308");
	//std::string s("1.7976931348623159e+308");	// inf
	 std::string s("2.471e-324");	// 5e-324
	//std::string s("2.470e-324");	// 0
	//std::string s("1.0862235697705999e+16");	// 0
	const Char* const b = s.data();
	const Char* const e = b + s.size();
	parseReal(b, e, f);
	std::cout << f << std::endl;
}

{
test(10862235697705999.0);
test(10862235697705998336.0);

double d = 0.6;
test(d);
d = nextafter(d, 1e100);
test(d);
d = nextafter(d, 1e100);
test(d);

	test(8529438593.23485e40);
	test(8.52943859323485e+49);
	test(0.6);
	test(0.3333);
	test(1.23456789e-309);
	test(5.23456789e-324);
	test(1.7976931348623157e+308);
	test(2.471e-324);	// 5e-324
	test(3.5e-323);
	test(3.2e-323);
	test(3.3e-323);
	test(17.0e-323);
	test(17.1e-323);
	test(17.2e-323);
	test(17.3e-323);
	test(17.4e-323);
	test(17.5e-323);
	test(17.6e-323);
	test(17.7e-323);
	test(17.8e-323);
	test(17.9e-323);
	stressTest2<double>(10000000);
}
	return 0;

	test(2.80259692864963414184746e-45f);
	test(nextafter(4.68467350360680649143641e-294, -1e300));
	test(4.68467350360680649143641e-294);
	test(nextafter(4.68467350360680649143641e-294, 1.0));
	test(nextafter(nextafter(4.68467350360680649143641e-294, 1.0), 1.0));
	test(0.01);
	test(nextafter(0.01, 1.0));
	double d = 1923912.12391;
	test(d);
	d = nextafter(d, 1.0);
	test(d);

	test(0.0f);
	test(-0.0f);
	test(nextafter(0.0f, 1.0f));
	test(std::numeric_limits<float>::max());
	test(0.0);
	test(-0.0);
	test(nextafter(0.0, 1.0));
	test(std::numeric_limits<double>::max());

	test(0.01f);
	test(0.02f);
	test(0.03f);
	test(0.04f);
	test(0.05f);
	test(0.06f);
	test(0.07f);
	test(0.08f);
	test(0.09f);
	test(0.1f);
	test(0.2f);
	test(0.3f);
	test(0.4f);
	test(0.5f);
	test(0.6f);
	test(0.7f);
	test(0.8f);
	test(0.9f);

	test(0.01);
	test(0.0);
	test(-1.0);
	test(1.1);
	test(1.12);
	test(1.123);
	test(1.12345);
	test(1.123456);
	test(1.123457);
	test(0.01);
	test(0.1);
	test(0.2);
	test(0.3);
	test(0.4);
	test(0.5);
	test(0.6);
	test(0.7);
	test(0.8);
	test(0.9);
	test(0.99);
	test(100.0);
	test(0.001);
	test(999.9999999999);
	test(123456789.123456789);
	test(1.234e20);
	test(1.234e40);
	test(1.234e80);
	test(1.234e-20);
	test(1.234e-40);
	test(1.234e-80);
#endif

#if 0
	if (false) {
		const int MAX_EXPONENT = EXPONENT_RANGE<double>::MAX;
		const int MIN_EXPONENT = EXPONENT_RANGE<double>::MIN;
    	std::mt19937 gen(1234);
    	std::uniform_real_distribution<double> rndReal(0.0, 1.0);
    	std::uniform_real_distribution<double> rndReal2(0.5, 1.0);
    	std::uniform_int_distribution<int> rndExp(MIN_EXPONENT + 1, MAX_EXPONENT);
    	std::uniform_int_distribution<int> rndExp2(-1022, 1023);

		for (int i = 0; i < 1000000; ++i) {
			char buffer[32];
			const double m = rndReal2(gen);
		//	const int x = rndExp(gen);
		//	const double rndD = m * pow((double)(10.0), x);
			const int x2 = rndExp2(gen);
			const double rndD = ldexp(m, x2);
			Char* end = realToStringC(buffer, rndD);
			std::string s0(buffer, end);
			std::cout << s0 << std::endl;
		/*	end = realToStringNuXJS(buffer, rndD);
			std::string s1(buffer, end);
			if (s0 != s1) {
				std::cout << "**** ";
			}
			std::cout << s0 << ", " << s1 << std::endl;*/
		}
	} else
#endif
	if (true) {
		if (testSmallValues<float>(10000) != 0) {
			return 1;
		}
		if (testSmallValues<double>(10000) != 0) {
			return 1;
		}
		if (testLargeValues<float>(10000) != 0) {
			return 1;
		}
		if (testLargeValues<double>(10000) != 0) {
			return 1;
		}
		if (stressTest3<float>(1000000) != 0) {
			return 1;
		}
		if (stressTest3<double>(1000000) != 0) {
			return 1;
		}
	}

	return 0;
}
