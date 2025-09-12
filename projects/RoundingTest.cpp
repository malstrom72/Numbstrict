//
//  RoundingTest.cpp
//  RoundingTest
//
//  Created by Magnus Lidström on 2025-09-10.
//

#include <iostream>
#include <cstdint>
#include <iomanip>
#include "Numbstrict.h"

inline uint64_t doubleToBits(const double d) {
	char buffer[8];
	memcpy(buffer, &d, sizeof (buffer));
	uint64_t v;
	memcpy(&v, buffer, sizeof (buffer));
	return v;
}

inline uint32_t floatToBits(const float f) {
	char buffer[4];
	memcpy(buffer, &f, sizeof (buffer));
	uint32_t v;
	memcpy(&v, buffer, sizeof (buffer));
	return v;
}

inline double bitsToDouble(const uint64_t i) {
	char buffer[8];
	memcpy(buffer, &i, sizeof (buffer));
	double d;
	memcpy(&d, buffer, sizeof (buffer));
	return d;
}

inline float bitsToFloat(const uint32_t i) {
	char buffer[4];
	memcpy(buffer, &i, sizeof (buffer));
	float f;
	memcpy(&f, buffer, sizeof (buffer));
	return f;
}

int main(int argc, const char* argv[]) {
/*	const std::string source = "-7.038531e-26";
	const float f = Numbstrict::stringToFloat(source);
	const uint32_t ui32 = floatToBits(f);
	const std::string back = Numbstrict::floatToString(f);
	std::cout << source << " = " << std::setprecision(20) << f << " = " << std::hex << ui32 << " -> " << back << std::endl;
	for (uint32_t i = ui32 - 2; i <= ui32 + 2; ++i) {
		std::cout << std::hex << i << " = " << std::setprecision(20) << bitsToFloat(i) << std::endl;
	}
*/
	const std::string source = "-1.365649281442437e-308";
	const double d = Numbstrict::stringToDouble(source);
//	const std::string back = Numbstrict::doubleToString(d);
	const uint64_t i64 = doubleToBits(d);
	std::cout << source << " = " << std::hex << i64 << std::endl; // << " = " << back << std::endl;
	for (uint64_t i = i64 - 1; i <= i64 + 1; ++i) {
		std::cout << std::hex << i << " = " << std::setprecision(20) << bitsToDouble(i) << std::endl;
	}

	return 0;
}
