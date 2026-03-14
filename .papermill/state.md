# Papermill State

## Paper Identity

- **Working Title**: The Single-Machine Thesis: Why Shared Memory IPC is a Different Computational Model
- **Short Name**: single-machine-thesis
- **Stage**: outlined (ready for drafting)
- **Target Venue**: TBD (candidates: HotOS workshop, arXiv preprint, USENIX ;login:, ACM Queue)
- **Format**: LaTeX

## Authors

1. **Alexander Towell**
   - Affiliation: Southern Illinois University Edwardsville
   - Email: lex@metafunctor.com
   - ORCID: 0000-0001-6443-9897
   - Role: sole author

## Thesis

Single-machine shared memory IPC is a fundamentally different computational model from distributed systems over a network. Modern software applies network-oriented patterns (serialization, message queues, consensus protocols) to co-located processes, paying enormous overhead for problems that don't exist on a single machine. Recognizing this distinction enables orders-of-magnitude performance improvements and a multi-paradigm programming model where C++, Python, Go, C, and shell scripts share lock-free data structures through a common binary format with zero serialization.

## Key Claims

1. Shared memory on a single machine has properties (reliable delivery, bounded latency, causal ordering, shared mutable state) that make it categorically different from networked IPC
2. These properties enable lock-free algorithms (CAS loops, Vyukov MPMC queues) that are impossible over a network
3. A minimal binary format enables cross-language zero-copy interop without type annotations in shared memory
4. This model naturally extends to multi-paradigm programming: compiled languages for performance, scripting languages for analysis, CLI tools for operations
5. Measured performance: 149M queue ops/sec, 26ns latency — orders of magnitude faster than localhost TCP/HTTP

## Existence Proof: ZeroIPC

- 4 language implementations: C++23, C99, Python, Go
- Lock-free data structures: Queue (Vyukov MPMC), Stack (4-state CAS), Array, Ring, Map, Set, Pool
- Synchronization primitives: Semaphore, Barrier, Latch, Mutex, Once, Event, Monitor, RWLock, Signal
- Computational structures: Future, Lazy, Stream, Channel
- CLI tools: Go CLI (inspection), C++ REPL (interactive), Python API (scripting)
- Cross-language interop: verified 3-way (C++ <-> Python <-> Go)
- Test suite: 524 Python + 23 C++ suites + Go + C tests

## Source Materials

- `docs/single_machine_thesis.md` — core thesis argument (strongest existing writing)
- `whitepaper/zeroipc_whitepaper.tex` — old paper (wrong framing, stale numbers)
- `whitepaper/submissions/arXiv/` — most complete old version (reference only)
- `whitepaper/submissions/JOSS/` — software paper (separate venue)
- `docs/design_philosophy.md` — design rationale
- `SPECIFICATION.md` — binary format spec
- `cpp/benchmarks/` — C++ benchmark suite (Queue, Stack, Array)
- `c/benchmarks/` — C benchmark suite
- `python/benchmarks/` — Python benchmark suite

## Measured Benchmarks (2026-03-13, 12-core machine, C++ -O3)

### Queue (Vyukov MPMC)

**Single-thread throughput:**
| Element | Push | Pop |
|---------|------|-----|
| int (4B) | 119M/s | 124M/s |
| 64B | 93M/s | 105M/s |
| 256B | 38M/s | 56M/s |
| 1KB | 11M/s | 167M/s* |
| 4KB | 2.7M/s | 187M/s* |

**Latency (int32):** avg 30ns push / 31ns pop, p50=30ns, p99=40ns

**Concurrent producer-consumer:**
| Threads (P+C) | Throughput |
|--------------|------------|
| 2 (1+1) | 24.6M ops/s |
| 4 (2+2) | 12.4M ops/s |
| 8 (4+4) | 9.7M ops/s |
| 12 (6+6) | 9.5M ops/s |

**Contention scaling (mixed push/pop):**
| Threads | Throughput | Success |
|---------|-----------|---------|
| 1 | 152.6M/s | 100% |
| 2 | 17.2M/s | 99.8% |
| 4 | 12.4M/s | 99.9% |
| 8 | 10.8M/s | 100% |
| 12 | 9.6M/s | 72.9% |

### Stack (4-state CAS)

**Single-thread throughput:**
| Element | Push | Pop |
|---------|------|-----|
| int (4B) | 118M/s | 117M/s |
| 64B | 111M/s | 113M/s |
| 256B | 73M/s | 113M/s* |
| 1KB | 11M/s | 239M/s* |

**Latency (int32):** avg 30ns push / 31ns pop / 25ns peek, p99=40ns

**Concurrent (mixed push/pop):**
| Threads | Throughput |
|---------|-----------|
| 2 | 80.6M/s |
| 4 | 65.5M/s |
| 8 | 43.4M/s |
| 12 | 49.4M/s |

**LIFO pattern:** 215-230M ops/sec (batch-size independent)

### Array (zero-copy access)
- Sequential 1M int32: ~1.8B ops/sec read and write
- int32 bandwidth: 14.4 GB/s read, 12.9 GB/s write
- double bandwidth: 17.6 GB/s read, 12.7 GB/s write

### Python (pure Python, no C extensions)
- int32 push: 240K/s, pop: 231K/s
- 64B push: 230K/s, pop: 211K/s
- 256B push: 295K/s, pop: 242K/s
- Still ~50x faster than localhost HTTP round-trips (~5K req/sec)

### Key comparisons
| Mechanism | Latency | vs ZeroIPC |
|-----------|---------|------------|
| ZeroIPC C++ queue push | 30ns | baseline |
| L1 cache access | ~1ns | 30x faster |
| L2 cache access | ~5ns | 6x faster |
| L3 cache access | ~20ns | 1.5x faster |
| RAM access | ~100ns | 3x slower |
| Unix pipe RT | ~5,000ns | 170x slower |
| Unix domain socket RT | ~10,000ns | 330x slower |
| Localhost TCP RT | ~10,000ns | 330x slower |
| ZeroMQ (ipc://) | ~30,000ns | 1000x slower |

*Pop faster for large elements = cache effect (data still warm from push)

## Benchmark TODO
- [x] Fix concurrent queue/stack benchmarks
- [x] Run Python benchmarks (partial — 240K ops/sec for int32)
- [ ] Add cross-language latency benchmark (C++ writes, Python reads)
- [ ] Measure actual localhost TCP/pipe/socket baseline on this machine
- [ ] NUMA effects on multi-socket systems

## Prior Art
- [x] Literature survey complete — 49 verified references in `.papermill/surveys/prior-art.md`
- [x] Gap analysis: no prior system combines lock-free shm containers + cross-language binary compat + sync primitives + codata + CLI
- [x] Three key competitors identified: Boost.Interprocess, ByteDance shmipc, rigtorp/MPMCQueue

## Outline
- [x] Paper outline complete — `.papermill/outline.md`
- 8 sections: Introduction, Network Tax, Four Properties, ZeroIPC Design, Evaluation, Related Work, Discussion, Conclusion
- 9 planned figures/tables
- Anticipated reviewer questions documented

## Remaining TODO
- [ ] Add cross-language latency benchmark (C++ writes, Python reads)
- [ ] Measure actual localhost TCP/pipe/socket baseline on this machine
- [ ] NUMA effects on multi-socket systems (future work, not blocking)
- [ ] Draft paper (use papermill:draft)

## Timeline
- 2026-03-13: Papermill initialized, old submissions cleaned, benchmarks run
- 2026-03-13: Prior art survey complete (49 references)
- 2026-03-13: Paper outline complete (8 sections)
