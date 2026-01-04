"""Tests for Event implementation."""

import os
import time
import threading
import pytest
import numpy as np

from zeroipc import Memory, Event, EventMode, Array


class TestEventBasic:
    """Basic Event functionality tests."""

    def test_create_auto_reset_event(self):
        """Test creating an auto-reset event."""
        shm_name = f"/test_event_auto_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            event = Event(memory, "test_event", EventMode.AUTO_RESET)

            assert event.name == "test_event"
            assert event.mode == EventMode.AUTO_RESET
            assert event.is_signaled == False

        finally:
            Memory.unlink(shm_name)

    def test_create_manual_reset_event(self):
        """Test creating a manual-reset event."""
        shm_name = f"/test_event_manual_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            event = Event(memory, "test_event", EventMode.MANUAL_RESET)

            assert event.name == "test_event"
            assert event.mode == EventMode.MANUAL_RESET
            assert event.is_signaled == False

        finally:
            Memory.unlink(shm_name)

    def test_open_existing_event(self):
        """Test opening an existing event."""
        shm_name = f"/test_event_open_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)

            # Create event
            event1 = Event(memory, "existing", EventMode.MANUAL_RESET)
            assert not event1.is_signaled

            # Open existing event
            event2 = Event(memory, "existing", create_if_missing=False)
            assert event2.name == "existing"
            assert event2.mode == EventMode.MANUAL_RESET

        finally:
            Memory.unlink(shm_name)

    def test_signal_and_wait_auto_reset(self):
        """Test signal and wait with auto-reset event."""
        shm_name = f"/test_event_signal_auto_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            event = Event(memory, "test", EventMode.AUTO_RESET)

            result = []

            def waiter():
                result.append("waiting")
                event.wait()
                result.append("signaled")

            t = threading.Thread(target=waiter)
            t.start()

            time.sleep(0.01)  # Let thread start waiting

            event.signal()
            t.join()

            assert result == ["waiting", "signaled"]
            # Auto-reset: should not be signaled after one wait consumes it
            assert event.is_signaled == False

        finally:
            Memory.unlink(shm_name)

    def test_signal_and_wait_manual_reset(self):
        """Test signal and wait with manual-reset event."""
        shm_name = f"/test_event_signal_manual_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            event = Event(memory, "test", EventMode.MANUAL_RESET)

            result = []

            def waiter():
                result.append("waiting")
                event.wait()
                result.append("signaled")

            t = threading.Thread(target=waiter)
            t.start()

            time.sleep(0.01)  # Let thread start waiting

            event.signal()
            t.join()

            assert result == ["waiting", "signaled"]
            # Manual-reset: should stay signaled
            assert event.is_signaled == True

        finally:
            Memory.unlink(shm_name)

    def test_reset(self):
        """Test resetting an event."""
        shm_name = f"/test_event_reset_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            event = Event(memory, "test", EventMode.MANUAL_RESET)

            event.signal()
            assert event.is_signaled == True

            event.reset()
            assert event.is_signaled == False

        finally:
            Memory.unlink(shm_name)

    def test_wait_timeout_success(self):
        """Test wait with timeout that succeeds."""
        shm_name = f"/test_event_timeout_ok_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            event = Event(memory, "test", EventMode.MANUAL_RESET)

            def signaler():
                time.sleep(0.05)
                event.signal()

            t = threading.Thread(target=signaler)
            t.start()

            # Should succeed before timeout
            result = event.wait(timeout=1.0)
            assert result == True
            assert event.is_signaled == True

            t.join()

        finally:
            Memory.unlink(shm_name)

    def test_wait_timeout_failure(self):
        """Test wait with timeout that expires."""
        shm_name = f"/test_event_timeout_fail_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            event = Event(memory, "test", EventMode.MANUAL_RESET)

            # Should timeout (event never signaled)
            result = event.wait(timeout=0.1)
            assert result == False
            assert event.is_signaled == False

        finally:
            Memory.unlink(shm_name)

    def test_pulse(self):
        """Test pulse operation."""
        shm_name = f"/test_event_pulse_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            event = Event(memory, "test", EventMode.MANUAL_RESET)

            event.pulse()
            time.sleep(0.01)  # Give pulse time to reset

            # Should be reset after pulse
            assert event.is_signaled == False

        finally:
            Memory.unlink(shm_name)

    def test_repr(self):
        """Test string representation."""
        shm_name = f"/test_event_repr_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            event = Event(memory, "test_event", EventMode.AUTO_RESET)

            repr_str = repr(event)
            assert "Event" in repr_str
            assert "test_event" in repr_str
            assert "AutoReset" in repr_str
            assert "not signaled" in repr_str

            event.signal()
            repr_str = repr(event)
            assert "signaled" in repr_str

        finally:
            Memory.unlink(shm_name)


class TestEventThreading:
    """Multi-threaded Event tests."""

    def test_manual_reset_wakes_all(self):
        """Test that manual-reset event wakes all waiters."""
        shm_name = f"/test_event_wake_all_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            event = Event(memory, "test", EventMode.MANUAL_RESET)
            results = []

            def waiter(tid):
                event.wait()
                results.append(tid)

            # Start multiple waiters
            threads = [threading.Thread(target=waiter, args=(i,)) for i in range(5)]
            for t in threads:
                t.start()

            time.sleep(0.05)  # Let all threads start waiting

            # Signal once
            event.signal()

            # All threads should wake
            for t in threads:
                t.join()

            assert len(results) == 5
            assert set(results) == {0, 1, 2, 3, 4}

        finally:
            Memory.unlink(shm_name)

    def test_auto_reset_wakes_one(self):
        """Test that auto-reset event wakes only one waiter."""
        shm_name = f"/test_event_wake_one_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            event = Event(memory, "test", EventMode.AUTO_RESET)
            counter = Array(memory, "counter", dtype=np.int32, capacity=1)
            counter[0] = 0

            def waiter():
                event.wait()
                counter[0] += 1

            # Start multiple waiters
            threads = [threading.Thread(target=waiter) for _ in range(3)]
            for t in threads:
                t.start()

            time.sleep(0.05)  # Let all threads start waiting

            # Signal once - should wake only one
            event.signal()
            time.sleep(0.02)

            # Only one should have woken
            assert counter[0] == 1

            # Signal again for remaining threads
            event.signal()
            time.sleep(0.02)
            assert counter[0] == 2

            event.signal()
            time.sleep(0.02)
            assert counter[0] == 3

            for t in threads:
                t.join()

        finally:
            Memory.unlink(shm_name)

    def test_producer_consumer_with_event(self):
        """Test producer-consumer pattern with event."""
        shm_name = f"/test_event_prodcons_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            ready = Event(memory, "ready", EventMode.AUTO_RESET)
            data = Array(memory, "data", dtype=np.int32, capacity=1)
            data[0] = 0

            def producer():
                for i in range(10):
                    data[0] = i
                    ready.signal()
                    time.sleep(0.01)

            def consumer():
                for i in range(10):
                    ready.wait()
                    assert data[0] == i

            prod = threading.Thread(target=producer)
            cons = threading.Thread(target=consumer)

            cons.start()
            time.sleep(0.005)  # Let consumer start waiting
            prod.start()

            prod.join()
            cons.join()

        finally:
            Memory.unlink(shm_name)

    def test_multiple_signals_manual_reset(self):
        """Test multiple signals on manual-reset event."""
        shm_name = f"/test_event_multi_signal_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            event = Event(memory, "test", EventMode.MANUAL_RESET)

            # Signal multiple times
            event.signal()
            event.signal()
            event.signal()

            # Should still be signaled
            assert event.is_signaled == True

            # All waiters should pass immediately
            event.wait()
            event.wait()
            event.wait()

            # Still signaled
            assert event.is_signaled == True

        finally:
            Memory.unlink(shm_name)


class TestEventEdgeCases:
    """Edge case tests for Event."""

    def test_wait_already_signaled_manual(self):
        """Test waiting on already-signaled manual-reset event."""
        shm_name = f"/test_event_already_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            event = Event(memory, "test", EventMode.MANUAL_RESET)

            # Signal first
            event.signal()

            # Wait should return immediately
            start = time.time()
            result = event.wait(timeout=1.0)
            elapsed = time.time() - start

            assert result == True
            assert elapsed < 0.1  # Should be nearly instant

        finally:
            Memory.unlink(shm_name)

    def test_signal_before_wait_auto_reset(self):
        """Test signaling before waiting with auto-reset event."""
        shm_name = f"/test_event_signal_first_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            event = Event(memory, "test", EventMode.AUTO_RESET)

            # Signal first
            event.signal()

            # Wait should consume the signal
            result = event.wait(timeout=0.1)
            assert result == True

            # Event should be reset now
            assert event.is_signaled == False

        finally:
            Memory.unlink(shm_name)

    def test_concurrent_signal_and_wait(self):
        """Test concurrent signaling and waiting."""
        shm_name = f"/test_event_concurrent_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            event = Event(memory, "test", EventMode.AUTO_RESET)
            woken = []

            def waiter(tid):
                if event.wait(timeout=1.0):
                    woken.append(tid)

            def signaler():
                for _ in range(5):
                    time.sleep(0.01)
                    event.signal()

            # Start waiters and signaler concurrently
            waiter_threads = [threading.Thread(target=waiter, args=(i,)) for i in range(5)]
            signaler_thread = threading.Thread(target=signaler)

            for t in waiter_threads:
                t.start()
            signaler_thread.start()

            for t in waiter_threads:
                t.join()
            signaler_thread.join()

            # All waiters should have been woken
            assert len(woken) == 5

        finally:
            Memory.unlink(shm_name)
