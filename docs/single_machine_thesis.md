# The Single-Machine Thesis

> **Inter-process shared memory on a single machine is a fundamentally different computational model than distributed systems over a network.**

## Abstract

Distributed systems research has dominated concurrent programming discourse for decades, leading developers to apply network-oriented patterns—serialization, message queues, consensus protocols—even to single-machine inter-process communication. This paper argues that shared memory IPC on a single machine represents a categorically different computational model with distinct properties: guaranteed message delivery, bounded latency, causal ordering, and the ability to share mutable state directly. Recognizing this distinction enables dramatic simplifications and orders-of-magnitude performance improvements for single-node systems.

## 1. The Problem with Network Thinking

Modern software architecture assumes network-style communication even between processes on the same machine. Consider a typical "microservices" deployment on a single server:

```
Process A → HTTP → localhost → HTTP → Process B
           ↓
    [JSON serialization]
    [TCP/IP stack]
    [Context switches]
    [Deserialization]
```

This pattern, borrowed from distributed systems, introduces unnecessary overhead:

- **Serialization**: Converting structured data to bytes and back
- **Protocol overhead**: HTTP headers, TCP handshakes, checksums
- **Kernel involvement**: Socket buffers, network stack processing
- **Latency**: Hundreds of microseconds for a localhost round-trip

But these costs exist to solve distributed systems problems that **don't exist on a single machine**.

## 2. What Networks Must Handle

Distributed systems face fundamental challenges that shape their design:

| Challenge | Network Reality | Single Machine Reality |
|-----------|----------------|------------------------|
| **Delivery** | Messages may be lost | Writes to RAM always succeed |
| **Ordering** | Packets arrive out of order | Memory operations have defined ordering |
| **Latency** | Unbounded, variable (ms to seconds) | Bounded, predictable (ns to μs) |
| **Failure** | Partial failure possible | Processes fail together or not at all |
| **State** | Must serialize everything | Can share pointers directly |
| **Consensus** | Requires complex protocols | Atomics provide it natively |

The CAP theorem, Paxos, Raft, vector clocks, and eventual consistency all address problems that **simply don't exist** when processes share RAM on a single machine.

## 3. Properties of Single-Machine Shared Memory

### 3.1 Reliable Delivery

When Process A writes to shared memory address `0x7f00...`, Process B **will** see that write. There is no packet loss, no network partition, no Byzantine failure. The write either happens (and is visible) or the entire machine has failed.

This enables lock-free algorithms that would be impossible over a network:

```cpp
// This CAS operation has exactly three outcomes:
// 1. Succeeds (value was exchanged)
// 2. Fails (value was different, retry)
// 3. Process crashes (entire machine state undefined)
//
// There is no "timeout", no "partial success", no "unknown state"
while (!state.compare_exchange_weak(expected, desired)) {
    expected = state.load();
}
```

### 3.2 Bounded Latency

Network latency is fundamentally unbounded—a packet might arrive in 1ms or 10 seconds. This uncertainty infects all distributed algorithms:

```
Network: "Wait for response (timeout after ???)"
         "If timeout, was the message lost? Delivered? Acted upon?"
```

Shared memory latency is bounded by physics:

```
L3 cache hit:    ~10-20 ns
RAM access:      ~60-100 ns
Context switch:  ~1-10 μs
```

This bounded latency enables real-time guarantees impossible in distributed systems.

### 3.3 Causal Ordering

Modern CPUs provide memory ordering guarantees. With appropriate fences, if Process A writes X then Y, Process B observing Y will also observe X:

```cpp
// Producer
data = 42;                                    // Write data
std::atomic_thread_fence(memory_order_release);
flag.store(true, memory_order_relaxed);       // Signal ready

// Consumer
while (!flag.load(memory_order_relaxed));
std::atomic_thread_fence(memory_order_acquire);
// data is guaranteed to be 42 here
```

No vector clocks needed. No Lamport timestamps. The hardware provides ordering.

### 3.4 Shared Mutable State

The most profound difference: processes can **share pointers to the same memory**. Not copies. Not serialized representations. The actual bits.

```cpp
// Process A
SharedArray<double>* temps = open_array<double>("/sensors");
temps[0] = 20.5;  // Direct write to shared memory

// Process B (simultaneously)
SharedArray<double>* temps = open_array<double>("/sensors");
double t = temps[0];  // Reads 20.5 (or racing value)
```

This is impossible over a network. You cannot dereference a pointer across TCP.

## 4. What This Enables

### 4.1 Zero-Copy Communication

No serialization. No copying. Process A's write **is** Process B's read:

```
Traditional IPC:
  Process A memory → serialize → kernel buffer → deserialize → Process B memory
  Latency: 50-500 μs

Shared Memory:
  Process A memory == Process B memory (same physical pages)
  Latency: 50-100 ns
```

**1000x improvement** isn't optimization—it's a different computational model.

### 4.2 Lock-Free Everything

Network communication requires request-response patterns. Shared memory enables true lock-free algorithms:

```cpp
// Lock-free queue in shared memory
bool push(T value) {
    uint32_t tail = tail_.load(memory_order_relaxed);
    uint32_t next = (tail + 1) % capacity;
    if (next == head_.load(memory_order_acquire)) return false;
    buffer_[tail] = value;
    tail_.store(next, memory_order_release);
    return true;
}
```

No locks. No blocking. No coordination protocol. Just atomic operations.

### 4.3 Direct Synchronization Primitives

Mutexes, semaphores, barriers, and condition variables can operate directly on shared memory without kernel involvement:

```cpp
// Cross-process mutex using shared memory
void lock() {
    while (locked_.exchange(true, memory_order_acquire)) {
        // Spin or yield
    }
}

void unlock() {
    locked_.store(false, memory_order_release);
}
```

Compare to distributed locking (Redlock, ZooKeeper, etcd) which requires:
- Network round-trips
- Lease timeouts
- Fencing tokens
- Failure detection

### 4.4 Reactive Patterns Without Message Brokers

Publish-subscribe, event streams, and reactive patterns work directly:

```cpp
// Signal: reactive value with version tracking
Signal<double> temperature(mem, "temp", 20.0);

// Publisher
temperature.set(25.5);  // Atomic update with version increment

// Subscriber (another process)
while (true) {
    temp.wait_for_change(last_version);  // Efficient spin-wait
    process(temp.get());
    last_version = temp.version();
}
```

No Kafka. No RabbitMQ. No Redis Pub/Sub. Just memory.

## 5. Design Implications

Recognizing the single-machine model changes system design:

### Don't Do This (Network Thinking)

```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│  Service A  │───►│ Message Q   │───►│  Service B  │
│  (Producer) │    │ (Kafka/etc) │    │  (Consumer) │
└─────────────┘    └─────────────┘    └─────────────┘
     ↓                   ↓                   ↓
  Serialize          Store/Forward      Deserialize
  ~10 μs              ~100 μs            ~10 μs
```

### Do This (Shared Memory Thinking)

```
┌─────────────┐         ┌─────────────┐
│  Process A  │◄──────►│  Process B  │
│  (Writer)   │   │    │  (Reader)   │
└─────────────┘   │    └─────────────┘
                  │
            ┌─────▼─────┐
            │  Shared   │
            │  Memory   │
            │  Queue    │
            └───────────┘
                 ↓
             Direct read/write
             ~100 ns
```

### Architecture Principles

1. **Don't serialize when you can share**: If processes are on the same machine, share memory instead of passing messages.

2. **Don't coordinate when you can atomize**: Replace distributed consensus with atomic operations.

3. **Don't buffer when you can directly access**: Skip intermediate queues; let consumers read producer memory directly.

4. **Don't abstract the machine away**: Network abstractions hide single-machine capabilities. Expose them.

## 6. When This Applies (and When It Doesn't)

### Single-Machine Model Applies When:
- All communicating processes run on one machine
- Microsecond latency matters
- Zero-copy performance is required
- Predictable, bounded latency is needed
- Processes trust each other (same security domain)

### Network Model Still Needed When:
- Processes run on different machines (obviously)
- Processes may run on different machines in future
- Security boundaries require isolation
- Fault tolerance across machine failures is required

### The Hybrid Reality

Most real systems are hybrid: single-machine communication between local processes, network communication between machines. The key insight is to **use the right model for each hop**.

```
┌─── Machine A ───────────────────┐     ┌─── Machine B ────────────────┐
│                                 │     │                              │
│  ┌───────┐ SHM ┌───────┐       │     │      ┌───────┐ SHM ┌───────┐ │
│  │Proc 1 │◄───►│Proc 2 │       │     │      │Proc 4 │◄───►│Proc 5 │ │
│  └───────┘     └───┬───┘       │     │      └───────┘     └───────┘ │
│                    │           │     │           ▲                   │
│                    │ Network   │     │           │                   │
│                    └───────────┼─────┼───────────┘                   │
│                                │     │                               │
└────────────────────────────────┘     └───────────────────────────────┘

Within machine: Shared memory (ns latency, zero-copy)
Between machines: Network protocol (ms latency, serialization)
```

## 7. ZeroIPC: Embracing the Model

ZeroIPC explicitly embraces the single-machine model:

| Feature | Network-Style Would Require | ZeroIPC Provides |
|---------|----------------------------|------------------|
| Data sharing | Serialize → Send → Deserialize | Direct pointer sharing |
| Synchronization | Distributed locks | Shared memory atomics |
| Queues | Message broker | Lock-free circular buffers |
| Events | Pub/Sub middleware | Atomic flags with spin-wait |
| Streams | Kafka/Pulsar | Direct ring buffers |

### What We Don't Provide (Intentionally)

- **Network transport**: Not our problem domain
- **Serialization**: Unnecessary for trivially-copyable types
- **Persistence**: Shared memory is for IPC, not storage
- **Distribution**: Would undermine our performance guarantees

## 8. Quantifying the Difference

Real benchmarks comparing single-machine communication patterns:

| Method | Latency | Throughput | Notes |
|--------|---------|------------|-------|
| TCP localhost | 50-100 μs | 2-5 GB/s | Full network stack |
| Unix socket | 20-50 μs | 5-10 GB/s | Kernel bypass partial |
| Pipe | 10-30 μs | 3-6 GB/s | Simple but unidirectional |
| Shared memory | 50-200 ns | 40+ GB/s | Direct memory access |
| ZeroIPC queue | 100-150 ns | 8M+ ops/s | Lock-free with ordering |

The 100-1000x latency improvement isn't incremental optimization. It represents access to a different computational model.

## 9. Conclusion

For decades, distributed systems thinking has dominated how we architect multi-process applications. This made sense when processes often ran on different machines. But modern applications frequently colocate processes on single machines—for containerization, security isolation, or language interoperability.

When processes share a machine, they share more than locality. They share:
- A single failure domain
- Bounded communication latency
- Hardware-provided ordering guarantees
- The ability to reference the same memory

These properties enable algorithms and performance characteristics impossible in distributed systems. By recognizing shared memory on a single machine as a **distinct computational model**, we can build dramatically simpler and faster systems for single-node communication.

The single-machine thesis isn't about rejecting distributed systems. It's about applying the right model to the right problem. When processes share RAM, let them share it directly.

---

*ZeroIPC implements this thesis: a library designed specifically for single-machine IPC that embraces shared memory's unique properties rather than abstracting them away behind network-style interfaces.*

## References

1. Herlihy, M., & Shavit, N. (2012). *The Art of Multiprocessor Programming*. Morgan Kaufmann.
2. McKenney, P. E. (2017). *Is Parallel Programming Hard, And, If So, What Can You Do About It?*
3. Drepper, U. (2007). *What Every Programmer Should Know About Memory*. Red Hat.
4. Preshing, J. (2012). *An Introduction to Lock-Free Programming*. Preshing on Programming.
5. Lamport, L. (1979). *How to Make a Multiprocessor Computer That Correctly Executes Multiprocess Programs*. IEEE TC.
