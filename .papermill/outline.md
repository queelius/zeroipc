# Paper Outline: The Single-Machine Thesis

**Working Title**: The Single-Machine Thesis: Why Shared Memory IPC is a Different Computational Model

**Narrative Arc**: Modern software applies network-oriented patterns to co-located processes, paying enormous overhead for problems that don't exist on a single machine. We formalize this observation as the "single-machine thesis," identify four properties that make shared memory IPC categorically different from networked communication, show that these properties enable lock-free cross-language data sharing with zero serialization, and demonstrate the thesis with measured performance on a real implementation spanning four programming languages.

**Target Length**: 12-16 pages (full paper) or 6-8 pages (workshop/short)

---

## Section 1: Introduction (1.5 pages)

**Purpose**: Hook the reader with the central tension, state the thesis, preview the contribution.

**Key Arguments**:
1. Open with Sun's "The Network is the Computer" (1984) as the reigning paradigm — then observe that modern deployments increasingly colocate processes (containers, sidecars, microservices on a single node)
2. The cost: Zhu et al. (2022, 2023) measured 185% latency overhead and 92% more vCPU in service mesh sidecars — most of this is serialization and socket IPC between *co-located* processes
3. The thesis: Shared memory IPC on a single machine is not merely "faster networking" — it is a *categorically different computational model* with properties that enable fundamentally different algorithms
4. Preview: We identify four properties (reliable delivery, bounded latency, causal ordering, shared mutable state), show they enable lock-free cross-language data sharing, and demonstrate this with ZeroIPC — 119M ops/sec queue throughput, 30ns latency, four languages sharing lock-free data structures through a common binary format

**Ends With**: "We do not claim that shared memory IPC is a new idea. We claim that recognizing it as a *different computational model* — not just a faster transport — changes what algorithms, abstractions, and system architectures become possible."

**Key Citations**: [Sun/Gage 1984], [Zhu et al. 2022], [Zhu et al. 2023], [Drepper 2007]

---

## Section 2: The Network Tax (1-1.5 pages)

**Purpose**: Quantify the problem. Show *why* this matters by measuring the overhead of applying network patterns to co-located processes.

**Key Arguments**:
1. The typical localhost IPC stack: process → serialize → syscall → kernel buffer → syscall → deserialize → process. Count the copies, context switches, and bytes touched.
2. The serialization tax: even "zero-copy" formats (FlatBuffers, Cap'n Proto) require schema compilation and immutable access patterns. True zero-copy means *no format conversion at all*.
3. The coordination tax: distributed consensus (Raft, Paxos) solves problems — unreliable delivery, Byzantine failures, network partitions — that cannot occur when processes share RAM.
4. Measured costs on our test machine (from benchmark TODOs — or cite Venkataraman 2015):
   - Localhost TCP round-trip: ~10,000ns
   - Unix domain socket: ~10,000ns
   - Unix pipe: ~5,000ns
   - Shared memory atomic: 30ns
   - **Gap: 170x-330x**

**Key Table**: Comparison of IPC mechanisms (latency, throughput, copies required, kernel crossings)

**Key Citations**: [Venkataraman 2015], [FlatBuffers], [Cap'n Proto], [Apache Arrow], [ZeroMQ]

---

## Section 3: Four Properties of the Single-Machine Model (2-2.5 pages)

**Purpose**: The formal contribution. Identify the four properties that distinguish shared memory IPC from networked communication and argue that they constitute a different computational model.

### 3.1 Reliable Delivery
- A write to a valid shared memory address *always* succeeds and *always* becomes visible to other processes mapping the same region
- No packet loss, no network partition, no partial delivery
- The only failure mode is total machine failure, which takes all processes down simultaneously (shared fate)
- **Consequence**: algorithms can assume writes succeed without acknowledgment — CAS loops have exactly two outcomes (success, retry), never "unknown"

### 3.2 Bounded Latency
- Memory access latency is bounded by physics: L1 ~1ns, L2 ~5ns, L3 ~20ns, RAM ~100ns
- Network latency is fundamentally unbounded — hence timeouts, retries, circuit breakers
- **Consequence**: real-time guarantees become possible. Spin-waits are viable synchronization because the wait is bounded.

### 3.3 Causal Ordering
- CPU memory models (x86 TSO, ARM with fences) provide ordering guarantees: if process A writes X then Y with a release fence, process B observing Y with an acquire fence will also observe X
- No vector clocks, no Lamport timestamps, no logical clocks — the hardware provides ordering
- **Consequence**: lock-free algorithms with release/acquire semantics are sufficient for producer-consumer coordination
- **Key Citations**: [Lamport 1979], [Boehm & Adve 2008], [Preshing 2012]

### 3.4 Shared Mutable State
- The most profound difference: processes can share *pointers to the same memory*. Not copies. Not serialized representations. The actual bits.
- Impossible over a network: you cannot dereference a pointer across TCP
- **Consequence**: zero-copy communication. Process A's write *is* Process B's read. No intermediate buffers, no serialization, no copying.

### 3.5 Synthesis: A Different Computational Model
- These four properties together enable algorithms impossible in the network model: lock-free queues (CAS), shared hash maps (atomic linear probing), reactive signals (atomic version counters)
- Position against the Actor model [Agha 1986] and CSP [Hoare 1978] — shared mutable state is not a bug, it's the mechanism that makes nanosecond IPC possible
- Acknowledge the limitation: this model applies *only* to a single machine. The contribution is recognizing when it applies and using different primitives accordingly.

**Key Citations**: [Herlihy 1991], [Lamport 1979], [Boehm & Adve 2008], [Agha 1986], [Hoare 1978]

---

## Section 4: Existence Proof — ZeroIPC Design (2-2.5 pages)

**Purpose**: Present the system design as an existence proof of the thesis. Show *what becomes possible* when you design for the single-machine model from scratch.

### 4.1 Binary Format: Minimal Metadata
- Table header (32 bytes): magic, version, entry_count, max_entries, memory_size, next_offset
- Table entries (48 bytes each): name (32 chars), offset (uint64), size (uint64)
- **No type information** — the table is a directory, not a schema. Users specify types at access time (templates in C++, dtype in Python, generics in Go)
- Design rationale: minimal metadata enables cross-language access without code generation or schema compilers. Any language that can memory-map a file and read known offsets can participate.

### 4.2 Lock-Free Data Structures in Shared Memory
- **Queue**: Vyukov bounded MPMC with per-slot sequence numbers — 1 CAS per push/pop, no ABA risk due to bounded indices [Vyukov 2014]
- **Stack**: 4-state CAS protocol (EMPTY → WRITING → READY → READING) — per-slot state machine eliminates torn reads
- **Map/Set**: Lock-free hash table with atomic linear probing and CAS-based insert
- Key insight: these algorithms are well-known in the *intra-process* setting (libcds, TBB, folly, crossbeam). Placing them in POSIX shared memory with a stable binary layout makes them *inter-process* and *cross-language*.
- Contrast with Boost.Interprocess: managed heap allocator vs. flat table; C++-only vs. 4 languages; STL container placement vs. purpose-built lock-free structures

### 4.3 Cross-Language Interop Without Serialization
- Four implementations: C++23 (header-only templates), C99 (static library), Python (mmap + struct/numpy), Go (generics + unsafe.Pointer)
- Each implementation reads the same bytes from the same shared memory. No marshaling, no code generation, no IDL.
- The "duck typing" principle: the table entry says "there are 1000 elements of 4 bytes each starting at offset 0x8000." C++ reads them as `float`, Python as `np.float32`, Go as `float32`. All three see the same IEEE 754 bits.
- Verified: 3-way interop tests (C++ writes → Python reads → Go reads, and every permutation)

### 4.4 Multi-Paradigm Programming Model
- The cross-language binary format enables a *multi-paradigm* workflow:
  - C++ for hot-path data production (119M ops/sec)
  - Python for analysis and visualization (240K ops/sec — still 50x faster than localhost HTTP)
  - Go for CLI tooling and services
  - Shell scripts via CLI tools for operations
- Not a new idea (polyglot programming), but a new *mechanism*: shared memory with zero serialization overhead, vs. the typical REST/gRPC/message-broker integration layer

**Key Citations**: [Boost.Interprocess], [Vyukov 2014], [rigtorp MPMCQueue], [Michael & Scott 1996], [Herlihy & Shavit 2020]

---

## Section 5: Evaluation (2.5-3 pages)

**Purpose**: Validate the thesis with measured performance. Answer: does the single-machine model deliver the promised orders-of-magnitude improvement?

### 5.1 Methodology
- Hardware: 12-core machine (report exact CPU, clock, cache sizes)
- C++ benchmarks compiled with -O3
- Python benchmarks using pure Python (no C extensions) — deliberately showing the *floor* of performance
- Each measurement: fresh shared memory region, proper warmup, microsecond-resolution timing
- Report: throughput (ops/sec), latency (ns, percentiles), and scaling behavior

### 5.2 Single-Thread Throughput
**Queue (Vyukov MPMC)**:
| Element Size | Push (ops/s) | Pop (ops/s) |
|---|---|---|
| 4B (int) | 119M | 124M |
| 64B | 93M | 105M |
| 256B | 38M | 56M |
| 1KB | 11M | 167M* |
| 4KB | 2.7M | 187M* |

**Stack (4-state CAS)**:
| Element Size | Push (ops/s) | Pop (ops/s) |
|---|---|---|
| 4B (int) | 118M | 117M |
| 64B | 111M | 113M |
| 256B | 73M | 113M* |
| 1KB | 11M | 239M* |

*Pop faster for large elements = cache warming effect from preceding push.

**Array (zero-copy)**:
- Sequential read/write: ~1.8B ops/sec for int32
- Bandwidth: 14.4 GB/s read, 12.9 GB/s write

**Discussion**: Single-thread throughput exceeds 100M ops/sec for small elements. Array access approaches memory bandwidth limits. Cache effects visible in asymmetric push/pop for large elements.

### 5.3 Latency
| Operation | Avg (ns) | p50 | p99 | p99.9 |
|---|---|---|---|---|
| Queue push | 30 | 30 | 40 | — |
| Queue pop | 31 | 30 | 40 | — |
| Stack push | 30 | — | 40 | — |
| Stack peek | 25 | — | — | — |

**Discussion**: 30ns average latency operates at L2/L3 cache speed. The tight p50-p99 spread (30-40ns) confirms bounded latency — a property impossible in network IPC where tail latencies span orders of magnitude.

### 5.4 Concurrency Scaling
**Queue producer-consumer**:
| Threads (P+C) | Throughput |
|---|---|
| 2 (1+1) | 24.6M ops/s |
| 4 (2+2) | 12.4M ops/s |
| 8 (4+4) | 9.7M ops/s |
| 12 (6+6) | 9.5M ops/s |

**Stack concurrent (mixed push/pop)**:
| Threads | Throughput |
|---|---|
| 2 | 80.6M/s |
| 4 | 65.5M/s |
| 8 | 43.4M/s |
| 12 | 49.4M/s |

**Honest discussion**: Concurrent throughput drops with thread count due to cache line contention on shared atomic head/tail. This is inherent to lock-free CAS algorithms, not specific to our implementation. Queue drops from 152M (single-thread) to 9.5M (12-thread) — an 16x reduction. The Disruptor [Thompson 2011] achieves better scaling through batching and padding; Nikolaev's FAA queue [2019] improves scalability through fetch-and-add. We deliberately chose the simpler Vyukov design; Section 7 discusses alternatives.

### 5.5 Cross-Language Performance
**Python (pure Python, no C extensions)**:
| Element | Push (ops/s) | Pop (ops/s) |
|---|---|---|
| 4B (int) | 240K | 231K |
| 64B | 230K | 211K |
| 256B | 295K | 242K |

**Discussion**: Pure Python achieves ~500x less throughput than C++. But 240K ops/sec is still ~50x faster than localhost HTTP round-trips (~5K req/sec). This validates the multi-paradigm model: a Python analysis script can read data produced by a C++ hot path at interactive speeds, without any serialization layer.

### 5.6 Comparison with Alternative IPC
| Mechanism | Latency | vs ZeroIPC |
|---|---|---|
| ZeroIPC C++ queue | 30ns | baseline |
| L3 cache access | ~20ns | 1.5x faster |
| RAM access | ~100ns | 3x slower |
| Unix pipe RT | ~5,000ns | 170x slower |
| Unix domain socket RT | ~10,000ns | 330x slower |
| Localhost TCP RT | ~10,000ns | 330x slower |
| ZeroMQ (ipc://) | ~30,000ns | 1,000x slower |

**Key Claims**: [Venkataraman 2015], [Thompson 2011], [ZeroMQ]

---

## Section 6: Related Work (1.5-2 pages)

**Purpose**: Position against prior art honestly. Acknowledge what exists, show where ZeroIPC differs, and identify the specific gap this work fills.

### 6.1 Lock-Free Data Structures
- Foundational: [Herlihy 1991], [Michael & Scott 1996], [Herlihy & Shavit 2020]
- Vyukov MPMC queue [2014]: the specific algorithm we implement. rigtorp/MPMCQueue [2016] is the most direct single-process C++ implementation. folly MPMCQueue, moodycamel, libcds all provide similar structures *within a single process*.
- Our contribution is not the algorithms — it is placing them in shared memory with a stable cross-language binary layout.
- Wait-free alternatives: [Kogan & Petrank 2011, 2012], [Timnat & Petrank 2014]. We chose lock-free (not wait-free) for simplicity; wait-free adds complexity for marginal performance gain.

### 6.2 Shared Memory IPC Systems
- **Boost.Interprocess**: The most direct C++ competitor. Managed segments with allocator, STL-compatible containers, synchronization primitives. Key differences: managed heap allocator (overhead, complexity) vs. flat table (minimal, predictable); C++-only vs. 4 languages; no lock-free concurrent containers.
- **ByteDance shmipc** [CloudWeGo 2023]: Production-deployed at 3,000+ services, 24% CPU reduction. Uses Unix/TCP sockets for signaling, shared memory for data transfer. Key differences: buffer-passing (not concurrent containers), Go/Rust only, requires socket signaling channel.
- **XMem** [Wegiel & Krintz 2008] and **CoLoRS** [2010]: Cross-language shared memory with GC integration. Runtime-level approach vs. our library-level approach. Requires managed runtimes.

### 6.3 Zero-Copy Serialization Formats
- Apache Arrow, FlatBuffers, Cap'n Proto: minimize but do not eliminate serialization overhead. Require schema compilation and code generation. Arrow is columnar/analytical, not concurrent. All three assume immutable data after construction.
- ZeroIPC takes the logical extreme: no serialization *at all*, because trivially-copyable types in shared memory need no format conversion.

### 6.4 High-Performance IPC and Bypass
- LMAX Disruptor [Thompson 2011]: closest industrial antecedent. Lock-free ring buffer, 25M+ msg/sec, <50ns latency. But: *intra-process* (threads in a JVM), not cross-process, not cross-language.
- DPDK, SPDK, io_uring: kernel bypass for networking/storage using shared memory rings. Support the thesis that co-located communication belongs in shared memory. Different problem domain (network/storage vs. IPC).
- ZeroMQ: high-performance messaging library with IPC transport. Still uses kernel IPC mechanisms underneath; 1,000x slower than direct shared memory.

### 6.5 The Gap
No prior system combines: (1) lock-free shared memory data structures, (2) cross-language binary compatibility without schema/codegen, (3) synchronization primitives, (4) codata structures, and (5) CLI tooling in a single coherent system. More importantly: no prior work makes the explicit architectural argument that shared memory IPC constitutes a *different computational model* rather than merely a faster transport.

---

## Section 7: Discussion (1-1.5 pages)

**Purpose**: Address limitations honestly, discuss design trade-offs, and point toward future work.

### 7.1 Limitations
- **Trivially-copyable types only**: Cannot share objects with pointers, vtables, or heap allocations. This is fundamental to zero-copy cross-language interop — not a bug, but a design constraint.
- **No defragmentation**: bump allocator means structures cannot be individually freed. Suitable for long-lived structures, not dynamic allocation patterns.
- **Single machine only**: The thesis explicitly does *not* apply to multi-machine systems. CXL may extend it (see 7.3).
- **Trust model**: all processes sharing memory must be in the same trust domain. No isolation between processes accessing the same segment.

### 7.2 Scalability and Contention
- Honest: CAS-based lock-free structures show throughput degradation under high contention (Section 5.4). This is fundamental to CAS algorithms [Hendler et al. 2010 on flat combining].
- Vyukov vs. FAA: Nikolaev's FAA queue [2019] achieves better scaling by replacing CAS with fetch-and-add. A future implementation could swap algorithms.
- Disruptor-style batching: not yet implemented, but compatible with the binary format.

### 7.3 Future Directions
- **CXL (Compute Express Link)**: extends cache-coherent shared memory across PCIe. ZeroIPC's model could work unmodified across CXL fabric — the four properties hold as long as cache coherence holds.
- **NUMA awareness**: flat shared memory assumes UMA. NUMA-aware structure placement could improve multi-socket performance.
- **Memory ordering across architectures**: x86 provides strong ordering (TSO); ARM is weaker. Current implementation uses explicit fences; formal verification of the cross-architecture correctness of each CAS protocol is future work.
- **Process lifecycle**: shared memory persists across process restarts. The table and lock-free structures are designed to be crash-safe (no persistent locks), but a formal analysis of recovery semantics is warranted.

---

## Section 8: Conclusion (0.5 pages)

**Purpose**: Restate the thesis, summarize the contribution, close with the key insight.

**Key Points**:
1. Single-machine shared memory IPC is a different computational model, not just a faster transport
2. Four properties (reliable delivery, bounded latency, causal ordering, shared mutable state) enable lock-free algorithms impossible over a network
3. A minimal binary format enables cross-language zero-copy interop without serialization or schema compilation
4. Measured: 119M ops/sec, 30ns latency, four languages sharing data structures through a common binary layout
5. The contribution is not the individual algorithms — it is recognizing that the single-machine setting enables a qualitatively different approach to IPC, and demonstrating this with a working system

**Closing Line (draft)**: "When Sun Microsystems declared 'the network is the computer,' they were right about the future of distributed systems. But for the processes sharing your RAM right now, the computer is still the computer."

---

## Figures and Tables Plan

1. **Figure 1**: Architecture diagram — network-style IPC (serialize → kernel → deserialize) vs. shared memory IPC (direct access). The opening visual.
2. **Figure 2**: Memory layout diagram — Table header, entries, structures in shared memory. Shows the binary format.
3. **Figure 3**: Vyukov MPMC queue algorithm — per-slot sequence numbers, CAS push/pop. Key algorithm visualization.
4. **Table 1**: Network Reality vs. Single Machine Reality — the four properties comparison (from Section 3).
5. **Table 2**: Single-thread throughput results (Queue, Stack, Array) across element sizes.
6. **Table 3**: Latency comparison — ZeroIPC vs. pipe vs. socket vs. TCP vs. ZeroMQ.
7. **Figure 4**: Concurrency scaling graph — throughput vs. thread count for Queue and Stack.
8. **Table 4**: Cross-language performance — C++ vs. Python (pure) vs. localhost HTTP baseline.
9. **Table 5**: Related work comparison matrix — features (lock-free containers, cross-language, zero-copy, sync primitives, CLI) by system (Boost, shmipc, Arrow, TBB, Disruptor, ZeroIPC).

---

## Appendix Material (if venue allows)

- A. Complete binary format specification (from SPECIFICATION.md)
- B. Full benchmark methodology and raw data
- C. Code examples: the same data structure accessed from C++, Python, Go, and CLI
- D. Synchronization primitives catalog (Semaphore, Barrier, Latch, etc.)

---

## Writing Notes

### Tone
- Position paper / systems contribution, not pure theory
- Honest about what is novel (the synthesis and framing) and what is not (the individual algorithms)
- Acknowledge the strongest competitors directly (Boost.Interprocess, ByteDance shmipc, rigtorp/MPMCQueue)
- Use measured data throughout — no estimates, no theoretical bounds

### Anticipated Reviewer Questions
1. "How is this different from Boost.Interprocess?" → Section 6.2 + design philosophy (flat table vs. managed allocator, cross-language vs. C++-only)
2. "The algorithms are all known" → Yes. The contribution is the system design + the explicit thesis. Section 6.5 states the gap.
3. "Why not wait-free?" → Section 6.1 discusses trade-off; lock-free chosen for simplicity
4. "Concurrent scaling is poor" → Section 5.4 + 7.2 discuss honestly; this is inherent to CAS, not our implementation
5. "What about NUMA?" → Section 7.3 acknowledges as future work
6. "Memory ordering across languages?" → Section 7.3 flags as open question
7. "Trust model?" → Section 7.1 acknowledges; same trust domain is a requirement
