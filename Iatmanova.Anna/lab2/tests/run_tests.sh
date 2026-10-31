#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SKIP_BUILD=0
if [[ "${1:-}" == "--skip-build" ]]; then
    SKIP_BUILD=1
fi

pass() { printf '[PASS] %s\n' "$1"; }
fail() { printf '[FAIL] %s\n' "$1" >&2; exit 1; }

cd "$ROOT"

[[ -f host.cpp ]] || fail "host.cpp exists"
for type in mq fifo sock; do
    [[ -f "conn_${type}.cpp" && -f "conn_${type}.hpp" ]] || fail "conn_${type} sources exist"
done
pass "required source files exist"

if [[ $SKIP_BUILD -eq 0 ]]; then
    ./build.sh >/tmp/os_lab2_build.log 2>&1 || { cat /tmp/os_lab2_build.log; fail "build.sh"; }
fi

for type in mq fifo sock; do
    [[ -x "host_${type}" ]] || fail "host_${type} was built"
done
pass "all three host_* executables were built"

for type in mq fifo sock; do
    output="$(timeout 12s "./host_${type}" --self-test 2>&1)" || { printf '%s\n' "$output"; fail "${type} self-test"; }
    grep -q "\[SELFTEST PASS\] ${type}" <<<"$output" || { printf '%s\n' "$output"; fail "${type} self-test output"; }
    pass "${type}: parent/child round trip over IPC"
done

# The build directory must be cleaned by build.sh.
[[ ! -d .build ]] || fail "build.sh cleans CMake intermediate files"
pass "build.sh cleans CMake intermediate files"

# Verify that make in the project root works as required.
make clean >/dev/null
make >/tmp/os_lab2_make.log 2>&1 || { cat /tmp/os_lab2_make.log; fail "make from project root"; }
for type in mq fifo sock; do
    [[ -x "host_${type}" ]] || fail "make did not build host_${type}"
done
pass "make from project root builds all IPC variants"

printf '\nAll tests passed.\n'
