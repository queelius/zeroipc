#!/usr/bin/env python3
"""Quick test of the Python bindings."""

import sys
import os
sys.path.insert(0, os.path.dirname(__file__))

import posix_shm_py as shm

def test_basic():
    """Test basic functionality."""
    print("Testing Python bindings for posix_shm...")
    
    # Create shared memory
    print("\n1. Creating shared memory segment...")
    mem = shm.SharedMemory("/test_python", 10 * 1024 * 1024)  # 10MB
    print(f"   Created: {mem}")
    
    # Test Queue
    print("\n2. Testing IntQueue...")
    queue = shm.IntQueue(mem, "test_queue", capacity=100)
    queue.enqueue(42)
    queue.enqueue(43)
    print(f"   Queue size: {len(queue)}")
    print(f"   Dequeue: {queue.dequeue()}")
    print(f"   Dequeue: {queue.dequeue()}")
    
    # Test Stack
    print("\n3. Testing IntStack...")
    stack = shm.IntStack(mem, "test_stack", capacity=100)
    stack.push(10)
    stack.push(20)
    print(f"   Stack size: {len(stack)}")
    print(f"   Pop: {stack.pop()}")
    print(f"   Pop: {stack.pop()}")
    
    # Test HashMap
    print("\n4. Testing IntFloatMap...")
    map = shm.IntFloatMap(mem, "test_map", capacity=100)
    map[1] = 3.14
    map[2] = 2.71
    print(f"   Map size: {len(map)}")
    print(f"   map[1] = {map[1]}")
    print(f"   map[2] = {map[2]}")
    print(f"   1 in map: {1 in map}")
    del map[1]
    print(f"   After delete, 1 in map: {1 in map}")
    
    # Test Set
    print("\n5. Testing IntSet...")
    set1 = shm.IntSet(mem, "test_set", capacity=100)
    set1.insert(1)
    set1.insert(2)
    set1.insert(3)
    print(f"   Set size: {len(set1)}")
    print(f"   1 in set: {1 in set1}")
    print(f"   4 in set: {4 in set1}")
    
    # Test Bitset
    print("\n6. Testing Bitset1024...")
    bits = shm.Bitset1024(mem, "test_bits")
    bits[10] = True
    bits[20] = True
    print(f"   Bitset size: {len(bits)}")
    print(f"   bits[10] = {bits[10]}")
    print(f"   bits[15] = {bits[15]}")
    print(f"   Count of set bits: {bits.count()}")
    
    # Test Array
    print("\n7. Testing FloatArray...")
    arr = shm.FloatArray(mem, "test_array", 10)
    arr[0] = 1.23
    arr[1] = 4.56
    print(f"   Array size: {len(arr)}")
    print(f"   arr[0] = {arr[0]}")
    print(f"   arr[1] = {arr[1]}")
    
    # Test Atomic
    print("\n8. Testing AtomicInt...")
    counter = shm.AtomicInt(mem, "test_counter", 0)
    counter.fetch_add(10)
    counter.fetch_add(5)
    print(f"   Counter value: {counter.load()}")
    
    # Clean up
    print("\n9. Cleaning up...")
    mem.unlink()
    print("   Shared memory unlinked")
    
    print("\n✅ All tests passed!")

if __name__ == "__main__":
    test_basic()