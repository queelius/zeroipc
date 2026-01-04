"""Tests for Monitor implementation."""

import os
import time
import threading
import pytest
import numpy as np

from zeroipc import Memory, Monitor, Array


class TestMonitorBasic:
    """Basic Monitor functionality tests."""

    def test_create_monitor(self):
        """Test creating a new monitor."""
        shm_name = f"/test_mon_create_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mon = Monitor(memory, "test_monitor")

            assert mon.name == "test_monitor"
            assert mon.waiting_count == 0

        finally:
            Memory.unlink(shm_name)

    def test_open_existing_monitor(self):
        """Test opening an existing monitor."""
        shm_name = f"/test_mon_open_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)

            # Create monitor
            mon1 = Monitor(memory, "existing")
            assert mon1.waiting_count == 0

            # Open existing monitor
            mon2 = Monitor(memory, "existing", create_if_missing=False)
            assert mon2.name == "existing"

        finally:
            Memory.unlink(shm_name)

    def test_lock_unlock(self):
        """Test basic lock and unlock operations."""
        shm_name = f"/test_mon_lock_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mon = Monitor(memory, "test")

            # Lock and unlock
            assert mon.lock() == True
            mon.unlock()

            # Lock again
            assert mon.lock() == True
            mon.unlock()

        finally:
            Memory.unlink(shm_name)

    def test_try_lock(self):
        """Test try_lock operation."""
        shm_name = f"/test_mon_trylock_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mon = Monitor(memory, "test")

            # Try lock should succeed
            assert mon.try_lock() == True

            # Try lock again should fail (already locked)
            assert mon.try_lock() == False

            mon.unlock()

        finally:
            Memory.unlink(shm_name)

    def test_context_manager(self):
        """Test monitor as context manager."""
        shm_name = f"/test_mon_context_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mon = Monitor(memory, "test")

            with mon:
                # Inside context - locked
                assert mon.try_lock() == False

            # Outside context - unlocked
            assert mon.try_lock() == True
            mon.unlock()

        finally:
            Memory.unlink(shm_name)

    def test_repr(self):
        """Test string representation."""
        shm_name = f"/test_mon_repr_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mon = Monitor(memory, "test_monitor")

            repr_str = repr(mon)
            assert "Monitor" in repr_str
            assert "test_monitor" in repr_str
            assert "waiting=0" in repr_str

        finally:
            Memory.unlink(shm_name)


class TestMonitorWaitNotify:
    """Wait and notify tests for Monitor."""

    def test_notify_one_basic(self):
        """Test basic notify_one operation."""
        shm_name = f"/test_mon_notify_one_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mon = Monitor(memory, "test")
            ready = Array(memory, "ready", dtype=np.int32, capacity=1)
            ready[0] = 0

            def waiter():
                mon.lock()
                mon.wait(lambda: ready[0] == 1)
                mon.unlock()

            t = threading.Thread(target=waiter)
            t.start()

            time.sleep(0.02)  # Let thread start waiting

            # Signal
            mon.lock()
            ready[0] = 1
            mon.notify_one()
            mon.unlock()

            t.join(timeout=1.0)
            assert not t.is_alive()
            assert ready[0] == 1

        finally:
            Memory.unlink(shm_name)

    def test_notify_all_basic(self):
        """Test basic notify_all operation."""
        shm_name = f"/test_mon_notify_all_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mon = Monitor(memory, "test")
            counter = Array(memory, "counter", dtype=np.int32, capacity=1)
            counter[0] = 0
            woken = []

            def waiter(tid):
                mon.lock()
                mon.wait(lambda: counter[0] >= 10)
                woken.append(tid)
                mon.unlock()

            # Start 4 waiters
            threads = [threading.Thread(target=waiter, args=(i,)) for i in range(4)]
            for t in threads:
                t.start()

            time.sleep(0.05)  # Let all threads start waiting

            # Signal all
            mon.lock()
            counter[0] = 10
            mon.notify_all()
            mon.unlock()

            for t in threads:
                t.join(timeout=1.0)

            assert len(woken) == 4
            assert set(woken) == {0, 1, 2, 3}

        finally:
            Memory.unlink(shm_name)

    def test_predicate_wait(self):
        """Test predicate-based wait."""
        shm_name = f"/test_mon_predicate_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mon = Monitor(memory, "test")
            value = Array(memory, "value", dtype=np.int32, capacity=1)
            value[0] = 0

            def waiter():
                mon.lock()
                # Wait until value >= 5
                mon.wait(lambda: value[0] >= 5)
                mon.unlock()

            t = threading.Thread(target=waiter)
            t.start()

            time.sleep(0.02)  # Let thread start waiting

            # Increment and notify multiple times
            for i in range(1, 6):
                mon.lock()
                value[0] = i
                mon.notify_one()
                mon.unlock()
                time.sleep(0.01)

            t.join(timeout=1.0)
            assert not t.is_alive()
            assert value[0] >= 5

        finally:
            Memory.unlink(shm_name)

    def test_wait_timeout(self):
        """Test wait with timeout."""
        shm_name = f"/test_mon_wait_timeout_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mon = Monitor(memory, "test")
            ready = Array(memory, "ready", dtype=np.int32, capacity=1)
            ready[0] = 0

            # Should timeout (condition never met)
            mon.lock()
            result = mon.wait(lambda: ready[0] == 1, timeout=0.1)
            mon.unlock()

            assert result == False
            assert ready[0] == 0

        finally:
            Memory.unlink(shm_name)

    def test_wait_timeout_success(self):
        """Test wait with timeout that succeeds."""
        shm_name = f"/test_mon_timeout_ok_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mon = Monitor(memory, "test")
            ready = Array(memory, "ready", dtype=np.int32, capacity=1)
            ready[0] = 0

            def signaler():
                time.sleep(0.05)
                mon.lock()
                ready[0] = 1
                mon.notify_one()
                mon.unlock()

            t = threading.Thread(target=signaler)
            t.start()

            # Should succeed before timeout
            mon.lock()
            result = mon.wait(lambda: ready[0] == 1, timeout=1.0)
            mon.unlock()

            assert result == True
            assert ready[0] == 1

            t.join()

        finally:
            Memory.unlink(shm_name)


class TestMonitorPatterns:
    """Common synchronization patterns with Monitor."""

    def test_simple_signal_wait(self):
        """Test simple signal-wait pattern."""
        shm_name = f"/test_mon_signal_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mon = Monitor(memory, "test")
            data = Array(memory, "data", dtype=np.int32, capacity=10)
            data[:] = 0

            def worker():
                mon.lock()
                mon.wait(lambda: data[0] > 0)
                # Process data
                result = np.sum(data[:])
                data[0] = result
                mon.unlock()

            t = threading.Thread(target=worker)
            t.start()

            time.sleep(0.02)  # Let thread start waiting

            # Prepare data and signal
            mon.lock()
            data[:] = np.arange(1, 11)  # [1, 2, 3, ..., 10]
            mon.notify_one()
            mon.unlock()

            t.join(timeout=1.0)

            # Sum should be 1+2+...+10 = 55
            assert data[0] == 55

        finally:
            Memory.unlink(shm_name)

    def test_bounded_buffer_simple(self):
        """Test simple bounded buffer pattern."""
        shm_name = f"/test_mon_buffer_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mon = Monitor(memory, "test")
            buffer = Array(memory, "buffer", dtype=np.int32, capacity=5)
            count = Array(memory, "count", dtype=np.int32, capacity=1)
            count[0] = 0

            produced = []
            consumed = []

            def producer():
                for i in range(3):
                    mon.lock()
                    # Wait for space
                    mon.wait(lambda: count[0] < 5)
                    # Produce
                    buffer[count[0]] = i
                    count[0] += 1
                    produced.append(i)
                    mon.notify_one()
                    mon.unlock()
                    time.sleep(0.01)

            def consumer():
                for _ in range(3):
                    mon.lock()
                    # Wait for data
                    mon.wait(lambda: count[0] > 0)
                    # Consume
                    item = buffer[count[0] - 1]
                    count[0] -= 1
                    consumed.append(item)
                    mon.notify_one()
                    mon.unlock()
                    time.sleep(0.01)

            prod = threading.Thread(target=producer)
            cons = threading.Thread(target=consumer)

            prod.start()
            cons.start()

            prod.join(timeout=2.0)
            cons.join(timeout=2.0)

            assert len(produced) == 3
            assert len(consumed) == 3
            assert produced == [0, 1, 2]
            assert set(consumed) == {0, 1, 2}

        finally:
            Memory.unlink(shm_name)

    def test_multiple_condition_wait(self):
        """Test waiting on multiple conditions."""
        shm_name = f"/test_mon_multi_cond_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mon = Monitor(memory, "test")
            state = Array(memory, "state", dtype=np.int32, capacity=2)
            state[:] = 0

            results = []

            def waiter1():
                mon.lock()
                mon.wait(lambda: state[0] == 1)
                results.append("waiter1")
                mon.unlock()

            def waiter2():
                mon.lock()
                mon.wait(lambda: state[1] == 1)
                results.append("waiter2")
                mon.unlock()

            t1 = threading.Thread(target=waiter1)
            t2 = threading.Thread(target=waiter2)

            t1.start()
            t2.start()

            time.sleep(0.02)

            # Signal waiter2 first
            mon.lock()
            state[1] = 1
            mon.notify_all()
            mon.unlock()

            time.sleep(0.02)

            # Signal waiter1
            mon.lock()
            state[0] = 1
            mon.notify_all()
            mon.unlock()

            t1.join(timeout=1.0)
            t2.join(timeout=1.0)

            assert len(results) == 2
            assert "waiter1" in results
            assert "waiter2" in results

        finally:
            Memory.unlink(shm_name)


class TestMonitorEdgeCases:
    """Edge case tests for Monitor."""

    def test_spurious_wakeup_handling(self):
        """Test that predicate-based wait handles spurious wakeups."""
        shm_name = f"/test_mon_spurious_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mon = Monitor(memory, "test")
            value = Array(memory, "value", dtype=np.int32, capacity=1)
            value[0] = 0
            checks = []

            def waiter():
                mon.lock()
                mon.wait(lambda: (checks.append(value[0]), value[0] == 10)[1])
                mon.unlock()

            t = threading.Thread(target=waiter)
            t.start()

            time.sleep(0.02)

            # Send multiple notifications with incremental changes
            for i in range(1, 11):
                mon.lock()
                value[0] = i
                mon.notify_one()
                mon.unlock()
                time.sleep(0.01)

            t.join(timeout=2.0)

            # Should have checked multiple times
            assert len(checks) >= 10
            assert value[0] == 10

        finally:
            Memory.unlink(shm_name)

    def test_notify_without_waiters(self):
        """Test notify when no threads are waiting."""
        shm_name = f"/test_mon_no_waiters_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mon = Monitor(memory, "test")

            # Notify with no waiters - should not block or error
            mon.lock()
            mon.notify_one()
            mon.notify_all()
            mon.unlock()

            assert True  # If we get here, no error occurred

        finally:
            Memory.unlink(shm_name)

    def test_lock_timeout(self):
        """Test lock with timeout."""
        shm_name = f"/test_mon_lock_timeout_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            mon = Monitor(memory, "test")

            # Lock in main thread
            mon.lock()

            result = []

            def try_lock():
                acquired = mon.lock(timeout=0.1)
                result.append(acquired)
                if acquired:
                    mon.unlock()

            t = threading.Thread(target=try_lock)
            t.start()
            t.join()

            # Should timeout
            assert result[0] == False

            mon.unlock()

        finally:
            Memory.unlink(shm_name)
