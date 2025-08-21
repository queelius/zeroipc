"""
Test suite for SharedMemory core functionality.
"""

import pytest
import sys
import os

# Add parent directory to path
sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))

import posix_shm_py as shm


class TestSharedMemory:
    """Test SharedMemory core functionality."""
    
    def setup_method(self):
        """Clean up any existing shared memory before each test."""
        self.test_names = []
        
    def teardown_method(self):
        """Clean up shared memory after each test."""
        for name in self.test_names:
            try:
                mem = shm.SharedMemory(name, 0)
                mem.unlink()
            except:
                pass
    
    def _register_shm(self, name):
        """Register a shared memory name for cleanup."""
        self.test_names.append(name)
        return name
    
    def test_create_shared_memory(self):
        """Test creating shared memory segments."""
        name = self._register_shm("/test_create")
        
        # Create shared memory
        mem = shm.SharedMemory(name, 1024 * 1024)
        assert mem is not None
        
        # Verify it exists by attaching from another instance
        mem2 = shm.SharedMemory(name, 0)
        assert mem2 is not None
    
    def test_repr(self):
        """Test string representation."""
        name = self._register_shm("/test_repr")
        mem = shm.SharedMemory(name, 1024)
        
        repr_str = repr(mem)
        assert repr_str == "<SharedMemory>"
    
    def test_unlink(self):
        """Test unlinking shared memory."""
        name = self._register_shm("/test_unlink")
        
        # Create and unlink
        mem = shm.SharedMemory(name, 1024)
        mem.unlink()
        
        # Should be able to create new one with same name
        mem2 = shm.SharedMemory(name, 2048)
        assert mem2 is not None
    
    def test_multiple_segments(self):
        """Test creating multiple shared memory segments."""
        names = [
            self._register_shm("/test_multi_1"),
            self._register_shm("/test_multi_2"),
            self._register_shm("/test_multi_3")
        ]
        
        segments = []
        for name in names:
            mem = shm.SharedMemory(name, 1024 * 1024)
            segments.append(mem)
        
        assert len(segments) == 3
        
        # Each should be independent
        for i, seg in enumerate(segments):
            queue = shm.IntQueue(seg, "queue", capacity=10)
            queue.enqueue(i)
            assert queue.dequeue() == i
    
    def test_large_segment(self):
        """Test creating large shared memory segment."""
        name = self._register_shm("/test_large")
        
        # Create 100MB segment
        mem = shm.SharedMemory(name, 100 * 1024 * 1024)
        assert mem is not None
        
        # Should be able to create large data structures
        queue = shm.IntQueue(mem, "large_queue", capacity=10000)
        assert queue is not None
    
    def test_invalid_operations(self):
        """Test error handling for invalid operations."""
        # Names without leading slash should work (added automatically in C++)
        mem = shm.SharedMemory("test_no_slash", 1024)
        self.test_names.append("/test_no_slash")
        assert mem is not None
    
    def test_persistence(self):
        """Test that data persists across connections."""
        name = self._register_shm("/test_persist")
        
        # Process 1: Create and write data
        mem1 = shm.SharedMemory(name, 10 * 1024 * 1024)
        queue1 = shm.IntQueue(mem1, "persist_queue", capacity=100)
        for i in range(10):
            queue1.enqueue(i)
        del queue1
        del mem1
        
        # Process 2: Attach and read data
        mem2 = shm.SharedMemory(name, 0)  # Attach mode
        queue2 = shm.IntQueue(mem2, "persist_queue")
        
        results = []
        while not queue2.empty():
            results.append(queue2.dequeue())
        
        assert results == list(range(10))


if __name__ == "__main__":
    pytest.main([__file__, "-v"])