"""Stack implementation for shared memory.

Uses 4-state CAS protocol binary layout matching C++/Go/C format.

When libzeroipc_ffi.so is available, push/pop use C11 atomics via ctypes
for true cross-process MPMC safety. Otherwise falls back to struct.pack_into
(SPSC-only across processes, MPMC within a single interpreter via threading.Lock).
"""

import struct
import time
import threading
from typing import Optional, TypeVar, Generic, Type
import numpy as np

from .memory import Memory
from . import _cffi

T = TypeVar('T')

# Per-slot state values matching C++/Go/C
_SLOT_EMPTY   = 0
_SLOT_WRITING = 1
_SLOT_READY   = 2
_SLOT_READING = 3

# Per-slot state format (uint32_t, matching C++ std::atomic<uint32_t>)
_STATE_FORMAT = 'I'
_STATE_SIZE = struct.calcsize(_STATE_FORMAT)


def _align8(n: int) -> int:
    """Round n up to the next multiple of 8 (8-byte section alignment, format v2)."""
    return (n + 7) & ~7


class Stack(Generic[T]):
    """Lock-free stack in shared memory.

    Uses atomic operations for thread-safe push/pop.
    Per-slot ready flags prevent reading uninitialized data when a
    concurrent push has advanced the top index but not yet finished
    writing the element.
    """

    # top (signed), capacity, elem_size, + 4 pad bytes (reserved). The trailing
    # pad makes the on-disk header 16 bytes so the data array is 8-aligned
    # (format v2). struct ignores the pad bytes on unpack and zeroes them on pack,
    # so header readers/writers still see three fields.
    HEADER_FORMAT = 'iII4x'
    HEADER_SIZE = struct.calcsize(HEADER_FORMAT)  # 16

    def __init__(self, memory: Memory, name: str,
                 capacity: Optional[int] = None,
                 dtype: Optional[Type] = None):
        """Create or open a stack.

        Args:
            memory: Memory instance
            name: Stack identifier
            capacity: Number of elements (required for creation)
            dtype: Element type (required)
        """
        self.memory = memory
        self.name = name

        if dtype is None:
            raise TypeError("dtype is required for Stack")

        # Convert dtype to numpy dtype
        self.dtype = np.dtype(dtype)
        self.elem_size = self.dtype.itemsize

        # Try to find existing stack
        entry = memory.table.find(name)

        if entry is None:
            # Create new stack
            if capacity is None:
                raise ValueError("capacity required to create new stack")

            self.capacity = capacity
            # Layout: [Header(16)][data: T*capacity][pad][state: uint32*capacity]
            total_size = (self.HEADER_SIZE
                          + _align8(self.elem_size * capacity)
                          + _STATE_SIZE * capacity)

            # Allocate in shared memory
            self.offset = memory.allocate(name, total_size)

            # Initialize header (top=-1 means empty)
            header_data = struct.pack(self.HEADER_FORMAT,
                                    -1,  # top (empty)
                                    capacity,
                                    self.elem_size)
            memory.data[self.offset:self.offset + self.HEADER_SIZE] = header_data

            # Initialize all ready flags to 0 (state array starts 8-aligned)
            ready_base = self.offset + self.HEADER_SIZE + _align8(self.elem_size * capacity)
            for i in range(capacity):
                off = ready_base + i * _STATE_SIZE
                struct.pack_into(_STATE_FORMAT, memory.data, off, 0)

        else:
            # Open existing stack
            self.offset = entry.offset
            self.capacity = capacity if capacity else self._read_capacity()

            # Verify element size matches
            stored_elem_size = self._read_elem_size()
            if stored_elem_size != self.elem_size:
                raise ValueError(f"Element size mismatch: expected {self.elem_size}, "
                                f"found {stored_elem_size}")

        # Create numpy array view of data
        data_offset = self.offset + self.HEADER_SIZE
        self.data = np.frombuffer(
            self.memory.data,
            dtype=self.dtype,
            count=self.capacity,
            offset=data_offset
        )

        # Base offset for the ready flags array in shared memory (8-aligned)
        self._ready_base = self.offset + self.HEADER_SIZE + _align8(self.elem_size * self.capacity)

        # Lock for atomic operations
        self._lock = threading.Lock()

    def _read_header(self):
        """Read header values."""
        header_bytes = self.memory.data[self.offset:self.offset + self.HEADER_SIZE]
        return struct.unpack(self.HEADER_FORMAT, header_bytes)

    def _write_header(self, top: int, capacity: int, elem_size: int):
        """Write header values."""
        header_data = struct.pack(self.HEADER_FORMAT, top, capacity, elem_size)
        self.memory.data[self.offset:self.offset + self.HEADER_SIZE] = header_data

    def _read_capacity(self) -> int:
        """Read capacity from header."""
        return struct.unpack_from('I', self.memory.data, self.offset + 4)[0]

    def _read_elem_size(self) -> int:
        """Read element size from header."""
        return struct.unpack_from('I', self.memory.data, self.offset + 8)[0]

    def _read_ready(self, index: int) -> int:
        """Read the ready flag for a slot."""
        off = self._ready_base + index * _STATE_SIZE
        return struct.unpack_from(_STATE_FORMAT, self.memory.data, off)[0]

    def _write_ready(self, index: int, value: int):
        """Write the ready flag for a slot."""
        off = self._ready_base + index * _STATE_SIZE
        struct.pack_into(_STATE_FORMAT, self.memory.data, off, value)

    def push(self, value: T) -> bool:
        """Push value onto stack. Returns True on success, False if full."""
        if _cffi.AVAILABLE:
            value_bytes = self.dtype.type(value).tobytes()
            return _cffi.stack_push(self.memory, self.offset,
                                    value_bytes, self.elem_size) == _cffi.OK

        with self._lock:
            top, capacity, elem_size = self._read_header()
            if top >= capacity - 1:
                return False

            new_top = top + 1
            self._write_header(new_top, capacity, elem_size)

            while self._read_ready(new_top) != _SLOT_EMPTY:
                time.sleep(0)
            self._write_ready(new_top, _SLOT_WRITING)
            self.data[new_top] = value
            self._write_ready(new_top, _SLOT_READY)
            return True

    def pop(self) -> Optional[T]:
        """Pop value from stack. Returns value or None if empty."""
        if _cffi.AVAILABLE:
            rc, raw = _cffi.stack_pop(self.memory, self.offset, self.elem_size)
            if rc != _cffi.OK:
                return None
            return np.frombuffer(raw, dtype=self.dtype)[0].copy()

        with self._lock:
            top, capacity, elem_size = self._read_header()
            if top < 0:
                return None

            self._write_header(top - 1, capacity, elem_size)

            while self._read_ready(top) != _SLOT_READY:
                time.sleep(0)
            self._write_ready(top, _SLOT_READING)
            value = self.data[top].copy()
            self._write_ready(top, _SLOT_EMPTY)
            return value

    def top(self) -> Optional[T]:
        """Peek at top value without removing it.

        Best-effort: the peek must win a CAS on the slot state to read safely,
        so under heavy contention (or a crashed peer holding the slot) it can
        return None even though the stack is non-empty. Do not treat None as
        an authoritative emptiness check; use size() for that.
        """
        if _cffi.AVAILABLE:
            rc, raw = _cffi.stack_top(self.memory, self.offset, self.elem_size)
            if rc != _cffi.OK:
                return None
            return np.frombuffer(raw, dtype=self.dtype)[0].copy()

        # Mirror pop: claim the slot exclusively (READY -> READING), copy, then
        # restore READY, so the copy never races a concurrent push recycling the
        # slot. Hold the lock for the same within-interpreter mutual exclusion
        # push/pop use.
        with self._lock:
            top_idx, _, _ = self._read_header()
            if top_idx < 0:
                return None

            for _ in range(10000):
                if self._read_ready(top_idx) == _SLOT_READY:
                    self._write_ready(top_idx, _SLOT_READING)
                    value = self.data[top_idx].copy()
                    self._write_ready(top_idx, _SLOT_READY)
                    return value
                current_top, _, _ = self._read_header()
                if current_top != top_idx:
                    return None
                time.sleep(0)
            return None

    def empty(self) -> bool:
        """Check if stack is empty."""
        if _cffi.AVAILABLE:
            return _cffi.stack_empty(self.memory, self.offset)
        top, _, _ = self._read_header()
        return top < 0

    def full(self) -> bool:
        """Check if stack is full."""
        if _cffi.AVAILABLE:
            return _cffi.stack_full(self.memory, self.offset)
        top, capacity, _ = self._read_header()
        return top >= capacity - 1

    def size(self) -> int:
        """Get current number of elements."""
        if _cffi.AVAILABLE:
            return _cffi.stack_size(self.memory, self.offset)
        top, _, _ = self._read_header()
        return 0 if top < 0 else top + 1

    def __len__(self) -> int:
        """Get current size."""
        return self.size()

    def __bool__(self) -> bool:
        """Check if not empty."""
        return not self.empty()