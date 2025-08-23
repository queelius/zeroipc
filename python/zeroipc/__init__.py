"""
ZeroIPC - Zero-copy Inter-Process Communication via Shared Memory

This is a pure Python implementation of the ZeroIPC specification.
It provides shared memory data structures that can be accessed from
multiple processes and languages.
"""

__version__ = "1.0.0"

from .table import Table
from .memory import Memory

__all__ = ["Table", "Memory"]

# Optional numpy-dependent modules
try:
    import numpy
    from .array import Array
    __all__.append("Array")
except ImportError:
    pass