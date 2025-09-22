# Float/String Optimization Plan

## Ground Rules
- [x] Always benchmark before and after every optimization attempt using `output/release/benchmarkToString` on the release build.
- [x] Always run `output/release/compareWithRyu 10000000` before landing a change to ensure bit-for-bit compatibility with the oracle.
- [x] Publish the benchmark results for each experiment using the standard format defined below and archive both the "before" and "after" numbers.
- [x] Roll back any optimization that fails correctness checks or does not deliver a meaningful speedup relative to the documented baseline.
- [x] Record benchmark commands, dataset notes, and any anomalies in the plan alongside the numerical results.

## Benchmark Reporting Template
Use the following template when documenting a new experiment:

```
### <Optimization name>
- Commit: <SHA or branch>
- Recorded: <YYYY-MM-DD>
- Dataset: 1,000,000 mixed values (release build)
- Commands:
  - timeout 180 ./build.sh
  - output/release/benchmarkToString
  - output/release/compareWithRyu 10000000

| Benchmark | Before (ns/value) | After (ns/value) | Δ | Notes |
| --- | --- | --- | --- | --- |
| doubleToString |  |  |  |  |
| stringToDouble |  |  |  |  |
| floatToString |  |  |  |  |
| stringToFloat |  |  |  |  |
```

Archive raw benchmark logs alongside the plan when practical so the numbers can be audited later.

## Current Baseline (work @ HEAD — recorded 2025-09-21)
The latest release benchmark on the current `work` branch produces the following results:

| Benchmark | Time (ns/value) | Reference |
| --- | --- | --- |
| doubleToString | 2,473.79 | Ryu d2s 87.68 / `std::ostringstream<double>` 1,216.11 |
| stringToDouble | 873.75 | `std::strtod` 300.66 / `std::istringstream<double>` 776.46 |
| floatToString | 1,544.03 | Ryu f2s 58.42 / `std::ostringstream<float>` 655.47 |
| stringToFloat | 694.96 | `std::strtof` 174.34 / `std::istringstream<float>` 594.48 |

## Benchmark Log
### 2025-09-21 – Release baseline refresh
- Commands: `timeout 180 ./build.sh`, `output/release/benchmarkToString`, `output/release/compareWithRyu 10000000`.
- Outcome: Baseline tables above; fuzz test passes with no mismatches.

### 2025-09-22 – Callgrind instrumentation run (count = 10,000)
- Commands: `timeout 180 ./build.sh`, `valgrind --tool=callgrind --callgrind-out-file=docs/profiles/callgrind.benchmarkToString.count10000.out output/release/benchmarkToString count=10000`.
- Dataset: 10,000-value sample emitted by `benchmarkToString` under instrumentation.
- Benchmark timings (instrumented):

| Benchmark | Time (ms) | Time (ns/value) |
| --- | --- | --- |
| Numbstrict::doubleToString | 652.326 | 65,232.6 |
| Ryu d2s | 34.146 | 3,414.6 |
| std::ostringstream<double> | 487.088 | 48,708.8 |
| Numbstrict::stringToDouble | 122.295 | 12,229.5 |
| std::strtod | 107.357 | 10,735.7 |
| std::istringstream<double> | 328.276 | 32,827.6 |
| Numbstrict::floatToString | 373.131 | 37,313.1 |
| Ryu f2s | 20.983 | 2,098.3 |
| std::ostringstream<float> | 282.037 | 28,203.7 |
| Numbstrict::stringToFloat | 80.019 | 8,001.9 |
| std::strtof | 66.873 | 6,687.3 |
| std::istringstream<float> | 235.012 | 23,501.2 |

- Instruction hotspots:
  - `Numbstrict::DoubleDouble::operator+` accounts for 21.6% of total instructions, making it the single largest internal hotspot.
  - `Numbstrict::realToString<double>` and `Numbstrict::realToString<float>` together consume ~51% of all instructions, with digit emission loops dominating their cost.
  - `Numbstrict::DoubleDouble::operator/(int)` contributes ~5% of instructions, reaffirming the need to replace repeated `/ 10` divisions.
  - `scaleAndRound<double>` and `scaleAndRound<float>` jointly add ~5.9% of instructions, highlighting redundant scaling and rounding work per digit.
  - Floating-point environment helpers such as `fegetenv` still show up, indicating there is remaining overhead to trim even after batching scopes.

### 2025-09-22 – String-to-real subset benchmark (mode = `stringToReal`)
- Commands: `timeout 180 ./build.sh`, `output/release/benchmarkToString mode=stringToReal`.
- Dataset: 1,000,000 mixed values focused on parsing.
- Results:

| Benchmark | Time (ns/value) | Reference |
| --- | --- | --- |
| Numbstrict::stringToDouble | 592.47 | `std::strtod` 209.73 / `std::istringstream<double>` 508.36 |
| Numbstrict::stringToFloat | 454.81 | `std::strtof` 108.69 / `std::istringstream<float>` 343.60 |

- Notes: The parser remains ~2.8× slower than the C library baselines despite recent staging improvements, underscoring the need to attack `DoubleDouble` math inside `parseReal`.

## Profiling Log
- 2025-09-22: Stored Callgrind capture at `docs/profiles/callgrind.benchmarkToString.count10000.out` for drill-down analysis of the instrumented benchmark run described above.
- 2025-09-22: Captured parse-only Callgrind runs with instrumentation toggled on inside `stringToReal<double>` and `stringToReal<float>`.
  - Commands: `timeout 180 ./build.sh`, `valgrind --tool=callgrind --collect-atstart=no --toggle-collect='double Numbstrict::stringToReal<double>(char const*, char const*, char const**)' --callgrind-out-file=docs/profiles/callgrind.stringToDouble.count10000.out output/release/benchmarkToString double count=10000`, `valgrind --tool=callgrind --collect-atstart=no --toggle-collect='float Numbstrict::stringToReal<float>(char const*, char const*, char const**)' --callgrind-out-file=docs/profiles/callgrind.stringToFloat.count10000.out output/release/benchmarkToString float count=10000`.
  - Notes: Instrumentation stays disabled during string generation and formatting so only the parsing loops contribute to the profile. For doubles the run recorded 21.2M instructions with `parseReal<double>` responsible for 55% and `DoubleDouble::operator/(int)` for 28% of the total. The float-focused run recorded 12.8M instructions with `parseReal<float>` consuming 49% and the same division helper accounting for 22%, reaffirming that the `/ 10` path dominates the parser hot spots.
- 2025-09-22: Profiled the benchmark with `mode=stringToReal` to capture combined parsing hotspots under instrumentation.
  - Command: `valgrind --tool=callgrind --callgrind-out-file=docs/profiles/callgrind.stringToReal.count10000.out output/release/benchmarkToString count=10000 mode=stringToReal`.
  - Highlights: 304M instructions recorded; `DoubleDouble::operator+` (19.3%), `DoubleDouble::operator/(int)` (5.9%), `parseReal<double>` (3.8%), and `scaleAndRound<double>` (3.3%) dominate internal cost while libc parsing helpers remain a secondary component. The profile confirms that parser-side `DoubleDouble` arithmetic is still the leading self-owned hotspot when formatting is disabled.

## Callgrind-Derived Priorities (2025-09-22)
- [ ] **Collapse `DoubleDouble` additions inside the formatter.** The 10,000-value benchmark capture spends over 61.8M instructions across 3,092,040 calls to `DoubleDouble::operator+`, making it the single largest self-owned cost out of 587M total instructions. Focus on emitting digits with specialized fused operations (e.g., combining multiply/add into a bespoke accumulator) to retire many of these general additions.【F:docs/profiles/callgrind.benchmarkToString.count10000.out†L18-L18】【F:docs/profiles/callgrind.benchmarkToString.count10000.out†L2039-L2047】
	- **Design:** Audit `realToString` digit emission to catalog every `operator+` call, then design a fused helper (e.g., `accumulateDigit(acc, magnitude, digit)`) that performs the multiply/add directly on the two-limb representation without constructing temporaries. Prototype against existing `DoubleDouble` invariants and document algebraic assumptions.
	- **Implementation:** Introduce the fused helper alongside existing `DoubleDouble` utilities, refactor the formatter loop to call it, and ensure the helper still cooperates with guard paths (digit decrement/increment). Keep the legacy code behind an `#if` or staging flag during development for quick rollback.
	- **Verification:** Run unit tests plus `output/release/compareWithRyu 10000000` to confirm bit-for-bit compatibility, then record before/after benchmarks with the standard template. Capture a fresh Callgrind slice to prove the addition hotspot shrinks.

- [ ] **Avoid redundant `DoubleDouble` comparisons while picking digits.** `DoubleDouble::operator<` alone accounts for roughly 29.9M instructions in the same capture, indicating that each digit still pays a non-trivial comparison cost. Carrying a running remainder or deriving the ordering from already-computed quotients could eliminate many of these comparator calls.【F:docs/profiles/callgrind.benchmarkToString.count10000.out†L2039-L2047】【F:docs/profiles/callgrind.benchmarkToString.count10000.out†L2089-L2096】
	- **Design:** Derive a per-digit remainder (`remaining = normalized - accumulator`) that gets updated once per emitted digit and reuse it to decide the next digit without calling `operator<`. Validate that the remainder fits within the `DoubleDouble` range and that rounding guards can reference it safely.
	- **Implementation:** Extend the formatter state with a cached remainder, refactor comparisons to use sign checks or integer digit bounds, and only fall back to `operator<` for exceptional branches (e.g., denormals). Maintain parity between float and double paths.
	- **Verification:** Exhaustively fuzz via `compareWithRyu 10000000`, add targeted unit cases around boundary digits (0↔1, 8↔9, tie-to-even), and benchmark before/after to ensure the reduced comparator usage translates to a measurable improvement.

- [ ] **Reduce `/10` divisions in the parser.** The double-only Callgrind run shows 163,739 invocations of `DoubleDouble::operator/(int)` costing 6.39M instructions, while the combined benchmark still pays for the same helper before every digit. Precomputing chunk magnitudes or keeping reciprocal ladders would directly attack this hotspot.【F:docs/profiles/callgrind.stringToDouble.count10000.out†L23-L79】【F:docs/profiles/callgrind.benchmarkToString.count10000.out†L1973-L1976】
	- **Design:** Map the parser’s digit-consumption flow and identify where each `/10` occurs. Build a cache strategy (e.g., array of `DoubleDouble` powers of ten or reciprocal ladder) keyed by the current exponent so the loop can reuse precomputed magnitudes.
	- **Implementation:** Generate the cache at startup or lazily on first use, thread it through `parseReal`, and replace direct `/10` calls with index lookups. Preserve precision by validating cached values against the existing division results.
	- **Verification:** Run the standard benchmark/fuzz regimen and add micro-benchmarks focusing on long mantissas to ensure no precision drift. Capture Callgrind before/after to confirm the divide hotspot shrinks.

- [ ] **Trim `scaleAndRound` scaffolding and FP environment churn.** Each formatter pass still enters `scaleAndRound`, triggering `fesetenv` and related math-library work for every digit; the double path alone spends ~300K instructions in the helper plus 450K in `fesetenv`. Investigate caching scaled values or restructuring the rounding guard so the environment only flips once per conversion.【F:docs/profiles/callgrind.benchmarkToString.count10000.out†L1977-L1987】【F:docs/profiles/callgrind.benchmarkToString.count10000.out†L2256-L2263】
	- **Design:** Profile `scaleAndRound` to decompose its work (scaling, rounding-mode toggles, comparisons). Plan to hoist environment setup into a `FloatStringBatchGuard`, cache the scaled mantissa, and reuse it for the `digit` and `digit+1` checks.
	- **Implementation:** Refactor the formatter to compute `scaleAndRound` once per digit, store the result, and evaluate ties without re-entering the helper. Ensure environment guards wrap the entire conversion rather than each digit, updating `StandardFPEnvScope` if necessary.
	- **Verification:** Run regression tests plus the mandated fuzz check to ensure rounding matches the oracle, then benchmark and record the results. Use Callgrind or perf to confirm `fesetenv`/`scaleAndRound` instruction counts drop.

- [ ] **Further amortize parsing setup.** Even with the fast-digit stage, `parseReal<double>` still consumes over 20.7M instructions per 10,000 samples and calls `StandardFPEnvScope` 10,000 times. Exploring larger staged batches, cheaper whitespace skipping, or hoisting the environment guard remains a leverage point.【F:docs/profiles/callgrind.stringToDouble.count10000.out†L23-L79】
	- **Design:** Evaluate the current staging threshold (18 digits) and model extending it to multiple base-1e9 blocks while maintaining exactness. Plan to move `StandardFPEnvScope` outside the tight loop via a batch API so repeated calls reuse the same environment setup.
	- **Implementation:** Prototype a multi-block accumulator that keeps a high-precision integer plus exponent and only converts to `DoubleDouble` when necessary. Modify public APIs or introduce helper guards to scope the FP environment once per batch of conversions.
	- **Verification:** Stress-test with extreme exponents and long mantissas under `compareWithRyu 10000000`, document benchmark deltas, and capture targeted Callgrind runs to ensure overall parser instruction counts fall.

## Optimization Backlog
 - [x] Batch `StandardFPEnvScope` usage so hot loops amortize floating-point environment setup without breaking denormal handling (10,000 parser calls still instantiate the scope during the Callgrind runs).【F:docs/profiles/callgrind.stringToDouble.count10000.out†L23-L45】【F:docs/profiles/callgrind.stringToFloat.count10000.out†L48-L71】
- [ ] Replace per-digit `DoubleDouble / 10` divisions with cached magnitudes sourced from `EXP10_TABLE` (163,739 divides per 10,000 samples consume 6.39M instructions in the double parser alone).【F:docs/profiles/callgrind.stringToDouble.count10000.out†L23-L79】
- [ ] Carry a running remainder in `realToString` to eliminate redundant subtraction when estimating digits.
- [ ] Extend the staged-digit parser to operate on multiple base-1e9 chunks without precision loss.
- [ ] Replace `frexp` exponent estimation with direct IEEE exponent extraction.
- [ ] Cache rounding intermediates so the formatter avoids duplicate `scaleAndRound` work inside the digit loop.
- [ ] Profile and tune the `compareWithRyu` harness to reduce measurement noise while keeping coverage intact.
- [x] Fuse multiply/add sequences or introduce dedicated helpers to reduce the number of `DoubleDouble::operator+` calls in the formatter digit loop.
- [ ] Provide a cheaper ordered comparison for `DoubleDouble` magnitude checks to shrink `operator<` cost inside `realToString`.
- [ ] Investigate restructuring `scaleAndRound` to reuse intermediate results across digits or leverage integer arithmetic to cut its 6% instruction share.
- [ ] Audit floating-point environment entry/exit paths after batching to eliminate the remaining `fegetenv`/`fesetenv` calls that still appear in the profile.
- [ ] Explore alternatives to `DoubleDouble::operator+` inside the parser (e.g., fused updates or wider staging buffers) to reduce its 19% share in the string-to-real profile.
- [ ] Reduce reliance on `DoubleDouble::operator/(int)` during parsing by caching or precomputing chunk magnitudes for the common `/10` cases observed in the new profile.
- [ ] Investigate lighter-weight magnitude comparisons or cached thresholds to cut `DoubleDouble::operator<` overhead in parse loops.

## Completed Experiments
### Float Environment Batch Guard (recorded 2025-09-22)
- Status: Landed; correctness verified with `compareWithRyu 10000000`.
- Summary: Introduced a thread-local floating-point environment state and the public `FloatStringBatchGuard`, then wrapped the benchmark harness so runs normalize the environment once per batch instead of per conversion.
- Benchmarks (release build, 1,000,000 values):

| Benchmark | Before (ns/value) | After (ns/value) | Δ | Notes |
| --- | --- | --- | --- | --- |
| doubleToString | 3,670.98 | 3,063.41 | ▼ ~17% | Guard avoids redundant FP env setup |
| stringToDouble | 891.18 | 490.23 | ▼ ~45% | Parser skips per-call env transitions |
| floatToString | 1,972.30 | 1,570.94 | ▼ ~20% | Same dataset as above |
| stringToFloat | 649.98 | 278.19 | ▼ ~57% | Batch guard removes repeated setup |

- Commands: `timeout 180 ./build.sh`, `output/release/benchmarkToString`, `output/release/compareWithRyu 10000000`.

### Quotient-Based Digit Extraction (recorded 2025-09-21)
- Status: Landed; correctness verified with `compareWithRyu 10000000`.
- Summary: Replaced the incremental digit search loop in `realToString` with a quotient-based approach that performs one `DoubleDouble` division per digit and retains the guard checks for rounding.
- Benchmarks (release build, 1,000,000 values):

| Benchmark | Before (ns/value) | After (ns/value) | Δ | Notes |
| --- | --- | --- | --- | --- |
| doubleToString | 3,392 – 3,395 | 2,457 – 2,462 | ▼ ~27% | Reduced per-digit additions |
| stringToDouble | 826 – 844 | 819 – 869 | ≈ 0% | Parser unaffected; variation within noise |
| floatToString | 1,904 – 1,926 | 1,513 – 1,609 | ▼ ~18% | Same dataset as above |
| stringToFloat | 628 – 654 | 630 – 638 | ≈ 0% | Variation within noise |

### Double Fast-Path Accumulation (recorded 2025-09-21)
- Status: Landed; correctness verified with `compareWithRyu 10000000`.
- Summary: Introduced a double-based staging loop in `parseReal` that accumulates up to 18 significant digits before converting the value into a `DoubleDouble`, reducing high-precision operations for short numbers.
- Benchmarks (release build, 1,000,000 values):

| Benchmark | Before (ns/value) | After (ns/value) | Δ | Notes |
| --- | --- | --- | --- | --- |
| doubleToString | 2,612 | 2,473 | ▼ ~5% | Formatter benefits from quicker dataset preparation |
| stringToDouble | 902 | 874 | ▼ ~3% | Primary beneficiary; fewer `DoubleDouble` operations |
| floatToString | 1,602 | 1,544 | ▼ ~4% | Secondary effects via dataset preparation |
| stringToFloat | 717 | 695 | ▼ ~3% | Direct improvement from batched digit accumulation |

- Commands: `timeout 180 ./build.sh`, `output/release/benchmarkToString`, `output/release/compareWithRyu 10000000`.

### Formatter next-sum reuse (recorded 2025-09-22)
- Status: Landed; correctness verified with `output/release/compareWithRyu 10000000`.
- Summary: Reused the `accumulator + magnitude` sum produced during digit selection so rounding checks share that `DoubleDouble`
addition instead of recomputing it for the `digit+1` candidate.
- Benchmarks (release build, 1,000,000 values):

| Benchmark | Before (ns/value) | After (ns/value) | Δ | Notes |
| --- | --- | --- | --- | --- |
| doubleToString | 3,167.46 | 2,996.63 | ▼ ~5% | Removes one `DoubleDouble::operator+` per digit |
| stringToDouble | 549.11 | 541.55 | ▼ ~1% | Parser unchanged; variation within noise |
| floatToString | 1,693.55 | 1,548.36 | ▼ ~9% | Same dataset as above |
| stringToFloat | 302.27 | 291.66 | ▼ ~4% | Parser unaffected; reflects noise |

- Commands: `timeout 180 ./build.sh`, `output/release/benchmarkToString`, `output/release/compareWithRyu 10000000`.

## Rolled Back or Abandoned Experiments
### Lazy `scaleAndRound` Reuse (recorded 2025-09-21)
- Summary: Cached the candidate `scaleAndRound` result and reused it across rounding checks to cut duplicate work.
- Outcome: `output/release/compareWithRyu 10000000` failed on input `0x3eb0c6f7a0b5ed8c`, producing the next-lower ULP. Change reverted immediately.

### Formatter Guard Loop Removal (recorded 2025-09-21)
- Summary: Attempted to drop the guard-adjustment loops after quotient digit estimation.
- Outcome: Introduced rounding drift near digit transitions; reverted to preserve correctness guarantees.

### Parser Chunk Magnitude Pre-Scaling (recorded 2025-09-21)
- Summary: Tried to reuse a pre-divided magnitude to avoid extra multiplications inside the chunking loop.
- Outcome: Required multiple `DoubleDouble` divides per chunk and provided no measurable speedup; reverted.

### Nine-Digit FAST_DIGIT Stage (recorded 2025-09-21)
- Summary: Replaced the steady-state parser loop with a persistent nine-digit fast path.
- Outcome: Violated `DoubleDouble` invariants and failed to outperform the chunked approach; removed in favor of the current staging loop.

### Bit-Level Exponent Helper (recorded 2025-09-21)
- Summary: Added helpers that bypassed `frexp` by manipulating IEEE exponent bits directly.
- Outcome: Produced no measurable formatter speedup; marked pointless and rolled back.

### Double Staging Accumulator (attempted 2025-09-22)
- Summary: Batched up to 18 significant digits into a `double` before updating the `DoubleDouble` accumulator to cut per-digit `multiplyAndAdd` calls.
- Outcome: `output/release/compareWithRyu 10000000` failed on fragile input `0x3eb0c6f7a0b5ed8c`, returning the next-lower ULP. Reverted immediately despite observing benchmark shifts (before: 3203.12 / 783.13 / 1723.49 / 575.14 ns/value; after: 3116.94 / 849.31 / 1743.29 / 559.14 ns/value for doubleToString/stringToDouble/floatToString/stringToFloat).

## Notes
- Maintain this document alongside code changes so every optimization attempt—successful or not—retains its benchmarks, decisions, and rollback criteria.
- When new profiling data is captured, cross-link it here and update the backlog priorities accordingly.
