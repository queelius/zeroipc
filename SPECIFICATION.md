# ZeroIPC Shared Memory Format Specification v1.2

## Overview

ZeroIPC defines a binary format for data structures in shared memory that can be accessed from multiple processes and programming languages. This specification ensures binary compatibility between all implementations.

## Design Principles

1. **Minimal Metadata**: Store only what's necessary (name, offset, size)
2. **Language Agnostic**: No type information - users specify types
3. **Runtime Configurable**: Table size determined at creation time
4. **No Deletion**: Structures are never removed (simplifies design)
5. **Zero-Copy**: Data is accessed directly in shared memory

## Memory Layout

```
+----------------+
| Table Header   |
+----------------+
| Table Entries  |
+----------------+
| Structure 1    |
+----------------+
| Structure 2    |
+----------------+
| ...            |
+----------------+
```

## Table Format

### Table Header (32 bytes)

```c
struct TableHeader {
    uint32_t magic;         // 0x00: Magic number 0x5A49504D ('ZIPM')
    uint32_t version;       // 0x04: Format version (currently 1)
    uint32_t entry_count;   // 0x08: Number of active entries
    uint32_t max_entries;   // 0x0C: Maximum table entries (0 = implementation default)
    uint64_t memory_size;   // 0x10: Total size of shared memory segment
    uint64_t next_offset;   // 0x18: Next allocation offset
};
```

### Table Entry (48 bytes each)
```c
struct TableEntry {
    char     name[32];      // 0x00: Null-terminated name
    uint64_t offset;        // 0x20: Offset from start of shared memory
    uint64_t size;          // 0x28: Total allocated size in bytes
};
```

### Runtime Configuration

The number of table entries is determined when the shared memory is created. The table size is:
```
table_size = 32 + max_entries * 48
```

## Data Structure Formats

### Array Structure
```c
struct ArrayHeader {
    uint64_t capacity;      // 0x00: Number of elements
};
// Followed by: capacity * element_size bytes of data
```

### Queue Structure (Vyukov Bounded MPMC)
```c
struct QueueHeader {
    atomic_uint32_t head;       // 0x00: Head index (monotonically increasing)
    atomic_uint32_t tail;       // 0x04: Tail index (monotonically increasing)
    uint32_t capacity;          // 0x08: Number of slots
    uint32_t elem_size;         // 0x0C: Element size in bytes
};
// Followed by: capacity * elem_size bytes of data
// Followed by: capacity * 4 bytes of per-slot sequence numbers (atomic_uint32_t)
// Total size: 16 + capacity * (elem_size + 4)
```

### Stack Structure (4-State CAS Lock-free)
```c
struct StackHeader {
    atomic_int32_t top;         // 0x00: Top index (-1 when empty)
    uint32_t capacity;          // 0x04: Number of slots
    uint32_t elem_size;         // 0x08: Element size in bytes
};
// Followed by: capacity * elem_size bytes of data
// Followed by: capacity * 4 bytes of per-slot state (atomic_uint32_t)
//   States: EMPTY(0) -> WRITING(1) -> READY(2) -> READING(3) -> EMPTY(0)
// Total size: 12 + capacity * (elem_size + 4)
```

### Semaphore Structure (Lock-free)
```c
struct SemaphoreHeader {
    atomic_int32_t count;       // 0x00: Current semaphore count
    atomic_int32_t waiting;     // 0x04: Number of waiting processes
    int32_t max_count;          // 0x08: Maximum count (0 = unbounded)
    int32_t _padding;           // 0x0C: Padding for alignment
};
// Total size: 16 bytes
```

**Semantics**:
- `count`: Non-negative integer representing available permits/resources
- `waiting`: Number of processes currently blocked on acquire()
- `max_count`: Maximum value for count (0 means unbounded)
- Operations:
  - `acquire()`: Atomically decrements count if > 0, otherwise blocks
  - `release()`: Atomically increments count, waking waiting processes
  - `try_acquire()`: Non-blocking acquire, returns immediately

**Usage**:
- Binary semaphore: `max_count = 1` (mutex behavior)
- Counting semaphore: `max_count = N` (resource pool of size N)
- Unbounded semaphore: `max_count = 0` (signal/event pattern)

### Barrier Structure (Lock-free)
```c
struct BarrierHeader {
    atomic_int32_t arrived;         // 0x00: Number of processes that have arrived
    atomic_int32_t generation;      // 0x04: Generation counter (for reusability)
    int32_t num_participants;       // 0x08: Total number of participants
    int32_t _padding;               // 0x0C: Padding for alignment
};
// Total size: 16 bytes
```

**Semantics**:
- `arrived`: Number of processes currently waiting at the barrier
- `generation`: Incremented each time all participants pass through (prevents early arrivals for next cycle from releasing current cycle)
- `num_participants`: Fixed number of processes that must arrive before barrier releases
- Operations:
  - `wait()`: Block until all participants have called wait(), then all are released simultaneously
  - `wait(timeout)`: Wait with timeout, returns true if barrier released, false if timeout

**Usage**:
- Synchronize N processes at a checkpoint
- All participants must reach barrier before any can proceed
- Barrier automatically resets after all participants pass through
- Example: Parallel algorithm phases - all workers must complete phase 1 before any start phase 2

**Algorithm**:
1. Process calls `wait()`
2. Atomically increment `arrived` counter
3. If `arrived < num_participants`: spin-wait checking if generation changed
4. If `arrived == num_participants`: increment generation, reset arrived to 0, all waiters proceed
5. Barrier is now ready for next cycle with new generation number

### Latch Structure (Lock-free)
```c
struct LatchHeader {
    atomic_int32_t count;       // 0x00: Current count (counts down to zero)
    int32_t initial_count;      // 0x04: Initial count value (immutable)
    int32_t _padding[2];        // 0x08: Padding for alignment
};
// Total size: 16 bytes
```

**Semantics**:
- `count`: Current countdown value, atomically decremented from initial_count to 0
- `initial_count`: The starting value, stored for reference (never changes)
- Operations:
  - `count_down(n=1)`: Atomically decrements count by n (saturates at 0)
  - `wait()`: Blocks until count reaches 0
  - `try_wait()`: Non-blocking check if count is 0
  - `wait(timeout)`: Wait with timeout, returns true if count reached 0

**Usage**:
- **One-time use**: Once count reaches 0, it stays at 0 (unlike Barrier which resets)
- **Countdown coordination**: Wait for N operations to complete before proceeding
- **Start gate**: Initialize with count=1, workers wait(), coordinator calls count_down() to release all
- **Completion detection**: Initialize with count=N, each worker calls count_down() when done, coordinator waits()
- Example: Launch N worker threads, each counts down when initialized, main thread waits for all to be ready

**Algorithm**:
1. Initialize with `count = initial_count`
2. Workers call `count_down()` to atomically decrement count (CAS loop, saturate at 0)
3. Waiters call `wait()` and spin-wait until count == 0
4. Once count reaches 0, all current and future waiters immediately proceed
5. Latch cannot be reset (one-time use)

**Difference from Barrier**:
- Latch: Counts down to 0 and stays there (one-time), any number of waiters
- Barrier: Cycles through generations (reusable), exactly N participants required

### Mutex Structure (Composite)

Mutex wraps a binary Semaphore with `max_count = 1`:

```c
// Stored at: name (as a Semaphore with max_count=1)
// Total size: 16 bytes (same as Semaphore)
```

**Semantics**:
- Binary semaphore with initial_count=1, max_count=1
- `lock()`: Acquires semaphore (decrements count from 1 to 0)
- `unlock()`: Releases semaphore (increments count from 0 to 1)
- `try_lock()`: Non-blocking lock attempt

**Usage**:
- Mutual exclusion for critical sections
- RAII-compatible with std::lock_guard

### Once Structure (Lock-free)
```c
struct OnceFlag {
    atomic_uint32_t state;      // 0x00: 0=pending, 1=executing, 2=done
};
// Total size: 4 bytes
```

**Semantics**:
- `state`: Atomic flag tracking initialization progress (3-state protocol)
  - `0` (PENDING): Callable has not been invoked yet
  - `1` (EXECUTING): One process is currently running the callable
  - `2` (DONE): Callable has completed successfully
- Operations:
  - `call(func)`: Execute func exactly once across all processes
  - `already_called()` / `is_called`: Check if state == 2
  - `reset_unsafe()`: Reset for testing (NOT thread-safe!)

**Usage**:
- One-time initialization across processes
- Lazy singleton initialization
- Similar to std::call_once

**Algorithm**:
1. Fast path: If state == 2 (DONE), return immediately
2. CAS(state, 0 → 1): If successful, execute func, then store state = 2
3. If CAS fails and state == 1: spin-wait until state == 2

### Event Structure (Lock-free)
```c
struct EventState {
    atomic_uint32_t signaled;   // 0x00: 0 = not signaled, 1 = signaled
    uint32_t mode;              // 0x04: 0 = AutoReset, 1 = ManualReset
    atomic_uint32_t waiting;    // 0x08: Number of waiting processes
};
// Stored at: name (12 bytes)
// Additional: Semaphore at name_sem (for blocking)
```

**Semantics**:
- `signaled`: Whether the event is currently signaled
- `mode`: EventMode enum (AutoReset = 0, ManualReset = 1)
- `waiting`: Number of processes blocked on wait()
- Operations:
  - `signal()`: Set signaled flag, wake waiters
  - `wait()`: Block until signaled
  - `reset()`: Clear signaled flag
  - `pulse()`: Signal + immediate reset

**Modes**:
- **AutoReset**: signal() wakes one waiter, auto-clears signaled flag
- **ManualReset**: signal() wakes all waiters, stays signaled until reset()

### RWLock Structure (Lock-free)
```c
struct RWLockState {
    atomic_int32_t readers;        // 0x00: Number of active readers
    atomic_int32_t writer_active;  // 0x04: 1 if writer active, 0 otherwise
};
// Stored at: name (8 bytes)
// Additional: Mutex at name_rmtx (reader coordination)
// Additional: Mutex at name_wmtx (writer exclusion)
```

**Semantics**:
- `readers`: Count of processes holding read lock
- `writer_active`: Flag indicating exclusive writer access
- Operations:
  - `reader_lock()`: Acquire shared read access
  - `reader_unlock()`: Release read access
  - `writer_lock()`: Acquire exclusive write access
  - `writer_unlock()`: Release write access

**Usage**:
- Read-heavy workloads where reads vastly outnumber writes
- Multiple concurrent readers OR one exclusive writer
- Writers have priority to prevent starvation

### Monitor Structure (Composite)

Monitor combines mutex + condition variable semantics:

```c
// Stored at: name (1 byte marker)
// Additional: Mutex at name_mtx (for lock/unlock)
// Additional: Semaphore at name_cond (for wait/notify)
// Additional: atomic_uint32_t at name_count (waiting count, 4 bytes)
```

**Semantics**:
- Mutex for lock/unlock
- Semaphore for condition wait/notify
- Waiting counter for notify_all
- Operations:
  - `lock()` / `unlock()`: Mutex operations
  - `wait(predicate)`: Atomically unlock mutex, wait for signal, re-lock
  - `notify_one()`: Wake one waiting process
  - `notify_all()`: Wake all waiting processes

**Usage**:
- Producer-consumer patterns
- Predicate-based waiting (spurious wakeup handling)
- Bounded buffer synchronization

### Signal Structure (Lock-free)
```c
template<typename T>
struct SignalState {
    atomic_uint64_t version;    // 0x00: Version number (increments on change)
    T value;                    // 0x08: The stored value
};
// Total size: 8 + sizeof(T) bytes
```

**Semantics**:
- `version`: Monotonically increasing version number
- `value`: The reactive value being stored
- Operations:
  - `get()`: Read current value
  - `set(value)`: Write new value, increment version
  - `update(func)`: Atomically apply func to current value
  - `version()`: Get current version number
  - `has_changed(last_version)`: Check if version changed
  - `wait_for_change(last_version)`: Block until version changes

**Usage**:
- Fine-grained reactivity (SolidJS/Preact style)
- Change detection across processes
- Publish-subscribe patterns with version tracking

**Type Constraints**:
- T must be trivially copyable
- Common types: int, double, fixed-size structs

## Alignment Requirements

- All structures must be aligned to 8-byte boundaries
- The next_offset in TableHeader includes any padding needed for alignment

## Usage Contract

1. **Type Safety**: Users are responsible for using consistent types when accessing structures
2. **Element Size**: Users must know the element size when opening existing structures
3. **Structure Type**: Users must know whether a name refers to an array, queue, stack, etc.
4. **Naming**: Names are limited to 31 characters (plus null terminator)

## Example Memory Layout

```text
Offset   Size    Content
0x0000   32      Table Header (magic=0x5A49504D, version=1, entries=2, max=64, mem_size=0x10000, next=0x1000)
0x0020   48      Entry 0: name="sensor_data", offset=0x1000, size=0x2008
0x0050   48      Entry 1: name="event_queue", offset=0x3008, size=0x0330
...
0x1000   8       Array Header: capacity=1000
0x1008   4000    Array Data: 1000 * 4 bytes (float32)
0x3008   16      Queue Header: head=0, tail=0, capacity=100, elem_size=4
0x3018   400     Queue Data: 100 * 4 bytes (int32)
0x31A8   400     Queue Sequences: 100 * 4 bytes (per-slot sequence numbers)
```

## Version History

- v1.2: Updated Table Header to 32 bytes (added max_entries, memory_size; widened next_offset to uint64), Table Entry to 48 bytes (widened offset/size to uint64), Once to 3-state protocol (pending/executing/done)
- v1.1: Extended synchronization primitives (Mutex, Once, Event, RWLock, Monitor, Signal)
- v1.0: Initial release with minimal metadata design, runtime configurable table