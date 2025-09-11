import math, struct
from decimal import Decimal, getcontext, ROUND_HALF_EVEN

getcontext().rounding = ROUND_HALF_EVEN

def double_bits(x: float) -> int:
    return struct.unpack(">Q", struct.pack(">d", x))[0]

def scale_decimal_denorm_target(acc, factor: float) -> float:
    S = Decimal(acc.high) + Decimal(acc.low)

    # Decode factor as exact power of two
    bits = double_bits(factor)
    exp = (bits >> 52) & 0x7FF
    frac = bits & ((1 << 52) - 1)
    if exp == 0:
        k = -1074 + (frac.bit_length() - 1)
    else:
        k = exp - 1023
        assert frac == 0, "factor must be power-of-two"

    if k >= -1022:
        return float(S * Decimal(factor))

    t = -1022 - 52  # fixed pre-shift
    S_scaled = S * (Decimal(2) ** t)

    x = float(S_scaled)

    # --- DEBUG: inspect x bits ---
    xb = double_bits(x)
    mantissa = xb & ((1 << 52) - 1)
    exp_field = (xb >> 52) & 0x7FF
    print(f"[DEBUG] x={x:.17e} bits=0x{xb:016x} exp_field={exp_field} mantissa=0x{mantissa:013x}")

    return math.ldexp(x, k - t)

import random

class DoubleDouble:
    def __init__(self, high, low):
        self.high = high
        self.low = low

random.seed(1234)
for i in range(10):
    # pick random full-precision normal numbers
    high = random.randrange(1 << 52, 1 << 53)
    low = random.random()
    acc = DoubleDouble(high, low)
    fac = struct.unpack(">d", struct.pack(">Q", 0x0000000020000000))[0]  # example denormal factor
    _ = scale_decimal_denorm_target(acc, fac)
