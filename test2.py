import math, struct

class DoubleDouble:
    def __init__(self, high: float = 0.0, low: float = 0.0):
        self.high = float(high)
        self.low  = float(low)
    def __float__(self):
        return self.high + self.low

# --- bit helpers ------------------------------------------------------------

def double_bits(x: float) -> int:
    return struct.unpack('>Q', struct.pack('>d', x))[0]

def bits_to_double(u: int) -> float:
    return struct.unpack('>d', struct.pack('>Q', u))[0]

# Extract exact power-of-two exponent from a double known to be a power of two.
# Returns integer k such that value == 2**k. Asserts that value is a pure power of two.
# Handles normal and subnormal powers of two.

def pow2_exponent_of_double(val: float) -> int:
    b = double_bits(val)
    exp = (b >> 52) & 0x7ff
    frac = b & ((1 << 52) - 1)
    if exp == 0:
        # subnormal: value = (frac / 2**52) * 2**(-1022)
        # for a power of two, frac must be a single 1-bit
        assert frac != 0 and (frac & (frac - 1)) == 0, f"factor not pure power-of-two (subnormal): frac=0x{frac:x}"
        top = frac.bit_length() - 1  # 0..51 (position of the single 1)
        return -1074 + top
    else:
        # normal: fraction must be zero
        assert frac == 0, f"factor not pure power-of-two (normal): frac=0x{frac:x}"
        return exp - 1023

# Round-to-nearest, ties-to-even on a real number represented as (integer part, fractional part)

def round_to_even_from_parts(int_part: int, frac_part: float) -> int:
    # int_part >= 0, 0 <= frac_part < 1
    if frac_part < 0.5:
        return int_part
    if frac_part > 0.5:
        return int_part + 1
    # exactly 0.5 -> ties to even
    return int_part if (int_part & 1) == 0 else int_part + 1

# --- subnormal-safe power-of-two scaling -----------------------------------
# We compute the subnormal significand N exactly (with integer rounding) and then
# manufacture the double from bits. This avoids adding two subnormals in hardware.
#
# value = (acc.high + acc.low) * 2**E, with factor = 2**E and value < 2**-1022
# Let T = E + 1074. Then N = round_even( (acc.high + acc.low) * 2**T ), 0 <= N <= 2**52 - 1
# and the resulting double is bits: sign|exp=0|frac=N. Sign is handled by caller.
#
# In Python we can use big ints directly; in C++03 this maps to two 32-bit limbs easily.

def scale_power2_subnormal_exact(acc: DoubleDouble, factor: float) -> float:
    # zero short-circuit
    if acc.high == 0.0 and acc.low == 0.0:
        return 0.0

    E = pow2_exponent_of_double(factor)  # exact power-of-two exponent
    if E >= -1022:
        return None  # caller should handle normal path

    # Compute T = E + 1074 (units of the subnormal ULP)
    T = E + 1074

    # Split X = H + L
    H = int(acc.high)           # exact integer (< 2**53)
    L = acc.low                 # 0 <= L < 1

    if T >= 0:
        # N = round_even( (H << T) + L * 2**T )
        A = H << T
        Bf = math.ldexp(L, T)   # <= 2**T, T <= 52 for subnormals when X in [1,2**53)
        Bi = int(Bf)            # floor
        frac = Bf - Bi          # exact in this range
        N = round_to_even_from_parts(A + Bi, frac)
    else:
        # T < 0: divide by 2**k with rounding to even: (H + L) / 2**k
        k = -T
        # integer part from H
        q = H >> k
        r = H & ((1 << k) - 1)
        # fractional from remainder and low
        Bf = (r + math.ldexp(L, k)) / (1 << k)  # in [0,1)
        # Round q by Bf (ties-to-even)
        N = round_to_even_from_parts(q, Bf)

    # clamp to [0, 2**52 - 1]
    if N <= 0:
        return 0.0
    if N > (1 << 52) - 1:
        # bumps into the first normal (which is 1 << 52 at exponent -1022)
        # Return the smallest normal (tie behavior up the caller’s exponent boundary)
        # but for subnormal routine we saturate at that boundary.
        N = (1 << 52) - 1

    # Build the double: exp=0 + fraction=N (sign applied by caller if needed)
    bits = N
    return bits_to_double(bits)

# Reference scaler variants (to compare):

def scale_float_legacy(acc: DoubleDouble, factor: float) -> float:
    return (acc.high + acc.low) * factor

def scale_float_split(acc: DoubleDouble, factor: float) -> float:
    return acc.high * factor + acc.low * factor

# Top-level: choose exact subnormal path, otherwise legacy normal path.

def scale_power2(acc: DoubleDouble, factor: float) -> float:
    E = pow2_exponent_of_double(factor)
    if E < -1022:
        y = scale_power2_subnormal_exact(acc, factor)
        if y is not None:
            return y
    # normal/zero/infinite cases -> plain multiply (safe; factor is power-of-two)
    return (acc.high + acc.low) * factor

# --- quick self-checks ------------------------------------------------------

def _hex(x: float) -> str:
    return f"0x{double_bits(x):016x}"

def scale_power2_subnormal_exact(acc: DoubleDouble, factor: float) -> float:
    """
    Exact power-of-two scaling for results that land in the subnormal/underflow region.
    Properly rounds to minimal normal when payload overflows 52 bits.
    Returns a float, or None if the result is not subnormal (caller should handle normal case).
    """
    if acc.high == 0.0 and acc.low == 0.0:
        return 0.0

    E = pow2_exponent_of_double(factor)   # factor == 2**E
    if E >= -1022:
        return None  # not a subnormal result; caller does normal path

    # Target payload: N = round_even( (high+low) * 2^(E+1074) )
    T = E + 1074
    H = int(acc.high)
    L = acc.low

    if T >= 0:
        # N = round_even( H<<T + L * 2^T )
        A  = H << T
        Bf = math.ldexp(L, T)     # exact
        Bi = int(Bf)
        frac = Bf - Bi
        N = round_to_even_from_parts(A + Bi, frac)
    else:
        # T < 0 : N = round_even( (H + L) / 2^(-T) )
        k = -T
        q = H >> k
        r = H & ((1 << k) - 1)
        Bf = (r + math.ldexp(L, k)) / (1 << k)   # exact fractional part
        N  = round_to_even_from_parts(q, Bf)

    if N <= 0:
        return 0.0

    # 2^52 is the boundary between subnormal payload (1..2^52-1) and minimal normal (2^-1022)
    if N >= (1 << 52):
        # minimal normal, exact: 2^-1022 (bits 0x0010000000000000)
        return math.ldexp(1.0, -1022)

    # Subnormal: payload N in the mantissa field with exponent==0
    return bits_to_double(N)

def demo():
    # Two concrete cases we discussed earlier
    cases = [
        (DoubleDouble(1717539029266041.0, 0.36205739230160194), bits_to_double(0x0000000020000000)),
        (DoubleDouble(123052907161600.0,    0.1646894993248682), bits_to_double(0x0000000020000000)),
    ]
    for acc, fac in cases:
        y_exact = scale_power2(acc, fac)
        y_legacy= scale_float_legacy(acc, fac)
        y_split = scale_float_split(acc, fac)
        print("acc=(%r,%r) fac=%s" % (acc.high, acc.low, _hex(fac)))
        print("  exact :", y_exact, _hex(y_exact))
        print("  legacy:", y_legacy, _hex(y_legacy))
        print("  split :", y_split,  _hex(y_split))
        print("---")

if __name__ == "__main__":
    demo()
