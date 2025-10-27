#include <gtest/gtest.h>
#include <zeroipc/memory.h>
#include <zeroipc/barrier.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <unistd.h>
#include "test_config.h"

using namespace zeroipc;
using namespace zeroipc::test;

class BarrierTest : public ::testing::Test {
protected:
    std::string shm_name;

    void SetUp() override {
        shm_name = "/test_barrier_" + std::to_string(getpid()) + "_" +
                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    }

    void TearDown() override {
        Memory::unlink(shm_name);
    }
};

// ============================================================================
// Basic Functionality Tests
// ============================================================================

TEST_F(BarrierTest, CreateBarrier) {
    Memory mem(shm_name, 1024*1024);
    Barrier barrier(mem, "test", 4);

    EXPECT_EQ(barrier.num_participants(), 4);
    EXPECT_EQ(barrier.arrived(), 0);
    EXPECT_EQ(barrier.generation(), 0);
    EXPECT_EQ(barrier.name(), "test");
}

TEST_F(BarrierTest, OpenExistingBarrier) {
    Memory mem(shm_name, 1024*1024);

    // Create barrier
    {
        Barrier barrier1(mem, "existing", 5);
        EXPECT_EQ(barrier1.num_participants(), 5);
    }

    // Open existing
    Barrier barrier2(mem, "existing");
    EXPECT_EQ(barrier2.num_participants(), 5);
    EXPECT_EQ(barrier2.arrived(), 0);
}

TEST_F(BarrierTest, InvalidNumParticipants) {
    Memory mem(shm_name, 1024*1024);

    EXPECT_THROW({
        Barrier barrier(mem, "test", 0);
    }, std::invalid_argument);

    EXPECT_THROW({
        Barrier barrier(mem, "test", -1);
    }, std::invalid_argument);
}

TEST_F(BarrierTest, NotFound) {
    Memory mem(shm_name, 1024*1024);

    EXPECT_THROW({
        Barrier barrier(mem, "nonexistent");
    }, std::runtime_error);
}

// ============================================================================
// Synchronization Tests
// ============================================================================

TEST_F(BarrierTest, TwoThreadBarrier) {
    Memory mem(shm_name, 1024*1024);
    Barrier barrier(mem, "sync", 2);

    std::atomic<int> phase{0};

    auto worker = [&]() {
        // Phase 1
        EXPECT_EQ(phase.load(), 0);
        barrier.wait();

        // Both threads should reach here together
        phase.fetch_add(1);
        barrier.wait();

        // Phase 2
        EXPECT_EQ(phase.load(), 2);
    };

    std::thread t1(worker);
    std::thread t2(worker);

    t1.join();
    t2.join();

    EXPECT_EQ(phase.load(), 2);
    EXPECT_EQ(barrier.arrived(), 0);  // Reset after release
    EXPECT_EQ(barrier.generation(), 2);  // Two passes
}

TEST_F(BarrierTest, MultipleThreadsBarrier) {
    Memory mem(shm_name, 1024*1024);
    const int num_threads = 8;
    Barrier barrier(mem, "sync", num_threads);

    std::atomic<int> counter{0};
    std::vector<int> thread_ids(num_threads);

    auto worker = [&](int id) {
        // All threads increment counter
        counter.fetch_add(1);

        // Wait at barrier
        barrier.wait();

        // All threads should see final count
        EXPECT_EQ(counter.load(), num_threads);
        thread_ids[id] = 1;
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    // All threads completed
    for (int i = 0; i < num_threads; ++i) {
        EXPECT_EQ(thread_ids[i], 1);
    }
}

TEST_F(BarrierTest, BarrierReusability) {
    Memory mem(shm_name, 1024*1024);
    const int num_threads = 4;
    const int num_iterations = 10;
    Barrier barrier(mem, "reusable", num_threads);

    std::atomic<int> phase_counter{0};

    auto worker = [&]() {
        for (int i = 0; i < num_iterations; ++i) {
            phase_counter.fetch_add(1);
            barrier.wait();

            // All threads should see same phase count
            EXPECT_EQ(phase_counter.load(), num_threads * (i + 1));

            barrier.wait();  // Second barrier for verification
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker);
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(phase_counter.load(), num_threads * num_iterations);
    EXPECT_EQ(barrier.generation(), num_iterations * 2);
}

TEST_F(BarrierTest, GenerationCounter) {
    Memory mem(shm_name, 1024*1024);
    Barrier barrier(mem, "gen_test", 3);

    EXPECT_EQ(barrier.generation(), 0);

    std::atomic<int> gen_after_wait{-1};

    auto worker = [&]() {
        barrier.wait();
        gen_after_wait.store(barrier.generation());
    };

    std::thread t1(worker);
    std::thread t2(worker);
    std::thread t3(worker);

    t1.join();
    t2.join();
    t3.join();

    // Generation should increment after all threads pass
    EXPECT_EQ(barrier.generation(), 1);
    EXPECT_EQ(gen_after_wait.load(), 1);
}

TEST_F(BarrierTest, ArrivedCounter) {
    Memory mem(shm_name, 1024*1024);
    Barrier barrier(mem, "arrived_test", 3);

    EXPECT_EQ(barrier.arrived(), 0);

    std::atomic<bool> thread1_waiting{false};
    std::atomic<bool> thread2_waiting{false};

    auto worker1 = [&]() {
        thread1_waiting.store(true);
        barrier.wait();
    };

    auto worker2 = [&]() {
        thread2_waiting.store(true);
        barrier.wait();
    };

    std::thread t1(worker1);
    std::thread t2(worker2);

    // Wait for threads to start waiting
    while (!thread1_waiting.load() || !thread2_waiting.load()) {
        std::this_thread::sleep_for(TestTiming::THREAD_START_DELAY);
    }

    // Give threads time to arrive at barrier
    std::this_thread::sleep_for(TestTiming::THREAD_SYNC_DELAY);

    // Two threads should be waiting
    EXPECT_EQ(barrier.arrived(), 2);

    // Release by having main thread arrive
    barrier.wait();

    t1.join();
    t2.join();

    // After release, arrived should reset to 0
    EXPECT_EQ(barrier.arrived(), 0);
}

// ============================================================================
// Timeout Tests
// ============================================================================

TEST_F(BarrierTest, WaitForSuccess) {
    Memory mem(shm_name, 1024*1024);
    Barrier barrier(mem, "timeout_test", 2);

    auto worker = [&]() {
        std::this_thread::sleep_for(TestTiming::SHORT_TIMEOUT);
        barrier.wait();
    };

    std::thread t(worker);

    // Should succeed before timeout
    bool result = barrier.wait_for(TestTiming::LONG_TIMEOUT);
    EXPECT_TRUE(result);

    t.join();
}

TEST_F(BarrierTest, WaitForTimeout) {
    Memory mem(shm_name, 1024*1024);
    Barrier barrier(mem, "timeout_test", 3);  // Need 3, but only 1 arrives

    auto start = std::chrono::steady_clock::now();
    bool result = barrier.wait_for(TestTiming::MEDIUM_TIMEOUT);
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(result);  // Should timeout
    EXPECT_GE(elapsed, TestTiming::MEDIUM_TIMEOUT);
}

TEST_F(BarrierTest, WaitForWithMultipleThreads) {
    Memory mem(shm_name, 1024*1024);
    Barrier barrier(mem, "timeout_multi", 4);

    std::atomic<int> success_count{0};
    std::atomic<int> timeout_count{0};

    auto worker = [&](std::chrono::milliseconds delay) {
        std::this_thread::sleep_for(delay);
        bool result = barrier.wait_for(TestTiming::MEDIUM_TIMEOUT);
        if (result) {
            success_count.fetch_add(1);
        } else {
            timeout_count.fetch_add(1);
        }
    };

    // All arrive within timeout - using minimal delays
    std::thread t1([&]() { worker(TestTiming::THREAD_START_DELAY); });
    std::thread t2([&]() { worker(TestTiming::THREAD_START_DELAY * 2); });
    std::thread t3([&]() { worker(TestTiming::THREAD_SYNC_DELAY); });
    std::thread t4([&]() { worker(TestTiming::THREAD_SYNC_DELAY * 2); });

    t1.join();
    t2.join();
    t3.join();
    t4.join();

    // All should succeed
    EXPECT_EQ(success_count.load(), 4);
    EXPECT_EQ(timeout_count.load(), 0);
}

// ============================================================================
// Phase-Based Parallel Algorithm Test
// ============================================================================

TEST_F(BarrierTest, ParallelPhaseBasedAlgorithm) {
    Memory mem(shm_name, 1024*1024);
    const int num_workers = 4;
    const int array_size = 100;
    Barrier barrier(mem, "phase_sync", num_workers);

    // Shared data
    std::vector<int> data(array_size, 0);
    std::atomic<int> phase{0};

    auto worker = [&](int worker_id) {
        int start = worker_id * (array_size / num_workers);
        int end = start + (array_size / num_workers);

        // Phase 1: Each worker initializes its section
        for (int i = start; i < end; ++i) {
            data[i] = i;
        }

        barrier.wait();  // All workers must complete phase 1

        // Phase 2: Each worker doubles values in its section
        for (int i = start; i < end; ++i) {
            data[i] *= 2;
        }

        barrier.wait();  // All workers must complete phase 2

        // Phase 3: Each worker verifies the entire array
        for (int i = 0; i < array_size; ++i) {
            EXPECT_EQ(data[i], i * 2);
        }

        barrier.wait();  // All workers must complete verification
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_workers; ++i) {
        threads.emplace_back(worker, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    // Verify final state
    for (int i = 0; i < array_size; ++i) {
        EXPECT_EQ(data[i], i * 2);
    }

    EXPECT_EQ(barrier.generation(), 3);  // Three barrier passes
}

// ============================================================================
// Stress Test
// ============================================================================

TEST_F(BarrierTest, StressTestManyIterations) {
    Memory mem(shm_name, 1024*1024);
    const int num_threads = 8;
    const int iterations = 100;
    Barrier barrier(mem, "stress", num_threads);

    std::atomic<int> total_passes{0};

    auto worker = [&]() {
        for (int i = 0; i < iterations; ++i) {
            barrier.wait();
            total_passes.fetch_add(1);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker);
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(total_passes.load(), num_threads * iterations);
    EXPECT_EQ(barrier.generation(), iterations);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(BarrierTest, SingleParticipant) {
    Memory mem(shm_name, 1024*1024);
    Barrier barrier(mem, "single", 1);

    // Should pass through immediately
    barrier.wait();
    EXPECT_EQ(barrier.generation(), 1);

    barrier.wait();
    EXPECT_EQ(barrier.generation(), 2);
}

TEST_F(BarrierTest, LargeNumberOfParticipants) {
    Memory mem(shm_name, 1024*1024);
    const int num_threads = 50;
    Barrier barrier(mem, "large", num_threads);

    std::atomic<int> counter{0};

    auto worker = [&]() {
        counter.fetch_add(1);
        barrier.wait();
        EXPECT_EQ(counter.load(), num_threads);
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker);
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(counter.load(), num_threads);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
