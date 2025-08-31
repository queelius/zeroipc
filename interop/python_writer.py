#!/usr/bin/env python3
"""
Python writer for cross-language integration test.
Creates data for C++ reader.
"""

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'python'))

from zeroipc import Memory, Array
import numpy as np


def main():
    # Create shared memory with test data
    mem = Memory("/interop_test_py", 1024 * 1024)  # 1MB
    
    # Create an integer array (Fibonacci sequence)
    int_array = Array(mem, "fibonacci", capacity=10, dtype=np.int32)
    int_array[0] = 0
    int_array[1] = 1
    for i in range(2, 10):
        int_array[i] = int_array[i-1] + int_array[i-2]
    
    # Create a float array (reciprocals)
    float_array = Array(mem, "reciprocals", capacity=5, dtype=np.float32)
    for i in range(5):
        float_array[i] = 1.0 / (i + 1)
    
    # Create coordinates as structured array
    coord_dtype = np.dtype([('lat', 'f4'), ('lon', 'f4')])
    coords = Array(mem, "coordinates", capacity=3, dtype=coord_dtype)
    coords[0] = (37.7749, -122.4194)  # San Francisco
    coords[1] = (40.7128, -74.0060)   # New York
    coords[2] = (51.5074, -0.1278)    # London
    
    print("Python writer created:")
    print(f"  - fibonacci: {list(int_array.data)}")
    print(f"  - reciprocals: {list(float_array.data)}")
    print("  - coordinates: 3 lat/lon pairs")
    print("Shared memory name: /interop_test_py")
    
    # Don't close - numpy arrays hold references


if __name__ == "__main__":
    main()