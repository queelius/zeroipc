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
        except:
            pass
    
    def teardown_method(self):
        """Clean up after each test."""
        try:
            Memory.unlink("/test_queue")
        except:
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
        """Test behavior when queue is full."""
        mem = Memory("/test_queue", size=1024*1024)
        queue = Queue(mem, "small_queue", capacity=3, dtype=np.int32)
        
        assert queue.push(1)
        assert queue.push(2)
        assert not queue.push(3)  # Should fail - circular buffer full (capacity-1 usable)
        
        assert queue.full()
        
        queue.pop()
        assert not queue.full()
        assert queue.push(3)
    
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