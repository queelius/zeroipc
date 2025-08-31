# ZeroIPC C++ Implementation

## Overview

The C++ implementation provides high-performance, type-safe access to shared memory data structures using modern C++23 features.

## Features

- **Template-based**: Compile-time type safety and optimization
- **Zero-overhead**: No runtime type checking in hot paths
- **RAII**: Automatic resource management
- **Lock-free**: Atomic operations for concurrent data structures
- **Header-only**: Easy integration

## Building

```bash
cmake -B build .
cmake --build build
```

## Running Tests

```bash
ctest --test-dir build
```

## Usage Example

```cpp
#include <zeroipc/memory.h>
#include <zeroipc/array.h>

int main() {
    // Create or open shared memory
    zeroipc::Memory mem("/sensor_data", 10 * 1024 * 1024);  // 10MB
    
    // Create array with compile-time type
    zeroipc::Array<float> readings(mem, "temperature", 1000);
    
    // Direct access
    readings[0] = 23.5f;
    
    // STL compatibility
    std::fill(readings.begin(), readings.end(), 0.0f);
    
    return 0;
}
```

## API Reference

### Memory

```cpp
Memory(const std::string& name, size_t size = 0, size_t max_entries = 64)
```
- `name`: Shared memory identifier (e.g., "/myshm")
- `size`: Size in bytes (0 to open existing)
- `max_entries`: Maximum table entries

### Array

```cpp
template<typename T>
Array(Memory& memory, std::string_view name, size_t capacity = 0)
```
- `T`: Element type (must be trivially copyable)
- `memory`: Memory instance
- `name`: Array identifier
- `capacity`: Number of elements (0 to open existing)

## Requirements

- C++23 compatible compiler
- POSIX shared memory support
- CMake 3.20+

## Design Notes

The C++ implementation prioritizes:
1. **Type safety** through templates
2. **Performance** via compile-time optimization
3. **Modern C++** idioms and features
4. **Zero-copy** access patterns