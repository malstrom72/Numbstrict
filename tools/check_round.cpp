#include "../src/Numbstrict.h"
#include <iostream>
#include <iomanip>
#include <cstring>
#include <cstdlib>
#include <cstdint>

static uint64_t bits(double v){ uint64_t u; std::memcpy(&u, &v, sizeof u); return u; }

int main(){
	double v;
	uint64_t u = 0x8009d1f053c113dcULL; // from test output
	std::memcpy(&v, &u, sizeof v);
	std::string ours = Numbstrict::doubleToString(v);
	double parsed = std::strtod(ours.c_str(), nullptr);
	std::cout << "orig bits:   " << std::hex << std::setw(16) << std::setfill('0') << bits(v) << "\n";
	std::cout << "ours string: " << ours << "\n";
	std::cout << "pars bits:   " << std::hex << std::setw(16) << std::setfill('0') << bits(parsed) << "\n";
	return 0;
}
