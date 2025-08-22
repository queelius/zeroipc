# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a C++ library implementing shared memory data structures using POSIX shared memory, now renamed to **zeroipc** for zero-copy inter-process communication. The library provides efficient IPC through various lock-free data structures that can be shared across processes.

## Build Commands

```bash
# Configure the project with CMake (requires C++23)
cmake -B build .

# Build all targets
cmake --build build

# Run tests (requires GoogleTest)
cd build && ctest
# Or directly:
./build/tests/zeroipc_tests

# Build and run examples
./build/examples/basic_usage
```

## Modern C++ Features

The library uses C++23 features for efficiency and safety:
- **Concepts** for type constraints (`requires std::is_trivially_copyable_v<T>`)
- **std::string_view** for zero-allocation string handling
- **[[nodiscard]]** attributes for API safety
- **std::optional** for safe error handling
- **std::span** for safe array views
- **Designated initializers** for clear struct initialization
- **std::atomic** with explicit memory ordering for lock-free operations

## Core Architecture

The library is built on three foundational components:

1. **`zeroipc::memory`** (include/zeroipc.h): POSIX shared memory wrapper with reference counting and automatic cleanup.

2. **`zeroipc::table`** (include/table.h): Metadata manager that enables dynamic discovery of shared structures by name. Stores metadata at the beginning of shared memory segments.

3. **`zeroipc::span<T>`** (include/span.h): Base template class providing common functionality for shared memory data structures.

Data structures are laid out in shared memory as: `[table][structure1][structure2]...[structureN]`

## Data Structure Implementations

The library implements various lockfree data structures optimized for shared memory:
- `zeroipc::array<T>`: Contiguous array with atomic compare-and-swap operations
- `zeroipc::queue<T>`: Lock-free circular buffer using CAS for enqueue/dequeue
- `zeroipc::stack<T>`: Lock-free stack with CAS push/pop operations
- `zeroipc::set<T>`: Hash set implementation
- `zeroipc::map<K,V>`: Hash map with linear probing
- `zeroipc::bitset<N>`: Atomic bit operations
- `zeroipc::ring<T>`: High-performance ring buffer
- `zeroipc::pool<T>`: Object pool for memory reuse

All structures support dynamic creation/discovery via the table metadata system.

## Key Design Decisions

- **Lock-free Operations**: All concurrent data structures (queue, stack, etc.) use atomic compare-and-swap (CAS) operations for true lock-free behavior. The queue implementation specifically uses CAS loops in both enqueue and dequeue to prevent race conditions.

- **No Automatic Defragmentation**: Users manage memory layout to avoid blocking operations

- **Parameterized Table Sizes**: The metadata table is fully parameterized via templates:
  - All predefined tables use 32-char names (sufficient for most identifiers)
  - Granular size options with clear naming: `table{N}` where N = number of entries
    - `table1`: 1 entry (minimal)
    - `table2`: 2 entries (dual)
    - `table4`: 4 entries (quad)
    - `table8`: 8 entries (tiny)
    - `table16`: 16 entries (small)
    - `table32`: 32 entries (compact)
    - `table64`: 64 entries (standard, default as `table`)
    - `table128`: 128 entries (medium)
    - `table256`: 256 entries (large)
    - `table512`: 512 entries (xlarge)
    - `table1024`: 1024 entries (huge)
    - `table2048`: 2048 entries (xhuge)
    - `table4096`: 4096 entries (maximum)
  - Custom sizes: `table_impl<name_size, max_entries>`
  - **Important**: Names exceeding the table's max name size will throw `std::invalid_argument`
  - **Important**: When creating many structures, ensure the table size is sufficient. The 64-entry default limit can be hit quickly in tests or applications creating many named structures.

- **Exception-Based Error Handling**: Uses C++ exceptions for allocation failures and bounds checking

## Recent Insights and Gotchas

### Queue Lock-Free Implementation

The queue must use proper CAS operations for true lock-free behavior:

```cpp
// CORRECT - uses CAS
if (hdr->tail.compare_exchange_weak(current_tail, next_tail,
                                     std::memory_order_release,
                                     std::memory_order_acquire)) {
    // We own the slot, safe to write
    data_start()[current_tail] = value;
    return true;
}
```

### Table Entry Limits

When writing tests or applications that create many structures:

1. Count the total number of named structures you'll create
2. If exceeding 64, use a larger table type:

```cpp
zeroipc::memory<table256> shm("/my_shm", size);   // 256 entries
// or
zeroipc::memory<table1024> shm("/my_shm", size);  // 1024 entries
```

### String Views Over Fixed Arrays

The table uses fixed-size char arrays for names but provides string_view interfaces to avoid unnecessary allocations. Always prefer string_view when passing or returning names.

### Test Isolation

Tests should use unique shared memory names (e.g., including process ID) to avoid conflicts when tests run in parallel or shared memory isn't properly cleaned up.

## Testing

Tests use GoogleTest framework and are located in `tests/`. Key test files:
- `zeroipc_tests`: Main test suite with all unit and integration tests
- Test coverage includes single-threaded, multi-threaded, and multi-process scenarios
- Stress tests validate lock-free implementations under high contention