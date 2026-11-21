#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf -- "$TEST_DIR"' EXIT
mkdir -p "$TEST_DIR/project" "$TEST_DIR/work"
cp "$PROJECT_DIR/Makefile" "$PROJECT_DIR/build.sh" "$PROJECT_DIR/CMakeLists.txt" "$TEST_DIR/project/"
cp -r "$PROJECT_DIR/src" "$TEST_DIR/project/"
export TMPDIR="$TEST_DIR/work"

cat > "$TEST_DIR/project/src/ordinary_probe.cpp" <<'CPP'
extern "C" int lab2_auto_source_probe() { return 42; }
CPP
make -C "$TEST_DIR/project" > "$TEST_DIR/build.log" 2>&1 || {
    cat "$TEST_DIR/build.log"
    exit 1
}
for role in host client; do
    for type in mmap shm pipe; do
        binary="$TEST_DIR/project/build/${role}_${type}"
        [[ -x "$binary" ]]
        nm "$binary" > "$TEST_DIR/symbols"
        grep -q ' T lab2_auto_source_probe$' "$TEST_DIR/symbols"
    done
done
[[ "$(find "$TEST_DIR/project/build" -mindepth 1 | wc -l)" -eq 6 ]]
[[ -z "$(find "$TMPDIR" -mindepth 1 -print -quit)" ]]
sha256sum "$TEST_DIR/project/build/"* > "$TEST_DIR/binaries.sha256"

echo '#error intentional build failure' > "$TEST_DIR/project/src/ordinary_probe.cpp"
# Invoke from another directory to verify path handling as well as failure cleanup.
if bash "$TEST_DIR/project/build.sh" > "$TEST_DIR/failure.log" 2>&1; then
    echo "FAIL: broken source compiled successfully"
    exit 1
fi
[[ -z "$(find "$TMPDIR" -mindepth 1 -print -quit)" ]]
sha256sum --check --status "$TEST_DIR/binaries.sha256"
echo "PASS: make, all six executables, automatic .cpp discovery, success/failure cleanup"
