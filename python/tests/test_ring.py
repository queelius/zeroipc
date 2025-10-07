"""
Test suite for lock-free ring buffer in shared memory.
"""

import os
import threading
import time
import pytest
import numpy as np

from zeroipc import Memory
from zeroipc.ring import Ring


class TestRingBasic:
    """Basic Ring buffer functionality tests."""

    def test_create_ring(self):
        """Test creating a new ring buffer."""
        shm_name = f"/test_ring_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Create ring with capacity 100
            ring = Ring(memory, "test_ring", capacity=100, dtype=np.int32)

            assert ring.name == "test_ring"
            assert ring.capacity == 100
            assert ring.size() == 0
            assert ring.empty() == True
            assert ring.full() == False

            # Test Ring with different name
            ring2 = Ring(memory, "ring2", capacity=50, dtype=np.float32)
            assert ring2.capacity == 50

        finally:
            Memory.unlink(shm_name)

    def test_push_pop(self):
        """Test basic push and pop operations."""
        shm_name = f"/test_ring_ops_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            ring = Ring(memory, "ops_ring", capacity=10, dtype=np.int32)

            # Push some values
            assert ring.push(10) == True
            assert ring.push(20) == True
            assert ring.push(30) == True

            assert ring.size() == 3
            assert ring.empty() == False

            # Pop values (FIFO order)
            assert ring.pop() == 10
            assert ring.pop() == 20
            assert ring.pop() == 30

            assert ring.size() == 0
            assert ring.empty() == True

        finally:
            Memory.unlink(shm_name)

    def test_front_back(self):
        """Test accessing front and back elements."""
        shm_name = f"/test_ring_access_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            ring = Ring(memory, "access_ring", capacity=10, dtype=np.int32)

            ring.push(10)
            ring.push(20)
            ring.push(30)

            # Front is the oldest (first to be popped)
            assert ring.front() == 10
            # Back is the newest (last pushed)
            assert ring.back() == 30

            # Pop doesn't affect back until we pop all
            ring.pop()
            assert ring.front() == 20
            assert ring.back() == 30

        finally:
            Memory.unlink(shm_name)

    def test_circular_behavior(self):
        """Test that ring buffer wraps around correctly."""
        shm_name = f"/test_ring_circular_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            capacity = 5
            ring = Ring(memory, "circular_ring", capacity=capacity, dtype=np.int32)

            # Fill the buffer
            for i in range(capacity):
                assert ring.push(i) == True

            assert ring.full() == True
            assert ring.size() == capacity

            # Pop one and push again
            assert ring.pop() == 0
            assert ring.push(capacity) == True

            # Should contain 1, 2, 3, 4, 5 now
            expected = list(range(1, capacity + 1))
            for exp in expected:
                assert ring.pop() == exp

        finally:
            Memory.unlink(shm_name)

    def test_full_buffer(self):
        """Test behavior when buffer is full."""
        shm_name = f"/test_ring_full_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            capacity = 3
            ring = Ring(memory, "full_ring", capacity=capacity, dtype=np.int32)

            # Fill buffer
            for i in range(capacity):
                assert ring.push(i) == True

            assert ring.full() == True

            # Try to push when full
            assert ring.push(99) == False

            # Pop one and push again
            ring.pop()
            assert ring.push(99) == True

        finally:
            Memory.unlink(shm_name)

    def test_clear(self):
        """Test clearing the ring buffer."""
        shm_name = f"/test_ring_clear_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            ring = Ring(memory, "clear_ring", capacity=10, dtype=np.int32)

            # Add some elements
            for i in range(5):
                ring.push(i)

            assert ring.size() == 5

            # Clear
            ring.clear()

            assert ring.size() == 0
            assert ring.empty() == True
            assert ring.pop() is None

        finally:
            Memory.unlink(shm_name)


class TestRingDataTypes:
    """Test Ring with different data types."""

    def test_float_ring(self):
        """Test ring buffer with float values."""
        shm_name = f"/test_ring_float_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            ring = Ring(memory, "float_ring", capacity=10, dtype=np.float32)

            values = [3.14, 2.718, 1.618, 0.577]
            for val in values:
                ring.push(val)

            for expected in values:
                actual = ring.pop()
                assert abs(actual - expected) < 0.001

        finally:
            Memory.unlink(shm_name)

    def test_int64_ring(self):
        """Test ring buffer with 64-bit integers."""
        shm_name = f"/test_ring_int64_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            ring = Ring(memory, "int64_ring", capacity=10, dtype=np.int64)

            large_val = 10**15
            ring.push(large_val)
            ring.push(large_val + 1)
            ring.push(large_val + 2)

            assert ring.pop() == large_val
            assert ring.pop() == large_val + 1
            assert ring.pop() == large_val + 2

        finally:
            Memory.unlink(shm_name)

    def test_uint8_ring(self):
        """Test ring buffer with unsigned 8-bit integers."""
        shm_name = f"/test_ring_uint8_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            ring = Ring(memory, "uint8_ring", capacity=256, dtype=np.uint8)

            # Test full range of uint8
            for i in range(256):
                ring.push(i)

            for i in range(256):
                assert ring.pop() == i

        finally:
            Memory.unlink(shm_name)


class TestRingConcurrency:
    """Test concurrent access to Ring buffer."""

    def test_concurrent_push_pop(self):
        """Test concurrent push and pop operations."""
        shm_name = f"/test_ring_concurrent_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            ring = Ring(memory, "concurrent_ring", capacity=1000, dtype=np.int32)

            pushed_values = []
            popped_values = []
            push_lock = threading.Lock()
            pop_lock = threading.Lock()

            def pusher(start, count):
                for i in range(count):
                    val = start + i
                    if ring.push(val):
                        with push_lock:
                            pushed_values.append(val)
                    time.sleep(0.0001)

            def popper(count):
                for _ in range(count):
                    val = ring.pop()
                    if val is not None:
                        with pop_lock:
                            popped_values.append(val)
                    time.sleep(0.0001)

            # Create threads
            push_threads = []
            pop_threads = []

            # Multiple pushers
            for i in range(3):
                t = threading.Thread(target=pusher, args=(i * 100, 50))
                push_threads.append(t)
                t.start()

            # Give pushers a head start
            time.sleep(0.01)

            # Multiple poppers
            for _ in range(2):
                t = threading.Thread(target=popper, args=(75,))
                pop_threads.append(t)
                t.start()

            # Wait for completion
            for t in push_threads:
                t.join()
            for t in pop_threads:
                t.join()

            # Verify no duplicates in popped values
            assert len(popped_values) == len(set(popped_values))

        finally:
            Memory.unlink(shm_name)

    def test_producer_consumer(self):
        """Test producer-consumer pattern."""
        shm_name = f"/test_ring_prodcons_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            ring = Ring(memory, "prodcons_ring", capacity=100, dtype=np.int32)

            produced = []
            consumed = []
            done = threading.Event()

            def producer():
                for i in range(50):
                    while not ring.push(i):
                        time.sleep(0.001)
                    produced.append(i)
                    time.sleep(0.001)
                done.set()

            def consumer():
                while not done.is_set() or not ring.empty():
                    val = ring.pop()
                    if val is not None:
                        consumed.append(val)
                    else:
                        time.sleep(0.001)

            # Start threads
            prod_thread = threading.Thread(target=producer)
            cons_thread = threading.Thread(target=consumer)

            prod_thread.start()
            cons_thread.start()

            prod_thread.join()
            cons_thread.join()

            # Verify all produced items were consumed
            assert sorted(consumed) == sorted(produced)

        finally:
            Memory.unlink(shm_name)


class TestRingEdgeCases:
    """Test edge cases and error conditions."""

    def test_empty_ring_operations(self):
        """Test operations on empty ring."""
        shm_name = f"/test_ring_empty_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            ring = Ring(memory, "empty_ring", capacity=10, dtype=np.int32)

            assert ring.pop() is None
            assert ring.front() is None
            assert ring.back() is None
            assert ring.empty() == True
            assert ring.full() == False
            assert ring.size() == 0

        finally:
            Memory.unlink(shm_name)

    def test_single_element(self):
        """Test ring with single element."""
        shm_name = f"/test_ring_single_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            ring = Ring(memory, "single_ring", capacity=10, dtype=np.int32)

            ring.push(42)

            assert ring.size() == 1
            assert ring.front() == 42
            assert ring.back() == 42

            assert ring.pop() == 42
            assert ring.empty() == True

        finally:
            Memory.unlink(shm_name)

    def test_zero_capacity_error(self):
        """Test that zero capacity raises error."""
        shm_name = f"/test_ring_zero_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            with pytest.raises(ValueError, match="capacity must be greater than 0"):
                Ring(memory, "zero_ring", capacity=0, dtype=np.int32)

        finally:
            Memory.unlink(shm_name)

    def test_missing_dtype_error(self):
        """Test that missing dtype raises error."""
        shm_name = f"/test_ring_dtype_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            with pytest.raises(TypeError, match="dtype is required"):
                Ring(memory, "bad_ring", capacity=10)

        finally:
            Memory.unlink(shm_name)

    def test_push_pop_many(self):
        """Test pushing and popping many elements."""
        shm_name = f"/test_ring_many_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            capacity = 100
            ring = Ring(memory, "many_ring", capacity=capacity, dtype=np.int32)

            # Push and pop many times the capacity
            total_ops = capacity * 10
            for i in range(total_ops):
                # Push a few
                for j in range(3):
                    if not ring.full():
                        ring.push(i * 3 + j)

                # Pop one
                if not ring.empty():
                    ring.pop()

            # Ring should still be functional
            ring.clear()
            ring.push(999)
            assert ring.pop() == 999

        finally:
            Memory.unlink(shm_name)


class TestRingPersistence:
    """Test ring buffer persistence across processes."""

    def test_reopen_ring(self):
        """Test reopening an existing ring buffer."""
        shm_name = f"/test_ring_persist_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Create and populate
            ring1 = Ring(memory, "persist_ring", capacity=50, dtype=np.int32)
            ring1.push(10)
            ring1.push(20)
            ring1.push(30)

            size_before = ring1.size()

            # Open existing ring
            ring2 = Ring(memory, "persist_ring", dtype=np.int32)

            # Should see same data
            assert ring2.size() == size_before
            assert ring2.pop() == 10
            assert ring2.pop() == 20
            assert ring2.pop() == 30

        finally:
            Memory.unlink(shm_name)


class TestRingStatistics:
    """Test ring buffer statistics and monitoring."""

    def test_capacity_utilization(self):
        """Test tracking capacity utilization."""
        shm_name = f"/test_ring_util_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            capacity = 10
            ring = Ring(memory, "util_ring", capacity=capacity, dtype=np.int32)

            # Test various fill levels
            for fill_level in [0, 3, 5, 7, 10]:
                ring.clear()
                for i in range(fill_level):
                    ring.push(i)

                utilization = ring.size() / capacity
                assert abs(utilization - fill_level/capacity) < 0.01

        finally:
            Memory.unlink(shm_name)

    def test_wrap_around_count(self):
        """Test counting wrap-arounds."""
        shm_name = f"/test_ring_wrap_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            capacity = 5
            ring = Ring(memory, "wrap_ring", capacity=capacity, dtype=np.int32)

            # Cause multiple wrap-arounds
            for cycle in range(3):
                for i in range(capacity):
                    ring.push(cycle * capacity + i)
                for _ in range(capacity):
                    ring.pop()

            # Ring should still work correctly
            ring.push(999)
            assert ring.pop() == 999

        finally:
            Memory.unlink(shm_name)