import math, struct

def double_bits(x: float) -> int:
    return struct.unpack(">Q", struct.pack(">d", x))[0]

def bits_to_double(u: int) -> float:
    return struct.unpack(">d", struct.pack(">Q", u))[0]

def round_to_even_shift(sig: int, shift: int) -> int:
    """Right-shift with round-to-nearest-even (ties to even)."""
    if shift == 0:
        return sig
    # extract dropped bits
    dropped = sig & ((1 << shift) - 1)
    keep    = sig >> shift
    half    = 1 << (shift - 1)

    if dropped > half:
        keep += 1
    elif dropped == half:
        if keep & 1:
            keep += 1
    return keep

def scale_down_by_shift(x: float, shift: int):
    u = double_bits(x)
    assert (u >> 52) & 0x7ff == 0, "x must be subnormal"
    sig = u & ((1 << 52) - 1)
    result_sig = round_to_even_shift(sig, shift)
    return bits_to_double(result_sig)

# Test: pick a random subnormal and compare
subnormal = bits_to_double(0x0008abcd12345678)  # some random payload
for shift in [1, 5, 10, 20]:
    via_ldexp = math.ldexp(subnormal, -shift)
    via_multi = subnormal * (2.0 ** -shift)
    via_shift = scale_down_by_shift(subnormal, shift)
    print(f"shift={shift}")
    print(f"  ldexp : 0x{double_bits(via_ldexp):016x}")
    print(f"  multi : 0x{double_bits(via_multi):016x}")
    print(f"  shift : 0x{double_bits(via_shift):016x}")
    print("---")