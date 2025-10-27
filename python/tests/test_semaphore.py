"""Tests for Semaphore implementation."""

import os
import time
import threading
import pytest
import numpy as np

from zeroipc import Memory, Semaphore


class TestSemaphoreBasic:
    """Basic Semaphore functionality tests."""

    def test_create_semaphore(self):
        """Test creating a new semaphore."""
        shm_name = f"/test_sem_create_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            sem = Semaphore(memory, "test_sem", initial_count=5)

            assert sem.count == 5
            assert sem.waiting == 0
            assert sem.max_count == 0  # unbounded
            assert sem.name == "test_sem"

        finally:
            Memory.unlink(shm_name)

    def test_create_binary_semaphore(self):
        """Test creating a binary semaphore (mutex)."""
        shm_name = f"/test_sem_binary_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            sem = Semaphore(memory, "mutex", initial_count=1, max_count=1)

            assert sem.count == 1
            assert sem.max_count == 1

        finally:
            Memory.unlink(shm_name)

    def test_open_existing_semaphore(self):
        """Test opening an existing semaphore."""
        shm_name = f"/test_sem_open_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)

            # Create semaphore
            sem1 = Semaphore(memory, "existing", initial_count=3, max_count=10)
            assert sem1.count == 3

            # Open existing semaphore
            sem2 = Semaphore(memory, "existing")
            assert sem2.count == 3
            assert sem2.max_count == 10

        finally:
            Memory.unlink(shm_name)

    def test_acquire_release(self):
        """Test basic acquire and release operations."""
        shm_name = f"/test_sem_acq_rel_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            sem = Semaphore(memory, "test", initial_count=3)

            # Acquire decrements count
            assert sem.acquire() == True
            assert sem.count == 2

            assert sem.acquire() == True
            assert sem.count == 1

            # Release increments count
            sem.release()
            assert sem.count == 2

            sem.release()
            assert sem.count == 3

        finally:
            Memory.unlink(shm_name)

    def test_try_acquire_success(self):
        """Test try_acquire when permits are available."""
        shm_name = f"/test_sem_try_success_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            sem = Semaphore(memory, "test", initial_count=2)

            assert sem.try_acquire() == True
            assert sem.count == 1

            assert sem.try_acquire() == True
            assert sem.count == 0

        finally:
            Memory.unlink(shm_name)

    def test_try_acquire_failure(self):
        """Test try_acquire when no permits available."""
        shm_name = f"/test_sem_try_fail_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            sem = Semaphore(memory, "test", initial_count=0)

            assert sem.try_acquire() == False
            assert sem.count == 0

        finally:
            Memory.unlink(shm_name)

    def test_acquire_timeout(self):
        """Test acquire with timeout when no permits available."""
        shm_name = f"/test_sem_timeout_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            sem = Semaphore(memory, "test", initial_count=0)

            # Should timeout
            start = time.time()
            result = sem.acquire(timeout=0.1)
            elapsed = time.time() - start

            assert result == False
            assert elapsed >= 0.1

        finally:
            Memory.unlink(shm_name)

    def test_acquire_timeout_success(self):
        """Test acquire with timeout when permit is available."""
        shm_name = f"/test_sem_timeout_success_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            sem = Semaphore(memory, "test", initial_count=1)

            # Should succeed immediately
            result = sem.acquire(timeout=0.1)
            assert result == True
            assert sem.count == 0

        finally:
            Memory.unlink(shm_name)

    def test_max_count_enforced(self):
        """Test that max_count is enforced on release."""
        shm_name = f"/test_sem_max_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            sem = Semaphore(memory, "test", initial_count=5, max_count=5)

            # Try to exceed max count
            with pytest.raises(OverflowError):
                sem.release()

            assert sem.count == 5

        finally:
            Memory.unlink(shm_name)

    def test_invalid_arguments(self):
        """Test invalid constructor arguments."""
        shm_name = f"/test_sem_invalid_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)

            # Negative initial count
            with pytest.raises(ValueError):
                Semaphore(memory, "test", initial_count=-1)

            # Negative max count
            with pytest.raises(ValueError):
                Semaphore(memory, "test", initial_count=5, max_count=-1)

            # Initial > max
            with pytest.raises(ValueError):
                Semaphore(memory, "test", initial_count=10, max_count=5)

        finally:
            Memory.unlink(shm_name)


class TestSemaphoreConcurrency:
    """Concurrency tests for Semaphore."""

    def test_mutual_exclusion(self):
        """Test using semaphore as a mutex."""
        shm_name = f"/test_sem_mutex_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            sem = Semaphore(memory, "mutex", initial_count=1, max_count=1)

            shared_counter = [0]  # Use list for mutability
            iterations = 50

            def worker():
                for _ in range(iterations):
                    sem.acquire()
                    # Critical section
                    temp = shared_counter[0]
                    time.sleep(0.0001)  # Force context switch
                    shared_counter[0] = temp + 1
                    sem.release()

            threads = []
            for _ in range(4):
                t = threading.Thread(target=worker)
                t.start()
                threads.append(t)

            for t in threads:
                t.join()

            # All increments should be counted
            assert shared_counter[0] == iterations * 4

        finally:
            Memory.unlink(shm_name)

    def test_resource_pool_limiting(self):
        """Test semaphore limits concurrent access."""
        shm_name = f"/test_sem_pool_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            sem = Semaphore(memory, "pool", initial_count=3)  # max 3 concurrent

            concurrent_users = [0]
            max_concurrent = [0]
            lock = threading.Lock()

            def worker():
                sem.acquire()

                # Inside critical region
                with lock:
                    concurrent_users[0] += 1
                    if concurrent_users[0] > max_concurrent[0]:
                        max_concurrent[0] = concurrent_users[0]

                time.sleep(0.01)

                with lock:
                    concurrent_users[0] -= 1

                sem.release()

            threads = []
            for _ in range(10):
                t = threading.Thread(target=worker)
                t.start()
                threads.append(t)

            for t in threads:
                t.join()

            # Never exceeded the limit
            assert max_concurrent[0] <= 3

        finally:
            Memory.unlink(shm_name)

    def test_context_manager(self):
        """Test semaphore as context manager."""
        shm_name = f"/test_sem_context_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            sem = Semaphore(memory, "mutex", initial_count=1, max_count=1)

            with sem:
                assert sem.count == 0  # Acquired

            assert sem.count == 1  # Released

        finally:
            Memory.unlink(shm_name)

    def test_context_manager_exception(self):
        """Test context manager releases on exception."""
        shm_name = f"/test_sem_ctx_exc_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            sem = Semaphore(memory, "mutex", initial_count=1, max_count=1)

            try:
                with sem:
                    assert sem.count == 0
                    raise RuntimeError("test exception")
            except RuntimeError:
                pass

            # Should be released despite exception
            assert sem.count == 1

        finally:
            Memory.unlink(shm_name)

    def test_multiple_waiters(self):
        """Test multiple threads waiting on semaphore."""
        shm_name = f"/test_sem_waiters_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            sem = Semaphore(memory, "test", initial_count=0)

            acquired_count = [0]
            lock = threading.Lock()

            def waiter():
                sem.acquire()
                with lock:
                    acquired_count[0] += 1

            # Start 5 waiting threads
            threads = []
            for _ in range(5):
                t = threading.Thread(target=waiter)
                t.start()
                threads.append(t)

            # Give threads time to start waiting
            time.sleep(0.1)
            assert sem.waiting == 5

            # Release permits one by one
            for _ in range(5):
                sem.release()
                time.sleep(0.05)  # Let waiter wake up

            for t in threads:
                t.join()

            assert acquired_count[0] == 5
            assert sem.waiting == 0

        finally:
            Memory.unlink(shm_name)

    def test_unbounded_semaphore(self):
        """Test unbounded semaphore (max_count=0)."""
        shm_name = f"/test_sem_unbounded_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            sem = Semaphore(memory, "unbounded", initial_count=0, max_count=0)

            # Can release many times
            for _ in range(100):
                sem.release()

            assert sem.count == 100

            # Can acquire all
            for _ in range(100):
                assert sem.try_acquire() == True

            assert sem.count == 0

        finally:
            Memory.unlink(shm_name)

    def test_repr(self):
        """Test string representation."""
        shm_name = f"/test_sem_repr_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            sem = Semaphore(memory, "test", initial_count=5, max_count=10)

            repr_str = repr(sem)
            assert "test" in repr_str
            assert "5" in repr_str
            assert "10" in repr_str

        finally:
            Memory.unlink(shm_name)
