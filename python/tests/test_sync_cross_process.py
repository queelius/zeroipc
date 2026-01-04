"""Cross-process tests for synchronization primitives using multiprocessing."""

import os
import time
import multiprocessing
import pytest
import numpy as np

from zeroipc import Memory, Array, Mutex, Once, Event, EventMode, Monitor, RWLock, Signal


def mutex_worker(shm_name: str, iterations: int):
    """Worker that increments counter with mutex protection."""
    mem = Memory(shm_name)
    mutex = Mutex(mem, "test_mutex", create_if_missing=False)
    counter = Array(mem, "counter", dtype=np.int32)

    for _ in range(iterations):
        mutex.lock()
        counter[0] += 1
        mutex.unlock()


def once_worker(shm_name: str):
    """Worker that tries to call once."""
    mem = Memory(shm_name)
    once = Once(mem, "test_once", create_if_missing=False)
    counter = Array(mem, "counter", dtype=np.int32)

    def increment():
        counter[0] += 1

    once.call(increment)


def event_worker(shm_name: str, worker_id: int):
    """Worker that waits for event signal."""
    mem = Memory(shm_name)
    event = Event(mem, "test_event", create_if_missing=False)
    results = Array(mem, "results", dtype=np.int32)

    # Wait for signal
    if event.wait(timeout=5.0):
        results[worker_id] = 1
    else:
        results[worker_id] = -1


def rwlock_reader_worker(shm_name: str, worker_id: int):
    """Worker that reads with RWLock."""
    mem = Memory(shm_name)
    rwlock = RWLock(mem, "test_rwlock", create_if_missing=False)
    data = Array(mem, "data", dtype=np.int32)
    results = Array(mem, "results", dtype=np.int32)

    with rwlock.reader():
        # Record that we were able to read
        results[worker_id] = data[0]
        time.sleep(0.01)  # Hold lock briefly


def rwlock_writer_worker(shm_name: str, value: int):
    """Worker that writes with RWLock."""
    mem = Memory(shm_name)
    rwlock = RWLock(mem, "test_rwlock", create_if_missing=False)
    data = Array(mem, "data", dtype=np.int32)

    with rwlock.writer():
        data[0] = value
        time.sleep(0.01)  # Hold lock briefly


def signal_observer_worker(shm_name: str, worker_id: int):
    """Worker that observes signal changes."""
    mem = Memory(shm_name)
    signal = Signal(mem, "test_signal", dtype=np.int32, create_if_missing=False)
    results = Array(mem, "results", dtype=np.int32)

    version = signal.version
    if signal.wait_for_change(version, timeout=5.0):
        results[worker_id] = signal.get()
    else:
        results[worker_id] = -1


class TestMutexCrossProcess:
    """Cross-process tests for Mutex."""

    def test_mutex_protects_counter(self):
        """Test that mutex protects shared counter across processes."""
        shm_name = f"/test_mutex_xproc_{os.getpid()}"
        num_processes = 4
        iterations_per_process = 100

        try:
            # Create shared memory and structures
            memory = Memory(shm_name, size=1024*1024)
            mutex = Mutex(memory, "test_mutex")
            counter = Array(memory, "counter", dtype=np.int32, capacity=1)
            counter[0] = 0

            # Spawn worker processes
            processes = [
                multiprocessing.Process(
                    target=mutex_worker,
                    args=(shm_name, iterations_per_process)
                )
                for _ in range(num_processes)
            ]

            for p in processes:
                p.start()

            for p in processes:
                p.join(timeout=10)
                assert p.exitcode == 0, f"Process exited with code {p.exitcode}"

            # With proper mutex protection, counter should be exact
            expected = num_processes * iterations_per_process
            assert counter[0] == expected, f"Expected {expected}, got {counter[0]}"

        finally:
            Memory.unlink(shm_name)


class TestOnceCrossProcess:
    """Cross-process tests for Once."""

    def test_once_executes_once_across_processes(self):
        """Test that Once executes exactly once across processes."""
        shm_name = f"/test_once_xproc_{os.getpid()}"
        num_processes = 5

        try:
            memory = Memory(shm_name, size=1024*1024)
            once = Once(memory, "test_once")
            counter = Array(memory, "counter", dtype=np.int32, capacity=1)
            counter[0] = 0

            # Spawn workers that all try to call once
            processes = [
                multiprocessing.Process(target=once_worker, args=(shm_name,))
                for _ in range(num_processes)
            ]

            for p in processes:
                p.start()

            for p in processes:
                p.join(timeout=10)
                assert p.exitcode == 0

            # Should be exactly 1
            assert counter[0] == 1, f"Expected 1, got {counter[0]}"

        finally:
            Memory.unlink(shm_name)


class TestEventCrossProcess:
    """Cross-process tests for Event."""

    def test_manual_reset_event_wakes_all_processes(self):
        """Test that manual-reset event wakes all waiting processes."""
        shm_name = f"/test_event_xproc_{os.getpid()}"
        num_workers = 3

        try:
            memory = Memory(shm_name, size=1024*1024)
            event = Event(memory, "test_event", EventMode.MANUAL_RESET)
            results = Array(memory, "results", dtype=np.int32, capacity=num_workers)
            results[:] = 0

            # Spawn workers that wait for event
            processes = [
                multiprocessing.Process(
                    target=event_worker,
                    args=(shm_name, i)
                )
                for i in range(num_workers)
            ]

            for p in processes:
                p.start()

            # Wait for workers to start waiting
            time.sleep(0.1)

            # Signal the event
            event.signal()

            for p in processes:
                p.join(timeout=10)
                assert p.exitcode == 0

            # All workers should have been woken
            assert all(results[i] == 1 for i in range(num_workers))

        finally:
            Memory.unlink(shm_name)


class TestRWLockCrossProcess:
    """Cross-process tests for RWLock."""

    def test_multiple_readers_concurrent(self):
        """Test that multiple reader processes can hold lock simultaneously."""
        shm_name = f"/test_rwlock_xproc_{os.getpid()}"
        num_readers = 4

        try:
            memory = Memory(shm_name, size=1024*1024)
            rwlock = RWLock(memory, "test_rwlock")
            data = Array(memory, "data", dtype=np.int32, capacity=1)
            results = Array(memory, "results", dtype=np.int32, capacity=num_readers)
            data[0] = 42
            results[:] = 0

            # Spawn reader processes
            processes = [
                multiprocessing.Process(
                    target=rwlock_reader_worker,
                    args=(shm_name, i)
                )
                for i in range(num_readers)
            ]

            for p in processes:
                p.start()

            for p in processes:
                p.join(timeout=10)
                assert p.exitcode == 0

            # All readers should have read the value
            assert all(results[i] == 42 for i in range(num_readers))

        finally:
            Memory.unlink(shm_name)

    def test_writer_exclusivity_across_processes(self):
        """Test that writer has exclusive access across processes."""
        shm_name = f"/test_rwlock_writer_xproc_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            rwlock = RWLock(memory, "test_rwlock")
            data = Array(memory, "data", dtype=np.int32, capacity=1)
            data[0] = 0

            # Spawn writer processes that each set a different value
            processes = [
                multiprocessing.Process(
                    target=rwlock_writer_worker,
                    args=(shm_name, i + 1)
                )
                for i in range(3)
            ]

            for p in processes:
                p.start()

            for p in processes:
                p.join(timeout=10)
                assert p.exitcode == 0

            # Data should have one of the written values (1, 2, or 3)
            assert data[0] in [1, 2, 3]

        finally:
            Memory.unlink(shm_name)


class TestSignalCrossProcess:
    """Cross-process tests for Signal."""

    def test_signal_change_notification_across_processes(self):
        """Test that signal changes are observed across processes."""
        shm_name = f"/test_signal_xproc_{os.getpid()}"
        num_observers = 3

        try:
            memory = Memory(shm_name, size=1024*1024)
            signal = Signal(memory, "test_signal", dtype=np.int32, initial_value=0)
            results = Array(memory, "results", dtype=np.int32, capacity=num_observers)
            results[:] = 0

            # Spawn observer processes
            processes = [
                multiprocessing.Process(
                    target=signal_observer_worker,
                    args=(shm_name, i)
                )
                for i in range(num_observers)
            ]

            for p in processes:
                p.start()

            # Wait for observers to start watching
            time.sleep(0.1)

            # Update the signal
            signal.set(42)

            for p in processes:
                p.join(timeout=10)
                assert p.exitcode == 0

            # All observers should have seen the new value
            assert all(results[i] == 42 for i in range(num_observers))

        finally:
            Memory.unlink(shm_name)


class TestMonitorCrossProcess:
    """Cross-process tests for Monitor."""

    def test_monitor_wait_notify_across_processes(self):
        """Test basic monitor wait/notify across processes."""
        shm_name = f"/test_monitor_xproc_{os.getpid()}"

        def waiter_process(shm_name):
            mem = Memory(shm_name)
            mon = Monitor(mem, "test_monitor", create_if_missing=False)
            ready = Array(mem, "ready", dtype=np.int32)
            result = Array(mem, "result", dtype=np.int32)

            mon.lock()
            mon.wait(lambda: ready[0] == 1, timeout=5.0)
            result[0] = 1
            mon.unlock()

        try:
            memory = Memory(shm_name, size=1024*1024)
            mon = Monitor(memory, "test_monitor")
            ready = Array(memory, "ready", dtype=np.int32, capacity=1)
            result = Array(memory, "result", dtype=np.int32, capacity=1)
            ready[0] = 0
            result[0] = 0

            # Start waiter process
            waiter = multiprocessing.Process(target=waiter_process, args=(shm_name,))
            waiter.start()

            # Wait for waiter to start
            time.sleep(0.1)

            # Signal the waiter
            mon.lock()
            ready[0] = 1
            mon.notify_all()
            mon.unlock()

            waiter.join(timeout=10)
            assert waiter.exitcode == 0

            # Waiter should have set result
            assert result[0] == 1

        finally:
            Memory.unlink(shm_name)
