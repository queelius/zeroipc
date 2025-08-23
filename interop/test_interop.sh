#!/bin/bash
# Cross-language integration test

set -e

echo "=== Cross-Language Integration Test ==="
echo

# Build C++ writer
echo "Building C++ writer..."
g++ -std=c++23 -I../cpp/include cpp_writer.cpp -o cpp_writer -lrt

# Run C++ writer
echo "Running C++ writer..."
./cpp_writer
echo

# Run Python reader
echo "Running Python reader..."
python3 python_reader.py
echo

# Cleanup
echo "Cleaning up shared memory..."
rm -f /dev/shm/interop_test

echo "=== Test Complete ==="