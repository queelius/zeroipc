# Examples

Real-world examples demonstrating ZeroIPC usage patterns.

## Example Categories

### [Cross-Language Communication](cross-language.md)

Examples of C++, Python, and C processes communicating:

- C++ producer, Python consumer
- Python producer, C++ consumer
- Multi-language pipeline
- Bidirectional messaging

### [Producer-Consumer](producer-consumer.md)

Classic producer-consumer patterns:

- Single producer, single consumer
- Multiple producers, single consumer
- Single producer, multiple consumers
- Work stealing queue

### [Sensor Data Sharing](sensor-data.md)

IoT and sensor data examples:

- Temperature monitoring
- Multi-sensor aggregation
- Real-time data visualization
- Historical data buffering

### [Reactive Processing](reactive-processing.md)

Event-driven programming with streams:

- Stream transformations (map, filter)
- Event aggregation
- Real-time analytics
- Backpressure handling

### [Real-Time Analytics](analytics.md)

High-performance data processing:

- Sliding window calculations
- Real-time aggregations
- Pattern detection
- Alert generation

## Quick Examples

### 1. Simple Data Sharing

Share an array between C++ and Python.

=== "C++ (writer.cpp)"
    ```cpp
    #include <zeroipc/memory.h>
    #include <zeroipc/array.h>
    
    int main() {
        zeroipc::Memory mem("/demo", 1024*1024);
        zeroipc::Array<double> data(mem, "values", 100);
        
        for (int i = 0; i < 100; ++i) {
            data[i] = i * 1.5;
        }
        
        std::cout << "Data ready. Press Ctrl+C to exit.\n";
        std::this_thread::sleep_for(std::chrono::hours(1));
    }
    ```

=== "Python (reader.py)"
    ```python
    from zeroipc import Memory, Array
    import numpy as np
    
    mem = Memory("/demo")
    data = Array(mem, "values", dtype=np.float64)
    
    print(f"First value: {data[0]}")
    print(f"Sum: {np.sum(data[:])}")
    print(f"Mean: {np.mean(data[:])}")
    ```

### 2. Work Queue

Distribute tasks across worker processes.

=== "Manager (C++)"
    ```cpp
    #include <zeroipc/memory.h>
    #include <zeroipc/queue.h>
    
    struct Task {
        int id;
        char data[64];
    };
    
    int main() {
        zeroipc::Memory mem("/tasks", 10*1024*1024);
        zeroipc::Queue<Task> queue(mem, "work", 1000);
        
        // Add tasks
        for (int i = 0; i < 1000; ++i) {
            Task t{i, "process_image"};
            queue.enqueue(t);
        }
        
        std::cout << "Added 1000 tasks\n";
    }
    ```

=== "Worker (Python)"
    ```python
    from zeroipc import Memory, Queue
    import numpy as np
    
    # Define matching dtype
    task_dtype = np.dtype([
        ('id', np.int32),
        ('data', 'S64')
    ])
    
    mem = Memory("/tasks")
    queue = Queue(mem, "work", dtype=task_dtype)
    
    while True:
        task = queue.dequeue()
        if task is not None:
            process_task(task)
        else:
            break  # No more tasks
    ```

### 3. Real-Time Stream Processing

Process sensor data with reactive streams.

=== "Sensor (C++)"
    ```cpp
    #include <zeroipc/memory.h>
    #include <zeroipc/stream.h>
    
    struct Reading {
        double temperature;
        double pressure;
        uint64_t timestamp;
    };
    
    int main() {
        zeroipc::Memory mem("/sensors", 10*1024*1024);
        zeroipc::Stream<Reading> stream(mem, "raw", 1000);
        
        while (running) {
            Reading r = read_sensor();
            stream.emit(r);
            std::this_thread::sleep_for(100ms);
        }
    }
    ```

=== "Processor (C++)"
    ```cpp
    zeroipc::Memory mem("/sensors");
    zeroipc::Stream<Reading> raw(mem, "raw");
    
    // Create derived streams
    auto high_temp = raw.filter(mem, "alerts", 
        [](Reading& r) { return r.temperature > 30.0; });
    
    auto celsius_to_f = raw.map(mem, "fahrenheit",
        [](Reading& r) { 
            Reading f = r;
            f.temperature = r.temperature * 9/5 + 32;
            return f;
        });
    
    // Subscribe to alerts
    high_temp.subscribe([](Reading& r) {
        send_alert("High temperature: " + std::to_string(r.temperature));
    });
    ```

## Example Repository

All examples with build files and instructions:

```bash
git clone https://github.com/yourusername/zeroipc.git
cd zeroipc/examples

# Each example has its own directory
ls -la
# basic/
# cross_language/
# producer_consumer/
# sensors/
# streams/
# analytics/
```

Each example includes:
- **README.md** - What it demonstrates
- **Makefile** or **CMakeLists.txt** - Build instructions
- **C++ source** - Complete working code
- **Python source** - Complete working code
- **run.sh** - Helper script to run the example

## Building Examples

### C++ Examples

```bash
cd examples/cross_language/cpp
mkdir build && cd build
cmake ..
make
./producer
```

### Python Examples

```bash
cd examples/cross_language/python
pip install -r requirements.txt
python consumer.py
```

## Next Steps

Explore specific examples:

- **[Cross-Language](cross-language.md)** - Language interop
- **[Producer-Consumer](producer-consumer.md)** - Work distribution
- **[Sensor Data](sensor-data.md)** - IoT patterns
- **[Reactive Processing](reactive-processing.md)** - Event-driven
- **[Analytics](analytics.md)** - Real-time processing
