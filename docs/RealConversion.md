# Real number conversion

How `stringToDouble`, `stringToFloat`, `doubleToString` and `floatToString` work, and why they are correct. The code
is the "Real number conversions" section of `src/Numbstrict.cpp`.

## The requirement

Parsing must return the binary number nearest to the decimal text (ties to even). Formatting must return the shortest
decimal text that parses back to the value, and the closest such text when several of that length do. Both are
questions about distances between decimals and binary numbers, and the decisive facts are how close a decimal with
a given number of significant digits can come to a binary *rounding midpoint* (the point halfway between two adjacent
doubles) or to a binary *value* without being exactly on it.

## Three facts from number theory

Write a decimal with n significant digits as D * 10^E with D < 10^n, and a binary64 midpoint as (2m + 1) * 2^g.
Their difference is 2^E * (D * 5^E - (2m + 1) * 2^(g - E)) for E >= 0, and a similar expression over 5^-E for E < 0:
a nonzero integer r times a known scale. How small |r| can be for D below 10^n is a question about how close the
multiples of 5^E (or of 2^j) come to a target modulo a power of the other prime, which the convergents of the
continued fraction of 5^E / 2^j answer exactly, and a Euclid-style search gives the exact minimum over any interval
of D. Running that for every decimal exponent and every binade gives:

1. A decimal with at most 20 significant digits is never within 2^-125.39 (relative) of a binary64 midpoint unless
   it lies exactly on it. The closest case is `82628059879762389505e54`; for 19 digits it is 2^-125.07 at
   `7120190517612959703e120`. Against binary32 midpoints the bound is 2^-94.
2. A decimal with at most 18 significant digits is never within 2^-124 (relative) of a double unless it equals it
   (binary32: nowhere near 2^-80).
3. No binary64 rounding midpoint has more than 767 significant decimal digits, so for an input longer than 800
   digits the later digits only matter by being nonzero.

Decimals with more digits can come arbitrarily close, so beyond 20 digits an exact comparison is needed; it is the
rare path described below.

## The one approximation

A table holds 5^q for q in -343..343 as a 128-bit significand P and a binary exponent e with 5^q = (P + f) * 2^e,
0 <= f < 1 (reciprocals for negative q). P is truncated, never rounded, so the true value is never below it. For a
significand w below 10^20 (67 bits) the product x = w * P is formed exactly, and the exact scaled value lies in
[x, x + w): an interval of relative width below 2^-127, which by fact 1 is either entirely on one side of every
midpoint or holds exactly one of them, in which case the input is an exact tie. The same interval, by fact 2, gives
exact integer parts and exact "is the remainder below, on or above one half" answers for the formatter's
v * 10^j. Entries up to 5^55 are exact, so the inputs where ties can occur are computed exactly.

Everything is integer arithmetic on `Words<N>`, an unsigned integer of N 32-bit words. Values are taken apart and
assembled on their bits. Nothing depends on the floating-point environment: rounding mode, flush-to-zero,
denormals-are-zero and x87 precision control do not change a single result.

## Parsing

The first 20 significant digits become an exact integer; `convertDecimal` forms the product, finds the bit position
of the result's least significant bit (from the product's bit length, or the subnormal floor), and decides from where
the interval [x, x + w) lies relative to the midpoint: below, above, or on it (round to even). The mantissa and
exponent field are then assembled additively, so a mantissa carry moves into the exponent and an overflowing exponent
becomes infinity.

Inputs with more than 20 digits lie strictly between the 20-digit truncation w and w + 1. Both are converted; if they
agree, that is the answer (rounding is monotonic). Otherwise, about once per thousand such inputs, the full digit
string (at most 800 digits plus a nonzero-tail flag, by fact 3) is compared exactly against the midpoint between the
two results using 128-word integers. This keeps the conversion exact for any length.

## Formatting

For a value v with leading decimal exponent k, the n-digit candidates are F = floor(v * 10^(n-1-k)) and F + 1; the
smallest n at which one of them converts back to v gives the shortest text, and when both do, the closer one wins
(on an exact half the one with an even last digit, as NuXJS and V8 print it; the largest finite value never takes
the upper candidate, so that its text stays below the overflow threshold for parsers that treat anything above it as
overflow). "Some n-digit decimal converts back" is monotone in n, so n is found by binary search, each probe costing
one scaled floor and at most two conversions.
A carry (F + 1 = 10^n) can only survive the search at n = 1 and is normalized afterwards.

## Verification

Exhaustively: every finite float through `floatToString` and back; every float rounding midpoint written as a
decimal; every float formatted and checked against exact big-integer arithmetic. By construction: the inputs at the
exact minima of facts 1 and 2, 183,577 near-ties of 8-20 digits built from the residue classes above, the exact
767-digit expansions of random midpoints and their neighbors, and the 25-digit exact ties in `unitTest`. At random:
tens of millions of decimal strings of 1-40 digits over the whole exponent range and random doubles, each checked
against exact arithmetic, and the formatter against the exact shortest/closest criteria. Also under a hostile
floating-point environment (round toward zero, flush to zero, denormals are zero).
