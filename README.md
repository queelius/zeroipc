# ZeroIPC - Active Computational Substrate for Shared Memory

## Overview

ZeroIPC transforms shared memory from passive storage into an active computational substrate, enabling both imperative and functional programming paradigms across process boundaries. It provides zero-copy data sharing with sophisticated concurrency primitives, reactive streams, and codata structures - bringing modern programming abstractions to inter-process communication.

### Key Features

- 🚀 **Zero-Copy Performance** - Direct memory access without serialization
- 🌐 **Language Independence** - C++ and Python implementations, not bindings
- 🔒 **Lock-Free Concurrency** - Atomic operations and CAS-based algorithms
- 📦 **Minimal Metadata** - Only store name/offset/size for true flexibility
- 🦆 **Duck Typing** - Runtime type specification (Python) or compile-time templates (C++)
- 🎯 **Simple Discovery** - Named structures for easy cross-process lookup
- ⚡ **Reactive Programming** - Functional reactive streams with operators
- 🔮 **Codata Support** - Futures, lazy evaluation, and infinite streams
- 🚪 **CSP Concurrency** - Channels for synchronous message passing
- 🛠️ **CLI Tools** - Comprehensive inspection and debugging utilities

## Quick Start

### Basic Data Sharing

**C++ Producer:**
```cpp
#include <zeroipc/memory.h>
#include <zeroipc/array.h>

// Create shared memory
zeroipc::Memory mem("/sensor_data", 10*1024*1024);  // 10MB

// Create typed array
zeroipc::Array<float> temps(mem, "temperature", 1000);
temps[0] = 23.5f;
```

**Python Consumer:**
```python
from zeroipc import Memory, Array
import numpy as np

# Open same shared memory
mem = Memory("/sensor_data")

# Read with duck typing - user specifies type
temps = Array(mem, "temperature", dtype=np.float32)
print(temps[0])  # 23.5
```

### Reactive Streams Example

**Process A - Sensor Data Producer:**
```cpp
#include <zeroipc/memory.h>
#include <zeroipc/stream.h>

zeroipc::Memory mem("/sensors", 10*1024*1024);
zeroipc::Stream<double> temperature(mem, "temp_stream", 1000);

while (running) {
    double temp = read_sensor();
    temperature.emit(temp);
    std::this_thread::sleep_for(100ms);
}
```

**Process B - Stream Processing:**
```cpp
zeroipc::Memory mem("/sensors");
zeroipc::Stream<double> temperature(mem, "temp_stream");

// Create derived streams with functional transformations
auto fahrenheit = temperature.map(mem, "temp_f", 
    [](double c) { return c * 9/5 + 32; });

auto warnings = fahrenheit.filter(mem, "warnings",
    [](double f) { return f > 100.0; });

// Subscribe to processed stream
warnings.subscribe([](double high_temp) {
    send_alert("High temperature: " + std::to_string(high_temp));
});
```

### Futures for Async Results

**Process A - Computation:**
```cpp
#include <zeroipc/future.h>

zeroipc::Memory mem("/compute", 10*1024*1024);
zeroipc::Future<double> result(mem, "expensive_calc");

// Perform expensive computation
double value = run_simulation();
result.set_value(value);
```

**Process B - Waiting for Result:**
```cpp
zeroipc::Memory mem("/compute");
zeroipc::Future<double> result(mem, "expensive_calc", true);

// Wait with timeout
if (auto value = result.get_for(std::chrono::seconds(5))) {
    process_result(*value);
} else {
    handle_timeout();
}
```

### CSP-Style Channels

**Process A - Producer:**
```cpp
#include <zeroipc/channel.h>

zeroipc::Memory mem("/messages", 10*1024*1024);
zeroipc::Channel<Message> ch(mem, "commands", 100);  // buffered

Message msg{.type = CMD_START, .data = 42};
ch.send(msg);  // Blocks if buffer full
```

**Process B - Consumer:**
```cpp
zeroipc::Memory mem("/messages");
zeroipc::Channel<Message> ch(mem, "commands");

while (auto msg = ch.receive()) {
    process_command(*msg);
}
```

## Architecture

### Binary Format Specification

All implementations follow the same binary format defined in [SPECIFICATION.md](SPECIFICATION.md):

```
[Table Header][Table Entries][Data Structure 1][Data Structure 2]...
```

- **Table Header**: Magic number, version, entry count, next offset
- **Table Entry**: Name (32 bytes), offset (4 bytes), size (4 bytes)
- **Data Structures**: Raw binary data, layout determined by structure type

### Minimal Metadata Philosophy

Unlike traditional IPC systems, ZeroIPC stores NO type information:
- **Name**: For discovery
- **Offset**: Where data starts
- **Size**: How much memory is used

This enables true language independence:
- **Both languages can create**: Python and C++ can both allocate new structures
- **Both languages can read**: Either can discover and access existing structures
- **Type safety per language**: C++ uses templates, Python uses NumPy dtypes

## Data Structures

### Traditional Data Structures
- ✅ **Array** - Fixed-size contiguous storage with atomic operations
- ✅ **Queue** - Lock-free MPMC circular buffer using CAS
- ✅ **Stack** - Lock-free LIFO with ABA-safe operations
- ✅ **Map** - Lock-free hash map with linear probing
- ✅ **Set** - Lock-free hash set for unique elements
- ✅ **Pool** - Object pool with free list management
- ✅ **Ring** - High-performance ring buffer for streaming
- ✅ **Table** - Metadata registry for dynamic discovery

### Synchronization Primitives
- ✅ **Semaphore** - Cross-process counting/binary semaphore with wait/signal
- ✅ **Barrier** - Multi-process synchronization barrier with generation counter
- ✅ **Latch** - One-shot countdown synchronization primitive

### Codata & Computational Structures
- ✅ **Future** - Asynchronous computation results across processes
- ✅ **Lazy** - Deferred computations with automatic memoization
- ✅ **Stream** - Reactive data flows with FRP operators (map, filter, fold)
- ✅ **Channel** - CSP-style synchronous/buffered message passing

### Why Codata?
Traditional data structures store values in space. Codata structures represent computations over time. This enables:
- **Cross-process async/await** - Future results shared between processes
- **Lazy evaluation** - Expensive computations cached and shared
- **Reactive pipelines** - Event-driven processing with backpressure
- **CSP concurrency** - Go-style channels for structured communication

See [Codata Guide](docs/codata_guide.md) for detailed explanation.

## Language Implementations

### [C Implementation](c/)
- Pure C99 for maximum portability
- Zero dependencies beyond POSIX
- Static library (libzeroipc.a)
- Minimal overhead

### [C++ Implementation](cpp/)
- Template-based for zero overhead
- Header-only library
- Modern C++23 features
- RAII resource management

### [Python Implementation](python/)
- Pure Python, no compilation required
- NumPy integration for performance
- Duck typing for flexibility
- mmap for direct memory access

## Building and Testing

### C
```bash
cd c
make            # Build library
make test       # Run tests
```

### C++
```bash
cd cpp
cmake -B build .
cmake --build build

# Run tests - optimized test suite (200x faster than previous versions)
cd build && ctest --output-on-failure                    # Default: fast + medium (~2 min)
cmake --build build --target test_fast                   # Fast tests only (~30 sec)
cmake --build build --target test_ci                     # CI mode: all except stress (~10 min)
cmake --build build --target test_all                    # Full suite including stress (~30 min)

# Run specific test categories
ctest -L fast --output-on-failure                        # Fast tests (<100ms each)
ctest -L medium --output-on-failure                      # Medium tests (<5s each)
ctest -L lockfree --output-on-failure                    # All lock-free structure tests
ctest -L sync --output-on-failure                        # All synchronization primitive tests
```

### Python
```bash
cd python
pip install -e .
python -m pytest tests/
```

### Cross-Language Tests
```bash
cd interop
./test_interop.sh          # C++ writes, Python reads
./test_reverse_interop.sh  # Python writes, C++ reads
```

## Design Principles

1. **Language Equality** - No language is "primary", all are first-class
2. **Minimal Overhead** - Table stores only what's absolutely necessary
3. **User Responsibility** - Users ensure type consistency across languages
4. **Zero Dependencies** - Each implementation stands alone
5. **Binary Compatibility** - All languages read/write the same format

## Performance

- **Array Access**: Identical to native arrays (zero overhead)
- **Queue Operations**: Lock-free with atomic CAS
- **Memory Allocation**: O(1) bump allocation
- **Discovery**: O(n) where n ≤ max_entries

## Use Cases

ZeroIPC excels at:
- ✅ High-frequency sensor data sharing
- ✅ Multi-process simulations
- ✅ Real-time analytics pipelines
- ✅ Cross-language scientific computing
- ✅ Zero-copy producer-consumer patterns

Not designed for:
- ❌ General-purpose memory allocation
- ❌ Network-distributed systems
- ❌ Persistent storage
- ❌ Garbage collection

## CLI Tools

### zeroipc
Comprehensive tool for inspecting and debugging shared memory with support for all data structures:

```bash
# Build the tool
cd cpp && cmake -B build . && cmake --build build
./build/tools/zeroipc

# List all ZeroIPC shared memory segments
./zeroipc list

# Show detailed information about a segment and all structures
./zeroipc show /sensor_data

# Inspect specific data structures (supports all 16 structure types)
./zeroipc array /sensor_data temperatures       # Array inspection
./zeroipc queue /sensor_data task_queue         # Queue state and contents
./zeroipc stack /sensor_data undo_stack         # Stack inspection
./zeroipc ring /sensor_data event_buffer        # Ring buffer state
./zeroipc map /sensor_data cache                # Hash map contents
./zeroipc set /sensor_data unique_ids           # Set contents
./zeroipc pool /sensor_data object_pool         # Pool allocation state
./zeroipc channel /sensor_data messages         # Channel state
./zeroipc stream /sensor_data events            # Stream contents
./zeroipc semaphore /sensor_data mutex          # Semaphore state
./zeroipc barrier /sensor_data sync_point       # Barrier state
./zeroipc latch /sensor_data countdown          # Latch state
./zeroipc future /sensor_data result            # Future state
./zeroipc lazy /sensor_data computation         # Lazy evaluation state

# Monitor a stream in real-time
./zeroipc monitor /sensors temperature_stream

# Dump raw memory contents
./zeroipc dump /compute --offset 0 --size 1024

# Interactive REPL mode for exploration
./zeroipc repl /sensor_data
```

See [docs/cli_tools.md](docs/cli_tools.md) for complete CLI documentation.

## Test Suite Performance

ZeroIPC features an optimized test suite with intelligent categorization for fast development workflows:

- **200x Performance Improvement**: Reduced from 20+ minutes to under 2 minutes for default suite
- **Smart Categorization**: Tests labeled as FAST, MEDIUM, SLOW, or STRESS
- **Parameterized Timing**: Configurable timing constants via `test_config.h`
- **Selective Execution**: Run only the tests you need with CTest labels
- **CI-Optimized**: Default suite completes in seconds for rapid feedback

See [docs/TESTING_STRATEGY.md](docs/TESTING_STRATEGY.md) for comprehensive testing documentation.

## Documentation

- [Testing Strategy](docs/TESTING_STRATEGY.md) - Test suite architecture and best practices
- [Codata Guide](docs/codata_guide.md) - Understanding codata and computational structures
- [API Reference](docs/api_reference.md) - Complete API documentation
- [Architecture](docs/architecture.md) - System design and memory layout
- [Design Patterns](docs/patterns.md) - Cross-process communication patterns
- [CLI Tools](docs/cli_tools.md) - Command-line utilities documentation
- [Examples](docs/examples/) - Complete working examples
- [Design Philosophy](docs/design_philosophy.md) - Core principles and trade-offs
- [Binary Specification](SPECIFICATION.md) - Wire format all implementations follow
- [C++ Documentation](cpp/README.md) - C++ specific details
- [Python Documentation](python/README.md) - Python specific details

## Contributing

Contributions welcome! When adding new language implementations:
1. Follow the binary specification exactly
2. Create a new directory for your language
3. Implement Memory, Table, and Array as minimum
4. Add cross-language tests in `interop/`

## Advanced Features

### Functional Programming in Shared Memory
ZeroIPC brings functional programming paradigms to IPC:
- **Lazy Evaluation**: Defer expensive computations until needed
- **Memoization**: Automatic caching of computation results
- **Stream Combinators**: map, filter, fold, take, skip, window
- **Monadic Composition**: Chain asynchronous operations with Futures

### Cross-Process Patterns
- **Producer-Consumer**: Lock-free queues with backpressure
- **Pub-Sub**: Multiple consumers on reactive streams
- **Request-Response**: Futures for RPC-like patterns
- **Pipeline**: Stream transformations across processes
- **Fork-Join**: Parallel computation with result aggregation

## Future Explorations

The boundary between data and code continues to blur:
- **Persistent Data Structures**: Immutable structures with structural sharing
- **Software Transactional Memory**: ACID transactions in shared memory
- **Dataflow Programming**: Computational graphs in shared memory
- **Actors**: Message-passing actors with mailboxes
- **Continuations**: Suspended computations for coroutines

## License

MIT