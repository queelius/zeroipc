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