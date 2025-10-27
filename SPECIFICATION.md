# ZeroIPC Shared Memory Format Specification v1.0

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

### Table Header (16 bytes)

```c
struct TableHeader {
    uint32_t magic;         // 0x00: Magic number 0x5A49504D ('ZIPM')
    uint32_t version;       // 0x04: Format version (currently 1)
    uint32_t entry_count;   // 0x08: Number of active entries
    uint32_t next_offset;   // 0x0C: Next allocation offset
};
```

### Table Entry (40 bytes each)
```c
struct TableEntry {
    char     name[32];      // 0x00: Null-terminated name
    uint32_t offset;        // 0x20: Offset from start of shared memory
    uint32_t size;          // 0x24: Total allocated size in bytes
};
```

### Runtime Configuration

The number of table entries is determined when the shared memory is created. The table size is:
```
table_size = sizeof(TableHeader) + max_entries * sizeof(TableEntry)
```

## Data Structure Formats

### Array Structure
```c
struct ArrayHeader {
    uint64_t capacity;      // 0x00: Number of elements
};
// Followed by: capacity * element_size bytes of data
```

### Queue Structure (Lock-free Circular Buffer)
```c
struct QueueHeader {
    atomic_uint64_t head;   // 0x00: Head index
    atomic_uint64_t tail;   // 0x08: Tail index  
    uint64_t capacity;      // 0x10: Number of elements
};
// Followed by: capacity * element_size bytes of circular buffer
```

### Stack Structure (Lock-free)
```c
struct StackHeader {
    atomic_uint64_t top;    // 0x00: Top index
    uint64_t capacity;      // 0x08: Number of elements
};
// Followed by: capacity * element_size bytes of data
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
0x0000   16      Table Header (magic=0x5A49504D, version=1, entries=2, next=0x1000)
0x0010   40      Entry 0: name="sensor_data", offset=0x1000, size=0x2008
0x0038   40      Entry 1: name="event_queue", offset=0x3008, size=0x0418
...
0x1000   8       Array Header: capacity=1000
0x1008   4000    Array Data: 1000 * 4 bytes (float32)
0x3008   24      Queue Header: head=0, tail=0, capacity=100
0x3020   400     Queue Data: 100 * 4 bytes (int32)
```

## Version History

- v1.0: Initial release with minimal metadata design, runtime configurable table