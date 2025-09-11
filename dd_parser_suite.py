#!/usr/bin/env python3
import math, random, struct, sys, time, json
from decimal import Decimal, getcontext

# ------------------------ config ------------------------

MIN_EXPONENT = -324
MAX_EXPONENT = 308

# Use the exact original negative-tail logic (no “boost” experiments here)
USE_ORIGINAL_TAIL = True

# Decimal precision for the “oracle” scale
DECIMAL_PREC = 100  # 21 works in practice; 42 gives headroom

SUITE_PATH = "dd_suite.json"

# Scaler-suite quotas
# - for each scaler, collect this many failure rows
TARGET_FAIL_PER_SCALER = 50
# - and also collect this many rows that work with all scalers
TARGET_WORKS_ALL = 50

# ---------------------- helpers -------------------------

def double_bits(x: float) -> int:
    return struct.unpack(">Q", struct.pack(">d", x))[0]

def bits_to_double(u: int) -> float:
    return struct.unpack(">d", struct.pack(">Q", u))[0]

def dbl_hex(x: float) -> str:
    return f"0x{double_bits(x):016x}"

def as_hex(x: float) -> str:
    return f"0x{double_bits(float(x)):016x}"

def exp10_of_str(s: str) -> int:
    p = 1 if s and s[0] in "+-" else 0
    exp10 = -1; i = p
    L = len(s)
    while i < L and s[i].isdigit(): exp10 += 1; i += 1
    if i < L and s[i] == '.':
        i += 1
        while i < L and s[i].isdigit(): i += 1
    if i < L and s[i] in 'eE':
        i += 1; sign = 1
        if i < L and s[i] in '+-':
            sign = -1 if s[i]=='-' else 1; i += 1
        j = i
        while j < L and s[j].isdigit(): j += 1
        if j > i: exp10 += sign * int(s[i:j])
    return exp10

# ------------------- DoubleDouble & table ----------------

class DoubleDouble:
    def __init__(self, high: float=0.0, low: float=0.0):
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
    def __truediv__(self, divisor):
        d = float(divisor)
        assert d != 0.0
        q_int = math.floor(self.high / d)           # exact (high is int < 2^53)
        rem   = self.high - q_int * d
        low_div = (self.low + rem) / d
        carry = math.floor(low_div)
        return DoubleDouble(q_int + carry, low_div - carry)
    def __lt__(self, other):
        return self.high < other.high or (self.high == other.high and self.low < other.low)
    def __float__(self):
        return self.high + self.low

def multiplyAndAdd(term: DoubleDouble, factorA: DoubleDouble, digit: int):
    fmaLow = factorA.low * digit + term.low
    overflow = math.floor(fmaLow)
    return DoubleDouble(factorA.high * digit + term.high + overflow, fmaLow - overflow)

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

        # Negative exponents — original tail
        normal = DoubleDouble(WIDTH, 0.0)
        factor = 1.0 / WIDTH
        for i in range(-1, MIN_EXPONENT - 1, -1):
            # avoid normalizing once factor/16 underflows to 0
            if normal.high < WIDTH and (factor / 16.0) > 0.0:
                factor /= 16.0
                normal = normal * 16
            normal = normal / 10
            self.normals[i - MIN_EXPONENT] = normal
            self.factors[i - MIN_EXPONENT] = factor

EXP10_TABLE = Exp10Table()

# ---------------- parse into (accumulator, factor) ----------------

def parse_components(s: str):
    """Return (sign, exponent, idx, accumulator(DoubleDouble), factor(float), significand_end_str)."""
    p = 0; e = len(s)
    sign = 1.0
    if p < e and (s[p] == '-' or s[p] == '+'):
        sign = -1.0 if s[p] == '-' else 1.0; p += 1
    t = s[p:].lower()
    if t.startswith("inf") or t.startswith("nan"):
        return (sign, None, None, None, None, None)

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
        return (sign, None, None, None, None, None)
    significandEnd = p

    if p < e and (s[p] == 'e' or s[p] == 'E'):
        p += 1; exp_sign = 1
        if p < e and (s[p] == '+' or s[p] == '-'):
            exp_sign = -1 if s[p] == '-' else 1; p += 1
        ui = 0; had = False
        while p < e and s[p].isdigit():
            ui = ui * 10 + (ord(s[p]) - 48); p += 1; had = True
        if had: exponent += exp_sign * ui

    p2 = significandBegin
    while p2 < significandEnd and (s[p2] == '0' or s[p2] == '.'):
        if s[p2] == '0': exponent -= 1
        p2 += 1

    if p2 == significandEnd or exponent < MIN_EXPONENT or exponent > MAX_EXPONENT:
        return (sign, exponent, None, None, None, s)

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
    return (sign, exponent, idx, acc, factor, s)

# --- New helpers ---

def two_sum(a: float, b: float):
    """
    Knuth TwoSum: returns (s, e) with s = fl(a+b), and s+e = exact(a+b).
    """
    s = a + b
    bb = s - a
    e = (a - (s - bb)) + (b - bb)
    return s, e

def ulp(x: float) -> float:
    """Unit in the last place of x under IEEE-754 round-to-nearest-even."""
    if x == 0.0:
        # Smallest subnormal ULP
        return math.ldexp(1.0, -1074)
    m, e2 = math.frexp(abs(x))  # x = m * 2^(e2-1), m in [0.5,1)
    # For normals, ulp = 2^(e2-53) because doubles have 53 sig bits incl. hidden bit.
    return math.ldexp(1.0, e2 - 53)

def nextafter(x: float, toward: float) -> float:
    """Minimal step toward 'toward'. Works for all finite x."""
    import struct
    if math.isnan(x) or math.isnan(toward):
        return math.nan
    if x == toward:
        return toward
    if x == 0.0:
        # Step to smallest subnormal with correct sign
        return math.copysign(math.ldexp(1.0, -1074), toward)
    u = struct.unpack('>Q', struct.pack('>d', x))[0]
    if (x > 0.0) == (toward > x):
        u += 1
    else:
        u -= 1
    return struct.unpack('>d', struct.pack('>Q', u))[0]

def round_using_error(s: float, err: float) -> float:
    """
    Given s = fl(a+b) and err so that s+err = exact(a+b), adjust s to correctly-rounded result.
    """
    u = ulp(s)
    half = 0.5 * u
    ae = abs(err)
    if ae > half:
        # Clearly on the other side of halfway: bump toward err.
        return nextafter(s, s + err)
    if ae < half:
        # Clearly closer to s: leave as-is.
        return s
    # Exactly halfway: round-to-even (i.e., adjust if s is odd ULP)
    # Check if s is exactly mid; if so and s's last bit is odd, nudge toward err.
    # We can detect parity via mantissa LSB.
    sbits = double_bits(s)
    lsb_is_odd = (sbits & 1) == 1
    if lsb_is_odd:
        return nextafter(s, s + err)
    return s

# --- New scaler (double-only, no Decimal, no int64) ---

def scale_float_twosum_round(acc: DoubleDouble, factor: float) -> float:
    """
    Exact power-of-two scaling of each limb, then TwoSum with error-guided rounding.
    This fixes the denormal-boundary mismatches seen with
      - float(acc)*factor
      - acc.high*factor + acc.low*factor
    """
    # Multiplication by power-of-two is exact in binary64:
    ah = math.ldexp(acc.high, math.frexp(factor)[1] - 1)  # since factor is 2^k => m=0.5
    al = math.ldexp(acc.low,  math.frexp(factor)[1] - 1)
    s, e = two_sum(ah, al)
    return round_using_error(s, e)

getcontext().prec = DECIMAL_PREC

def scale_float(acc: DoubleDouble, factor: float) -> float:
    return float(acc) * factor

def scale_float_2(acc: DoubleDouble, factor: float) -> float:
    return (acc.high * factor + acc.low * factor)

def scale_float_shifted(acc, factor):
    # Bring factor into the normal range by multiplying with 2^T
    m, e = math.frexp(factor)     # factor = m * 2**e, with 0.5<=m<1 or subnormal normalized
    # Choose T so factor2 is safely normal (>= 2**(-1022)). Add a little headroom.
    T = max(0, (-1022 - e) + 2)
    factor2 = math.ldexp(factor, T)         # normal
    s = (acc.high * factor2) + (acc.low * factor2)   # all in normal range, single rounding on the add
    return math.ldexp(s, -T)                # one final rounding when shifting back

def scale_decimal(acc: DoubleDouble, factor: float) -> float:
    # “oracle”: higher-precision sum then scale then round once
    return float((Decimal(acc.high) + Decimal(acc.low)) * Decimal(factor))


# -------------------------
# helpers
# -------------------------
def double_bits(x: float) -> int:
    return struct.unpack('>Q', struct.pack('>d', x))[0]

def bits_to_double(u: int) -> float:
    return struct.unpack('>d', struct.pack('>Q', u))[0]

EMIN = 1 - 1023 - 52  # -1074, minimum unbiased exponent for IEEE-754 double (subnormal ULP = 2^EMIN)

def round_even_double(x: float) -> float:
    """
    Round-to-nearest, ties-to-even using only IEEE doubles.
    Works for any magnitude (no int64 needed).
    """
    i = math.floor(x)
    f = x - i        # 0 <= f < 1
    if f < 0.5: 
        return i
    if f > 0.5: 
        return i + 1.0
    # Tie: f == 0.5 -> round to even
    # i is an integer in double; i % 2 == 0 tests evenness
    return i if (math.fmod(i, 2.0) == 0.0) else (i + 1.0)

def scale_power2_subnormal_doubleonly(acc, factor: float):
    """
    acc: object with .high (integer < 2^53) and .low in [0,1)
    factor: exact power-of-two (as in your table)
    neg: boolean sign for the final value

    Returns (handled: bool, value: float). If handled==True, 'value' is the
    correctly rounded subnormal result. If handled==False, caller should use
    the normal path (not subnormal).
    """
    # zero fast-path
    if acc.high == 0.0 and acc.low == 0.0:
        return True, 0.0

    # factor = m * 2^(e2-1), 0.5 <= m < 1. For power-of-two, m==0.5 exactly.
    m, e2 = math.frexp(factor)    # factor = m * 2^(e2)
    E2 = e2 - 1                   # real power-of-two exponent

    if E2 >= EMIN:
        # Result will be normal (or zero) — let the normal path handle it.
        return False, 0.0

    # K = how many subnormal ULPs we scale by before packing back
    K = E2 - EMIN   # typically positive around the denormal boundary

    # Exact power-of-two scaling with ldexp:
    A = math.ldexp(acc.high, K)   # integer, exact
    B = math.ldexp(acc.low,  K)   # exact scaled fractional contribution

    RB = round_even_double(B)     # integer in double
    N  = A + RB                   # integer payload in double

    # Boundaries for subnormals: payload 1..(2^52-1)
    max_payload = math.ldexp(1.0, 52) - 1.0

    if N <= 0.0:
        return True, 0.0

    if N > max_payload:
        # Rounds up into minimal normal
        y = math.ldexp(1.0, EMIN + 1)   # 2^(emin+1)
        return True, y

    # Pack: value = N * 2^EMIN
    y = math.ldexp(N, EMIN)
    return True, y


# Example “finish” function to replace the old final scaling:
def finish_scale(acc: DoubleDouble, factor: float) -> float:
    # Try subnormal-specialized path (exact powers of two assumed)
    handled, y = scale_power2_subnormal_doubleonly(acc, factor)
    if handled:
        return y
    # Normal case: your legacy path (two multiplies + add). This matches your C++.
    # (You can also use float(acc) * factor if you prefer.)
    return acc.high * factor + acc.low * factor

def scale_float_twosum_round(acc: DoubleDouble, factor: float) -> float:
    # (paste the implementation above here)
    ah = math.ldexp(acc.high, math.frexp(factor)[1] - 1)
    al = math.ldexp(acc.low,  math.frexp(factor)[1] - 1)
    s, e = two_sum(ah, al)
    return round_using_error(s, e)

import math, struct

def double_bits(x: float) -> int:
    return struct.unpack('>Q', struct.pack('>d', x))[0]

def bits_to_double(u: int) -> float:
    return struct.unpack('>d', struct.pack('>Q', u))[0]

def pow2_exponent_from_bits(f: float) -> int:
    """
    Return k such that f == 2**k, using IEEE-754 bit layout.
    Assumes f > 0 and is exactly a power of two; asserts otherwise.
    Works for normals and subnormals.
    """
    u = double_bits(f)
    exp = (u >> 52) & 0x7FF
    frac = u & ((1 << 52) - 1)

    if exp == 0:
        # subnormal: value = (frac / 2**52) * 2**(-1022) = frac * 2**(-1074)
        # must be exact power of two => frac must have exactly one bit set
        assert frac != 0 and (frac & (frac - 1)) == 0, f"factor not pure pow2 (subnormal): bits=0x{u:016x}"
        # position of the bit (0..51) => k = -1074 + pos
        pos = (frac.bit_length() - 1)  # msb index
        return -1074 + pos
    else:
        # normal: value = (1 + frac/2**52) * 2**(exp-1023)
        # exact pow2 implies frac == 0
        assert frac == 0, f"factor not pure pow2 (normal): bits=0x{u:016x}"
        return exp - 1023

def nextafter(x: float, toward: float) -> float:
    if math.isnan(x) or math.isnan(toward):
        return math.nan
    if x == toward:
        return toward
    if x == 0.0:
        return math.copysign(math.ldexp(1.0, -1074), toward)
    u = double_bits(x)
    if (x > 0.0) == (toward > x):
        u += 1
    else:
        u -= 1
    return bits_to_double(u)

def ulp(x: float) -> float:
    if x == 0.0:
        return math.ldexp(1.0, -1074)
    m, e2 = math.frexp(abs(x))  # x = m * 2**e2, m in [0.5,1)
    return math.ldexp(1.0, e2 - 53)

def fast_two_sum(a: float, b: float):
    """Requires |a| >= |b|. Returns (s,e) with s = fl(a+b), s+e = exact."""
    s = a + b
    e = b - (s - a)
    return s, e

def round_using_error(s: float, err: float) -> float:
    u = ulp(s)
    half = 0.5 * u
    ae = abs(err)
    if ae > half:
        return nextafter(s, s + err)
    if ae < half:
        return s
    # exact tie: round to even
    sbits = double_bits(s)
    lsb_is_odd = (sbits & 1) == 1
    return nextafter(s, s + err) if lsb_is_odd else s

# --- Scalers ---

def scale_float_shifted_2(acc, factor: float) -> float:
    # your previous shifted variant (kept for comparison)
    m, e2 = math.frexp(factor)     # factor = m * 2**e2, with m in [0.5,1)
    k = e2 - 1                     # for a pure 2**k, m==0.5 so k==e2-1
    ah = math.ldexp(acc.high, k)
    al = math.ldexp(acc.low,  k)
    return ah + al

def scale_float_twosum_round_v2(acc, factor: float) -> float:
    """
    Power-of-two shift from *bits* (no frexp), then FastTwoSum + error-guided rounding.
    This removes all dependence on frexp subtleties around subnormals.
    """
    assert factor > 0.0
    k = pow2_exponent_from_bits(factor)

    # exact power-of-two scaling
    ah = math.ldexp(acc.high, k)
    al = math.ldexp(acc.low,  k)

    # enforce |ah| >= |al| for FastTwoSum (true in practice, but be safe)
    if abs(al) > abs(ah):
        ah, al = al, ah

    s, e = fast_two_sum(ah, al)
    return round_using_error(s, e)

import math, struct

def double_bits(x: float) -> int:
    import struct
    return struct.unpack('>Q', struct.pack('>d', x))[0]

def bits_to_double(u: int) -> float:
    import struct
    return struct.unpack('>d', struct.pack('>Q', u))[0]

def pow2_exponent_from_bits(f: float) -> int:
    """Return k where f == 2**k. Asserts f is a pure power of two (normal or subnormal)."""
    u = double_bits(f)
    exp = (u >> 52) & 0x7FF
    frac = u & ((1 << 52) - 1)
    if exp == 0:
        # subnormal: f = frac * 2**(-1074), must be a single-bit frac
        assert frac != 0 and (frac & (frac - 1)) == 0, f"factor not pure pow2: 0x{u:016x}"
        return -1074 + (frac.bit_length() - 1)
    else:
        assert frac == 0, f"factor not pure pow2: 0x{u:016x}"
        return exp - 1023

def fast_two_sum(a: float, b: float):
    # requires |a| >= |b|
    s = a + b
    e = b - (s - a)
    return s, e

def scale_float_prescale(acc, factor: float) -> float:
    """
    All-double, no Decimal:
    1) exact power-of-two scale (k from bits)
    2) prescale by +S so that the larger addend is normal
    3) FastTwoSum in that safe regime
    4) scale back by -S
    """
    # exact shift from factor
    k = pow2_exponent_from_bits(factor)
    ah0 = math.ldexp(acc.high, k)
    al0 = math.ldexp(acc.low,  k)

    # Ensure |ah0| >= |al0|
    if abs(al0) > abs(ah0):
        ah0, al0 = al0, ah0

    if ah0 == 0.0:
        # trivial
        return ah0 + al0

    # Find exponent of |ah0|; if subnormal, bring it into the normal range.
    m, e2 = math.frexp(ah0)   # ah0 = m * 2**e2, m in [0.5,1)
    if e2 <= -1022:
        # Need to lift by S so that ah = ldexp(ah0, S) is normal.
        # Make it comfortably normal (e.g., target exponent = -1020).
        S = (-1020 - e2)
        ah = math.ldexp(ah0, S)
        al = math.ldexp(al0, S)
        # |ah| >= |al| still holds
        s, e = fast_two_sum(ah, al)
        # scale back; one rounding happens here
        return math.ldexp(s, -S)
    else:
        # already normal; plain add is fine
        return ah0 + al0

def pow2_exponent_of_double(val: float) -> int:
    b = double_bits(val)
    exp = (b >> 52) & 0x7ff
    frac = b & ((1 << 52) - 1)
    if exp == 0:
        assert frac != 0 and (frac & (frac - 1)) == 0
        top = frac.bit_length() - 1
        return -1074 + top
    else:
        assert frac == 0
        return exp - 1023

def round_to_even_from_parts(int_part: int, frac_part: float) -> int:
    if frac_part < 0.5:
        return int_part
    if frac_part > 0.5:
        return int_part + 1
    return int_part if (int_part & 1) == 0 else int_part + 1

def scale_power2_subnormal_exact(acc: DoubleDouble, factor: float) -> float:
    if acc.high == 0.0 and acc.low == 0.0:
        return 0.0

    E = pow2_exponent_of_double(factor)
    if E >= -1022:
        return None

    T = E + 1074
    H = int(acc.high)
    L = acc.low

    if T >= 0:
        A = H << T
        Bf = math.ldexp(L, T)
        Bi = int(Bf)
        frac = Bf - Bi
        N = round_to_even_from_parts(A + Bi, frac)
    else:
        k = -T
        q = H >> k
        r = H & ((1 << k) - 1)
        Bf = (r + math.ldexp(L, k)) / (1 << k)
        N = round_to_even_from_parts(q, Bf)

    if N <= 0:
        return 0.0
    if N > (1 << 52) - 1:
        N = (1 << 52) - 1

    bits = N
    return bits_to_double(bits)

def scale_power2(acc: DoubleDouble, factor: float) -> float:
    E = pow2_exponent_of_double(factor)
    if E < -1022:
        y = scale_power2_subnormal_exact(acc, factor)
        if y is not None:
            return y
    return (acc.high + acc.low) * factor

from decimal import Decimal
import math

def is_denormal(f: float) -> bool:
    u = double_bits(f)
    return ((u >> 52) & 0x7FF) == 0 and (u & ((1 << 52) - 1)) != 0

def scale_decimal_denormal(acc: DoubleDouble, factor: float) -> float:
    S = Decimal(acc.high) + Decimal(acc.low)
    if acc.high == 0.0 and acc.low == 0.0:
        return 0.0

    E = pow2_exponent_of_double(factor)
    if E < -1022:
        # Shift enough to guarantee subnormal even for S near 2^53
        pre_exp = -53 #-(1022 + 53)
        S_scaled = S * (Decimal(2) ** pre_exp)
        while S_scaled < Decimal(0.5):
            S_scaled *= 2
            pre_exp += 1
        pre_exp -= 1021
        S_scaled = S * (Decimal(2) ** pre_exp)
        x = float(S_scaled)
        # print(str(acc.high) + " : " + dbl_hex(x))

        assert not is_denormal(x), f"Expected subnormal, got bits=0x{double_bits(x):016x}"
        # assert x < 1.0, f"Expected <1.0, got {x}"

        remaining_shift = E - pre_exp
        return math.ldexp(x, remaining_shift)

    # Normal path
    return float(S * Decimal(factor))

# Register scalers here to compare. Order defines report order.
SCALERS = [
    ("floatScale", scale_float),
    ("floatScale2", scale_float_2),
    ("scale_float_shifted", scale_float_shifted),
    ("finish_scale", finish_scale),
    ("scale_float_twosum_round", scale_float_twosum_round),
    ("scale_float_shifted_2", scale_float_shifted_2),
    ("scale_float_twosum_round_v2", scale_float_twosum_round_v2),
    ("scale_float_prescale", scale_float_prescale),
    ("scale_power2", scale_power2),
    ("scale_decimal_denormal", scale_decimal_denormal),
]

# ---------------- suite builder & runner ----------------

def build_suite():
    random.seed(2025)

    per_scaler_target = TARGET_FAIL_PER_SCALER
    works_all_target = TARGET_WORKS_ALL

    assigned_fail_rows = {name: [] for name, _ in SCALERS}
    works_all_rows = []
    seen_bits = set()

    def all_targets_met():
        if len(works_all_rows) < works_all_target:
            return False
        for name in assigned_fail_rows:
            if len(assigned_fail_rows[name]) < per_scaler_target:
                return False
        return True

    def row_from_x(x: float):
        s = repr(x)
        sign, exponent, idx, acc, factor, _ = parse_components(s)
        if acc is None:
            return None
        oracle_bits = double_bits(x)
        algo_results = {}
        any_fail = False
        all_pass = True
        for name, fn in SCALERS:
            y = sign * fn(acc, factor)
            bits = double_bits(y)
            fails = (bits != oracle_bits)
            any_fail = any_fail or fails
            all_pass = all_pass and (not fails)
            algo_results[name] = {
                "bits": f"0x{bits:016x}",
                "value_17e": f"{y:.17e}",
                "fails_oracle": fails,
            }
        # optional: decimal-based reconstruction for debug
        y_dec = sign * scale_decimal(acc, factor)
        row = {
            "orig_bits": f"0x{oracle_bits:016x}",
            "str": s,
            "exp10": exp10_of_str(s),
            "exponent": exponent,
            "idx": idx,
            "acc_high": acc.high,
            "acc_low": acc.low,
            "acc_high_hex": as_hex(acc.high),
            "acc_low_hex": as_hex(acc.low),
            "factor": factor,
            "factor_hex": dbl_hex(factor),
            "oracle_17e": f"{x:.17e}",
            "decimal_bits": f"0x{double_bits(y_dec):016x}",
            "decimal_17e": f"{y_dec:.17e}",
            "algo_results": algo_results,
            "assigned": None,
        }
        return row, any_fail, all_pass

    def try_assign(row, any_fail, all_pass):
        key = row["orig_bits"]
        if key in seen_bits:
            return False
        if all_pass:
            if len(works_all_rows) < works_all_target:
                row["assigned"] = "works_all"
                works_all_rows.append(row)
                seen_bits.add(key)
                return True
            return False
        failing = [name for name, _ in SCALERS if row["algo_results"][name]["fails_oracle"]]
        candidate = None
        best_count = 1 << 60
        for name in failing:
            cnt = len(assigned_fail_rows[name])
            if cnt < per_scaler_target and cnt < best_count:
                best_count = cnt
                candidate = name
        if candidate is None:
            return False
        row["assigned"] = candidate
        assigned_fail_rows[candidate].append(row)
        seen_bits.add(key)
        return True

    # Strategy 1: sweep many subnormals (exponent field == 0)
    step = 97_3
    for u in range(1, 1<<20, step):
        if all_targets_met():
            break
        x = bits_to_double(u)
        if not math.isfinite(x):
            continue
        out = row_from_x(x)
        if out is None:
            continue
        row, any_fail, all_pass = out
        try_assign(row, any_fail, all_pass)

    # Strategy 2: around the smallest normal and its neighborhood
    base = 0x0010000000000000
    for off in range(0, 1<<18, 1019):
        if all_targets_met():
            break
        for signbit in (0, 1<<63):
            u = base + off + signbit
            x = bits_to_double(u)
            if not math.isfinite(x):
                continue
            out = row_from_x(x)
            if out is None:
                continue
            row, any_fail, all_pass = out
            try_assign(row, any_fail, all_pass)

    # Strategy 3: random fallbacks
    tries = 0
    while not all_targets_met():
        if tries > 5_000_000:
            break
        tries += 1
        u = random.getrandbits(64)
        x = bits_to_double(u)
        if not math.isfinite(x):
            continue
        out = row_from_x(x)
        if out is None:
            continue
        row, any_fail, all_pass = out
        try_assign(row, any_fail, all_pass)

    # Build final suite: interleave per-scaler groups then append works-all, then randomize
    suite = []
    buckets = [assigned_fail_rows[name] for name, _ in SCALERS]
    max_len = max((len(b) for b in buckets), default=0)
    for i in range(max_len):
        for b in buckets:
            if i < len(b):
                suite.append(b[i])
    suite.extend(works_all_rows)
    random.shuffle(suite)

    with open(SUITE_PATH, "w") as f:
        json.dump(suite, f, indent=2)

    print(f"Built suite: {len(suite)} rows. Saved to {SUITE_PATH}")
    for name, _ in SCALERS:
        print(f"  assigned failures for {name}: {len(assigned_fail_rows[name])}")
    print(f"  assigned works_all: {len(works_all_rows)}")

def run_suite():
    with open(SUITE_PATH, "r") as f:
        suite = json.load(f)
    print(f"Loaded {len(suite)} rows from {SUITE_PATH}")

    per_scaler_mismatches = {name: 0 for name, _ in SCALERS}
    decimal_vs_oracle = 0
    assigned_group_breakdown = {"works_all": 0, **{name: 0 for name, _ in SCALERS}}

    for i, row in enumerate(suite):
        s = row["str"]
        sign, exponent, idx, acc, factor, _ = parse_components(s)
        if acc is None:
            print(f"[{i}] SKIP (non-finite or empty)")
            continue

        x = bits_to_double(int(row["orig_bits"], 16))

        # Optional: sanity check on decimal reconstruction
        y_dec = sign * scale_decimal(acc, factor)
        if double_bits(y_dec) != double_bits(x):
            decimal_vs_oracle += 1
            print(f"[{i}] Decimal != Oracle? BUG")
            print(row)
            break

        for name, fn in SCALERS:
            y = sign * fn(acc, factor)
            if double_bits(y) != double_bits(x):
                per_scaler_mismatches[name] += 1

        assigned = row.get("assigned")
        if assigned in assigned_group_breakdown:
            assigned_group_breakdown[assigned] += 1

    print("Summary on suite:")
    for name, _ in SCALERS:
        print(f"  {name} vs oracle mismatches: {per_scaler_mismatches[name]}/{len(suite)}")
    print(f"  decimalScale vs oracle:        {decimal_vs_oracle}/{len(suite)}")
    if any(v > 0 for v in assigned_group_breakdown.values()):
        print("  assigned groups:")
        for key, val in assigned_group_breakdown.items():
            print(f"    {key}: {val}")

def fuzz(n=200000, seed=1234):
    random.seed(seed)
    t0 = time.time()

    per_scaler_mismatches = {name: 0 for name, _ in SCALERS}
    decimal_vs_oracle = 0
    highest = -999

    count = 0
    while count < n:
        u = random.getrandbits(64)
        x = bits_to_double(u)
        if not math.isfinite(x):
            continue
        s = repr(x)
        sign, exponent, idx, acc, factor, _ = parse_components(s)
        if acc is None:
            continue

        oracle_bits = double_bits(x)

        # Decimal sanity check
        y_dec = sign * scale_decimal(acc, factor)
        if double_bits(y_dec) != oracle_bits:
            decimal_vs_oracle += 1

        # Check all scalers
        ex10 = exp10_of_str(s)
        for name, fn in SCALERS:
            y = sign * fn(acc, factor)
            if double_bits(y) != oracle_bits:
                per_scaler_mismatches[name] += 1
                highest = max(highest, ex10)

        count += 1

    t1 = time.time()
    print(f"[FUZZ] total: {n}, time_s: {round(t1-t0,2)}")
    for name, _ in SCALERS:
        print(f"  {name} vs oracle mismatches: {per_scaler_mismatches[name]}")
    print(f"  decimalScale vs oracle mismatches: {decimal_vs_oracle}")
    print(f"  highest_exp10_among_mismatches: {highest}")

# ------------------------- main -------------------------

def main():
    if len(sys.argv) < 2:
        print("Usage:")
        print("  python dd_parser_suite.py build          # build suite: equal per-scaler failures + works_all")
        print("  python dd_parser_suite.py run            # run current scalers on stored suite")
        print("  python dd_parser_suite.py fuzz [N] [SEED]# random fuzz comparison")
        sys.exit(1)
    cmd = sys.argv[1]
    if cmd == "build":
        build_suite()
    elif cmd == "run":
        run_suite()
    elif cmd == "fuzz":
        n = int(sys.argv[2]) if len(sys.argv) > 2 else 200000
        seed = int(sys.argv[3]) if len(sys.argv) > 3 else 1234
        fuzz(n, seed)
    else:
        print("Unknown command.")
        sys.exit(2)

if __name__ == "__main__":
    main()
