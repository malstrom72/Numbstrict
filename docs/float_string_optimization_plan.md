# Float/String Conversion Optimization Plan

## Objectives
- [ ] Close the performance gap versus state-of-the-art float/string conversion routines while preserving exact IEEE-754 semantics.
- [ ] Maintain full compliance with existing tests and external API contracts.
- [ ] Produce measurable improvements on the repository's benchmarks and representative real-world workloads.

## Ground Rules
- [ ] Always run the release benchmark suite both before and after every optimization attempt (currently `timeout 180 ./build.sh` followed by `output/release/benchmarkToString`).
- [ ] Publish benchmark results for each optimization using the standardized format documented below so that performance history stays auditable.
- [ ] Always execute the 10,000,000-iteration `compareWithRyu` fuzz regression (`output/release/compareWithRyu 10000000`) when validating changes, and record any discrepancies.

## Measurement Baseline
- [ ] Reproduce the current performance numbers by running the benchmark suite referenced by `./build.sh` and any additional float/string microbenchmarks.
- [ ] Capture CPU architecture, compiler flags, and input corpus characteristics for reproducibility.
- [ ] Record profiles (e.g., `perf`, VTune, or `Instruments`) to pinpoint hot spots before making changes.

## Workstream A: Environment Setup Optimizations
- [ ] **Reduce `StandardFPEnvScope` cost.**
   - [ ] Audit all call sites in the parser and formatter to determine whether redundant floating-point environment resets occur.
   - [ ] Experiment with thread-local fast paths that skip environment changes when the default rounding mode is already active.
   - [ ] Validate that any skip logic maintains determinism in multi-threaded scenarios.
- [ ] **Batch environment transitions.**
   - [ ] Investigate hoisting the environment scope out of tight conversion loops so it is entered once per batch instead of per value.
   - [ ] Update callers that perform bulk conversions to amortize the setup cost.

## Workstream B: Parsing Fast Paths
- [ ] **Stage digits in integer form.**
   - [ ] Implement an initial loop that consumes up to 18 significant digits into a 64-bit accumulator before converting to `DoubleDouble`.
   - [ ] Benchmark the cutoff point (e.g., 18 vs. 19 digits) to balance precision and overhead.
- [ ] **Table-driven magnitude handling.**
   - [ ] Precompute powers of ten for the most common exponent ranges and replace repetitive `DoubleDouble / 10` operations with lookups.
   - [ ] Ensure the table covers denormals and extreme exponents; fall back to the existing path when out of range.
- [ ] **Profile-guided tuning.**
   - [ ] Use microbenchmarks with typical JSON/CSV numeric strings to measure the impact of the fast path before and after implementation.

## Workstream C: Formatting Improvements
- [ ] **Direct digit extraction.**
   - [ ] Replace the inner digit-selection loop with a quotient-based approach that derives each digit via a single `DoubleDouble` division.
   - [ ] Verify rounding correctness across boundaries (0–9) and for tie cases.
- [ ] **Reuse `scaleAndRound` results.**
   - [ ] Cache intermediate scaling values and reuse them across digit iterations to avoid duplicate computations.
   - [ ] Confirm that memoization does not introduce stale state between iterations.
- [ ] **Efficient exponent estimation.**
   - [ ] Prototype an `ilogb`/bit-inspection path to replace repeated `frexp` calls.
   - [ ] Validate the approach across all supported platforms and compilers.

## Workstream D: Benchmarking & Validation
- [ ] Extend existing benchmarks to cover mixed workloads (short decimals, long decimals, edge cases like denormals, NaNs, infinities).
- [ ] Compare against reference implementations (e.g., Ryu, double-conversion) using identical inputs.
- [ ] Track performance regressions continuously; add automated alerts if throughput drops below baseline.

## Benchmark Reporting Format
- [ ] Log the machine name, compiler, and exact commands used for every measurement run.
- [ ] Record results in the standard table below so comparisons stay consistent across optimizations.

### Reporting Template
| Benchmark | Before (ns/value) | After (ns/value) | Δ ns/value | Δ % | Notes |
| --- | --- | --- | --- | --- | --- |
| doubleToString |  |  |  |  | e.g., release build, 1e6 mixed values |
| stringToDouble |  |  |  |  |  |
| floatToString |  |  |  |  |  |
| stringToFloat |  |  |  |  |  |

## Benchmark History

### 2025-09-20 – Quotient-based digit extraction in `realToString`
| Benchmark | Before (ns/value) | After (ns/value) | Δ ns/value | Δ % | Notes |
| --- | --- | --- | --- | --- | --- |
| doubleToString | 2400.88 | 1735.70 | -665.18 | -27.7% | Release build, 1,000,000-value corpus |
| stringToDouble | 588.93 | 585.10 | -3.83 | -0.7% | Same corpus, parsing pass |
| floatToString | 1364.93 | 1054.09 | -310.84 | -22.8% | Release build, 1,000,000-value corpus |
| stringToFloat | 435.23 | 459.51 | +24.28 | +5.6% | Regression within noise envelope |

### 2025-09-20 – Double fast-path digit accumulation in `parseReal`
| Benchmark | Before (ns/value) | After (ns/value) | Δ ns/value | Δ % | Notes |
| --- | --- | --- | --- | --- | --- |
| doubleToString | 1735.70 | 1737.73 | +2.03 | +0.1% | Formatter unchanged within noise |
| stringToDouble | 585.10 | 547.72 | -37.38 | -6.4% | Release build, 1,000,000-value corpus |
| floatToString | 1054.09 | 1047.19 | -6.90 | -0.7% | Formatter unchanged within noise |
| stringToFloat | 459.51 | 427.00 | -32.51 | -7.1% | Release build, 1,000,000-value corpus |

## Risk Mitigation
- [ ] Maintain a full suite of unit tests and fuzzers; run them after each major change.
- [ ] Document any changes in rounding behavior and add targeted regression tests.
- [ ] Keep fallbacks to the current implementation until the new paths are proven robust.

## Deliverables
- [ ] Implementation branches for parsing and formatting optimizations, each with dedicated benchmarks and profiling data.
- [ ] Documentation updates summarizing new algorithms and configuration knobs.
- [ ] Final report comparing throughput and accuracy before and after optimizations.
