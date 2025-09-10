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

inline double bitsToDouble(const uint64_t i) {
	char buffer[8];
	memcpy(buffer, &i, sizeof (buffer));
	double d;
	memcpy(&d, buffer, sizeof (buffer));
	return d;
}

int main(int argc, const char* argv[]) {
	const std::string source = "1.945478849582046e-308";
	const double d = Numbstrict::stringToDouble(source);
	const std::string back = Numbstrict::doubleToString(d);
	const uint64_t i64 = doubleToBits(d);
	std::cout << source << " = " << std::hex << i64 << " = " << back << std::endl;
	for (uint64_t i = i64 - 1; i <= i64 + 1; ++i) {
		std::cout << std::hex << i << " = " << std::setprecision(std::numeric_limits<double>::max_digits10) << bitsToDouble(i) << std::endl;
	}
	return 0;
}
