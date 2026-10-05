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
	size_t expPos = s.find('e');
	if (expPos == String::npos) expPos = s.find('E');
	const String exponent = (expPos == String::npos ? String() : s.substr(expPos));
	String mantissa = (expPos == String::npos ? s : s.substr(0, expPos));

	if (mantissa.empty()) return true;

	const size_t signOffset = (mantissa[0] == '-' || mantissa[0] == '+' ? 1 : 0);
	if (mantissa.size() < signOffset + 1) return true;

	// If mantissa ends with '.', try dropping it (e.g., "123." -> "123")
	if (mantissa.size() > signOffset + 1 && mantissa.back() == '.') {
		String shortened = mantissa.substr(0, mantissa.size() - 1) + exponent;
		T r = fromString<T>(shortened);
		if (bitsEqual<T>(r, value)) return false;
	}

	// Try removing a trailing ".0" (e.g., "123.0" -> "123"), but keep 1 frac digit in E-form
	size_t dotPos0 = mantissa.find('.');
	if (dotPos0 != String::npos && mantissa.size() >= 2 && mantissa.back() == '0' && mantissa[mantissa.size() - 2] == '.') {
		if (expPos == String::npos || (mantissa.size() - dotPos0 > 2)) {
			String shortened = mantissa.substr(0, mantissa.size() - 2) + exponent;
			T r = fromString<T>(shortened);
			if (bitsEqual<T>(r, value)) return false;
		}
	}

	// Trim last coefficient digit while preserving:
	// - round-trip equality
	// - at least 1 fractional digit when exponent is present
	String work = mantissa;
	while (work.size() > signOffset + 1) {
		// recompute dot every iteration (the original code used a stale position)
		size_t dotPos = work.find('.');

		// In E-form, keep at least one fractional digit (i.e., don't let "x.y" shrink to "x.")
		if (expPos != String::npos && dotPos != String::npos) {
			size_t fracLen = work.size() - dotPos - 1; // digits after '.'
			if (fracLen <= 1) break;
		}

		// drop last char
		work.erase(work.size() - 1);

		// if we ended on '.', try without the dot as well (only allowed in fixed form)
		if (!work.empty() && work.back() == '.') {
			if (expPos == String::npos) {
				String shortened = work.substr(0, work.size() - 1) + exponent;
				T r = fromString<T>(shortened);
				if (bitsEqual<T>(r, value)) return false;
			}
			break; // stop at the dot
		} else {
			String shortened = work + exponent;
			T r = fromString<T>(shortened);
			if (bitsEqual<T>(r, value)) return false;
		}
	}
	return true;
}

template<typename T, typename Rng> static void testType(Rng& rng) {
	for (int i = 0; i != 10000000; ++i) {
		typename std::conditional<sizeof(T) == 4, uint32_t, uint64_t>::type bits = rng();
		T value;
		std::memcpy(&value, &bits, sizeof value);
		if (!std::isfinite(value)) continue;

		const String s = toString<T>(value);
		const T round = fromString<T>(s);
		assert(bitsEqual<T>(round, value));
		static_cast<void>(round);

		assert(isShortest<T>(s, value));
	}
}

int main() {
	std::mt19937_64 rng(1234567);
	testType<double>(rng);
	testType<float>(rng);
	return 0;
}
