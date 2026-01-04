"""Tests for Mutex implementation."""

import os
import time
import threading
import pytest
import numpy as np

from zeroipc import Memory, Mutex, Array


class TestMutexBasic:
    """Basic Mutex functionality tests."""

    def test_create_mutex(self):
        """Test creating a new mutex."""
        shm_name = f"/test_mutex_create_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mtx = Mutex(memory, "test_mutex")

            assert mtx.name == "test_mutex"
            assert mtx.is_locked == False

        finally:
            Memory.unlink(shm_name)

    def test_open_existing_mutex(self):
        """Test opening an existing mutex."""
        shm_name = f"/test_mutex_open_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)

            # Create mutex
            mtx1 = Mutex(memory, "existing")
            assert not mtx1.is_locked

            # Open existing mutex
            mtx2 = Mutex(memory, "existing", create_if_missing=False)
            assert mtx2.name == "existing"

        finally:
            Memory.unlink(shm_name)

    def test_lock_unlock(self):
        """Test basic lock and unlock operations."""
        shm_name = f"/test_mutex_lock_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mtx = Mutex(memory, "test")

            # Lock mutex
            assert mtx.lock() == True
            assert mtx.is_locked == True

            # Unlock mutex
            mtx.unlock()
            assert mtx.is_locked == False

        finally:
            Memory.unlink(shm_name)

    def test_try_lock_success(self):
        """Test try_lock when mutex is available."""
        shm_name = f"/test_mutex_try_success_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mtx = Mutex(memory, "test")

            assert mtx.try_lock() == True
            assert mtx.is_locked == True

            mtx.unlock()

        finally:
            Memory.unlink(shm_name)

    def test_try_lock_failure(self):
        """Test try_lock when mutex is locked."""
        shm_name = f"/test_mutex_try_fail_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mtx = Mutex(memory, "test")

            # Lock mutex
            mtx.lock()

            # Try lock should fail
            assert mtx.try_lock() == False

            mtx.unlock()

        finally:
            Memory.unlink(shm_name)

    def test_context_manager(self):
        """Test mutex as context manager."""
        shm_name = f"/test_mutex_context_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mtx = Mutex(memory, "test")

            assert not mtx.is_locked

            with mtx:
                assert mtx.is_locked

            assert not mtx.is_locked

        finally:
            Memory.unlink(shm_name)


class TestMutexThreading:
    """Multi-threaded Mutex tests."""

    def test_mutual_exclusion(self):
        """Test that mutex provides mutual exclusion."""
        shm_name = f"/test_mutex_exclusion_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mtx = Mutex(memory, "test")
            counter = Array(memory, "counter", dtype=np.int32, capacity=1)
            counter[0] = 0

            def increment():
                for _ in range(100):
                    mtx.lock()
                    val = counter[0]
                    time.sleep(0.0001)  # Force context switch
                    counter[0] = val + 1
                    mtx.unlock()

            threads = [threading.Thread(target=increment) for _ in range(4)]
            for t in threads:
                t.start()
            for t in threads:
                t.join()

            # With proper mutex, we should get exactly 400
            assert counter[0] == 400

        finally:
            Memory.unlink(shm_name)

    def test_lock_contention(self):
        """Test multiple threads contending for mutex."""
        shm_name = f"/test_mutex_contention_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mtx = Mutex(memory, "test")
            results = []

            def worker(thread_id):
                acquired = mtx.lock(timeout=1.0)
                if acquired:
                    results.append(thread_id)
                    time.sleep(0.01)
                    mtx.unlock()

            threads = [threading.Thread(target=worker, args=(i,)) for i in range(5)]
            for t in threads:
                t.start()
            for t in threads:
                t.join()

            # All threads should have acquired the mutex
            assert len(results) == 5
            assert set(results) == {0, 1, 2, 3, 4}

        finally:
            Memory.unlink(shm_name)

    def test_critical_section_protection(self):
        """Test that critical sections are properly protected."""
        shm_name = f"/test_mutex_critical_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mtx = Mutex(memory, "test")
            data = Array(memory, "data", dtype=np.int32, capacity=10)
            data[:] = 0

            def modify_data():
                for i in range(10):
                    with mtx:
                        # Read
                        vals = data[:].copy()
                        # Modify
                        vals += 1
                        # Write
                        data[:] = vals

            threads = [threading.Thread(target=modify_data) for _ in range(3)]
            for t in threads:
                t.start()
            for t in threads:
                t.join()

            # All elements should be incremented 30 times (3 threads × 10 iterations)
            assert all(data[:] == 30)

        finally:
            Memory.unlink(shm_name)

    def test_lock_timeout(self):
        """Test lock with timeout."""
        shm_name = f"/test_mutex_timeout_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mtx = Mutex(memory, "test")

            # Lock mutex in main thread
            mtx.lock()

            result = []

            def try_lock_with_timeout():
                acquired = mtx.lock(timeout=0.1)
                result.append(acquired)

            t = threading.Thread(target=try_lock_with_timeout)
            t.start()
            t.join()

            # Should timeout
            assert result[0] == False

            # Unlock and verify thread can now acquire
            mtx.unlock()

            result.clear()
            t = threading.Thread(target=try_lock_with_timeout)
            t.start()
            t.join()

            assert result[0] == True

        finally:
            Memory.unlink(shm_name)


class TestMutexEdgeCases:
    """Edge case tests for Mutex."""

    def test_double_unlock(self):
        """Test that double unlock raises error."""
        shm_name = f"/test_mutex_double_unlock_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mtx = Mutex(memory, "test")

            mtx.lock()
            mtx.unlock()

            # Second unlock should raise OverflowError (from semaphore)
            with pytest.raises(OverflowError):
                mtx.unlock()

        finally:
            Memory.unlink(shm_name)

    def test_reentrant_lock_deadlock(self):
        """Test that same thread locking twice causes deadlock (as expected)."""
        shm_name = f"/test_mutex_reentrant_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mtx = Mutex(memory, "test")

            mtx.lock()

            # Trying to lock again should timeout (mutex is not reentrant)
            acquired = mtx.lock(timeout=0.1)
            assert acquired == False

            mtx.unlock()

        finally:
            Memory.unlink(shm_name)

    def test_repr(self):
        """Test string representation."""
        shm_name = f"/test_mutex_repr_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mtx = Mutex(memory, "test_mutex")

            repr_str = repr(mtx)
            assert "Mutex" in repr_str
            assert "test_mutex" in repr_str
            assert "unlocked" in repr_str

            mtx.lock()
            repr_str = repr(mtx)
            assert "locked" in repr_str

            mtx.unlock()

        finally:
            Memory.unlink(shm_name)
