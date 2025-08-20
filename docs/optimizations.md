# Performance Optimizations Guide

## Current Performance Characteristics

Our benchmarks show **zero overhead** for shared memory reads:
- ~2.3ns per array access (identical to heap arrays)
- Lock-free operations in 10-15ns range
- No cache line false sharing with proper alignment

## Optimization Opportunities

### 1. Memory Prefetching for Sequential Access

For simulation workloads with predictable access patterns:

```cpp
template<typename T>
class shm_array_prefetch : public shm_array<T> {
    void prefetch_next(size_t current_idx, size_t distance = 8) {
        if (current_idx + distance < this->size()) {
            __builtin_prefetch(&(*this)[current_idx + distance], 0, 1);
        }
    }
};
```

**Benefit**: 20-40% improvement for streaming access patterns

### 2. SIMD-Friendly Alignment

Ensure arrays are aligned for AVX-512:

```cpp
template<typename T>
class shm_array_simd : public shm_array<T> {
    static constexpr size_t SIMD_ALIGN = 64;  // AVX-512 alignment
    
    void* allocate_aligned(size_t size) {
        size_t offset = shm.get_current_offset();
        size_t aligned = (offset + SIMD_ALIGN - 1) & ~(SIMD_ALIGN - 1);
        return shm.allocate_at(aligned, size);
    }
};
```

**Benefit**: Enables vectorized operations, 4-8x speedup for bulk operations

### 3. Cache-Conscious Data Structures

#### Packed Arrays for Structs of Arrays (SoA)

Transform Array of Structs (AoS) to Struct of Arrays (SoA):

```cpp
// Instead of:
struct Particle {
    float x, y, z;
    float vx, vy, vz;
};
shm_array<Particle> particles;

// Use:
struct ParticlesSoA {
    shm_array<float> x, y, z;
    shm_array<float> vx, vy, vz;
};
```

**Benefit**: Better cache utilization, enables SIMD on individual components

### 4. Huge Pages Support

Enable 2MB/1GB pages for large simulations:

```cpp
void* mmap_huge(size_t size) {
    return mmap(NULL, size, 
                PROT_READ | PROT_WRITE,
                MAP_SHARED | MAP_HUGETLB | MAP_HUGE_2MB,
                fd, 0);
}
```

**Benefit**: Reduces TLB misses by 512x, 5-15% overall improvement

### 5. NUMA-Aware Allocation

For multi-socket systems:

```cpp
class numa_shm : public posix_shm {
    void bind_to_node(int node) {
        numa_tonode_memory(base_addr, size, node);
    }
    
    void interleave_nodes() {
        numa_set_interleave_mask(numa_all_nodes_ptr);
    }
};
```

**Benefit**: 2-3x improvement for NUMA-sensitive workloads

### 6. Lock-Free Improvements

#### Relaxed Memory Ordering Where Safe

```cpp
// For statistics/counters that don't need strict ordering:
counter.fetch_add(1, std::memory_order_relaxed);  // Faster than seq_cst
```

#### Padding to Prevent False Sharing

```cpp
struct alignas(64) CacheLineCounter {
    std::atomic<uint64_t> value;
    char padding[56];  // Ensure exclusive cache line
};
```

**Benefit**: 10-50x improvement under contention

### 7. Batch Operations

Amortize overhead with bulk operations:

```cpp
template<typename T>
class shm_array_batch : public shm_array<T> {
    void write_batch(size_t start, std::span<const T> values) {
        // Single bounds check
        if (start + values.size() > this->size()) 
            throw std::out_of_range("Batch write out of bounds");
        
        // Optimized memcpy for trivially copyable types
        std::memcpy(&(*this)[start], values.data(), 
                    values.size() * sizeof(T));
    }
};
```

**Benefit**: 5-10x faster than individual writes

### 8. Compile-Time Optimizations

#### Link-Time Optimization (LTO)

```cmake
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
target_compile_options(posix_shm INTERFACE -flto)
```

#### Profile-Guided Optimization (PGO)

```bash
# Generate profile
g++ -fprofile-generate ...
./simulation_benchmark
# Use profile
g++ -fprofile-use ...
```

**Benefit**: 10-20% improvement from better inlining and branch prediction

### 9. Memory Pool with Size Classes

For dynamic allocation patterns:

```cpp
template<size_t... Sizes>
class shm_size_class_pool {
    std::tuple<shm_object_pool<Sizes>...> pools;
    
    template<size_t Size>
    auto& get_pool() {
        return std::get<shm_object_pool<Size>>(pools);
    }
};
```

**Benefit**: O(1) allocation without fragmentation

### 10. Zero-Copy String Views

For string data in shared memory:

```cpp
class shm_string_view {
    size_t offset;
    size_t length;
    
    std::string_view get(const posix_shm& shm) const {
        return std::string_view(
            static_cast<const char*>(shm.get_base_addr()) + offset,
            length
        );
    }
};
```

**Benefit**: No string allocation overhead

## Simulation-Specific Optimizations

### Particle Simulations

```cpp
// Use SoA layout
// Enable SIMD operations
// Prefetch next particles during force calculation
// Use spatial hashing for neighbor finding
```

### Time-Series Data

```cpp
// Ring buffer with power-of-2 size (faster modulo)
// Cache-aligned write position
// Bulk read/write operations
// Consider compression for historical data
```

### Graph Simulations

```cpp
// CSR format for sparse adjacency
// Cache-blocking for matrix operations
// Parallel edge iteration with atomic updates
// Consider vertex reordering for locality
```

## Benchmarking Recommendations

1. **Use `perf` for profiling**:
   ```bash
   perf record -e cache-misses,cache-references ./simulation
   perf report
   ```

2. **Monitor TLB misses**:
   ```bash
   perf stat -e dTLB-load-misses ./simulation
   ```

3. **Check false sharing**:
   ```bash
   perf c2c record ./simulation
   perf c2c report
   ```

4. **Measure memory bandwidth**:
   ```bash
   mbw 1000  # Memory bandwidth benchmark
   ```

## Implementation Priority

Based on typical simulation workloads:

1. **High Priority**: Cache-line alignment, SIMD alignment, batch operations
2. **Medium Priority**: Huge pages, prefetching, SoA layout
3. **Low Priority**: NUMA optimization (unless multi-socket), PGO

## Trade-offs to Consider

- **Complexity vs Performance**: Simple code is often fast enough
- **Memory vs Speed**: Padding wastes memory but improves speed
- **Latency vs Throughput**: Batching improves throughput but adds latency
- **Portability vs Optimization**: Platform-specific optimizations reduce portability

Remember: **Measure first, optimize second**. Profile your specific workload to identify actual bottlenecks before applying these optimizations.