# ZeroIPC - High-Performance Shared Memory IPC

Zero-copy data sharing between processes in C++, Python, Go, and C. No serialization, no bindings — parallel native implementations of the same binary format.

## What's New (v2.2.0)

- Fixed critical concurrency bugs in lock-free Queue and Stack (head/tail confusion, fence placement)
- Go implementation with generics, CLI tool, and cross-language interop
- 9 synchronization primitives: Mutex, RWLock, Monitor, Event, Semaphore, Barrier, Latch, Once, Signal

## Quick Start

**C++ writes, Python reads — zero-copy:**

```cpp
#include <zeroipc/memory.h>
#include <zeroipc/array.h>

zeroipc::Memory mem("/sensor_data", 10*1024*1024);  // 10MB
zeroipc::Array<float> temps(mem, "temperature", 1000);
temps[0] = 23.5f;
```

```python
from zeroipc import Memory, Array
import numpy as np

mem = Memory("/sensor_data")
temps = Array(mem, "temperature", dtype=np.float32)
print(temps[0])  # 23.5
```

```go
mem, _ := zeroipc.OpenMemory("/sensor_data", 10*1024*1024)
defer mem.Close()
temps, _ := zeroipc.OpenArray[float32](mem, "temperature")
fmt.Println(temps.Get(0))  // 23.5
```

See [docs/examples/](docs/examples/) for streams, futures, channels, and more.

## Data Structures

**Core** — Array, Queue (lock-free MPMC), Stack (lock-free), Ring, Map (lock-free), Set, Pool, Table

**Sync** — Semaphore, Mutex, RWLock, Monitor, Barrier, Latch, Once, Event, Signal

**Codata** — Future, Lazy, Stream (with map/filter/fold), Channel (CSP-style)

See [docs/codata_guide.md](docs/codata_guide.md) for futures, streams, and lazy evaluation.
See [docs/sync_primitives_guide.md](docs/sync_primitives_guide.md) for synchronization primitives.

## Design Principles

All implementations follow the same [binary specification](SPECIFICATION.md). The metadata table stores only **name, offset, and size** — no type information. This enables true language independence: C++ uses templates, Python uses NumPy dtypes, Go uses generics. Users ensure type consistency across languages.

```
[Table Header][Table Entries][Structure 1][Structure 2]...[Structure N]
```

Key design choices:
- **Language equality** — no language is "primary"; all are first-class implementations
- **Zero dependencies** — each implementation stands alone
- **Lock-free concurrency** — atomic CAS operations, no kernel calls in hot paths
- **User-controlled layout** — no GC, no defragmentation, no hidden allocations

## Language Implementations

| Language | Dir | Highlights |
|----------|-----|------------|
| [C++](cpp/) | `cpp/` | Header-only, C++23 templates, RAII |
| [Go](go/) | `go/` | Go 1.21+ generics, CLI tool |
| [Python](python/) | `python/` | Pure Python, NumPy integration, mmap |
| [C](c/) | `c/` | C99, zero dependencies, static lib |

## Building and Testing

```bash
# C++
cmake -B build cpp && cmake --build build
cd build && ctest --output-on-failure    # fast + medium tests (~2 min)

# Go
cd go && go test ./zeroipc/...

# Python
cd python && pip install -e . && python -m pytest tests/

# C
cd c && make && make test

# Cross-language
cd interop && ./test_interop.sh
cd go && go run ./cmd/interop
```

See [docs/TESTING_STRATEGY.md](docs/TESTING_STRATEGY.md) for test categories (FAST/MEDIUM/SLOW/STRESS) and CI configuration.

## CLI Tool

```bash
cd go && go build -o zeroipc ./cmd/zeroipc

./zeroipc list                              # List shared memory segments
./zeroipc show /sensor_data                 # Inspect segment and structures
./zeroipc array /sensor_data temperatures   # Inspect specific structure
./zeroipc monitor /sensors temp_stream      # Real-time stream monitoring
./zeroipc repl /sensor_data                 # Interactive exploration
```

Supports all 16 structure types. See [docs/cli_tools.md](docs/cli_tools.md) for full documentation.

## Use Cases

ZeroIPC is for **single-machine, multi-process** scenarios:
- High-frequency sensor data sharing
- Multi-process simulations and scientific computing
- Real-time analytics pipelines
- Cross-language data processing

Not designed for: network distribution, persistent storage, or general-purpose memory allocation.

## Documentation

- [Binary Specification](SPECIFICATION.md) — wire format all implementations follow
- [Architecture](docs/architecture.md) — system design and memory layout
- [API Reference](docs/api_reference.md) — complete API docs
- [Codata Guide](docs/codata_guide.md) — futures, streams, lazy evaluation
- [Sync Primitives Guide](docs/sync_primitives_guide.md) — mutex, monitor, rwlock, etc.
- [Design Philosophy](design_philosophy.md) — core principles and trade-offs
- [The Single-Machine Thesis](single_machine_thesis.md) — why shared memory IPC matters
- [Design Patterns](docs/patterns.md) — cross-process communication patterns
- [CLI Tools](docs/cli_tools.md) — command-line utilities
- [Testing Strategy](docs/TESTING_STRATEGY.md) — test suite architecture
- [Examples](docs/examples/) — working code examples

## Contributing

1. Follow the [binary specification](SPECIFICATION.md) exactly
2. Implement Memory, Table, and Array as minimum
3. Add cross-language tests in `interop/`

## License

MIT
