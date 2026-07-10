"""Tests for Queue implementation."""

import pytest
import numpy as np
import threading
import time
from zeroipc import Memory, Queue


class TestQueue:
    """Test Queue functionality."""
    
    def setup_method(self):
        """Clean up before each test."""
        try:
            Memory.unlink("/test_queue")
        except FileNotFoundError:
            pass

    def teardown_method(self):
        """Clean up after each test."""
        try:
            Memory.unlink("/test_queue")
        except FileNotFoundError:
            pass
    
    def test_create_and_basic_ops(self):
        """Test queue creation and basic operations."""
        mem = Memory("/test_queue", size=1024*1024)
        queue = Queue(mem, "int_queue", capacity=100, dtype=np.int32)
        
        assert queue.empty()
        assert not queue.full()
        assert queue.size() == 0
        assert len(queue) == 0
        
        # Push some values
        assert queue.push(10)
        assert queue.push(20)
        assert queue.push(30)
        
        assert not queue.empty()
        assert queue.size() == 3
        assert bool(queue)  # Should be truthy when not empty
        
        # Pop values (FIFO order)
        assert queue.pop() == 10
        assert queue.pop() == 20
        assert queue.pop() == 30
        
        assert queue.empty()
        assert queue.pop() is None
    
    def test_full_queue(self):
        """Test behavior when queue is full (Vyukov uses all N slots)."""
        mem = Memory("/test_queue", size=1024*1024)
        # Request 3, get 4 (power-of-two rounding for wrap-safety)
        queue = Queue(mem, "small_queue", capacity=3, dtype=np.int32)
        assert queue.capacity == 4

        assert queue.push(1)
        assert queue.push(2)
        assert queue.push(3)
        assert queue.push(4)  # Vyukov uses all N slots
        assert not queue.push(5)  # Should fail - queue full

        assert queue.full()

        queue.pop()
        assert not queue.full()
        assert queue.push(5)

    def test_capacity_rounds_up_to_power_of_two(self):
        """Requested capacities round up so the slot mapping survives the
        2^32 counter wraparound (capacity must divide 2^32)."""
        mem = Memory("/test_queue", size=1024*1024)
        assert Queue(mem, "q_p2_1", capacity=1, dtype=np.int32).capacity == 1
        assert Queue(mem, "q_p2_2", capacity=2, dtype=np.int32).capacity == 2
        assert Queue(mem, "q_p2_5", capacity=5, dtype=np.int32).capacity == 8
        assert Queue(mem, "q_p2_1000", capacity=1000, dtype=np.int32).capacity == 1024

    def test_wraparound_at_2_32(self):
        """Regression for the 2^32 counter wraparound: seed head/tail just
        below UINT32_MAX (with matching seq[pos % cap] = pos) and stream
        elements across the boundary in FIFO order."""
        import struct

        mem = Memory("/test_queue", size=1024*1024)
        cap = 8
        queue = Queue(mem, "wrap32_queue", capacity=cap, dtype=np.uint32)
        assert queue.capacity == cap

        entry = mem.table.find("wrap32_queue")
        base = entry.offset
        seq_base = base + 16 + ((4 * cap + 7) & ~7)

        # Position both counters 4 increments before the wrap.
        t0 = 0xFFFFFFFC
        struct.pack_into("<I", mem.data, base, t0)      # head
        struct.pack_into("<I", mem.data, base + 4, t0)  # tail
        for k in range(cap):
            pos = (t0 + k) & 0xFFFFFFFF  # wraps through 0
            struct.pack_into("<I", mem.data, seq_base + (pos % cap) * 4, pos)

        # Stream 3 full generations through the queue, crossing the wrap.
        next_in = next_out = 0
        for _ in range(3):
            for _ in range(cap):
                assert queue.push(next_in)
                next_in += 1
            assert queue.full()
            for _ in range(cap):
                assert queue.pop() == next_out, "FIFO order broken at wrap"
                next_out += 1
            assert queue.empty()

        tail = struct.unpack_from("<I", mem.data, base + 4)[0]
        assert tail < t0  # counters wrapped past zero
    
    def test_circular_wrap(self):
        """Test circular buffer wrapping."""
        mem = Memory("/test_queue", size=1024*1024)
        queue = Queue(mem, "wrap_queue", capacity=5, dtype=np.int32)
        
        # Fill queue
        for i in range(4):
            assert queue.push(i)
        
        # Pop some
        assert queue.pop() == 0
        assert queue.pop() == 1
        
        # Push more (wrapping around)
        assert queue.push(4)
        assert queue.push(5)
        
        # Verify order
        assert queue.pop() == 2
        assert queue.pop() == 3
        assert queue.pop() == 4
        assert queue.pop() == 5
        assert queue.empty()
    
    def test_open_existing(self):
        """Test opening an existing queue."""
        mem = Memory("/test_queue", size=1024*1024)
        
        # Create and populate queue
        queue1 = Queue(mem, "float_queue", capacity=50, dtype=np.float32)
        queue1.push(3.14)
        queue1.push(2.71)
        
        # Open same queue
        queue2 = Queue(mem, "float_queue", dtype=np.float32)
        assert queue2.size() == 2
        
        val = queue2.pop()
        assert pytest.approx(val) == 3.14
    
    def test_struct_type(self):
        """Test queue with structured dtype."""
        mem = Memory("/test_queue", size=1024*1024)
        
        # Define a structured dtype
        point_dtype = np.dtype([('x', 'f4'), ('y', 'f4'), ('z', 'f4')])
        queue = Queue(mem, "point_queue", capacity=10, dtype=point_dtype)
        
        # Push structured data
        point1 = np.array((1.0, 2.0, 3.0), dtype=point_dtype)
        point2 = np.array((4.0, 5.0, 6.0), dtype=point_dtype)
        
        assert queue.push(point1)
        assert queue.push(point2)
        
        # Pop and verify
        p = queue.pop()
        assert p['x'] == 1.0
        assert p['y'] == 2.0
        assert p['z'] == 3.0
    
    def test_concurrent_access(self):
        """Test concurrent push/pop operations."""
        mem = Memory("/test_queue", size=10*1024*1024)
        queue = Queue(mem, "concurrent_queue", capacity=1000, dtype=np.int32)
        
        num_items = 1000
        produced = []
        consumed = []
        
        def producer():
            for i in range(num_items):
                while not queue.push(i):
                    time.sleep(0.0001)
                produced.append(i)
        
        def consumer():
            count = 0
            while count < num_items:
                val = queue.pop()
                if val is not None:
                    consumed.append(val)
                    count += 1
                else:
                    time.sleep(0.0001)
        
        # Run producer and consumer in parallel
        t1 = threading.Thread(target=producer)
        t2 = threading.Thread(target=consumer)
        
        t1.start()
        t2.start()
        
        t1.join()
        t2.join()
        
        assert len(produced) == num_items
        assert len(consumed) == num_items
        assert sum(produced) == sum(consumed)
        assert queue.empty()