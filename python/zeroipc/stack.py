"""Stack implementation for shared memory.

Uses 4-state CAS protocol binary layout matching C++/Go/C format.

Note: Python's struct operations are not truly atomic across processes.
Cross-process usage should follow SPSC discipline. For cross-process MPMC,
use the C++ or Go implementations.
"""

import struct
import time
import threading
from typing import Optional, TypeVar, Generic, Type
import numpy as np

from .memory import Memory

T = TypeVar('T')

# Per-slot state values matching C++/Go/C
_SLOT_EMPTY   = 0
_SLOT_WRITING = 1
_SLOT_READY   = 2
_SLOT_READING = 3

# Per-slot state format (uint32_t, matching C++ std::atomic<uint32_t>)
_STATE_FORMAT = 'I'
_STATE_SIZE = struct.calcsize(_STATE_FORMAT)


class Stack(Generic[T]):
    """Lock-free stack in shared memory.

    Uses atomic operations for thread-safe push/pop.
    Per-slot ready flags prevent reading uninitialized data when a
    concurrent push has advanced the top index but not yet finished
    writing the element.
    """

    HEADER_FORMAT = 'iII'  # top (signed), capacity, elem_size
    HEADER_SIZE = struct.calcsize(HEADER_FORMAT)

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
            # Layout: [Header][data: T * capacity][ready: uint32 * capacity]
            total_size = (self.HEADER_SIZE
                          + self.elem_size * capacity
                          + _STATE_SIZE * capacity)

            # Allocate in shared memory
            self.offset = memory.allocate(name, total_size)

            # Initialize header (top=-1 means empty)
            header_data = struct.pack(self.HEADER_FORMAT,
                                    -1,  # top (empty)
                                    capacity,
                                    self.elem_size)
            memory.data[self.offset:self.offset + self.HEADER_SIZE] = header_data

            # Initialize all ready flags to 0
            ready_base = self.offset + self.HEADER_SIZE + self.elem_size * capacity
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

        # Base offset for the ready flags array in shared memory
        self._ready_base = self.offset + self.HEADER_SIZE + self.elem_size * self.capacity

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
        _, capacity, _ = self._read_header()
        return capacity

    def _read_elem_size(self) -> int:
        """Read element size from header."""
        _, _, elem_size = self._read_header()
        return elem_size

    def _read_ready(self, index: int) -> int:
        """Read the ready flag for a slot."""
        off = self._ready_base + index * _STATE_SIZE
        return struct.unpack_from(_STATE_FORMAT, self.memory.data, off)[0]

    def _write_ready(self, index: int, value: int):
        """Write the ready flag for a slot."""
        off = self._ready_base + index * _STATE_SIZE
        struct.pack_into(_STATE_FORMAT, self.memory.data, off, value)

    def push(self, value: T) -> bool:
        """Push value onto stack.

        Uses full 4-state protocol matching C++/Go/C:
        EMPTY(0) -> WRITING(1) -> READY(2)

        Returns:
            True if successful, False if stack is full
        """
        with self._lock:
            top, capacity, elem_size = self._read_header()

            # Check if full
            if top >= capacity - 1:
                return False

            # Step 1: Advance top to reserve slot
            new_top = top + 1
            self._write_header(new_top, capacity, elem_size)

            # Step 2: Transition EMPTY -> WRITING (wait for slot to be EMPTY)
            while self._read_ready(new_top) != _SLOT_EMPTY:
                time.sleep(0)  # yield
            self._write_ready(new_top, _SLOT_WRITING)

            # Step 3: Write data
            self.data[new_top] = value

            # Step 4: Transition WRITING -> READY
            self._write_ready(new_top, _SLOT_READY)
            return True

    def pop(self) -> Optional[T]:
        """Pop value from stack.

        Uses full 4-state protocol matching C++/Go/C:
        READY(2) -> READING(3) -> EMPTY(0)

        Returns:
            Value if available, None if stack is empty
        """
        with self._lock:
            top, capacity, elem_size = self._read_header()

            # Check if empty
            if top < 0:
                return None

            # Step 1: Decrement top to reserve slot
            self._write_header(top - 1, capacity, elem_size)

            # Step 2: Transition READY -> READING (wait for data to be ready)
            while self._read_ready(top) != _SLOT_READY:
                time.sleep(0)  # yield
            self._write_ready(top, _SLOT_READING)

            # Step 3: Read value
            value = self.data[top].copy()

            # Step 4: Transition READING -> EMPTY
            self._write_ready(top, _SLOT_EMPTY)

            return value

    _PEEK_MAX_SPINS = 10000

    def top(self) -> Optional[T]:
        """Peek at top value without removing it.

        Returns:
            Value if available, None if stack is empty or top changed during peek
        """
        top_idx, _, _ = self._read_header()

        if top_idx < 0:
            return None

        # Wait for slot data to be ready, but bail if top changes
        spins = 0
        while self._read_ready(top_idx) != _SLOT_READY:
            spins += 1
            if spins > self._PEEK_MAX_SPINS:
                return None  # Slot was popped or never became ready
            # Re-check that top hasn't changed (avoids infinite spin)
            current_top, _, _ = self._read_header()
            if current_top != top_idx:
                return None
            time.sleep(0)  # yield

        return self.data[top_idx].copy()

    def empty(self) -> bool:
        """Check if stack is empty."""
        top, _, _ = self._read_header()
        return top < 0

    def full(self) -> bool:
        """Check if stack is full."""
        top, capacity, _ = self._read_header()
        return top >= capacity - 1

    def size(self) -> int:
        """Get current number of elements."""
        top, _, _ = self._read_header()
        return 0 if top < 0 else top + 1

    def __len__(self) -> int:
        """Get current size."""
        return self.size()

    def __bool__(self) -> bool:
        """Check if not empty."""
        return not self.empty()