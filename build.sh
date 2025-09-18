#!/usr/bin/env bash
set -e -o pipefail -u

cd "$(dirname "$0")"

original_cpp_options="${CPP_OPTIONS:-}"

cpp_options="$original_cpp_options"
if [[ "$cpp_options" != *"-std="* ]]; then
	if [[ -n "$cpp_options" ]]; then
		cpp_options="$cpp_options -std=c++11"
	else
		cpp_options="-std=c++11"
	fi
fi

c_options=""
if [[ -n "$original_cpp_options" ]]; then
	for opt in $original_cpp_options; do
		if [[ "$opt" == -std=* ]]; then
			continue
		fi
		if [[ -n "$c_options" ]]; then
			c_options="$c_options $opt"
		else
			c_options="$opt"
		fi
	done
fi
if [[ "$c_options" != *"-std="* ]]; then
	if [[ -n "$c_options" ]]; then
		c_options="$c_options -std=c11"
	else
		c_options="-std=c11"
	fi
fi

for target in beta release; do
	out_dir="output/$target"
	mkdir -p "$out_dir"

	CPP_OPTIONS="$cpp_options" bash tools/BuildCpp.sh "$target" native "$out_dir/smoke" \
		-I src tests/smoke.cpp src/Numbstrict.cpp src/Makaron.cpp
	"$out_dir/smoke" > /dev/null

	CPP_OPTIONS="$cpp_options" bash tools/BuildCpp.sh "$target" native "$out_dir/doubleFloatToString" \
		-I src tests/doubleFloatToString.cpp src/Numbstrict.cpp src/Makaron.cpp
	"$out_dir/doubleFloatToString" > /dev/null

	CPP_OPTIONS="$cpp_options" bash tools/BuildCpp.sh "$target" native "$out_dir/MakaronCmd" \
		-I src tools/MakaronCmd.cpp src/Makaron.cpp

	CPP_COMPILER=clang CPP_OPTIONS="$c_options" bash tools/BuildCpp.sh "$target" native "$out_dir/ryu_d2s.o" \
		-I externals/ryu -c externals/ryu/ryu/d2s.c

	CPP_COMPILER=clang CPP_OPTIONS="$c_options" bash tools/BuildCpp.sh "$target" native "$out_dir/ryu_f2s.o" \
		-I externals/ryu -c externals/ryu/ryu/f2s.c

	CPP_OPTIONS="$cpp_options" bash tools/BuildCpp.sh "$target" native "$out_dir/hexDoubleToDecimal" \
		-I externals/ryu tools/HexDoubleToDecimal.cpp "$out_dir/ryu_d2s.o"

	CPP_OPTIONS="$cpp_options" bash tools/BuildCpp.sh "$target" native "$out_dir/compareWithRyu" \
		-I src -I externals/ryu tests/compareWithRyu.cpp src/Numbstrict.cpp src/Makaron.cpp \
		"$out_dir/ryu_d2s.o" "$out_dir/ryu_f2s.o"
	"$out_dir/compareWithRyu" float

	CPP_OPTIONS="$cpp_options" bash tools/BuildCpp.sh "$target" native "$out_dir/dd_parser_downscale_table" \
		dd_parser_downscale_table.cpp
	"$out_dir/dd_parser_downscale_table" > /dev/null
done

echo "Build and tests completed"
