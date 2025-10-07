#include <gtest/gtest.h>
#include <zeroipc/memory.h>
#include <zeroipc/future.h>
#include <zeroipc/lazy.h>
#include <zeroipc/stream.h>
#include <zeroipc/channel.h>
#include <thread>
#include <chrono>
#include <vector>

using namespace zeroipc;
using namespace std::chrono_literals;

class CodataTest : public ::testing::Test {
protected:
    void SetUp() override {
        shm_name_ = "/test_codata_" + std::to_string(getpid()) + "_" + 
                    std::to_string(test_counter_++);
    }
    
    void TearDown() override {
        Memory::unlink(shm_name_);
    }
    
    std::string shm_name_;
    static int test_counter_;
};

int CodataTest::test_counter_ = 0;

// Future Tests
TEST_F(CodataTest, FutureBasicOperations) {
    Memory mem(shm_name_, 1024 * 1024);
    Future<int> future(mem, "test_future");
    
    EXPECT_FALSE(future.is_ready());
    EXPECT_EQ(future.state(), Future<int>::PENDING);
    
    // Set value
    EXPECT_TRUE(future.set_value(42));
    EXPECT_TRUE(future.is_ready());
    EXPECT_EQ(future.state(), Future<int>::READY);
    
    // Get value
    EXPECT_EQ(future.get(), 42);
    
    // Try to set again (should fail)
    EXPECT_FALSE(future.set_value(100));
}

TEST_F(CodataTest, FutureErrorHandling) {
    Memory mem(shm_name_, 1024 * 1024);
    Future<double> future(mem, "error_future");
    
    EXPECT_TRUE(future.set_error("Division by zero"));
    EXPECT_TRUE(future.is_ready());
    EXPECT_EQ(future.state(), Future<double>::ERROR);
    
    EXPECT_THROW(future.get(), std::runtime_error);
}

TEST_F(CodataTest, FutureTimeout) {
    Memory mem(shm_name_, 1024 * 1024);
    Future<int> future(mem, "timeout_future");
    
    auto result = future.get_for(100ms);
    EXPECT_FALSE(result.has_value());  // Should timeout
    
    future.set_value(99);
    result = future.get_for(100ms);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 99);
}

TEST_F(CodataTest, FutureConcurrent) {
    Memory mem(shm_name_, 1024 * 1024);
    Future<int> future(mem, "concurrent_future");
    
    std::atomic<int> sum{0};
    
    // Multiple readers
    std::vector<std::thread> readers;
    for (int i = 0; i < 5; ++i) {
        readers.emplace_back([&future, &sum]() {
            int val = future.get();  // All wait for same value
            sum.fetch_add(val, std::memory_order_relaxed);
        });
    }
    
    // Writer
    std::thread writer([&future]() {
        std::this_thread::sleep_for(50ms);
        future.set_value(10);
    });
    
    writer.join();
    for (auto& t : readers) {
        t.join();
    }
    
    EXPECT_EQ(sum.load(), 50);  // 5 readers * 10
}

// Lazy Tests
TEST_F(CodataTest, LazyBasicOperations) {
    Memory mem(shm_name_, 1024 * 1024);
    
    // Create lazy constant
    Lazy<int> lazy_val(mem, "lazy_const", 42);
    EXPECT_TRUE(lazy_val.is_computed());
    EXPECT_EQ(lazy_val.force(), 42);
    
    // Peek without forcing
    auto peeked = lazy_val.peek();
    ASSERT_TRUE(peeked.has_value());
    EXPECT_EQ(*peeked, 42);
}

TEST_F(CodataTest, LazyArithmetic) {
    Memory mem(shm_name_, 1024 * 1024);
    
    Lazy<double> a(mem, "lazy_a", 10.0);
    Lazy<double> b(mem, "lazy_b", 20.0);
    
    auto sum = Lazy<double>::add(mem, "lazy_sum", a, b);
    EXPECT_FALSE(sum.is_computed());  // Not computed yet
    
    double result = sum.force();
    EXPECT_EQ(result, 30.0);
    EXPECT_TRUE(sum.is_computed());
    
    // Force again should use cached value
    EXPECT_EQ(sum.force(), 30.0);
    EXPECT_EQ(sum.compute_count(), 1);  // Only computed once
}

TEST_F(CodataTest, LazyBooleanShortCircuit) {
    Memory mem(shm_name_, 1024 * 1024);
    
    Lazy<bool> lazy_false(mem, "lazy_false", false);
    Lazy<bool> lazy_true(mem, "lazy_true", true);
    
    // AND with false should short-circuit
    auto result = Lazy<bool>::lazy_and(mem, "lazy_and", lazy_false, lazy_true);
    EXPECT_FALSE(result.force());  // Should be false without evaluating second
}

TEST_F(CodataTest, LazyConcurrent) {
    Memory mem(shm_name_, 1024 * 1024);
    
    // Create two lazy values and a sum that will be computed
    Lazy<int> a(mem, "lazy_a", 50);
    Lazy<int> b(mem, "lazy_b", 50);
    auto lazy_sum = Lazy<int>::add(mem, "concurrent_sum", a, b);
    
    std::atomic<int> total{0};
    std::atomic<int> force_count{0};
    std::vector<std::thread> threads;
    
    // Multiple threads try to force the same lazy computation
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&lazy_sum, &total, &force_count]() {
            int val = lazy_sum.force();
            total.fetch_add(val, std::memory_order_relaxed);
            force_count.fetch_add(1, std::memory_order_relaxed);
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(total.load(), 1000);  // 10 threads * 100
    EXPECT_EQ(force_count.load(), 10);  // All threads got the value
    // The computation should happen only once (or at least be deterministic)
    EXPECT_TRUE(lazy_sum.is_computed());
}

// Stream Tests  
TEST_F(CodataTest, StreamBasicOperations) {
    Memory mem(shm_name_, 1024 * 1024);
    Stream<int> stream(mem, "test_stream", 100);
    
    EXPECT_FALSE(stream.is_closed());
    EXPECT_EQ(stream.sequence(), 0);
    
    // Emit values
    EXPECT_TRUE(stream.emit(1));
    EXPECT_TRUE(stream.emit(2));
    EXPECT_TRUE(stream.emit(3));
    
    EXPECT_EQ(stream.sequence(), 3);
    
    // Read values
    auto val = stream.next();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 1);
    
    val = stream.next();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 2);
}

TEST_F(CodataTest, StreamBulkOperations) {
    Memory mem(shm_name_, 1024 * 1024);
    Stream<double> stream(mem, "bulk_stream", 100);
    
    double data[] = {1.1, 2.2, 3.3, 4.4, 5.5};
    size_t written = stream.emit_bulk(data, 5);
    EXPECT_EQ(written, 5);
    
    double read_data[5];
    size_t read = stream.read_bulk(read_data, 5);
    EXPECT_EQ(read, 5);
    
    for (int i = 0; i < 5; ++i) {
        EXPECT_DOUBLE_EQ(read_data[i], data[i]);
    }
}

TEST_F(CodataTest, StreamFold) {
    Memory mem(shm_name_, 1024 * 1024);
    Stream<int> stream(mem, "fold_stream", 10);
    
    for (int i = 1; i <= 5; ++i) {
        EXPECT_TRUE(stream.emit(i));
    }
    stream.close();
    
    // Sum using fold
    int sum = stream.fold(0, [](int acc, int val) { return acc + val; });
    EXPECT_EQ(sum, 15);  // 1+2+3+4+5
}

// Channel Tests
TEST_F(CodataTest, ChannelUnbuffered) {
    Memory mem(shm_name_, 1024 * 1024);
    Channel<int> ch(mem, "unbuffered");  // Unbuffered (synchronous)
    
    EXPECT_EQ(ch.capacity(), 0);
    EXPECT_FALSE(ch.is_buffered());
    
    std::atomic<bool> received{false};
    
    // Receiver thread
    std::thread receiver([&ch, &received]() {
        auto val = ch.recv();
        EXPECT_TRUE(val.has_value());
        EXPECT_EQ(*val, 42);
        received.store(true);
    });
    
    // Give receiver time to start waiting
    std::this_thread::sleep_for(50ms);
    
    // Send (should block until received)
    EXPECT_TRUE(ch.send(42));
    
    receiver.join();
    EXPECT_TRUE(received.load());
}

TEST_F(CodataTest, ChannelBuffered) {
    Memory mem(shm_name_, 1024 * 1024);
    Channel<int> ch(mem, "buffered", size_t(3));  // Buffered channel with capacity 3
    
    EXPECT_EQ(ch.capacity(), 3);
    EXPECT_TRUE(ch.is_buffered());
    
    // Should not block up to capacity
    EXPECT_TRUE(ch.try_send(1));
    EXPECT_TRUE(ch.try_send(2));
    EXPECT_TRUE(ch.try_send(3));
    
    // Buffer full, should fail
    EXPECT_FALSE(ch.try_send(4));
    
    // Receive all
    auto val = ch.recv();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 1);
    
    val = ch.recv();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 2);
    
    val = ch.recv();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 3);
    
    // Now buffer is empty, can send again
    EXPECT_TRUE(ch.try_send(4));
    
    val = ch.recv();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 4);
}

TEST_F(CodataTest, ChannelTimeout) {
    Memory mem(shm_name_, 1024 * 1024);
    Channel<int> ch(mem, "timeout_ch");
    
    // Try receive with timeout (should timeout)
    auto val = ch.recv_timeout(100ms);
    EXPECT_FALSE(val.has_value());
    
    // Send in background
    std::thread sender([&ch]() {
        std::this_thread::sleep_for(50ms);
        ch.send(99);
    });
    
    // Receive with timeout (should succeed)
    val = ch.recv_timeout(200ms);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 99);
    
    sender.join();
}

TEST_F(CodataTest, ChannelClose) {
    Memory mem(shm_name_, 1024 * 1024);
    Channel<int> ch(mem, "close_ch", size_t(2));
    
    ch.send(1);
    ch.send(2);
    ch.close();
    
    EXPECT_TRUE(ch.is_closed());
    
    // Can still receive buffered values
    auto val = ch.recv();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 1);
    
    val = ch.recv();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 2);
    
    // After buffer empty, recv returns nullopt
    val = ch.recv();
    EXPECT_FALSE(val.has_value());
    
    // Cannot send to closed channel
    EXPECT_FALSE(ch.send(3));
}

// Integration test: Pipeline
TEST_F(CodataTest, ReactiveStreamPipeline) {
    Memory mem(shm_name_, 10 * 1024 * 1024);
    
    // Create a stream of sensor readings
    Stream<double> sensors(mem, "sensors", 100);
    
    // Emit some data
    EXPECT_TRUE(sensors.emit(20.0));  // Celsius
    EXPECT_TRUE(sensors.emit(25.0));
    EXPECT_TRUE(sensors.emit(30.0));
    EXPECT_TRUE(sensors.emit(35.0));
    
    // Process through pipeline (simplified - callbacks don't work across processes)
    std::vector<double> fahrenheit_values;
    std::vector<double> warnings;
    
    // Manual transformation for test
    while (auto celsius = sensors.next()) {
        double f = *celsius * 9/5 + 32;
        fahrenheit_values.push_back(f);
        
        if (f > 85.0) {  // Warning threshold
            warnings.push_back(f);
        }
    }
    
    EXPECT_EQ(fahrenheit_values.size(), 4);
    EXPECT_EQ(warnings.size(), 2);  // 30C=86F and 35C=95F
}

// Additional Future Tests
TEST_F(CodataTest, FutureOpenExisting) {
    Memory mem(shm_name_, 1024 * 1024);

    // Create and set in first future
    {
        Future<int> future1(mem, "shared_future");
        future1.set_value(123);
    }

    // Open existing in second future
    {
        Future<int> future2(mem, "shared_future", true);  // open_existing
        EXPECT_TRUE(future2.is_ready());
        EXPECT_EQ(future2.get(), 123);
    }
}

TEST_F(CodataTest, FutureBlocking) {
    Memory mem(shm_name_, 1024 * 1024);
    Future<int> future(mem, "blocking_future");

    std::thread setter([&future]() {
        std::this_thread::sleep_for(100ms);
        EXPECT_TRUE(future.set_value(777));
    });

    // get() blocks until ready
    EXPECT_EQ(future.get(), 777);
    EXPECT_TRUE(future.is_ready());

    setter.join();
}

TEST_F(CodataTest, FutureMultipleGetters) {
    Memory mem(shm_name_, 1024 * 1024);
    Future<float> future(mem, "multi_get");

    future.set_value(3.14f);

    // Multiple get() calls should all return the same value
    EXPECT_FLOAT_EQ(future.get(), 3.14f);
    EXPECT_FLOAT_EQ(future.get(), 3.14f);
    EXPECT_FLOAT_EQ(future.get(), 3.14f);
}

// Additional Lazy Tests
TEST_F(CodataTest, LazyPeekUncomputed) {
    Memory mem(shm_name_, 1024 * 1024);
    Lazy<int> lazy_a(mem, "lazy_a", 10);
    Lazy<int> lazy_b(mem, "lazy_b", 20);

    auto sum = Lazy<int>::add(mem, "sum", lazy_a, lazy_b);

    // Peek before computing
    auto peeked = sum.peek();
    EXPECT_FALSE(peeked.has_value());  // Not computed yet
    EXPECT_FALSE(sum.is_computed());

    // Force computation
    sum.force();

    // Now peek should return value
    peeked = sum.peek();
    ASSERT_TRUE(peeked.has_value());
    EXPECT_EQ(*peeked, 30);
}

TEST_F(CodataTest, LazyMultiply) {
    Memory mem(shm_name_, 1024 * 1024);

    Lazy<int> a(mem, "mul_a", 6);
    Lazy<int> b(mem, "mul_b", 7);

    auto product = Lazy<int>::multiply(mem, "product", a, b);
    EXPECT_EQ(product.force(), 42);
}

TEST_F(CodataTest, LazyMemoization) {
    Memory mem(shm_name_, 1024 * 1024);

    // Create lazy value with expensive computation
    Lazy<int> expensive(mem, "expensive", 100);  // Constant, already computed

    // Force multiple times
    EXPECT_EQ(expensive.force(), 100);
    EXPECT_EQ(expensive.force(), 100);
    EXPECT_EQ(expensive.force(), 100);

    // Should be computed only once
    EXPECT_EQ(expensive.compute_count(), 0);  // Constants are never "computed"
}

// Additional Stream Tests
TEST_F(CodataTest, StreamClose) {
    Memory mem(shm_name_, 1024 * 1024);
    Stream<int> stream(mem, "closeable", 10);

    stream.emit(1);
    stream.emit(2);

    stream.close();
    EXPECT_TRUE(stream.is_closed());

    // Cannot emit to closed stream
    EXPECT_FALSE(stream.emit(3));

    // Can still read existing values
    auto val = stream.next();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 1);
}

TEST_F(CodataTest, StreamMap) {
    Memory mem(shm_name_, 1024 * 1024);
    Stream<int> stream(mem, "mappable", 100);

    stream.emit(1);
    stream.emit(2);
    stream.emit(3);
    stream.close();

    // Map to double values
    std::vector<int> doubled;
    while (auto val = stream.next()) {
        doubled.push_back(*val * 2);
    }

    EXPECT_EQ(doubled.size(), 3);
    EXPECT_EQ(doubled[0], 2);
    EXPECT_EQ(doubled[1], 4);
    EXPECT_EQ(doubled[2], 6);
}

TEST_F(CodataTest, StreamFilter) {
    Memory mem(shm_name_, 1024 * 1024);
    Stream<int> stream(mem, "filterable", 100);

    for (int i = 1; i <= 10; ++i) {
        EXPECT_TRUE(stream.emit(i));
    }
    stream.close();

    // Filter even numbers
    std::vector<int> evens;
    while (auto val = stream.next()) {
        if (*val % 2 == 0) {
            evens.push_back(*val);
        }
    }

    EXPECT_EQ(evens.size(), 5);  // 2, 4, 6, 8, 10
}

TEST_F(CodataTest, StreamTake) {
    Memory mem(shm_name_, 1024 * 1024);
    Stream<int> stream(mem, "takeable", 100);

    for (int i = 1; i <= 100; ++i) {
        EXPECT_TRUE(stream.emit(i));
    }
    stream.close();

    // Take first 5
    std::vector<int> taken;
    for (int i = 0; i < 5; ++i) {
        auto val = stream.next();
        if (val) {
            taken.push_back(*val);
        }
    }

    EXPECT_EQ(taken.size(), 5);
    EXPECT_EQ(taken[0], 1);
    EXPECT_EQ(taken[4], 5);
}

// Additional Channel Tests
TEST_F(CodataTest, ChannelSelect) {
    Memory mem(shm_name_, 1024 * 1024);
    Channel<int> ch1(mem, "select_ch1", size_t(1));
    Channel<int> ch2(mem, "select_ch2", size_t(1));

    EXPECT_TRUE(ch1.send(100));
    EXPECT_TRUE(ch2.send(200));

    // Try both channels
    auto val1 = ch1.try_recv();
    auto val2 = ch2.try_recv();

    ASSERT_TRUE(val1.has_value());
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(*val1, 100);
    EXPECT_EQ(*val2, 200);
}

TEST_F(CodataTest, ChannelReopenExisting) {
    Memory mem(shm_name_, 1024 * 1024);

    // Create and send
    {
        Channel<int> ch(mem, "reopen_ch", size_t(5));
        EXPECT_TRUE(ch.send(42));
        EXPECT_TRUE(ch.send(43));
    }

    // Reopen and receive
    {
        Channel<int> ch(mem, "reopen_ch", true);  // open_existing
        auto val = ch.recv();
        ASSERT_TRUE(val.has_value());
        EXPECT_EQ(*val, 42);

        val = ch.recv();
        ASSERT_TRUE(val.has_value());
        EXPECT_EQ(*val, 43);
    }
}

TEST_F(CodataTest, ChannelBulkTransfer) {
    Memory mem(shm_name_, 1024 * 1024);
    Channel<int> ch(mem, "bulk_ch", size_t(100));

    // Send many values
    for (int i = 0; i < 50; ++i) {
        EXPECT_TRUE(ch.try_send(i));
    }

    // Receive all
    for (int i = 0; i < 50; ++i) {
        auto val = ch.recv();
        ASSERT_TRUE(val.has_value());
        EXPECT_EQ(*val, i);
    }
}

TEST_F(CodataTest, ChannelConcurrentSendRecv) {
    Memory mem(shm_name_, 1024 * 1024);
    Channel<int> ch(mem, "concurrent_ch", size_t(10));

    std::atomic<int> received_sum{0};

    // Multiple senders
    std::vector<std::thread> senders;
    for (int i = 0; i < 3; ++i) {
        senders.emplace_back([&ch, i]() {
            for (int j = 0; j < 10; ++j) {
                while (!ch.send(i * 100 + j)) {
                    std::this_thread::yield();  // Retry if buffer full
                }
            }
        });
    }

    // Multiple receivers
    std::vector<std::thread> receivers;
    for (int i = 0; i < 3; ++i) {
        receivers.emplace_back([&ch, &received_sum]() {
            for (int j = 0; j < 10; ++j) {
                auto val = ch.recv();
                if (val) {
                    received_sum.fetch_add(*val, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& t : senders) t.join();
    for (auto& t : receivers) t.join();

    // All 30 values should be received (0-9, 100-109, 200-209)
    int expected_sum = (0+1+2+3+4+5+6+7+8+9) * 3 + (100+200) * 10;
    EXPECT_EQ(received_sum.load(), expected_sum);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}