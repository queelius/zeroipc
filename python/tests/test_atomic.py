"""
Test suite for atomic operations in shared memory.
"""

import os
import struct
import threading
import multiprocessing as mp
import pytest
import numpy as np

from zeroipc import Memory
from zeroipc.atomic import (
    AtomicInt, AtomicInt64, atomic_thread_fence, spin_wait,
    MEMORY_ORDER_RELAXED, MEMORY_ORDER_ACQUIRE, MEMORY_ORDER_RELEASE,
    MEMORY_ORDER_ACQ_REL, MEMORY_ORDER_SEQ_CST
)


class TestAtomicInt:
    """Test 32-bit atomic integer operations."""

    def test_basic_operations(self):
        """Test basic load, store operations."""
        shm_name = f"/test_atomic_{os.getpid()}"

        try:
            # Create memory
            memory = Memory(shm_name, size=1024*1024)
            buffer = memory.mmap

            # Create atomic int
            atom = AtomicInt(buffer, offset=0)

            # Test store and load
            atom.store(42)
            assert atom.load() == 42

            atom.store(100)
            assert atom.load() == 100

            # Test with different memory orders
            atom.store(200, memory_order=MEMORY_ORDER_RELEASE)
            assert atom.load(memory_order=MEMORY_ORDER_ACQUIRE) == 200

        finally:
            Memory.unlink(shm_name)

    def test_compare_exchange(self):
        """Test compare-and-swap operations."""
        shm_name = f"/test_atomic_cas_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024)
            buffer = memory.mmap
            atom = AtomicInt(buffer, offset=0)

            # Initial value
            atom.store(10)

            # Successful exchange
            assert atom.compare_exchange_weak(10, 20) == True
            assert atom.load() == 20

            # Failed exchange (wrong expected value)
            assert atom.compare_exchange_weak(10, 30) == False
            assert atom.load() == 20  # Value unchanged

            # Another successful exchange
            assert atom.compare_exchange_weak(20, 50) == True
            assert atom.load() == 50

        finally:
            Memory.unlink(shm_name)

    def test_fetch_add(self):
        """Test atomic fetch-and-add operations."""
        shm_name = f"/test_atomic_add_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024)
            buffer = memory.mmap
            atom = AtomicInt(buffer, offset=0)

            # Initial value
            atom.store(100)

            # Fetch and add
            old_val = atom.fetch_add(10)
            assert old_val == 100
            assert atom.load() == 110

            # Add negative value
            old_val = atom.fetch_add(-20)
            assert old_val == 110
            assert atom.load() == 90

            # Multiple additions
            for i in range(10):
                atom.fetch_add(1)
            assert atom.load() == 100

        finally:
            Memory.unlink(shm_name)

    def test_multiple_offsets(self):
        """Test multiple atomic ints at different offsets."""
        shm_name = f"/test_atomic_offsets_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024)
            buffer = memory.mmap

            # Create multiple atomic ints
            atom1 = AtomicInt(buffer, offset=0)
            atom2 = AtomicInt(buffer, offset=4)
            atom3 = AtomicInt(buffer, offset=8)

            # Store different values
            atom1.store(10)
            atom2.store(20)
            atom3.store(30)

            # Verify they don't interfere
            assert atom1.load() == 10
            assert atom2.load() == 20
            assert atom3.load() == 30

            # Modify one doesn't affect others
            atom2.fetch_add(5)
            assert atom1.load() == 10
            assert atom2.load() == 25
            assert atom3.load() == 30

        finally:
            Memory.unlink(shm_name)

    def test_thread_safety(self):
        """Test atomic operations with multiple threads."""
        shm_name = f"/test_atomic_threads_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024)
            buffer = memory.mmap
            atom = AtomicInt(buffer, offset=0)
            atom.store(0)

            def increment_worker(n_increments):
                for _ in range(n_increments):
                    atom.fetch_add(1)

            # Create multiple threads
            threads = []
            n_threads = 10
            n_increments = 1000

            for _ in range(n_threads):
                t = threading.Thread(target=increment_worker, args=(n_increments,))
                threads.append(t)
                t.start()

            # Wait for completion
            for t in threads:
                t.join()

            # Verify final value
            expected = n_threads * n_increments
            assert atom.load() == expected

        finally:
            Memory.unlink(shm_name)


class TestAtomicInt64:
    """Test 64-bit atomic integer operations."""

    def test_basic_operations(self):
        """Test basic 64-bit operations."""
        shm_name = f"/test_atomic64_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024)
            buffer = memory.mmap
            atom = AtomicInt64(buffer, offset=0)

            # Test with large values
            large_val = 2**40
            atom.store(large_val)
            assert atom.load() == large_val

            # Test max values
            max_val = 2**63 - 1  # Max signed 64-bit
            atom.store(max_val)
            assert atom.load() == max_val

        finally:
            Memory.unlink(shm_name)

    def test_compare_exchange_64(self):
        """Test 64-bit CAS operations."""
        shm_name = f"/test_atomic64_cas_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024)
            buffer = memory.mmap
            atom = AtomicInt64(buffer, offset=0)

            large_val = 10**15
            atom.store(large_val)

            # Successful exchange
            assert atom.compare_exchange_weak(large_val, large_val * 2) == True
            assert atom.load() == large_val * 2

            # Failed exchange
            assert atom.compare_exchange_weak(large_val, large_val * 3) == False
            assert atom.load() == large_val * 2

        finally:
            Memory.unlink(shm_name)

    def test_fetch_add_64(self):
        """Test 64-bit fetch-and-add."""
        shm_name = f"/test_atomic64_add_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024)
            buffer = memory.mmap
            atom = AtomicInt64(buffer, offset=0)

            base_val = 10**12
            atom.store(base_val)

            # Add large value
            add_val = 10**10
            old = atom.fetch_add(add_val)
            assert old == base_val
            assert atom.load() == base_val + add_val

        finally:
            Memory.unlink(shm_name)

    def test_mixed_atomic_sizes(self):
        """Test 32-bit and 64-bit atomics together."""
        shm_name = f"/test_atomic_mixed_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024)
            buffer = memory.mmap

            # 32-bit at offset 0
            atom32 = AtomicInt(buffer, offset=0)
            # 64-bit at offset 8 (avoiding overlap)
            atom64 = AtomicInt64(buffer, offset=8)

            atom32.store(100)
            atom64.store(10**10)

            assert atom32.load() == 100
            assert atom64.load() == 10**10

            # Modifications don't interfere
            atom32.fetch_add(50)
            atom64.fetch_add(10**9)

            assert atom32.load() == 150
            assert atom64.load() == 10**10 + 10**9

        finally:
            Memory.unlink(shm_name)


class TestMemoryFence:
    """Test memory fence and spin wait utilities."""

    def test_memory_fence(self):
        """Test memory fence function (mostly no-op in Python)."""
        # Just verify it doesn't crash
        atomic_thread_fence()
        atomic_thread_fence(MEMORY_ORDER_RELAXED)
        atomic_thread_fence(MEMORY_ORDER_ACQUIRE)
        atomic_thread_fence(MEMORY_ORDER_RELEASE)
        atomic_thread_fence(MEMORY_ORDER_ACQ_REL)
        atomic_thread_fence(MEMORY_ORDER_SEQ_CST)

    def test_spin_wait(self):
        """Test spin wait function."""
        # Just verify it doesn't crash and returns quickly
        import time
        start = time.time()
        for _ in range(100):
            spin_wait()
        elapsed = time.time() - start
        # Should be fast (sub-second)
        assert elapsed < 1.0


class TestAtomicEdgeCases:
    """Test edge cases and error conditions."""

    def test_buffer_types(self):
        """Test with different buffer types."""
        # Test with bytes
        buffer = bytearray(100)
        atom = AtomicInt(buffer, 0)
        atom.store(42)
        assert atom.load() == 42

        # Test with memoryview
        mv = memoryview(buffer)
        atom2 = AtomicInt(mv, 4)
        atom2.store(100)
        assert atom2.load() == 100

    def test_boundary_values(self):
        """Test boundary values for 32-bit atomic."""
        buffer = bytearray(100)
        atom = AtomicInt(buffer, 0)

        # Test 0
        atom.store(0)
        assert atom.load() == 0

        # Test max unsigned 32-bit
        max_val = 2**32 - 1
        atom.store(max_val)
        assert atom.load() == max_val

    def test_concurrent_cas_competition(self):
        """Test CAS operations under contention."""
        shm_name = f"/test_atomic_contention_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024)
            buffer = memory.mmap
            atom = AtomicInt(buffer, offset=0)
            atom.store(0)

            success_count = [0]  # Use list for mutability in nested function

            def cas_worker():
                local_success = 0
                for _ in range(100):
                    while True:
                        current = atom.load()
                        if atom.compare_exchange_weak(current, current + 1):
                            local_success += 1
                            break
                success_count[0] += local_success

            # Create competing threads
            threads = []
            n_threads = 5

            for _ in range(n_threads):
                t = threading.Thread(target=cas_worker)
                threads.append(t)
                t.start()

            for t in threads:
                t.join()

            # Verify consistency
            assert atom.load() == n_threads * 100
            assert success_count[0] == n_threads * 100

        finally:
            Memory.unlink(shm_name)