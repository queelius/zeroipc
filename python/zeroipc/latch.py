"""Lock-free latch implementation for shared memory."""

import struct
import time
import threading
from typing import Optional

from .memory import Memory


class Latch:
    """
    Lock-free latch for one-time countdown synchronization.

    A latch counts down from an initial value to zero. Once it reaches zero,
    it stays at zero (one-time use). Any number of processes can wait() for
    the count to reach zero, and any process can count_down() to decrement it.

    Unlike Barrier which resets and cycles through generations, Latch is
    single-use and cannot be reset.

    Thread-safe and process-safe.

    Common use cases:
    - Start gate: Initialize with count=1, workers wait(), coordinator counts down
    - Completion detection: Initialize with count=N, each worker counts down when done

    Example:
        # Wait for all workers to initialize
        ready_latch = Latch(mem, "workers_ready", count=num_workers)

        # Each worker thread:
        initialize()
        ready_latch.count_down()  # Signal ready

        # Coordinator:
        ready_latch.wait()  # Wait for all workers to be ready
        start_work()
    """

    HEADER_FORMAT = 'iiii'  # count, initial_count, padding[2]
    HEADER_SIZE = struct.calcsize(HEADER_FORMAT)

    def __init__(self, memory: Memory, name: str,
                 count: Optional[int] = None):
        """
        Create or open a latch.

        Args:
            memory: Memory instance
            name: Latch identifier
            count: Initial count value (required for creation, None to open)

        Raises:
            ValueError: If parameters are invalid
            RuntimeError: If latch not found when opening
        """
        self.memory = memory
        self.name = name

        # Try to find existing latch
        entry = memory.table.find(name)

        if entry is None:
            # Create new latch
            if count is None:
                raise RuntimeError(f"Latch not found: {name}")

            if count < 0:
                raise ValueError("Count must be non-negative")

            # Allocate in shared memory
            self.offset = memory.allocate(name, self.HEADER_SIZE)

            # Initialize header
            header_data = struct.pack(
                self.HEADER_FORMAT,
                count,          # count
                count,          # initial_count
                0,              # padding
                0               # padding
            )
            memory.data[self.offset:self.offset + self.HEADER_SIZE] = header_data

            self.initial_count_value = count

        else:
            # Open existing latch
            self.offset = entry.offset

            if entry.size != self.HEADER_SIZE:
                raise RuntimeError(f"Invalid latch size: {entry.size}")

            # Read initial_count from header
            _, self.initial_count_value, _, _ = self._read_header()

        # Create atomic views for signed integers
        self._count_offset = self.offset
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

    def _compare_exchange_count(self, expected: int, desired: int) -> bool:
        """Atomically compare and exchange count value."""
        with self._lock:
            current = self._load_count()
            if current == expected:
                self._store_count(desired)
                return True
            return False

    def count_down(self, n: int = 1):
        """
        Decrement the count by n (default 1).

        Atomically decrements the count, saturating at 0. If the count
        reaches 0, all waiting processes are released.

        Args:
            n: Amount to decrement (default 1, must be > 0)

        Raises:
            ValueError: If n <= 0
        """
        if n <= 0:
            raise ValueError("count_down amount must be positive")

        current = self._load_count()

        while current > 0:
            new_count = max(0, current - n)

            if self._compare_exchange_count(current, new_count):
                return

            # CAS failed, current was updated, retry
            current = self._load_count()

    def wait(self, timeout: Optional[float] = None) -> bool:
        """
        Wait for the count to reach zero.

        Blocks until the latch count reaches 0. If the count is already 0,
        returns immediately.

        Uses spin-waiting with exponential backoff to reduce CPU usage.

        Args:
            timeout: Maximum time to wait in seconds (None for infinite)

        Returns:
            True if count reached 0, False if timed out
        """
        start_time = time.time() if timeout is not None else None

        backoff = 0.0001  # 0.1ms
        max_backoff = 0.001  # 1ms

        while self._load_count() > 0:
            # Check timeout
            if timeout is not None:
                elapsed = time.time() - start_time
                if elapsed >= timeout:
                    return False

            # Exponential backoff to reduce CPU usage
            time.sleep(backoff)
            if backoff < max_backoff:
                backoff *= 2

        return True

    def try_wait(self) -> bool:
        """
        Try to wait without blocking.

        Returns:
            True if count is 0, False if still counting down
        """
        return self._load_count() == 0

    @property
    def count(self) -> int:
        """Get current count value."""
        return self._load_count()

    @property
    def initial_count(self) -> int:
        """Get initial count value that the latch was created with."""
        return self.initial_count_value

    def __repr__(self):
        return (f"Latch(name='{self.name}', count={self.count}, "
                f"initial_count={self.initial_count})")
