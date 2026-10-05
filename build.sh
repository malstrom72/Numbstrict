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

# The committed fuzz corpora, replayed through the fuzz targets in beta builds
mkdir -p output/fuzzReplay
tar -xzf tests/fuzz/numbstrictCorpus.tar.gz -C output/fuzzReplay
tar -xzf tests/fuzz/makaronCorpus.tar.gz -C output/fuzzReplay

for target in beta release; do
	out_dir="output/$target"
	mkdir -p "$out_dir"

	CPP_OPTIONS="$cpp_options" bash tools/BuildCpp.sh "$target" native "$out_dir/smoke" \
		-I src tests/smoke.cpp src/Numbstrict.cpp src/Makaron.cpp
	"$out_dir/smoke" > /dev/null

	# 32-bit x86 has its own floating-point environment code path. Needs a multilib toolchain (not available on macOS).
	if [[ "$(uname -s)" != "Darwin" ]] \
			&& echo 'int main() { return 0; }' | ${CPP_COMPILER:-g++} -m32 -x c++ - -o "$out_dir/m32probe" 2>/dev/null; then
		CPP_OPTIONS="$cpp_options -msse2 -mfpmath=sse" bash tools/BuildCpp.sh "$target" x86 "$out_dir/smoke_x86" \
			-I src tests/smoke.cpp src/Numbstrict.cpp src/Makaron.cpp
		"$out_dir/smoke_x86" > /dev/null
	else
		echo "Skipping x86 smoke test (no 32-bit toolchain)"
	fi
	rm -f "$out_dir/m32probe"

	if [[ "$target" == beta ]]; then
		CPP_OPTIONS="$cpp_options" bash tools/BuildCpp.sh "$target" native "$out_dir/NumbstrictFuzzReplay" \
			tests/NumbstrictFuzz.cpp tests/FuzzMain.cpp src/Numbstrict.cpp
		"$out_dir/NumbstrictFuzzReplay" output/fuzzReplay/numbstrict > /dev/null
		CPP_OPTIONS="$cpp_options" bash tools/BuildCpp.sh "$target" native "$out_dir/MakaronFuzzReplay" \
			-I src tests/MakaronFuzz.cpp tests/FuzzMain.cpp src/Makaron.cpp
		"$out_dir/MakaronFuzzReplay" output/fuzzReplay/makaron > /dev/null
	fi

	CPP_OPTIONS="$cpp_options" bash tools/BuildCpp.sh "$target" native "$out_dir/doubleFloatToString" \
		-I src tests/doubleFloatToString.cpp src/Numbstrict.cpp src/Makaron.cpp
	"$out_dir/doubleFloatToString" > /dev/null

	CPP_OPTIONS="$cpp_options" bash tools/BuildCpp.sh "$target" native "$out_dir/MakaronCmd" \
		-I src tools/MakaronCmd.cpp src/Makaron.cpp

	if [[ -d externals/ryu ]]; then
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

		CPP_COMPILER=clang CPP_OPTIONS="$c_options" bash tools/BuildCpp.sh "$target" native "$out_dir/ryu_d2s.o" \
			-I externals/ryu -c externals/ryu/ryu/d2s.c

		CPP_COMPILER=clang CPP_OPTIONS="$c_options" bash tools/BuildCpp.sh "$target" native "$out_dir/ryu_f2s.o" \
			-I externals/ryu -c externals/ryu/ryu/f2s.c

		CPP_OPTIONS="$cpp_options" bash tools/BuildCpp.sh "$target" native "$out_dir/hexDoubleToDecimal" \
			-I externals/ryu tools/HexDoubleToDecimal.cpp "$out_dir/ryu_d2s.o"

		CPP_OPTIONS="$cpp_options" bash tools/BuildCpp.sh "$target" native "$out_dir/benchmarkToString" \
			-I src -I externals/ryu tests/benchmarkToString.cpp src/Numbstrict.cpp src/Makaron.cpp \
			"$out_dir/ryu_d2s.o" "$out_dir/ryu_f2s.o"
	fi
done

echo "Build and tests completed"
