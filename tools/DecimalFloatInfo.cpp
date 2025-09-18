#include "ryu/ryu.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

static std::string ryuFloatPretty(float v) {
	if (v == 0.0f) return (std::signbit(v) ? "-0.0" : "0.0");
	if (std::isnan(v)) return "nan";
	if (std::isinf(v)) return v < 0.0f ? "-inf" : "inf";
	char buf[32];
	int len = f2s_buffered_n(v, buf);
	std::string s(buf, len);
	bool neg = false;
	if (!s.empty() && s[0] == '-') { neg = true; s.erase(0, 1); }
	size_t ePos = s.find('E');
	std::string mant = (ePos == std::string::npos ? s : s.substr(0, ePos));
	int exp = 0;
	if (ePos != std::string::npos) {
		exp = std::stoi(s.substr(ePos + 1));
	}
	size_t dot = mant.find('.');
	std::string intPart = mant.substr(0, dot);
	std::string fracPart = (dot == std::string::npos ? "" : mant.substr(dot + 1));
	const int NEG_E = -6;
	const int POS_E = 10;
	std::string result;
	if (exp < NEG_E || exp >= POS_E) {
		result = intPart;
		if (!fracPart.empty()) result += '.' + fracPart; else result += ".0";
		result += 'e';
		result += (exp >= 0 ? '+' : '-');
		result += std::to_string(exp >= 0 ? exp : -exp);
	} else {
		std::string digits = intPart + fracPart;
		int decimalPos = static_cast<int>(intPart.size()) + exp;
		if (decimalPos <= 0) {
			result = "0.";
			result.append(-decimalPos, '0');
			result += digits;
		} else if (decimalPos >= static_cast<int>(digits.size())) {
			result = digits;
			result.append(decimalPos - digits.size(), '0');
			result += ".0";
		} else {
			result = digits;
			result.insert(decimalPos, ".");
		}
	}
	if (neg && result != "0.0") result.insert(0, "-");
	return result;
}

static std::string toHex32(uint32_t value) {
	std::ostringstream stream;
	stream << std::hex << std::nouppercase << std::setfill('0') << std::setw(8) << value;
	return stream.str();
}

int main(int argc, char** argv) {
	if (argc != 2) {
		std::fprintf(stderr, "Usage: %s <decimal-float>\n", argv[0]);
		return 1;
	}
	const char* s = argv[1];
	char* end = nullptr;
	float v = std::strtof(s, &end);
	if (!s[0] || (end && *end)) {
		std::fprintf(stderr, "Invalid decimal float input\n");
		return 1;
	}
	uint32_t bits;
	std::memcpy(&bits, &v, sizeof bits);
	std::string pretty = ryuFloatPretty(v);
	std::cout << pretty << "\n" << toHex32(bits) << "\n";
	return 0;
}

