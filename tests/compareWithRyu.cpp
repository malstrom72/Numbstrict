#include "Numbstrict.h"
#include "ryu/ryu.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <cinttypes>
#include <iostream>
#include <limits>

static std::string ryuDouble(double v) {
	if (v == 0.0) return (std::signbit(v) ? "-0.0" : "0.0");
	if (std::isnan(v)) return "nan";
	if (std::isinf(v)) return v < 0.0 ? "-inf" : "inf";
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

static std::string ryuFloat(float v) {
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

static uint64_t bits(double v) { uint64_t u; std::memcpy(&u, &v, sizeof u); return u; }
static uint32_t bits(float v) { uint32_t u; std::memcpy(&u, &v, sizeof u); return u; }

static std::string toHex64(uint64_t value) {
	std::ostringstream stream;
	stream << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << value;
	return stream.str();
}

static std::string toHex32(uint32_t value) {
	std::ostringstream stream;
	stream << std::hex << std::nouppercase << std::setfill('0') << std::setw(8) << value;
	return stream.str();
}

static bool is_decimal_tie_equivalent(const std::string& a, const std::string& b) {
	if ((a.size() && a[0] == '-') != (b.size() && b[0] == '-')) return false;
	auto split = [](const std::string& s) {
		size_t e = s.find('e');
		std::string m = (e == std::string::npos ? s : s.substr(0, e));
		std::string x = (e == std::string::npos ? "" : s.substr(e));
		return std::pair<std::string,std::string>(m, x);
	};
	std::pair<std::string,std::string> pa = split(a[0]=='-' ? a.substr(1) : a);
	std::pair<std::string,std::string> pb = split(b[0]=='-' ? b.substr(1) : b);
	const std::string& ma = pa.first;
	const std::string& xa = pa.second;
	const std::string& mb = pb.first;
	const std::string& xb = pb.second;
	if (xa != xb) return false;
	if (ma.size() != mb.size()) return false;
	if (ma.empty()) return false;
	size_t diff = 0, pos = 0;
	for (size_t i = 0; i < ma.size(); ++i) {
		if (ma[i] != mb[i]) { ++diff; pos = i; }
	}
	if (diff != 1) return false;
	if (ma[pos] < '0' || ma[pos] > '9' || mb[pos] < '0' || mb[pos] > '9') return false;
	int da = ma[pos] - '0';
	int db = mb[pos] - '0';
	return std::abs(da - db) == 1;
}

struct FragileDoubleCase {
	uint64_t bits;
	const char* expected;
};

struct FragileFloatCase {
	uint32_t bits;
	const char* expected;
};

static const FragileDoubleCase kFragileDoubles[] = {
	{ 0x0000000000000000ull, "0.0" },
	{ 0x0000000000000001ull, "5.0e-324" },
	{ 0x0000000000000002ull, "1.0e-323" },
	{ 0x000ffffffffffff8ull, "2.2250738585071974e-308" },
	{ 0x000ffffffffffff9ull, "2.225073858507198e-308" },
	{ 0x000ffffffffffffaull, "2.2250738585071984e-308" },
	{ 0x000ffffffffffffbull, "2.225073858507199e-308" },
	{ 0x000ffffffffffffcull, "2.2250738585071994e-308" },
	{ 0x000ffffffffffffdull, "2.2250738585072e-308" },
	{ 0x000ffffffffffffeull, "2.2250738585072004e-308" },
	{ 0x000fffffffffffffull, "2.225073858507201e-308" },
	{ 0x0010000000000000ull, "2.2250738585072014e-308" },
	{ 0x0010000000000001ull, "2.225073858507202e-308" },
	{ 0x0010000000000002ull, "2.2250738585072024e-308" },
	{ 0x0010000000000003ull, "2.225073858507203e-308" },
	{ 0x0010000000000004ull, "2.2250738585072034e-308" },
	{ 0x0010000000000005ull, "2.225073858507204e-308" },
	{ 0x0010000000000006ull, "2.2250738585072043e-308" },
	{ 0x0010000000000007ull, "2.225073858507205e-308" },
	{ 0x0010000000000008ull, "2.2250738585072053e-308" },
	{ 0x3e7ad7f29abcaf47ull, "9.999999999999998e-8" },
	{ 0x3e7ad7f29abcaf49ull, "1.0000000000000001e-7" },
	{ 0x3eb0c6f7a0b5ed8cull, "9.999999999999997e-7" },
	{ 0x3eb0c6f7a0b5ed8eull, "0.0000010000000000000002" },
	{ 0x4202a05f1fffffffull, "9999999999.999998" },
	{ 0x4202a05f20000001ull, "1.0000000000000002e+10" },
	{ 0x7feffffffffffffeull, "1.7976931348623155e+308" },
	{ 0x7fefffffffffffffull, "1.7976931348623157e+308" },
	{ 0x7ff0000000000000ull, "inf" },
	{ 0x8000000000000001ull, "-5.0e-324" },
	{ 0x8000000000000002ull, "-1.0e-323" },
	{ 0x8009d1f053c113dcull, "-1.3656492814424367e-308" },	// ours: -1.3656492814424367e-308
	{ 0x800ffffffffffffeull, "-2.2250738585072004e-308" },
	{ 0x800fffffffffffffull, "-2.225073858507201e-308" },
	{ 0x8010000000000000ull, "-2.2250738585072014e-308" },
	{ 0x8010000000000001ull, "-2.225073858507202e-308" },
	{ 0xbe7ad7f29abcaf47ull, "-9.999999999999998e-8" },
	{ 0xbe7ad7f29abcaf49ull, "-1.0000000000000001e-7" },
	{ 0xbeb0c6f7a0b5ed8cull, "-9.999999999999997e-7" },
	{ 0xbeb0c6f7a0b5ed8eull, "-0.0000010000000000000002" },
	{ 0xc202a05f1fffffffull, "-9999999999.999998" },
	{ 0xc202a05f20000001ull, "-1.0000000000000002e+10" },
	{ 0xffeffffffffffffeull, "-1.7976931348623155e+308" },
	{ 0xffefffffffffffffull, "-1.7976931348623157e+308" },
	{ 0xfff0000000000000ull, "-inf" },
};

static const FragileFloatCase kFragileFloats[] = {
	{ 0x00000000u, "0.0" },
	{ 0x00000001u, "1.0e-45" },
	{ 0x00000002u, "3.0e-45" },
	{ 0x007ffffeu, "1.1754941e-38" },
	{ 0x007fffffu, "1.1754942e-38" },
	{ 0x00800000u, "1.1754944e-38" },
	{ 0x00800001u, "1.1754945e-38" },
	{ 0x33d6bf94u, "9.9999994e-8" },
	{ 0x33d6bf96u, "1.0000001e-7" },
	{ 0x358637bcu, "9.999999e-7" },
	{ 0x358637beu, "0.0000010000001" },
	{ 0x501502f8u, "9999999000.0" },
	{ 0x501502fau, "1.0000001e+10" },
	{ 0x7f800000u, "inf" },
	{ 0x80000001u, "-1.0e-45" },
	{ 0x80000002u, "-3.0e-45" },
	{ 0x807ffffeu, "-1.1754941e-38" },
	{ 0x807fffffu, "-1.1754942e-38" },
	{ 0x80800000u, "-1.1754944e-38" },
	{ 0x80800001u, "-1.1754945e-38" },
	{ 0x8d2eaca7u, "-5.382571e-31" },	// ours: -5.3825712e-31
	{ 0x95ae43feu, "-7.0385313e-26" },	// ours: -7.038531e-26 (reported)
	{ 0xb3d6bf94u, "-9.9999994e-8" },
	{ 0xb3d6bf96u, "-1.0000001e-7" },
	{ 0xb58637bcu, "-9.999999e-7" },
	{ 0xb58637beu, "-0.0000010000001" },
	{ 0xd01502f8u, "-9999999000.0" },
	{ 0xd01502fau, "-1.0000001e+10" },
	{ 0xeb000000u, "-1.5474251e+26" },	// ours: -1.5474250e+26
	{ 0xff800000u, "-inf" },
};

static void emitDoubleListEntry(uint64_t valueBits, const std::string& oracle) {
	std::cout << "	{ 0x" << toHex64(valueBits) << "ull, \"" << oracle << "\" }" << '\n';
}

static void emitFloatListEntry(uint32_t valueBits, const std::string& oracle) {
	std::cout << "	{ 0x" << toHex32(valueBits) << "u, \"" << oracle << "\" }" << '\n';
}

static bool verifyDoubleCase(double v, const std::string& oracle, const char* context, bool printEntry) {
	std::string ours = Numbstrict::doubleToString(v);
	const uint64_t valueBits = bits(v);
	if (ours != oracle) {
		double ra = std::strtod(ours.c_str(), nullptr);
		double rb = std::strtod(oracle.c_str(), nullptr);
		if (!(bits(ra) == valueBits && bits(rb) == valueBits && is_decimal_tie_equivalent(ours, oracle))) {
			std::printf("%s\nbits: %016" PRIx64 "\n ours: %s\n ryu:  %s\n", context, valueBits, ours.c_str(), oracle.c_str());
			if (printEntry) emitDoubleListEntry(valueBits, oracle);
			return false;
		}
	}
	double round = std::strtod(ours.c_str(), nullptr);
	const uint64_t roundBits = bits(round);
	if (roundBits != valueBits) {
		std::printf("%s round-trip mismatch\nbits: %016" PRIx64 "\n str: %s\n round_bits: %016" PRIx64 "\n", context, valueBits, ours.c_str(), roundBits);
		if (printEntry) emitDoubleListEntry(valueBits, oracle);
		return false;
	}
	double na = Numbstrict::stringToDouble(ours);
	const uint64_t oursBits = bits(na);
	if (!(oursBits == valueBits || (v == 0.0 && na == 0.0))) {
		std::printf("%s stringToDouble(ours) mismatch\nbits: %016" PRIx64 "\n ours: %s\n result_bits: %016" PRIx64 "\n", context, valueBits, ours.c_str(), oursBits);
		if (printEntry) emitDoubleListEntry(valueBits, oracle);
		return false;
	}
	double nb = Numbstrict::stringToDouble(oracle);
	const uint64_t oracleBits = bits(nb);
	if (!(oracleBits == valueBits || (v == 0.0 && nb == 0.0))) {
		std::printf("%s stringToDouble(ryu) mismatch\nbits: %016" PRIx64 "\n ryu:  %s\n result_bits: %016" PRIx64 "\n", context, valueBits, oracle.c_str(), oracleBits);
		if (printEntry) emitDoubleListEntry(valueBits, oracle);
		return false;
	}
	return true;
}

static bool verifyFloatCase(float v, const std::string& oracle, const char* context, bool printEntry) {
	std::string ours = Numbstrict::floatToString(v);
	if (ours != oracle) {
		float ra = std::strtof(ours.c_str(), nullptr);
		float rb = std::strtof(oracle.c_str(), nullptr);
		if (!(bits(ra) == bits(v) && bits(rb) == bits(v) && is_decimal_tie_equivalent(ours, oracle))) {
			std::printf("%s\nbits: %08x\n ours: %s\n ryu:  %s\n", context, bits(v), ours.c_str(), oracle.c_str());
			if (printEntry) emitFloatListEntry(bits(v), oracle);
			return false;
		}
	}
	float round = std::strtof(ours.c_str(), nullptr);
	if (bits(round) != bits(v)) {
		std::printf("%s round-trip mismatch\nbits: %08x\n str: %s\n round_bits: %08x\n", context, bits(v), ours.c_str(), bits(round));
		if (printEntry) emitFloatListEntry(bits(v), oracle);
		return false;
	}
	float na = Numbstrict::stringToFloat(ours);
	if (!(bits(na) == bits(v) || (v == 0.0f && na == 0.0f))) {
		std::printf("%s stringToFloat(ours) mismatch\nbits: %08x\n ours: %s\n result_bits: %08x\n", context, bits(v), ours.c_str(), bits(na));
		if (printEntry) emitFloatListEntry(bits(v), oracle);
		return false;
	}
	float nb = Numbstrict::stringToFloat(oracle);
	if (!(bits(nb) == bits(v) || (v == 0.0f && nb == 0.0f))) {
		std::printf("%s stringToFloat(ryu) mismatch\nbits: %08x\n ryu:  %s\n result_bits: %08x\n", context, bits(v), oracle.c_str(), bits(nb));
		if (printEntry) emitFloatListEntry(bits(v), oracle);
		return false;
	}
	return true;
}

int main(int argc, char** argv) {
	bool testDouble = false;
	bool testFloat = false;
	int testCount = 1000000;
	bool seedProvided = false;
	uint64_t seedValue = 0u;
	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "double")) { testDouble = true; continue; }
		if (!std::strcmp(argv[i], "float")) { testFloat = true; continue; }
		const char* seedPrefix = "seed=";
		if (!std::strncmp(argv[i], seedPrefix, 5)) {
			char* endSeed = nullptr;
			uint64_t parsed = static_cast<uint64_t>(std::strtoull(argv[i] + 5, &endSeed, 10));
			if (endSeed && *endSeed == '\0') {
				seedProvided = true;
				seedValue = parsed;
				continue;
			}
		}
		char* end = nullptr;
		int64_t parsedCount = static_cast<int64_t>(std::strtoll(argv[i], &end, 10));
		if (end && *end == '\0' && parsedCount > 0) { testCount = static_cast<int>(parsedCount); continue; }
	}
	if (!testDouble && !testFloat) { testDouble = true; testFloat = true; }

	if (testDouble) {
		for (const FragileDoubleCase& entry : kFragileDoubles) {
			double v; std::memcpy(&v, &entry.bits, sizeof v);
			if (!verifyDoubleCase(v, entry.expected, "double mismatch (fragile)", true)) return 1;
		}
	}
	if (testFloat) {
		for (const FragileFloatCase& entry : kFragileFloats) {
			float v; std::memcpy(&v, &entry.bits, sizeof v);
			if (!verifyFloatCase(v, entry.expected, "float mismatch (fragile)", true)) return 1;
		}
	}

	const uint64_t doubleSeed = seedProvided ? seedValue : 123456ull;
	const uint64_t floatSeed = seedProvided ? (seedValue ^ 0x9e3779b97f4a7c15ull) : 654321ull;
	std::mt19937_64 drng(static_cast<std::mt19937_64::result_type>(doubleSeed));
	std::uniform_int_distribution<uint64_t> ddist;
	std::mt19937 frng(static_cast<std::mt19937::result_type>(static_cast<unsigned int>(floatSeed & 0xffffffffu)));
	std::uniform_int_distribution<uint32_t> fdist;

	for (int i = 0; i < testCount; ++i) {
		if (testDouble) {
			uint64_t u = ddist(drng);
			double v; std::memcpy(&v, &u, sizeof v);
			if (std::isfinite(v)) {
				std::string oracle = ryuDouble(v);
				if (!verifyDoubleCase(v, oracle, "double mismatch", true)) return 1;
			}
		}
		if (testFloat) {
			uint32_t u = fdist(frng);
			float v; std::memcpy(&v, &u, sizeof v);
			if (std::isfinite(v)) {
				std::string oracle = ryuFloat(v);
				if (!verifyFloatCase(v, oracle, "float mismatch", true)) return 1;
			}
		}
	}

	std::cout << "All tests passed\n";
	return 0;
}
