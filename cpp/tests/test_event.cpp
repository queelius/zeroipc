#include <gtest/gtest.h>
#include <zeroipc/memory.h>
#include <zeroipc/event.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <unistd.h>
#include "test_config.h"

using namespace zeroipc::test;

class EventTest : public ::testing::Test {
protected:
    void SetUp() override {
        shm_name_ = "/test_event_" + std::to_string(getpid());
    }

    void TearDown() override {
        shm_unlink(shm_name_.c_str());
    }

    std::string shm_name_;
};

TEST_F(EventTest, AutoResetBasic) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::Event event(mem, "test_event", zeroipc::EventMode::AutoReset);

    EXPECT_FALSE(event.is_signaled());

    // Signal the event
    event.signal();

    // Should be signaled
    EXPECT_TRUE(event.is_signaled());

    // Wait should succeed immediately and auto-reset
    event.wait();

    // Should be reset now
    EXPECT_FALSE(event.is_signaled());
}

TEST_F(EventTest, ManualResetBasic) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::Event event(mem, "test_event", zeroipc::EventMode::ManualReset);

    EXPECT_FALSE(event.is_signaled());

    // Signal the event
    event.signal();
    EXPECT_TRUE(event.is_signaled());

    // Multiple waits should succeed
    event.wait();
    EXPECT_TRUE(event.is_signaled());  // Still signaled

    event.wait();
    EXPECT_TRUE(event.is_signaled());  // Still signaled

    // Reset
    event.reset();
    EXPECT_FALSE(event.is_signaled());
}

TEST_F(EventTest, AutoResetSingleWaiter) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::Event event(mem, "test_event", zeroipc::EventMode::AutoReset);

    std::atomic<int> counter{0};

    std::thread waiter([&]() {
        event.wait();
        counter++;
    });

    std::this_thread::sleep_for(TestTiming::THREAD_START_DELAY);  // 1ms

    EXPECT_EQ(counter.load(), 0);  // Still waiting

    event.signal();

    waiter.join();

    EXPECT_EQ(counter.load(), 1);
    EXPECT_FALSE(event.is_signaled());  // Auto-reset
}

TEST_F(EventTest, ManualResetMultipleWaiters) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::Event event(mem, "test_event", zeroipc::EventMode::ManualReset);

    std::atomic<int> counter{0};

    std::vector<std::thread> waiters;
    for (int i = 0; i < 4; i++) {
        waiters.emplace_back([&]() {
            event.wait();
            counter++;
        });
    }

    std::this_thread::sleep_for(TestTiming::THREAD_START_DELAY);  // 1ms

    EXPECT_EQ(counter.load(), 0);  // All still waiting

    // Signal should wake all waiters
    event.signal();

    for (auto& t : waiters) {
        t.join();
    }

    EXPECT_EQ(counter.load(), 4);  // All woke up
    EXPECT_TRUE(event.is_signaled());  // Still signaled
}

TEST_F(EventTest, AutoResetOnlyWakesOne) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::Event event(mem, "test_event", zeroipc::EventMode::AutoReset);

    std::atomic<int> counter{0};

    std::vector<std::thread> waiters;
    for (int i = 0; i < 4; i++) {
        waiters.emplace_back([&]() {
            event.wait();
            counter++;
        });
    }

    std::this_thread::sleep_for(TestTiming::THREAD_START_DELAY);  // 1ms

    // Signal once - only one waiter should wake
    event.signal();
    std::this_thread::sleep_for(TestTiming::THREAD_SYNC_DELAY);  // 2ms

    EXPECT_EQ(counter.load(), 1);

    // Signal again - one more wakes
    event.signal();
    std::this_thread::sleep_for(TestTiming::THREAD_SYNC_DELAY);  // 2ms

    EXPECT_EQ(counter.load(), 2);

    // Signal remaining
    event.signal();
    event.signal();

    for (auto& t : waiters) {
        t.join();
    }

    EXPECT_EQ(counter.load(), 4);
}

TEST_F(EventTest, WaitFor) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::Event event(mem, "test_event", zeroipc::EventMode::AutoReset);

    // Should timeout
    auto start = std::chrono::steady_clock::now();
    bool result = event.wait_for(std::chrono::milliseconds(10));
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(result);
    EXPECT_GE(elapsed, std::chrono::milliseconds(10));

    // Now signal
    event.signal();
    result = event.wait_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(result);
}

TEST_F(EventTest, CrossProcessManualReset) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::Event event(mem, "test_event", zeroipc::EventMode::ManualReset);

    pid_t pid = fork();
    ASSERT_NE(pid, -1);

    if (pid == 0) {
        // Child process - waits for signal
        zeroipc::Memory child_mem(shm_name_);
        zeroipc::Event child_event(child_mem, "test_event");

        child_event.wait();

        EXPECT_TRUE(child_event.is_signaled());
        exit(0);
    } else {
        // Parent process - signals
        std::this_thread::sleep_for(TestTiming::THREAD_START_DELAY);  // 1ms

        event.signal();

        int status;
        waitpid(pid, &status, 0);
        EXPECT_EQ(WEXITSTATUS(status), 0);
    }
}

TEST_F(EventTest, OpenExisting) {
    {
        zeroipc::Memory mem(shm_name_, 1024 * 1024);
        zeroipc::Event event(mem, "test_event", zeroipc::EventMode::ManualReset);
        event.signal();
    }

    // Reopen
    {
        zeroipc::Memory mem(shm_name_);
        zeroipc::Event event(mem, "test_event");

        EXPECT_TRUE(event.is_signaled());
    }
}

TEST_F(EventTest, Pulse) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::Event event(mem, "test_event", zeroipc::EventMode::ManualReset);

    std::atomic<int> counter{0};

    std::vector<std::thread> waiters;
    for (int i = 0; i < 4; i++) {
        waiters.emplace_back([&]() {
            event.wait();
            counter++;
        });
    }

    std::this_thread::sleep_for(TestTiming::THREAD_START_DELAY);  // 1ms

    // Pulse: signal + reset atomically
    event.pulse();

    for (auto& t : waiters) {
        t.join();
    }

    // All waiters woke up
    EXPECT_EQ(counter.load(), 4);
    // But event is reset
    EXPECT_FALSE(event.is_signaled());
}
