"""
ZeroIPC - Zero-copy Inter-Process Communication via Shared Memory

This is a pure Python implementation of the ZeroIPC specification.
It provides shared memory data structures that can be accessed from
multiple processes and languages.
"""

__version__ = "2.0.0"

# Core structures (always available)
from .table import Table
from .memory import Memory

__all__ = ["Table", "Memory"]

# Optional numpy-dependent modules
try:
    import numpy

    # Basic data structures
    from .array import Array
    from .queue import Queue
    from .stack import Stack

    # Advanced data structures
    from .map import Map
    from .set import Set, HashSet
    from .pool import Pool, PoolAllocator
    from .ring import Ring

    # Codata structures for asynchronous programming
    from .future import Future, Promise, FutureState
    from .lazy import Lazy, LazyState, lazy_constant, lazy_function
    from .stream import Stream, create_number_stream, create_random_stream
    from .channel import Channel, ChannelClosed, Select, make_channel, make_unbuffered_channel, make_buffered_channel

    # Synchronization primitives
    from .semaphore import Semaphore
    from .barrier import Barrier
    from .latch import Latch

    # Atomic operations
    from .atomic import AtomicInt, AtomicInt64, atomic_thread_fence, spin_wait
    from .atomic import MEMORY_ORDER_RELAXED, MEMORY_ORDER_ACQUIRE, MEMORY_ORDER_RELEASE, MEMORY_ORDER_ACQ_REL, MEMORY_ORDER_SEQ_CST

    __all__.extend([
        # Basic data structures
        "Array", "Queue", "Stack",
        # Advanced data structures
        "Map", "Set", "HashSet", "Pool", "PoolAllocator", "Ring",
        # Codata structures
        "Future", "Promise", "FutureState",
        "Lazy", "LazyState", "lazy_constant", "lazy_function",
        "Stream", "create_number_stream", "create_random_stream",
        "Channel", "ChannelClosed", "Select", "make_channel", "make_unbuffered_channel", "make_buffered_channel",
        # Synchronization primitives
        "Semaphore", "Barrier", "Latch",
        # Atomic operations
        "AtomicInt", "AtomicInt64", "atomic_thread_fence", "spin_wait",
        "MEMORY_ORDER_RELAXED", "MEMORY_ORDER_ACQUIRE", "MEMORY_ORDER_RELEASE", "MEMORY_ORDER_ACQ_REL", "MEMORY_ORDER_SEQ_CST"
    ])

except ImportError:
    pass