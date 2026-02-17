# ZeroIPC Documentation

Zero-copy shared memory IPC for C++, Python, Go, and C. See the [project README](../README.md) for an overview, quick start, and build instructions.

**Current version: 2.2.0** — lock-free concurrency fixes, Go implementation, 9 sync primitives.

## Guides

- [Architecture](architecture.md) — memory layout and system design
- [API Reference](api_reference.md) — complete API documentation
- [Codata Guide](codata_guide.md) — futures, streams, lazy evaluation
- [Sync Primitives Guide](sync_primitives_guide.md) — mutex, monitor, rwlock, event, etc.
- [Design Philosophy](design_philosophy.md) — core principles and trade-offs
- [The Single-Machine Thesis](single_machine_thesis.md) — why shared memory IPC matters
- [Design Patterns](patterns.md) — cross-process communication patterns

## Reference

- [Binary Specification](../SPECIFICATION.md) — wire format all implementations follow
- [Lock-Free Patterns](lock_free_patterns.md) — CAS loops, memory ordering, ABA prevention
- [CLI Tools](cli_tools.md) — Go-based inspection and debugging utilities
- [Testing Strategy](TESTING_STRATEGY.md) — test categories, timing config, CI setup

## Language Docs

- [C++](../cpp/README.md) — header-only C++23 templates
- [Go](../go/README.md) — generics-based implementation
- [Python](../python/README.md) — pure Python with NumPy
- [C](../c/README.md) — C99 static library

## Examples

- [Getting Started](examples/index.md) — working code examples
- [Tutorial](tutorial.md) — step-by-step introduction
