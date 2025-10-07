# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**ZeroIPC** is a high-performance cross-language shared memory IPC library that enables zero-copy data sharing between processes. The project implements a binary format specification allowing different languages to share data structures through POSIX shared memory without serialization overhead.

## Architecture

### Parallel Language Implementations
- `c/` - Pure C99 implementation with static library
- `cpp/` - Modern C++23 header-only template library  
- `python/` - Pure Python implementation using mmap and numpy
- `interop/` - Cross-language integration tests
- `SPECIFICATION.md` - Binary format all implementations must follow

### Core Design Principles
- **Minimal metadata**: Table stores only name, offset, and size (no type information)
- **Duck typing**: Users specify types at runtime (Python) or compile time (C++)
- **Lock-free operations**: All concurrent structures use atomic CAS operations
- **No automatic memory management**: Users control layout, no defragmentation
- **Binary compatibility**: All languages read/write identical format

### Memory Layout
```
[Table Header][Table Entries][Structure 1][Structure 2]...[Structure N]
```

## Build Commands

### C++ (Primary development focus)
```bash
# Configure with CMake (requires C++23, GoogleTest auto-downloaded)
cmake -B build .

# Build everything
cmake --build build

# Run all tests
cd build && ctest --output-on-failure

# Run specific test
./build/tests/test_queue
./build/tests/test_lockfree_comprehensive

# Run tests with verbose output
cd build && ctest -V

# Clean build
rm -rf build/
```

### C
```bash
cd c
make            # Build static library
make test       # Run tests  
make clean      # Clean artifacts
```

### Python
```bash
cd python
make install-dev  # Set up dev environment with all dependencies
make test         # Run tests
make test-cov     # Run with coverage report
make lint         # Run linting (ruff, mypy)
make format       # Format code (black, isort)
```

### Cross-Language Testing
```bash
cd interop
./test_interop.sh          # C++ writes, Python reads
./test_reverse_interop.sh  # Python writes, C++ reads  
```

## C++ Implementation Details

### Modern C++23 Features Used
- **Concepts**: `requires std::is_trivially_copyable_v<T>` for type constraints
- **std::atomic**: Explicit memory ordering for lock-free operations
- **std::optional**: Safe error handling without exceptions
- **std::string_view**: Zero-allocation string handling
- **[[nodiscard]]**: API safety attributes
- **Designated initializers**: Clear struct initialization

### Lock-Free Implementation

All concurrent structures (Queue, Stack) use compare-and-swap (CAS) loops:

```cpp
// CORRECT lock-free enqueue pattern
do {
    current_tail = tail.load(std::memory_order_relaxed);
    next_tail = (current_tail + 1) % capacity;
    if (queue_full) return false;
} while (!tail.compare_exchange_weak(current_tail, next_tail,
                                     std::memory_order_relaxed,
                                     std::memory_order_relaxed));
// Write data with proper fence
data[current_tail] = value;
std::atomic_thread_fence(std::memory_order_release);
```

### Table Size Configuration

The metadata table uses template parameters for compile-time size configuration:

```cpp
// Predefined table sizes (N = number of entries)
table1      // 1 entry (minimal)
table4      // 4 entries (quad)  
table8      // 8 entries (tiny)
table16     // 16 entries (small)
table32     // 32 entries (compact)
table64     // 64 entries (standard, aliased as 'table')
table128    // 128 entries (medium)
table256    // 256 entries (large)
table512    // 512 entries (xlarge)
table1024   // 1024 entries (huge)
table2048   // 2048 entries (xhuge)
table4096   // 4096 entries (maximum)

// Custom configuration
table_impl<name_size, max_entries>  // Full control
```

**Important**: Tests creating many structures need larger tables:
```cpp
zeroipc::memory<table256> shm("/test", size);  // For tests with >64 structures
```

## Data Structures

### Currently Implemented
- `memory`: POSIX shared memory wrapper with reference counting
- `table`: Metadata registry for dynamic structure discovery  
- `array<T>`: Fixed-size contiguous array with atomic operations
- `queue<T>`: Lock-free MPMC circular buffer using CAS
- `stack<T>`: Lock-free stack with CAS push/pop

### Structure Creation Pattern

Both languages can create and discover structures:

```cpp
// C++ creates
zeroipc::memory mem("/data", 10*1024*1024);
zeroipc::array<float> temps(mem, "temperatures", 1000);

// Python discovers and reads
mem = Memory("/data")
temps = Array(mem, "temperatures", dtype=np.float32)
```

## Testing Approach

### Unit Tests
- GoogleTest for C++ (`tests/test_*.cpp`)
- pytest for Python (`tests/test_*.py`)
- Each structure has dedicated test file

### Integration Tests  
- `test_lockfree_comprehensive.cpp`: Multi-threaded stress testing
- `test_stress.cpp`: High contention scenarios
- `test_aba_problem.cpp`: ABA problem verification
- `test_memory_boundaries.cpp`: Memory safety validation
- `test_failure_recovery.cpp`: Process crash recovery

### Test Isolation
Tests use unique shared memory names (often with PID) to avoid conflicts:
```cpp
std::string shm_name = "/test_" + std::to_string(getpid());
```

## Common Development Tasks

### Adding a New Data Structure
1. Define binary format in SPECIFICATION.md
2. Implement C++ version in `cpp/include/zeroipc/`
3. Add comprehensive tests in `cpp/tests/test_<structure>.cpp`
4. Implement Python version in `python/zeroipc/`
5. Add Python tests in `python/tests/`
6. Create interop tests in `interop/`

### Debugging Lock-Free Issues
1. Run `test_lockfree_comprehensive` with thread sanitizer
2. Check `test_aba_problem` for ABA vulnerabilities
3. Verify memory ordering in CAS loops
4. Use `test_stress` to reproduce race conditions

### Performance Profiling
```bash
cd cpp/build
./benchmarks/benchmark_queue --benchmark_repetitions=10
./benchmarks/benchmark_array
```

## Key Gotchas

1. **Table Entry Limits**: Default 64 entries fills quickly in tests. Use larger table types when needed.

2. **String Names**: Limited to 31 characters (32 with null terminator). Longer names throw `std::invalid_argument`.

3. **Type Consistency**: No runtime type checking - users must ensure type agreement across languages.

4. **Memory Fences**: Lock-free operations require careful fence placement for visibility.

5. **Shared Memory Cleanup**: Failed tests may leave `/dev/shm/test_*` files. Clean with:
   ```bash
   rm -f /dev/shm/test_* /dev/shm/zeroipc_*
   ```

## Recent Changes

- Switched to minimal metadata design (v1.0) - only name/offset/size stored
- Implemented proper lock-free queue with CAS loops
- Added comprehensive test suite for concurrency edge cases
- Standardized table naming: `tableN` where N = entry count
- Fixed ABA problems in lock-free structures