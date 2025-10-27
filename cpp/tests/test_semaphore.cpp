#include <gtest/gtest.h>
#include <zeroipc/memory.h>
#include <zeroipc/semaphore.h>
#include <thread>
#include <vector>
#include <chrono>
#include <unistd.h>
#include "test_config.h"

using namespace zeroipc;
using namespace std::chrono_literals;
using namespace zeroipc::test;

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

// Basic tests

TEST_F(SemaphoreTest, CreateSemaphore) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "test_sem", 5);  // initial count = 5

    EXPECT_EQ(sem.count(), 5);
    EXPECT_EQ(sem.waiting(), 0);
    EXPECT_EQ(sem.max_count(), 0);  // unbounded
    EXPECT_EQ(sem.name(), "test_sem");
}

TEST_F(SemaphoreTest, CreateBinarySemaphore) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "mutex", 1, 1);  // binary semaphore (mutex)

    EXPECT_EQ(sem.count(), 1);
    EXPECT_EQ(sem.max_count(), 1);
}

TEST_F(SemaphoreTest, OpenExistingSemaphore) {
    Memory mem(shm_name_, 1024 * 1024);

    // Create semaphore
    {
        Semaphore sem(mem, "existing", 3, 10);
        EXPECT_EQ(sem.count(), 3);
    }

    // Open existing semaphore
    Semaphore sem2(mem, "existing");
    EXPECT_EQ(sem2.count(), 3);
    EXPECT_EQ(sem2.max_count(), 10);
}

TEST_F(SemaphoreTest, AcquireRelease) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "test", 3);

    // Acquire decrements count
    sem.acquire();
    EXPECT_EQ(sem.count(), 2);

    sem.acquire();
    EXPECT_EQ(sem.count(), 1);

    // Release increments count
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
    Semaphore sem(mem, "test", 0);  // count = 0

    EXPECT_FALSE(sem.try_acquire());
    EXPECT_EQ(sem.count(), 0);
}

TEST_F(SemaphoreTest, AcquireTimeout) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "test", 0);

    // Should timeout
    auto start = std::chrono::steady_clock::now();
    bool result = sem.acquire_for(TestTiming::MEDIUM_TIMEOUT);
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(result);
    EXPECT_GE(elapsed, TestTiming::MEDIUM_TIMEOUT);
}

TEST_F(SemaphoreTest, AcquireTimeoutSuccess) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "test", 1);

    // Should succeed immediately
    bool result = sem.acquire_for(TestTiming::MEDIUM_TIMEOUT);
    EXPECT_TRUE(result);
    EXPECT_EQ(sem.count(), 0);
}

TEST_F(SemaphoreTest, MaxCountEnforced) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "test", 5, 5);  // max = 5

    // Try to exceed max count
    EXPECT_THROW(sem.release(), std::overflow_error);
    EXPECT_EQ(sem.count(), 5);
}

TEST_F(SemaphoreTest, InvalidArguments) {
    Memory mem(shm_name_, 1024 * 1024);

    // Negative initial count
    EXPECT_THROW(Semaphore(mem, "test", -1), std::invalid_argument);

    // Negative max count
    EXPECT_THROW(Semaphore(mem, "test", 5, -1), std::invalid_argument);

    // Initial > max
    EXPECT_THROW(Semaphore(mem, "test", 10, 5), std::invalid_argument);
}

// Thread concurrency tests

TEST_F(SemaphoreTest, MutualExclusion) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "mutex", 1, 1);  // binary semaphore

    int shared_counter = 0;
    const int iterations = 100;

    auto worker = [&]() {
        for (int i = 0; i < iterations; i++) {
            sem.acquire();
            // Critical section
            int temp = shared_counter;
            std::this_thread::sleep_for(TestTiming::CRITICAL_SECTION_DELAY);  // Force context switch
            shared_counter = temp + 1;
            sem.release();
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; i++) {
        threads.emplace_back(worker);
    }

    for (auto& t : threads) {
        t.join();
    }

    // All increments should be counted
    EXPECT_EQ(shared_counter, iterations * 4);
}

TEST_F(SemaphoreTest, ResourcePoolLimiting) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "pool", 3);  // max 3 concurrent users

    std::atomic<int> concurrent_users{0};
    std::atomic<int> max_concurrent{0};

    auto worker = [&]() {
        sem.acquire();

        // Inside critical region
        int current = ++concurrent_users;

        // Update max
        int expected_max = max_concurrent.load();
        while (current > expected_max &&
               !max_concurrent.compare_exchange_weak(expected_max, current)) {
        }

        std::this_thread::sleep_for(TestTiming::THREAD_SYNC_DELAY);

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

    // Never exceeded the limit
    EXPECT_LE(max_concurrent.load(), 3);
}

TEST_F(SemaphoreTest, ProducerConsumer) {
    Memory mem(shm_name_, 1024 * 1024);

    Semaphore items(mem, "items", 0);  // available items
    Semaphore slots(mem, "slots", 5);  // available slots

    std::vector<int> buffer;
    std::mutex buffer_mutex;

    auto producer = [&](int id) {
        for (int i = 0; i < 10; i++) {
            slots.acquire();  // Wait for slot
            {
                std::lock_guard<std::mutex> lock(buffer_mutex);
                buffer.push_back(id * 100 + i);
            }
            items.release();  // Signal item available
        }
    };

    auto consumer = [&]() {
        for (int i = 0; i < 20; i++) {  // Consume all 20 items (2 producers * 10)
            items.acquire();  // Wait for item
            {
                std::lock_guard<std::mutex> lock(buffer_mutex);
                EXPECT_FALSE(buffer.empty());
                buffer.pop_back();
            }
            slots.release();  // Signal slot available
        }
    };

    std::thread p1(producer, 1);
    std::thread p2(producer, 2);
    std::thread c(consumer);

    p1.join();
    p2.join();
    c.join();

    EXPECT_EQ(buffer.size(), 0);  // All items consumed
}

TEST_F(SemaphoreTest, SemaphoreGuard) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "mutex", 1, 1);

    {
        SemaphoreGuard guard(sem);
        EXPECT_EQ(sem.count(), 0);  // Acquired
        // Automatic release on scope exit
    }

    EXPECT_EQ(sem.count(), 1);  // Released
}

TEST_F(SemaphoreTest, SemaphoreGuardException) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "mutex", 1, 1);

    try {
        SemaphoreGuard guard(sem);
        EXPECT_EQ(sem.count(), 0);
        throw std::runtime_error("test exception");
    } catch (...) {
        // Guard should have released
    }

    EXPECT_EQ(sem.count(), 1);  // Released despite exception
}

TEST_F(SemaphoreTest, MultipleWaiters) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "test", 0);

    std::atomic<int> acquired{0};

    auto waiter = [&]() {
        sem.acquire();
        acquired++;
    };

    // Start 5 waiting threads
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; i++) {
        threads.emplace_back(waiter);
    }

    // Give threads time to start waiting
    std::this_thread::sleep_for(TestTiming::MEDIUM_TIMEOUT);
    EXPECT_EQ(sem.waiting(), 5);

    // Release permits one by one
    for (int i = 0; i < 5; i++) {
        sem.release();
        std::this_thread::sleep_for(TestTiming::SHORT_TIMEOUT);  // Let waiter wake up
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(acquired.load(), 5);
    EXPECT_EQ(sem.waiting(), 0);
}

TEST_F(SemaphoreTest, UnboundedSemaphore) {
    Memory mem(shm_name_, 1024 * 1024);
    Semaphore sem(mem, "unbounded", 0, 0);  // unbounded

    // Can release many times
    for (int i = 0; i < 100; i++) {
        sem.release();
    }

    EXPECT_EQ(sem.count(), 100);

    // Can acquire all
    for (int i = 0; i < 100; i++) {
        EXPECT_TRUE(sem.try_acquire());
    }

    EXPECT_EQ(sem.count(), 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
