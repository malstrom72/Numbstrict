#!/usr/bin/env bash
set -e -o pipefail -u

cd "$(dirname "$0")"

for target in beta release; do
	out_dir="output/$target"
	mkdir -p "$out_dir"
	bash tools/BuildCpp.sh "$target" native "$out_dir/smoke" \
		-I src tests/smoke.cpp src/Numbstrict.cpp src/Makaron.cpp
	"$out_dir/smoke" > /dev/null
	bash tools/BuildCpp.sh "$target" native "$out_dir/doubleFloatToString" \
		-I src tests/doubleFloatToString.cpp src/Numbstrict.cpp src/Makaron.cpp
	"$out_dir/doubleFloatToString" > /dev/null
	bash tools/BuildCpp.sh "$target" native "$out_dir/MakaronCmd" \
		-I src tools/MakaronCmd.cpp src/Makaron.cpp
	bash tools/BuildCpp.sh "$target" native "$out_dir/hexDoubleToDecimal" \
		-I externals/ryu tools/HexDoubleToDecimal.cpp externals/ryu/ryu/d2s.c
	bash tools/BuildCpp.sh "$target" native "$out_dir/compareWithRyu" \
		-I src -I externals/ryu tests/compareWithRyu.cpp src/Numbstrict.cpp src/Makaron.cpp \
		externals/ryu/ryu/d2s.c externals/ryu/ryu/f2s.c
		"$out_dir/compareWithRyu" float
done

echo "Build and tests completed"

