# ZeroIPC

[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Documentation](https://img.shields.io/badge/docs-doxygen-blue.svg)](https://queelius.github.io/zeroipc/)

High-performance zero-copy IPC library for C++23 with lock-free data structures built on POSIX shared memory.

## Features

- **Header-only** library with C++23 support
- **Lock-free** data structures (queue, atomic types)
- **Zero-copy** inter-process communication
- **Dynamic discovery** of named structures
- **SIMD optimizations** for performance-critical paths
- **Stack allocation** strategy (no fragmentation)
- **Multi-process safe** with reference counting

## Requirements

- C++23 compatible compiler (GCC 12+, Clang 15+)
- POSIX-compliant system (Linux, macOS, BSD)
- CMake 3.14+

## Installation

### Using CMake FetchContent (Recommended)

Add to your `CMakeLists.txt`:

```cmake
include(FetchContent)
FetchContent_Declare(
    zeroipc
    GIT_REPOSITORY https://github.com/queelius/zeroipc.git
    GIT_TAG        v1.0.0
)
FetchContent_MakeAvailable(zeroipc)

target_link_libraries(your_target PRIVATE zeroipc::zeroipc)
```

### Using CPM.cmake

```cmake
CPMAddPackage(
    NAME zeroipc
    GITHUB_REPOSITORY queelius/zeroipc
    VERSION 1.0.0
)

target_link_libraries(your_target PRIVATE zeroipc::zeroipc)
```

### Using Conan

```bash
conan install zeroipc/1.0.0@
```

Add to your `CMakeLists.txt`:

```cmake
find_package(zeroipc REQUIRED)
target_link_libraries(your_target PRIVATE zeroipc::zeroipc)
```

### Using vcpkg

```bash
vcpkg install zeroipc
```

Add to your `CMakeLists.txt`:

```cmake
find_package(zeroipc CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE zeroipc::zeroipc)
```

### Manual Installation

```bash
git clone https://github.com/queelius/zeroipc.git
cd zeroipc
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
make install
```

Then in your project:

```cmake
find_package(zeroipc REQUIRED)
target_link_libraries(your_target PRIVATE zeroipc::zeroipc)
```

## Quick Start

```cpp
#include <zeroipc.h>
#include <array.h>
#include <queue.h>

int main() {
    // Create shared memory segment
    zeroipc::memory shm("my_simulation", 10 * 1024 * 1024);  // 10MB
    
    // Create array in shared memory
    zeroipc::array<float> sensors(shm, "sensor_data", 1000);
    sensors[0] = 42.0f;
    
    // Create lock-free queue
    zeroipc::queue<int> events(shm, "event_queue", 100);
    events.enqueue(123);
    
    // In another process, attach and use:
    zeroipc::memory shm2("my_simulation", 0);  // 0 = attach to existing
    zeroipc::array<float> sensors2(shm2, "sensor_data");  // Attach to existing
    float value = sensors2[0];  // Read: 42.0f
    
    // Clean up when done
    shm.unlink();  // Remove from system
    
    return 0;
}
```

## Data Structures

- **zeroipc::array\<T\>** - Fixed-size array with O(1) access
- **zeroipc::queue\<T\>** - Lock-free circular queue  
- **zeroipc::atomic_value\<T\>** - Atomic types for synchronization
- **zeroipc::ring\<T\>** - Ring buffer for streaming data
- **zeroipc::pool\<T\>** - Object pool for allocation
- **zeroipc::map\<K,V\>** - Hash map for key-value storage
- **zeroipc::set\<T\>** - Set for unique elements
- **zeroipc::bitset\<N\>** - Compact bit array
- **zeroipc::stack\<T\>** - LIFO stack

## Documentation

- [API Reference](https://queelius.github.io/zeroipc/)
- [Design Philosophy](docs/design_philosophy.md)
- [Performance Guide](docs/performance.md)
- [Examples](examples/)

## Building Tests & Examples

```bash
mkdir build && cd build
cmake .. -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
make
ctest  # Run tests
```

## Performance

Benchmarks on Intel i7-12700K (single-threaded):

- Array read: 3.5 GB/s
- Array write: 2.8 GB/s  
- Queue enqueue/dequeue: 15M ops/sec
- Atomic increment: 50M ops/sec

## License

MIT License - see [LICENSE](LICENSE) file

## Author

Alex Towell

## Contributing

Pull requests welcome! Please ensure:

- All tests pass
- Code follows C++23 best practices
- Documentation is updated
