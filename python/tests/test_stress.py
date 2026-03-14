#!/usr/bin/env python3
"""
Comprehensive stress tests for Python ZeroIPC implementation.
Tests queue, stack, and array under high concurrency and edge cases.
"""

import sys
import os
import time
import threading
import multiprocessing
import numpy as np
import random

# Add parent directory to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from zeroipc import Memory, Queue, Stack, Array

# Test parameters
STRESS_THREADS = 16
ITEMS_PER_THREAD = 1000
QUEUE_SIZE = 100
SMALL_QUEUE_SIZE = 10

# ==================
# Queue Stress Tests
# ==================

def test_queue_basic_correctness():
    """Test Queue basic operations."""
    print("Testing Queue basic correctness...")
    
    mem = Memory("/test_queue_basic", size=10 * 1024 * 1024)
    q = Queue(mem, "test_queue", capacity=100, dtype=np.int32)
    
    # Test empty
    assert q.empty()
    assert q.pop() is None
    
    # Test push/pop single
    assert q.push(42)
    assert not q.empty()
    val = q.pop()
    assert val == 42
    assert q.empty()
    
    # Test fill to capacity (Vyukov uses all N slots)
    for i in range(100):
        assert q.push(i)
    assert q.full()
    assert not q.push(999)  # Should fail when full

    # Test drain
    for i in range(100):
        val = q.pop()
        assert val == i
    assert q.empty()
    
    # Clean up references before closing
    del q
    try:
        mem.close()
    except BufferError:
        pass  # Numpy arrays might hold references
    Memory.unlink("/test_queue_basic")
    print("  ✓ Queue basic correctness passed")


def test_queue_threading():
    """Test Queue with Python threading (GIL-limited concurrency)."""
    print("Testing Queue with threading...")
    
    mem = Memory("/test_queue_thread", size=100 * 1024 * 1024)
    q = Queue(mem, "thread_queue", capacity=QUEUE_SIZE, dtype=np.int32)
    
    produced = []
    consumed = []
    produced_lock = threading.Lock()
    consumed_lock = threading.Lock()
    
    def producer(thread_id):
        local_produced = []
        for i in range(ITEMS_PER_THREAD):
            value = thread_id * ITEMS_PER_THREAD + i
            while not q.push(value):
                time.sleep(0.0001)
            local_produced.append(value)
        with produced_lock:
            produced.extend(local_produced)
    
    def consumer():
        local_consumed = []
        for i in range(ITEMS_PER_THREAD):
            while True:
                value = q.pop()
                if value is not None:
                    local_consumed.append(value)
                    break
                time.sleep(0.0001)
        with consumed_lock:
            consumed.extend(local_consumed)
    
    threads = []
    
    # Half producers, half consumers
    for i in range(STRESS_THREADS // 2):
        t = threading.Thread(target=producer, args=(i,))
        threads.append(t)
        t.start()
    
    for i in range(STRESS_THREADS // 2):
        t = threading.Thread(target=consumer)
        threads.append(t)
        t.start()
    
    for t in threads:
        t.join()
    
    expected_count = (STRESS_THREADS // 2) * ITEMS_PER_THREAD
    assert len(produced) == expected_count
    assert len(consumed) == expected_count
    assert sum(produced) == sum(consumed)  # Checksum
    assert q.empty()
    
    try:
        mem.close()
    except BufferError:
        pass  # Numpy arrays might hold references
    Memory.unlink("/test_queue_thread")
    print(f"  ✓ Produced: {len(produced)}, Consumed: {len(consumed)}, Checksum verified")


def _mp_producer(queue_name, items_count):
    """Producer for multiprocessing test (must be top-level for pickling)."""
    mem = Memory("/test_queue_mp", size=0)
    q = Queue(mem, queue_name, dtype=np.int32)
    for i in range(items_count):
        while not q.push(i):
            time.sleep(0.0001)
    mem.close()


def _mp_consumer(queue_name, items_count, result_shm_name):
    """Consumer for multiprocessing test (must be top-level for pickling)."""
    mem = Memory("/test_queue_mp", size=0)
    q = Queue(mem, queue_name, dtype=np.int32)
    # Write results to a shared array so the parent can read them
    result_mem = Memory(result_shm_name, size=0)
    from zeroipc import Array
    results = Array(result_mem, "results", dtype=np.int32)
    for i in range(items_count):
        while True:
            value = q.pop()
            if value is not None:
                results[i] = int(value)
                break
            time.sleep(0.0001)
    mem.close()
    result_mem.close()


def test_queue_multiprocessing():
    """Test Queue with multiprocessing (SPSC: single-producer, single-consumer).

    Python cannot do true atomic CAS on shared memory, so MPMC across
    processes is not safe. SPSC works because producer only writes tail
    and consumer only writes head — no write contention on the same field.
    """
    print("Testing Queue with multiprocessing (SPSC)...")
    from zeroipc import Array

    num_items = 2000

    # Create shared memory for queue and results
    Memory.unlink("/test_queue_mp")
    Memory.unlink("/test_queue_mp_results")
    mem = Memory("/test_queue_mp", size=10 * 1024 * 1024)
    Queue(mem, "mp_queue", capacity=1000, dtype=np.int32)
    mem.close()

    result_mem = Memory("/test_queue_mp_results", size=1 * 1024 * 1024)
    Array(result_mem, "results", capacity=num_items, dtype=np.int32)
    result_mem.close()

    # Start both processes
    producer = multiprocessing.Process(
        target=_mp_producer, args=("mp_queue", num_items))
    consumer = multiprocessing.Process(
        target=_mp_consumer, args=("mp_queue", num_items, "/test_queue_mp_results"))

    producer.start()
    consumer.start()

    producer.join(timeout=30)
    consumer.join(timeout=30)

    assert not producer.is_alive(), "Producer timed out"
    assert not consumer.is_alive(), "Consumer timed out"

    # Read results
    result_mem = Memory("/test_queue_mp_results", size=0)
    results = Array(result_mem, "results", dtype=np.int32)
    consumed = [int(results[i]) for i in range(num_items)]
    result_mem.close()

    # Verify all items received in order (SPSC preserves FIFO)
    assert consumed == list(range(num_items))

    Memory.unlink("/test_queue_mp")
    Memory.unlink("/test_queue_mp_results")
    print(f"  ✓ Multiprocessing SPSC test completed ({num_items} items)")


# ==================
# Stack Stress Tests
# ==================

def test_stack_basic_correctness():
    """Test Stack basic operations."""
    print("Testing Stack basic correctness...")
    
    mem = Memory("/test_stack_basic", size=10 * 1024 * 1024)
    s = Stack(mem, "test_stack", capacity=100, dtype=np.int32)
    
    # Test empty
    assert s.empty()
    assert s.pop() is None
    
    # Test push/pop single
    assert s.push(42)
    assert not s.empty()
    val = s.pop()
    assert val == 42
    assert s.empty()
    
    # Test LIFO order
    for i in range(50):
        assert s.push(i)
    
    for i in range(49, -1, -1):
        val = s.pop()
        assert val == i
    assert s.empty()
    
    # Test fill to capacity
    for i in range(100):
        assert s.push(i)
    assert s.full()
    assert not s.push(999)  # Should fail when full
    
    try:
        mem.close()
    except BufferError:
        pass  # Numpy arrays might hold references
    Memory.unlink("/test_stack_basic")
    print("  ✓ Stack basic correctness passed")


def test_stack_threading():
    """Test Stack with threading."""
    print("Testing Stack with threading...")
    
    mem = Memory("/test_stack_thread", size=100 * 1024 * 1024)
    s = Stack(mem, "thread_stack", capacity=QUEUE_SIZE, dtype=np.int32)
    
    pushed = []
    popped = []
    pushed_lock = threading.Lock()
    popped_lock = threading.Lock()
    
    def pusher(thread_id):
        local_pushed = []
        for i in range(ITEMS_PER_THREAD):
            value = thread_id * ITEMS_PER_THREAD + i
            while not s.push(value):
                time.sleep(0.0001)
            local_pushed.append(value)
        with pushed_lock:
            pushed.extend(local_pushed)
    
    def popper():
        local_popped = []
        for i in range(ITEMS_PER_THREAD):
            while True:
                value = s.pop()
                if value is not None:
                    local_popped.append(value)
                    break
                time.sleep(0.0001)
        with popped_lock:
            popped.extend(local_popped)
    
    threads = []
    
    # Half pushers, half poppers
    for i in range(STRESS_THREADS // 2):
        t = threading.Thread(target=pusher, args=(i,))
        threads.append(t)
        t.start()
    
    for i in range(STRESS_THREADS // 2):
        t = threading.Thread(target=popper)
        threads.append(t)
        t.start()
    
    for t in threads:
        t.join()
    
    expected_count = (STRESS_THREADS // 2) * ITEMS_PER_THREAD
    assert len(pushed) == expected_count
    assert len(popped) == expected_count
    assert sum(pushed) == sum(popped)  # Checksum
    assert s.empty()
    
    try:
        mem.close()
    except BufferError:
        pass  # Numpy arrays might hold references
    Memory.unlink("/test_stack_thread")
    print(f"  ✓ Pushed: {len(pushed)}, Popped: {len(popped)}, Checksum verified")


# ==================
# Array Stress Tests
# ==================

def test_array_concurrent_access():
    """Test Array with concurrent read/write."""
    print("Testing Array concurrent access...")
    
    mem = Memory("/test_array_concurrent", size=10 * 1024 * 1024)
    arr = Array(mem, "concurrent_array", capacity=1000, dtype=np.int32)
    
    # Initialize array
    for i in range(1000):
        arr[i] = i
    
    errors = []
    
    def reader():
        for _ in range(1000):
            idx = random.randint(0, 999)
            val = arr[idx]
            if val != idx:
                errors.append(f"Read error at {idx}: expected {idx}, got {val}")
    
    def writer():
        for _ in range(1000):
            idx = random.randint(0, 999)
            arr[idx] = idx  # Always write expected value
    
    threads = []
    for i in range(8):
        t = threading.Thread(target=reader if i % 2 == 0 else writer)
        threads.append(t)
        t.start()
    
    for t in threads:
        t.join()
    
    assert len(errors) == 0, f"Concurrent access errors: {errors[:5]}"
    
    try:
        mem.close()
    except BufferError:
        pass  # Numpy arrays might hold references
    Memory.unlink("/test_array_concurrent")
    print("  ✓ Array concurrent access passed")


def test_array_large_data():
    """Test Array with large data types."""
    print("Testing Array with large data...")
    
    mem = Memory("/test_array_large", size=100 * 1024 * 1024)
    
    # Test with structured dtype
    dtype = np.dtype([
        ('id', np.int32),
        ('data', np.float64, (100,)),
        ('checksum', np.uint32)
    ])
    
    arr = Array(mem, "large_array", capacity=100, dtype=dtype)
    
    # Write structured data
    for i in range(100):
        record = np.zeros(1, dtype=dtype)[0]
        record['id'] = i
        record['data'][:] = np.arange(100) * i
        record['checksum'] = np.uint32(i * 0xDEADBEEF & 0xFFFFFFFF)
        arr[i] = record
    
    # Verify
    for i in range(100):
        record = arr[i]
        assert record['id'] == i
        assert record['checksum'] == np.uint32(i * 0xDEADBEEF & 0xFFFFFFFF)
        assert np.allclose(record['data'], np.arange(100) * i)
    
    try:
        mem.close()
    except BufferError:
        pass  # Numpy arrays might hold references
    Memory.unlink("/test_array_large")
    print("  ✓ Array large data passed")


# ==================
# Edge Cases
# ==================

def test_edge_cases():
    """Test edge cases and error conditions."""
    print("Testing edge cases...")
    
    # Test minimum size queue (Vyukov uses all N slots)
    mem = Memory("/test_edge_min", size=1 * 1024 * 1024)
    q = Queue(mem, "min_queue", capacity=2, dtype=np.int32)

    assert q.push(1)
    assert q.push(2)  # Vyukov uses all 2 slots
    assert not q.push(3)  # Now full
    assert q.pop() == 1
    assert q.pop() == 2
    assert q.empty()
    
    try:
        mem.close()
    except BufferError:
        pass  # Numpy arrays might hold references
    Memory.unlink("/test_edge_min")
    
    # Test rapid create/destroy
    for i in range(100):
        mem = Memory(f"/test_edge_rapid_{i}", size=1 * 1024 * 1024)
        q = Queue(mem, "rapid_queue", capacity=10, dtype=np.int32)
        q.push(i)
        assert q.pop() == i
        mem.close()
        Memory.unlink(f"/test_edge_rapid_{i}")
    
    # Test name collision
    mem = Memory("/test_edge_collision", size=10 * 1024 * 1024)
    q1 = Queue(mem, "queue1", capacity=10, dtype=np.int32)
    
    try:
        q2 = Queue(mem, "queue1", capacity=10, dtype=np.int32)  # Same name
        # Should either reuse or raise error
    except Exception:
        pass  # Expected if implementation prevents duplicates
    
    try:
        mem.close()
    except BufferError:
        pass  # Numpy arrays might hold references
    Memory.unlink("/test_edge_collision")
    
    print("  ✓ Edge cases passed")


# ==================
# Performance Test
# ==================

def test_performance():
    """Measure performance metrics."""
    print("Testing performance metrics...")
    
    mem = Memory("/test_perf", size=100 * 1024 * 1024)
    q = Queue(mem, "perf_queue", capacity=10000, dtype=np.int32)
    
    ops = 100000
    
    # Single-threaded throughput
    start = time.perf_counter()
    for i in range(ops):
        q.push(i)
    for i in range(ops):
        q.pop()
    end = time.perf_counter()
    
    duration = end - start
    ops_per_sec = (ops * 2) / duration
    print(f"  Single-thread: {ops_per_sec / 1000000:.2f} M ops/sec")
    
    # Multi-threaded throughput (limited by GIL)
    total_ops = 0
    start = time.perf_counter()
    
    def worker():
        nonlocal total_ops
        for i in range(ops // 8):
            if i % 2 == 0:
                while not q.push(i):
                    time.sleep(0.00001)
            else:
                while q.pop() is None:
                    time.sleep(0.00001)
            total_ops += 1
    
    threads = []
    for i in range(8):
        t = threading.Thread(target=worker)
        threads.append(t)
        t.start()
    
    for t in threads:
        t.join()
    
    end = time.perf_counter()
    duration = end - start
    ops_per_sec = total_ops / duration
    print(f"  Multi-thread (8): {ops_per_sec / 1000000:.2f} M ops/sec")
    
    try:
        mem.close()
    except BufferError:
        pass  # Numpy arrays might hold references
    Memory.unlink("/test_perf")


# ==================
# Main Test Runner
# ==================

def main():
    print("=== Python Comprehensive Stress Tests ===\n")
    
    # Queue tests
    print("Queue Tests:")
    test_queue_basic_correctness()
    test_queue_threading()
    # test_queue_multiprocessing()  # Skip - needs process coordination
    print()
    
    # Stack tests
    print("Stack Tests:")
    test_stack_basic_correctness()
    test_stack_threading()
    print()
    
    # Array tests
    print("Array Tests:")
    test_array_concurrent_access()
    test_array_large_data()
    print()
    
    # Advanced tests
    print("Advanced Tests:")
    test_edge_cases()
    test_performance()
    
    print("\n✓ All Python stress tests passed!")


if __name__ == "__main__":
    main()