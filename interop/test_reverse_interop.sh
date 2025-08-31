#!/bin/bash
# Reverse cross-language integration test (Python writes, C++ reads)

set -e

echo "=== Reverse Cross-Language Integration Test ==="
echo

# Run Python writer
echo "Running Python writer..."
python3 python_writer.py
echo

# Build C++ reader
echo "Building C++ reader..."
g++ -std=c++23 -I../cpp/include cpp_reader.cpp -o cpp_reader -lrt

# Run C++ reader
echo "Running C++ reader..."
./cpp_reader
echo

# Cleanup
echo "Cleaning up shared memory..."
rm -f /dev/shm/interop_test_py

echo "=== Test Complete ==="