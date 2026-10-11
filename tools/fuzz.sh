#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"/..

# Fuzzes the libFuzzer targets by hand (never on a schedule: CI runs on pushes and ad hoc). Builds each target, runs it
# for the given number of minutes from the committed corpus with its dictionary, then minimizes the grown corpus back
# into tests/fuzz/, ready to commit if git shows it changed. A finding stops the script with libFuzzer's exit code and
# leaves the input in output/fuzz/ (crash-*, timeout-* or oom-*) with the log next to it.
#	bash tools/fuzz.sh [minutes] [numbstrict|makaron|realConversion ...]		default: 10 minutes, all three targets

minutes="${1:-10}"
shift || true
targets=("$@")
if [[ ${#targets[@]} -eq 0 ]]; then
	targets=(numbstrict makaron realConversion)
fi
rm -rf output/fuzz
mkdir -p output/fuzz/merged

for target in "${targets[@]}"; do
	case "$target" in
		numbstrict) script=build_numbstrict_fuzz; exe=NumbstrictFuzz; options=(-dict=tests/fuzz/numbstrict.dict) ;;
		makaron) script=build_makaron_fuzz; exe=MakaronFuzz
			options=(-dict=tests/fuzz/makaron.dict tests/fuzz/makaronCrashes) ;;
		realConversion) script=build_realconversion_fuzz; exe=RealConversionFuzz; options=(-max_len=1200) ;;
		*) echo "Unknown target: $target"; exit 1 ;;
	esac
	bash "tools/$script.sh"
	tar -xzf "tests/fuzz/${target}Corpus.tar.gz" -C output/fuzz
	before="$(ls "output/fuzz/$target" | wc -l | tr -d ' ')"
	echo "Fuzzing $exe for $minutes minutes from $before inputs"
	if ! "./output/$exe" "-max_total_time=$((minutes * 60))" -artifact_prefix=output/fuzz/ "output/fuzz/$target" \
			"${options[@]}" > "output/fuzz/$target.log" 2>&1; then
		echo "$exe found something: see output/fuzz/$target.log and the input in output/fuzz/"
		exit 1
	fi
	mkdir -p "output/fuzz/merged/$target"
	"./output/$exe" -merge=1 "output/fuzz/merged/$target" "output/fuzz/$target" > "output/fuzz/$target.merge.log" 2>&1
	tar -czf "tests/fuzz/${target}Corpus.tar.gz" -C output/fuzz/merged "$target"
	after="$(ls "output/fuzz/merged/$target" | wc -l | tr -d ' ')"
	echo "$exe: no finding, corpus $before -> $after inputs, tests/fuzz/${target}Corpus.tar.gz updated"
done
echo "Fuzzing completed; commit the corpora in tests/fuzz/ if git shows them changed"
