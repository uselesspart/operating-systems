#!/bin/sh
# Сборка демона disk_monitor
#
# Использование:       ./build.sh или sh build.sh
# Другой компилятор:   CXX=clang++ ./build.sh

set -eu

cd "$(dirname "$0")"

CXX="${CXX:-g++}"
CXXFLAGS="-std=c++17 -O2 -Wall -Werror -Iinclude"
TARGET="disk_monitor"

if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "Ошибка: компилятор '$CXX' не найден. Установите его, например:" >&2
    echo "  Debian/Ubuntu:  sudo apt install g++" >&2
    echo "  Fedora/RHEL:    sudo dnf install gcc-c++" >&2
    echo "  Arch:           sudo pacman -S gcc" >&2
    exit 1
fi

OBJ_DIR="$(mktemp -d)"
trap 'rm -rf "$OBJ_DIR"' EXIT

for src in src/*.cpp; do
    obj="$OBJ_DIR/$(basename "${src%.cpp}").o"
    echo "  CXX   $src"
    $CXX $CXXFLAGS -c "$src" -o "$obj"
done

echo "  LINK  $TARGET"
$CXX $CXXFLAGS "$OBJ_DIR"/*.o -o "$TARGET"

echo "Готово: $(pwd)/$TARGET"
