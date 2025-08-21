"""
posix_shm - High-performance lock-free data structures in POSIX shared memory

This module provides Python bindings for the C++ POSIX shared memory library,
enabling efficient inter-process communication through lock-free data structures.
"""

from typing import TYPE_CHECKING

# Import the C++ extension module
try:
    from posix_shm_py import (
        # Core classes
        SharedMemory,
        
        # Type-specific data structures
        IntQueue,
        FloatQueue,
        IntStack,
        FloatArray,
        IntFloatMap,
        StringFloatMap,
        IntSet,
        Bitset1024,
        AtomicInt,
        
        # Version info
        __version__,
        
        # Helper function
        exists,
    )
    
    # Create aliases for common usage
    Queue = IntQueue  # Default to int queue
    Stack = IntStack  # Default to int stack
    Array = FloatArray  # Default to float array
    HashMap = IntFloatMap  # Default to int->float map
    HashSet = IntSet  # Default to int set
    Bitset = Bitset1024  # Default to 1024-bit bitset
except ImportError as e:
    raise ImportError(
        "Failed to import posix_shm C++ extension. "
        "Please ensure the package is properly installed."
    ) from e

# Python helper modules
from .helpers import (
    create_shared_memory,
    open_shared_memory,
    cleanup_shared_memory,
)

from .numpy_utils import (
    array_to_numpy,
    numpy_to_array,
)

__all__ = [
    # Core
    "SharedMemory",
    
    # Data structures
    "Queue",
    "Stack", 
    "Array",
    "HashMap",
    "HashSet",
    "Bitset",
    
    # Helpers
    "create_shared_memory",
    "open_shared_memory",
    "cleanup_shared_memory",
    
    # NumPy integration
    "array_to_numpy",
    "numpy_to_array",
    
    # Version
    "__version__",
]

# Type checking support
if TYPE_CHECKING:
    from typing import Any, Optional, Union
    import numpy as np