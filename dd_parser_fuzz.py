#!/usr/bin/env python3
import math, random, struct, sys, time
from decimal import Decimal, getcontext

# ------------------------ config ------------------------

MIN_EXPONENT = -324
MAX_EXPONENT = 308

# Decimal precision for the "oracle" scale
DECIMAL_PREC = 100		# 21 works in practice; 42 gives headroom

# ---------------------- helpers -------------------------

def double_bits(x: float) -> int:
	return struct.unpack(">Q", struct.pack(">d", x))[0]


def bits_to_double(u: int) -> float:
	return struct.unpack(">d", struct.pack(">Q", u))[0]



def exp10_of_str(s: str) -> int:
	p = 1 if s and s[0] in "+-" else 0
	exp10 = -1
	i = p
	L = len(s)
	while i < L and s[i].isdigit():
		exp10 += 1
		i += 1
	if i < L and s[i] == '.':
		i += 1
		while i < L and s[i].isdigit():
			i += 1
	if i < L and s[i] in 'eE':
		i += 1
		sign = 1
		if i < L and s[i] in '+-':
			sign = -1 if s[i] == '-' else 1
			i += 1
		j = i
		while j < L and s[j].isdigit():
			j += 1
		if j > i:
			exp10 += sign * int(s[i:j])
	return exp10


# ------------------- DoubleDouble & table ----------------


class DoubleDouble:
	def __init__(self, high: float = 0.0, low: float = 0.0):
		self.high = float(high)
		self.low = float(low)


	def __mul__(self, factor: int):
		low_times_factor = self.low * factor
		overflow = math.floor(low_times_factor)
		return DoubleDouble((self.high * factor) + overflow, low_times_factor - overflow)

	def __truediv__(self, divisor):
		d = float(divisor)
		assert d != 0.0
		q_int = math.floor(self.high / d)  # exact (high is int < 2^53)
		rem = self.high - q_int * d
		low_div = (self.low + rem) / d
		carry = math.floor(low_div)
		return DoubleDouble(q_int + carry, low_div - carry)


	def __float__(self):
		return self.high + self.low


def multiplyAndAdd(term: DoubleDouble, factorA: DoubleDouble, digit: int):
	fmaLow = factorA.low * digit + term.low
	overflow = math.floor(fmaLow)
	return DoubleDouble(factorA.high * digit + term.high + overflow, fmaLow - overflow)


class Exp10Table:
	def __init__(self):
		self.normals = [None] * (MAX_EXPONENT + 1 - MIN_EXPONENT)
		self.factors = [0.0] * (MAX_EXPONENT + 1 - MIN_EXPONENT)
		WIDTH = math.ldexp(1.0, 53 - 4)

		# Non-negative exponents
		normal = DoubleDouble(WIDTH, 0.0)
		factor = 1.0 / WIDTH
		for i in range(0, MAX_EXPONENT + 1):
			if normal.high >= WIDTH:
				factor *= 16.0
				normal = normal / 16
			self.normals[i - MIN_EXPONENT] = normal
			self.factors[i - MIN_EXPONENT] = factor
			normal = normal * 10

		# Negative exponents — original tail
		normal = DoubleDouble(WIDTH, 0.0)
		factor = 1.0 / WIDTH
		for i in range(-1, MIN_EXPONENT - 1, -1):
			if normal.high < WIDTH and (factor / 16.0) > 0.0:
				factor /= 16.0
				normal = normal * 16
			normal = normal / 10
			self.normals[i - MIN_EXPONENT] = normal
			self.factors[i - MIN_EXPONENT] = factor


EXP10_TABLE = Exp10Table()


# ---------------- parse into (sign, accumulator, factor) ----------------


def parse_components(s: str):
	"""Return (sign, accumulator(DoubleDouble), factor(float)) or None."""
	p = 0
	e = len(s)
	sign = 1.0
	if p < e and (s[p] == '-' or s[p] == '+'):
		sign = -1.0 if s[p] == '-' else 1.0
		p += 1
	t = s[p:].lower()
	if t.startswith("inf") or t.startswith("nan"):
		return None

	exponent = -1
	significandBegin = p
	while p < e and s[p].isdigit():
		exponent += 1
		p += 1
	if p < e and s[p] == '.':
		if p == significandBegin:
			significandBegin += 1
		p += 1
		while p < e and s[p].isdigit():
			p += 1
	if p == significandBegin:
		return None
	significandEnd = p

	if p < e and (s[p] == 'e' or s[p] == 'E'):
		p += 1
		exp_sign = 1
		if p < e and (s[p] == '+' or s[p] == '-'):
			exp_sign = -1 if s[p] == '-' else 1
			p += 1
		ui = 0
		had = False
		while p < e and s[p].isdigit():
			ui = ui * 10 + (ord(s[p]) - 48)
			p += 1
			had = True
		if had:
			exponent += exp_sign * ui

	p2 = significandBegin
	while p2 < significandEnd and (s[p2] == '0' or s[p2] == '.'):
		if s[p2] == '0':
			exponent -= 1
		p2 += 1

	if p2 == significandEnd or exponent < MIN_EXPONENT or exponent > MAX_EXPONENT:
		return None

	idx = exponent - MIN_EXPONENT
	magnitude = EXP10_TABLE.normals[idx]
	acc = DoubleDouble(0.0, 0.0)
	j = p2
	while j < significandEnd:
		if s[j] != '.':
			acc = multiplyAndAdd(acc, magnitude, ord(s[j]) - 48)
			magnitude = magnitude / 10
		j += 1

	factor = EXP10_TABLE.factors[idx]  # power-of-two
	return (sign, acc, factor)


# ---------------- scaling helpers ----------------


def round_to_even_from_parts(int_part: int, frac_part: float) -> int:
	"""Round int_part + frac_part to nearest-even integer without forming huge floats."""
	if frac_part < 0.5:
		return int_part
	if frac_part > 0.5:
		return int_part + 1
	return int_part if (int_part & 1) == 0 else (int_part + 1)


def round_shift_right_to_even(N: int, r: int) -> int:
	"""Return round_to_nearest_even(N / 2**r) assuming r >= 0."""
	if r <= 0:
		return N << (-r)
	q = N >> r
	rem = N & ((1 << r) - 1)
	half = 1 << (r - 1)
	if rem > half:
		return q + 1
	if rem < half:
		return q
	return q + (q & 1)


def ilog2_from_double(x: float) -> int:
	"""Exact floor(log2(x)) for finite positive double x <= 2**53 using frexp."""
	m, e2 = math.frexp(x)
	return e2 - 1


def pow2_exponent_from_bits(f: float) -> int:
	"""Return k where f == 2**k. Asserts f is a pure power of two (normal or subnormal)."""
	u = double_bits(f)
	exp = (u >> 52) & 0x7FF
	frac = u & ((1 << 52) - 1)
	if exp == 0:
		assert frac != 0 and (frac & (frac - 1)) == 0, f"factor not pure pow2 (subnormal): bits=0x{u:016x}"
		pos = frac.bit_length() - 1
		return -1074 + pos
	else:
		assert frac == 0, f"factor not pure pow2 (normal): bits=0x{u:016x}"
		return exp - 1023


getcontext().prec = DECIMAL_PREC


def scale_float(acc: DoubleDouble, factor: float) -> float:
	return float(acc) * factor


def scale_decimal(acc: DoubleDouble, factor: float) -> float:
	return float((Decimal(acc.high) + Decimal(acc.low)) * Decimal(factor))


def scale_dd_pow2_exact(acc: DoubleDouble, factor: float) -> float:
	"""
	Correctly round (acc.high + acc.low) * factor for factor = exact power-of-two.
	- NORMAL results: build a 53-bit significand N and pack once.
	- SUBNORMAL results: round the payload (mantissa) directly once (no prior 53-bit rounding).
	"""
	assert factor > 0.0
	k = pow2_exponent_from_bits(factor)

	H = int(acc.high)
	L = float(acc.low)

	if H == 0 and L == 0.0:
		return 0.0

	if H == 0:
		mL, eL = math.frexp(L)
		e = (eL - 1) + k
	else:
		e = ilog2_from_double(acc.high) + k

	if e < -1022:
		E = k
		T = E + 1074
		if T >= 0:
			A = H << T
			Bf = math.ldexp(L, T)
			Bi = int(Bf)
			frac = Bf - Bi
			N = A + Bi
		else:
			kk = -T
			q = H >> kk
			r = H & ((1 << kk) - 1)
			Bf = (r + math.ldexp(L, kk)) / float(1 << kk)
			frac = Bf
			N = q
		if frac > 0.5:
			N += 1
		elif frac == 0.5 and (N & 1) == 1:
			N += 1
		if N <= 0:
			return 0.0
		if N >= (1 << 52):
			return bits_to_double(0x0010000000000000)
		return bits_to_double(N)

	if H == 0:
		mL, eL = math.frexp(L)
		e = (eL - 1) + k
		t = math.ldexp(mL, 53)
		Ni = int(t)
		frac = t - Ni
		N = round_to_even_from_parts(Ni, frac)
		if N == (1 << 53):
			N = (1 << 52)
			e += 1
	else:
		exph = ilog2_from_double(acc.high)
		s = 52 - exph
		if s >= 0:
			A = H << s
			Bf = math.ldexp(L, s)
			Bi = int(Bf)
			frac = Bf - Bi
			N = round_to_even_from_parts(A + Bi, frac)
		else:
			k2 = -s
			q = H >> k2
			r = H & ((1 << k2) - 1)
			Bf = (r + math.ldexp(L, k2)) / float(1 << k2)
			N = round_to_even_from_parts(q, Bf)
		e = exph + k
		if N == (1 << 53):
			N = (1 << 52)
			e += 1

	expfield = e + 1023
	if expfield <= 0:
		rshift = 1 - expfield
		payload = round_shift_right_to_even(N, rshift)
		if payload <= 0:
			return 0.0
		if payload >= (1 << 52):
			return bits_to_double(0x0010000000000000)
		return bits_to_double(payload)
	if expfield >= 0x7FF:
		return float('inf')
	mant = N - (1 << 52)
	bits = (expfield << 52) | mant
	return bits_to_double(bits)


SCALERS = [
	("floatScale", scale_float),
	("scale_dd_pow2_exact", scale_dd_pow2_exact),
]



def fuzz(n=200000, seed=1234, sample=0):
        random.seed(seed)
        t0 = time.time()

        per_scaler_mismatches = {name: 0 for name, _ in SCALERS}
        decimal_vs_oracle = 0
        highest = -999

        mismatch_samples = []
        match_buckets = {}
        bucket_counts = {}

        count = 0
        while count < n:
                u = random.getrandbits(64)
                x = bits_to_double(u)
                if not math.isfinite(x):
                        continue
                s = repr(x)
                parsed = parse_components(s)
                if parsed is None:
                        continue
                sign, acc, factor = parsed

                oracle_bits = double_bits(x)

                y_dec = sign * scale_decimal(acc, factor)
                if double_bits(y_dec) != oracle_bits:
                        decimal_vs_oracle += 1

                ex10 = exp10_of_str(s)
                for name, fn in SCALERS:
                        y = sign * fn(acc, factor)
                        bits = double_bits(y)
                        if bits != oracle_bits:
                                per_scaler_mismatches[name] += 1
                                highest = max(highest, ex10)
                                if name == "floatScale" and len(mismatch_samples) < sample:
                                        mismatch_samples.append((double_bits(sign * acc.high), double_bits(sign * acc.low), double_bits(factor), oracle_bits))
                        elif name == "floatScale" and sample > 0:
                                entry = (double_bits(sign * acc.high), double_bits(sign * acc.low), double_bits(factor), oracle_bits)
                                k = pow2_exponent_from_bits(factor)
                                bucket_counts[k] = bucket_counts.get(k, 0) + 1
                                bucket = match_buckets.setdefault(k, [])
                                if len(bucket) < sample:
                                        bucket.append(entry)
                                else:
                                        r = random.randrange(bucket_counts[k])
                                        if r < sample:
                                                bucket[r] = entry

                count += 1

        t1 = time.time()
        print(f"[FUZZ] total: {n}, time_s: {round(t1 - t0, 2)}")
        for name, _ in SCALERS:
                print(f"  {name} vs oracle mismatches: {per_scaler_mismatches[name]}")
        print(f"  decimalScale vs oracle mismatches: {decimal_vs_oracle}")
        print(f"  highest_exp10_among_mismatches: {highest}")

        if sample > 0:
                match_samples = []
                exps = sorted(match_buckets.keys())
                if exps:
                        step = max(1, len(exps) // sample)
                        for i in range(0, len(exps), step):
                                k = exps[i]
                                bucket = match_buckets[k]
                                match_samples.append(random.choice(bucket))
                                if len(match_samples) >= sample:
                                        break

                def dump(arr, name):
                        print(f"{name} = [")
                        for h, l, fbits, obits in arr:
                                print(f"        [0x{h:016x}, 0x{l:016x}, 0x{fbits:016x}, 0x{obits:016x}],")
                        print("]")
                dump(mismatch_samples, "mismatches")
                dump(match_samples, "matches")


# ------------------------- main -------------------------


def main():
        n = int(sys.argv[1]) if len(sys.argv) > 1 else 200000
        seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1234
        sample = int(sys.argv[3]) if len(sys.argv) > 3 else 0
        fuzz(n, seed, sample)


if __name__ == "__main__":
	main()
