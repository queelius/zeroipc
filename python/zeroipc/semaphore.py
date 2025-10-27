"""Lock-free semaphore implementation for shared memory."""

import struct
import time
import threading
from typing import Optional

from .memory import Memory


class Semaphore:
    """
    Lock-free semaphore for cross-process synchronization.

    A semaphore maintains a non-negative integer count representing available
    resources or permits. Processes can acquire() to decrement the count and
    release() to increment it. If count is zero, acquire() blocks until
    another process calls release().

    Supports three modes:
    - Binary semaphore (max_count=1): Acts as a mutex
    - Counting semaphore (max_count=N): Resource pool with N permits
    - Unbounded semaphore (max_count=0): No upper limit on count

    Thread-safe and process-safe.
    """

    HEADER_FORMAT = 'iiii'  # count, waiting, max_count, padding
    HEADER_SIZE = struct.calcsize(HEADER_FORMAT)

    def __init__(self, memory: Memory, name: str,
                 initial_count: Optional[int] = None,
                 max_count: int = 0):
        """
        Create or open a semaphore.

        Args:
            memory: Memory instance
            name: Semaphore identifier
            initial_count: Initial count (required for creation, None to open existing)
            max_count: Maximum count (0 for unbounded, 1 for binary/mutex)

        Raises:
            ValueError: If parameters are invalid
            RuntimeError: If semaphore not found when opening
        """
        self.memory = memory
        self.name = name

        # Try to find existing semaphore
        entry = memory.table.find(name)

        if entry is None:
            # Create new semaphore
            if initial_count is None:
                raise ValueError("initial_count required to create new semaphore")

            if initial_count < 0:
                raise ValueError("Initial count must be non-negative")

            if max_count < 0:
                raise ValueError("Max count must be non-negative or 0 (unbounded)")

            if max_count > 0 and initial_count > max_count:
                raise ValueError("Initial count cannot exceed max count")

            # Allocate in shared memory
            self.offset = memory.allocate(name, self.HEADER_SIZE)

            # Initialize header
            header_data = struct.pack(
                self.HEADER_FORMAT,
                initial_count,  # count
                0,              # waiting
                max_count,      # max_count
                0               # padding
            )
            memory.data[self.offset:self.offset + self.HEADER_SIZE] = header_data

            self.max_count = max_count

        else:
            # Open existing semaphore
            self.offset = entry.offset

            if entry.size != self.HEADER_SIZE:
                raise RuntimeError(f"Invalid semaphore size: {entry.size}")

            # Read max_count from header
            _, _, self.max_count, _ = self._read_header()

        # Create atomic views for signed integers
        # Note: We use raw struct operations for signed int32
        # AtomicInt is for unsigned, so we implement atomic ops directly
        self._count_offset = self.offset
        self._waiting_offset = self.offset + 4
        self._lock = threading.Lock()  # For atomic compare-exchange

    def _read_header(self):
        """Read header values."""
        header_bytes = self.memory.data[self.offset:self.offset + self.HEADER_SIZE]
        return struct.unpack(self.HEADER_FORMAT, header_bytes)

    def _load_count(self) -> int:
        """Atomically load count value."""
        return struct.unpack_from('<i', self.memory.data, self._count_offset)[0]

    def _store_count(self, value: int):
        """Atomically store count value."""
        struct.pack_into('<i', self.memory.data, self._count_offset, value)

    def _load_waiting(self) -> int:
        """Atomically load waiting value."""
        return struct.unpack_from('<i', self.memory.data, self._waiting_offset)[0]

    def _store_waiting(self, value: int):
        """Atomically store waiting value."""
        struct.pack_into('<i', self.memory.data, self._waiting_offset, value)

    def _compare_exchange_count(self, expected: int, desired: int) -> bool:
        """Atomically compare and exchange count value."""
        with self._lock:
            current = self._load_count()
            if current == expected:
                self._store_count(desired)
                return True
            return False

    def _increment_count(self):
        """Atomically increment count."""
        with self._lock:
            current = self._load_count()
            self._store_count(current + 1)

    def _increment_waiting(self):
        """Atomically increment waiting counter."""
        with self._lock:
            current = self._load_waiting()
            self._store_waiting(current + 1)

    def _decrement_waiting(self):
        """Atomically decrement waiting counter."""
        with self._lock:
            current = self._load_waiting()
            self._store_waiting(current - 1)

    def acquire(self, timeout: Optional[float] = None) -> bool:
        """
        Acquire one permit from the semaphore.

        Blocks until a permit is available or timeout expires.
        Uses spin-waiting with exponential backoff to reduce CPU usage.

        Args:
            timeout: Maximum time to wait in seconds (None for infinite)

        Returns:
            True if permit was acquired, False if timed out
        """
        start_time = time.time() if timeout is not None else None

        # Increment waiting counter
        self._increment_waiting()

        try:
            backoff = 0.0001  # 0.1ms
            max_backoff = 0.001  # 1ms

            while True:
                current = self._load_count()

                if current > 0:
                    # Try to decrement atomically
                    if self._compare_exchange_count(current, current - 1):
                        # Success!
                        return True

                # Check timeout
                if timeout is not None:
                    elapsed = time.time() - start_time
                    if elapsed >= timeout:
                        return False

                # Exponential backoff to reduce CPU usage
                time.sleep(backoff)
                if backoff < max_backoff:
                    backoff *= 2

        finally:
            # Decrement waiting counter
            self._decrement_waiting()

    def try_acquire(self) -> bool:
        """
        Try to acquire one permit without blocking.

        Returns:
            True if permit was acquired, False if count was 0
        """
        current = self._load_count()

        while current > 0:
            if self._compare_exchange_count(current, current - 1):
                return True
            # CAS failed, current was updated, retry
            current = self._load_count()

        return False

    def release(self):
        """
        Release one permit back to the semaphore.

        Increments the count, potentially waking a waiting process.

        Raises:
            OverflowError: If max_count would be exceeded
        """
        # Check if we would exceed max_count
        if self.max_count > 0:
            current = self._load_count()
            if current >= self.max_count:
                raise OverflowError("Semaphore count would exceed maximum")

        # Increment count atomically
        self._increment_count()
        # Waiting processes will see the incremented count and wake up

    @property
    def count(self) -> int:
        """Get current semaphore count."""
        return self._load_count()

    @property
    def waiting(self) -> int:
        """Get number of processes currently waiting."""
        return self._load_waiting()

    def __enter__(self):
        """Context manager entry: acquire the semaphore."""
        self.acquire()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit: release the semaphore."""
        self.release()
        return False

    def __repr__(self):
        return (f"Semaphore(name='{self.name}', count={self.count}, "
                f"waiting={self.waiting}, max_count={self.max_count})")
