"""
Helper functions for working with POSIX shared memory in Python.
"""

from typing import Optional, Any, ContextManager
from contextlib import contextmanager
import os
import signal
import atexit

# Import from C++ extension
from posix_shm_py import SharedMemory


def create_shared_memory(name: str, size: int) -> SharedMemory:
    """
    Create a new shared memory segment.
    
    Args:
        name: Name of the shared memory segment (e.g., "/my_shm")
        size: Size in bytes
        
    Returns:
        SharedMemory object
        
    Raises:
        RuntimeError: If creation fails
    """
    if not name.startswith("/"):
        name = "/" + name
    
    return SharedMemory(name, size)


def open_shared_memory(name: str) -> SharedMemory:
    """
    Open an existing shared memory segment.
    
    Args:
        name: Name of the shared memory segment
        
    Returns:
        SharedMemory object
        
    Raises:
        RuntimeError: If segment doesn't exist
    """
    if not name.startswith("/"):
        name = "/" + name
        
    return SharedMemory(name, 0)  # Size 0 means attach-only


def cleanup_shared_memory(name: str) -> None:
    """
    Unlink a shared memory segment.
    
    Args:
        name: Name of the shared memory segment
    """
    if not name.startswith("/"):
        name = "/" + name
        
    try:
        # Try using the SharedMemory unlink if available
        shm = SharedMemory(name, 0)
        shm.unlink()
    except:
        # Fall back to OS-level unlink
        import posix_ipc
        try:
            posix_ipc.unlink_shared_memory(name)
        except:
            pass  # Already unlinked or doesn't exist


@contextmanager
def shared_memory_context(name: str, size: Optional[int] = None, 
                         cleanup: bool = True) -> ContextManager[SharedMemory]:
    """
    Context manager for shared memory with automatic cleanup.
    
    Args:
        name: Name of the shared memory segment
        size: Size in bytes (None to open existing)
        cleanup: Whether to unlink on exit
        
    Yields:
        SharedMemory object
        
    Example:
        with shared_memory_context("/test", 1024*1024) as shm:
            queue = Queue(shm, "my_queue", max_size=100)
            queue.push(42)
    """
    if not name.startswith("/"):
        name = "/" + name
        
    # Create or open
    if size is not None:
        shm = SharedMemory(name, size)
    else:
        shm = SharedMemory(name, 0)
    
    try:
        yield shm
    finally:
        if cleanup:
            try:
                shm.unlink()
            except:
                pass


class SharedMemoryPool:
    """
    Pool of shared memory segments for managing multiple data structures.
    """
    
    def __init__(self, base_name: str, segment_size: int = 10 * 1024 * 1024):
        """
        Initialize a shared memory pool.
        
        Args:
            base_name: Base name for segments
            segment_size: Size of each segment in bytes
        """
        self.base_name = base_name if base_name.startswith("/") else "/" + base_name
        self.segment_size = segment_size
        self.segments = {}
        self.current_segment = 0
        
        # Register cleanup on exit
        atexit.register(self.cleanup_all)
        
    def get_segment(self, hint: Optional[str] = None) -> SharedMemory:
        """
        Get a shared memory segment, creating if necessary.
        
        Args:
            hint: Optional hint for segment selection
            
        Returns:
            SharedMemory object
        """
        if hint and hint in self.segments:
            return self.segments[hint]
            
        # Create new segment
        segment_name = f"{self.base_name}_{self.current_segment}"
        shm = SharedMemory(segment_name, self.segment_size)
        
        self.segments[hint or str(self.current_segment)] = shm
        self.current_segment += 1
        
        return shm
    
    def cleanup_all(self):
        """Clean up all segments in the pool."""
        for shm in self.segments.values():
            try:
                shm.unlink()
            except:
                pass
        self.segments.clear()


def handle_signals(cleanup_func: callable) -> None:
    """
    Register signal handlers for graceful cleanup.
    
    Args:
        cleanup_func: Function to call on signal
    """
    def signal_handler(signum, frame):
        cleanup_func()
        exit(0)
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)