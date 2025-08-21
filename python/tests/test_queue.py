"""
Test suite for SharedMemory Queue.
"""

import pytest
import multiprocessing as mp
from posix_shm import SharedMemory, Queue
from posix_shm.helpers import shared_memory_context


class TestQueue:
    """Test Queue functionality."""
    
    def test_basic_operations(self):
        """Test basic push/pop operations."""
        with shared_memory_context("/test_queue", 1024*1024) as shm:
            queue = Queue(shm, "test", max_size=10)
            
            # Test empty queue
            assert queue.empty()
            assert queue.size() == 0
            
            # Test push
            queue.push(42)
            assert not queue.empty()
            assert queue.size() == 1
            
            # Test pop
            value = queue.pop()
            assert value == 42
            assert queue.empty()
    
    def test_fifo_order(self):
        """Test FIFO ordering."""
        with shared_memory_context("/test_fifo", 1024*1024) as shm:
            queue = Queue(shm, "fifo", max_size=100)
            
            # Push multiple items
            items = list(range(10))
            for item in items:
                queue.push(item)
            
            # Pop and verify order
            popped = []
            while not queue.empty():
                popped.append(queue.pop())
            
            assert popped == items
    
    def test_full_queue(self):
        """Test behavior when queue is full."""
        with shared_memory_context("/test_full", 1024*1024) as shm:
            queue = Queue(shm, "full", max_size=3)
            
            # Fill queue
            queue.push(1)
            queue.push(2)
            queue.push(3)
            
            assert queue.full()
            
            # Try to push to full queue
            with pytest.raises(RuntimeError):
                queue.push(4)
    
    def test_multiprocess_producer_consumer(self):
        """Test queue with multiple processes."""
        def producer(name, start, count):
            shm = SharedMemory(name, 0)  # Attach
            queue = Queue(shm, "mp_queue")
            
            for i in range(start, start + count):
                queue.push(i)
        
        def consumer(name, expected_count):
            shm = SharedMemory(name, 0)  # Attach
            queue = Queue(shm, "mp_queue")
            
            items = []
            while len(items) < expected_count:
                if not queue.empty():
                    items.append(queue.pop())
            
            return sorted(items)
        
        with shared_memory_context("/test_mp", 10*1024*1024) as shm:
            queue = Queue(shm, "mp_queue", max_size=1000)
            
            # Start producers
            p1 = mp.Process(target=producer, args=("/test_mp", 0, 50))
            p2 = mp.Process(target=producer, args=("/test_mp", 50, 50))
            
            p1.start()
            p2.start()
            
            p1.join()
            p2.join()
            
            # Verify all items are in queue
            items = []
            while not queue.empty():
                items.append(queue.pop())
            
            assert sorted(items) == list(range(100))
    
    def test_persistence(self):
        """Test queue persistence across process restarts."""
        # Process 1: Create and populate
        shm1 = SharedMemory("/test_persist", 1024*1024)
        queue1 = Queue(shm1, "persist", max_size=10)
        queue1.push(42)
        queue1.push(43)
        del queue1
        del shm1
        
        # Process 2: Open and read
        shm2 = SharedMemory("/test_persist", 0)  # Attach
        queue2 = Queue(shm2, "persist")
        
        assert queue2.size() == 2
        assert queue2.pop() == 42
        assert queue2.pop() == 43
        
        shm2.unlink()
    
    def test_different_types(self):
        """Test queue with different data types."""
        with shared_memory_context("/test_types", 1024*1024) as shm:
            # Int queue
            int_queue = Queue(shm, "ints", max_size=10, dtype=int)
            int_queue.push(42)
            assert int_queue.pop() == 42
            
            # Float queue
            float_queue = Queue(shm, "floats", max_size=10, dtype=float)
            float_queue.push(3.14)
            assert abs(float_queue.pop() - 3.14) < 0.001
    
    def test_try_operations(self):
        """Test non-blocking try_push and try_pop."""
        with shared_memory_context("/test_try", 1024*1024) as shm:
            queue = Queue(shm, "try", max_size=2)
            
            # try_pop on empty queue
            result = queue.try_pop()
            assert result is None
            
            # try_push
            assert queue.try_push(1) == True
            assert queue.try_push(2) == True
            assert queue.try_push(3) == False  # Full
            
            # try_pop
            assert queue.try_pop() == 1
            assert queue.try_pop() == 2
            assert queue.try_pop() is None


if __name__ == "__main__":
    pytest.main([__file__, "-v"])