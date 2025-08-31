"""Lock-free stack implementation for shared memory."""

import struct
import threading
from typing import Optional, TypeVar, Generic, Type
import numpy as np

from .memory import Memory

T = TypeVar('T')


class Stack(Generic[T]):
    """Lock-free stack in shared memory.
    
    Uses atomic operations for thread-safe push/pop.
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
            total_size = self.HEADER_SIZE + self.elem_size * capacity
            
            # Allocate in shared memory
            self.offset = memory.allocate(name, total_size)
            
            # Initialize header (top=-1 means empty)
            header_data = struct.pack(self.HEADER_FORMAT,
                                    -1,  # top (empty)
                                    capacity,
                                    self.elem_size)
            memory.data[self.offset:self.offset + self.HEADER_SIZE] = header_data
            
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
    
    def push(self, value: T) -> bool:
        """Push value onto stack.
        
        Returns:
            True if successful, False if stack is full
        """
        with self._lock:
            top, capacity, elem_size = self._read_header()
            
            # Check if full
            if top >= capacity - 1:
                return False
            
            # Increment top and write value
            new_top = top + 1
            self.data[new_top] = value
            
            # Update header
            self._write_header(new_top, capacity, elem_size)
            return True
    
    def pop(self) -> Optional[T]:
        """Pop value from stack.
        
        Returns:
            Value if available, None if stack is empty
        """
        with self._lock:
            top, capacity, elem_size = self._read_header()
            
            # Check if empty
            if top < 0:
                return None
            
            # Read value
            value = self.data[top].copy()  # Copy to avoid reference issues
            
            # Update top
            self._write_header(top - 1, capacity, elem_size)
            
            return value
    
    def top(self) -> Optional[T]:
        """Peek at top value without removing it.
        
        Returns:
            Value if available, None if stack is empty
        """
        top_idx, _, _ = self._read_header()
        
        if top_idx < 0:
            return None
        
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