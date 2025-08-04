#!/usr/bin/env bash
set -e -o pipefail -u

cd "$(dirname "$0")"

for target in beta release; do
	out_dir="output/$target"
	mkdir -p "$out_dir"
	bash tools/BuildCpp.sh "$target" native "$out_dir/smoke" -I src tests/smoke.cpp src/Numbstrict.cpp src/Makaron.cpp
	"$out_dir/smoke" > /dev/null
done

echo "Build and tests completed"
