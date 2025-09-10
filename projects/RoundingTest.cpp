//
//  RoundingTest.cpp
//  RoundingTest
//
//  Created by Magnus Lidström on 2025-09-10.
//

#include <iostream>
#include "Numbstrict.h"

int main(int argc, const char* argv[]) {
	const std::string source = "1.945478849582046e-308";
	const double d = Numbstrict::stringToDouble(source);
	const std::string back = Numbstrict::doubleToString(d);
	std::cout << source << " = " << back << std::endl;
	return 0;
}
