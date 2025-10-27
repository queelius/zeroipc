"""Tests for Latch implementation."""

import os
import time
import threading
import pytest

from zeroipc import Memory, Latch


class TestLatchBasic:
    """Basic Latch functionality tests."""

    def test_create_latch(self):
        """Test creating a new latch."""
        shm_name = f"/test_latch_create_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            latch = Latch(memory, "test", count=5)

            assert latch.count == 5
            assert latch.initial_count == 5
            assert latch.name == "test"
            assert not latch.try_wait()

        finally:
            Memory.unlink(shm_name)

    def test_create_latch_with_zero(self):
        """Test creating a latch with count=0."""
        shm_name = f"/test_latch_zero_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            latch = Latch(memory, "zero", count=0)

            assert latch.count == 0
            assert latch.initial_count == 0
            assert latch.try_wait()  # Already at 0

        finally:
            Memory.unlink(shm_name)

    def test_open_existing_latch(self):
        """Test opening an existing latch."""
        shm_name = f"/test_latch_open_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)

            # Create latch
            latch1 = Latch(memory, "existing", count=10)
            assert latch1.count == 10

            # Open existing
            latch2 = Latch(memory, "existing")
            assert latch2.count == 10
            assert latch2.initial_count == 10

        finally:
            Memory.unlink(shm_name)

    def test_invalid_count(self):
        """Test invalid count values."""
        shm_name = f"/test_latch_invalid_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)

            # Negative count
            with pytest.raises(ValueError):
                Latch(memory, "test", count=-1)

        finally:
            Memory.unlink(shm_name)

    def test_latch_not_found(self):
        """Test opening non-existent latch."""
        shm_name = f"/test_latch_notfound_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)

            with pytest.raises(RuntimeError):
                Latch(memory, "nonexistent")

        finally:
            Memory.unlink(shm_name)


class TestLatchCountDown:
    """Count down tests for Latch."""

    def test_count_down_by_one(self):
        """Test counting down by 1."""
        shm_name = f"/test_latch_down1_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            latch = Latch(memory, "test", count=3)

            assert latch.count == 3

            latch.count_down()
            assert latch.count == 2

            latch.count_down()
            assert latch.count == 1

            latch.count_down()
            assert latch.count == 0

        finally:
            Memory.unlink(shm_name)

    def test_count_down_by_n(self):
        """Test counting down by n."""
        shm_name = f"/test_latch_downn_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            latch = Latch(memory, "test", count=10)

            latch.count_down(3)
            assert latch.count == 7

            latch.count_down(5)
            assert latch.count == 2

            latch.count_down(2)
            assert latch.count == 0

        finally:
            Memory.unlink(shm_name)

    def test_count_down_saturates_at_zero(self):
        """Test that count_down saturates at 0."""
        shm_name = f"/test_latch_saturate_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            latch = Latch(memory, "test", count=5)

            latch.count_down(10)  # Count down more than current
            assert latch.count == 0

            latch.count_down()  # Count down when already at 0
            assert latch.count == 0

        finally:
            Memory.unlink(shm_name)

    def test_count_down_invalid_amount(self):
        """Test invalid count_down amounts."""
        shm_name = f"/test_latch_invalid_down_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            latch = Latch(memory, "test", count=5)

            with pytest.raises(ValueError):
                latch.count_down(0)

            with pytest.raises(ValueError):
                latch.count_down(-1)

        finally:
            Memory.unlink(shm_name)


class TestLatchWait:
    """Wait tests for Latch."""

    def test_wait_when_already_zero(self):
        """Test wait returns immediately when count is 0."""
        shm_name = f"/test_latch_wait_zero_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            latch = Latch(memory, "test", count=0)

            # Should return immediately
            start = time.time()
            result = latch.wait(timeout=1.0)
            elapsed = time.time() - start

            assert result is True
            assert elapsed < 0.1

        finally:
            Memory.unlink(shm_name)

    def test_try_wait(self):
        """Test try_wait method."""
        shm_name = f"/test_latch_try_wait_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            latch = Latch(memory, "test", count=2)

            assert not latch.try_wait()

            latch.count_down()
            assert not latch.try_wait()

            latch.count_down()
            assert latch.try_wait()

        finally:
            Memory.unlink(shm_name)

    def test_wait_released_by_count_down(self):
        """Test that wait is released by count_down."""
        shm_name = f"/test_latch_wait_release_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            latch = Latch(memory, "test", count=1)

            waiter_done = [False]

            def waiter():
                latch.wait()
                waiter_done[0] = True

            t = threading.Thread(target=waiter)
            t.start()

            # Give thread time to start waiting
            time.sleep(0.05)
            assert not waiter_done[0]

            # Count down to release waiter
            latch.count_down()

            t.join(timeout=1.0)
            assert waiter_done[0]

        finally:
            Memory.unlink(shm_name)

    def test_wait_timeout_success(self):
        """Test wait with timeout when latch releases."""
        shm_name = f"/test_latch_timeout_ok_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            latch = Latch(memory, "test", count=1)

            def countdown():
                time.sleep(0.05)
                latch.count_down()

            t = threading.Thread(target=countdown)
            t.start()

            # Should succeed before timeout
            result = latch.wait(timeout=0.2)
            assert result is True

            t.join()

        finally:
            Memory.unlink(shm_name)

    def test_wait_timeout_failure(self):
        """Test wait with timeout when latch doesn't release."""
        shm_name = f"/test_latch_timeout_fail_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            latch = Latch(memory, "test", count=100)  # Never reaches 0

            start = time.time()
            result = latch.wait(timeout=0.1)
            elapsed = time.time() - start

            assert result is False
            assert elapsed >= 0.1

        finally:
            Memory.unlink(shm_name)


class TestLatchConcurrency:
    """Concurrency tests for Latch."""

    def test_multiple_threads_count_down(self):
        """Test multiple threads counting down."""
        shm_name = f"/test_latch_multi_down_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            num_threads = 10
            latch = Latch(memory, "test", count=num_threads)

            threads = []
            for i in range(num_threads):
                t = threading.Thread(target=latch.count_down)
                t.start()
                threads.append(t)

            for t in threads:
                t.join()

            assert latch.count == 0
            assert latch.try_wait()

        finally:
            Memory.unlink(shm_name)

    def test_multiple_waiters(self):
        """Test multiple waiters released simultaneously."""
        shm_name = f"/test_latch_multi_wait_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            latch = Latch(memory, "test", count=3)

            waiters_released = [0]
            lock = threading.Lock()

            def waiter():
                latch.wait()
                with lock:
                    waiters_released[0] += 1

            # Start multiple waiters
            threads = []
            for i in range(5):
                t = threading.Thread(target=waiter)
                t.start()
                threads.append(t)

            # Give threads time to start waiting
            time.sleep(0.05)
            assert waiters_released[0] == 0

            # Count down to release all waiters
            latch.count_down(3)

            for t in threads:
                t.join(timeout=1.0)

            assert waiters_released[0] == 5

        finally:
            Memory.unlink(shm_name)


class TestLatchUseCases:
    """Use case tests for Latch."""

    def test_start_gate_pattern(self):
        """Test start gate pattern."""
        shm_name = f"/test_latch_start_gate_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            start_latch = Latch(memory, "start_gate", count=1)

            workers_started = [0]
            lock = threading.Lock()

            def worker():
                # Wait for coordinator to signal start
                start_latch.wait()
                with lock:
                    workers_started[0] += 1

            # Start worker threads
            threads = []
            for i in range(5):
                t = threading.Thread(target=worker)
                t.start()
                threads.append(t)

            # Give threads time to reach latch
            time.sleep(0.05)
            assert workers_started[0] == 0

            # Release all workers
            start_latch.count_down()

            for t in threads:
                t.join(timeout=1.0)

            assert workers_started[0] == 5

        finally:
            Memory.unlink(shm_name)

    def test_completion_detection_pattern(self):
        """Test completion detection pattern."""
        shm_name = f"/test_latch_completion_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            num_workers = 8
            completion_latch = Latch(memory, "completion", count=num_workers)

            work_done = [0]
            lock = threading.Lock()

            def worker():
                # Do some work
                time.sleep(0.01)
                with lock:
                    work_done[0] += 1

                # Signal completion
                completion_latch.count_down()

            # Start workers
            threads = []
            for i in range(num_workers):
                t = threading.Thread(target=worker)
                t.start()
                threads.append(t)

            # Wait for all workers to complete
            completion_latch.wait()

            assert work_done[0] == num_workers
            assert completion_latch.count == 0

            for t in threads:
                t.join()

        finally:
            Memory.unlink(shm_name)


class TestLatchEdgeCases:
    """Edge case tests for Latch."""

    def test_single_count_latch(self):
        """Test latch with count=1."""
        shm_name = f"/test_latch_single_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            latch = Latch(memory, "single", count=1)

            assert latch.count == 1
            assert not latch.try_wait()

            latch.count_down()

            assert latch.count == 0
            assert latch.try_wait()

        finally:
            Memory.unlink(shm_name)

    def test_large_count(self):
        """Test latch with large count."""
        shm_name = f"/test_latch_large_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            latch = Latch(memory, "large", count=1000000)

            latch.count_down(1000000)
            assert latch.count == 0

        finally:
            Memory.unlink(shm_name)

    def test_one_time_use(self):
        """Test that latch is one-time use (stays at 0)."""
        shm_name = f"/test_latch_onetime_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            latch = Latch(memory, "onetime", count=2)

            latch.count_down(2)
            assert latch.count == 0

            # Latch stays at 0 (cannot be reset)
            latch.count_down()
            assert latch.count == 0

            # Wait should return immediately
            result = latch.wait(timeout=0.01)
            assert result is True
            assert latch.try_wait()

        finally:
            Memory.unlink(shm_name)

    def test_repr(self):
        """Test string representation."""
        shm_name = f"/test_latch_repr_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            latch = Latch(memory, "test", count=5)

            repr_str = repr(latch)
            assert "test" in repr_str
            assert "5" in repr_str
            assert "count" in repr_str

        finally:
            Memory.unlink(shm_name)
