# ZeroIPC C Implementation

## Overview

Pure C implementation of the ZeroIPC shared memory protocol. Provides a minimal, dependency-free library for high-performance IPC.

## Features

- **Pure C99**: Maximum portability
- **Zero dependencies**: Only POSIX APIs
- **Minimal overhead**: Direct memory access
- **Simple API**: Easy to integrate
- **Cross-language**: Interoperates with C++ and Python

## Building

```bash
make            # Build library
make test       # Run tests
make examples   # Build examples
make clean      # Clean build artifacts
```

## Installation

The library builds as a static archive `libzeroipc.a`. To use in your project:

1. Copy `include/zeroipc.h` to your include path
2. Link with `libzeroipc.a -lrt -lpthread`

## Usage Example

```c
#include <zeroipc.h>

int main() {
    // Create shared memory
    zeroipc_memory_t* mem = zeroipc_memory_create("/sensors", 10*1024*1024, 64);
    
    // Create array
    zeroipc_array_t* temps = zeroipc_array_create(mem, "temperature", 
                                                   sizeof(float), 1000);
    
    // Write data
    float value = 23.5f;
    zeroipc_array_set(temps, 0, &value);
    
    // Direct access
    float* data = (float*)zeroipc_array_data(temps);
    data[1] = 24.0f;
    
    // Clean up
    zeroipc_array_close(temps);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/sensors");
    
    return 0;
}
```

## API Reference

### Memory Management

```c
// Create new shared memory
zeroipc_memory_t* zeroipc_memory_create(const char* name, size_t size, 
                                        size_t max_entries);

// Open existing shared memory
zeroipc_memory_t* zeroipc_memory_open(const char* name);

// Close memory handle
void zeroipc_memory_close(zeroipc_memory_t* mem);

// Remove shared memory
void zeroipc_memory_unlink(const char* name);
```

### Table Operations

```c
// Add entry to table
int zeroipc_table_add(zeroipc_memory_t* mem, const char* name, 
                      size_t size, size_t* offset);

// Find entry in table
int zeroipc_table_find(zeroipc_memory_t* mem, const char* name, 
                       size_t* offset, size_t* size);

// Remove entry from table
int zeroipc_table_remove(zeroipc_memory_t* mem, const char* name);

// Get entry count
size_t zeroipc_table_count(zeroipc_memory_t* mem);
```

### Array Operations

```c
// Create new array
zeroipc_array_t* zeroipc_array_create(zeroipc_memory_t* mem, const char* name,
                                      size_t elem_size, size_t capacity);

// Open existing array
zeroipc_array_t* zeroipc_array_open(zeroipc_memory_t* mem, const char* name);

// Get/set elements
void* zeroipc_array_get(zeroipc_array_t* array, size_t index);
int zeroipc_array_set(zeroipc_array_t* array, size_t index, const void* value);

// Direct data access
void* zeroipc_array_data(zeroipc_array_t* array);
```

## Error Handling

All functions that can fail return error codes from `zeroipc_error_t`:

```c
typedef enum {
    ZEROIPC_OK = 0,
    ZEROIPC_ERROR_OPEN = -1,
    ZEROIPC_ERROR_MMAP = -2,
    ZEROIPC_ERROR_SIZE = -3,
    ZEROIPC_ERROR_NOT_FOUND = -4,
    ZEROIPC_ERROR_TABLE_FULL = -5,
    // ...
} zeroipc_error_t;

// Get human-readable error string
const char* zeroipc_error_string(zeroipc_error_t error);
```

## Cross-Language Example

**C Producer:**
```c
zeroipc_memory_t* mem = zeroipc_memory_create("/data", 1024*1024, 64);
zeroipc_array_t* arr = zeroipc_array_create(mem, "values", sizeof(int), 100);
int value = 42;
zeroipc_array_set(arr, 0, &value);
```

**Python Consumer:**
```python
from zeroipc import Memory, Array
import numpy as np

mem = Memory("/data")
arr = Array(mem, "values", dtype=np.int32)
print(arr[0])  # 42
```

## Design Notes

The C implementation prioritizes:
1. **Simplicity** - Minimal API surface
2. **Portability** - Pure C99, POSIX only
3. **Performance** - Zero-copy, direct access
4. **Compatibility** - Same binary format as C++/Python

## Requirements

- C99 compiler
- POSIX shared memory support (Linux/macOS)
- librt (for shm_open/shm_unlink)

## Limitations

- No type safety (void* based API)
- Manual memory management
- User must track element sizes/types