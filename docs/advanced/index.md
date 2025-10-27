# Advanced Topics

Advanced concepts and techniques for expert ZeroIPC users.

## Topics

### [Codata Programming](codata.md)

Understanding codata and computational structures:

- What is codata?
- Futures and promises
- Lazy evaluation
- Infinite streams
- Reactive programming
- CSP-style channels

Codata represents **computations over time** rather than values in space, enabling powerful abstractions like async/await across processes.

### [Lock-Free Patterns](lock-free-patterns.md)

Deep dive into lock-free programming:

- Compare-And-Swap (CAS) operations
- ABA problem and solutions
- Memory ordering models
- Progress guarantees
- Lock-free data structures
- Performance characteristics

Learn how ZeroIPC implements lock-free queues, stacks, and maps for high-performance concurrent access.

### [Memory Ordering](memory-ordering.md)

Understanding C++ memory ordering:

- Sequential consistency
- Acquire-release semantics
- Relaxed ordering
- Memory fences
- Happens-before relationships
- Cross-process synchronization

Master the subtleties of memory ordering for correct lock-free implementations.

### [Custom Structures](custom-structures.md)

Creating your own ZeroIPC structures:

- Designing binary formats
- Implementing in C++
- Implementing in Python
- Adding to the specification
- Testing cross-language compatibility
- Contributing back

## Preview: Codata Programming

Traditional data structures store **values**:
```cpp
Array<int> values(mem, "data", 100);  // 100 integers stored in memory
```

Codata structures represent **computations**:
```cpp
Future<Result> result(mem, "calc");  // A result that will exist in the future
Stream<Event> events(mem, "stream"); // An infinite sequence of events
Lazy<Value> value(mem, "lazy");      // A value computed on-demand
```

### Why Codata Matters

Codata enables:

1. **Async/Await Across Processes**
   ```cpp
   // Process A
   Future<double> result(mem, "pi_calculation");
   result.set_value(calculate_pi(1000000));
   
   // Process B
   Future<double> result(mem, "pi_calculation", true);
   double pi = result.get();  // Waits if not ready
   ```

2. **Reactive Event Processing**
   ```cpp
   Stream<Event> raw(mem, "raw");
   auto filtered = raw.filter(mem, "important", 
       [](Event& e) { return e.priority > 5; });
   auto transformed = filtered.map(mem, "alerts",
       [](Event& e) { return Alert{e}; });
   ```

3. **Lazy Evaluation and Caching**
   ```cpp
   Lazy<ExpensiveResult> cached(mem, "result");
   // First call: computes and caches
   auto r1 = cached.value();
   // Subsequent calls: returns cached value
   auto r2 = cached.value();  // Same instance
   ```

## Preview: Lock-Free Patterns

ZeroIPC uses lock-free algorithms for all concurrent structures. Here's how lock-free enqueue works:

```cpp
template<typename T>
bool Queue<T>::enqueue(const T& value) {
    uint64_t current_tail, next_tail;
    
    do {
        // 1. Load current tail (can be stale)
        current_tail = tail_.load(std::memory_order_relaxed);
        
        // 2. Calculate next position
        next_tail = (current_tail + 1) % capacity_;
        
        // 3. Check if full
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false;  // Queue full
        }
        
        // 4. Try to claim this slot
    } while (!tail_.compare_exchange_weak(
        current_tail, 
        next_tail,
        std::memory_order_relaxed,
        std::memory_order_relaxed
    ));
    
    // 5. Write data (we own this slot now)
    data_[current_tail] = value;
    
    // 6. Ensure write is visible to dequeuers
    std::atomic_thread_fence(std::memory_order_release);
    
    return true;
}
```

**Key points:**
- No locks, no blocking
- CAS loop retries on contention
- Memory fences ensure ordering
- Safe for multiple producers

## Preview: Memory Ordering

Different memory orders have different guarantees:

```cpp
// Relaxed: No ordering guarantees, fastest
value.store(42, std::memory_order_relaxed);

// Release: Previous writes visible to acquire loads
data_ready.store(true, std::memory_order_release);

// Acquire: Subsequent reads see previous releases
if (data_ready.load(std::memory_order_acquire)) {
    use_data();
}

// Sequential consistency: Total ordering, slowest
counter.fetch_add(1, std::memory_order_seq_cst);
```

For shared memory, you need to carefully choose ordering to ensure:
1. **Correctness** - No races or undefined behavior
2. **Performance** - Not stricter than necessary
3. **Cross-process** - Works across process boundaries

## When to Read Advanced Topics

These topics are for:

- **Codata**: When building reactive systems or need async/await
- **Lock-Free**: When implementing custom structures or debugging races
- **Memory Ordering**: When optimizing performance or ensuring correctness
- **Custom Structures**: When ZeroIPC doesn't provide what you need

## Prerequisites

Before diving into advanced topics:

- ✓ Completed the [Tutorial](../tutorial/index.md)
- ✓ Understand [Architecture](../architecture/index.md)
- ✓ Comfortable with C++ templates (for C++ users)
- ✓ Understand multi-threading basics
- ✓ Familiar with the [API Reference](../api/index.md)

## Next Steps

Choose your path:

- **[Codata Programming](codata.md)** - Computational structures
- **[Lock-Free Patterns](lock-free-patterns.md)** - Concurrent algorithms
- **[Memory Ordering](memory-ordering.md)** - Synchronization details
- **[Custom Structures](custom-structures.md)** - Extend ZeroIPC
