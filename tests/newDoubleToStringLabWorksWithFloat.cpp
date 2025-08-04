#ifdef __GNUC__
#pragma GCC push_options
#pragma GCC optimize ("no-finite-math-only")
// older gcc might need this too:
// #pragma GCC optimize ("float-store")
#endif

#ifdef _MSC_VER
#pragma float_control(precise, on, push)  
#endif

#ifdef __FAST_MATH__
	#error This code requires IEEE compliant floating point handling. Avoid -Ofast / -ffast-math etc (at least for this source file).
#endif

#include "assert.h"
#include <random>
#include <algorithm>
#include <iostream>
#include <string>
#include <cmath>

typedef char Char;
typedef std::string String;

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

/*
bool Parser::tryToParse(double& d) {
	d = 0.0;
	white();
	double sign = 1.0;
	if (!eof() && (*p == '+' || *p == '-')) {
		sign = ((*p == '-') ? -1.0 : 1.0);
		++p;
	}
	if (!eof() && *p == '0') {
		++p;
	} else {
		const StringIt b = p;
		while (!eof() && *p >= '0' && *p <= '9') {
			d = d * 10.0 + (*p - '0');
			++p;
		}
		if (p == b) {
			return false;
		}
	}
	if (!eof() && *p == '.') {
		++p;
		double f = 1.0;
		const StringIt b = p;
		while (!eof() && *p >= '0' && *p <= '9') {
			d += (*p - '0') * (f *= 0.1);
			++p;
		}
		if (p == b) {
			return false;
		}
	}
	d *= sign;
	if (!eof() && (*p == 'e' || *p == 'E')) {
		++p;
		int i;
		if (!integer(i)) {
			return false;
		}
		d *= pow(10.0, i);
	}
	white();
	return eof();
}
*/

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

static Char* realToStringA(Char buffer[32], double value) {
	const int MAX_EXPONENT = 308;
	const int MIN_EXPONENT = -324;

	Char* p = buffer;
	if (value == 0.0) {
		*p++ = '0';
		return p;
	}
	if (value < 0) {
		*p++ = '-';
		value = -value;
	}

	int base2Exponent;
	(void) frexp(value, &base2Exponent);	// frexp is fast and precise and gives log2(x), log10(x) = log2(x) / log2(10)
	int exponent = std::max(static_cast<int>(ceil(0.30102999566398119521 * (base2Exponent - 1))) - 1, MIN_EXPONENT);
	if (exponent < MAX_EXPONENT && value >= pow(10.0, static_cast<double>(exponent + 1))) {
		++exponent;
	}
	const bool eNotation = (exponent < -6 || exponent >= 21);
	Char* periodPosition = p + (eNotation || exponent < 0 ? 0 : exponent) + 1;
	if (!eNotation && exponent < 0) {
		*p++ = '0';
		*p++ = '.';
		while (p < periodPosition - exponent) {
			*p++ = '0';
		}
	}
	const char* e = p + 5;
	const double minResidual = pow(10.0, static_cast<double>(exponent - 5));
	
	double residual = floor(value * pow(10.0, static_cast<double>(-exponent + 5)) + 0.5);
	residual /= pow(10.0, 5.0);
	double magnitude = 1.0;
	while (p < e/*buffer + 27*/ && residual >= minResidual) {
		if (p == periodPosition) {
			*p++ = '.';
		}
		int digit = static_cast<int>(residual / magnitude);
		double x = digit * magnitude;
		if (residual - x < 0 || digit == 10) { 	// When underflowing the residual (because of rounding problems) we drop to a lower digit. Also with some extreme values (e.g. the lowest denormals), rounding can cause residual / magnitude to become 1.0 and we must turn the 10 into a 9.
			--digit;
			x = digit * magnitude;
		}
		residual -= x;
		*p++ = '0' + digit;
		magnitude /= 10.0;
	} 	// p < buffer + 27 is an extra precaution if toNumber(toString(x)) == x is never met (e.g. because of too aggressive optimizations). 27 leaves room for longest exponent.
	while (p < periodPosition) {
		*p++ = '0';
	}
	if (eNotation) {
		*p++ = 'e';
		*p++ = (exponent < 0 ? '-' : '+');
		p = std::copy(intToString(buffer, exponent < 0 ? -exponent : exponent), buffer + 32, p);	// intToString fills the buffer from right (buffer + 32) to left and there should always be enough space
	}
	return p;
}

static const Char* parseUnsignedInt(const Char* p, const Char* e, unsigned int& i) {
	for (i = 0; p != e && *p >= '0' && *p <= '9'; ++p) {
		i = i * 10 + (*p - '0');
	}
	return p;
}

template<typename T> struct EXPONENT_RANGE { };
template<> struct EXPONENT_RANGE<double> { enum { MIN = -324, MIN_NORM = -290, MAX = 308, NORM_EXP2 = 64 }; };
template<> struct EXPONENT_RANGE<float> { enum { MIN = -45, MIN_NORM = -35, MAX = 38, NORM_EXP2 = 24 }; };

template<typename T> static const Char* parseReal(const Char* const b, const Char* const e, T& value) {
	const int MAX_EXPONENT = EXPONENT_RANGE<T>::MAX;
	const int MIN_EXPONENT = EXPONENT_RANGE<T>::MIN;
	const int MIN_NORM_EXPONENT = EXPONENT_RANGE<T>::MIN_NORM;
	const int NORM_EXP = EXPONENT_RANGE<T>::NORM_EXP2;

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
	if (e - p >= 8 && strncmp(p, "infinity", 8) == 0) {
		p += 8;
		value = std::numeric_limits<T>::infinity();
		numberEnd = p;
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
		
		if (p == significandEnd || exponent < MIN_EXPONENT) {
			value = (T)(0.0);
		} else if (exponent > MAX_EXPONENT) {
			value = std::numeric_limits<T>::infinity();
		} else {
		#if 0
			T scale = (exponent < MIN_NORM_EXPONENT ? exp2((T)(-NORM_EXP)) : (T)(1.0));	// An extra scaling step is necessary to reach the lowest denormal since the lowest 10^x is out of range.
			int xp = exponent + (exponent < MIN_NORM_EXPONENT ? 1 : 0);
			T compensation = 0.0;
			T d = (T)(0.0);
			volatile T magnitude = pow((T)(10.0), xp) / scale;
			magnitude /= (exponent < MIN_NORM_EXPONENT ? 10 : 1);
		#else
			T scale = (exponent < MIN_NORM_EXPONENT ? (T)(1e-20) : (T)(1.0));	// An extra scaling step is necessary to reach the lowest denormal since the lowest 10^x is out of range.
			int xp = exponent + (exponent < MIN_NORM_EXPONENT ? 20 : 0);
			T compensation = 0.0;
			T d = (T)(0.0);
			T magnitude = pow((T)(10.0), xp);
		#endif

			while (p != significandEnd) {
				if (*p != '.') {
					assert(xp <= MAX_EXPONENT);
					const int digit = (*p - '0');
					volatile T a = digit * magnitude;
					volatile T x = a * scale;
					const T adjusted = x - compensation;	// Kahan summation for greater accuracy
					T sum = d + adjusted;
					compensation = sum - d;
					compensation -= adjusted;
					d = sum;
					magnitude /= (T)(10.0);
				}
				++p;
			}
			value = d;
		}
	}
	value *= sign;
	return numberEnd;
}

template<typename T> static Char* realToStringB(Char buffer[32], T value) {
	int base2Exponent;
	const T mantissa = frexp(value, &base2Exponent);	// frexp is fast and precise and gives log2(x), log10(x) = log2(x) / log2(10)
	int base10Exponent = static_cast<int>(ceil((T)(0.30102999566398119521) * base2Exponent));
	const T normalizer = (exp2(base2Exponent) / pow((T)(10.0), base10Exponent));// * pow((T)(10.0), 16);
	const T normalized = mantissa * normalizer;
	const T b = normalized / normalizer;
//	assert(b == mantissa);
	T residual = normalized;
	T x = 1.0;
	if (residual < exp2((T)(53.0))) {
		residual *= 10;//normalized * pow((T)(10.0), 17);
		x = 10.0;
	}
	const T back = (residual / x) / normalizer;
	return buffer;
}

/*
	parseDouble() and realToString() are designed to convert back and forth losslessly. In realToString() we
	simultaneously generate and interpret the converted result. When the interpreted value is identical to the input
	value we have enough digits.
*/
template<typename T> static Char* realToString(Char buffer[32], T value) {
	const int MAX_EXPONENT = EXPONENT_RANGE<T>::MAX;
	const int MIN_EXPONENT = EXPONENT_RANGE<T>::MIN;
	const int MIN_NORM_EXPONENT = EXPONENT_RANGE<T>::MIN_NORM;
	const int NORM_EXP = EXPONENT_RANGE<T>::NORM_EXP2;

	Char* p = buffer;
	if (value == 0.0) {
		*p++ = '0';
		*p++ = '.';
		*p++ = '0';
		return p;
	}
	if (value < 0) {
		*p++ = '-';
		value = -value;
	}
	
	int base2Exponent;
	(void) frexp(value, &base2Exponent);	// frexp is fast and precise and gives log2(x), log10(x) = log2(x) / log2(10)
	int exponent = std::max(static_cast<int>(ceil((T)(0.30102999566398119521)
			* (base2Exponent - 1))) - 1, MIN_EXPONENT);
	if (exponent < MAX_EXPONENT && value >= pow((T)(10.0), exponent + 1)) {
		++exponent;
	}
	
	const bool eNotation = (exponent < -6 || exponent >= 21);
	Char* periodPosition = p + (eNotation || exponent < 0 ? 0 : exponent) + 1;
	if (!eNotation && exponent < 0) {
		*p++ = '0';
		*p++ = '.';
		while (p < periodPosition - exponent) {
			*p++ = '0';
		}
	}
#if 0
	const T scale = (exponent < MIN_NORM_EXPONENT ? exp2((T)(-NORM_EXP)) : (T)(1.0));	// An extra scaling step is necessary to reach the lowest denormal (approx 0.5e-324) since 1e-325 is out of range.
	int xp = exponent + (exponent < MIN_NORM_EXPONENT ? 1 : 0);
	T residual = value;
	T reconstructed = (T)(0.0);
	T compensation = (T)(0.0);
	T sum = (T)(0.0);
	volatile T magnitude = pow((T)(10.0), xp) / scale;
	magnitude /= (exponent < MIN_NORM_EXPONENT ? 10 : 1);
#else
	const T scale = (exponent < MIN_NORM_EXPONENT ? (T)(1e-20) : (T)(1.0));	// An extra scaling step is necessary to reach the lowest denormal (approx 0.5e-324) since 1e-325 is out of range.
	int xp = exponent + (exponent < MIN_NORM_EXPONENT ? 20 : 0);
	T residual = value;
	T reconstructed = (T)(0.0);
	T compensation = (T)(0.0);
	T sum = (T)(0.0);
	T magnitude = pow((T)(10.0), xp);
#endif
	do {
		assert(MIN_EXPONENT <= xp && xp <= MAX_EXPONENT);
		if (p == periodPosition) {
			*p++ = '.';
		}
		assert(residual >= 0);
		assert(magnitude >= 0);
		volatile T a = residual / magnitude;
		assert(scale >= 0);
		a /= scale;
		assert(a >= 0);
		int digit = std::min(static_cast<int>(a), 9);
if (digit < 0) std::cout << value << ", " << a << ", " << digit << std::endl;
		assert(digit >= 0);
		a = digit * magnitude;
		volatile T x = a * scale;
		while (residual - x < 0) { 	// When underflowing the residual (because of rounding problems) we drop to a lower digit. Also with some extreme values (e.g. the lowest denormals), rounding can cause residual / magnitude to become 1.0 and we must turn the 10 into a 9.
			--digit;
			a = digit * magnitude;
			x = a * scale;
		}
		residual -= x;
		assert(residual >= 0);
		
		const T adjusted = x - compensation;	// Kahan summation for greater accuracy
		sum = reconstructed + adjusted;

		if (sum != value && digit < 9) {	// Catch shorter rounded solutions by trying one digit higher too (except if last digit was 9 in which case we should have caught the rounded solution in the previous step anyhow).
			a = (digit + 1) * magnitude;
			const T altAdjusted = a * scale - compensation;
			T altSum = reconstructed + altAdjusted;
			if (altSum == value) {
				sum = altSum;
				++digit;
			}
		}

		*p++ = '0' + digit;

		compensation = sum - reconstructed;
		compensation -= adjusted;
		reconstructed = sum;
		magnitude /= (T)(10.0);
		assert(magnitude >= 0);
	} while (p < buffer + 27 && magnitude > (T)(0.0) && residual != (T)(0.0) && sum != value);	// p < buffer + 27 is an extra precaution if toNumber(toString(x)) == x is never met (e.g. because of too aggressive optimizations). 27 leaves room for longest exponent.
	
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
		p = std::copy(intToString(buffer, exponent < 0 ? -exponent : exponent), buffer + 32, p);	// intToString fills the buffer from right (buffer + 32) to left and there should always be enough space
	}
	assert(p <= buffer + 32);
	return p;
}

String d2s(double d) {
	Char buffer[32];
	return String(buffer, realToString<double>(buffer, d));
}

String f2s(double d) {
	Char buffer[32];
	return String(buffer, realToString<float>(buffer, d));
}

double s2d(String s) {
	const Char* const b = s.data();
	const Char* const e = b + s.size();
	double d = 0.0;
	parseReal(b, e, d);
	return d;
}

template<typename T> void test(T d) {
	Char buffer[32];
//	String s2(buffer, realToStringB<T>(buffer, d));
	String s(buffer, realToString<T>(buffer, d));
	const Char* const b = s.data();
	const Char* const e = b + s.size();
	T f = 0.0;
	parseReal(b, e, f);
	std::cout << d << " = " << s << " = " << f << std::endl;
}

void test(float f) {
	std::cout << f << " = " << f2s(f) << std::endl;
}

template<typename T> void stressTest(int iterations) {
	{
		const int MAX_EXPONENT = EXPONENT_RANGE<T>::MAX;
		const int MIN_EXPONENT = EXPONENT_RANGE<T>::MIN;
    	std::mt19937 gen(1234);
    	std::uniform_real_distribution<T> rndReal(0.0, 1.0);
    	std::uniform_int_distribution<int> rndExp(MIN_EXPONENT + 1, MAX_EXPONENT);

		for (int i = 0; i < 10000000; ++i) {
			char buffer[32];
			const T m = rndReal(gen);
			const int x = rndExp(gen);
			const T rndD = m * pow((T)(10.0), x);
			Char* end = realToString(buffer, rndD);
			//std::cout << std::string(buffer, end) << std::endl;
			T t;
			if (parseReal(buffer, end, t) != end) {
				std::cout << "BAD!" << std::endl;
			//	return 1;
			}
			if (t != rndD) {
				std::cout << std::string(buffer, end) << std::endl;
				std::cout << t << " != " << rndD << std::endl;
				std::cout << "BAD!" << std::endl;
			//	return 1;
			}
		}
	}
}

int main(int argc, const char* argv[]) {
	std::cout.precision(13);
	for (int i = -1; i < 10; ++i) {
		double f = pow(10.0, i);
		int base2Exponent;
		double g = frexp(f, &base2Exponent);
		std::cout << f << "=" << g << "=" << "*2^" << base2Exponent << "=" << std::hex << *(int64_t*)(&g) << std::endl;
	}

	std::cout.precision(24);
	test(2.80259692864963414184746e-45f);
	test(4.68467350360680649143641e-294);
	test(nextafter(4.68467350360680649143641e-294, 1.0));
	test(nextafter(nextafter(4.68467350360680649143641e-294, 1.0), 1.0));
	test(0.01);
	test(nextafter(0.01, 1.0));
	double d = 1923912.12391;
	test(d);
	d = nextafter(d, 1.0);
	test(d);

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

	s2d("5.0000e-324");
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
	
	stressTest<float>(10000000);
	stressTest<double>(10000000);

	return 0;
}
