#include "../src/Numbstrict.h"
#include <cassert>
#include <random>
#include <string>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <type_traits>

using Numbstrict::String;

template<typename T> static String toString(T value);
template<> String toString<float>(float value) { return Numbstrict::floatToString(value); }
template<> String toString<double>(double value) { return Numbstrict::doubleToString(value); }

template<typename T> static T fromString(const String& s);
template<> float fromString<float>(const String& s) { return Numbstrict::stringToFloat(s); }
template<> double fromString<double>(const String& s) { return Numbstrict::stringToDouble(s); }

template<typename T> static bool bitsEqual(T a, T b);
template<> bool bitsEqual<float>(float a, float b) {
	uint32_t ua, ub;
	std::memcpy(&ua, &a, 4);
	std::memcpy(&ub, &b, 4);
	return ua == ub;
}
template<> bool bitsEqual<double>(double a, double b) {
	uint64_t ua, ub;
	std::memcpy(&ua, &a, 8);
	std::memcpy(&ub, &b, 8);
	return ua == ub;
}

template<typename T> static bool isShortest(const String& s, T value) {
	// split into mantissa + exponent (support 'e' or 'E')
	size_t expPos = s.find('e');
	if (expPos == String::npos) expPos = s.find('E');
	const String exponent = (expPos == String::npos ? String() : s.substr(expPos));
	String mantissa = (expPos == String::npos ? s : s.substr(0, expPos));
	size_t dotPos = mantissa.find('.');

	const size_t signOffset = (mantissa[0] == '-' || mantissa[0] == '+' ? 1 : 0);

	// 1) Try removing a trailing ".0" entirely (e.g., "123.0" -> "123")
	if (dotPos != String::npos && mantissa.back() == '0' && mantissa[mantissa.size() - 2] == '.') {
		if (expPos == String::npos || mantissa.size() - dotPos > 2) {
			String shortened = mantissa.substr(0, mantissa.size() - 2) + exponent;
			T r = fromString<T>(shortened);
			if (bitsEqual<T>(r, value)) return false;
		}
	}

	// 2) Trim digits but keep at least one fractional digit when exponent is present
	String work = mantissa;
	while (work.size() > signOffset + 1) {
		if (expPos != String::npos && dotPos != String::npos) {
			if (work.size() - dotPos <= 2) {
				break;
			}
		}
		// drop last char (a digit)
		work.erase(work.size() - 1);

		// if we ended on '.', try without the dot as well
		if (!work.empty() && work.back() == '.') {
			if (expPos == String::npos) {
				String shortened = work.substr(0, work.size() - 1) + exponent;
				T r = fromString<T>(shortened);
				if (bitsEqual<T>(r, value)) return false;
			}
			// stop further trimming when dot reached (no more fractional digits to drop safely here)
			break;
		} else {
			String shortened = work + exponent;
			T r = fromString<T>(shortened);
			if (bitsEqual<T>(r, value)) return false;
		}
	}

	return true;
}

template<typename T, typename Rng> static void testType(Rng& rng) {
	for (int i = 0; i != 1000000; ++i) {
		typename std::conditional<sizeof(T) == 4, uint32_t, uint64_t>::type bits = rng();
		T value;
		std::memcpy(&value, &bits, sizeof value);
		if (!std::isfinite(value)) {
			continue;
		}
		const String s = toString<T>(value);
		const T round = fromString<T>(s);
		assert(bitsEqual<T>(round, value));
		static_cast<void>(round);
		if (s.find(".0") != String::npos && s.find('e') == String::npos && s.find('E') == String::npos) {
			continue;
		}
		assert(isShortest<T>(s, value));
	}
}

int main() {
	std::mt19937_64 rng(1234567);
	testType<double>(rng);
	testType<float>(rng);
	return 0;
}
