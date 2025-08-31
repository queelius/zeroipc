"""Tests for Stack implementation."""

import pytest
import numpy as np
import threading
from zeroipc import Memory, Stack


class TestStack:
    """Test Stack functionality."""
    
    def setup_method(self):
        """Clean up before each test."""
        try:
            Memory.unlink("/test_stack")
        except:
            pass
    
    def teardown_method(self):
        """Clean up after each test."""
        try:
            Memory.unlink("/test_stack")
        except:
            pass
    
    def test_create_and_basic_ops(self):
        """Test stack creation and basic operations."""
        mem = Memory("/test_stack", size=1024*1024)
        stack = Stack(mem, "int_stack", capacity=100, dtype=np.int32)
        
        assert stack.empty()
        assert not stack.full()
        assert stack.size() == 0
        assert len(stack) == 0
        
        # Push some values
        assert stack.push(10)
        assert stack.push(20)
        assert stack.push(30)
        
        assert not stack.empty()
        assert stack.size() == 3
        assert bool(stack)  # Should be truthy when not empty
        
        # Check top
        assert stack.top() == 30
        assert stack.size() == 3  # Top doesn't remove
        
        # Pop values (LIFO order)
        assert stack.pop() == 30
        assert stack.pop() == 20
        assert stack.pop() == 10
        
        assert stack.empty()
        assert stack.pop() is None
        assert stack.top() is None
    
    def test_full_stack(self):
        """Test behavior when stack is full."""
        mem = Memory("/test_stack", size=1024*1024)
        stack = Stack(mem, "small_stack", capacity=3, dtype=np.int32)
        
        assert stack.push(1)
        assert stack.push(2)
        assert stack.push(3)
        assert not stack.push(4)  # Should fail - stack full
        
        assert stack.full()
        
        stack.pop()
        assert not stack.full()
        assert stack.push(4)
    
    def test_open_existing(self):
        """Test opening an existing stack."""
        mem = Memory("/test_stack", size=1024*1024)
        
        # Create and populate stack
        stack1 = Stack(mem, "double_stack", capacity=50, dtype=np.float64)
        stack1.push(3.14)
        stack1.push(2.71)
        stack1.push(1.41)
        
        # Open same stack
        stack2 = Stack(mem, "double_stack", dtype=np.float64)
        assert stack2.size() == 3
        
        # Pop in LIFO order
        assert pytest.approx(stack2.pop()) == 1.41
        assert pytest.approx(stack2.pop()) == 2.71
        assert pytest.approx(stack2.pop()) == 3.14
    
    def test_struct_type(self):
        """Test stack with structured dtype."""
        mem = Memory("/test_stack", size=1024*1024)
        
        # Define a structured dtype
        point_dtype = np.dtype([('x', 'f4'), ('y', 'f4'), ('z', 'f4')])
        stack = Stack(mem, "point_stack", capacity=10, dtype=point_dtype)
        
        # Push structured data
        point1 = np.array((1.0, 2.0, 3.0), dtype=point_dtype)
        point2 = np.array((4.0, 5.0, 6.0), dtype=point_dtype)
        
        assert stack.push(point1)
        assert stack.push(point2)
        
        # Pop and verify (LIFO)
        p = stack.pop()
        assert p['x'] == 4.0
        assert p['y'] == 5.0
        assert p['z'] == 6.0
    
    def test_concurrent_push_pop(self):
        """Test concurrent push/pop operations."""
        mem = Memory("/test_stack", size=10*1024*1024)
        stack = Stack(mem, "concurrent_stack", capacity=10000, dtype=np.int32)
        
        num_threads = 4
        items_per_thread = 1000
        
        def worker(thread_id):
            # Push phase
            for i in range(items_per_thread):
                value = thread_id * items_per_thread + i
                while not stack.push(value):
                    pass  # Spin until successful
            
            # Pop phase (half the items)
            for i in range(items_per_thread // 2):
                while stack.pop() is None:
                    if stack.empty():
                        break
        
        # Run workers in parallel
        threads = []
        for t in range(num_threads):
            thread = threading.Thread(target=worker, args=(t,))
            threads.append(thread)
            thread.start()
        
        for thread in threads:
            thread.join()
        
        # Should have half the items left
        assert stack.size() == num_threads * items_per_thread // 2
        
        # Pop remaining
        count = 0
        while stack.pop() is not None:
            count += 1
        assert count == num_threads * items_per_thread // 2
        assert stack.empty()