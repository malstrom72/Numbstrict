#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"/..
mkdir -p ./output
# Apple clang ships without libFuzzer, so use Homebrew LLVM on macOS when it is installed
if [[ "$(uname -s)" == "Darwin" && -z "${CPP_COMPILER:-}" ]] && command -v brew > /dev/null \
		&& [[ -x "$(brew --prefix llvm)/bin/clang++" ]]; then
	export CPP_COMPILER="$(brew --prefix llvm)/bin/clang++"
fi
CPP_OPTIONS="-std=c++11 -fsanitize=fuzzer,address ${CPP_OPTIONS:-}" bash ./tools/BuildCpp.sh beta native output/NumbstrictFuzz tests/NumbstrictFuzz.cpp src/Numbstrict.cpp
