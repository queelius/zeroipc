#include <gtest/gtest.h>
#include <zeroipc/memory.h>
#include <zeroipc/latch.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <unistd.h>

using namespace zeroipc;

class LatchTest : public ::testing::Test {
protected:
    std::string shm_name;

    void SetUp() override {
        shm_name = "/test_latch_" + std::to_string(getpid()) + "_" +
                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    }

    void TearDown() override {
        Memory::unlink(shm_name);
    }
};

// ============================================================================
// Basic Functionality Tests
// ============================================================================

TEST_F(LatchTest, CreateLatch) {
    Memory mem(shm_name, 1024*1024);
    Latch latch(mem, "test", 5);

    EXPECT_EQ(latch.count(), 5);
    EXPECT_EQ(latch.initial_count(), 5);
    EXPECT_EQ(latch.name(), "test");
    EXPECT_FALSE(latch.try_wait());
}

TEST_F(LatchTest, CreateLatchWithZero) {
    Memory mem(shm_name, 1024*1024);
    Latch latch(mem, "zero", 0);

    EXPECT_EQ(latch.count(), 0);
    EXPECT_EQ(latch.initial_count(), 0);
    EXPECT_TRUE(latch.try_wait());  // Already at 0
}

TEST_F(LatchTest, OpenExistingLatch) {
    Memory mem(shm_name, 1024*1024);

    // Create latch
    {
        Latch latch1(mem, "existing", 10);
        EXPECT_EQ(latch1.count(), 10);
    }

    // Open existing
    Latch latch2(mem, "existing");
    EXPECT_EQ(latch2.count(), 10);
    EXPECT_EQ(latch2.initial_count(), 10);
}

TEST_F(LatchTest, InvalidCount) {
    Memory mem(shm_name, 1024*1024);

    EXPECT_THROW({
        Latch latch(mem, "test", -1);
    }, std::invalid_argument);
}

TEST_F(LatchTest, NotFound) {
    Memory mem(shm_name, 1024*1024);

    EXPECT_THROW({
        Latch latch(mem, "nonexistent");
    }, std::runtime_error);
}

// ============================================================================
// Count Down Tests
// ============================================================================

TEST_F(LatchTest, CountDownByOne) {
    Memory mem(shm_name, 1024*1024);
    Latch latch(mem, "test", 3);

    EXPECT_EQ(latch.count(), 3);

    latch.count_down();
    EXPECT_EQ(latch.count(), 2);

    latch.count_down();
    EXPECT_EQ(latch.count(), 1);

    latch.count_down();
    EXPECT_EQ(latch.count(), 0);
}

TEST_F(LatchTest, CountDownByN) {
    Memory mem(shm_name, 1024*1024);
    Latch latch(mem, "test", 10);

    latch.count_down(3);
    EXPECT_EQ(latch.count(), 7);

    latch.count_down(5);
    EXPECT_EQ(latch.count(), 2);

    latch.count_down(2);
    EXPECT_EQ(latch.count(), 0);
}

TEST_F(LatchTest, CountDownSaturatesAtZero) {
    Memory mem(shm_name, 1024*1024);
    Latch latch(mem, "test", 5);

    latch.count_down(10);  // Count down more than current
    EXPECT_EQ(latch.count(), 0);

    latch.count_down();  // Count down when already at 0
    EXPECT_EQ(latch.count(), 0);
}

TEST_F(LatchTest, CountDownInvalidAmount) {
    Memory mem(shm_name, 1024*1024);
    Latch latch(mem, "test", 5);

    EXPECT_THROW({
        latch.count_down(0);
    }, std::invalid_argument);

    EXPECT_THROW({
        latch.count_down(-1);
    }, std::invalid_argument);
}

// ============================================================================
// Wait Tests
// ============================================================================

TEST_F(LatchTest, WaitWhenAlreadyZero) {
    Memory mem(shm_name, 1024*1024);
    Latch latch(mem, "test", 0);

    // Should return immediately
    auto start = std::chrono::steady_clock::now();
    latch.wait();
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_LT(elapsed, std::chrono::milliseconds(10));
}

TEST_F(LatchTest, TryWait) {
    Memory mem(shm_name, 1024*1024);
    Latch latch(mem, "test", 2);

    EXPECT_FALSE(latch.try_wait());

    latch.count_down();
    EXPECT_FALSE(latch.try_wait());

    latch.count_down();
    EXPECT_TRUE(latch.try_wait());
}

TEST_F(LatchTest, WaitReleasedByCountDown) {
    Memory mem(shm_name, 1024*1024);
    Latch latch(mem, "test", 1);

    std::atomic<bool> waiter_done{false};

    auto waiter = [&]() {
        latch.wait();
        waiter_done.store(true);
    };

    std::thread t(waiter);

    // Give thread time to start waiting
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(waiter_done.load());

    // Count down to release waiter
    latch.count_down();

    t.join();
    EXPECT_TRUE(waiter_done.load());
}

TEST_F(LatchTest, WaitForSuccess) {
    Memory mem(shm_name, 1024*1024);
    Latch latch(mem, "test", 1);

    auto countdown_thread = [&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        latch.count_down();
    };

    std::thread t(countdown_thread);

    // Should succeed before timeout
    bool result = latch.wait_for(std::chrono::milliseconds(200));
    EXPECT_TRUE(result);

    t.join();
}

TEST_F(LatchTest, WaitForTimeout) {
    Memory mem(shm_name, 1024*1024);
    Latch latch(mem, "test", 100);  // Never reaches 0

    auto start = std::chrono::steady_clock::now();
    bool result = latch.wait_for(std::chrono::milliseconds(100));
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(result);
    EXPECT_GE(elapsed, std::chrono::milliseconds(100));
}

// ============================================================================
// Concurrent Tests
// ============================================================================

TEST_F(LatchTest, MultipleThreadsCountDown) {
    Memory mem(shm_name, 1024*1024);
    const int num_threads = 10;
    Latch latch(mem, "test", num_threads);

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            latch.count_down();
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(latch.count(), 0);
    EXPECT_TRUE(latch.try_wait());
}

TEST_F(LatchTest, MultipleWaiters) {
    Memory mem(shm_name, 1024*1024);
    Latch latch(mem, "test", 3);

    std::atomic<int> waiters_released{0};

    auto waiter = [&]() {
        latch.wait();
        waiters_released.fetch_add(1);
    };

    // Start multiple waiters
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back(waiter);
    }

    // Give threads time to start waiting
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(waiters_released.load(), 0);

    // Count down to release all waiters
    latch.count_down(3);

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(waiters_released.load(), 5);
}

// ============================================================================
// Use Case: Start Gate
// ============================================================================

TEST_F(LatchTest, StartGatePattern) {
    Memory mem(shm_name, 1024*1024);
    Latch start_latch(mem, "start_gate", 1);

    std::atomic<int> workers_started{0};

    auto worker = [&]() {
        // Wait for coordinator to signal start
        start_latch.wait();
        workers_started.fetch_add(1);
    };

    // Start worker threads
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back(worker);
    }

    // Give threads time to reach latch
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(workers_started.load(), 0);

    // Release all workers
    start_latch.count_down();

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(workers_started.load(), 5);
}

// ============================================================================
// Use Case: Completion Detection
// ============================================================================

TEST_F(LatchTest, CompletionDetectionPattern) {
    Memory mem(shm_name, 1024*1024);
    const int num_workers = 8;
    Latch completion_latch(mem, "completion", num_workers);

    std::atomic<int> work_done{0};

    auto worker = [&]() {
        // Do some work
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        work_done.fetch_add(1);

        // Signal completion
        completion_latch.count_down();
    };

    // Start workers
    std::vector<std::thread> threads;
    for (int i = 0; i < num_workers; ++i) {
        threads.emplace_back(worker);
    }

    // Wait for all workers to complete
    completion_latch.wait();

    EXPECT_EQ(work_done.load(), num_workers);
    EXPECT_EQ(completion_latch.count(), 0);

    for (auto& t : threads) {
        t.join();
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(LatchTest, SingleCountLatch) {
    Memory mem(shm_name, 1024*1024);
    Latch latch(mem, "single", 1);

    EXPECT_EQ(latch.count(), 1);
    EXPECT_FALSE(latch.try_wait());

    latch.count_down();

    EXPECT_EQ(latch.count(), 0);
    EXPECT_TRUE(latch.try_wait());
}

TEST_F(LatchTest, LargeCount) {
    Memory mem(shm_name, 1024*1024);
    Latch latch(mem, "large", 1000000);

    latch.count_down(1000000);
    EXPECT_EQ(latch.count(), 0);
}

TEST_F(LatchTest, OneTimeUse) {
    Memory mem(shm_name, 1024*1024);
    Latch latch(mem, "onetime", 2);

    latch.count_down(2);
    EXPECT_EQ(latch.count(), 0);

    // Latch stays at 0 (cannot be reset)
    latch.count_down();
    EXPECT_EQ(latch.count(), 0);

    latch.wait();  // Should return immediately
    EXPECT_TRUE(latch.try_wait());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
