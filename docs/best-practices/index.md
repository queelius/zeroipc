# Best Practices

Learn how to use ZeroIPC effectively and avoid common pitfalls.

## Quick Guidelines

### Do's

- Use fixed-width integer types (`int32_t` not `int`)
- Document shared data structures and types
- Choose appropriate table sizes for your workload
- Clean up test shared memory segments
- Use meaningful names for segments and structures
- Handle errors appropriately (check return values)
- Test cross-language compatibility early

### Don'ts

- Don't use platform-dependent types (`long`, `size_t`) for shared data
- Don't assume structure exists without checking
- Don't fill tables to 100% capacity
- Don't create circular dependencies
- Don't ignore type size mismatches
- Don't leak shared memory segments
- Don't rely on undefined behavior

## Best Practices by Topic

### [Performance Tips](performance.md)

Optimize for maximum throughput and minimum latency:

- Choose appropriate data structures
- Minimize contention
- Use proper memory ordering
- Batch operations when possible
- Pre-allocate structures
- Monitor utilization

### [Common Pitfalls](pitfalls.md)

Avoid these frequent mistakes:

- Type size mismatches
- Table overflow
- Memory leaks
- Race conditions
- Deadlocks (with user locks)
- Name collisions
- Incorrect cleanup

### [Type Safety](type-safety.md)

Maintain consistency across languages:

- Use shared type definitions
- Document type mappings
- Create type verification utilities
- Test cross-language compatibility
- Use fixed-width types
- Align structure members

### [Error Handling](error-handling.md)

Handle errors gracefully:

- Check return values
- Handle `std::optional` / `None` returns
- Validate inputs
- Provide clear error messages
- Fail fast on corruption
- Log errors appropriately

### [Testing Applications](testing.md)

Test shared memory applications:

- Unit test each component
- Integration test cross-language
- Stress test under high concurrency
- Test error paths
- Verify cleanup
- Use unique names in tests

## Code Style Guide

### C++ Style

```cpp
// Good: Clear, safe, idiomatic
#include <zeroipc/memory.h>
#include <zeroipc/array.h>

void process_data() {
    // Use RAII for automatic cleanup
    zeroipc::Memory mem("/sensors", 1024*1024);
    
    // Use fixed-width types
    zeroipc::Array<int32_t> data(mem, "readings", 1000);
    
    // Check optional returns
    if (auto value = data.at(0)) {
        std::cout << "First value: " << *value << "\n";
    }
    
    // Clear names
    constexpr size_t SENSOR_COUNT = 100;
}

// Bad: Unclear, unsafe, non-idiomatic
void bad_example() {
    auto m = new zeroipc::Memory("/x", 999999);  // Manual alloc
    zeroipc::Array<long> d(m, "d", 9999);  // Platform-dependent type
    cout << d[0];  // No error checking
    // Memory leak - never deleted
}
```

### Python Style

```python
# Good: Clear, type-safe, Pythonic
from zeroipc import Memory, Array
import numpy as np
from typing import Optional

def process_data() -> None:
    """Process sensor data from shared memory."""
    # Use context managers (when available)
    mem = Memory("/sensors")
    
    # Explicit dtype
    data = Array(mem, "readings", dtype=np.int32)
    
    # Check None
    value = data[0]
    if value is not None:
        print(f"First value: {value}")
    
    # Clear constants
    SENSOR_COUNT = 100

# Bad: Unclear, fragile
def bad_example():
    m = Memory("/x")
    d = Array(m, "d")  # Missing dtype!
    print(d[0])  # May crash
```

## Architecture Patterns

### Pattern 1: Publisher-Subscriber

One publisher, multiple subscribers:

```cpp
// Publisher
zeroipc::Memory mem("/events", 10*1024*1024);
zeroipc::Stream<Event> events(mem, "stream", 1000);

while (running) {
    Event e = generate_event();
    events.emit(e);
}

// Subscribers (many processes)
zeroipc::Memory mem("/events");
zeroipc::Stream<Event> events(mem, "stream");

events.subscribe([](Event& e) {
    process_event(e);
});
```

### Pattern 2: Request-Response

Use futures for RPC-like patterns:

```cpp
// Server
zeroipc::Memory mem("/rpc", 10*1024*1024);
zeroipc::Future<Response> result(mem, "response");

Request req = get_request();
Response res = process(req);
result.set_value(res);

// Client
zeroipc::Memory mem("/rpc");
zeroipc::Future<Response> result(mem, "response", true);  // Read-only

if (auto res = result.get_for(std::chrono::seconds(5))) {
    use_response(*res);
} else {
    handle_timeout();
}
```

### Pattern 3: Pipeline

Chain processing stages:

```cpp
// Stage 1: Raw data
zeroipc::Stream<RawData> raw(mem, "raw", 1000);

// Stage 2: Validated data
auto validated = raw.filter(mem, "validated",
    [](RawData& d) { return d.is_valid(); });

// Stage 3: Transformed data
auto transformed = validated.map(mem, "transformed",
    [](RawData& d) { return transform(d); });

// Stage 4: Final processing
transformed.subscribe([](TransformedData& d) {
    final_process(d);
});
```

## Documentation Templates

### Shared Type Definition

Create shared headers:

**shared_types.hpp (C++):**
```cpp
#pragma once
#include <cstdint>

namespace shared {

struct SensorReading {
    float temperature;
    float humidity;
    uint64_t timestamp;
};

constexpr size_t MAX_SENSORS = 100;
constexpr char SEGMENT_NAME[] = "/sensors";

}  // namespace shared
```

**shared_types.py (Python):**
```python
"""Shared type definitions for sensor application."""
import numpy as np

# Match C++ struct SensorReading
SENSOR_READING_DTYPE = np.dtype([
    ('temperature', np.float32),
    ('humidity', np.float32),
    ('timestamp', np.uint64),
])

MAX_SENSORS = 100
SEGMENT_NAME = "/sensors"
```

## Checklist

Use this checklist for new projects:

- [ ] Defined shared types in both languages
- [ ] Chosen appropriate table size
- [ ] Documented segment names
- [ ] Added error handling
- [ ] Created cleanup procedures
- [ ] Written unit tests
- [ ] Written integration tests
- [ ] Tested cross-language compatibility
- [ ] Added monitoring/debugging support
- [ ] Documented deployment requirements

## Next Steps

Dive deeper into specific topics:

- **[Performance Tips](performance.md)**
- **[Common Pitfalls](pitfalls.md)**
- **[Type Safety](type-safety.md)**
- **[Error Handling](error-handling.md)**
- **[Testing Applications](testing.md)**
