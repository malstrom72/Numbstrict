#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <vector>
#include <array>
#include <fstream>
#include <string>
// #include <quadmath.h>

static uint64_t doubleBits(double x) {
	uint64_t u;
	std::memcpy(&u, &x, sizeof(u));
	return u;
}

static double bitsToDouble(uint64_t u) {
	double x;
	std::memcpy(&x, &u, sizeof(x));
	return x;
}

static uint64_t roundToEvenFromParts(uint64_t intPart, double fracPart) {
	if (fracPart < 0.5) {
		return intPart;
	}
	if (fracPart > 0.5) {
		return intPart + 1;
	}
	return (intPart & 1) == 0 ? intPart : (intPart + 1);
}

static uint64_t roundShiftRightToEven(uint64_t N, int r) {
	if (r <= 0) {
		return N << (-r);
	}
	uint64_t q = N >> r;
	uint64_t rem = N & ((uint64_t(1) << r) - 1);
	uint64_t half = uint64_t(1) << (r - 1);
	if (rem > half) {
		return q + 1;
	}
	if (rem < half) {
		return q;
	}
	return q + (q & 1);
}

static int ilog2FromDouble(double x) {
	int e2;
	std::frexp(x, &e2);
	return e2 - 1;
}

static int pow2ExponentFromBits(double f) {
	uint64_t u = doubleBits(f);
	int exp = int((u >> 52) & 0x7ff);
	uint64_t frac = u & ((uint64_t(1) << 52) - 1);
	if (exp == 0) {
		assert(frac != 0 && (frac & (frac - 1)) == 0);
		int pos = 63 - __builtin_clzll(frac);
		return -1074 + pos;
	}
	assert(frac == 0);
	return exp - 1023;
}

struct DoubleDouble {
	double high;
	double low;
	DoubleDouble(double h = 0.0, double l = 0.0) : high(h), low(l) {}
	double toDouble() const { return high + low; }
};

double scaleFloat(const DoubleDouble& acc, double factor) {
	return acc.toDouble() * factor;
}

/*double scaleDecimal(const DoubleDouble& acc, double factor) {
	__float128 a = static_cast<__float128>(acc.high);
	a += static_cast<__float128>(acc.low);
	a *= static_cast<__float128>(factor);
	return static_cast<double>(a);
}*/

double scaleDDPow2Exact(const DoubleDouble& acc, double factor) {
	assert(factor > 0.0);
	int k = pow2ExponentFromBits(factor);
	uint64_t H = static_cast<uint64_t>(acc.high);
	double L = acc.low;
	if (H == 0 && L == 0.0) {
		return 0.0;
	}
	int e;
	if (H == 0) {
		std::frexp(L, &e);
		e = (e - 1) + k;
	} else {
		e = ilog2FromDouble(acc.high) + k;
	}
	uint64_t N = 0;
	if (e < -1022) {
		int E = k;
		int T = E + 1074;
		double frac;
		if (T >= 0) {
			uint64_t A = H << T;
			double Bf = std::ldexp(L, T);
			uint64_t Bi = static_cast<uint64_t>(Bf);
			frac = Bf - static_cast<double>(Bi);
			N = A + Bi;
		} else {
			int kk = -T;
			uint64_t q = H >> kk;
			uint64_t r = H & ((uint64_t(1) << kk) - 1);
			double Bf = (r + std::ldexp(L, kk)) / static_cast<double>(uint64_t(1) << kk);
			frac = Bf;
			N = q;
		}
		if (frac > 0.5) {
			++N;
		} else if (frac == 0.5 && (N & 1) == 1) {
			++N;
		}
		if (N <= 0) {
			return 0.0;
		}
		if (N >= (uint64_t(1) << 52)) {
			return bitsToDouble(0x0010000000000000ULL);
		}
		return bitsToDouble(N);
	}
	if (H == 0) {
		double mL = std::frexp(L, &e);
		e = (e - 1) + k;
		double t = std::ldexp(mL, 53);
		uint64_t Ni = static_cast<uint64_t>(t);
		double frac = t - static_cast<double>(Ni);
		N = roundToEvenFromParts(Ni, frac);
		if (N == (uint64_t(1) << 53)) {
			N = uint64_t(1) << 52;
			++e;
		}
	} else {
		int exph = ilog2FromDouble(acc.high);
		int s = 52 - exph;
		double frac;
		if (s >= 0) {
			uint64_t A = H << s;
			double Bf = std::ldexp(L, s);
			uint64_t Bi = static_cast<uint64_t>(Bf);
			frac = Bf - static_cast<double>(Bi);
			N = roundToEvenFromParts(A + Bi, frac);
		} else {
			int k2 = -s;
			uint64_t q = H >> k2;
			uint64_t r = H & ((uint64_t(1) << k2) - 1);
			double Bf = (r + std::ldexp(L, k2)) / static_cast<double>(uint64_t(1) << k2);
			N = roundToEvenFromParts(q, Bf);
		}
		e = exph + k;
		if (N == (uint64_t(1) << 53)) {
			N = uint64_t(1) << 52;
			++e;
		}
	}
	int expfield = e + 1023;
	if (expfield <= 0) {
		int rshift = 1 - expfield;
		uint64_t payload = roundShiftRightToEven(N, rshift);
		if (payload <= 0) {
			return 0.0;
		}
		if (payload >= (uint64_t(1) << 52)) {
			return bitsToDouble(0x0010000000000000ULL);
		}
		return bitsToDouble(payload);
	}
	if (expfield >= 0x7ff) {
		return INFINITY;
	}
	uint64_t mant = N - (uint64_t(1) << 52);
	uint64_t bits = (static_cast<uint64_t>(expfield) << 52) | mant;
	return bitsToDouble(bits);
}

struct TestCase {
	uint64_t hbits;
	uint64_t lbits;
	uint64_t fbits;
	uint64_t obits;
};

static std::vector<TestCase> loadTests(const char* path) {
	std::vector<TestCase> tests;
	std::ifstream f(path);
	std::string line;
	while (std::getline(f, line)) {
		unsigned long long h, l, fb, o;
		if (std::sscanf(line.c_str(), " [%llx, %llx, %llx, %llx],", &h, &l, &fb, &o) == 4) {
			tests.push_back({static_cast<uint64_t>(h), static_cast<uint64_t>(l), static_cast<uint64_t>(fb), static_cast<uint64_t>(o)});
		}
	}
	return tests;
}
int main() {
	auto tests = loadTests("dd_parser_fuzz_table.txt");
	struct Scaler { const char* name; double (*fn)(const DoubleDouble&, double); };
	Scaler scalers[] = {
		{"scale_float", scaleFloat},
//		{"scale_decimal", scaleDecimal},
		{"scale_dd_pow2_exact", scaleDDPow2Exact},
	};
	size_t counts[sizeof(scalers)/sizeof(scalers[0])] = {0};
	for (const auto& t : tests) {
		double raw_h = bitsToDouble(t.hbits);
		double raw_l = bitsToDouble(t.lbits);
		double sign = (raw_h < 0.0 || (raw_h == 0.0 && raw_l < 0.0)) ? -1.0 : 1.0;
		DoubleDouble acc(std::fabs(raw_h), std::fabs(raw_l));
		double factor = bitsToDouble(t.fbits);
		for (size_t i = 0; i < sizeof(scalers)/sizeof(scalers[0]); ++i) {
			double y = sign * scalers[i].fn(acc, factor);
			if (doubleBits(y) != t.obits) {
				++counts[i];
			}
		}
	}
	for (size_t i = 0; i < sizeof(scalers)/sizeof(scalers[0]); ++i) {
		std::printf("%s mismatches: %zu\n", scalers[i].name, counts[i]);
	}
	return 0;
}
