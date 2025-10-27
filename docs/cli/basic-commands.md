# CLI Basic Commands Documentation

This page covers all basic commands available in the `zeroipc` tool.

## Global Options

Available for all commands:

```bash
zeroipc [global options] <command> [arguments]
```

**Global Options:**
- `-h, --help` - Show help message
- `-v, --version` - Show version information
- `--json` - Output in JSON format (where applicable)
- `-r, --repl` - Enter REPL mode

## Command Reference

### list

List all ZeroIPC shared memory segments on the system.

**Syntax:**
```bash
zeroipc list [options]
```

**Options:**
- `--all` - Include non-ZeroIPC segments
- `--details` - Show detailed information
- `--json` - JSON output

**Examples:**

Basic listing:
```bash
$ zeroipc list
/sensor_data    10.0 MB    5 structures
/analytics      50.0 MB   12 structures  
/messages        1.0 MB    2 structures
```

Detailed listing:
```bash
$ zeroipc list --details
NAME            SIZE        CREATED              PROCESSES  STRUCTURES
/sensor_data    10485760    2024-01-15 14:23:01  3          5
/analytics      52428800    2024-01-15 14:20:15  2          12
/messages       1048576     2024-01-15 14:25:44  4          2

Total: 3 segments, 63.0 MB
```

JSON output:
```bash
$ zeroipc list --json
{
  "segments": [
    {
      "name": "/sensor_data",
      "size": 10485760,
      "structures": 5,
      "created": "2024-01-15T14:23:01Z"
    }
  ]
}
```

### show

Display detailed information about a specific shared memory segment.

**Syntax:**
```bash
zeroipc show <segment_name> [options]
```

**Options:**
- `--structures` - List all structures in the segment
- `--metadata` - Show raw table metadata
- `--json` - JSON output

**Examples:**

Basic info:
```bash
$ zeroipc show /sensor_data
Segment: /sensor_data
Size: 10.0 MB (10485760 bytes)
Table entries: 5/64 used
Memory used: 45632 bytes
```

With structures:
```bash
$ zeroipc show /sensor_data --structures
Segment: /sensor_data

Table Information:
  Version: 1.0
  Max entries: 64
  Used entries: 5
  Next offset: 45632

Structures:
  NAME              TYPE      OFFSET    SIZE      CAPACITY
  temperatures      array     1024      4000      1000
  pressure          array     5024      8000      1000
  alerts            queue     13024     8192      256
  config            lazy      21216     8032      -
  temp_stream       stream    29248     16384     1000
```

### dump

Dump raw memory contents in hexadecimal format.

**Syntax:**
```bash
zeroipc dump <segment_name> [options]
```

**Options:**
- `--offset <n>` - Starting offset (default: 0)
- `--size <n>` - Number of bytes (default: 256)
- `--format <fmt>` - Output format: hex, ascii, both (default: both)

**Example:**
```bash
$ zeroipc dump /sensor_data --offset 0 --size 64
0000: 5a 49 50 4d 01 00 00 00  05 00 00 00 20 b2 00 00  |ZIPM........ ...|
0010: 74 65 6d 70 65 72 61 74  75 72 65 73 00 00 00 00  |temperatures....|
0020: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
0030: 00 04 00 00 a0 0f 00 00  70 72 65 73 73 75 72 65  |........pressure|
```

### create

Create structures directly from the CLI (for testing).

**Syntax:**
```bash
zeroipc create <type> <segment> <name> [options]
```

**Types:** array, queue, stack, ring, map, set, pool, semaphore, barrier, latch

**Examples:**

Create an array:
```bash
zeroipc create array /test test_array --capacity 100 --type int32
```

Create a queue:
```bash
zeroipc create queue /test task_queue --capacity 50 --type uint64
```

Create a semaphore:
```bash
zeroipc create semaphore /test mutex --initial 1
```

### delete

Delete a shared memory segment.

**Syntax:**
```bash
zeroipc delete <segment_name> [options]
```

**Options:**
- `-f, --force` - Don't ask for confirmation
- `--keep-structures` - Only unlink, don't remove from /dev/shm

**Example:**
```bash
$ zeroipc delete /test_data
WARNING: This will permanently delete segment /test_data
Continue? (y/N): y
Deleted /test_data
```

## Structure-Specific Commands

### array

Inspect array contents.

**Syntax:**
```bash
zeroipc array <segment> <name> [options]
```

**Options:**
- `--range <start>:<end>` - Show specific range
- `--limit <n>` - Limit output to n elements
- `--stats` - Show statistics (min, max, mean, etc.)

**Example:**
```bash
$ zeroipc array /sensor_data temperatures --range 0:10
Array: temperatures
Type: float (inferred from size)
Capacity: 1000
Elements shown: [0:10]

[0] = 23.45
[1] = 24.12
[2] = 22.89
[3] = 25.01
[4] = 23.67
[5] = 24.55
[6] = 23.98
[7] = 24.23
[8] = 23.12
[9] = 24.87
```

### queue

Inspect queue state and contents.

**Syntax:**
```bash
zeroipc queue <segment> <name> [options]
```

**Options:**
- `--limit <n>` - Limit number of elements shown
- `--stats` - Show queue statistics

**Example:**
```bash
$ zeroipc queue /messages task_queue
Queue: task_queue
Capacity: 100
Head: 15
Tail: 42
Size: 27/100 (27% full)

Contents (oldest to newest):
[0] = Task{id: 123, priority: 5, ...}
[1] = Task{id: 124, priority: 3, ...}
[2] = Task{id: 125, priority: 7, ...}
...
[26] = Task{id: 149, priority: 4, ...}
```

### semaphore

Inspect semaphore state.

**Syntax:**
```bash
zeroipc semaphore <segment> <name>
```

**Example:**
```bash
$ zeroipc semaphore /sync mutex
Semaphore: mutex
Type: Binary semaphore (max_count = 1)
Current count: 0 (locked)
Waiting processes: 2
```

### barrier

Inspect barrier state.

**Syntax:**
```bash
zeroipc barrier <segment> <name>
```

**Example:**
```bash
$ zeroipc barrier /sync checkpoint
Barrier: checkpoint
Participants: 4
Arrived: 2/4
Generation: 15
Status: Waiting for 2 more participants
```

### latch

Inspect latch state.

**Syntax:**
```bash
zeroipc latch <segment> <name>
```

**Example:**
```bash
$ zeroipc latch /sync startup
Latch: startup
Initial count: 5
Current count: 2
Status: Waiting for 2 more count_down calls
```

### stream

Inspect stream contents.

**Syntax:**
```bash
zeroipc stream <segment> <name> [options]
```

**Options:**
- `--tail <n>` - Show last n events
- `--follow` - Follow mode (like `tail -f`)

**Example:**
```bash
$ zeroipc stream /events sensor_stream --tail 5
Stream: sensor_stream
Capacity: 1000
Head: 523
Tail: 528
Events shown (most recent):

[528] = {temp: 23.5, humidity: 45.2, timestamp: 1642345678}
[527] = {temp: 23.4, humidity: 45.5, timestamp: 1642345677}
[526] = {temp: 23.6, humidity: 45.0, timestamp: 1642345676}
[525] = {temp: 23.5, humidity: 45.3, timestamp: 1642345675}
[524] = {temp: 23.7, humidity: 44.8, timestamp: 1642345674}
```

## Next Steps

- **[Virtual Filesystem](virtual-filesystem.md)** - Interactive navigation
- **[Structure Inspection](structure-inspection.md)** - Detailed structure viewing
- **[Monitoring](monitoring.md)** - Real-time monitoring
