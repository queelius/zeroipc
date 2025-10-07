"""
Lazy evaluation container for deferred computations in shared memory.

This module provides a Lazy data structure that represents a computation
that is deferred until its value is actually needed. The computation is
performed at most once, and the result is cached for subsequent accesses.
"""

import struct
from typing import Optional, TypeVar, Union, Callable, Any
from enum import IntEnum
import numpy as np

from .memory import Memory
from .atomic import AtomicInt

T = TypeVar('T')


class LazyState(IntEnum):
    """States of a Lazy computation."""
    UNEVALUATED = 0
    EVALUATING = 1
    EVALUATED = 2
    ERROR = 3


class Lazy:
    """
    Lazy evaluation container for deferred computations in shared memory.

    Lazy<T> represents a computation that is deferred until its value is
    actually needed. The computation is performed at most once, and the
    result is cached for subsequent accesses.

    Example:
        # Create lazy value
        mem = Memory("/simulation", 10*1024*1024)
        lazy_pi = Lazy(mem, "pi", dtype=np.float64)

        # Set computation function (only first process to access will compute)
        lazy_pi.set_computation(lambda: 3.14159265359)

        # Force evaluation - computed only once
        pi_value = lazy_pi.force()  # Computation happens here

        # Subsequent accesses use cached value
        pi_value2 = lazy_pi.force()  # No recomputation

        # Or provide immediate value
        lazy_answer = Lazy(mem, "answer", value=42, dtype=np.int32)
        answer = lazy_answer.force()  # Already evaluated
    """

    def __init__(self, memory: Memory, name: str,
                 dtype: Optional[Union[np.dtype, str, type]] = None,
                 value: Optional[T] = None,
                 computation: Optional[Callable[[], T]] = None,
                 open_existing: bool = False):
        """
        Create or open a Lazy computation.

        Args:
            memory: Shared memory instance
            name: Lazy value name
            dtype: Value data type (required)
            value: Immediate value (if provided, skips lazy evaluation)
            computation: Computation function (stored locally, not shared)
            open_existing: If True, open existing Lazy; if False, create new

        Raises:
            ValueError: If required parameters are missing
            TypeError: If data type is not specified
            RuntimeError: If Lazy is not found when opening existing
        """
        if len(name) > 31:
            raise ValueError("Name too long (max 31 characters)")

        if dtype is None:
            raise TypeError("dtype is required")

        self.memory = memory
        self.name = name
        self.dtype = np.dtype(dtype)
        self.value_size = self.dtype.itemsize
        self.computation = computation
        self._compute_fn = computation  # Private storage

        # Try to find existing lazy
        entry = memory.table.find(name)

        if entry is None:
            if open_existing:
                raise RuntimeError(f"Lazy not found: {name}")
            # Create new lazy
            self._create_new(value)
        else:
            # Open existing lazy
            self._open_existing(entry)

    def _create_new(self, immediate_value: Optional[T] = None):
        """Create a new Lazy in shared memory."""
        # Header: state(4) + value + error_msg(256)
        header_size = 4 + self.value_size + 256
        total_size = header_size

        # Allocate space
        self.offset = self.memory.table.allocate(total_size)

        if not self.memory.table.add(self.name, self.offset, total_size):
            raise RuntimeError("Failed to add Lazy to table")

        # Get memory view
        self.buffer = self.memory.at(self.offset)

        if immediate_value is not None:
            # Initialize as already evaluated with provided value
            struct.pack_into('<I', self.buffer, 0, LazyState.EVALUATED)
            self._write_value(immediate_value)
        else:
            # Initialize as unevaluated
            struct.pack_into('<I', self.buffer, 0, LazyState.UNEVALUATED)

        # Zero out value and error message areas
        if immediate_value is None:
            value_offset = 4
            for i in range(self.value_size):
                self.buffer[value_offset + i] = 0

        error_offset = 4 + self.value_size
        for i in range(256):
            self.buffer[error_offset + i] = 0

    def _open_existing(self, entry):
        """Open an existing Lazy from shared memory."""
        self.offset = entry.offset
        self.buffer = self.memory.at(self.offset)

    def _get_value_offset(self) -> int:
        """Get byte offset of value in buffer."""
        return 4

    def _get_error_offset(self) -> int:
        """Get byte offset of error message in buffer."""
        return 4 + self.value_size

    def _read_value(self) -> T:
        """Read value from shared memory."""
        value_offset = self._get_value_offset()
        if self.dtype.kind in 'iuf':  # integer, unsigned, float
            return struct.unpack_from(f'<{self.dtype.char}', self.buffer, value_offset)[0]
        else:
            value_bytes = self.buffer[value_offset:value_offset + self.value_size]
            return np.frombuffer(value_bytes, dtype=self.dtype)[0]

    def _write_value(self, value: T):
        """Write value to shared memory."""
        value_offset = self._get_value_offset()
        if self.dtype.kind in 'iuf':
            struct.pack_into(f'<{self.dtype.char}', self.buffer, value_offset, value)
        else:
            value_array = np.array([value], dtype=self.dtype)
            self.buffer[value_offset:value_offset + self.value_size] = value_array.tobytes()

    def _write_error(self, error_message: str):
        """Write error message to shared memory."""
        error_offset = self._get_error_offset()
        error_bytes = error_message.encode('utf-8')[:255]  # Truncate if too long
        error_bytes += b'\x00'  # Null terminate

        # Clear error area first
        for i in range(256):
            self.buffer[error_offset + i] = 0

        # Write error message
        self.buffer[error_offset:error_offset + len(error_bytes)] = error_bytes

    def _read_error(self) -> str:
        """Read error message from shared memory."""
        error_offset = self._get_error_offset()
        error_bytes = bytes(self.buffer[error_offset:error_offset + 256])
        return error_bytes.split(b'\x00')[0].decode('utf-8', errors='ignore')

    def set_computation(self, computation: Callable[[], T]):
        """
        Set the computation function for this lazy value.

        Args:
            computation: Function that computes the value

        Note: The computation function is stored locally and not shared
        across processes. Each process that needs to force evaluation
        must provide its own computation function.
        """
        self.computation = computation
        self.compute_fn = computation  # Keep alias in sync

    def init(self, computation: Callable[[], T]):
        """
        Initialize the computation function (alias for set_computation).

        Args:
            computation: Function that computes the value
        """
        self.set_computation(computation)

    def force(self) -> T:
        """
        Force evaluation of the lazy value.

        Returns:
            The computed or cached value

        Raises:
            RuntimeError: If computation failed or no computation function provided
        """
        state_atomic = AtomicInt(self.buffer, 0)

        while True:
            current_state = state_atomic.load()

            if current_state == LazyState.EVALUATED:
                # Already computed, return cached value
                return self._read_value()

            elif current_state == LazyState.ERROR:
                # Computation failed previously
                error_msg = self._read_error()
                raise RuntimeError(f"Lazy computation failed: {error_msg}")

            elif current_state == LazyState.UNEVALUATED:
                # Try to claim evaluation
                if state_atomic.compare_exchange_weak(LazyState.UNEVALUATED, LazyState.EVALUATING):
                    # We won the race, perform computation
                    try:
                        if self.computation is None:
                            raise RuntimeError("No computation function provided")

                        # Use compute_fn if set, otherwise use computation
                        compute_func = getattr(self, 'compute_fn', None) or self.computation
                        if compute_func is None:
                            raise RuntimeError("No computation function provided")

                        # Perform the computation
                        result = compute_func()

                        # Store the result
                        self._write_value(result)

                        # Mark as evaluated
                        state_atomic.store(LazyState.EVALUATED)

                        return result

                    except Exception as e:
                        # Store error and mark as error state
                        self._write_error(str(e))
                        state_atomic.store(LazyState.ERROR)
                        raise RuntimeError(f"Lazy computation failed: {e}")

            elif current_state == LazyState.EVALUATING:
                # Another process is evaluating, wait
                import time
                time.sleep(0.001)  # Brief sleep
                continue

    def is_evaluated(self) -> bool:
        """Check if the lazy value has been evaluated."""
        state = struct.unpack_from('<I', self.buffer, 0)[0]
        return state == LazyState.EVALUATED

    def is_error(self) -> bool:
        """Check if the lazy value is in error state."""
        state = struct.unpack_from('<I', self.buffer, 0)[0]
        return state == LazyState.ERROR

    def get_state(self) -> LazyState:
        """Get current state of the lazy value."""
        state = struct.unpack_from('<I', self.buffer, 0)[0]
        return LazyState(state)

    def try_get(self) -> Optional[T]:
        """
        Try to get the value without forcing computation.

        Returns:
            The value if already computed, None otherwise

        Raises:
            RuntimeError: If computation failed
        """
        current_state = self.get_state()

        if current_state == LazyState.EVALUATED:
            return self._read_value()
        elif current_state == LazyState.ERROR:
            error_msg = self._read_error()
            raise RuntimeError(f"Lazy computation failed: {error_msg}")
        else:
            return None

    def reset(self, value: Optional[T] = None):
        """
        Reset the lazy value to unevaluated state, or set it to a specific value.

        Args:
            value: Optional value to set immediately (if None, resets to unevaluated)

        Warning: This operation is not atomic and should only be used
        when no other processes are accessing the lazy value.
        """
        if value is not None:
            # Set to evaluated state with the provided value
            struct.pack_into('<I', self.buffer, 0, LazyState.EVALUATED)
            self._write_value(value)

            # Clear error area
            error_offset = self._get_error_offset()
            for i in range(256):
                self.buffer[error_offset + i] = 0
        else:
            # Reset to unevaluated state
            struct.pack_into('<I', self.buffer, 0, LazyState.UNEVALUATED)

            # Clear value and error areas
            value_offset = self._get_value_offset()
            for i in range(self.value_size):
                self.buffer[value_offset + i] = 0

            error_offset = self._get_error_offset()
            for i in range(256):
                self.buffer[error_offset + i] = 0

    def __call__(self) -> T:
        """Make Lazy callable (alias for force)."""
        return self.force()

    def __bool__(self) -> bool:
        """Check if lazy value is evaluated."""
        return self.is_evaluated()

    def __str__(self) -> str:
        """String representation."""
        state = self.get_state()
        if state == LazyState.EVALUATED:
            try:
                value = self._read_value()
                return f"Lazy(name='{self.name}', state={state.name}, value={value}, dtype={self.dtype})"
            except:
                return f"Lazy(name='{self.name}', state={state.name}, dtype={self.dtype})"
        else:
            return f"Lazy(name='{self.name}', state={state.name}, dtype={self.dtype})"

    def __repr__(self) -> str:
        """String representation."""
        return self.__str__()


def lazy_constant(memory: Memory, name: str, value: T, dtype: Optional[Union[np.dtype, str, type]] = None) -> Lazy:
    """
    Create a lazy value that is already evaluated with a constant.

    Args:
        memory: Shared memory instance
        name: Lazy value name
        value: Constant value
        dtype: Value data type (inferred if not provided)

    Returns:
        Lazy value already evaluated with the constant
    """
    if dtype is None:
        if isinstance(value, (int, np.integer)):
            dtype = np.int64
        elif isinstance(value, (float, np.floating)):
            dtype = np.float64
        else:
            dtype = type(value)

    return Lazy(memory, name, dtype=dtype, value=value)


def lazy_function(memory: Memory, name: str, computation: Callable[[], T], dtype: Union[np.dtype, str, type]) -> Lazy:
    """
    Create a lazy value with a computation function.

    Args:
        memory: Shared memory instance
        name: Lazy value name
        computation: Function that computes the value
        dtype: Value data type

    Returns:
        Lazy value with computation function
    """
    return Lazy(memory, name, dtype=dtype, computation=computation)