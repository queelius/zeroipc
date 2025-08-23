"""
Fixed-size array in shared memory with duck typing support.
"""

import struct
import numpy as np
from typing import Optional, Union
from .memory import Memory


class Array:
    """
    Fixed-size array in shared memory.
    
    This implementation uses duck typing - users specify the dtype
    and are responsible for type consistency.
    """
    
    HEADER_FORMAT = '<Q'  # uint64_t capacity
    HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
    
    def __init__(self, memory: Memory, name: str, 
                 capacity: Optional[int] = None, 
                 dtype: Optional[Union[np.dtype, str]] = None):
        """
        Create or open an array.
        
        Args:
            memory: Shared memory instance
            name: Name of the array
            capacity: Number of elements (required for new arrays)
            dtype: NumPy dtype (required)
        
        Raises:
            ValueError: If name is too long or required parameters missing
            TypeError: If dtype is not specified
        """
        if len(name) > 31:
            raise ValueError("Name too long (max 31 characters)")
        
        if dtype is None:
            raise TypeError("dtype is required")
        
        self.memory = memory
        self.name = name
        self.dtype = np.dtype(dtype)
        
        entry = memory.table.find(name)
        
        if entry:
            # Open existing array
            self.offset = entry.offset
            
            # Read capacity from header
            capacity_bytes = memory.mmap[self.offset:self.offset + self.HEADER_SIZE]
            stored_capacity = struct.unpack(self.HEADER_FORMAT, capacity_bytes)[0]
            
            # Validate capacity if provided
            if capacity is not None and capacity != stored_capacity:
                raise ValueError(
                    f"Capacity mismatch: array has {stored_capacity} "
                    f"but requested {capacity}"
                )
            
            self.capacity = stored_capacity
            
        else:
            # Create new array
            if capacity is None:
                raise ValueError("Capacity required to create new array")
            
            self.capacity = capacity
            
            # Allocate space
            total_size = self.HEADER_SIZE + capacity * self.dtype.itemsize
            self.offset = memory.table.allocate(total_size)
            
            # Add to table
            if not memory.table.add(name, self.offset, total_size):
                raise RuntimeError("Failed to add array to table (table full?)")
            
            # Write header
            struct.pack_into(
                self.HEADER_FORMAT, memory.mmap, self.offset, capacity
            )
            
            # Zero-initialize data
            data_offset = self.offset + self.HEADER_SIZE
            data_size = capacity * self.dtype.itemsize
            memory.mmap[data_offset:data_offset + data_size] = bytes(data_size)
        
        # Create NumPy array view of the data
        data_offset = self.offset + self.HEADER_SIZE
        self.array = np.frombuffer(
            memory.mmap,
            dtype=self.dtype,
            count=self.capacity,
            offset=data_offset
        )
    
    def __getitem__(self, index):
        """Get item by index"""
        return self.array[index]
    
    def __setitem__(self, index, value):
        """Set item by index"""
        self.array[index] = value
    
    def __len__(self):
        """Get array length"""
        return self.capacity
    
    def __iter__(self):
        """Iterate over array"""
        return iter(self.array)
    
    def fill(self, value):
        """Fill array with value"""
        self.array[:] = value
    
    @property
    def data(self):
        """Get underlying NumPy array"""
        return self.array
    
    def __repr__(self):
        return f"Array(name='{self.name}', capacity={self.capacity}, dtype={self.dtype})"