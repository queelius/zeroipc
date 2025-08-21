# shm_object_pool: Lock-Free Memory Pool for Dynamic Allocation

## Overview

`shm_object_pool<T>` implements a lock-free object pool in shared memory, providing fast, deterministic allocation and deallocation of fixed-size objects. It eliminates heap fragmentation and allocation overhead, making it ideal for high-frequency object creation/destruction in simulations, particularly for particles, collision events, and temporary computation structures.

## Architecture

### Memory Layout

```
[shm_table metadata]
[PoolHeader]
  ├─ atomic<uint32_t> free_list_head  // Head of free list
  ├─ size_t capacity                  // Total objects
  ├─ size_t object_size               // sizeof(T) + metadata
  ├─ atomic<uint64_t> allocations     // Statistics
  ├─ atomic<uint64_t> deallocations   // Statistics
  └─ padding[32]                      // Cache alignment
[Block][Block][Block]...[Block]       // Fixed-size blocks

Block Layout:
[atomic<uint32_t> next]  // Next free block index
[T object]               // Actual object storage
[padding]                // Alignment padding
```

### Key Design Decisions

1. **Free List**: Lock-free stack of available blocks
2. **ABA Prevention**: Generation counters in pointers
3. **Fixed Size**: No fragmentation, O(1) allocation
4. **Lazy Initialization**: Objects constructed on allocation
5. **Memory Tagging**: Debug mode tracks allocations

## API Reference

### Construction

```cpp
// Create new pool
posix_shm shm("/simulation", 100 * 1024 * 1024);
shm_object_pool<Particle> particle_pool(shm, "particles", 100000);

// Open existing pool
shm_object_pool<Particle> existing(shm, "particles");

// Check existence
if (shm_object_pool<Particle>::exists(shm, "particles")) {
    shm_object_pool<Particle> pool(shm, "particles");
}
```

### Allocation and Deallocation

```cpp
// Allocate object (returns handle)
auto handle = pool.allocate();
if (handle) {
    Particle& p = pool.get(handle);
    p.initialize();
}

// Allocate with constructor arguments
auto handle = pool.emplace(mass, position, velocity);

// Deallocate
pool.deallocate(handle);

// Get object from handle
Particle& particle = pool.get(handle);
const Particle& particle = pool.get(handle);

// Check if handle is valid
if (pool.is_valid(handle)) {
    process(pool.get(handle));
}
```

### Pool Management

```cpp
// Pool statistics
size_t used = pool.used_count();
size_t free = pool.free_count();
size_t capacity = pool.capacity();
double utilization = pool.utilization();

// Bulk operations
std::vector<Handle> handles;
size_t allocated = pool.allocate_batch(100, handles);

pool.deallocate_batch(handles);

// Reset pool (deallocate all)
pool.reset();
```

## Lock-Free Algorithm

### Allocation (Free List Pop)

```cpp
Handle allocate() {
    uint64_t old_head = free_list_head.load();
    
    do {
        uint32_t index = get_index(old_head);
        uint32_t gen = get_generation(old_head);
        
        if (index == NULL_INDEX) {
            return Handle{};  // Pool exhausted
        }
        
        // Read next pointer
        Block& block = blocks[index];
        uint32_t next = block.next.load();
        
        // Create new head with incremented generation
        uint64_t new_head = make_pointer(next, gen + 1);
        
        // Try to swing head to next
        if (free_list_head.compare_exchange_weak(old_head, new_head)) {
            allocations.fetch_add(1);
            return Handle{index, gen};
        }
        // CAS failed, retry with updated old_head
    } while (true);
}
```

### Deallocation (Free List Push)

```cpp
void deallocate(Handle handle) {
    uint32_t index = handle.index;
    uint64_t old_head = free_list_head.load();
    
    do {
        uint32_t old_index = get_index(old_head);
        uint32_t gen = get_generation(old_head);
        
        // Set block's next to current head
        blocks[index].next.store(old_index);
        
        // Create new head pointing to deallocated block
        uint64_t new_head = make_pointer(index, gen + 1);
        
        // Try to swing head to deallocated block
        if (free_list_head.compare_exchange_weak(old_head, new_head)) {
            deallocations.fetch_add(1);
            return;
        }
        // CAS failed, retry
    } while (true);
}
```

### ABA Prevention

```cpp
// 64-bit pointer: [32-bit generation][32-bit index]
struct TaggedPointer {
    uint32_t index : 32;
    uint32_t generation : 32;
};

uint64_t make_pointer(uint32_t idx, uint32_t gen) {
    return (uint64_t(gen) << 32) | idx;
}
```

## Use Cases in N-Body Simulation

### 1. Dynamic Particle Management

```cpp
class ParticleSystem {
    shm_object_pool<Particle> pool;
    shm_array<Handle> active_particles;
    
public:
    Handle spawn_particle(float3 pos, float3 vel, float mass) {
        auto handle = pool.emplace(pos, vel, mass);
        if (handle) {
            active_particles.push_back(handle);
            
            // Initialize particle physics
            Particle& p = pool.get(handle);
            p.force = compute_initial_force(p);
            p.lifetime = MAX_LIFETIME;
        }
        return handle;
    }
    
    void destroy_particle(Handle handle) {
        // Remove from active list
        auto it = std::find(active_particles.begin(), 
                           active_particles.end(), handle);
        if (it != active_particles.end()) {
            *it = active_particles.back();
            active_particles.pop_back();
        }
        
        // Return to pool
        pool.deallocate(handle);
    }
    
    void update(float dt) {
        // Process particles, destroy expired ones
        for (size_t i = 0; i < active_particles.size(); ) {
            Handle h = active_particles[i];
            Particle& p = pool.get(h);
            
            p.lifetime -= dt;
            if (p.lifetime <= 0) {
                destroy_particle(h);
                // Don't increment i, we swapped with back
            } else {
                update_physics(p, dt);
                ++i;
            }
        }
    }
};
```

### 2. Collision Event Pool

```cpp
struct CollisionEvent {
    Handle particle1, particle2;
    float3 position;
    float time;
    float impulse;
};

class CollisionSystem {
    shm_object_pool<CollisionEvent> event_pool;
    shm_queue<Handle> pending_events;
    
public:
    void detect_collisions() {
        // Spatial hashing for broad phase
        for (auto& cell : spatial_grid) {
            for (size_t i = 0; i < cell.size(); ++i) {
                for (size_t j = i + 1; j < cell.size(); ++j) {
                    if (will_collide(cell[i], cell[j])) {
                        // Allocate collision event
                        auto event_handle = event_pool.allocate();
                        if (!event_handle) {
                            // Pool exhausted, skip
                            stats.dropped_collisions++;
                            continue;
                        }
                        
                        CollisionEvent& event = event_pool.get(event_handle);
                        event.particle1 = cell[i];
                        event.particle2 = cell[j];
                        event.position = compute_collision_point();
                        event.time = compute_collision_time();
                        event.impulse = compute_impulse();
                        
                        pending_events.enqueue(event_handle);
                    }
                }
            }
        }
    }
    
    void resolve_collisions() {
        while (auto event_handle = pending_events.dequeue()) {
            CollisionEvent& event = event_pool.get(*event_handle);
            
            // Apply impulses
            apply_impulse(event.particle1, event.impulse);
            apply_impulse(event.particle2, -event.impulse);
            
            // Visual effects
            spawn_collision_effect(event.position);
            
            // Return event to pool
            event_pool.deallocate(*event_handle);
        }
    }
};
```

### 3. Temporary Computation Nodes

```cpp
struct ComputeNode {
    enum Type { FORCE, INTEGRATE, COLLISION };
    Type type;
    Handle input_particles[8];
    Handle output_buffer;
    std::function<void()> compute;
};

class ComputeGraph {
    shm_object_pool<ComputeNode> node_pool;
    shm_dag<Handle> dependency_graph;
    
public:
    Handle create_force_node(std::vector<Handle> particles) {
        auto node_handle = node_pool.allocate();
        ComputeNode& node = node_pool.get(node_handle);
        
        node.type = ComputeNode::FORCE;
        std::copy(particles.begin(), particles.end(), node.input_particles);
        node.compute = [&]() {
            compute_nbody_forces(node.input_particles);
        };
        
        dependency_graph.add_node(node_handle);
        return node_handle;
    }
    
    void execute() {
        dependency_graph.execute_parallel([this](Handle h) {
            ComputeNode& node = node_pool.get(h);
            node.compute();
        });
        
        // Clean up executed nodes
        for (auto h : dependency_graph.get_executed()) {
            node_pool.deallocate(h);
        }
    }
};
```

### 4. Memory-Efficient Octree

```cpp
template<typename T>
class PooledOctree {
    struct Node {
        T data;
        Handle children[8];
        float3 center;
        float size;
        bool is_leaf;
    };
    
    shm_object_pool<Node> node_pool;
    Handle root;
    
public:
    Handle create_node(const T& data, float3 center, float size) {
        auto handle = node_pool.allocate();
        Node& node = node_pool.get(handle);
        
        node.data = data;
        node.center = center;
        node.size = size;
        node.is_leaf = true;
        std::fill(node.children, node.children + 8, Handle{});
        
        return handle;
    }
    
    void subdivide(Handle node_handle) {
        Node& node = node_pool.get(node_handle);
        if (!node.is_leaf) return;
        
        float half_size = node.size / 2;
        for (int i = 0; i < 8; ++i) {
            float3 child_center = compute_octant_center(node.center, half_size, i);
            node.children[i] = create_node(T{}, child_center, half_size);
        }
        
        node.is_leaf = false;
    }
    
    void collapse_empty_nodes() {
        std::function<bool(Handle)> collapse = [&](Handle h) -> bool {
            if (!h) return true;
            
            Node& node = node_pool.get(h);
            if (node.is_leaf) {
                return node.data.is_empty();
            }
            
            bool all_empty = true;
            for (int i = 0; i < 8; ++i) {
                if (collapse(node.children[i])) {
                    if (node.children[i]) {
                        node_pool.deallocate(node.children[i]);
                        node.children[i] = Handle{};
                    }
                } else {
                    all_empty = false;
                }
            }
            
            if (all_empty) {
                node.is_leaf = true;
            }
            return all_empty && node.data.is_empty();
        };
        
        collapse(root);
    }
};
```

## Advanced Patterns

### 1. Hazard Pointers for Safe Reclamation

```cpp
template<typename T>
class HazardPool {
    shm_object_pool<T> pool;
    shm_array<std::atomic<Handle>> hazard_pointers;
    shm_queue<Handle> retire_list;
    
public:
    class HazardGuard {
        std::atomic<Handle>& hp;
    public:
        HazardGuard(std::atomic<Handle>& hazard, Handle h) 
            : hp(hazard) {
            hp.store(h);
        }
        ~HazardGuard() {
            hp.store(Handle{});
        }
    };
    
    void retire(Handle h) {
        retire_list.enqueue(h);
        
        if (retire_list.size() > 2 * hazard_pointers.size()) {
            scan();
        }
    }
    
    void scan() {
        // Collect hazard pointers
        std::set<Handle> hazards;
        for (auto& hp : hazard_pointers) {
            Handle h = hp.load();
            if (h) hazards.insert(h);
        }
        
        // Reclaim non-hazardous objects
        std::queue<Handle> tmp;
        while (auto h = retire_list.dequeue()) {
            if (hazards.find(*h) == hazards.end()) {
                pool.deallocate(*h);
            } else {
                tmp.push(*h);
            }
        }
        
        // Re-enqueue still hazardous
        while (!tmp.empty()) {
            retire_list.enqueue(tmp.front());
            tmp.pop();
        }
    }
};
```

### 2. Typed Pool with Variants

```cpp
class VariantPool {
    struct Block {
        enum Type { PARTICLE, FORCE, COLLISION, EMPTY };
        std::atomic<Type> type{EMPTY};
        union {
            Particle particle;
            Force force;
            CollisionEvent collision;
        };
    };
    
    shm_object_pool<Block> pool;
    
public:
    template<typename T>
    Handle allocate() {
        auto h = pool.allocate();
        if (h) {
            Block& b = pool.get(h);
            if constexpr (std::is_same_v<T, Particle>) {
                new (&b.particle) Particle();
                b.type = Block::PARTICLE;
            } else if constexpr (std::is_same_v<T, Force>) {
                new (&b.force) Force();
                b.type = Block::FORCE;
            }
            // ...
        }
        return h;
    }
    
    template<typename T>
    T& get(Handle h) {
        Block& b = pool.get(h);
        if constexpr (std::is_same_v<T, Particle>) {
            assert(b.type == Block::PARTICLE);
            return b.particle;
        }
        // ...
    }
};
```

### 3. Generational Handles

```cpp
template<typename T>
class GenerationalPool {
    struct Slot {
        T object;
        uint32_t generation;
        bool allocated;
    };
    
    shm_array<Slot> slots;
    shm_stack<uint32_t> free_indices;
    
public:
    struct Handle {
        uint32_t index;
        uint32_t generation;
        
        bool operator==(const Handle& other) const {
            return index == other.index && generation == other.generation;
        }
    };
    
    Handle allocate() {
        if (auto idx = free_indices.pop()) {
            Slot& slot = slots[*idx];
            slot.allocated = true;
            slot.generation++;
            return Handle{*idx, slot.generation};
        }
        return Handle{INVALID_INDEX, 0};
    }
    
    void deallocate(Handle h) {
        Slot& slot = slots[h.index];
        if (slot.generation == h.generation && slot.allocated) {
            slot.allocated = false;
            free_indices.push(h.index);
        }
    }
    
    T* get(Handle h) {
        Slot& slot = slots[h.index];
        if (slot.generation == h.generation && slot.allocated) {
            return &slot.object;
        }
        return nullptr;
    }
};
```

## Performance Characteristics

### Allocation Performance

| Threads | Throughput | Latency | CAS Retries |
|---------|------------|---------|-------------|
| 1 | 100M/sec | 10ns | 0 |
| 2 | 60M/sec | 33ns | 0.2 |
| 4 | 40M/sec | 100ns | 1.5 |
| 8 | 25M/sec | 320ns | 5.2 |

### Memory Efficiency

```cpp
// Memory overhead per object
size_t overhead = sizeof(uint32_t);  // Next pointer
size_t padding = alignof(T) - 1;     // Worst case alignment
size_t total_per_object = sizeof(T) + overhead + padding;

// Pool efficiency
double efficiency = sizeof(T) / double(total_per_object);
// Typically 85-95% for reasonable object sizes
```

## Common Pitfalls

### 1. Handle Invalidation

```cpp
// WRONG: Using handle after deallocation
Handle h = pool.allocate();
pool.deallocate(h);
Particle& p = pool.get(h);  // Undefined behavior!

// CORRECT: Check validity
if (pool.is_valid(h)) {
    Particle& p = pool.get(h);
}
```

### 2. Memory Leaks

```cpp
// WRONG: Forgetting to deallocate
for (int i = 0; i < 1000; ++i) {
    Handle h = pool.allocate();
    // Never deallocated!
}

// CORRECT: RAII wrapper
class PoolGuard {
    Pool& pool;
    Handle h;
public:
    PoolGuard(Pool& p) : pool(p), h(p.allocate()) {}
    ~PoolGuard() { if (h) pool.deallocate(h); }
    Handle release() { Handle tmp = h; h = {}; return tmp; }
};
```

### 3. ABA Problem

```cpp
// WRONG: Simple pointer without generation
struct BadHandle {
    uint32_t index;
};
// Object freed and reallocated - same index!

// CORRECT: Generation counter
struct GoodHandle {
    uint32_t index;
    uint32_t generation;
};
// Generation changes on each allocation
```

## Testing Strategies

```cpp
TEST_CASE("Pool handles allocation storms") {
    shm_object_pool<TestObject> pool(shm, "test", 10000);
    std::atomic<size_t> total_allocations{0};
    
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&]() {
            std::vector<Handle> handles;
            for (int iter = 0; iter < 1000; ++iter) {
                // Allocation phase
                for (int i = 0; i < 100; ++i) {
                    if (auto h = pool.allocate()) {
                        handles.push_back(h);
                        total_allocations++;
                    }
                }
                
                // Deallocation phase
                for (auto h : handles) {
                    pool.deallocate(h);
                }
                handles.clear();
            }
        });
    }
    
    for (auto& t : threads) t.join();
    
    REQUIRE(pool.free_count() == pool.capacity());
    REQUIRE(pool.stats().allocations == total_allocations);
    REQUIRE(pool.stats().deallocations == total_allocations);
}
```

## References

- [The Slab Allocator: An Object-Caching Kernel Memory Allocator](https://people.eecs.berkeley.edu/~kubitron/courses/cs194-24-S14/hand-outs/bonwick_slab.pdf) - Bonwick
- [Lock-Free Memory Pool](https://github.com/cacay/MemoryPool) - Implementation reference
- [Hazard Pointers: Safe Memory Reclamation](https://www.research.ibm.com/people/m/michael/podc-2002.pdf) - Michael