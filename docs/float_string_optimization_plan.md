# Float/String Optimization Plan

## Ground Rules
- [x] Always benchmark before and after every optimization attempt using `output/release/benchmarkToString` on the release build.
- [x] Always run `output/release/compareWithRyu 10000000` before landing a change to ensure bit-for-bit compatibility with the oracle.
- [x] Publish the benchmark results for each experiment using the standard format defined below and archive both the "before" and "after" numbers.
- [x] Roll back any optimization that fails correctness checks or does not deliver a meaningful speedup relative to the documented baseline.

## Benchmark Reporting Template
Use the following template when documenting a new experiment:

```
### <Optimization name>
- Commit: <SHA or branch>
- Date: <YYYY-MM-DD>
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

## Current Baseline (work @ HEAD)
The latest release benchmark on the current `work` branch produces the following results:

| Benchmark | Time (ns/value) | Reference |
| --- | --- | --- |
| doubleToString | 2,473.79 | Ryu d2s 87.68 / `std::ostringstream<double>` 1,216.11 |
| stringToDouble | 873.75 | `std::strtod` 300.66 / `std::istringstream<double>` 776.46 |
| floatToString | 1,544.03 | Ryu f2s 58.42 / `std::ostringstream<float>` 655.47 |
| stringToFloat | 694.96 | `std::strtof` 174.34 / `std::istringstream<float>` 594.48 |

## Optimization Backlog
- [ ] Batch `StandardFPEnvScope` usage so hot loops amortize floating-point environment setup.
- [ ] Extend the parser fast path to consume up to 18 significant digits in native precision before switching to `DoubleDouble` arithmetic.
- [ ] Replace per-digit `DoubleDouble / 10` divisions with cached magnitudes from `EXP10_TABLE`.
- [ ] Implement a quotient-based digit extractor in the formatter that emits digits using a single `DoubleDouble` division per step.
- [ ] Cache rounding intermediates to avoid duplicate `scaleAndRound` calls inside `realToString`.
- [ ] Replace `frexp` exponent estimation with direct IEEE exponent extraction.

## Completed Experiments

### Quotient-Based Digit Extraction (recorded 2024-02-21)
- Status: Rolled out; correctness verified with `compareWithRyu 10000000` at the time of measurement.
- Summary: Replaced the incremental digit search loop in `realToString` with a quotient-based approach that performs one `DoubleDouble` division per digit and retains the guard checks for rounding.

| Benchmark | Before (ns/value) | After (ns/value) | Δ | Notes |
| --- | --- | --- | --- | --- |
| doubleToString | 3,392 – 3,395 | 2,457 – 2,462 | ▼ ~27% | Release build, 1,000,000 mixed values |
| stringToDouble | 826 – 844 | 819 – 869 | ≈ 0% | Parser unaffected; variation within noise |
| floatToString | 1,904 – 1,926 | 1,513 – 1,609 | ▼ ~18% | Same dataset as above |
| stringToFloat | 628 – 654 | 630 – 638 | ≈ 0% | Variation within noise |

### Double Fast-Path Accumulation (recorded 2024-02-18)
- Status: Landed; correctness verified with `compareWithRyu 10000000` at the time of measurement.
- Summary: Introduced a double-based staging loop in `parseReal` that accumulates up to 18 significant digits before converting the value into a `DoubleDouble`, reducing high-precision operations for short numbers.

| Benchmark | Before (ns/value) | After (ns/value) | Δ | Notes |
| --- | --- | --- | --- | --- |
| doubleToString | 2,612 | 2,473 | ▼ ~5% | Formatter indirectly benefits from reduced parser overhead during benchmark setup |
| stringToDouble | 902 | 874 | ▼ ~3% | Primary beneficiary; fewer `DoubleDouble` operations |
| floatToString | 1,602 | 1,544 | ▼ ~4% | Secondary effects via dataset preparation |
| stringToFloat | 717 | 695 | ▼ ~3% | Direct improvement from batched digit accumulation |

## Deferred or Abandoned Ideas
- [ ] Bit-level exponent helpers in the formatter *(rolled back; no measurable performance gain, deemed pointless for now).* 
- [ ] Staged nine-digit fast-digit parser *(reverted due to lack of improvement and code complexity).* 

