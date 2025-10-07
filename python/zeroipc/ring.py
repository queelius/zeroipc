"""
Lock-free ring buffer implementation for shared memory.

This module provides a ring buffer data structure that allows continuous
reading and writing of data in a circular buffer using atomic operations.
"""

import struct
from typing import Optional, TypeVar, Union, List
import numpy as np

from .memory import Memory
from .atomic import AtomicInt64

T = TypeVar('T')


class Ring:
    """
    Lock-free ring buffer in shared memory.

    This implementation uses atomic write and read positions to provide
    lock-free continuous data streaming. Data is stored in a circular buffer
    where new writes can overwrite old data if the buffer fills up.
    """

    def __init__(self, memory: Memory, name: str,
                 capacity: Optional[int] = None,
                 dtype: Optional[Union[np.dtype, str, type]] = None):
        """
        Create or open a ring buffer.

        Args:
            memory: Shared memory instance
            name: Ring buffer name
            capacity: Buffer capacity in number of elements (required for creation)
            dtype: Element data type

        Raises:
            ValueError: If required parameters are missing
            TypeError: If data type is not specified
            RuntimeError: If ring buffer is not found or type mismatch
        """
        if len(name) > 31:
            raise ValueError("Name too long (max 31 characters)")

        if dtype is None:
            raise TypeError("dtype is required")

        self.memory = memory
        self.name = name
        self.dtype = np.dtype(dtype)
        self.elem_size = self.dtype.itemsize

        # Try to find existing ring buffer
        entry = memory.table.find(name)

        if entry is None:
            # Create new ring buffer
            if capacity is None:
                raise ValueError("capacity required to create new ring buffer")

            if capacity == 0:
                raise ValueError("Ring capacity must be greater than 0")

            # Store capacity in elements and calculate byte capacity for buffer
            self.capacity = capacity  # Number of elements
            self.byte_capacity = capacity * self.elem_size  # Bytes for buffer

            self._create_new()
        else:
            # Open existing ring buffer
            self._open_existing(entry)

    def _create_new(self):
        """Create a new ring buffer in shared memory."""
        # Header: write_pos(8) + read_pos(8) + capacity_elements(4) + elem_size(4)
        header_size = 24
        total_size = header_size + self.byte_capacity

        # Allocate space
        self.offset = self.memory.table.allocate(total_size)

        if not self.memory.table.add(self.name, self.offset, total_size):
            raise RuntimeError("Failed to add ring buffer to table")

        # Get memory view
        self.buffer = self.memory.at(self.offset)

        # Initialize header - store element capacity and element size
        struct.pack_into('<QQII', self.buffer, 0,
                        0,  # write_pos
                        0,  # read_pos
                        self.capacity,  # capacity in elements
                        self.elem_size)

        # Zero out data area
        data_start = header_size
        for i in range(self.byte_capacity):
            self.buffer[data_start + i] = 0

    def _open_existing(self, entry):
        """Open an existing ring buffer from shared memory."""
        self.offset = entry.offset
        self.buffer = self.memory.at(self.offset)

        # Read header
        write_pos, read_pos, capacity, elem_size = struct.unpack_from('<QQII', self.buffer, 0)

        if elem_size != self.elem_size:
            raise RuntimeError(f"Type size mismatch: expected {self.elem_size}, got {elem_size}")

        self.capacity = capacity  # Elements
        self.byte_capacity = capacity * self.elem_size  # Bytes

    def _get_data_offset(self, position: int) -> int:
        """Get byte offset in data area for given position."""
        return 24 + (position % self.byte_capacity)

    def push(self, value: T) -> bool:
        """
        Push a single value to the ring buffer.

        Args:
            value: Value to push

        Returns:
            True if successful, False if full
        """
        bytes_written = self.write(value)
        return bytes_written > 0

    def write(self, data: Union[T, List[T], np.ndarray]) -> int:
        """
        Write data to the ring buffer.

        Args:
            data: Data to write (single element, list, or numpy array)

        Returns:
            Number of bytes written
        """
        # Convert data to bytes
        if isinstance(data, (list, tuple)):
            data_array = np.array(data, dtype=self.dtype)
        elif isinstance(data, np.ndarray):
            data_array = data.astype(self.dtype)
        else:
            data_array = np.array([data], dtype=self.dtype)

        data_bytes = data_array.tobytes()
        bytes_to_write = len(data_bytes)

        if bytes_to_write == 0:
            return 0

        # Check if we have enough space (bounded buffer behavior)
        available_space = self.available_write()
        if bytes_to_write > available_space:
            return 0  # Not enough space

        # Atomically reserve space for writing
        write_pos_atomic = AtomicInt64(self.buffer, 0)
        start_pos = write_pos_atomic.fetch_add(bytes_to_write)

        # Write data in chunks if it wraps around
        bytes_written = 0
        current_pos = start_pos

        while bytes_written < bytes_to_write:
            # Calculate how much we can write before wrapping
            ring_pos = current_pos % self.byte_capacity
            space_to_end = self.byte_capacity - ring_pos
            chunk_size = min(bytes_to_write - bytes_written, space_to_end)

            # Write chunk
            data_offset = self._get_data_offset(current_pos)
            chunk_data = data_bytes[bytes_written:bytes_written + chunk_size]
            self.buffer[data_offset:data_offset + chunk_size] = chunk_data

            bytes_written += chunk_size
            current_pos += chunk_size

        return bytes_to_write

    def pop(self) -> Optional[T]:
        """
        Pop a single value from the ring buffer (FIFO).

        Returns:
            Next value, or None if empty
        """
        data = self.read(self.elem_size)  # Read one element
        if data is not None and len(data) > 0:
            return data[0]
        return None

    def read(self, max_bytes: Optional[int] = None) -> Optional[np.ndarray]:
        """
        Read data from the ring buffer.

        Args:
            max_bytes: Maximum bytes to read (default: all available)

        Returns:
            Numpy array of read data, or None if no data available
        """
        write_pos_atomic = AtomicInt64(self.buffer, 0)
        read_pos_atomic = AtomicInt64(self.buffer, 8)

        write_pos = write_pos_atomic.load()
        read_pos = read_pos_atomic.load()

        # Calculate available data
        available = write_pos - read_pos

        if available <= 0:
            return None

        # Limit by max_bytes if specified
        if max_bytes is not None:
            available = min(available, max_bytes)

        # Ensure we read complete elements
        elements_available = available // self.elem_size
        if elements_available == 0:
            return None

        bytes_to_read = elements_available * self.elem_size

        # Atomically reserve data for reading
        actual_read_pos = read_pos_atomic.fetch_add(bytes_to_read)

        # Read data, handling wrapping
        data_bytes = bytearray(bytes_to_read)
        bytes_read = 0
        current_pos = actual_read_pos

        while bytes_read < bytes_to_read:
            # Calculate how much we can read before wrapping
            ring_pos = current_pos % self.byte_capacity
            space_to_end = self.byte_capacity - ring_pos
            chunk_size = min(bytes_to_read - bytes_read, space_to_end)

            # Read chunk
            data_offset = self._get_data_offset(current_pos)
            chunk_data = self.buffer[data_offset:data_offset + chunk_size]
            data_bytes[bytes_read:bytes_read + chunk_size] = chunk_data

            bytes_read += chunk_size
            current_pos += chunk_size

        # Convert to numpy array
        return np.frombuffer(data_bytes, dtype=self.dtype)

    def peek(self, max_bytes: Optional[int] = None) -> Optional[np.ndarray]:
        """
        Peek at data without consuming it.

        Args:
            max_bytes: Maximum bytes to peek (default: all available)

        Returns:
            Numpy array of data, or None if no data available
        """
        write_pos_atomic = AtomicInt64(self.buffer, 0)
        read_pos_atomic = AtomicInt64(self.buffer, 8)

        write_pos = write_pos_atomic.load()
        read_pos = read_pos_atomic.load()

        # Calculate available data
        available = write_pos - read_pos

        if available <= 0:
            return None

        # Limit by max_bytes if specified
        if max_bytes is not None:
            available = min(available, max_bytes)

        # Ensure we read complete elements
        elements_available = available // self.elem_size
        if elements_available == 0:
            return None

        bytes_to_read = elements_available * self.elem_size

        # Read data without advancing read position
        data_bytes = bytearray(bytes_to_read)
        bytes_read = 0
        current_pos = read_pos

        while bytes_read < bytes_to_read:
            # Calculate how much we can read before wrapping
            ring_pos = current_pos % self.byte_capacity
            space_to_end = self.byte_capacity - ring_pos
            chunk_size = min(bytes_to_read - bytes_read, space_to_end)

            # Read chunk
            data_offset = self._get_data_offset(current_pos)
            chunk_data = self.buffer[data_offset:data_offset + chunk_size]
            data_bytes[bytes_read:bytes_read + chunk_size] = chunk_data

            bytes_read += chunk_size
            current_pos += chunk_size

        # Convert to numpy array
        return np.frombuffer(data_bytes, dtype=self.dtype)

    def size(self) -> int:
        """Get number of elements currently in the ring buffer."""
        return self.available_read() // self.elem_size

    def front(self) -> Optional[T]:
        """
        Get the front (oldest) element without removing it.

        Returns:
            Front element, or None if empty
        """
        data = self.peek(self.elem_size)  # Peek one element
        if data is not None and len(data) > 0:
            return data[0]
        return None

    def back(self) -> Optional[T]:
        """
        Get the back (newest) element without removing it.

        Returns:
            Back element, or None if empty
        """
        write_pos_atomic = AtomicInt64(self.buffer, 0)
        read_pos_atomic = AtomicInt64(self.buffer, 8)

        write_pos = write_pos_atomic.load()
        read_pos = read_pos_atomic.load()

        available = write_pos - read_pos
        if available < self.elem_size:
            return None

        # Calculate position of the last complete element
        last_element_pos = write_pos - self.elem_size

        # Read the last element
        data_bytes = bytearray(self.elem_size)
        bytes_read = 0
        current_pos = last_element_pos

        while bytes_read < self.elem_size:
            ring_pos = current_pos % self.byte_capacity
            space_to_end = self.byte_capacity - ring_pos
            chunk_size = min(self.elem_size - bytes_read, space_to_end)

            data_offset = self._get_data_offset(current_pos)
            chunk_data = self.buffer[data_offset:data_offset + chunk_size]
            data_bytes[bytes_read:bytes_read + chunk_size] = chunk_data

            bytes_read += chunk_size
            current_pos += chunk_size

        return np.frombuffer(data_bytes, dtype=self.dtype)[0]

    def clear(self):
        """Clear all data from the ring buffer (alias for reset)."""
        self.reset()

    def available_read(self) -> int:
        """Get number of bytes available for reading."""
        write_pos = struct.unpack_from('<Q', self.buffer, 0)[0]
        read_pos = struct.unpack_from('<Q', self.buffer, 8)[0]
        return max(0, write_pos - read_pos)

    def available_write(self) -> int:
        """
        Get approximate space available for writing.

        Note: In a ring buffer, this is theoretically infinite as old data
        can be overwritten. This returns the space before overwriting occurs.
        """
        write_pos = struct.unpack_from('<Q', self.buffer, 0)[0]
        read_pos = struct.unpack_from('<Q', self.buffer, 8)[0]
        used = write_pos - read_pos
        return max(0, self.byte_capacity - used)

    def empty(self) -> bool:
        """Check if ring buffer has no data to read."""
        return self.available_read() == 0

    def full(self) -> bool:
        """Check if ring buffer is full (no space before overwriting)."""
        return self.available_write() == 0

    def reset(self):
        """
        Reset the ring buffer (clear all data).

        Warning: This operation is not atomic and should only be used
        when no other processes are accessing the buffer.
        """
        struct.pack_into('<QQ', self.buffer, 0, 0, 0)  # Reset both positions

    def __len__(self) -> int:
        """Get number of bytes available for reading."""
        return self.available_read()

    def __bool__(self) -> bool:
        """Check if ring buffer has data to read."""
        return not self.empty()

    def __str__(self) -> str:
        """String representation."""
        return (f"Ring(name='{self.name}', capacity={self.capacity}, "
                f"available_read={self.available_read()}, dtype={self.dtype})")

    def __repr__(self) -> str:
        """String representation."""
        return self.__str__()