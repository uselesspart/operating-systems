#!/usr/bin/env bash

set -eu

cd "$(dirname "$0")/.."

./build.sh
./build.sh test
./disk_monitor_tests
./tests/test_daemon.sh

echo "All tests passed"

