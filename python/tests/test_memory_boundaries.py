"""Memory boundary tests for ZeroIPC structures."""

import pytest
import numpy as np
import threading
import resource
import gc
from concurrent.futures import ThreadPoolExecutor
from zeroipc import Memory, Array, Queue, Stack


class TestMemoryBoundaries:
    """Test behavior at memory limits and boundaries."""
    
    def setup_method(self):
        """Setup for each test."""
        Memory.unlink("/test_boundary")
    
    def teardown_method(self):
        """Cleanup after each test."""
        Memory.unlink("/test_boundary")
    
    # ========== SIZE LIMIT TESTS ==========
    
    def test_minimum_viable_memory(self):
        """Test smallest memory that can hold structures."""
        # Minimum size for table and small structure
        mem = Memory("/test_boundary", size=4096)
        
        # Should work with tiny array
        arr = Array(mem, "tiny", capacity=10, dtype=np.uint8)
        arr[0] = 255
        assert arr[0] == 255
    
    def test_large_allocation_near_limit(self):
        """Test allocation that nearly fills memory."""
        mem_size = 10 * 1024 * 1024  # 10MB
        mem = Memory("/test_boundary", size=mem_size)
        
        # Calculate available space (minus table overhead ~3KB)
        table_overhead = 3000
        available = mem_size - table_overhead
        
        # Try to allocate 90% of available space
        element_size = np.dtype(np.float64).itemsize
        array_elements = (available * 9 // 10) // element_size
        
        arr = Array(mem, "large", capacity=array_elements, dtype=np.float64)
        
        # Write to boundaries
        arr[0] = 3.14159
        arr[-1] = 2.71828
        
        assert arr[0] == pytest.approx(3.14159)
        assert arr[-1] == pytest.approx(2.71828)
        
        # Next allocation should fail
        with pytest.raises(RuntimeError):
            Array(mem, "overflow", capacity=array_elements, dtype=np.float64)
    
    def test_maximum_queue_capacity(self):
        """Test queue with maximum possible capacity."""
        mem_size = 100 * 1024 * 1024  # 100MB
        mem = Memory("/test_boundary", size=mem_size)
        
        # Large dtype to test limits
        dtype = np.dtype([
            ('data', 'u1', 1024)  # 1KB per element
        ])
        
        # Calculate maximum capacity
        overhead = 1024  # Conservative estimate
        max_capacity = (mem_size - overhead) // dtype.itemsize - 1
        
        # Create queue with near-maximum capacity
        test_capacity = max_capacity - 100
        queue = Queue(mem, "maxq", capacity=test_capacity, dtype=dtype)
        
        # Fill to capacity
        item = np.zeros(1, dtype=dtype)[0]
        item['data'][:] = 88
        
        pushed = 0
        while pushed < test_capacity - 1:
            if not queue.push(item):
                break
            pushed += 1
        
        print(f"Pushed {pushed} of {test_capacity} items")
        assert pushed >= test_capacity - 2
    
    # ========== FRAGMENTATION TESTS ==========
    
    def test_memory_fragmentation(self):
        """Test behavior with fragmented memory."""
        mem = Memory("/test_boundary", size=50*1024*1024)
        
        structures = []
        
        # Create many small structures
        for i in range(30):
            name = f"frag_{i}"
            if i % 3 == 0:
                structures.append(
                    Queue(mem, name, capacity=100, dtype=np.int32)
                )
            elif i % 3 == 1:
                structures.append(
                    Stack(mem, name, capacity=100, dtype=np.int32)
                )
            else:
                structures.append(
                    Array(mem, name, capacity=100, dtype=np.int32)
                )
        
        # Large allocation should fail due to fragmentation
        with pytest.raises(RuntimeError):
            Array(mem, "large_array", capacity=1000000, dtype=np.float64)
    
    def test_table_exhaustion(self):
        """Test behavior when table entries are exhausted."""
        mem = Memory("/test_boundary", size=100*1024*1024)
        
        arrays = []
        
        # Fill table to near capacity (default 64 entries)
        for i in range(63):
            name = f"arr_{i}"
            arrays.append(
                Array(mem, name, capacity=10, dtype=np.int32)
            )
        
        # Next allocation may fail depending on table size
        try:
            overflow = Array(mem, "overflow", capacity=10, dtype=np.int32)
            print("Table has more than 63 entries available")
        except RuntimeError as e:
            print(f"Table exhausted as expected: {e}")
    
    # ========== EXTREME VALUES TESTS ==========
    
    def test_extreme_sizes(self):
        """Test with extreme size values."""
        mem = Memory("/test_boundary", size=10*1024*1024)
        
        # Test with maximum size
        with pytest.raises((ValueError, RuntimeError, OverflowError)):
            Queue(mem, "extreme", capacity=2**63-1, dtype=np.int32)
        
        # Test with zero
        with pytest.raises((ValueError, RuntimeError)):
            Queue(mem, "zero", capacity=0, dtype=np.int32)
    
    def test_large_struct_types(self):
        """Test with very large structured dtypes."""
        mem = Memory("/test_boundary", size=100*1024*1024)
        
        # 1MB per element
        huge_dtype = np.dtype([
            ('data', 'u1', 1024*1024)
        ])
        
        queue = Queue(mem, "huge", capacity=10, dtype=huge_dtype)
        
        item = np.zeros(1, dtype=huge_dtype)[0]
        item['data'][:] = 65  # 'A'
        
        count = 0
        for _ in range(10):
            if queue.push(item):
                count += 1
            else:
                break
        
        print(f"Fitted {count} 1MB structures")
        assert count >= 1, "Should fit at least one huge element"
        assert count <= 10, "Should not exceed specified capacity"
    
    # ========== NUMPY DTYPE BOUNDARIES ==========
    
    def test_complex_dtypes(self):
        """Test with complex numpy dtypes."""
        mem = Memory("/test_boundary", size=10*1024*1024)
        
        # Nested dtype
        nested_dtype = np.dtype([
            ('header', [
                ('version', 'u4'),
                ('flags', 'u8')
            ]),
            ('data', 'f8', (10, 10)),  # 10x10 matrix
            ('footer', 'S32')  # 32-byte string
        ])
        
        arr = Array(mem, "nested", capacity=100, dtype=nested_dtype)
        
        item = np.zeros(1, dtype=nested_dtype)[0]
        item['header']['version'] = 42
        item['header']['flags'] = 0xDEADBEEF
        item['data'][:] = np.eye(10)
        item['footer'] = b'END_OF_RECORD'
        
        arr[0] = item
        retrieved = arr[0]
        
        assert retrieved['header']['version'] == 42
        assert retrieved['header']['flags'] == 0xDEADBEEF
        assert np.array_equal(retrieved['data'], np.eye(10))
        assert retrieved['footer'] == b'END_OF_RECORD'
    
    def test_aligned_dtypes(self):
        """Test dtypes with specific alignment requirements."""
        mem = Memory("/test_boundary", size=10*1024*1024)
        
        # Create dtypes with alignment
        aligned_dtype = np.dtype([
            ('pad1', 'u1', 7),   # 7 bytes padding
            ('aligned8', 'f8'),   # 8-byte aligned
            ('pad2', 'u1', 15),   # 15 bytes padding  
            ('aligned16', 'f8', 2), # 16-byte aligned
        ], align=True)
        
        arr = Array(mem, "aligned", capacity=100, dtype=aligned_dtype)
        
        item = np.zeros(1, dtype=aligned_dtype)[0]
        item['aligned8'] = 3.14
        item['aligned16'][0] = 2.71
        
        arr[50] = item
        assert arr[50]['aligned8'] == pytest.approx(3.14)
        assert arr[50]['aligned16'][0] == pytest.approx(2.71)
    
    # ========== RESOURCE EXHAUSTION TESTS ==========
    
    def test_system_memory_pressure(self):
        """Test behavior under system memory pressure."""
        # Try very large allocation
        huge_size = 1024 * 1024 * 1024  # 1GB
        
        try:
            mem = Memory("/test_boundary", size=huge_size)
            arr = Array(mem, "test", capacity=1000, dtype=np.int32)
            arr[0] = 42
            assert arr[0] == 42
            print("System allowed 1GB shared memory allocation")
        except (OSError, RuntimeError) as e:
            print(f"Large allocation failed (expected): {e}")
    
    def test_rapid_allocation_deallocation(self):
        """Test rapid creation and destruction of structures."""
        mem = Memory("/test_boundary", size=50*1024*1024)
        
        # Rapidly create and destroy
        for round in range(100):
            name = f"rapid_{round % 10}"
            
            # Create queue
            q = Queue(mem, name, capacity=1000, dtype=np.int32)
            q.push(round)
            del q
            
            # Reuse same name with stack
            s = Stack(mem, name, capacity=1000, dtype=np.int32)
            s.push(round * 2)
            del s
        
        # Memory should not be exhausted
        final = Array(mem, "final", capacity=1000, dtype=np.int32)
        final[0] = 999
        assert final[0] == 999
    
    # ========== CONCURRENT BOUNDARY TESTS ==========
    
    def test_concurrent_near_capacity(self):
        """Test concurrent operations near capacity."""
        mem = Memory("/test_boundary", size=10*1024*1024)
        queue = Queue(mem, "concurrent", capacity=100, dtype=np.int32)
        
        # Fill to near capacity
        for i in range(98):
            assert queue.push(i)
        
        # Multiple threads compete for last slots
        successes = []
        failures = []
        
        def compete():
            local_success = 0
            local_failure = 0
            for i in range(10):
                if queue.push(1000 + i):
                    local_success += 1
                else:
                    local_failure += 1
            return local_success, local_failure
        
        with ThreadPoolExecutor(max_workers=10) as executor:
            futures = [executor.submit(compete) for _ in range(10)]
            for f in futures:
                s, f_count = f.result()
                successes.append(s)
                failures.append(f_count)
        
        total_success = sum(successes)
        total_failure = sum(failures)
        
        # Should have exactly 1 success (1 slot remaining)
        assert total_success == 1
        assert total_failure == 99
        assert queue.full()
    
    def test_stress_memory_barriers(self):
        """Stress test at memory boundaries."""
        mem = Memory("/test_boundary", size=50*1024*1024)
        
        structures = []
        structures_created = 0
        
        # Allocate until memory is full
        try:
            for i in range(1000):
                if i % 2 == 0:
                    structures.append(
                        Queue(mem, f"q_{i}", capacity=10000, dtype=np.int32)
                    )
                else:
                    structures.append(
                        Stack(mem, f"s_{i}", capacity=10000, dtype=np.int32)
                    )
                structures_created += 1
        except RuntimeError:
            # Expected to fail at some point
            pass
        
        print(f"Created {structures_created} structures before exhaustion")
        assert structures_created > 0
        
        # Verify all structures still work
        for s in structures:
            if isinstance(s, Queue):
                s.push(42)
                assert s.pop() == 42
            else:  # Stack
                s.push(84)
                assert s.pop() == 84
    
    # ========== WRAPAROUND STRESS ==========
    
    def test_extreme_wraparound(self):
        """Test queue wraparound under extreme conditions."""
        mem = Memory("/test_boundary", size=10*1024*1024)
        
        # Very small queue to force frequent wraparound
        queue = Queue(mem, "wrap", capacity=3, dtype=np.int64)
        
        # Do millions of push/pop cycles
        for cycle in range(10000):
            # Push to capacity
            assert queue.push(cycle * 3)
            assert queue.push(cycle * 3 + 1)
            
            # Pop one
            val = queue.pop()
            assert val == cycle * 3
            
            # Push another
            assert queue.push(cycle * 3 + 2)
            
            # Pop all
            val1 = queue.pop()
            val2 = queue.pop()
            
            assert val1 == cycle * 3 + 1
            assert val2 == cycle * 3 + 2
            
            assert queue.empty()