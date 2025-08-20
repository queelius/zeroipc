# Design Philosophy & Trade-offs

## Core Philosophy: Simplicity Through Constraints

> "Perfection is achieved not when there is nothing more to add, but when there is nothing left to take away." - Antoine de Saint-Exupéry

This library makes **deliberate constraints** that enable extraordinary simplicity and performance. We're not trying to be a general-purpose shared memory allocator. We're building the **fastest possible IPC for simulations**.

## The Original Vision

We set out to solve a specific problem:
- **Performance-critical simulations** with multiple processes
- **Fixed data structures** known at compile time
- **Zero-overhead reads** matching native array performance
- **Lock-free operations** where algorithmically possible
- **Simple discovery** via named structures

What we explicitly did NOT try to build:
- ❌ A general-purpose memory allocator
- ❌ A replacement for malloc/new
- ❌ A distributed computing framework
- ❌ A database or persistence layer

## Key Design Decisions

### 1. Stack Allocation: Simple by Design

**The Decision:**
We use stack/bump allocation - memory only grows forward, no individual deallocation.

```cpp
// What we do (dead simple)
size_t allocate(size_t size) {
    size_t offset = current_offset;
    current_offset += size;  // Just bump forward
    return offset;
}

// What we DON'T do (complex)
size_t allocate(size_t size) {
    scan_free_list();
    find_best_fit();
    handle_fragmentation();
    update_metadata();
    // ... 100s of lines of allocator code
}
```

**Why This Is The Right Choice:**

✅ **Matches Simulation Patterns**
- Simulations initialize structures once at startup
- Structures live for entire program lifetime
- Dynamic needs handled by `shm_object_pool` (which DOES have a free list)

✅ **Fastest Possible Allocation**
- O(1) with just addition
- No scanning, no fragmentation
- Predictable memory layout

✅ **Eliminates Entire Classes of Bugs**
- No use-after-free
- No double-free
- No fragmentation issues
- No allocator metadata corruption

**The Trade-off:**
- Can't deallocate individual structures
- Can "leak" if you repeatedly create/destroy

**Our Answer:**
- Don't repeatedly create/destroy!
- Initialize once, use forever
- This constraint makes the system simpler AND faster

### 2. Fixed-Size Tables: Predictability Over Flexibility

**The Decision:**
Compile-time table sizes, no dynamic growth.

```cpp
template<size_t MaxNameSize = 32, size_t MaxEntries = 64>
class shm_table_impl {
    entry entries[MAX_ENTRIES];  // Fixed size
};
```

**Why This Is The Right Choice:**

✅ **Zero Dynamic Allocation**
- No hidden malloc calls
- No surprising memory spikes
- Works in constrained environments

✅ **Predictable Performance**
- Lookup is always O(n) where n ≤ MAX_ENTRIES
- Memory usage known at compile time
- No reallocation pauses

✅ **Template Flexibility**
```cpp
// Choose what YOU need
using tiny = shm_table_impl<16, 8>;    // 520 bytes
using huge = shm_table_impl<256, 1024>; // 400KB
```

**The Trade-off:**
- Must know limits at compile time
- Can't grow beyond MAX_ENTRIES

**Our Answer:**
- Simulations have known structure counts
- If you need 1000 structures, compile with 1024
- Explicit is better than implicit

### 3. No STL Allocator Interface: Focus Over Features

**The Temptation:**
"Let's add STL allocators so users can use any container!"

**Why We Said No:**

✅ **Maintains Simplicity**
```cpp
// Our way (clear, obvious)
shm_array<float> data(shm, "sensor_data", 1000);

// STL allocator way (complex, subtle)
using Alloc = shm_allocator<float>;
std::vector<float, Alloc> data(Alloc(shm));
// Where does it allocate? How much? When?
```

✅ **Preserves Performance Guarantees**
- `shm_array[i]` is EXACTLY a pointer dereference
- No hidden indirection from std::vector
- No capacity vs size confusion

✅ **Avoids Impedance Mismatch**
- STL expects individual deallocation
- We don't support that (by design)
- Would lead to confusion and bugs

✅ **We Already Solved The Problem**
```cpp
// Need dynamic objects? Use the pool!
shm_object_pool<Particle> particles(shm, "particles", 10000);
auto handle = particles.acquire();

// Need a queue? We have that!
shm_queue<Event> events(shm, "events", 1000);

// Need an array? Done!
shm_array<float> data(shm, "data", 50000);
```

**The Trade-off:**
- Can't use arbitrary STL containers
- Must use our provided structures

**Our Answer:**
- Our structures cover 95% of simulation needs
- They're optimized for shared memory
- Constraints enable optimization

## The YAGNI Principle

**"You Aren't Gonna Need It"** - We actively resist:

### Features We Consciously Rejected

1. **Arbitrary Container Support**
   - Why: STL containers aren't designed for shared memory
   - Instead: Purpose-built structures that excel at IPC

2. **Defragmentation**
   - Why: Adds complexity, unpredictable pauses
   - Instead: Stack allocation with upfront sizing

3. **Serialization**
   - Why: We use trivially-copyable types
   - Instead: Direct memory access, zero overhead

4. **Network Transparency**
   - Why: Different problem domain entirely
   - Instead: Focus on single-node performance

5. **Persistence**
   - Why: Shared memory is for IPC, not storage
   - Instead: Let users handle persistence separately

## Real-World Validation

This design philosophy is validated by production systems:

### Game Engines
**Unity's Burst Compiler/Job System:**
- Fixed-size, preallocated buffers
- No dynamic allocation in hot paths
- Stack allocators for frame data

### High-Frequency Trading
**LMAX Disruptor:**
- Fixed-size ring buffers
- Preallocated everything
- Zero allocation during trading

### Embedded Systems
**NASA's JPL Coding Standard:**
- No dynamic allocation after init
- Fixed-size tables
- Predictability over flexibility

## When NOT to Use This Library

Be honest about limitations:

**Don't Use If You Need:**
- ❌ General-purpose shared memory allocation
- ❌ Arbitrary container types in shared memory
- ❌ Dynamic structure creation/destruction
- ❌ Garbage collection or automatic memory management
- ❌ Network-distributed shared memory

**Do Use If You Have:**
- ✅ Known data structures at compile time
- ✅ Multiple processes on single node
- ✅ Performance-critical requirements
- ✅ Need for zero-overhead reads
- ✅ Simulation or real-time systems

## The Benefit of Constraints

Our constraints aren't limitations - they're **features**:

| Constraint | Enables |
|-----------|---------|
| Stack allocation only | Zero fragmentation, O(1) allocation |
| Fixed-size tables | Compile-time memory bounds |
| No individual deallocation | No use-after-free bugs |
| Trivially-copyable only | Zero-copy, no serialization |
| Named structures | Simple discovery, debugging |

## Conclusion: Simplicity Is A Feature

We could add:
- Complex allocators with free lists
- STL allocator interfaces  
- Defragmentation algorithms
- Serialization layers
- Network transparency

But we **choose not to**. 

Every feature we DON'T add:
- Keeps the codebase smaller
- Makes performance more predictable
- Reduces bug surface area
- Lowers cognitive load
- Maintains our zero-overhead guarantee

**Our success metric isn't feature count - it's nanoseconds.**

The library does exactly what it promises:
- **Fast**: Proven identical to native arrays
- **Simple**: ~2000 lines of focused code
- **Reliable**: No allocator bugs to worry about
- **Predictable**: No hidden behaviors

And that's exactly what simulations need.