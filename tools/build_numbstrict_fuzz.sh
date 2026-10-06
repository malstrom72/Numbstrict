#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"/..
mkdir -p ./output
# Builds the libFuzzer target with clang: optimized with asserts on, AddressSanitizer and UBSan. Give libFuzzer a
# scratch corpus directory first, since it writes new inputs there:
#	./output/NumbstrictFuzz -dict=tests/fuzz/numbstrict.dict -artifact_prefix=output/ output/numbstrictCorpus
# Apple clang ships without libFuzzer, so use Homebrew LLVM on macOS when it is installed
if [[ "$(uname -s)" == "Darwin" && -z "${CPP_COMPILER:-}" ]] && command -v brew > /dev/null \
		&& [[ -x "$(brew --prefix llvm)/bin/clang++" ]]; then
	export CPP_COMPILER="$(brew --prefix llvm)/bin/clang++"
fi
CPP_COMPILER="${CPP_COMPILER:-clang++}" \
		CPP_OPTIONS="-std=c++11 -O2 -g -UNDEBUG -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all ${CPP_OPTIONS:-}" \
		bash ./tools/BuildCpp.sh release native output/NumbstrictFuzz tests/NumbstrictFuzz.cpp src/Numbstrict.cpp
