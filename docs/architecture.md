# Architecture

## System Architecture Overview

```
┌──────────────────────────────────────────────────────────┐
│                     Application Layer                      │
│  ┌─────────────┐  ┌──────────────┐  ┌──────────────┐    │
│  │ Simulation  │  │   Renderer   │  │   Analytics  │    │
│  └─────────────┘  └──────────────┘  └──────────────┘    │
└───────────────────────┬──────────────────────────────────┘
                        │
┌───────────────────────▼──────────────────────────────────┐
│                Data Structure Layer                       │
│  ┌────────┐ ┌────────┐ ┌──────┐ ┌──────┐ ┌────────┐   │
│  │ Array  │ │ Queue  │ │ Stack │ │ Map  │ │  Set   │   │
│  └────────┘ └────────┘ └──────┘ └──────┘ └────────┘   │
└───────────────────────┬──────────────────────────────────┘
                        │
┌───────────────────────▼──────────────────────────────────┐
│                  Foundation Layer                         │
│  ┌──────────┐  ┌──────────┐  ┌─────────────────────┐   │
│  │  Memory  │  │   Table  │  │  Language Bindings  │   │
│  └──────────┘  └──────────┘  └─────────────────────┘   │
└───────────────────────┬──────────────────────────────────┘
                        │
┌───────────────────────▼──────────────────────────────────┐
│                    POSIX API Layer                        │
│           shm_open, mmap, shm_unlink, futex              │
└───────────────────────┬──────────────────────────────────┘
                        │
┌───────────────────────▼──────────────────────────────────┐
│                    Kernel Space                           │
│          Virtual Memory, Page Tables, IPC                │
└──────────────────────────────────────────────────────────┘
```

## Memory Layout

### Shared Memory Segment Structure

```
Offset   Size     Component
──────────────────────────────────────────
0x0000   16       Table Header
0x0010   Variable Metadata Table Entries
0xXXXX   Variable Data Structure 1
0xYYYY   Variable Data Structure 2
...
```

### Metadata Table Entry

```
Field    Size     Description
──────────────────────────────────────────
name     32       Structure identifier
offset   4        Byte offset in segment
size     4        Total size in bytes
```

## Component Design

### 1. Memory Management

**Responsibilities:**
- POSIX shared memory lifecycle
- Reference counting
- Memory mapping
- Automatic cleanup

**Key Design Decisions:**

1. **Automatic Cleanup**: Resources managed automatically
2. **Table Embedding**: Metadata stored in shared memory
3. **Runtime Configuration**: Table size determined at creation
4. **Language Independence**: Each implementation manages its own way

### 2. Table - Discovery System

**Responsibilities:**
- Name-to-offset mapping
- Dynamic structure discovery
- Allocation tracking
- Metadata storage

**Design Pattern: Service Locator**

- Register structures by name
- Discover structures at runtime
- Language-agnostic lookup

**Trade-offs:**
- Fixed maximum entries (runtime-configured)
- Linear search (simple, cache-friendly)
- No defragmentation (predictable performance)

**Memory Management: Stack/Bump Allocation**

The implementation uses **stack allocation** (also called bump allocation):

This means:
- ✅ **Simple**: O(1) allocation, no scanning
- ✅ **Predictable**: No fragmentation delays
- ❌ **No reclamation**: Erased structures leave permanent gaps
- ❌ **Memory leak**: Repeated create/delete exhausts memory

**Best Practices:**
1. **Initialize once**: Create all structures at startup
2. **Never erase**: Treat structures as permanent
3. **Use pools**: For dynamic needs, use `shm_object_pool`
4. **Size appropriately**: Allocate enough memory upfront

**Future Improvement:**
A free-list allocator could reuse gaps, but adds complexity and potential fragmentation issues.

### 3. Data Structure Implementations

#### Array - Contiguous Storage

**Memory Layout:**
```
[Element][Element][Element][Element]...
```

**Operations:**
- Direct indexing
- Fixed size (no dynamic growth)
- Zero-copy access

#### Queue - Lock-Free FIFO

**Memory Layout:**
```
[Header]
  head index
  tail index
  capacity
[Element][Element][Element]... (circular buffer)
```

**Properties:**
- Lock-free enqueue/dequeue using atomic operations
- Bounded capacity
- Wait-free progress for single producer/consumer

#### Stack - Lock-Free LIFO

**Memory Layout:**
```
[Header]
  top index
  capacity
[Element][Element][Element]...
```

**Properties:**
- Lock-free push/pop using CAS
- Bounded capacity
- ABA problem mitigation

#### Map - Hash Table

**Memory Layout:**
```
[Header]
  bucket count
  size
[Bucket][Bucket][Bucket]...
```

**Properties:**
- Linear probing for collision resolution
- Fixed bucket count
- Key-value storage

## Synchronization Strategies

### 1. Lock-Free Algorithms

**Used In:** Queue, Stack, Map operations

**Techniques:**
- Compare-And-Swap (CAS)
- Atomic operations
- Memory ordering semantics

**Benefits:**
- No kernel involvement
- Bounded latency
- Progress guarantee

### 2. Wait-Free Readers

**Used In:** Array, read-only operations

**Guarantee:** Readers never block

### 3. Memory Ordering

**Consistency Model:**
- Acquire-release semantics for synchronization
- Sequential consistency where needed
- Relaxed ordering for counters

## Language Implementation Strategies

### C++ Implementation

**Approach:**
- Template-based for compile-time optimization
- Zero-overhead abstractions
- RAII for resource management
- Type constraints for safety

### Python Implementation

**Approach:**
- Duck typing for flexibility
- NumPy integration for performance
- mmap for direct memory access
- Runtime type specification

## Performance Optimizations

### 1. Cache Line Alignment

**Strategy:** Align data structures to cache line boundaries (typically 64 bytes)

**Prevents:** False sharing between cores

### 2. Memory Access Patterns

**Strategy:** Sequential access patterns for prefetcher efficiency

**Benefit:** Maximizes memory bandwidth utilization

### 3. Bulk Operations

**Strategy:** Process multiple elements in single operation

**Benefit:** Amortizes overhead and improves throughput

### 4. Zero-Copy Design

**Strategy:** Direct memory access without serialization

**Benefit:** Eliminates copying overhead

## Error Handling Philosophy

### 1. Creation Failures

**Strategy:** Report errors during structure creation

**Rationale:** Fail fast at initialization

### 2. Operation Failures

**Strategy:** Return success/failure indicators

**Rationale:** Let caller decide on failure handling

### 3. Resource Exhaustion

**Strategy:** Graceful degradation

**Rationale:** System continues with reduced capacity

## Scalability Considerations

### Process Scaling

**Strategy:** Read-heavy optimization

```cpp
// Multiple readers, single writer pattern
class MRSW_Array {
    T* data;  // No synchronization for reads
    std::atomic<uint64_t> version;  // Version for consistency
};
```

### Memory Scaling

**Strategy:** Hierarchical structures

```cpp
// Two-level structure for large datasets
class LargeMap {
    shm_array<Bucket> buckets;
    shm_object_pool<Node> nodes;
};
```

### NUMA Awareness

```cpp
// Pin shared memory to NUMA node
void* addr = mmap(...);
mbind(addr, size, MPOL_BIND, &nodemask, ...);
```

## Security Considerations

### 1. Access Control

```cpp
// Set permissions on creation
int fd = shm_open(name, O_CREAT | O_RDWR, 0660);
```

### 2. Input Validation

```cpp
if (index >= capacity)
    throw std::out_of_range("...");
```

### 3. Resource Limits

```cpp
static constexpr size_t MAX_ALLOCATION = 1ULL << 30;  // 1GB
if (size > MAX_ALLOCATION)
    throw std::invalid_argument("...");
```

## Future Directions

### 1. Additional Language Support

- Rust implementation for safety guarantees
- Go implementation for concurrent systems
- Java implementation via JNI or Panama

### 2. Persistent Memory Support

- Intel Optane DC integration
- Battery-backed NVDIMM support
- Crash-consistent data structures

### 3. GPU Shared Memory

- CUDA unified memory integration
- OpenCL buffer sharing
- Heterogeneous computing support

### 4. Distributed Shared Memory

- RDMA support for cluster computing
- Network-transparent operations
- Coherence protocols

## Design Principles Summary

1. **Zero-Overhead Abstraction**: Pay only for what you use
2. **Lock-Free Where Possible**: Minimize contention
3. **Cache-Conscious**: Optimize for memory hierarchy
4. **Fail-Safe**: Graceful degradation over crashes
5. **Discoverable**: Named structures for flexibility
6. **Composable**: Build complex from simple
7. **Type-Safe**: Compile-time constraints
8. **RAII**: Automatic resource management