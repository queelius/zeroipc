#!/usr/bin/env python3
"""Simple interop test - Python side"""

import sys
sys.path.insert(0, '/home/spinoza/github/beta/zeroipc/python')

import numpy as np
import time
from zeroipc import Memory, Array, Queue

def main():
    print("=== Simple Python Interop Test ===")

    # Create shared memory
    mem = Memory("/simple_interop", 1024 * 1024)  # 1MB
    print(f"Created shared memory: {mem}")

    # Create float array
    arr = Array(mem, "float_array", capacity=10, dtype=np.float32)
    for i in range(10):
        arr[i] = float(i * 1.1)
    print(f"Created float array with {len(arr)} elements")
    print(f"Initial values: {arr[:5]}")

    # Create int queue
    queue = Queue(mem, "int_queue", capacity=20, dtype=np.int32)
    for i in range(5):
        queue.push(i * 10)
    print(f"Created queue with {queue.size()} elements")

    print("\nWaiting for C++ to modify...")
    time.sleep(2)

    # Check modifications
    print(f"\nAfter C++ modifications:")
    print(f"arr[0] = {arr[0]} (should be 99.99)")
    print(f"Queue size: {queue.size()}")

    # Pop all queue elements
    print("Queue contents:")
    while not queue.empty():
        val = queue.pop()
        print(f"  Popped: {val}")

    print("\nPython test complete")

if __name__ == "__main__":
    main()