"""
Shared memory Future for asynchronous computation results.

This module provides a Future data structure that enables asynchronous
computation patterns across process boundaries. It allows one process
to compute a value while others wait for the result.
"""

import struct
import time
from typing import Optional, TypeVar, Union, Callable, Any
from enum import IntEnum
import numpy as np

from .memory import Memory
from .atomic import AtomicInt, AtomicInt64

T = TypeVar('T')


class FutureState(IntEnum):
    """States of a Future computation."""
    PENDING = 0
    COMPUTING = 1
    READY = 2
    ERROR = 3


class Future:
    """
    Shared memory Future for asynchronous computation results.

    Future represents a value that will be available at some point in the future.
    It enables asynchronous computation patterns where one process can compute
    a value while others wait for the result.

    Example:
        # Process A: Producer
        mem = Memory("/simulation", 10*1024*1024)
        energy_future = Future(mem, "total_energy", dtype=np.float64)

        # Compute expensive result
        energy = compute_system_energy()
        energy_future.set_value(energy)

        # Process B: Consumer
        mem = Memory("/simulation")
        energy_future = Future(mem, "total_energy", open_existing=True, dtype=np.float64)

        # Wait for result (blocks)
        energy = energy_future.get()

        # Or try with timeout
        energy = energy_future.get_timeout(1.0)  # 1 second timeout
        if energy is not None:
            process_energy(energy)
    """

    def __init__(self, memory: Memory, name: str,
                 dtype: Optional[Union[np.dtype, str, type]] = None,
                 open_existing: bool = False):
        """
        Create or open a Future.

        Args:
            memory: Shared memory instance
            name: Future name
            dtype: Value data type (required)
            open_existing: If True, open existing Future; if False, create new

        Raises:
            ValueError: If required parameters are missing
            TypeError: If data type is not specified
            RuntimeError: If Future is not found when opening existing
        """
        if len(name) > 31:
            raise ValueError("Name too long (max 31 characters)")

        if dtype is None:
            raise TypeError("dtype is required")

        self.memory = memory
        self.name = name
        self.dtype = np.dtype(dtype)
        self.value_size = self.dtype.itemsize

        # Try to find existing future
        entry = memory.table.find(name)

        if entry is None:
            if open_existing:
                raise RuntimeError(f"Future not found: {name}")
            # Create new future
            self._create_new()
        else:
            # Open existing future
            self._open_existing(entry)

    def _create_new(self):
        """Create a new Future in shared memory."""
        # Header: state(4) + waiters(4) + completion_time(8) + value + error_msg(256)
        header_size = 16 + self.value_size + 256
        total_size = header_size

        # Allocate space
        self.offset = self.memory.table.allocate(total_size)

        if not self.memory.table.add(self.name, self.offset, total_size):
            raise RuntimeError("Failed to add Future to table")

        # Get memory view
        self.buffer = self.memory.at(self.offset)

        # Initialize header
        struct.pack_into('<IIQ', self.buffer, 0,
                        FutureState.PENDING,  # state
                        0,  # waiters
                        0)  # completion_time

        # Zero out value and error message
        value_offset = 16
        error_offset = value_offset + self.value_size

        # Zero value area
        for i in range(self.value_size):
            self.buffer[value_offset + i] = 0

        # Zero error message area
        for i in range(256):
            self.buffer[error_offset + i] = 0

    def _open_existing(self, entry):
        """Open an existing Future from shared memory."""
        self.offset = entry.offset
        self.buffer = self.memory.at(self.offset)

        # No type checking for Future since it could store any compatible type

    def _get_value_offset(self) -> int:
        """Get byte offset of value in buffer."""
        return 16

    def _get_error_offset(self) -> int:
        """Get byte offset of error message in buffer."""
        return 16 + self.value_size

    def set_value(self, value: T) -> bool:
        """
        Set the future's value (completes the computation).

        Args:
            value: The computed value

        Returns:
            True if value was set successfully
        """
        # Try to transition from PENDING or COMPUTING to READY
        state_atomic = AtomicInt(self.buffer, 0)

        # First try PENDING -> COMPUTING transition
        if not state_atomic.compare_exchange_weak(FutureState.PENDING, FutureState.COMPUTING):
            # If not pending, check if we're already computing
            current_state = state_atomic.load()
            if current_state != FutureState.COMPUTING:
                return False  # Already completed or in error

        # Write the value
        value_offset = self._get_value_offset()
        if self.dtype.kind in 'iuf':  # integer, unsigned, float
            # Map numpy char to struct format, handling int64 specifically
            if self.dtype == np.dtype('int64'):
                format_char = 'q'  # Always use 'q' for int64 (8 bytes)
            elif self.dtype == np.dtype('uint64'):
                format_char = 'Q'  # Always use 'Q' for uint64 (8 bytes)
            else:
                format_char = self.dtype.char
            struct.pack_into(f'<{format_char}', self.buffer, value_offset, value)
        else:
            value_array = np.array([value], dtype=self.dtype)
            self.buffer[value_offset:value_offset + self.value_size] = value_array.tobytes()

        # Set completion time
        completion_time_atomic = AtomicInt64(self.buffer, 8)
        completion_time_atomic.store(int(time.time() * 1000000))  # microseconds

        # Transition to READY
        state_atomic.store(FutureState.READY)

        return True

    def set_exception(self, error_code: Union[int, str]) -> bool:
        """
        Set the future to error state with error code or message.

        Args:
            error_code: Error code (int) or message (str)

        Returns:
            True if error was set successfully
        """
        if isinstance(error_code, int):
            error_message = f"Error code: {error_code}"
        else:
            error_message = str(error_code)
        return self.set_error(error_message)

    def set_error(self, error_message: str) -> bool:
        """
        Set the future to error state with message.

        Args:
            error_message: Error message (max 255 chars)

        Returns:
            True if error was set successfully
        """
        # Try to transition from PENDING or COMPUTING to ERROR
        state_atomic = AtomicInt(self.buffer, 0)

        # First try PENDING -> COMPUTING transition
        if not state_atomic.compare_exchange_weak(FutureState.PENDING, FutureState.COMPUTING):
            # If not pending, check if we're already computing
            current_state = state_atomic.load()
            if current_state != FutureState.COMPUTING:
                return False  # Already completed

        # Write error message
        error_offset = self._get_error_offset()
        error_bytes = error_message.encode('utf-8')[:255]  # Truncate if too long
        error_bytes += b'\x00'  # Null terminate

        # Clear error area first
        for i in range(256):
            self.buffer[error_offset + i] = 0

        # Write error message
        self.buffer[error_offset:error_offset + len(error_bytes)] = error_bytes

        # Set completion time
        completion_time_atomic = AtomicInt64(self.buffer, 8)
        completion_time_atomic.store(int(time.time() * 1000000))  # microseconds

        # Transition to ERROR
        state_atomic.store(FutureState.ERROR)

        return True

    def get(self) -> T:
        """
        Get the future's value (blocks until ready).

        Returns:
            The computed value

        Raises:
            RuntimeError: If future is in error state
            TimeoutError: If timeout occurs (when used with timeout)
        """
        return self._wait_for_completion()

    def wait(self, timeout: float) -> Optional[T]:
        """
        Wait for the future's value with timeout (alias for get_timeout).

        Args:
            timeout: Maximum time to wait in seconds

        Returns:
            The computed value, or None if timeout occurred

        Raises:
            RuntimeError: If future is in error state
        """
        return self.get_timeout(timeout)

    def get_timeout(self, timeout_seconds: float) -> Optional[T]:
        """
        Get the future's value with timeout.

        Args:
            timeout_seconds: Maximum time to wait in seconds

        Returns:
            The computed value, or None if timeout occurred

        Raises:
            RuntimeError: If future is in error state
        """
        return self._wait_for_completion(timeout_seconds)

    def _wait_for_completion(self, timeout_seconds: Optional[float] = None) -> Optional[T]:
        """
        Wait for future completion with optional timeout.

        Args:
            timeout_seconds: Optional timeout in seconds

        Returns:
            The computed value, or None if timeout occurred

        Raises:
            RuntimeError: If future is in error state
        """
        state_atomic = AtomicInt(self.buffer, 0)
        waiters_atomic = AtomicInt(self.buffer, 4)

        # Increment waiter count
        waiters_atomic.fetch_add(1)

        try:
            start_time = time.time()

            while True:
                current_state = state_atomic.load()

                if current_state == FutureState.READY:
                    # Read and return value
                    value_offset = self._get_value_offset()
                    if self.dtype.kind in 'iuf':
                        # Map numpy char to struct format, handling int64 specifically
                        if self.dtype == np.dtype('int64'):
                            format_char = 'q'  # Always use 'q' for int64 (8 bytes)
                        elif self.dtype == np.dtype('uint64'):
                            format_char = 'Q'  # Always use 'Q' for uint64 (8 bytes)
                        else:
                            format_char = self.dtype.char
                        return struct.unpack_from(f'<{format_char}', self.buffer, value_offset)[0]
                    else:
                        value_bytes = self.buffer[value_offset:value_offset + self.value_size]
                        return np.frombuffer(value_bytes, dtype=self.dtype)[0]

                elif current_state == FutureState.ERROR:
                    # Read error message and raise
                    error_offset = self._get_error_offset()
                    error_bytes = bytes(self.buffer[error_offset:error_offset + 256])
                    error_msg = error_bytes.split(b'\x00')[0].decode('utf-8', errors='ignore')
                    raise RuntimeError(f"Future computation failed: {error_msg}")

                # Check timeout
                if timeout_seconds is not None:
                    elapsed = time.time() - start_time
                    if elapsed >= timeout_seconds:
                        return None

                # Brief sleep to avoid busy waiting
                time.sleep(0.001)  # 1ms

        finally:
            # Decrement waiter count
            current_waiters = waiters_atomic.load()
            while current_waiters > 0:
                if waiters_atomic.compare_exchange_weak(current_waiters, current_waiters - 1):
                    break

    def is_ready(self) -> bool:
        """Check if the future is ready (has value or error)."""
        state = struct.unpack_from('<I', self.buffer, 0)[0]
        return state in (FutureState.READY, FutureState.ERROR)

    def is_pending(self) -> bool:
        """Check if the future is still pending."""
        state = struct.unpack_from('<I', self.buffer, 0)[0]
        return state in (FutureState.PENDING, FutureState.COMPUTING)

    def is_error(self) -> bool:
        """Check if the future is in error state."""
        state = struct.unpack_from('<I', self.buffer, 0)[0]
        return state == FutureState.ERROR

    def get_status(self) -> FutureState:
        """Get current state of the future (alias for get_state)."""
        return self.get_state()

    def get_state(self) -> FutureState:
        """Get current state of the future."""
        state = struct.unpack_from('<I', self.buffer, 0)[0]
        return FutureState(state)

    def try_get(self) -> Optional[T]:
        """
        Try to get the value without blocking.

        Returns:
            The computed value if ready, None otherwise

        Raises:
            RuntimeError: If future is in error state
        """
        current_state = self.get_state()

        if current_state == FutureState.READY:
            value_offset = self._get_value_offset()
            if self.dtype.kind in 'iuf':
                # Map numpy char to struct format, handling int64 specifically
                if self.dtype == np.dtype('int64'):
                    format_char = 'q'  # Always use 'q' for int64 (8 bytes)
                elif self.dtype == np.dtype('uint64'):
                    format_char = 'Q'  # Always use 'Q' for uint64 (8 bytes)
                else:
                    format_char = self.dtype.char
                return struct.unpack_from(f'<{format_char}', self.buffer, value_offset)[0]
            else:
                value_bytes = self.buffer[value_offset:value_offset + self.value_size]
                return np.frombuffer(value_bytes, dtype=self.dtype)[0]
        elif current_state == FutureState.ERROR:
            error_offset = self._get_error_offset()
            error_bytes = bytes(self.buffer[error_offset:error_offset + 256])
            error_msg = error_bytes.split(b'\x00')[0].decode('utf-8', errors='ignore')
            raise RuntimeError(f"Future computation failed: {error_msg}")
        else:
            return None

    def then(self, callback: Callable[[T], Any]):
        """
        Register a callback to be executed when the future completes.

        Args:
            callback: Function to call with the result

        Note: This is a simple implementation that checks the future
        in the current thread. A full implementation would use
        event loops or background threads.
        """
        import threading
        import time

        def callback_worker():
            try:
                result = self.get()  # Will block until ready
                callback(result)
            except Exception:
                pass  # Ignore callback errors

        # Start callback in background thread
        thread = threading.Thread(target=callback_worker, daemon=True)
        thread.start()

    def get_waiter_count(self) -> int:
        """Get number of processes waiting on this future."""
        return struct.unpack_from('<I', self.buffer, 4)[0]

    def __bool__(self) -> bool:
        """Check if future is ready."""
        return self.is_ready()

    def __str__(self) -> str:
        """String representation."""
        state = self.get_state()
        return f"Future(name='{self.name}', state={state.name}, waiters={self.get_waiter_count()}, dtype={self.dtype})"

    def __repr__(self) -> str:
        """String representation."""
        return self.__str__()


class Promise:
    """
    Promise interface for setting Future values.

    This provides a cleaner interface for the producer side of Future computation.
    """

    def __init__(self, memory_or_future: Union[Memory, Future], name: Optional[str] = None,
                 dtype: Optional[Union[np.dtype, str, type]] = None):
        """
        Initialize Promise with associated Future.

        Args:
            memory_or_future: Either a Memory instance (to create new Future) or existing Future
            name: Future name (required if memory_or_future is Memory)
            dtype: Value data type (required if memory_or_future is Memory)
        """
        if isinstance(memory_or_future, Future):
            # Existing Future provided
            self.future = memory_or_future
        else:
            # Memory provided, create new Future
            if name is None or dtype is None:
                raise ValueError("name and dtype are required when creating new Promise")
            self.future = Future(memory_or_future, name, dtype=dtype)

    def get_future(self) -> Future:
        """Get the associated Future."""
        return self.future

    def set_value(self, value: T) -> bool:
        """Set the promise's value."""
        return self.future.set_value(value)

    def set_exception(self, error_code: Union[int, str]) -> bool:
        """Set the promise to error state."""
        return self.future.set_exception(error_code)

    def set_error(self, error_message: str) -> bool:
        """Set the promise to error state."""
        return self.future.set_error(error_message)

    def __str__(self) -> str:
        """String representation."""
        return f"Promise({self.future})"

    def __repr__(self) -> str:
        """String representation."""
        return self.__str__()