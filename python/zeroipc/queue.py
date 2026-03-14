"""Queue implementation for shared memory.

Uses Vyukov bounded MPMC queue binary layout with per-slot sequence numbers,
matching the C++/Go/C format for cross-language interoperability.

Binary layout:
  [head:u32][tail:u32][capacity:u32][elem_size:u32]  (16 bytes header)
  [data[0]][data[1]]...[data[cap-1]]                 (elem_size * capacity bytes)
  [seq[0]:u32][seq[1]:u32]...[seq[cap-1]:u32]        (4 * capacity bytes)

When libzeroipc_ffi.so is available, push/pop use C11 atomics via ctypes
for true cross-process MPMC safety. Otherwise falls back to struct.pack_into
(SPSC-only across processes, MPMC within a single interpreter via threading.Lock).
"""

import struct
import threading
from typing import Optional, TypeVar, Generic, Type
import numpy as np

from .memory import Memory
from . import _cffi

T = TypeVar('T')

# Sequence number format (uint32)
_SEQ_FORMAT = 'I'
_SEQ_SIZE = struct.calcsize(_SEQ_FORMAT)


class Queue(Generic[T]):
    """Lock-free circular buffer queue in shared memory.

    Uses Vyukov bounded MPMC algorithm with per-slot sequence numbers
    for cross-language compatibility with C++, Go, and C.

    Safe for both multi-threaded and multi-process use. The Vyukov
    algorithm uses per-slot sequence numbers to detect contention
    without requiring cross-process locks.
    """

    HEADER_FORMAT = 'IIII'  # head, tail, capacity, elem_size
    HEADER_SIZE = struct.calcsize(HEADER_FORMAT)

    def __init__(self, memory: Memory, name: str,
                 capacity: Optional[int] = None,
                 dtype: Optional[Type] = None):
        """Create or open a queue.

        Args:
            memory: Memory instance
            name: Queue identifier
            capacity: Number of elements (required for creation)
            dtype: Element type (required)
        """
        self.memory = memory
        self.name = name

        if dtype is None:
            raise TypeError("dtype is required for Queue")

        # Convert dtype to numpy dtype
        self.dtype = np.dtype(dtype)
        self.elem_size = self.dtype.itemsize

        # Try to find existing queue
        entry = memory.table.find(name)

        if entry is None:
            # Create new queue
            if capacity is None:
                raise ValueError("capacity required to create new queue")
            if capacity < 1:
                raise ValueError("capacity must be at least 1")

            self.capacity = capacity
            # Layout: [Header][data: T * capacity][seq: uint32 * capacity]
            total_size = (self.HEADER_SIZE
                          + self.elem_size * capacity
                          + _SEQ_SIZE * capacity)

            # Allocate in shared memory
            self.offset = memory.allocate(name, total_size)

            # Initialize header
            header_data = struct.pack(self.HEADER_FORMAT,
                                    0,  # head
                                    0,  # tail
                                    capacity,
                                    self.elem_size)
            memory.data[self.offset:self.offset + self.HEADER_SIZE] = header_data

            # Initialize per-slot sequence numbers: seq[i] = i
            seq_base = self.offset + self.HEADER_SIZE + self.elem_size * capacity
            for i in range(capacity):
                off = seq_base + i * _SEQ_SIZE
                struct.pack_into(_SEQ_FORMAT, memory.data, off, i)

        else:
            # Open existing queue
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

        # Base offset for the sequence array
        self._seq_base = self.offset + self.HEADER_SIZE + self.elem_size * self.capacity

        # Offsets for head/tail in shared memory
        self._head_off = self.offset
        self._tail_off = self.offset + 4

        # Lock for thread safety within a single process.
        # Cross-process safety relies on SPSC discipline (one producer
        # process writes tail, one consumer process writes head).
        self._lock = threading.Lock()

    def _read_head(self) -> int:
        """Read head from header."""
        return struct.unpack_from('I', self.memory.data, self._head_off)[0]

    def _write_head(self, head: int):
        """Write head to header."""
        struct.pack_into('I', self.memory.data, self._head_off, head)

    def _read_tail(self) -> int:
        """Read tail from header."""
        return struct.unpack_from('I', self.memory.data, self._tail_off)[0]

    def _write_tail(self, tail: int):
        """Write tail to header."""
        struct.pack_into('I', self.memory.data, self._tail_off, tail)

    def _read_capacity(self) -> int:
        """Read capacity from header."""
        return struct.unpack_from('I', self.memory.data, self.offset + 8)[0]

    def _read_elem_size(self) -> int:
        """Read element size from header."""
        return struct.unpack_from('I', self.memory.data, self.offset + 12)[0]

    def _read_seq(self, slot: int) -> int:
        """Read sequence number for a slot."""
        off = self._seq_base + slot * _SEQ_SIZE
        return struct.unpack_from(_SEQ_FORMAT, self.memory.data, off)[0]

    def _write_seq(self, slot: int, value: int):
        """Write sequence number for a slot."""
        off = self._seq_base + slot * _SEQ_SIZE
        struct.pack_into(_SEQ_FORMAT, self.memory.data, off, value & 0xFFFFFFFF)

    @staticmethod
    def _signed_diff(a: int, b: int) -> int:
        """Compute (a - b) as a signed 32-bit difference.

        Handles uint32 wraparound by interpreting the unsigned difference
        as a signed int32 value.
        """
        diff = (a - b) & 0xFFFFFFFF
        if diff > 0x7FFFFFFF:
            diff -= 0x100000000
        return diff

    def push(self, value: T) -> bool:
        """Push value onto queue. Returns True on success, False if full."""
        if _cffi.AVAILABLE:
            value_bytes = self.dtype.type(value).tobytes()
            return _cffi.queue_push(self.memory, self.offset,
                                    value_bytes, self.elem_size) == _cffi.OK

        with self._lock:
            tail = self._read_tail()
            slot = tail % self.capacity
            seq = self._read_seq(slot)

            if self._signed_diff(seq, tail) != 0:
                return False

            self._write_tail((tail + 1) & 0xFFFFFFFF)
            self.data[slot] = value
            self._write_seq(slot, (tail + 1) & 0xFFFFFFFF)
            return True

    def pop(self) -> Optional[T]:
        """Pop value from queue. Returns value or None if empty."""
        if _cffi.AVAILABLE:
            rc, raw = _cffi.queue_pop(self.memory, self.offset, self.elem_size)
            if rc != _cffi.OK:
                return None
            return np.frombuffer(raw, dtype=self.dtype)[0].copy()

        with self._lock:
            head = self._read_head()
            slot = head % self.capacity
            seq = self._read_seq(slot)

            expected = (head + 1) & 0xFFFFFFFF
            if self._signed_diff(seq, expected) != 0:
                return None

            self._write_head((head + 1) & 0xFFFFFFFF)
            value = self.data[slot].copy()
            self._write_seq(slot, (head + self.capacity) & 0xFFFFFFFF)
            return value

    def empty(self) -> bool:
        """Check if queue is empty."""
        if _cffi.AVAILABLE:
            return _cffi.queue_empty(self.memory, self.offset)
        return self._read_head() == self._read_tail()

    def full(self) -> bool:
        """Check if queue is full."""
        if _cffi.AVAILABLE:
            return _cffi.queue_full(self.memory, self.offset)
        return ((self._read_tail() - self._read_head()) & 0xFFFFFFFF) >= self.capacity

    def size(self) -> int:
        """Get current number of elements."""
        if _cffi.AVAILABLE:
            return _cffi.queue_size(self.memory, self.offset)
        return (self._read_tail() - self._read_head()) & 0xFFFFFFFF

    def __len__(self) -> int:
        """Get current size."""
        return self.size()

    def __bool__(self) -> bool:
        """Check if not empty."""
        return not self.empty()
