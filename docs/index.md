# ZeroIPC

**High-performance cross-language shared memory IPC.**

Zero-copy data sharing between processes in C++, Python, Go, and C. No serialization, no bindings — parallel native implementations of the same binary format.

---

## Documents

<div class="grid cards" markdown>

-   :material-lightbulb-on:{ .lg .middle } **The Single-Machine Thesis**

    ---

    Why shared memory IPC on a single machine is a fundamentally different computational model than distributed systems over a network.

    [:octicons-arrow-right-24: Read the thesis](single_machine_thesis.md)

-   :material-hammer-wrench:{ .lg .middle } **Design Philosophy**

    ---

    The deliberate constraints that enable ZeroIPC's simplicity and performance — and why we chose them.

    [:octicons-arrow-right-24: Read the philosophy](design_philosophy.md)

</div>

## At a Glance

| Property | Value |
|----------|-------|
| **Languages** | C++23, C99, Python, Go |
| **Queue latency** | 30ns (Vyukov MPMC) |
| **Queue throughput** | 119M ops/sec single-thread |
| **Array bandwidth** | 14.4 GB/s read |
| **Data structures** | 8 core + 9 sync + 4 codata |
| **Serialization** | None (zero-copy binary format) |

## Links

- [Source code](https://github.com/queelius/zeroipc)
- [Binary specification](https://github.com/queelius/zeroipc/blob/master/SPECIFICATION.md)
