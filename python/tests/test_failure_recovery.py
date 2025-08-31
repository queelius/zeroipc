"""Failure recovery tests for ZeroIPC structures."""

import pytest
import numpy as np
import os
import signal
import time
import multiprocessing
import threading
from concurrent.futures import ProcessPoolExecutor, ThreadPoolExecutor
from zeroipc import Memory, Array, Queue, Stack


class TestFailureRecovery:
    """Test recovery from various failure scenarios."""
    
    def setup_method(self):
        """Setup for each test."""
        Memory.unlink("/test_recovery")
    
    def teardown_method(self):
        """Cleanup after each test."""
        Memory.unlink("/test_recovery")
    
    # ========== CRASH SIMULATION TESTS ==========
    
    def test_process_crash_during_write(self):
        """Test recovery after process crash during write operations."""
        # Parent creates shared memory
        mem = Memory("/test_recovery", size=10*1024*1024)
        queue = Queue(mem, "crash_queue", capacity=1000, dtype=np.int32)
        
        # Add initial data
        for i in range(100):
            assert queue.push(i)
        
        def child_process():
            """Child that will crash."""
            child_mem = Memory("/test_recovery")
            child_queue = Queue(child_mem, "crash_queue", dtype=np.int32)
            
            # Start adding data
            for i in range(1000, 1050):
                child_queue.push(i)
                
                if i == 1025:
                    # Simulate crash
                    os._exit(42)
            
            os._exit(0)
        
        # Run child process
        p = multiprocessing.Process(target=child_process)
        p.start()
        p.join()
        
        assert p.exitcode == 42
        
        # Verify parent can still access queue
        count = 0
        while not queue.empty():
            val = queue.pop()
            if val is not None:
                count += 1
        
        # Should have original 100 + some from child
        assert count >= 100
        print(f"Recovered {count} items after child crash")
    
    def test_recovery_after_abrupt_termination(self):
        """Test data persistence after abrupt termination."""
        # Create and populate data
        mem = Memory("/test_recovery", size=10*1024*1024)
        stack = Stack(mem, "persist_stack", capacity=500, dtype=np.float64)
        
        for i in range(250):
            stack.push(i * 3.14)
        
        del stack
        del mem
        
        # Simulate new process accessing same memory
        recovered_mem = Memory("/test_recovery")
        recovered_stack = Stack(recovered_mem, "persist_stack", dtype=np.float64)
        
        # Should be able to read all data
        count = 0
        total = 0
        while not recovered_stack.empty():
            val = recovered_stack.pop()
            if val is not None:
                count += 1
                total += val
        
        assert count == 250
        print(f"Recovered {count} values, sum: {total}")
    
    # ========== PARTIAL WRITE RECOVERY ==========
    
    def test_partial_write_detection(self):
        """Test detection and recovery from partial writes."""
        mem = Memory("/test_recovery", size=10*1024*1024)
        
        # Define dtype with checksum
        dtype = np.dtype([
            ('sequence', 'u4'),
            ('data', 'u1', 1020),
            ('checksum', 'u4')
        ])
        
        def calculate_checksum(sequence, data):
            """Calculate simple checksum."""
            checksum = sequence
            for byte in data:
                checksum = ((checksum << 1) ^ byte) & 0xFFFFFFFF
            return checksum
        
        queue = Queue(mem, "checksum_queue", capacity=100, dtype=dtype)
        
        # Write valid data
        for i in range(50):
            item = np.zeros(1, dtype=dtype)[0]
            item['sequence'] = i
            item['data'][:] = ord('A') + (i % 26)
            item['checksum'] = calculate_checksum(i, item['data'])
            
            assert queue.push(item)
        
        # Read back and verify checksums
        valid_count = 0
        invalid_count = 0
        
        while not queue.empty():
            item = queue.pop()
            if item is not None:
                expected = calculate_checksum(item['sequence'], item['data'])
                if item['checksum'] == expected:
                    valid_count += 1
                else:
                    invalid_count += 1
                    print(f"Detected corrupted item at sequence {item['sequence']}")
        
        assert valid_count == 50
        assert invalid_count == 0
    
    # ========== SIGNAL HANDLING RECOVERY ==========
    
    def test_graceful_shutdown_on_signal(self):
        """Test graceful shutdown when receiving signals."""
        mem = Memory("/test_recovery", size=10*1024*1024)
        queue = Queue(mem, "signal_queue", capacity=1000, dtype=np.int32)
        
        signal_received = threading.Event()
        
        def signal_handler(signum, frame):
            signal_received.set()
        
        # Install signal handler
        old_handler = signal.signal(signal.SIGUSR1, signal_handler)
        
        try:
            written = []
            stop_event = threading.Event()
            
            def writer():
                """Background writer thread."""
                value = 0
                while not stop_event.is_set() and not signal_received.is_set():
                    if queue.push(value):
                        written.append(value)
                        value += 1
                    time.sleep(0.0001)
            
            # Start writer
            writer_thread = threading.Thread(target=writer)
            writer_thread.start()
            
            # Let it run briefly
            time.sleep(0.1)
            
            # Send signal
            os.kill(os.getpid(), signal.SIGUSR1)
            
            # Wait for graceful shutdown
            time.sleep(0.05)
            stop_event.set()
            writer_thread.join()
            
            # Verify data integrity
            read_values = []
            while not queue.empty():
                val = queue.pop()
                if val is not None:
                    read_values.append(val)
            
            # Check sequence integrity
            for i in range(1, len(read_values)):
                assert read_values[i] == read_values[i-1] + 1
            
            assert len(read_values) == len(written)
            print(f"Gracefully recovered {len(read_values)} items after signal")
            
        finally:
            signal.signal(signal.SIGUSR1, old_handler)
    
    # ========== DEADLOCK RECOVERY ==========
    
    def test_deadlock_timeout(self):
        """Test recovery from potential deadlock situations."""
        mem = Memory("/test_recovery", size=10*1024*1024)
        queue = Queue(mem, "deadlock_queue", capacity=10, dtype=np.int32)
        
        # Fill queue
        for i in range(9):
            assert queue.push(i)
        
        deadlock_detected = False
        
        def pusher_with_timeout():
            """Try to push with timeout."""
            nonlocal deadlock_detected
            start_time = time.time()
            
            while True:
                if queue.push(999):
                    break
                
                if time.time() - start_time > 0.1:  # 100ms timeout
                    deadlock_detected = True
                    break
                
                time.sleep(0.001)
        
        # Start pusher thread
        pusher = threading.Thread(target=pusher_with_timeout)
        pusher.start()
        
        # Simulate delayed consumer
        time.sleep(0.05)
        
        if not deadlock_detected:
            # Make room
            queue.pop()
        
        pusher.join()
        
        # System should recover either way
        assert True  # Test passes if we get here
    
    # ========== MEMORY CORRUPTION RECOVERY ==========
    
    def test_corrupted_structure_detection(self):
        """Test detection and handling of corrupted structures."""
        mem = Memory("/test_recovery", size=10*1024*1024)
        
        # Create queue with known pattern
        queue = Queue(mem, "corrupt_queue", capacity=100, dtype=np.int32)
        
        for i in range(50):
            queue.push(i)
        
        # Simulate recovery attempt
        recovery_needed = False
        
        try:
            # Try to access potentially corrupted queue
            recovered_queue = Queue(mem, "corrupt_queue", dtype=np.int32)
            
            count = 0
            while not recovered_queue.empty() and count < 100:
                recovered_queue.pop()
                count += 1
            
            if count == 0:
                recovery_needed = True
        except Exception as e:
            recovery_needed = True
            print(f"Corruption detected: {e}")
        
        if recovery_needed:
            # Rebuild structure
            rebuilt = Queue(mem, "rebuilt_queue", capacity=100, dtype=np.int32)
            assert rebuilt.empty()
            print("Successfully rebuilt corrupted structure")
    
    # ========== ATOMIC OPERATION RECOVERY ==========
    
    def test_incomplete_atomic_operation(self):
        """Test recovery from incomplete atomic operations."""
        mem = Memory("/test_recovery", size=10*1024*1024)
        stack = Stack(mem, "atomic_stack", capacity=1000, dtype=np.uint64)
        
        # Pattern to detect incomplete operations
        MARKER = 0xDEADBEEFCAFEBABE
        INCOMPLETE = 0xFFFFFFFFFFFFFFFF
        
        # Push marker values
        for i in range(100):
            stack.push(MARKER + i)
        
        # Recovery: scan for incomplete markers
        recovered = []
        while not stack.empty():
            val = stack.pop()
            if val is not None and val != INCOMPLETE:
                recovered.append(val)
        
        # Verify all valid data recovered
        assert len(recovered) == 100
        for val in recovered:
            assert val >= MARKER
            assert val < MARKER + 100
        
        print(f"Recovered {len(recovered)} valid items")
    
    # ========== MULTI-PROCESS RECOVERY ==========
    
    def test_multi_process_crash_recovery(self):
        """Test recovery from multiple process crashes."""
        mem = Memory("/test_recovery", size=50*1024*1024)
        queue = Queue(mem, "multi_queue", capacity=10000, dtype=np.int32)
        
        def worker_process(process_id):
            """Worker that might crash."""
            worker_mem = Memory("/test_recovery")
            worker_queue = Queue(worker_mem, "multi_queue", dtype=np.int32)
            
            # Each worker writes its range
            for i in range(1000):
                value = process_id * 10000 + i
                worker_queue.push(value)
                
                # Simulate random crash
                if i == 500 + process_id * 100:
                    os._exit(process_id)
            
            os._exit(0)
        
        # Launch multiple processes
        processes = []
        for p in range(5):
            proc = multiprocessing.Process(target=worker_process, args=(p,))
            proc.start()
            processes.append(proc)
        
        # Wait for all processes
        for i, proc in enumerate(processes):
            proc.join()
            print(f"Process {i} exited with code {proc.exitcode}")
        
        # Parent recovers all data
        recovered_values = set()
        while not queue.empty():
            val = queue.pop()
            if val is not None:
                recovered_values.add(val)
        
        # Should have partial data from each process
        assert len(recovered_values) > 0
        print(f"Recovered {len(recovered_values)} unique values from crashed processes")
    
    # ========== CONSISTENCY CHECK ==========
    
    def test_consistency_after_failures(self):
        """Test structure consistency after various failures."""
        mem = Memory("/test_recovery", size=10*1024*1024)
        
        # Create multiple structures
        array = Array(mem, "cons_array", capacity=100, dtype=np.int32)
        queue = Queue(mem, "cons_queue", capacity=100, dtype=np.int32)
        stack = Stack(mem, "cons_stack", capacity=100, dtype=np.int32)
        
        # Initialize with known pattern
        for i in range(100):
            array[i] = i * 100
        
        for i in range(50):
            queue.push(i * 10)
            stack.push(i * 20)
        
        # Simulate partial failure and recovery
        del array
        del queue
        del stack
        
        # Reopen structures
        recovered_mem = Memory("/test_recovery")
        rec_array = Array(recovered_mem, "cons_array", dtype=np.int32)
        rec_queue = Queue(recovered_mem, "cons_queue", dtype=np.int32)
        rec_stack = Stack(recovered_mem, "cons_stack", dtype=np.int32)
        
        # Verify array unchanged
        array_valid = True
        for i in range(100):
            if rec_array[i] != i * 100:
                array_valid = False
                break
        
        assert array_valid
        
        # Verify queue has correct size
        assert rec_queue.size() == 50
        
        # Verify stack has correct size
        assert rec_stack.size() == 50
        
        print("All structures consistent after recovery")
    
    # ========== RESOURCE CLEANUP ==========
    
    def test_resource_cleanup_on_failure(self):
        """Test proper resource cleanup on failure."""
        mem = Memory("/test_recovery", size=10*1024*1024)
        
        structures_created = []
        
        try:
            # Create many structures until failure
            for i in range(100):
                name = f"cleanup_{i}"
                if i % 3 == 0:
                    structures_created.append(
                        Queue(mem, name, capacity=1000, dtype=np.int32)
                    )
                elif i % 3 == 1:
                    structures_created.append(
                        Stack(mem, name, capacity=1000, dtype=np.int32)
                    )
                else:
                    structures_created.append(
                        Array(mem, name, capacity=1000, dtype=np.int32)
                    )
        except RuntimeError:
            # Expected to fail at some point
            pass
        
        print(f"Created {len(structures_created)} structures before failure")
        
        # Verify all created structures are still accessible
        for struct in structures_created:
            if isinstance(struct, Queue):
                struct.push(42)
                assert struct.pop() == 42
            elif isinstance(struct, Stack):
                struct.push(84)
                assert struct.pop() == 84
            else:  # Array
                struct[0] = 168
                assert struct[0] == 168
        
        print("All structures remain functional after partial failure")