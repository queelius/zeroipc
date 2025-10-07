"""
Test suite for Future codata structure in shared memory.
"""

import os
import threading
import time
import pytest
import numpy as np

from zeroipc import Memory
from zeroipc.future import Future, FutureState, Promise


class TestFutureBasic:
    """Basic Future functionality tests."""

    def test_create_future(self):
        """Test creating a new future."""
        shm_name = f"/test_future_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Create future for int32 value
            future = Future(memory, "test_future", dtype=np.int32)

            assert future.name == "test_future"
            assert future.is_ready() == False
            assert future.get_status() == FutureState.PENDING

        finally:
            Memory.unlink(shm_name)

    def test_set_and_get_value(self):
        """Test setting and getting future value."""
        shm_name = f"/test_future_value_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            future = Future(memory, "value_future", dtype=np.int32)

            # Set value
            assert future.set_value(42) == True
            assert future.is_ready() == True
            assert future.get_status() == FutureState.READY

            # Get value
            assert future.get() == 42

            # Try to set again (should fail)
            assert future.set_value(100) == False
            assert future.get() == 42  # Value unchanged

        finally:
            Memory.unlink(shm_name)

    def test_set_exception(self):
        """Test setting exception on future."""
        shm_name = f"/test_future_except_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            future = Future(memory, "except_future", dtype=np.int32)

            # Set exception
            error_code = 404
            assert future.set_exception(error_code) == True
            assert future.is_ready() == True
            assert future.get_status() == FutureState.ERROR

            # Getting value should raise or return None
            with pytest.raises(RuntimeError):
                future.get()

        finally:
            Memory.unlink(shm_name)

    def test_wait_for_value(self):
        """Test waiting for future value."""
        shm_name = f"/test_future_wait_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            future = Future(memory, "wait_future", dtype=np.int32)

            result = []

            def setter():
                time.sleep(0.1)
                future.set_value(100)

            def getter():
                val = future.wait(timeout=1.0)
                result.append(val)

            # Start threads
            set_thread = threading.Thread(target=setter)
            get_thread = threading.Thread(target=getter)

            get_thread.start()
            set_thread.start()

            set_thread.join()
            get_thread.join()

            assert result[0] == 100

        finally:
            Memory.unlink(shm_name)

    def test_wait_timeout(self):
        """Test wait timeout on future."""
        shm_name = f"/test_future_timeout_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            future = Future(memory, "timeout_future", dtype=np.int32)

            # Wait with short timeout
            start = time.time()
            result = future.wait(timeout=0.1)
            elapsed = time.time() - start

            assert result is None  # Timed out
            assert elapsed < 0.2  # Should timeout quickly

        finally:
            Memory.unlink(shm_name)

    def test_try_get(self):
        """Test non-blocking try_get."""
        shm_name = f"/test_future_try_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            future = Future(memory, "try_future", dtype=np.int32)

            # Try get when not ready
            assert future.try_get() is None

            # Set value
            future.set_value(50)

            # Try get when ready
            assert future.try_get() == 50

        finally:
            Memory.unlink(shm_name)

    def test_then_callback(self):
        """Test then() callback chaining."""
        shm_name = f"/test_future_then_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            future = Future(memory, "then_future", dtype=np.int32)

            result = []

            def callback(value):
                result.append(value * 2)

            # Register callback
            future.then(callback)

            # Set value (should trigger callback)
            future.set_value(10)

            # Give callback time to execute
            time.sleep(0.01)

            assert 20 in result

        finally:
            Memory.unlink(shm_name)


class TestPromise:
    """Test Promise wrapper for Future."""

    def test_promise_future_pair(self):
        """Test Promise-Future pair."""
        shm_name = f"/test_promise_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Create promise-future pair
            promise = Promise(memory, "promise_test", dtype=np.int32)
            future = promise.get_future()

            assert future.is_ready() == False

            # Fulfill promise
            promise.set_value(100)

            assert future.is_ready() == True
            assert future.get() == 100

        finally:
            Memory.unlink(shm_name)

    def test_promise_exception(self):
        """Test setting exception through promise."""
        shm_name = f"/test_promise_except_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            promise = Promise(memory, "promise_except", dtype=np.int32)
            future = promise.get_future()

            # Set exception through promise
            promise.set_exception(500)

            assert future.get_status() == FutureState.ERROR

            with pytest.raises(RuntimeError):
                future.get()

        finally:
            Memory.unlink(shm_name)


class TestFutureConcurrency:
    """Test concurrent Future operations."""

    def test_multiple_waiters(self):
        """Test multiple threads waiting on same future."""
        shm_name = f"/test_future_multi_wait_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            future = Future(memory, "multi_wait", dtype=np.int32)

            results = []
            lock = threading.Lock()

            def waiter():
                val = future.wait(timeout=1.0)
                with lock:
                    results.append(val)

            # Create waiter threads
            threads = []
            for _ in range(5):
                t = threading.Thread(target=waiter)
                threads.append(t)
                t.start()

            # Set value after a delay
            time.sleep(0.1)
            future.set_value(42)

            for t in threads:
                t.join()

            # All waiters should get the same value
            assert len(results) == 5
            assert all(r == 42 for r in results)

        finally:
            Memory.unlink(shm_name)

    def test_concurrent_set_attempts(self):
        """Test concurrent attempts to set future value."""
        shm_name = f"/test_future_race_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            future = Future(memory, "race_future", dtype=np.int32)

            success_count = [0]
            lock = threading.Lock()

            def setter(value):
                if future.set_value(value):
                    with lock:
                        success_count[0] += 1

            # Create setter threads
            threads = []
            for i in range(10):
                t = threading.Thread(target=setter, args=(i,))
                threads.append(t)
                t.start()

            for t in threads:
                t.join()

            # Only one should succeed
            assert success_count[0] == 1
            assert future.is_ready() == True

            # Value should be one of the attempted values
            val = future.get()
            assert 0 <= val < 10

        finally:
            Memory.unlink(shm_name)


class TestFutureDataTypes:
    """Test Futures with different data types."""

    def test_float_future(self):
        """Test future with float value."""
        shm_name = f"/test_future_float_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            future = Future(memory, "float_future", dtype=np.float32)

            future.set_value(3.14159)
            assert abs(future.get() - 3.14159) < 0.0001

        finally:
            Memory.unlink(shm_name)

    def test_int64_future(self):
        """Test future with 64-bit integer."""
        shm_name = f"/test_future_int64_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            future = Future(memory, "int64_future", dtype=np.int64)

            large_val = 10**15
            future.set_value(large_val)
            assert future.get() == large_val

        finally:
            Memory.unlink(shm_name)

    def test_double_future(self):
        """Test future with double precision."""
        shm_name = f"/test_future_double_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            future = Future(memory, "double_future", dtype=np.float64)

            pi = 3.141592653589793
            future.set_value(pi)
            assert future.get() == pi

        finally:
            Memory.unlink(shm_name)


class TestFutureEdgeCases:
    """Test edge cases and error conditions."""

    def test_get_before_set(self):
        """Test getting value before it's set."""
        shm_name = f"/test_future_early_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            future = Future(memory, "early_future", dtype=np.int32)

            # Non-blocking get should return None
            assert future.try_get() is None

            # Blocking get with timeout should timeout
            assert future.wait(timeout=0.01) is None

        finally:
            Memory.unlink(shm_name)

    def test_missing_dtype_error(self):
        """Test that missing dtype raises error."""
        shm_name = f"/test_future_dtype_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            with pytest.raises(TypeError, match="dtype is required"):
                Future(memory, "bad_future")

        finally:
            Memory.unlink(shm_name)

    def test_double_set_value(self):
        """Test setting value twice."""
        shm_name = f"/test_future_double_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            future = Future(memory, "double_future", dtype=np.int32)

            # First set succeeds
            assert future.set_value(10) == True

            # Second set fails
            assert future.set_value(20) == False

            # Value remains first one
            assert future.get() == 10

        finally:
            Memory.unlink(shm_name)

    def test_set_both_value_and_exception(self):
        """Test setting both value and exception."""
        shm_name = f"/test_future_both_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            future = Future(memory, "both_future", dtype=np.int32)

            # Set value first
            future.set_value(100)

            # Try to set exception (should fail)
            assert future.set_exception(404) == False

            # Future still has value
            assert future.get() == 100

        finally:
            Memory.unlink(shm_name)


class TestFuturePersistence:
    """Test future persistence across processes."""

    def test_reopen_future(self):
        """Test reopening an existing future."""
        shm_name = f"/test_future_persist_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Create and set future
            future1 = Future(memory, "persist_future", dtype=np.int32)
            future1.set_value(42)

            # Open existing future
            future2 = Future(memory, "persist_future", dtype=np.int32)

            # Should see the value
            assert future2.is_ready() == True
            assert future2.get() == 42

        finally:
            Memory.unlink(shm_name)


class TestFutureNameValidation:
    """Test Future name validation."""

    def test_long_name_error(self):
        """Test that names longer than 31 characters raise error."""
        shm_name = f"/test_future_longname_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Name with 32 characters (too long)
            long_name = "a" * 32

            with pytest.raises(ValueError, match="Name too long"):
                Future(memory, long_name, dtype=np.int32)

        finally:
            Memory.unlink(shm_name)

    def test_open_nonexistent_future(self):
        """Test opening a non-existent future with open_existing=True."""
        shm_name = f"/test_future_nonexist_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Try to open non-existent future
            with pytest.raises(RuntimeError, match="Future not found"):
                Future(memory, "nonexistent", dtype=np.int32, open_existing=True)

        finally:
            Memory.unlink(shm_name)


class TestFutureComplexTypes:
    """Test futures with complex numpy types."""

    def test_uint64_future(self):
        """Test future with uint64 type."""
        shm_name = f"/test_future_uint64_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            future = Future(memory, "uint64_future", dtype=np.uint64)

            large_val = 18446744073709551615  # Max uint64
            future.set_value(large_val)
            assert future.get() == large_val

        finally:
            Memory.unlink(shm_name)

    def test_complex_dtype_future(self):
        """Test future with complex numpy dtype."""
        shm_name = f"/test_future_complex_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            # Use structured array dtype
            dt = np.dtype([('x', 'i4'), ('y', 'f8')])
            future = Future(memory, "complex_future", dtype=dt)

            # Complex value as structured array element
            val = np.array([(42, 3.14)], dtype=dt)[0]
            future.set_value(val)
            result = future.get()
            assert result['x'] == 42
            assert abs(result['y'] - 3.14) < 1e-10

        finally:
            Memory.unlink(shm_name)


class TestFutureAdditionalEdgeCases:
    """Test additional edge cases for better coverage."""

    def test_state_queries(self):
        """Test various state query methods."""
        shm_name = f"/test_future_states_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            future = Future(memory, "state_future", dtype=np.int32)

            # Check initial state
            assert future.is_pending() == True
            assert future.is_ready() == False
            assert future.is_error() == False
            assert future.get_state() == FutureState.PENDING
            assert future.get_status() == FutureState.PENDING

            # Set value
            future.set_value(42)

            # Check ready state
            assert future.is_pending() == False
            assert future.is_ready() == True
            assert future.is_error() == False
            assert future.get_state() == FutureState.READY

        finally:
            Memory.unlink(shm_name)

    def test_error_state_queries(self):
        """Test state queries when in error state."""
        shm_name = f"/test_future_error_states_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            future = Future(memory, "error_state_future", dtype=np.int32)

            # Set error
            future.set_error("Test error")

            # Check error state
            assert future.is_pending() == False
            assert future.is_ready() == True  # Error is considered "ready"
            assert future.is_error() == True
            assert future.get_state() == FutureState.ERROR

        finally:
            Memory.unlink(shm_name)

    def test_try_get_exception(self):
        """Test try_get when future has exception."""
        shm_name = f"/test_future_try_except_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            future = Future(memory, "try_except", dtype=np.int32)

            # Set exception
            future.set_exception("Test error")

            # try_get should raise
            with pytest.raises(RuntimeError, match="Test error"):
                future.try_get()

        finally:
            Memory.unlink(shm_name)

    def test_waiter_count(self):
        """Test waiter count tracking."""
        shm_name = f"/test_future_waiters_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            future = Future(memory, "waiter_future", dtype=np.int32)

            # Initially no waiters
            assert future.get_waiter_count() == 0

            # Start a waiter in a thread
            result = []
            def waiter():
                val = future.wait(timeout=0.5)
                result.append(val)

            t = threading.Thread(target=waiter)
            t.start()

            # Give thread time to start waiting
            time.sleep(0.05)

            # Should see one waiter
            assert future.get_waiter_count() >= 1

            # Set value to unblock waiter
            future.set_value(42)
            t.join()

            # Waiter should have gotten the value
            assert result[0] == 42

        finally:
            Memory.unlink(shm_name)

    def test_string_representations(self):
        """Test Future __str__ and __repr__ methods."""
        shm_name = f"/test_future_str_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            future = Future(memory, "str_future", dtype=np.int32)

            # Check string representation includes key info
            str_repr = str(future)
            assert "str_future" in str_repr
            assert "PENDING" in str_repr
            assert "int32" in str_repr

            # __repr__ should match __str__
            assert repr(future) == str(future)

            # Check boolean conversion
            assert not bool(future)  # Not ready yet

            future.set_value(42)
            assert bool(future)  # Now ready

        finally:
            Memory.unlink(shm_name)


class TestPromiseAdditional:
    """Test additional Promise functionality."""

    def test_promise_from_existing_future(self):
        """Test creating Promise from existing Future."""
        shm_name = f"/test_promise_existing_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            future = Future(memory, "existing_future", dtype=np.int32)

            # Create promise from existing future
            promise = Promise(future)
            assert promise.get_future() == future

            # Use promise to set value
            promise.set_value(200)
            assert future.get() == 200

        finally:
            Memory.unlink(shm_name)

    def test_promise_missing_params(self):
        """Test Promise creation with missing parameters."""
        shm_name = f"/test_promise_params_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Missing name
            with pytest.raises(ValueError, match="name and dtype are required"):
                Promise(memory, dtype=np.int32)

            # Missing dtype
            with pytest.raises(ValueError, match="name and dtype are required"):
                Promise(memory, name="test")

        finally:
            Memory.unlink(shm_name)

    def test_promise_string_representations(self):
        """Test Promise __str__ and __repr__ methods."""
        shm_name = f"/test_promise_str_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            promise = Promise(memory, "str_promise", dtype=np.int32)

            # Check string representation
            str_repr = str(promise)
            assert "Promise" in str_repr
            assert "str_promise" in str_repr

            # __repr__ should match __str__
            assert repr(promise) == str(promise)

        finally:
            Memory.unlink(shm_name)