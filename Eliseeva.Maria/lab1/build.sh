set -e
cd "$(dirname "$0")"

OUT=daemon
OBJDIR=$(mktemp -d ./build_tmp.XXXXXX)
trap 'rm -rf "$OBJDIR"' EXIT

for src in src/*.cpp; do
    g++ -std=c++17 -Wall -Werror -Isrc -c "$src" -o "$OBJDIR/$(basename "${src%.cpp}").o"
done
g++ "$OBJDIR"/*.o -o "$OUT"

echo "Build OK: ./$OUT"
