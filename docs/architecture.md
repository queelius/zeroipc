# Architecture Deep Dive

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
│  │ Array  │ │ Queue  │ │ Pool │ │ Ring │ │ Atomic │   │
│  └────────┘ └────────┘ └──────┘ └──────┘ └────────┘   │
└───────────────────────┬──────────────────────────────────┘
                        │
┌───────────────────────▼──────────────────────────────────┐
│                  Foundation Layer                         │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐              │
│  │ posix_shm│  │ shm_table│  │ shm_span │              │
│  └──────────┘  └──────────┘  └──────────┘              │
└───────────────────────┬──────────────────────────────────┘
                        │
┌───────────────────────▼──────────────────────────────────┐
│                    POSIX API Layer                        │
│         shm_open, mmap, shm_unlink, futex                │
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
0x0000   4        Reference Counter (atomic)
0x0004   4-424KB  Metadata Table (configurable)
0xXXXX   Variable Data Structure 1
0xYYYY   Variable Data Structure 2
...
```

### Metadata Table Entry

```cpp
struct entry {
    char name[MAX_NAME_SIZE];  // Unique identifier
    size_t offset;              // Byte offset in segment
    size_t size;                // Total size in bytes
    size_t elem_size;           // Element size (for arrays)
    size_t num_elem;            // Number of elements
    bool active;                // Entry valid flag
};
```

## Component Design

### 1. posix_shm - Memory Management

**Responsibilities:**
- POSIX shared memory lifecycle
- Reference counting
- Memory mapping
- Automatic cleanup

**Key Design Decisions:**

1. **RAII Pattern**: Automatic cleanup in destructor
2. **Reference Counting**: Last process cleans up
3. **Header Embedding**: Table stored in shared memory
4. **Template Configuration**: Compile-time table sizing

```cpp
template<typename TableType>
class posix_shm_impl {
    struct header {
        std::atomic<int> ref_count;
        TableType table;  // Embedded metadata
    };
};
```

### 2. shm_table - Discovery System

**Responsibilities:**
- Name-to-offset mapping
- Dynamic structure discovery
- Allocation tracking
- Metadata storage

**Design Pattern: Service Locator**

```cpp
// Register service
table->add("sensor_data", offset, size);

// Discover service
auto* entry = table->find("sensor_data");
```

**Trade-offs:**
- Fixed maximum entries (compile-time)
- Linear search (simple, cache-friendly)
- No defragmentation (predictable performance)

**Memory Management: Stack/Bump Allocation**

Our current implementation uses **stack allocation** (also called bump allocation):
```cpp
// Allocation strategy (simplified)
size_t allocate(size_t size) {
    size_t offset = sizeof(table) + get_total_allocated_size();
    total_allocated += size;
    return offset;
}
```

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

#### shm_array<T> - Contiguous Storage

**Memory Layout:**
```
[T][T][T][T][T][T][T][T]...
```

**Operations:**
- `operator[]`: Direct pointer arithmetic
- Iterators: Raw pointers
- No dynamic growth (fixed size)

#### shm_queue<T> - Lock-Free FIFO

**Memory Layout:**
```
[Header]
  atomic<size_t> head
  atomic<size_t> tail
  size_t capacity
[T][T][T][T]... (circular buffer)
```

**Algorithm: Lock-Free Enqueue**
```cpp
bool enqueue(T value) {
    size_t tail = tail_.load(acquire);
    size_t next = (tail + 1) % capacity;
    
    if (next == head_.load(acquire))
        return false;  // Full
        
    buffer[tail] = value;
    tail_.store(next, release);
    return true;
}
```

#### shm_object_pool<T> - Free List

**Memory Layout:**
```
[Header]
  atomic<uint32_t> free_head
  uint32_t next[capacity]
[T][T][T][T]... (object storage)
```

**Algorithm: Lock-Free Stack**
```cpp
uint32_t acquire() {
    uint32_t old_head = free_head.load();
    while (old_head != NULL) {
        uint32_t new_head = next[old_head];
        if (CAS(&free_head, old_head, new_head))
            return old_head;
    }
    return NULL;
}
```

#### shm_ring_buffer<T> - Bulk Operations

**Design Goals:**
- Optimize for throughput over latency
- Support non-consuming reads
- Allow overwrite mode for streaming

**Key Features:**
```cpp
// Bulk operations for efficiency
size_t push_bulk(span<T> values);
size_t pop_bulk(span<T> output);

// Streaming mode
void push_overwrite(T value);  // Drops oldest

// Analytics
uint64_t total_written();  // Monitor throughput
```

## Synchronization Strategies

### 1. Lock-Free Algorithms

**Used In:** Queue, Pool, Ring Buffer

**Techniques:**
- Compare-And-Swap (CAS)
- Fetch-And-Add (FAA)
- Memory ordering (acquire-release)

**Benefits:**
- No kernel involvement
- Bounded latency
- Progress guarantee

### 2. Wait-Free Readers

**Used In:** Array, Atomic

**Guarantee:** Readers never block

```cpp
T read(size_t index) {
    return data[index];  // Always wait-free
}
```

### 3. Memory Ordering

```cpp
// Writer
data[index] = value;
flag.store(true, memory_order_release);

// Reader  
if (flag.load(memory_order_acquire)) {
    auto value = data[index];  // Synchronized
}
```

## Template Metaprogramming

### Compile-Time Configuration

```cpp
template<size_t MaxNameSize, size_t MaxEntries>
class shm_table_impl {
    static constexpr size_t size_bytes() {
        return sizeof(entry) * MaxEntries + sizeof(size_t);
    }
};
```

**Benefits:**
- Zero runtime overhead
- Custom memory footprints
- Type safety

### Concept Constraints

```cpp
template<typename T>
    requires std::is_trivially_copyable_v<T>
class shm_array {
    // Ensures T can be safely shared
};
```

## Performance Optimizations

### 1. Cache Line Alignment

```cpp
struct alignas(64) CacheLine {
    std::atomic<uint64_t> value;
    char padding[56];
};
```

**Prevents:** False sharing between cores

### 2. Memory Prefetching

```cpp
for (size_t i = 0; i < n; i++) {
    __builtin_prefetch(&data[i + 8], 0, 1);
    process(data[i]);
}
```

**Benefit:** Hides memory latency

### 3. Bulk Operations

```cpp
// Bad: Multiple system calls
for (auto& item : items)
    queue.push(item);

// Good: Single operation
queue.push_bulk(items);
```

**Benefit:** Amortizes overhead

### 4. Branch-Free Code

```cpp
// Branch-free min
size_t min = a ^ ((a ^ b) & -(a > b));
```

**Benefit:** No pipeline stalls

## Error Handling Philosophy

### 1. Constructor Failures

```cpp
shm_array(shm, name, size) {
    if (!initialize())
        throw std::runtime_error("...");
}
```

**Rationale:** RAII requires successful construction

### 2. Operation Failures

```cpp
[[nodiscard]] bool enqueue(T value);
[[nodiscard]] std::optional<T> dequeue();
```

**Rationale:** Let caller decide on failure handling

### 3. Resource Exhaustion

```cpp
if (pool.full())
    return invalid_handle;  // Graceful degradation
```

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

### 1. Persistent Memory Support

```cpp
// Intel Optane DC support
class pmem_shm : public posix_shm {
    void persist() {
        pmem_persist(base_addr, size);
    }
};
```

### 2. GPU Shared Memory

```cpp
// CUDA unified memory integration
class cuda_shm {
    void* allocate(size_t size) {
        cudaMallocManaged(&ptr, size);
    }
};
```

### 3. Distributed Shared Memory

```cpp
// Network-transparent shared memory
class dsm_array : public shm_array {
    void sync() {
        rdma_write(remote_addr, local_addr, size);
    }
};
```

## Design Principles Summary

1. **Zero-Overhead Abstraction**: Pay only for what you use
2. **Lock-Free Where Possible**: Minimize contention
3. **Cache-Conscious**: Optimize for memory hierarchy
4. **Fail-Safe**: Graceful degradation over crashes
5. **Discoverable**: Named structures for flexibility
6. **Composable**: Build complex from simple
7. **Type-Safe**: Compile-time constraints
8. **RAII**: Automatic resource management