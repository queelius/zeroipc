"""
NumPy integration utilities for posix_shm arrays.
"""

from typing import Optional, Any, TYPE_CHECKING
import warnings

if TYPE_CHECKING:
    import numpy as np
    from posix_shm_py import Array

try:
    import numpy as np
    HAS_NUMPY = True
except ImportError:
    HAS_NUMPY = False
    

def array_to_numpy(arr: "Array", copy: bool = False) -> "np.ndarray":
    """
    Convert a shared memory array to a NumPy array.
    
    Args:
        arr: posix_shm Array object
        copy: If True, return a copy instead of a view
        
    Returns:
        NumPy array view or copy of the data
        
    Raises:
        ImportError: If NumPy is not installed
        
    Example:
        >>> shm = SharedMemory("/test", 1024*1024)
        >>> arr = Array(shm, "data", size=1000, dtype=float)
        >>> np_arr = array_to_numpy(arr)
        >>> np_arr[0] = 42.0  # Modifies shared memory directly
    """
    if not HAS_NUMPY:
        raise ImportError("NumPy is required for this function. Install with: pip install numpy")
    
    # Get the raw memory pointer and size from the C++ extension
    # This would need to be exposed in the C++ bindings
    ptr = arr._get_data_ptr()
    size = arr.size()
    dtype = arr._get_dtype()
    
    # Create NumPy array from memory buffer
    np_dtype = _get_numpy_dtype(dtype)
    np_arr = np.frombuffer(ptr, dtype=np_dtype, count=size)
    
    if copy:
        return np_arr.copy()
    else:
        # Return view - changes will affect shared memory
        np_arr.flags.writeable = True
        return np_arr


def numpy_to_array(np_arr: "np.ndarray", shm: "SharedMemory", 
                   name: str) -> "Array":
    """
    Create a shared memory array from a NumPy array.
    
    Args:
        np_arr: Source NumPy array
        shm: SharedMemory object
        name: Name for the array in shared memory
        
    Returns:
        posix_shm Array object
        
    Raises:
        ImportError: If NumPy is not installed
        ValueError: If array dtype is not supported
        
    Example:
        >>> data = np.arange(1000, dtype=np.float64)
        >>> shm = SharedMemory("/test", 10*1024*1024)
        >>> arr = numpy_to_array(data, shm, "numpy_data")
    """
    if not HAS_NUMPY:
        raise ImportError("NumPy is required for this function. Install with: pip install numpy")
    
    from posix_shm_py import Array
    
    # Ensure array is C-contiguous
    if not np_arr.flags['C_CONTIGUOUS']:
        np_arr = np.ascontiguousarray(np_arr)
    
    # Map NumPy dtype to our array type
    dtype = _map_numpy_dtype(np_arr.dtype)
    
    # Create array in shared memory
    arr = Array(shm, name, size=np_arr.size, dtype=dtype)
    
    # Copy data
    for i, val in enumerate(np_arr.flat):
        arr[i] = val
        
    return arr


def create_numpy_backed_array(shm: "SharedMemory", name: str, 
                             shape: tuple, dtype: "np.dtype") -> "np.ndarray":
    """
    Create a NumPy array backed by shared memory.
    
    This is a convenience function that creates both the shared memory
    array and returns a NumPy view of it.
    
    Args:
        shm: SharedMemory object
        name: Name for the array
        shape: Shape of the array
        dtype: NumPy dtype
        
    Returns:
        NumPy array backed by shared memory
        
    Example:
        >>> shm = SharedMemory("/test", 100*1024*1024)
        >>> arr = create_numpy_backed_array(shm, "matrix", (1000, 1000), np.float64)
        >>> arr[0, 0] = 42.0  # Directly modifies shared memory
    """
    if not HAS_NUMPY:
        raise ImportError("NumPy is required for this function. Install with: pip install numpy")
    
    from posix_shm_py import Array
    
    # Calculate total size
    total_size = np.prod(shape)
    
    # Create shared memory array
    mapped_dtype = _map_numpy_dtype(dtype)
    arr = Array(shm, name, size=total_size, dtype=mapped_dtype)
    
    # Get NumPy view and reshape
    np_arr = array_to_numpy(arr, copy=False)
    np_arr = np_arr.reshape(shape)
    
    return np_arr


def _get_numpy_dtype(dtype_str: str) -> "np.dtype":
    """Map internal dtype string to NumPy dtype."""
    dtype_map = {
        'float': np.float32,
        'double': np.float64,
        'int': np.int32,
        'long': np.int64,
        'uint': np.uint32,
        'ulong': np.uint64,
        'int8': np.int8,
        'int16': np.int16,
        'int32': np.int32,
        'int64': np.int64,
        'uint8': np.uint8,
        'uint16': np.uint16,
        'uint32': np.uint32,
        'uint64': np.uint64,
        'float32': np.float32,
        'float64': np.float64,
    }
    
    if dtype_str not in dtype_map:
        raise ValueError(f"Unsupported dtype: {dtype_str}")
        
    return dtype_map[dtype_str]


def _map_numpy_dtype(np_dtype: "np.dtype") -> str:
    """Map NumPy dtype to internal dtype string."""
    dtype_map = {
        np.float32: 'float32',
        np.float64: 'float64',
        np.int8: 'int8',
        np.int16: 'int16',
        np.int32: 'int32',
        np.int64: 'int64',
        np.uint8: 'uint8',
        np.uint16: 'uint16',
        np.uint32: 'uint32',
        np.uint64: 'uint64',
    }
    
    # Handle both dtype objects and type objects
    np_type = np_dtype.type if hasattr(np_dtype, 'type') else np_dtype
    
    for key, value in dtype_map.items():
        if np.issubdtype(np_type, key):
            return value
            
    raise ValueError(f"Unsupported NumPy dtype: {np_dtype}")


# Validation functions
def validate_array_compatibility(arr1: "Array", arr2: "Array") -> bool:
    """Check if two arrays are compatible for operations."""
    return (arr1.size() == arr2.size() and 
            arr1._get_dtype() == arr2._get_dtype())


def validate_numpy_compatibility(np_arr: "np.ndarray") -> bool:
    """Check if a NumPy array can be stored in shared memory."""
    if not np_arr.flags['C_CONTIGUOUS']:
        warnings.warn("Array is not C-contiguous, will be copied")
        
    try:
        _map_numpy_dtype(np_arr.dtype)
        return True
    except ValueError:
        return False