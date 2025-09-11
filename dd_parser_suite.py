#!/usr/bin/env python3
import math, random, struct, sys, time, json
from decimal import Decimal, getcontext

# ------------------------ config ------------------------

MIN_EXPONENT = -324
MAX_EXPONENT = 308

# Use the exact original negative-tail logic (no “boost” experiments here)
USE_ORIGINAL_TAIL = True

# Decimal precision for the “oracle” scale
DECIMAL_PREC = 42  # 21 works in practice; 42 gives headroom

SUITE_PATH = "dd_suite.json"
TARGET_SUITE_DIFF = 50
TARGET_SUITE_SAME = 50

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

# ---------------- two scaling paths ----------------

getcontext().prec = DECIMAL_PREC

def scale_float(acc: DoubleDouble, factor: float) -> float:
    # legacy: (float64 + float64) * float64, single rounding per multiply
    return float(acc) * factor

def scale_decimal(acc: DoubleDouble, factor: float) -> float:
    # “oracle”: higher-precision sum then scale then round once
    return float((Decimal(acc.high) + Decimal(acc.low)) * Decimal(factor))

# ---------------- suite builder & runner ----------------

def build_suite():
    random.seed(2025)
    diff_rows, same_rows = [], []

    def maybe_add(row, differ: bool):
        nonlocal diff_rows, same_rows
        if differ and len(diff_rows) < TARGET_SUITE_DIFF:
            diff_rows.append(row); return True
        if (not differ) and len(same_rows) < TARGET_SUITE_SAME:
            same_rows.append(row); return True
        return False

    def row_from_x(x: float):
        s = repr(x)
        sign, exponent, idx, acc, factor, _ = parse_components(s)
        if acc is None: return None
        y_f = scale_float(acc, factor)   # float scaling
        y_d = scale_decimal(acc, factor) # Decimal scaling
        differ = (double_bits(y_f) != double_bits(y_d))
        return {
            "orig_bits": f"0x{double_bits(x):016x}",
            "str": s,
            "exp10": exp10_of_str(s),
            "exponent": exponent,
            "idx": idx,
            "acc_high": acc.high,
            "acc_low": acc.low,
            "acc_high_hex": as_hex(acc.high),
            "acc_low_hex":  as_hex(acc.low),
            "factor": factor,
            "factor_hex": dbl_hex(factor),
            "float_bits": f"0x{double_bits(y_f):016x}",
            "decimal_bits": f"0x{double_bits(y_d):016x}",
            "float_17e": f"{y_f:.17e}",
            "decimal_17e": f"{y_d:.17e}",
            "oracle_17e": f"{x:.17e}",
            "differ": differ
        }

    # Strategy 1: sweep many subnormals (exponent field == 0)
    # Iterate through a wide sample but bail once we hit quotas.
    step = 97_3  # odd stride to decorrelate patterns
    for u in range(1, 1<<20, step):  # ~1M window, we sample ~10k
        if len(diff_rows) >= TARGET_SUITE_DIFF and len(same_rows) >= TARGET_SUITE_SAME:
            break
        x = bits_to_double(u)
        if not math.isfinite(x): continue
        row = row_from_x(x)
        if row is None: continue
        if maybe_add(row, row["differ"]): continue

    # Strategy 2: around the smallest *normal* and its neighborhood
    base = 0x0010000000000000  # smallest normal
    for off in range(0, 1<<18, 1019):
        if len(diff_rows) >= TARGET_SUITE_DIFF and len(same_rows) >= TARGET_SUITE_SAME:
            break
        for signbit in (0, 1<<63):
            u = base + off + signbit
            x = bits_to_double(u)
            if not math.isfinite(x): continue
            row = row_from_x(x)
            if row is None: continue
            if maybe_add(row, row["differ"]): continue

    # Strategy 3: random fallbacks (just in case we still need to fill)
    tries = 0
    while len(diff_rows) < TARGET_SUITE_DIFF or len(same_rows) < TARGET_SUITE_SAME:
        if tries > 5_000_000:
            break  # sanity
        tries += 1
        u = random.getrandbits(64)
        x = bits_to_double(u)
        if not math.isfinite(x): continue
        row = row_from_x(x)
        if row is None: continue
        if maybe_add(row, row["differ"]): continue

    suite = diff_rows + same_rows
    random.shuffle(suite)
    with open(SUITE_PATH, "w") as f:
        json.dump(suite, f, indent=2)
    print(f"Built suite: {len(suite)} rows "
          f"(DIFF={len(diff_rows)}, SAME={len(same_rows)}). Saved to {SUITE_PATH}")

def run_suite():
    with open(SUITE_PATH, "r") as f:
        suite = json.load(f)
    print(f"Loaded {len(suite)} rows from {SUITE_PATH}")
    mismatches_float_vs_decimal = 0
    mismatches_decimal_vs_oracle = 0
    mismatches_float_vs_oracle = 0

    for i, row in enumerate(suite):
        s = row["str"]
        sign, exponent, idx, acc, factor, _ = parse_components(s)
        if acc is None:
            print(f"[{i}] SKIP (non-finite or empty)")
            continue

        y_f = scale_float(acc, factor)
        y_d = scale_decimal(acc, factor)
        x = bits_to_double(int(row["orig_bits"], 16))

        if double_bits(y_f) != double_bits(y_d):
            mismatches_float_vs_decimal += 1
        if double_bits(y_d) != double_bits(x):
            mismatches_decimal_vs_oracle += 1
            print(f"[{i}] Decimal != Oracle? BUG")
            print(row); break
        if double_bits(y_f) != double_bits(x):
            mismatches_float_vs_oracle += 1

    print("Summary on suite:")
    print(f"  floatScale vs decimalScale DIFF: {mismatches_float_vs_decimal}/{len(suite)}")
    print(f"  decimalScale vs oracle (repr(x)): {mismatches_decimal_vs_oracle}/{len(suite)}")
    print(f"  floatScale vs oracle (repr(x)):   {mismatches_float_vs_oracle}/{len(suite)}")

def fuzz(n=200000, seed=1234):
    random.seed(seed)
    mismatches = 0
    highest = -999
    examples = []
    t0 = time.time()
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
        y_f = scale_float(acc, factor)
        y_d = scale_decimal(acc, factor)
        if double_bits(y_f) != double_bits(y_d):
            mismatches += 1
            ex10 = exp10_of_str(s)
            highest = max(highest, ex10)
            if len(examples) < 10:
                examples.append((f"0x{u:016x}", s, ex10,
                                 f"0x{double_bits(y_f):016x}", f"{y_f:.17e}",
                                 f"0x{double_bits(y_d):016x}", f"{y_d:.17e}"))
        count += 1
    t1 = time.time()
    print(f"[FUZZ] total: {n}, float_vs_decimal_mismatches: {mismatches}, "
          f"highest_exp10: {highest}, time_s: {round(t1-t0,2)}")
    for row in examples:
        print("bits:", row[0])
        print("str: ", row[1])
        print("exp10:", row[2])
        print("float_bits:", row[3])
        print("float_17e:", row[4])
        print("dec_bits:  ", row[5])
        print("dec_17e:  ", row[6])
        print("----")

# ------------------------- main -------------------------

def main():
    if len(sys.argv) < 2:
        print("Usage:")
        print("  python dd_parser_suite.py build          # build 100-row suite (50 DIFF / 50 SAME)")
        print("  python dd_parser_suite.py run            # run current scaler on stored suite")
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