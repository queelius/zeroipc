# TDD Best Practices for Lock-Free Concurrent Systems

## Introduction

Lock-free data structures present unique testing challenges. Traditional unit testing approaches fail to catch race conditions, memory ordering bugs, and ABA problems. This guide provides proven strategies for test-driven development of lock-free systems like ZeroIPC.

---

## The Testing Pyramid for Lock-Free Code

```
         /\
        /  \  E2E Tests (Few)
       /____\  - Cross-process scenarios
      /      \  - Full system validation
     /________\ Integration Tests (Some)
    /          \ - Multi-threaded stress
   /____________\ - Concurrent invariants
  /              \ Unit Tests (Many)
 /________________\ - Single-threaded correctness
                    - Algorithm validation
```

### Layer 1: Deterministic Functional Tests (70%)

**Purpose:** Verify algorithm correctness without concurrency

**Why:** Lock-free bugs are hard to debug. Ensure basic correctness first.

**Example:**
```cpp
TEST_F(QueueTest, FIFOOrdering) {
    Queue<int> q(mem, "test", 100);

    // Single-threaded - deterministic
    for (int i = 0; i < 50; i++) {
        ASSERT_TRUE(q.push(i));
    }

    for (int i = 0; i < 50; i++) {
        auto val = q.pop();
        ASSERT_TRUE(val.has_value());
        EXPECT_EQ(*val, i) << "FIFO order violated at index " << i;
    }
}
```

**What to test:**
- Basic operations (push, pop, empty, full)
- Boundary conditions (empty, full, wrap-around)
- Edge cases (capacity-1, capacity, capacity+1)
- Error handling (invalid arguments, null pointers)

### Layer 2: Invariant-Based Concurrent Tests (20%)

**Purpose:** Verify correctness properties hold under concurrency

**Why:** Lock-free algorithms maintain specific invariants that must survive all thread interleavings.

**Example:**
```cpp
TEST_F(QueueTest, MPMCConservation) {
    Queue<int> q(mem, "test", 1000);

    std::atomic<int> sum_produced{0};
    std::atomic<int> sum_consumed{0};
    const int threads = 8;
    const int items_per_thread = 1000;

    auto producer = [&](int thread_id) {
        int local_sum = 0;
        for (int i = 0; i < items_per_thread; i++) {
            int value = thread_id * items_per_thread + i;
            while (!q.push(value)) std::this_thread::yield();
            local_sum += value;
        }
        sum_produced.fetch_add(local_sum);
    };

    auto consumer = [&]() {
        int local_sum = 0;
        for (int i = 0; i < items_per_thread; i++) {
            while (true) {
                auto val = q.pop();
                if (val.has_value()) {
                    local_sum += *val;
                    break;
                }
                std::this_thread::yield();
            }
        }
        sum_consumed.fetch_add(local_sum);
    };

    // Half producers, half consumers
    std::vector<std::thread> threads_vec;
    for (int i = 0; i < threads/2; i++) {
        threads_vec.emplace_back(producer, i);
    }
    for (int i = 0; i < threads/2; i++) {
        threads_vec.emplace_back(consumer);
    }

    for (auto& t : threads_vec) t.join();

    // Invariant: Conservation of values
    EXPECT_EQ(sum_produced.load(), sum_consumed.load())
        << "Values were lost or duplicated";
    EXPECT_TRUE(q.empty()) << "Queue should be empty after all operations";
}
```

**Invariants to verify:**
- **Conservation:** produced == consumed
- **No duplication:** Each value appears exactly once
- **No loss:** All values accounted for
- **Consistency:** State variables are coherent (e.g., empty() ⟺ size()==0)

### Layer 3: Stress Tests (10%)

**Purpose:** Force race conditions to surface through sustained high contention

**Why:** Some race conditions only appear under extreme load.

**Example:**
```cpp
TEST_F(QueueStressTest, HighContentionSmallQueue) {
    // Small queue + many threads = maximum contention
    Queue<int> q(mem, "test", 10);  // Only 10 slots

    const int threads = 32;  // Way more threads than slots
    const int iterations = 10000;
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    auto producer = [&]() {
        for (int i = 0; i < iterations; i++) {
            while (!q.push(i)) {
                std::this_thread::yield();
                // This loop will spin frequently due to contention
            }
            produced.fetch_add(1);
        }
    };

    auto consumer = [&]() {
        for (int i = 0; i < iterations; i++) {
            while (!q.pop().has_value()) {
                std::this_thread::yield();
            }
            consumed.fetch_add(1);
        }
    };

    std::vector<std::thread> threads_vec;
    for (int i = 0; i < threads/2; i++) {
        threads_vec.emplace_back(producer);
        threads_vec.emplace_back(consumer);
    }

    for (auto& t : threads_vec) t.join();

    int expected = (threads/2) * iterations;
    EXPECT_EQ(produced.load(), expected);
    EXPECT_EQ(consumed.load(), expected);
}
```

**Key technique:** queue_size << num_threads forces CAS failures

---

## Common Lock-Free Bug Patterns & How to Test

### 1. ABA Problem

**What it is:** CAS succeeds when it shouldn't because value wrapped around

```
Thread 1: Read A
Thread 2: Change A→B→A
Thread 1: CAS(A, new) succeeds incorrectly
```

**How to test:**
```cpp
TEST_F(ABATest, RapidRecycling) {
    Stack<int> s(mem, "test", 100);

    // Rapidly push/pop same values to reuse memory locations
    auto worker = [&]() {
        for (int gen = 0; gen < 10000; gen++) {
            s.push(42);
            auto val = s.pop();
            // If ABA problem exists, might see:
            // - Corruption
            // - Lost updates
            // - Unexpected empty states
            ASSERT_TRUE(val.has_value());
            EXPECT_EQ(*val, 42);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 8; i++) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) t.join();
}
```

**What to look for:**
- Segfaults (accessing freed memory)
- Corrupted values
- Lost items
- Unexpected empty/full states

### 2. Memory Ordering Bugs

**What it is:** Insufficient fences allow torn reads or stale values

```cpp
// WRONG: Missing fence
tail.store(new_tail, std::memory_order_relaxed);
data[old_tail] = value;  // Might be reordered before tail update!
```

**How to test:**
```cpp
TEST_F(MemoryOrderingTest, NoTornReads) {
    struct Pair { int x, y; };
    Queue<Pair> q(mem, "test", 100);

    auto producer = [&]() {
        for (int i = 0; i < 10000; i++) {
            q.push({i, i});  // Always matching values
        }
    };

    auto consumer = [&]() {
        for (int i = 0; i < 10000; i++) {
            auto val = q.pop();
            while (!val.has_value()) {
                std::this_thread::yield();
                val = q.pop();
            }
            // If memory ordering is wrong, might see {5, 7}
            EXPECT_EQ(val->x, val->y)
                << "Torn read: x=" << val->x << ", y=" << val->y;
        }
    };

    std::thread p(producer);
    std::thread c(consumer);
    p.join();
    c.join();
}
```

**Use ThreadSanitizer:**
```bash
cmake -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" -B build .
cd build && ctest
```

### 3. Lost Updates

**What it is:** Concurrent operations clobber each other's changes

**How to test:**
```cpp
TEST_F(QueueTest, NoDuplicatesOrLoss) {
    Queue<int> q(mem, "test", 1000);

    std::set<int> expected_values;
    std::mutex set_mutex;

    auto producer = [&](int thread_id) {
        for (int i = 0; i < 1000; i++) {
            int value = thread_id * 1000 + i;
            {
                std::lock_guard<std::mutex> lock(set_mutex);
                expected_values.insert(value);
            }
            while (!q.push(value)) std::this_thread::yield();
        }
    };

    std::vector<int> consumed_values;
    std::mutex vec_mutex;

    auto consumer = [&]() {
        for (int i = 0; i < 1000; i++) {
            auto val = q.pop();
            while (!val.has_value()) {
                std::this_thread::yield();
                val = q.pop();
            }
            std::lock_guard<std::mutex> lock(vec_mutex);
            consumed_values.push_back(*val);
        }
    };

    std::thread p1(producer, 0);
    std::thread p2(producer, 1);
    std::thread c1(consumer);
    std::thread c2(consumer);

    p1.join(); p2.join(); c1.join(); c2.join();

    // Convert consumed to set
    std::set<int> consumed_set(consumed_values.begin(), consumed_values.end());

    // Check for duplicates
    EXPECT_EQ(consumed_values.size(), consumed_set.size())
        << "Duplicates detected";

    // Check for loss
    EXPECT_EQ(expected_values, consumed_set)
        << "Some values were lost";
}
```

### 4. Livelock / Starvation

**What it is:** Threads keep retrying but never make progress

**How to test:**
```cpp
TEST_F(QueueTest, NoLivelock) {
    Queue<int> q(mem, "test", 100);

    std::atomic<bool> timeout{false};

    auto producer = [&]() {
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 1000; i++) {
            while (!q.push(i)) {
                std::this_thread::yield();

                auto elapsed = std::chrono::steady_clock::now() - start;
                if (elapsed > 5s) {
                    timeout.store(true);
                    return;
                }
            }
        }
    };

    std::thread p(producer);
    p.join();

    EXPECT_FALSE(timeout.load())
        << "Producer timed out - possible livelock";
}
```

---

## Testing Anti-Patterns to Avoid

### ❌ Testing Implementation Details

```cpp
// BAD: Testing internal state
TEST_F(QueueTest, InternalIndexValues) {
    Queue<int> q(mem, "test", 10);
    q.push(42);
    EXPECT_EQ(q._head_index, 0);  // Implementation detail!
    EXPECT_EQ(q._tail_index, 1);
}
```

**Why bad:** Breaks when you refactor, even if behavior is correct.

**Better:**
```cpp
TEST_F(QueueTest, SizeAfterOperations) {
    Queue<int> q(mem, "test", 10);
    EXPECT_EQ(q.size(), 0);
    q.push(42);
    EXPECT_EQ(q.size(), 1);
    q.pop();
    EXPECT_EQ(q.size(), 0);
}
```

### ❌ Timing-Dependent Tests

```cpp
// BAD: Assumes specific thread scheduling
TEST_F(QueueTest, ThreadsRunInOrder) {
    std::atomic<int> counter{0};

    std::thread t1([&]() {
        counter.store(1);
    });

    std::this_thread::sleep_for(10ms);  // Assume t1 runs first

    EXPECT_EQ(counter.load(), 1);  // FLAKY!
    t1.join();
}
```

**Why bad:** Nondeterministic - fails randomly based on OS scheduler.

**Better:**
```cpp
TEST_F(QueueTest, ThreadEventuallyCompletes) {
    std::atomic<bool> completed{false};

    std::thread t1([&]() {
        completed.store(true);
    });

    t1.join();  // Synchronize properly
    EXPECT_TRUE(completed.load());
}
```

### ❌ Testing Exact CAS Failure Counts

```cpp
// BAD: CAS failures are nondeterministic
TEST_F(QueueTest, CASFailureRate) {
    int cas_failures = run_concurrent_test();
    EXPECT_EQ(cas_failures, 42);  // Will never be stable
}
```

**Why bad:** Number of CAS failures depends on scheduling, CPU load, etc.

**Better:** Test invariants, not implementation metrics.

---

## Test Organization Strategies

### Strategy 1: Separate Fast/Slow Tests

```cpp
// Fast tests - deterministic, <100ms
class QueueTest : public ::testing::Test { };

TEST_F(QueueTest, BasicPushPop) { ... }
TEST_F(QueueTest, EmptyBehavior) { ... }

// Slow tests - concurrent, >1s
class QueueStressTest : public ::testing::Test { };

TEST_F(QueueStressTest, HighContention) { ... }
TEST_F(QueueStressTest, SustainedLoad) { ... }
```

### Strategy 2: Parameterized Thread Counts

```cpp
class QueueConcurrentTest : public ::testing::TestWithParam<int> {
    // Param = number of threads
};

TEST_P(QueueConcurrentTest, MPMCCorrectness) {
    int threads = GetParam();
    // ... test with 'threads' threads
}

INSTANTIATE_TEST_SUITE_P(
    ThreadCount,
    QueueConcurrentTest,
    ::testing::Values(2, 4, 8, 16)  // Test different concurrency levels
);
```

### Strategy 3: Separate Test Executables

```
tests/
├── test_queue_unit.cpp        # Fast, deterministic
├── test_queue_concurrent.cpp  # Medium, multi-threaded
└── test_queue_stress.cpp      # Slow, exhaustive
```

CMake:
```cmake
add_executable(test_queue_unit tests/test_queue_unit.cpp)
set_tests_properties(test_queue_unit PROPERTIES LABELS "fast;unit")

add_executable(test_queue_stress tests/test_queue_stress.cpp)
set_tests_properties(test_queue_stress PROPERTIES
    LABELS "stress"
    DISABLED TRUE)
```

---

## Coverage Metrics for Lock-Free Code

### Traditional Code Coverage (Necessary but Insufficient)

```bash
# Line coverage
lcov --capture --directory build --output-file coverage.info

# Branch coverage
gcov -b queue.cpp
```

**Target:** 85%+ line coverage on core algorithms

**But:** 100% line coverage doesn't catch race conditions!

### Concurrency Coverage (Essential)

**ThreadSanitizer:**
```bash
cmake -DCMAKE_CXX_FLAGS="-fsanitize=thread -g -O1" -B build .
cd build && ctest
```

Detects:
- Data races
- Missing synchronization
- Lock order violations

**Helgrind (Valgrind):**
```bash
valgrind --tool=helgrind ./test_queue
```

Detects:
- Race conditions
- Incorrect lock usage
- Memory ordering issues

**Stress Testing Duration:**
Run tests for extended periods:
```bash
# Run for 1 hour
timeout 3600 ./test_queue_stress --gtest_repeat=10000
```

### Multi-Dimensional Coverage Checklist

- [ ] **Line coverage:** 85%+ on core code
- [ ] **Branch coverage:** 80%+ on error paths
- [ ] **Thread interleaving:** Tested with 2, 4, 8, 16+ threads
- [ ] **Load patterns:** Tested with equal/unequal producer/consumer ratios
- [ ] **Queue sizes:** Tested with small (10), medium (1000), large (100K) capacities
- [ ] **Data types:** Tested with int, struct, large objects
- [ ] **Platform coverage:** Tested on Linux, macOS (if applicable)
- [ ] **Sanitizer runs:** All tests pass under ThreadSanitizer
- [ ] **Extended duration:** Stress tests run for 1+ hours without failures

---

## TDD Workflow for Lock-Free Algorithms

### Step 1: Design the Interface (Red)

```cpp
// Write test first - interface doesn't exist yet
TEST_F(QueueTest, BasicInterface) {
    Queue<int> q(mem, "test", 100);

    EXPECT_TRUE(q.empty());
    EXPECT_FALSE(q.full());

    q.push(42);
    EXPECT_FALSE(q.empty());

    auto val = q.pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 42);
}
```

### Step 2: Implement Single-Threaded Version (Green)

```cpp
// Simplest possible implementation
template<typename T>
class Queue {
    T* data;
    size_t head = 0, tail = 0;

public:
    bool push(const T& value) {
        if (full()) return false;
        data[tail] = value;
        tail = (tail + 1) % capacity;
        return true;
    }

    std::optional<T> pop() {
        if (empty()) return std::nullopt;
        T value = data[head];
        head = (head + 1) % capacity;
        return value;
    }
};
```

**Test passes!** ✅

### Step 3: Add Concurrent Test (Red)

```cpp
TEST_F(QueueTest, BasicConcurrency) {
    Queue<int> q(mem, "test", 100);

    std::thread producer([&]() {
        for (int i = 0; i < 50; i++) {
            q.push(i);
        }
    });

    std::thread consumer([&]() {
        for (int i = 0; i < 50; i++) {
            while (!q.pop().has_value()) {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    EXPECT_TRUE(q.empty());
}
```

**Test fails!** ❌ Data race detected

### Step 4: Make Thread-Safe (Green)

```cpp
template<typename T>
class Queue {
    T* data;
    std::atomic<size_t> head{0}, tail{0};  // Now atomic

public:
    bool push(const T& value) {
        size_t current_tail = tail.load(std::memory_order_relaxed);
        size_t next_tail = (current_tail + 1) % capacity;

        if (next_tail == head.load(std::memory_order_acquire)) {
            return false;  // Full
        }

        data[current_tail] = value;
        std::atomic_thread_fence(std::memory_order_release);
        tail.store(next_tail, std::memory_order_relaxed);
        return true;
    }
    // ... similar for pop()
};
```

**Test passes!** ✅ (but not lock-free yet)

### Step 5: Refactor to Lock-Free (Refactor)

```cpp
bool push(const T& value) {
    size_t current_tail, next_tail;

    do {
        current_tail = tail.load(std::memory_order_relaxed);
        next_tail = (current_tail + 1) % capacity;

        if (next_tail == head.load(std::memory_order_acquire)) {
            return false;  // Full
        }
    } while (!tail.compare_exchange_weak(
        current_tail, next_tail,
        std::memory_order_release,
        std::memory_order_relaxed
    ));

    data[current_tail] = value;
    return true;
}
```

**Tests still pass!** ✅ Now lock-free with CAS

### Step 6: Add Stress Tests (Red → Green)

Add progressively harder tests:
1. MPMC with 4 threads
2. MPMC with 16 threads
3. High contention (small queue, many threads)
4. Sustained load (millions of ops)
5. ABA detection

Fix bugs as they appear, keeping all previous tests green.

---

## Practical Tips

### 1. Use Assertions Liberally

```cpp
auto val = q.pop();
ASSERT_TRUE(val.has_value()) << "Pop failed when queue should have data";
EXPECT_EQ(*val, expected_value) << "Got " << *val << " expected " << expected_value;
```

Better error messages = faster debugging.

### 2. Isolate Tests

```cpp
void SetUp() override {
    shm_name_ = "/test_" + std::to_string(getpid()) + "_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}
```

Prevent tests from interfering with each other.

### 3. Test Cleanup

```cpp
void TearDown() override {
    Memory::unlink(shm_name_);
}
```

Don't leave shared memory segments around.

### 4. Use Timeouts

```cpp
set_tests_properties(queue_stress PROPERTIES TIMEOUT 300)
```

Prevent infinite loops from hanging CI.

### 5. Run Tests in Parallel

```bash
ctest -j$(nproc)
```

But be careful - concurrent tests might interfere if they share resources.

---

## Summary

**Lock-free testing is fundamentally different from traditional testing:**

1. **Start simple** - Verify single-threaded correctness first
2. **Test invariants** - Not implementation details
3. **Use multiple thread counts** - Race conditions vary with concurrency
4. **Maximize contention** - Small queues + many threads
5. **Run for duration** - Some bugs only appear after millions of operations
6. **Use sanitizers** - ThreadSanitizer catches what coverage misses
7. **Separate fast/slow tests** - Keep CI fast, run stress tests nightly
8. **Expect nondeterminism** - Test properties, not exact timings
9. **Keep tests green** - Flaky tests are worse than no tests

**Remember:** Lock-free code is hard. Tests won't catch everything. But a comprehensive test suite gives you confidence to refactor and evolve your design.
