#include "ryu/ryu.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static std::string ryuDouble(double v) {
	if (v == 0.0) return "0.0";
	char buf[64];
	int len = d2s_buffered_n(v, buf);
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

int main(int argc, char** argv) {
	if (argc != 2) {
		std::fprintf(stderr, "Usage: %s <hex double>\n", argv[0]);
		return 1;
	}
	const char* hex = argv[1];
	if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex += 2;
	char* end;
	unsigned long long u = std::strtoull(hex, &end, 16);
	if (!hex[0] || *end) {
		std::fprintf(stderr, "Invalid hex value\n");
		return 1;
	}
	double v;
	std::memcpy(&v, &u, sizeof v);
	std::string out = ryuDouble(v);
	std::printf("%s\n", out.c_str());
	return 0;
}

