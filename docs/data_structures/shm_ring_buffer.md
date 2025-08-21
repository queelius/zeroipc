# shm_ring_buffer: High-Performance Circular Buffer for Streaming

## Overview

`shm_ring_buffer<T>` implements a lock-free circular buffer optimized for streaming data between processes. It provides single-producer/single-consumer (SPSC) guarantees with wait-free operations, making it ideal for real-time data pipelines, sensor streams, and continuous telemetry in simulations.

## Architecture

### Memory Layout

```
[shm_table metadata]
[RingBufferHeader]
  ├─ atomic<size_t> write_pos  // Producer's write position
  ├─ char padding[60]          // Cache line separation
  ├─ atomic<size_t> read_pos   // Consumer's read position  
  ├─ size_t capacity           // Buffer size (power of 2)
  └─ char padding[48]          // Cache line alignment
[T][T][T]...[T]                // Circular data buffer
```

### Key Design Decisions

1. **Cache Line Separation**: Read and write positions in different cache lines
2. **Power-of-2 Size**: Enables fast modulo with bitwise AND
3. **SPSC Optimization**: No CAS needed, just atomic loads/stores
4. **Overflow Modes**: Configurable behavior when full

## API Reference

### Construction

```cpp
// Create new ring buffer
posix_shm shm("/streaming", 100 * 1024 * 1024);
shm_ring_buffer<SensorData> sensor_stream(shm, "sensors", 65536);

// Open existing buffer
shm_ring_buffer<SensorData> existing(shm, "sensors");

// Check existence
if (shm_ring_buffer<SensorData>::exists(shm, "sensors")) {
    shm_ring_buffer<SensorData> buffer(shm, "sensors");
}
```

### Producer API

```cpp
// Write single element
bool success = buffer.write(data);

// Write multiple elements
std::vector<T> batch = {...};
size_t written = buffer.write_batch(batch.data(), batch.size());

// Get contiguous write space
auto [ptr, size] = buffer.write_ptr();
if (size > 0) {
    // Direct write to buffer
    memcpy(ptr, source, size * sizeof(T));
    buffer.commit_write(size);
}

// Check available space
size_t space = buffer.write_available();
```

### Consumer API

```cpp
// Read single element
if (auto data = buffer.read()) {
    process(*data);
}

// Read multiple elements
std::vector<T> batch(100);
size_t read = buffer.read_batch(batch.data(), batch.size());

// Get contiguous read space
auto [ptr, size] = buffer.read_ptr();
if (size > 0) {
    // Direct read from buffer
    process_batch(ptr, size);
    buffer.commit_read(size);
}

// Check available data
size_t available = buffer.read_available();
```

### Buffer State

```cpp
bool is_empty = buffer.empty();
bool is_full = buffer.full();
size_t current_size = buffer.size();
size_t capacity = buffer.capacity();

// Reset positions (not thread-safe!)
buffer.reset();
```

## Lock-Free Algorithm (SPSC)

### Write Operation

```cpp
bool write(const T& value) {
    size_t write = write_pos.load(memory_order_relaxed);
    size_t next_write = (write + 1) & (capacity - 1);
    
    // Check if full
    if (next_write == read_pos.load(memory_order_acquire)) {
        return false;  // Buffer full
    }
    
    // Write data
    buffer[write] = value;
    
    // Update write position (publish)
    write_pos.store(next_write, memory_order_release);
    return true;
}
```

### Read Operation

```cpp
optional<T> read() {
    size_t read = read_pos.load(memory_order_relaxed);
    
    // Check if empty
    if (read == write_pos.load(memory_order_acquire)) {
        return nullopt;  // Buffer empty
    }
    
    // Read data
    T value = buffer[read];
    
    // Update read position
    read_pos.store((read + 1) & (capacity - 1), memory_order_release);
    return value;
}
```

## Use Cases in N-Body Simulation

### 1. Telemetry Streaming

```cpp
struct TelemetryPacket {
    uint64_t timestamp;
    float total_energy;
    float momentum[3];
    uint32_t active_particles;
    float temperature;
};

// Producer: Simulation engine
shm_ring_buffer<TelemetryPacket> telemetry(shm, "telemetry", 1024);

void simulation_loop() {
    while (running) {
        simulate_timestep();
        
        if (frame_count % TELEMETRY_INTERVAL == 0) {
            TelemetryPacket packet{
                .timestamp = get_timestamp(),
                .total_energy = calculate_energy(),
                .momentum = {px, py, pz},
                .active_particles = count_active(),
                .temperature = calculate_temperature()
            };
            
            if (!telemetry.write(packet)) {
                stats.telemetry_drops++;
            }
        }
    }
}

// Consumer: Monitoring dashboard
void monitor_process() {
    shm_ring_buffer<TelemetryPacket> telemetry(shm, "telemetry");
    
    while (running) {
        while (auto packet = telemetry.read()) {
            update_dashboard(*packet);
            check_invariants(*packet);
            
            if (packet->total_energy > THRESHOLD) {
                alert_operator("Energy violation detected");
            }
        }
        
        std::this_thread::sleep_for(10ms);
    }
}
```

### 2. Frame Buffer for Visualization

```cpp
struct Frame {
    static constexpr size_t MAX_PARTICLES = 10000;
    uint32_t particle_count;
    float positions[MAX_PARTICLES * 3];
    float colors[MAX_PARTICLES * 4];
    uint64_t frame_number;
};

// Double buffering with ring buffer
shm_ring_buffer<Frame> frames(shm, "frames", 4);  // Triple buffer + 1

// Physics thread
void physics_thread() {
    Frame frame;
    while (running) {
        compute_physics();
        
        // Prepare frame
        frame.particle_count = active_particles;
        copy_positions(frame.positions);
        compute_colors(frame.colors);
        frame.frame_number++;
        
        // Non-blocking write
        if (!frames.write(frame)) {
            // Skip frame if buffer full (renderer is slow)
            stats.dropped_frames++;
        }
    }
}

// Render thread
void render_thread() {
    while (running) {
        if (auto frame = frames.read()) {
            render_particles(*frame);
            present_frame();
        } else {
            // No new frame, repeat last or interpolate
            repeat_last_frame();
        }
    }
}
```

### 3. Event Recording

```cpp
struct Event {
    enum Type { COLLISION, MERGE, SPLIT, ESCAPE };
    Type type;
    uint64_t timestamp;
    uint32_t particles[2];
    float data[4];
};

shm_ring_buffer<Event> events(shm, "events", 10000);

// Multiple producers with synchronization
class EventRecorder {
    shm_ring_buffer<Event>& buffer;
    shm_spinlock lock;  // For MPSC
    
public:
    void record(const Event& event) {
        std::lock_guard<shm_spinlock> guard(lock);
        if (!buffer.write(event)) {
            // Buffer full - implement overflow policy
            if (event.type == COLLISION) {
                // High priority - force write
                buffer.read();  // Drop oldest
                buffer.write(event);
            }
        }
    }
};

// Single consumer
void event_processor() {
    std::vector<Event> batch;
    batch.reserve(100);
    
    while (running) {
        // Batch read for efficiency
        size_t count = events.read_batch(batch.data(), batch.capacity());
        
        for (size_t i = 0; i < count; ++i) {
            switch (batch[i].type) {
                case Event::COLLISION:
                    handle_collision(batch[i]);
                    break;
                case Event::MERGE:
                    handle_merge(batch[i]);
                    break;
                // ...
            }
        }
        
        if (count == 0) {
            std::this_thread::sleep_for(1ms);
        }
    }
}
```

### 4. Command Buffer

```cpp
struct Command {
    char name[32];
    float args[8];
    std::function<void(float*)> execute;
};

class CommandBuffer {
    shm_ring_buffer<Command> commands;
    
public:
    // Batched command execution
    void flush() {
        std::vector<Command> batch(commands.size());
        size_t count = commands.read_batch(batch.data(), batch.size());
        
        // Sort by command type for better cache usage
        std::sort(batch.begin(), batch.begin() + count,
                  [](const Command& a, const Command& b) {
                      return strcmp(a.name, b.name) < 0;
                  });
        
        // Execute sorted commands
        for (size_t i = 0; i < count; ++i) {
            batch[i].execute(batch[i].args);
        }
    }
};
```

## Advanced Patterns

### 1. Multi-Producer Ring Buffer (MPSC)

```cpp
template<typename T>
class MPSCRingBuffer {
    shm_ring_buffer<T> buffer;
    shm_atomic<size_t> write_claim{0};
    
public:
    bool write(const T& value) {
        // Claim a slot
        size_t slot = write_claim.fetch_add(1);
        size_t index = slot & (buffer.capacity() - 1);
        
        // Wait for our turn (sequence consistency)
        while (buffer.write_pos.load() != slot) {
            std::this_thread::yield();
        }
        
        // Write and publish
        buffer.data[index] = value;
        buffer.write_pos.store(slot + 1);
        return true;
    }
};
```

### 2. Watermark-Based Flow Control

```cpp
template<typename T>
class WatermarkBuffer {
    shm_ring_buffer<T> buffer;
    static constexpr size_t LOW_WATER = 25;   // 25% full
    static constexpr size_t HIGH_WATER = 75;  // 75% full
    shm_atomic<bool> backpressure{false};
    
public:
    bool write(const T& value) {
        size_t percent = (buffer.size() * 100) / buffer.capacity();
        
        if (percent > HIGH_WATER) {
            backpressure.store(true);
            // Signal producer to slow down
            return false;
        } else if (percent < LOW_WATER) {
            backpressure.store(false);
            // Signal producer to speed up
        }
        
        return buffer.write(value);
    }
    
    bool should_throttle() const {
        return backpressure.load();
    }
};
```

### 3. Zero-Copy Streaming

```cpp
template<typename T>
class ZeroCopyStream {
    shm_ring_buffer<T> buffer;
    
public:
    // Producer gets direct buffer access
    template<typename Producer>
    size_t produce(Producer&& prod) {
        auto [ptr, size] = buffer.write_ptr();
        if (size == 0) return 0;
        
        // Producer writes directly to buffer
        size_t produced = prod(ptr, size);
        
        buffer.commit_write(produced);
        return produced;
    }
    
    // Consumer gets direct buffer access
    template<typename Consumer>
    size_t consume(Consumer&& cons) {
        auto [ptr, size] = buffer.read_ptr();
        if (size == 0) return 0;
        
        // Consumer reads directly from buffer
        size_t consumed = cons(ptr, size);
        
        buffer.commit_read(consumed);
        return consumed;
    }
};

// Usage with file I/O
void stream_file_to_shm(int fd) {
    ZeroCopyStream<char> stream(shm, "file_stream");
    
    stream.produce([fd](char* buffer, size_t size) {
        return read(fd, buffer, size);
    });
}
```

## Performance Characteristics

### Throughput

| Configuration | Throughput | Latency |
|--------------|------------|---------|
| SPSC | 100M ops/sec | ~10ns |
| MPSC (2 producers) | 50M ops/sec | ~20ns |
| MPSC (4 producers) | 25M ops/sec | ~40ns |
| MPMC | 10M ops/sec | ~100ns |

### Memory Bandwidth

- **Sequential Write**: ~30 GB/s (memory bandwidth limited)
- **Sequential Read**: ~50 GB/s (memory bandwidth limited)
- **Random Access**: Not applicable (sequential only)

### Cache Effects

```cpp
// Optimal: Process in batches
const size_t BATCH_SIZE = 64;  // Full cache line
T batch[BATCH_SIZE];

while (running) {
    size_t count = buffer.read_batch(batch, BATCH_SIZE);
    
    // Process entire batch (cache friendly)
    for (size_t i = 0; i < count; ++i) {
        process(batch[i]);
    }
}

// Poor: One at a time
while (running) {
    if (auto item = buffer.read()) {
        process(*item);  // Cache miss likely
    }
}
```

## Comparison with Alternatives

| Feature | Ring Buffer | Queue | Pipe | Shared Memory |
|---------|------------|-------|------|---------------|
| Bounded | Yes | Yes | No* | No |
| Zero-copy | Yes | No | No | Yes |
| SPSC | Optimal | Good | Good | Manual |
| MPSC | Good | Good | Poor | Manual |
| Ordering | FIFO | FIFO | FIFO | None |
| Overflow | Drop/Block | Block | Block | N/A |

## Common Pitfalls

### 1. Not Power-of-2 Size

```cpp
// WRONG: Expensive modulo operation
size_t next = (current + 1) % 1000;  // Slow division

// CORRECT: Fast bitwise AND
size_t next = (current + 1) & (1024 - 1);  // Single instruction
```

### 2. False Sharing

```cpp
// WRONG: Read and write positions adjacent
struct BadHeader {
    std::atomic<size_t> write_pos;
    std::atomic<size_t> read_pos;  // Same cache line!
};

// CORRECT: Cache line separation
struct GoodHeader {
    std::atomic<size_t> write_pos;
    char padding[56];  // Separate cache lines
    std::atomic<size_t> read_pos;
};
```

### 3. Inconsistent Memory Ordering

```cpp
// WRONG: Relaxed ordering loses updates
write_pos.store(new_pos, std::memory_order_relaxed);
// Consumer might not see the write!

// CORRECT: Acquire-release semantics
write_pos.store(new_pos, std::memory_order_release);
// Guarantees consumer sees the write
```

## Testing Strategies

```cpp
TEST_CASE("Ring buffer maintains order") {
    shm_ring_buffer<int> buffer(shm, "test", 1024);
    
    // Producer thread
    std::thread producer([&]() {
        for (int i = 0; i < 10000; ++i) {
            while (!buffer.write(i)) {
                std::this_thread::yield();
            }
        }
    });
    
    // Consumer thread
    std::thread consumer([&]() {
        for (int expected = 0; expected < 10000; ++expected) {
            int value;
            while (!buffer.read(value)) {
                std::this_thread::yield();
            }
            REQUIRE(value == expected);  // Order preserved
        }
    });
    
    producer.join();
    consumer.join();
}
```

## References

- [Disruptor: High Performance Alternative to Bounded Queues](https://lmax-exchange.github.io/disruptor/disruptor.html)
- [A Lock-Free Ring Buffer](https://www.codeproject.com/Articles/43510/Lock-Free-Single-Producer-Single-Consumer-Circula)
- [The Virtues of Lamport's Bakery Algorithm](https://brooker.co.za/blog/2023/05/10/bakery.html)