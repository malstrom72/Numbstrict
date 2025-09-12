#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include <array>
#include <fstream>
#include <string>
#include <inttypes.h>

static inline uint64_t doubleBits(double x) {
	uint64_t u;
	std::memcpy(&u, &x, sizeof(u));
	return u;
}

static inline double bitsToDouble(uint64_t u) {
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


struct DoubleDouble {
	double high;
	double low;
	double toDouble() const { return high + low; }
};

static inline double scaleFloat(const DoubleDouble& acc, double factor) {
	return acc.toDouble() * factor;
}

double scaleDDPow2Exact(const DoubleDouble& acc, double factor) {
	assert(factor > 0.0);
	int tmp;
	std::frexp(factor, &tmp);
	int k = tmp - 1;				// factor is assumed exact 2^k
	uint64_t H = static_cast<uint64_t>(acc.high);	// integer part of |high| (non-zero)
	double L = acc.low; 				// low tail (|low|)
	std::frexp(acc.high, &tmp);
	int exph = tmp - 1;				// unbiased exponent of high
	int e = exph + k; 				// target unbiased exponent after scaling
	uint64_t N = 0; 				// accumulator for 53-bit significand with hidden bit
	if (e < -1022) {				// result underflows normal range -> subnormal/zero
		int T = k + 1074; 			// 1074 = 1022 (min exp bias) + 52 (mantissa bits)
		double frac; 				// fractional tail after fitting into payload window
		if (T >= 0) {
			uint64_t A = H << T; 		// shift high into subnormal payload scale
			double Bf = std::ldexp(L, T);	// align low the same way
			uint64_t Bi = static_cast<uint64_t>(Bf);
			frac = Bf - static_cast<double>(Bi); // leftover fractional contribution
			N = A + Bi; 				// integer payload before rounding
		} else {
			int kk = -T; 				// need to right-shift high into payload bits
			uint64_t q = H >> kk; 		// integer payload candidate
			uint64_t r = H & ((uint64_t(1) << kk) - 1);
			double Bf = (r + std::ldexp(L, kk)) / static_cast<double>(uint64_t(1) << kk);
			frac = Bf; 				// fractional remainder to round with ties-to-even
			N = q;
		}
		// Round to nearest, ties-to-even using frac and current N
		if (frac > 0.5) {
			++N;
		} else if (frac == 0.5 && (N & 1) == 1) {
			++N;
		}
		// Clamp to zero or smallest normal if rounding crosses the boundary
		if (N <= 0) {
			return 0.0; 				// rounded all the way to zero
		}
		if (N >= (uint64_t(1) << 52)) {
			return bitsToDouble(0x0010000000000000ULL); // rounded up into smallest normal
		}
		return bitsToDouble(N); 		// subnormal result (exp=0, payload=N)
	}
	int s = 52 - exph; 				// align high+low onto the 52-bit payload window
	double frac; 					// fractional tail not captured in integer payload
	if (s >= 0) {
		uint64_t A = H << s; 			// left-shift high into payload scale
		double Bf = std::ldexp(L, s); 	// align low to same scale
		uint64_t Bi = static_cast<uint64_t>(Bf);
		frac = Bf - static_cast<double>(Bi);
		N = roundToEvenFromParts(A + Bi, frac); // combine and round (keeps hidden 1 if present)
	} else {
		int k2 = -s; 					// need to right-shift high
		uint64_t q = H >> k2; 			// integer payload candidate
		uint64_t r = H & ((uint64_t(1) << k2) - 1);
		double Bf = (r + std::ldexp(L, k2)) / static_cast<double>(uint64_t(1) << k2);
		N = roundToEvenFromParts(q, Bf); 	// q plus fractional tail rounded to even
	}
	// Handle rounding overflow that turns 1.111.. into 10.000.. (carry into exponent)
	if (N == (uint64_t(1) << 53)) {
		N = uint64_t(1) << 52; 		// renormalize payload to 1<<52 (implicit 1)
		++e; 						// bump exponent accordingly
	}
	int expfield = e + 1023; 			// biased exponent field for IEEE-754 binary64
	if (expfield <= 0) {
		// Borderline: normal after rounding became subnormal -> shift and round payload
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
	if (expfield >= 0x7ff) {			// overflow to +inf
		return INFINITY;
	}
	// Pack normal: N includes the hidden 1, remove it and assemble final bits
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
		TestCase t;
		if (std::sscanf(line.c_str(),
						" [%" SCNx64 ", %" SCNx64 ", %" SCNx64 ", %" SCNx64 "],",
						&t.hbits, &t.lbits, &t.fbits, &t.obits) == 4) {
			tests.push_back(t);
		}
	}
	return tests;
}
int main() {
	const auto tests = loadTests("dd_parser_fuzz_table.txt");
	struct Scaler { const char* name; double (*fn)(const DoubleDouble&, double); };
	const std::array<Scaler, 2> scalers = {{
		{"scale_float", scaleFloat},
		{"scale_dd_pow2_exact", scaleDDPow2Exact},
	}};
	std::array<size_t, scalers.size()> counts = {};
	for (const auto& t : tests) {
		double raw_h = bitsToDouble(t.hbits);
		double raw_l = bitsToDouble(t.lbits);
		double sign = (raw_h < 0.0 || (raw_h == 0.0 && raw_l < 0.0)) ? -1.0 : 1.0;
		DoubleDouble acc{std::fabs(raw_h), std::fabs(raw_l)};
		double factor = bitsToDouble(t.fbits);
		for (size_t i = 0; i < scalers.size(); ++i) {
			double y = sign * scalers[i].fn(acc, factor);
			if (doubleBits(y) != t.obits) {
				++counts[i];
			}
		}
	}
	for (size_t i = 0; i < scalers.size(); ++i) {
		std::printf("%s mismatches: %zu\n", scalers[i].name, counts[i]);
	}
	return 0;
}
