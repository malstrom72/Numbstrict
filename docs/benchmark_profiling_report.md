# Float/String Benchmark Profiling Report

## Environment
- Kernel: `Linux 4a3d3024d240 6.12.13 #1 SMP Thu Mar 13 11:34:50 UTC 2025 x86_64` (`uname -a`)
- Compiler: `g++ (Ubuntu 13.3.0-6ubuntu2~24.04) 13.3.0`
- Profiler: `valgrind-3.22.0` Callgrind

## Commands Executed
1. `timeout 180 ./build.sh`
2. `output/release/compareWithRyu 10000000`
3. `output/release/benchmarkToString`
4. `valgrind --tool=callgrind --callgrind-out-file=callgrind.out.benchmark output/release/benchmarkToString count=10000`
5. `callgrind_annotate --auto=yes callgrind.out.benchmark`

## Benchmark Summary (release build)

| Benchmark | Corpus Size | Runtime | ns/value | Reference |
| --- | --- | --- | --- | --- |
| `Numbstrict::doubleToString` | 1,000,000 | 1,832.78 ms | 1,832.78 |  | 
| `Ryu d2s` | 1,000,000 | 90.9938 ms | 90.9938 |  |
| `std::ostringstream<double>` | 1,000,000 | 1,258.27 ms | 1,258.27 |  |
| `Numbstrict::stringToDouble` | 1,000,000 | 501.72 ms | 501.72 |  |
| `std::strtod` | 1,000,000 | 290.321 ms | 290.321 |  |
| `std::istringstream<double>` | 1,000,000 | 718.492 ms | 718.492 |  |
| `Numbstrict::floatToString` | 1,000,000 | 1,002.77 ms | 1,002.77 |  |
| `Ryu f2s` | 1,000,000 | 57.5708 ms | 57.5708 |  |
| `std::ostringstream<float>` | 1,000,000 | 626.337 ms | 626.337 |  |
| `Numbstrict::stringToFloat` | 1,000,000 | 291.048 ms | 291.048 |  |
| `std::strtof` | 1,000,000 | 173.589 ms | 173.589 |  |
| `std::istringstream<float>` | 1,000,000 | 559.561 ms | 559.561 |  |

## Profiling Run (Callgrind)
- Command: `valgrind --tool=callgrind --callgrind-out-file=callgrind.out.benchmark output/release/benchmarkToString count=10000`
- Corpus Size: 10,000 values (reduced for manageable profiling time)
- Total Instructions: 501,637,480 `Ir`

### Observed Timings Under Callgrind (count=10000)

| Benchmark | Runtime | ns/value |
| --- | --- | --- |
| `Numbstrict::doubleToString` | 603.738 ms | 60,373.8 |
| `Ryu d2s` | 46.9681 ms | 4,696.81 |
| `std::ostringstream<double>` | 692.848 ms | 69,284.8 |
| `Numbstrict::stringToDouble` | 189.382 ms | 18,938.2 |
| `std::strtod` | 152.445 ms | 15,244.5 |
| `std::istringstream<double>` | 493.759 ms | 49,375.9 |
| `Numbstrict::floatToString` | 378.866 ms | 37,886.6 |
| `Ryu f2s` | 26.8662 ms | 2,686.62 |
| `std::ostringstream<float>` | 381.152 ms | 38,115.2 |
| `Numbstrict::stringToFloat` | 121.64 ms | 12,164.0 |
| `std::strtof` | 95.575 ms | 9,557.5 |
| `std::istringstream<float>` | 334.575 ms | 33,457.5 |

### Top Hotspots (Instruction Counts)

| Function | Share of `Ir` |
| --- | --- |
| `Numbstrict::DoubleDouble::operator/(int) const` | 8.62% |
| `Numbstrict::realToString<double>` | 8.18% |
| `Numbstrict::multiplyAndAdd` | 6.32% |
| `Numbstrict::realToString<float>` | 4.16% |
| `Numbstrict::scaleAndRound<double>` | 3.92% |
| `Numbstrict::DoubleDouble::operator+` | 3.86% |
| `Numbstrict::scaleAndRound<float>` | 2.85% |
| `Numbstrict::DoubleDouble::operator<` | 2.52% |
| `Numbstrict::DoubleDouble::operator-` | 2.39% |
| `Numbstrict::parseReal<double>` | 1.26% |
| `Numbstrict::parseReal<float>` | 0.77% |

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
