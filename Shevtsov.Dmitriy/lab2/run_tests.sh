#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"

bash build.sh

status=0
for tests in ./tests_*; do
	echo "=== $tests"
	"$tests" || status=1
done
exit $status
