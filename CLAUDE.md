# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**ZeroIPC** is a high-performance cross-language shared memory IPC library that enables zero-copy data sharing between processes. The project implements a binary format specification allowing different languages to share data structures through POSIX shared memory without serialization overhead.

## Architecture

### Parallel Language Implementations
- `cpp/` - Modern C++23 header-only template library (primary development focus)
- `go/` - Go 1.21+ implementation with generics and CLI tool
- `python/` - Pure Python implementation using mmap and numpy, with optional C FFI backend
- `c/` - Pure C99 implementation: static library `libzeroipc.a` plus shared library `libzeroipc_ffi.so` (FFI for Python)
- `interop/` - Cross-language integration tests (C++/Python/Go)
- `docs/` - MkDocs site sources (Single Machine Thesis, Design Philosophy)
- `.papermill/` - Paper project (state, prior art survey, outline)
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

# Run tests. Test registration is in cpp/CMakeLists.txt, not the top-level CMake
cd build/cpp && ctest --output-on-failure       # Default: fast + medium (~2 min)
cmake --build build --target test_fast          # Fast tests only (~30 sec)
cmake --build build --target test_default       # Same as default (fast + medium)
cmake --build build --target test_ci            # CI mode: all except stress (~10 min)
cmake --build build --target test_all           # Full suite including stress (~30 min)

# Run specific test categories using CTest labels (must be from build/cpp/)
cd build/cpp && ctest -L fast --output-on-failure       # FAST: <100ms, core functionality
cd build/cpp && ctest -L medium --output-on-failure     # MEDIUM: <5s, multi-threaded
cd build/cpp && ctest -L slow --output-on-failure       # SLOW: >5s, full sync tests
cd build/cpp && ctest -L stress --output-on-failure     # STRESS: >30s, exhaustive
cd build/cpp && ctest -L lockfree --output-on-failure   # All lock-free structure tests
cd build/cpp && ctest -L sync --output-on-failure       # All synchronization primitive tests

# Run specific test by name
cd build/cpp && ctest -R "queue_test" --output-on-failure
```

### C
```bash
cd c
make            # Build both libzeroipc.a (static) and libzeroipc_ffi.so (FFI shared)
make shared     # Build only libzeroipc_ffi.so
make test       # Run tests
make clean      # Clean artifacts
```

The shared library `libzeroipc_ffi.so` is a thin, stateless C11 atomics layer for Python's `_cffi.py` module. It compiles from a single file (`c/src/ffi.c`) with no dependencies beyond `stdatomic.h`, `string.h`, and `sched.h` (`sched_yield` in the bounded spin loops). Self-contained by design so Python can use real cross-process atomics without the C library having any other linkage.

### Python
```bash
cd python
make install-dev  # Set up dev environment with all dependencies
make test         # Run tests
make test-cov     # Run with coverage report
make lint         # Run linting (ruff, mypy)
make format       # Format code (black, isort)
```

**C FFI backend (v2.3.0+).** When `libzeroipc_ffi.so` is found, Python's Queue and Stack delegate `push`/`pop`/`top`/`size` to C11 atomic operations via ctypes, giving true cross-process MPMC safety. Without it, Python falls back to pure-Python `struct.pack_into` (SPSC-only across processes, MPMC-only within one interpreter via `threading.Lock`). The fallback is graceful: same API, different guarantees.

Library search order in `_cffi.py`:
1. `ZEROIPC_FFI_LIB` env var (set to `none` to disable, set to a path to use that library)
2. `python/zeroipc/libzeroipc_ffi.so` (bundled install)
3. `c/libzeroipc_ffi.so` (in-tree development)
4. System `find_library("zeroipc_ffi")`

Verify FFI is loaded: `python -c "from zeroipc import _cffi; print(_cffi.AVAILABLE)"`. The MPMC integration tests in `python/tests/test_cffi.py` use `multiprocessing` to spawn real producer/consumer processes and assert no lost or duplicated items.

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
./test_queue_interop.sh    # Cross-language queue exercises
./test_data_types.sh       # Type matrix interop
```

(`test_three_way_interop.sh` was removed in v2.3.0 because it depended on the deleted C core API.)

### Documentation Site
```bash
mkdocs build               # Generate static site to ./site/
mkdocs serve               # Live preview at http://localhost:8000
mkdocs gh-deploy           # Push to GitHub Pages
```

The site is intentionally minimal: 3 pages (landing, Single Machine Thesis, Design Philosophy). Material theme with light/dark toggle. Source files live in `docs/`.

## Lock-Free Algorithms

Two primary algorithms are used across all four language implementations:

### Vyukov bounded MPMC queue (Queue)

Per-slot sequence numbers, monotonically increasing head/tail (NOT modular indices), one CAS per push or pop. Reference: `cpp/include/zeroipc/queue.h` and `c/src/queue.c`. Key invariants:

- `seq[i]` is initialized to `i`
- Push CAS-advances `tail`, then writes data, then publishes `seq[slot] = tail + 1`
- Pop CAS-advances `head`, then reads data, then recycles `seq[slot] = head + capacity`
- Slot diff `(int32_t)(seq - tail)` uses signed arithmetic to handle uint32 wraparound
- Capacity MUST be a power of two (2026-07-10 spec amendment): the slot mapping `counter % capacity` is only continuous across the 2^32 counter wraparound when capacity divides 2^32. Creators round requested capacities up (`Queue(mem, name, 100)` gives `capacity() == 128`); open paths reject non-power-of-two capacities. Wraparound regression tests seed head/tail near UINT32_MAX and stream across the boundary (`WraparoundAt2To32` in each language's queue tests)

### 4-state CAS stack (Stack)

Per-slot state machine: `EMPTY(0) -> WRITING(1) -> READY(2) -> READING(3) -> EMPTY(0)`. Reference: `cpp/include/zeroipc/stack.h`. Key invariants:

- Top reservation (CAS on the top index) is decoupled from slot ownership (CAS on per-slot state)
- This decoupling eliminates the ABA vulnerability the older single-CAS design had
- ALL slot-state spin loops are bounded (MAX_SPINS = 10000, yielding each iteration): `push`, `pop`, and `top` bail out instead of hanging when a crashed peer leaves a slot stuck. On bail-out, push/pop best-effort-undo their top reservation (single CAS on the top index) so no item is silently dropped, then return failure. All three operations are therefore best-effort: they can fail spuriously if a peer died mid-operation. Deterministic tests simulate the ghost by poking the state array: `cpp/tests/test_crash_safety.cpp`, `test_crashed_peer` in `c/tests/test_queue_stack.c`, `go/zeroipc/crash_test.go`, `python/tests/test_crash_safety.py`

When changing either algorithm, the change must be applied consistently across all four languages AND `c/src/ffi.c` (which has its own copies for the FFI Python uses).

### Table / Memory Configuration

The C++ `Memory` class takes `max_entries` as a runtime parameter (default 64). Construct larger tables explicitly when tests need many structures:

```cpp
zeroipc::Memory mem("/test", size, /*max_entries=*/256);
```

The Table layout is fixed: 32-byte header (`magic`, `version`, `entry_count`, `max_entries`, `memory_size`, `next_offset`) followed by 48-byte entries (`name[32]`, `offset:u64`, `size:u64`).

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
- GoogleTest for C++ (`cpp/tests/test_*.cpp`), 26 test files
- pytest for Python (`python/tests/test_*.py`), including `test_cffi.py` for the C FFI backend with multi-process MPMC tests
- `go test` for Go (`go/zeroipc/`)
- C unit tests (`c/tests/test_*.c`) via `make test`

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
cd build/cpp/benchmarks
./benchmark_queue
./benchmark_stack
./benchmark_array
```

These are hand-rolled `iostream` benchmarks, not Google Benchmark. They have no flag interface and run once per invocation. Wrap in a shell loop if you need repetitions.

## Key Gotchas

1. **Table Entry Limits**: Default 64 entries fills quickly in tests. Pass a larger `max_entries` to `Memory(name, size, max_entries=...)` (see Lock-Free Algorithms / Table Configuration above).

2. **String Names**: Limited to 31 characters (32 with null terminator). Longer names throw `std::invalid_argument`.

3. **Type Consistency**: No runtime type checking - users must ensure type agreement across languages.

4. **Memory Fences**: Lock-free operations require careful fence placement for visibility.

5. **Shared Memory Cleanup**: Failed tests may leave `/dev/shm/test_*` files. Clean with:
   ```bash
   rm -f /dev/shm/test_* /dev/shm/zeroipc_*
   ```

## Recent Changes

### v2.3.0 (April 2026)
- **C FFI backend for Python**: `c/src/ffi.c` plus `python/zeroipc/_cffi.py`. Python Queue and Stack delegate atomic operations to C11 via ctypes when `libzeroipc_ffi.so` is loaded. True cross-process MPMC. Pure-Python fallback (SPSC) when the .so is absent.
- **Cross-language concurrency fixes**: Vyukov queue and 4-state stack consistent across all four languages. Map insert uses `CAS(OCCUPIED -> INSERTING)` for exclusive update access. Future/Channel waiter-count CAS leak fixed.
- **C core API removed**: `c/src/core.c` (750 lines) deleted; standalone `queue.c`/`stack.c` are the single source. Avoids the parallel-implementation drift bug class.
- **Spec corrections**: Queue/Stack headers documented as `uint32_t` (matching all impls). Table header field renamed `reserved` to `max_entries`, all four languages now write the actual value.
- **Repo cleanup**: 18,000+ lines removed (bloated docs site, stale venue submissions, packaging files, tracked binaries). MkDocs site rebuilt minimally with thesis + design philosophy.

### Previous Releases
- **v2.2**: Full Go implementation with generics, CLI tool, cross-language interop. Critical lock-free fixes (queue head/tail, stack ABA).
- **v2.1**: Added Mutex, Once, Event, Monitor, RWLock, Signal<T> sync primitives (C++ and Python).
- **v2.0**: 200x test speedup, test categorization (FAST/MEDIUM/SLOW/STRESS), CLI feature parity for all 16 structures.
- **v1.0**: Minimal metadata design, lock-free foundations, initial sync primitives, codata structures.