"""
Test suite for Atomic operations.
"""

import pytest
import sys
import os
import threading
import multiprocessing as mp

sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
import posix_shm_py as shm


class TestAtomicInt:
    """Test AtomicInt functionality."""
    
    def setup_method(self):
        """Set up test fixtures."""
        self.shm_name = "/test_atomic"
        self.mem = shm.SharedMemory(self.shm_name, 1024 * 1024)
    
    def teardown_method(self):
        """Clean up after tests."""
        try:
            self.mem.unlink()
        except:
            pass
    
    def test_basic_operations(self):
        """Test basic atomic operations."""
        counter = shm.AtomicInt(self.mem, "counter", initial=0)
        
        # Test load
        assert counter.load() == 0
        assert int(counter) == 0  # __int__ conversion
        
        # Test store
        counter.store(42)
        assert counter.load() == 42
        
        # Test exchange
        old = counter.exchange(100)
        assert old == 42
        assert counter.load() == 100
    
    def test_arithmetic_operations(self):
        """Test atomic arithmetic operations."""
        counter = shm.AtomicInt(self.mem, "arith", initial=10)
        
        # Test fetch_add
        old = counter.fetch_add(5)
        assert old == 10
        assert counter.load() == 15
        
        # Test fetch_sub
        old = counter.fetch_sub(3)
        assert old == 15
        assert counter.load() == 12
        
        # Test in-place operators
        counter += 8
        assert counter.load() == 20
        
        counter -= 5
        assert counter.load() == 15
    
    def test_compare_exchange(self):
        """Test compare and exchange operation."""
        counter = shm.AtomicInt(self.mem, "cas", initial=100)
        
        # Successful CAS
        success = counter.compare_exchange(100, 200)
        assert success == True
        assert counter.load() == 200
        
        # Failed CAS
        success = counter.compare_exchange(100, 300)
        assert success == False
        assert counter.load() == 200  # Unchanged
    
    def test_repr(self):
        """Test string representation."""
        counter = shm.AtomicInt(self.mem, "repr_test", initial=42)
        repr_str = repr(counter)
        assert repr_str == "<AtomicInt value=42>"
    
    def test_concurrent_increments(self):
        """Test thread-safe increments."""
        counter = shm.AtomicInt(self.mem, "concurrent", initial=0)
        num_threads = 4
        increments_per_thread = 1000
        
        def increment_worker():
            for _ in range(increments_per_thread):
                counter.fetch_add(1)
        
        threads = []
        for _ in range(num_threads):
            t = threading.Thread(target=increment_worker)
            threads.append(t)
            t.start()
        
        for t in threads:
            t.join()
        
        # All increments should be atomic
        assert counter.load() == num_threads * increments_per_thread
    
    def test_multiprocess_atomic(self):
        """Test atomic operations across processes."""
        counter = shm.AtomicInt(self.mem, "mp_counter", initial=0)
        
        def worker(shm_name, count):
            mem = shm.SharedMemory(shm_name, 0)
            counter = shm.AtomicInt(mem, "mp_counter")
            for _ in range(count):
                counter.fetch_add(1)
        
        processes = []
        num_procs = 4
        increments = 100
        
        for _ in range(num_procs):
            p = mp.Process(target=worker, args=(self.shm_name, increments))
            processes.append(p)
            p.start()
        
        for p in processes:
            p.join()
        
        # Re-attach to verify
        mem2 = shm.SharedMemory(self.shm_name, 0)
        counter2 = shm.AtomicInt(mem2, "mp_counter")
        assert counter2.load() == num_procs * increments
    
    def test_negative_values(self):
        """Test with negative values."""
        counter = shm.AtomicInt(self.mem, "negative", initial=-100)
        
        assert counter.load() == -100
        
        counter.fetch_add(50)
        assert counter.load() == -50
        
        counter.fetch_sub(-30)
        assert counter.load() == -20
    
    def test_boundary_values(self):
        """Test with boundary values."""
        # Test with large positive value
        counter = shm.AtomicInt(self.mem, "large_pos", initial=1000000)
        counter.fetch_add(1000000)
        assert counter.load() == 2000000
        
        # Test with large negative value
        counter2 = shm.AtomicInt(self.mem, "large_neg", initial=-1000000)
        counter2.fetch_sub(1000000)
        assert counter2.load() == -2000000


if __name__ == "__main__":
    pytest.main([__file__, "-v"])