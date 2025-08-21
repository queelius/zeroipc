# shm_array: Fixed-Size Arrays in Shared Memory

## Overview

`shm_array<T>` provides a fixed-size, contiguous array in POSIX shared memory with O(1) random access. It's the simplest and most cache-friendly shared memory data structure, ideal for storing particle data, matrices, and other fixed collections in n-body simulations.

## Architecture

### Memory Layout

```
[shm_table metadata]
[ArrayHeader]
  ├─ size_t count            // Number of elements
  └─ padding[56]             // Cache line alignment
[T][T][T]...[T]              // Contiguous elements
```

### Key Design Decisions

1. **Contiguous Memory**: Optimal cache locality and prefetching
2. **Zero Overhead**: Direct memory access after initialization
3. **SIMD-Friendly**: Natural alignment for vectorized operations
4. **Compare-and-Swap**: Atomic operations on individual elements

## API Reference

### Construction

```cpp
// Create new array
posix_shm shm("/nbody", 100 * 1024 * 1024);
shm_array<Particle> particles(shm, "particles", 1000000);

// Open existing array
shm_array<Particle> existing(shm, "particles");

// Check existence
if (shm_array<Particle>::exists(shm, "particles")) {
    shm_array<Particle> arr(shm, "particles");
}
```

### Element Access

```cpp
// Direct indexing
particles[42].position = {1.0f, 2.0f, 3.0f};
float mass = particles[42].mass;

// Bounds-checked access
Particle& p = particles.at(42);  // Throws if out of bounds

// Get raw pointer for bulk operations
Particle* data = particles.data();
memcpy(data, source, sizeof(Particle) * count);

// Size and iteration
size_t n = particles.size();
for (size_t i = 0; i < particles.size(); ++i) {
    process(particles[i]);
}
```

### Atomic Operations

```cpp
// Compare-and-swap on element
Particle expected = particles[i];
Particle desired = compute_new_state(expected);
bool success = particles.compare_exchange(i, expected, desired);

// Atomic update with retry
void atomic_update(size_t idx, const Particle& update) {
    Particle current = particles[idx];
    while (!particles.compare_exchange(idx, current, update)) {
        current = particles[idx];
    }
}
```

### SIMD Operations

```cpp
// Bulk operations with SIMD
void update_positions(shm_array<float3>& positions, 
                      const shm_array<float3>& velocities,
                      float dt) {
    float* pos = reinterpret_cast<float*>(positions.data());
    const float* vel = reinterpret_cast<const float*>(velocities.data());
    size_t n = positions.size() * 3;
    
    // Process 8 floats at a time with AVX
    for (size_t i = 0; i < n; i += 8) {
        __m256 p = _mm256_load_ps(&pos[i]);
        __m256 v = _mm256_load_ps(&vel[i]);
        __m256 dt_vec = _mm256_set1_ps(dt);
        p = _mm256_fmadd_ps(v, dt_vec, p);
        _mm256_store_ps(&pos[i], p);
    }
}
```

## Use Cases in N-Body Simulation

### 1. Particle Storage (Array of Structures)

```cpp
struct Particle {
    float3 position;
    float3 velocity;
    float mass;
    float radius;
};

shm_array<Particle> particles(shm, "particles", 1000000);

// Update particle physics
void update_particle(size_t idx, float dt) {
    Particle& p = particles[idx];
    p.position += p.velocity * dt;
    
    // Apply boundary conditions
    for (int i = 0; i < 3; ++i) {
        if (p.position[i] < 0 || p.position[i] > box_size) {
            p.velocity[i] = -p.velocity[i];
        }
    }
}
```

### 2. Structure of Arrays (SoA) for SIMD

```cpp
// Better cache usage and SIMD efficiency
struct ParticleSystem {
    shm_array<float> pos_x, pos_y, pos_z;
    shm_array<float> vel_x, vel_y, vel_z;
    shm_array<float> mass;
    
    ParticleSystem(posix_shm& shm, size_t n)
        : pos_x(shm, "pos_x", n), pos_y(shm, "pos_y", n), pos_z(shm, "pos_z", n),
          vel_x(shm, "vel_x", n), vel_y(shm, "vel_y", n), vel_z(shm, "vel_z", n),
          mass(shm, "mass", n) {}
    
    void update_positions_simd(float dt) {
        #pragma omp parallel for simd
        for (size_t i = 0; i < pos_x.size(); ++i) {
            pos_x[i] += vel_x[i] * dt;
            pos_y[i] += vel_y[i] * dt;
            pos_z[i] += vel_z[i] * dt;
        }
    }
};
```

### 3. Spatial Grid

```cpp
template<typename T>
class SpatialGrid {
    shm_array<std::vector<uint32_t>> cells;
    float cell_size;
    uint32_t grid_dim;
    
public:
    SpatialGrid(posix_shm& shm, float world_size, float cell_sz)
        : cell_size(cell_sz),
          grid_dim(world_size / cell_sz),
          cells(shm, "grid", grid_dim * grid_dim * grid_dim) {}
    
    uint32_t cell_index(const float3& pos) {
        uint32_t x = pos.x / cell_size;
        uint32_t y = pos.y / cell_size;
        uint32_t z = pos.z / cell_size;
        return x + y * grid_dim + z * grid_dim * grid_dim;
    }
    
    void insert(uint32_t particle_id, const float3& pos) {
        uint32_t idx = cell_index(pos);
        cells[idx].push_back(particle_id);
    }
    
    std::vector<uint32_t> neighbors(const float3& pos) {
        std::vector<uint32_t> result;
        int cx = pos.x / cell_size;
        int cy = pos.y / cell_size;
        int cz = pos.z / cell_size;
        
        // Check 27 neighboring cells
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dz = -1; dz <= 1; ++dz) {
                    int x = cx + dx, y = cy + dy, z = cz + dz;
                    if (x >= 0 && x < grid_dim && 
                        y >= 0 && y < grid_dim && 
                        z >= 0 && z < grid_dim) {
                        uint32_t idx = x + y * grid_dim + z * grid_dim * grid_dim;
                        result.insert(result.end(), 
                                    cells[idx].begin(), cells[idx].end());
                    }
                }
            }
        }
        return result;
    }
};
```

### 4. Matrix Operations

```cpp
template<typename T, size_t Rows, size_t Cols>
class Matrix {
    shm_array<T> data;
    
public:
    Matrix(posix_shm& shm, const std::string& name)
        : data(shm, name, Rows * Cols) {}
    
    T& operator()(size_t row, size_t col) {
        return data[row * Cols + col];
    }
    
    // Matrix multiplication with blocking for cache
    void multiply(const Matrix& A, const Matrix& B, Matrix& C) {
        constexpr size_t BLOCK = 64 / sizeof(T);
        
        for (size_t ii = 0; ii < Rows; ii += BLOCK) {
            for (size_t jj = 0; jj < Cols; jj += BLOCK) {
                for (size_t kk = 0; kk < Cols; kk += BLOCK) {
                    // Process block
                    for (size_t i = ii; i < std::min(ii + BLOCK, Rows); ++i) {
                        for (size_t j = jj; j < std::min(jj + BLOCK, Cols); ++j) {
                            T sum = 0;
                            for (size_t k = kk; k < std::min(kk + BLOCK, Cols); ++k) {
                                sum += A(i, k) * B(k, j);
                            }
                            C(i, j) += sum;
                        }
                    }
                }
            }
        }
    }
};
```

## Performance Characteristics

### Memory Access Patterns

| Pattern | Performance | Cache Behavior |
|---------|------------|----------------|
| Sequential | Optimal | Prefetch friendly |
| Strided | Good | Depends on stride |
| Random | Poor | Cache thrashing |
| SIMD aligned | Optimal | Vectorized |

### Throughput

- **Sequential Read**: Memory bandwidth limited (~50 GB/s)
- **Sequential Write**: Memory bandwidth limited (~30 GB/s)
- **Random Access**: ~10M ops/sec (cache misses)
- **SIMD Operations**: 4-16x speedup for suitable operations

### Cache Effects

```cpp
// Poor: Column-major access in row-major layout
for (size_t col = 0; col < COLS; ++col) {
    for (size_t row = 0; row < ROWS; ++row) {
        process(matrix[row * COLS + col]);  // Cache miss every iteration
    }
}

// Good: Row-major access
for (size_t row = 0; row < ROWS; ++row) {
    for (size_t col = 0; col < COLS; ++col) {
        process(matrix[row * COLS + col]);  // Sequential, cache friendly
    }
}

// Best: Blocked for cache
for (size_t rb = 0; rb < ROWS; rb += BLOCK) {
    for (size_t cb = 0; cb < COLS; cb += BLOCK) {
        // Process block that fits in cache
        for (size_t r = rb; r < rb + BLOCK; ++r) {
            for (size_t c = cb; c < cb + BLOCK; ++c) {
                process(matrix[r * COLS + c]);
            }
        }
    }
}
```

## Advanced Patterns

### 1. Memory Mapping Views

```cpp
template<typename T>
class ArrayView {
    T* ptr;
    size_t len;
    
public:
    ArrayView(shm_array<T>& arr, size_t start, size_t count)
        : ptr(arr.data() + start), len(count) {}
    
    T& operator[](size_t i) { return ptr[i]; }
    size_t size() const { return len; }
    
    // Subview
    ArrayView slice(size_t start, size_t count) {
        return ArrayView(ptr + start, count);
    }
};

// Partition array for parallel processing
void parallel_process(shm_array<float>& data) {
    size_t chunk_size = data.size() / num_threads;
    
    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        ArrayView<float> chunk(data, tid * chunk_size, chunk_size);
        
        for (size_t i = 0; i < chunk.size(); ++i) {
            chunk[i] = expensive_computation(chunk[i]);
        }
    }
}
```

### 2. Copy-on-Write Semantics

```cpp
template<typename T>
class COWArray {
    shm_array<T> data;
    shm_array<uint32_t> version;
    shm_atomic<uint32_t> global_version;
    
public:
    T read(size_t idx) {
        return data[idx];
    }
    
    void write(size_t idx, const T& value) {
        uint32_t current_ver = global_version.load();
        if (version[idx] < current_ver) {
            // First write in this version
            save_undo(idx, data[idx]);
            version[idx] = current_ver;
        }
        data[idx] = value;
    }
    
    void new_version() {
        global_version.fetch_add(1);
    }
};
```

### 3. Parallel Reduction

```cpp
template<typename T, typename Op>
T parallel_reduce(shm_array<T>& arr, T init, Op op) {
    const size_t CACHE_LINE = 64;
    const size_t PADDING = CACHE_LINE / sizeof(T);
    
    // Per-thread partial sums with padding to avoid false sharing
    shm_array<T> partials(shm, "partials", num_threads * PADDING);
    
    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        size_t chunk_size = arr.size() / num_threads;
        size_t start = tid * chunk_size;
        size_t end = (tid == num_threads - 1) ? arr.size() : start + chunk_size;
        
        T local_sum = init;
        for (size_t i = start; i < end; ++i) {
            local_sum = op(local_sum, arr[i]);
        }
        
        partials[tid * PADDING] = local_sum;
    }
    
    // Final reduction
    T result = init;
    for (int i = 0; i < num_threads; ++i) {
        result = op(result, partials[i * PADDING]);
    }
    return result;
}

// Usage
float total_mass = parallel_reduce(masses, 0.0f, std::plus<float>());
```

## Comparison with Other Structures

| Aspect | shm_array | std::vector | shm_queue | shm_ring_buffer |
|--------|-----------|-------------|-----------|-----------------|
| Size | Fixed | Dynamic | Bounded | Bounded |
| Access | O(1) | O(1) | No random | O(1) |
| Cache | Optimal | Good | Good | Good |
| Growth | No | Yes | No | No |
| SIMD | Natural | Good | Poor | Good |
| Memory | Contiguous | Contiguous | Contiguous | Circular |

## Common Pitfalls

### 1. False Sharing

```cpp
// WRONG: Multiple threads updating adjacent elements
shm_array<Counter> counters(shm, "counters", num_threads);
#pragma omp parallel
{
    int tid = omp_get_thread_num();
    for (int i = 0; i < 1000000; ++i) {
        counters[tid].value++;  // False sharing!
    }
}

// CORRECT: Padding to cache line
struct PaddedCounter {
    std::atomic<uint64_t> value;
    char padding[56];  // Total 64 bytes
};
shm_array<PaddedCounter> counters(shm, "counters", num_threads);
```

### 2. Array of Structures vs Structure of Arrays

```cpp
// AoS: Poor cache usage for single field access
struct Particle { float x, y, z, vx, vy, vz, m; };
shm_array<Particle> particles(shm, "aos", n);

// Accessing only positions wastes cache
for (size_t i = 0; i < n; ++i) {
    process_position(particles[i].x, particles[i].y, particles[i].z);
    // Loads 28 bytes but uses only 12
}

// SoA: Better cache usage
struct Particles {
    shm_array<float> x, y, z, vx, vy, vz, m;
};

// Accessing positions is cache-efficient
for (size_t i = 0; i < n; ++i) {
    process_position(x[i], y[i], z[i]);
    // Each array access is cache-friendly
}
```

## Testing Strategies

```cpp
TEST_CASE("Array handles concurrent CAS") {
    shm_array<std::atomic<int>> arr(shm, "atomic", 100);
    
    // Initialize
    for (int i = 0; i < 100; ++i) {
        arr[i].store(0);
    }
    
    // Concurrent increments
    std::vector<std::thread> threads;
    for (int t = 0; t < 10; ++t) {
        threads.emplace_back([&]() {
            for (int iter = 0; iter < 1000; ++iter) {
                for (int i = 0; i < 100; ++i) {
                    arr[i].fetch_add(1);
                }
            }
        });
    }
    
    for (auto& t : threads) t.join();
    
    // Verify
    for (int i = 0; i < 100; ++i) {
        REQUIRE(arr[i].load() == 10000);
    }
}
```

## References

- [What Every Programmer Should Know About Memory](https://people.freebsd.org/~lstewart/articles/cpumemory.pdf) - Ulrich Drepper
- [Gallery of Processor Cache Effects](http://igoro.com/archive/gallery-of-processor-cache-effects/) - Igor Ostrovsky
- [False Sharing](https://mechanical-sympathy.blogspot.com/2011/07/false-sharing.html) - Martin Thompson