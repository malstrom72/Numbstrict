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

## Profiling Log
- 2025-09-22: Stored Callgrind capture at `docs/profiles/callgrind.benchmarkToString.count10000.out` for drill-down analysis of the instrumented benchmark run described above.
- 2025-09-22: Captured parse-only Callgrind runs with instrumentation toggled on inside `stringToReal<double>` and `stringToReal<float>`.
  - Commands: `timeout 180 ./build.sh`, `valgrind --tool=callgrind --collect-atstart=no --toggle-collect='double Numbstrict::stringToReal<double>(char const*, char const*, char const**)' --callgrind-out-file=docs/profiles/callgrind.stringToDouble.count10000.out output/release/benchmarkToString double count=10000`, `valgrind --tool=callgrind --collect-atstart=no --toggle-collect='float Numbstrict::stringToReal<float>(char const*, char const*, char const**)' --callgrind-out-file=docs/profiles/callgrind.stringToFloat.count10000.out output/release/benchmarkToString float count=10000`.
  - Notes: Instrumentation stays disabled during string generation and formatting so only the parsing loops contribute to the profile. For doubles the run recorded 21.2M instructions with `parseReal<double>` responsible for 55% and `DoubleDouble::operator/(int)` for 28% of the total. The float-focused run recorded 12.8M instructions with `parseReal<float>` consuming 49% and the same division helper accounting for 22%, reaffirming that the `/ 10` path dominates the parser hot spots.

## Optimization Backlog
- [ ] Batch `StandardFPEnvScope` usage so hot loops amortize floating-point environment setup without breaking denormal handling.
- [ ] Replace per-digit `DoubleDouble / 10` divisions with cached magnitudes sourced from `EXP10_TABLE`.
- [ ] Carry a running remainder in `realToString` to eliminate redundant subtraction when estimating digits.
- [ ] Extend the staged-digit parser to operate on multiple base-1e9 chunks without precision loss.
- [ ] Replace `frexp` exponent estimation with direct IEEE exponent extraction.
- [ ] Cache rounding intermediates so the formatter avoids duplicate `scaleAndRound` work inside the digit loop.
- [ ] Profile and tune the `compareWithRyu` harness to reduce measurement noise while keeping coverage intact.
- [ ] Fuse multiply/add sequences or introduce dedicated helpers to reduce the number of `DoubleDouble::operator+` calls in the formatter digit loop.
- [ ] Provide a cheaper ordered comparison for `DoubleDouble` magnitude checks to shrink `operator<` cost inside `realToString`.
- [ ] Investigate restructuring `scaleAndRound` to reuse intermediate results across digits or leverage integer arithmetic to cut its 6% instruction share.
- [ ] Audit floating-point environment entry/exit paths after batching to eliminate the remaining `fegetenv`/`fesetenv` calls that still appear in the profile.

## Completed Experiments
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

## Notes
- Maintain this document alongside code changes so every optimization attempt—successful or not—retains its benchmarks, decisions, and rollback criteria.
- When new profiling data is captured, cross-link it here and update the backlog priorities accordingly.
