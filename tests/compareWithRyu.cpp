#include "Numbstrict.h"
#include "ryu/ryu.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <random>
#include <string>
#include <utility>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <limits>

static std::string ryuDouble(double v) {
		if (v == 0.0) return "0.0";
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
				int decimalPos = static_cast<int>(intPart.size()) + exp; // exp can be 0
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

static bool is_decimal_tie_equivalent(const std::string& a, const std::string& b) {
		// same sign
		if ((a.size() && a[0] == '-') != (b.size() && b[0] == '-')) return false;

		// split mantissa/exponent (lowercase 'e' is what we emit)
		auto split = [](const std::string& s) {
				size_t e = s.find('e');
				std::string m = (e == std::string::npos ? s : s.substr(0, e));
				std::string x = (e == std::string::npos ? "" : s.substr(e)); // includes 'e' and sign
				return std::pair<std::string,std::string>(m, x);
		};
		std::pair<std::string,std::string> pa = split(a[0]=='-' ? a.substr(1) : a);
		std::pair<std::string,std::string> pb = split(b[0]=='-' ? b.substr(1) : b);
		const std::string& ma = pa.first;
		const std::string& xa = pa.second;
		const std::string& mb = pb.first;
		const std::string& xb = pb.second;
		if (xa != xb) return false; // require same exponent string

		// compare mantissas: identical except last digit differs by 1
		if (ma.size() != mb.size()) return false;
		if (ma.empty()) return false;

		size_t diff = 0, pos = 0;
		for (size_t i = 0; i < ma.size(); ++i) {
				if (ma[i] != mb[i]) { ++diff; pos = i; }
		}
		if (diff != 1) return false;

		// the differing char must be a digit and differ by exactly 1
		if (ma[pos] < '0' || ma[pos] > '9' || mb[pos] < '0' || mb[pos] > '9') return false;
		int da = ma[pos] - '0';
		int db = mb[pos] - '0';
		return std::abs(da - db) == 1;
}

int main(int argc, char** argv) {
/*	{
		// convert the string "-7.038531e-26" with ryu and output hex bits
		const char* s = "-7.038531e-26";
		char* end = nullptr;
		float v = std::strtof(s, &end);
		if (end != s + std::strlen(s)) {
			std::printf("strtof failed\n");
			return 1;
		}
		std::string ryu = ryuFloat(v);
		float vr = std::strtof(ryu.c_str(), nullptr);
		std::printf("bits: %08lx\n ryu:  %s\n", (unsigned long)bits(v), ryu.c_str());
	}*/
	const std::string source = "2.22507385850720088902458687609E-308";
	const double d = Numbstrict::stringToDouble(source);
	std::cout << d << std::endl;
		bool testDouble = false;
		bool testFloat = false;
		int testCount = 1000000; /// default random-test count
		for (int i = 1; i < argc; ++i) {
				if (!std::strcmp(argv[i], "double")) { testDouble = true; continue; }
				if (!std::strcmp(argv[i], "float")) { testFloat = true; continue; }
				char* end = nullptr;
				long long v = std::strtoll(argv[i], &end, 10);
				if (end && *end == '\0' && v > 0) { testCount = (int)v; continue; }
		}
		if (!testDouble && !testFloat) { testDouble = true; testFloat = true; }

		if (testDouble) {
				double dEdge[] = {
					0.0,
					std::numeric_limits<double>::denorm_min(),
					std::nextafter(std::numeric_limits<double>::min(), 0.0),
					std::numeric_limits<double>::min(),
					std::numeric_limits<double>::max(),
					std::numeric_limits<double>::infinity(),
					-std::numeric_limits<double>::denorm_min(),
					-std::nextafter(std::numeric_limits<double>::min(), 0.0),
					-std::numeric_limits<double>::min(),
					-std::numeric_limits<double>::max(),
					-std::numeric_limits<double>::infinity()
				};
				for (double v : dEdge) {
						std::string ours = Numbstrict::doubleToString(v);
						std::string oracle = ryuDouble(v);
						assert(ours == oracle);
						double round = std::strtod(ours.c_str(), nullptr);
						assert(bits(round) == bits(v));
						double na = Numbstrict::stringToDouble(ours);
						double nb = Numbstrict::stringToDouble(oracle);
						assert(bits(na) == bits(v) || (v == 0.0 && na == 0.0));
						assert(bits(nb) == bits(v) || (v == 0.0 && nb == 0.0));
						static_cast<void>(round);
				}

				// ULP ring around DBL_MIN: a few under (subnormals), exactly at, and a few above
				{
						const double base = std::numeric_limits<double>::min();
						// exactly at DBL_MIN
						{
								const double v = base;
								std::string ours = Numbstrict::doubleToString(v);
								std::string oracle = ryuDouble(v);
								assert(ours == oracle);
								double round = std::strtod(ours.c_str(), nullptr);
								assert(bits(round) == bits(v));
								double na = Numbstrict::stringToDouble(ours);
								double nb = Numbstrict::stringToDouble(oracle);
								assert(bits(na) == bits(v) || (v == 0.0 && na == 0.0));
								assert(bits(nb) == bits(v) || (v == 0.0 && nb == 0.0));
								static_cast<void>(round);
						}
						// a few ULPs under DBL_MIN (into subnormals)
						double under = base;
						for (int i = 0; i < 8; ++i) {
								under = std::nextafter(under, 0.0);
								if (under == 0.0) break;
								std::string ours = Numbstrict::doubleToString(under);
								std::string oracle = ryuDouble(under);
								if (ours != oracle) {
										double ra = std::strtod(ours.c_str(), nullptr);
										double rb = std::strtod(oracle.c_str(), nullptr);
										if (bits(ra) == bits(under) && bits(rb) == bits(under) && is_decimal_tie_equivalent(ours, oracle)) {
												// accept tie canonicalization difference
										} else {
												std::printf("double mismatch (DBL_MIN under)\nbits: %016llx\n base: %s\n val:  %s\n ryu:  %s\n",
														(unsigned long long)bits(under), Numbstrict::doubleToString(base).c_str(), ours.c_str(), oracle.c_str());
												return 1;
										}
								}
								double round = std::strtod(ours.c_str(), nullptr);
								assert(bits(round) == bits(under));
								double na = Numbstrict::stringToDouble(ours);
								double nb = Numbstrict::stringToDouble(oracle);
								if (!(bits(na) == bits(under) || (under == 0.0 && na == 0.0))) {
										std::printf("stringToDouble(ours) mismatch (DBL_MIN under)\nbits: %016llx\n ours: %s\n", (unsigned long long)bits(under), ours.c_str());
										return 1;
								}
								if (!(bits(nb) == bits(under) || (under == 0.0 && nb == 0.0))) {
										std::printf("stringToDouble(ryu) mismatch (DBL_MIN under)\nbits: %016llx\n ryu:  %s\n", (unsigned long long)bits(under), oracle.c_str());
										return 1;
								}
								static_cast<void>(round);
						}
						// a few ULPs above DBL_MIN (still normal)
						double over = base;
						for (int i = 0; i < 8; ++i) {
								over = std::nextafter(over, std::numeric_limits<double>::infinity());
								std::string ours = Numbstrict::doubleToString(over);
								std::string oracle = ryuDouble(over);
								if (ours != oracle) {
										double ra = std::strtod(ours.c_str(), nullptr);
										double rb = std::strtod(oracle.c_str(), nullptr);
										if (bits(ra) == bits(over) && bits(rb) == bits(over) && is_decimal_tie_equivalent(ours, oracle)) {
												// accept tie canonicalization difference
										} else {
												std::printf("double mismatch (DBL_MIN over)\nbits: %016llx\n base: %s\n val:  %s\n ryu:  %s\n",
														(unsigned long long)bits(over), Numbstrict::doubleToString(base).c_str(), ours.c_str(), oracle.c_str());
												return 1;
										}
								}
								double round = std::strtod(ours.c_str(), nullptr);
								assert(bits(round) == bits(over));
								double na = Numbstrict::stringToDouble(ours);
								double nb = Numbstrict::stringToDouble(oracle);
								if (!(bits(na) == bits(over) || (over == 0.0 && na == 0.0))) {
										std::printf("stringToDouble(ours) mismatch (DBL_MIN over)\nbits: %016llx\n ours: %s\n", (unsigned long long)bits(over), ours.c_str());
										return 1;
								}
								if (!(bits(nb) == bits(over) || (over == 0.0 && nb == 0.0))) {
										std::printf("stringToDouble(ryu) mismatch (DBL_MIN over)\nbits: %016llx\n ryu:  %s\n", (unsigned long long)bits(over), oracle.c_str());
										return 1;
								}
								static_cast<void>(round);
						}
				}

				// One-ULP neighbors around selected edge cases
				for (double base : dEdge) {
						if (!std::isfinite(base) || base == 0.0) continue; // skip inf and +/-0 (avoid -0 rounding mismatch)
						double n1 = std::nextafter(base, -std::numeric_limits<double>::infinity());
						double n2 = std::nextafter(base, std::numeric_limits<double>::infinity());
						for (double v : { n1, n2 }) {
								if (!std::isfinite(v) || v == 0.0) continue;
								std::string ours = Numbstrict::doubleToString(v);
								std::string oracle = ryuDouble(v);
								if (ours != oracle) {
										double ra = std::strtod(ours.c_str(), nullptr);
										double rb = std::strtod(oracle.c_str(), nullptr);
										if (bits(ra) == bits(v) && bits(rb) == bits(v) && is_decimal_tie_equivalent(ours, oracle)) {
												// accept tie canonicalization difference
										} else {
												std::printf("double mismatch (neighbor)\nbits: %016llx\n base: %s\n val:  %s\n ryu:  %s\n",
														(unsigned long long)bits(v), Numbstrict::doubleToString(base).c_str(), ours.c_str(), oracle.c_str());
												return 1;
										}
								}
								double round = std::strtod(ours.c_str(), nullptr);
								assert(bits(round) == bits(v));
								double na = Numbstrict::stringToDouble(ours);
								double nb = Numbstrict::stringToDouble(oracle);
								if (!(bits(na) == bits(v) || (v == 0.0 && na == 0.0))) {
										std::printf("stringToDouble(ours) mismatch (neighbor)\nbits: %016llx\n ours: %s\n", (unsigned long long)bits(v), ours.c_str());
										return 1;
								}
								if (!(bits(nb) == bits(v) || (v == 0.0 && nb == 0.0))) {
										std::printf("stringToDouble(ryu) mismatch (neighbor)\nbits: %016llx\n ryu:  %s\n", (unsigned long long)bits(v), oracle.c_str());
										return 1;
								}
								static_cast<void>(round);
						}
				}

				// Boundary values around decimal formatting thresholds
				double dBoundary[] = {
						1e-6, -1e-6,
						1e-7, -1e-7,
						1e10, -1e10
				};
				for (double base : dBoundary) {
						for (double dir : { -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity() }) {
								double v = std::nextafter(base, dir);
								if (!std::isfinite(v) || v == 0.0) continue;
								std::string ours = Numbstrict::doubleToString(v);
								std::string oracle = ryuDouble(v);
								if (ours != oracle) {
										double ra = std::strtod(ours.c_str(), nullptr);
										double rb = std::strtod(oracle.c_str(), nullptr);
										if (bits(ra) == bits(v) && bits(rb) == bits(v) && is_decimal_tie_equivalent(ours, oracle)) {
												// accept tie canonicalization difference
										} else {
												std::printf("double mismatch (boundary)\nbits: %016llx\n base: %s\n val:  %s\n ryu:  %s\n",
														(unsigned long long)bits(v), Numbstrict::doubleToString(base).c_str(), ours.c_str(), oracle.c_str());
												return 1;
										}
								}
								double round = std::strtod(ours.c_str(), nullptr);
								assert(bits(round) == bits(v));
								double na = Numbstrict::stringToDouble(ours);
								double nb = Numbstrict::stringToDouble(oracle);
								if (!(bits(na) == bits(v) || (v == 0.0 && na == 0.0))) {
										std::printf("stringToDouble(ours) mismatch (boundary)\nbits: %016llx\n ours: %s\n", (unsigned long long)bits(v), ours.c_str());
										return 1;
								}
								if (!(bits(nb) == bits(v) || (v == 0.0 && nb == 0.0))) {
										std::printf("stringToDouble(ryu) mismatch (boundary)\nbits: %016llx\n ryu:  %s\n", (unsigned long long)bits(v), oracle.c_str());
										return 1;
								}
								static_cast<void>(round);
						}
				}
		}

		if (testFloat) {
				float fEdge[] = {
					0.0f,
					std::numeric_limits<float>::denorm_min(),
					std::nextafter(std::numeric_limits<float>::min(), 0.0f),
					std::numeric_limits<float>::min(),
					std::numeric_limits<float>::infinity(),
					-std::numeric_limits<float>::denorm_min(),
					-std::nextafter(std::numeric_limits<float>::min(), 0.0f),
					-std::numeric_limits<float>::min(),
					-std::numeric_limits<float>::infinity()
				};
				for (float v : fEdge) {
						std::string ours = Numbstrict::floatToString(v);
						std::string oracle = ryuFloat(v);
						assert(ours == oracle);
						float round = std::strtof(ours.c_str(), nullptr);
						assert(bits(round) == bits(v));
						float na = Numbstrict::stringToFloat(ours);
						float nb = Numbstrict::stringToFloat(oracle);
						assert(bits(na) == bits(v) || (v == 0.0f && na == 0.0f));
						assert(bits(nb) == bits(v) || (v == 0.0f && nb == 0.0f));
						static_cast<void>(round);
				}

				// One-ULP neighbors around selected edge cases
				for (float base : fEdge) {
						if (!std::isfinite(base) || base == 0.0f) continue; // skip inf and +/-0
						float n1 = std::nextafter(base, -std::numeric_limits<float>::infinity());
						float n2 = std::nextafter(base, std::numeric_limits<float>::infinity());
						for (float v : { n1, n2 }) {
								if (!std::isfinite(v) || v == 0.0f) continue;
								std::string ours = Numbstrict::floatToString(v);
								std::string oracle = ryuFloat(v);
								if (ours != oracle) {
										float ra = std::strtof(ours.c_str(), nullptr);
										float rb = std::strtof(oracle.c_str(), nullptr);
										if (bits(ra) == bits(v) && bits(rb) == bits(v) && is_decimal_tie_equivalent(ours, oracle)) {
												// accept tie canonicalization difference
										} else {
												std::printf("float mismatch (neighbor)\nbits: %08x\n base: %s\n val:  %s\n ryu:  %s\n", bits(v), Numbstrict::floatToString(base).c_str(), ours.c_str(), oracle.c_str());
												return 1;
										}
								}
								float round = std::strtof(ours.c_str(), nullptr);
								assert(bits(round) == bits(v));
								float na = Numbstrict::stringToFloat(ours);
								float nb = Numbstrict::stringToFloat(oracle);
								if (!(bits(na) == bits(v) || (v == 0.0f && na == 0.0f))) {
										std::printf("stringToFloat(ours) mismatch (neighbor)\nbits: %08x\n ours: %s\n", bits(v), ours.c_str());
										return 1;
								}
								if (!(bits(nb) == bits(v) || (v == 0.0f && nb == 0.0f))) {
										std::printf("stringToFloat(ryu) mismatch (neighbor)\nbits: %08x\n ryu:  %s\n", bits(v), oracle.c_str());
										return 1;
								}
								static_cast<void>(round);
						}
				}

				// Boundary values around decimal formatting thresholds
				float fBoundary[] = {
						1e-6f, -1e-6f,
						1e-7f, -1e-7f,
						1e10f, -1e10f
				};
				for (float base : fBoundary) {
						for (float dir : { -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity() }) {
								float v = std::nextafter(base, dir);
								if (!std::isfinite(v) || v == 0.0f) continue;
								std::string ours = Numbstrict::floatToString(v);
								std::string oracle = ryuFloat(v);
								if (ours != oracle) {
										float ra = std::strtof(ours.c_str(), nullptr);
										float rb = std::strtof(oracle.c_str(), nullptr);
										if (bits(ra) == bits(v) && bits(rb) == bits(v) && is_decimal_tie_equivalent(ours, oracle)) {
												// accept tie canonicalization difference
										} else {
												std::printf("float mismatch (boundary)\nbits: %08x\n base: %s\n val:  %s\n ryu:  %s\n", bits(v), Numbstrict::floatToString(base).c_str(), ours.c_str(), oracle.c_str());
												return 1;
										}
								}
								float round = std::strtof(ours.c_str(), nullptr);
								assert(bits(round) == bits(v));
								float na = Numbstrict::stringToFloat(ours);
								float nb = Numbstrict::stringToFloat(oracle);
								if (!(bits(na) == bits(v) || (v == 0.0f && na == 0.0f))) {
										std::printf("stringToFloat(ours) mismatch (boundary)\nbits: %08x\n ours: %s\n", bits(v), ours.c_str());
										return 1;
								}
								if (!(bits(nb) == bits(v) || (v == 0.0f && nb == 0.0f))) {
										std::printf("stringToFloat(ryu) mismatch (boundary)\nbits: %08x\n ryu:  %s\n", bits(v), oracle.c_str());
										return 1;
								}
								static_cast<void>(round);
						}
				}
		}

		if (testDouble) {
				std::mt19937_64 drng(123456);
				std::uniform_int_distribution<uint64_t> ddist;
				for (int i = 0; i < testCount; ++i) {
						uint64_t u = ddist(drng);
						double v;
						std::memcpy(&v, &u, sizeof v);
						if (!std::isfinite(v)) continue;
						std::string ours = Numbstrict::doubleToString(v);
						std::string oracle = ryuDouble(v);
						if (ours != oracle) {
								double ra = std::strtod(ours.c_str(), nullptr);
								double rb = std::strtod(oracle.c_str(), nullptr);
								if (bits(ra) == bits(v) && bits(rb) == bits(v) && is_decimal_tie_equivalent(ours, oracle)) {
										// accept tie canonicalization difference
								} else {
										std::printf("double mismatch\nbits: %016llx\n ours: %s\n ryu:  %s\n", (unsigned long long)bits(v), ours.c_str(), oracle.c_str());
										return 1;
								}
						}
						double round = std::strtod(ours.c_str(), nullptr);
						assert(bits(round) == bits(v));
						double na = Numbstrict::stringToDouble(ours);
						double nb = Numbstrict::stringToDouble(oracle);
						if (!(bits(na) == bits(v) || (v == 0.0 && na == 0.0))) {
								std::printf("stringToDouble(ours) mismatch\nbits: %016llx\n ours: %s\n", (unsigned long long)bits(v), ours.c_str());
								return 1;
						}
						if (!(bits(nb) == bits(v) || (v == 0.0 && nb == 0.0))) {
								std::printf("stringToDouble(ryu) mismatch\nbits: %016llx\n ryu:  %s\n", (unsigned long long)bits(v), oracle.c_str());
								return 1;
						}
						static_cast<void>(round);
				}
		}

		if (testFloat) {
				std::mt19937 frng(654321);
				std::uniform_int_distribution<uint32_t> fdist;
				for (int i = 0; i < testCount; ++i) {
						uint32_t u = fdist(frng);
						float v;
						std::memcpy(&v, &u, sizeof v);
						if (!std::isfinite(v)) continue;
						std::string ours = Numbstrict::floatToString(v);
						std::string oracle = ryuFloat(v);
						if (ours != oracle) {
								float ra = std::strtof(ours.c_str(), nullptr);
								float rb = std::strtof(oracle.c_str(), nullptr);
								if (bits(ra) == bits(v) && bits(rb) == bits(v) && is_decimal_tie_equivalent(ours, oracle)) {
										// accept tie canonicalization difference
								} else {
										std::printf("float mismatch\nbits: %08x\n ours: %s\n ryu:  %s\n", bits(v), ours.c_str(), oracle.c_str());
										return 1;
								}
						}
						float round = std::strtof(ours.c_str(), nullptr);
						assert(bits(round) == bits(v));
						float na = Numbstrict::stringToFloat(ours);
						float nb = Numbstrict::stringToFloat(oracle);
						if (!(bits(na) == bits(v) || (v == 0.0f && na == 0.0f))) {
								std::printf("stringToFloat(ours) mismatch\nbits: %08x\n ours: %s\n", bits(v), ours.c_str());
								return 1;
						}
						if (!(bits(nb) == bits(v) || (v == 0.0f && nb == 0.0f))) {
								std::printf("stringToFloat(ryu) mismatch\nbits: %08x\n ryu:  %s\n", bits(v), oracle.c_str());
								return 1;
						}
						static_cast<void>(round);
				}
		}
		
		std::cout << "All tests passed\n";

		return 0;
}
