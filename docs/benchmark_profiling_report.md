# Float/String Benchmark Profiling Report

## Environment
- Kernel: `Linux 789fa85a70e3 6.12.13 #1 SMP Thu Mar 13 11:34:50 UTC 2025 x86_64` (`uname -a`)
- Compiler: `g++ (Ubuntu 13.3.0-6ubuntu2~24.04) 13.3.0`
- Profiler: `valgrind-3.22.0` Callgrind

## Commands Executed
1. `timeout 180 ./build.sh`
2. `output/release/compareWithRyu 10000000`
3. `output/release/benchmarkToString`
4. `valgrind --tool=callgrind --callgrind-out-file=docs/profiles/callgrind.benchmarkToString.count10000.out output/release/benchmarkToString count=10000`
5. `callgrind_annotate --auto=yes docs/profiles/callgrind.benchmarkToString.count10000.out`

## Benchmark Summary (release build)

| Benchmark | Corpus Size | Runtime | ns/value | Reference |
| --- | --- | --- | --- | --- |
| `Numbstrict::doubleToString` | 1,000,000 | 1,750.87 ms | 1,750.87 |  |
| `Ryu d2s` | 1,000,000 | 102.363 ms | 102.363 |  |
| `std::ostringstream<double>` | 1,000,000 | 1,405.24 ms | 1,405.24 |  |
| `Numbstrict::stringToDouble` | 1,000,000 | 284.847 ms | 284.847 |  |
| `std::strtod` | 1,000,000 | 326.86 ms | 326.86 |  |
| `std::istringstream<double>` | 1,000,000 | 744.721 ms | 744.721 |  |
| `Numbstrict::floatToString` | 1,000,000 | 950.431 ms | 950.431 |  |
| `Ryu f2s` | 1,000,000 | 57.3767 ms | 57.3767 |  |
| `std::ostringstream<float>` | 1,000,000 | 740.362 ms | 740.362 |  |
| `Numbstrict::stringToFloat` | 1,000,000 | 209.851 ms | 209.851 |  |
| `std::strtof` | 1,000,000 | 172.182 ms | 172.182 |  |
| `std::istringstream<float>` | 1,000,000 | 500.202 ms | 500.202 |  |

## Profiling Run (Callgrind)
- Command: `valgrind --tool=callgrind --callgrind-out-file=docs/profiles/callgrind.benchmarkToString.count10000.out output/release/benchmarkToString count=10000`
- Corpus Size: 10,000 values (reduced for manageable profiling time)
- Total Instructions: 488,944,809 `Ir`

### Observed Timings Under Callgrind (count=10000)

| Benchmark | Runtime | ns/value |
| --- | --- | --- |
| `Numbstrict::doubleToString` | 687.72 ms | 68,772.0 |
| `Ryu d2s` | 51.7172 ms | 5,171.72 |
| `std::ostringstream<double>` | 767.967 ms | 76,796.7 |
| `Numbstrict::stringToDouble` | 131.794 ms | 13,179.4 |
| `std::strtod` | 175.003 ms | 17,500.3 |
| `std::istringstream<double>` | 532.859 ms | 53,285.9 |
| `Numbstrict::floatToString` | 423.568 ms | 42,356.8 |
| `Ryu f2s` | 32.3127 ms | 3,231.27 |
| `std::ostringstream<float>` | 435.507 ms | 43,550.7 |
| `Numbstrict::stringToFloat` | 99.7793 ms | 9,977.93 |
| `std::strtof` | 102.375 ms | 10,237.5 |
| `std::istringstream<float>` | 367.221 ms | 36,722.1 |

### Top Hotspots (Instruction Counts)

| Function | Share of `Ir` |
| --- | --- |
| `Numbstrict::realToString<double>` | 8.39% |
| `Numbstrict::DoubleDouble::operator/(int) const` | 7.65% |
| `Numbstrict::multiplyAndAdd` | 5.03% |
| `Numbstrict::realToString<float>` | 4.27% |
| `Numbstrict::scaleAndRound<double>` | 4.02% |
| `Numbstrict::DoubleDouble::operator+` | 3.96% |
| `Numbstrict::scaleAndRound<float>` | 2.93% |
| `Numbstrict::DoubleDouble::operator<` | 2.58% |
| `Numbstrict::DoubleDouble::operator-` | 2.45% |
| `Numbstrict::parseReal<double>` | 1.33% |
| `Numbstrict::parseReal<float>` | 0.85% |

### Notes
- The profiling run confirms that `DoubleDouble` arithmetic (division, addition, comparison) and the formatter’s `realToString` implementation dominate instruction counts, aligning with previous optimization targets.
- Library fallbacks (`std::strtod`, `std::ostringstream`) still incur significant cost but serve as baselines for comparison.

## Prioritized Optimization Plan
1. **Cut per-digit `DoubleDouble` divisions in the formatter (top hotspot: `DoubleDouble::operator/(int)`).**
- Replace the repeated `magnitude / 10` and quotient estimation divides with cached powers of ten or reciprocal multiplies.
- Validate that the new approach preserves rounding across 0–9 digit boundaries using the 10,000,000-case `compareWithRyu` fuzz test before landing.
2. **Streamline the formatter core (`realToString` and `scaleAndRound`).**
- Cache `scaleAndRound` outputs so each digit uses one scaling pass instead of recomputing for both the candidate and `candidate + 1`.
- Examine opportunities to move invariant work (e.g., exponent prep) out of the digit loop per profiling evidence.
3. **Reduce `DoubleDouble` helper traffic (`operator+`, comparisons, subtraction).**
- Profile whether batching digit accumulation or reusing intermediate sums can reduce the 6–8% instruction budget consumed by `multiplyAndAdd` and the basic operators.
- Consider specialized helpers for common cases (e.g., adding a small integer) to avoid full 128-bit style arithmetic when unnecessary.
4. **Tighten parsing hotspots (`parseReal`, `multiplyAndAdd`).**
- Extend the floating fast-path accumulator to cover more digits without sacrificing exactness, so fewer iterations reach the expensive `DoubleDouble` loop.
- Investigate caching of per-digit magnitudes similar to the formatter to eliminate the mirrored `/ 10` cost on the parsing side.
5. **Re-measure after each change.**
- Benchmarks must be captured before and after every optimization using the release harness plus the Callgrind profile when relevant.
- Record the deltas in both this report and the optimization plan so the impact of each change remains auditable.

## Artifacts
- Raw profile: `docs/profiles/callgrind.benchmarkToString.count10000.out`
- Annotated summary: `callgrind_annotate --auto=yes docs/profiles/callgrind.benchmarkToString.count10000.out`
