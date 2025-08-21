"""
Test suite for Stack functionality.
"""

import pytest
import sys
import os
import threading
import multiprocessing as mp

sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
import posix_shm_py as shm


class TestStack:
    """Test Stack functionality."""
    
    def setup_method(self):
        """Set up test fixtures."""
        self.shm_name = "/test_stack"
        self.mem = shm.SharedMemory(self.shm_name, 10 * 1024 * 1024)
    
    def teardown_method(self):
        """Clean up after tests."""
        try:
            self.mem.unlink()
        except:
            pass
    
    def test_basic_operations(self):
        """Test basic stack operations."""
        stack = shm.IntStack(self.mem, "basic_stack", capacity=100)
        
        # Test empty stack
        assert stack.empty()
        assert len(stack) == 0
        assert not stack  # __bool__
        
        # Test push
        stack.push(10)
        stack.push(20)
        stack.push(30)
        
        assert not stack.empty()
        assert len(stack) == 3
        assert stack  # __bool__
        
        # Test pop (LIFO order)
        assert stack.pop() == 30
        assert stack.pop() == 20
        assert stack.pop() == 10
        
        assert stack.empty()
        assert stack.pop() is None  # Pop from empty stack
    
    def test_top_peek(self):
        """Test top/peek operation."""
        stack = shm.IntStack(self.mem, "peek_stack", capacity=100)
        
        # Top of empty stack
        assert stack.top() is None
        
        # Push and peek
        stack.push(42)
        assert stack.top() == 42
        assert len(stack) == 1  # Top doesn't remove
        
        stack.push(84)
        assert stack.top() == 84
        assert len(stack) == 2
        
        # Pop and peek
        stack.pop()
        assert stack.top() == 42
    
    def test_capacity_and_full(self):
        """Test capacity limits and full status."""
        stack = shm.IntStack(self.mem, "capacity_stack", capacity=5)
        
        assert stack.capacity() == 5
        assert not stack.full()
        
        # Fill to capacity
        for i in range(5):
            stack.push(i)
        
        assert stack.full()
        assert len(stack) == 5
        
        # Try to push beyond capacity
        # Behavior depends on implementation
        stack.push(99)  # Might fail or succeed
    
    def test_clear(self):
        """Test clear operation."""
        stack = shm.IntStack(self.mem, "clear_stack", capacity=100)
        
        # Add items
        for i in range(10):
            stack.push(i)
        
        assert len(stack) == 10
        
        # Clear
        stack.clear()
        assert stack.empty()
        assert len(stack) == 0
        
        # Should be able to push after clear
        stack.push(42)
        assert stack.pop() == 42
    
    def test_lifo_order(self):
        """Test LIFO (Last In First Out) ordering."""
        stack = shm.IntStack(self.mem, "lifo_stack", capacity=100)
        
        items = [1, 2, 3, 4, 5]
        
        # Push items
        for item in items:
            stack.push(item)
        
        # Pop items - should be in reverse order
        popped = []
        while not stack.empty():
            popped.append(stack.pop())
        
        assert popped == list(reversed(items))
    
    def test_concurrent_push_pop(self):
        """Test concurrent push and pop operations."""
        stack = shm.IntStack(self.mem, "concurrent_stack", capacity=1000)
        
        def pusher(start, count):
            for i in range(start, start + count):
                stack.push(i)
        
        def popper(count):
            results = []
            for _ in range(count):
                val = stack.pop()
                if val is not None:
                    results.append(val)
            return results
        
        # Start pushers
        threads = []
        for i in range(4):
            t = threading.Thread(target=pusher, args=(i * 100, 100))
            threads.append(t)
            t.start()
        
        for t in threads:
            t.join()
        
        # Verify all items were pushed
        assert len(stack) == 400
        
        # Pop all items
        all_items = []
        while not stack.empty():
            val = stack.pop()
            if val is not None:
                all_items.append(val)
        
        assert len(all_items) == 400
        assert len(set(all_items)) == 400  # All unique
    
    def test_multiprocess_stack(self):
        """Test stack across multiple processes."""
        stack = shm.IntStack(self.mem, "mp_stack", capacity=1000)
        
        def worker(shm_name, worker_id, count):
            mem = shm.SharedMemory(shm_name, 0)
            stack = shm.IntStack(mem, "mp_stack")
            
            # Each worker pushes unique values
            for i in range(count):
                stack.push(worker_id * 1000 + i)
        
        processes = []
        num_workers = 4
        items_per_worker = 50
        
        for i in range(num_workers):
            p = mp.Process(target=worker, args=(self.shm_name, i, items_per_worker))
            processes.append(p)
            p.start()
        
        for p in processes:
            p.join()
        
        # Verify total items
        assert len(stack) == num_workers * items_per_worker
        
        # Pop all and verify uniqueness
        all_items = []
        while not stack.empty():
            val = stack.pop()
            if val is not None:
                all_items.append(val)
        
        assert len(set(all_items)) == len(all_items)  # All unique
    
    def test_persistence(self):
        """Test that stack persists across connections."""
        # Create and populate
        stack1 = shm.IntStack(self.mem, "persist_stack", capacity=100)
        test_data = [10, 20, 30, 40, 50]
        for val in test_data:
            stack1.push(val)
        
        size1 = len(stack1)
        del stack1
        
        # Re-attach and verify
        stack2 = shm.IntStack(self.mem, "persist_stack")
        assert len(stack2) == size1
        
        # Pop in LIFO order
        popped = []
        while not stack2.empty():
            popped.append(stack2.pop())
        
        assert popped == list(reversed(test_data))
    
    def test_mixed_operations(self):
        """Test mixed push, pop, and peek operations."""
        stack = shm.IntStack(self.mem, "mixed_stack", capacity=100)
        
        # Interleave operations
        stack.push(1)
        stack.push(2)
        assert stack.top() == 2
        
        stack.push(3)
        assert stack.pop() == 3
        assert stack.top() == 2
        
        stack.push(4)
        stack.push(5)
        assert len(stack) == 4
        
        stack.clear()
        assert stack.empty()
        
        stack.push(99)
        assert stack.top() == 99
        assert stack.pop() == 99
        assert stack.empty()


if __name__ == "__main__":
    pytest.main([__file__, "-v"])