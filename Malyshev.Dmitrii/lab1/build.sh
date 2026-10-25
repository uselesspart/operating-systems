#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

g++ -std=c++17 -Wall -Werror -O2 -o lab1_daemon src/main.cpp src/Daemon.cpp

echo "Build OK: ./lab1_daemon"
