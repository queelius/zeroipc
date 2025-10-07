"""
Lock-free memory pool implementation for shared memory.

This module provides a memory pool data structure that allows allocation
and deallocation of fixed-size objects in shared memory using lock-free
operations with a free list.
"""

import struct
from typing import Optional, TypeVar, Union, List
import numpy as np

from .memory import Memory
from .atomic import AtomicInt

T = TypeVar('T')


class Pool:
    """
    Lock-free memory pool in shared memory.

    This implementation uses a free list to manage allocation and deallocation
    of fixed-size objects. All operations are lock-free using atomic operations.
    """

    NULL_INDEX = 0xFFFFFFFF

    def __init__(self, memory: Memory, name: str,
                 capacity: Optional[int] = None,
                 dtype: Optional[Union[np.dtype, str, type]] = None,
                 block_size: Optional[int] = None,
                 block_count: Optional[int] = None):
        """
        Create or open a memory pool.

        Args:
            memory: Shared memory instance
            name: Pool name
            capacity: Number of slots (for backward compatibility)
            dtype: Element data type (for backward compatibility)
            block_size: Size of each block in bytes (new API)
            block_count: Number of blocks (new API)

        Raises:
            ValueError: If required parameters are missing
            TypeError: If data type is not specified
            RuntimeError: If pool is not found or type mismatch
        """
        if len(name) > 31:
            raise ValueError("Name too long (max 31 characters)")

        self.memory = memory
        self.name = name

        # Try to find existing pool first
        entry = memory.table.find(name)

        if entry is not None:
            # Opening existing pool - read metadata from header
            self._open_existing_init(entry)
        else:
            # Creating new pool - validate parameters
            # Handle both API styles
            if block_size is not None and block_count is not None:
                # New API: block-based
                if block_size <= 0:
                    raise ValueError("block_size must be greater than 0")
                if block_count <= 0:
                    raise ValueError("block_count must be greater than 0")

                self.block_size = block_size
                self.block_count = block_count
                self.capacity = block_count  # For compatibility
                self.elem_size = block_size
                self.dtype = np.dtype('uint8')  # Treat as raw bytes
            else:
                # Old API: dtype-based
                if dtype is None:
                    raise TypeError("dtype is required")

                self.dtype = np.dtype(dtype)
                self.elem_size = self.dtype.itemsize
                self.block_size = self.elem_size  # For compatibility

                if capacity is None:
                    raise ValueError("capacity required to create new pool")

                if capacity == 0:
                    raise ValueError("Pool capacity must be greater than 0")

                self.capacity = capacity
                self.block_count = capacity  # For compatibility

            # Node size: data + next_index(4)
            # Align to 8 bytes
            node_size = self.elem_size + 4
            self.node_size = (node_size + 7) & ~7

            # Create new pool
            self._create_new()

    def _create_new(self):
        """Create a new pool in shared memory."""
        # Header: free_head(4) + allocated(4) + capacity(4) + elem_size(4)
        header_size = 16
        total_size = header_size + self.node_size * self.capacity

        # Allocate space
        self.offset = self.memory.table.allocate(total_size)

        if not self.memory.table.add(self.name, self.offset, total_size):
            raise RuntimeError("Failed to add pool to table")

        # Get memory view
        self.buffer = self.memory.at(self.offset)

        # Initialize header
        struct.pack_into('<IIII', self.buffer, 0,
                        0,  # free_head starts at index 0
                        0,  # allocated count
                        self.capacity,
                        self.elem_size)

        # Initialize free list - all nodes are free
        for i in range(self.capacity - 1):
            node_offset = header_size + i * self.node_size
            next_index = i + 1
            # Write next index at the end of each node
            struct.pack_into('<I', self.buffer, node_offset + self.elem_size, next_index)

        # Last node points to NULL_INDEX
        last_node_offset = header_size + (self.capacity - 1) * self.node_size
        struct.pack_into('<I', self.buffer, last_node_offset + self.elem_size, self.NULL_INDEX)

    def _open_existing_init(self, entry):
        """Open an existing pool from shared memory."""
        self.offset = entry.offset
        self.buffer = self.memory.at(self.offset)

        # Read header
        free_head, allocated, capacity, elem_size = struct.unpack_from('<IIII', self.buffer, 0)

        # Initialize fields from header
        self.capacity = capacity
        self.elem_size = elem_size
        self.block_size = elem_size
        self.block_count = capacity
        self.dtype = np.dtype('uint8')  # Treat as raw bytes when opening

        # Node size: data + next_index(4)
        # Align to 8 bytes
        node_size = self.elem_size + 4
        self.node_size = (node_size + 7) & ~7

    def _get_node_offset(self, index: int) -> int:
        """Get byte offset of node at given index."""
        return 16 + index * self.node_size

    def _read_node_next(self, index: int) -> int:
        """Read next index from node."""
        offset = self._get_node_offset(index) + self.elem_size
        return struct.unpack_from('<I', self.buffer, offset)[0]

    def _write_node_next(self, index: int, next_index: int):
        """Write next index to node."""
        offset = self._get_node_offset(index) + self.elem_size
        struct.pack_into('<I', self.buffer, offset, next_index)

    def _read_node_data(self, index: int) -> T:
        """Read data from node."""
        offset = self._get_node_offset(index)
        if self.dtype.kind in 'iuf':  # integer, unsigned, float
            return struct.unpack_from(f'<{self.dtype.char}', self.buffer, offset)[0]
        else:
            # For complex types, read raw bytes and convert
            data_bytes = self.buffer[offset:offset + self.elem_size]
            return np.frombuffer(data_bytes, dtype=self.dtype)[0]

    def _write_node_data(self, index: int, data: T):
        """Write data to node."""
        offset = self._get_node_offset(index)
        if self.dtype.kind in 'iuf':
            struct.pack_into(f'<{self.dtype.char}', self.buffer, offset, data)
        else:
            data_array = np.array([data], dtype=self.dtype)
            self.buffer[offset:offset + self.elem_size] = data_array.tobytes()

    def allocate(self) -> Optional[int]:
        """
        Allocate an object from the pool.

        Returns:
            Index of allocated object, or None if pool is full
        """
        # Try to get a free node
        free_head_atomic = AtomicInt(self.buffer, 0)

        while True:
            free_index = free_head_atomic.load()

            if free_index == self.NULL_INDEX:
                return None  # Pool is full

            # Read next from the free node
            next_index = self._read_node_next(free_index)

            # Try to update the free head
            if free_head_atomic.compare_exchange_weak(free_index, next_index):
                # Successfully allocated, increment allocated count
                allocated_atomic = AtomicInt(self.buffer, 4)
                allocated_atomic.fetch_add(1)
                return free_index

    def deallocate(self, index: int) -> bool:
        """
        Deallocate an object back to the pool.

        Args:
            index: Index of object to deallocate

        Returns:
            True if deallocation succeeded
        """
        if index is None or index < 0 or index >= self.capacity:
            return False

        # Add the node back to the free list
        free_head_atomic = AtomicInt(self.buffer, 0)

        while True:
            current_head = free_head_atomic.load()

            # Set this node's next to point to current head
            self._write_node_next(index, current_head)

            # Try to make this node the new head
            if free_head_atomic.compare_exchange_weak(current_head, index):
                # Successfully deallocated, decrement allocated count
                allocated_atomic = AtomicInt(self.buffer, 4)
                current_allocated = allocated_atomic.load()
                while current_allocated > 0:
                    if allocated_atomic.compare_exchange_weak(current_allocated, current_allocated - 1):
                        break
                return True

    def get(self, index: int) -> Optional[T]:
        """
        Get data from allocated object.

        Args:
            index: Index of object

        Returns:
            Object data, or None if invalid index
        """
        if index >= self.capacity:
            return None

        return self._read_node_data(index)

    def set(self, index: int, data: T) -> bool:
        """
        Set data for allocated object.

        Args:
            index: Index of object
            data: Data to store

        Returns:
            True if successful
        """
        if index >= self.capacity:
            return False

        self._write_node_data(index, data)
        return True

    def allocated_count(self) -> int:
        """Get number of currently allocated objects."""
        return struct.unpack_from('<I', self.buffer, 4)[0]

    def available_count(self) -> int:
        """Get number of available (free) objects."""
        return self.capacity - self.allocated_count()

    def available(self) -> int:
        """Get number of available (free) objects (alias for available_count)."""
        return self.available_count()

    def free(self, index: int) -> bool:
        """Free/deallocate an object (alias for deallocate)."""
        return self.deallocate(index)

    def full(self) -> bool:
        """Check if pool is full."""
        return self.allocated_count() >= self.capacity

    def empty(self) -> bool:
        """Check if pool has no allocated objects."""
        return self.allocated_count() == 0

    def get_block_buffer(self, index: int) -> Optional[memoryview]:
        """
        Get a buffer for direct access to a block.

        Args:
            index: Block index

        Returns:
            Memory view for the block, or None if invalid index
        """
        if index >= self.capacity:
            return None

        offset = self._get_node_offset(index)
        return self.buffer[offset:offset + self.elem_size]

    def get_stats(self) -> dict:
        """
        Get pool statistics.

        Returns:
            Dictionary with pool statistics
        """
        return {
            'total_blocks': self.capacity,
            'available_blocks': self.available(),
            'allocated_blocks': self.allocated_count(),
            'block_size': self.block_size
        }

    def reset(self):
        """
        Reset the pool to initial state (all blocks free).

        Warning: This operation is not atomic and should only be used
        when no other processes are accessing the pool.
        """
        # Reset header
        struct.pack_into('<IIII', self.buffer, 0,
                        0,  # free_head starts at index 0
                        0,  # allocated count
                        self.capacity,
                        self.elem_size)

        # Rebuild free list - all nodes are free
        header_size = 16
        for i in range(self.capacity - 1):
            node_offset = header_size + i * self.node_size
            next_index = i + 1
            # Write next index at the end of each node
            struct.pack_into('<I', self.buffer, node_offset + self.elem_size, next_index)

        # Last node points to NULL_INDEX
        last_node_offset = header_size + (self.capacity - 1) * self.node_size
        struct.pack_into('<I', self.buffer, last_node_offset + self.elem_size, self.NULL_INDEX)

    def __len__(self) -> int:
        """Get number of allocated objects."""
        return self.allocated_count()

    def __str__(self) -> str:
        """String representation."""
        return f"Pool(name='{self.name}', allocated={self.allocated_count()}/{self.capacity}, dtype={self.dtype})"

    def __repr__(self) -> str:
        """String representation."""
        return self.__str__()


class PoolAllocator:
    """
    RAII-style allocator for pool objects.

    This provides automatic cleanup of allocated pool objects
    using context manager protocol.
    """

    def __init__(self, pool: Pool):
        """
        Initialize allocator.

        Args:
            pool: Pool to allocate from
        """
        self.pool = pool
        self.allocated_indices: List[int] = []

    def allocate(self) -> Optional[int]:
        """
        Allocate an object and track it for cleanup.

        Returns:
            Index of allocated object, or None if pool is full
        """
        index = self.pool.allocate()
        if index is not None:
            self.allocated_indices.append(index)
        return index

    def deallocate(self, index: int) -> bool:
        """
        Deallocate a tracked object.

        Args:
            index: Index to deallocate

        Returns:
            True if successful
        """
        if index in self.allocated_indices:
            self.allocated_indices.remove(index)
            return self.pool.deallocate(index)
        return False

    def cleanup(self):
        """Deallocate all tracked objects."""
        for index in self.allocated_indices:
            self.pool.deallocate(index)
        self.allocated_indices.clear()

    def __enter__(self):
        """Context manager entry."""
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit with cleanup."""
        self.cleanup()

    def __del__(self):
        """Cleanup on deletion."""
        self.cleanup()