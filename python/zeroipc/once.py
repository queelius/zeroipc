"""One-time initialization primitive for shared memory."""

import struct
import time
import threading
from typing import Callable

from .memory import Memory


class Once:
    """
    One-time initialization primitive for shared memory.

    Ensures a function is executed exactly once across all processes,
    similar to C++ std::call_once or pthread_once. Thread-safe and
    process-safe.

    Uses 3-state protocol to ensure callers wait for completion:
    - PENDING (0): No one has started execution yet
    - EXECUTING (1): A thread/process is currently running the function
    - DONE (2): The function has completed execution

    Binary layout: 4 bytes (atomic state: 0 = PENDING, 1 = EXECUTING, 2 = DONE)

    Example:
        >>> mem = Memory("/config", 1024)
        >>> init = Once(mem, "initialize")
        >>>
        >>> # This will execute exactly once across all processes
        >>> def initialize_resources():
        >>>     print("Initializing shared resources...")
        >>>     # Expensive initialization here
        >>>
        >>> init.call(initialize_resources)  # First process: executes
        >>> init.call(initialize_resources)  # Same/other process: waits then skips
    """

    HEADER_FORMAT = 'I'  # Single uint32 for state
    HEADER_SIZE = struct.calcsize(HEADER_FORMAT)

    STATE_PENDING   = 0
    STATE_EXECUTING = 1
    STATE_DONE      = 2

    def __init__(self, memory: Memory, name: str, create_if_missing: bool = True):
        """
        Create or open a Once flag.

        Args:
            memory: Memory instance
            name: Once flag identifier
            create_if_missing: If True, creates new once flag; if False, opens existing

        Raises:
            RuntimeError: If once flag not found when create_if_missing=False
        """
        self.memory = memory
        self.name = name

        # Try to find existing once flag
        entry = memory.table.find(name)

        if entry is None:
            if not create_if_missing:
                raise RuntimeError(f"Once flag '{name}' not found")

            # Create new once flag
            self.offset = memory.allocate(name, self.HEADER_SIZE)

            # Initialize to PENDING state
            state_data = struct.pack(self.HEADER_FORMAT, self.STATE_PENDING)
            memory.data[self.offset:self.offset + self.HEADER_SIZE] = state_data

        else:
            # Open existing once flag
            self.offset = entry.offset

            if entry.size != self.HEADER_SIZE:
                raise RuntimeError(f"Invalid once flag size: {entry.size}")

        # Lock for atomic compare-exchange
        self._lock = threading.Lock()

    def _load_state(self) -> int:
        """Atomically load state value."""
        return struct.unpack_from('<I', self.memory.data, self.offset)[0]

    def _store_state(self, value: int):
        """Atomically store state value."""
        struct.pack_into('<I', self.memory.data, self.offset, value)

    def _compare_exchange(self, expected: int, desired: int) -> bool:
        """Atomically compare and exchange state value."""
        with self._lock:
            current = self._load_state()
            if current == expected:
                self._store_state(desired)
                return True
            return False

    def call(self, func: Callable[[], None]):
        """
        Execute function exactly once.

        If this is the first call across all processes, executes func.
        If another process is currently executing, blocks until complete.
        If already completed, returns immediately without executing func.

        The function should be idempotent and have no return value.

        Args:
            func: Callable with no arguments that performs initialization

        Example:
            >>> init = Once(mem, "setup")
            >>> init.call(lambda: print("Initialized once!"))
        """
        # Fast path: already done
        if self._load_state() == self.STATE_DONE:
            return

        # Try to atomically transition from PENDING to EXECUTING
        if self._compare_exchange(self.STATE_PENDING, self.STATE_EXECUTING):
            # We won the race - execute the function
            try:
                func()
            except Exception:
                # Even if func throws, mark as done (std::call_once semantics)
                self._store_state(self.STATE_DONE)
                raise

            # Transition from EXECUTING to DONE
            self._store_state(self.STATE_DONE)
        else:
            # Someone else is executing or already done
            # Spin-wait while state is EXECUTING
            backoff = 0.0001  # 0.1ms
            max_backoff = 0.001  # 1ms

            while self._load_state() == self.STATE_EXECUTING:
                time.sleep(backoff)
                if backoff < max_backoff:
                    backoff *= 2

    @property
    def is_called(self) -> bool:
        """Check if the once flag has been called."""
        return self._load_state() == self.STATE_DONE

    def reset(self):
        """
        Reset the once flag to PENDING state.

        WARNING: This is not thread-safe and should only be used for testing
        or when you're certain no other processes are accessing the flag.
        """
        self._store_state(self.STATE_PENDING)

    def __repr__(self):
        status = "called" if self.is_called else "not called"
        return f"Once(name='{self.name}', status={status})"
