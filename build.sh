#!/bin/bash
# Builds build/heretic.xex in WSL Debian, with xsdk at ~/xsdk and SDL-xenon built at ~/SDL-xenon/build-xsdk
# (README.md). Run from this folder: ./build.sh
set -e
cd "$(dirname "$0")"
cmake -S . -B build -G Ninja \
	-DCMAKE_TOOLCHAIN_FILE="$HOME/xsdk/cmake/xsdk.cmake" \
	-DSDL3_DIR="$HOME/SDL-xenon/build-xsdk" \
	-DCMAKE_BUILD_TYPE=Release
cmake --build build
ls -l build/heretic.xex
