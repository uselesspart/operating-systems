#!/bin/bash

echo "Building lab1 daemon..."
g++ -Wall -Werror -std=c++17 main.cpp -o lab1_daemon

if [ $? -eq 0 ]; then
    echo "Build successful."
else
    echo "Build failed."
    exit 1
fi

rm -f *.o