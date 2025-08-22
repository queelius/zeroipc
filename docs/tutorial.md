# Tutorial: Building High-Performance IPC Systems

## Table of Contents
1. [Basic Concepts](#basic-concepts)
2. [Simple Producer-Consumer](#simple-producer-consumer)
3. [Particle Simulation System](#particle-simulation-system)
4. [Sensor Data Pipeline](#sensor-data-pipeline)
5. [Multi-Process Coordination](#multi-process-coordination)
6. [Error Handling & Recovery](#error-handling-recovery)

## Basic Concepts

### The Power of the Table System

The metadata table is what makes our shared memory system powerful. It allows **multiple data structures to coexist in the same shared memory segment**, each discoverable by name. This is crucial for complex systems.

```cpp
// One shared memory segment, many data structures!
posix_shm shm("my_system", 100 * 1024 * 1024);  // 100MB total

// All these live in the SAME shared memory:
shm_array<float> sensor_data(shm, "sensors", 10000);
shm_queue<Event> event_queue(shm, "events", 500);
shm_atomic_uint64 frame_counter(shm, "frame", 0);
shm_object_pool<Entity> entities(shm, "entities", 1000);
shm_ring_buffer<LogEntry> logs(shm, "logs", 10000);

// Another process can discover ALL of them:
posix_shm shm2("my_system");  // Open existing
shm_array<float> sensors2(shm2, "sensors");  // Find by name
shm_queue<Event> events2(shm2, "events");    // Find by name
// ... etc
```

### Creating Shared Memory

```cpp
#include "posix_shm.h"

// Process 1: Create shared memory
posix_shm shm("my_simulation", 10 * 1024 * 1024);  // 10MB

// Process 2: Open existing shared memory
posix_shm shm("my_simulation");  // Size discovered automatically
```

### Choosing Table Configuration

```cpp
// Minimal overhead (904B) for embedded systems
posix_shm_small shm("embedded", 1024 * 1024);

// Default (4KB) for most applications  
posix_shm shm("default", 10 * 1024 * 1024);

// Large (26KB) for complex simulations
posix_shm_large shm("complex", 100 * 1024 * 1024);

// Custom configuration
using my_table = shm_table_impl<48, 128>;  // 48 char names, 128 entries
using my_shm = posix_shm_impl<my_table>;
my_shm shm("custom", 50 * 1024 * 1024);
```

## Complete Multi-Structure Example

### Game Server State (Multiple Structures in One Segment)

This example shows how a game server uses multiple data structures in a single shared memory segment for different purposes:

```cpp
// game_server.cpp
#include "posix_shm.h"
#include "shm_array.h"
#include "shm_queue.h"
#include "shm_object_pool.h"
#include "shm_atomic.h"
#include "shm_ring_buffer.h"

class GameServer {
private:
    // Single shared memory segment for everything
    posix_shm_large shm;  // Using large table for many structures
    
    // Game entities (dynamic allocation)
    shm_object_pool<Player, shm_table256> player_pool;
    shm_object_pool<Monster, shm_table256> monster_pool;
    shm_object_pool<Projectile, shm_table256> projectile_pool;
    
    // Spatial grid for collision detection
    shm_array<uint32_t, shm_table256> collision_grid;
    
    // Event queues for different systems
    shm_queue<DamageEvent, shm_table256> damage_events;
    shm_queue<SpawnRequest, shm_table256> spawn_requests;
    shm_queue<NetworkMessage, shm_table256> network_msgs;
    
    // Performance metrics
    shm_ring_buffer<FrameStats, shm_table256> frame_history;
    
    // Global state
    shm_atomic_uint64<shm_table256> tick_counter;
    shm_atomic_uint32<shm_table256> player_count;
    shm_atomic_bool<shm_table256> match_active;
    
    // Leaderboard
    shm_array<ScoreEntry, shm_table256> leaderboard;
    
public:
    GameServer() 
        : shm("game_server", 500 * 1024 * 1024),  // 500MB total
          // Initialize all structures in the SAME shared memory
          player_pool(shm, "players", 100),
          monster_pool(shm, "monsters", 1000),  
          projectile_pool(shm, "projectiles", 500),
          collision_grid(shm, "collision", 256 * 256),
          damage_events(shm, "damage_queue", 1000),
          spawn_requests(shm, "spawn_queue", 100),
          network_msgs(shm, "network_queue", 5000),
          frame_history(shm, "frame_stats", 3600),  // 1 minute at 60fps
          tick_counter(shm, "tick", 0),
          player_count(shm, "players_online", 0),
          match_active(shm, "match_active", false),
          leaderboard(shm, "leaderboard", 10) {
        
        std::cout << "Game server initialized with structures:\n";
        print_memory_layout();
    }
    
    void print_memory_layout() {
        // The table tracks everything!
        auto* table = shm.get_table();
        
        std::cout << "Shared Memory Layout:\n";
        std::cout << "Total size: 500MB\n";
        std::cout << "Table overhead: " << sizeof(shm_table256) << " bytes\n";
        std::cout << "Structures allocated: " << table->get_entry_count() << "\n\n";
        
        // We could iterate through all entries if the table exposed that
        std::cout << "Named structures:\n";
        std::cout << "  - players (object pool): " << player_pool.capacity() << " slots\n";
        std::cout << "  - monsters (object pool): " << monster_pool.capacity() << " slots\n"; 
        std::cout << "  - projectiles (object pool): " << projectile_pool.capacity() << " slots\n";
        std::cout << "  - collision (array): " << collision_grid.size() << " cells\n";
        std::cout << "  - damage_queue: " << damage_events.capacity() << " capacity\n";
        std::cout << "  - spawn_queue: " << spawn_requests.capacity() << " capacity\n";
        std::cout << "  - network_queue: " << network_msgs.capacity() << " capacity\n";
        std::cout << "  - frame_stats (ring): " << frame_history.capacity() << " samples\n";
        std::cout << "  - tick (atomic): current=" << tick_counter.load() << "\n";
        std::cout << "  - players_online (atomic): " << player_count.load() << "\n";
        std::cout << "  - match_active (atomic): " << match_active.load() << "\n";
        std::cout << "  - leaderboard (array): " << leaderboard.size() << " entries\n";
    }
};

// ai_system.cpp - Separate process that reads game state
class AISystem {
private:
    posix_shm_large shm;
    
    // Discover existing structures by name
    shm_object_pool<Player, shm_table256> players;
    shm_object_pool<Monster, shm_table256> monsters;
    shm_array<uint32_t, shm_table256> collision_grid;
    shm_queue<SpawnRequest, shm_table256> spawn_queue;
    shm_atomic_uint64<shm_table256> tick;
    
public:
    AISystem()
        : shm("game_server"),  // Attach to existing
          // Find all structures by name - they already exist!
          players(shm, "players"),
          monsters(shm, "monsters"),
          collision_grid(shm, "collision"),
          spawn_queue(shm, "spawn_queue"),
          tick(shm, "tick") {
        
        std::cout << "AI System connected to game server\n";
        std::cout << "Found " << monsters.num_allocated() << " active monsters\n";
    }
    
    void update_ai() {
        uint64_t current_tick = tick.load();
        
        // Read monster positions, make decisions
        for (uint32_t handle = 0; handle < monsters.capacity(); ++handle) {
            if (auto* monster = monsters.get(handle)) {
                // Read collision grid around monster
                int grid_x = monster->x / CELL_SIZE;
                int grid_y = monster->y / CELL_SIZE;
                uint32_t cell = collision_grid[grid_y * 256 + grid_x];
                
                // Make AI decision...
                if (should_spawn_adds(monster)) {
                    SpawnRequest req{
                        .type = SPAWN_MINION,
                        .x = monster->x,
                        .y = monster->y
                    };
                    spawn_queue.enqueue(req);  // Communicate back!
                }
            }
        }
    }
};

// analytics.cpp - Another process for metrics
class Analytics {
private:
    posix_shm_large shm;
    shm_ring_buffer<FrameStats, shm_table256> frame_history;
    shm_atomic_uint32<shm_table256> player_count;
    shm_array<ScoreEntry, shm_table256> leaderboard;
    
public:
    Analytics()
        : shm("game_server"),
          frame_history(shm, "frame_stats"),
          player_count(shm, "players_online"),
          leaderboard(shm, "leaderboard") {
    }
    
    void generate_report() {
        // Read last 60 seconds of frame data
        FrameStats stats[3600];
        size_t count = frame_history.get_last_n(3600, stats);
        
        // Calculate metrics
        double avg_fps = calculate_average_fps(stats, count);
        uint32_t current_players = player_count.load();
        
        std::cout << "=== Performance Report ===\n";
        std::cout << "Average FPS: " << avg_fps << "\n";
        std::cout << "Players online: " << current_players << "\n";
        std::cout << "\nTop Players:\n";
        
        for (size_t i = 0; i < leaderboard.size(); ++i) {
            auto& entry = leaderboard[i];
            if (entry.score > 0) {
                std::cout << i+1 << ". " << entry.name 
                          << " - " << entry.score << "\n";
            }
        }
    }
};
```

### Memory Layout Visualization

Here's what the shared memory segment looks like with all these structures:

```
┌────────────────────────────────────────────────┐
│ Shared Memory: "game_server" (500MB)          │
├────────────────────────────────────────────────┤
│ Offset    | Size    | Structure               │
├────────────────────────────────────────────────┤
│ 0x0000    | 4B      | Reference Counter       │
│ 0x0004    | 26KB    | shm_table256         │
│           |         |   ├─ "players"          │
│           |         |   ├─ "monsters"         │
│           |         |   ├─ "projectiles"      │
│           |         |   ├─ "collision"        │
│           |         |   ├─ "damage_queue"     │
│           |         |   ├─ "spawn_queue"      │
│           |         |   ├─ "network_queue"    │
│           |         |   ├─ "frame_stats"      │
│           |         |   ├─ "tick"             │
│           |         |   ├─ "players_online"   │
│           |         |   ├─ "match_active"     │
│           |         |   └─ "leaderboard"      │
├────────────────────────────────────────────────┤
│ 0x6808    | ~4KB    | Player Pool             │
│ 0x7808    | ~40KB   | Monster Pool            │
│ 0x11408   | ~10KB   | Projectile Pool         │
│ 0x13C08   | 256KB   | Collision Grid          │
│ 0x53C08   | ~16KB   | Damage Queue            │
│ 0x57C08   | ~2KB    | Spawn Queue             │
│ 0x58408   | ~80KB   | Network Queue           │
│ 0x6C408   | ~56KB   | Frame History           │
│ 0x7A008   | 8B      | Tick Counter            │
│ 0x7A010   | 4B      | Player Count            │
│ 0x7A014   | 1B      | Match Active            │
│ 0x7A018   | 400B    | Leaderboard             │
│ ...       | ...     | (Unused space)          │
└────────────────────────────────────────────────┘
```

### Key Points About Multiple Structures

1. **Single Allocation**: One `posix_shm` object manages the entire segment
2. **Automatic Layout**: The table system tracks offsets automatically
3. **No Fragmentation**: Structures are allocated sequentially
4. **Discovery by Name**: Any process can find any structure
5. **Type Safety**: Template parameters ensure consistency
6. **Independent Lifecycles**: Each structure can be used independently

## Simple Producer-Consumer

### Producer Process

```cpp
#include "posix_shm.h"
#include "shm_queue.h"
#include <iostream>
#include <thread>
#include <chrono>

struct Message {
    uint64_t timestamp;
    int producer_id;
    double value;
};

int main() {
    // Create shared memory and queue
    posix_shm shm("producer_consumer", 1024 * 1024);
    shm_queue<Message> queue(shm, "messages", 100);
    
    // Produce messages
    for (int i = 0; i < 1000; ++i) {
        Message msg{
            .timestamp = std::chrono::system_clock::now().time_since_epoch().count(),
            .producer_id = 1,
            .value = i * 3.14
        };
        
        // Retry if queue is full
        while (!queue.enqueue(msg)) {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
        
        std::cout << "Produced message " << i << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    return 0;
}
```

### Consumer Process

```cpp
int main() {
    // Open existing shared memory
    posix_shm shm("producer_consumer");
    
    // Discover queue by name
    shm_queue<Message> queue(shm, "messages");
    
    // Consume messages
    int count = 0;
    while (count < 1000) {
        if (auto msg = queue.dequeue()) {
            std::cout << "Consumed message from producer " 
                      << msg->producer_id 
                      << " with value " << msg->value << "\n";
            count++;
        } else {
            // Queue empty, wait a bit
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    }
    
    return 0;
}
```

## Particle Simulation System

### Simulation Core

```cpp
#include "posix_shm.h"
#include "shm_object_pool.h"
#include "shm_array.h"
#include "shm_atomic.h"

class ParticleSimulation {
private:
    posix_shm shm;
    shm_object_pool<Particle> particle_pool;
    shm_array<uint32_t> active_particles;
    shm_atomic_uint64 frame_counter;
    shm_atomic_uint32 active_count;
    
public:
    struct Particle {
        float position[3];
        float velocity[3];
        float mass;
        float lifetime;
        uint32_t type;
        uint32_t flags;
    };
    
    ParticleSimulation(const std::string& name, size_t max_particles)
        : shm(name, calculate_memory_size(max_particles)),
          particle_pool(shm, "particles", max_particles),
          active_particles(shm, "active_list", max_particles),
          frame_counter(shm, "frame", 0),
          active_count(shm, "active", 0) {
    }
    
    static size_t calculate_memory_size(size_t max_particles) {
        return sizeof(shm_table) +                          // Metadata table
               sizeof(Particle) * max_particles +           // Particle pool
               sizeof(uint32_t) * max_particles +           // Active list
               sizeof(std::atomic<uint64_t>) +              // Frame counter
               sizeof(std::atomic<uint32_t>) +              // Active count
               1024 * 1024;                                  // Extra buffer
    }
    
    uint32_t spawn_particle(const Particle& p) {
        auto handle = particle_pool.acquire();
        if (handle == particle_pool.invalid_handle) {
            return particle_pool.invalid_handle;  // Pool full
        }
        
        // Initialize particle
        particle_pool[handle] = p;
        
        // Add to active list
        uint32_t index = active_count.fetch_add(1);
        active_particles[index] = handle;
        
        return handle;
    }
    
    void update_physics(float dt) {
        uint32_t count = active_count.load();
        
        // Parallel update possible - each particle independent
        #pragma omp parallel for
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t handle = active_particles[i];
            auto& p = particle_pool[handle];
            
            // Simple physics
            p.position[0] += p.velocity[0] * dt;
            p.position[1] += p.velocity[1] * dt;
            p.position[2] += p.velocity[2] * dt;
            
            // Gravity
            p.velocity[1] -= 9.8f * dt;
            
            // Lifetime
            p.lifetime -= dt;
        }
        
        // Remove dead particles
        compact_active_list();
        
        frame_counter++;
    }
    
    void compact_active_list() {
        uint32_t read = 0, write = 0;
        uint32_t count = active_count.load();
        
        while (read < count) {
            uint32_t handle = active_particles[read];
            auto& p = particle_pool[handle];
            
            if (p.lifetime > 0) {
                active_particles[write++] = handle;
            } else {
                particle_pool.release(handle);
            }
            read++;
        }
        
        active_count.store(write);
    }
};
```

### Renderer Process

```cpp
int main() {
    // Open existing simulation
    posix_shm shm("particle_sim");
    
    // Discover data structures
    shm_object_pool<Particle> particles(shm, "particles");
    shm_array<uint32_t> active_list(shm, "active_list");
    shm_atomic_uint32 active_count(shm, "active");
    shm_atomic_uint64 frame(shm, "frame");
    
    uint64_t last_frame = 0;
    
    while (true) {
        uint64_t current_frame = frame.load();
        
        // Only render if new frame
        if (current_frame != last_frame) {
            uint32_t count = active_count.load();
            
            // Read particle positions
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t handle = active_list[i];
                const auto& p = particles[handle];
                
                render_particle(p.position, p.type);
            }
            
            present_frame();
            last_frame = current_frame;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(16));  // 60 FPS
    }
}
```

## Sensor Data Pipeline

### High-Frequency Data Collection

```cpp
#include "shm_ring_buffer.h"

class SensorCollector {
private:
    posix_shm shm;
    shm_ring_buffer<SensorReading> buffer;
    shm_atomic_uint64 total_samples;
    shm_atomic_uint64 dropped_samples;
    
public:
    struct SensorReading {
        uint64_t timestamp_ns;
        float values[8];  // 8 channels
        uint16_t status;
        uint16_t sensor_id;
    };
    
    SensorCollector()
        : shm("sensor_pipeline", 100 * 1024 * 1024),  // 100MB
          buffer(shm, "readings", 1000000),  // 1M samples
          total_samples(shm, "total", 0),
          dropped_samples(shm, "dropped", 0) {
    }
    
    void collect_samples() {
        SensorReading batch[1000];
        
        while (true) {
            // Read from hardware (simulated)
            size_t collected = read_sensors(batch, 1000);
            
            // Bulk push for efficiency
            size_t pushed = buffer.push_bulk({batch, collected});
            
            total_samples.fetch_add(pushed);
            
            if (pushed < collected) {
                // Buffer full, using overwrite mode
                for (size_t i = pushed; i < collected; ++i) {
                    buffer.push_overwrite(batch[i]);
                    dropped_samples++;
                }
            }
            
            // 1MHz sampling rate
            std::this_thread::sleep_for(std::chrono::microseconds(1000));
        }
    }
};
```

### Data Processing Pipeline

```cpp
class DataProcessor {
private:
    posix_shm shm;
    shm_ring_buffer<SensorReading> input_buffer;
    shm_ring_buffer<ProcessedData> output_buffer;
    
public:
    void process_pipeline() {
        SensorReading raw_batch[100];
        ProcessedData processed_batch[100];
        
        while (true) {
            // Bulk read from input
            size_t count = input_buffer.pop_bulk(raw_batch);
            
            if (count > 0) {
                // Process data (FFT, filtering, etc.)
                for (size_t i = 0; i < count; ++i) {
                    processed_batch[i] = process_reading(raw_batch[i]);
                }
                
                // Push to next stage
                output_buffer.push_bulk({processed_batch, count});
            } else {
                // No data, brief wait
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    }
    
    ProcessedData process_reading(const SensorReading& raw) {
        ProcessedData result;
        // Apply filters, transforms, etc.
        return result;
    }
};
```

## Multi-Process Coordination

### Master-Worker Pattern

```cpp
class TaskScheduler {
private:
    posix_shm shm;
    shm_queue<Task> task_queue;
    shm_queue<Result> result_queue;
    shm_atomic_uint32 workers_ready;
    shm_atomic_bool shutdown;
    
public:
    struct Task {
        uint64_t task_id;
        uint32_t type;
        char parameters[256];
    };
    
    struct Result {
        uint64_t task_id;
        uint32_t worker_id;
        bool success;
        char output[1024];
    };
    
    // Master process
    void run_master() {
        // Wait for workers
        while (workers_ready.load() < 4) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Distribute tasks
        for (uint64_t i = 0; i < 1000; ++i) {
            Task t{.task_id = i, .type = i % 4};
            
            while (!task_queue.enqueue(t)) {
                // Process results while waiting
                process_results();
            }
        }
        
        // Collect all results
        for (int i = 0; i < 1000; ++i) {
            process_results();
        }
        
        shutdown.store(true);
    }
    
    // Worker process
    void run_worker(uint32_t worker_id) {
        workers_ready++;
        
        while (!shutdown.load()) {
            if (auto task = task_queue.dequeue()) {
                Result r{
                    .task_id = task->task_id,
                    .worker_id = worker_id,
                    .success = true
                };
                
                // Process task
                execute_task(*task, r);
                
                // Submit result
                while (!result_queue.enqueue(r)) {
                    if (shutdown.load()) break;
                    std::this_thread::yield();
                }
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        }
    }
};
```

## Error Handling & Recovery

### Robust Initialization

```cpp
class RobustSystem {
public:
    static std::unique_ptr<RobustSystem> create(const std::string& name) {
        try {
            // Try to create new
            return std::make_unique<RobustSystem>(name, true);
        } catch (const std::exception& e) {
            // Try to attach to existing
            try {
                return std::make_unique<RobustSystem>(name, false);
            } catch (...) {
                // Clean up and retry
                cleanup_shared_memory(name);
                return std::make_unique<RobustSystem>(name, true);
            }
        }
    }
    
private:
    static void cleanup_shared_memory(const std::string& name) {
        // Force cleanup of stale shared memory
        shm_unlink(name.c_str());
    }
};
```

### Crash Recovery

```cpp
class CrashResilientQueue {
private:
    shm_queue<Message> queue;
    shm_atomic_uint64 last_processed_id;
    
public:
    void process_with_recovery() {
        uint64_t last_id = last_processed_id.load();
        
        while (true) {
            if (auto msg = queue.dequeue()) {
                if (msg->id <= last_id) {
                    // Already processed, skip
                    continue;
                }
                
                try {
                    process_message(*msg);
                    last_processed_id.store(msg->id);
                } catch (const std::exception& e) {
                    // Log error but continue
                    log_error(e.what());
                    // Message lost but system continues
                }
            }
        }
    }
};
```

### Monitoring & Diagnostics

```cpp
class SystemMonitor {
private:
    struct Stats {
        std::atomic<uint64_t> messages_processed{0};
        std::atomic<uint64_t> errors_count{0};
        std::atomic<uint64_t> queue_full_count{0};
        std::atomic<double> avg_latency_us{0};
        std::atomic<uint64_t> last_heartbeat{0};
    };
    
    posix_shm shm;
    shm_array<Stats> process_stats;
    
public:
    void monitor_system() {
        while (true) {
            auto now = std::chrono::system_clock::now().time_since_epoch().count();
            
            for (size_t i = 0; i < process_stats.size(); ++i) {
                auto& stats = process_stats[i];
                uint64_t last_heartbeat = stats.last_heartbeat.load();
                
                if (now - last_heartbeat > 5000000000) {  // 5 seconds
                    std::cerr << "Process " << i << " appears hung!\n";
                    alert_operator(i);
                }
                
                std::cout << "Process " << i << ": "
                          << stats.messages_processed.load() << " messages, "
                          << stats.errors_count.load() << " errors, "
                          << stats.avg_latency_us.load() << " µs latency\n";
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
};
```

## Best Practices

### 1. **Always Name Your Structures**
```cpp
// Good - discoverable
shm_array<float> data(shm, "sensor_readings", 1000);

// Bad - anonymous, not discoverable
float* data = static_cast<float*>(shm.get_base_addr());
```

### 2. **Handle Full Conditions Gracefully**
```cpp
// Good - retry with backoff
while (!queue.enqueue(msg)) {
    if (++retries > max_retries) {
        handle_overflow();
        break;
    }
    std::this_thread::sleep_for(backoff);
    backoff *= 2;
}

// Bad - silent data loss
queue.enqueue(msg);  // Ignoring return value
```

### 3. **Use Bulk Operations for Efficiency**
```cpp
// Good - amortize overhead
Reading batch[100];
size_t count = buffer.pop_bulk(batch);
process_batch(batch, count);

// Bad - one at a time
for (int i = 0; i < 100; ++i) {
    if (auto val = buffer.pop()) {
        process_single(*val);
    }
}
```

### 4. **Monitor System Health**
```cpp
// Good - observable system
shm_atomic_uint64 heartbeat(shm, "heartbeat");
while (running) {
    do_work();
    heartbeat.store(now());
}

// Bad - black box
while (running) {
    do_work();  // No visibility
}
```

### 5. **Plan for Growth**
```cpp
// Good - configurable
constexpr size_t MAX_PARTICLES = 
    std::getenv("MAX_PARTICLES") ? 
    std::atoi(std::getenv("MAX_PARTICLES")) : 10000;

// Bad - hardcoded limits
constexpr size_t MAX_PARTICLES = 1000;  // Will need recompile
```