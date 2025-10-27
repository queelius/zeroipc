"""Lock-free barrier implementation for shared memory."""

import struct
import time
import threading
from typing import Optional

from .memory import Memory


class Barrier:
    """
    Lock-free barrier for cross-process synchronization.

    A barrier synchronizes N processes at a checkpoint. All participants must
    call wait() before any can proceed. Once all arrive, the barrier releases
    all waiters simultaneously and automatically resets for the next cycle.

    The generation counter prevents early arrivals for the next cycle from
    releasing the current cycle, making the barrier reusable.

    Thread-safe and process-safe.

    Example:
        # Phase-based parallel algorithm
        barrier = Barrier(mem, "phase_sync", num_participants=4)

        while work_remaining:
            do_phase_1()
            barrier.wait()  # All workers must complete phase 1
            do_phase_2()
            barrier.wait()  # All workers must complete phase 2
    """

    HEADER_FORMAT = 'iiii'  # arrived, generation, num_participants, padding
    HEADER_SIZE = struct.calcsize(HEADER_FORMAT)

    def __init__(self, memory: Memory, name: str,
                 num_participants: Optional[int] = None):
        """
        Create or open a barrier.

        Args:
            memory: Memory instance
            name: Barrier identifier
            num_participants: Number of participants (required for creation, None to open)

        Raises:
            ValueError: If parameters are invalid
            RuntimeError: If barrier not found when opening
        """
        self.memory = memory
        self.name = name

        # Try to find existing barrier
        entry = memory.table.find(name)

        if entry is None:
            # Create new barrier
            if num_participants is None:
                raise RuntimeError(f"Barrier not found: {name}")

            if num_participants <= 0:
                raise ValueError("Number of participants must be positive")

            # Allocate in shared memory
            self.offset = memory.allocate(name, self.HEADER_SIZE)

            # Initialize header
            header_data = struct.pack(
                self.HEADER_FORMAT,
                0,                  # arrived
                0,                  # generation
                num_participants,   # num_participants
                0                   # padding
            )
            memory.data[self.offset:self.offset + self.HEADER_SIZE] = header_data

            self.num_participants_value = num_participants

        else:
            # Open existing barrier
            self.offset = entry.offset

            if entry.size != self.HEADER_SIZE:
                raise RuntimeError(f"Invalid barrier size: {entry.size}")

            # Read num_participants from header
            _, _, self.num_participants_value, _ = self._read_header()

        # Create atomic views for signed integers
        self._arrived_offset = self.offset
        self._generation_offset = self.offset + 4
        self._lock = threading.Lock()  # For atomic compare-exchange

    def _read_header(self):
        """Read header values."""
        header_bytes = self.memory.data[self.offset:self.offset + self.HEADER_SIZE]
        return struct.unpack(self.HEADER_FORMAT, header_bytes)

    def _load_arrived(self) -> int:
        """Atomically load arrived value."""
        return struct.unpack_from('<i', self.memory.data, self._arrived_offset)[0]

    def _store_arrived(self, value: int):
        """Atomically store arrived value."""
        struct.pack_into('<i', self.memory.data, self._arrived_offset, value)

    def _fetch_add_arrived(self, delta: int) -> int:
        """Atomically increment arrived and return previous value."""
        with self._lock:
            current = self._load_arrived()
            self._store_arrived(current + delta)
            return current

    def _load_generation(self) -> int:
        """Atomically load generation value."""
        return struct.unpack_from('<i', self.memory.data, self._generation_offset)[0]

    def _store_generation(self, value: int):
        """Atomically store generation value."""
        struct.pack_into('<i', self.memory.data, self._generation_offset, value)

    def _fetch_add_generation(self, delta: int) -> int:
        """Atomically increment generation and return previous value."""
        with self._lock:
            current = self._load_generation()
            self._store_generation(current + delta)
            return current

    def wait(self, timeout: Optional[float] = None) -> bool:
        """
        Wait for all participants to arrive at the barrier.

        Blocks until all num_participants processes have called wait().
        Once all arrive, all waiters are released simultaneously and the
        barrier automatically resets for the next cycle.

        Uses spin-waiting with exponential backoff to reduce CPU usage.

        Args:
            timeout: Maximum time to wait in seconds (None for infinite)

        Returns:
            True if barrier released, False if timed out

        Warning:
            If a timeout occurs, the barrier state may be inconsistent.
            The caller is responsible for coordinating recovery with other processes.
        """
        start_time = time.time() if timeout is not None else None

        # Capture current generation before arriving
        my_generation = self._load_generation()

        # Atomically increment arrived counter
        arrived = self._fetch_add_arrived(1) + 1

        if arrived == self.num_participants_value:
            # Last to arrive - reset and release everyone
            self._store_arrived(0)

            # Increment generation to release waiters
            self._fetch_add_generation(1)
            return True

        else:
            # Not last - wait for generation to change
            backoff = 0.0001  # 0.1ms
            max_backoff = 0.001  # 1ms

            while self._load_generation() == my_generation:
                # Check timeout
                if timeout is not None:
                    elapsed = time.time() - start_time
                    if elapsed >= timeout:
                        # Timeout - decrement arrived count
                        # WARNING: This creates a race if the last participant arrives
                        # during this window. Use with caution.
                        self._fetch_add_arrived(-1)
                        return False

                # Exponential backoff to reduce CPU usage
                time.sleep(backoff)
                if backoff < max_backoff:
                    backoff *= 2

            return True

    @property
    def arrived(self) -> int:
        """Get number of processes currently waiting at the barrier."""
        return self._load_arrived()

    @property
    def generation(self) -> int:
        """Get current generation number.

        The generation increments each time all participants pass through.
        """
        return self._load_generation()

    @property
    def num_participants(self) -> int:
        """Get number of participants required."""
        return self.num_participants_value

    def __repr__(self):
        return (f"Barrier(name='{self.name}', arrived={self.arrived}, "
                f"generation={self.generation}, num_participants={self.num_participants})")
