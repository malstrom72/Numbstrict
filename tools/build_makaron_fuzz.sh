#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"/..
mkdir -p ./output
CPP_OPTIONS="-std=c++11 -fsanitize=fuzzer,address" bash ./tools/BuildCpp.sh beta native output/MakaronFuzz -I ./src tests/MakaronFuzz.cpp src/Makaron.cpp
