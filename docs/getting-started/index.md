# Getting Started with ZeroIPC

Welcome to ZeroIPC! This guide will help you get up and running with cross-language shared memory IPC in minutes.

## What You'll Learn

This section covers everything you need to start using ZeroIPC:

1. **[Installation](installation.md)** - Install ZeroIPC for C++, C, or Python
2. **[Quick Start](quick-start.md)** - Write your first shared memory program
3. **[Basic Concepts](concepts.md)** - Understanding shared memory, metadata tables, and data structures

## Prerequisites

Before you begin, make sure you have:

- **Operating System**: Linux, macOS, or any POSIX-compliant system
- **For C++**: C++23-compatible compiler (GCC 12+, Clang 15+)
- **For C**: C99-compatible compiler
- **For Python**: Python 3.8+ with NumPy

!!! tip "Platform Support"
    ZeroIPC uses POSIX shared memory (`shm_open`, `mmap`), which is available on all Unix-like systems. Windows support via Cygwin or WSL.

## Typical Workflow

Here's the typical development workflow with ZeroIPC:

```mermaid
graph LR
    A[Install ZeroIPC] --> B[Create Shared Memory]
    B --> C[Add Data Structures]
    C --> D[Share Between Processes]
    D --> E[Inspect with CLI]
    E --> F[Production Use]
```

## Quick Decision Guide

### Which Language Should I Use?

Choose based on your needs:

| Use Case | Recommended Language | Why |
|----------|---------------------|-----|
| Maximum performance | **C++** | Header-only templates, zero overhead |
| Legacy integration | **C** | Pure C99, minimal dependencies |
| Data science/ML | **Python** | NumPy integration, REPL convenience |
| Mixed environment | **All three** | Language independence is the point! |

### Which Data Structure Should I Use?

| Need | Structure | Best For |
|------|-----------|----------|
| Random access | `Array` | Sensor readings, bulk data |
| FIFO queue | `Queue` | Task queues, event streams |
| LIFO stack | `Stack` | Undo buffers, call stacks |
| Key-value lookup | `Map` | Caching, configuration |
| Unique elements | `Set` | Deduplication, membership tests |
| Object recycling | `Pool` | Memory-efficient object reuse |
| Streaming | `Stream` | Reactive event processing |
| Async results | `Future` | Cross-process async/await |
| Message passing | `Channel` | CSP-style communication |

## Example: Temperature Monitoring

Let's look at a complete example of a C++ producer and Python consumer:

=== "C++ Producer (producer.cpp)"

    ```cpp
    #include <zeroipc/memory.h>
    #include <zeroipc/array.h>
    #include <chrono>
    #include <thread>
    #include <random>
    
    int main() {
        // Create 10MB shared memory
        zeroipc::Memory mem("/sensors", 10*1024*1024);
        
        // Create array for 1000 temperature readings
        zeroipc::Array<float> temps(mem, "temperatures", 1000);
        
        // Simulate sensor readings
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dis(20.0f, 30.0f);
        
        for (int i = 0; i < 1000; ++i) {
            temps[i] = dis(gen);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        return 0;
    }
    ```

=== "Python Consumer (consumer.py)"

    ```python
    from zeroipc import Memory, Array
    import numpy as np
    import time
    
    # Open existing shared memory
    mem = Memory("/sensors")
    
    # Access temperature array
    temps = Array(mem, "temperatures", dtype=np.float32)
    
    # Monitor temperatures
    while True:
        current_temps = np.array(temps[:100])  # Read first 100
        avg_temp = np.mean(current_temps)
        max_temp = np.max(current_temps)
        
        print(f"Average: {avg_temp:.2f}°C, Max: {max_temp:.2f}°C")
        time.sleep(1)
    ```

## Next Steps

Ready to install ZeroIPC? Head to the **[Installation Guide](installation.md)** to get started!

Already installed? Jump to the **[Quick Start](quick-start.md)** to write your first program.

Want to understand the concepts first? Read **[Basic Concepts](concepts.md)**.
