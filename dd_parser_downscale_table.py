#!/usr/bin/env python3
import math, struct
from decimal import Decimal, getcontext

DECIMAL_PREC = 100
getcontext().prec = DECIMAL_PREC

def double_bits(x: float) -> int:
	return struct.unpack(">Q", struct.pack(">d", x))[0]

def bits_to_double(u: int) -> float:
	return struct.unpack(">d", struct.pack(">Q", u))[0]

def round_to_even_from_parts(int_part: int, frac_part: float) -> int:
	if frac_part < 0.5:
		return int_part
	if frac_part > 0.5:
		return int_part + 1
	return int_part if (int_part & 1) == 0 else (int_part + 1)

def round_shift_right_to_even(N: int, r: int) -> int:
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
	m, e2 = math.frexp(x)
	return e2 - 1

def pow2_exponent_from_bits(f: float) -> int:
	u = double_bits(f)
	exp = (u >> 52) & 0x7FF
	frac = u & ((1 << 52) - 1)
	if exp == 0:
		assert frac != 0 and (frac & (frac - 1)) == 0, f"factor not pure pow2 (subnormal): bits=0x{u:016x}"
		pos = frac.bit_length() - 1
		return -1074 + pos
	assert frac == 0, f"factor not pure pow2 (normal): bits=0x{u:016x}"
	return exp - 1023

class DoubleDouble:
	def __init__(self, high: float = 0.0, low: float = 0.0):
		self.high = float(high)
		self.low = float(low)

	def __float__(self):
		return self.high + self.low

def scale_float(acc: DoubleDouble, factor: float) -> float:
	return float(acc) * factor

def scale_decimal(acc: DoubleDouble, factor: float) -> float:
	return float((Decimal(acc.high) + Decimal(acc.low)) * Decimal(factor))

def scale_dd_pow2_exact(acc: DoubleDouble, factor: float) -> float:
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

def run():
	ns = {}
	with open("dd_parser_fuzz_table.txt") as f:
		exec(f.read(), ns)
	tests = ns["mismatches"] + ns["matches"]
	scalers = [
		("scale_float", scale_float),
		("scale_decimal", scale_decimal),
		("scale_dd_pow2_exact", scale_dd_pow2_exact),
	]
	counts = {name: 0 for name, _ in scalers}
	for hbits, lbits, fbits, obits in tests:
		raw_h = bits_to_double(hbits)
		raw_l = bits_to_double(lbits)
		sign = -1.0 if raw_h < 0 or (raw_h == 0.0 and raw_l < 0) else 1.0
		acc = DoubleDouble(abs(raw_h), abs(raw_l))
		factor = bits_to_double(fbits)
		for name, fn in scalers:
			y = sign * fn(acc, factor)
			if double_bits(y) != obits:
				counts[name] += 1
	for name in counts:
		print(f"{name} mismatches: {counts[name]}")

if __name__ == "__main__":
	run()
