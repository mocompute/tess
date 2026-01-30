#!/usr/bin/env bash

usage() {
    echo "cmake-build-test <build_dir> [ctest options...]"
}

if [ $# -lt 1 ]; then usage; exit 1; fi

build_dir=$1
shift
if [[ $build_dir == -* ]]; then usage; exit 1; fi

# Leak detection is off by default on macOS because of spurious error.
# This turns it on:
# export ASAN_OPTIONS=detect_leaks=1

cd "$build_dir" && ninja && ctest "$@"
