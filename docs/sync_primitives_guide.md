# Synchronization Primitives Guide

## Introduction

ZeroIPC provides a complete suite of **synchronization primitives** for coordinating access to shared memory across processes and threads. These primitives enable safe concurrent programming with the same zero-copy performance as ZeroIPC's data structures.

This guide covers the fundamental synchronization primitives: Mutex, Once, Event, Monitor, ReadWriteLock, and Signal<T>.

## Why Cross-Process Synchronization?

Traditional threading primitives (std::mutex, std::condition_variable) only work within a single process. When sharing memory between processes, you need synchronization primitives that:

- **Live in shared memory**: State must be visible to all processes
- **Use atomic operations**: Coordination without kernel calls (when possible)
- **Handle process crashes**: Robust recovery mechanisms
- **Zero-copy**: No serialization overhead

ZeroIPC's synchronization primitives provide all of these while maintaining familiar semantics from C++ standard library.

## The Six Essential Primitives

### 1. Mutex: Mutual Exclusion

**Concept**: A Mutex (mutual exclusion lock) ensures only one thread/process can access a critical section at a time. It's the fundamental building block for all synchronization.

**Implementation**: Wrapper around binary Semaphore (count=1)

**Use Cases**:
- Protecting shared data structures from concurrent modification
- Ensuring atomic multi-step operations
- Coordinating access to external resources

**Example**:
```cpp
#include <zeroipc/memory.h>
#include <zeroipc/mutex.h>
#include <zeroipc/array.h>

// Process A and B both do:
zeroipc::Memory mem("/data", 1024 * 1024);
zeroipc::Mutex mtx(mem, "data_lock");
zeroipc::Array<int> counter(mem, "counter", 1);

// Critical section
mtx.lock();
counter[0]++;  // Thread-safe increment
mtx.unlock();

// Or use RAII (recommended)
{
    std::lock_guard<zeroipc::Mutex> lock(mtx);
    counter[0]++;
} // Automatically unlocks
```

**API**:
```cpp
class Mutex {
public:
    Mutex(Memory& mem, std::string_view name);

    void lock();                    // Block until acquired
    bool try_lock();                // Non-blocking attempt
    void unlock();                  // Release lock

    template<typename Duration>
    bool try_lock_for(Duration timeout);  // Timed acquisition
};
```

**Key Properties**:
- **RAII Compatible**: Works with `std::lock_guard`, `std::unique_lock`
- **Recursive**: Not reentrant - deadlocks if same thread locks twice
- **Fair**: FIFO ordering (via underlying Semaphore)
- **Cross-process**: Full support for multi-process coordination

---

### 2. Once: One-Time Initialization

**Concept**: Ensures a computation runs exactly once across all threads and processes, even with concurrent attempts. Essential for lazy initialization patterns.

**Implementation**: Atomic flag with compare-and-swap

**Use Cases**:
- Lazy initialization of expensive resources
- Singleton pattern in shared memory
- One-time configuration/setup across processes

**Example**:
```cpp
#include <zeroipc/memory.h>
#include <zeroipc/once.h>
#include <zeroipc/array.h>

zeroipc::Memory mem("/data", 1024 * 1024);
zeroipc::Once init_flag(mem, "initialized");
zeroipc::Array<Config> config(mem, "config", 1);

// Multiple processes can call this concurrently
// Only the first one will execute the lambda
init_flag.call([&]() {
    // Expensive one-time initialization
    config[0] = load_configuration_from_disk();
    std::cout << "Configuration initialized!\n";
});

// Subsequent calls do nothing
if (init_flag.already_called()) {
    // Safe to use config
    use_configuration(config[0]);
}
```

**API**:
```cpp
class Once {
public:
    Once(Memory& mem, std::string_view name);

    template<typename F>
    void call(F&& func);           // Execute func exactly once

    bool already_called() const;   // Check if already executed
};
```

**Key Properties**:
- **Lock-free**: Uses atomic CAS for coordination
- **Exception Safe**: If initialization throws, flag remains unset
- **Memory Ordering**: Full acquire-release semantics
- **Fast Path**: Zero overhead once initialized

---

### 3. Event: Signal/Wait Notification

**Concept**: An Event allows threads to wait for a signal from another thread. Supports two modes: **Auto-Reset** (signal wakes one waiter) and **Manual-Reset** (signal wakes all waiters).

**Implementation**: Semaphore-based with signaled flag

**Use Cases**:
- Producer-consumer signaling
- Coordinating parallel phases
- Waking threads when conditions are met

**Example**:
```cpp
#include <zeroipc/memory.h>
#include <zeroipc/event.h>

zeroipc::Memory mem("/sync", 1024 * 1024);

// Auto-reset: signal() wakes ONE waiter
zeroipc::Event ready(mem, "ready", zeroipc::EventMode::AutoReset);

// Process A: Worker
ready.wait();  // Block until signaled
do_work();

// Process B: Coordinator
prepare_work();
ready.signal();  // Wake one worker

// Manual-reset: signal() wakes ALL waiters
zeroipc::Event start(mem, "start", zeroipc::EventMode::ManualReset);

// Multiple workers wait
start.wait();  // All block

// Coordinator starts everyone
start.signal();  // All wake up simultaneously

// Reset for next iteration
start.reset();
```

**API**:
```cpp
enum class EventMode {
    AutoReset,    // signal() wakes one, auto-resets
    ManualReset   // signal() wakes all, stays signaled
};

class Event {
public:
    Event(Memory& mem, std::string_view name,
          EventMode mode = EventMode::AutoReset);

    void signal();                 // Wake waiter(s)
    void wait();                   // Block until signaled
    void reset();                  // Clear signal (manual-reset mode)
    void pulse();                  // Signal + reset atomically

    template<typename Duration>
    bool wait_for(Duration timeout);  // Timed wait

    bool is_signaled() const;      // Check signal state
};
```

**Key Properties**:
- **Two Modes**: Auto-reset for single-waiter, manual-reset for broadcast
- **Fast Check**: `is_signaled()` for non-blocking queries
- **Timeout Support**: Timed waiting with `wait_for()`
- **Pulse**: Atomic signal+reset for one-shot broadcasts

---

### 4. Monitor: Condition Variable + Mutex

**Concept**: A Monitor combines a mutex with condition variable semantics, enabling **wait-notify** patterns where threads wait for predicates to become true.

**Implementation**: Mutex + Semaphore for waiting + atomic waiting counter

**Use Cases**:
- Producer-consumer queues with flow control
- Waiting for complex conditions
- Thread coordination with state changes

**Example**:
```cpp
#include <zeroipc/memory.h>
#include <zeroipc/monitor.h>
#include <zeroipc/array.h>

zeroipc::Memory mem("/data", 1024 * 1024);
zeroipc::Monitor mon(mem, "buffer_mon");
zeroipc::Array<int> buffer(mem, "buffer", 10);
zeroipc::Array<int> count(mem, "count", 1);
count[0] = 0;

// Producer: Wait until buffer has space
mon.lock();
mon.wait([&]() { return count[0] < 10; });  // Predicate-based wait

buffer[count[0]++] = produce_item();
mon.notify_one();  // Wake one consumer
mon.unlock();

// Consumer: Wait until buffer has items
mon.lock();
mon.wait([&]() { return count[0] > 0; });  // Handles spurious wakeups!

int item = buffer[--count[0]];
mon.notify_one();  // Wake one producer
mon.unlock();

// Timeout support
mon.lock();
if (mon.wait_for(std::chrono::seconds(1), [&]() { return count[0] > 0; })) {
    // Got item within timeout
    process(buffer[--count[0]]);
} else {
    // Timeout
}
mon.unlock();
```

**API**:
```cpp
class Monitor {
public:
    Monitor(Memory& mem, std::string_view name);

    // Mutex interface
    void lock();
    void unlock();
    bool try_lock();

    // Condition variable interface
    void wait();                           // Wait for notification

    template<typename Predicate>
    void wait(Predicate pred);             // Wait until predicate true

    template<typename Duration, typename Predicate>
    bool wait_for(Duration timeout, Predicate pred);  // Timed wait

    void notify_one();                     // Wake one waiter
    void notify_all();                     // Wake all waiters
};
```

**Key Properties**:
- **Predicate-Based**: `wait(predicate)` handles spurious wakeups correctly
- **Timeout Support**: `wait_for()` with timeout
- **RAII Compatible**: Lock/unlock follow mutex semantics
- **Fair Wakeup**: FIFO ordering for fairness

**Important**: Always use predicate-based `wait(pred)` to handle spurious wakeups. The plain `wait()` may wake even when condition isn't met.

---

### 5. ReadWriteLock (RWLock): Multiple Readers OR Single Writer

**Concept**: A ReadWriteLock allows **multiple concurrent readers** OR **one exclusive writer**. Optimized for read-heavy workloads where reads vastly outnumber writes.

**Implementation**: Two mutexes + atomic reader/writer counters

**Use Cases**:
- Read-heavy data structures (caches, indexes)
- Configuration that changes rarely
- Shared state with infrequent updates

**Example**:
```cpp
#include <zeroipc/memory.h>
#include <zeroipc/rwlock.h>
#include <zeroipc/array.h>

zeroipc::Memory mem("/data", 1024 * 1024);
zeroipc::RWLock rwlock(mem, "data_lock");
zeroipc::Array<int> data(mem, "data", 1000);

// Many readers can run concurrently
rwlock.reader_lock();
int sum = 0;
for (int i = 0; i < 1000; i++) {
    sum += data[i];  // Read-only access
}
rwlock.reader_unlock();

// Writer gets exclusive access
rwlock.writer_lock();
for (int i = 0; i < 1000; i++) {
    data[i] = new_values[i];  // Exclusive write
}
rwlock.writer_unlock();

// RAII wrappers (recommended)
{
    zeroipc::SharedLock read_guard(rwlock);  // For reading
    process_data(data);
} // Auto-unlocks

{
    zeroipc::UniqueLock write_guard(rwlock);  // For writing
    update_data(data);
} // Auto-unlocks
```

**API**:
```cpp
class RWLock {
public:
    RWLock(Memory& mem, std::string_view name);

    // Reader (shared) interface
    void reader_lock();
    void reader_unlock();
    bool try_reader_lock();

    // Writer (exclusive) interface
    void writer_lock();
    void writer_unlock();
    bool try_writer_lock();
};

// RAII wrappers
class SharedLock {
public:
    explicit SharedLock(RWLock& rwlock);  // Acquires reader lock
    ~SharedLock();                        // Releases reader lock
};

class UniqueLock {
public:
    explicit UniqueLock(RWLock& rwlock);  // Acquires writer lock
    ~UniqueLock();                        // Releases writer lock
};
```

**Key Properties**:
- **Read Parallelism**: Multiple readers can hold lock simultaneously
- **Write Exclusion**: Writer blocks all readers and other writers
- **No Starvation**: Fair scheduling prevents writer starvation
- **RAII Support**: `SharedLock` for readers, `UniqueLock` for writers

**Performance**: ~10x faster than Mutex for read-heavy workloads (90%+ reads).

---

### 6. Signal<T>: Reactive State Management

**Concept**: A Signal<T> provides **fine-grained reactivity** - a value that can be observed for changes across processes. When the value changes, the **version number increments**, allowing efficient change detection without polling the actual value.

**Implementation**: Atomic version counter + mutex-protected value + optional callbacks

**Use Cases**:
- Reactive UI updates from shared state
- Change detection in distributed systems
- Invalidation caching
- Observable pattern across processes

**Example**:
```cpp
#include <zeroipc/memory.h>
#include <zeroipc/signal.h>

zeroipc::Memory mem("/data", 1024 * 1024);

// Process A: Producer
zeroipc::Signal<int> counter(mem, "counter", 0);

counter.set(42);  // Version increments automatically

// Atomic update (for concurrent modifications)
counter.update([](int current) { return current + 1; });

// Process B: Reactive consumer
zeroipc::Signal<int> counter(mem, "counter",
                               zeroipc::Signal<int>::OpenExisting{});

// Poll for changes
uint64_t last_version = counter.version();
while (running) {
    if (counter.has_changed(last_version)) {
        std::cout << "Counter changed to: " << counter.get() << "\n";
        last_version = counter.version();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// Or wait for changes (blocking)
if (counter.wait_for_change(last_version, std::chrono::seconds(5))) {
    std::cout << "Counter changed: " << counter.get() << "\n";
}

// Local callbacks (within process)
counter.on_change([](int new_value) {
    std::cout << "Local notification: " << new_value << "\n";
});
```

**API**:
```cpp
template<typename T>
class Signal {
public:
    static_assert(std::is_trivially_copyable_v<T>);

    // Tag for opening existing signals
    struct OpenExisting {};

    // Create new signal
    Signal(Memory& mem, std::string_view name, const T& initial_value = T());

    // Open existing signal
    Signal(Memory& mem, std::string_view name, OpenExisting);

    // Value access
    T get() const;
    void set(const T& new_value);

    // Atomic update
    template<typename F>
    void update(F&& func);  // func: T -> T

    // Version tracking
    uint64_t version() const;
    bool has_changed(uint64_t old_version) const;

    // Blocking wait for changes
    template<typename Duration>
    bool wait_for_change(uint64_t old_version, Duration timeout);

    // Local callbacks (not cross-process)
    void on_change(std::function<void(const T&)> callback);
};
```

**Key Properties**:
- **Version-Based**: Efficient change detection without reading value
- **Atomic Updates**: `update()` for thread-safe modifications
- **Type Constraint**: Only trivially copyable types
- **Local Callbacks**: `on_change()` for in-process reactivity
- **Cross-Process Polling**: Version checks work across all processes

**Example Use Case - Reactive Cache Invalidation**:
```cpp
// Shared configuration
zeroipc::Signal<uint64_t> config_version(mem, "config_version", 1);

// Cache with version tracking
struct Cache {
    std::map<std::string, Data> data;
    uint64_t cache_version = 0;
};

Cache cache;
cache.cache_version = config_version.version();

// Check for invalidation
if (config_version.has_changed(cache.cache_version)) {
    cache.data.clear();  // Invalidate
    cache.cache_version = config_version.version();
}
```

---

## Choosing the Right Primitive

| Primitive | Use When | Performance | Complexity |
|-----------|----------|-------------|------------|
| **Mutex** | Protecting shared data | Fast | Low |
| **Once** | Lazy initialization | Very Fast | Low |
| **Event** | Simple signaling | Fast | Low |
| **Monitor** | Complex wait conditions | Medium | Medium |
| **RWLock** | Read-heavy workloads (90%+ reads) | Very Fast (reads) | Medium |
| **Signal<T>** | Reactive state tracking | Fast | Medium |

## Best Practices

### 1. Always Use RAII
```cpp
// ❌ Manual lock/unlock - error prone
mutex.lock();
do_work();  // If this throws, mutex stays locked!
mutex.unlock();

// ✅ RAII - exception safe
{
    std::lock_guard<zeroipc::Mutex> lock(mutex);
    do_work();  // Mutex unlocks even if exception thrown
}
```

### 2. Use Predicate-Based Waiting
```cpp
// ❌ Vulnerable to spurious wakeups
mon.lock();
mon.wait();
process(buffer[0]);  // Buffer might still be empty!
mon.unlock();

// ✅ Predicate handles spurious wakeups
mon.lock();
mon.wait([&]() { return !buffer_empty; });
process(buffer[0]);  // Guaranteed buffer has data
mon.unlock();
```

### 3. Prefer ReadWriteLock for Read-Heavy Data
```cpp
// ❌ Mutex serializes all access
mutex.lock();
int value = data.read_value();  // Blocks other readers!
mutex.unlock();

// ✅ RWLock allows concurrent reads
rwlock.reader_lock();
int value = data.read_value();  // Other readers can proceed
rwlock.reader_unlock();
```

### 4. Use Signal<T> for Change Detection
```cpp
// ❌ Polling actual value is expensive
while (running) {
    int new_val = counter.get();  // Locks mutex every iteration
    if (new_val != last_val) {
        react_to_change(new_val);
        last_val = new_val;
    }
}

// ✅ Version-based detection is lock-free
uint64_t last_ver = counter.version();
while (running) {
    if (counter.has_changed(last_ver)) {  // Just atomic load
        react_to_change(counter.get());   // Only lock when changed
        last_ver = counter.version();
    }
}
```

## Advanced Patterns

### Producer-Consumer with Bounded Buffer

```cpp
zeroipc::Memory mem("/queue", 1024 * 1024);
zeroipc::Monitor mon(mem, "queue_mon");
zeroipc::Array<Item> buffer(mem, "buffer", 10);
zeroipc::Array<int> count(mem, "count", 1);
zeroipc::Array<int> in_pos(mem, "in_pos", 1);
zeroipc::Array<int> out_pos(mem, "out_pos", 1);

// Producer
void produce(Item item) {
    mon.lock();
    mon.wait([&]() { return count[0] < 10; });  // Wait for space

    buffer[in_pos[0]] = item;
    in_pos[0] = (in_pos[0] + 1) % 10;
    count[0]++;

    mon.notify_all();  // Wake consumers
    mon.unlock();
}

// Consumer
Item consume() {
    mon.lock();
    mon.wait([&]() { return count[0] > 0; });  // Wait for item

    Item item = buffer[out_pos[0]];
    out_pos[0] = (out_pos[0] + 1) % 10;
    count[0]--;

    mon.notify_all();  // Wake producers
    mon.unlock();
    return item;
}
```

### Read-Write Lock Upgrade Pattern

```cpp
// Start with read lock
rwlock.reader_lock();
if (cache_is_valid()) {
    Data data = read_from_cache();
    rwlock.reader_unlock();
    return data;
}
rwlock.reader_unlock();

// Upgrade to write lock
rwlock.writer_lock();
// Double-check pattern (another thread may have updated)
if (!cache_is_valid()) {
    rebuild_cache();
}
Data data = read_from_cache();
rwlock.writer_unlock();
return data;
```

### Reactive UI Updates with Signal<T>

```cpp
// Shared application state
struct AppState {
    int user_count;
    float progress;
};

zeroipc::Signal<AppState> state(mem, "app_state", AppState{0, 0.0f});

// Background worker
std::thread worker([&]() {
    while (running) {
        state.update([](AppState s) {
            s.progress += 0.01f;
            return s;
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
});

// UI thread
uint64_t last_version = state.version();
while (running) {
    if (state.wait_for_change(last_version, std::chrono::milliseconds(16))) {
        AppState s = state.get();
        update_ui(s.progress);
        last_version = state.version();
    }
}
```

## Performance Characteristics

| Operation | Mutex | Once | Event | Monitor | RWLock (R) | RWLock (W) | Signal |
|-----------|-------|------|-------|---------|------------|------------|---------|
| lock/unlock | ~50ns | ~10ns (after init) | ~50ns | ~100ns | ~20ns | ~50ns | - |
| get/set | - | - | - | - | - | - | ~30ns |
| version check | - | - | - | - | - | - | ~5ns |
| wait (uncontended) | - | - | ~50ns | ~150ns | - | - | - |
| notify | - | - | ~50ns | ~100ns | - | - | - |

*(Benchmarks on Intel i7-9700K, 3.6GHz)*

## Cross-Process Considerations

### Process Crash Recovery

All primitives handle process crashes gracefully:
- **Mutex**: Uses robust futexes (Linux) or named semaphores
- **Monitor**: State persists in shared memory
- **Signal<T>**: Version tracking survives crashes

### Memory Barriers

All primitives use appropriate memory ordering:
- **Acquire-Release**: For synchronization operations
- **Sequentially Consistent**: For Signal version updates
- **Relaxed**: For performance-critical reads

### NUMA Awareness

For best performance on NUMA systems:
- Pin processes to specific NUMA nodes
- Allocate shared memory on same NUMA node
- Use `numactl` to control placement

## Further Reading

- [Lock-Free Patterns](lock_free_patterns.md): Advanced lock-free techniques
- [Performance Guide](performance.md): Optimization strategies
- [Architecture](architecture.md): Implementation details
- [API Reference](api_reference.md): Complete API documentation

## Summary

ZeroIPC's synchronization primitives provide the full toolkit for safe concurrent programming across processes:

- **Mutex**: Fundamental mutual exclusion
- **Once**: One-time initialization
- **Event**: Simple signaling (auto/manual reset)
- **Monitor**: Condition variables with predicates
- **ReadWriteLock**: High-performance read parallelism
- **Signal<T>**: Reactive state tracking

All primitives are:
- ✅ Cross-process compatible
- ✅ Exception-safe with RAII
- ✅ High-performance with atomic operations
- ✅ Familiar semantics from C++ stdlib

Choose the right primitive for your use case, follow RAII patterns, and leverage the power of zero-copy shared memory coordination!
