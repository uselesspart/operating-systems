#!/bin/bash
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

make clean
make

rm -rf build

echo "Built:"
ls -1 host_*