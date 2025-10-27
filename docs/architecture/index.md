# Architecture

Deep dive into ZeroIPC's internal design and implementation.

## Overview

ZeroIPC is built on three fundamental pillars:

1. **Binary Format Specification** - Language-agnostic memory layout
2. **Lock-Free Algorithms** - High-performance concurrent operations
3. **Minimal Metadata** - Maximum flexibility through duck typing

## Architecture Documents

### [Binary Format](binary-format.md)

Complete specification of the on-disk/in-memory format:

- Table header structure
- Table entry layout
- Data structure formats
- Alignment requirements
- Version compatibility

All language implementations must follow this specification exactly.

### [Design Principles](design-principles.md)

Core philosophy and trade-offs:

- **Language Equality** - No primary language
- **Minimal Overhead** - Only essential metadata
- **User Responsibility** - Trust users for type consistency
- **Zero Dependencies** - Each implementation stands alone
- **Binary Compatibility** - Strict format adherence

### [Lock-Free Implementation](lock-free.md)

How concurrent structures work without locks:

- Compare-And-Swap (CAS) operations
- Memory ordering and fences
- ABA problem prevention
- Progress guarantees
- Performance characteristics

### [Memory Layout](memory-layout.md)

How data is organized in shared memory:

- Segment structure
- Table organization
- Data structure placement
- Alignment and padding
- Growth and allocation

### [Testing Strategy](testing.md)

Comprehensive testing approach:

- Unit tests per structure
- Integration tests (cross-language)
- Stress tests (high concurrency)
- Property-based testing
- Test categorization (FAST/MEDIUM/SLOW/STRESS)

## Key Concepts

### Shared Memory Model

```
┌─────────────────────────────────────┐
│        POSIX Shared Memory          │
│         (/dev/shm/name)             │
├─────────────────────────────────────┤
│          Table Header               │
│  (magic, version, count, offset)    │
├─────────────────────────────────────┤
│         Table Entries               │
│ (name, offset, size) × max_entries  │
├─────────────────────────────────────┤
│       Data Structures               │
│  ┌──────────────────────────────┐  │
│  │      Array Header            │  │
│  │      Array Data              │  │
│  └──────────────────────────────┘  │
│  ┌──────────────────────────────┐  │
│  │      Queue Header            │  │
│  │      Queue Buffer            │  │
│  └──────────────────────────────┘  │
│  ...                                │
└─────────────────────────────────────┘
```

### Lock-Free Queue Algorithm

High-level overview of lock-free enqueue:

```
1. Load current tail index (relaxed)
2. Calculate next tail index
3. Check if queue full
4. CAS tail to next index
   - If successful: write data, fence
   - If failed: retry from step 1
```

This avoids locks while maintaining consistency across threads/processes.

### Type System

ZeroIPC uses **structural typing** (duck typing):

- **No type metadata** stored in shared memory
- **Users specify types** at access time
- **Binary layout** must match between languages
- **Size checking** only validation performed

Example:
```cpp
// C++ creates with int32_t
Array<int32_t> arr(mem, "data", 100);
```

```python
# Python must match with np.int32
arr = Array(mem, "data", dtype=np.int32)
```

If types mismatch in size, behavior is undefined!

## Performance Characteristics

### Operation Complexity

| Operation | Time Complexity | Lock-Free |
|-----------|----------------|-----------|
| Array index | O(1) | N/A |
| Array atomic op | O(1) | Yes |
| Queue enqueue | O(1) amortized | Yes |
| Queue dequeue | O(1) amortized | Yes |
| Stack push | O(1) | Yes |
| Stack pop | O(1) | Yes |
| Map insert | O(1) average | Yes |
| Map lookup | O(1) average | Yes |

### Memory Overhead

| Structure | Metadata Size | Notes |
|-----------|--------------|-------|
| Memory | 16 bytes + table | Table size = 16 + 40*max_entries |
| Array | 8 bytes | Just capacity |
| Queue | 24 bytes | Head, tail, capacity |
| Stack | 16 bytes | Top, capacity |
| Map | Variable | Bucket count dependent |

## Design Decisions

### Why No Type Metadata?

**Advantages:**
- Maximum flexibility across languages
- Smaller memory footprint
- Simpler implementation
- Faster allocation

**Trade-offs:**
- Users must track types externally
- No runtime type checking
- Potential for type mismatches

### Why No Deallocation?

**Advantages:**
- Simpler allocation (bump pointer)
- No fragmentation
- No free list management
- Faster creation

**Trade-offs:**
- Cannot reclaim individual structures
- Must delete entire segment to free memory
- Poor fit for dynamic workloads

### Why Lock-Free?

**Advantages:**
- No deadlocks
- No priority inversion
- Scalable across cores
- Progress guarantees

**Trade-offs:**
- More complex implementation
- May retry on high contention
- Larger instruction footprint
- Requires atomic operations

## Implementation Notes

### C++ Implementation

- **Header-only** for zero link-time overhead
- **Templates** for compile-time type safety
- **Concepts** for type constraints
- **RAII** for automatic cleanup

### Python Implementation

- **Pure Python** for portability
- **NumPy** for performance
- **mmap** for direct access
- **ctypes** for atomics

### C Implementation

- **Static library** for linking
- **Explicit management** (no RAII)
- **Minimal dependencies** (just POSIX)
- **Maximum portability**

## Next Steps

- **[Binary Format](binary-format.md)** - Detailed format specification
- **[Lock-Free Implementation](lock-free.md)** - Algorithm deep dive
- **[Testing Strategy](testing.md)** - How we ensure correctness
- **[Design Principles](design-principles.md)** - Philosophy and trade-offs
