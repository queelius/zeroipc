# posix_shm

[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Documentation](https://img.shields.io/badge/docs-doxygen-blue.svg)](https://queelius.github.io/posix_shm/)

High-performance POSIX shared memory library for C++23 with lock-free data structures and zero-copy IPC.

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
    posix_shm
    GIT_REPOSITORY https://github.com/queelius/posix_shm.git
    GIT_TAG        v1.0.0
)
FetchContent_MakeAvailable(posix_shm)

target_link_libraries(your_target PRIVATE posix_shm::posix_shm)
```

### Using CPM.cmake

```cmake
CPMAddPackage(
    NAME posix_shm
    GITHUB_REPOSITORY queelius/posix_shm
    VERSION 1.0.0
)

target_link_libraries(your_target PRIVATE posix_shm::posix_shm)
```

### Using Conan

```bash
conan install posix_shm/1.0.0@
```

Add to your `CMakeLists.txt`:

```cmake
find_package(posix_shm REQUIRED)
target_link_libraries(your_target PRIVATE posix_shm::posix_shm)
```

### Using vcpkg

```bash
vcpkg install posix-shm
```

Add to your `CMakeLists.txt`:

```cmake
find_package(posix_shm CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE posix_shm::posix_shm)
```

### Manual Installation

```bash
git clone https://github.com/queelius/posix_shm.git
cd posix_shm
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
make install
```

Then in your project:

```cmake
find_package(posix_shm REQUIRED)
target_link_libraries(your_target PRIVATE posix_shm::posix_shm)
```

## Quick Start

```cpp
#include <posix_shm.h>
#include <shm_array.h>
#include <shm_queue.h>

int main() {
    // Create shared memory segment
    posix_shm shm("my_simulation", 10 * 1024 * 1024);  // 10MB
    
    // Create array in shared memory
    shm_array<float> sensors(shm, "sensor_data", 1000);
    sensors[0] = 42.0f;
    
    // Create lock-free queue
    shm_queue<int> events(shm, "event_queue", 100);
    events.enqueue(123);
    
    // In another process, attach and use:
    posix_shm shm2("my_simulation", 0);  // 0 = attach to existing
    auto* sensors2 = shm_array<float>::open(shm2, "sensor_data");
    float value = (*sensors2)[0];  // Read: 42.0f
    
    // Clean up when done
    shm.unlink();  // Remove from system
    
    return 0;
}
```

## Data Structures

- **shm_array\<T\>** - Fixed-size array with O(1) access
- **shm_queue\<T\>** - Lock-free circular queue  
- **shm_atomic\<T\>** - Atomic types for synchronization
- **shm_ring_buffer\<T\>** - Ring buffer for streaming data
- **shm_object_pool\<T\>** - Object pool for allocation

## Documentation

- [API Reference](https://queelius.github.io/posix_shm/)
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
