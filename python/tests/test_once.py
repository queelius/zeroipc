"""Tests for Once implementation."""

import os
import time
import threading
import pytest
import numpy as np

from zeroipc import Memory, Once, Array


class TestOnceBasic:
    """Basic Once functionality tests."""

    def test_create_once(self):
        """Test creating a new once flag."""
        shm_name = f"/test_once_create_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            once = Once(memory, "test_once")

            assert once.name == "test_once"
            assert once.is_called == False

        finally:
            Memory.unlink(shm_name)

    def test_open_existing_once(self):
        """Test opening an existing once flag."""
        shm_name = f"/test_once_open_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)

            # Create once flag
            once1 = Once(memory, "existing")
            assert not once1.is_called

            # Open existing once flag
            once2 = Once(memory, "existing", create_if_missing=False)
            assert once2.name == "existing"
            assert not once2.is_called

        finally:
            Memory.unlink(shm_name)

    def test_call_once(self):
        """Test that function is called exactly once."""
        shm_name = f"/test_once_call_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            once = Once(memory, "test")
            counter = Array(memory, "counter", dtype=np.int32, capacity=1)
            counter[0] = 0

            def increment():
                counter[0] += 1

            # First call - should execute
            once.call(increment)
            assert counter[0] == 1
            assert once.is_called == True

            # Second call - should not execute
            once.call(increment)
            assert counter[0] == 1  # Still 1, not 2

            # Third call - should not execute
            once.call(increment)
            assert counter[0] == 1  # Still 1, not 3

        finally:
            Memory.unlink(shm_name)

    def test_reset(self):
        """Test resetting once flag."""
        shm_name = f"/test_once_reset_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            once = Once(memory, "test")
            counter = Array(memory, "counter", dtype=np.int32, capacity=1)
            counter[0] = 0

            def increment():
                counter[0] += 1

            # Call once
            once.call(increment)
            assert counter[0] == 1
            assert once.is_called

            # Reset
            once.reset()
            assert not once.is_called

            # Can call again after reset
            once.call(increment)
            assert counter[0] == 2
            assert once.is_called

        finally:
            Memory.unlink(shm_name)

    def test_repr(self):
        """Test string representation."""
        shm_name = f"/test_once_repr_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            once = Once(memory, "test_once")

            repr_str = repr(once)
            assert "Once" in repr_str
            assert "test_once" in repr_str
            assert "not called" in repr_str

            once.call(lambda: None)
            repr_str = repr(once)
            assert "called" in repr_str

        finally:
            Memory.unlink(shm_name)


class TestOnceThreading:
    """Multi-threaded Once tests."""

    def test_multiple_threads_call_once(self):
        """Test that multiple threads calling once results in single execution."""
        shm_name = f"/test_once_threads_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            once = Once(memory, "test")
            counter = Array(memory, "counter", dtype=np.int32, capacity=1)
            counter[0] = 0

            def increment():
                time.sleep(0.01)  # Simulate expensive initialization
                counter[0] += 1

            # 10 threads all trying to call once
            threads = [threading.Thread(target=lambda: once.call(increment)) for _ in range(10)]
            for t in threads:
                t.start()
            for t in threads:
                t.join()

            # Should be called exactly once
            assert counter[0] == 1
            assert once.is_called

        finally:
            Memory.unlink(shm_name)

    def test_racing_threads(self):
        """Test racing threads all calling once."""
        shm_name = f"/test_once_race_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            once = Once(memory, "test")
            results = []

            def append_thread_id(tid):
                results.append(tid)

            # 20 threads racing to be first
            threads = [
                threading.Thread(target=lambda i=i: once.call(lambda: append_thread_id(i)))
                for i in range(20)
            ]
            for t in threads:
                t.start()
            for t in threads:
                t.join()

            # Exactly one thread should have executed
            assert len(results) == 1

        finally:
            Memory.unlink(shm_name)

    def test_expensive_initialization(self):
        """Test with expensive initialization function."""
        shm_name = f"/test_once_expensive_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            once = Once(memory, "test")
            data = Array(memory, "data", dtype=np.int32, capacity=100)
            data[:] = 0

            def expensive_init():
                # Simulate expensive initialization
                time.sleep(0.05)
                data[:] = np.arange(100)

            start = time.time()

            # Multiple threads call once
            threads = [threading.Thread(target=lambda: once.call(expensive_init)) for _ in range(5)]
            for t in threads:
                t.start()
            for t in threads:
                t.join()

            elapsed = time.time() - start

            # Should only run once, so time should be ~0.05s, not 0.25s
            assert elapsed < 0.15  # Some margin for overhead

            # Data should be initialized
            assert np.all(data[:] == np.arange(100))

        finally:
            Memory.unlink(shm_name)


class TestOnceEdgeCases:
    """Edge case tests for Once."""

    def test_exception_during_call(self):
        """Test that exception during call marks as done (std::call_once semantics)."""
        shm_name = f"/test_once_exception_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            once = Once(memory, "test")
            counter = Array(memory, "counter", dtype=np.int32, capacity=1)
            counter[0] = 0

            def failing_func():
                counter[0] += 1
                raise ValueError("Intentional failure")

            # First call raises exception
            with pytest.raises(ValueError):
                once.call(failing_func)

            # Once flag should be marked as done even after exception
            # (matches std::call_once semantics - the once is consumed)
            assert once.is_called
            assert counter[0] == 1

            # Subsequent calls should NOT execute (already done)
            def succeeding_func():
                counter[0] += 10

            once.call(succeeding_func)
            assert once.is_called
            assert counter[0] == 1  # Still 1, second func was not called

        finally:
            Memory.unlink(shm_name)

    def test_lambda_function(self):
        """Test with lambda function."""
        shm_name = f"/test_once_lambda_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            once = Once(memory, "test")
            data = Array(memory, "data", dtype=np.int32, capacity=1)
            data[0] = 0

            once.call(lambda: data.__setitem__(0, 42))
            assert data[0] == 42
            assert once.is_called

        finally:
            Memory.unlink(shm_name)

    def test_nested_once_calls(self):
        """Test nested once calls (once inside once)."""
        shm_name = f"/test_once_nested_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            outer_once = Once(memory, "outer")
            inner_once = Once(memory, "inner")
            counter = Array(memory, "counter", dtype=np.int32, capacity=1)
            counter[0] = 0

            def inner_func():
                counter[0] += 1

            def outer_func():
                counter[0] += 10
                inner_once.call(inner_func)

            outer_once.call(outer_func)
            assert counter[0] == 11  # 10 from outer + 1 from inner

            # Call again - nothing should execute
            outer_once.call(outer_func)
            assert counter[0] == 11

            # But inner can be called directly
            inner_once.call(inner_func)
            assert counter[0] == 11  # Inner already called

        finally:
            Memory.unlink(shm_name)
