#!/usr/bin/env bash

set -eu

cd "$(dirname "$0")"

cxx=${CXX:-g++}
cxxflags="-std=c++17 -O2 -Wall -Wextra -Werror -pedantic -Iinclude"
sources="src/Config.cpp src/Daemon.cpp src/EventFormatter.cpp src/FileWatcher.cpp src/Logger.cpp src/PidFile.cpp src/main.cpp"

if ! command -v "$cxx" >/dev/null 2>&1; then
    echo "Compiler '$cxx' was not found" >&2
    exit 1
fi

build_dir=$(mktemp -d "${TMPDIR:-/tmp}/disk-monitor-build.XXXXXX")
cleanup() {
    rm -rf "$build_dir"
}
trap cleanup EXIT HUP INT TERM

objects=""
for source in $sources; do
    object="$build_dir/$(basename "${source%.cpp}.o")"
    "$cxx" $cxxflags -c "$source" -o "$object"
    objects="$objects $object"
done

"$cxx" $cxxflags $objects -o disk_monitor

if [ "${1:-}" = "test" ]; then
    test_sources="src/Config.cpp src/EventFormatter.cpp src/Logger.cpp src/PidFile.cpp tests/test_runner.cpp"
    test_objects=""
    for source in $test_sources; do
        object="$build_dir/test_$(basename "${source%.cpp}.o")"
        "$cxx" $cxxflags -c "$source" -o "$object"
        test_objects="$test_objects $object"
    done
    "$cxx" $cxxflags $test_objects -o disk_monitor_tests
fi

echo "Built $(pwd)/disk_monitor"
if [ "${1:-}" = "test" ]; then
    echo "Built $(pwd)/disk_monitor_tests"
fi

