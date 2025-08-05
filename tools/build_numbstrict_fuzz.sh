#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"/..
mkdir -p ./output
CPP_OPTIONS="-std=c++11 -fsanitize=fuzzer,address ${CPP_OPTIONS:-}" bash ./tools/BuildCpp.sh beta native output/NumbstrictFuzz tests/NumbstrictFuzz.cpp src/Numbstrict.cpp
