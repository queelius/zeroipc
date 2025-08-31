"""
POSIX shared memory wrapper with automatic cleanup and table management.
"""

import os
import mmap
import tempfile
from typing import Optional
from .table import Table


class Memory:
    """
    POSIX shared memory wrapper with table management.
    
    This class manages a shared memory segment and its metadata table.
    The table is always placed at the beginning of the shared memory.
    """
    
    def __init__(self, name: str, size: int = 0, max_entries: int = 64, table_size: int = None):
        """
        Create or open shared memory.
        
        Args:
            name: Shared memory name (e.g., "/myshm")
            size: Size in bytes (0 to open existing)
            max_entries: Maximum number of table entries (default 64)
            table_size: Alias for max_entries for compatibility
        """
        self.name = name
        self.size = size
        self.max_entries = table_size if table_size is not None else max_entries
        self.fd = None
        self.mmap = None
        self.table = None
        self.owner = size > 0
        
        if size > 0:
            self._create()
        else:
            self._open()
        
        # Initialize table
        self.table = Table(self.mmap, max_entries, self.owner)
    
    def _create(self):
        """Create new shared memory"""
        # On Linux, shared memory is in /dev/shm
        shm_path = f"/dev/shm{self.name}"
        
        # Remove if exists
        if os.path.exists(shm_path):
            os.unlink(shm_path)
        
        # Create and size the file
        self.fd = os.open(shm_path, os.O_CREAT | os.O_RDWR | os.O_EXCL, 0o666)
        os.ftruncate(self.fd, self.size)
        
        # Map the memory
        self.mmap = mmap.mmap(self.fd, self.size)
        
        # Zero out the memory
        self.mmap[:] = bytes(self.size)
    
    def _open(self):
        """Open existing shared memory"""
        shm_path = f"/dev/shm{self.name}"
        
        if not os.path.exists(shm_path):
            raise FileNotFoundError(f"Shared memory {self.name} not found")
        
        # Open the file
        self.fd = os.open(shm_path, os.O_RDWR)
        
        # Get size
        stat = os.fstat(self.fd)
        self.size = stat.st_size
        
        # Map the memory
        self.mmap = mmap.mmap(self.fd, self.size)
    
    def unlink(self):
        """Unlink (delete) the shared memory"""
        Memory.unlink_static(self.name)
    
    def allocate(self, name: str, size: int) -> int:
        """
        Allocate space for a new structure.
        
        Args:
            name: Structure name
            size: Size in bytes
            
        Returns:
            Offset where structure was allocated
        """
        # Get current next_offset from table
        next_offset = self.table.next_offset()
        
        # Add entry to table
        if not self.table.add(name, next_offset, size):
            raise RuntimeError(f"Failed to add '{name}' to table (table full?)")
        
        # Update next_offset in table
        self.table.set_next_offset(next_offset + size)
        
        return next_offset
    
    def find(self, name: str, offset: Optional[int] = None, size: Optional[int] = None):
        """
        Find structure in table.
        
        Args:
            name: Structure name
            offset: Optional output for offset
            size: Optional output for size
            
        Returns:
            Success indicator
        """
        entry = self.table.find(name)
        if entry:
            if offset is not None:
                return entry.offset
            if size is not None:
                return entry.size
            return True
        return False
    
    def at(self, offset: int) -> memoryview:
        """
        Get memory view at specific offset.
        
        Args:
            offset: Offset from start of shared memory
            
        Returns:
            Memory view starting at offset
        """
        if offset >= self.size:
            raise IndexError(f"Offset {offset} out of bounds (size: {self.size})")
        return memoryview(self.mmap)[offset:]
    
    def base(self):
        """Get base memory buffer."""
        return self.mmap
    
    @property
    def data(self):
        """Get memory as mmap object."""
        return self.mmap
    
    def close(self):
        """Close the shared memory"""
        if self.mmap:
            self.mmap.close()
            self.mmap = None
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None
    
    def __enter__(self):
        """Context manager entry"""
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit"""
        self.close()
    
    def __del__(self):
        """Cleanup on deletion"""
        self.close()
    
    @staticmethod
    def unlink_static(name: str):
        """
        Unlink (delete) shared memory by name.
        
        Args:
            name: Shared memory name
        """
        shm_path = f"/dev/shm{name}"
        if os.path.exists(shm_path):
            os.unlink(shm_path)
    
    @staticmethod
    def unlink(name: str):
        """
        Unlink (delete) shared memory by name.
        
        Args:
            name: Shared memory name
        """
        Memory.unlink_static(name)