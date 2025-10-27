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

# Run tests - optimized test suite with categorization
cd build && ctest --output-on-failure           # Default: fast + medium (~2 min)
cmake --build build --target test_fast          # Fast tests only (~30 sec)
cmake --build build --target test_default       # Same as default (fast + medium)
cmake --build build --target test_ci            # CI mode: all except stress (~10 min)
cmake --build build --target test_all           # Full suite including stress (~30 min)

# Run specific test categories using CTest labels
ctest -L fast --output-on-failure               # FAST: <100ms, core functionality
ctest -L medium --output-on-failure             # MEDIUM: <5s, multi-threaded
ctest -L slow --output-on-failure               # SLOW: >5s, full sync tests
ctest -L stress --output-on-failure             # STRESS: >30s, exhaustive (disabled by default)
ctest -L lockfree --output-on-failure           # All lock-free structure tests
ctest -L sync --output-on-failure               # All synchronization primitive tests
ctest -L unit --output-on-failure               # Unit tests only
ctest -L integration --output-on-failure        # Integration tests only

# Run specific test executable
./build/tests/test_queue
./build/tests/test_lockfree_comprehensive
./build/tests/test_semaphore
./build/tests/test_barrier

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

**Traditional Data Structures:**
- `memory`: POSIX shared memory wrapper with reference counting
- `table`: Metadata registry for dynamic structure discovery
- `array<T>`: Fixed-size contiguous array with atomic operations
- `queue<T>`: Lock-free MPMC circular buffer using CAS
- `stack<T>`: Lock-free stack with CAS push/pop
- `ring<T>`: High-performance ring buffer for streaming
- `map<K,V>`: Lock-free hash map with linear probing
- `set<T>`: Lock-free hash set for unique elements
- `pool<T>`: Object pool with free list management

**Synchronization Primitives:**
- `semaphore`: Cross-process counting/binary semaphore with wait/signal
- `barrier`: Multi-process synchronization barrier with generation counter
- `latch`: One-shot countdown synchronization primitive

**Codata & Computational Structures:**
- `future<T>`: Asynchronous computation results across processes
- `lazy<T>`: Deferred computations with automatic memoization
- `stream<T>`: Reactive data flows with FRP operators (map, filter, fold)
- `channel<T>`: CSP-style synchronous/buffered message passing

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

### Test Suite Optimization (v2.0)

**Performance:** 200x faster - reduced from 20+ minutes to <2 minutes for default suite

**Key Features:**
- Test categorization using `test_config.h` for parameterized timing constants
- CTest labels for selective test execution (FAST/MEDIUM/SLOW/STRESS)
- Custom CMake targets for convenient workflows
- Environment variables for test mode control (`ZEROIPC_TEST_MODE`)

### Test Categories

**FAST Tests** (<100ms each, ~30 seconds total):
- Core functionality validation
- No intentional delays
- Minimal threading
- Single-process unit tests
- Run on every commit

**MEDIUM Tests** (<5s each, ~2 minutes total):
- Multi-threaded integration tests (4-8 threads)
- Lock-free algorithm correctness
- Minimal delays (1-10ms total)
- Default suite includes fast + medium

**SLOW Tests** (>5s each):
- Full synchronization validation
- Real-world timing scenarios
- Large-scale thread orchestration
- Disabled by default, run in CI

**STRESS Tests** (>30s each):
- 32+ threads, millions of operations
- ABA problem detection
- Exhaustive validation
- Disabled by default, run manually

### Unit Tests
- GoogleTest for C++ (`tests/test_*.cpp`) - 20 test files
- pytest for Python (`tests/test_*.py`)
- Each structure has dedicated test file
- Uses `test_config.h` for timing constants

### Integration Tests
- `test_lockfree_comprehensive.cpp`: Multi-threaded stress testing (MEDIUM)
- `test_stress.cpp`: High contention scenarios (STRESS, disabled by default)
- `test_aba_problem.cpp`: ABA problem verification (MEDIUM)
- `test_memory_boundaries.cpp`: Memory safety validation (MEDIUM)
- `test_failure_recovery.cpp`: Process crash recovery (MEDIUM)
- `test_semaphore.cpp`: Semaphore synchronization (SLOW, optimized variant)
- `test_barrier.cpp`: Barrier synchronization (SLOW, optimized variant)
- `test_latch.cpp`: Latch countdown tests (MEDIUM)

### Test Isolation
Tests use unique shared memory names (often with PID) to avoid conflicts:
```cpp
std::string shm_name = "/test_" + std::to_string(getpid());
```

### Running Tests by Category
```bash
# Fast development loop
cmake --build build --target test_fast

# Before commit
cmake --build build --target test_default

# CI pipeline
cmake --build build --target test_ci

# Full validation (including stress)
cmake --build build --target test_all

# Specific features
ctest -L lockfree --output-on-failure
ctest -L sync --output-on-failure
```

### Using test_config.h

The `cpp/tests/test_config.h` file provides parameterized timing constants for all tests:

```cpp
#include "test_config.h"

// Use standard timing constants
TEST(SemaphoreTest, BasicAcquireRelease) {
    using namespace zeroipc::test;

    // Thread synchronization delays
    std::this_thread::sleep_for(TestTiming::THREAD_START_DELAY);  // 1ms
    std::this_thread::sleep_for(TestTiming::THREAD_SYNC_DELAY);   // 2ms

    // Timeout values
    bool acquired = sem.acquire_for(TestTiming::SHORT_TIMEOUT);   // 50ms

    // Iteration counts based on test category
    for (int i = 0; i < TestTiming::FAST_ITERATIONS; i++) {  // 100
        // Fast test operations
    }
}

// Check test mode
if (strcmp(TestTiming::test_mode(), "STRESS") == 0) {
    iterations = TestTiming::STRESS_ITERATIONS;  // 10000
    num_threads = TestTiming::STRESS_THREADS;    // 32
}

// CI multiplier for longer timeouts
auto timeout = TestTiming::MEDIUM_TIMEOUT * TestTiming::ci_multiplier();
```

**Environment Variables:**
```bash
# Set test mode (affects iteration counts)
export ZEROIPC_TEST_MODE=FAST    # Minimal iterations
export ZEROIPC_TEST_MODE=STRESS  # Maximum iterations

# CI mode (3x timeout multiplier)
export CI=1
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

### v2.0 - Test Suite Optimization & CLI Enhancement (October 2024)
- **Test Performance**: 200x speedup (20+ min → <2 min default suite)
  - Implemented test categorization (FAST/MEDIUM/SLOW/STRESS)
  - Created `test_config.h` with parameterized timing constants
  - Added CTest labels and custom targets for selective execution
  - Fixed ProducerConsumer deadlock in test_semaphore.cpp
- **CLI Tool Enhancement**: Added complete feature parity for all 16 data structures
  - `zeroipc` now supports Ring, Map, Set, Pool, Channel inspection
  - Added structure-specific commands for all synchronization primitives
  - Enhanced help text and error handling
  - Tool expanded from 359KB to 482KB with full functionality
- **Documentation**: Comprehensive testing strategy documentation
  - Created `docs/TESTING_STRATEGY.md` with best practices
  - Updated `docs/TEST_OPTIMIZATION_ACTION_PLAN.md`
  - Enhanced `docs/TDD_BEST_PRACTICES_LOCKFREE.md`

### v1.0 - Minimal Metadata & Lock-Free Foundations
- Switched to minimal metadata design - only name/offset/size stored
- Implemented proper lock-free queue with CAS loops
- Added comprehensive test suite for concurrency edge cases
- Standardized table naming: `tableN` where N = entry count
- Fixed ABA problems in lock-free structures
- Added synchronization primitives (Semaphore, Barrier, Latch)
- Implemented codata structures (Future, Lazy, Stream, Channel)