# POSIX Shared Memory Data Structures

## Zero-Overhead Inter-Process Communication for High-Performance Computing

### Overview

This library provides **production-ready, lock-free data structures** built on POSIX shared memory for high-performance inter-process communication (IPC). Designed for simulations, real-time systems, and high-throughput applications where nanosecond-level performance matters.

### Key Features

- 🚀 **Zero read overhead** - Proven identical performance to native arrays
- 🔓 **Lock-free operations** - Where algorithmically possible  
- 📦 **Auto-discovery** - Named data structures findable across processes
- 🎯 **Cache-efficient** - Optimized memory layouts for modern CPUs
- 🔧 **Modern C++23** - Concepts, ranges, string_view, [[nodiscard]]
- 📏 **Configurable overhead** - Template-based table sizes from 904B to 26KB
- 🧪 **Battle-tested** - Comprehensive test suite with Catch2

### Why Shared Memory?

Traditional IPC methods (sockets, pipes, message queues) require:
- **Kernel transitions** (~1000ns overhead)
- **Data copying** (2x memory bandwidth)  
- **Serialization** (CPU cycles + allocations)

Shared memory provides:
- **Direct memory access** (~0.5ns for L1 hit)
- **Zero-copy** data sharing
- **No serialization** for POD types
- **Cache coherence** handled by hardware

### Performance Guarantees

| Operation | Time Complexity | Actual Performance |
|-----------|----------------|-------------------|
| Array Read | O(1) | 0.5-2ns (cache hit) |
| Array Write | O(1) | 2-5ns (atomic CAS) |
| Queue Enqueue | O(1) | 5-10ns (lock-free) |
| Queue Dequeue | O(1) | 5-10ns (lock-free) |
| Pool Acquire | O(1) | 10-20ns (lock-free) |
| Atomic Update | O(1) | 2-5ns (hardware) |
| Discovery | O(n) | ~100ns (one-time) |

### Architecture

```
┌─────────────────────────────────────────────────┐
│                Shared Memory Segment            │
├─────────────────────────────────────────────────┤
│  ┌──────────┐                                   │
│  │ RefCount │  Atomic reference counting        │
│  ├──────────┤                                   │
│  │ Table    │  Metadata for discovery           │
│  │  ├─Entry1│  "sensor_data" → offset, size     │
│  │  ├─Entry2│  "event_queue" → offset, size     │
│  │  └─...   │                                   │
│  ├──────────┤                                   │
│  │ Array<T> │  Contiguous data                  │
│  ├──────────┤                                   │
│  │ Queue<T> │  Circular buffer + atomics        │
│  ├──────────┤                                   │
│  │ Pool<T>  │  Free list + object storage       │
│  └──────────┘                                   │
└─────────────────────────────────────────────────┘
```

### Core Components

#### 1. Foundation Layer
- **posix_shm** - POSIX shared memory lifecycle management
- **shm_table** - Metadata and discovery system
- **shm_span** - Base class for memory regions

#### 2. Data Structures
- **shm_array<T>** - Fixed-size contiguous array
- **shm_queue<T>** - Lock-free FIFO queue
- **shm_atomic<T>** - Named atomic variables
- **shm_object_pool<T>** - O(1) object allocation
- **shm_ring_buffer<T>** - Bulk operations for streaming

#### 3. Template Configurations
```cpp
// Minimal overhead (904 bytes) - embedded systems
using my_shm = posix_shm_impl<shm_table_impl<16, 16>>;

// Default (4KB) - balanced
using my_shm = posix_shm;  

// Large (26KB) - complex simulations  
using my_shm = posix_shm_impl<shm_table_impl<64, 256>>;
```

### Quick Start

```cpp
#include "posix_shm.h"
#include "shm_array.h"
#include "shm_queue.h"

// Process 1: Create and populate
posix_shm shm("simulation", 10*1024*1024);  // 10MB
shm_array<double> sensors(shm, "sensors", 1000);
sensors[0] = 3.14159;

shm_queue<Event> events(shm, "events", 100);
events.enqueue({timestamp, data});

// Process 2: Discover and use
posix_shm shm("simulation");  // Open existing
shm_array<double> sensors(shm, "sensors");  // Find by name
auto value = sensors[0];  // Direct memory read!

shm_queue<Event> events(shm, "events");
if (auto e = events.dequeue()) {
    process(*e);
}
```

### Use Cases

#### High-Frequency Trading
- Market data distribution
- Order book sharing
- Strategy coordination

#### Scientific Simulation
- Particle systems (10,000+ entities)
- Sensor data aggregation (MHz rates)
- Grid-based computations (CFD, weather)

#### Robotics & Autonomous Systems
- Sensor fusion pipelines
- Control loop communication
- Perception data sharing

#### Game Servers
- Entity state replication
- Physics synchronization
- Event broadcasting

### Proven Performance

Our benchmarks demonstrate **zero overhead** for reads:

```
Sequential Read Performance:
Heap array:              2.32 ns/op
Shared array:            2.32 ns/op  ← Identical!
Shared raw pointer:      2.31 ns/op  ← Direct access

Random Access Performance:
Heap array:              2.33 ns/op  
Shared array:            2.33 ns/op  ← Same cache behavior
```

### Safety & Correctness

- **Type safety** via C++23 concepts
- **Bounds checking** in debug builds
- **RAII** memory management
- **Atomic operations** for thread safety
- **Process crash resilience** 

### Getting Started

1. [Tutorial](tutorial.md) - Step-by-step guide
2. [Performance Guide](performance.md) - Optimization tips
3. [Architecture](architecture.md) - Design deep dive
4. [API Reference](annotated.html) - Complete documentation

### Requirements

- C++23 compiler (GCC 13+, Clang 16+)
- POSIX-compliant OS (Linux, macOS, BSD)
- CMake 3.20+

### License

MIT License - Use freely in commercial projects

### Contributing

Contributions welcome! Areas of interest:
- Additional data structures (B-tree, hash map)
- Performance optimizations (huge pages, NUMA)
- Language bindings (Python, Rust)
- Platform ports (Windows shared memory)