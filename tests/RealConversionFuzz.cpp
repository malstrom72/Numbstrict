#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
#include "../src/Numbstrict.h"
#include "FuzzInitialize.h"

/*
	Checks Numbstrict's real number conversions against exact big integer arithmetic, which shares nothing with the
	conversion code under test. For every input, `doubleToString` and `floatToString` must produce the shortest string
	that rounds back to the value, and the closest one of that length, with an even last digit on an exact tie;
	`stringToDouble` and `stringToFloat` must return the correctly rounded nearest value (ties to even) for a number
	string built from the input.
	Input layout: bytes 0-7 a double, 8-11 a float, 12 flags, 13-16 an exponent, then mantissa digits.
*/

static void fail(const std::string& what) {
	fprintf(stderr, "RealConversionFuzz: %s\n", what.c_str());
	abort();
}

/*
	Unsigned integer of any size, least significant 32-bit limb first, with just the operations the checks need.
*/
class Natural {
	public:
		explicit Natural(uint64_t value = 0) {
			for (; value != 0; value >>= 32) {
				limbs.push_back(static_cast<uint32_t>(value));
			}
		}
		bool isZero() const { return limbs.empty(); }
		Natural& multiplyAdd(uint32_t factor, uint32_t addend) {
			uint64_t carry = addend;
			for (size_t i = 0; i < limbs.size(); ++i) {
				carry += static_cast<uint64_t>(limbs[i]) * factor;
				limbs[i] = static_cast<uint32_t>(carry);
				carry >>= 32;
			}
			if (carry != 0) {
				limbs.push_back(static_cast<uint32_t>(carry));
			}
			trim();
			return *this;
		}
		Natural& subtractOne() {
			assert(!isZero());
			size_t i = 0;
			while (limbs[i] == 0) {
				limbs[i] = 0xFFFFFFFFu;
				++i;
			}
			--limbs[i];
			trim();
			return *this;
		}
		uint32_t divideBy10() {
			uint64_t remainder = 0;
			for (size_t i = limbs.size(); i > 0; --i) {
				remainder = (remainder << 32) | limbs[i - 1];
				limbs[i - 1] = static_cast<uint32_t>(remainder / 10);
				remainder %= 10;
			}
			trim();
			return static_cast<uint32_t>(remainder);
		}
		Natural& multiplyByPowerOf10(int64_t exponent) {
			for (; exponent >= 9; exponent -= 9) {
				multiplyAdd(1000000000u, 0);
			}
			for (; exponent > 0; --exponent) {
				multiplyAdd(10, 0);
			}
			return *this;
		}
		Natural& shiftLeft(int64_t bits) {
			for (; bits >= 32; bits -= 32) {
				if (!isZero()) {
					limbs.insert(limbs.begin(), 0);
				}
			}
			return multiplyAdd(static_cast<uint32_t>(1) << bits, 0);
		}
		int compare(const Natural& other) const {
			if (limbs.size() != other.limbs.size()) {
				return limbs.size() < other.limbs.size() ? -1 : 1;
			}
			for (size_t i = limbs.size(); i > 0; --i) {
				if (limbs[i - 1] != other.limbs[i - 1]) {
					return limbs[i - 1] < other.limbs[i - 1] ? -1 : 1;
				}
			}
			return 0;
		}

	private:
		void trim() {
			while (!limbs.empty() && limbs.back() == 0) {
				limbs.pop_back();
			}
		}
		std::vector<uint32_t> limbs;
};

struct Decimal {	// `mantissa` times 10 to the power of `exponent`
	Natural mantissa;
	int64_t exponent;
	int digitCount;	// digits in `mantissa`, 0 for zero
};

struct Binary {	// `mantissa` times 2 to the power of `exponent`
	uint64_t mantissa;
	int exponent;
};

static int compare(const Decimal& decimal, const Binary& binary) {
	Natural left = decimal.mantissa;
	Natural right(binary.mantissa);
	if (decimal.exponent >= 0) {
		left.multiplyByPowerOf10(decimal.exponent);
	} else {
		right.multiplyByPowerOf10(-decimal.exponent);
	}
	if (binary.exponent >= 0) {
		right.shiftLeft(binary.exponent);
	} else {
		left.shiftLeft(-binary.exponent);
	}
	return left.compare(right);
}

static Binary midpoint(const Binary& a, const Binary& b) {
	const int low = (a.exponent < b.exponent ? a.exponent : b.exponent);
	assert(a.exponent - low < 10 && b.exponent - low < 10);
	const Binary sum = { (a.mantissa << (a.exponent - low)) + (b.mantissa << (b.exponent - low)), low - 1 };
	return sum;
}

template<typename T> struct Traits;
template<> struct Traits<double> {
	typedef uint64_t Bits;
	static const int MANTISSA_BITS = 52;
	static const int MIN_EXPONENT = -1074;	// of the least significant mantissa bit
	static const int MAX_EXPONENT = 1024;	// 2 to this power is the first value that overflows
	static const int MIN_DECIMAL = -325;	// below 10 to this power everything rounds to zero
	static const int MAX_DECIMAL = 309;	// from 10 to this power everything rounds to infinity
	static std::string toString(double value) { return Numbstrict::doubleToString(value); }
	static double fromString(const std::string& s, size_t* next) { return Numbstrict::stringToDouble(s, next); }
};
template<> struct Traits<float> {
	typedef uint32_t Bits;
	static const int MANTISSA_BITS = 23;
	static const int MIN_EXPONENT = -149;
	static const int MAX_EXPONENT = 128;
	static const int MIN_DECIMAL = -47;
	static const int MAX_DECIMAL = 39;
	static std::string toString(float value) { return Numbstrict::floatToString(value); }
	static float fromString(const std::string& s, size_t* next) { return Numbstrict::stringToFloat(s, next); }
};

template<typename T> static typename Traits<T>::Bits bitsOf(T value) {
	typename Traits<T>::Bits bits;
	memcpy(&bits, &value, sizeof bits);
	return bits;
}

template<typename T> static Binary binaryOf(T value) {	// `value` must be finite and not negative
	const typename Traits<T>::Bits bits = bitsOf(value);
	const int field = static_cast<int>(bits >> Traits<T>::MANTISSA_BITS);
	const uint64_t fraction = bits & ((static_cast<uint64_t>(1) << Traits<T>::MANTISSA_BITS) - 1);
	const Binary binary = { field == 0 ? fraction : fraction | (static_cast<uint64_t>(1) << Traits<T>::MANTISSA_BITS)
			, Traits<T>::MIN_EXPONENT + (field == 0 ? 0 : field - 1) };
	return binary;
}

/*
	True if the non-negative `decimal` rounds to `value` (also non-negative) with round to nearest, ties to even.
*/
template<typename T> static bool roundsTo(const Decimal& decimal, T value) {
	if (decimal.mantissa.isZero()) {
		return value == 0;
	}
	const int64_t leadingExponent = decimal.exponent + decimal.digitCount - 1;
	if (leadingExponent >= Traits<T>::MAX_DECIMAL) {
		return std::isinf(value);
	}
	if (leadingExponent < Traits<T>::MIN_DECIMAL) {
		return value == 0;
	}
	const Binary overflow = { static_cast<uint64_t>(1) << (Traits<T>::MANTISSA_BITS + 1)
			, Traits<T>::MAX_EXPONENT - Traits<T>::MANTISSA_BITS - 1 };	// 2 to the power of MAX_EXPONENT
	if (std::isinf(value)) {	// the largest finite value has an odd mantissa, so its upper tie overflows
		return compare(decimal, midpoint(binaryOf(std::numeric_limits<T>::max()), overflow)) >= 0;
	}
	const Binary binary = binaryOf(value);
	const bool isEven = ((binary.mantissa & 1) == 0);
	const T next = std::nextafter(value, std::numeric_limits<T>::infinity());
	const int upper = compare(decimal, midpoint(binary, std::isinf(next) ? overflow : binaryOf(next)));
	if (upper > 0 || (upper == 0 && !isEven)) {
		return false;
	}
	if (value == 0) {
		return true;
	}
	const int lower = compare(decimal, midpoint(binaryOf(std::nextafter(value, static_cast<T>(0))), binary));
	return lower > 0 || (lower == 0 && isEven);
}

/*
	Reads `[-+]digits[.digits][(e|E)[-+]digits]` into `negative` and `decimal`. Huge exponents saturate. Returns false
	for anything else.
*/
static bool readDecimal(const std::string& s, bool& negative, Decimal& decimal) {
	size_t i = 0;
	negative = (i < s.size() && s[i] == '-');
	if (i < s.size() && (s[i] == '-' || s[i] == '+')) {
		++i;
	}
	decimal.mantissa = Natural();
	decimal.exponent = 0;
	decimal.digitCount = 0;
	bool hasDigits = false;
	bool afterPoint = false;
	for (; i < s.size() && ((s[i] >= '0' && s[i] <= '9') || (s[i] == '.' && !afterPoint)); ++i) {
		if (s[i] == '.') {
			afterPoint = true;
		} else {
			hasDigits = true;
			decimal.mantissa.multiplyAdd(10, s[i] - '0');
			if (decimal.digitCount != 0 || s[i] != '0') {
				++decimal.digitCount;
			}
			if (afterPoint) {
				--decimal.exponent;
			}
		}
	}
	if (!hasDigits) {
		return false;
	}
	if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
		++i;
		const bool negativeExponent = (i < s.size() && s[i] == '-');
		if (i < s.size() && (s[i] == '-' || s[i] == '+')) {
			++i;
		}
		if (i == s.size()) {
			return false;
		}
		int64_t exponent = 0;
		for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) {
			exponent = (exponent < 1000000000000ll ? exponent * 10 + (s[i] - '0') : exponent);
		}
		decimal.exponent += (negativeExponent ? -exponent : exponent);
	}
	return i == s.size();
}

template<typename T> static std::string describe(T value) {
	char buffer[64];
	snprintf(buffer, sizeof buffer, "%.17g (bits 0x%llx)", static_cast<double>(value)
			, static_cast<unsigned long long>(bitsOf(value)));
	return buffer;
}

template<typename T> static void checkFormat(T value) {
	const std::string text = Traits<T>::toString(value);
	const std::string context = text + " for " + describe(value);
	size_t next;
	const T parsed = Traits<T>::fromString(text, &next);
	if (std::isnan(value)) {	// a nan's sign and payload are not kept
		if (text != "nan" || next != text.size() || !std::isnan(parsed)) {
			fail("nan does not round-trip: " + context);
		}
		return;
	}
	if (next != text.size() || bitsOf(parsed) != bitsOf(value)) {
		fail("Numbstrict does not parse its own output back: " + context);
	}
	const T magnitude = std::fabs(value);
	if (std::isinf(magnitude)) {
		if (text != (value < 0 ? "-inf" : "inf")) {
			fail("unexpected infinity text: " + context);
		}
		return;
	}
	bool negative;
	Decimal decimal;
	if (!readDecimal(text, negative, decimal) || negative != std::signbit(value)) {
		fail("unexpected number format: " + context);
	}
	if (!roundsTo(decimal, magnitude)) {
		fail("output does not round to the value: " + context);
	}
	while (decimal.digitCount > 1) {	// drop trailing zeros, which the format may require
		Natural shorter = decimal.mantissa;
		if (shorter.divideBy10() != 0) {
			break;
		}
		decimal.mantissa = shorter;
		++decimal.exponent;
		--decimal.digitCount;
	}
	if (decimal.digitCount > 1) {
		Decimal shorter = decimal;
		shorter.mantissa.divideBy10();
		++shorter.exponent;
		--shorter.digitCount;
		Decimal shorterUp = shorter;
		shorterUp.mantissa.multiplyAdd(1, 1);
		if ((!shorter.mantissa.isZero() && roundsTo(shorter, magnitude)) || roundsTo(shorterUp, magnitude)) {
			fail("output is not the shortest: " + context);
		}
	}
	// A neighbor of the same length that also rounds to the value must not be closer, and when it is exactly as close
	// the last digit must be even. The largest finite value is the exception: Numbstrict never rounds its last digit
	// up, so that the text stays below the overflow threshold.
	if (decimal.digitCount > 0 && magnitude != std::numeric_limits<T>::max()) {
		const Binary twice = { binaryOf(magnitude).mantissa, binaryOf(magnitude).exponent + 1 };
		Decimal below = decimal;
		below.mantissa.subtractOne();
		Decimal above = decimal;
		above.mantissa.multiplyAdd(1, 1);
		Decimal belowMiddle = decimal;	// halfway to `below`, doubled so that it stays an integer
		belowMiddle.mantissa.multiplyAdd(2, 0).subtractOne();
		Decimal aboveMiddle = decimal;
		aboveMiddle.mantissa.multiplyAdd(2, 1);
		const bool belowFits = (!below.mantissa.isZero() && roundsTo(below, magnitude));
		const int belowOrder = (belowFits ? compare(belowMiddle, twice) : -1);	// midpoint to `below` vs the value
		const int aboveOrder = (roundsTo(above, magnitude) ? compare(aboveMiddle, twice) : 1);
		if (belowOrder > 0 || aboveOrder < 0) {
			fail("output is not the closest of its length: " + context);
		}
		Natural lastDigit = decimal.mantissa;
		if ((belowOrder == 0 || aboveOrder == 0) && lastDigit.divideBy10() % 2 != 0) {
			fail("output takes the odd digit of an exact tie: " + context);
		}
	}
}

/*
	Builds a number string in Numbstrict's real syntax from the input and checks that both parsers round it correctly.
	`flags` bits: 0 minus sign, 1 plus sign, 2 infinity or nan, 3 nan, 4 exponent, 5 huge exponent, 6 'E', 7 '+' on
	the exponent. Mantissa bytes give a digit (modulo 11) or, for 10, the decimal point.
*/
static std::string buildNumber(const unsigned char* data, size_t size) {
	const unsigned flags = (size > 12 ? data[12] : 0);
	uint32_t exponent = 0;
	for (size_t i = 13; i < 17 && i < size; ++i) {
		exponent = (exponent << 8) | data[i];
	}
	if ((flags & 4) != 0) {
		return (flags & 8) != 0 ? "nan" : std::string((flags & 1) != 0 ? "-" : (flags & 2) != 0 ? "+" : "") + "inf";
	}
	std::string integerPart;
	std::string fractionPart;
	bool afterPoint = false;
	for (size_t i = 17; i < size; ++i) {
		const int symbol = data[i] % 11;
		if (symbol == 10) {
			afterPoint = true;
		} else if (afterPoint) {
			fractionPart += static_cast<char>('0' + symbol);
		} else if (!integerPart.empty() || symbol != 0) {
			integerPart += static_cast<char>('0' + symbol);
		}
	}
	std::string number = ((flags & 1) != 0 ? "-" : (flags & 2) != 0 ? "+" : "");
	number += (integerPart.empty() ? "0" : integerPart);
	if (!fractionPart.empty()) {
		number += "." + fractionPart;
	}
	if ((flags & 16) != 0) {
		const int32_t signedExponent = static_cast<int32_t>(exponent);
		number += ((flags & 64) != 0 ? "E" : "e");
		number += (signedExponent < 0 ? "-" : (flags & 128) != 0 ? "+" : "");
		const uint32_t magnitude = (signedExponent < 0 ? 0u - exponent : exponent);
		char digits[16];
		snprintf(digits, sizeof digits, "%u", static_cast<unsigned>(magnitude));
		number += ((flags & 32) != 0 && magnitude != 0 ? "99999999999" : "") + std::string(digits);
	}
	return number;
}

template<typename T> static void checkParse(const std::string& text) {
	size_t next;
	const T value = Traits<T>::fromString(text, &next);
	const std::string context = text + " parsed as " + describe(value);
	if (next != text.size()) {
		fail("parse stopped early: " + context);
	}
	if (text == "nan") {
		if (!std::isnan(value)) {
			fail("nan expected: " + context);
		}
		return;
	}
	bool negative = (text[0] == '-');
	Decimal decimal;
	const bool isInfinity = (text.find("inf") != std::string::npos);
	if (!isInfinity && !readDecimal(text, negative, decimal)) {
		fail("the generated number is not readable: " + text);
	}
	if (std::isnan(value) || std::signbit(value) != negative
			|| (isInfinity ? !std::isinf(value) : !roundsTo(decimal, std::fabs(value)))) {
		fail("not the correctly rounded value: " + context);
	}
}

extern "C" int LLVMFuzzerTestOneInput(const unsigned char* data, size_t size) {
	unsigned char head[12] = { 0 };
	memcpy(head, data, size < sizeof head ? size : sizeof head);
	double doubleValue;
	memcpy(&doubleValue, head, sizeof doubleValue);
	float floatValue;
	memcpy(&floatValue, head + 8, sizeof floatValue);
	checkFormat(doubleValue);
	checkFormat(floatValue);
	const std::string number = buildNumber(data, size);
	checkParse<double>(number);
	checkParse<float>(number);
	return 0;
}
