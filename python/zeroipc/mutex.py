"""Mutex implementation for shared memory."""

from typing import Optional

from .memory import Memory
from .semaphore import Semaphore


class Mutex:
    """
    Mutex for mutual exclusion across processes.

    Provides RAII-compatible mutual exclusion synchronization. Works across
    processes sharing the same memory region. Internally implemented as a
    binary semaphore (initial count = 1, max_count = 1).

    Thread-safe and process-safe.

    Example:
        >>> mem = Memory("/data", 1024 * 1024)
        >>> mtx = Mutex(mem, "data_mutex")
        >>>
        >>> # Manual locking
        >>> mtx.lock()
        >>> # ...critical section...
        >>> mtx.unlock()
        >>>
        >>> # Context manager (RAII-style)
        >>> with mtx:
        >>>     # ...critical section...
        >>>     pass  # Automatically unlocked
    """

    def __init__(self, memory: Memory, name: str, create_if_missing: bool = True):
        """
        Create or open a Mutex.

        Args:
            memory: Memory instance
            name: Mutex identifier
            create_if_missing: If True, creates new mutex; if False, opens existing

        Raises:
            RuntimeError: If mutex not found when create_if_missing=False
        """
        self.memory = memory
        self.name = name

        # Try to find existing mutex/semaphore
        entry = memory.table.find(name)

        if entry is None:
            if not create_if_missing:
                raise RuntimeError(f"Mutex '{name}' not found")

            # Create new binary semaphore (count=1, max_count=1 for mutex)
            self._sem = Semaphore(memory, name, initial_count=1, max_count=1)
        else:
            # Open existing semaphore
            self._sem = Semaphore(memory, name, initial_count=None)

            # Verify it's a binary semaphore (mutex)
            if self._sem.max_count != 1:
                raise RuntimeError(
                    f"'{name}' is not a mutex (max_count={self._sem.max_count}, expected 1)"
                )

    def lock(self, timeout: Optional[float] = None) -> bool:
        """
        Lock the mutex.

        Blocks until the mutex is acquired or timeout expires.
        If another thread/process holds the lock, this will wait until released.

        Args:
            timeout: Maximum time to wait in seconds (None for infinite)

        Returns:
            True if lock acquired, False if timed out
        """
        return self._sem.acquire(timeout=timeout)

    def try_lock(self) -> bool:
        """
        Try to lock the mutex without blocking.

        Returns:
            True if lock acquired, False if already locked
        """
        return self._sem.try_acquire()

    def unlock(self):
        """
        Unlock the mutex.

        Must be called by the same thread/process that locked it.

        Raises:
            OverflowError: If mutex is already unlocked
        """
        self._sem.release()

    @property
    def is_locked(self) -> bool:
        """Check if mutex is currently locked."""
        return self._sem.count == 0

    def __enter__(self):
        """Context manager entry: lock the mutex."""
        self.lock()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit: unlock the mutex."""
        self.unlock()
        return False

    def __repr__(self):
        status = "locked" if self.is_locked else "unlocked"
        return f"Mutex(name='{self.name}', status={status})"
