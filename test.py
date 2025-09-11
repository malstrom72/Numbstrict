import math
import struct

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

def scale_power2_subnormal_doubleonly(acc, factor: float, neg: bool):
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
        return True, (-0.0 if neg else 0.0)

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
        return True, (-0.0 if neg else 0.0)

    if N > max_payload:
        # Rounds up into minimal normal
        y = math.ldexp(1.0, EMIN + 1)   # 2^(emin+1)
        return True, (-y if neg else y)

    # Pack: value = N * 2^EMIN
    y = math.ldexp(N, EMIN)
    return True, (-y if neg else y)

# -------------------------
# wiring into your parser
# -------------------------
# Example DoubleDouble compatible with your code:
class DoubleDouble:
    def __init__(self, high=0.0, low=0.0):
        self.high = float(high)
        self.low  = float(low)

# Example “finish” function to replace the old final scaling:
def finish_scale(acc: DoubleDouble, factor: float, sign_negative: bool) -> float:
    # Try subnormal-specialized path (exact powers of two assumed)
    handled, y = scale_power2_subnormal_doubleonly(acc, factor, sign_negative)
    if handled:
        return y
    # Normal case: your legacy path (two multiplies + add). This matches your C++.
    # (You can also use float(acc) * factor if you prefer.)
    y = acc.high * factor + acc.low * factor
    return -y if sign_negative else y

# -------------------------
# tiny sanity check examples
# -------------------------
if __name__ == "__main__":
    # An artificial acc close to denormal boundary:
    acc = DoubleDouble(123052907161600.0, 0.1646894993248682)
    # factor with bit pattern 0x0000000020000000  (≈ 4.2439e-314):
    factor = bits_to_double(0x0000000020000000)

    y1 = acc.high * factor + acc.low * factor      # legacy
    y2_handled, y2 = scale_power2_subnormal_doubleonly(acc, factor, False)
    y3 = finish_scale(acc, factor, False)

    print("factor bits =", hex(double_bits(factor)))
    print("legacy bits =", hex(double_bits(y1)))
    print("subnorm bits=", hex(double_bits(y2 if y2_handled else float('nan'))))
    print("finish  bits=", hex(double_bits(y3)))