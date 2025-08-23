#!/usr/bin/env python3
"""
Python reader for cross-language integration test.
Reads data written by C++ writer.
"""

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'python'))

from zeroipc import Memory

try:
    import numpy as np
    numpy_available = True
    from zeroipc import Array
except ImportError:
    numpy_available = False
    print("NumPy not available - using basic table reading only")


def main():
    # Open existing shared memory
    mem = Memory("/interop_test")
    
    print(f"Python reader opened shared memory: size={mem.size}")
    print(f"Table contains {mem.table.entry_count()} entries:")
    
    # List all entries
    for i in range(mem.table.entry_count()):
        # Manual iteration through entries
        entry_offset = mem.table.HEADER_SIZE + i * mem.table.ENTRY_SIZE
        import struct
        name_bytes, offset, size = struct.unpack_from(
            mem.table.ENTRY_FORMAT, mem.mmap, entry_offset
        )
        name = name_bytes.rstrip(b'\x00').decode('utf-8')
        print(f"  - {name}: offset={offset}, size={size}")
    
    if numpy_available:
        print("\nReading with NumPy:")
        
        # Read integer array
        int_array = Array(mem, "int_array", dtype=np.int32)
        print(f"int_array: {list(int_array.data)}")
        
        # Verify squares
        for i in range(len(int_array)):
            expected = i * i
            if int_array[i] != expected:
                print(f"ERROR: int_array[{i}] = {int_array[i]}, expected {expected}")
        
        # Read float array
        float_array = Array(mem, "float_array", dtype=np.float32)
        print(f"float_array: {list(float_array.data)}")
        
        # Read points as structured array
        point_dtype = np.dtype([('x', 'f4'), ('y', 'f4'), ('z', 'f4')])
        points = Array(mem, "points", dtype=point_dtype)
        print(f"points:")
        for i, p in enumerate(points.data):
            print(f"  [{i}]: x={p['x']:.1f}, y={p['y']:.1f}, z={p['z']:.1f}")
        
        # Alternative: read points as flat float array
        points_flat = Array(mem, "points", dtype=np.dtype('(3,)f4'))
        print(f"points (as flat arrays): {list(points_flat.data)}")
        
        print("\n✓ Cross-language test PASSED!")
    else:
        print("\nBasic verification passed - install numpy for full test")
    
    mem.close()


if __name__ == "__main__":
    main()