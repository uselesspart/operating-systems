
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SKIP_BUILD=0
if [[ "${1:-}" == "--skip-build" ]]; then
    SKIP_BUILD=1
fi

pass() { printf '[PASS] %s\n' "$1"; }
fail() { printf '[FAIL] %s\n' "$1" >&2; exit 1; }

cd "$ROOT"

rm -f /tmp/os_lab2_v13_*.fifo /tmp/os_lab2_v13_*.sock

[[ -f host.cpp ]] || fail "host.cpp exists"
for type in mq fifo sock; do
    [[ -f "conn_${type}.cpp" && -f "conn_${type}.hpp" ]] || fail "conn_${type} sources exist"
done
pass "required source files exist"


interface_signature() {
    sed -n '/class Conn {/,/private:/p' "$1" \
        | sed 's/conn_[a-zA-Z0-9_]*\.hpp/conn_TYPE.hpp/g' \
        | sed '/^[[:space:]]*$/d'
}
base_interface="$(interface_signature conn_mq.hpp)"
for type in fifo sock; do
    [[ "$(interface_signature "conn_${type}.hpp")" == "$base_interface" ]] \
        || fail "conn_${type} public interface differs from conn_mq"
done
pass "all conn_* classes have the same public interface"

if [[ $SKIP_BUILD -eq 0 ]]; then
    ./build.sh >/tmp/os_lab2_build.log 2>&1 || { cat /tmp/os_lab2_build.log; fail "build.sh"; }
fi

for type in mq fifo sock; do
    [[ -x "host_${type}" ]] || fail "host_${type} was built"
done
pass "all three host_* executables were built"

for type in mq fifo sock; do
    output="$(timeout 12s "./host_${type}" --self-test 2>&1)" \
        || { printf '%s\n' "$output"; fail "${type} self-test"; }
    grep -q "\[SELFTEST PASS\] ${type}" <<<"$output" \
        || { printf '%s\n' "$output"; fail "${type} self-test output"; }
    pass "${type}: parent/child round trip over IPC"
done


if compgen -G '/tmp/os_lab2_v13_*.fifo' >/dev/null; then
    fail "FIFO files were not removed after the parent finished"
fi
if compgen -G '/tmp/os_lab2_v13_*.sock' >/dev/null; then
    fail "UNIX socket files were not removed after the parent finished"
fi
pass "IPC filesystem objects are cleaned after parent termination"


[[ ! -d .build ]] || fail "build.sh cleans CMake intermediate files"
pass "build.sh cleans CMake intermediate files"


PROBE_SOURCE="$ROOT/auto_compile_probe.cpp"
PROBE_BUILD="$ROOT/.autocompile_test_build"
PROBE_LOG="/tmp/os_lab2_autocompile.log"
PROBE_CONFIG_LOG="/tmp/os_lab2_autoconfigure.log"
cleanup_probe() {
    rm -f "$PROBE_SOURCE" "$PROBE_LOG" "$PROBE_CONFIG_LOG"
    rm -rf "$PROBE_BUILD"
}
trap cleanup_probe EXIT
cleanup_probe
cat > "$PROBE_SOURCE" <<'CPP'
extern "C" int os_lab2_auto_compile_probe() {
    return 42;
}
CPP

cmake -S "$ROOT" -B "$PROBE_BUILD" -DCMAKE_BUILD_TYPE=Release >"$PROBE_CONFIG_LOG" 2>&1 \
    || { cat "$PROBE_CONFIG_LOG"; fail "configure with newly added ordinary .cpp"; }
cmake --build "$PROBE_BUILD" --parallel --verbose >"$PROBE_LOG" 2>&1 \
    || { cat "$PROBE_LOG"; fail "build with newly added ordinary .cpp"; }

grep -q "auto_compile_probe.cpp" "$PROBE_LOG" \
    || fail "new ordinary .cpp was not compiled automatically"
grep -q -- '-Wall' "$PROBE_LOG" || fail "-Wall was not used during compilation"
grep -q -- '-Werror' "$PROBE_LOG" || fail "-Werror was not used during compilation"
for type in mq fifo sock; do
    find "$PROBE_BUILD/CMakeFiles/host_${type}.dir" -type f -name 'auto_compile_probe.cpp.o' -print -quit \
        | grep -q . || fail "ordinary .cpp object was not created for host_${type}"
    [[ -x "$PROBE_BUILD/host_${type}" ]] \
        || fail "host_${type} was not built with the newly added ordinary .cpp"
done
pass "new ordinary .cpp is automatically compiled to an object without editing CMakeLists.txt"
pass "all compiled sources use -Wall and -Werror"
cleanup_probe
trap - EXIT


CONN_PROBE_CPP="$ROOT/conn_probe.cpp"
CONN_PROBE_HPP="$ROOT/conn_probe.hpp"
CONN_PROBE_BUILD="$ROOT/.conn_probe_test_build"
cleanup_conn_probe() {
    rm -f "$CONN_PROBE_CPP" "$CONN_PROBE_HPP"
    rm -rf "$CONN_PROBE_BUILD"
}
trap cleanup_conn_probe EXIT
cleanup_conn_probe
cat > "$CONN_PROBE_HPP" <<'HPP'
#pragma once
#include "common.hpp"
#include <string>
class Conn {
public:
    Conn(Role, pid_t, SharedSync*);
    bool Read(void*, std::size_t);
    bool Write(const void*, std::size_t);
    bool Failed() const;
    std::string Error() const;
    ~Conn();
};
HPP
cat > "$CONN_PROBE_CPP" <<'CPP'
#include "conn_probe.hpp"
Conn::Conn(Role, pid_t, SharedSync*) {}
bool Conn::Read(void*, std::size_t) { return false; }
bool Conn::Write(const void*, std::size_t) { return true; }
bool Conn::Failed() const { return false; }
std::string Conn::Error() const { return {}; }
Conn::~Conn() = default;
CPP
cmake -S "$ROOT" -B "$CONN_PROBE_BUILD" -DCMAKE_BUILD_TYPE=Release >/tmp/os_lab2_conn_probe_config.log 2>&1 \
    || { cat /tmp/os_lab2_conn_probe_config.log; fail "configure with newly added conn_probe"; }
cmake --build "$CONN_PROBE_BUILD" --parallel --target host_probe >/tmp/os_lab2_conn_probe_build.log 2>&1 \
    || { cat /tmp/os_lab2_conn_probe_build.log; fail "automatic host_probe target"; }
[[ -x "$CONN_PROBE_BUILD/host_probe" ]] || fail "new conn_probe did not create host_probe"
pass "new conn_TYPE source automatically creates host_TYPE"
cleanup_conn_probe
trap - EXIT
rm -f /tmp/os_lab2_conn_probe_config.log /tmp/os_lab2_conn_probe_build.log


make clean >/dev/null
make >/tmp/os_lab2_make.log 2>&1 || { cat /tmp/os_lab2_make.log; fail "make from project root"; }
for type in mq fifo sock; do
    [[ -x "host_${type}" ]] || fail "make did not build host_${type}"
done
pass "make from project root builds all IPC variants"

printf '\nAll tests passed.\n'
