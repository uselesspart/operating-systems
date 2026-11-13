#!/usr/bin/env bash
#
# Builds the lab1 daemon into a single executable file.
# Intermediate object files are placed in a temporary build directory that is
# removed on exit, whatever the result of the build is.

set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly BUILD_DIR="${SCRIPT_DIR}/build"
readonly TARGET="${SCRIPT_DIR}/lab1daemon"
readonly SOURCE_DIR="${SCRIPT_DIR}/src"
readonly CXX="${CXX:-g++}"
readonly CXXFLAGS=(-std=c++17 -Wall -Werror -O2)

cleanup() {
    rm -rf "${BUILD_DIR}"
}
trap cleanup EXIT

for tool in "${CXX}" rm mkdir; do
    if ! command -v "${tool}" > /dev/null 2>&1; then
        echo "build.sh: required tool '${tool}' not found" >&2
        exit 1
    fi
done

if [ ! -d "${SOURCE_DIR}" ]; then
    echo "build.sh: source directory '${SOURCE_DIR}' not found" >&2
    exit 1
fi

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

objects=()
for source in "${SOURCE_DIR}"/*.cpp; do
    if [ ! -f "${source}" ]; then
        echo "build.sh: no source files in '${SOURCE_DIR}'" >&2
        exit 1
    fi
    object="${BUILD_DIR}/$(basename "${source}" .cpp).o"
    echo "compiling $(basename "${source}")"
    "${CXX}" "${CXXFLAGS[@]}" -I"${SOURCE_DIR}" -c "${source}" -o "${object}"
    objects+=("${object}")
done

echo "linking $(basename "${TARGET}")"
"${CXX}" "${CXXFLAGS[@]}" -o "${TARGET}" "${objects[@]}"

echo "build.sh: done, executable is ${TARGET}"
