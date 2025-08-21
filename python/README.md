# posix-shm: Python Bindings

High-performance lock-free data structures in POSIX shared memory for Python.

## Installation

```bash
pip install posix-shm
```

## Quick Start

```python
from posix_shm import SharedMemory, Queue, HashMap, Bitset

# Create shared memory segment
shm = SharedMemory("/my_shared_mem", size=10*1024*1024)  # 10MB

# Create a lock-free queue
queue = Queue(shm, "task_queue", max_size=1000)

# Producer process
queue.push({"task": "process_data", "id": 42})

# Consumer process (can be different process)
if not queue.empty():
    task = queue.pop()
    print(f"Processing: {task}")

# Create a shared hash map
cache = HashMap(shm, "cache", max_entries=10000)
cache["user:123"] = {"name": "Alice", "score": 95.5}

# Create a bitset for flags
flags = Bitset(shm, "flags", size=1000000)
flags.set(42)
if flags.test(42):
    print("Flag 42 is set")
```

## Features

- **Zero-copy IPC**: Direct memory access between processes
- **Lock-free algorithms**: High-performance concurrent operations
- **Type-safe**: Full type hints and runtime checking
- **Pythonic API**: Natural Python interfaces with magic methods
- **NumPy integration**: Efficient array operations
- **Process-safe**: Designed for multiprocessing

## Data Structures

### Queue
FIFO queue with lock-free push/pop operations:
```python
queue = Queue(shm, "my_queue", max_size=1000)
queue.push(item)
item = queue.pop()
```

### Stack
LIFO stack with lock-free operations:
```python
stack = Stack(shm, "my_stack", max_size=1000)
stack.push(item)
item = stack.pop()
```

### HashMap
Key-value store with lock-free operations:
```python
map = HashMap(shm, "my_map", max_entries=10000)
map["key"] = value
value = map["key"]
del map["key"]
```

### Bitset
Compact bit array with atomic operations:
```python
bits = Bitset(shm, "my_bits", size=1000000)
bits.set(42)
bits.flip(100)
if bits.test(42):
    process()
```

### Array
Fixed-size array with atomic operations:
```python
arr = Array(shm, "my_array", size=1000)
arr[0] = 42.5
value = arr[0]
```

## Multiprocessing Example

```python
from multiprocessing import Process
from posix_shm import SharedMemory, Queue

def producer(shm_name):
    shm = SharedMemory(shm_name, 0)  # Attach to existing
    queue = Queue(shm, "tasks")
    
    for i in range(100):
        queue.push(f"task_{i}")

def consumer(shm_name):
    shm = SharedMemory(shm_name, 0)  # Attach to existing
    queue = Queue(shm, "tasks")
    
    while True:
        if not queue.empty():
            task = queue.pop()
            print(f"Processing: {task}")
        else:
            break

# Main process
shm = SharedMemory("/work_queue", 10*1024*1024)
queue = Queue(shm, "tasks", max_size=1000)

# Start workers
p1 = Process(target=producer, args=("/work_queue",))
p2 = Process(target=consumer, args=("/work_queue",))

p1.start()
p2.start()

p1.join()
p2.join()

# Cleanup
shm.unlink()
```

## Requirements

- Linux (POSIX shared memory support)
- Python 3.8+
- C++23 compiler (for building from source)

## Performance

All data structures use lock-free algorithms for maximum performance:
- Queue: ~10M ops/sec
- HashMap: ~5M ops/sec  
- Bitset: ~100M ops/sec
- Array: ~50M ops/sec

Benchmarks available in `benchmarks/` directory.

## Documentation

Full documentation: https://queelius.github.io/posix_shm/

## License

MIT License - see LICENSE file for details.