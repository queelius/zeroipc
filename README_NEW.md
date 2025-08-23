# ZeroIPC - Cross-Language Shared Memory IPC

## Overview

ZeroIPC is a high-performance inter-process communication library that enables zero-copy data sharing through POSIX shared memory. It provides true language independence with parallel implementations in C++ and Python.

- **Parallel Implementations**: C++ and Python are equal partners, not bindings
- **Minimal Metadata**: Only store name/offset/size - no type information
- **Duck Typing**: Python users specify types, C++ uses templates
- **Runtime Configuration**: Table size determined at creation, not compile time
- **Zero Dependencies**: Pure implementations in each language

## Project Structure

```
zeroipc/
├── SPECIFICATION.md      # Binary format all implementations follow
├── cpp/                  # C++ implementation
│   ├── include/zeroipc/  # Headers
│   └── tests/           # Unit tests
├── python/              # Pure Python implementation  
│   ├── zeroipc/         # Package
│   └── tests/           # Unit tests
└── interop/             # Cross-language tests
```

## Quick Start

### C++ Creates Data

```cpp
#include <zeroipc/memory.h>
#include <zeroipc/array.h>

// Create shared memory
zeroipc::Memory mem("/mydata", 10*1024*1024);  // 10MB

// Create typed array
zeroipc::Array<float> data(mem, "sensor_data", 1000);
data[0] = 3.14f;
```

### Python Reads It

```python
from zeroipc import Memory, Array
import numpy as np

# Open same shared memory
mem = Memory("/mydata")

# Read with duck typing - user specifies type
data = Array(mem, "sensor_data", dtype=np.float32)
print(data[0])  # 3.14
```

## Key Design Principles

1. **No Type Storage**: Users are responsible for type consistency
2. **Minimal Overhead**: Table only stores what's absolutely necessary  
3. **Language Equality**: No language is "primary" - all are first-class
4. **Binary Compatibility**: All languages read/write the same format

## Building

### C++
```bash
cd cpp
cmake -B build .
cmake --build build
ctest --test-dir build
```

### Python
```bash
cd python
python -m pytest tests/
```

### Integration Test
```bash
cd interop
./test_interop.sh
```


## Supported Data Structures

Currently implemented:
- `Table` - Metadata registry
- `Memory` - Shared memory manager
- `Array` - Fixed-size arrays

Coming soon:
- `Queue` - Lock-free circular buffer
- `Stack` - Lock-free stack
- `Map` - Hash map
- More...

## License

MIT