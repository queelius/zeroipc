#include <gtest/gtest.h>
#include <zeroipc/memory.h>
#include <zeroipc/semaphore.h>
#include <thread>
#include <vector>
#include <chrono>
#include <unistd.h>
#include "test_config.h"

using namespace zeroipc;
using namespace zeroipc::test;
using namespace std::chrono_literals;

class SemaphoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        shm_name_ = "/test_semaphore_" + std::to_string(getpid());
    }

    void TearDown() override {
        Memory::unlink(shm_name_);
    }

    std::string shm_name_;
};

// ============================================================================
// FAST TESTS - Core functionality, no time delays
// ============================================================================

TEST_F(SemaphoreTest, CreateSemaphore) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "test_sem", 5);

    EXPECT_EQ(sem.count(), 5);
    EXPECT_EQ(sem.waiting(), 0);
    EXPECT_EQ(sem.max_count(), 0);
    EXPECT_EQ(sem.name(), "test_sem");
}

TEST_F(SemaphoreTest, CreateBinarySemaphore) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "mutex", 1, 1);

    EXPECT_EQ(sem.count(), 1);
    EXPECT_EQ(sem.max_count(), 1);
}

TEST_F(SemaphoreTest, OpenExistingSemaphore) {
    Memory mem(shm_name_, 1024 * 1024);

    {
        Semaphore sem(mem, "existing", 3, 10);
        EXPECT_EQ(sem.count(), 3);
    }

    Semaphore sem2(mem, "existing");
    EXPECT_EQ(sem2.count(), 3);
    EXPECT_EQ(sem2.max_count(), 10);
}

TEST_F(SemaphoreTest, AcquireRelease) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "test", 3);

    sem.acquire();
    EXPECT_EQ(sem.count(), 2);

    sem.acquire();
    EXPECT_EQ(sem.count(), 1);

    sem.release();
    EXPECT_EQ(sem.count(), 2);

    sem.release();
    EXPECT_EQ(sem.count(), 3);
}

TEST_F(SemaphoreTest, TryAcquireSuccess) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "test", 2);

    EXPECT_TRUE(sem.try_acquire());
    EXPECT_EQ(sem.count(), 1);

    EXPECT_TRUE(sem.try_acquire());
    EXPECT_EQ(sem.count(), 0);
}

TEST_F(SemaphoreTest, TryAcquireFailure) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "test", 0);

    EXPECT_FALSE(sem.try_acquire());
    EXPECT_EQ(sem.count(), 0);
}

TEST_F(SemaphoreTest, MaxCountEnforced) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "test", 5, 5);

    EXPECT_THROW(sem.release(), std::overflow_error);
    EXPECT_EQ(sem.count(), 5);
}

TEST_F(SemaphoreTest, InvalidArguments) {
    Memory mem(shm_name_, 1024 * 1024);

    EXPECT_THROW(Semaphore(mem, "test", -1), std::invalid_argument);
    EXPECT_THROW(Semaphore(mem, "test", 5, -1), std::invalid_argument);
    EXPECT_THROW(Semaphore(mem, "test", 10, 5), std::invalid_argument);
}

TEST_F(SemaphoreTest, SemaphoreGuard) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "mutex", 1, 1);

    {
        SemaphoreGuard guard(sem);
        EXPECT_EQ(sem.count(), 0);
    }

    EXPECT_EQ(sem.count(), 1);
}

TEST_F(SemaphoreTest, SemaphoreGuardException) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "mutex", 1, 1);

    try {
        SemaphoreGuard guard(sem);
        EXPECT_EQ(sem.count(), 0);
        throw std::runtime_error("test exception");
    } catch (...) {
    }

    EXPECT_EQ(sem.count(), 1);
}

// ============================================================================
// MEDIUM TESTS - Multi-threaded with minimal delays
// ============================================================================

TEST_F(SemaphoreTest, MutualExclusion) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "mutex", 1, 1);

    int shared_counter = 0;
    const int iterations = TestTiming::FAST_ITERATIONS;
    const int num_threads = TestTiming::FAST_THREADS;

    auto worker = [&]() {
        for (int i = 0; i < iterations; i++) {
            sem.acquire();
            // Critical section - use minimal delay to force context switch
            int temp = shared_counter;
            std::this_thread::yield();  // Cooperative yield instead of sleep
            shared_counter = temp + 1;
            sem.release();
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(worker);
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(shared_counter, iterations * num_threads);
}

TEST_F(SemaphoreTest, ResourcePoolLimiting) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "pool", 3);

    std::atomic<int> concurrent_users{0};
    std::atomic<int> max_concurrent{0};

    auto worker = [&]() {
        sem.acquire();

        int current = ++concurrent_users;

        int expected_max = max_concurrent.load();
        while (current > expected_max &&
               !max_concurrent.compare_exchange_weak(expected_max, current)) {
        }

        // Minimal delay - just enough to ensure overlap
        std::this_thread::sleep_for(TestTiming::CRITICAL_SECTION_DELAY);

        --concurrent_users;
        sem.release();
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 10; i++) {
        threads.emplace_back(worker);
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_LE(max_concurrent.load(), 3);
}

TEST_F(SemaphoreTest, ProducerConsumer) {
    Memory mem(shm_name_, 1024 * 1024);

    Semaphore items(mem, "items", 0);
    Semaphore slots(mem, "slots", 5);

    std::vector<int> buffer;
    std::mutex buffer_mutex;

    const int items_per_producer = 10;

    auto producer = [&](int id) {
        for (int i = 0; i < items_per_producer; i++) {
            slots.acquire();
            {
                std::lock_guard<std::mutex> lock(buffer_mutex);
                buffer.push_back(id * 100 + i);
            }
            items.release();
        }
    };

    auto consumer = [&]() {
        for (int i = 0; i < items_per_producer; i++) {
            items.acquire();
            {
                std::lock_guard<std::mutex> lock(buffer_mutex);
                EXPECT_FALSE(buffer.empty());
                buffer.pop_back();
            }
            slots.release();
        }
    };

    std::thread p1(producer, 1);
    std::thread p2(producer, 2);
    std::thread c(consumer);

    p1.join();
    p2.join();
    c.join();

    EXPECT_EQ(buffer.size(), 10);
}

TEST_F(SemaphoreTest, UnboundedSemaphore) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "unbounded", 0, 0);

    for (int i = 0; i < 100; i++) {
        sem.release();
    }

    EXPECT_EQ(sem.count(), 100);

    for (int i = 0; i < 100; i++) {
        EXPECT_TRUE(sem.try_acquire());
    }

    EXPECT_EQ(sem.count(), 0);
}

// ============================================================================
// TIMEOUT TESTS - Only test timeout mechanism, not long waits
// ============================================================================

TEST_F(SemaphoreTest, AcquireTimeoutFast) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "test", 0);

    // Use short timeout for fast test
    auto start = std::chrono::steady_clock::now();
    bool result = sem.acquire_for(TestTiming::SHORT_TIMEOUT);
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(result);
    EXPECT_GE(elapsed, TestTiming::SHORT_TIMEOUT);
    // Allow some tolerance for scheduling
    EXPECT_LT(elapsed, TestTiming::SHORT_TIMEOUT * 2);
}

TEST_F(SemaphoreTest, AcquireTimeoutSuccess) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "test", 1);

    bool result = sem.acquire_for(TestTiming::SHORT_TIMEOUT);
    EXPECT_TRUE(result);
    EXPECT_EQ(sem.count(), 0);
}

// ============================================================================
// SLOW TESTS - Only when explicitly requested
// ============================================================================

// These tests should be disabled by default and run only in SLOW or STRESS mode
class SemaphoreSlowTest : public SemaphoreTest {};

TEST_F(SemaphoreSlowTest, DISABLED_MultipleWaiters) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "test", 0);

    std::atomic<int> acquired{0};

    auto waiter = [&]() {
        sem.acquire();
        acquired++;
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 5; i++) {
        threads.emplace_back(waiter);
    }

    // Minimal wait for threads to start
    std::this_thread::sleep_for(TestTiming::THREAD_START_DELAY * TestTiming::ci_multiplier());
    EXPECT_EQ(sem.waiting(), 5);

    for (int i = 0; i < 5; i++) {
        sem.release();
        std::this_thread::sleep_for(TestTiming::THREAD_SYNC_DELAY);
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(acquired.load(), 5);
    EXPECT_EQ(sem.waiting(), 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // Print test mode for debugging
    std::cout << "Test mode: " << TestTiming::test_mode() << std::endl;
    std::cout << "CI mode: " << (TestTiming::is_ci() ? "yes" : "no") << std::endl;

    return RUN_ALL_TESTS();
}
