# ZeroIPC CLI Tools Documentation

## Overview

ZeroIPC provides command-line tools for inspecting, debugging, and monitoring shared memory segments and their data structures. These tools are essential for development, debugging, and operations.

**Current Version:** 3.0 (Enhanced with full data structure support)

**Supported Structures:** All 16 ZeroIPC data structures including:
- Traditional: Array, Queue, Stack, Ring, Map, Set, Pool, Table
- Synchronization: Semaphore, Barrier, Latch
- Codata: Future, Lazy, Stream, Channel

## zeroipc

The primary inspection tool for ZeroIPC shared memory segments with comprehensive structure-specific commands.

### Installation

```bash
cd cpp
cmake -B build .
cmake --build build
sudo cp build/tools/zeroipc /usr/local/bin/  # Optional: install system-wide
```

### Commands

#### list - List all ZeroIPC segments

Lists all shared memory segments that appear to be ZeroIPC-managed (contain valid table headers).

```bash
zeroipc list [options]
```

**Options:**
- `--all` - Include non-ZeroIPC segments
- `--details` - Show detailed information (size, permissions, etc.)
- `--json` - Output in JSON format

**Example:**
```bash
$ zeroipc list --details
NAME            SIZE        CREATED              PROCESSES  STRUCTURES
/sensor_data    10485760    2024-01-15 14:23:01  3          5
/analytics      52428800    2024-01-15 14:20:15  2          12
/messages       1048576     2024-01-15 14:25:44  4          2

Total: 3 segments, 63.0 MB
```

#### show - Display segment information

Shows detailed information about a specific shared memory segment.

```bash
zeroipc show <segment_name> [options]
```

**Options:**
- `--structures` - List all data structures
- `--metadata` - Show raw table metadata
- `--json` - Output in JSON format

**Example:**
```bash
$ zeroipc show /sensor_data --structures
Segment: /sensor_data
Size: 10485760 bytes (10.0 MB)
Created: 2024-01-15 14:23:01
Last modified: 2024-01-15 14:45:32
Processes attached: 3

Table Information:
  Version: 1.0
  Max entries: 64
  Used entries: 5
  Next offset: 45632

Structures:
  NAME                TYPE        OFFSET    SIZE      DESCRIPTION
  temperatures        array       1024      4000      Array<float>[1000]
  pressure           array       5024      8000      Array<double>[1000]
  temp_stream        stream      13024     16384     Stream<double>
  alerts             queue       29408     8192      Queue<Alert>[256]
  config             lazy        37600     8032      Lazy<Config>
```

#### dump - Dump raw memory contents

Dumps raw bytes from a shared memory segment for debugging.

```bash
zeroipc dump <segment_name> [options]
```

**Options:**
- `--offset <n>` - Starting offset (default: 0)
- `--size <n>` - Number of bytes to dump (default: 256)
- `--hex` - Display in hexadecimal (default)
- `--binary` - Display in binary
- `--ascii` - Show ASCII representation
- `--output <file>` - Write to file instead of stdout

**Example:**
```bash
$ zeroipc dump /sensor_data --offset 1024 --size 64 --ascii
00000400: 00 00 c8 41 00 00 ca 41 00 00 cc 41 00 00 ce 41  ...A...A...A...A
00000410: 00 00 d0 41 00 00 d2 41 00 00 d4 41 00 00 d6 41  ...A...A...A...A
00000420: 00 00 d8 41 00 00 da 41 00 00 dc 41 00 00 de 41  ...A...A...A...A
00000430: 00 00 e0 41 00 00 e2 41 00 00 e4 41 00 00 e6 41  ...A...A...A...A
```

#### monitor - Monitor data structures in real-time

Monitors a specific data structure, showing updates as they occur.

```bash
zeroipc monitor <segment_name> <structure_name> [options]
```

**Options:**
- `--interval <ms>` - Update interval in milliseconds (default: 1000)
- `--tail <n>` - For queues/streams, show last n items
- `--filter <expr>` - Filter expression for values
- `--json` - Output updates as JSON
- `--csv` - Output as CSV for logging

**Example:**
```bash
$ zeroipc monitor /sensors temperature_stream --tail 5 --interval 500
Monitoring: /sensors::temperature_stream (Stream<double>)
Update interval: 500ms
Press Ctrl+C to stop

[14:45:32.123] 23.5
[14:45:32.623] 23.7
[14:45:33.124] 23.6
[14:45:33.625] 23.8
[14:45:34.126] 24.1
^C
Summary: 5 values observed, avg: 23.74, min: 23.5, max: 24.1
```

#### Structure-Specific Commands (v3.0)

The CLI tool now supports dedicated commands for each data structure type:

##### array - Inspect arrays
```bash
zeroipc array <segment_name> <array_name> [options]

Options:
  --index <n>        Show specific index
  --range <start:end> Show range of indices
  --stats            Show array statistics (min, max, avg)
  --type <t>         Interpret as type (int32, float, double, etc.)

Example:
$ zeroipc array /sensor_data temperatures --range 0:5 --type float
Array: temperatures
Capacity: 1000
Type: float (assumed)
Data:
  [0]: 23.5
  [1]: 23.7
  [2]: 23.6
  [3]: 23.8
  [4]: 24.1
```

##### queue - Inspect queues
```bash
zeroipc queue <segment_name> <queue_name> [options]

Options:
  --peek <n>         Peek at first n elements without removing
  --stats            Show queue statistics
  --type <t>         Element type

Example:
$ zeroipc queue /tasks work_queue --peek 3
Queue: work_queue
Capacity: 1000
Head: 42, Tail: 58
Current size: 16
Full: false, Empty: false

Peek (first 3 elements):
  [0]: Task{id=123, priority=5}
  [1]: Task{id=124, priority=3}
  [2]: Task{id=125, priority=8}
```

##### stack - Inspect stacks
```bash
zeroipc stack <segment_name> <stack_name> [options]

Options:
  --peek <n>         Peek at top n elements
  --stats            Show stack statistics

Example:
$ zeroipc stack /app undo_stack --peek 5
Stack: undo_stack
Capacity: 500
Top: 23
Current size: 24
Full: false, Empty: false

Top 5 elements:
  [23]: UndoAction{type=TEXT_EDIT, offset=452}
  [22]: UndoAction{type=DELETE, offset=445}
  [21]: UndoAction{type=INSERT, offset=440}
  [20]: UndoAction{type=TEXT_EDIT, offset=420}
  [19]: UndoAction{type=FORMAT, offset=400}
```

##### ring - Inspect ring buffers
```bash
zeroipc ring <segment_name> <ring_name> [options]

Options:
  --tail <n>         Show last n elements
  --stats            Show ring buffer statistics
  --monitor          Monitor in real-time

Example:
$ zeroipc ring /events event_log --tail 10
Ring Buffer: event_log
Capacity: 10000
Head: 5432, Tail: 5442
Current size: 10
Overwrite mode: true

Last 10 events:
  [5432]: Event{timestamp=1697123456, type=USER_LOGIN}
  [5433]: Event{timestamp=1697123457, type=PAGE_VIEW}
  [5434]: Event{timestamp=1697123458, type=CLICK}
  ...
```

##### map - Inspect hash maps
```bash
zeroipc map <segment_name> <map_name> [options]

Options:
  --key <k>          Lookup specific key
  --keys             List all keys
  --stats            Show map statistics (load factor, collisions)

Example:
$ zeroipc map /cache user_sessions --stats
Hash Map: user_sessions
Capacity: 1000
Current entries: 347
Load factor: 34.7%
Longest probe: 3

Statistics:
  Occupied buckets: 347
  Empty buckets: 653
  Average probe length: 1.2
  Max probe length: 3
```

##### set - Inspect hash sets
```bash
zeroipc set <segment_name> <set_name> [options]

Options:
  --contains <v>     Check if value exists
  --list             List all values
  --stats            Show set statistics

Example:
$ zeroipc set /app unique_ids --stats
Hash Set: unique_ids
Capacity: 5000
Current elements: 1234
Load factor: 24.7%

Statistics:
  Occupied buckets: 1234
  Empty buckets: 3766
  Duplicates prevented: 456
```

##### pool - Inspect object pools
```bash
zeroipc pool <segment_name> <pool_name> [options]

Options:
  --stats            Show allocation statistics
  --free-list        Show free list state

Example:
$ zeroipc pool /app connection_pool --stats
Object Pool: connection_pool
Capacity: 100
Object size: 256 bytes
Current allocations: 67
Free objects: 33

Statistics:
  Total allocated: 67
  Total freed: 245
  Peak usage: 89
  Fragmentation: low
```

##### channel - Inspect channels
```bash
zeroipc channel <segment_name> <channel_name> [options]

Options:
  --peek <n>         Peek at buffered messages
  --stats            Show channel statistics

Example:
$ zeroipc channel /ipc message_chan --stats
Channel: message_chan
Buffer capacity: 100
Buffered mode: true
Current messages: 23
Senders waiting: 0
Receivers waiting: 2

Statistics:
  Messages sent: 45678
  Messages received: 45655
  Send blocks: 12
  Receive blocks: 456
```

##### semaphore - Inspect semaphores
```bash
zeroipc semaphore <segment_name> <semaphore_name>

Example:
$ zeroipc semaphore /sync resource_sem
Semaphore: resource_sem
Current count: 3
Max count: 10 (counting semaphore)
Waiting processes: 2

Status: 3 permits available, 2 processes waiting
```

##### barrier - Inspect barriers
```bash
zeroipc barrier <segment_name> <barrier_name>

Example:
$ zeroipc barrier /sync phase_barrier
Barrier: phase_barrier
Participants: 8
Arrived: 5
Generation: 42
Status: Waiting for 3 more participants
```

##### latch - Inspect latches
```bash
zeroipc latch <segment_name> <latch_name>

Example:
$ zeroipc latch /sync startup_latch
Latch: startup_latch
Initial count: 10
Current count: 3
Status: Waiting for 3 more counts to reach zero
```

##### future - Inspect futures
```bash
zeroipc future <segment_name> <future_name> [options]

Options:
  --value            Show value if ready
  --wait             Wait for value to become available

Example:
$ zeroipc future /compute result_future
Future: result_future
State: READY
Value available: yes
Value: 42.7182818284

$ zeroipc future /compute pending_future
Future: pending_future
State: PENDING
Value available: no
Waiting processes: 3
```

##### lazy - Inspect lazy computations
```bash
zeroipc lazy <segment_name> <lazy_name>

Example:
$ zeroipc lazy /compute expensive_calc
Lazy: expensive_calc
State: COMPUTED
Memoized: yes
Value: 3.14159265359
Computation time: 2.345s (cached)
```

##### stream - Inspect reactive streams
```bash
zeroipc stream <segment_name> <stream_name> [options]

Options:
  --tail <n>         Show last n emitted values
  --subscribers      Show subscriber count
  --monitor          Monitor emissions in real-time

Example:
$ zeroipc stream /sensors temp_stream --tail 5
Stream: temp_stream
Buffer size: 1000
Current elements: 487
Subscribers: 3
Emissions/sec: 10.2

Last 5 emissions:
  [482]: 23.5 (10ms ago)
  [483]: 23.7 (9ms ago)
  [484]: 23.6 (8ms ago)
  [485]: 23.8 (7ms ago)
  [486]: 24.1 (6ms ago)
```

#### read - Read values from data structures

Legacy read command (use structure-specific commands above for better output):

```bash
zeroipc read <segment_name> <structure_name> [options]
```

**Options:**
- `--index <n>` - For arrays, specific index
- `--range <start:end>` - Range of indices
- `--key <k>` - For maps, specific key
- `--type <t>` - Interpret as type (int32, float, double, etc.)
- `--json` - Output as JSON

**Example:**
```bash
$ zeroipc read /sensor_data temperatures --range 0:10 --type float
temperatures[0]: 23.5
temperatures[1]: 23.7
temperatures[2]: 23.6
temperatures[3]: 23.8
temperatures[4]: 24.1
temperatures[5]: 24.0
temperatures[6]: 23.9
temperatures[7]: 24.2
temperatures[8]: 24.3
temperatures[9]: 24.1
```

#### write - Write values to data structures

Writes values to a data structure (use with caution in production).

```bash
zeroipc write <segment_name> <structure_name> [options]
```

**Options:**
- `--index <n>` - Array index to write
- `--key <k>` - Map key to write
- `--value <v>` - Value to write
- `--type <t>` - Value type
- `--stdin` - Read values from stdin

**Example:**
```bash
$ zeroipc write /sensor_data temperatures --index 0 --value 25.0 --type float
Writing 25.0 to temperatures[0]
Success: Value written

# Bulk write from file
$ cat values.txt | zeroipc write /sensor_data temperatures --stdin --type float
Written 100 values to temperatures
```

#### stats - Show statistics

Displays statistics about a segment or structure.

```bash
zeroipc stats <segment_name> [structure_name] [options]
```

**Options:**
- `--period <s>` - Stats for last n seconds
- `--live` - Continuously update stats
- `--json` - Output as JSON

**Example:**
```bash
$ zeroipc stats /sensor_data temp_stream --live
Stream: temp_stream
Type: Stream<double>
Buffer size: 1024
Current elements: 512

Statistics (updating every 1s):
  Messages/sec: 10.2
  Bytes/sec: 81.6
  Producers: 1
  Consumers: 3
  Buffer usage: 50.0%
  Dropped: 0
  
[14:46:00] rate: 10.2 msg/s, buffer: 50.0%, dropped: 0
[14:46:01] rate: 11.1 msg/s, buffer: 52.3%, dropped: 0
[14:46:02] rate: 9.8 msg/s, buffer: 49.8%, dropped: 0
^C
```

#### watch - Watch for structure changes

Monitors a segment for new structures being created or destroyed.

```bash
zeroipc watch <segment_name> [options]
```

**Options:**
- `--events` - Types of events to watch (create, destroy, modify)
- `--json` - Output events as JSON

**Example:**
```bash
$ zeroipc watch /analytics --events create,destroy
Watching /analytics for changes...
[14:47:15] CREATED: correlation_matrix (Array<double>[10000])
[14:47:23] CREATED: pca_result (Future<PCAResult>)
[14:47:45] DESTROYED: temp_buffer
[14:48:02] CREATED: model_weights (Array<float>[1000000])
```

#### validate - Validate segment integrity

Checks a segment for corruption or inconsistencies.

```bash
zeroipc validate <segment_name> [options]
```

**Options:**
- `--repair` - Attempt to repair issues (dangerous!)
- `--verbose` - Show detailed validation steps

**Example:**
```bash
$ zeroipc validate /sensor_data --verbose
Validating segment: /sensor_data

[✓] Segment accessible
[✓] Table header valid (magic: 0x5A49504D, version: 1.0)
[✓] Table entries consistent (5 entries)
[✓] No overlapping allocations
[✓] All offsets within bounds
[✓] Structure headers valid

Result: VALID (no issues found)
```

#### clean - Clean up orphaned segments

Removes shared memory segments with no attached processes.

```bash
zeroipc clean [options]
```

**Options:**
- `--dry-run` - Show what would be cleaned without doing it
- `--force` - Clean even if processes attached
- `--pattern <p>` - Only clean segments matching pattern

**Example:**
```bash
$ zeroipc clean --dry-run
Found 3 orphaned segments:
  /test_12345 (1.2 MB) - last accessed 2 days ago
  /tmp_worker_7 (256 KB) - last accessed 5 hours ago
  /benchmark_old (50 MB) - last accessed 1 week ago

Total: 51.5 MB would be freed
Run without --dry-run to actually clean
```

### Advanced Usage

#### Scripting with JSON Output

```bash
#!/bin/bash
# Monitor queue depth and alert if too high
while true; do
    depth=$(zeroipc stats /app message_queue --json | jq '.current_elements')
    if [ $depth -gt 900 ]; then
        send_alert "Queue depth critical: $depth"
    fi
    sleep 5
done
```

#### Continuous Monitoring Pipeline

```bash
# Log stream values to file with timestamps
zeroipc monitor /sensors temp_stream --csv | \
    awk '{print strftime("%Y-%m-%d %H:%M:%S"), $0}' >> sensor_log.csv
```

#### Debugging Memory Leaks

```bash
# Track memory usage over time
while true; do
    zeroipc show /app --json | \
        jq '{time: now, used: .next_offset, free: (.size - .next_offset)}' >> memory_usage.jsonl
    sleep 60
done
```

### Environment Variables

- `ZEROIPC_INSPECT_COLOR` - Enable/disable colored output (auto, always, never)
- `ZEROIPC_INSPECT_FORMAT` - Default output format (text, json, csv)
- `ZEROIPC_INSPECT_VERBOSE` - Verbose logging (0-3)

### Configuration File

Create `~/.zeroipc/inspect.conf`:

```ini
[general]
color = auto
format = text
verbose = 1

[monitor]
default_interval = 1000
max_tail = 100

[aliases]
sensors = /sensor_data
app = /application_main
```

## Future Tools (Planned)

### zeroipc-bench

Performance benchmarking tool:
```bash
zeroipc-bench queue --size 1000 --producers 4 --consumers 2
zeroipc-bench stream --rate 10000 --duration 60
```

### zeroipc-replay

Replay captured data:
```bash
zeroipc-replay capture.bin --segment /test --speed 2.0
```

### zeroipc-trace

Trace operations on structures:
```bash
zeroipc-trace /app --operations push,pop,emit --output trace.log
```

## Troubleshooting

### Common Issues

#### Permission Denied
```bash
$ zeroipc show /app
Error: Permission denied accessing /app

# Fix: Check permissions
ls -la /dev/shm/app

# Fix: Run with appropriate user
sudo zeroipc show /app
```

#### Segment Not Found
```bash
$ zeroipc show /missing
Error: Shared memory segment /missing not found

# Check if it exists
ls /dev/shm/ | grep missing

# List all segments
zeroipc list --all
```

#### Invalid Table Header
```bash
$ zeroipc show /corrupt
Error: Invalid table header (magic number mismatch)

# This segment may not be ZeroIPC-managed or is corrupted
# Validate to check
zeroipc validate /corrupt
```

### Debug Mode

Enable debug output:
```bash
ZEROIPC_INSPECT_VERBOSE=3 zeroipc show /app
```

### Getting Help

```bash
# General help
zeroipc --help

# Command-specific help
zeroipc monitor --help

# Version information
zeroipc --version
```

## Best Practices

1. **Regular Monitoring**: Set up automated monitoring for production systems
2. **Clean Regularly**: Schedule cleanup of orphaned segments
3. **Validate After Crashes**: Always validate segments after unexpected shutdowns
4. **Use JSON for Automation**: Parse JSON output in scripts for reliability
5. **Archive Stats**: Keep historical statistics for capacity planning
6. **Access Control**: Limit write operations in production environments
7. **Backup Critical Data**: Dump important segments before maintenance

## Integration Examples

### Prometheus Exporter

```python
#!/usr/bin/env python3
import subprocess
import json
from prometheus_client import start_http_server, Gauge

queue_depth = Gauge('zeroipc_queue_depth', 'Queue depth', ['segment', 'queue'])
stream_rate = Gauge('zeroipc_stream_rate', 'Stream message rate', ['segment', 'stream'])

def collect_metrics():
    segments = json.loads(subprocess.check_output(
        ['zeroipc', 'list', '--json']
    ))
    
    for seg in segments:
        stats = json.loads(subprocess.check_output(
            ['zeroipc', 'stats', seg['name'], '--json']
        ))
        
        for struct in stats['structures']:
            if struct['type'] == 'queue':
                queue_depth.labels(seg['name'], struct['name']).set(struct['depth'])
            elif struct['type'] == 'stream':
                stream_rate.labels(seg['name'], struct['name']).set(struct['rate'])

if __name__ == '__main__':
    start_http_server(8000)
    while True:
        collect_metrics()
        time.sleep(10)
```

### Grafana Dashboard

```json
{
  "dashboard": {
    "title": "ZeroIPC Monitoring",
    "panels": [
      {
        "title": "Queue Depths",
        "targets": [
          {
            "expr": "zeroipc_queue_depth",
            "legendFormat": "{{segment}}/{{queue}}"
          }
        ]
      },
      {
        "title": "Stream Rates",
        "targets": [
          {
            "expr": "rate(zeroipc_stream_rate[5m])",
            "legendFormat": "{{segment}}/{{stream}}"
          }
        ]
      }
    ]
  }
}
```