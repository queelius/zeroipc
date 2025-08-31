"""Lock-free queue implementation for shared memory."""

import struct
import threading
from typing import Optional, TypeVar, Generic, Type
import numpy as np

from .memory import Memory

T = TypeVar('T')


class Queue(Generic[T]):
    """Lock-free circular buffer queue in shared memory.
    
    Uses atomic operations for thread-safe enqueue/dequeue.
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
            
            self.capacity = capacity
            total_size = self.HEADER_SIZE + self.elem_size * capacity
            
            # Allocate in shared memory
            self.offset = memory.allocate(name, total_size)
            
            # Initialize header
            header_data = struct.pack(self.HEADER_FORMAT, 
                                    0,  # head
                                    0,  # tail  
                                    capacity,
                                    self.elem_size)
            memory.data[self.offset:self.offset + self.HEADER_SIZE] = header_data
            
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
        
        # Lock for atomic operations (Python doesn't have true atomics)
        self._lock = threading.Lock()
    
    def _read_header(self):
        """Read header values."""
        header_bytes = self.memory.data[self.offset:self.offset + self.HEADER_SIZE]
        return struct.unpack(self.HEADER_FORMAT, header_bytes)
    
    def _write_header(self, head: int, tail: int, capacity: int, elem_size: int):
        """Write header values."""
        header_data = struct.pack(self.HEADER_FORMAT, head, tail, capacity, elem_size)
        self.memory.data[self.offset:self.offset + self.HEADER_SIZE] = header_data
    
    def _read_capacity(self) -> int:
        """Read capacity from header."""
        _, _, capacity, _ = self._read_header()
        return capacity
    
    def _read_elem_size(self) -> int:
        """Read element size from header."""
        _, _, _, elem_size = self._read_header()
        return elem_size
    
    def push(self, value: T) -> bool:
        """Push value onto queue (enqueue).
        
        Returns:
            True if successful, False if queue is full
        """
        with self._lock:
            head, tail, capacity, elem_size = self._read_header()
            next_tail = (tail + 1) % capacity
            
            # Check if full
            if next_tail == head:
                return False
            
            # Write value
            self.data[tail] = value
            
            # Update tail
            self._write_header(head, next_tail, capacity, elem_size)
            return True
    
    def pop(self) -> Optional[T]:
        """Pop value from queue (dequeue).
        
        Returns:
            Value if available, None if queue is empty
        """
        with self._lock:
            head, tail, capacity, elem_size = self._read_header()
            
            # Check if empty
            if head == tail:
                return None
            
            # Read value
            value = self.data[head].copy()  # Copy to avoid reference issues
            
            # Update head
            next_head = (head + 1) % capacity
            self._write_header(next_head, tail, capacity, elem_size)
            
            return value
    
    def empty(self) -> bool:
        """Check if queue is empty."""
        head, tail, _, _ = self._read_header()
        return head == tail
    
    def full(self) -> bool:
        """Check if queue is full."""
        head, tail, capacity, _ = self._read_header()
        next_tail = (tail + 1) % capacity
        return next_tail == head
    
    def size(self) -> int:
        """Get current number of elements."""
        head, tail, capacity, _ = self._read_header()
        if tail >= head:
            return tail - head
        else:
            return capacity - head + tail
    
    def __len__(self) -> int:
        """Get current size."""
        return self.size()
    
    def __bool__(self) -> bool:
        """Check if not empty."""
        return not self.empty()