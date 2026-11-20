#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
mkdir -p "$work/project" "$work/scratch"
cp -a -- "$root/." "$work/project/"
cd -- "$work/project"
printf 'int ordinary_cpp_probe() { return 13; }\n' > src/AutoProbe.cpp
printf 'int ordinary_c_probe(void) { return 13; }\n' > src/AutoCProbe.c
printf 'int root_cpp_probe() { return 13; }\n' > RootProbe.cpp
cp src/conn_sock.cpp src/conn_probe.cpp
cmake -S . -B "$work/objects" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "$work/objects" --parallel "${JOBS:-2}"
test -f "$work/objects/CMakeFiles/chat_core.dir/src/AutoProbe.cpp.o"
test -f "$work/objects/CMakeFiles/chat_core.dir/src/AutoCProbe.c.o"
test -f "$work/objects/CMakeFiles/chat_core.dir/RootProbe.cpp.o"
test -x "$work/objects/bin/host_probe"
grep -q -- '-Wall' "$work/objects/compile_commands.json"
grep -q -- '-Werror' "$work/objects/compile_commands.json"
printf 'PASS: automatic .cpp/.c compilation and host_TYPE generation, mandatory flags\n'
TMPDIR="$work/scratch" make
for code in mq fifo sock probe; do test -x "host_$code"; done
test -z "$(find "$work/scratch" -mindepth 1 -print -quit)"
printf 'PASS: make builds all transports and removes intermediate files\n'
printf '#error EXPECTED_BUILD_FAILURE\n' > src/AutoProbe.cpp
if TMPDIR="$work/scratch" bash build.sh > "$work/expected-failure.log" 2>&1; then
    printf 'FAIL: invalid automatically discovered source was ignored\n' >&2
    exit 1
fi
grep -q EXPECTED_BUILD_FAILURE "$work/expected-failure.log"
test -z "$(find "$work/scratch" -mindepth 1 -print -quit)"
printf 'PASS: intermediate files removed on compilation failure\n'
