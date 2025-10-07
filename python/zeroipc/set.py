"""
Lock-free hash set implementation for shared memory.

This module provides a hash set data structure that works in shared memory
based on the Map implementation with dummy values.
"""

from typing import Optional, TypeVar, Union, Iterator
import numpy as np

from .memory import Memory
from .map import Map

T = TypeVar('T')


class Set:
    """
    Lock-free hash set in shared memory.

    This implementation uses the Map data structure internally with
    dummy values, providing a set interface for storing unique elements.
    """

    def __init__(self, memory: Memory, name: str,
                 capacity: Optional[int] = None,
                 dtype: Optional[Union[np.dtype, str, type]] = None):
        """
        Create or open a hash set.

        Args:
            memory: Shared memory instance
            name: Set name
            capacity: Number of slots (required for creation)
            dtype: Element data type

        Raises:
            ValueError: If required parameters are missing
            TypeError: If data type is not specified
            RuntimeError: If set is not found or type mismatch
        """
        if dtype is None:
            raise TypeError("dtype is required")

        self.memory = memory
        self.name = name
        self.dtype = np.dtype(dtype)

        # Use Map with uint8 dummy values (we only care about keys)
        self._map = Map[T, int](memory, name, capacity, dtype, np.uint8)

    def insert(self, element: T) -> bool:
        """
        Insert an element into the set.

        Args:
            element: Element to insert

        Returns:
            True if insertion succeeded, False if set is full
        """
        return self._map.insert(element, 1)  # Use dummy value 1

    def add(self, element: T) -> bool:
        """
        Add an element to the set (alias for insert).

        Args:
            element: Element to add

        Returns:
            True if insertion succeeded, False if set is full
        """
        return self.insert(element)

    def contains(self, element: T) -> bool:
        """
        Check if element exists in set.

        Args:
            element: Element to check

        Returns:
            True if element exists
        """
        return self._map.find(element) is not None

    def erase(self, element: T) -> bool:
        """
        Remove an element from the set.

        Args:
            element: Element to remove

        Returns:
            True if element was found and removed
        """
        return self._map.erase(element)

    def remove(self, element: T) -> bool:
        """
        Remove an element from the set (alias for erase).

        Args:
            element: Element to remove

        Returns:
            True if element was found and removed
        """
        return self.erase(element)

    def discard(self, element: T) -> None:
        """
        Remove an element from the set if present (no error if not found).

        Args:
            element: Element to remove
        """
        self.erase(element)

    def size(self) -> int:
        """Get current number of elements."""
        return self._map.size()

    def empty(self) -> bool:
        """Check if set is empty."""
        return self._map.empty()

    def clear(self) -> None:
        """
        Clear all elements from the set.

        Note: This operation is not atomic and may not be suitable
        for concurrent use.
        """
        # This is not efficient but provides the functionality
        # In practice, clearing would require recreating the structure
        raise NotImplementedError("Clear operation not supported in lock-free implementation")

    def __len__(self) -> int:
        """Get number of elements (len() support)."""
        return self.size()

    def __contains__(self, element: T) -> bool:
        """Check if element is in set (in operator support)."""
        return self.contains(element)

    def __bool__(self) -> bool:
        """Check if set is non-empty (bool() support)."""
        return not self.empty()

    def __str__(self) -> str:
        """String representation."""
        return f"Set(name='{self.name}', size={self.size()}, dtype={self.dtype})"

    def __repr__(self) -> str:
        """String representation."""
        return self.__str__()


# Type alias for better ergonomics
HashSet = Set