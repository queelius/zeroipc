#include <gtest/gtest.h>
#include <zeroipc/memory.h>
#include <zeroipc/queue.h>
#include <thread>
#include <vector>
#include <atomic>
#include "test_config.h"

using namespace zeroipc;
using namespace zeroipc::test;

class QueueTest : public SharedMemoryTestBase {
};

TEST_F(QueueTest, CreateAndBasicOps) {
    Memory mem(shm_name_, 1024*1024);
    Queue<int> queue(mem, "int_queue", 100);
    
    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.full());
    EXPECT_EQ(queue.size(), 0);
    // Requested 100, rounded up to the next power of two (wrap-safety)
    EXPECT_EQ(queue.capacity(), 128);
    
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
    Memory mem(shm_name_, 1024*1024);
    // Request 3, get 4 (power-of-two rounding for wrap-safety)
    Queue<int> queue(mem, "small_queue", 3);
    EXPECT_EQ(queue.capacity(), 4);

    // Vyukov-style queue uses all capacity slots (no wasted slot)
    EXPECT_TRUE(queue.push(1));
    EXPECT_TRUE(queue.push(2));
    EXPECT_TRUE(queue.push(3));
    EXPECT_TRUE(queue.push(4));   // All 4 slots usable
    EXPECT_FALSE(queue.push(5));  // Now full

    EXPECT_TRUE(queue.full());

    ASSERT_TRUE(queue.pop().has_value());
    EXPECT_FALSE(queue.full());
    EXPECT_TRUE(queue.push(5));
}

TEST_F(QueueTest, CapacityRoundsUpToPowerOfTwo) {
    Memory mem(shm_name_, 1024*1024);

    Queue<int> q1(mem, "q_p2_1", 1);
    EXPECT_EQ(q1.capacity(), 1);
    Queue<int> q2(mem, "q_p2_2", 2);
    EXPECT_EQ(q2.capacity(), 2);
    Queue<int> q5(mem, "q_p2_5", 5);
    EXPECT_EQ(q5.capacity(), 8);
    Queue<int> q1000(mem, "q_p2_1000", 1000);
    EXPECT_EQ(q1000.capacity(), 1024);
}

// Regression for the 2^32 counter wraparound. head/tail increase
// monotonically and wrap; with a power-of-two capacity the slot mapping
// counter % capacity is continuous across the wrap. Seed the counters just
// below UINT32_MAX (with matching per-slot sequence numbers, seq[pos % cap]
// = pos) and stream elements across the boundary in FIFO order.
TEST_F(QueueTest, WraparoundAt2To32) {
    Memory mem(shm_name_, 1024*1024);
    constexpr uint32_t CAP = 8;
    Queue<uint32_t> queue(mem, "wrap32_queue", CAP);
    ASSERT_EQ(queue.capacity(), CAP);

    size_t offset = 0, size = 0;
    ASSERT_TRUE(mem.find("wrap32_queue", offset, size));
    char* base = static_cast<char*>(mem.base()) + offset;
    auto* head = reinterpret_cast<std::atomic<uint32_t>*>(base);
    auto* tail = reinterpret_cast<std::atomic<uint32_t>*>(base + 4);
    auto* seq = reinterpret_cast<std::atomic<uint32_t>*>(
        base + 16 + align_up(sizeof(uint32_t) * CAP, 8));

    // Position both counters 4 increments before the wrap.
    const uint32_t T0 = 0xFFFFFFFCu;
    head->store(T0);
    tail->store(T0);
    for (uint32_t k = 0; k < CAP; ++k) {
        uint32_t pos = T0 + k;  // wraps through 0
        seq[pos % CAP].store(pos);
    }

    // Stream 3 full generations through the queue, crossing the wrap.
    uint32_t next_in = 0, next_out = 0;
    for (int round = 0; round < 3; ++round) {
        for (uint32_t i = 0; i < CAP; ++i) {
            ASSERT_TRUE(queue.push(next_in)) << "push " << next_in;
            ++next_in;
        }
        EXPECT_TRUE(queue.full());
        for (uint32_t i = 0; i < CAP; ++i) {
            auto v = queue.pop();
            ASSERT_TRUE(v.has_value()) << "pop " << next_out;
            EXPECT_EQ(*v, next_out) << "FIFO order broken at wrap";
            ++next_out;
        }
        EXPECT_TRUE(queue.empty());
    }
    // The counters have wrapped past zero.
    EXPECT_LT(tail->load(), T0);
}

TEST_F(QueueTest, CircularWrap) {
    Memory mem(shm_name_, 1024*1024);
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
    Memory mem(shm_name_, 1024*1024);

    {
        Queue<float> queue1(mem, "float_queue", 50);
        ASSERT_TRUE(queue1.push(3.14f));
        ASSERT_TRUE(queue1.push(2.71f));
    }
    
    Queue<float> queue2(mem, "float_queue");
    EXPECT_EQ(queue2.capacity(), 64);  // 50 rounded up at creation
    EXPECT_EQ(queue2.size(), 2);
    
    auto val = queue2.pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_FLOAT_EQ(*val, 3.14f);
}

TEST_F(QueueTest, ConcurrentProducerConsumer) {
    Memory mem(shm_name_, 10*1024*1024);
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
    Memory mem(shm_name_, 10*1024*1024);
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