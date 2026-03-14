#include <gtest/gtest.h>
#include <zeroipc/memory.h>
#include <zeroipc/signal.h>
#include <thread>
#include <vector>
#include <atomic>
#include <unistd.h>
#include "test_config.h"

using namespace zeroipc::test;

class SignalTest : public SharedMemoryTestBase {
};

TEST_F(SignalTest, BasicGetSet) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::Signal<int> sig(mem, "test_signal", 42);

    EXPECT_EQ(sig.get(), 42);

    sig.set(100);
    EXPECT_EQ(sig.get(), 100);
}

TEST_F(SignalTest, VersionIncrementsOnSet) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::Signal<int> sig(mem, "test_signal", 0);

    uint64_t v1 = sig.version();

    sig.set(1);
    uint64_t v2 = sig.version();

    EXPECT_GT(v2, v1);

    sig.set(2);
    uint64_t v3 = sig.version();

    EXPECT_GT(v3, v2);
}

TEST_F(SignalTest, OnChange) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::Signal<int> sig(mem, "test_signal", 0);

    int observed = -1;
    sig.on_change([&](int new_val) {
        observed = new_val;
    });

    sig.set(42);
    EXPECT_EQ(observed, 42);

    sig.set(100);
    EXPECT_EQ(observed, 100);
}

TEST_F(SignalTest, MultipleObservers) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::Signal<int> sig(mem, "test_signal", 0);

    int obs1 = 0, obs2 = 0;

    sig.on_change([&](int val) { obs1 = val; });
    sig.on_change([&](int val) { obs2 = val * 2; });

    sig.set(10);
    EXPECT_EQ(obs1, 10);
    EXPECT_EQ(obs2, 20);
}

TEST_F(SignalTest, CrossProcessReactivity) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::Signal<int> sig(mem, "test_signal", 0);

    pid_t pid = fork();
    ASSERT_NE(pid, -1);

    if (pid == 0) {
        // Child - observer
        zeroipc::Memory child_mem(shm_name_);
        zeroipc::Signal<int> child_sig(child_mem, "test_signal", zeroipc::Signal<int>::OpenExisting{});

        // Wait for signal to reach 42
        while (child_sig.get() != 42) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        exit(child_sig.get() == 42 ? 0 : 1);
    } else {
        // Parent - producer
        std::this_thread::sleep_for(TestTiming::THREAD_START_DELAY);

        sig.set(42);

        int status;
        waitpid(pid, &status, 0);
        EXPECT_EQ(WEXITSTATUS(status), 0);
    }
}

TEST_F(SignalTest, Poll) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::Signal<int> sig(mem, "test_signal", 10);

    uint64_t version = sig.version();

    // No change
    EXPECT_FALSE(sig.has_changed(version));

    // Change
    sig.set(20);
    EXPECT_TRUE(sig.has_changed(version));

    // Update version
    version = sig.version();
    EXPECT_FALSE(sig.has_changed(version));
}

TEST_F(SignalTest, WaitForChange) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::Signal<int> sig(mem, "test_signal", 0);

    std::thread changer([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        sig.set(42);
    });

    uint64_t old_version = sig.version();
    bool changed = sig.wait_for_change(old_version, std::chrono::milliseconds(100));

    EXPECT_TRUE(changed);
    EXPECT_EQ(sig.get(), 42);

    changer.join();
}

TEST_F(SignalTest, WaitForChangeTimeout) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::Signal<int> sig(mem, "test_signal", 0);

    uint64_t version = sig.version();
    bool changed = sig.wait_for_change(version, std::chrono::milliseconds(10));

    EXPECT_FALSE(changed);
}

TEST_F(SignalTest, OpenExisting) {
    {
        zeroipc::Memory mem(shm_name_, 1024 * 1024);
        zeroipc::Signal<int> sig(mem, "test_signal", 99);
    }

    // Reopen
    {
        zeroipc::Memory mem(shm_name_);
        zeroipc::Signal<int> sig(mem, "test_signal", zeroipc::Signal<int>::OpenExisting{});

        EXPECT_EQ(sig.get(), 99);
    }
}

TEST_F(SignalTest, AtomicUpdate) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::Signal<int> sig(mem, "test_signal", 0);

    std::vector<std::thread> threads;
    for (int i = 0; i < 8; i++) {
        threads.emplace_back([&]() {
            for (int j = 0; j < 100; j++) {
                sig.update([](int val) { return val + 1; });
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(sig.get(), 800);
}
