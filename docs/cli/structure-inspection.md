# Structure Inspection

Learn how to inspect and understand each of the 16 ZeroIPC data structures using the CLI tool.

## Overview

The `zeroipc` tool provides specialized commands for each structure type, showing type-specific information and contents.

## Array Inspection

Arrays are the simplest structure—contiguous storage with direct indexing.

### Command

```bash
zeroipc array <segment> <name> [options]
```

### Options

- `--range <start>:<end>` - Show specific range of elements
- `--limit <n>` - Limit to first n elements
- `--stats` - Calculate statistics (min, max, mean, stddev)
- `--format <fmt>` - Output format (default, csv, json)

### Examples

Basic inspection:
```bash
$ zeroipc array /sensor_data temperatures
Array: temperatures
Capacity: 1000
Element size: 4 bytes (likely float32)
Total size: 4000 bytes

[0] = 23.45
[1] = 24.12
[2] = 22.89
... (showing first 100)
```

Specific range:
```bash
$ zeroipc array /sensor_data temperatures --range 100:110
Elements [100:110]:
[100] = 25.12
[101] = 25.45
... 
[109] = 24.33
```

With statistics:
```bash
$ zeroipc array /sensor_data temperatures --stats
Array: temperatures
Capacity: 1000

Statistics:
  Min: 20.15
  Max: 28.93
  Mean: 24.56
  Stddev: 1.87
  Median: 24.50
```

## Queue Inspection

Lock-free circular buffer with head and tail pointers.

### Command

```bash
zeroipc queue <segment> <name> [options]
```

### Options

- `--limit <n>` - Limit elements shown
- `--stats` - Show utilization statistics

### Example

```bash
$ zeroipc queue /messages task_queue
Queue: task_queue
Type: MPMC Circular Buffer
Capacity: 100
Head: 42
Tail: 67
Size: 25/100 (25% full)

Contents (FIFO order, oldest first):
[0] = {id: 1042, priority: 5, data: "process_image"}
[1] = {id: 1043, priority: 3, data: "backup_data"}
...
[24] = {id: 1066, priority: 7, data: "send_email"}

Statistics:
  Peak size: 87/100 (87%)
  Enqueue operations: 15,234
  Dequeue operations: 15,209
```

## Stack Inspection

Lock-free LIFO stack.

### Command

```bash
zeroipc stack <segment> <name> [options]
```

### Example

```bash
$ zeroipc stack /undo undo_stack
Stack: undo_stack
Capacity: 50
Top: 12
Size: 12/50

Contents (LIFO order, top first):
[top] = {action: "delete", object_id: 523}
[-1]  = {action: "modify", object_id: 521, ...}
[-2]  = {action: "create", object_id: 520, ...}
...
```

## Map Inspection

Lock-free hash map with linear probing.

### Command

```bash
zeroipc map <segment> <name> [options]
```

### Options

- `--limit <n>` - Limit entries shown
- `--stats` - Show hash table statistics

### Example

```bash
$ zeroipc map /cache session_cache
Map: session_cache
Buckets: 256
Load factor: 0.45 (115/256)
Collision rate: 12.3%

Entries:
  "user_123" => {login_time: 1642345678, role: "admin"}
  "user_456" => {login_time: 1642345690, role: "user"}
  "user_789" => {login_time: 1642345702, role: "user"}
...

Statistics:
  Max probe length: 5
  Avg probe length: 1.3
  Total lookups: 45,234
  Cache hit rate: 87.5%
```

## Semaphore Inspection

Cross-process counting semaphore.

### Command

```bash
zeroipc semaphore <segment> <name>
```

### Example

Binary semaphore (mutex):
```bash
$ zeroipc semaphore /sync mutex
Semaphore: mutex
Type: Binary (max_count = 1)
Current count: 0 (LOCKED)
Waiting: 2 processes
Max count: 1

State: Currently held, 2 processes waiting
```

Counting semaphore:
```bash
$ zeroipc semaphore /sync resource_pool
Semaphore: resource_pool
Type: Counting (max_count = 10)
Current count: 3 (3 available)
Waiting: 0 processes
Max count: 10

State: 7/10 resources in use, 3 available
```

## Barrier Inspection

Multi-process synchronization barrier.

### Command

```bash
zeroipc barrier <segment> <name>
```

### Example

```bash
$ zeroipc barrier /sync checkpoint
Barrier: checkpoint
Participants: 8
Arrived: 5/8
Generation: 42
Waiting: 3 more processes needed

State: Waiting at generation 42
  Completed cycles: 42
  Current cycle progress: 5/8 (62.5%)
```

## Stream Inspection

Reactive stream with FRP operators.

### Command

```bash
zeroipc stream <segment> <name> [options]
```

### Options

- `--tail <n>` - Show last n events
- `--follow` - Follow mode (real-time)
- `--stats` - Show stream statistics

### Example

```bash
$ zeroipc stream /events sensor_stream --tail 5
Stream: sensor_stream
Capacity: 1000
Head: 12,523
Tail: 12,528
Backpressure: None
Subscribers: 3

Recent events (most recent first):
[12528] {temp: 23.5, pressure: 1013.2, time: 1642345678}
[12527] {temp: 23.4, pressure: 1013.1, time: 1642345677}
[12526] {temp: 23.6, pressure: 1013.3, time: 1642345676}
[12525] {temp: 23.5, pressure: 1013.2, time: 1642345675}
[12524] {temp: 23.7, pressure: 1013.4, time: 1642345674}

Statistics:
  Event rate: 10.5 events/sec
  Dropped events: 0
  Subscriber lag: max 2 events
```

## Future Inspection

Asynchronous computation result.

### Command

```bash
zeroipc future <segment> <name>
```

### Example

Pending future:
```bash
$ zeroipc future /compute calculation
Future: calculation
State: PENDING
Waiting: 2 processes

The future has not been set yet.
```

Completed future:
```bash
$ zeroipc future /compute calculation
Future: calculation
State: READY
Value: 42.7865
Set at: 2024-01-15 14:35:22

The future has been fulfilled.
```

## Best Practices

### 1. Start with Overview

Always start with the segment overview:
```bash
zeroipc show /segment_name --structures
```

### 2. Check Structure Type

Verify the structure type before detailed inspection:
```bash
zeroipc show /segment --structures | grep structure_name
```

### 3. Use Appropriate Range

For large arrays, use ranges to avoid overwhelming output:
```bash
# Good: specific range
zeroipc array /data big_array --range 0:100

# Bad: dumping millions of elements
zeroipc array /data huge_array
```

### 4. Monitor Critical Structures

Use `--stats` for production monitoring:
```bash
# Check queue utilization
zeroipc queue /tasks work_queue --stats

# Check map efficiency
zeroipc map /cache data_cache --stats
```

### 5. Use JSON for Automation

Parse output programmatically:
```bash
zeroipc array /data sensors --json | jq '.values | .[0:10]'
```

## Next Steps

- **[Monitoring and Debugging](monitoring.md)** - Real-time monitoring
- **[Basic Commands](basic-commands.md)** - All available commands
- **[Virtual Filesystem](virtual-filesystem.md)** - Interactive exploration
