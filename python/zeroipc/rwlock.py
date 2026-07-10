"""Read-Write Lock for shared memory."""

import struct
import time
import threading
from typing import Optional

from .memory import Memory
from .mutex import Mutex


class RWLock:
    """
    Read-Write Lock for shared memory.

    Allows multiple concurrent readers OR one exclusive writer.
    Optimized for read-heavy workloads where reads vastly outnumber writes.

    Features:
    - Multiple readers can hold lock simultaneously
    - Writer gets exclusive access (no readers, no other writers)
    - RAII-compatible with context managers

    Performance characteristics:
    - reader_lock(): Fast when no writers (just atomic increment)
    - writer_lock(): Waits for all readers to finish

    Binary layout:
    - 4 bytes: active readers count
    - 4 bytes: writer active flag
    - Mutex for reader coordination
    - Mutex for writer exclusion

    Example:
        >>> mem = Memory("/data", 1024 * 1024)
        >>> rwlock = RWLock(mem, "data_lock")
        >>> data = Array(mem, "data", dtype=np.int32, capacity=1000)
        >>>
        >>> # Many readers (concurrent)
        >>> rwlock.reader_lock()
        >>> value = data[0]
        >>> rwlock.reader_unlock()
        >>>
        >>> # Single writer (exclusive)
        >>> rwlock.writer_lock()
        >>> data[0] = 42
        >>> rwlock.writer_unlock()
        >>>
        >>> # RAII style
        >>> with rwlock.reader():  # For reading
        >>>     value = data[0]
        >>>
        >>> with rwlock.writer():  # For writing
        >>>     data[0] = 99
    """

    STATE_FORMAT = 'ii'  # readers, writer_active
    STATE_SIZE = struct.calcsize(STATE_FORMAT)

    def __init__(self, memory: Memory, name: str, create_if_missing: bool = True):
        """
        Create or open a RWLock.

        Args:
            memory: Memory instance
            name: RWLock identifier
            create_if_missing: If True, creates new lock; if False, opens existing

        Raises:
            RuntimeError: If lock not found when create_if_missing=False
        """
        self.memory = memory
        self.name = name

        state_name = name + "_state"
        reader_mtx_name = name + "_rmtx"
        writer_mtx_name = name + "_wmtx"

        # Try to find existing RWLock
        entry = memory.table.find(name)

        if entry is None:
            if not create_if_missing:
                raise RuntimeError(f"RWLock '{name}' not found")

            # Create new RWLock state
            self.state_offset = memory.allocate(state_name, self.STATE_SIZE)

            # Initialize state (readers=0, writer_active=0)
            state_data = struct.pack(self.STATE_FORMAT, 0, 0)
            memory.data[self.state_offset:self.state_offset + self.STATE_SIZE] = state_data

            # Create mutexes
            self._reader_mutex = Mutex(memory, reader_mtx_name, create_if_missing=True)
            self._writer_mutex = Mutex(memory, writer_mtx_name, create_if_missing=True)

            # Add marker entry
            memory.allocate(name, 1)

        else:
            # Open existing RWLock
            state_entry = memory.table.find(state_name)
            if not state_entry:
                raise RuntimeError(f"RWLock state '{state_name}' not found")

            self.state_offset = state_entry.offset

            if state_entry.size != self.STATE_SIZE:
                raise RuntimeError(f"Invalid RWLock state size: {state_entry.size}")

            # Open existing mutexes
            self._reader_mutex = Mutex(memory, reader_mtx_name, create_if_missing=False)
            self._writer_mutex = Mutex(memory, writer_mtx_name, create_if_missing=False)

        # Offsets for atomic operations
        self._readers_offset = self.state_offset
        self._writer_active_offset = self.state_offset + 4

        # Lock for atomic operations
        self._lock = threading.Lock()

    def _load_readers(self) -> int:
        """Atomically load readers count."""
        return struct.unpack_from('<i', self.memory.data, self._readers_offset)[0]

    def _store_readers(self, value: int):
        """Atomically store readers count."""
        struct.pack_into('<i', self.memory.data, self._readers_offset, value)

    def _load_writer_active(self) -> int:
        """Atomically load writer active flag."""
        return struct.unpack_from('<i', self.memory.data, self._writer_active_offset)[0]

    def _store_writer_active(self, value: int):
        """Atomically store writer active flag."""
        struct.pack_into('<i', self.memory.data, self._writer_active_offset, value)

    def _increment_readers(self):
        """Atomically increment readers count."""
        with self._lock:
            current = self._load_readers()
            self._store_readers(current + 1)

    def _decrement_readers(self):
        """Atomically decrement readers count."""
        with self._lock:
            current = self._load_readers()
            self._store_readers(current - 1)

    def reader_lock(self):
        """
        Acquire read lock (shared access).

        Multiple threads can hold read locks simultaneously.
        Blocks if a writer currently holds the lock.
        """
        # Acquire reader mutex
        self._reader_mutex.lock()

        # Wait for any active writer to finish
        while self._load_writer_active() != 0:
            self._reader_mutex.unlock()
            time.sleep(0.0001)  # Yield to other threads
            self._reader_mutex.lock()

        # Increment readers count
        self._increment_readers()

        # Release reader mutex
        self._reader_mutex.unlock()

    def reader_unlock(self):
        """
        Release read lock.

        Must be called by the same thread that acquired the read lock.
        """
        # Acquire reader mutex
        self._reader_mutex.lock()

        # Decrement readers count
        self._decrement_readers()

        # Release reader mutex
        self._reader_mutex.unlock()

    def writer_lock(self, timeout: Optional[float] = None) -> bool:
        """
        Acquire write lock (exclusive access).

        Only one thread can hold the write lock at a time.
        Blocks until all readers have released their locks.

        Args:
            timeout: Maximum time to wait in seconds (None for infinite)

        Returns:
            True if lock acquired, False if timeout
        """
        # Acquire writer mutex (blocks other writers)
        if not self._writer_mutex.lock(timeout=timeout):
            return False

        # Mark writer as active UNDER the reader mutex. Readers check
        # writer_active and increment readers while holding the reader
        # mutex; setting the flag under the same mutex closes the race
        # where a reader passes its writer check while the writer
        # simultaneously passes its readers check, letting a reader and
        # a writer hold the lock at once.
        if not self._reader_mutex.lock(timeout=timeout):
            self._writer_mutex.unlock()
            return False
        self._store_writer_active(1)
        self._reader_mutex.unlock()

        # Wait for all readers to finish
        start_time = time.time() if timeout is not None else None
        backoff = 0.0001  # 0.1ms
        max_backoff = 0.001  # 1ms

        while self._load_readers() > 0:
            # Check timeout
            if timeout is not None:
                elapsed = time.time() - start_time
                if elapsed >= timeout:
                    # Timeout - release writer mutex and fail
                    self._store_writer_active(0)
                    self._writer_mutex.unlock()
                    return False

            # Spin-wait with backoff
            time.sleep(backoff)
            if backoff < max_backoff:
                backoff *= 2

        return True

    def writer_unlock(self):
        """
        Release write lock.

        Must be called by the same thread that acquired the write lock.
        """
        # Mark writer as inactive
        self._store_writer_active(0)

        # Release writer mutex
        self._writer_mutex.unlock()

    @property
    def readers(self) -> int:
        """Get current number of active readers."""
        return self._load_readers()

    @property
    def writer_active(self) -> bool:
        """Check if a writer is currently active."""
        return self._load_writer_active() == 1

    def reader(self):
        """
        Context manager for read lock.

        Example:
            >>> with rwlock.reader():
            >>>     value = shared_data[0]
        """
        return _ReaderLock(self)

    def writer(self):
        """
        Context manager for write lock.

        Example:
            >>> with rwlock.writer():
            >>>     shared_data[0] = 42
        """
        return _WriterLock(self)

    def __repr__(self):
        readers = self.readers
        writer = "yes" if self.writer_active else "no"
        return f"RWLock(name='{self.name}', readers={readers}, writer_active={writer})"


class _ReaderLock:
    """Context manager for reader lock."""

    def __init__(self, rwlock: RWLock):
        self.rwlock = rwlock

    def __enter__(self):
        self.rwlock.reader_lock()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.rwlock.reader_unlock()
        return False


class _WriterLock:
    """Context manager for writer lock."""

    def __init__(self, rwlock: RWLock):
        self.rwlock = rwlock

    def __enter__(self):
        self.rwlock.writer_lock()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.rwlock.writer_unlock()
        return False
