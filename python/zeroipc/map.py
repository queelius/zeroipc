"""
Lock-free hash map implementation for shared memory.

This module provides a hash map data structure that works in shared memory
with lock-free operations using linear probing and atomic state management.
"""

import struct
from typing import Optional, TypeVar, Generic, Any, Union
import numpy as np

from .memory import Memory
from .atomic import AtomicInt

K = TypeVar('K')
V = TypeVar('V')


class Map(Generic[K, V]):
    """
    Lock-free hash map in shared memory using linear probing.

    This implementation uses atomic state management for lock-free operations
    across multiple processes. Keys and values must have consistent binary
    representations across processes.
    """

    # Entry states
    EMPTY = 0
    OCCUPIED = 1
    DELETED = 2
    INSERTING = 3

    # Numpy dtype → struct format char (class-level constant)
    _DTYPE_FORMAT = {
        np.dtype('int8'): 'b', np.dtype('uint8'): 'B',
        np.dtype('int16'): 'h', np.dtype('uint16'): 'H',
        np.dtype('int32'): 'i', np.dtype('uint32'): 'I',
        np.dtype('int64'): 'q', np.dtype('uint64'): 'Q',
        np.dtype('float32'): 'f', np.dtype('float64'): 'd',
    }

    def __init__(self, memory: Memory, name: str,
                 capacity: Optional[int] = None,
                 key_dtype: Optional[Union[np.dtype, str, type]] = None,
                 value_dtype: Optional[Union[np.dtype, str, type]] = None):
        """
        Create or open a hash map.

        Args:
            memory: Shared memory instance
            name: Map name
            capacity: Number of slots (required for creation)
            key_dtype: Key data type
            value_dtype: Value data type

        Raises:
            ValueError: If required parameters are missing
            TypeError: If data types are not specified
            RuntimeError: If map is not found or type mismatch
        """
        if len(name) > 31:
            raise ValueError("Name too long (max 31 characters)")

        if key_dtype is None or value_dtype is None:
            raise TypeError("key_dtype and value_dtype are required")

        self.memory = memory
        self.name = name
        self.key_dtype = np.dtype(key_dtype)
        self.value_dtype = np.dtype(value_dtype)
        self.key_size = self.key_dtype.itemsize
        self.value_size = self.value_dtype.itemsize

        # Calculate entry size: state(4) + key + value (aligned)
        entry_size = 4 + self.key_size + self.value_size
        # Align to 8 bytes
        self.entry_size = (entry_size + 7) & ~7

        # Try to find existing map
        entry = memory.table.find(name)

        if entry is None:
            # Create new map
            if capacity is None:
                raise ValueError("capacity required to create new map")

            if capacity == 0:
                raise ValueError("Map capacity must be greater than 0")

            self.capacity = capacity
            self._create_new()
        else:
            # Open existing map
            self._open_existing(entry)

    def _create_new(self):
        """Create a new map in shared memory."""
        # Header: size(4) + capacity(4) + key_size(4) + value_size(4)
        header_size = 16
        total_size = header_size + self.entry_size * self.capacity

        # Allocate space
        self.offset = self.memory.table.allocate(total_size)

        if not self.memory.table.add(self.name, self.offset, total_size):
            raise RuntimeError("Failed to add map to table")

        # Get memory view
        self.buffer = self.memory.at(self.offset)

        # Initialize header
        struct.pack_into('<IIII', self.buffer, 0,
                        0,  # size
                        self.capacity,
                        self.key_size,
                        self.value_size)

        # Initialize all entries as empty
        for i in range(self.capacity):
            entry_offset = 16 + i * self.entry_size
            struct.pack_into('<I', self.buffer, entry_offset, self.EMPTY)

    def _open_existing(self, entry):
        """Open an existing map from shared memory."""
        self.offset = entry.offset
        self.buffer = self.memory.at(self.offset)

        # Read header
        size, capacity, key_size, value_size = struct.unpack_from('<IIII', self.buffer, 0)

        if key_size != self.key_size or value_size != self.value_size:
            raise RuntimeError(f"Type size mismatch: expected key={self.key_size}, value={self.value_size}, "
                             f"got key={key_size}, value={value_size}")

        self.capacity = capacity

    def _hash_key(self, key: K) -> int:
        """
        Hash a key to an integer.

        Args:
            key: Key to hash

        Returns:
            Hash value as integer
        """
        # Multiplicative hash (matches C++ implementation's approach)
        if isinstance(key, (int, float, np.number)):
            # Normalize to target dtype, then hash the raw bytes
            val = np.array([key], dtype=self.key_dtype).view(np.uint8)
            h = 0
            for b in val:
                h = h * 31 + int(b)
            return (h * 2654435761) & 0xFFFFFFFFFFFFFFFF
        elif isinstance(key, str):
            key_bytes = key.encode('utf-8')
        elif isinstance(key, bytes):
            key_bytes = key
        else:
            key_bytes = np.array([key], dtype=self.key_dtype).tobytes()

        h = 0
        for b in key_bytes:
            h = h * 31 + b
        return (h * 2654435761) & 0xFFFFFFFFFFFFFFFF

    def _keys_equal(self, key1: K, key2: K) -> bool:
        """
        Compare two keys for equality.

        Args:
            key1: First key
            key2: Second key

        Returns:
            True if keys are equal
        """
        # For numeric types, normalize both to the target dtype to handle
        # float32/float64 precision differences
        if isinstance(key1, (int, float, np.number)) and isinstance(key2, (int, float, np.number)):
            # Convert both to target dtype for proper comparison
            norm1 = np.array([key1], dtype=self.key_dtype)[0]
            norm2 = np.array([key2], dtype=self.key_dtype)[0]
            return norm1 == norm2
        elif isinstance(key1, str) and isinstance(key2, str):
            return key1 == key2
        elif isinstance(key1, bytes) and isinstance(key2, bytes):
            return key1 == key2
        else:
            # Convert both to numpy arrays and compare
            arr1 = np.array([key1], dtype=self.key_dtype)
            arr2 = np.array([key2], dtype=self.key_dtype)
            return np.array_equal(arr1, arr2)

    def _get_entry_offset(self, index: int) -> int:
        """Get byte offset of entry at given index."""
        return 16 + index * self.entry_size

    def _get_struct_format(self, dtype):
        """Get proper struct format for numpy dtype."""
        return self._DTYPE_FORMAT.get(dtype, dtype.char)

    def _read_entry_state(self, index: int) -> int:
        """Read entry state at given index."""
        offset = self._get_entry_offset(index)
        return struct.unpack_from('<I', self.buffer, offset)[0]

    def _read_entry_key(self, index: int) -> K:
        """Read entry key at given index."""
        offset = self._get_entry_offset(index) + 4
        if self.key_dtype.kind in 'iuf':  # integer, unsigned, float
            fmt = self._get_struct_format(self.key_dtype)
            return struct.unpack_from(f'<{fmt}', self.buffer, offset)[0]
        else:
            # For complex types, read raw bytes and convert
            key_bytes = self.buffer[offset:offset + self.key_size]
            return np.frombuffer(key_bytes, dtype=self.key_dtype)[0]

    def _read_entry_value(self, index: int) -> V:
        """Read entry value at given index."""
        offset = self._get_entry_offset(index) + 4 + self.key_size
        if self.value_dtype.kind in 'iuf':  # integer, unsigned, float
            fmt = self._get_struct_format(self.value_dtype)
            return struct.unpack_from(f'<{fmt}', self.buffer, offset)[0]
        else:
            # For complex types, read raw bytes and convert
            value_bytes = self.buffer[offset:offset + self.value_size]
            return np.frombuffer(value_bytes, dtype=self.value_dtype)[0]

    def _write_entry_key_value(self, index: int, key: K, value: V):
        """Write key and value at given index without touching state."""
        offset = self._get_entry_offset(index)

        # Write key
        if self.key_dtype.kind in 'iuf':
            fmt = self._get_struct_format(self.key_dtype)
            struct.pack_into(f'<{fmt}', self.buffer, offset + 4, key)
        else:
            key_array = np.array([key], dtype=self.key_dtype)
            self.buffer[offset + 4:offset + 4 + self.key_size] = key_array.tobytes()

        # Write value
        value_offset = offset + 4 + self.key_size
        if self.value_dtype.kind in 'iuf':
            fmt = self._get_struct_format(self.value_dtype)
            struct.pack_into(f'<{fmt}', self.buffer, value_offset, value)
        else:
            value_array = np.array([value], dtype=self.value_dtype)
            self.buffer[value_offset:value_offset + self.value_size] = value_array.tobytes()

    def _store_entry_state(self, index: int, state: int):
        """Atomically store entry state at given index."""
        offset = self._get_entry_offset(index)
        struct.pack_into('<I', self.buffer, offset, state)

    def _cas_entry_state(self, index: int, expected: int, desired: int) -> bool:
        """
        Compare-and-swap entry state.

        Args:
            index: Entry index
            expected: Expected current state
            desired: Desired new state

        Returns:
            True if CAS succeeded
        """
        offset = self._get_entry_offset(index)
        atomic_state = AtomicInt(self.buffer, offset)
        return atomic_state.compare_exchange_weak(expected, desired)

    def put(self, key: K, value: V) -> bool:
        """
        Insert or update a key-value pair.

        Args:
            key: Key to insert
            value: Value to associate with key

        Returns:
            True if insertion succeeded, False if map is full
        """
        return self.insert(key, value)

    def insert(self, key: K, value: V) -> bool:
        """
        Insert or update a key-value pair.

        Uses a 3-state protocol (EMPTY -> INSERTING -> OCCUPIED) to prevent
        concurrent readers from seeing uninitialized key/value data.

        Args:
            key: Key to insert
            value: Value to associate with key

        Returns:
            True if insertion succeeded, False if map is full
        """
        hash_val = self._hash_key(key)

        # Linear probing
        for i in range(self.capacity):
            idx = (hash_val + i) % self.capacity

            # Try to claim an empty slot: EMPTY -> INSERTING
            if self._cas_entry_state(idx, self.EMPTY, self.INSERTING):
                # We exclusively own this slot; write key and value
                self._write_entry_key_value(idx, key, value)
                # Publish the entry: INSERTING -> OCCUPIED
                self._store_entry_state(idx, self.OCCUPIED)
                # Increment size
                size_atomic = AtomicInt(self.buffer, 0)
                size_atomic.fetch_add(1)
                return True

            # Check if it's our key (update case)
            # Skip INSERTING slots (another thread is writing)
            current_state = self._read_entry_state(idx)
            if current_state == self.OCCUPIED:
                existing_key = self._read_entry_key(idx)
                if self._keys_equal(existing_key, key):
                    # CAS OCCUPIED -> INSERTING for exclusive update access
                    if self._cas_entry_state(idx, self.OCCUPIED, self.INSERTING):
                        self._write_entry_key_value(idx, key, value)
                        self._store_entry_state(idx, self.OCCUPIED)
                        return True
                    # CAS failed — slot was erased or another updater won; retry from this slot
                    continue

            # Try deleted slots too: DELETED -> INSERTING
            if self._cas_entry_state(idx, self.DELETED, self.INSERTING):
                self._write_entry_key_value(idx, key, value)
                # Publish the entry: INSERTING -> OCCUPIED
                self._store_entry_state(idx, self.OCCUPIED)
                size_atomic = AtomicInt(self.buffer, 0)
                size_atomic.fetch_add(1)
                return True

        return False  # Map is full

    def get(self, key: K) -> Optional[V]:
        """
        Find value by key.

        Args:
            key: Key to search for

        Returns:
            Value if found, None otherwise
        """
        return self.find(key)

    def find(self, key: K) -> Optional[V]:
        """
        Find value by key.

        Args:
            key: Key to search for

        Returns:
            Value if found, None otherwise
        """
        hash_val = self._hash_key(key)

        # Linear probing
        for i in range(self.capacity):
            idx = (hash_val + i) % self.capacity
            state = self._read_entry_state(idx)

            if state == self.EMPTY:
                return None  # Key not found

            if state == self.OCCUPIED:
                existing_key = self._read_entry_key(idx)
                if self._keys_equal(existing_key, key):
                    return self._read_entry_value(idx)

            # Continue searching through DELETED and INSERTING entries
            # INSERTING slots are being written by another thread; skip them

        return None  # Key not found

    def remove(self, key: K) -> bool:
        """
        Remove a key-value pair.

        Args:
            key: Key to remove

        Returns:
            True if key was found and removed
        """
        return self.erase(key)

    def erase(self, key: K) -> bool:
        """
        Remove a key-value pair.

        Uses CAS from OCCUPIED to DELETED so that only one thread
        can successfully erase a given entry (prevents double-decrement).

        Args:
            key: Key to remove

        Returns:
            True if key was found and removed
        """
        hash_val = self._hash_key(key)

        # Linear probing
        for i in range(self.capacity):
            idx = (hash_val + i) % self.capacity
            state = self._read_entry_state(idx)

            if state == self.EMPTY:
                return False  # Key not found

            if state == self.OCCUPIED:
                existing_key = self._read_entry_key(idx)
                if self._keys_equal(existing_key, key):
                    # CAS from OCCUPIED to DELETED; only the winner decrements size
                    if self._cas_entry_state(idx, self.OCCUPIED, self.DELETED):
                        size_atomic = AtomicInt(self.buffer, 0)
                        size_atomic.fetch_add(-1)
                        return True
                    # CAS failed: another thread already erased this slot
                    # Re-read to see if it's now DELETED
                    new_state = self._read_entry_state(idx)
                    if new_state == self.DELETED:
                        return False
                    # Otherwise continue probing (e.g., INSERTING)

            # Continue searching through DELETED and INSERTING entries

        return False  # Key not found

    def size(self) -> int:
        """Get current number of elements."""
        return struct.unpack_from('<I', self.buffer, 0)[0]

    def empty(self) -> bool:
        """Check if map is empty."""
        return self.size() == 0

    def clear(self):
        """
        Clear all entries from the map.

        Warning: This operation is not atomic and should only be used
        when no other processes are accessing the map.
        """
        # Reset size to 0
        struct.pack_into('<I', self.buffer, 0, 0)

        # Mark all entries as empty
        for i in range(self.capacity):
            entry_offset = self._get_entry_offset(i)
            struct.pack_into('<I', self.buffer, entry_offset, self.EMPTY)

    def contains(self, key: K) -> bool:
        """Check if key exists in map."""
        return self.find(key) is not None

    __contains__ = contains

    def __getitem__(self, key: K) -> V:
        """Get value by key (dict-like interface)."""
        value = self.find(key)
        if value is None:
            raise KeyError(key)
        return value

    def __setitem__(self, key: K, value: V) -> None:
        """Set value by key (dict-like interface)."""
        if not self.insert(key, value):
            raise RuntimeError("Map is full")

    def __delitem__(self, key: K) -> None:
        """Delete key (dict-like interface)."""
        if not self.erase(key):
            raise KeyError(key)