#!/usr/bin/env bash
set -e -o pipefail -u

cd "$(dirname "$0")"

for target in beta release; do
	out_dir="output/$target"
	mkdir -p "$out_dir"
	CPP_OPTIONS="-std=c++11" bash tools/BuildCpp.sh "$target" native "$out_dir/smoke" \
		-I src tests/smoke.cpp src/Numbstrict.cpp src/Makaron.cpp
	"$out_dir/smoke" > /dev/null
	CPP_OPTIONS="-std=c++11" bash tools/BuildCpp.sh "$target" native "$out_dir/doubleFloatToString" \
		-I src tests/doubleFloatToString.cpp src/Numbstrict.cpp src/Makaron.cpp
	"$out_dir/doubleFloatToString" > /dev/null
	CPP_OPTIONS="-std=c++11" bash tools/BuildCpp.sh "$target" native "$out_dir/MakaronCmd" \
		-I src tools/MakaronCmd.cpp src/Makaron.cpp
done

echo "Build and tests completed"
