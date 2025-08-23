#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"
bash BuildCpp.sh release x64 makaron -I ../src/ MakaronCmd.cpp ../src/Makaron.cpp
