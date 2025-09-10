
import math, random, struct, sys, time

MIN_EXPONENT = -324
MAX_EXPONENT = 308
BREAK_EXP    = -240
BOOST_K      = 100
BOOST_V      = math.ldexp(1.0, BOOST_K)
BOOST_V_RCP  = 1.0 / BOOST_V

# Switches
ENABLE_BOOST       = False     # use the boosted two-phase tail (<= BREAK_EXP)
USE_ORIGINAL_TAIL  = True    # when True, IGNORE boost and use the original C++ tail loop exactly

class DoubleDouble:
	def __init__(self, high: float = 0.0, low: float = 0.0):
		self.high = float(high)
		self.low  = float(low)
	def __add__(self, other):
		low_sum = self.low + other.low
		overflow = math.floor(low_sum)
		return DoubleDouble((self.high + other.high) + overflow, low_sum - overflow)
	def __mul__(self, factor: int):
		low_times_factor = self.low * factor
		overflow = math.floor(low_times_factor)
		return DoubleDouble((self.high * factor) + overflow, low_times_factor - overflow)

	'''
	def __truediv__(self, divisor: int):
		floored = math.floor(self.high / divisor)
		remainder = self.high - floored * divisor
		return DoubleDouble(floored, (self.low + remainder) / divisor)
	'''

	def  __truediv__(self, divisor):
		d = float(divisor)
		assert d != 0.0

		# high is an integer < 2^53, so this quotient and floor are exact in double.
		q_int = math.floor(self.high / d)
		rem   = self.high - q_int * d

		# Fractional part of the quotient lives here.
		low_div = (self.low + rem) / d

		# Renormalize so 0 ≤ low < 1
		carry = math.floor(low_div)
		return DoubleDouble(q_int + carry, low_div - carry)
	
	def __lt__(self, other):
		return self.high < other.high or (self.high == other.high and self.low < other.low)
	def __float__(self):
		return self.high + self.low

def multiplyAndAdd(term: DoubleDouble, factorA: DoubleDouble, factorB: float):
	fmaLow = factorA.low * factorB + term.low
	overflow = math.floor(fmaLow)
	return DoubleDouble(factorA.high * factorB + term.high + overflow, fmaLow - overflow)

def double_bits(x: float) -> int:
	return struct.unpack('>Q', struct.pack('>d', x))[0]

class Exp10Table:
	def __init__(self):
		self.normals = [None] * (MAX_EXPONENT + 1 - MIN_EXPONENT)
		self.factors = [0.0 ] * (MAX_EXPONENT + 1 - MIN_EXPONENT)
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

		# Negative exponents
		normal = DoubleDouble(WIDTH, 0.0)
		factor = 1.0 / WIDTH

		if USE_ORIGINAL_TAIL:
			# Exact port of your original C++ negative loop
			for i in range(-1, MIN_EXPONENT - 1, -1):
				if ENABLE_BOOST and i == BREAK_EXP:
					factor *= BOOST_V
				if normal.high < WIDTH and (factor / 16.0) > 0:
					factor /= 16.0
					normal = normal * 16
				normal = normal / 10
				self.normals[i - MIN_EXPONENT] = normal
				self.factors[i - MIN_EXPONENT] = factor

				if i < BREAK_EXP:
					# debug print the normal and factor for every iteration in hex format
					print(f"exp10={i}, normal=(0x{double_bits(float(normal.high)):016x},0x{double_bits(float(normal.low)):016x}), factor=0x{double_bits(factor):016x}")
		else:
			# Two-phase scheme with optional boost (your later experiment)
			# (a) -1 down to BREAK_EXP+1: keep 16x normalization
			for i in range(-1, BREAK_EXP, -1):
				if normal * 10 < DoubleDouble(math.ldexp(1.0, 53 - 1), 0):
					assert (factor / 16.0) > 0.0
					factor /= 16.0
					normal = normal * 16
				normal = normal / 10
				self.normals[i - MIN_EXPONENT] = normal
				self.factors[i - MIN_EXPONENT] = factor
			# (b) tail ≤ BREAK_EXP
			if ENABLE_BOOST:
				factor *= BOOST_V
			for i in range(BREAK_EXP, MIN_EXPONENT - 1, -1):
				while normal * 10 < DoubleDouble(math.ldexp(1.0, 53 - 1), 0):
					assert (factor / 2.0) > 0.0
					factor /= 2.0
					normal = normal * 2
				normal = normal / 10
				self.normals[i - MIN_EXPONENT] = normal
				self.factors[i - MIN_EXPONENT] = factor
				
				# debug print the normal and factor for every iteration in hex format
				print(f"exp10={i}, normal=(0x{double_bits(float(normal.high)):016x},0x{double_bits(float(normal.low)):016x}), factor=0x{double_bits(factor):016x}")

EXP10_TABLE = Exp10Table()

def scaleAndConvert(factorA: DoubleDouble, factorB: float) -> float:
	return factorA.high * factorB + factorA.low * factorB

def parseReal(s: str) -> float:
	p = 0; e = len(s)
	sign = 1.0
	if p < e and (s[p] == '-' or s[p] == '+'):
		sign = -1.0 if s[p] == '-' else 1.0; p += 1
	t = s[p:].lower()
	if t.startswith("inf"): return math.copysign(math.inf, sign)
	if t.startswith("nan"): return math.nan

	exponent = -1
	significandBegin = p
	while p < e and s[p].isdigit():
		exponent += 1; p += 1
	if p < e and s[p] == '.':
		if p == significandBegin: significandBegin += 1
		p += 1
		while p < e and s[p].isdigit():
			p += 1
	if p == significandBegin:
		return math.copysign(0.0, sign)
	significandEnd = p

	if p < e and (s[p] == 'e' or s[p] == 'E'):
		p += 1; exp_sign = 1
		if p < e and (s[p] == '+' or s[p] == '-'):
			exp_sign = -1 if s[p] == '-' else 1; p += 1
		ui = 0; had = False
		while p < e and s[p].isdigit():
			ui = ui * 10 + (ord(s[p]) - 48); p += 1; had = True
		if had: exponent += exp_sign * ui

	p = significandBegin
	while p < significandEnd and (s[p] == '0' or s[p] == '.'):
		if s[p] == '0': exponent -= 1
		p += 1

	if p == significandEnd or exponent < MIN_EXPONENT:
		return math.copysign(0.0, sign)
	if exponent > MAX_EXPONENT:
		return math.copysign(math.inf, sign)

	idx = exponent - MIN_EXPONENT
	magnitude = EXP10_TABLE.normals[idx]
	accumulator = DoubleDouble(0.0, 0.0)
	j = p
	while j < significandEnd:
		if s[j] != '.':
			accumulator = multiplyAndAdd(accumulator, magnitude, (ord(s[j]) - 48))
			magnitude = magnitude / 10
		j += 1

	factor = EXP10_TABLE.factors[idx]
	value_abs = float(accumulator) * factor
	# value_abs = scaleAndConvert(accumulator, factor)
	if (not USE_ORIGINAL_TAIL) and ENABLE_BOOST and exponent <= BREAK_EXP:
		value_abs *= BOOST_V_RCP
	return math.copysign(value_abs, sign)

def double_bits(x: float) -> int:
	return struct.unpack('>Q', struct.pack('>d', x))[0]

def bits_to_double(u: int) -> float:
	return struct.unpack('>d', struct.pack('>Q', u))[0]

def run_test(n=200000, seed=1234, use_repr=True):
	random.seed(seed)
	mismatches = 0
	highest = -999
	examples = []
	count = 0
	t0 = time.time()
	while count < n:
		u = random.getrandbits(64)
		x = bits_to_double(u)
		if not math.isfinite(x):
			continue
		s = repr(x) if use_repr else f"{x:.17e}"
		# compute exp10 for stats
		p = 1 if s[0] in '+-' else 0
		exp10 = -1; i = p
		while i < len(s) and s[i].isdigit(): exp10 += 1; i += 1
		if i < len(s) and s[i] == '.':
			i += 1
			while i < len(s) and s[i].isdigit(): i += 1
		if i < len(s) and s[i] in 'eE':
			i += 1; es = 1
			if i < len(s) and s[i] in '+-': es = -1 if s[i]=='-' else 1; i += 1
			j = i
			while j < len(s) and s[j].isdigit(): j += 1
			if j > i: exp10 += es * int(s[i:j])
		y = parseReal(s)
		if double_bits(x) != double_bits(y):
			mismatches += 1
			if exp10 > highest: highest = exp10
			if len(examples) < 10:
				examples.append((f"0x{u:016x}", s, exp10, f"0x{double_bits(y):016x}", f"{y:.17e}", f"{x:.17e}"))
		count += 1
	t1 = time.time()
	print(f"[USE_ORIGINAL_TAIL={USE_ORIGINAL_TAIL}, ENABLE_BOOST={ENABLE_BOOST}] total tested: {n}, mismatches: {mismatches}, highest_exp10: {highest}, time_s: {round(t1-t0,2)}")
	if examples:
		for row in examples:
			print('bits:', row[0])
			print('str: ', row[1])
			print('exp10:', row[2])
			print('parsed_bits:', row[3])
			print('parsed_17e:', row[4])
			print('oracle_17e:', row[5])
			print('----')

if __name__ == '__main__':
	# defaults: shortest string (repr), 200k samples
	n = int(sys.argv[1]) if len(sys.argv) > 1 else 200000
	seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1234
	run_test(n, seed, use_repr=True)
