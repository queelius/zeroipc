# ZeroIPC C++ API Reference

## Table of Contents

1. [Core Components](#core-components)
   - [Memory](#memory)
   - [Table](#table)
2. [Traditional Data Structures](#traditional-data-structures)
   - [Array](#array)
   - [Queue](#queue)
   - [Stack](#stack)
   - [Map](#map)
   - [Set](#set)
   - [Pool](#pool)
   - [Ring](#ring)
3. [Codata Structures](#codata-structures)
   - [Future](#future)
   - [Lazy](#lazy)
   - [Stream](#stream)
   - [Channel](#channel)

---

## Core Components

### Memory

POSIX shared memory wrapper with automatic lifecycle management.

```cpp
namespace zeroipc {
    template<typename TableImpl = table64>
    class memory;
}
```

#### Constructor

```cpp
memory(std::string_view name, size_t size = 0, bool auto_create = true);
```

Creates or opens shared memory segment.

**Parameters:**
- `name`: Shared memory identifier (e.g., "/mydata")
- `size`: Size in bytes (0 to open existing)
- `auto_create`: Create if doesn't exist

**Example:**
```cpp
// Create new segment
memory mem("/sensors", 10*1024*1024);  // 10MB

// Open existing
memory mem_existing("/sensors");
```

#### Methods

| Method | Description | Return Type |
|--------|-------------|-------------|
| `base()` | Get base pointer | `void*` |
| `size()` | Get total size | `size_t` |
| `allocate(name, size)` | Allocate named region | `size_t` (offset) |
| `find(name, offset, size)` | Find named region | `bool` |
| `list()` | List all allocations | `vector<TableEntry>` |

### Table

Metadata registry for dynamic structure discovery.

```cpp
// Predefined table sizes
using table1 = table_impl<32, 1>;       // 1 entry
using table64 = table_impl<32, 64>;     // 64 entries (default)
using table256 = table_impl<32, 256>;   // 256 entries
using table4096 = table_impl<32, 4096>; // 4096 entries
```

#### Table Entry Structure

```cpp
struct table_entry {
    char name[32];      // Structure name
    uint32_t offset;    // Offset in memory
    uint32_t size;      // Size in bytes
};
```

---

## Traditional Data Structures

### Array

Fixed-size contiguous array with atomic operations.

```cpp
template<typename T>
class array;
```

#### Constructor

```cpp
// Create new
array(memory& mem, std::string_view name, size_t capacity);

// Open existing
array(memory& mem, std::string_view name);
```

#### Methods

| Method | Description | Return Type |
|--------|-------------|-------------|
| `operator[]` | Element access | `T&` |
| `at(index)` | Bounds-checked access | `T&` |
| `size()` | Number of elements | `size_t` |
| `capacity()` | Maximum elements | `size_t` |
| `data()` | Raw pointer | `T*` |
| `fill(value)` | Set all elements | `void` |
| `atomic_add(index, value)` | Atomic addition | `T` |
| `atomic_compare_exchange(index, expected, desired)` | CAS operation | `bool` |

**Example:**
```cpp
memory mem("/data", 10*1024*1024);
array<float> temps(mem, "temperatures", 1000);
temps[0] = 23.5f;
float old = temps.atomic_add(0, 1.5f);  // Returns 23.5f
```

### Queue

Lock-free multi-producer multi-consumer circular buffer.

```cpp
template<typename T>
class queue;
```

#### Constructor

```cpp
// Create new
queue(memory& mem, std::string_view name, size_t capacity);

// Open existing
queue(memory& mem, std::string_view name);
```

#### Methods

| Method | Description | Return Type |
|--------|-------------|-------------|
| `push(value)` | Enqueue element | `bool` |
| `pop()` | Dequeue element | `std::optional<T>` |
| `try_push(value)` | Non-blocking push | `bool` |
| `try_pop()` | Non-blocking pop | `std::optional<T>` |
| `size()` | Current elements | `size_t` |
| `capacity()` | Maximum elements | `size_t` |
| `empty()` | Check if empty | `bool` |
| `full()` | Check if full | `bool` |

**Example:**
```cpp
queue<Message> msgs(mem, "messages", 1000);
msgs.push(Message{.id = 1, .data = "Hello"});
if (auto msg = msgs.pop()) {
    process(*msg);
}
```

### Stack

Lock-free LIFO stack with ABA problem prevention.

```cpp
template<typename T>
class stack;
```

#### Methods

| Method | Description | Return Type |
|--------|-------------|-------------|
| `push(value)` | Push element | `bool` |
| `pop()` | Pop element | `std::optional<T>` |
| `top()` | Peek at top | `std::optional<T>` |
| `size()` | Current elements | `size_t` |
| `empty()` | Check if empty | `bool` |

### Map

Lock-free hash map with linear probing.

```cpp
template<typename K, typename V>
class map;
```

#### Constructor

```cpp
// Create new
map(memory& mem, std::string_view name, size_t capacity);

// Open existing
map(memory& mem, std::string_view name);
```

#### Methods

| Method | Description | Return Type |
|--------|-------------|-------------|
| `insert(key, value)` | Insert/update | `bool` |
| `get(key)` | Retrieve value | `std::optional<V>` |
| `remove(key)` | Delete entry | `bool` |
| `contains(key)` | Check existence | `bool` |
| `size()` | Number of entries | `size_t` |
| `clear()` | Remove all entries | `void` |
| `operator[]` | Access/insert | `V&` |

**Example:**
```cpp
map<uint32_t, double> cache(mem, "score_cache", 10000);
cache.insert(42, 0.95);
if (auto score = cache.get(42)) {
    std::cout << "Score: " << *score << std::endl;
}
```

### Set

Lock-free hash set for unique elements.

```cpp
template<typename T>
class set;
```

#### Methods

| Method | Description | Return Type |
|--------|-------------|-------------|
| `insert(value)` | Add element | `bool` |
| `remove(value)` | Remove element | `bool` |
| `contains(value)` | Check membership | `bool` |
| `size()` | Number of elements | `size_t` |
| `clear()` | Remove all | `void` |

### Pool

Object pool with free list management.

```cpp
template<typename T>
class pool;
```

#### Constructor

```cpp
pool(memory& mem, std::string_view name, size_t capacity);
```

#### Methods

| Method | Description | Return Type |
|--------|-------------|-------------|
| `allocate()` | Get object from pool | `T*` |
| `deallocate(ptr)` | Return to pool | `void` |
| `available()` | Free objects count | `size_t` |
| `capacity()` | Total objects | `size_t` |
| `reset()` | Return all to pool | `void` |

**Example:**
```cpp
struct Task { int id; char data[256]; };
pool<Task> task_pool(mem, "tasks", 100);

Task* t = task_pool.allocate();
t->id = 42;
// ... use task ...
task_pool.deallocate(t);
```

### Ring

High-performance ring buffer for streaming data.

```cpp
template<typename T>
class ring;
```

#### Methods

| Method | Description | Return Type |
|--------|-------------|-------------|
| `write(data, count)` | Write elements | `size_t` |
| `read(buffer, count)` | Read elements | `size_t` |
| `peek(buffer, count)` | Read without consuming | `size_t` |
| `skip(count)` | Skip elements | `size_t` |
| `available()` | Readable elements | `size_t` |
| `space()` | Writable space | `size_t` |

---

## Codata Structures

### Future

Asynchronous computation results in shared memory.

```cpp
template<typename T>
class future;
```

#### Constructor

```cpp
// Create new
future(memory& mem, std::string_view name);

// Open existing
future(memory& mem, std::string_view name, bool);
```

#### Methods

| Method | Description | Return Type |
|--------|-------------|-------------|
| `set_value(value)` | Set result | `void` |
| `set_error(msg)` | Set error state | `void` |
| `get()` | Get value (blocks) | `T` |
| `try_get()` | Non-blocking get | `std::optional<T>` |
| `get_for(duration)` | Get with timeout | `std::optional<T>` |
| `wait()` | Wait for ready | `void` |
| `wait_for(duration)` | Wait with timeout | `bool` |
| `is_ready()` | Check if ready | `bool` |
| `has_value()` | Check if has value | `bool` |
| `has_error()` | Check if error | `bool` |
| `get_error()` | Get error message | `std::string` |

**Example:**
```cpp
// Producer
future<Result> result(mem, "computation");
std::thread([&]() {
    Result r = expensive_computation();
    result.set_value(r);
}).detach();

// Consumer
future<Result> result(mem, "computation", true);
if (auto r = result.get_for(5s)) {
    process(*r);
}
```

### Lazy

Deferred computation with automatic memoization.

```cpp
template<typename T>
class lazy;
```

#### Constructor

```cpp
// Create new
lazy(memory& mem, std::string_view name);

// Open existing
lazy(memory& mem, std::string_view name, bool);
```

#### Methods

| Method | Description | Return Type |
|--------|-------------|-------------|
| `set_computation(func)` | Define computation | `void` |
| `get()` | Get value (computes if needed) | `T` |
| `is_computed()` | Check if cached | `bool` |
| `invalidate()` | Clear cache | `void` |
| `compute_async()` | Trigger async computation | `void` |

**Example:**
```cpp
lazy<Config> config(mem, "app_config");
config.set_computation([]() {
    return parse_config_file("/etc/app.conf");
});

// First access computes and caches
Config c = config.get();

// Subsequent accesses use cache
Config c2 = config.get();  // Instant
```

### Stream

Reactive data streams with functional operators.

```cpp
template<typename T>
class stream;
```

#### Constructor

```cpp
// Create new
stream(memory& mem, std::string_view name, size_t buffer_size = 1024);

// Open existing
stream(memory& mem, std::string_view name);
```

#### Methods

| Method | Description | Return Type |
|--------|-------------|-------------|
| `emit(value)` | Push value to stream | `bool` |
| `subscribe(callback)` | Add subscriber | `subscription` |
| `map(mem, name, func)` | Transform elements | `stream<U>` |
| `filter(mem, name, pred)` | Filter elements | `stream<T>` |
| `take(mem, name, count)` | Take first n | `stream<T>` |
| `skip(mem, name, count)` | Skip first n | `stream<T>` |
| `fold(mem, name, init, func)` | Reduce to value | `future<S>` |
| `window(mem, name, size)` | Group into windows | `stream<vector<T>>` |
| `merge(mem, name, other)` | Combine streams | `stream<T>` |
| `zip(mem, name, other)` | Pair elements | `stream<pair<T,U>>` |
| `close()` | Close stream | `void` |

**Example:**
```cpp
stream<double> temps(mem, "temperatures", 1000);

// Create processing pipeline
auto warnings = temps
    .map(mem, "celsius", [](double k) { return k - 273.15; })
    .filter(mem, "high", [](double c) { return c > 35.0; })
    .window(mem, "5min", 300);

// Subscribe to processed stream
auto sub = warnings.subscribe([](auto window) {
    double avg = std::accumulate(window.begin(), window.end(), 0.0) / window.size();
    if (avg > 37.0) send_heat_warning();
});
```

#### Stream Subscription

```cpp
class subscription {
    void unsubscribe();
    bool is_active() const;
};
```

### Channel

CSP-style communication channel.

```cpp
template<typename T>
class channel;
```

#### Constructor

```cpp
// Unbuffered (synchronous)
channel(memory& mem, std::string_view name);

// Buffered (asynchronous)
channel(memory& mem, std::string_view name, size_t buffer_size);

// Open existing
channel(memory& mem, std::string_view name, bool);
```

#### Methods

| Method | Description | Return Type |
|--------|-------------|-------------|
| `send(value)` | Send value | `bool` |
| `receive()` | Receive value | `std::optional<T>` |
| `try_send(value)` | Non-blocking send | `bool` |
| `try_receive()` | Non-blocking receive | `std::optional<T>` |
| `send_for(value, duration)` | Send with timeout | `bool` |
| `receive_for(duration)` | Receive with timeout | `std::optional<T>` |
| `close()` | Close channel | `void` |
| `is_closed()` | Check if closed | `bool` |
| `size()` | Messages in buffer | `size_t` |

**Example:**
```cpp
// Unbuffered - synchronous rendezvous
channel<Command> commands(mem, "cmds");

// Producer (blocks until consumer receives)
commands.send(Command{.type = START});

// Consumer
while (auto cmd = commands.receive()) {
    execute(*cmd);
}

// Buffered - asynchronous up to capacity
channel<Event> events(mem, "events", 1000);
events.send(Event{.type = CLICK});  // Doesn't block unless full
```

#### Select Operation

Wait on multiple channels:

```cpp
template<typename... Channels>
class select {
    static auto receive(Channels&... channels);
};

// Example
auto result = select::receive(chan1, chan2, chan3);
switch (result.index()) {
    case 0: process(std::get<0>(result)); break;
    case 1: process(std::get<1>(result)); break;
    case 2: process(std::get<2>(result)); break;
}
```

---

## Type Requirements

All types used with ZeroIPC structures must be **trivially copyable**:

```cpp
static_assert(std::is_trivially_copyable_v<T>);
```

This means:
- No virtual functions
- No user-defined copy/move constructors
- No user-defined destructors
- All members must be trivially copyable

**Valid Types:**
```cpp
struct Point { float x, y, z; };           // ✓ POD struct
struct Config { int id; char name[32]; };  // ✓ C-style string
using Data = std::array<double, 100>;      // ✓ Fixed array
```

**Invalid Types:**
```cpp
struct Bad1 { std::string name; };         // ✗ std::string
struct Bad2 { virtual void foo(); };       // ✗ Virtual function
struct Bad3 { std::vector<int> data; };    // ✗ Dynamic allocation
```

---

## Error Handling

All operations that can fail return:
- `bool` for success/failure
- `std::optional<T>` for values that might not exist
- Exceptions only for constructor failures

**Example:**
```cpp
queue<int> q(mem, "myqueue", 100);

// Check return values
if (!q.push(42)) {
    // Queue was full
}

if (auto value = q.pop()) {
    // Use *value
} else {
    // Queue was empty
}

// Exception handling for construction
try {
    array<float> arr(mem, "nonexistent");  // Throws if not found
} catch (const std::runtime_error& e) {
    std::cerr << "Array not found: " << e.what() << std::endl;
}
```

---

## Thread Safety

All structures are designed for concurrent access:

| Structure | Producer | Consumer | Thread-Safe |
|-----------|----------|----------|-------------|
| Array | Multiple | Multiple | Atomic ops only |
| Queue | Multiple | Multiple | Lock-free |
| Stack | Multiple | Multiple | Lock-free |
| Map | Multiple | Multiple | Lock-free |
| Set | Multiple | Multiple | Lock-free |
| Pool | Multiple | Multiple | Lock-free |
| Ring | Single | Single | Memory barriers |
| Future | Single | Multiple | Atomic state |
| Lazy | Single | Multiple | Once computation |
| Stream | Multiple | Multiple | Lock-free buffer |
| Channel | Multiple | Multiple | Lock-free |

---

## Memory Management

### Allocation Strategy

ZeroIPC uses bump allocation with no defragmentation:

```cpp
memory mem("/data", 100*1024*1024);  // 100MB

// Each allocation advances the offset
array<int> a1(mem, "array1", 1000);     // Offset: 0
queue<float> q1(mem, "queue1", 500);    // Offset: 4000
stack<double> s1(mem, "stack1", 200);   // Offset: 6000
```

### Best Practices

1. **Pre-allocate**: Size shared memory for all structures upfront
2. **Name carefully**: Use hierarchical names (e.g., "sensor/temp/raw")
3. **Check existence**: Use try/catch or check return values
4. **Clean shutdown**: Unlink shared memory when done
5. **Monitor usage**: Use `memory::available()` to track free space

---

## Performance Characteristics

| Operation | Complexity | Notes |
|-----------|------------|-------|
| Array access | O(1) | Direct memory access |
| Queue push/pop | O(1) | Lock-free CAS |
| Stack push/pop | O(1) | Lock-free CAS |
| Map insert/lookup | O(1) avg | Linear probing |
| Set insert/contains | O(1) avg | Hash-based |
| Pool allocate | O(1) | Free list |
| Ring read/write | O(n) | Bulk operations |
| Future get | O(1) | After ready |
| Lazy get | O(1) | After cached |
| Stream emit | O(1) | Ring buffer |
| Channel send/receive | O(1) | Queue-based |

---

## Examples Repository

For complete working examples, see:
- [docs/examples/](examples/) - Categorized examples
- [cpp/tests/](../cpp/tests/) - Unit tests showing usage
- [interop/](../interop/) - Cross-language examples