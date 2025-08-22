#include <iostream>
#include <random>
#include <chrono>
#include <thread>
#include "zeroipc.h"
#include "pool.h"
#include "ring.h"
#include "atomic.h"
#include "array.h"

// Example: High-performance particle simulation using shared memory

struct Particle {
    float x, y, z;
    float vx, vy, vz;
    float lifetime;
    uint32_t type;
};

struct SensorReading {
    uint64_t timestamp;
    float temperature;
    float pressure;
    float humidity;
};

void particle_simulation_example() {
    std::cout << "\n=== Particle Simulation with Object Pool ===\n";
    
    zeroipc::memory shm("particle_sim", 100 * 1024 * 1024);  // 100MB
    
    // Object pool for dynamic particle allocation
    constexpr size_t MAX_PARTICLES = 10000;
    zeroipc::pool<Particle> particles(shm, "particles", MAX_PARTICLES);
    
    // Statistics
    shm_atomic_uint64 total_spawned(shm, "total_spawned", 0);
    shm_atomic_uint64 total_destroyed(shm, "total_destroyed", 0);
    
    std::cout << "Pool capacity: " << particles.capacity() << " particles\n";
    
    // Spawn some particles
    std::vector<uint32_t> handles;
    for (int i = 0; i < 100; ++i) {
        auto h = particles.acquire_construct();
        if (h.has_value()) {
            auto& p = particles[*h];
            p.x = i * 0.1f;
            p.y = i * 0.2f;
            p.z = 0.0f;
            p.lifetime = 10.0f;
            handles.push_back(*h);
            total_spawned++;
        }
    }
    
    std::cout << "Spawned " << handles.size() << " particles\n";
    std::cout << "Pool usage: " << particles.num_allocated() 
              << "/" << particles.capacity() << "\n";
    
    // Simulate one frame - update particles
    for (auto h : handles) {
        auto& p = particles[h];
        p.x += p.vx * 0.016f;  // 60 FPS timestep
        p.y += p.vy * 0.016f;
        p.lifetime -= 0.016f;
    }
    
    // Destroy some particles
    for (size_t i = 0; i < handles.size() / 2; ++i) {
        particles.release(handles[i]);
        total_destroyed++;
    }
    
    std::cout << "After destroying half:\n";
    std::cout << "Pool usage: " << particles.num_allocated() 
              << "/" << particles.capacity() << "\n";
    std::cout << "Total spawned: " << total_spawned.load() << "\n";
    std::cout << "Total destroyed: " << total_destroyed.load() << "\n";
}

void sensor_streaming_example() {
    std::cout << "\n=== Sensor Data Streaming with Ring Buffer ===\n";
    
    zeroipc::memory shm("sensor_stream", 10 * 1024 * 1024);
    
    // Ring buffer for continuous sensor data
    constexpr size_t BUFFER_SIZE = 1000;
    zeroipc::ring<SensorReading> sensor_buffer(shm, "sensors", BUFFER_SIZE);
    
    // Generate some sensor data
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> temp_dist(20.0, 30.0);
    std::uniform_real_distribution<> pressure_dist(1000.0, 1020.0);
    
    auto now = std::chrono::system_clock::now();
    
    // Push sensor readings
    for (int i = 0; i < 50; ++i) {
        SensorReading reading{
            .timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                now.time_since_epoch()).count() + i * 1000,
            .temperature = static_cast<float>(temp_dist(gen)),
            .pressure = static_cast<float>(pressure_dist(gen)),
            .humidity = 45.0f + i * 0.1f
        };
        
        if (!sensor_buffer.push(reading)) {
            std::cout << "Buffer full at " << i << " readings\n";
            break;
        }
    }
    
    std::cout << "Buffer contains " << sensor_buffer.size() << " readings\n";
    std::cout << "Total written: " << sensor_buffer.total_written() << "\n";
    
    // Get last 5 readings (most recent data)
    SensorReading last_readings[5];
    size_t got = sensor_buffer.get_last_n(5, last_readings);
    std::cout << "\nLast " << got << " readings:\n";
    for (size_t i = 0; i < got; ++i) {
        std::cout << "  T=" << last_readings[i].temperature 
                  << "°C, P=" << last_readings[i].pressure << " hPa\n";
    }
    
    // Bulk read for processing
    SensorReading batch[10];
    size_t read = sensor_buffer.pop_bulk(batch);
    std::cout << "\nProcessed " << read << " readings in batch\n";
    std::cout << "Buffer now contains " << sensor_buffer.size() << " readings\n";
}

void grid_simulation_example() {
    std::cout << "\n=== Grid-based Simulation ===\n";
    
    zeroipc::memory shm("grid_sim", 50 * 1024 * 1024);
    
    // Fixed grid for spatial data (e.g., fluid simulation, cellular automata)
    constexpr size_t GRID_SIZE = 256 * 256;
    
    struct Cell {
        float density;
        float velocity_x;
        float velocity_y;
        uint8_t type;
        uint8_t flags;
        uint16_t metadata;
    };
    
    // Two grids for double buffering
    zeroipc::array<Cell> grid_current(shm, "grid_current", GRID_SIZE);
    zeroipc::array<Cell> grid_next(shm, "grid_next", GRID_SIZE);
    
    // Simulation step counter
    shm_atomic_uint64 step_counter(shm, "step", 0);
    
    std::cout << "Grid size: " << 256 << "x" << 256 
              << " = " << GRID_SIZE << " cells\n";
    std::cout << "Memory per grid: " << sizeof(Cell) * GRID_SIZE / 1024 
              << " KB\n";
    
    // Initialize grid
    for (size_t i = 0; i < 100; ++i) {
        grid_current[i].density = 1.0f;
        grid_current[i].type = 1;
    }
    
    // Simulate one step (simplified)
    for (size_t y = 1; y < 255; ++y) {
        for (size_t x = 1; x < 255; ++x) {
            size_t idx = y * 256 + x;
            
            // Simple diffusion
            float avg_density = (
                grid_current[idx - 256].density +  // North
                grid_current[idx + 256].density +  // South
                grid_current[idx - 1].density +    // West
                grid_current[idx + 1].density      // East
            ) * 0.25f;
            
            grid_next[idx].density = avg_density;
        }
    }
    
    step_counter++;
    std::cout << "Simulation at step: " << step_counter.load() << "\n";
}

void usage_patterns() {
    std::cout << "\n=== Common Simulation Patterns ===\n\n";
    
    std::cout << "1. Object Pool (zeroipc::pool):\n";
    std::cout << "   - Dynamic entity management (particles, enemies, projectiles)\n";
    std::cout << "   - Avoids fragmentation, O(1) allocation\n";
    std::cout << "   - Perfect for systems with many temporary objects\n\n";
    
    std::cout << "2. Ring Buffer (zeroipc::ring):\n";
    std::cout << "   - Sensor data streams\n";
    std::cout << "   - Event/message logging\n";
    std::cout << "   - Time-series data with bulk operations\n\n";
    
    std::cout << "3. Fixed Arrays (zeroipc::array):\n";
    std::cout << "   - Spatial grids (collision, physics, fluid)\n";
    std::cout << "   - Lookup tables\n";
    std::cout << "   - Double-buffered state\n\n";
    
    std::cout << "4. Atomics (zeroipc::atomic_value):\n";
    std::cout << "   - Global counters and statistics\n";
    std::cout << "   - Synchronization flags\n";
    std::cout << "   - Lock-free coordination\n\n";
    
    std::cout << "5. Queues (zeroipc::queue):\n";
    std::cout << "   - Task distribution\n";
    std::cout << "   - Event handling\n";
    std::cout << "   - Producer-consumer patterns\n";
}

int main() {
    try {
        usage_patterns();
        particle_simulation_example();
        sensor_streaming_example();
        grid_simulation_example();
        
        std::cout << "\n=== All simulation patterns demonstrated successfully! ===\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}