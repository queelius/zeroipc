# ZeroIPC Documentation

## Overview

ZeroIPC is a cross-language shared memory IPC library that enables zero-copy data sharing between processes. This documentation covers the overall architecture, design philosophy, and usage patterns.

## Documentation Structure

- **[Architecture](architecture.md)** - System design and memory layout
- **[Design Philosophy](design_philosophy.md)** - Core principles and decisions
- **[Data Structures](data_structures/)** - Detailed documentation for each data structure
- **[Performance](performance.md)** - Benchmarks and optimization details
- **[Tutorial](tutorial.md)** - Getting started guide

## Language-Specific Documentation

- **[C++ Implementation](../cpp/README.md)** - C++ specific details
- **[Python Implementation](../python/README.md)** - Python specific details

## Binary Specification

See [SPECIFICATION.md](../SPECIFICATION.md) for the complete binary format specification that all implementations follow.

## Quick Start

### C++
```cpp
#include <zeroipc/memory.h>
#include <zeroipc/array.h>

zeroipc::Memory mem("/mydata", 10*1024*1024);
zeroipc::Array<float> data(mem, "sensor_data", 1000);
```

### Python
```python
from zeroipc import Memory, Array
import numpy as np

mem = Memory("/mydata")
data = Array(mem, "sensor_data", dtype=np.float32)
```

## Key Concepts

1. **Minimal Metadata**: Only store what's necessary (name, offset, size)
2. **Duck Typing**: Users specify types at runtime
3. **Language Independence**: Each implementation stands alone
4. **Zero-Copy**: Direct memory access without serialization