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

## Optimization Backlog
- [ ] Batch `StandardFPEnvScope` usage so hot loops amortize floating-point environment setup without breaking denormal handling.
- [ ] Replace per-digit `DoubleDouble / 10` divisions with cached magnitudes sourced from `EXP10_TABLE`.
- [ ] Carry a running remainder in `realToString` to eliminate redundant subtraction when estimating digits.
- [ ] Extend the staged-digit parser to operate on multiple base-1e9 chunks without precision loss.
- [ ] Replace `frexp` exponent estimation with direct IEEE exponent extraction.
- [ ] Cache rounding intermediates so the formatter avoids duplicate `scaleAndRound` work inside the digit loop.
- [ ] Profile and tune the `compareWithRyu` harness to reduce measurement noise while keeping coverage intact.

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
