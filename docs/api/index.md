# API Reference

Complete API documentation for ZeroIPC across all supported languages.

## Language APIs

### [C++ API](cpp/memory.md)

Modern C++23 header-only template library.

**Key Features:**
- Template-based for zero overhead
- RAII resource management  
- Compile-time type safety
- Lock-free atomic operations

**Documentation:**
- [Memory & Table](cpp/memory.md) - Core memory management
- [Data Structures](cpp/data-structures.md) - Arrays, queues, stacks, etc.
- [Synchronization](cpp/synchronization.md) - Semaphores, barriers, latches
- [Codata Structures](cpp/codata.md) - Futures, streams, channels

### [Python API](python/memory.md)

Pure Python implementation with NumPy integration.

**Key Features:**
- No compilation required
- NumPy for performance
- Duck typing for flexibility
- mmap for direct memory access

**Documentation:**
- [Memory & Table](python/memory.md) - Core memory management
- [Data Structures](python/data-structures.md) - Arrays, queues, stacks, etc.
- [Synchronization](python/synchronization.md) - Semaphores, barriers, latches
- [Codata Structures](python/codata.md) - Futures, streams, channels

### [C API](c/overview.md)

Pure C99 static library for maximum portability.

**Key Features:**
- Zero dependencies beyond POSIX
- Explicit memory management
- Minimal overhead
- Maximum portability

## Quick Reference

### Common Operations

#### Creating Shared Memory

=== "C++"
    ```cpp
    #include <zeroipc/memory.h>
    zeroipc::Memory mem("/name", 1024*1024);  // 1MB
    ```

=== "Python"
    ```python
    from zeroipc import Memory
    mem = Memory("/name", 1024*1024)  # 1MB
    ```

=== "C"
    ```c
    #include <zeroipc/memory.h>
    zipc_memory_t* mem = zipc_memory_create("/name", 1024*1024);
    ```

#### Creating an Array

=== "C++"
    ```cpp
    #include <zeroipc/array.h>
    zeroipc::Array<float> arr(mem, "data", 1000);
    ```

=== "Python"
    ```python
    from zeroipc import Array
    import numpy as np
    arr = Array(mem, "data", dtype=np.float32, capacity=1000)
    ```

=== "C"
    ```c
    #include <zeroipc/array.h>
    zipc_array_t* arr = zipc_array_create(mem, "data", sizeof(float), 1000);
    ```

#### Creating a Queue

=== "C++"
    ```cpp
    #include <zeroipc/queue.h>
    zeroipc::Queue<int> q(mem, "tasks", 100);
    ```

=== "Python"
    ```python
    from zeroipc import Queue
    q = Queue(mem, "tasks", dtype=np.int32, capacity=100)
    ```

## Type Mapping

Cross-language type compatibility:

| C++ | Python | C | Size |
|-----|--------|---|------|
| `int8_t` | `np.int8` | `int8_t` | 1 byte |
| `int16_t` | `np.int16` | `int16_t` | 2 bytes |
| `int32_t` | `np.int32` | `int32_t` | 4 bytes |
| `int64_t` | `np.int64` | `int64_t` | 8 bytes |
| `float` | `np.float32` | `float` | 4 bytes |
| `double` | `np.float64` | `double` | 8 bytes |

## Naming Conventions

### C++
- **Classes**: PascalCase (`Memory`, `Array<T>`)
- **Methods**: camelCase (`enqueue()`, `dequeue()`)
- **Namespaces**: `zeroipc::`

### Python
- **Classes**: PascalCase (`Memory`, `Array`)
- **Methods**: snake_case (`enqueue()`, `dequeue()`)
- **Module**: `zeroipc`

### C
- **Functions**: `zipc_*` prefix (`zipc_memory_create()`)
- **Types**: `zipc_*_t` suffix (`zipc_memory_t`)
- **Struct tags**: snake_case

## Error Handling

### C++
```cpp
// Returns std::optional<T> for operations that may fail
auto value = queue.dequeue();
if (value) {
    process(*value);
} else {
    // Queue was empty
}
```

### Python
```python
# Returns None for operations that may fail
value = queue.dequeue()
if value is not None:
    process(value)
else:
    # Queue was empty
```

### C
```c
// Returns status codes
int result = zipc_queue_dequeue(queue, &value);
if (result == ZIPC_SUCCESS) {
    process(value);
} else {
    // Queue was empty
}
```

## Next Steps

- Browse the **[C++ API](cpp/memory.md)**
- Browse the **[Python API](python/memory.md)**
- Browse the **[C API](c/overview.md)**
- See **[Examples](../examples/index.md)** for usage patterns
