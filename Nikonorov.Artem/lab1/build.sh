#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT="${ROOT_DIR}/disk-monitor"

CXX="${CXX:-g++}"
SOURCES=(
  "${ROOT_DIR}/src/main.cpp"
  "${ROOT_DIR}/src/Config.cpp"
  "${ROOT_DIR}/src/Logging.cpp"
  "${ROOT_DIR}/src/ProcessControl.cpp"
  "${ROOT_DIR}/src/WatchManager.cpp"
  "${ROOT_DIR}/src/WatchManagerEvents.cpp"
  "${ROOT_DIR}/src/Daemon.cpp"
)

"${CXX}" -std=c++17 -Wall -Wextra -Werror -pedantic -O2 \
  -I"${ROOT_DIR}/include" "${SOURCES[@]}" -o "${OUTPUT}"
printf 'Built: %s\n' "${OUTPUT}"
