"""Error handling tests for synchronization primitives."""

import os
import pytest
import numpy as np

from zeroipc import Memory, Mutex, Once, Event, EventMode, Monitor, RWLock, Signal


class TestMutexErrorHandling:
    """Error handling tests for Mutex."""

    def test_open_nonexistent_mutex_raises(self):
        """Test that opening non-existent mutex raises error."""
        shm_name = f"/test_mutex_err_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)

            with pytest.raises(RuntimeError, match="not found"):
                Mutex(memory, "nonexistent", create_if_missing=False)

        finally:
            Memory.unlink(shm_name)

    def test_double_unlock_raises_overflow(self):
        """Test that unlocking without lock held raises OverflowError."""
        shm_name = f"/test_mutex_double_unlock_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mutex = Mutex(memory, "test")

            # First lock/unlock is fine
            mutex.lock()
            mutex.unlock()

            # Second unlock without lock raises OverflowError
            # (binary semaphore can't exceed max_count=1)
            with pytest.raises(OverflowError, match="exceed maximum"):
                mutex.unlock()

        finally:
            Memory.unlink(shm_name)


class TestOnceErrorHandling:
    """Error handling tests for Once."""

    def test_open_nonexistent_once_raises(self):
        """Test that opening non-existent once raises error."""
        shm_name = f"/test_once_err_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)

            with pytest.raises(RuntimeError, match="not found"):
                Once(memory, "nonexistent", create_if_missing=False)

        finally:
            Memory.unlink(shm_name)

    def test_exception_during_call_allows_retry(self):
        """Test that exception during call allows retry."""
        shm_name = f"/test_once_exception_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            once = Once(memory, "test")
            call_count = [0]

            def failing_init():
                call_count[0] += 1
                if call_count[0] == 1:
                    raise ValueError("First call fails")
                # Second call succeeds

            # First call should raise
            with pytest.raises(ValueError, match="First call fails"):
                once.call(failing_init)

            # After reset, second call should succeed
            once.reset()
            once.call(failing_init)  # Should not raise
            assert call_count[0] == 2

        finally:
            Memory.unlink(shm_name)


class TestEventErrorHandling:
    """Error handling tests for Event."""

    def test_open_nonexistent_event_raises(self):
        """Test that opening non-existent event raises error."""
        shm_name = f"/test_event_err_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)

            with pytest.raises(RuntimeError, match="not found"):
                Event(memory, "nonexistent", create_if_missing=False)

        finally:
            Memory.unlink(shm_name)

    def test_wait_timeout_returns_false(self):
        """Test that wait with timeout returns False when not signaled."""
        shm_name = f"/test_event_timeout_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            event = Event(memory, "test", EventMode.MANUAL_RESET)

            # Wait should timeout and return False
            result = event.wait(timeout=0.1)
            assert result is False

        finally:
            Memory.unlink(shm_name)


class TestMonitorErrorHandling:
    """Error handling tests for Monitor."""

    def test_open_nonexistent_monitor_raises(self):
        """Test that opening non-existent monitor raises error."""
        shm_name = f"/test_monitor_err_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)

            with pytest.raises(RuntimeError, match="not found"):
                Monitor(memory, "nonexistent", create_if_missing=False)

        finally:
            Memory.unlink(shm_name)

    def test_wait_timeout_returns_false(self):
        """Test that wait with timeout returns False when not notified."""
        shm_name = f"/test_monitor_timeout_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            monitor = Monitor(memory, "test")

            monitor.lock()
            result = monitor.wait(lambda: False, timeout=0.1)
            monitor.unlock()

            assert result is False

        finally:
            Memory.unlink(shm_name)

    def test_notify_without_waiters_is_safe(self):
        """Test that notify without waiters doesn't cause issues."""
        shm_name = f"/test_monitor_nowait_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            monitor = Monitor(memory, "test")

            # Should not raise or cause issues
            monitor.lock()
            monitor.notify_one()
            monitor.notify_all()
            monitor.unlock()

        finally:
            Memory.unlink(shm_name)


class TestRWLockErrorHandling:
    """Error handling tests for RWLock."""

    def test_open_nonexistent_rwlock_raises(self):
        """Test that opening non-existent rwlock raises error."""
        shm_name = f"/test_rwlock_err_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)

            with pytest.raises(RuntimeError, match="not found"):
                RWLock(memory, "nonexistent", create_if_missing=False)

        finally:
            Memory.unlink(shm_name)

    def test_writer_lock_timeout(self):
        """Test that writer lock timeout returns False when readers active."""
        shm_name = f"/test_rwlock_timeout_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            rwlock = RWLock(memory, "test")

            # Acquire reader lock
            rwlock.reader_lock()

            # Try to get writer lock with timeout (should fail)
            result = rwlock.writer_lock(timeout=0.1)
            assert result is False

            rwlock.reader_unlock()

        finally:
            Memory.unlink(shm_name)


class TestSignalErrorHandling:
    """Error handling tests for Signal."""

    def test_open_nonexistent_signal_raises(self):
        """Test that opening non-existent signal raises error."""
        shm_name = f"/test_signal_err_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)

            with pytest.raises(RuntimeError, match="not found"):
                Signal(memory, "nonexistent", dtype=np.int32, create_if_missing=False)

        finally:
            Memory.unlink(shm_name)

    def test_create_signal_requires_initial_value(self):
        """Test that creating signal requires initial_value."""
        shm_name = f"/test_signal_noinit_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)

            with pytest.raises(ValueError, match="initial_value"):
                Signal(memory, "test", dtype=np.int32)  # No initial_value

        finally:
            Memory.unlink(shm_name)

    def test_wait_for_change_timeout(self):
        """Test that wait_for_change returns False on timeout."""
        shm_name = f"/test_signal_timeout_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            signal = Signal(memory, "test", dtype=np.int32, initial_value=0)

            # Wait for change that never happens
            result = signal.wait_for_change(signal.version, timeout=0.1)
            assert result is False

        finally:
            Memory.unlink(shm_name)
