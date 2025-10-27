# Installation Guide

This guide covers installing ZeroIPC for C++, C, and Python on various platforms.

## System Requirements

### Operating System
- Linux (Ubuntu 20.04+, Debian 10+, Fedora 33+, etc.)
- macOS 10.15+
- Any POSIX-compliant Unix system
- Windows via WSL2 or Cygwin

### Compiler Requirements

| Language | Minimum Version | Recommended |
|----------|----------------|-------------|
| C++ | GCC 12+ or Clang 15+ | GCC 13+ or Clang 17+ |
| C | GCC 9+ or Clang 10+ | GCC 11+ or Clang 14+ |
| Python | Python 3.8+ | Python 3.10+ |

!!! warning "C++23 Required"
    The C++ implementation requires C++23 support. Ensure your compiler is up-to-date.

## Quick Install

=== "C++"

    ```bash
    # Clone repository
    git clone https://github.com/yourusername/zeroipc.git
    cd zeroipc/cpp
    
    # Configure and build
    cmake -B build -DCMAKE_BUILD_TYPE=Release .
    cmake --build build
    
    # Install (optional)
    sudo cmake --install build
    ```

=== "Python"

    ```bash
    # Install from PyPI (when available)
    pip install zeroipc
    
    # Or install from source
    git clone https://github.com/yourusername/zeroipc.git
    cd zeroipc/python
    pip install -e .
    ```

=== "C"

    ```bash
    # Clone repository
    git clone https://github.com/yourusername/zeroipc.git
    cd zeroipc/c
    
    # Build static library
    make
    
    # Install (optional)
    sudo make install
    ```

## Detailed Installation Instructions

### C++ Installation

#### 1. Install Dependencies

=== "Ubuntu/Debian"

    ```bash
    sudo apt update
    sudo apt install -y \
        build-essential \
        cmake \
        libgtest-dev \
        g++-13
    
    # Set GCC 13 as default
    sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100
    sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100
    ```

=== "Fedora/RHEL"

    ```bash
    sudo dnf install -y \
        gcc-c++ \
        cmake \
        gtest-devel
    ```

=== "macOS"

    ```bash
    # Install Homebrew if not already installed
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    
    # Install dependencies
    brew install cmake googletest llvm
    
    # Use LLVM's clang (for C++23 support)
    export CC=/usr/local/opt/llvm/bin/clang
    export CXX=/usr/local/opt/llvm/bin/clang++
    ```

#### 2. Build ZeroIPC

```bash
# Clone the repository
git clone https://github.com/yourusername/zeroipc.git
cd zeroipc/cpp

# Configure CMake
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=23 \
    -DBUILD_TESTS=ON \
    .

# Build
cmake --build build -j$(nproc)

# Run tests to verify installation
cd build && ctest --output-on-failure
```

#### 3. Install (Optional)

```bash
# Install to /usr/local
sudo cmake --install build

# Or install to custom location
cmake --install build --prefix /opt/zeroipc
```

#### 4. Use in Your Project

ZeroIPC is header-only, so you can:

**Option 1: Direct include**
```bash
# Copy headers to your project
cp -r zeroipc/cpp/include/zeroipc /path/to/your/project/include/
```

**Option 2: CMake find_package**
```cmake
# In your CMakeLists.txt
find_package(zeroipc REQUIRED)
target_link_libraries(your_target PRIVATE zeroipc::zeroipc)
```

**Option 3: CMake add_subdirectory**
```cmake
# In your CMakeLists.txt
add_subdirectory(external/zeroipc/cpp)
target_link_libraries(your_target PRIVATE zeroipc::zeroipc)
```

### Python Installation

#### 1. Install Python Dependencies

```bash
# Install NumPy
pip install numpy

# For development
pip install numpy pytest pytest-cov black mypy ruff
```

#### 2. Install ZeroIPC

=== "From PyPI (Recommended)"

    ```bash
    pip install zeroipc
    ```

=== "From Source"

    ```bash
    git clone https://github.com/yourusername/zeroipc.git
    cd zeroipc/python
    
    # Development install (editable)
    pip install -e .
    
    # Or regular install
    pip install .
    ```

#### 3. Verify Installation

```python
import zeroipc
print(zeroipc.__version__)

from zeroipc import Memory, Array
# If no errors, installation successful!
```

### C Installation

#### 1. Build Static Library

```bash
git clone https://github.com/yourusername/zeroipc.git
cd zeroipc/c

# Build
make

# Run tests
make test
```

#### 2. Install

```bash
# Install to /usr/local
sudo make install

# Or specify prefix
make install PREFIX=/opt/zeroipc
```

This installs:
- `libzeroipc.a` to `$PREFIX/lib/`
- Headers to `$PREFIX/include/zeroipc/`

#### 3. Link in Your Project

```bash
gcc -o myapp myapp.c -lzeroipc -lrt -lpthread
```

Or in a Makefile:
```makefile
CFLAGS = -std=c99 -Wall
LDFLAGS = -lzeroipc -lrt -lpthread

myapp: myapp.c
    $(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
```

## Building CLI Tools

The `zeroipc` CLI tool is built automatically with the C++ library:

```bash
cd cpp
cmake -B build .
cmake --build build

# The tool will be at build/tools/zeroipc
./build/tools/zeroipc --help
```

To install globally:
```bash
sudo cp build/tools/zeroipc /usr/local/bin/
```

## Verification

### C++ Verification

Create a test file `test_install.cpp`:

```cpp
#include <zeroipc/memory.h>
#include <zeroipc/array.h>
#include <iostream>

int main() {
    zeroipc::Memory mem("/test", 1024*1024);
    zeroipc::Array<int> arr(mem, "numbers", 10);
    arr[0] = 42;
    std::cout << "ZeroIPC works! Value: " << arr[0] << std::endl;
    return 0;
}
```

Compile and run:
```bash
g++ -std=c++23 -o test_install test_install.cpp -lrt -lpthread
./test_install
# Output: ZeroIPC works! Value: 42
```

### Python Verification

```python
from zeroipc import Memory, Array
import numpy as np

mem = Memory("/test", 1024*1024)
arr = Array(mem, "numbers", dtype=np.int32, capacity=10)
arr[0] = 42
print(f"ZeroIPC works! Value: {arr[0]}")
# Output: ZeroIPC works! Value: 42
```

### C Verification

Create `test_install.c`:

```c
#include <zeroipc/memory.h>
#include <zeroipc/array.h>
#include <stdio.h>

int main() {
    zipc_memory_t* mem = zipc_memory_create("/test", 1024*1024);
    zipc_array_t* arr = zipc_array_create(mem, "numbers", sizeof(int), 10);
    
    int value = 42;
    zipc_array_set(arr, 0, &value);
    
    int result;
    zipc_array_get(arr, 0, &result);
    
    printf("ZeroIPC works! Value: %d\n", result);
    
    zipc_array_destroy(arr);
    zipc_memory_destroy(mem);
    return 0;
}
```

Compile and run:
```bash
gcc -std=c99 -o test_install test_install.c -lzeroipc -lrt -lpthread
./test_install
# Output: ZeroIPC works! Value: 42
```

## Troubleshooting

### Common Issues

#### Issue: "C++23 features not available"

**Solution**: Update your compiler:
```bash
# Ubuntu
sudo apt install g++-13

# macOS
brew install llvm
export CXX=/usr/local/opt/llvm/bin/clang++
```

#### Issue: "Cannot find -lrt"

**Solution**: The realtime library is not available. On most Linux systems:
```bash
# Usually part of glibc, but ensure it's installed
sudo apt install libc6-dev
```

On macOS, `-lrt` is not needed (POSIX shared memory is in libSystem).

#### Issue: "Permission denied when creating shared memory"

**Solution**: Check `/dev/shm` permissions:
```bash
ls -la /dev/shm
# Should be drwxrwxrwt

# If not, fix permissions
sudo chmod 1777 /dev/shm
```

#### Issue: "Python import fails"

**Solution**: Ensure NumPy is installed:
```bash
pip install numpy
```

### Getting Help

If you encounter issues:

1. Check the [FAQ](../best-practices/pitfalls.md#common-questions)
2. Search [GitHub Issues](https://github.com/yourusername/zeroipc/issues)
3. Create a new issue with:
   - OS and version
   - Compiler version
   - Error message
   - Minimal reproduction code

## Next Steps

Installation complete! Now:

1. **[Quick Start](quick-start.md)** - Write your first program
2. **[Tutorial](../tutorial/index.md)** - Learn all the features
3. **[Examples](../examples/index.md)** - See real-world usage
