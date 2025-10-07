#!/bin/bash

# Simple interop test between Python and C++

echo "==================================="
echo "  Simple Python-C++ Interop Test  "
echo "==================================="
echo

# Clean up any existing shared memory
rm -f /dev/shm/simple_interop 2>/dev/null

# Build C++ test
echo "Building C++ test..."
g++ -std=c++23 -I../cpp/include -o simple_test simple_test.cpp -lrt -pthread
if [ $? -ne 0 ]; then
    echo "Failed to build C++ test"
    exit 1
fi
echo "✓ C++ test built"
echo

# Run Python to create structures
echo "Starting Python (creates structures)..."
python3 simple_test.py &
PYTHON_PID=$!

# Wait a moment for Python to create structures
sleep 1

# Run C++ to modify structures
echo -e "\nRunning C++ (modifies structures)..."
./simple_test

# Wait for Python to finish
wait $PYTHON_PID

# Clean up
rm -f /dev/shm/simple_interop
rm -f simple_test

echo -e "\n✓ Test complete!"