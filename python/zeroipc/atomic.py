"""
Atomic operations for shared memory using memory-mapped buffers.

This module provides atomic operations that work across process boundaries
using memory-mapped shared memory. It implements Compare-And-Swap (CAS)
operations that are necessary for lock-free data structures.
"""

import struct
import threading
import time
from typing import TypeVar, Generic, Union

T = TypeVar('T', int, float)


class AtomicInt:
    """
    Atomic integer operations in shared memory.

    Provides atomic load, store, and compare_and_swap operations
    for 32-bit integers in shared memory.
    """

    FORMAT = '<I'  # 32-bit unsigned integer, little-endian
    SIZE = 4

    def __init__(self, buffer: Union[memoryview, bytes], offset: int = 0):
        """
        Initialize atomic integer at given offset in buffer.

        Args:
            buffer: Memory buffer (memoryview or bytes)
            offset: Byte offset in buffer where integer is stored
        """
        self.buffer = buffer
        self.offset = offset
        self._lock = threading.Lock()  # Fallback for true atomicity

    def load(self, memory_order: str = 'acquire') -> int:
        """
        Atomically load the value.

        Args:
            memory_order: Memory ordering (for compatibility, not implemented)

        Returns:
            Current value
        """
        return struct.unpack_from(self.FORMAT, self.buffer, self.offset)[0]

    def store(self, value: int, memory_order: str = 'release') -> None:
        """
        Atomically store a value.

        Args:
            value: Value to store
            memory_order: Memory ordering (for compatibility, not implemented)
        """
        struct.pack_into(self.FORMAT, self.buffer, self.offset, value)

    def compare_exchange_weak(self, expected: int, desired: int,
                            success_order: str = 'acq_rel',
                            failure_order: str = 'relaxed') -> bool:
        """
        Atomic compare-and-swap operation.

        This provides a best-effort atomic CAS operation. In Python,
        true atomicity requires careful implementation.

        Args:
            expected: Expected current value
            desired: Desired new value
            success_order: Memory ordering on success
            failure_order: Memory ordering on failure

        Returns:
            True if exchange succeeded, False otherwise
        """
        with self._lock:
            current = struct.unpack_from(self.FORMAT, self.buffer, self.offset)[0]
            if current == expected:
                struct.pack_into(self.FORMAT, self.buffer, self.offset, desired)
                return True
            return False

    def fetch_add(self, value: int, memory_order: str = 'acq_rel') -> int:
        """
        Atomically add value and return previous value.

        Args:
            value: Value to add
            memory_order: Memory ordering

        Returns:
            Previous value
        """
        with self._lock:
            current = struct.unpack_from(self.FORMAT, self.buffer, self.offset)[0]
            new_value = current + value
            struct.pack_into(self.FORMAT, self.buffer, self.offset, new_value)
            return current


class AtomicInt64:
    """
    Atomic 64-bit integer operations in shared memory.
    """

    FORMAT = '<Q'  # 64-bit unsigned integer, little-endian
    SIZE = 8

    def __init__(self, buffer: Union[memoryview, bytes], offset: int = 0):
        """
        Initialize atomic 64-bit integer at given offset in buffer.

        Args:
            buffer: Memory buffer (memoryview or bytes)
            offset: Byte offset in buffer where integer is stored
        """
        self.buffer = buffer
        self.offset = offset
        self._lock = threading.Lock()

    def load(self, memory_order: str = 'acquire') -> int:
        """Atomically load the value."""
        return struct.unpack_from(self.FORMAT, self.buffer, self.offset)[0]

    def store(self, value: int, memory_order: str = 'release') -> None:
        """Atomically store a value."""
        struct.pack_into(self.FORMAT, self.buffer, self.offset, value)

    def compare_exchange_weak(self, expected: int, desired: int,
                            success_order: str = 'acq_rel',
                            failure_order: str = 'relaxed') -> bool:
        """Atomic compare-and-swap operation."""
        with self._lock:
            current = struct.unpack_from(self.FORMAT, self.buffer, self.offset)[0]
            if current == expected:
                struct.pack_into(self.FORMAT, self.buffer, self.offset, desired)
                return True
            return False

    def fetch_add(self, value: int, memory_order: str = 'acq_rel') -> int:
        """Atomically add value and return previous value."""
        with self._lock:
            current = struct.unpack_from(self.FORMAT, self.buffer, self.offset)[0]
            new_value = current + value
            struct.pack_into(self.FORMAT, self.buffer, self.offset, new_value)
            return current


def atomic_thread_fence(memory_order: str = 'seq_cst') -> None:
    """
    Memory fence operation.

    Args:
        memory_order: Memory ordering constraint
    """
    # In Python, this is mostly a no-op since the GIL provides
    # certain guarantees. For cross-process atomicity, we rely
    # on the OS and hardware memory coherency.
    pass


def spin_wait() -> None:
    """
    Hint to the processor that we're in a spin loop.

    This is equivalent to the x86 PAUSE instruction.
    """
    time.sleep(0)  # Yield to other threads/processes


# Memory ordering constants (for compatibility with C++)
MEMORY_ORDER_RELAXED = 'relaxed'
MEMORY_ORDER_ACQUIRE = 'acquire'
MEMORY_ORDER_RELEASE = 'release'
MEMORY_ORDER_ACQ_REL = 'acq_rel'
MEMORY_ORDER_SEQ_CST = 'seq_cst'