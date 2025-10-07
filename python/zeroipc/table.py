"""
Runtime-configurable table for managing named structures in shared memory.
"""

import struct
from typing import Optional, NamedTuple

# Constants matching C++ implementation
TABLE_MAGIC = 0x5A49504D  # 'ZIPM'
TABLE_VERSION = 1


class TableEntry(NamedTuple):
    """Entry in the table"""
    name: str
    offset: int
    size: int


class Table:
    """
    Table for tracking named structures in shared memory.
    
    The table is stored at the beginning of shared memory and tracks
    all allocated structures by name, offset, and size.
    """
    
    HEADER_FORMAT = '<IIIIQQ'  # magic, version, entry_count, reserved, memory_size, next_offset
    HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
    ENTRY_FORMAT = '<32sQQ'  # name[32], offset, size (64-bit)
    ENTRY_SIZE = struct.calcsize(ENTRY_FORMAT)
    
    def __init__(self, buffer: memoryview, max_entries: int, create: bool = False, memory_size: int = 0):
        """
        Initialize a table in existing memory.

        Args:
            buffer: Memory buffer (memoryview)
            max_entries: Maximum number of entries this table can hold
            create: If True, initialize new table; if False, open existing
            memory_size: Total size of the shared memory segment (required for creation)
        """
        self.buffer = buffer
        self.max_entries = max_entries
        self.memory_size = memory_size

        if create:
            if memory_size <= 0:
                raise ValueError("memory_size required when creating table")
            self._initialize()
        else:
            self._validate()
    
    def _initialize(self):
        """Initialize a new table"""
        # Write header
        next_offset = self.calculate_size(self.max_entries)
        struct.pack_into(
            self.HEADER_FORMAT, self.buffer, 0,
            TABLE_MAGIC, TABLE_VERSION, 0, 0, self.memory_size, next_offset
        )
        
        # Zero out entry area
        entry_start = self.HEADER_SIZE
        entry_area_size = self.max_entries * self.ENTRY_SIZE
        if entry_start + entry_area_size <= len(self.buffer):
            for i in range(entry_area_size):
                self.buffer[entry_start + i] = 0
        else:
            raise RuntimeError(f"Buffer too small: need {entry_start + entry_area_size}, have {len(self.buffer)}")
    
    def _validate(self):
        """Validate an existing table"""
        magic, version, entry_count, reserved, memory_size, next_offset = struct.unpack_from(
            self.HEADER_FORMAT, self.buffer, 0
        )

        if magic != TABLE_MAGIC:
            raise ValueError(f"Invalid table magic: {magic:#x}")

        if version != TABLE_VERSION:
            raise ValueError(f"Incompatible table version: {version}")

        if entry_count > self.max_entries:
            raise ValueError(f"Table corruption: entry count {entry_count} > max {self.max_entries}")

        # Use the stored memory size when opening existing table
        self.memory_size = memory_size
    
    def find(self, name: str) -> Optional[TableEntry]:
        """
        Find an entry by name.
        
        Args:
            name: Name to search for
            
        Returns:
            TableEntry if found, None otherwise
        """
        if len(name) > 31:
            raise ValueError("Name too long (max 31 characters)")
        
        entry_count = self.entry_count()
        entry_offset = self.HEADER_SIZE
        
        for i in range(entry_count):
            name_bytes, offset, size = struct.unpack_from(
                self.ENTRY_FORMAT, self.buffer, entry_offset
            )
            entry_name = name_bytes.rstrip(b'\x00').decode('utf-8')
            
            if entry_name == name:
                return TableEntry(entry_name, offset, size)
            
            entry_offset += self.ENTRY_SIZE
        
        return None
    
    def add(self, name: str, offset: int, size: int) -> bool:
        """
        Add a new entry to the table.
        
        Args:
            name: Entry name
            offset: Offset in shared memory
            size: Size in bytes
            
        Returns:
            True if successful, False if table is full
        """
        if len(name) > 31:
            raise ValueError("Name too long (max 31 characters)")
        
        current_count = self.entry_count()
        if current_count >= self.max_entries:
            return False
        
        if self.find(name) is not None:
            raise ValueError(f"Name already exists: {name}")
        
        # Add new entry
        entry_offset = self.HEADER_SIZE + current_count * self.ENTRY_SIZE
        name_bytes = name.encode('utf-8').ljust(32, b'\x00')
        struct.pack_into(
            self.ENTRY_FORMAT, self.buffer, entry_offset,
            name_bytes, offset, size
        )
        
        # Update entry count
        self._set_entry_count(current_count + 1)
        
        return True
    
    def allocate(self, size: int, alignment: int = 8) -> int:
        """
        Allocate space for a new structure.
        
        Args:
            size: Size in bytes to allocate
            alignment: Alignment requirement (default 8)
            
        Returns:
            Offset of allocated space
        """
        next_offset = self.next_offset()

        # Align the offset
        aligned = (next_offset + alignment - 1) & ~(alignment - 1)

        # Check for overflow
        if aligned + size < aligned:
            raise RuntimeError("Allocation size overflow")

        # Check against total memory size
        if aligned + size > self.memory_size:
            raise RuntimeError("Allocation would exceed memory bounds")

        # Update next offset
        self._set_next_offset(aligned + size)

        return aligned
    
    def entry_count(self) -> int:
        """Get the number of entries in the table"""
        return struct.unpack_from('<I', self.buffer, 8)[0]

    def next_offset(self) -> int:
        """Get the next allocation offset"""
        return struct.unpack_from('<Q', self.buffer, 24)[0]

    def _set_entry_count(self, count: int):
        """Set the entry count"""
        struct.pack_into('<I', self.buffer, 8, count)

    def _set_next_offset(self, offset: int):
        """Set the next allocation offset"""
        struct.pack_into('<Q', self.buffer, 24, offset)
    
    def set_next_offset(self, offset: int):
        """Public method to set the next allocation offset"""
        self._set_next_offset(offset)
    
    @staticmethod
    def calculate_size(max_entries: int) -> int:
        """Calculate the total size of the table in bytes"""
        return Table.HEADER_SIZE + max_entries * Table.ENTRY_SIZE