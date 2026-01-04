#include <gtest/gtest.h>
#include <zeroipc/memory.h>
#include <zeroipc/monitor.h>
#include <zeroipc/array.h>
#include <thread>
#include <vector>
#include <atomic>
#include <unistd.h>
#include "test_config.h"

using namespace zeroipc::test;

class MonitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        shm_name_ = "/test_monitor_" + std::to_string(getpid());
    }

    void TearDown() override {
        shm_unlink(shm_name_.c_str());
    }

    std::string shm_name_;
};

TEST_F(MonitorTest, BasicNotify) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::Monitor mon(mem, "test_mon");
    zeroipc::Array<int> ready(mem, "ready", 1);
    ready[0] = 0;

    std::thread waiter([&]() {
        mon.lock();
        while (ready[0] == 0) {
            mon.wait();
        }
        mon.unlock();
    });

    std::this_thread::sleep_for(TestTiming::THREAD_START_DELAY);

    mon.lock();
    ready[0] = 1;
    mon.notify_one();
    mon.unlock();

    waiter.join();
    EXPECT_EQ(ready[0], 1);
}

TEST_F(MonitorTest, NotifyAll) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::Monitor mon(mem, "test_mon");
    zeroipc::Array<int> counter(mem, "counter", 1);
    counter[0] = 0;

    std::atomic<int> woken{0};

    std::vector<std::thread> waiters;
    for (int i = 0; i < 4; i++) {
        waiters.emplace_back([&]() {
            mon.lock();
            while (counter[0] < 10) {
                mon.wait();
            }
            woken++;
            mon.unlock();
        });
    }

    std::this_thread::sleep_for(TestTiming::THREAD_START_DELAY);

    mon.lock();
    counter[0] = 10;
    mon.notify_all();
    mon.unlock();

    for (auto& t : waiters) {
        t.join();
    }

    EXPECT_EQ(woken.load(), 4);
}

TEST_F(MonitorTest, PredicateWait) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::Monitor mon(mem, "test_mon");
    zeroipc::Array<int> value(mem, "value", 1);
    value[0] = 0;

    std::thread waiter([&]() {
        mon.lock();
        mon.wait([&]() { return value[0] >= 5; });
        mon.unlock();
    });

    std::this_thread::sleep_for(TestTiming::THREAD_START_DELAY);

    // Increment and notify multiple times
    for (int i = 1; i <= 5; i++) {
        mon.lock();
        value[0] = i;
        mon.notify_one();
        mon.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    waiter.join();
    EXPECT_GE(value[0], 5);
}

TEST_F(MonitorTest, ProducerConsumer) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::Monitor mon(mem, "test_mon");
    zeroipc::Array<int> buffer(mem, "buffer", 10);
    zeroipc::Array<int> count(mem, "count", 1);
    count[0] = 0;

    std::atomic<int> consumed{0};
    std::atomic<int> produced{0};
    const int NUM_ITEMS = 5;

    // Consumer
    std::thread consumer([&]() {
        for (int i = 0; i < NUM_ITEMS; i++) {
            mon.lock();
            mon.wait([&]() { return count[0] > 0; });

            int idx = --count[0];
            int value = buffer[idx];
            consumed += value;

            mon.notify_one();
            mon.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // Producer
    std::thread producer([&]() {
        for (int i = 0; i < NUM_ITEMS; i++) {
            mon.lock();
            mon.wait([&]() { return count[0] < 3; });

            int idx = count[0]++;
            buffer[idx] = i + 1;
            produced++;

            mon.notify_one();
            mon.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    producer.join();
    consumer.join();

    // Sum of 1+2+...+5 = 15
    EXPECT_EQ(consumed.load(), 15);
    EXPECT_EQ(produced.load(), 5);
}

TEST_F(MonitorTest, WaitFor) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::Monitor mon(mem, "test_mon");
    zeroipc::Array<int> ready(mem, "ready", 1);
    ready[0] = 0;

    // Should timeout
    mon.lock();
    bool result = mon.wait_for(std::chrono::milliseconds(10), [&]() {
        return ready[0] == 1;
    });
    mon.unlock();

    EXPECT_FALSE(result);

    // Now signal
    std::thread signaler([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        mon.lock();
        ready[0] = 1;
        mon.notify_one();
        mon.unlock();
    });

    mon.lock();
    result = mon.wait_for(std::chrono::milliseconds(100), [&]() {
        return ready[0] == 1;
    });
    mon.unlock();

    signaler.join();
    EXPECT_TRUE(result);
}

TEST_F(MonitorTest, CrossProcess) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::Monitor mon(mem, "test_mon");
    zeroipc::Array<int> data(mem, "data", 1);
    data[0] = 0;

    pid_t pid = fork();
    ASSERT_NE(pid, -1);

    if (pid == 0) {
        // Child - consumer
        zeroipc::Memory child_mem(shm_name_);
        zeroipc::Monitor child_mon(child_mem, "test_mon");
        zeroipc::Array<int> child_data(child_mem, "data");

        child_mon.lock();
        child_mon.wait([&]() { return child_data[0] == 42; });
        child_mon.unlock();

        exit(child_data[0] == 42 ? 0 : 1);
    } else {
        // Parent - producer
        std::this_thread::sleep_for(TestTiming::THREAD_START_DELAY);

        mon.lock();
        data[0] = 42;
        mon.notify_all();
        mon.unlock();

        int status;
        waitpid(pid, &status, 0);
        EXPECT_EQ(WEXITSTATUS(status), 0);
    }
}

TEST_F(MonitorTest, SpuriousWakeupHandling) {
    zeroipc::Memory mem(shm_name_, 1024 * 1024);
    zeroipc::Monitor mon(mem, "test_mon");
    zeroipc::Array<int> value(mem, "value", 1);
    value[0] = 0;

    std::atomic<int> checks{0};

    std::thread waiter([&]() {
        mon.lock();
        mon.wait([&]() {
            checks++;
            return value[0] == 10;
        });
        mon.unlock();
    });

    std::this_thread::sleep_for(TestTiming::THREAD_START_DELAY);

    // Send multiple notifications before condition is true
    for (int i = 1; i <= 10; i++) {
        mon.lock();
        value[0] = i;
        mon.notify_one();
        mon.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    waiter.join();

    // Should have checked multiple times (handling spurious wakeups)
    EXPECT_GE(checks.load(), 10);
}
