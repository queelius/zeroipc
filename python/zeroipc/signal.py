"""Reactive signal for fine-grained reactivity across processes."""

import struct
import time
import threading
from typing import Any, Callable, Optional

import numpy as np

from .memory import Memory
from .mutex import Mutex


class Signal:
    """
    Reactive signal for fine-grained reactivity across processes.

    A Signal stores a value that can be observed for changes. When the value
    changes, the version number increments, allowing other processes to detect
    and react to changes.

    Features:
    - Fine-grained reactivity (SolidJS/Preact style)
    - Version tracking for efficient polling
    - Cross-process change detection
    - Atomic updates via update()

    This enables reactive programming patterns across shared memory!

    Binary layout:
    - 8 bytes: version (increments on each change)
    - N bytes: value (dtype dependent)

    Example:
        >>> mem = Memory("/data", 1024 * 1024)
        >>> counter = Signal(mem, "counter", dtype=np.int32, initial_value=0)
        >>>
        >>> # Process A - producer
        >>> counter.set(counter.get() + 1)
        >>>
        >>> # Process B - reactive consumer
        >>> last_version = counter.version
        >>> while True:
        >>>     if counter.has_changed(last_version):
        >>>         print(f"Counter changed to: {counter.get()}")
        >>>         last_version = counter.version
        >>>
        >>> # Or wait for changes with timeout
        >>> if counter.wait_for_change(last_version, timeout=1.0):
        >>>     print(f"New value: {counter.get()}")
    """

    VERSION_FORMAT = 'Q'  # uint64 for version
    VERSION_SIZE = struct.calcsize(VERSION_FORMAT)

    def __init__(self, memory: Memory, name: str, *,
                 dtype: np.dtype,
                 initial_value: Any = None,
                 create_if_missing: bool = True):
        """
        Create or open a Signal.

        Args:
            memory: Memory instance
            name: Signal identifier
            dtype: NumPy dtype for the value
            initial_value: Initial value (only used when creating new signal)
            create_if_missing: If True, creates new signal; if False, opens existing

        Raises:
            RuntimeError: If signal not found when create_if_missing=False
        """
        self.memory = memory
        self.name = name
        self.dtype = np.dtype(dtype)
        self.elem_size = self.dtype.itemsize

        state_name = name + "_state"
        mutex_name = name + "_mtx"

        # Try to find existing signal
        entry = memory.table.find(name)

        if entry is None:
            if not create_if_missing:
                raise RuntimeError(f"Signal '{name}' not found")

            if initial_value is None:
                raise ValueError("initial_value required to create new signal")

            # Create new signal state
            state_size = self.VERSION_SIZE + self.elem_size
            self.state_offset = memory.allocate(state_name, state_size)

            # Initialize version to 0
            version_data = struct.pack(self.VERSION_FORMAT, 0)
            memory.data[self.state_offset:self.state_offset + self.VERSION_SIZE] = version_data

            # Initialize value
            value_array = np.array([initial_value], dtype=self.dtype)
            value_offset = self.state_offset + self.VERSION_SIZE
            memory.data[value_offset:value_offset + self.elem_size] = value_array.tobytes()

            # Create mutex
            self._mutex = Mutex(memory, mutex_name, create_if_missing=True)

            # Add marker entry
            memory.allocate(name, 1)

        else:
            # Open existing signal
            state_entry = memory.table.find(state_name)
            if not state_entry:
                raise RuntimeError(f"Signal state '{state_name}' not found")

            self.state_offset = state_entry.offset

            expected_size = self.VERSION_SIZE + self.elem_size
            if state_entry.size != expected_size:
                raise RuntimeError(
                    f"Signal state size mismatch: expected {expected_size}, got {state_entry.size}"
                )

            # Open existing mutex
            self._mutex = Mutex(memory, mutex_name, create_if_missing=False)

        # Offsets
        self._version_offset = self.state_offset
        self._value_offset = self.state_offset + self.VERSION_SIZE

        # Lock for atomic operations
        self._lock = threading.Lock()

    def _load_version(self) -> int:
        """Atomically load version."""
        return struct.unpack_from('<Q', self.memory.data, self._version_offset)[0]

    def _store_version(self, value: int):
        """Atomically store version."""
        struct.pack_into('<Q', self.memory.data, self._version_offset, value)

    def _increment_version(self):
        """Atomically increment version."""
        with self._lock:
            current = self._load_version()
            self._store_version(current + 1)

    def get(self) -> Any:
        """
        Get current value.

        Returns:
            Current value of the signal
        """
        value_bytes = self.memory.data[self._value_offset:self._value_offset + self.elem_size]
        value_array = np.frombuffer(value_bytes, dtype=self.dtype)
        return value_array[0]

    def set(self, value: Any):
        """
        Set new value and increment version.

        This is an atomic operation protected by the mutex.

        Args:
            value: New value to set
        """
        with self._mutex:
            # Write new value
            value_array = np.array([value], dtype=self.dtype)
            self.memory.data[self._value_offset:self._value_offset + self.elem_size] = value_array.tobytes()

            # Increment version
            self._increment_version()

    def update(self, func: Callable[[Any], Any]):
        """
        Update value using a function atomically.

        The function receives the current value and returns the new value.
        This is useful for atomic read-modify-write operations.

        Args:
            func: Function that takes current value and returns new value

        Example:
            >>> counter.update(lambda x: x + 1)  # Atomic increment
        """
        with self._mutex:
            # Read current value
            current = self.get()

            # Compute new value
            new_value = func(current)

            # Write new value
            value_array = np.array([new_value], dtype=self.dtype)
            self.memory.data[self._value_offset:self._value_offset + self.elem_size] = value_array.tobytes()

            # Increment version
            self._increment_version()

    @property
    def version(self) -> int:
        """Get current version number."""
        return self._load_version()

    def has_changed(self, last_version: int) -> bool:
        """
        Check if signal has changed since last_version.

        Args:
            last_version: Previous version number to compare against

        Returns:
            True if version has changed, False otherwise
        """
        return self._load_version() != last_version

    def wait_for_change(self, last_version: int, timeout: Optional[float] = None) -> bool:
        """
        Wait for signal to change from last_version.

        Uses spin-waiting with exponential backoff.

        Args:
            last_version: Previous version number to wait for change from
            timeout: Maximum time to wait in seconds (None for infinite)

        Returns:
            True if changed, False if timeout

        Example:
            >>> last_ver = signal.version
            >>> if signal.wait_for_change(last_ver, timeout=1.0):
            >>>     print(f"Changed to: {signal.get()}")
        """
        start_time = time.time() if timeout is not None else None
        backoff = 0.0001  # 0.1ms
        max_backoff = 0.001  # 1ms

        while not self.has_changed(last_version):
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

    def __repr__(self):
        return f"Signal(name='{self.name}', dtype={self.dtype}, version={self.version}, value={self.get()})"
