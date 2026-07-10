"""Monitor for condition variable synchronization across processes."""

import struct
import time
import threading
from typing import Callable, Optional

from .memory import Memory
from .mutex import Mutex
from .semaphore import Semaphore


class Monitor:
    """
    Monitor for condition variable synchronization across processes.

    A Monitor combines a mutex with condition variable semantics, allowing
    threads/processes to wait for conditions to become true. Similar to
    std::condition_variable but works across shared memory.

    Provides:
    - Mutual exclusion via embedded mutex
    - wait() to atomically release lock and block until signaled
    - notify_one() to wake one waiter
    - notify_all() to wake all waiters
    - Predicate-based waiting (handles spurious wakeups)

    Binary layout:
    - Mutex (via Semaphore)
    - Semaphore for condition signaling
    - Atomic counter for waiting threads (4 bytes)

    Example:
        >>> mem = Memory("/sync", 1024 * 1024)
        >>> mon = Monitor(mem, "producer_consumer")
        >>> buffer = Array(mem, "buffer", dtype=np.int32, capacity=100)
        >>> count = Array(mem, "count", dtype=np.int32, capacity=1)
        >>>
        >>> # Producer
        >>> mon.lock()
        >>> mon.wait(lambda: count[0] < 100)  # Wait for space
        >>> buffer[count[0]] = item
        >>> count[0] += 1
        >>> mon.notify_one()
        >>> mon.unlock()
        >>>
        >>> # Consumer
        >>> mon.lock()
        >>> mon.wait(lambda: count[0] > 0)  # Wait for data
        >>> item = buffer[count[0] - 1]
        >>> count[0] -= 1
        >>> mon.notify_one()
        >>> mon.unlock()
    """

    COUNTER_FORMAT = 'I'  # Single uint32 for waiting count
    COUNTER_SIZE = struct.calcsize(COUNTER_FORMAT)

    def __init__(self, memory: Memory, name: str, create_if_missing: bool = True):
        """
        Create or open a Monitor.

        Args:
            memory: Memory instance
            name: Monitor identifier
            create_if_missing: If True, creates new monitor; if False, opens existing

        Raises:
            RuntimeError: If monitor not found when create_if_missing=False
        """
        self.memory = memory
        self.name = name

        mutex_name = name + "_mtx"
        cond_name = name + "_cond"
        counter_name = name + "_count"

        # Try to find existing monitor
        entry = memory.table.find(name)

        if entry is None:
            if not create_if_missing:
                raise RuntimeError(f"Monitor '{name}' not found")

            # Create new monitor components
            self._mutex = Mutex(memory, mutex_name, create_if_missing=True)
            self._cond_sem = Semaphore(memory, cond_name, initial_count=0, max_count=0)

            # Create waiting counter
            self.counter_offset = memory.allocate(counter_name, self.COUNTER_SIZE)
            counter_data = struct.pack(self.COUNTER_FORMAT, 0)
            memory.data[self.counter_offset:self.counter_offset + self.COUNTER_SIZE] = counter_data

            # Add marker entry
            memory.allocate(name, 1)

        else:
            # Open existing monitor
            self._mutex = Mutex(memory, mutex_name, create_if_missing=False)
            self._cond_sem = Semaphore(memory, cond_name, initial_count=None)

            counter_entry = memory.table.find(counter_name)
            if not counter_entry:
                raise RuntimeError(f"Monitor counter '{counter_name}' not found")

            self.counter_offset = counter_entry.offset

        # Lock for atomic counter operations
        self._lock = threading.Lock()

    def _load_waiting_count(self) -> int:
        """Atomically load waiting count."""
        return struct.unpack_from('<I', self.memory.data, self.counter_offset)[0]

    def _store_waiting_count(self, value: int):
        """Atomically store waiting count."""
        struct.pack_into('<I', self.memory.data, self.counter_offset, value)

    def _increment_waiting(self):
        """Atomically increment waiting counter."""
        with self._lock:
            current = self._load_waiting_count()
            self._store_waiting_count(current + 1)

    def _decrement_waiting(self):
        """Atomically decrement waiting counter."""
        with self._lock:
            current = self._load_waiting_count()
            self._store_waiting_count(current - 1)

    def lock(self, timeout: Optional[float] = None) -> bool:
        """
        Lock the monitor's mutex.

        Must be called before wait() or accessing shared data.

        Args:
            timeout: Maximum time to wait in seconds (None for infinite)

        Returns:
            True if lock acquired, False if timeout
        """
        return self._mutex.lock(timeout=timeout)

    def unlock(self):
        """Unlock the monitor's mutex."""
        self._mutex.unlock()

    def try_lock(self) -> bool:
        """
        Try to lock without blocking.

        Returns:
            True if lock acquired, false otherwise
        """
        return self._mutex.try_lock()

    def wait(self, predicate: Optional[Callable[[], bool]] = None,
             timeout: Optional[float] = None) -> bool:
        """
        Wait for notification or predicate to become true.

        Atomically releases the lock and blocks until notify_one() or
        notify_all() is called. When woken, reacquires the lock.

        If a predicate is provided, repeatedly waits until the predicate
        returns True, handling spurious wakeups automatically.

        WARNING: Must hold the lock before calling wait().

        Args:
            predicate: Optional callable that returns bool. If provided,
                      waits until predicate returns True.
            timeout: Maximum time to wait in seconds (None for infinite)

        Returns:
            True if woken (and predicate satisfied if provided),
            False if timeout

        Example:
            >>> mon.lock()
            >>> mon.wait(lambda: buffer_count > 0, timeout=1.0)
            >>> # Now buffer_count is guaranteed > 0 (or timeout occurred)
            >>> mon.unlock()
        """
        if predicate is None:
            # Simple wait without predicate
            return self._wait_impl(timeout)
        else:
            # Predicate-based wait (handles spurious wakeups)
            start_time = time.time() if timeout is not None else None

            while not predicate():
                # Calculate remaining timeout
                remaining = None
                if timeout is not None:
                    elapsed = time.time() - start_time
                    if elapsed >= timeout:
                        return False  # Timeout
                    remaining = timeout - elapsed

                # Wait for notification
                if not self._wait_impl(remaining):
                    # Timeout on wait, check predicate one last time
                    return predicate()

            return True

    def _wait_impl(self, timeout: Optional[float]) -> bool:
        """
        Internal wait implementation.

        Returns:
            True if woken, False if timeout
        """
        # Increment waiting counter
        self._increment_waiting()

        try:
            # Release lock and wait on condition semaphore
            self._mutex.unlock()

            # Wait for signal
            result = self._cond_sem.acquire(timeout=timeout)

            # Reacquire lock
            self._mutex.lock()

            return result

        finally:
            # Decrement waiting counter
            self._decrement_waiting()

    def notify_one(self):
        """
        Wake one waiting thread/process.

        If multiple threads are waiting, wakes one arbitrarily.
        Does nothing if no threads are waiting.

        NOTE: Should be called while holding the lock for proper semantics.
        """
        # Only release when a waiter is registered, matching the C++
        # implementation. Releasing unconditionally accumulates permits in
        # the unbounded semaphore, making a later bare wait() (no
        # predicate) return spuriously.
        if self._load_waiting_count() > 0:
            self._cond_sem.release()

    def notify_all(self):
        """
        Wake all waiting threads/processes.

        Wakes all currently waiting threads. New waiters are not affected.

        NOTE: Should be called while holding the lock for proper semantics.
        """
        count = self._load_waiting_count()
        for _ in range(count):
            self._cond_sem.release()

    @property
    def waiting_count(self) -> int:
        """Get number of threads/processes currently waiting."""
        return self._load_waiting_count()

    def __enter__(self):
        """Context manager entry: lock the monitor."""
        self.lock()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit: unlock the monitor."""
        self.unlock()
        return False

    def __repr__(self):
        return f"Monitor(name='{self.name}', waiting={self.waiting_count})"
