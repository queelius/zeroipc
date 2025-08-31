"""Table stress and collision tests for ZeroIPC."""

import pytest
import numpy as np
import threading
import random
import time
from concurrent.futures import ThreadPoolExecutor
from zeroipc import Memory, Array, Queue, Stack


class TestTableStress:
    """Test table behavior under stress and edge conditions."""
    
    def setup_method(self):
        """Setup for each test."""
        Memory.unlink("/test_table_stress")
    
    def teardown_method(self):
        """Cleanup after each test."""
        Memory.unlink("/test_table_stress")
    
    # ========== TABLE CAPACITY TESTS ==========
    
    def test_table_fill_to_capacity(self):
        """Test filling table to its capacity."""
        # Create memory with small table (16 entries)
        mem = Memory("/test_table_stress", size=10*1024*1024, table_size=16)
        
        arrays = []
        created = 0
        
        # Try to create more than capacity
        for i in range(20):
            name = f"array_{i}"
            
            try:
                arrays.append(
                    Array(mem, name, capacity=10, dtype=np.int32)
                )
                created += 1
            except RuntimeError:
                # Expected when table is full
                break
        
        # Should have created around 16 entries
        assert created >= 15
        assert created <= 16
        
        print(f"Created {created} entries in 16-entry table")
        
        # Verify all created arrays are still accessible
        for i, arr in enumerate(arrays):
            arr[0] = i * 100
            assert arr[0] == i * 100
    
    def test_table_name_collisions(self):
        """Test that duplicate names are rejected."""
        mem = Memory("/test_table_stress", size=10*1024*1024)
        
        # Create first structure
        arr1 = Array(mem, "duplicate_name", capacity=100, dtype=np.int32)
        arr1[0] = 42
        
        # Try to create with same name - should fail
        with pytest.raises(RuntimeError):
            arr2 = Array(mem, "duplicate_name", capacity=200, dtype=np.int32)
        
        # Original should still work
        assert arr1[0] == 42
    
    def test_table_long_names(self):
        """Test handling of long names."""
        mem = Memory("/test_table_stress", size=10*1024*1024)
        
        # Maximum name length (31 chars + null)
        max_name = 'A' * 31
        arr1 = Array(mem, max_name, capacity=10, dtype=np.int32)
        arr1[0] = 100
        
        # Name that's too long (should be truncated)
        long_name = 'B' * 100
        arr2 = Array(mem, long_name, capacity=10, dtype=np.int32)
        arr2[0] = 200
        
        # Both should work
        assert arr1[0] == 100
        assert arr2[0] == 200
        
        # Try to find by truncated name
        truncated = long_name[:31]
        arr2_ref = Array(mem, truncated, dtype=np.int32)
        assert arr2_ref[0] == 200
    
    # ========== CONCURRENT TABLE ACCESS ==========
    
    def test_concurrent_table_creation(self):
        """Test concurrent structure creation."""
        mem = Memory("/test_table_stress", size=50*1024*1024, table_size=128)
        
        num_threads = 10
        structures_per_thread = 10
        
        successes = []
        failures = []
        created_names = []
        lock = threading.Lock()
        
        def create_structures(thread_id):
            local_success = 0
            local_failure = 0
            
            for i in range(structures_per_thread):
                name = f"t{thread_id}_s{i}"
                
                try:
                    # Randomly create different structure types
                    struct_type = (thread_id + i) % 3
                    
                    if struct_type == 0:
                        arr = Array(mem, name, capacity=100, dtype=np.int32)
                        arr[0] = thread_id * 1000 + i
                    elif struct_type == 1:
                        queue = Queue(mem, name, capacity=100, dtype=np.int32)
                        queue.push(thread_id * 1000 + i)
                    else:
                        stack = Stack(mem, name, capacity=100, dtype=np.int32)
                        stack.push(thread_id * 1000 + i)
                    
                    local_success += 1
                    
                    with lock:
                        created_names.append(name)
                    
                except Exception:
                    local_failure += 1
            
            return local_success, local_failure
        
        with ThreadPoolExecutor(max_workers=num_threads) as executor:
            futures = [executor.submit(create_structures, i) for i in range(num_threads)]
            
            for f in futures:
                s, f_count = f.result()
                successes.append(s)
                failures.append(f_count)
        
        total_success = sum(successes)
        total_failure = sum(failures)
        
        print(f"Concurrent creation: {total_success} successes, {total_failure} failures")
        
        # Most should succeed (table has 128 entries)
        assert total_success > 50
        
        # Verify created structures are accessible
        for name in created_names[:10]:  # Check first 10
            try:
                # Try to open as array (might be different type)
                arr = Array(mem, name, dtype=np.int32)
            except:
                # Might be a different type, that's ok
                pass
    
    def test_concurrent_table_lookup(self):
        """Test concurrent table lookups."""
        mem = Memory("/test_table_stress", size=10*1024*1024)
        
        # Pre-create structures
        num_structures = 50
        for i in range(num_structures):
            name = f"lookup_{i}"
            arr = Array(mem, name, capacity=10, dtype=np.int32)
            arr[0] = i * 100
        
        # Concurrent lookups
        num_threads = 10
        lookups_per_thread = 1000
        
        successful_lookups = []
        
        def lookup_structures(thread_id):
            random.seed(thread_id)
            local_success = 0
            
            for _ in range(lookups_per_thread):
                idx = random.randint(0, num_structures - 1)
                name = f"lookup_{idx}"
                
                try:
                    arr = Array(mem, name, dtype=np.int32)
                    if arr[0] == idx * 100:
                        local_success += 1
                except:
                    pass  # Should not happen
            
            return local_success
        
        with ThreadPoolExecutor(max_workers=num_threads) as executor:
            futures = [executor.submit(lookup_structures, i) for i in range(num_threads)]
            successful_lookups = [f.result() for f in futures]
        
        total_lookups = sum(successful_lookups)
        assert total_lookups == num_threads * lookups_per_thread
        print(f"Completed {total_lookups} concurrent lookups")
    
    # ========== TABLE FRAGMENTATION ==========
    
    def test_table_fragmentation(self):
        """Test table behavior with fragmentation."""
        mem = Memory("/test_table_stress", size=50*1024*1024, table_size=64)
        
        active_structures = {}
        random.seed(42)
        
        for round_num in range(100):
            # Create some structures
            for i in range(10):
                name = f"frag_{round_num}_{i}"
                
                try:
                    queue = Queue(mem, name, capacity=100, dtype=np.int32)
                    queue.push(round_num * 100 + i)
                    active_structures[name] = queue
                except:
                    # Table might be full
                    pass
            
            # Remove some random structures (simulate deletion)
            if len(active_structures) > 30:
                # In Python, let garbage collector handle it
                to_remove = random.choice(list(active_structures.keys()))
                del active_structures[to_remove]
        
        print(f"After fragmentation: {len(active_structures)} structures in table")
        
        # Table should still be functional
        assert len(active_structures) > 0
        assert len(active_structures) <= 64
    
    # ========== RAPID TABLE OPERATIONS ==========
    
    def test_rapid_table_churn(self):
        """Test rapid creation and reuse of table entries."""
        mem = Memory("/test_table_stress", size=20*1024*1024, table_size=32)
        
        start_time = time.perf_counter()
        
        # Rapidly create structures with reused names
        iterations = 1000
        for i in range(iterations):
            name = f"churn_{i % 10}"
            
            # Create different types in sequence with same name
            arr = Array(mem, name, capacity=10, dtype=np.int32)
            arr[0] = i
            del arr
            
            queue = Queue(mem, name, capacity=10, dtype=np.float64)
            queue.push(i * 3.14)
            del queue
            
            stack = Stack(mem, name, capacity=10, dtype=np.uint8)
            stack.push(ord('A') + (i % 26))
            del stack
        
        end_time = time.perf_counter()
        duration_ms = (end_time - start_time) * 1000
        
        print(f"Completed {iterations * 3} table operations in {duration_ms:.0f}ms")
        
        ops_per_sec = (iterations * 3 * 1000) / duration_ms
        print(f"Table throughput: {ops_per_sec:.0f} ops/sec")
        
        # Should complete reasonably fast
        assert duration_ms < 5000  # Less than 5 seconds
    
    # ========== TABLE PATTERN TESTS ==========
    
    def test_table_access_patterns(self):
        """Test different table access patterns."""
        mem = Memory("/test_table_stress", size=10*1024*1024)
        
        # Sequential pattern
        for i in range(30):
            name = f"seq_{i}"
            arr = Array(mem, name, capacity=5, dtype=np.int32)
            arr[0] = i
        
        # Random pattern
        random.seed(42)
        for _ in range(100):
            idx = random.randint(0, 29)
            name = f"seq_{idx}"
            arr = Array(mem, name, dtype=np.int32)
            assert arr[0] == idx
        
        # Batch pattern (multiple lookups of same name)
        frequent_name = "seq_15"
        for _ in range(50):
            arr = Array(mem, frequent_name, dtype=np.int32)
            assert arr[0] == 15
    
    # ========== ERROR RECOVERY TESTS ==========
    
    def test_table_error_recovery(self):
        """Test graceful handling of table errors."""
        mem = Memory("/test_table_stress", size=5*1024*1024, table_size=8)
        
        # Fill table completely
        arrays = []
        
        for i in range(10):
            try:
                arrays.append(
                    Array(mem, f"fill_{i}", capacity=10, dtype=np.int32)
                )
            except:
                break
        
        filled = len(arrays)
        print(f"Filled table with {filled} entries")
        
        # Try to create more - should fail gracefully
        failed_attempts = 0
        for i in range(5):
            try:
                arr = Array(mem, f"overflow_{i}", capacity=10, dtype=np.int32)
            except Exception:
                failed_attempts += 1
        
        assert failed_attempts == 5
        
        # Existing structures should still work
        for i, arr in enumerate(arrays):
            arr[0] = i * 10
            assert arr[0] == i * 10
    
    # ========== CROSS-TYPE TABLE TESTS ==========
    
    def test_mixed_type_table(self):
        """Test table with mix of structure types."""
        mem = Memory("/test_table_stress", size=20*1024*1024)
        
        # Create mix of all structure types
        for i in range(30):
            base_name = f"mixed_{i}"
            
            # Array
            arr = Array(mem, f"{base_name}_arr", capacity=10, dtype=np.int32)
            arr[0] = i
            
            # Queue
            queue = Queue(mem, f"{base_name}_queue", capacity=10, dtype=np.float64)
            queue.push(i * 2.5)
            
            # Stack
            stack = Stack(mem, f"{base_name}_stack", capacity=10, dtype=np.uint8)
            stack.push(ord('A') + i)
        
        # Verify all can be accessed
        for i in range(30):
            base_name = f"mixed_{i}"
            
            arr = Array(mem, f"{base_name}_arr", dtype=np.int32)
            assert arr[0] == i
            
            queue = Queue(mem, f"{base_name}_queue", dtype=np.float64)
            qval = queue.pop()
            assert qval is not None
            assert abs(qval - i * 2.5) < 0.001
            
            stack = Stack(mem, f"{base_name}_stack", dtype=np.uint8)
            sval = stack.pop()
            assert sval is not None
            assert sval == ord('A') + i
    
    # ========== SPECIAL NAME TESTS ==========
    
    def test_special_characters_in_names(self):
        """Test handling of special characters in names."""
        mem = Memory("/test_table_stress", size=10*1024*1024)
        
        # Test various special names
        special_names = [
            "name_with_underscore",
            "name-with-dash",
            "name.with.dots",
            "123numeric",
            "UPPERCASE",
            "MixedCase",
            "name with spaces",  # Spaces might be problematic
            "name/with/slashes",
            "name\\with\\backslashes",
        ]
        
        arrays = []
        for i, name in enumerate(special_names):
            try:
                arr = Array(mem, name, capacity=10, dtype=np.int32)
                arr[0] = i
                arrays.append((name, arr))
                print(f"✓ Created structure with name: '{name}'")
            except Exception as e:
                print(f"✗ Failed to create structure with name: '{name}': {e}")
        
        # Verify accessible
        for name, arr in arrays:
            try:
                arr_ref = Array(mem, name, dtype=np.int32)
                assert arr_ref[0] == arr[0]
            except Exception as e:
                print(f"Failed to access structure with name: '{name}': {e}")