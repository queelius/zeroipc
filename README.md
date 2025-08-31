# ZeroIPC - Cross-Language Shared Memory IPC

## Overview

ZeroIPC is a high-performance inter-process communication library that enables zero-copy data sharing through POSIX shared memory. It provides true language independence with parallel implementations in C++ and Python.

### Key Features

- 🚀 **Zero-Copy Performance** - Direct memory access without serialization
- 🌐 **Language Independence** - C++ and Python are equal partners, not bindings
- 🔒 **Lock-Free Operations** - Atomic operations for concurrent access
- 📦 **Minimal Metadata** - Only store name/offset/size - no type information
- 🦆 **Duck Typing** - Python users specify types at runtime, C++ uses templates
- 🎯 **Simple Discovery** - Named structures for easy cross-process lookup

## Quick Start

### C++ Creates, Python Reads

**C++ Producer:**
```cpp
#include <zeroipc/memory.h>
#include <zeroipc/array.h>

// Create shared memory
zeroipc::Memory mem("/sensor_data", 10*1024*1024);  // 10MB

// Create typed array
zeroipc::Array<float> temps(mem, "temperature", 1000);
temps[0] = 23.5f;
```

**Python Consumer:**
```python
from zeroipc import Memory, Array
import numpy as np

# Open same shared memory
mem = Memory("/sensor_data")

# Read with duck typing - user specifies type
temps = Array(mem, "temperature", dtype=np.float32)
print(temps[0])  # 23.5
```

### Python Creates, C++ Reads

**Python Producer:**
```python
from zeroipc import Memory, Array
import numpy as np

# Create shared memory
mem = Memory("/analytics", size=10*1024*1024)

# Python CREATES new array (allocates in shared memory)
results = Array(mem, "scores", capacity=100, dtype=np.float64)
results[0] = 0.95

# Create another structure
metrics = Array(mem, "metrics", capacity=50, dtype=np.int32)
metrics[0] = 42
```

**C++ Consumer:**
```cpp
// Open existing shared memory
zeroipc::Memory mem("/analytics");

// C++ reads what Python created
zeroipc::Array<double> results(mem, "scores");
std::cout << results[0];  // 0.95

zeroipc::Array<int32_t> metrics(mem, "metrics");
std::cout << metrics[0];  // 42
```

## Architecture

### Binary Format Specification

All implementations follow the same binary format defined in [SPECIFICATION.md](SPECIFICATION.md):

```
[Table Header][Table Entries][Data Structure 1][Data Structure 2]...
```

- **Table Header**: Magic number, version, entry count, next offset
- **Table Entry**: Name (32 bytes), offset (4 bytes), size (4 bytes)
- **Data Structures**: Raw binary data, layout determined by structure type

### Minimal Metadata Philosophy

Unlike traditional IPC systems, ZeroIPC stores NO type information:
- **Name**: For discovery
- **Offset**: Where data starts
- **Size**: How much memory is used

This enables true language independence:
- **Both languages can create**: Python and C++ can both allocate new structures
- **Both languages can read**: Either can discover and access existing structures
- **Type safety per language**: C++ uses templates, Python uses NumPy dtypes

## Data Structures

Currently implemented:
- ✅ **Array** - Fixed-size contiguous storage
- ✅ **Table** - Metadata registry for discovery

Coming soon:
- 🔄 **Queue** - Lock-free FIFO circular buffer
- 📚 **Stack** - Lock-free LIFO 
- 🗺️ **Map** - Hash table with linear probing
- 🎱 **Pool** - Object pool with free list
- ⚛️ **Atomic** - Single atomic variables
- 🔮 **Future** - Async result handling
- 🚀 **Function** - RPC through shared memory
- ∞ **Codata** - Streams and infinite data structures

## Language Implementations

### [C Implementation](c/)
- Pure C99 for maximum portability
- Zero dependencies beyond POSIX
- Static library (libzeroipc.a)
- Minimal overhead

### [C++ Implementation](cpp/)
- Template-based for zero overhead
- Header-only library
- Modern C++23 features
- RAII resource management

### [Python Implementation](python/)
- Pure Python, no compilation required
- NumPy integration for performance
- Duck typing for flexibility
- mmap for direct memory access

## Building and Testing

### C
```bash
cd c
make            # Build library
make test       # Run tests
```

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
pip install -e .
python -m pytest tests/
```

### Cross-Language Tests
```bash
cd interop
./test_interop.sh          # C++ writes, Python reads
./test_reverse_interop.sh  # Python writes, C++ reads
```

## Design Principles

1. **Language Equality** - No language is "primary", all are first-class
2. **Minimal Overhead** - Table stores only what's absolutely necessary
3. **User Responsibility** - Users ensure type consistency across languages
4. **Zero Dependencies** - Each implementation stands alone
5. **Binary Compatibility** - All languages read/write the same format

## Performance

- **Array Access**: Identical to native arrays (zero overhead)
- **Queue Operations**: Lock-free with atomic CAS
- **Memory Allocation**: O(1) bump allocation
- **Discovery**: O(n) where n ≤ max_entries

## Use Cases

ZeroIPC excels at:
- ✅ High-frequency sensor data sharing
- ✅ Multi-process simulations
- ✅ Real-time analytics pipelines
- ✅ Cross-language scientific computing
- ✅ Zero-copy producer-consumer patterns

Not designed for:
- ❌ General-purpose memory allocation
- ❌ Network-distributed systems
- ❌ Persistent storage
- ❌ Garbage collection

## Documentation

- [Architecture](docs/architecture.md) - System design and memory layout
- [Design Philosophy](docs/design_philosophy.md) - Core principles and trade-offs
- [Binary Specification](SPECIFICATION.md) - Wire format all implementations follow
- [C++ Documentation](cpp/README.md) - C++ specific details
- [Python Documentation](python/README.md) - Python specific details

## Contributing

Contributions welcome! When adding new language implementations:
1. Follow the binary specification exactly
2. Create a new directory for your language
3. Implement Memory, Table, and Array as minimum
4. Add cross-language tests in `interop/`

## Future Vision

The boundary between data and code is fluid. Future explorations:
- **Codata**: Infinite streams and lazy evaluation
- **Functions**: First-class functions as data structures
- **Continuations**: Suspended computations in shared memory
- **Reactive Streams**: Event-driven data flows

## License

MIT