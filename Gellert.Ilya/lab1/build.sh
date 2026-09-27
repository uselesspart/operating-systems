#!/bin/bash

set -e

echo "==> Initializing the build via CMake..."

rm -rf build out
mkdir build
mkdir out

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF

echo "==> Compiling source files..."
cmake --build build --target disk_monitor -j$(nproc)

echo "==> Moving artifacts..."
cp build/src/disk_monitor out/

if [ -f build/disk-monitor.service ]; then
    cp build/disk-monitor.service out/
fi

echo "==> Cleaning up intermediate files..."
rm -rf build

echo "==> Build completed successfully!"
echo "Executable file: ./out/disk_monitor"
echo "Systemd unit: ./out/disk-monitor.service"