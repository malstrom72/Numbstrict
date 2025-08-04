#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"/..
mkdir -p ./output
CPP_OPTIONS="-I ./src -fsanitize=fuzzer,address" \
  bash ./tools/BuildCpp.sh beta native output/MakaronFuzz \
    tests/MakaronFuzz.cpp src/Makaron.cpp
