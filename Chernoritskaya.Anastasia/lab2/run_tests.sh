#!/bin/bash
set -e

cd "$(dirname "$0")"

echo "Running tests"

if [ ! -x "build/unit_tests" ]; then
    echo "Error: Unit tests executable not found."
    echo "Please run 'make all' first."
    exit 1
fi

./build/unit_tests

echo "All tests passed"