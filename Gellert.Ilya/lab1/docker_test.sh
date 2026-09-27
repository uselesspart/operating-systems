#!/bin/bash

set -e

echo "==> Building Docker-image..."
docker build -q -t disk_monitor_env -f tests/Dockerfile .

echo "==> Running full test cycle in isolated container..."
echo "==============================================================="

docker run --rm -w /app -v "$(pwd):/app" disk_monitor_env bash -c "
    rsyslogd && \
    echo '>>> Building Project <<<' && \
    ./build.sh && \
    echo -e '\n>>> Unit Tests <<<' && \
    ./run_tests.sh && \
    echo -e '\n>>> Integration Test <<<' && \
    ./tests/integration_test.sh
"

echo "================================================================"
echo "==> All tests passed successfully in isolated container!"
