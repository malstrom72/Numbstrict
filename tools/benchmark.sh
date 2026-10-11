#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"/..
# Builds and runs the benchmark of parsing and composing (tests/benchmark.cpp) in release and prints its table.
mkdir -p output/release
CPP_OPTIONS="-std=c++11 ${CPP_OPTIONS:-}" bash tools/BuildCpp.sh release native output/release/benchmark \
	-I src tests/benchmark.cpp src/Numbstrict.cpp
output/release/benchmark
