#!/bin/bash

cd "$(dirname "$0")"
chmod +x ./BuildCpp.sh
./BuildCpp.sh release 64 ./makaron -I ../src/ ./MakaronCmd.cpp ../src/Makaron.cpp
