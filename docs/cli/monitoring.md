# Monitoring and Debugging

Learn how to monitor ZeroIPC shared memory in real-time and debug issues effectively.

## Real-Time Monitoring

### monitor Command

Watch a structure update in real-time (similar to `watch` or `tail -f`).

**Syntax:**
```bash
zeroipc monitor <segment> <structure> [options]
```

**Options:**
- `--interval <ms>` - Update interval in milliseconds (default: 1000)
- `--limit <n>` - Limit displayed elements
- `--diff` - Highlight changes

**Examples:**

Monitor array:
```bash
$ zeroipc monitor /sensor_data temperatures --interval 500
Monitoring: /sensor_data/temperatures (refreshing every 500ms)
Press Ctrl+C to stop

[14:35:22] temperatures[0] = 23.45
[14:35:22] temperatures[1] = 24.12
...
[14:35:22.5] temperatures[0] = 23.47  <- changed
[14:35:22.5] temperatures[1] = 24.12
...
```

Monitor queue:
```bash
$ zeroipc monitor /tasks work_queue
Monitoring: /tasks/work_queue (refreshing every 1000ms)

[14:35:22] Size: 25/100 (25%)
           Head: 42, Tail: 67
[14:35:23] Size: 24/100 (24%)  <- dequeued 1
           Head: 43, Tail: 67
[14:35:24] Size: 26/100 (26%)  <- enqueued 2
           Head: 43, Tail: 69
```

Monitor stream:
```bash
$ zeroipc stream /events sensor_stream --follow
Following: /events/sensor_stream
New events will appear below (Ctrl+C to stop)

[14:35:22.123] {temp: 23.5, pressure: 1013.2}
[14:35:22.223] {temp: 23.4, pressure: 1013.1}
[14:35:22.323] {temp: 23.6, pressure: 1013.3}
^C
```

## Debugging Workflows

### Common Issues and Solutions

#### Issue 1: Structure Not Found

**Symptoms:**
```bash
$ zeroipc array /data numbers
Error: Structure 'numbers' not found in /data
```

**Debug steps:**

1. List all structures:
```bash
$ zeroipc show /data --structures
# Check if structure exists with different name
```

2. Check raw table:
```bash
$ zeroipc show /data --metadata
# Verify table entries
```

3. Check for corruption:
```bash
$ zeroipc dump /data --offset 0 --size 64
# Verify magic number: 5a 49 50 4d ('ZIPM')
```

#### Issue 2: Incorrect Data Values

**Symptoms:**
```bash
$ zeroipc array /data numbers
[0] = -2.14748e+09  # Garbage values
```

**Debug steps:**

1. Check type mismatch:
```bash
# Created as int32 but reading as float32?
$ zeroipc array /data numbers --hint-type int32
[0] = 42  # Correct!
```

2. Verify element size:
```bash
$ zeroipc show /data --structures
# Check size field: size / capacity = element_size
```

3. Check alignment:
```bash
$ zeroipc dump /data --offset <structure_offset>
# Verify data alignment
```

#### Issue 3: Memory Corruption

**Symptoms:**
```bash
$ zeroipc show /data
Error: Invalid table header (bad magic number)
```

**Debug steps:**

1. Check magic number:
```bash
$ zeroipc dump /data --offset 0 --size 16
# Should start with: 5a 49 50 4d
```

2. Backup if possible:
```bash
$ cp /dev/shm/data /tmp/data_backup
```

3. Try recovery (future feature):
```bash
$ zeroipc repair /data
```

#### Issue 4: Performance Problems

**Symptoms:**
- Slow enqueue/dequeue operations
- High CPU usage
- Excessive contention

**Debug steps:**

1. Check structure utilization:
```bash
$ zeroipc queue /tasks work_queue --stats
Load factor: 0.95 (95/100)  # Too full!
```

2. Monitor contention:
```bash
$ zeroipc monitor /tasks work_queue --interval 100
# Watch for thrashing (head/tail changing rapidly without progress)
```

3. Check for ABA problems:
```bash
# Look for suspicious patterns in lock-free structures
$ zeroipc monitor /data lock_free_stack
# Watch for: head reversing, duplicate values, etc.
```

## Production Monitoring

### Health Checks

Create monitoring scripts:

**monitor_queues.sh:**
```bash
#!/bin/bash
# Alert if queues are too full

for segment in $(zeroipc list | awk '{print $1}'); do
    queues=$(zeroipc show "$segment" --structures | grep queue | awk '{print $2}')
    for queue in $queues; do
        utilization=$(zeroipc queue "$segment" "$queue" --stats | grep "Load factor" | awk '{print $3}')
        if (( $(echo "$utilization > 0.90" | bc -l) )); then
            echo "WARNING: $segment/$queue is $utilization full"
        fi
    done
done
```

**check_semaphores.sh:**
```bash
#!/bin/bash
# Detect potential deadlocks

for segment in $(zeroipc list | awk '{print $1}'); do
    sems=$(zeroipc show "$segment" --structures | grep semaphore | awk '{print $2}')
    for sem in $sems; do
        waiting=$(zeroipc semaphore "$segment" "$sem" | grep "Waiting:" | awk '{print $2}')
        if [ "$waiting" -gt 5 ]; then
            echo "ALERT: $segment/$sem has $waiting processes waiting"
        fi
    done
done
```

### Metrics Collection

Collect metrics for graphing:

```bash
#!/bin/bash
# Collect time-series metrics

while true; do
    timestamp=$(date +%s)
    
    # Queue sizes
    size=$(zeroipc queue /tasks work_queue --json | jq '.size')
    echo "queue.size,$timestamp,$size" >> metrics.csv
    
    # Array statistics
    mean=$(zeroipc array /sensors temp --stats --json | jq '.mean')
    echo "sensor.temp.mean,$timestamp,$mean" >> metrics.csv
    
    sleep 60
done
```

## Debugging Techniques

### 1. Diff Mode

Compare snapshots to find changes:

```bash
# Take snapshot 1
zeroipc array /data values > snapshot1.txt

# Wait for changes...

# Take snapshot 2
zeroipc array /data values > snapshot2.txt

# Compare
diff snapshot1.txt snapshot2.txt
```

### 2. Watch Mode

Monitor specific indices:

```bash
# Watch a specific array element
watch -n 1 'zeroipc array /data counter --range 0:1'

# Watch queue size
watch -n 1 'zeroipc queue /tasks work --stats | grep "Size:"'
```

### 3. Log Correlation

Correlate CLI output with application logs:

```bash
# Terminal 1: Monitor structure
zeroipc monitor /data critical_value

# Terminal 2: Watch application logs
tail -f /var/log/myapp.log

# Look for correlations between value changes and log events
```

### 4. Memory Forensics

Analyze memory dumps:

```bash
# Dump entire segment
zeroipc dump /data --offset 0 --size 1048576 > memory_dump.hex

# Analyze with hex editor or custom tools
xxd memory_dump.hex | less

# Search for patterns
grep -a "some_pattern" memory_dump.hex
```

## Advanced Topics

### Custom Monitoring Scripts

Python example for custom monitoring:

```python
#!/usr/bin/env python3
import subprocess
import json
import time

def get_queue_stats(segment, queue_name):
    """Get queue statistics as JSON"""
    result = subprocess.run(
        ['zeroipc', 'queue', segment, queue_name, '--stats', '--json'],
        capture_output=True, text=True
    )
    return json.loads(result.stdout)

def monitor_queue(segment, queue_name, threshold=0.8):
    """Alert if queue exceeds threshold"""
    stats = get_queue_stats(segment, queue_name)
    utilization = stats['size'] / stats['capacity']
    
    if utilization > threshold:
        print(f"ALERT: {segment}/{queue_name} is {utilization:.1%} full")
        # Send to monitoring system
        send_alert(f"{segment}/{queue_name}", utilization)

while True:
    monitor_queue('/tasks', 'work_queue')
    monitor_queue('/events', 'event_queue')
    time.sleep(10)
```

### Integration with Monitoring Systems

**Prometheus Exporter:**

```python
from prometheus_client import Gauge, start_http_server
import subprocess
import json
import time

# Define metrics
queue_size = Gauge('zeroipc_queue_size', 'Queue size', ['segment', 'queue'])
queue_utilization = Gauge('zeroipc_queue_util', 'Queue utilization', ['segment', 'queue'])

def collect_metrics():
    # Collect from ZeroIPC
    segments = get_segments()  # Your implementation
    for seg in segments:
        queues = get_queues(seg)
        for q in queues:
            stats = get_queue_stats(seg, q)
            queue_size.labels(segment=seg, queue=q).set(stats['size'])
            queue_utilization.labels(segment=seg, queue=q).set(stats['size'] / stats['capacity'])

if __name__ == '__main__':
    start_http_server(8000)
    while True:
        collect_metrics()
        time.sleep(15)
```

## Troubleshooting Checklist

When debugging issues:

- [ ] Verify segment exists: `zeroipc list`
- [ ] Check segment integrity: `zeroipc show /segment`
- [ ] Verify structure exists: `zeroipc show /segment --structures`
- [ ] Check structure contents: `zeroipc <type> /segment structure`
- [ ] Verify type consistency across languages
- [ ] Check permissions: `ls -l /dev/shm/segment`
- [ ] Monitor for changes: `zeroipc monitor /segment structure`
- [ ] Check raw memory if needed: `zeroipc dump /segment`
- [ ] Verify no corruption: Check magic number and table
- [ ] Review application logs for errors

## Next Steps

- **[Basic Commands](basic-commands.md)** - Learn all commands
- **[Virtual Filesystem](virtual-filesystem.md)** - Interactive exploration
- **[Best Practices](../best-practices/index.md)** - Avoid common issues
