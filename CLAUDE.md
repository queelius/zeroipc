# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**ZeroIPC** is a high-performance cross-language shared memory IPC library that enables zero-copy data sharing between processes. The project implements a binary format specification allowing different languages to share data structures through POSIX shared memory without serialization overhead.

## Architecture

### Parallel Language Implementations
- `cpp/` - Modern C++23 header-only template library (primary development focus)
- `go/` - Go 1.21+ implementation with generics and CLI tool
- `python/` - Pure Python implementation using mmap and numpy
- `c/` - Pure C99 implementation with static library
- `interop/` - Cross-language integration tests (C++/Python/Go)
- `SPECIFICATION.md` - Binary format all implementations must follow

### Core Design Principles
- **Minimal metadata**: Table stores only name, offset, and size (no type information)
- **Duck typing**: Users specify types at runtime (Python), compile time (C++), or via generics (Go)
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

# Run specific test by name
cd build && ctest -R "QueueTest" --output-on-failure
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

### Go
```bash
cd go
go test ./zeroipc/...      # Run tests
go build -o zeroipc ./cmd/zeroipc  # Build CLI tool
go run ./cmd/interop       # Run Go cross-language interop
```

### Cross-Language Testing
```bash
cd interop
./test_interop.sh          # C++ writes, Python reads
./test_reverse_interop.sh  # Python writes, C++ reads
./test_go_cpp_interop.sh   # Go/C++ specific interop
```

## C++ Implementation Details

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

Predefined sizes: `table1` through `table4096` (default `table` = `table64`). Use `table_impl<name_size, max_entries>` for custom configuration.

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
- `mutex`: Binary semaphore wrapper for mutual exclusion
- `once`: One-time initialization primitive (call_once semantics)
- `event`: AutoReset/ManualReset event for thread signaling
- `monitor`: Condition variable + mutex for predicate-based waiting
- `rwlock`: Read-Write lock (multiple concurrent readers OR exclusive writer)
- `signal<T>`: Reactive signal with version tracking for change detection

**Codata & Computational Structures:**
- `future<T>`: Asynchronous computation results across processes
- `lazy<T>`: Deferred computations with automatic memoization
- `stream<T>`: Reactive data flows with FRP operators (map, filter, fold)
- `channel<T>`: CSP-style synchronous/buffered message passing

### Structure Creation Pattern

All languages can create and discover structures:

```cpp
// C++ creates
zeroipc::memory mem("/data", 10*1024*1024);
zeroipc::array<float> temps(mem, "temperatures", 1000);

// Python discovers and reads
mem = Memory("/data")
temps = Array(mem, "temperatures", dtype=np.float32)

// Go discovers and reads
mem, _ := zeroipc.OpenMemory("/data", 10*1024*1024)
temps, _ := zeroipc.OpenArray[float32](mem, "temperatures")
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

- **FAST** (<100ms): unit tests, no threading — run on every commit
- **MEDIUM** (<5s): multi-threaded, lock-free correctness — included in default suite
- **SLOW** (>5s): full sync validation — CI only
- **STRESS** (>30s): exhaustive, 32+ threads — manual only

### Test Frameworks
- GoogleTest for C++ (`cpp/tests/test_*.cpp`) — 26 test files
- pytest for Python (`python/tests/test_*.py`)
- `go test` for Go (`go/zeroipc/`)

### Test Isolation
Tests use unique shared memory names (often with PID) to avoid conflicts:
```cpp
std::string shm_name = "/test_" + std::to_string(getpid());
```

### test_config.h

Include `cpp/tests/test_config.h` and use `zeroipc::test::TestTiming::` constants for delays (`THREAD_START_DELAY`, `THREAD_SYNC_DELAY`), timeouts (`SHORT_TIMEOUT`, `MEDIUM_TIMEOUT`), and iterations (`FAST_ITERATIONS`, `STRESS_ITERATIONS`). Environment variables: `ZEROIPC_TEST_MODE=FAST|STRESS`, `CI=1` (3x timeout multiplier).

## Common Development Tasks

### Adding a New Data Structure
1. Define binary format in SPECIFICATION.md
2. Implement C++ version in `cpp/include/zeroipc/`
3. Add comprehensive tests in `cpp/tests/test_<structure>.cpp`
4. Implement Python version in `python/zeroipc/`
5. Add Python tests in `python/tests/`
6. Implement Go version in `go/zeroipc/`
7. Add Go tests in `go/zeroipc/`
8. Create cross-language interop tests in `interop/`

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

### v2.2.0 - Go Implementation & Concurrency Fixes (March 2026)
- **Go Implementation**: Full Go 1.21+ implementation with generics in `go/zeroipc/`
  - CLI tool (`go/cmd/zeroipc/`) and cross-language interop tool (`go/cmd/interop/`)
- **Critical Lock-Free Fixes**: Queue head/tail confusion, Stack ABA vulnerability, proper fence placement
- **Documentation Overhaul**: Consolidated README, removed stale docs

### Previous Releases
- **v2.1**: Added Mutex, Once, Event, Monitor, RWLock, Signal<T> sync primitives (C++ and Python)
- **v2.0**: 200x test speedup, test categorization (FAST/MEDIUM/SLOW/STRESS), CLI feature parity for all 16 structures
- **v1.0**: Minimal metadata design, lock-free foundations, initial sync primitives (Semaphore, Barrier, Latch), codata structures