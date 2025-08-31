#include <gtest/gtest.h>
#include <zeroipc/memory.h>
#include <zeroipc/queue.h>
#include <thread>
#include <vector>
#include <atomic>

using namespace zeroipc;

class QueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clean up any leftover shared memory
        Memory::unlink("/test_queue");
    }
    
    void TearDown() override {
        Memory::unlink("/test_queue");
    }
};

TEST_F(QueueTest, CreateAndBasicOps) {
    Memory mem("/test_queue", 1024*1024);
    Queue<int> queue(mem, "int_queue", 100);
    
    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.full());
    EXPECT_EQ(queue.size(), 0);
    EXPECT_EQ(queue.capacity(), 100);
    
    // Push some values
    EXPECT_TRUE(queue.push(10));
    EXPECT_TRUE(queue.push(20));
    EXPECT_TRUE(queue.push(30));
    
    EXPECT_FALSE(queue.empty());
    EXPECT_EQ(queue.size(), 3);
    
    // Pop values
    auto val = queue.pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 10);
    
    val = queue.pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 20);
    
    val = queue.pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 30);
    
    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.pop().has_value());
}

TEST_F(QueueTest, FullQueue) {
    Memory mem("/test_queue", 1024*1024);
    Queue<int> queue(mem, "small_queue", 3);
    
    EXPECT_TRUE(queue.push(1));
    EXPECT_TRUE(queue.push(2));
    EXPECT_FALSE(queue.push(3)); // Should fail - circular buffer full
    
    EXPECT_TRUE(queue.full());
    
    queue.pop();
    EXPECT_FALSE(queue.full());
    EXPECT_TRUE(queue.push(3));
}

TEST_F(QueueTest, CircularWrap) {
    Memory mem("/test_queue", 1024*1024);
    Queue<int> queue(mem, "wrap_queue", 5);
    
    // Fill queue
    for (int i = 0; i < 4; i++) {
        EXPECT_TRUE(queue.push(i));
    }
    
    // Pop some
    EXPECT_EQ(*queue.pop(), 0);
    EXPECT_EQ(*queue.pop(), 1);
    
    // Push more (wrapping around)
    EXPECT_TRUE(queue.push(4));
    EXPECT_TRUE(queue.push(5));
    
    // Verify order
    EXPECT_EQ(*queue.pop(), 2);
    EXPECT_EQ(*queue.pop(), 3);
    EXPECT_EQ(*queue.pop(), 4);
    EXPECT_EQ(*queue.pop(), 5);
    EXPECT_TRUE(queue.empty());
}

TEST_F(QueueTest, OpenExisting) {
    Memory mem("/test_queue", 1024*1024);
    
    {
        Queue<float> queue1(mem, "float_queue", 50);
        queue1.push(3.14f);
        queue1.push(2.71f);
    }
    
    Queue<float> queue2(mem, "float_queue");
    EXPECT_EQ(queue2.capacity(), 50);
    EXPECT_EQ(queue2.size(), 2);
    
    auto val = queue2.pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_FLOAT_EQ(*val, 3.14f);
}

TEST_F(QueueTest, ConcurrentProducerConsumer) {
    Memory mem("/test_queue", 10*1024*1024);
    Queue<int> queue(mem, "concurrent_queue", 1000);
    
    const int num_items = 10000;
    std::atomic<int> sum_produced{0};
    std::atomic<int> sum_consumed{0};
    
    // Producer thread
    std::thread producer([&]() {
        for (int i = 0; i < num_items; i++) {
            while (!queue.push(i)) {
                std::this_thread::yield();
            }
            sum_produced += i;
        }
    });
    
    // Consumer thread
    std::thread consumer([&]() {
        int count = 0;
        while (count < num_items) {
            auto val = queue.pop();
            if (val) {
                sum_consumed += *val;
                count++;
            } else {
                std::this_thread::yield();
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    EXPECT_EQ(sum_produced.load(), sum_consumed.load());
    EXPECT_TRUE(queue.empty());
}

TEST_F(QueueTest, MultipleProducersConsumers) {
    Memory mem("/test_queue", 10*1024*1024);
    Queue<int> queue(mem, "mpmc_queue", 1000);
    
    const int num_producers = 4;
    const int num_consumers = 4;
    const int items_per_producer = 1000;
    
    std::atomic<int> total_produced{0};
    std::atomic<int> total_consumed{0};
    
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    
    // Start producers
    for (int p = 0; p < num_producers; p++) {
        producers.emplace_back([&, p]() {
            for (int i = 0; i < items_per_producer; i++) {
                int value = p * items_per_producer + i;
                while (!queue.push(value)) {
                    std::this_thread::yield();
                }
                total_produced++;
            }
        });
    }
    
    // Start consumers
    for (int c = 0; c < num_consumers; c++) {
        consumers.emplace_back([&]() {
            while (total_consumed < num_producers * items_per_producer) {
                auto val = queue.pop();
                if (val) {
                    total_consumed++;
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    // Wait for completion
    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();
    
    EXPECT_EQ(total_produced.load(), num_producers * items_per_producer);
    EXPECT_EQ(total_consumed.load(), num_producers * items_per_producer);
    EXPECT_TRUE(queue.empty());
}