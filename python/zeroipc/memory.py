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
    
    def __init__(self, name: str, size: int = 0, max_entries: int = 64):
        """
        Create or open shared memory.
        
        Args:
            name: Shared memory name (e.g., "/myshm")
            size: Size in bytes (0 to open existing)
            max_entries: Maximum number of table entries (default 64)
        """
        self.name = name
        self.size = size
        self.max_entries = max_entries
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
        shm_path = f"/dev/shm{self.name}"
        if os.path.exists(shm_path):
            os.unlink(shm_path)
    
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