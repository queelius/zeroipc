"""Event synchronization primitive for shared memory."""

import struct
import time
import threading
from enum import IntEnum
from typing import Optional

from .memory import Memory
from .semaphore import Semaphore


class EventMode(IntEnum):
    """Event synchronization mode."""
    AUTO_RESET = 0   # Signal wakes one waiter, auto-resets
    MANUAL_RESET = 1  # Signal wakes all waiters, stays signaled


class Event:
    """
    Event synchronization primitive for shared memory.

    Provides manual-reset and auto-reset event semantics similar to
    Win32 Events or POSIX condition variables.

    - **AutoReset**: signal() wakes one waiter, then auto-resets
    - **ManualReset**: signal() wakes all waiters, stays signaled until reset()

    Binary layout:
    - 4 bytes: signaled flag (0 = not signaled, 1 = signaled)
    - 4 bytes: mode (AutoReset or ManualReset)
    - 4 bytes: waiting count
    - Semaphore for blocking (separate structure)

    Example:
        >>> mem = Memory("/sync", 1024 * 1024)
        >>> ready = Event(mem, "ready", EventMode.MANUAL_RESET)
        >>>
        >>> # Process A - waits for ready signal
        >>> ready.wait()
        >>> process_data()
        >>>
        >>> # Process B - signals when ready
        >>> prepare_data()
        >>> ready.signal()
    """

    STATE_FORMAT = 'III'  # signaled, mode, waiting
    STATE_SIZE = struct.calcsize(STATE_FORMAT)

    def __init__(self, memory: Memory, name: str,
                 mode: EventMode = EventMode.AUTO_RESET,
                 create_if_missing: bool = True):
        """
        Create or open an Event.

        Args:
            memory: Memory instance
            name: Event identifier
            mode: Event mode (only used when creating new event)
            create_if_missing: If True, creates new event; if False, opens existing

        Raises:
            RuntimeError: If event not found when create_if_missing=False
        """
        self.memory = memory
        self.name = name

        sem_name = name + "_sem"

        # Try to find existing event (state stored directly at 'name')
        entry = memory.table.find(name)

        if entry is None:
            if not create_if_missing:
                raise RuntimeError(f"Event '{name}' not found")

            # Create new event state directly at 'name'
            self.state_offset = memory.allocate(name, self.STATE_SIZE)

            # Initialize state (signaled=0, mode, waiting=0)
            state_data = struct.pack(self.STATE_FORMAT, 0, mode, 0)
            memory.data[self.state_offset:self.state_offset + self.STATE_SIZE] = state_data

            # Create semaphore for blocking (initially locked, count=0)
            self._sem = Semaphore(memory, sem_name, initial_count=0, max_count=0)

            self.mode = mode

        else:
            # Open existing event - state is stored directly at 'name'
            self.state_offset = entry.offset

            if entry.size != self.STATE_SIZE:
                raise RuntimeError(
                    f"Invalid event state size: {entry.size}, expected {self.STATE_SIZE}"
                )

            # Open existing semaphore
            self._sem = Semaphore(memory, sem_name, initial_count=None)

            # Read mode from state
            _, self.mode, _ = self._read_state()

        # Offsets for atomic operations
        self._signaled_offset = self.state_offset
        self._mode_offset = self.state_offset + 4
        self._waiting_offset = self.state_offset + 8

        # Lock for atomic operations
        self._lock = threading.Lock()

    def _read_state(self):
        """Read state values."""
        state_bytes = self.memory.data[self.state_offset:self.state_offset + self.STATE_SIZE]
        return struct.unpack(self.STATE_FORMAT, state_bytes)

    def _load_signaled(self) -> int:
        """Atomically load signaled flag."""
        return struct.unpack_from('<I', self.memory.data, self._signaled_offset)[0]

    def _store_signaled(self, value: int):
        """Atomically store signaled flag."""
        struct.pack_into('<I', self.memory.data, self._signaled_offset, value)

    def signal(self):
        """
        Signal the event.

        - AutoReset: Wakes one waiting thread (semaphore release)
        - ManualReset: Sets signaled flag, wakes all threads
        """
        self._store_signaled(1)

        if self.mode == EventMode.MANUAL_RESET:
            # ManualReset: threads will check signaled flag directly
            # No semaphore release needed - they spin-check the flag
            pass
        else:
            # AutoReset: release semaphore to wake one waiter
            self._sem.release()

    def wait(self, timeout: Optional[float] = None) -> bool:
        """
        Wait for the event to be signaled.

        Blocks until signal() is called or timeout expires.
        For AutoReset events, consuming the signal is automatic.
        For ManualReset events, the event stays signaled until reset().

        Args:
            timeout: Maximum time to wait in seconds (None for infinite)

        Returns:
            True if signaled, False if timeout
        """
        if self.mode == EventMode.MANUAL_RESET:
            # ManualReset: check signaled flag
            start_time = time.time() if timeout is not None else None
            backoff = 0.0001  # 0.1ms
            max_backoff = 0.001  # 1ms

            while self._load_signaled() == 0:
                # Check timeout
                if timeout is not None:
                    elapsed = time.time() - start_time
                    if elapsed >= timeout:
                        return False

                # Spin-wait with backoff
                time.sleep(backoff)
                if backoff < max_backoff:
                    backoff *= 2

            return True

        else:
            # AutoReset: acquire the semaphore, then clear signaled flag
            if self._sem.acquire(timeout=timeout):
                self._store_signaled(0)
                return True
            return False

    def reset(self):
        """
        Reset the event to non-signaled state.

        Only meaningful for ManualReset events. AutoReset events
        reset automatically when consumed.
        """
        self._store_signaled(0)

    def pulse(self):
        """
        Pulse the event (signal + reset atomically).

        Wakes all waiting threads then immediately resets.
        Useful for one-shot broadcasts.
        """
        self.signal()
        time.sleep(0.0001)  # Let waiters wake (0.1ms)
        self.reset()

    @property
    def is_signaled(self) -> bool:
        """Check if event is currently signaled."""
        return self._load_signaled() == 1

    def __repr__(self):
        mode_str = "AutoReset" if self.mode == EventMode.AUTO_RESET else "ManualReset"
        status = "signaled" if self.is_signaled else "not signaled"
        return f"Event(name='{self.name}', mode={mode_str}, status={status})"
