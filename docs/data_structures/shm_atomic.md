# shm_atomic: Lock-Free Atomic Operations in Shared Memory

## Overview

`shm_atomic<T>` provides the fundamental building block for lock-free inter-process communication in POSIX shared memory. It wraps a single value of type `T` with atomic operations, enabling safe concurrent access from multiple processes without explicit locking.

## Why Start with Atomics?

In the hierarchy of concurrent data structures, atomic operations form the foundation:

1. **Hardware Level**: Modern CPUs provide atomic instructions (CAS, fetch-and-add, etc.)
2. **Language Level**: C++ `std::atomic` exposes these as a portable interface
3. **IPC Level**: `shm_atomic` extends this to shared memory between processes

For n-body simulations and other high-performance computing applications, atomic operations enable:
- Lock-free progress guarantees
- Minimal synchronization overhead
- Cache-friendly memory access patterns
- Scalable multi-process coordination

## Technical Design

### Memory Layout

```
[shm_table metadata]
[shm_atomic header]
  ├─ atomic<T> value
  └─ padding (cache line alignment)
```

### Key Features

- **Cache Line Alignment**: Prevents false sharing between processes
- **Memory Ordering**: Configurable memory ordering (relaxed, acquire, release, seq_cst)
- **Type Safety**: Compile-time checks for trivially copyable types
- **Zero Overhead**: Direct hardware atomic operations

## API Reference

### Construction

```cpp
// Create new atomic value in shared memory
posix_shm shm("/simulation", 1024 * 1024);
shm_atomic<uint64_t> counter(shm, "particle_count", 0);

// Open existing atomic value
shm_atomic<uint64_t> existing(shm, "particle_count");
```

### Basic Operations

```cpp
// Store (atomically write)
counter.store(1000);

// Load (atomically read)
uint64_t count = counter.load();

// Exchange (swap values atomically)
uint64_t old = counter.exchange(2000);

// Compare and swap (CAS)
uint64_t expected = 1000;
bool success = counter.compare_exchange_strong(expected, 2000);
```

### Arithmetic Operations (for integral types)

```cpp
// Atomic increment/decrement
counter.fetch_add(1);    // Returns old value
counter.fetch_sub(1);

// Atomic bitwise operations  
counter.fetch_and(0xFF);
counter.fetch_or(0x100);
counter.fetch_xor(0x200);

// Operators (return new value)
++counter;  // Pre-increment
counter++;  // Post-increment
counter += 5;
```

## Use Cases in N-Body Simulation

### 1. Global Particle Counter

```cpp
// Process 1: Particle generator
shm_atomic<size_t> total_particles(shm, "total_particles", 0);
for (int i = 0; i < batch_size; ++i) {
    create_particle();
    total_particles.fetch_add(1, std::memory_order_relaxed);
}

// Process 2: Monitor
while (running) {
    size_t count = total_particles.load(std::memory_order_relaxed);
    std::cout << "Particles: " << count << std::endl;
    sleep(1);
}
```

### 2. Synchronization Barrier

```cpp
shm_atomic<uint32_t> barrier(shm, "sync_barrier", 0);
const uint32_t num_processes = 4;

// Each process increments and waits
uint32_t my_turn = barrier.fetch_add(1) + 1;
if (my_turn == num_processes) {
    // Last process resets for next barrier
    barrier.store(0);
    // Wake others...
} else {
    // Wait for barrier completion
    while (barrier.load() != 0) {
        std::this_thread::yield();
    }
}
```

### 3. Lock-Free Statistics

```cpp
struct SimStats {
    shm_atomic<uint64_t> collisions;
    shm_atomic<double> total_energy;
    shm_atomic<uint64_t> iterations;
};

// Multiple processes update statistics concurrently
stats.collisions.fetch_add(local_collisions);
stats.iterations.fetch_add(1);

// Atomic floating-point requires CAS loop
double current = stats.total_energy.load();
double desired;
do {
    desired = current + local_energy;
} while (!stats.total_energy.compare_exchange_weak(current, desired));
```

### 4. State Machine Coordination

```cpp
enum SimState : uint32_t {
    INITIALIZING = 0,
    RUNNING = 1,
    PAUSED = 2,
    STOPPING = 3
};

shm_atomic<SimState> state(shm, "sim_state", INITIALIZING);

// Controller process
state.store(RUNNING);

// Worker processes
while (state.load() == RUNNING) {
    simulate_timestep();
}
```

## Performance Considerations

### Memory Ordering

Choose the weakest ordering that maintains correctness:

```cpp
// Relaxed: No synchronization, only atomicity
counter.fetch_add(1, std::memory_order_relaxed);  // Fastest

// Acquire-Release: Synchronizes with other atomic operations
flag.store(true, std::memory_order_release);
if (flag.load(std::memory_order_acquire)) { ... }

// Sequential Consistency: Total order across all threads
state.store(READY, std::memory_order_seq_cst);  // Slowest but safest
```

### Cache Coherency

- **False Sharing**: Ensure atomics in different cache lines
- **Contention**: Use relaxed ordering for high-frequency updates
- **NUMA**: Consider process-to-CPU pinning for large systems

### Lock-Free vs Wait-Free

`shm_atomic` operations are:
- **Lock-free**: At least one thread makes progress
- **Wait-free**: Every thread completes in bounded time (for most operations)

CAS loops are lock-free but not wait-free:
```cpp
// Lock-free but not wait-free
while (!atomic.compare_exchange_weak(expected, desired)) {
    expected = atomic.load();
}
```

## Integration with Other Data Structures

`shm_atomic` forms the basis for more complex lock-free structures:

1. **shm_queue**: Uses atomic head/tail pointers
2. **shm_stack**: Uses atomic top pointer  
3. **shm_object_pool**: Uses atomic free list
4. **shm_ring_buffer**: Uses atomic read/write indices

Example building a simple spinlock:
```cpp
class shm_spinlock {
    shm_atomic<bool> locked;
public:
    void lock() {
        bool expected = false;
        while (!locked.compare_exchange_weak(expected, true)) {
            expected = false;
            std::this_thread::yield();
        }
    }
    
    void unlock() {
        locked.store(false);
    }
};
```

## Common Pitfalls

### 1. ABA Problem

```cpp
// Thread 1 reads A
T* old_head = head.load();
// Thread 2: changes A→B→A
// Thread 1: CAS succeeds but state has changed!
head.compare_exchange_strong(old_head, new_head);
```

Solution: Use hazard pointers or epoch-based reclamation.

### 2. Memory Ordering Bugs

```cpp
// WRONG: data race
data = 42;
flag.store(true, std::memory_order_relaxed);  

// CORRECT: proper synchronization
data = 42;
flag.store(true, std::memory_order_release);
```

### 3. Overflow/Underflow

```cpp
shm_atomic<uint32_t> counter(shm, "counter");
// Check for overflow
uint32_t old = counter.load();
if (old == UINT32_MAX) {
    // Handle overflow
}
counter.fetch_add(1);
```

## Testing Strategies

### Unit Tests

```cpp
TEST_CASE("shm_atomic basic operations") {
    posix_shm shm("/test", 1024);
    shm_atomic<int> atom(shm, "test_atom", 42);
    
    REQUIRE(atom.load() == 42);
    REQUIRE(atom.exchange(100) == 42);
    REQUIRE(atom.load() == 100);
    
    int expected = 100;
    REQUIRE(atom.compare_exchange_strong(expected, 200));
    REQUIRE(atom.load() == 200);
}
```

### Stress Testing

```cpp
TEST_CASE("shm_atomic concurrent increments") {
    shm_atomic<uint64_t> counter(shm, "counter", 0);
    const int num_threads = 8;
    const int increments = 1000000;
    
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < increments; ++j) {
                counter.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    
    for (auto& t : threads) t.join();
    REQUIRE(counter.load() == num_threads * increments);
}
```

## Further Reading

- [C++ Memory Model](https://en.cppreference.com/w/cpp/atomic/memory_order)
- [Lock-Free Programming](https://preshing.com/20120612/an-introduction-to-lock-free-programming/)
- [The Art of Multiprocessor Programming](https://www.amazon.com/Art-Multiprocessor-Programming-Maurice-Herlihy/dp/0124159508)
- [Intel x86 Memory Ordering](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)

## Next: Building on Atomics

With `shm_atomic` as our foundation, we can build increasingly sophisticated data structures:

1. **[shm_queue](shm_queue.md)**: FIFO queue with atomic head/tail
2. **[shm_stack](shm_stack.md)**: LIFO stack with atomic top
3. **[shm_ring_buffer](shm_ring_buffer.md)**: Circular buffer for streaming
4. **[shm_object_pool](shm_object_pool.md)**: Lock-free memory pool

Each builds on atomic operations to provide safe, efficient inter-process communication for high-performance computing applications.