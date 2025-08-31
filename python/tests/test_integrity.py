"""Data integrity tests for ZeroIPC structures."""

import pytest
import numpy as np
import threading
import hashlib
import struct
import time
from concurrent.futures import ThreadPoolExecutor
from zeroipc import Memory, Array, Queue, Stack


class TestDataIntegrity:
    """Test data integrity under various conditions."""
    
    def setup_method(self):
        """Setup for each test."""
        Memory.unlink("/test_integrity")
    
    def teardown_method(self):
        """Cleanup after each test."""
        Memory.unlink("/test_integrity")
    
    # ========== CHECKSUM TESTS ==========
    
    def test_queue_data_integrity_checksums(self):
        """Verify data integrity using checksums."""
        mem = Memory("/test_integrity", size=10*1024*1024)
        
        # Use structured dtype with checksum
        dtype = np.dtype([
            ('sequence', 'i4'),
            ('data', 'f8', 100),
            ('checksum', 'u8')
        ])
        
        queue = Queue(mem, "checksum_queue", capacity=100, dtype=dtype)
        
        def calculate_checksum(seq, data):
            """Calculate simple checksum."""
            data_bytes = data.tobytes()
            seq_bytes = struct.pack('i', seq)
            combined = seq_bytes + data_bytes
            return hash(combined) & 0xFFFFFFFFFFFFFFFF
        
        # Push data with checksums
        num_items = 50
        for i in range(num_items):
            item = np.zeros(1, dtype=dtype)[0]
            item['sequence'] = i
            item['data'] = np.random.randn(100)
            item['checksum'] = calculate_checksum(i, item['data'])
            
            assert queue.push(item)
        
        # Pop and verify checksums
        corrupted = 0
        for _ in range(num_items):
            item = queue.pop()
            assert item is not None
            
            expected_checksum = calculate_checksum(
                item['sequence'], 
                item['data']
            )
            
            if item['checksum'] != expected_checksum:
                corrupted += 1
        
        assert corrupted == 0, f"Found {corrupted} corrupted items"
    
    # ========== SEQUENCE TESTS ==========
    
    def test_queue_sequence_integrity(self):
        """Verify FIFO ordering is maintained."""
        mem = Memory("/test_integrity", size=10*1024*1024)
        queue = Queue(mem, "seq_queue", capacity=1000, dtype=np.int64)
        
        # Push sequence
        sequence = list(range(500))
        for val in sequence:
            assert queue.push(val)
        
        # Pop and verify order
        received = []
        while not queue.empty():
            val = queue.pop()
            if val is not None:
                received.append(val)
        
        assert received == sequence, "FIFO order violated"
    
    def test_stack_sequence_integrity(self):
        """Verify LIFO ordering is maintained."""
        mem = Memory("/test_integrity", size=10*1024*1024)
        stack = Stack(mem, "seq_stack", capacity=1000, dtype=np.int64)
        
        # Push sequence
        sequence = list(range(500))
        for val in sequence:
            assert stack.push(val)
        
        # Pop and verify LIFO order
        received = []
        while not stack.empty():
            val = stack.pop()
            if val is not None:
                received.append(val)
        
        assert received == list(reversed(sequence)), "LIFO order violated"
    
    # ========== CONCURRENT INTEGRITY TESTS ==========
    
    def test_concurrent_unique_values(self):
        """Ensure no values are lost or duplicated under concurrency."""
        mem = Memory("/test_integrity", size=50*1024*1024)
        queue = Queue(mem, "unique_queue", capacity=10000, dtype=np.int64)
        
        num_threads = 10
        values_per_thread = 1000
        
        # Each thread pushes unique values
        def producer(thread_id):
            base = thread_id * values_per_thread
            for i in range(values_per_thread):
                value = base + i
                while not queue.push(value):
                    time.sleep(0.0001)
        
        # Produce values
        with ThreadPoolExecutor(max_workers=num_threads) as executor:
            futures = [executor.submit(producer, i) for i in range(num_threads)]
            for f in futures:
                f.result()
        
        # Collect all values
        all_values = set()
        while not queue.empty():
            val = queue.pop()
            if val is not None:
                all_values.add(int(val))
        
        # Verify we got all unique values
        expected = set(range(num_threads * values_per_thread))
        assert all_values == expected, f"Missing values: {expected - all_values}"
    
    def test_array_concurrent_writes_no_corruption(self):
        """Verify array data isn't corrupted by concurrent writes."""
        mem = Memory("/test_integrity", size=10*1024*1024)
        
        # Large struct to increase chance of partial writes
        dtype = np.dtype([
            ('thread_id', 'i4'),
            ('sequence', 'i4'),
            ('data', 'f8', 100),
            ('guard', 'i4')  # Guard value to detect corruption
        ])
        
        array = Array(mem, "concurrent_array", capacity=100, dtype=dtype)
        
        def writer(thread_id, indices):
            for seq, idx in enumerate(indices):
                item = np.zeros(1, dtype=dtype)[0]
                item['thread_id'] = thread_id
                item['sequence'] = seq
                item['data'][:] = thread_id + seq / 1000.0
                item['guard'] = 0xDEADBEEF
                array[idx] = item
        
        # Divide array indices among threads
        num_threads = 10
        indices_per_thread = 10
        
        threads = []
        for t in range(num_threads):
            indices = list(range(t * indices_per_thread, (t + 1) * indices_per_thread))
            thread = threading.Thread(target=writer, args=(t, indices))
            threads.append(thread)
            thread.start()
        
        for thread in threads:
            thread.join()
        
        # Verify all writes are consistent
        corrupted = 0
        for i in range(num_threads * indices_per_thread):
            item = array[i]
            
            # Check guard value
            if item['guard'] != 0xDEADBEEF:
                corrupted += 1
                continue
            
            # Check data consistency
            thread_id = item['thread_id']
            sequence = item['sequence']
            expected_data = thread_id + sequence / 1000.0
            
            if not np.allclose(item['data'], expected_data):
                corrupted += 1
        
        assert corrupted == 0, f"Found {corrupted} corrupted entries"
    
    # ========== PATTERN TESTS ==========
    
    def test_alternating_pattern_integrity(self):
        """Test alternating push/pop pattern maintains integrity."""
        mem = Memory("/test_integrity", size=10*1024*1024)
        queue = Queue(mem, "pattern_queue", capacity=100, dtype=np.int32)
        
        # Alternating pattern
        for i in range(1000):
            # Push 2
            assert queue.push(i * 2)
            assert queue.push(i * 2 + 1)
            
            # Pop 1
            val = queue.pop()
            assert val == i * 2
            
            # Verify queue has exactly 1 item
            assert queue.size() == 1
        
        # Drain remaining
        remaining = []
        while not queue.empty():
            remaining.append(queue.pop())
        
        # Should be all odd numbers from the last iterations
        for i, val in enumerate(remaining):
            expected = (1000 - len(remaining) + i) * 2 + 1
            assert val == expected
    
    def test_binary_pattern_verification(self):
        """Use binary patterns to detect bit flips."""
        mem = Memory("/test_integrity", size=10*1024*1024)
        
        patterns = [
            0x0000000000000000,  # All zeros
            0xFFFFFFFFFFFFFFFF,  # All ones
            0xAAAAAAAAAAAAAAAA,  # Alternating 1010
            0x5555555555555555,  # Alternating 0101
            0x00FF00FF00FF00FF,  # Byte alternating
            0xFF00FF00FF00FF00,  # Byte alternating inverse
        ]
        
        stack = Stack(mem, "pattern_stack", capacity=100, dtype=np.uint64)
        
        # Push patterns
        for pattern in patterns:
            assert stack.push(pattern)
        
        # Pop and verify (LIFO)
        for pattern in reversed(patterns):
            val = stack.pop()
            assert val == pattern, f"Pattern corrupted: expected {pattern:016X}, got {val:016X}"
    
    # ========== BOUNDARY TESTS ==========
    
    def test_wraparound_integrity(self):
        """Test data integrity across circular buffer wraparound."""
        mem = Memory("/test_integrity", size=10*1024*1024)
        queue = Queue(mem, "wrap_queue", capacity=10, dtype=np.int32)
        
        # Fill, partially empty, refill many times
        for cycle in range(100):
            # Fill to capacity
            for i in range(9):  # capacity - 1
                assert queue.push(cycle * 1000 + i)
            
            # Remove half
            for _ in range(5):
                val = queue.pop()
                assert val is not None
                assert val // 1000 == cycle  # Verify cycle number
            
            # Refill
            for i in range(5):
                assert queue.push(cycle * 1000 + 900 + i)
            
            # Verify we have 9 items
            assert queue.size() == 9
            
            # Drain all and verify sequence
            values = []
            while not queue.empty():
                values.append(queue.pop())
            
            # First 4 from original fill, last 5 from refill
            expected = [cycle * 1000 + i for i in range(5, 9)]
            expected += [cycle * 1000 + 900 + i for i in range(5)]
            
            assert values == expected
    
    # ========== STRESS INTEGRITY ==========
    
    def test_sustained_integrity_under_load(self):
        """Verify integrity during sustained concurrent operations."""
        mem = Memory("/test_integrity", size=50*1024*1024)
        queue = Queue(mem, "stress_queue", capacity=5000, dtype=np.int64)
        
        duration = 2.0  # seconds
        stop_event = threading.Event()
        integrity_errors = []
        
        def producer():
            sequence = 0
            while not stop_event.is_set():
                if queue.push(sequence):
                    sequence += 1
                else:
                    time.sleep(0.0001)
        
        def consumer():
            expected = 0
            while not stop_event.is_set():
                val = queue.pop()
                if val is not None:
                    if val != expected:
                        integrity_errors.append(
                            f"Expected {expected}, got {val}"
                        )
                    expected = val + 1
        
        # Start threads
        prod_thread = threading.Thread(target=producer)
        cons_thread = threading.Thread(target=consumer)
        
        prod_thread.start()
        cons_thread.start()
        
        # Run for duration
        time.sleep(duration)
        stop_event.set()
        
        prod_thread.join()
        cons_thread.join()
        
        assert len(integrity_errors) == 0, f"Integrity errors: {integrity_errors[:10]}"
    
    def test_mixed_operations_integrity(self):
        """Test integrity with mixed read/write operations."""
        mem = Memory("/test_integrity", size=10*1024*1024)
        
        # Create multiple structures
        array = Array(mem, "mixed_array", capacity=100, dtype=np.int32)
        queue = Queue(mem, "mixed_queue", capacity=100, dtype=np.int32)
        stack = Stack(mem, "mixed_stack", capacity=100, dtype=np.int32)
        
        # Initialize array with known values
        for i in range(100):
            array[i] = i * 100
        
        # Perform mixed operations
        for i in range(100):
            # Queue operations
            queue.push(i)
            
            # Stack operations
            stack.push(i * 10)
            
            # Array read - verify unchanged
            assert array[i] == i * 100
            
            # Interleaved pops
            if i % 2 == 0 and not queue.empty():
                val = queue.pop()
                assert val <= i  # Should be an earlier value
            
            if i % 3 == 0 and not stack.empty():
                val = stack.pop()
                assert val % 10 == 0  # Should be multiple of 10
        
        # Final verification - array should be unchanged
        for i in range(100):
            assert array[i] == i * 100, f"Array corrupted at index {i}"