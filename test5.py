#!/usr/bin/env python3
# Float32 parser that mirrors the C++ path exactly:
#  - Exp10Table is built with DoubleDouble for ALL exponents
#  - negative tail uses the same guarded /16 normalization
#  - accumulation uses DoubleDouble magnitudes
#  - final scale is static_cast<float>((double)acc * factor)

import math, struct, random, sys, time

# -------------------- bit helpers --------------------

def f32_from_bits(u: int) -> float:
    return struct.unpack('>f', struct.pack('>I', u & 0xffffffff))[0]

def f32_bits(x: float) -> int:
    return struct.unpack('>I', struct.pack('>f', float(x)))[0]

# -------------------- DoubleDouble --------------------

class DoubleDouble:
    __slots__ = ("high", "low")
    def __init__(self, high: float = 0.0, low: float = 0.0):
        self.high = float(high)
        self.low  = float(low)
    def __add__(self, other: "DoubleDouble") -> "DoubleDouble":
        low_sum = self.low + other.low
        overflow = math.floor(low_sum)
        return DoubleDouble((self.high + other.high) + overflow, low_sum - overflow)
    def __mul__(self, factor: int) -> "DoubleDouble":
        low_times_factor = self.low * factor
        overflow = math.floor(low_times_factor)
        return DoubleDouble((self.high * factor) + overflow, low_times_factor - overflow)
    def __truediv__(self, divisor) -> "DoubleDouble":
        d = float(divisor)
        assert d != 0.0
        q_int = math.floor(self.high / d)   # exact (high < 2^53)
        rem   = self.high - q_int * d
        low_div = (self.low + rem) / d
        carry = math.floor(low_div)
        return DoubleDouble(q_int + carry, low_div - carry)
    def __lt__(self, other: "DoubleDouble") -> bool:
        return self.high < other.high or (self.high == other.high and self.low < other.low)
    def __float__(self) -> float:
        return self.high + self.low

# ----------------- multiplyAndAdd (DD) ----------------

def multiplyAndAdd(term: DoubleDouble, factorA: DoubleDouble, digit: int) -> DoubleDouble:
    fmaLow = factorA.low * digit + term.low
    overflow = math.floor(fmaLow)
    return DoubleDouble(factorA.high * digit + term.high + overflow, fmaLow - overflow)

# ------------------- Traits<float> --------------------

class TraitsFloat:
    MIN_EXPONENT = -45
    MAX_EXPONENT = 38
    # Hires is double in C++ for float; accumulator is a Python float (binary64).

# --------------- Exp10Table (DoubleDouble) ------------

class Exp10Table:
    def __init__(self):
        self.normals = [None] * (TraitsFloat.MAX_EXPONENT + 1 - TraitsFloat.MIN_EXPONENT)
        self.factors = [0.0 ] * (TraitsFloat.MAX_EXPONENT + 1 - TraitsFloat.MIN_EXPONENT)
        WIDTH = math.ldexp(1.0, 53 - 4)

        # Non-negative exponents
        normal = DoubleDouble(WIDTH, 0.0)
        factor = 1.0 / WIDTH
        for i in range(0, TraitsFloat.MAX_EXPONENT + 1):
            if normal.high >= WIDTH:
                factor *= 16.0
                normal = normal / 16.0
            self.normals[i - TraitsFloat.MIN_EXPONENT] = normal
            self.factors[i - TraitsFloat.MIN_EXPONENT] = factor
            normal = normal * 10

        # Negative exponents (guarded normalization exactly like the C++)
        normal = DoubleDouble(WIDTH, 0.0)
        factor = 1.0 / WIDTH
        for i in range(-1, TraitsFloat.MIN_EXPONENT - 1, -1):
            if normal.high < WIDTH and (factor / 16.0) > 0.0:
                factor /= 16.0
                normal = normal * 16
            normal = normal / 10.0
            self.normals[i - TraitsFloat.MIN_EXPONENT] = normal
            self.factors[i - TraitsFloat.MIN_EXPONENT] = factor

EXP10F = Exp10Table()

# -------------------- parseUnsignedInt -----------------

def parseUnsignedInt(s: str, i: int):
    ui = 0
    n = len(s)
    start = i
    while i < n and s[i].isdigit():
        ui = ui * 10 + (ord(s[i]) - 48)
        i += 1
    return (ui, i, (i != start))

# -------------------- parseReal<float> -----------------

NEGATIVE_E_NOTATION_START = -6
POSITIVE_E_NOTATION_START = 10

def parseReal_f32(text: str) -> float:
    s = text
    n = len(s)
    p = 0

    # sign
    sign = 1.0
    if p < n and (s[p] == '-' or s[p] == '+'):
        sign = -1.0 if s[p] == '-' else 1.0
        p += 1
        significandBegin = p
    else:
        significandBegin = p

    # inf / nan
    tail = s[p:].lower()
    if tail.startswith("inf"):
        return math.copysign(math.inf, sign)
    if tail.startswith("nan"):
        return math.nan

    # integer part
    exponent = -1
    while p < n and s[p].isdigit():
        exponent += 1
        p += 1

    # fraction
    if p < n and s[p] == '.':
        if p == significandBegin:
            significandBegin += 1
        p += 1
        while p < n and s[p].isdigit():
            p += 1

    if p == significandBegin:
        return math.copysign(0.0, sign)

    significandEnd = p

    # exponent part
    if p < n and (s[p] == 'e' or s[p] == 'E'):
        p += 1
        exp_sign = 1
        if p < n and (s[p] == '+' or s[p] == '-'):
            exp_sign = -1 if s[p] == '-' else 1
            p += 1
        ui, p2, had = parseUnsignedInt(s, p)
        if had:
            exponent += exp_sign * ui
            p = p2

    # strip leading zeros
    q = significandBegin
    while q < significandEnd and (s[q] == '0' or s[q] == '.'):
        if s[q] == '0':
            exponent -= 1
        q += 1

    # clamp to float32 range in base-10 (same constants as Traits<float>)
    if q == significandEnd or exponent < TraitsFloat.MIN_EXPONENT:
        return math.copysign(0.0, sign)
    if exponent > TraitsFloat.MAX_EXPONENT:
        return math.copysign(math.inf, sign)

    # build accumulator using DoubleDouble magnitudes
    idx = exponent - TraitsFloat.MIN_EXPONENT
    magnitude = EXP10F.normals[idx]
    acc = DoubleDouble(0.0, 0.0)
    j = q
    while j < significandEnd:
        if s[j] != '.':
            acc = multiplyAndAdd(acc, magnitude, ord(s[j]) - 48)
            magnitude = magnitude / 10.0
        j += 1

    factor = EXP10F.factors[idx]

    # final scale-and-cast EXACTLY like C++ float path:
    #   static_cast<float>( (double)acc * factor )
    acc_double = float(acc)
    val_abs64 = acc_double * factor
    val_f32 = f32_from_bits(f32_bits(val_abs64))
    return math.copysign(val_f32, sign)

# -------------------- test & fuzz ----------------------

def shortest_float32(x: float) -> str:
    # 9 significant digits are sufficient for IEEE754 binary32 roundtrip
    return f"{x:.9g}"

def demo_case():
    # From your report: bits 95ae43fe -> -7.0385313e-26
    u = 0x95ae43fe
    x = f32_from_bits(u)
    s = shortest_float32(x)
    y = parseReal_f32(s)
    print("bits:", f"{u:08x}")
    print(" str:", s)
    print(" ours:", f"{y:.9g}")
    print(" ok?:", "YES" if f32_bits(x) == f32_bits(y) else "NO")

def fuzz(count=200_000, seed=1234):
    random.seed(seed)
    mismatches = 0
    t0 = time.time()
    for _ in range(count):
        u = random.getrandbits(32)
        # Keep only finite values
        exp = (u >> 23) & 0xff
        if exp == 0xff:
            continue
        x = f32_from_bits(u)
        s = shortest_float32(x)
        y = parseReal_f32(s)
        if f32_bits(x) != f32_bits(y):
            mismatches += 1
            if mismatches <= 10:
                print("float mismatch")
                print("bits:", f"{u:08x}")
                print(" str:", s)
                print(" orig:", f"{x:.9g}")
                print(" pars:", f"{y:.9g}")
                print("----")
    t1 = time.time()
    print(f"total tested: {count}, mismatches: {mismatches}, time_s: {t1 - t0:.2f}")

if __name__ == "__main__":
    demo_case()
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 200_000
    fuzz(n)
