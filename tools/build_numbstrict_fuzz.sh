#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"/..
mkdir -p ./output
CPP_OPTIONS="-fsanitize=fuzzer,address" \
  bash ./tools/BuildCpp.sh beta native output/NumbstrictFuzz \
    tests/NumbstrictFuzz.cpp src/Numbstrict.cpp
