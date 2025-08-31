# ZeroIPC Python Implementation

## Overview

Pure Python implementation of the ZeroIPC shared memory protocol. This is not a binding to C++ code - it's a standalone implementation that follows the same binary specification.

## Features

- **Pure Python**: No compilation required
- **NumPy integration**: Efficient array operations
- **Duck typing**: Users specify types at runtime
- **Zero-copy**: Direct memory mapping via mmap
- **Cross-language**: Interoperates with C++ and other implementations

## Installation

```bash
pip install -e .
```

## Running Tests

```bash
python -m pytest tests/
```

## Usage Example

```python
from zeroipc import Memory, Array
import numpy as np

# Open or create shared memory
mem = Memory("/sensor_data", size=10*1024*1024)  # 10MB

# Create or open array with runtime type
data = Array(mem, "temperature", capacity=1000, dtype=np.float32)

# NumPy-like interface
data[0] = 23.5
data[:10] = np.arange(10)

# Direct NumPy array access
print(data.data.mean())
```

## API Reference

### Memory

```python
Memory(name: str, size: int = 0, max_entries: int = 64)
```
- `name`: Shared memory identifier (e.g., "/myshm")
- `size`: Size in bytes (0 to open existing)
- `max_entries`: Maximum table entries

### Array

```python
Array(memory: Memory, name: str, capacity: int = None, dtype = None)
```
- `memory`: Memory instance
- `name`: Array identifier
- `capacity`: Number of elements (None to open existing)
- `dtype`: NumPy dtype or type string

## Type Specification

Since Python uses duck typing, users must specify types when accessing data:

```python
# Integer array
int_array = Array(mem, "counts", dtype=np.int32)

# Float array
float_array = Array(mem, "values", dtype=np.float64)

# Structured array
coord_dtype = np.dtype([('lat', 'f4'), ('lon', 'f4')])
coords = Array(mem, "locations", dtype=coord_dtype)
```

## Requirements

- Python 3.8+
- NumPy
- POSIX shared memory support (Linux/macOS)

## Design Notes

The Python implementation emphasizes:
1. **Simplicity** through duck typing
2. **NumPy compatibility** for scientific computing
3. **Pure Python** for easy distribution
4. **Binary compatibility** with other language implementations