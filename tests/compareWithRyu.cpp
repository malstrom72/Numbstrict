#include "Numbstrict.h"
#include "ryu/ryu.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <random>
#include <string>
#include <cstdlib>
#include <cstdio>

static std::string ryuDouble(double v) {
	if (v == 0.0) return "0.0";
	char buf[64];
	int len = d2s_buffered_n(v, buf);
	std::string s(buf, len);
	bool neg = false;
	if (s[0] == '-') {
		neg = true;
		s.erase(0, 1);
	}
	size_t ePos = s.find('E');
	std::string mant = s.substr(0, ePos);
	int exp = std::stoi(s.substr(ePos + 1));
	size_t dot = mant.find('.');
	std::string intPart = mant.substr(0, dot);
	std::string fracPart = dot == std::string::npos ? "" : mant.substr(dot + 1);
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

static std::string ryuFloat(float v) {
	if (v == 0.0f) return "0.0";
	char buf[32];
	int len = f2s_buffered_n(v, buf);
	std::string s(buf, len);
	bool neg = false;
	if (s[0] == '-') {
		neg = true;
		s.erase(0, 1);
	}
	size_t ePos = s.find('E');
	std::string mant = s.substr(0, ePos);
	int exp = std::stoi(s.substr(ePos + 1));
	size_t dot = mant.find('.');
	std::string intPart = mant.substr(0, dot);
	std::string fracPart = dot == std::string::npos ? "" : mant.substr(dot + 1);
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

static uint64_t bits(double v) { uint64_t u; std::memcpy(&u, &v, sizeof u); return u; }
static uint32_t bits(float v) { uint32_t u; std::memcpy(&u, &v, sizeof u); return u; }

int main() {
	double dEdge[] = {0.0, 1.0, 1000.0, 1e20, 1e-5, 1e-7};
	for (double v : dEdge) {
		std::string ours = Numbstrict::doubleToString(v);
		std::string oracle = ryuDouble(v);
		assert(ours == oracle);
		double round = std::strtod(ours.c_str(), nullptr);
		assert(bits(round) == bits(v));
		static_cast<void>(round);
	}

	float fEdge[] = {0.0f, 1.0f, 1000.0f, 1e20f, 1e-5f, 1e-7f};
	for (float v : fEdge) {
		std::string ours = Numbstrict::floatToString(v);
		std::string oracle = ryuFloat(v);
		assert(ours == oracle);
		float round = std::strtof(ours.c_str(), nullptr);
		assert(bits(round) == bits(v));
		static_cast<void>(round);
	}

        std::mt19937_64 drng(123456);
        std::uniform_int_distribution<uint64_t> ddist;
        for (int i = 0; i < 1000000; ++i) {
                uint64_t u = ddist(drng);
                double v;
                std::memcpy(&v, &u, sizeof v);
                if (!std::isfinite(v)) continue;
                std::string ours = Numbstrict::doubleToString(v);
                std::string oracle = ryuDouble(v);
                if (ours != oracle) {
                        std::printf("double mismatch\nbits: %016llx\n ours: %s\n ryu:  %s\n", (unsigned long long)bits(v), ours.c_str(), oracle.c_str());
                        return 1;
                }
                double round = std::strtod(ours.c_str(), nullptr);
                assert(bits(round) == bits(v));
                static_cast<void>(round);
        }

        std::mt19937 frng(654321);
        std::uniform_int_distribution<uint32_t> fdist;
        for (int i = 0; i < 1000000; ++i) {
                uint32_t u = fdist(frng);
                float v;
                std::memcpy(&v, &u, sizeof v);
                if (!std::isfinite(v)) continue;
                std::string ours = Numbstrict::floatToString(v);
                std::string oracle = ryuFloat(v);
                if (ours != oracle) {
                        std::printf("float mismatch\nbits: %08x\n ours: %s\n ryu:  %s\n", bits(v), ours.c_str(), oracle.c_str());
                        return 1;
                }
                float round = std::strtof(ours.c_str(), nullptr);
                assert(bits(round) == bits(v));
                static_cast<void>(round);
        }

	return 0;
}
