# POSIX Shared Memory Data Structures

## Overview

This library provides a comprehensive set of lock-free data structures designed for high-performance inter-process communication (IPC) using POSIX shared memory. Each structure is carefully crafted for use in demanding applications like n-body simulations, real-time systems, and distributed computing.

## Learning Path

We recommend following this progression to understand the architecture:

### 1. Foundations
- **[shm_atomic](shm_atomic.md)** - Atomic operations in shared memory
- **[Memory Model](memory_model.md)** - Understanding memory ordering and coherency

### 2. Basic Structures  
- **[shm_array](shm_array.md)** - Fixed-size contiguous arrays
- **[shm_queue](shm_queue.md)** - Lock-free FIFO queue
- **[shm_stack](shm_stack.md)** - Lock-free LIFO stack

### 3. Advanced Structures
- **[shm_ring_buffer](shm_ring_buffer.md)** - Circular buffer for streaming
- **[shm_object_pool](shm_object_pool.md)** - Memory pool for dynamic allocation
- **[shm_hash_map](shm_hash_map.md)** - Concurrent hash table

### 4. Specialized Components
- **[shm_simd](shm_simd.md)** - SIMD operations for vectorized computing
- **[shm_graph](shm_graph.md)** - Graph structures for network algorithms

## Design Principles

### Lock-Free Algorithms
All data structures use atomic operations to ensure:
- **Progress Guarantee**: At least one thread always makes progress
- **No Deadlocks**: Impossible by design
- **Scalability**: Performance scales with CPU cores
- **Low Latency**: No kernel transitions for locks

### Memory Efficiency
- **Cache-Line Alignment**: Prevents false sharing
- **Compact Layout**: Minimal metadata overhead
- **Zero-Copy**: Direct memory access between processes
- **Stack Allocation**: No heap fragmentation

### Type Safety
- **Compile-Time Checks**: `requires std::is_trivially_copyable_v<T>`
- **RAII**: Automatic resource management
- **Strong Types**: No void* or unsafe casts

## Use Case: N-Body Simulation

For a large-scale n-body simulation with multiple processes:

```cpp
// Shared memory segment for simulation
posix_shm shm("/nbody_sim", 100 * 1024 * 1024);  // 100MB

// Particle data
struct Particle {
    float pos[3];
    float vel[3];
    float mass;
};

// Shared data structures
shm_array<Particle> particles(shm, "particles", 1000000);
shm_atomic<uint64_t> iteration(shm, "iteration", 0);
shm_queue<uint32_t> work_queue(shm, "work_queue", 10000);
shm_atomic<double> total_energy(shm, "total_energy", 0.0);

// Process 1: Physics simulation
void physics_worker() {
    while (running) {
        uint32_t particle_id;
        if (work_queue.dequeue(particle_id)) {
            compute_forces(particles[particle_id]);
            update_position(particles[particle_id]);
        }
    }
}

// Process 2: Work distribution
void scheduler() {
    while (running) {
        for (uint32_t i = 0; i < particles.size(); ++i) {
            work_queue.enqueue(i);
        }
        iteration.fetch_add(1);
        wait_for_completion();
    }
}

// Process 3: Monitoring
void monitor() {
    while (running) {
        uint64_t iter = iteration.load();
        double energy = total_energy.load();
        std::cout << "Iteration: " << iter 
                  << ", Energy: " << energy << std::endl;
        sleep(1);
    }
}
```

## Performance Characteristics

| Structure | Insert | Remove | Access | Space | Use Case |
|-----------|--------|--------|--------|-------|----------|
| shm_atomic | O(1) | O(1) | O(1) | O(1) | Counters, flags |
| shm_array | - | - | O(1) | O(n) | Fixed collections |
| shm_queue | O(1)* | O(1)* | - | O(n) | Task distribution |
| shm_stack | O(1)* | O(1)* | - | O(n) | Undo/redo, recursion |
| shm_ring_buffer | O(1) | O(1) | O(1) | O(n) | Data streaming |
| shm_object_pool | O(1)* | O(1)* | O(1) | O(n) | Dynamic allocation |

\* Amortized, may retry under contention

## Thread Safety Guarantees

All structures provide:
- **Thread-safe** read/write operations
- **Process-safe** through shared memory
- **Signal-safe** for async signal handlers (with care)
- **Fork-safe** with proper initialization

## Memory Requirements

### Overhead per Structure
- Metadata table entry: 64 bytes
- Cache line alignment: up to 63 bytes padding
- Structure header: 64-128 bytes (structure-dependent)

### Example Calculation
For 1 million particles (24 bytes each):
- Data: 24 MB
- shm_table: 4 KB (default size)
- shm_array header: 64 bytes
- Total: ~24.004 MB

## Building Your Own Structures

To create custom lock-free structures:

1. **Inherit from shm_span**
```cpp
template<typename T>
class my_structure : public shm_span<T, posix_shm> {
    // Your implementation
};
```

2. **Use atomic operations**
```cpp
std::atomic<size_t> index;
index.fetch_add(1, std::memory_order_relaxed);
```

3. **Ensure trivially copyable**
```cpp
static_assert(std::is_trivially_copyable_v<T>);
```

4. **Register in shm_table**
```cpp
table->add(name, offset, size, sizeof(T), count);
```

## Testing and Validation

Each structure includes:
- Unit tests with Catch2
- Stress tests for concurrency
- Cross-process validation
- Memory leak detection
- Performance benchmarks

Run tests:
```bash
make test
./build/tests/posix_shm_tests
```

## Contributing

We welcome contributions! Areas of interest:
- Additional lock-free structures
- Performance optimizations
- Platform-specific enhancements
- Documentation improvements
- Example applications

## References

### Books
- "The Art of Multiprocessor Programming" - Herlihy & Shavit
- "C++ Concurrency in Action" - Anthony Williams
- "Perfbook" - Paul McKenney

### Papers
- "Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue Algorithms" - Michael & Scott
- "Hazard Pointers: Safe Memory Reclamation for Lock-Free Objects" - Michael
- "A Pragmatic Implementation of Non-Blocking Linked-Lists" - Harris

### Online Resources
- [Lock-Free Programming](https://preshing.com/archives/)
- [C++ Memory Model](https://en.cppreference.com/w/cpp/atomic/memory_order)
- [Intel Threading Building Blocks](https://github.com/oneapi-src/oneTBB)