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
- [x] **Reduce `StandardFPEnvScope` cost.**
   - [x] Audit all call sites in the parser and formatter to determine whether redundant floating-point environment resets occur.
   - [x] Experiment with thread-local fast paths that skip environment changes when the default rounding mode is already active.
   - [x] Validate that any skip logic maintains determinism in multi-threaded scenarios.
- [x] **Batch environment transitions.**
   - [x] Investigate hoisting the environment scope out of tight conversion loops so it is entered once per batch instead of per value.
   - [x] Update callers that perform bulk conversions to amortize the setup cost.

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
- [x] **Direct digit extraction.**
   - [x] Replace the inner digit-selection loop with a quotient-based approach that derives each digit via a single `DoubleDouble` division.
   - [x] Verify rounding correctness across boundaries (0–9) and for tie cases.
- [x] **Reuse `scaleAndRound` results.**
   - [x] Cache intermediate scaling values and reuse them across digit iterations to avoid duplicate computations.
   - [x] Confirm that memoization does not introduce stale state between iterations.
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

### 2025-09-23 – Thread-local `StandardFPEnvScope` reuse
| Benchmark | Before (ns/value) | After (ns/value) | Δ ns/value | Δ % | Notes |
| --- | --- | --- | --- | --- | --- |
| doubleToString | 1286.80 | 1288.17 | +1.37 | +0.1% | Release build, 1,000,000-value corpus; thread-local skip verification |
| stringToDouble | 357.78 | 393.68 | +35.90 | +10.0% | Release build, same corpus; regression from additional `fegetenv` checks |
| floatToString | 693.03 | 734.82 | +41.79 | +6.0% | Release build, same corpus; guard amortization still pending tuning |
| stringToFloat | 202.69 | 218.85 | +16.16 | +8.0% | Release build, same corpus; follow-up needed to recover baseline |

### 2025-09-22 – Batch `FloatStringBatchGuard` for conversions
| Benchmark | Before (ns/value) | After (ns/value) | Δ ns/value | Δ % | Notes |
| --- | --- | --- | --- | --- | --- |
| doubleToString | 1507.51 | 1269.65 | -237.86 | -15.8% | Release build, 1,000,000-value corpus; batch guard active |
| stringToDouble | 659.51 | 360.73 | -298.78 | -45.3% | Same corpus, batch guard hoisted outside loop |
| floatToString | 956.25 | 695.88 | -260.37 | -27.2% | Release build, guard scoped once per run |
| stringToFloat | 456.96 | 201.79 | -255.18 | -55.8% | Same corpus, guard reused for parsing |

### 2025-09-21 – Quotient-guided digit estimation in `realToString`
| Benchmark | Before (ns/value) | After (ns/value) | Δ ns/value | Δ % | Notes |
| --- | --- | --- | --- | --- | --- |
| doubleToString | 2931.49 | 1998.30 | -933.19 | -31.8% | Release build, 1,000,000-value corpus |
| stringToDouble | 789.79 | 761.03 | -28.76 | -3.6% | Same corpus, parsing pass |
| floatToString | 1748.86 | 1203.78 | -545.08 | -31.2% | Release build, 1,000,000-value corpus |
| stringToFloat | 570.69 | 566.48 | -4.21 | -0.7% | Same corpus, parsing pass |

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

### 2025-09-20 – Lazy `scaleAndRound` evaluation in `realToString`
| Benchmark | Before (ns/value) | After (ns/value) | Δ ns/value | Δ % | Notes |
| --- | --- | --- | --- | --- | --- |
| doubleToString | 3377.93 | 3510.06 | +132.13 | +3.9% | Averaged over two runs, release build |
| stringToDouble | 841.91 | 839.43 | -2.48 | -0.3% | Averaged over two runs, release build |
| floatToString | 1930.93 | 1892.82 | -38.11 | -2.0% | Averaged over two runs, release build |
| stringToFloat | 637.19 | 620.08 | -17.11 | -2.7% | Averaged over two runs, release build |

## Risk Mitigation
- [ ] Maintain a full suite of unit tests and fuzzers; run them after each major change.
- [ ] Document any changes in rounding behavior and add targeted regression tests.
- [ ] Keep fallbacks to the current implementation until the new paths are proven robust.

## Deliverables
- [ ] Implementation branches for parsing and formatting optimizations, each with dedicated benchmarks and profiling data.
- [ ] Documentation updates summarizing new algorithms and configuration knobs.
- [ ] Final report comparing throughput and accuracy before and after optimizations.
