import math, struct, random
from decimal import Decimal, getcontext

getcontext().prec = 50  # high precision for oracle

class DoubleDouble:
    def __init__(self, high: float, low: float):
        self.high = high
        self.low = low


def scale_float(acc: DoubleDouble, factor: float) -> float:
    return float(acc.high + acc.low) * factor

def scale_float_2(acc: DoubleDouble, factor: float) -> float:
    return (acc.high * factor + acc.low * factor)


def double_bits(x: float) -> int:
    return struct.unpack('>Q', struct.pack('>d', x))[0]

def bits_to_double(u: int) -> float:
    return struct.unpack('>d', struct.pack('>Q', u))[0]

def scale_decimal(acc: DoubleDouble, factor: float) -> float:
    return float((Decimal(acc.high) + Decimal(acc.low)) * Decimal(factor))

def scale_with_grs(acc: DoubleDouble, factor: float) -> float:
    # Assume factor is power of two, possibly subnormal
    k = math.frexp(factor)[1] - 1
    ah = math.ldexp(acc.high, k)
    al = math.ldexp(acc.low,  k)
    s = ah + al
    e = (ah - s) + al  # error term from naive summation

    mant, exp = math.frexp(s)
    mant *= 2.0
    exp -= 1

    grs_bit = 1 if abs(e) > (0.5 * math.ldexp(1.0, exp - 52)) else 0
    mantissa = int(mant * (1 << 52))
    if grs_bit:
        mantissa += 1
    return math.ldexp(mantissa, exp - 52)

def is_denormal(x: float) -> bool:
    u = double_bits(x)
    exp = (u >> 52) & 0x7FF
    frac = u & ((1 << 52) - 1)
    return exp == 0 and frac != 0

# Return k where f == 2**k. Asserts f is a pure power of two (normal or subnormal).
def pow2_exponent_from_bits(f: float) -> int:
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

# ---------------- Exact subnormal scaling (GRS on payload) ----------------

def scale_subnormal_exact_from_dd(high: float, low: float, factor: float) -> float:
    """
    Given a DoubleDouble-like (high integer < 2**53, 0<=low<1) and a power-of-two 'factor'
    whose result falls into the subnormal range (E < -1022),
    produce the correctly rounded subnormal by rounding the payload (mantissa) with ties-to-even.
    """
    E = pow2_exponent_from_bits(factor)        # power of two
    assert E < -1022
    H = int(high)
    L = low
    T = E + 1074                                # scaling into payload units

    if T >= 0:
        A  = H << T                             # integer contribution from 'high'
        Bf = L * (2**T)                         # fractional contrib from 'low'
        Bi = int(Bf)
        frac = Bf - Bi
        N = A + Bi
    else:
        k   = -T
        q   = H >> k
        r   = H & ((1 << k) - 1)
        Bf  = (r + L * (2**k)) / (2**k)         # in [0,1)
        frac = Bf                                # integer part is zero here
        N    = q

    # Round to nearest, ties to even (GRS logic on payload)
    if frac > 0.5:
        N += 1
    elif frac == 0.5 and (N & 1) == 1:
        N += 1

    if N <= 0:
        return 0.0
    if N > (1 << 52) - 1:
        # rounds up into the smallest normal
        return bits_to_double(0x0010000000000000)

    # Pack subnormal: sign=0, exp=0, mantissa=N
    return bits_to_double(N)

def demo_single():
    acc = DoubleDouble(106310997483919.0, 0.5396973792564765)
    factor = bits_to_double(0x0000000000000020)
    y_grs = scale_with_grs(acc, factor)
    y_dec = scale_decimal(acc, factor)
    print(f"acc=({acc.high},{acc.low}), factor=0x{double_bits(factor):016x}")
    print(f"  grs: {y_grs:.17e}, bits=0x{double_bits(y_grs):016x}")
    print(f"  dec: {y_dec:.17e}, bits=0x{double_bits(y_dec):016x}")

def demo_single_2():
    high = 106310997483919.0
    low  = 0.5396973792564765
    factor = bits_to_double(0x0000000000000020)     # 2**-1069 (subnormal power-of-two)

    y_exact   = scale_subnormal_exact_from_dd(high, low, factor)

    oracle_bits = 0x800c160ea7b431f1

    def hx(x): return f"0x{double_bits(x):016x}"

    print("Results (magnitudes):")
    print(f"  exact_subnormal : {y_exact:.17e}  bits={hx(y_exact)} denorm={is_denormal(y_exact)}")

    print("\nResults (with negative sign):")
    print(f"  exact_subnormal : bits={hx(-y_exact)}")
    print(f"\nOracle bits      : 0x{oracle_bits:016x}")


def fuzz(n=100000):
    random.seed(1234)
    mismatches = 0
    for _ in range(n):
        high = random.randint(0, 1 << 53)
        low = random.random()
        acc = DoubleDouble(high, low)
        exp = random.randint(-324, -300)
        factor = bits_to_double(0x0000000000000020) # math.ldexp(1.0, exp)
        y_grs = scale_subnormal_exact_from_dd(acc.high, acc.low, factor)
        y_dec = scale_decimal(acc, factor)
        if double_bits(y_grs) != double_bits(y_dec):
            mismatches += 1
    print(f"Fuzzed {n} cases, mismatches={mismatches}")

if __name__ == "__main__":
    demo_single()
    demo_single_2()
    fuzz(1000000)
